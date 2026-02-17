#define _POSIX_C_SOURCE 200809L
#include "events.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_SUBSCRIPTIONS 256

typedef struct {
    int id;
    DwlEventType type;
    DwlEventHandler handler;
    void *ctx;
    bool active;
} Subscription;

struct DwlEventBus {
    Subscription subscriptions[MAX_SUBSCRIPTIONS];
    int next_id;
    int count;
};

static uint64_t get_timestamp(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

DwlEventBus *dwl_event_bus_create(void)
{
    DwlEventBus *bus = calloc(1, sizeof(*bus));
    if (!bus)
        return NULL;

    bus->next_id = 1;
    return bus;
}

void dwl_event_bus_destroy(DwlEventBus *bus)
{
    if (bus)
        free(bus);
}

int dwl_event_bus_subscribe(DwlEventBus *bus, DwlEventType type,
                            DwlEventHandler handler, void *ctx)
{
    if (!bus || !handler)
        return -1;

    if (bus->count >= MAX_SUBSCRIPTIONS)
        return -1;

    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (!bus->subscriptions[i].active) {
            bus->subscriptions[i].id = bus->next_id++;
            bus->subscriptions[i].type = type;
            bus->subscriptions[i].handler = handler;
            bus->subscriptions[i].ctx = ctx;
            bus->subscriptions[i].active = true;
            bus->count++;
            return bus->subscriptions[i].id;
        }
    }

    return -1;
}

void dwl_event_bus_unsubscribe(DwlEventBus *bus, int subscription_id)
{
    if (!bus || subscription_id <= 0)
        return;

    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (bus->subscriptions[i].active &&
            bus->subscriptions[i].id == subscription_id) {
            bus->subscriptions[i].active = false;
            bus->count--;
            return;
        }
    }
}

void dwl_event_bus_emit(DwlEventBus *bus, const DwlEvent *event)
{
    if (!bus || !event)
        return;

    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (bus->subscriptions[i].active &&
            bus->subscriptions[i].type == event->type) {
            bus->subscriptions[i].handler(bus->subscriptions[i].ctx, event);
        }
    }
}

void dwl_event_bus_emit_simple(DwlEventBus *bus, DwlEventType type, void *data)
{
    DwlEvent event = {
        .type = type,
        .data = data,
        .data_size = 0,
        .timestamp = get_timestamp(),
    };
    dwl_event_bus_emit(bus, &event);
}
