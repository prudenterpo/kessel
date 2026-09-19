#ifndef KS_TTL_H
#define KS_TTL_H

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t (*ks_ttl_now_fn)(void* context);

typedef struct {
    ks_ttl_now_fn now;
    void* context;
} ks_ttl_clock_t;

ks_ttl_clock_t ks_ttl_system_clock(void);
uint64_t ks_ttl_now(const ks_ttl_clock_t* clock);
bool ks_ttl_deadline(uint64_t now_ms, uint64_t ttl_seconds,
                     uint64_t* deadline_ms);

#endif
