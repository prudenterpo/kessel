#include "ks_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

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

static int parse_log_level(const char* s, int* level) {
    if (0 == strcasecmp(s, "DEBUG")) *level = 3;
    else if (0 == strcasecmp(s, "INFO")) *level = 2;
    else if (0 == strcasecmp(s, "WARN")) *level = 1;
    else if (0 == strcasecmp(s, "ERROR")) *level = 0;
    else return -1;
    return 0;
}

static int parse_port(const char* s, uint16_t* port) {
    char* end = NULL;
    errno = 0;
    long value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value < 1 || value > UINT16_MAX) {
        return -1;
    }
    *port = (uint16_t)value;
    return 0;
}

int ks_config_from_argv(ks_config_t* cfg, int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            cfg->host = argv[++i];

        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (parse_port(argv[++i], &cfg->port) != 0) {
                fprintf(stderr, "Invalid port: %s\n", argv[i]);
                return -1;
            }

        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            if (parse_log_level(argv[++i], &cfg->log_level) != 0) {
                fprintf(stderr, "Invalid log level: %s\n", argv[i]);
                return -1;
            }

        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}
