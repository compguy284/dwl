#define _POSIX_C_SOURCE 200809L
#include "client_internal.h"
#include "compositor.h"
#include "monitor.h"
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>

#ifdef DWL_XWAYLAND

static void x11_handle_associate(struct wl_listener *listener, void *data);
static void x11_handle_dissociate(struct wl_listener *listener, void *data);
static void x11_handle_map(struct wl_listener *listener, void *data);
static void x11_handle_unmap(struct wl_listener *listener, void *data);
static void x11_handle_destroy(struct wl_listener *listener, void *data);
static void x11_handle_request_fullscreen(struct wl_listener *listener, void *data);
static void x11_handle_request_configure(struct wl_listener *listener, void *data);
static void x11_handle_set_title(struct wl_listener *listener, void *data);
static void x11_handle_set_class(struct wl_listener *listener, void *data);

DwlClient *dwl_client_create_x11(DwlClientManager *mgr, struct wlr_xwayland_surface *surface)
{
    DwlClient *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->magic = DWL_CLIENT_MAGIC;
    c->id = mgr->next_id++;
    c->mgr = mgr;
    c->xwayland = surface;
    c->is_x11 = true;
    c->border_width = 2;

    // Check if this should be floating (transient windows, dialogs, etc.)
    if (surface->parent || surface->modal) {
        c->floating = true;
    }

    // Listen for associate/dissociate - these fire when wlr_surface is ready
    c->associate.notify = x11_handle_associate;
    wl_signal_add(&surface->events.associate, &c->associate);

    c->dissociate.notify = x11_handle_dissociate;
    wl_signal_add(&surface->events.dissociate, &c->dissociate);

    c->destroy.notify = x11_handle_destroy;
    wl_signal_add(&surface->events.destroy, &c->destroy);

    c->request_fullscreen.notify = x11_handle_request_fullscreen;
    wl_signal_add(&surface->events.request_fullscreen, &c->request_fullscreen);

    c->commit.notify = x11_handle_request_configure;
    wl_signal_add(&surface->events.request_configure, &c->commit);

    c->set_title.notify = x11_handle_set_title;
    wl_signal_add(&surface->events.set_title, &c->set_title);

    c->set_app_id.notify = x11_handle_set_class;
    wl_signal_add(&surface->events.set_class, &c->set_app_id);

    // Store initial title/class
    if (surface->title) {
        c->title = strdup(surface->title);
    }
    if (surface->class) {
        c->app_id = strdup(surface->class);
    }

    wl_list_insert(&mgr->clients, &c->link);
    wl_list_insert(&mgr->focus_stack, &c->flink);

    return c;
}

static void x11_handle_associate(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, associate);
    (void)data;

    // Now that surface is associated, we can add map/unmap listeners
    c->map.notify = x11_handle_map;
    wl_signal_add(&c->xwayland->surface->events.map, &c->map);

    c->unmap.notify = x11_handle_unmap;
    wl_signal_add(&c->xwayland->surface->events.unmap, &c->unmap);
}

static void x11_handle_dissociate(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, dissociate);
    (void)data;

    // Remove map/unmap listeners when surface is dissociated
    wl_list_remove(&c->map.link);
    wl_list_remove(&c->unmap.link);
}

static void x11_handle_map(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, map);
    (void)data;

    c->mapped = true;

    if (c->xwayland) {
        c->width = c->xwayland->width;
        c->height = c->xwayland->height;

        if (c->xwayland->title && !c->title)
            c->title = strdup(c->xwayland->title);
        if (c->xwayland->class && !c->app_id)
            c->app_id = strdup(c->xwayland->class);
    }

    // Apply window rules
    if (c->mgr->rules)
        dwl_rule_engine_apply(c->mgr->rules, c);

    dwl_scene_client_create(c->mgr->scene_mgr, c);

    DwlRenderer *renderer = dwl_compositor_get_renderer(c->mgr->comp);
    DwlRenderConfig cfg = dwl_renderer_get_config(renderer);
    c->border_width = cfg.border_width;  // Sync with config
    dwl_scene_update_borders(c, cfg.border_width, cfg.border_color_unfocused);

    // Apply corner radius to surface buffers
    if (cfg.corner_radius > 0)
        dwl_scene_client_set_corner_radius(c, cfg.corner_radius);

    dwl_client_focus(c);

    // Store the output name for restore-monitor feature
    if (c->mon) {
        struct wlr_output *output = dwl_monitor_get_wlr_output(c->mon);
        if (output && output->name) {
            free(c->output_name);
            c->output_name = strdup(output->name);
        }
    }

    DwlEventBus *bus = dwl_compositor_get_event_bus(c->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_CREATE, c);

    dwl_monitor_arrange(c->mon);
}

static void x11_handle_unmap(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, unmap);
    (void)data;

    c->mapped = false;

    if (c->mgr->focused == c) {
        c->mgr->focused = NULL;
        struct wlr_seat *seat = dwl_compositor_get_seat(c->mgr->comp);
        wlr_seat_keyboard_notify_clear_focus(seat);

        DwlClient *next;
        wl_list_for_each(next, &c->mgr->focus_stack, flink) {
            if (next != c && next->mapped) {
                dwl_client_focus(next);
                break;
            }
        }
    }

    dwl_scene_client_destroy(c->mgr->scene_mgr, c);

    if (c->mon)
        dwl_monitor_arrange(c->mon);
}

static void x11_handle_destroy(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, destroy);
    (void)data;

    // Clear focused pointer if this was the focused client
    if (c->mgr->focused == c)
        c->mgr->focused = NULL;

    DwlEventBus *bus = dwl_compositor_get_event_bus(c->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_DESTROY, c);

    // Remove associate/dissociate listeners
    wl_list_remove(&c->associate.link);
    wl_list_remove(&c->dissociate.link);

    // map/unmap are only added after associate, check if they're valid
    if (c->xwayland->surface) {
        wl_list_remove(&c->map.link);
        wl_list_remove(&c->unmap.link);
    }

    wl_list_remove(&c->destroy.link);
    wl_list_remove(&c->commit.link);
    wl_list_remove(&c->request_fullscreen.link);
    wl_list_remove(&c->set_title.link);
    wl_list_remove(&c->set_app_id.link);
    wl_list_remove(&c->link);
    wl_list_remove(&c->flink);

    free(c->app_id);
    free(c->title);
    free(c->output_name);
    c->magic = 0;
    free(c);
}

static void x11_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, request_fullscreen);
    (void)data;

    dwl_client_set_fullscreen(c, !c->fullscreen);
}

static void x11_handle_request_configure(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, commit);
    struct wlr_xwayland_surface_configure_event *event = data;

    if (c->floating) {
        c->x = event->x;
        c->y = event->y;
        c->width = event->width;
        c->height = event->height;
    }

    wlr_xwayland_surface_configure(c->xwayland, event->x, event->y,
        event->width, event->height);
}

static void x11_handle_set_title(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, set_title);
    (void)data;

    if (c->xwayland && c->xwayland->title) {
        free(c->title);
        c->title = strdup(c->xwayland->title);
    }
}

static void x11_handle_set_class(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, set_app_id);
    (void)data;

    if (c->xwayland && c->xwayland->class) {
        free(c->app_id);
        c->app_id = strdup(c->xwayland->class);
    }
}

bool dwl_client_is_x11(const DwlClient *client)
{
    return client && client->is_x11;
}

bool dwl_client_is_x11_unmanaged(const DwlClient *client)
{
    if (!client || !client->is_x11 || !client->xwayland)
        return false;
    return client->xwayland->override_redirect;
}

const char *dwl_client_get_x11_class(const DwlClient *client)
{
    if (!client || !client->is_x11 || !client->xwayland)
        return NULL;
    return client->xwayland->class;
}

const char *dwl_client_get_x11_instance(const DwlClient *client)
{
    if (!client || !client->is_x11 || !client->xwayland)
        return NULL;
    return client->xwayland->instance;
}

int dwl_client_get_x11_pid(const DwlClient *client)
{
    if (!client || !client->is_x11 || !client->xwayland)
        return -1;
    return client->xwayland->pid;
}

struct wlr_xwayland_surface *dwl_client_get_xwayland_surface(DwlClient *client)
{
    return client ? client->xwayland : NULL;
}

#endif /* DWL_XWAYLAND */
