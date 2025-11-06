#include "ks_log.h"
#include <stdarg.h>
#include <time.h>

static int KS_LVL = KS_LOG_INFO;

void ks_log_set_level(int level) {
    KS_LVL = level;
}

static void vlog(const int lvl, const char* tag, const char* fmt, const va_list ap) {
    if (lvl > KS_LVL) return;
    time_t t = time(NULL);
    struct tm tmv;
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmv);
    fprintf(stderr, "[%s][%s] ", buf, tag);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

void ks_log_err (const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(KS_LOG_ERROR, "ERROR", fmt, ap);
    va_end(ap);
}

void ks_log_warn(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(KS_LOG_WARN, "WARN", fmt, ap);
    va_end(ap);
}

void ks_log_info(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(KS_LOG_INFO, "INFO", fmt, ap);
    va_end(ap);
}

void ks_log_dbg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlog(KS_LOG_DEBUG, "DEBUG", fmt, ap);
    va_end(ap);
}



