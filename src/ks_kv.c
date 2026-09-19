#include "ks_kv.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t size;
    uint64_t deadline_ms;
    char data[];
} ks_kv_value_t;

static uint64_t ks_kv_hash(const void* key) {
    const unsigned char* bytes = key;
    uint64_t hash = UINT64_C(14695981039346656037);
    while (*bytes != '\0') {
        hash ^= *bytes++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool ks_kv_equal(const void* left, const void* right) {
    return strcmp(left, right) == 0;
}

static char* ks_kv_copy_key(const char* key) {
    size_t size = strlen(key) + 1U;
    char* copy = malloc(size);
    if (copy != NULL) {
        memcpy(copy, key, size);
    }
    return copy;
}

static ks_kv_value_t* ks_kv_copy_value(const char* value, size_t value_size,
                                       uint64_t deadline_ms) {
    if (value_size > SIZE_MAX - sizeof(ks_kv_value_t) - 1U) {
        return NULL;
    }
    ks_kv_value_t* copy = malloc(sizeof(*copy) + value_size + 1U);
    if (copy == NULL) {
        return NULL;
    }
    copy->size = value_size;
    copy->deadline_ms = deadline_ms;
    if (value_size != 0U) {
        memcpy(copy->data, value, value_size);
    }
    copy->data[value_size] = '\0';
    return copy;
}

bool ks_kv_init(ks_kv_t* store) {
    return ks_kv_init_with_clock(store, ks_ttl_system_clock());
}

bool ks_kv_init_with_clock(ks_kv_t* store, ks_ttl_clock_t clock) {
    if (store == NULL) {
        return false;
    }
    if (clock.now == NULL ||
        !ks_ds_init(&store->entries, ks_kv_hash, ks_kv_equal, free, free)) {
        return false;
    }
    store->clock = clock;
    store->reaper_cursor = 0U;
    return true;
}

void ks_kv_destroy(ks_kv_t* store) {
    if (store != NULL) {
        ks_ds_destroy(&store->entries);
    }
}

ks_kv_set_result_t ks_kv_set(ks_kv_t* store, const char* key,
                             const char* value, size_t value_size) {
    return ks_kv_set_ex(store, key, value, value_size, 0U);
}

ks_kv_set_result_t ks_kv_set_ex(ks_kv_t* store, const char* key,
                                const char* value, size_t value_size,
                                uint64_t ttl_seconds) {
    if (store == NULL || key == NULL || (value == NULL && value_size != 0U)) {
        return KS_KV_SET_ERROR;
    }

    uint64_t deadline_ms = 0U;
    if (ttl_seconds != 0U &&
        !ks_ttl_deadline(ks_ttl_now(&store->clock), ttl_seconds,
                         &deadline_ms)) {
        return KS_KV_SET_ERROR;
    }

    char* key_copy = ks_kv_copy_key(key);
    ks_kv_value_t* value_copy =
        ks_kv_copy_value(value, value_size, deadline_ms);
    if (key_copy == NULL || value_copy == NULL) {
        free(key_copy);
        free(value_copy);
        return KS_KV_SET_ERROR;
    }

    ks_ds_put_result_t result = ks_ds_put(&store->entries, key_copy, value_copy);
    if (result == KS_DS_PUT_ERROR) {
        free(key_copy);
        free(value_copy);
        return KS_KV_SET_ERROR;
    }
    return result == KS_DS_PUT_INSERTED ? KS_KV_SET_INSERTED
                                        : KS_KV_SET_REPLACED;
}

static ks_kv_value_t* ks_kv_find_live(ks_kv_t* store, const char* key) {
    if (store == NULL || key == NULL) {
        return NULL;
    }
    ks_kv_value_t* value = ks_ds_get(&store->entries, key);
    if (value != NULL && value->deadline_ms != 0U &&
        value->deadline_ms <= ks_ttl_now(&store->clock)) {
        (void)ks_ds_remove(&store->entries, key);
        return NULL;
    }
    return value;
}

const char* ks_kv_get(ks_kv_t* store, const char* key,
                      size_t* value_size) {
    if (value_size != NULL) {
        *value_size = 0U;
    }
    const ks_kv_value_t* value = ks_kv_find_live(store, key);
    if (value == NULL) {
        return NULL;
    }
    if (value_size != NULL) {
        *value_size = value->size;
    }
    return value->data;
}

bool ks_kv_delete(ks_kv_t* store, const char* key) {
    return ks_kv_find_live(store, key) != NULL &&
           ks_ds_remove(&store->entries, key);
}

bool ks_kv_exists(ks_kv_t* store, const char* key) {
    return ks_kv_find_live(store, key) != NULL;
}

ks_kv_expire_result_t ks_kv_expire(ks_kv_t* store, const char* key,
                                   uint64_t ttl_seconds) {
    ks_kv_value_t* value = ks_kv_find_live(store, key);
    if (value == NULL) {
        return KS_KV_EXPIRE_MISSING;
    }
    if (ttl_seconds == 0U) {
        return ks_ds_remove(&store->entries, key) ? KS_KV_EXPIRE_UPDATED
                                                  : KS_KV_EXPIRE_MISSING;
    }
    uint64_t deadline_ms = 0U;
    if (!ks_ttl_deadline(ks_ttl_now(&store->clock), ttl_seconds,
                         &deadline_ms)) {
        return KS_KV_EXPIRE_ERROR;
    }
    value->deadline_ms = deadline_ms;
    return KS_KV_EXPIRE_UPDATED;
}

int64_t ks_kv_ttl(ks_kv_t* store, const char* key) {
    ks_kv_value_t* value = ks_kv_find_live(store, key);
    if (value == NULL) {
        return -2;
    }
    if (value->deadline_ms == 0U) {
        return -1;
    }
    uint64_t now_ms = ks_ttl_now(&store->clock);
    if (value->deadline_ms <= now_ms) {
        (void)ks_ds_remove(&store->entries, key);
        return -2;
    }
    return (int64_t)((value->deadline_ms - now_ms) / UINT64_C(1000));
}

size_t ks_kv_reap(ks_kv_t* store, size_t budget) {
    if (store == NULL || store->entries.entries == NULL || budget == 0U) {
        return 0U;
    }

    size_t removed = 0U;
    uint64_t now_ms = ks_ttl_now(&store->clock);
    for (size_t scanned = 0U; scanned < budget; ++scanned) {
        if (store->reaper_cursor >= store->entries.capacity) {
            store->reaper_cursor = 0U;
        }
        ks_ds_entry_t* entry = &store->entries.entries[store->reaper_cursor++];
        if (entry->state != KS_DS_OCCUPIED) {
            continue;
        }
        const ks_kv_value_t* value = entry->value;
        if (value->deadline_ms != 0U && value->deadline_ms <= now_ms) {
            const char* key = entry->key;
            if (ks_ds_remove(&store->entries, key)) {
                ++removed;
            }
        }
    }
    return removed;
}

size_t ks_kv_size(ks_kv_t* store) {
    if (store == NULL) {
        return 0U;
    }
    (void)ks_kv_reap(store, store->entries.capacity);
    return ks_ds_size(&store->entries);
}
