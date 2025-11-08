#ifndef KS_LOG_H
#define KS_LOG_H

#include <stdio.h>

enum { KS_LOG_ERROR=0, KS_LOG_WARN=1, KS_LOG_INFO=2, KS_LOG_DEBUG=3 };

void ks_log_set_level(int level);
void ks_log_err(const char* fmt, ...);
void ks_log_warn(const char* fmt, ...);
void ks_log_info(const char* fmt, ...);
void ks_log_dbg(const char* fmt, ...);

#endif
