#ifndef KS_SERVER_H
#define KS_SERVER_H

#include "ks_config.h"

// Run the main event loop (Phase 1: PING/ECHO/HELP)
int ks_server_run(const ks_config_t* cfg);

#endif
