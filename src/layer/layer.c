#define _POSIX_C_SOURCE 200809L
#include "layer.h"
#include "client.h"
#include "compositor.h"
#include "monitor.h"
#include "scene.h"
#include "events.h"
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>

struct SwlLayerSurface {
    SwlLayerManager *mgr;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;
    SwlMonitor *mon;

    bool mapped;
    int x, y;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;

    struct wl_list link;
};

struct SwlLayerManager {
    SwlCompositor *comp;
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_list surfaces;

    struct wl_listener new_surface;
};

static struct wlr_scene_tree *layer_to_scene_tree(SwlLayerManager *mgr, enum zwlr_layer_shell_v1_layer layer)
{
    SwlClientManager *clients = swl_compositor_get_clients(mgr->comp);
    SwlSceneManager *scene_mgr = swl_client_manager_get_scene(clients);

    switch (layer) {
    case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND:
        return swl_scene_get_layer(scene_mgr, SWL_LAYER_BACKGROUND);
    case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:
        return swl_scene_get_layer(scene_mgr, SWL_LAYER_BOTTOM);
    case ZWLR_LAYER_SHELL_V1_LAYER_TOP:
        return swl_scene_get_layer(scene_mgr, SWL_LAYER_TOP);
    case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:
        return swl_scene_get_layer(scene_mgr, SWL_LAYER_OVERLAY);
    default:
        return swl_scene_get_layer(scene_mgr, SWL_LAYER_TOP);
    }
}

static void layer_surface_handle_map(struct wl_listener *listener, void *data)
{
    SwlLayerSurface *surface = wl_container_of(listener, surface, map);
    (void)data;

    surface->mapped = true;
    swl_layer_arrange(surface->mgr, surface->mon);

    // Update keyboard focus if requested
    if (surface->layer_surface->current.keyboard_interactive &&
        surface->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
        struct wlr_seat *seat = swl_compositor_get_seat(surface->mgr->comp);
        struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
        if (kb)
            wlr_seat_keyboard_notify_enter(seat, surface->layer_surface->surface,
                kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }

    SwlEventBus *bus = swl_compositor_get_event_bus(surface->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_LAYER_MAP, surface);
}

static void layer_surface_handle_unmap(struct wl_listener *listener, void *data)
{
    SwlLayerSurface *surface = wl_container_of(listener, surface, unmap);
    (void)data;

    surface->mapped = false;
    swl_layer_arrange(surface->mgr, surface->mon);

    // Restore keyboard focus to the previously focused client
    if (surface->layer_surface->current.keyboard_interactive) {
        SwlClientManager *clients = swl_compositor_get_clients(surface->mgr->comp);
        SwlClient *focused = swl_client_focused(clients);
        if (focused)
            swl_client_focus(focused);
        else {
            struct wlr_seat *seat = swl_compositor_get_seat(surface->mgr->comp);
            wlr_seat_keyboard_notify_clear_focus(seat);
        }
    }

    SwlEventBus *bus = swl_compositor_get_event_bus(surface->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_LAYER_UNMAP, surface);
}

static void layer_surface_handle_destroy(struct wl_listener *listener, void *data)
{
    SwlLayerSurface *surface = wl_container_of(listener, surface, destroy);
    (void)data;

    wl_list_remove(&surface->map.link);
    wl_list_remove(&surface->unmap.link);
    wl_list_remove(&surface->destroy.link);
    wl_list_remove(&surface->commit.link);
    wl_list_remove(&surface->link);

    if (surface->mapped)
        swl_layer_arrange(surface->mgr, surface->mon);

    free(surface);
}

static void layer_surface_handle_commit(struct wl_listener *listener, void *data)
{
    SwlLayerSurface *surface = wl_container_of(listener, surface, commit);
    (void)data;

    if (!surface->layer_surface->initialized)
        return;

    // Check for layer change
    uint32_t committed = surface->layer_surface->current.committed;
    if (committed & WLR_LAYER_SURFACE_V1_STATE_LAYER) {
        struct wlr_scene_tree *new_tree = layer_to_scene_tree(surface->mgr,
            surface->layer_surface->current.layer);
        wlr_scene_node_reparent(&surface->scene_layer_surface->tree->node, new_tree);
    }

    // Arrange to send configure (initial or update)
    swl_layer_arrange(surface->mgr, surface->mon);
}

static void handle_new_surface(struct wl_listener *listener, void *data)
{
    SwlLayerManager *mgr = wl_container_of(listener, mgr, new_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;

    SwlLayerSurface *surface = calloc(1, sizeof(*surface));
    if (!surface) {
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    surface->mgr = mgr;
    surface->layer_surface = layer_surface;

    // Find the output
    SwlOutputManager *output_mgr = swl_compositor_get_output(mgr->comp);
    if (layer_surface->output) {
        struct wlr_output *target = layer_surface->output;
        // Find monitor by wlr_output
        for (size_t i = 0; i < swl_monitor_count(output_mgr); i++) {
            SwlMonitor *mon = swl_monitor_by_index(output_mgr, i);
            if (swl_monitor_get_wlr_output(mon) == target) {
                surface->mon = mon;
                break;
            }
        }
        if (!surface->mon)
            surface->mon = swl_monitor_get_focused(output_mgr);
    } else {
        surface->mon = swl_monitor_get_focused(output_mgr);
        if (surface->mon) {
            // Set the output on the layer surface
            layer_surface->output = swl_monitor_get_wlr_output(surface->mon);
        }
    }

    if (!surface->mon) {
        free(surface);
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    // Create scene layer surface
    struct wlr_scene_tree *tree = layer_to_scene_tree(mgr, layer_surface->pending.layer);
    surface->scene_layer_surface = wlr_scene_layer_surface_v1_create(tree, layer_surface);
    if (!surface->scene_layer_surface) {
        free(surface);
        wlr_layer_surface_v1_destroy(layer_surface);
        return;
    }

    layer_surface->data = surface;
    surface->scene_layer_surface->tree->node.data = surface;

    // Set up listeners
    surface->map.notify = layer_surface_handle_map;
    wl_signal_add(&layer_surface->surface->events.map, &surface->map);

    surface->unmap.notify = layer_surface_handle_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &surface->unmap);

    surface->destroy.notify = layer_surface_handle_destroy;
    wl_signal_add(&layer_surface->events.destroy, &surface->destroy);

    surface->commit.notify = layer_surface_handle_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &surface->commit);

    wl_list_insert(&mgr->surfaces, &surface->link);
}

SwlLayerManager *swl_layer_manager_create(SwlCompositor *comp)
{
    SwlLayerManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->comp = comp;
    wl_list_init(&mgr->surfaces);

    struct wl_display *display = swl_compositor_get_wl_display(comp);
    mgr->layer_shell = wlr_layer_shell_v1_create(display, 4);
    if (!mgr->layer_shell) {
        free(mgr);
        return NULL;
    }

    mgr->new_surface.notify = handle_new_surface;
    wl_signal_add(&mgr->layer_shell->events.new_surface, &mgr->new_surface);

    return mgr;
}

void swl_layer_manager_destroy(SwlLayerManager *mgr)
{
    if (!mgr)
        return;

    wl_list_remove(&mgr->new_surface.link);

    SwlLayerSurface *s, *tmp;
    wl_list_for_each_safe(s, tmp, &mgr->surfaces, link) {
        wl_list_remove(&s->link);
        // Don't remove listeners - they'll be cleaned up when surface is destroyed
    }

    free(mgr);
}

static void apply_exclusive_zone(struct wlr_layer_surface_v1_state *state,
                                  int *usable_x, int *usable_y,
                                  int *usable_w, int *usable_h)
{
    uint32_t anchor = state->anchor;
    int32_t exclusive = state->exclusive_zone;

    if (exclusive <= 0)
        return;

    // Exclusive zone for surfaces anchored to one edge
    if (anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP ||
        (anchor == (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                    ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))) {
        *usable_y += exclusive;
        *usable_h -= exclusive;
    } else if (anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM ||
               (anchor == (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))) {
        *usable_h -= exclusive;
    } else if (anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT ||
               (anchor == (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))) {
        *usable_x += exclusive;
        *usable_w -= exclusive;
    } else if (anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT ||
               (anchor == (ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                           ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))) {
        *usable_w -= exclusive;
    }
}

void swl_layer_arrange(SwlLayerManager *mgr, SwlMonitor *mon)
{
    if (!mgr || !mon)
        return;

    SwlMonitorInfo info = swl_monitor_get_info(mon);
    int full_x = info.x, full_y = info.y;
    int full_w = info.width, full_h = info.height;

    int usable_x = full_x, usable_y = full_y;
    int usable_w = full_w, usable_h = full_h;

    // Process layers in order: background, bottom, top, overlay
    // Apply exclusive zones for each layer
    enum zwlr_layer_shell_v1_layer layers[] = {
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND,
        ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM,
        ZWLR_LAYER_SHELL_V1_LAYER_TOP,
        ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
    };

    for (int i = 0; i < 4; i++) {
        SwlLayerSurface *surface;
        wl_list_for_each(surface, &mgr->surfaces, link) {
            if (surface->mon != mon)
                continue;
            // Only configure surfaces that have been initialized
            if (!surface->layer_surface->initialized)
                continue;
            if (surface->layer_surface->current.layer != layers[i])
                continue;

            struct wlr_layer_surface_v1_state *state = &surface->layer_surface->current;
            struct wlr_scene_layer_surface_v1 *scene_surface = surface->scene_layer_surface;

            // Use full area for background/bottom, usable for top/overlay
            int box_x, box_y, box_w, box_h;
            if (layers[i] == ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND ||
                layers[i] == ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM) {
                box_x = full_x; box_y = full_y;
                box_w = full_w; box_h = full_h;
            } else {
                box_x = usable_x; box_y = usable_y;
                box_w = usable_w; box_h = usable_h;
            }

            struct wlr_box bounds = {
                .x = box_x,
                .y = box_y,
                .width = box_w,
                .height = box_h,
            };

            wlr_scene_layer_surface_v1_configure(scene_surface, &bounds, &bounds);

            // Track surface position
            surface->x = scene_surface->tree->node.x;
            surface->y = scene_surface->tree->node.y;

            // Apply exclusive zone
            if (surface->layer_surface->surface->mapped)
                apply_exclusive_zone(state, &usable_x, &usable_y, &usable_w, &usable_h);
        }
    }

    // Update monitor's usable area
    swl_monitor_set_usable_area(mon, usable_x, usable_y, usable_w, usable_h);

    // Re-arrange clients after usable area changed
    swl_monitor_arrange(mon);
}

void swl_layer_get_exclusive_zone(SwlLayerManager *mgr, SwlMonitor *mon,
                                   int *top, int *bottom, int *left, int *right)
{
    if (!mgr || !mon) {
        if (top) *top = 0;
        if (bottom) *bottom = 0;
        if (left) *left = 0;
        if (right) *right = 0;
        return;
    }

    int t = 0, b = 0, l = 0, r = 0;

    SwlLayerSurface *surface;
    wl_list_for_each(surface, &mgr->surfaces, link) {
        if (surface->mon != mon || !surface->layer_surface->surface->mapped)
            continue;

        struct wlr_layer_surface_v1_state *state = &surface->layer_surface->current;
        uint32_t anchor = state->anchor;
        int32_t exclusive = state->exclusive_zone;

        if (exclusive <= 0)
            continue;

        // Top-anchored
        if (anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP ||
            (anchor == (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                        ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                        ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))) {
            t += exclusive;
        }
        // Bottom-anchored
        else if (anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM ||
                 (anchor == (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT))) {
            b += exclusive;
        }
        // Left-anchored
        else if (anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT ||
                 (anchor == (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))) {
            l += exclusive;
        }
        // Right-anchored
        else if (anchor == ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT ||
                 (anchor == (ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                             ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM))) {
            r += exclusive;
        }
    }

    if (top) *top = t;
    if (bottom) *bottom = b;
    if (left) *left = l;
    if (right) *right = r;
}

void swl_layer_foreach(SwlLayerManager *mgr, SwlLayerSurfaceIterator iter, void *data)
{
    if (!mgr || !iter)
        return;

    SwlLayerSurface *s, *tmp;
    wl_list_for_each_safe(s, tmp, &mgr->surfaces, link) {
        if (!iter(s, data))
            break;
    }
}

void swl_layer_foreach_on_monitor(SwlLayerManager *mgr, SwlMonitor *mon,
                                   SwlLayerSurfaceIterator iter, void *data)
{
    if (!mgr || !mon || !iter)
        return;

    SwlLayerSurface *s, *tmp;
    wl_list_for_each_safe(s, tmp, &mgr->surfaces, link) {
        if (s->mon != mon)
            continue;
        if (!iter(s, data))
            break;
    }
}

SwlLayerSurfaceInfo swl_layer_surface_get_info(const SwlLayerSurface *surface)
{
    SwlLayerSurfaceInfo info = {0};
    if (!surface || !surface->layer_surface)
        return info;

    info.namespace = surface->layer_surface->namespace;
    info.x = surface->x;
    info.y = surface->y;
    info.width = surface->layer_surface->current.actual_width;
    info.height = surface->layer_surface->current.actual_height;
    info.layer = (SwlLayerShellLayer)surface->layer_surface->current.layer;
    info.mapped = surface->mapped;
    info.keyboard_interactive = surface->layer_surface->current.keyboard_interactive;
    info.anchor = surface->layer_surface->current.anchor;
    info.exclusive_zone = surface->layer_surface->current.exclusive_zone;

    return info;
}

struct wlr_surface *swl_layer_surface_get_wlr_surface(const SwlLayerSurface *surface)
{
    if (!surface || !surface->layer_surface)
        return NULL;
    return surface->layer_surface->surface;
}

SwlMonitor *swl_layer_surface_get_monitor(const SwlLayerSurface *surface)
{
    return surface ? surface->mon : NULL;
}
