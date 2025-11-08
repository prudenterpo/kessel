#include "ks_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
    #define strcasecmp _stricmp
#else
    #include <strings.h>
#endif

void ks_config_init(ks_config_t* cfg) {
    cfg->host = "0.0.0.0";
    cfg->port = 7070;
    cfg->log_level = 2;
}

static int parse_log_level(const char* s) {
    if (!s) return 2;
    if (0 == strcasecmp(s, "DEBUG")) return 3;
    if (0 == strcasecmp(s, "INFO"))  return 2;
    if (0 == strcasecmp(s, "WARN"))  return 1;
    if (0 == strcasecmp(s, "ERROR")) return 0;
    return 2;
}

int ks_config_from_argv(ks_config_t* cfg, int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            cfg->host = argv[++i];

        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            cfg->port = (uint16_t)atoi(argv[++i]);

        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            cfg->log_level = parse_log_level(argv[++i]);

        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}
