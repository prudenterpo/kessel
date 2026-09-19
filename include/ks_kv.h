#ifndef KS_KV_H
#define KS_KV_H

#include "ks_ds.h"
#include "ks_ttl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    ks_ds_t entries;
    ks_ttl_clock_t clock;
    size_t reaper_cursor;
} ks_kv_t;

typedef enum {
    KS_KV_SET_ERROR = 0,
    KS_KV_SET_INSERTED,
    KS_KV_SET_REPLACED
} ks_kv_set_result_t;

typedef enum {
    KS_KV_EXPIRE_ERROR = -1,
    KS_KV_EXPIRE_MISSING = 0,
    KS_KV_EXPIRE_UPDATED = 1
} ks_kv_expire_result_t;

bool ks_kv_init(ks_kv_t* store);
bool ks_kv_init_with_clock(ks_kv_t* store, ks_ttl_clock_t clock);
void ks_kv_destroy(ks_kv_t* store);

/* The store copies key and value; caller-owned input remains unchanged. */
ks_kv_set_result_t ks_kv_set(ks_kv_t* store, const char* key,
                             const char* value, size_t value_size);
ks_kv_set_result_t ks_kv_set_ex(ks_kv_t* store, const char* key,
                                const char* value, size_t value_size,
                                uint64_t ttl_seconds);

/* The returned value remains owned by the store and is invalidated by writes. */
const char* ks_kv_get(ks_kv_t* store, const char* key,
                      size_t* value_size);

bool ks_kv_delete(ks_kv_t* store, const char* key);
bool ks_kv_exists(ks_kv_t* store, const char* key);
ks_kv_expire_result_t ks_kv_expire(ks_kv_t* store, const char* key,
                                   uint64_t ttl_seconds);
int64_t ks_kv_ttl(ks_kv_t* store, const char* key);

/* Examines at most budget hash-table slots and returns removed key count. */
size_t ks_kv_reap(ks_kv_t* store, size_t budget);
size_t ks_kv_size(ks_kv_t* store);

#endif
