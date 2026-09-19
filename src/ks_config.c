#include "ks_config.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #define strcasecmp _stricmp
#else
    #include <strings.h>
#endif

void ks_config_init(ks_config_t* cfg) {
    cfg->host = "0.0.0.0";
    cfg->port = 7070;
    cfg->log_level = 2;
    cfg->max_clients = 256;
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

static int parse_max_clients(const char* s, size_t* max_clients) {
    char* end = NULL;
    errno = 0;
    unsigned long value = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value < 1 || value > 1023) {
        return -1;
    }
    *max_clients = (size_t)value;
    return 0;
}

static int apply_host(ks_config_t* cfg, const char* value, const char* source) {
    if (value == NULL || value[0] == '\0') {
        fprintf(stderr, "Invalid host from %s\n", source);
        return -1;
    }
    cfg->host = value;
    return 0;
}

static int config_from_env(ks_config_t* cfg, int use_host, int use_port,
                           int use_log_level, int use_max_clients) {
    const char* value = getenv("KESSEL_HOST");
    if (use_host && value != NULL && apply_host(cfg, value, "KESSEL_HOST") != 0) {
        return -1;
    }

    value = getenv("KESSEL_PORT");
    if (use_port && value != NULL && parse_port(value, &cfg->port) != 0) {
        fprintf(stderr, "Invalid port from KESSEL_PORT: %s\n", value);
        return -1;
    }

    value = getenv("KESSEL_LOG_LEVEL");
    if (use_log_level && value != NULL && parse_log_level(value, &cfg->log_level) != 0) {
        fprintf(stderr, "Invalid log level from KESSEL_LOG_LEVEL: %s\n", value);
        return -1;
    }

    value = getenv("KESSEL_MAX_CLIENTS");
    if (use_max_clients && value != NULL && parse_max_clients(value, &cfg->max_clients) != 0) {
        fprintf(stderr, "Invalid max clients from KESSEL_MAX_CLIENTS: %s\n", value);
        return -1;
    }
    return 0;
}

int ks_config_from_env(ks_config_t* cfg) {
    return config_from_env(cfg, 1, 1, 1, 1);
}

static int has_argument(int argc, char* argv[], const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

int ks_config_from_argv(ks_config_t* cfg, int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            if (apply_host(cfg, argv[++i], "--host") != 0) {
                return -1;
            }

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

        } else if (strcmp(argv[i], "--max-clients") == 0 && i + 1 < argc) {
            if (parse_max_clients(argv[++i], &cfg->max_clients) != 0) {
                fprintf(stderr, "Invalid max clients: %s\n", argv[i]);
                return -1;
            }

        } else {
            fprintf(stderr, "Unknown arg: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int ks_config_load(ks_config_t* cfg, int argc, char* argv[]) {
    int use_host = !has_argument(argc, argv, "--host");
    int use_port = !has_argument(argc, argv, "--port");
    int use_log_level = !has_argument(argc, argv, "--log");
    int use_max_clients = !has_argument(argc, argv, "--max-clients");

    if (config_from_env(cfg, use_host, use_port, use_log_level,
                        use_max_clients) != 0) {
        return -1;
    }
    return ks_config_from_argv(cfg, argc, argv);
}
