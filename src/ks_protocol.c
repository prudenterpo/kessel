#include "ks_protocol.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void clear_command(ks_cmd_t* out) {
    out->cmd = NULL;
    out->argc = 0;
    for (size_t i = 0; i < KS_MAX_ARGS; ++i) {
        out->argv[i] = NULL;
    }
}

static void trim_crlf(char* line, size_t* length) {
    while (*length > 0 &&
           (line[*length - 1] == '\n' || line[*length - 1] == '\r')) {
        line[--(*length)] = '\0';
    }
}

static bool is_separator(unsigned char c) {
    return c == ' ' || c == '\t';
}

static size_t bounded_length(const char* text, size_t limit) {
    size_t length = 0;
    while (length < limit && text[length] != '\0') {
        ++length;
    }
    return length;
}

static bool parse_token(char** cursor, char** token) {
    char* read = *cursor;
    char* write = read;

    if (*read == '"') {
        ++read;
        write = read;
        *token = write;

        while (*read != '\0' && *read != '"') {
            if (*read == '\\') {
                ++read;
                if (*read != '"' && *read != '\\') {
                    return false;
                }
            }
            *write++ = *read++;
        }

        if (*read != '"') {
            return false;
        }
        ++read;
        if (*read != '\0' && !is_separator((unsigned char)*read)) {
            return false;
        }
        *write = '\0';
    } else {
        *token = read;
        while (*read != '\0' && !is_separator((unsigned char)*read)) {
            if (*read == '"') {
                return false;
            }
            ++read;
        }
        if (*read != '\0') {
            *read++ = '\0';
        }
    }

    while (is_separator((unsigned char)*read)) {
        ++read;
    }
    *cursor = read;
    return true;
}

bool ks_parse_line(char* line, ks_cmd_t* out) {
    if (line == NULL || out == NULL) {
        return false;
    }

    clear_command(out);
    size_t length = bounded_length(line, KS_MAX_REQUEST + 3U);
    if (length > KS_MAX_REQUEST + 2U) {
        return false;
    }
    trim_crlf(line, &length);
    if (length > KS_MAX_REQUEST) {
        return false;
    }

    char* cursor = line;
    while (is_separator((unsigned char)*cursor)) {
        ++cursor;
    }
    if (*cursor == '\0') {
        return false;
    }

    if (!parse_token(&cursor, &out->cmd) || out->cmd[0] == '\0') {
        clear_command(out);
        return false;
    }

    while (*cursor != '\0') {
        if (out->argc == KS_MAX_ARGS) {
            clear_command(out);
            return false;
        }
        if (!parse_token(&cursor, &out->argv[out->argc])) {
            clear_command(out);
            return false;
        }
        ++out->argc;
    }

    for (char* p = out->cmd; *p != '\0'; ++p) {
        *p = (char)toupper((unsigned char)*p);
    }
    return true;
}

static size_t format_text(char* dst, size_t cap, const char* prefix,
                          const char* text) {
    if (dst == NULL || cap == 0 || prefix == NULL || text == NULL) {
        return 0;
    }
    dst[0] = '\0';
    int written = snprintf(dst, cap, "%s%s\r\n", prefix, text);
    if (written < 0 || (size_t)written >= cap) {
        dst[0] = '\0';
        return 0;
    }
    return (size_t)written;
}

size_t ks_fmt_simple(char* dst, size_t cap, const char* s) {
    return format_text(dst, cap, "+", s);
}

size_t ks_fmt_error(char* dst, size_t cap, const char* msg) {
    return format_text(dst, cap, "-ERR ", msg);
}

size_t ks_fmt_int(char* dst, size_t cap, long long v) {
    if (dst == NULL || cap == 0) {
        return 0;
    }
    dst[0] = '\0';
    int written = snprintf(dst, cap, ":%lld\r\n", v);
    if (written < 0 || (size_t)written >= cap) {
        dst[0] = '\0';
        return 0;
    }
    return (size_t)written;
}

size_t ks_fmt_bulk(char* dst, size_t cap, const char* s, size_t n) {
    if (dst == NULL || cap == 0 || (s == NULL && n != 0)) {
        return 0;
    }
    dst[0] = '\0';

    int written = snprintf(dst, cap, "$%zu\r\n", n);
    if (written < 0 || (size_t)written >= cap) {
        dst[0] = '\0';
        return 0;
    }

    size_t header_length = (size_t)written;
    if (cap - header_length < 3U || n > cap - header_length - 3U) {
        dst[0] = '\0';
        return 0;
    }
    if (n != 0) {
        memcpy(dst + header_length, s, n);
    }
    dst[header_length + n] = '\r';
    dst[header_length + n + 1U] = '\n';
    dst[header_length + n + 2U] = '\0';
    return header_length + n + 2U;
}

size_t ks_fmt_nil(char* dst, size_t cap) {
    static const char nil[] = "$-1\r\n";
    if (dst == NULL || cap < sizeof(nil)) {
        if (dst != NULL && cap != 0) {
            dst[0] = '\0';
        }
        return 0;
    }
    memcpy(dst, nil, sizeof(nil));
    return sizeof(nil) - 1U;
}

size_t ks_fmt_array_header(char* dst, size_t cap, size_t count) {
    if (dst == NULL || cap == 0) {
        return 0;
    }
    dst[0] = '\0';
    int written = snprintf(dst, cap, "*%zu\r\n", count);
    if (written < 0 || (size_t)written >= cap) {
        dst[0] = '\0';
        return 0;
    }
    return (size_t)written;
}
