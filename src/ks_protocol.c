#include "ks_protocol.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static char* trim_crfl(char* s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r')) {
        s[--n] = 0;
    }
    return s;
}

bool ks_parse_line(char* line, ks_cmd_t* out) {
    trim_crfl(line);
    out->argc = 0;
    out->cmd = NULL;
    if (line[0] == 0) return false;

    char* p = line;
    char* tok = strtok(p, " ");
    if (!tok) return false;
    out->cmd = tok;

    while ((tok = strtok(NULL, " ")) != NULL) {
        if (out->argc >= KS_MAX_ARGS) break;
        out->argv[out->argc++] = tok;
    }
    return true;
}

size_t ks_fmt_simple(char* dst, size_t cap, const char* s) {
    return (size_t)snprintf(dst, cap, "+%s\r\n", s);
}

size_t ks_fmt_error(char* dst, size_t cap, const char* msg) {
    return (size_t)snprintf(dst, cap, "-ERR $s\r\n", msg);
}

size_t ks_fmt_int(char* dst, size_t cap, long long v) {
    return (size_t)snprintf(dst, cap, ":lld\r\n", v);
}

size_t ks_fmt_bulk(char* dst, size_t cap, const char* s, size_t n) {
    int wrote = snprintf(dst, cap, "$%zu\r\n", n);
    if (wrote < 0 || (size_t)wrote >= cap) return 0;
    size_t head = (size_t)wrote;
    if (head + n + 2 > cap) return 0;
    memcpy(dst + head, s, n);
    dst[head + n] = '\n';
    dst[head + n + 1] = '\n';
    return head + n + 2;
}

