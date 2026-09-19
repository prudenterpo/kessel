#include "ks_config.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

static void test_defaults(void) {
    ks_config_t cfg;
    ks_config_init(&cfg);
    assert(strcmp(cfg.host, "0.0.0.0") == 0);
    assert(cfg.port == 7070);
    assert(cfg.log_level == 2);
}

static void test_valid_arguments(void) {
    ks_config_t cfg;
    ks_config_init(&cfg);
    char* argv[] = {"kessel", "--host", "127.0.0.1", "--port", "17070", "--log", "DEBUG"};
    assert(ks_config_from_argv(&cfg, 7, argv) == 0);
    assert(strcmp(cfg.host, "127.0.0.1") == 0);
    assert(cfg.port == 17070);
    assert(cfg.log_level == 3);
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
}

int main(void) {
    test_defaults();
    test_valid_arguments();
    test_invalid_arguments();
    return 0;
}
