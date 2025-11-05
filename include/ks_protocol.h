#ifndef KS_PROTOCOL_H
#define KS_PROTOCOL_H

#include <stddef.h>
#include <stdbool.h>

#define KS_MAX_ARGS 16
#define KS_MAX_LINE 4096

typedef struct {
    char* cmd;
    char* argv[KS_MAX_ARGS];
    int argc;
} ks_cmd_t;

// Parses "WORD arg1 arg2 ..."in-place (line must be mutable)
bool ks_parse_line(char* line, ks_cmd_t* out);

//Format helpers (write RESP-LIKE string into dst; return bytes written)
size_t ks_fmt_simple(char* dst, size_t cap, const char* s);
size_t ks_fmt_error(char* dst, size_t cap, const char* msg);
size_t ks_fmt_int(char* dst, size_t cap, long long v);
size_t ks_fmt_bulk(char* dst, size_t cap, const char* s, size_t n);

#endif
