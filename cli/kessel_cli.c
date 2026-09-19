#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#if defined(KESSEL_HAVE_LIBEDIT)
#include <histedit.h>
#endif

#define KESSEL_CLI_DEFAULT_HOST "127.0.0.1"
#define KESSEL_CLI_DEFAULT_PORT "7070"
#define KESSEL_CLI_MAX_LINE (64U * 1024U)

typedef struct {
    const char* host;
    const char* port;
} cli_config_t;

typedef struct {
    int fd;
    unsigned char buffer[4096];
    size_t begin;
    size_t end;
} response_reader_t;

typedef enum {
    RESPONSE_OK = 0,
    RESPONSE_DISCONNECTED,
    RESPONSE_IO_ERROR,
    RESPONSE_PROTOCOL_ERROR,
    RESPONSE_INTERRUPTED
} response_status_t;

typedef enum {
    SUBSCRIBE_NOT_COMMAND = 0,
    SUBSCRIBE_COMMAND,
    SUBSCRIBE_NO_MEMORY
} subscribe_status_t;

typedef enum {
    RESPONSE_KIND_SIMPLE,
    RESPONSE_KIND_ERROR,
    RESPONSE_KIND_INTEGER,
    RESPONSE_KIND_BULK,
    RESPONSE_KIND_ARRAY
} response_kind_t;

typedef struct {
    char* line;
    size_t capacity;
#if defined(KESSEL_HAVE_LIBEDIT)
    EditLine* editor;
    History* history;
    HistEvent event;
#endif
} input_t;

static volatile sig_atomic_t subscription_interrupted = 0;
static bool subscription_active = false;
static bool unsubscribe_sent = false;
static const char* unsubscribe_command = NULL;
static size_t unsubscribe_command_length = 0;
static struct timespec unsubscribe_deadline;

static void handle_subscription_interrupt(int signal_number) {
    (void)signal_number;
    subscription_interrupted = 1;
}

static void print_usage(FILE* stream, const char* program) {
    fprintf(stream, "Usage: %s [-h host] [-p port]\n", program);
}

static bool valid_port(const char* port) {
    if (port == NULL || *port == '\0') {
        return false;
    }

    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(port, &end, 10);
    return errno == 0 && *end == '\0' && value >= 1 && value <= UINT16_MAX;
}

static int parse_args(int argc, char* argv[], cli_config_t* config) {
    config->host = KESSEL_CLI_DEFAULT_HOST;
    config->port = KESSEL_CLI_DEFAULT_PORT;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(stdout, argv[0]);
            return 1;
        }
        if ((strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) &&
            i + 1 < argc) {
            config->host = argv[++i];
            continue;
        }
        if ((strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) &&
            i + 1 < argc) {
            config->port = argv[++i];
            continue;
        }

        fprintf(stderr, "kessel-cli: unknown or incomplete option: %s\n", argv[i]);
        print_usage(stderr, argv[0]);
        return -1;
    }

    if (config->host[0] == '\0') {
        fputs("kessel-cli: host cannot be empty\n", stderr);
        return -1;
    }
    if (!valid_port(config->port)) {
        fprintf(stderr, "kessel-cli: invalid port: %s\n", config->port);
        return -1;
    }
    return 0;
}

static int connect_to_server(const cli_config_t* config) {
    struct addrinfo hints = {0};
    struct addrinfo* addresses = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int result = getaddrinfo(config->host, config->port, &hints, &addresses);
    if (result != 0) {
        fprintf(stderr, "kessel-cli: cannot resolve %s: %s\n", config->host,
                gai_strerror(result));
        return -1;
    }

    int fd = -1;
    int last_error = ECONNREFUSED;
    for (const struct addrinfo* address = addresses; address != NULL;
         address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype,
                    address->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }
        last_error = errno;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);

    if (fd < 0) {
        fprintf(stderr, "kessel-cli: cannot connect to %s:%s: %s\n",
                config->host, config->port, strerror(last_error));
    }
    return fd;
}

static bool write_all(int fd, const void* data, size_t length) {
    const unsigned char* cursor = data;
    while (length > 0) {
#if defined(MSG_NOSIGNAL)
        ssize_t written = send(fd, cursor, length, MSG_NOSIGNAL);
#else
        ssize_t written = send(fd, cursor, length, 0);
#endif
        if (written > 0) {
            cursor += (size_t)written;
            length -= (size_t)written;
        } else if (written < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

static bool deadline_reached(const struct timespec* deadline) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return true;
    }
    return now.tv_sec > deadline->tv_sec ||
           (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}

static response_status_t send_pending_unsubscribe(int fd) {
    if (!subscription_interrupted || unsubscribe_sent) {
        return RESPONSE_OK;
    }
    if (unsubscribe_command == NULL ||
        !write_all(fd, unsubscribe_command, unsubscribe_command_length)) {
        return RESPONSE_IO_ERROR;
    }
    unsubscribe_sent = true;
    if (clock_gettime(CLOCK_MONOTONIC, &unsubscribe_deadline) != 0) {
        return RESPONSE_IO_ERROR;
    }
    ++unsubscribe_deadline.tv_sec;
    return RESPONSE_OK;
}

static response_status_t fill_reader(response_reader_t* reader) {
    for (;;) {
        if (subscription_active) {
            response_status_t status = send_pending_unsubscribe(reader->fd);
            if (status != RESPONSE_OK) {
                return status;
            }
            if (unsubscribe_sent && deadline_reached(&unsubscribe_deadline)) {
                return RESPONSE_INTERRUPTED;
            }

            struct pollfd descriptor = {
                .fd = reader->fd,
                .events = POLLIN,
                .revents = 0,
            };
            int result = poll(&descriptor, 1, 100);
            if (result == 0) {
                continue;
            }
            if (result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return RESPONSE_IO_ERROR;
            }
            if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) {
                return RESPONSE_IO_ERROR;
            }
        }

        ssize_t received = recv(reader->fd, reader->buffer,
                                sizeof(reader->buffer), 0);
        if (received > 0) {
            reader->begin = 0;
            reader->end = (size_t)received;
            return RESPONSE_OK;
        }
        if (received == 0) {
            return RESPONSE_DISCONNECTED;
        }
        if (errno != EINTR) {
            return RESPONSE_IO_ERROR;
        }
    }
}

static response_status_t read_byte(response_reader_t* reader,
                                   unsigned char* byte) {
    if (reader->begin == reader->end) {
        response_status_t status = fill_reader(reader);
        if (status != RESPONSE_OK) {
            return status;
        }
    }
    *byte = reader->buffer[reader->begin++];
    return RESPONSE_OK;
}

static response_status_t read_exact(response_reader_t* reader, void* output,
                                    size_t length) {
    unsigned char* cursor = output;
    while (length > 0) {
        if (reader->begin == reader->end) {
            response_status_t status = fill_reader(reader);
            if (status != RESPONSE_OK) {
                return status;
            }
        }
        size_t available = reader->end - reader->begin;
        size_t chunk = available < length ? available : length;
        memcpy(cursor, reader->buffer + reader->begin, chunk);
        reader->begin += chunk;
        cursor += chunk;
        length -= chunk;
    }
    return RESPONSE_OK;
}

static response_status_t read_header(response_reader_t* reader, char* output,
                                     size_t capacity) {
    size_t length = 0;
    for (;;) {
        unsigned char byte = 0;
        response_status_t status = read_byte(reader, &byte);
        if (status != RESPONSE_OK) {
            return status;
        }
        if (byte == '\n') {
            if (length == 0 || output[length - 1] != '\r') {
                return RESPONSE_PROTOCOL_ERROR;
            }
            output[length - 1] = '\0';
            return RESPONSE_OK;
        }
        if (length + 1 >= capacity) {
            return RESPONSE_PROTOCOL_ERROR;
        }
        output[length++] = (char)byte;
    }
}

static bool parse_integer(const char* text, long long* value) {
    if (text == NULL || *text == '\0') {
        return false;
    }
    char* end = NULL;
    errno = 0;
    long long parsed = strtoll(text, &end, 10);
    if (errno != 0 || *end != '\0') {
        return false;
    }
    *value = parsed;
    return true;
}

static response_status_t render_bulk(response_reader_t* reader,
                                     const char* header) {
    long long length = 0;
    if (!parse_integer(header, &length) || length < -1 ||
        length > (long long)KESSEL_CLI_MAX_LINE) {
        return RESPONSE_PROTOCOL_ERROR;
    }
    if (length == -1) {
        puts("(nil)");
        return RESPONSE_OK;
    }

    size_t remaining = (size_t)length;
    unsigned char output[4096];
    while (remaining > 0) {
        size_t chunk = remaining < sizeof(output) ? remaining : sizeof(output);
        response_status_t status = read_exact(reader, output, chunk);
        if (status != RESPONSE_OK) {
            return status;
        }
        if (fwrite(output, 1, chunk, stdout) != chunk) {
            return RESPONSE_IO_ERROR;
        }
        remaining -= chunk;
    }

    unsigned char terminator[2];
    response_status_t status = read_exact(reader, terminator, sizeof(terminator));
    if (status != RESPONSE_OK) {
        return status;
    }
    if (terminator[0] != '\r' || terminator[1] != '\n') {
        return RESPONSE_PROTOCOL_ERROR;
    }
    if (fputc('\n', stdout) == EOF) {
        return RESPONSE_IO_ERROR;
    }
    return RESPONSE_OK;
}

static response_status_t render_array(response_reader_t* reader,
                                      const char* header) {
    long long count = 0;
    if (!parse_integer(header, &count) || count < 0 || count > 16) {
        return RESPONSE_PROTOCOL_ERROR;
    }

    for (long long i = 0; i < count; ++i) {
        unsigned char type = 0;
        response_status_t status = read_byte(reader, &type);
        if (status != RESPONSE_OK) {
            return status;
        }
        if (type != '$') {
            return RESPONSE_PROTOCOL_ERROR;
        }

        char bulk_header[32];
        status = read_header(reader, bulk_header, sizeof(bulk_header));
        if (status != RESPONSE_OK) {
            return status;
        }
        long long length = 0;
        if (!parse_integer(bulk_header, &length) || length < -1 ||
            length > (long long)KESSEL_CLI_MAX_LINE) {
            return RESPONSE_PROTOCOL_ERROR;
        }

        if (i > 0 && fputc(' ', stdout) == EOF) {
            return RESPONSE_IO_ERROR;
        }
        if (length == -1) {
            if (fputs("(nil)", stdout) == EOF) {
                return RESPONSE_IO_ERROR;
            }
            continue;
        }

        size_t remaining = (size_t)length;
        unsigned char output[4096];
        while (remaining > 0) {
            size_t chunk = remaining < sizeof(output) ? remaining : sizeof(output);
            status = read_exact(reader, output, chunk);
            if (status != RESPONSE_OK) {
                return status;
            }
            if (fwrite(output, 1, chunk, stdout) != chunk) {
                return RESPONSE_IO_ERROR;
            }
            remaining -= chunk;
        }
        unsigned char terminator[2];
        status = read_exact(reader, terminator, sizeof(terminator));
        if (status != RESPONSE_OK) {
            return status;
        }
        if (terminator[0] != '\r' || terminator[1] != '\n') {
            return RESPONSE_PROTOCOL_ERROR;
        }
    }
    return fputc('\n', stdout) == EOF ? RESPONSE_IO_ERROR : RESPONSE_OK;
}

static response_status_t render_response(response_reader_t* reader,
                                         response_kind_t* kind) {
    unsigned char type = 0;
    response_status_t status = read_byte(reader, &type);
    if (status != RESPONSE_OK) {
        return status;
    }

    char header[KESSEL_CLI_MAX_LINE + 1U];
    status = read_header(reader, header, sizeof(header));
    if (status != RESPONSE_OK) {
        return status;
    }

    switch (type) {
        case '+':
            *kind = RESPONSE_KIND_SIMPLE;
            puts(header);
            return RESPONSE_OK;
        case '-':
            *kind = RESPONSE_KIND_ERROR;
            printf("(error) %s\n", header);
            return RESPONSE_OK;
        case ':': {
            *kind = RESPONSE_KIND_INTEGER;
            long long value = 0;
            if (!parse_integer(header, &value)) {
                return RESPONSE_PROTOCOL_ERROR;
            }
            printf("(integer) %lld\n", value);
            return RESPONSE_OK;
        }
        case '$':
            *kind = RESPONSE_KIND_BULK;
            return render_bulk(reader, header);
        case '*':
            *kind = RESPONSE_KIND_ARRAY;
            return render_array(reader, header);
        default:
            return RESPONSE_PROTOCOL_ERROR;
    }
}

static int input_init(input_t* input, const char* program, bool interactive) {
    input->line = NULL;
    input->capacity = 0;
#if defined(KESSEL_HAVE_LIBEDIT)
    input->editor = NULL;
    input->history = NULL;
    if (!interactive) {
        return 0;
    }
    input->history = history_init();
    input->editor = el_init(program, stdin, stdout, stderr);
    if (input->history == NULL || input->editor == NULL) {
        if (input->editor != NULL) {
            el_end(input->editor);
        }
        if (input->history != NULL) {
            history_end(input->history);
        }
        return -1;
    }
    (void)history(input->history, &input->event, H_SETSIZE, 100);
    (void)el_set(input->editor, EL_EDITOR, "emacs");
    (void)el_set(input->editor, EL_HIST, history, input->history);
#else
    (void)program;
    (void)interactive;
#endif
    return 0;
}

static void input_destroy(input_t* input) {
#if defined(KESSEL_HAVE_LIBEDIT)
    if (input->editor != NULL) {
        el_end(input->editor);
    }
    if (input->history != NULL) {
        history_end(input->history);
    }
#endif
    free(input->line);
}

#if defined(KESSEL_HAVE_LIBEDIT)
static const char* prompt(EditLine* editor) {
    (void)editor;
    return "kessel> ";
}
#endif

static const char* input_read(input_t* input, bool interactive,
                              size_t* length) {
#if defined(KESSEL_HAVE_LIBEDIT)
    if (interactive) {
        int count = 0;
        (void)el_set(input->editor, EL_PROMPT, prompt);
        const char* line = el_gets(input->editor, &count);
        if (line == NULL) {
            return NULL;
        }
        *length = (size_t)count;
        if (*length > 1U) {
            (void)history(input->history, &input->event, H_ENTER, line);
        }
        return line;
    }
#endif
    if (interactive) {
        fputs("kessel> ", stdout);
        fflush(stdout);
    }
    ssize_t count = getline(&input->line, &input->capacity, stdin);
    if (count < 0) {
        return NULL;
    }
    *length = (size_t)count;
    return input->line;
}

static bool is_exit_command(const char* line, size_t length) {
    while (length > 0 && (line[length - 1] == '\n' ||
                          line[length - 1] == '\r' || line[length - 1] == ' ' ||
                          line[length - 1] == '\t')) {
        --length;
    }
    while (length > 0 && (*line == ' ' || *line == '\t')) {
        ++line;
        --length;
    }
    return length == 4U && strncasecmp(line, "EXIT", length) == 0;
}

static bool send_command(int fd, const char* line, size_t length) {
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        --length;
    }
    if (!write_all(fd, line, length)) {
        return false;
    }
    return write_all(fd, "\r\n", 2);
}

static size_t command_length(const char* line, size_t length) {
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        --length;
    }
    return length;
}

static subscribe_status_t copy_subscribe_argument(const char* line,
                                                   size_t length,
                                                   char** argument) {
    *argument = NULL;
    length = command_length(line, length);
    while (length > 0 && (*line == ' ' || *line == '\t')) {
        ++line;
        --length;
    }

    static const char command[] = "SUBSCRIBE";
    size_t command_size = sizeof(command) - 1U;
    if (length <= command_size || strncasecmp(line, command, command_size) != 0 ||
        (line[command_size] != ' ' && line[command_size] != '\t')) {
        return SUBSCRIBE_NOT_COMMAND;
    }
    line += command_size;
    length -= command_size;
    while (length > 0 && (*line == ' ' || *line == '\t')) {
        ++line;
        --length;
    }
    while (length > 0 && (line[length - 1] == ' ' || line[length - 1] == '\t')) {
        --length;
    }
    if (length == 0) {
        return SUBSCRIBE_NOT_COMMAND;
    }

    *argument = malloc(length + 1U);
    if (*argument == NULL) {
        return SUBSCRIBE_NO_MEMORY;
    }
    memcpy(*argument, line, length);
    (*argument)[length] = '\0';
    return SUBSCRIBE_COMMAND;
}

static response_status_t wait_for_subscription_data(response_reader_t* reader) {
    while (reader->begin == reader->end && !subscription_interrupted) {
        struct pollfd descriptor = {
            .fd = reader->fd,
            .events = POLLIN,
            .revents = 0,
        };
        int result = poll(&descriptor, 1, 100);
        if (result < 0 && errno != EINTR) {
            return RESPONSE_IO_ERROR;
        }
        if (result > 0 && (descriptor.revents & (POLLIN | POLLHUP)) == 0) {
            return RESPONSE_IO_ERROR;
        }
    }
    return RESPONSE_OK;
}

static response_status_t run_subscription(int fd, response_reader_t* reader,
                                          bool response_interrupted) {
    response_status_t status = RESPONSE_OK;
    while (!subscription_interrupted && !response_interrupted) {
        status = wait_for_subscription_data(reader);
        if (status != RESPONSE_OK || subscription_interrupted) {
            break;
        }
        response_kind_t kind;
        status = render_response(reader, &kind);
        if (status != RESPONSE_OK) {
            break;
        }
        fflush(stdout);
    }

    if ((status == RESPONSE_OK && subscription_interrupted) ||
        response_interrupted) {
        status = send_pending_unsubscribe(fd);

        while (status == RESPONSE_OK) {
            response_kind_t kind;
            status = render_response(reader, &kind);
            if (status != RESPONSE_OK || kind == RESPONSE_KIND_INTEGER ||
                kind == RESPONSE_KIND_ERROR) {
                break;
            }
        }
        fflush(stdout);
    }

    return status;
}

static void report_response_error(response_status_t status) {
    if (status == RESPONSE_DISCONNECTED) {
        fputs("kessel-cli: server disconnected\n", stderr);
    } else if (status == RESPONSE_PROTOCOL_ERROR) {
        fputs("kessel-cli: invalid response from server\n", stderr);
    } else if (status != RESPONSE_INTERRUPTED) {
        fprintf(stderr, "kessel-cli: connection error: %s\n", strerror(errno));
    }
}

int main(int argc, char* argv[]) {
    cli_config_t config;
    int argument_status = parse_args(argc, argv, &config);
    if (argument_status != 0) {
        return argument_status > 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    (void)signal(SIGPIPE, SIG_IGN);
    int fd = connect_to_server(&config);
    if (fd < 0) {
        return EXIT_FAILURE;
    }

    bool interactive = isatty(STDIN_FILENO) != 0;
    input_t input;
    if (input_init(&input, argv[0], interactive) != 0) {
        fputs("kessel-cli: cannot initialize command input\n", stderr);
        close(fd);
        return EXIT_FAILURE;
    }

    response_reader_t reader = {.fd = fd, .begin = 0, .end = 0};
    int exit_code = EXIT_SUCCESS;
    for (;;) {
        size_t length = 0;
        const char* line = input_read(&input, interactive, &length);
        if (line == NULL) {
            if (ferror(stdin)) {
                fputs("kessel-cli: cannot read command\n", stderr);
                exit_code = EXIT_FAILURE;
            }
            break;
        }
        if (is_exit_command(line, length)) {
            break;
        }
        if (command_length(line, length) == 0) {
            continue;
        }
        if (command_length(line, length) > KESSEL_CLI_MAX_LINE) {
            fputs("kessel-cli: command exceeds 64 KiB\n", stderr);
            continue;
        }
        char* subscribe_argument = NULL;
        subscribe_status_t subscribe_status =
            copy_subscribe_argument(line, length, &subscribe_argument);
        if (subscribe_status == SUBSCRIBE_NO_MEMORY) {
            fputs("kessel-cli: cannot allocate subscription command\n", stderr);
            exit_code = EXIT_FAILURE;
            break;
        }

        void (*previous_sigint)(int) = SIG_DFL;
        char* unsubscribe = NULL;
        if (subscribe_status == SUBSCRIBE_COMMAND) {
            size_t capacity = strlen(subscribe_argument) +
                              sizeof("UNSUBSCRIBE \r\n");
            unsubscribe = malloc(capacity);
            if (unsubscribe == NULL) {
                free(subscribe_argument);
                fputs("kessel-cli: cannot allocate subscription command\n", stderr);
                exit_code = EXIT_FAILURE;
                break;
            }
            int written = snprintf(unsubscribe, capacity, "UNSUBSCRIBE %s\r\n",
                                   subscribe_argument);
            if (written < 0 || (size_t)written >= capacity) {
                free(unsubscribe);
                free(subscribe_argument);
                fputs("kessel-cli: cannot prepare subscription command\n", stderr);
                exit_code = EXIT_FAILURE;
                break;
            }

            subscription_interrupted = 0;
            unsubscribe_sent = false;
            unsubscribe_command = unsubscribe;
            unsubscribe_command_length = (size_t)written;
            subscription_active = true;
            previous_sigint = signal(SIGINT, handle_subscription_interrupt);
            if (previous_sigint == SIG_ERR) {
                subscription_active = false;
                unsubscribe_command = NULL;
                free(unsubscribe);
                free(subscribe_argument);
                fputs("kessel-cli: cannot install interrupt handler\n", stderr);
                exit_code = EXIT_FAILURE;
                break;
            }
        }

        if (!send_command(fd, line, length)) {
            if (subscribe_status == SUBSCRIBE_COMMAND) {
                (void)signal(SIGINT, previous_sigint);
                subscription_active = false;
                unsubscribe_command = NULL;
            }
            free(unsubscribe);
            free(subscribe_argument);
            fprintf(stderr, "kessel-cli: connection error: %s\n", strerror(errno));
            exit_code = EXIT_FAILURE;
            break;
        }

        response_kind_t kind;
        response_status_t status = render_response(&reader, &kind);
        bool response_interrupted = status == RESPONSE_INTERRUPTED;
        if (subscribe_status == SUBSCRIBE_COMMAND &&
            ((status == RESPONSE_OK &&
              (kind == RESPONSE_KIND_SIMPLE || subscription_interrupted)) ||
             response_interrupted)) {
            status = run_subscription(fd, &reader, response_interrupted);
        }
        if (subscribe_status == SUBSCRIBE_COMMAND) {
            (void)signal(SIGINT, previous_sigint);
            subscription_active = false;
            subscription_interrupted = 0;
            unsubscribe_sent = false;
            unsubscribe_command = NULL;
            unsubscribe_command_length = 0;
        }
        free(unsubscribe);
        free(subscribe_argument);
        if (status == RESPONSE_INTERRUPTED) {
            break;
        }
        if (status != RESPONSE_OK) {
            report_response_error(status);
            exit_code = EXIT_FAILURE;
            break;
        }
        fflush(stdout);
    }

    input_destroy(&input);
    close(fd);
    return exit_code;
}
