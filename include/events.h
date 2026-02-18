#ifndef DWL_EVENTS_H
#define DWL_EVENTS_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DWL_EVENT_CLIENT_CREATE,
    DWL_EVENT_CLIENT_DESTROY,
    DWL_EVENT_CLIENT_FOCUS,
    DWL_EVENT_CLIENT_UNFOCUS,
    DWL_EVENT_CLIENT_FULLSCREEN,
    DWL_EVENT_CLIENT_FLOAT,
    DWL_EVENT_CLIENT_MOVE,
    DWL_EVENT_CLIENT_RESIZE,
    DWL_EVENT_CLIENT_URGENT,
    DWL_EVENT_MONITOR_ADD,
    DWL_EVENT_MONITOR_REMOVE,
    DWL_EVENT_MONITOR_FOCUS,
    DWL_EVENT_LAYOUT_CHANGE,
    DWL_EVENT_KEY_PRESS,
    DWL_EVENT_KEY_RELEASE,
    DWL_EVENT_CONFIG_RELOAD,
    DWL_EVENT_RENDER_START,
    DWL_EVENT_RENDER_END,
    DWL_EVENT_LAYER_MAP,
    DWL_EVENT_LAYER_UNMAP,
} DwlEventType;

typedef struct DwlEvent {
    DwlEventType type;
    void *data;
    size_t data_size;
    uint64_t timestamp;
} DwlEvent;

typedef void (*DwlEventHandler)(void *ctx, const DwlEvent *event);

typedef struct DwlEventBus DwlEventBus;

DwlEventBus *dwl_event_bus_create(void);
void dwl_event_bus_destroy(DwlEventBus *bus);

int dwl_event_bus_subscribe(DwlEventBus *bus, DwlEventType type,
                            DwlEventHandler handler, void *ctx);
void dwl_event_bus_unsubscribe(DwlEventBus *bus, int subscription_id);
void dwl_event_bus_emit(DwlEventBus *bus, const DwlEvent *event);
void dwl_event_bus_emit_simple(DwlEventBus *bus, DwlEventType type, void *data);

#endif /* DWL_EVENTS_H */
