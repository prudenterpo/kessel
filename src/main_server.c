#include <stdio.h>
#include "ks_config.h"
#include "ks_server.h"
#include "ks_log.h"

int main(int argc, char* argv[]) {
    ks_config_t cfg;
    ks_config_init(&cfg);

    if (ks_config_load(&cfg, argc, argv) != 0) {
        fprintf(stderr, "Usage: kessel [--host 0.0.0.0] [--port 7070] "
                        "[--log DEBUG|INFO|WARN|ERROR] [--max-clients 256]\n");
        return 1;
    }

    ks_log_set_level(cfg.log_level);
    ks_log_info("Kessel starting on %s:%u (max-clients=%zu)", cfg.host, cfg.port,
                cfg.max_clients);

    return ks_server_run(&cfg);
}
