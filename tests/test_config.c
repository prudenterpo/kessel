#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ks_config.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static void test_defaults(void) {
    ks_config_t cfg;
    ks_config_init(&cfg);
    assert(strcmp(cfg.host, "0.0.0.0") == 0);
    assert(cfg.port == 7070);
    assert(cfg.log_level == 2);
    assert(cfg.max_clients == 256);
}

static void test_valid_arguments(void) {
    ks_config_t cfg;
    ks_config_init(&cfg);
    char* argv[] = {"kessel", "--host", "127.0.0.1", "--port", "17070", "--log", "DEBUG",
                    "--max-clients", "42"};
    assert(ks_config_from_argv(&cfg, 9, argv) == 0);
    assert(strcmp(cfg.host, "127.0.0.1") == 0);
    assert(cfg.port == 17070);
    assert(cfg.log_level == 3);
    assert(cfg.max_clients == 42);
}

static void test_environment_and_argument_precedence(void) {
    assert(setenv("KESSEL_HOST", "127.0.0.2", 1) == 0);
    assert(setenv("KESSEL_PORT", "17071", 1) == 0);
    assert(setenv("KESSEL_LOG_LEVEL", "WARN", 1) == 0);
    assert(setenv("KESSEL_MAX_CLIENTS", "23", 1) == 0);

    ks_config_t cfg;
    ks_config_init(&cfg);
    char* argv[] = {"kessel", "--port", "17072", "--max-clients", "24"};
    assert(ks_config_load(&cfg, 5, argv) == 0);
    assert(strcmp(cfg.host, "127.0.0.2") == 0);
    assert(cfg.port == 17072);
    assert(cfg.log_level == 1);
    assert(cfg.max_clients == 24);

    assert(unsetenv("KESSEL_HOST") == 0);
    assert(unsetenv("KESSEL_PORT") == 0);
    assert(unsetenv("KESSEL_LOG_LEVEL") == 0);
    assert(unsetenv("KESSEL_MAX_CLIENTS") == 0);

    assert(setenv("KESSEL_PORT", "bad", 1) == 0);
    ks_config_init(&cfg);
    char* override_bad_env[] = {"kessel", "--port", "17073"};
    assert(ks_config_load(&cfg, 3, override_bad_env) == 0);
    assert(cfg.port == 17073);
    assert(unsetenv("KESSEL_PORT") == 0);
}

static void test_invalid_arguments(void) {
    const char* invalid_ports[] = {"0", "65536", "abc", "7070x", "-1"};
    for (size_t i = 0; i < sizeof(invalid_ports) / sizeof(invalid_ports[0]); i++) {
        ks_config_t cfg;
        ks_config_init(&cfg);
        char* argv[] = {"kessel", "--port", (char*)invalid_ports[i]};
        assert(ks_config_from_argv(&cfg, 3, argv) == -1);
    }

    ks_config_t cfg;
    ks_config_init(&cfg);
    char* invalid_log[] = {"kessel", "--log", "TRACE"};
    assert(ks_config_from_argv(&cfg, 3, invalid_log) == -1);

    char* missing_value[] = {"kessel", "--port"};
    assert(ks_config_from_argv(&cfg, 2, missing_value) == -1);

    char* unknown[] = {"kessel", "--unknown"};
    assert(ks_config_from_argv(&cfg, 2, unknown) == -1);

    const char* invalid_counts[] = {"0", "1024", "abc", "10x", "-1"};
    for (size_t i = 0; i < sizeof(invalid_counts) / sizeof(invalid_counts[0]); i++) {
        ks_config_init(&cfg);
        char* invalid_max[] = {"kessel", "--max-clients", (char*)invalid_counts[i]};
        assert(ks_config_from_argv(&cfg, 3, invalid_max) == -1);
    }

    assert(setenv("KESSEL_PORT", "bad", 1) == 0);
    ks_config_init(&cfg);
    assert(ks_config_from_env(&cfg) == -1);
    assert(unsetenv("KESSEL_PORT") == 0);
}

int main(void) {
    test_defaults();
    test_valid_arguments();
    test_environment_and_argument_precedence();
    test_invalid_arguments();
    return 0;
}
