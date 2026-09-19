#ifndef KS_PROTOCOL_H
#define KS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>

#define KS_MAX_ARGS 16
#define KS_MAX_REQUEST (64U * 1024U)
/* Kept as the public line-buffer limit for existing callers. */
#define KS_MAX_LINE KS_MAX_REQUEST

typedef struct {
    char* cmd;
    char* argv[KS_MAX_ARGS];
    int argc;
} ks_cmd_t;

/*
 * Parses one NUL-terminated request in place.
 *
 * Arguments may be unquoted or enclosed in double quotes. Inside quoted
 * arguments, \" and \\ are decoded. The command is normalized to uppercase;
 * arguments retain their original case. Returned pointers remain owned by the
 * input buffer. The function returns false for empty or oversized requests,
 * malformed quoting, unsupported escapes, or too many arguments.
 */
bool ks_parse_line(char* line, ks_cmd_t* out);

/*
 * Writes one response, including CRLF, and returns its byte length. A return
 * value of zero means invalid input, formatting failure, or insufficient
 * capacity. Successful output is always NUL-terminated.
 */
size_t ks_fmt_simple(char* dst, size_t cap, const char* s);
size_t ks_fmt_error(char* dst, size_t cap, const char* msg);
size_t ks_fmt_int(char* dst, size_t cap, long long v);
size_t ks_fmt_bulk(char* dst, size_t cap, const char* s, size_t n);
size_t ks_fmt_nil(char* dst, size_t cap);

#endif
