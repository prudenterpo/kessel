#ifndef KS_DS_H
#define KS_DS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t (*ks_ds_hash_fn)(const void* key);
typedef bool (*ks_ds_equal_fn)(const void* left, const void* right);
typedef void (*ks_ds_destroy_fn)(void* value);

typedef enum {
    KS_DS_EMPTY = 0,
    KS_DS_OCCUPIED,
    KS_DS_TOMBSTONE
} ks_ds_slot_state_t;

typedef struct {
    uint64_t hash;
    void* key;
    void* value;
    ks_ds_slot_state_t state;
} ks_ds_entry_t;

typedef struct {
    ks_ds_entry_t* entries;
    size_t capacity;
    size_t size;
    size_t occupied;
    ks_ds_hash_fn hash;
    ks_ds_equal_fn equal;
    ks_ds_destroy_fn destroy_key;
    ks_ds_destroy_fn destroy_value;
} ks_ds_t;

typedef enum {
    KS_DS_PUT_ERROR = 0,
    KS_DS_PUT_INSERTED,
    KS_DS_PUT_REPLACED
} ks_ds_put_result_t;

/*
 * Initializes an empty table. The callbacks must remain valid for the table's
 * lifetime. Destroy callbacks may be NULL.
 */
bool ks_ds_init(ks_ds_t* table, ks_ds_hash_fn hash, ks_ds_equal_fn equal,
                ks_ds_destroy_fn destroy_key,
                ks_ds_destroy_fn destroy_value);

void ks_ds_destroy(ks_ds_t* table);

/*
 * On success, the table takes ownership of key and value. On replacement, the
 * previously stored key and value are destroyed. On error, ownership remains
 * with the caller.
 */
ks_ds_put_result_t ks_ds_put(ks_ds_t* table, void* key, void* value);

void* ks_ds_get(const ks_ds_t* table, const void* key);
bool ks_ds_contains(const ks_ds_t* table, const void* key);
bool ks_ds_remove(ks_ds_t* table, const void* key);

size_t ks_ds_size(const ks_ds_t* table);
size_t ks_ds_capacity(const ks_ds_t* table);

#endif
