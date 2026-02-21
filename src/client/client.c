#define _POSIX_C_SOURCE 200809L
#include "client_internal.h"
#include "compositor.h"
#include "config.h"
#include "monitor.h"
#include "session_lock.h"
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

ClientSceneData *swl_client_get_scene_data(SwlClient *client)
{
    return client ? client->scene_data : NULL;
}

void swl_client_set_scene_data(SwlClient *client, ClientSceneData *data)
{
    if (client)
        client->scene_data = data;
}

struct wlr_xdg_toplevel *swl_client_get_xdg_toplevel(SwlClient *client)
{
    return client ? client->xdg : NULL;
}

// Check if a client pointer is still valid (has valid magic number)
bool swl_client_is_valid(SwlClient *client)
{
    if (!client)
        return false;
    return client->magic == SWL_CLIENT_MAGIC;
}

static void client_handle_map(struct wl_listener *listener, void *data);
static void client_handle_unmap(struct wl_listener *listener, void *data);
static void client_handle_destroy(struct wl_listener *listener, void *data);
static void client_handle_commit(struct wl_listener *listener, void *data);
static void client_handle_request_fullscreen(struct wl_listener *listener, void *data);
static void client_handle_set_title(struct wl_listener *listener, void *data);
static void client_handle_set_app_id(struct wl_listener *listener, void *data);

SwlClientManager *swl_client_manager_create(SwlCompositor *comp)
{
    SwlClientManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->comp = comp;
    mgr->next_id = 1;
    wl_list_init(&mgr->clients);
    wl_list_init(&mgr->focus_stack);

    mgr->scene_mgr = swl_scene_manager_create(comp);
    mgr->rules = swl_rule_engine_create();

    // Load rules from config
    swl_client_manager_load_rules(mgr);

    return mgr;
}

void swl_client_manager_destroy(SwlClientManager *mgr)
{
    if (!mgr)
        return;

    SwlClient *c, *tmp;
    wl_list_for_each_safe(c, tmp, &mgr->clients, link) {
        wl_list_remove(&c->link);
        wl_list_remove(&c->flink);
        free(c->app_id);
        free(c->title);
        c->magic = 0;
        free(c);
    }

    swl_scene_manager_destroy(mgr->scene_mgr);
    swl_rule_engine_destroy(mgr->rules);
    free(mgr);
}

SwlClient *swl_client_create_xdg(SwlClientManager *mgr, struct wlr_xdg_toplevel *toplevel)
{
    SwlClient *c = calloc(1, sizeof(*c));
    if (!c)
        return NULL;

    c->magic = SWL_CLIENT_MAGIC;
    c->id = mgr->next_id++;
    c->mgr = mgr;
    c->xdg = toplevel;
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

static void focus_client_internal(SwlClient *c)
{
    if (!c || !c->mgr)
        return;

    SwlCompositor *comp = c->mgr->comp;
    struct wlr_seat *seat = swl_compositor_get_seat(comp);
    struct wlr_surface *surface = NULL;

    if (c->xdg && c->xdg->base)
        surface = c->xdg->base->surface;

#ifdef SWL_XWAYLAND
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

#ifdef SWL_XWAYLAND
    if (c->is_x11 && c->xwayland)
        wlr_xwayland_surface_activate(c->xwayland, true);
#endif

    if (c->scene_data && c->scene_data->tree)
        wlr_scene_node_raise_to_top(&c->scene_data->tree->node);
}

static void unfocus_client_internal(SwlClient *c)
{
    if (!c)
        return;

    if (c->xdg && c->xdg->base && c->xdg->base->initialized)
        wlr_xdg_toplevel_set_activated(c->xdg, false);

#ifdef SWL_XWAYLAND
    if (c->is_x11 && c->xwayland)
        wlr_xwayland_surface_activate(c->xwayland, false);
#endif
}

static void client_handle_map(struct wl_listener *listener, void *data)
{
    SwlClient *c = wl_container_of(listener, c, map);
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

        // Use surface geometry (client's actual size) rather than
        // current state (last server-sent configure, which may be 0,0)
        c->width = c->xdg->base->geometry.width;
        c->height = c->xdg->base->geometry.height;
    }

    // Auto-float windows that have a parent (dialogs) or fixed size
    if (c->xdg) {
        if (c->xdg->parent
            || (c->xdg->current.min_width &&
                c->xdg->current.min_width == c->xdg->current.max_width &&
                c->xdg->current.min_height == c->xdg->current.max_height))
            c->floating = true;
    }

    // Move child windows to parent's monitor
    if (c->xdg && c->xdg->parent) {
        SwlClient *p;
        wl_list_for_each(p, &c->mgr->clients, link) {
            if (p->xdg == c->xdg->parent) {
                if (p->mon && p->mon != c->mon)
                    swl_client_move_to_monitor(c, p->mon);
                break;
            }
        }
    }

    // Apply window rules (may override auto-float)
    if (c->mgr->rules)
        swl_rule_engine_apply(c->mgr->rules, c);

    // Tell tiled clients they're tiled on all edges
    if (!c->floating && c->xdg && c->xdg->base && c->xdg->base->initialized) {
        uint32_t edges = WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT;
        wlr_xdg_toplevel_set_tiled(c->xdg, edges);
    }

    swl_scene_client_create(c->mgr->scene_mgr, c);

    // Place floating clients on the float layer and center on monitor
    if (c->floating && c->mgr->scene_mgr)
        swl_scene_client_set_layer(c->mgr->scene_mgr, c, SWL_LAYER_FLOAT);
    if (c->floating && c->mon) {
        int mx, my, mw, mh;
        swl_monitor_get_usable_area(c->mon, &mx, &my, &mw, &mh);
        int bw = c->border_width;
        c->x = mx + (mw - c->width - 2 * bw) / 2;
        c->y = my + (mh - c->height - 2 * bw) / 2;
    }

    SwlRenderer *renderer = swl_compositor_get_renderer(c->mgr->comp);
    SwlRenderConfig cfg = swl_renderer_get_config(renderer);
    c->border_width = cfg.border_width;  // Sync with config
    swl_scene_update_borders(c, cfg.border_width, cfg.border_color_unfocused);

    // Apply corner radius to surface buffers
    if (cfg.corner_radius > 0)
        swl_scene_client_set_corner_radius(c, cfg.corner_radius);

    swl_client_focus(c);

    // Store the output name for restore-monitor feature
    if (c->mon) {
        struct wlr_output *output = swl_monitor_get_wlr_output(c->mon);
        if (output && output->name) {
            free(c->output_name);
            c->output_name = strdup(output->name);
        }
    }

    SwlEventBus *bus = swl_compositor_get_event_bus(c->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_CLIENT_CREATE, c);

    swl_monitor_arrange(c->mon);
}

static void client_handle_unmap(struct wl_listener *listener, void *data)
{
    SwlClient *c = wl_container_of(listener, c, unmap);
    (void)data;

    c->mapped = false;

    if (c->mgr->focused == c) {
        c->mgr->focused = NULL;
        struct wlr_seat *seat = swl_compositor_get_seat(c->mgr->comp);
        wlr_seat_keyboard_notify_clear_focus(seat);

        SwlClient *next;
        wl_list_for_each(next, &c->mgr->focus_stack, flink) {
            if (next != c && next->mapped) {
                swl_client_focus(next);
                break;
            }
        }
    }

    swl_client_unlink_column(c);
    swl_scene_client_destroy(c->mgr->scene_mgr, c);

    if (c->mon)
        swl_monitor_arrange(c->mon);
}

static void client_handle_destroy(struct wl_listener *listener, void *data)
{
    SwlClient *c = wl_container_of(listener, c, destroy);
    (void)data;

    // Clear focused pointer if this was the focused client
    if (c->mgr->focused == c)
        c->mgr->focused = NULL;

    swl_client_unlink_column(c);

    SwlEventBus *bus = swl_compositor_get_event_bus(c->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_CLIENT_DESTROY, c);

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
    SwlClient *c = wl_container_of(listener, c, commit);
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

    // Re-apply resize on every commit, like swl_mac does
    // This ensures the client receives the configure and resizes properly
    int bw = c->border_width;
    int total_w = c->width + 2 * bw;
    int total_h = c->height + 2 * bw;
    swl_client_resize(c, c->x, c->y, total_w, total_h);
}

static void client_handle_request_fullscreen(struct wl_listener *listener, void *data)
{
    SwlClient *c = wl_container_of(listener, c, request_fullscreen);
    (void)data;

    swl_client_set_fullscreen(c, !c->fullscreen);
}

static void client_handle_set_title(struct wl_listener *listener, void *data)
{
    SwlClient *c = wl_container_of(listener, c, set_title);
    (void)data;

    if (c->xdg && c->xdg->title) {
        free(c->title);
        c->title = strdup(c->xdg->title);
    }
}

static void client_handle_set_app_id(struct wl_listener *listener, void *data)
{
    SwlClient *c = wl_container_of(listener, c, set_app_id);
    (void)data;

    if (c->xdg && c->xdg->app_id) {
        free(c->app_id);
        c->app_id = strdup(c->xdg->app_id);
    }
}

void swl_client_foreach(SwlClientManager *mgr, SwlClientIterator iter, void *data)
{
    if (!mgr || !iter)
        return;

    SwlClient *c, *tmp;
    wl_list_for_each_safe(c, tmp, &mgr->clients, link) {
        if (!iter(c, data))
            break;
    }
}

void swl_client_foreach_visible(SwlClientManager *mgr, SwlMonitor *mon,
                                 SwlClientIterator iter, void *data)
{
    if (!mgr || !iter)
        return;

    SwlClient *c, *tmp;
    wl_list_for_each_safe(c, tmp, &mgr->clients, link) {
        if (!c->mapped)
            continue;
        if (mon && c->mon != mon)
            continue;
        if (!iter(c, data))
            break;
    }
}

SwlClient *swl_client_at(SwlClientManager *mgr, double x, double y)
{
    if (!mgr)
        return NULL;

    SwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (!c->mapped)
            continue;
        if (x >= c->x && x < c->x + c->width + 2 * c->border_width &&
            y >= c->y && y < c->y + c->height + 2 * c->border_width)
            return c;
    }

    return NULL;
}

SwlClient *swl_client_focused(SwlClientManager *mgr)
{
    return mgr ? mgr->focused : NULL;
}

SwlClient *swl_client_focus_top_on_monitor(SwlClientManager *mgr, SwlMonitor *mon)
{
    if (!mgr || !mon)
        return NULL;
    SwlClient *c;
    wl_list_for_each(c, &mgr->focus_stack, flink) {
        if (c->mapped && c->mon == mon)
            return c;
    }
    return NULL;
}

SwlClient *swl_client_by_id(SwlClientManager *mgr, uint32_t id)
{
    if (!mgr)
        return NULL;

    SwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (c->id == id)
            return c;
    }

    return NULL;
}

SwlClient *swl_client_by_surface(SwlClientManager *mgr, struct wlr_surface *surface)
{
    if (!mgr || !surface)
        return NULL;

    SwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        struct wlr_surface *client_surface = swl_client_get_surface(c);
        if (client_surface == surface)
            return c;
    }

    return NULL;
}

size_t swl_client_count(SwlClientManager *mgr)
{
    if (!mgr)
        return 0;

    size_t count = 0;
    SwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        count++;
    }

    return count;
}

size_t swl_client_count_visible(SwlClientManager *mgr, SwlMonitor *mon)
{
    if (!mgr)
        return 0;

    size_t count = 0;

    SwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (!c->mapped)
            continue;
        if (mon && c->mon != mon)
            continue;
        count++;
    }

    return count;
}

SwlClientInfo swl_client_get_info(const SwlClient *client)
{
    SwlClientInfo info = {0};
    if (!client)
        return info;

    info.id = client->id;
    info.app_id = client->app_id;
    info.title = client->title;
    info.geometry.x = client->x;
    info.geometry.y = client->y;
    info.geometry.width = client->width;
    info.geometry.height = client->height;
    info.border_width = client->border_width;
    info.floating = client->floating;
    info.fullscreen = client->fullscreen;
    info.urgent = client->urgent;
    info.focused = client->focused;
#ifdef SWL_XWAYLAND
    info.x11 = client->is_x11;
#endif

    return info;
}

SwlMonitor *swl_client_get_monitor(const SwlClient *client)
{
    return client ? client->mon : NULL;
}

struct wlr_surface *swl_client_get_surface(const SwlClient *client)
{
    if (!client)
        return NULL;

#ifdef SWL_XWAYLAND
    if (client->is_x11 && client->xwayland)
        return client->xwayland->surface;
#endif

    if (client->xdg && client->xdg->base)
        return client->xdg->base->surface;

    return NULL;
}

const char *swl_client_get_output_name(const SwlClient *client)
{
    return client ? client->output_name : NULL;
}

void swl_client_set_monitor_internal(SwlClient *client, SwlMonitor *mon)
{
    if (client)
        client->mon = mon;
}

float swl_client_get_scroller_ratio(const SwlClient *client)
{
    return client ? client->scroller_ratio : 0.0f;
}

SwlError swl_client_set_scroller_ratio(SwlClient *client, float ratio)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

    if (ratio < 0.0f)
        ratio = 0.0f;
    if (ratio > 1.0f)
        ratio = 1.0f;

    client->scroller_ratio = ratio;
    return SWL_OK;
}

bool swl_client_is_column_head(const SwlClient *client)
{
    return client && !client->column_prev;
}

void swl_client_unlink_column(SwlClient *client)
{
    if (!client)
        return;

    if (client->column_prev)
        client->column_prev->column_next = client->column_next;
    if (client->column_next)
        client->column_next->column_prev = client->column_prev;

    client->column_prev = NULL;
    client->column_next = NULL;
}

SwlClient *swl_client_column_next(const SwlClient *client)
{
    return client ? client->column_next : NULL;
}

static SwlClient *column_head(SwlClient *c)
{
    while (c && c->column_prev)
        c = c->column_prev;
    return c;
}

static SwlClient *column_tail(SwlClient *c)
{
    while (c && c->column_next)
        c = c->column_next;
    return c;
}

SwlError swl_client_consume_or_expel(SwlClientManager *mgr, SwlClient *focused, int dir)
{
    if (!mgr || !focused)
        return SWL_ERR_INVALID_ARG;

    if (focused->column_prev || focused->column_next) {
        // EXPEL: client is part of a column stack — remove and make standalone
        // Save column neighbors BEFORE unlinking so we have valid references
        // for repositioning (focused itself may be the head or tail, and after
        // unlink + wl_list_remove its link becomes self-referencing).
        SwlClient *prev_in_col = focused->column_prev;
        SwlClient *next_in_col = focused->column_next;

        swl_client_unlink_column(focused);
        wl_list_remove(&focused->link);

        if (dir < 0) {
            // Insert before the column: find the (new) column head
            SwlClient *new_head = prev_in_col ? column_head(prev_in_col) : next_in_col;
            wl_list_insert(new_head->link.prev, &focused->link);
        } else {
            // Insert after the column: find the (new) column tail
            SwlClient *new_tail = next_in_col ? column_tail(next_in_col) : prev_in_col;
            wl_list_insert(&new_tail->link, &focused->link);
        }
    } else {
        // CONSUME: find adjacent tiled column head and merge
        SwlClient *neighbor = NULL;
        SwlClient *c;
        struct wl_list *pos;

        if (dir > 0) {
            // Walk forward from focused to find the next tiled column head
            for (pos = focused->link.next; pos != &mgr->clients; pos = pos->next) {
                c = wl_container_of(pos, c, link);
                if (!c->mapped || c->floating || c->fullscreen)
                    continue;
                if (c->mon != focused->mon)
                    continue;
                if (swl_client_is_column_head(c)) {
                    neighbor = c;
                    break;
                }
            }
        } else {
            // Walk backward from focused to find the previous tiled column head
            for (pos = focused->link.prev; pos != &mgr->clients; pos = pos->prev) {
                c = wl_container_of(pos, c, link);
                if (!c->mapped || c->floating || c->fullscreen)
                    continue;
                if (c->mon != focused->mon)
                    continue;
                // Any tiled client we find belongs to some column; find its head
                neighbor = column_head(c);
                break;
            }
        }

        if (!neighbor)
            return SWL_ERR_NOT_FOUND;

        // Merge: append focused to the neighbor's column
        SwlClient *ntail = column_tail(neighbor);
        ntail->column_next = focused;
        focused->column_prev = ntail;

        // Move focused in wl_list to right after the neighbor's tail
        wl_list_remove(&focused->link);
        wl_list_insert(&ntail->link, &focused->link);
    }

    if (focused->mon)
        swl_monitor_arrange(focused->mon);

    return SWL_OK;
}

SwlError swl_client_close(SwlClient *client)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

#ifdef SWL_XWAYLAND
    if (client->is_x11 && client->xwayland) {
        wlr_xwayland_surface_close(client->xwayland);
        return SWL_OK;
    }
#endif

    if (client->xdg) {
        wlr_xdg_toplevel_send_close(client->xdg);
        return SWL_OK;
    }

    return SWL_ERR_INVALID_ARG;
}

SwlError swl_client_focus(SwlClient *client)
{
    if (!client || !client->mgr)
        return SWL_ERR_INVALID_ARG;

    // Don't allow focus to regular clients while session is locked
    SwlSessionLock *session_lock = swl_compositor_get_session_lock(client->mgr->comp);
    if (swl_session_lock_is_locked(session_lock))
        return SWL_OK;

    SwlClient *old = client->mgr->focused;
    if (old == client)
        return SWL_OK;

    SwlRenderer *renderer = swl_compositor_get_renderer(client->mgr->comp);
    SwlRenderConfig cfg = swl_renderer_get_config(renderer);

    // Validate old client is still valid (guards against stale pointers)
    if (swl_client_is_valid(old)) {
        old->focused = false;
        unfocus_client_internal(old);

        swl_scene_update_borders(old, cfg.border_width, cfg.border_color_unfocused);
        swl_scene_client_set_opacity(old, cfg.opacity_inactive);

        SwlEventBus *bus = swl_compositor_get_event_bus(client->mgr->comp);
        swl_event_bus_emit_simple(bus, SWL_EVENT_CLIENT_UNFOCUS, old);
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

    swl_scene_update_borders(client, cfg.border_width, cfg.border_color_focused);
    swl_scene_client_set_opacity(client, cfg.opacity_active);

    SwlEventBus *bus = swl_compositor_get_event_bus(client->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_CLIENT_FOCUS, client);

    // Re-arrange for layouts that depend on focus (e.g., scroller)
    if (client->mon)
        swl_monitor_arrange(client->mon);

    return SWL_OK;
}

SwlError swl_client_set_floating(SwlClient *client, bool floating)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

    if (floating)
        swl_client_unlink_column(client);

    client->floating = floating;

    if (client->mgr->scene_mgr) {
        SwlSceneLayer layer = floating ? SWL_LAYER_FLOAT : SWL_LAYER_TILES;
        swl_scene_client_set_layer(client->mgr->scene_mgr, client, layer);
    }

    SwlEventBus *bus = swl_compositor_get_event_bus(client->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_CLIENT_FLOAT, client);

    if (client->mon)
        swl_monitor_arrange(client->mon);

    return SWL_OK;
}

SwlError swl_client_toggle_floating(SwlClient *client)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

    return swl_client_set_floating(client, !client->floating);
}

SwlError swl_client_set_fullscreen(SwlClient *client, bool fullscreen)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

    if (fullscreen)
        swl_client_unlink_column(client);

    client->fullscreen = fullscreen;

    if (client->xdg && client->xdg->base && client->xdg->base->initialized)
        wlr_xdg_toplevel_set_fullscreen(client->xdg, fullscreen);

    if (client->mgr->scene_mgr) {
        SwlSceneLayer layer = fullscreen ? SWL_LAYER_FULLSCREEN :
            (client->floating ? SWL_LAYER_FLOAT : SWL_LAYER_TILES);
        swl_scene_client_set_layer(client->mgr->scene_mgr, client, layer);
    }

    SwlEventBus *bus = swl_compositor_get_event_bus(client->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_CLIENT_FULLSCREEN, client);

    if (client->mon)
        swl_monitor_arrange(client->mon);

    return SWL_OK;
}

SwlError swl_client_toggle_fullscreen(SwlClient *client)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

    return swl_client_set_fullscreen(client, !client->fullscreen);
}

SwlError swl_client_move_to_monitor(SwlClient *client, SwlMonitor *mon)
{
    if (!client || !mon)
        return SWL_ERR_INVALID_ARG;

    swl_client_unlink_column(client);

    SwlMonitor *old = client->mon;
    client->mon = mon;

    // Center floating windows on the new monitor
    if (client->floating) {
        int mx, my, mw, mh;
        swl_monitor_get_usable_area(mon, &mx, &my, &mw, &mh);
        int bw = client->border_width;
        client->x = mx + (mw - client->width - 2 * bw) / 2;
        client->y = my + (mh - client->height - 2 * bw) / 2;
    }

    // Update the stored output name for restore-monitor feature
    struct wlr_output *output = swl_monitor_get_wlr_output(mon);
    if (output && output->name) {
        free(client->output_name);
        client->output_name = strdup(output->name);
    }

    if (old && old != mon)
        swl_monitor_arrange(old);
    swl_monitor_arrange(mon);

    return SWL_OK;
}

SwlError swl_client_resize(SwlClient *client, int x, int y, int w, int h)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

    // w and h are TOTAL geometry (including borders), like swl_mac
    // Calculate content size by subtracting borders
    int bw = client->border_width;
    int content_w = w - 2 * bw;
    int content_h = h - 2 * bw;

    client->x = x;
    client->y = y;
    client->width = content_w;
    client->height = content_h;

    swl_scene_client_set_position(client, x, y);
    swl_scene_client_set_size(client, content_w, content_h);

#ifdef SWL_XWAYLAND
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
        swl_monitor_get_usable_area(client->mon, &mx, &my, &mw, &mh);

        int client_left = x;
        int client_top = y;
        int client_right = x + w;
        int client_bottom = y + h;

        // Check if client is completely outside monitor bounds
        if (client_right <= mx || client_left >= mx + mw ||
            client_bottom <= my || client_top >= my + mh) {
            // Completely outside - hide the client
            swl_scene_client_set_visible(client, false);
        } else if (client_left < mx || client_top < my ||
                   client_right > mx + mw || client_bottom > my + mh) {
            // Partially visible - show and clip
            swl_scene_client_set_visible(client, true);

            // Calculate clip box in client-local coords (tree top-left is 0,0)
            int clip_x = (client_left < mx) ? (mx - client_left) : 0;
            int clip_y = (client_top < my) ? (my - client_top) : 0;
            int clip_right = (client_right > mx + mw) ? (mx + mw - client_left) : w;
            int clip_bottom = (client_bottom > my + mh) ? (my + mh - client_top) : h;
            int clip_w = clip_right - clip_x;
            int clip_h = clip_bottom - clip_y;

            if (clip_w > 0 && clip_h > 0) {
                swl_scene_client_set_clip(client, clip_x, clip_y, clip_w, clip_h);
            }
        } else {
            // Client fully within monitor - show and clear any clip
            swl_scene_client_set_visible(client, true);
            swl_scene_client_clear_clip(client);
        }
    }

    SwlEventBus *bus = swl_compositor_get_event_bus(client->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_CLIENT_RESIZE, client);

    return SWL_OK;
}

SwlError swl_client_set_border_color(SwlClient *client, const float color[4])
{
    if (!client || !color)
        return SWL_ERR_INVALID_ARG;

    swl_scene_update_borders(client, client->border_width, color);
    return SWL_OK;
}

SwlError swl_client_set_border_width(SwlClient *client, int width)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

    client->border_width = width;
    return SWL_OK;
}

SwlError swl_client_set_urgent(SwlClient *client, bool urgent)
{
    if (!client)
        return SWL_ERR_INVALID_ARG;

    client->urgent = urgent;

    // Update border color if mapped
    if (client->mapped && !client->focused) {
        SwlRenderer *renderer = swl_compositor_get_renderer(client->mgr->comp);
        SwlRenderConfig cfg = swl_renderer_get_config(renderer);
        const float *color = urgent ? cfg.border_color_urgent : cfg.border_color_unfocused;
        swl_scene_update_borders(client, cfg.border_width, color);
    }

    return SWL_OK;
}

SwlError swl_client_zoom(SwlClientManager *mgr)
{
    if (!mgr)
        return SWL_ERR_INVALID_ARG;

    SwlClient *focused = mgr->focused;
    if (!focused || !focused->mapped)
        return SWL_ERR_NOT_FOUND;

    // Find the first tiled client on the same monitor
    SwlClient *first = NULL;
    SwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (!c->mapped || c->floating || c->fullscreen)
            continue;
        if (c->mon != focused->mon)
            continue;
        first = c;
        break;
    }

    if (!first || first == focused)
        return SWL_OK;

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
        swl_monitor_arrange(focused->mon);

    return SWL_OK;
}

SwlClient *swl_client_in_direction(SwlClientManager *mgr, SwlClient *from, int direction)
{
    if (!mgr || !from)
        return NULL;

    int from_cx = from->x + from->width / 2;
    int from_cy = from->y + from->height / 2;

    SwlClient *best = NULL;
    int best_dist = INT_MAX;

    SwlClient *c;
    wl_list_for_each(c, &mgr->clients, link) {
        if (c == from || !c->mapped)
            continue;
        if (c->mon != from->mon)
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

SwlSceneManager *swl_client_manager_get_scene(SwlClientManager *mgr)
{
    return mgr ? mgr->scene_mgr : NULL;
}

SwlRuleEngine *swl_client_manager_get_rules(SwlClientManager *mgr)
{
    return mgr ? mgr->rules : NULL;
}

SwlError swl_client_manager_load_rules(SwlClientManager *mgr)
{
    if (!mgr || !mgr->rules)
        return SWL_ERR_INVALID_ARG;

    SwlConfig *cfg = swl_compositor_get_config(mgr->comp);
    if (!cfg)
        return SWL_ERR_NOT_FOUND;

    // Clear existing rules
    swl_rule_engine_clear(mgr->rules);

    // Enumerate rule indices (rules.0, rules.1, etc.)
    // Find the highest index by checking for rules.N.app_id or rules.N.title
    for (int i = 0; i < 128; i++) {
        char key_app_id[64], key_title[64], key_floating[64], key_monitor[64];
        snprintf(key_app_id, sizeof(key_app_id), "rules.%d.app_id", i);
        snprintf(key_title, sizeof(key_title), "rules.%d.title", i);
        snprintf(key_floating, sizeof(key_floating), "rules.%d.floating", i);
        snprintf(key_monitor, sizeof(key_monitor), "rules.%d.monitor", i);

        // Check if this rule index exists
        if (!swl_config_has_key(cfg, key_app_id) && !swl_config_has_key(cfg, key_title))
            continue;

        SwlRule rule = {0};
        rule.app_id_pattern = swl_config_get_string(cfg, key_app_id, NULL);
        rule.title_pattern = swl_config_get_string(cfg, key_title, NULL);
        rule.floating = swl_config_get_bool(cfg, key_floating, false);
        rule.monitor = swl_config_get_int(cfg, key_monitor, -1);

        swl_rule_engine_add(mgr->rules, &rule);
    }

    return SWL_OK;
}
