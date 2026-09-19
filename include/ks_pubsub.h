#ifndef KS_PUBSUB_H
#define KS_PUBSUB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ks_pubsub ks_pubsub_t;
typedef uint64_t ks_pubsub_client_id;

typedef enum {
    KS_PUBSUB_OK = 0,
    KS_PUBSUB_ALREADY_SUBSCRIBED,
    KS_PUBSUB_NOT_SUBSCRIBED,
    KS_PUBSUB_NO_MEMORY,
    KS_PUBSUB_INVALID_ARGUMENT
} ks_pubsub_status;

/*
 * Called once for each subscriber present when a message is published.
 * Return true when the message was accepted by the client. The channel and
 * message remain owned by the caller/registry and are only
 * valid for the duration of the callback. The callback must not mutate the
 * registry that initiated the publish operation.
 */
typedef bool (*ks_pubsub_delivery_fn)(ks_pubsub_client_id client_id,
                                      const char* channel,
                                      const char* message,
                                      size_t message_length,
                                      void* context);

ks_pubsub_t* ks_pubsub_create(void);
void ks_pubsub_destroy(ks_pubsub_t* pubsub);

/* The registry copies and owns channel names. Empty channel names are invalid. */
ks_pubsub_status ks_pubsub_subscribe(ks_pubsub_t* pubsub,
                                     const char* channel,
                                     ks_pubsub_client_id client_id);
ks_pubsub_status ks_pubsub_unsubscribe(ks_pubsub_t* pubsub,
                                       const char* channel,
                                       ks_pubsub_client_id client_id);

/* Removes every subscription for a client and returns the number removed. */
size_t ks_pubsub_remove_client(ks_pubsub_t* pubsub,
                               ks_pubsub_client_id client_id);

/*
 * Delivers a message to the current subscribers and returns the number of
 * callbacks that accepted it. The message may be NULL only when
 * message_length is zero.
 */
size_t ks_pubsub_publish(const ks_pubsub_t* pubsub,
                         const char* channel,
                         const char* message,
                         size_t message_length,
                         ks_pubsub_delivery_fn deliver,
                         void* context);

size_t ks_pubsub_channel_count(const ks_pubsub_t* pubsub);
size_t ks_pubsub_subscription_count(const ks_pubsub_t* pubsub);

#endif
