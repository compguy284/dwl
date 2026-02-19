#ifndef SWL_EVENTS_H
#define SWL_EVENTS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    SWL_EVENT_CLIENT_CREATE,
    SWL_EVENT_CLIENT_DESTROY,
    SWL_EVENT_CLIENT_FOCUS,
    SWL_EVENT_CLIENT_UNFOCUS,
    SWL_EVENT_CLIENT_FULLSCREEN,
    SWL_EVENT_CLIENT_FLOAT,
    SWL_EVENT_CLIENT_MOVE,
    SWL_EVENT_CLIENT_RESIZE,
    SWL_EVENT_CLIENT_URGENT,
    SWL_EVENT_MONITOR_ADD,
    SWL_EVENT_MONITOR_REMOVE,
    SWL_EVENT_MONITOR_FOCUS,
    SWL_EVENT_LAYOUT_CHANGE,
    SWL_EVENT_KEY_PRESS,
    SWL_EVENT_KEY_RELEASE,
    SWL_EVENT_CONFIG_RELOAD,
    SWL_EVENT_RENDER_START,
    SWL_EVENT_RENDER_END,
    SWL_EVENT_LAYER_MAP,
    SWL_EVENT_LAYER_UNMAP,
    SWL_EVENT_SESSION_LOCK,
    SWL_EVENT_SESSION_UNLOCK,
    SWL_EVENT_LID_CLOSE,
    SWL_EVENT_LID_OPEN,
} SwlEventType;

typedef struct SwlEvent {
    SwlEventType type;
    void *data;
    size_t data_size;
    uint64_t timestamp;
} SwlEvent;

typedef void (*SwlEventHandler)(void *ctx, const SwlEvent *event);

typedef struct SwlEventBus SwlEventBus;

SwlEventBus *swl_event_bus_create(void);
void swl_event_bus_destroy(SwlEventBus *bus);

int swl_event_bus_subscribe(SwlEventBus *bus, SwlEventType type,
                            SwlEventHandler handler, void *ctx);
void swl_event_bus_unsubscribe(SwlEventBus *bus, int subscription_id);
void swl_event_bus_emit(SwlEventBus *bus, const SwlEvent *event);
void swl_event_bus_emit_simple(SwlEventBus *bus, SwlEventType type, void *data);

#endif /* SWL_EVENTS_H */
