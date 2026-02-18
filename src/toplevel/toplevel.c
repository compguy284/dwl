#define _POSIX_C_SOURCE 200809L
#include "toplevel.h"
#include "compositor.h"
#include "client.h"
#include "monitor.h"
#include "events.h"
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_output.h>

typedef struct ToplevelHandle {
    SwlToplevelManager *mgr;
    SwlClient *client;
    struct wlr_foreign_toplevel_handle_v1 *handle;

    struct wl_listener request_activate;
    struct wl_listener request_close;
    struct wl_listener request_fullscreen;
    struct wl_listener request_maximize;
    struct wl_listener request_minimize;

    struct wl_list link;
} ToplevelHandle;

struct SwlToplevelManager {
    SwlCompositor *comp;
    struct wlr_foreign_toplevel_manager_v1 *manager;
    struct wl_list handles;

    int sub_create;
    int sub_destroy;
    int sub_focus;
    int sub_fullscreen;
};

static ToplevelHandle *find_handle(SwlToplevelManager *mgr, SwlClient *client)
{
    ToplevelHandle *h;
    wl_list_for_each(h, &mgr->handles, link) {
        if (h->client == client)
            return h;
    }
    return NULL;
}

static void handle_request_activate(struct wl_listener *listener, void *data)
{
    ToplevelHandle *h = wl_container_of(listener, h, request_activate);
    struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
    (void)event;

    if (h->client)
        swl_client_focus(h->client);
}

static void handle_request_close(struct wl_listener *listener, void *data)
{
    ToplevelHandle *h = wl_container_of(listener, h, request_close);
    (void)data;

    if (h->client)
        swl_client_close(h->client);
}

static void handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    ToplevelHandle *h = wl_container_of(listener, h, request_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;

    if (h->client)
        swl_client_set_fullscreen(h->client, event->fullscreen);
}

static void handle_request_maximize(struct wl_listener *listener, void *data)
{
    ToplevelHandle *h = wl_container_of(listener, h, request_maximize);
    struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    (void)event;

    // We don't have maximize in our design, but we can toggle floating
    // For now, just ignore
}

static void handle_request_minimize(struct wl_listener *listener, void *data)
{
    ToplevelHandle *h = wl_container_of(listener, h, request_minimize);
    struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
    (void)h;
    (void)event;

    // We don't have minimize in our design
}

static void create_handle(SwlToplevelManager *mgr, SwlClient *client)
{
    if (!mgr || !client)
        return;

    // Check if handle already exists
    if (find_handle(mgr, client))
        return;

    ToplevelHandle *h = calloc(1, sizeof(*h));
    if (!h)
        return;

    h->mgr = mgr;
    h->client = client;

    h->handle = wlr_foreign_toplevel_handle_v1_create(mgr->manager);
    if (!h->handle) {
        free(h);
        return;
    }

    // Set initial state
    SwlClientInfo info = swl_client_get_info(client);
    if (info.title)
        wlr_foreign_toplevel_handle_v1_set_title(h->handle, info.title);
    if (info.app_id)
        wlr_foreign_toplevel_handle_v1_set_app_id(h->handle, info.app_id);

    // Set output
    SwlMonitor *mon = swl_client_get_monitor(client);
    if (mon) {
        struct wlr_output *output = swl_monitor_get_wlr_output(mon);
        if (output)
            wlr_foreign_toplevel_handle_v1_output_enter(h->handle, output);
    }

    // Set up listeners
    h->request_activate.notify = handle_request_activate;
    wl_signal_add(&h->handle->events.request_activate, &h->request_activate);

    h->request_close.notify = handle_request_close;
    wl_signal_add(&h->handle->events.request_close, &h->request_close);

    h->request_fullscreen.notify = handle_request_fullscreen;
    wl_signal_add(&h->handle->events.request_fullscreen, &h->request_fullscreen);

    h->request_maximize.notify = handle_request_maximize;
    wl_signal_add(&h->handle->events.request_maximize, &h->request_maximize);

    h->request_minimize.notify = handle_request_minimize;
    wl_signal_add(&h->handle->events.request_minimize, &h->request_minimize);

    wl_list_insert(&mgr->handles, &h->link);
}

static void destroy_handle(ToplevelHandle *h)
{
    if (!h)
        return;

    wl_list_remove(&h->request_activate.link);
    wl_list_remove(&h->request_close.link);
    wl_list_remove(&h->request_fullscreen.link);
    wl_list_remove(&h->request_maximize.link);
    wl_list_remove(&h->request_minimize.link);
    wl_list_remove(&h->link);

    wlr_foreign_toplevel_handle_v1_destroy(h->handle);
    free(h);
}

static void handle_client_create(void *ctx, const SwlEvent *event)
{
    SwlToplevelManager *mgr = ctx;
    SwlClient *client = event->data;
    create_handle(mgr, client);
}

static void handle_client_destroy(void *ctx, const SwlEvent *event)
{
    SwlToplevelManager *mgr = ctx;
    SwlClient *client = event->data;
    ToplevelHandle *h = find_handle(mgr, client);
    if (h)
        destroy_handle(h);
}

static void handle_client_focus(void *ctx, const SwlEvent *event)
{
    SwlToplevelManager *mgr = ctx;
    SwlClient *client = event->data;

    ToplevelHandle *h = find_handle(mgr, client);
    if (h)
        wlr_foreign_toplevel_handle_v1_set_activated(h->handle, true);

    // Unset activated on other handles
    ToplevelHandle *other;
    wl_list_for_each(other, &mgr->handles, link) {
        if (other != h)
            wlr_foreign_toplevel_handle_v1_set_activated(other->handle, false);
    }
}

static void handle_client_fullscreen(void *ctx, const SwlEvent *event)
{
    SwlToplevelManager *mgr = ctx;
    SwlClient *client = event->data;

    ToplevelHandle *h = find_handle(mgr, client);
    if (h) {
        SwlClientInfo info = swl_client_get_info(client);
        wlr_foreign_toplevel_handle_v1_set_fullscreen(h->handle, info.fullscreen);
    }
}

SwlToplevelManager *swl_toplevel_manager_create(SwlCompositor *comp)
{
    SwlToplevelManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->comp = comp;
    wl_list_init(&mgr->handles);

    struct wl_display *display = swl_compositor_get_wl_display(comp);
    mgr->manager = wlr_foreign_toplevel_manager_v1_create(display);
    if (!mgr->manager) {
        free(mgr);
        return NULL;
    }

    // Subscribe to client events
    SwlEventBus *bus = swl_compositor_get_event_bus(comp);
    mgr->sub_create = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, handle_client_create, mgr);
    mgr->sub_destroy = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_DESTROY, handle_client_destroy, mgr);
    mgr->sub_focus = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_FOCUS, handle_client_focus, mgr);
    mgr->sub_fullscreen = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_FULLSCREEN, handle_client_fullscreen, mgr);

    return mgr;
}

void swl_toplevel_manager_destroy(SwlToplevelManager *mgr)
{
    if (!mgr)
        return;

    // Unsubscribe from events
    SwlEventBus *bus = swl_compositor_get_event_bus(mgr->comp);
    swl_event_bus_unsubscribe(bus, mgr->sub_create);
    swl_event_bus_unsubscribe(bus, mgr->sub_destroy);
    swl_event_bus_unsubscribe(bus, mgr->sub_focus);
    swl_event_bus_unsubscribe(bus, mgr->sub_fullscreen);

    // Destroy all handles
    ToplevelHandle *h, *tmp;
    wl_list_for_each_safe(h, tmp, &mgr->handles, link) {
        destroy_handle(h);
    }

    free(mgr);
}

void swl_toplevel_client_create(SwlToplevelManager *mgr, SwlClient *client)
{
    create_handle(mgr, client);
}

void swl_toplevel_client_destroy(SwlToplevelManager *mgr, SwlClient *client)
{
    ToplevelHandle *h = find_handle(mgr, client);
    if (h)
        destroy_handle(h);
}

void swl_toplevel_client_set_title(SwlToplevelManager *mgr, SwlClient *client, const char *title)
{
    ToplevelHandle *h = find_handle(mgr, client);
    if (h && title)
        wlr_foreign_toplevel_handle_v1_set_title(h->handle, title);
}

void swl_toplevel_client_set_app_id(SwlToplevelManager *mgr, SwlClient *client, const char *app_id)
{
    ToplevelHandle *h = find_handle(mgr, client);
    if (h && app_id)
        wlr_foreign_toplevel_handle_v1_set_app_id(h->handle, app_id);
}

void swl_toplevel_client_set_output(SwlToplevelManager *mgr, SwlClient *client, SwlMonitor *mon)
{
    ToplevelHandle *h = find_handle(mgr, client);
    if (!h || !mon)
        return;

    struct wlr_output *output = swl_monitor_get_wlr_output(mon);
    if (output)
        wlr_foreign_toplevel_handle_v1_output_enter(h->handle, output);
}

void swl_toplevel_client_set_activated(SwlToplevelManager *mgr, SwlClient *client, bool activated)
{
    ToplevelHandle *h = find_handle(mgr, client);
    if (h)
        wlr_foreign_toplevel_handle_v1_set_activated(h->handle, activated);
}

void swl_toplevel_client_set_maximized(SwlToplevelManager *mgr, SwlClient *client, bool maximized)
{
    ToplevelHandle *h = find_handle(mgr, client);
    if (h)
        wlr_foreign_toplevel_handle_v1_set_maximized(h->handle, maximized);
}

void swl_toplevel_client_set_minimized(SwlToplevelManager *mgr, SwlClient *client, bool minimized)
{
    ToplevelHandle *h = find_handle(mgr, client);
    if (h)
        wlr_foreign_toplevel_handle_v1_set_minimized(h->handle, minimized);
}

void swl_toplevel_client_set_fullscreen(SwlToplevelManager *mgr, SwlClient *client, bool fullscreen)
{
    ToplevelHandle *h = find_handle(mgr, client);
    if (h)
        wlr_foreign_toplevel_handle_v1_set_fullscreen(h->handle, fullscreen);
}
