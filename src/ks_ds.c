#include "ks_ds.h"

#include <stdint.h>
#include <stdlib.h>

#define KS_DS_INITIAL_CAPACITY 8U

static size_t ks_ds_index(uint64_t hash, size_t capacity) {
    return (size_t)(hash & (uint64_t)(capacity - 1U));
}

static void ks_ds_release(const ks_ds_t* table, ks_ds_entry_t* entry) {
    if (table->destroy_key != NULL) {
        table->destroy_key(entry->key);
    }
    if (table->destroy_value != NULL) {
        table->destroy_value(entry->value);
    }
}

static size_t ks_ds_find(const ks_ds_t* table, const void* key, uint64_t hash,
                         bool* found) {
    size_t index = ks_ds_index(hash, table->capacity);
    size_t first_tombstone = SIZE_MAX;

    for (;;) {
        const ks_ds_entry_t* entry = &table->entries[index];
        if (entry->state == KS_DS_EMPTY) {
            *found = false;
            return first_tombstone == SIZE_MAX ? index : first_tombstone;
        }
        if (entry->state == KS_DS_TOMBSTONE) {
            if (first_tombstone == SIZE_MAX) {
                first_tombstone = index;
            }
        } else if (entry->hash == hash && table->equal(entry->key, key)) {
            *found = true;
            return index;
        }
        index = (index + 1U) & (table->capacity - 1U);
    }
}

static bool ks_ds_rebuild(ks_ds_t* table, size_t capacity) {
    ks_ds_entry_t* entries = calloc(capacity, sizeof(*entries));
    if (entries == NULL) {
        return false;
    }

    ks_ds_entry_t* old_entries = table->entries;
    size_t old_capacity = table->capacity;
    table->entries = entries;
    table->capacity = capacity;
    table->occupied = table->size;

    for (size_t i = 0; i < old_capacity; ++i) {
        ks_ds_entry_t entry = old_entries[i];
        if (entry.state != KS_DS_OCCUPIED) {
            continue;
        }
        bool found = false;
        size_t index = ks_ds_find(table, entry.key, entry.hash, &found);
        table->entries[index] = entry;
    }
    free(old_entries);
    return true;
}

static bool ks_ds_prepare_insert(ks_ds_t* table) {
    if ((table->occupied + 1U) * 10U <= table->capacity * 7U) {
        return true;
    }

    size_t capacity = table->capacity;
    if ((table->size + 1U) * 10U > table->capacity * 7U) {
        if (capacity > SIZE_MAX / 2U) {
            return false;
        }
        capacity *= 2U;
    }
    return ks_ds_rebuild(table, capacity);
}

bool ks_ds_init(ks_ds_t* table, ks_ds_hash_fn hash, ks_ds_equal_fn equal,
                ks_ds_destroy_fn destroy_key,
                ks_ds_destroy_fn destroy_value) {
    if (table == NULL || hash == NULL || equal == NULL) {
        return false;
    }

    ks_ds_entry_t* entries = calloc(KS_DS_INITIAL_CAPACITY, sizeof(*entries));
    if (entries == NULL) {
        return false;
    }

    *table = (ks_ds_t){
        .entries = entries,
        .capacity = KS_DS_INITIAL_CAPACITY,
        .hash = hash,
        .equal = equal,
        .destroy_key = destroy_key,
        .destroy_value = destroy_value,
    };
    return true;
}

void ks_ds_destroy(ks_ds_t* table) {
    if (table == NULL) {
        return;
    }

    for (size_t i = 0; i < table->capacity; ++i) {
        if (table->entries[i].state == KS_DS_OCCUPIED) {
            ks_ds_release(table, &table->entries[i]);
        }
    }
    free(table->entries);
    *table = (ks_ds_t){0};
}

ks_ds_put_result_t ks_ds_put(ks_ds_t* table, void* key, void* value) {
    if (table == NULL || table->entries == NULL || key == NULL) {
        return KS_DS_PUT_ERROR;
    }

    uint64_t hash = table->hash(key);
    bool found = false;
    size_t index = ks_ds_find(table, key, hash, &found);
    if (found) {
        ks_ds_entry_t* entry = &table->entries[index];
        ks_ds_release(table, entry);
        entry->key = key;
        entry->value = value;
        entry->hash = hash;
        return KS_DS_PUT_REPLACED;
    }

    if (!ks_ds_prepare_insert(table)) {
        return KS_DS_PUT_ERROR;
    }
    index = ks_ds_find(table, key, hash, &found);

    ks_ds_entry_t* entry = &table->entries[index];
    if (entry->state == KS_DS_EMPTY) {
        ++table->occupied;
    }
    *entry = (ks_ds_entry_t){
        .hash = hash,
        .key = key,
        .value = value,
        .state = KS_DS_OCCUPIED,
    };
    ++table->size;
    return KS_DS_PUT_INSERTED;
}

void* ks_ds_get(const ks_ds_t* table, const void* key) {
    if (table == NULL || table->entries == NULL || key == NULL) {
        return NULL;
    }

    uint64_t hash = table->hash(key);
    bool found = false;
    size_t index = ks_ds_find(table, key, hash, &found);
    return found ? table->entries[index].value : NULL;
}

bool ks_ds_contains(const ks_ds_t* table, const void* key) {
    if (table == NULL || table->entries == NULL || key == NULL) {
        return false;
    }

    uint64_t hash = table->hash(key);
    bool found = false;
    (void)ks_ds_find(table, key, hash, &found);
    return found;
}

bool ks_ds_remove(ks_ds_t* table, const void* key) {
    if (table == NULL || table->entries == NULL || key == NULL) {
        return false;
    }

    uint64_t hash = table->hash(key);
    bool found = false;
    size_t index = ks_ds_find(table, key, hash, &found);
    if (!found) {
        return false;
    }

    ks_ds_entry_t* entry = &table->entries[index];
    ks_ds_release(table, entry);
    *entry = (ks_ds_entry_t){.state = KS_DS_TOMBSTONE};
    --table->size;
    return true;
}

size_t ks_ds_size(const ks_ds_t* table) {
    return table == NULL ? 0U : table->size;
}

size_t ks_ds_capacity(const ks_ds_t* table) {
    return table == NULL ? 0U : table->capacity;
}
