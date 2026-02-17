#define _POSIX_C_SOURCE 200809L
#include "client_internal.h"
#include "compositor.h"
#include "config.h"
#include "monitor.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>

ClientSceneData *dwl_client_get_scene_data(DwlClient *client)
{
    return client ? client->scene_data : NULL;
}

void dwl_client_set_scene_data(DwlClient *client, ClientSceneData *data)
{
    if (client)
        client->scene_data = data;
}

struct wlr_xdg_toplevel *dwl_client_get_xdg_toplevel(DwlClient *client)
{
    return client ? client->xdg : NULL;
}

// Check if a client pointer is still valid (has valid magic number)
bool dwl_client_is_valid(DwlClient *client)
{
    if (!client)
        return false;
    return client->magic == DWL_CLIENT_MAGIC;
}

static void client_handle_map(struct wl_listener *listener, void *data);
static void client_handle_unmap(struct wl_listener *listener, void *data);
static void client_handle_destroy(struct wl_listener *listener, void *data);
static void client_handle_commit(struct wl_listener *listener, void *data);
static void client_handle_request_fullscreen(struct wl_listener *listener, void *data);
static void client_handle_set_title(struct wl_listener *listener, void *data);
static void client_handle_set_app_id(struct wl_listener *listener, void *data);

DwlClientManager *dwl_client_manager_create(DwlCompositor *comp)
{
    DwlClientManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->comp = comp;
    mgr->next_id = 1;
    wl_list_init(&mgr->clients);
    wl_list_init(&mgr->focus_stack);

    mgr->scene_mgr = dwl_scene_manager_create(comp);
    mgr->rules = dwl_rule_engine_create();

    // Load rules from config
    dwl_client_manager_load_rules(mgr);

    return mgr;
}

void dwl_client_manager_destroy(DwlClientManager *mgr)
{
    if (!mgr)
        return;

    DwlClient *c, *tmp;
    wl_list_for_each_safe(c, tmp, &mgr->clients, link) {
        wl_list_remove(&c->link);
        wl_list_remove(&c->flink);
        free(c->app_id);
        free(c->title);
        c->magic = 0;
        free(c);
    }

    dwl_scene_manager_destroy(mgr->scene_mgr);
    dwl_rule_engine_destroy(mgr->rules);
    free(mgr);
}

DwlClient *dwl_client_create_xdg(DwlClientManager *mgr, struct wlr_xdg_toplevel *toplevel)
{
    DwlClient *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->magic = DWL_CLIENT_MAGIC;
    c->id = mgr->next_id++;
    c->mgr = mgr;
    c->xdg = toplevel;
    c->tags = 1;
    c->border_width = 2;

    c->map.notify = client_handle_map;
    wl_signal_add(&toplevel->base->surface->events.map, &c->map);

    c->unmap.notify = client_handle_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &c->unmap);

    c->destroy.notify = client_handle_destroy;
    wl_signal_add(&toplevel->events.destroy, &c->destroy);

    c->commit.notify = client_handle_commit;
    wl_signal_add(&toplevel->base->surface->events.commit, &c->commit);

    c->request_fullscreen.notify = client_handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &c->request_fullscreen);

    c->set_title.notify = client_handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &c->set_title);

    c->set_app_id.notify = client_handle_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &c->set_app_id);

    wl_list_insert(&mgr->clients, &c->link);
    wl_list_insert(&mgr->focus_stack, &c->flink);

    return c;
}

static void focus_client_internal(DwlClient *c)
{
    if (!c || !c->mgr)
        return;

    DwlCompositor *comp = c->mgr->comp;
    struct wlr_seat *seat = dwl_compositor_get_seat(comp);
    struct wlr_surface *surface = NULL;

    if (c->xdg && c->xdg->base)
        surface = c->xdg->base->surface;

#ifdef DWL_XWAYLAND
    if (c->is_x11 && c->xwayland)
        surface = c->xwayland->surface;
#endif

    if (surface) {
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
        if (kb)
            wlr_seat_keyboard_notify_enter(seat, surface,
                kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }

    if (c->xdg && c->xdg->base && c->xdg->base->initialized)
        wlr_xdg_toplevel_set_activated(c->xdg, true);

#ifdef DWL_XWAYLAND
    if (c->is_x11 && c->xwayland)
        wlr_xwayland_surface_activate(c->xwayland, true);
#endif

    if (c->scene_data && c->scene_data->tree)
        wlr_scene_node_raise_to_top(&c->scene_data->tree->node);
}

static void unfocus_client_internal(DwlClient *c)
{
    if (!c)
        return;

    if (c->xdg && c->xdg->base && c->xdg->base->initialized)
        wlr_xdg_toplevel_set_activated(c->xdg, false);

#ifdef DWL_XWAYLAND
    if (c->is_x11 && c->xwayland)
        wlr_xwayland_surface_activate(c->xwayland, false);
#endif
}

static void client_handle_map(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, map);
    (void)data;

    c->mapped = true;

    if (c->xdg) {
        if (c->xdg->app_id) {
            free(c->app_id);
            c->app_id = strdup(c->xdg->app_id);
        }
        if (c->xdg->title) {
            free(c->title);
            c->title = strdup(c->xdg->title);
        }

        c->width = c->xdg->current.width;
        c->height = c->xdg->current.height;
    }

    // Auto-float windows that have a parent (dialogs) or fixed size
    if (c->xdg) {
        if (c->xdg->parent
            || (c->xdg->current.min_width &&
                c->xdg->current.min_width == c->xdg->current.max_width &&
                c->xdg->current.min_height == c->xdg->current.max_height))
            c->floating = true;
    }

    // Apply window rules (may override auto-float)
    if (c->mgr->rules)
        dwl_rule_engine_apply(c->mgr->rules, c);

    // Tell client it's tiled on all edges (like dwl_mac)
    if (c->xdg && c->xdg->base && c->xdg->base->initialized) {
        uint32_t edges = WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT;
        wlr_xdg_toplevel_set_tiled(c->xdg, edges);
    }

    dwl_scene_client_create(c->mgr->scene_mgr, c);

    // Place floating clients on the float layer
    if (c->floating && c->mgr->scene_mgr)
        dwl_scene_client_set_layer(c->mgr->scene_mgr, c, DWL_LAYER_FLOAT);

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

static void client_handle_unmap(struct wl_listener *listener, void *data)
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

static void client_handle_destroy(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, destroy);
    (void)data;

    // Clear focused pointer if this was the focused client
    if (c->mgr->focused == c)
        c->mgr->focused = NULL;

    DwlEventBus *bus = dwl_compositor_get_event_bus(c->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_DESTROY, c);

    wl_list_remove(&c->map.link);
    wl_list_remove(&c->unmap.link);
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

static void client_handle_commit(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, commit);
    (void)data;

    if (!c->xdg || !c->xdg->base)
        return;

    // Handle initial commit - send configure to let client know it can proceed
    if (c->xdg->base->initial_commit) {
        wlr_xdg_toplevel_set_wm_capabilities(c->xdg,
            WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
        // Send configure with 0,0 to let client choose its size
        wlr_xdg_toplevel_set_size(c->xdg, 0, 0);
        return;
    }

    if (!c->mapped)
        return;

    // Re-apply resize on every commit, like dwl_mac does
    // This ensures the client receives the configure and resizes properly
    int bw = c->border_width;
    int total_w = c->width + 2 * bw;
    int total_h = c->height + 2 * bw;
    dwl_client_resize(c, c->x, c->y, total_w, total_h);
}

static void client_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, request_fullscreen);
    (void)data;

    dwl_client_set_fullscreen(c, !c->fullscreen);
}

static void client_handle_set_title(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, set_title);
    (void)data;

    if (c->xdg && c->xdg->title) {
        free(c->title);
        c->title = strdup(c->xdg->title);
    }
}

static void client_handle_set_app_id(struct wl_listener *listener, void *data)
{
    DwlClient *c = wl_container_of(listener, c, set_app_id);
    (void)data;

    if (c->xdg && c->xdg->app_id) {
        free(c->app_id);
        c->app_id = strdup(c->xdg->app_id);
    }
}

void dwl_client_foreach(DwlClientManager *mgr, DwlClientIterator iter, void *data)
{
    if (!mgr || !iter)
        return;

    DwlClient *c, *tmp;
    wl_list_for_each_safe(c, tmp, &mgr->clients, link) {
        if (!iter(c, data))
            break;
    }
}

void dwl_client_foreach_visible(DwlClientManager *mgr, DwlMonitor *mon,
                                 DwlClientIterator iter, void *data)
{
    if (!mgr || !iter)
        return;

    uint32_t tags = mon ? dwl_monitor_get_tags(mon) : ~0u;

    DwlClient *c, *tmp;
    wl_list_for_each_safe(c, tmp, &mgr->clients, link) {
        if (!c->mapped)
            continue;
        if (mon && c->mon != mon)
            continue;
        if (!(c->tags & tags))
            continue;
        if (!iter(c, data))
            break;
    }
}

void dwl_client_foreach_on_tag(DwlClientManager *mgr, uint32_t tags,
                                DwlClientIterator iter, void *data)
{
    if (!mgr || !iter)
        return;

    DwlClient *c, *tmp;
    wl_list_for_each_safe(c, tmp, &mgr->clients, link) {
        if (c->tags & tags) {
            if (!iter(c, data))
                break;
        }
    }
}

DwlClient *dwl_client_at(DwlClientManager *mgr, double x, double y)
{
    if (!mgr)
        return NULL;

    DwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (!c->mapped)
            continue;
        if (x >= c->x && x < c->x + c->width + 2 * c->border_width &&
            y >= c->y && y < c->y + c->height + 2 * c->border_width)
            return c;
    }

    return NULL;
}

DwlClient *dwl_client_focused(DwlClientManager *mgr)
{
    return mgr ? mgr->focused : NULL;
}

DwlClient *dwl_client_by_id(DwlClientManager *mgr, uint32_t id)
{
    if (!mgr)
        return NULL;

    DwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (c->id == id)
            return c;
    }

    return NULL;
}

DwlClient *dwl_client_by_surface(DwlClientManager *mgr, struct wlr_surface *surface)
{
    if (!mgr || !surface)
        return NULL;

    DwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        struct wlr_surface *client_surface = dwl_client_get_surface(c);
        if (client_surface == surface)
            return c;
    }

    return NULL;
}

size_t dwl_client_count(DwlClientManager *mgr)
{
    if (!mgr)
        return 0;

    size_t count = 0;
    DwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        count++;
    }

    return count;
}

size_t dwl_client_count_visible(DwlClientManager *mgr, DwlMonitor *mon)
{
    if (!mgr)
        return 0;

    uint32_t tags = mon ? dwl_monitor_get_tags(mon) : ~0u;
    size_t count = 0;

    DwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (!c->mapped)
            continue;
        if (mon && c->mon != mon)
            continue;
        if (c->tags & tags)
            count++;
    }

    return count;
}

DwlClientInfo dwl_client_get_info(const DwlClient *client)
{
    DwlClientInfo info = {0};
    if (!client)
        return info;

    info.id = client->id;
    info.app_id = client->app_id;
    info.title = client->title;
    info.geometry.x = client->x;
    info.geometry.y = client->y;
    info.geometry.width = client->width;
    info.geometry.height = client->height;
    info.tags = client->tags;
    info.floating = client->floating;
    info.fullscreen = client->fullscreen;
    info.urgent = client->urgent;
    info.focused = client->focused;
#ifdef DWL_XWAYLAND
    info.x11 = client->is_x11;
#endif

    return info;
}

DwlMonitor *dwl_client_get_monitor(const DwlClient *client)
{
    return client ? client->mon : NULL;
}

struct wlr_surface *dwl_client_get_surface(const DwlClient *client)
{
    if (!client)
        return NULL;

#ifdef DWL_XWAYLAND
    if (client->is_x11 && client->xwayland)
        return client->xwayland->surface;
#endif

    if (client->xdg && client->xdg->base)
        return client->xdg->base->surface;

    return NULL;
}

const char *dwl_client_get_output_name(const DwlClient *client)
{
    return client ? client->output_name : NULL;
}

void dwl_client_set_monitor_internal(DwlClient *client, DwlMonitor *mon)
{
    if (client)
        client->mon = mon;
}

DwlError dwl_client_close(DwlClient *client)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

#ifdef DWL_XWAYLAND
    if (client->is_x11 && client->xwayland) {
        wlr_xwayland_surface_close(client->xwayland);
        return DWL_OK;
    }
#endif

    if (client->xdg) {
        wlr_xdg_toplevel_send_close(client->xdg);
        return DWL_OK;
    }

    return DWL_ERR_INVALID_ARG;
}

DwlError dwl_client_focus(DwlClient *client)
{
    if (!client || !client->mgr)
        return DWL_ERR_INVALID_ARG;

    DwlClient *old = client->mgr->focused;
    if (old == client)
        return DWL_OK;

    DwlRenderer *renderer = dwl_compositor_get_renderer(client->mgr->comp);
    DwlRenderConfig cfg = dwl_renderer_get_config(renderer);

    // Validate old client is still valid (guards against stale pointers)
    if (dwl_client_is_valid(old)) {
        old->focused = false;
        unfocus_client_internal(old);

        dwl_scene_update_borders(old, cfg.border_width, cfg.border_color_unfocused);
        dwl_scene_client_set_opacity(old, cfg.opacity_inactive);

        DwlEventBus *bus = dwl_compositor_get_event_bus(client->mgr->comp);
        dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_UNFOCUS, old);
    } else if (old) {
        // Stale pointer - clear it
        client->mgr->focused = NULL;
    }

    client->mgr->focused = client;
    client->focused = true;
    client->urgent = false;  // Clear urgent when focused

    wl_list_remove(&client->flink);
    wl_list_insert(&client->mgr->focus_stack, &client->flink);

    focus_client_internal(client);

    dwl_scene_update_borders(client, cfg.border_width, cfg.border_color_focused);
    dwl_scene_client_set_opacity(client, cfg.opacity_active);

    DwlEventBus *bus = dwl_compositor_get_event_bus(client->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_FOCUS, client);

    // Re-arrange for layouts that depend on focus (e.g., scroller)
    if (client->mon)
        dwl_monitor_arrange(client->mon);

    return DWL_OK;
}

DwlError dwl_client_set_tags(DwlClient *client, uint32_t tags)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    if (tags == 0)
        tags = 1;

    client->tags = tags;

    DwlEventBus *bus = dwl_compositor_get_event_bus(client->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_TAG, client);

    if (client->mon)
        dwl_monitor_arrange(client->mon);

    return DWL_OK;
}

DwlError dwl_client_toggle_tag(DwlClient *client, uint32_t tag)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    uint32_t newtags = client->tags ^ tag;
    if (newtags == 0)
        return DWL_OK;

    return dwl_client_set_tags(client, newtags);
}

DwlError dwl_client_set_floating(DwlClient *client, bool floating)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    client->floating = floating;

    if (client->mgr->scene_mgr) {
        DwlSceneLayer layer = floating ? DWL_LAYER_FLOAT : DWL_LAYER_TILES;
        dwl_scene_client_set_layer(client->mgr->scene_mgr, client, layer);
    }

    DwlEventBus *bus = dwl_compositor_get_event_bus(client->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_FLOAT, client);

    if (client->mon)
        dwl_monitor_arrange(client->mon);

    return DWL_OK;
}

DwlError dwl_client_toggle_floating(DwlClient *client)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    return dwl_client_set_floating(client, !client->floating);
}

DwlError dwl_client_set_fullscreen(DwlClient *client, bool fullscreen)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    client->fullscreen = fullscreen;

    if (client->xdg && client->xdg->base && client->xdg->base->initialized)
        wlr_xdg_toplevel_set_fullscreen(client->xdg, fullscreen);

    if (client->mgr->scene_mgr) {
        DwlSceneLayer layer = fullscreen ? DWL_LAYER_FULLSCREEN :
            (client->floating ? DWL_LAYER_FLOAT : DWL_LAYER_TILES);
        dwl_scene_client_set_layer(client->mgr->scene_mgr, client, layer);
    }

    DwlEventBus *bus = dwl_compositor_get_event_bus(client->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_FULLSCREEN, client);

    if (client->mon)
        dwl_monitor_arrange(client->mon);

    return DWL_OK;
}

DwlError dwl_client_toggle_fullscreen(DwlClient *client)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    return dwl_client_set_fullscreen(client, !client->fullscreen);
}

DwlError dwl_client_move_to_monitor(DwlClient *client, DwlMonitor *mon)
{
    if (!client || !mon)
        return DWL_ERR_INVALID_ARG;

    DwlMonitor *old = client->mon;
    client->mon = mon;

    // Update the stored output name for restore-monitor feature
    struct wlr_output *output = dwl_monitor_get_wlr_output(mon);
    if (output && output->name) {
        free(client->output_name);
        client->output_name = strdup(output->name);
    }

    if (old && old != mon)
        dwl_monitor_arrange(old);
    dwl_monitor_arrange(mon);

    return DWL_OK;
}

DwlError dwl_client_resize(DwlClient *client, int x, int y, int w, int h)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    // w and h are TOTAL geometry (including borders), like dwl_mac
    // Calculate content size by subtracting borders
    int bw = client->border_width;
    int content_w = w - 2 * bw;
    int content_h = h - 2 * bw;

    client->x = x;
    client->y = y;
    client->width = content_w;
    client->height = content_h;

    dwl_scene_client_set_position(client, x, y);
    dwl_scene_client_set_size(client, content_w, content_h);

#ifdef DWL_XWAYLAND
    // Configure XWayland surface with its position and size
    if (client->is_x11 && client->xwayland) {
        wlr_xwayland_surface_configure(client->xwayland,
            x + bw, y + bw, content_w, content_h);
    }
#endif

    // Apply clipping/visibility based on monitor boundaries
    // Note: w and h are total geometry including borders
    if (client->mon) {
        int mx, my, mw, mh;
        dwl_monitor_get_usable_area(client->mon, &mx, &my, &mw, &mh);

        int client_left = x;
        int client_top = y;
        int client_right = x + w;
        int client_bottom = y + h;

        // Check if client is completely outside monitor bounds
        if (client_right <= mx || client_left >= mx + mw ||
            client_bottom <= my || client_top >= my + mh) {
            // Completely outside - hide the client
            dwl_scene_client_set_visible(client, false);
        } else if (client_left < mx || client_top < my ||
                   client_right > mx + mw || client_bottom > my + mh) {
            // Partially visible - show and clip
            dwl_scene_client_set_visible(client, true);

            // Calculate clip box in client-local coords (tree top-left is 0,0)
            int clip_x = (client_left < mx) ? (mx - client_left) : 0;
            int clip_y = (client_top < my) ? (my - client_top) : 0;
            int clip_right = (client_right > mx + mw) ? (mx + mw - client_left) : w;
            int clip_bottom = (client_bottom > my + mh) ? (my + mh - client_top) : h;
            int clip_w = clip_right - clip_x;
            int clip_h = clip_bottom - clip_y;

            if (clip_w > 0 && clip_h > 0) {
                dwl_scene_client_set_clip(client, clip_x, clip_y, clip_w, clip_h);
            }
        } else {
            // Client fully within monitor - show and clear any clip
            dwl_scene_client_set_visible(client, true);
            dwl_scene_client_clear_clip(client);
        }
    }

    DwlEventBus *bus = dwl_compositor_get_event_bus(client->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_CLIENT_RESIZE, client);

    return DWL_OK;
}

DwlError dwl_client_set_border_color(DwlClient *client, const float color[4])
{
    if (!client || !color)
        return DWL_ERR_INVALID_ARG;

    dwl_scene_update_borders(client, client->border_width, color);
    return DWL_OK;
}

DwlError dwl_client_set_border_width(DwlClient *client, int width)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    client->border_width = width;
    return DWL_OK;
}

DwlError dwl_client_set_urgent(DwlClient *client, bool urgent)
{
    if (!client)
        return DWL_ERR_INVALID_ARG;

    client->urgent = urgent;

    // Update border color if mapped
    if (client->mapped && !client->focused) {
        DwlRenderer *renderer = dwl_compositor_get_renderer(client->mgr->comp);
        DwlRenderConfig cfg = dwl_renderer_get_config(renderer);
        const float *color = urgent ? cfg.border_color_urgent : cfg.border_color_unfocused;
        dwl_scene_update_borders(client, cfg.border_width, color);
    }

    return DWL_OK;
}

DwlError dwl_client_zoom(DwlClientManager *mgr)
{
    if (!mgr)
        return DWL_ERR_INVALID_ARG;

    DwlClient *focused = mgr->focused;
    if (!focused || !focused->mapped)
        return DWL_ERR_NOT_FOUND;

    // Find the first tiled client on the same monitor
    DwlClient *first = NULL;
    DwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (!c->mapped || c->floating || c->fullscreen)
            continue;
        if (c->mon != focused->mon)
            continue;
        if (!(c->tags & dwl_monitor_get_tags(c->mon)))
            continue;
        first = c;
        break;
    }

    if (!first || first == focused)
        return DWL_OK;

    // Swap positions in the client list
    struct wl_list *focused_prev = focused->link.prev;
    struct wl_list *first_prev = first->link.prev;

    // Remove both from list
    wl_list_remove(&focused->link);
    wl_list_remove(&first->link);

    // Re-insert swapped
    wl_list_insert(first_prev, &focused->link);
    wl_list_insert(focused_prev, &first->link);

    // Re-arrange
    if (focused->mon)
        dwl_monitor_arrange(focused->mon);

    return DWL_OK;
}

DwlClient *dwl_client_in_direction(DwlClientManager *mgr, DwlClient *from, int direction)
{
    if (!mgr || !from)
        return NULL;

    int from_cx = from->x + from->width / 2;
    int from_cy = from->y + from->height / 2;

    DwlClient *best = NULL;
    int best_dist = INT_MAX;

    DwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (c == from || !c->mapped)
            continue;
        if (c->mon != from->mon)
            continue;
        if (!(c->tags & dwl_monitor_get_tags(c->mon)))
            continue;

        int c_cx = c->x + c->width / 2;
        int c_cy = c->y + c->height / 2;

        int dx = c_cx - from_cx;
        int dy = c_cy - from_cy;

        bool valid = false;
        int dist = 0;

        switch (direction) {
        case 0: // up
            if (dy < 0) {
                valid = true;
                dist = -dy + (dx > 0 ? dx : -dx) / 2;
            }
            break;
        case 1: // down
            if (dy > 0) {
                valid = true;
                dist = dy + (dx > 0 ? dx : -dx) / 2;
            }
            break;
        case 2: // left
            if (dx < 0) {
                valid = true;
                dist = -dx + (dy > 0 ? dy : -dy) / 2;
            }
            break;
        case 3: // right
            if (dx > 0) {
                valid = true;
                dist = dx + (dy > 0 ? dy : -dy) / 2;
            }
            break;
        }

        if (valid && dist < best_dist) {
            best_dist = dist;
            best = c;
        }
    }

    return best;
}

DwlSceneManager *dwl_client_manager_get_scene(DwlClientManager *mgr)
{
    return mgr ? mgr->scene_mgr : NULL;
}

DwlRuleEngine *dwl_client_manager_get_rules(DwlClientManager *mgr)
{
    return mgr ? mgr->rules : NULL;
}

DwlError dwl_client_manager_load_rules(DwlClientManager *mgr)
{
    if (!mgr || !mgr->rules)
        return DWL_ERR_INVALID_ARG;

    DwlConfig *cfg = dwl_compositor_get_config(mgr->comp);
    if (!cfg)
        return DWL_ERR_NOT_FOUND;

    // Clear existing rules
    dwl_rule_engine_clear(mgr->rules);

    // Enumerate rule indices (rules.0, rules.1, etc.)
    // Find the highest index by checking for rules.N.app_id or rules.N.title
    for (int i = 0; i < 128; i++) {
        char key_app_id[64], key_title[64], key_tags[64], key_floating[64], key_monitor[64];
        snprintf(key_app_id, sizeof(key_app_id), "rules.%d.app_id", i);
        snprintf(key_title, sizeof(key_title), "rules.%d.title", i);
        snprintf(key_tags, sizeof(key_tags), "rules.%d.tags", i);
        snprintf(key_floating, sizeof(key_floating), "rules.%d.floating", i);
        snprintf(key_monitor, sizeof(key_monitor), "rules.%d.monitor", i);

        // Check if this rule index exists
        if (!dwl_config_has_key(cfg, key_app_id) && !dwl_config_has_key(cfg, key_title))
            continue;

        DwlRule rule = {0};
        rule.app_id_pattern = dwl_config_get_string(cfg, key_app_id, NULL);
        rule.title_pattern = dwl_config_get_string(cfg, key_title, NULL);
        rule.tags = (uint32_t)dwl_config_get_int(cfg, key_tags, 0);
        rule.floating = dwl_config_get_bool(cfg, key_floating, false);
        rule.monitor = dwl_config_get_int(cfg, key_monitor, -1);

        dwl_rule_engine_add(mgr->rules, &rule);
    }

    return DWL_OK;
}
