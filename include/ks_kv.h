#ifndef KS_KV_H
#define KS_KV_H

#include "ks_ds.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    ks_ds_t entries;
} ks_kv_t;

typedef enum {
    KS_KV_SET_ERROR = 0,
    KS_KV_SET_INSERTED,
    KS_KV_SET_REPLACED
} ks_kv_set_result_t;

bool ks_kv_init(ks_kv_t* store);
void ks_kv_destroy(ks_kv_t* store);

/* The store copies key and value; caller-owned input remains unchanged. */
ks_kv_set_result_t ks_kv_set(ks_kv_t* store, const char* key,
                             const char* value, size_t value_size);

/* The returned value remains owned by the store and is invalidated by writes. */
const char* ks_kv_get(const ks_kv_t* store, const char* key,
                      size_t* value_size);

bool ks_kv_delete(ks_kv_t* store, const char* key);
bool ks_kv_exists(const ks_kv_t* store, const char* key);
size_t ks_kv_size(const ks_kv_t* store);

#endif
