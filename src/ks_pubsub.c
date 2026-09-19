#include "ks_pubsub.h"

#include <stdlib.h>
#include <string.h>

typedef struct ks_pubsub_subscriber {
    ks_pubsub_client_id client_id;
    struct ks_pubsub_subscriber* next;
} ks_pubsub_subscriber_t;

typedef struct ks_pubsub_channel {
    char* name;
    ks_pubsub_subscriber_t* subscribers;
    struct ks_pubsub_channel* next;
} ks_pubsub_channel_t;

struct ks_pubsub {
    ks_pubsub_channel_t* channels;
    size_t channel_count;
    size_t subscription_count;
};

static ks_pubsub_channel_t* find_channel(const ks_pubsub_t* pubsub,
                                         const char* channel) {
    for (ks_pubsub_channel_t* current = pubsub->channels; current != NULL;
         current = current->next) {
        if (strcmp(current->name, channel) == 0) {
            return current;
        }
    }
    return NULL;
}

static char* copy_string(const char* source) {
    size_t length = strlen(source);
    char* copy = malloc(length + 1U);
    if (copy != NULL) {
        memcpy(copy, source, length + 1U);
    }
    return copy;
}

static void destroy_channel(ks_pubsub_channel_t* channel) {
    ks_pubsub_subscriber_t* subscriber = channel->subscribers;
    while (subscriber != NULL) {
        ks_pubsub_subscriber_t* next = subscriber->next;
        free(subscriber);
        subscriber = next;
    }
    free(channel->name);
    free(channel);
}

ks_pubsub_t* ks_pubsub_create(void) {
    return calloc(1U, sizeof(ks_pubsub_t));
}

void ks_pubsub_destroy(ks_pubsub_t* pubsub) {
    if (pubsub == NULL) {
        return;
    }

    ks_pubsub_channel_t* channel = pubsub->channels;
    while (channel != NULL) {
        ks_pubsub_channel_t* next = channel->next;
        destroy_channel(channel);
        channel = next;
    }
    free(pubsub);
}

ks_pubsub_status ks_pubsub_subscribe(ks_pubsub_t* pubsub,
                                     const char* channel,
                                     ks_pubsub_client_id client_id) {
    if (pubsub == NULL || channel == NULL || channel[0] == '\0') {
        return KS_PUBSUB_INVALID_ARGUMENT;
    }

    ks_pubsub_channel_t* existing_channel = find_channel(pubsub, channel);
    if (existing_channel != NULL) {
        for (ks_pubsub_subscriber_t* current = existing_channel->subscribers;
             current != NULL; current = current->next) {
            if (current->client_id == client_id) {
                return KS_PUBSUB_ALREADY_SUBSCRIBED;
            }
        }

        ks_pubsub_subscriber_t* subscriber = malloc(sizeof(*subscriber));
        if (subscriber == NULL) {
            return KS_PUBSUB_NO_MEMORY;
        }
        *subscriber = (ks_pubsub_subscriber_t){
            .client_id = client_id,
            .next = existing_channel->subscribers,
        };
        existing_channel->subscribers = subscriber;
        pubsub->subscription_count++;
        return KS_PUBSUB_OK;
    }

    char* channel_name = copy_string(channel);
    ks_pubsub_channel_t* new_channel = malloc(sizeof(*new_channel));
    ks_pubsub_subscriber_t* subscriber = malloc(sizeof(*subscriber));
    if (channel_name == NULL || new_channel == NULL || subscriber == NULL) {
        free(channel_name);
        free(new_channel);
        free(subscriber);
        return KS_PUBSUB_NO_MEMORY;
    }

    *subscriber = (ks_pubsub_subscriber_t){
        .client_id = client_id,
        .next = NULL,
    };
    *new_channel = (ks_pubsub_channel_t){
        .name = channel_name,
        .subscribers = subscriber,
        .next = pubsub->channels,
    };
    pubsub->channels = new_channel;
    pubsub->channel_count++;
    pubsub->subscription_count++;
    return KS_PUBSUB_OK;
}

ks_pubsub_status ks_pubsub_unsubscribe(ks_pubsub_t* pubsub,
                                       const char* channel,
                                       ks_pubsub_client_id client_id) {
    if (pubsub == NULL || channel == NULL || channel[0] == '\0') {
        return KS_PUBSUB_INVALID_ARGUMENT;
    }

    ks_pubsub_channel_t** channel_link = &pubsub->channels;
    while (*channel_link != NULL && strcmp((*channel_link)->name, channel) != 0) {
        channel_link = &(*channel_link)->next;
    }
    if (*channel_link == NULL) {
        return KS_PUBSUB_NOT_SUBSCRIBED;
    }

    ks_pubsub_channel_t* matched_channel = *channel_link;
    ks_pubsub_subscriber_t** subscriber_link = &matched_channel->subscribers;
    while (*subscriber_link != NULL && (*subscriber_link)->client_id != client_id) {
        subscriber_link = &(*subscriber_link)->next;
    }
    if (*subscriber_link == NULL) {
        return KS_PUBSUB_NOT_SUBSCRIBED;
    }

    ks_pubsub_subscriber_t* removed = *subscriber_link;
    *subscriber_link = removed->next;
    free(removed);
    pubsub->subscription_count--;

    if (matched_channel->subscribers == NULL) {
        *channel_link = matched_channel->next;
        matched_channel->next = NULL;
        destroy_channel(matched_channel);
        pubsub->channel_count--;
    }
    return KS_PUBSUB_OK;
}

size_t ks_pubsub_remove_client(ks_pubsub_t* pubsub,
                               ks_pubsub_client_id client_id) {
    if (pubsub == NULL) {
        return 0;
    }

    size_t removed_count = 0;
    ks_pubsub_channel_t** channel_link = &pubsub->channels;
    while (*channel_link != NULL) {
        ks_pubsub_channel_t* channel = *channel_link;
        ks_pubsub_subscriber_t** subscriber_link = &channel->subscribers;
        while (*subscriber_link != NULL) {
            if ((*subscriber_link)->client_id == client_id) {
                ks_pubsub_subscriber_t* removed = *subscriber_link;
                *subscriber_link = removed->next;
                free(removed);
                removed_count++;
                pubsub->subscription_count--;
                break;
            }
            subscriber_link = &(*subscriber_link)->next;
        }

        if (channel->subscribers == NULL) {
            *channel_link = channel->next;
            channel->next = NULL;
            destroy_channel(channel);
            pubsub->channel_count--;
        } else {
            channel_link = &channel->next;
        }
    }
    return removed_count;
}

size_t ks_pubsub_publish(const ks_pubsub_t* pubsub,
                         const char* channel,
                         const char* message,
                         size_t message_length,
                         ks_pubsub_delivery_fn deliver,
                         void* context) {
    if (pubsub == NULL || channel == NULL || channel[0] == '\0' ||
        (message == NULL && message_length != 0) || deliver == NULL) {
        return 0;
    }

    ks_pubsub_channel_t* matched_channel = find_channel(pubsub, channel);
    if (matched_channel == NULL) {
        return 0;
    }

    size_t recipients = 0;
    for (ks_pubsub_subscriber_t* subscriber = matched_channel->subscribers;
         subscriber != NULL; subscriber = subscriber->next) {
        if (deliver(subscriber->client_id, matched_channel->name, message,
                    message_length, context)) {
            recipients++;
        }
    }
    return recipients;
}

size_t ks_pubsub_channel_count(const ks_pubsub_t* pubsub) {
    return pubsub == NULL ? 0 : pubsub->channel_count;
}

size_t ks_pubsub_subscription_count(const ks_pubsub_t* pubsub) {
    return pubsub == NULL ? 0 : pubsub->subscription_count;
}
