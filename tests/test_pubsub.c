#include "ks_pubsub.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
                    #condition);                                                \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

typedef struct {
    ks_pubsub_client_id clients[8];
    size_t count;
    const char* expected_channel;
    const char* expected_message;
    size_t expected_message_length;
} delivery_log_t;

static void record_delivery(ks_pubsub_client_id client_id,
                            const char* channel,
                            const char* message,
                            size_t message_length,
                            void* context) {
    delivery_log_t* log = context;
    CHECK(strcmp(channel, log->expected_channel) == 0);
    CHECK(message_length == log->expected_message_length);
    if (message_length != 0) {
        if (message == NULL) {
            CHECK(message != NULL);
        } else {
            CHECK(memcmp(message, log->expected_message, message_length) == 0);
        }
    }
    CHECK(log->count < sizeof(log->clients) / sizeof(log->clients[0]));
    if (log->count < sizeof(log->clients) / sizeof(log->clients[0])) {
        log->clients[log->count++] = client_id;
    }
}

static int delivered_to(const delivery_log_t* log,
                        ks_pubsub_client_id client_id) {
    for (size_t i = 0; i < log->count; ++i) {
        if (log->clients[i] == client_id) {
            return 1;
        }
    }
    return 0;
}

static void test_subscribe_and_fanout(void) {
    ks_pubsub_t* pubsub = ks_pubsub_create();
    CHECK(pubsub != NULL);
    if (pubsub == NULL) {
        return;
    }

    char mutable_channel[] = "prices";
    CHECK(ks_pubsub_subscribe(pubsub, mutable_channel, 10) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_subscribe(pubsub, "prices", 20) == KS_PUBSUB_OK);
    mutable_channel[0] = 'x';

    CHECK(ks_pubsub_channel_count(pubsub) == 1);
    CHECK(ks_pubsub_subscription_count(pubsub) == 2);

    const char message[] = {'u', 'p', '\0', '!'};
    delivery_log_t log = {
        .expected_channel = "prices",
        .expected_message = message,
        .expected_message_length = sizeof(message),
    };
    CHECK(ks_pubsub_publish(pubsub, "prices", message, sizeof(message),
                            record_delivery, &log) == 2);
    CHECK(log.count == 2);
    CHECK(delivered_to(&log, 10));
    CHECK(delivered_to(&log, 20));

    ks_pubsub_destroy(pubsub);
}

static void test_idempotent_subscription(void) {
    ks_pubsub_t* pubsub = ks_pubsub_create();
    CHECK(pubsub != NULL);
    if (pubsub == NULL) {
        return;
    }

    CHECK(ks_pubsub_subscribe(pubsub, "events", 7) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_subscribe(pubsub, "events", 7) ==
          KS_PUBSUB_ALREADY_SUBSCRIBED);
    CHECK(ks_pubsub_channel_count(pubsub) == 1);
    CHECK(ks_pubsub_subscription_count(pubsub) == 1);

    delivery_log_t log = {
        .expected_channel = "events",
        .expected_message = "",
        .expected_message_length = 0,
    };
    CHECK(ks_pubsub_publish(pubsub, "events", NULL, 0, record_delivery, &log) ==
          1);
    CHECK(log.count == 1);

    ks_pubsub_destroy(pubsub);
}

static void test_unsubscribe_and_channel_cleanup(void) {
    ks_pubsub_t* pubsub = ks_pubsub_create();
    CHECK(pubsub != NULL);
    if (pubsub == NULL) {
        return;
    }

    CHECK(ks_pubsub_subscribe(pubsub, "events", 1) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_subscribe(pubsub, "events", 2) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_unsubscribe(pubsub, "events", 3) ==
          KS_PUBSUB_NOT_SUBSCRIBED);
    CHECK(ks_pubsub_unsubscribe(pubsub, "events", 1) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_channel_count(pubsub) == 1);
    CHECK(ks_pubsub_subscription_count(pubsub) == 1);

    CHECK(ks_pubsub_unsubscribe(pubsub, "events", 2) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_channel_count(pubsub) == 0);
    CHECK(ks_pubsub_subscription_count(pubsub) == 0);
    CHECK(ks_pubsub_unsubscribe(pubsub, "events", 2) ==
          KS_PUBSUB_NOT_SUBSCRIBED);

    ks_pubsub_destroy(pubsub);
}

static void test_remove_client(void) {
    ks_pubsub_t* pubsub = ks_pubsub_create();
    CHECK(pubsub != NULL);
    if (pubsub == NULL) {
        return;
    }

    CHECK(ks_pubsub_subscribe(pubsub, "one", 1) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_subscribe(pubsub, "two", 1) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_subscribe(pubsub, "two", 2) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_subscribe(pubsub, "three", 3) == KS_PUBSUB_OK);
    CHECK(ks_pubsub_remove_client(pubsub, 1) == 2);
    CHECK(ks_pubsub_remove_client(pubsub, 1) == 0);
    CHECK(ks_pubsub_channel_count(pubsub) == 2);
    CHECK(ks_pubsub_subscription_count(pubsub) == 2);

    delivery_log_t log = {
        .expected_channel = "two",
        .expected_message = "message",
        .expected_message_length = 7,
    };
    CHECK(ks_pubsub_publish(pubsub, "two", "message", 7, record_delivery, &log) ==
          1);
    CHECK(log.count == 1);
    CHECK(delivered_to(&log, 2));

    ks_pubsub_destroy(pubsub);
}

static void test_invalid_arguments(void) {
    ks_pubsub_t* pubsub = ks_pubsub_create();
    CHECK(pubsub != NULL);
    if (pubsub == NULL) {
        return;
    }

    CHECK(ks_pubsub_subscribe(NULL, "channel", 1) ==
          KS_PUBSUB_INVALID_ARGUMENT);
    CHECK(ks_pubsub_subscribe(pubsub, NULL, 1) == KS_PUBSUB_INVALID_ARGUMENT);
    CHECK(ks_pubsub_subscribe(pubsub, "", 1) == KS_PUBSUB_INVALID_ARGUMENT);
    CHECK(ks_pubsub_unsubscribe(NULL, "channel", 1) ==
          KS_PUBSUB_INVALID_ARGUMENT);
    CHECK(ks_pubsub_unsubscribe(pubsub, "", 1) == KS_PUBSUB_INVALID_ARGUMENT);
    CHECK(ks_pubsub_publish(pubsub, "channel", "x", 1, NULL, NULL) == 0);
    CHECK(ks_pubsub_publish(pubsub, "channel", NULL, 1, record_delivery, NULL) ==
          0);
    CHECK(ks_pubsub_remove_client(NULL, 1) == 0);
    CHECK(ks_pubsub_channel_count(NULL) == 0);
    CHECK(ks_pubsub_subscription_count(NULL) == 0);

    ks_pubsub_destroy(pubsub);
    ks_pubsub_destroy(NULL);
}

int main(void) {
    test_subscribe_and_fanout();
    test_idempotent_subscription();
    test_unsubscribe_and_channel_cleanup();
    test_remove_client();
    test_invalid_arguments();

    if (failures != 0) {
        fprintf(stderr, "%d pubsub test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    puts("pubsub tests passed");
    return EXIT_SUCCESS;
}
