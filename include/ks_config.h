#ifndef KS_CONFIG_H
#define KS_CONFIG_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char* host;
    uint16_t port;
    int log_level;
    size_t max_clients;
} ks_config_t;

void ks_config_init(ks_config_t* cfg);
int  ks_config_from_env(ks_config_t* cfg);
int  ks_config_from_argv(ks_config_t* cfg, int argc, char* argv[]);
int  ks_config_load(ks_config_t* cfg, int argc, char* argv[]);

#endif
