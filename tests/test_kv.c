#include "ks_ds.h"
#include "ks_kv.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int destroyed_keys = 0;
static int destroyed_values = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                                \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static char* copy_string(const char* value) {
    size_t size = strlen(value) + 1U;
    char* copy = malloc(size);
    CHECK(copy != NULL);
    if (copy != NULL) {
        memcpy(copy, value, size);
    }
    return copy;
}

static uint64_t constant_hash(const void* key) {
    (void)key;
    return UINT64_C(7);
}

static bool string_equal(const void* left, const void* right) {
    return strcmp(left, right) == 0;
}

static void destroy_key(void* key) {
    ++destroyed_keys;
    free(key);
}

static void destroy_value(void* value) {
    ++destroyed_values;
    free(value);
}

static void test_kv_crud_and_copy_ownership(void) {
    ks_kv_t store;
    CHECK(ks_kv_init(&store));

    char key[] = "animal";
    char value[] = "cat";
    CHECK(ks_kv_set(&store, key, value, strlen(value)) == KS_KV_SET_INSERTED);
    key[0] = 'x';
    value[0] = 'r';

    size_t size = 0U;
    const char* stored = ks_kv_get(&store, "animal", &size);
    CHECK(stored != NULL);
    CHECK(size == 3U);
    CHECK(stored != NULL && memcmp(stored, "cat", 3U) == 0);
    CHECK(ks_kv_exists(&store, "animal"));
    CHECK(ks_kv_size(&store) == 1U);

    CHECK(ks_kv_set(&store, "animal", "capybara", 8U) ==
          KS_KV_SET_REPLACED);
    stored = ks_kv_get(&store, "animal", &size);
    CHECK(size == 8U);
    CHECK(stored != NULL && memcmp(stored, "capybara", 8U) == 0);
    CHECK(ks_kv_size(&store) == 1U);

    CHECK(ks_kv_delete(&store, "animal"));
    CHECK(!ks_kv_delete(&store, "animal"));
    CHECK(!ks_kv_exists(&store, "animal"));
    CHECK(ks_kv_get(&store, "animal", &size) == NULL);
    CHECK(size == 0U);
    ks_kv_destroy(&store);
}

static void test_empty_and_binary_values(void) {
    ks_kv_t store;
    CHECK(ks_kv_init(&store));
    CHECK(ks_kv_set(&store, "empty", NULL, 0U) == KS_KV_SET_INSERTED);
    const char bytes[] = {'a', '\0', 'b'};
    CHECK(ks_kv_set(&store, "bytes", bytes, sizeof(bytes)) ==
          KS_KV_SET_INSERTED);

    size_t size = 99U;
    const char* empty = ks_kv_get(&store, "empty", &size);
    CHECK(empty != NULL);
    CHECK(size == 0U);
    const char* stored = ks_kv_get(&store, "bytes", &size);
    CHECK(stored != NULL);
    CHECK(size == sizeof(bytes));
    CHECK(stored != NULL && memcmp(stored, bytes, sizeof(bytes)) == 0);
    ks_kv_destroy(&store);
}

static void test_collisions_tombstones_and_ownership(void) {
    destroyed_keys = 0;
    destroyed_values = 0;
    ks_ds_t table;
    CHECK(ks_ds_init(&table, constant_hash, string_equal, destroy_key,
                     destroy_value));

    CHECK(ks_ds_put(&table, copy_string("a"), copy_string("one")) ==
          KS_DS_PUT_INSERTED);
    CHECK(ks_ds_put(&table, copy_string("b"), copy_string("two")) ==
          KS_DS_PUT_INSERTED);
    CHECK(ks_ds_put(&table, copy_string("c"), copy_string("three")) ==
          KS_DS_PUT_INSERTED);
    CHECK(strcmp(ks_ds_get(&table, "b"), "two") == 0);

    CHECK(ks_ds_remove(&table, "b"));
    CHECK(destroyed_keys == 1);
    CHECK(destroyed_values == 1);
    CHECK(ks_ds_get(&table, "c") != NULL);
    CHECK(ks_ds_put(&table, copy_string("d"), copy_string("four")) ==
          KS_DS_PUT_INSERTED);
    CHECK(strcmp(ks_ds_get(&table, "d"), "four") == 0);

    CHECK(ks_ds_put(&table, copy_string("a"), copy_string("new")) ==
          KS_DS_PUT_REPLACED);
    CHECK(destroyed_keys == 2);
    CHECK(destroyed_values == 2);
    CHECK(strcmp(ks_ds_get(&table, "a"), "new") == 0);

    ks_ds_destroy(&table);
    CHECK(destroyed_keys == 5);
    CHECK(destroyed_values == 5);
}

static void test_resize_preserves_entries(void) {
    ks_ds_t table;
    CHECK(ks_ds_init(&table, constant_hash, string_equal, free, free));
    size_t initial_capacity = ks_ds_capacity(&table);

    char key[32];
    char value[32];
    for (int i = 0; i < 100; ++i) {
        (void)snprintf(key, sizeof(key), "key-%d", i);
        (void)snprintf(value, sizeof(value), "value-%d", i);
        CHECK(ks_ds_put(&table, copy_string(key), copy_string(value)) ==
              KS_DS_PUT_INSERTED);
    }
    CHECK(ks_ds_capacity(&table) > initial_capacity);
    CHECK(ks_ds_size(&table) == 100U);

    for (int i = 0; i < 100; ++i) {
        (void)snprintf(key, sizeof(key), "key-%d", i);
        (void)snprintf(value, sizeof(value), "value-%d", i);
        const char* stored = ks_ds_get(&table, key);
        CHECK(stored != NULL);
        CHECK(stored != NULL && strcmp(stored, value) == 0);
    }
    ks_ds_destroy(&table);
}

static void test_invalid_inputs(void) {
    ks_kv_t store;
    CHECK(!ks_kv_init(NULL));
    CHECK(ks_kv_init(&store));
    CHECK(ks_kv_set(NULL, "key", "value", 5U) == KS_KV_SET_ERROR);
    CHECK(ks_kv_set(&store, NULL, "value", 5U) == KS_KV_SET_ERROR);
    CHECK(ks_kv_set(&store, "key", NULL, 1U) == KS_KV_SET_ERROR);
    CHECK(ks_kv_get(NULL, "key", NULL) == NULL);
    CHECK(!ks_kv_exists(&store, NULL));
    CHECK(!ks_kv_delete(NULL, "key"));
    ks_kv_destroy(&store);
    ks_kv_destroy(NULL);
}

int main(void) {
    test_kv_crud_and_copy_ownership();
    test_empty_and_binary_values();
    test_collisions_tombstones_and_ownership();
    test_resize_preserves_entries();
    test_invalid_inputs();

    if (failures != 0) {
        fprintf(stderr, "%d key-value test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("key-value tests passed");
    return EXIT_SUCCESS;
}
