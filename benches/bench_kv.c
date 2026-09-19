#define _POSIX_C_SOURCE 200809L

#include "ks_kv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { KS_BENCH_OPS = 100000 };

static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

static int run_phase(const char* name, int (*body)(ks_kv_t*)) {
    ks_kv_t store;
    if (!ks_kv_init(&store)) {
        fprintf(stderr, "bench_kv: failed to init store\n");
        return 1;
    }

    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int status = body(&store);
    clock_gettime(CLOCK_MONOTONIC, &end);
    ks_kv_destroy(&store);
    if (status != 0) {
        return 1;
    }

    double seconds = elapsed_seconds(start, end);
    if (seconds <= 0.0) {
        seconds = 0.000001;
    }
    printf("%s,%d,%.6f,%.0f\n", name, KS_BENCH_OPS, seconds,
           (double)KS_BENCH_OPS / seconds);
    return 0;
}

static int insert_keys(ks_kv_t* store) {
    char key[32];
    char value[32];
    for (int i = 0; i < KS_BENCH_OPS; ++i) {
        snprintf(key, sizeof(key), "k%d", i);
        snprintf(value, sizeof(value), "v%d", i);
        if (ks_kv_set(store, key, value, strlen(value)) == KS_KV_SET_ERROR) {
            return 1;
        }
    }
    return 0;
}

static int lookup_keys(ks_kv_t* store) {
    if (insert_keys(store) != 0) {
        return 1;
    }
    char key[32];
    for (int i = 0; i < KS_BENCH_OPS; ++i) {
        snprintf(key, sizeof(key), "k%d", i);
        size_t size = 0;
        if (ks_kv_get(store, key, &size) == NULL) {
            return 1;
        }
    }
    return 0;
}

static int delete_keys(ks_kv_t* store) {
    if (insert_keys(store) != 0) {
        return 1;
    }
    char key[32];
    for (int i = 0; i < KS_BENCH_OPS; ++i) {
        snprintf(key, sizeof(key), "k%d", i);
        if (!ks_kv_delete(store, key)) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    puts("metric,ops,seconds,ops_per_sec");
    if (run_phase("kv_set", insert_keys) != 0) {
        return EXIT_FAILURE;
    }
    if (run_phase("kv_get", lookup_keys) != 0) {
        return EXIT_FAILURE;
    }
    if (run_phase("kv_del", delete_keys) != 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
