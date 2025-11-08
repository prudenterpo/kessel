#include "ks_server.h"
#include "ks_net.h"
#include "ks_protocol.h"
#include "ks_log.h"

#if !defined(_WIN32)
    #include <sys/select.h>
    #include <unistd.h>
#endif
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define KS_MAX_CLIENTS 1024

typedef struct {
    int used;
    int fd;
} ks_client_t;

static void handle_command(int cfd, char* line) {
    ks_cmd_t cmd;
    char resp[KS_MAX_LINE + 64];

    if (!ks_parse_line(line, &cmd) || cmd.cmd == NULL) {
        ks_fmt_error(resp, sizeof(resp), "empty command");
        ks_writeall(cfd, resp, strlen(resp));
        return;
    }

    for (char* p = cmd.cmd; *p; ++p)
        *p = (char)toupper((unsigned char)*p);

    if (strcmp(cmd.cmd, "PING") == 0) {
        ks_fmt_simple(resp, sizeof(resp), "PONG");
        ks_writeall(cfd, resp, strlen(resp));
        return;
    }

    if (strcmp(cmd.cmd, "ECHO") == 0) {
        if (cmd.argc < 1) {
            ks_fmt_error(resp, sizeof(resp), "wrong number of arguments to 'ECHO'");
        } else {
            size_t n = strlen(cmd.argv[0]);
            ks_fmt_bulk(resp, sizeof(resp), cmd.argv[0], n);
        }
        ks_writeall(cfd, resp, strlen(resp));
        return;
    }

    if (strcmp(cmd.cmd, "HELP") == 0) {
        const char* help =
            "Kessel commands (Phase 1):\n"
            " PING\n"
            " ECHO <string>\n"
            " HELP\n";
        size_t n = strlen(help);
        ks_fmt_bulk(resp, sizeof(resp), help, n);
        ks_writeall(cfd, resp, strlen(resp));
        return;
    }

    ks_fmt_error(resp, sizeof(resp), "unknown command");
    ks_writeall(cfd, resp, strlen(resp));
}

int ks_server_run(const ks_config_t* cfg) {
    int lfd = ks_listen(cfg->host, cfg->port);
    if (lfd < 0) return 1;
    ks_log_info("Listening on %s:%u", cfg->host, cfg->port);

    ks_client_t clients[KS_MAX_CLIENTS] = {0};

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(lfd, &rfds);
        int maxfd = lfd;

        for (int i = 0; i < KS_MAX_CLIENTS; i++) {
            if (clients[i].used) {
                FD_SET(clients[i].fd, &rfds);
                if (clients[i].fd > maxfd)
                    maxfd = clients[i].fd;
            }
        }

        int rv = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if (rv < 0) {
            ks_log_err("select failed");
            break;
        }

        // New connection
        if (FD_ISSET(lfd, &rfds)) {
            int cfd = ks_accept(lfd);
            if (cfd >= 0) {
                int slot = -1;
                for (int i = 0; i < KS_MAX_CLIENTS; i++) {
                    if (!clients[i].used) { slot = i; break; }
                }

                if (slot >= 0) {
                    clients[slot].used = 1;
                    clients[slot].fd = cfd;
                    ks_log_info("Client connected (fd=%d)", cfd);
                } else {
                    ks_log_warn("Too many clients");
#if defined(_WIN32)
                    closesocket(cfd);
#else
                    close(cfd);
#endif
                }
            }
        }

        // Existing clients
        for (int i = 0; i < KS_MAX_CLIENTS; i++) {
            if (!clients[i].used) continue;
            int cfd = clients[i].fd;
            if (!FD_ISSET(cfd, &rfds)) continue;

            char line[KS_MAX_LINE];
            int n = ks_readline(cfd, line, sizeof(line));
            if (n < 0) {
                ks_log_info("Client disconnected (fd=%d)", cfd);
#if defined(_WIN32)
                closesocket(cfd);
#else
                close(cfd);
#endif
                clients[i].used = 0;
                continue;
            }

            handle_command(cfd, line);
        }
    }

    return 0;
}

