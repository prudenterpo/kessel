#include "ks_kv.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t size;
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

static ks_kv_value_t* ks_kv_copy_value(const char* value, size_t value_size) {
    if (value_size > SIZE_MAX - sizeof(ks_kv_value_t) - 1U) {
        return NULL;
    }
    ks_kv_value_t* copy = malloc(sizeof(*copy) + value_size + 1U);
    if (copy == NULL) {
        return NULL;
    }
    copy->size = value_size;
    if (value_size != 0U) {
        memcpy(copy->data, value, value_size);
    }
    copy->data[value_size] = '\0';
    return copy;
}

bool ks_kv_init(ks_kv_t* store) {
    if (store == NULL) {
        return false;
    }
    return ks_ds_init(&store->entries, ks_kv_hash, ks_kv_equal, free, free);
}

void ks_kv_destroy(ks_kv_t* store) {
    if (store != NULL) {
        ks_ds_destroy(&store->entries);
    }
}

ks_kv_set_result_t ks_kv_set(ks_kv_t* store, const char* key,
                             const char* value, size_t value_size) {
    if (store == NULL || key == NULL || (value == NULL && value_size != 0U)) {
        return KS_KV_SET_ERROR;
    }

    char* key_copy = ks_kv_copy_key(key);
    ks_kv_value_t* value_copy = ks_kv_copy_value(value, value_size);
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

const char* ks_kv_get(const ks_kv_t* store, const char* key,
                      size_t* value_size) {
    if (value_size != NULL) {
        *value_size = 0U;
    }
    if (store == NULL || key == NULL) {
        return NULL;
    }

    const ks_kv_value_t* value = ks_ds_get(&store->entries, key);
    if (value == NULL) {
        return NULL;
    }
    if (value_size != NULL) {
        *value_size = value->size;
    }
    return value->data;
}

bool ks_kv_delete(ks_kv_t* store, const char* key) {
    return store != NULL && ks_ds_remove(&store->entries, key);
}

bool ks_kv_exists(const ks_kv_t* store, const char* key) {
    return store != NULL && ks_ds_contains(&store->entries, key);
}

size_t ks_kv_size(const ks_kv_t* store) {
    return store == NULL ? 0U : ks_ds_size(&store->entries);
}
