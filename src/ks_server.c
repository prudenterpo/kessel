#include "ks_server.h"
#include "ks_log.h"
#include "ks_kv.h"
#include "ks_net.h"
#include "ks_protocol.h"

#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <errno.h>
#include <sys/select.h>
#endif

#define KS_MAX_CLIENTS 1024
#define KS_INPUT_CAP (KS_MAX_REQUEST + 2u)
#define KS_OUTPUT_CAP (2u * (KS_MAX_REQUEST + 64u))
#define KS_OUTPUT_HIGH_WATER (KS_OUTPUT_CAP / 2u)
#define KS_ACCEPT_BUDGET 64

typedef struct {
    int used;
    int fd;
    int close_after_write;
    char* input;
    size_t input_len;
    char* output;
    size_t output_offset;
    size_t output_len;
} ks_client_t;

static volatile sig_atomic_t ks_stop_requested = 0;

static void handle_stop_signal(int signal_number) {
    (void)signal_number;
    ks_stop_requested = 1;
}

static void client_reset(ks_client_t* client) {
    if (client->used) {
        (void)ks_close(client->fd);
    }
    free(client->input);
    free(client->output);
    *client = (ks_client_t){0};
}

static int client_init(ks_client_t* client, int fd) {
    char* input = malloc(KS_INPUT_CAP + 1u);
    char* output = malloc(KS_OUTPUT_CAP);
    if (input == NULL || output == NULL) {
        free(input);
        free(output);
        (void)ks_close(fd);
        return -1;
    }
    *client = (ks_client_t){.used = 1, .fd = fd, .input = input, .output = output};
    return 0;
}

static int client_queue(ks_client_t* client, const char* data, size_t length) {
    if (client->output_offset > 0) {
        size_t pending = client->output_len - client->output_offset;
        memmove(client->output, client->output + client->output_offset, pending);
        client->output_offset = 0;
        client->output_len = pending;
    }
    if (length > KS_OUTPUT_CAP - client->output_len) {
        return -1;
    }
    memcpy(client->output + client->output_len, data, length);
    client->output_len += length;
    return 0;
}

static int queue_error(ks_client_t* client, const char* message) {
    char response[256];
    size_t length = ks_fmt_error(response, sizeof(response), message);
    return length == 0 ? -1 : client_queue(client, response, length);
}

static int handle_command(ks_client_t* client, ks_kv_t* store, char* line) {
    ks_cmd_t command;
    char response[KS_MAX_REQUEST + 64u];
    size_t response_len = 0;

    if (!ks_parse_line(line, &command) || command.cmd == NULL) {
        return queue_error(client, "invalid command");
    }

    if (strcmp(command.cmd, "PING") == 0) {
        if (command.argc != 0) {
            return queue_error(client, "wrong number of arguments to 'PING'");
        }
        response_len = ks_fmt_simple(response, sizeof(response), "PONG");
    } else if (strcmp(command.cmd, "ECHO") == 0) {
        if (command.argc != 1) {
            return queue_error(client, "wrong number of arguments to 'ECHO'");
        }
        response_len = ks_fmt_bulk(response, sizeof(response), command.argv[0],
                                   strlen(command.argv[0]));
    } else if (strcmp(command.cmd, "HELP") == 0) {
        if (command.argc != 0) {
            return queue_error(client, "wrong number of arguments to 'HELP'");
        }
        const char* help =
            "Kessel commands:\n"
            " PING\n"
            " ECHO <string>\n"
            " SET <key> <value>\n"
            " GET <key>\n"
            " DEL <key>\n"
            " EXISTS <key>\n"
            " HELP\n";
        response_len = ks_fmt_bulk(response, sizeof(response), help, strlen(help));
    } else if (strcmp(command.cmd, "SET") == 0) {
        if (command.argc != 2) {
            return queue_error(client, "wrong number of arguments to 'SET'");
        }
        if (ks_kv_set(store, command.argv[0], command.argv[1],
                      strlen(command.argv[1])) == KS_KV_SET_ERROR) {
            return queue_error(client, "unable to store value");
        }
        response_len = ks_fmt_simple(response, sizeof(response), "OK");
    } else if (strcmp(command.cmd, "GET") == 0) {
        if (command.argc != 1) {
            return queue_error(client, "wrong number of arguments to 'GET'");
        }
        size_t value_size = 0;
        const char* value = ks_kv_get(store, command.argv[0], &value_size);
        response_len = value == NULL
                           ? ks_fmt_nil(response, sizeof(response))
                           : ks_fmt_bulk(response, sizeof(response), value, value_size);
    } else if (strcmp(command.cmd, "DEL") == 0) {
        if (command.argc != 1) {
            return queue_error(client, "wrong number of arguments to 'DEL'");
        }
        response_len = ks_fmt_int(response, sizeof(response),
                                  ks_kv_delete(store, command.argv[0]) ? 1 : 0);
    } else if (strcmp(command.cmd, "EXISTS") == 0) {
        if (command.argc != 1) {
            return queue_error(client, "wrong number of arguments to 'EXISTS'");
        }
        response_len = ks_fmt_int(response, sizeof(response),
                                  ks_kv_exists(store, command.argv[0]) ? 1 : 0);
    } else {
        return queue_error(client, "unknown command");
    }

    return response_len == 0 ? -1 : client_queue(client, response, response_len);
}

static int process_requests(ks_client_t* client, ks_kv_t* store) {
    size_t consumed = 0;
    while (consumed < client->input_len) {
        if (client->output_len - client->output_offset >= KS_OUTPUT_HIGH_WATER) {
            break;
        }
        char* newline = memchr(client->input + consumed, '\n', client->input_len - consumed);
        if (newline == NULL) {
            break;
        }

        size_t end = (size_t)(newline - client->input) + 1u;
        size_t line_len = end - consumed;
        size_t content_len = line_len - 1u;
        if (content_len > 0 && client->input[end - 2u] == '\r') {
            content_len--;
        }
        if (content_len > KS_MAX_REQUEST) {
            if (queue_error(client, "request too large") != 0) {
                return -1;
            }
            client->close_after_write = 1;
            consumed = client->input_len;
            break;
        }

        if (memchr(client->input + consumed, '\0', line_len) != NULL) {
            if (queue_error(client, "invalid command") != 0) {
                return -1;
            }
            consumed = end;
            continue;
        }

        int has_following_data = end < client->input_len;
        char saved = has_following_data ? client->input[end] : '\0';
        client->input[end] = '\0';
        int result = handle_command(client, store, client->input + consumed);
        if (has_following_data) {
            client->input[end] = saved;
        }
        if (result != 0) {
            return -1;
        }
        consumed = end;
    }

    if (consumed > 0) {
        size_t remaining = client->input_len - consumed;
        memmove(client->input, client->input + consumed, remaining);
        client->input_len = remaining;
    }
    return 0;
}

static int read_client(ks_client_t* client, ks_kv_t* store) {
    for (;;) {
        if (client->output_len - client->output_offset >= KS_OUTPUT_HIGH_WATER) {
            return 0;
        }
        if (client->input_len == KS_INPUT_CAP) {
            if (queue_error(client, "request too large") != 0) {
                return -1;
            }
            client->input_len = 0;
            client->close_after_write = 1;
            return 0;
        }

        size_t received = 0;
        ks_io_status status = ks_recv(client->fd, client->input + client->input_len,
                                      KS_INPUT_CAP - client->input_len, &received);
        if (status == KS_IO_WOULD_BLOCK) {
            return 0;
        }
        if (status == KS_IO_CLOSED) {
            if (client->input_len != 0 &&
                queue_error(client, "incomplete command") != 0) {
                return -1;
            }
            client->input_len = 0;
            client->close_after_write = 1;
            return 0;
        }
        if (status == KS_IO_ERROR) {
            return -1;
        }

        client->input_len += received;
        if (process_requests(client, store) != 0) {
            return -1;
        }
        if (client->close_after_write) {
            return 0;
        }
    }
}

static int write_client(ks_client_t* client) {
    while (client->output_offset < client->output_len) {
        size_t sent = 0;
        ks_io_status status = ks_send(client->fd, client->output + client->output_offset,
                                      client->output_len - client->output_offset, &sent);
        if (status == KS_IO_WOULD_BLOCK) {
            return 0;
        }
        if (status == KS_IO_CLOSED || status == KS_IO_ERROR) {
            return -1;
        }
        client->output_offset += sent;
    }
    client->output_offset = 0;
    client->output_len = 0;
    return client->close_after_write ? -1 : 0;
}

static int accept_clients(int listen_fd, ks_client_t clients[]) {
    for (int accepted = 0; accepted < KS_ACCEPT_BUDGET && !ks_stop_requested; accepted++) {
        int client_fd = ks_accept(listen_fd);
        if (client_fd == KS_NET_ACCEPT_WOULD_BLOCK) {
            return 0;
        }
        if (client_fd == KS_NET_ACCEPT_ERROR) {
            ks_log_warn("accept failed");
            return -1;
        }

        int slot = -1;
        for (int i = 0; i < KS_MAX_CLIENTS; i++) {
            if (!clients[i].used) {
                slot = i;
                break;
            }
        }
        if (slot < 0 || client_fd >= FD_SETSIZE) {
            ks_log_warn("too many clients");
            (void)ks_close(client_fd);
            continue;
        }
        if (client_init(&clients[slot], client_fd) != 0) {
            ks_log_warn("failed to allocate client buffers");
            continue;
        }
        ks_log_info("client connected (fd=%d)", client_fd);
    }
    return 0;
}

int ks_server_run(const ks_config_t* cfg) {
    int listen_fd = ks_listen(cfg->host, cfg->port);
    if (listen_fd < 0) {
        return 1;
    }
    if (listen_fd >= FD_SETSIZE) {
        ks_log_err("listener exceeds FD_SETSIZE");
        (void)ks_close(listen_fd);
        return 1;
    }

    ks_kv_t store;
    if (!ks_kv_init(&store)) {
        ks_log_err("failed to initialize key-value store");
        (void)ks_close(listen_fd);
        return 1;
    }

    ks_stop_requested = 0;
    void (*previous_sigint)(int) = signal(SIGINT, handle_stop_signal);
    void (*previous_sigterm)(int) = signal(SIGTERM, handle_stop_signal);
    ks_log_info("listening on %s:%u", cfg->host, cfg->port);

    ks_client_t clients[KS_MAX_CLIENTS] = {0};
    int result = 0;
    int accept_backoff_ticks = 0;
    while (!ks_stop_requested) {
        fd_set read_fds;
        fd_set write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        if (accept_backoff_ticks == 0) {
            FD_SET(listen_fd, &read_fds);
        } else {
            accept_backoff_ticks--;
        }
        int max_fd = listen_fd;

        for (int i = 0; i < KS_MAX_CLIENTS; i++) {
            ks_client_t* client = &clients[i];
            if (!client->used) {
                continue;
            }
            if (!client->close_after_write &&
                client->output_len - client->output_offset < KS_OUTPUT_HIGH_WATER) {
                FD_SET(client->fd, &read_fds);
            }
            if (client->output_offset < client->output_len) {
                FD_SET(client->fd, &write_fds);
            }
            if (client->fd > max_fd) {
                max_fd = client->fd;
            }
        }

        struct timeval timeout = {.tv_sec = 0, .tv_usec = 100000};
        int ready = select(max_fd + 1, &read_fds, &write_fds, NULL, &timeout);
        if (ready < 0) {
#if !defined(_WIN32)
            if (errno == EINTR) {
                continue;
            }
#endif
            ks_log_err("select failed");
            result = 1;
            break;
        }

        if (accept_backoff_ticks == 0 && FD_ISSET(listen_fd, &read_fds) &&
            accept_clients(listen_fd, clients) != 0) {
            accept_backoff_ticks = 10;
        }

        for (int i = 0; i < KS_MAX_CLIENTS; i++) {
            ks_client_t* client = &clients[i];
            if (!client->used) {
                continue;
            }

            int should_close = 0;
            if (FD_ISSET(client->fd, &read_fds) && read_client(client, &store) != 0) {
                should_close = 1;
            }
            if (!should_close && FD_ISSET(client->fd, &write_fds) && write_client(client) != 0) {
                should_close = 1;
            }
            if (!should_close && client->input_len > 0 &&
                client->output_len - client->output_offset < KS_OUTPUT_HIGH_WATER &&
                process_requests(client, &store) != 0) {
                should_close = 1;
            }
            if (!should_close && client->close_after_write &&
                client->output_len == client->output_offset) {
                should_close = 1;
            }
            if (should_close) {
                ks_log_info("client disconnected (fd=%d)", client->fd);
                client_reset(client);
            }
        }
    }

    for (int i = 0; i < KS_MAX_CLIENTS; i++) {
        client_reset(&clients[i]);
    }
    (void)ks_close(listen_fd);
    ks_kv_destroy(&store);
    if (previous_sigint != SIG_ERR) {
        (void)signal(SIGINT, previous_sigint);
    }
    if (previous_sigterm != SIG_ERR) {
        (void)signal(SIGTERM, previous_sigterm);
    }
    ks_log_info("server stopped");
    return result;
}
