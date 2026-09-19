#include "ks_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                                \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void test_unquoted_command(void) {
    char line[] = "  sEt key Value\r\n";
    ks_cmd_t command;

    CHECK(ks_parse_line(line, &command));
    CHECK(strcmp(command.cmd, "SET") == 0);
    CHECK(command.argc == 2);
    CHECK(strcmp(command.argv[0], "key") == 0);
    CHECK(strcmp(command.argv[1], "Value") == 0);
}

static void test_quoted_arguments(void) {
    char line[] = "echo \"hello world\" \"a\\\"b\\\\c\" \"\"";
    ks_cmd_t command;

    CHECK(ks_parse_line(line, &command));
    CHECK(strcmp(command.cmd, "ECHO") == 0);
    CHECK(command.argc == 3);
    CHECK(strcmp(command.argv[0], "hello world") == 0);
    CHECK(strcmp(command.argv[1], "a\"b\\c") == 0);
    CHECK(strcmp(command.argv[2], "") == 0);
}

static void test_tabs_and_argument_limit(void) {
    char line[] = "PING\tone\t two";
    ks_cmd_t command;
    CHECK(ks_parse_line(line, &command));
    CHECK(command.argc == 2);

    char too_many[256] = "cmd";
    size_t used = 3;
    for (int i = 0; i < KS_MAX_ARGS + 1; ++i) {
        if (used + 2 >= sizeof(too_many)) {
            break;
        }
        too_many[used++] = ' ';
        too_many[used++] = 'x';
        too_many[used] = '\0';
    }
    CHECK(!ks_parse_line(too_many, &command));
    CHECK(command.cmd == NULL);
    CHECK(command.argc == 0);
}

static void test_malformed_requests(void) {
    ks_cmd_t command;
    char empty[] = " \t\r\n";
    char unterminated[] = "SET key \"value";
    char bad_escape[] = "SET key \"bad\\nvalue\"";
    char suffix[] = "SET key \"value\"tail";
    char embedded_quote[] = "SET ke\"y value";

    CHECK(!ks_parse_line(NULL, &command));
    CHECK(!ks_parse_line(empty, &command));
    CHECK(!ks_parse_line(unterminated, &command));
    CHECK(!ks_parse_line(bad_escape, &command));
    CHECK(!ks_parse_line(suffix, &command));
    CHECK(!ks_parse_line(embedded_quote, &command));
    CHECK(!ks_parse_line(empty, NULL));

    char empty_command[] = "\"\"";
    CHECK(!ks_parse_line(empty_command, &command));
    char unknown_escape[] = "SET key \"ok\\tvalue\"";
    CHECK(!ks_parse_line(unknown_escape, &command));
}

static void test_request_limit(void) {
    ks_cmd_t command;
    char* accepted = malloc(KS_MAX_REQUEST + 3U);
    char* rejected = malloc(KS_MAX_REQUEST + 4U);
    CHECK(accepted != NULL);
    CHECK(rejected != NULL);
    if (accepted == NULL || rejected == NULL) {
        free(accepted);
        free(rejected);
        return;
    }

    memset(accepted, 'a', KS_MAX_REQUEST);
    accepted[KS_MAX_REQUEST] = '\r';
    accepted[KS_MAX_REQUEST + 1U] = '\n';
    accepted[KS_MAX_REQUEST + 2U] = '\0';
    CHECK(ks_parse_line(accepted, &command));

    memset(rejected, 'a', KS_MAX_REQUEST + 1U);
    rejected[KS_MAX_REQUEST + 1U] = '\r';
    rejected[KS_MAX_REQUEST + 2U] = '\n';
    rejected[KS_MAX_REQUEST + 3U] = '\0';
    CHECK(!ks_parse_line(rejected, &command));

    free(accepted);
    free(rejected);
}

static void test_response_formatters(void) {
    char response[64];

    CHECK(ks_fmt_simple(response, sizeof(response), "OK") == 5);
    CHECK(strcmp(response, "+OK\r\n") == 0);
    CHECK(ks_fmt_error(response, sizeof(response), "bad request") == 18);
    CHECK(strcmp(response, "-ERR bad request\r\n") == 0);
    CHECK(ks_fmt_int(response, sizeof(response), -42) == 6);
    CHECK(strcmp(response, ":-42\r\n") == 0);
    CHECK(ks_fmt_bulk(response, sizeof(response), "a\0b", 3) == 9);
    CHECK(memcmp(response, "$3\r\na\0b\r\n", 9) == 0);
    CHECK(response[9] == '\0');
    CHECK(ks_fmt_bulk(response, sizeof(response), NULL, 0) == 6);
    CHECK(strcmp(response, "$0\r\n\r\n") == 0);
    CHECK(ks_fmt_nil(response, sizeof(response)) == 5);
    CHECK(strcmp(response, "$-1\r\n") == 0);
    CHECK(ks_fmt_array_header(response, sizeof(response), 3) == 4);
    CHECK(strcmp(response, "*3\r\n") == 0);
}

static void test_formatter_bounds(void) {
    char exact[6];
    char short_buffer[5] = "xxxx";

    CHECK(ks_fmt_simple(exact, sizeof(exact), "OK") == 5);
    CHECK(ks_fmt_simple(short_buffer, sizeof(short_buffer), "OK") == 0);
    CHECK(short_buffer[0] == '\0');
    CHECK(ks_fmt_int(NULL, 0, 1) == 0);
    CHECK(ks_fmt_bulk(short_buffer, sizeof(short_buffer), "x", 1) == 0);
    CHECK(short_buffer[0] == '\0');
    CHECK(ks_fmt_bulk(exact, sizeof(exact), NULL, 1) == 0);
    CHECK(ks_fmt_nil(short_buffer, sizeof(short_buffer)) == 0);
    CHECK(short_buffer[0] == '\0');
    CHECK(ks_fmt_array_header(NULL, 0, 1) == 0);
}

int main(void) {
    test_unquoted_command();
    test_quoted_arguments();
    test_tabs_and_argument_limit();
    test_malformed_requests();
    test_request_limit();
    test_response_formatters();
    test_formatter_bounds();

    if (failures != 0) {
        fprintf(stderr, "%d protocol test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("protocol tests passed");
    return EXIT_SUCCESS;
}
