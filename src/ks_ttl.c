#include "ks_ttl.h"

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

static uint64_t system_now(void* context) {
    (void)context;
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec time = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0) {
        return 0U;
    }
    return (uint64_t)time.tv_sec * UINT64_C(1000) +
           (uint64_t)time.tv_nsec / UINT64_C(1000000);
#endif
}

ks_ttl_clock_t ks_ttl_system_clock(void) {
    return (ks_ttl_clock_t){.now = system_now, .context = NULL};
}

uint64_t ks_ttl_now(const ks_ttl_clock_t* clock) {
    return clock == NULL || clock->now == NULL ? 0U
                                               : clock->now(clock->context);
}

bool ks_ttl_deadline(uint64_t now_ms, uint64_t ttl_seconds,
                     uint64_t* deadline_ms) {
    if (deadline_ms == NULL || ttl_seconds == 0U ||
        ttl_seconds > (UINT64_MAX - now_ms) / UINT64_C(1000)) {
        return false;
    }
    *deadline_ms = now_ms + ttl_seconds * UINT64_C(1000);
    return true;
}
