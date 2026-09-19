#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
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
    RESPONSE_PROTOCOL_ERROR
} response_status_t;

typedef struct {
    char* line;
    size_t capacity;
#if defined(KESSEL_HAVE_LIBEDIT)
    EditLine* editor;
    History* history;
    HistEvent event;
#endif
} input_t;

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

static response_status_t fill_reader(response_reader_t* reader) {
    for (;;) {
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

static response_status_t render_response(response_reader_t* reader) {
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
            puts(header);
            return RESPONSE_OK;
        case '-':
            printf("(error) %s\n", header);
            return RESPONSE_OK;
        case ':': {
            long long value = 0;
            if (!parse_integer(header, &value)) {
                return RESPONSE_PROTOCOL_ERROR;
            }
            printf("(integer) %lld\n", value);
            return RESPONSE_OK;
        }
        case '$':
            return render_bulk(reader, header);
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

static void report_response_error(response_status_t status) {
    if (status == RESPONSE_DISCONNECTED) {
        fputs("kessel-cli: server disconnected\n", stderr);
    } else if (status == RESPONSE_PROTOCOL_ERROR) {
        fputs("kessel-cli: invalid response from server\n", stderr);
    } else {
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
        if (!send_command(fd, line, length)) {
            fprintf(stderr, "kessel-cli: connection error: %s\n", strerror(errno));
            exit_code = EXIT_FAILURE;
            break;
        }

        response_status_t status = render_response(&reader);
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
