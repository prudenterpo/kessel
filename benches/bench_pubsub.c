#define _POSIX_C_SOURCE 200809L

#include "ks_pubsub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    KS_BENCH_SUBSCRIBERS = 64,
    KS_BENCH_PUBLISHES = 20000
};

static bool accept_message(ks_pubsub_client_id client_id, const char* channel,
                           const char* message, size_t message_length,
                           void* context) {
    (void)client_id;
    (void)channel;
    (void)message;
    (void)message_length;
    size_t* delivered = context;
    ++(*delivered);
    return true;
}

static double elapsed_seconds(struct timespec start, struct timespec end) {
    return (double)(end.tv_sec - start.tv_sec) +
           (double)(end.tv_nsec - start.tv_nsec) / 1000000000.0;
}

int main(void) {
    ks_pubsub_t* pubsub = ks_pubsub_create();
    if (pubsub == NULL) {
        fprintf(stderr, "bench_pubsub: failed to create registry\n");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < KS_BENCH_SUBSCRIBERS; ++i) {
        if (ks_pubsub_subscribe(pubsub, "bench", (ks_pubsub_client_id)(i + 1)) !=
            KS_PUBSUB_OK) {
            ks_pubsub_destroy(pubsub);
            return EXIT_FAILURE;
        }
    }

    const char payload[] = "payload";
    size_t delivered = 0;
    struct timespec start;
    struct timespec end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < KS_BENCH_PUBLISHES; ++i) {
        size_t accepted = ks_pubsub_publish(
            pubsub, "bench", payload, sizeof(payload) - 1U, accept_message,
            &delivered);
        if (accepted != (size_t)KS_BENCH_SUBSCRIBERS) {
            ks_pubsub_destroy(pubsub);
            return EXIT_FAILURE;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    ks_pubsub_destroy(pubsub);

    double seconds = elapsed_seconds(start, end);
    if (seconds <= 0.0) {
        seconds = 0.000001;
    }
    int ops = KS_BENCH_PUBLISHES;
    puts("metric,ops,seconds,ops_per_sec,deliveries");
    printf("pubsub_publish,%d,%.6f,%.0f,%zu\n", ops, seconds,
           (double)ops / seconds, delivered);
    return EXIT_SUCCESS;
}
