#define _POSIX_C_SOURCE 200809L
#include "monitor.h"
#include "compositor.h"
#include "config.h"
#include "layout.h"
#include "client.h"
#include "layer.h"
#include "events.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_damage_ring.h>
#include <scenefx/types/wlr_scene.h>

struct SwlMonitor {
    uint32_t id;
    SwlOutputManager *mgr;
    struct wlr_output *output;
    struct wlr_scene_output *scene_output;

    int x, y, width, height;
    int usable_x, usable_y, usable_width, usable_height;

    const SwlLayout *layout;
    const SwlLayout *prev_layout;

    float mfact;
    float scroller_ratio;
    int nmaster;
    int gap_inner_h, gap_inner_v;
    int gap_outer_h, gap_outer_v;

    struct wl_listener frame;
    struct wl_listener destroy;
    struct wl_listener request_state;

    struct wl_list link;
};

struct SwlOutputManager {
    SwlCompositor *comp;
    struct wlr_output_layout *layout;
    struct wl_list monitors;
    SwlMonitor *focused;
    uint32_t next_id;

    struct wlr_output_manager_v1 *output_mgmt;
    struct wl_listener output_mgmt_apply;
    struct wl_listener output_mgmt_test;

    struct wl_listener new_output;
    struct wl_listener layout_change;
};

static void handle_frame(struct wl_listener *listener, void *data);
static void handle_destroy(struct wl_listener *listener, void *data);
static void handle_request_state(struct wl_listener *listener, void *data);
static void handle_new_output(struct wl_listener *listener, void *data);
static void handle_layout_change(struct wl_listener *listener, void *data);
static void handle_output_mgmt_apply(struct wl_listener *listener, void *data);
static void handle_output_mgmt_test(struct wl_listener *listener, void *data);
static void update_output_management(SwlOutputManager *mgr);
static void apply_monitor_rules(SwlMonitor *mon);

/* Data structure for restore_client_to_monitor callback */
typedef struct {
    SwlMonitor *mon;
    const char *output_name;
    bool restored;
} RestoreMonitorData;

static bool restore_client_to_monitor(SwlClient *c, void *data)
{
    RestoreMonitorData *rd = data;
    const char *client_output = swl_client_get_output_name(c);
    if (client_output && strcmp(client_output, rd->output_name) == 0) {
        swl_client_set_monitor_internal(c, rd->mon);
        rd->restored = true;
    }
    return true;
}

SwlOutputManager *swl_output_create(SwlCompositor *comp)
{
    SwlOutputManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->comp = comp;
    mgr->next_id = 1;
    wl_list_init(&mgr->monitors);

    // Use the compositor's output layout (shared with XDG output manager)
    mgr->layout = swl_compositor_get_output_layout(comp);

    struct wlr_backend *backend = swl_compositor_get_backend(comp);
    mgr->new_output.notify = handle_new_output;
    wl_signal_add(&backend->events.new_output, &mgr->new_output);

    mgr->layout_change.notify = handle_layout_change;
    wl_signal_add(&mgr->layout->events.change, &mgr->layout_change);

    struct wl_display *display = swl_compositor_get_wl_display(comp);
    mgr->output_mgmt = wlr_output_manager_v1_create(display);
    if (mgr->output_mgmt) {
        mgr->output_mgmt_apply.notify = handle_output_mgmt_apply;
        wl_signal_add(&mgr->output_mgmt->events.apply, &mgr->output_mgmt_apply);
        mgr->output_mgmt_test.notify = handle_output_mgmt_test;
        wl_signal_add(&mgr->output_mgmt->events.test, &mgr->output_mgmt_test);
    }

    return mgr;
}

void swl_output_destroy(SwlOutputManager *mgr)
{
    if (!mgr)
        return;

    if (mgr->output_mgmt) {
        wl_list_remove(&mgr->output_mgmt_apply.link);
        wl_list_remove(&mgr->output_mgmt_test.link);
    }
    wl_list_remove(&mgr->new_output.link);
    wl_list_remove(&mgr->layout_change.link);

    SwlMonitor *mon, *tmp;
    wl_list_for_each_safe(mon, tmp, &mgr->monitors, link) {
        // Remove all listeners before freeing to prevent use-after-free
        // when backend destroy triggers output destroy signals
        wl_list_remove(&mon->frame.link);
        wl_list_remove(&mon->destroy.link);
        wl_list_remove(&mon->request_state.link);
        wl_list_remove(&mon->link);
        free(mon);
    }

    // Note: layout is owned by compositor, not destroyed here
    free(mgr);
}

static void handle_new_output(struct wl_listener *listener, void *data)
{
    SwlOutputManager *mgr = wl_container_of(listener, mgr, new_output);
    struct wlr_output *output = data;

    wlr_output_init_render(output,
        swl_compositor_get_allocator(mgr->comp),
        swl_compositor_get_wlr_renderer(mgr->comp));

    SwlMonitor *mon = calloc(1, sizeof(*mon));
    if (!mon)
        return;

    mon->id = mgr->next_id++;
    mon->mgr = mgr;
    mon->output = output;
    SwlConfig *cfg = swl_compositor_get_config(mgr->comp);
    mon->mfact = swl_config_get_float(cfg, "appearance.mfact", 0.55f);
    mon->scroller_ratio = swl_config_get_float(cfg, "appearance.scroller_ratio", 0.8f);
    mon->nmaster = swl_config_get_int(cfg, "appearance.nmaster", 1);
    mon->gap_inner_h = swl_config_get_int(cfg, "appearance.gap_inner_h", 10);
    mon->gap_inner_v = swl_config_get_int(cfg, "appearance.gap_inner_v", 10);
    mon->gap_outer_h = swl_config_get_int(cfg, "appearance.gap_outer_h", 10);
    mon->gap_outer_v = swl_config_get_int(cfg, "appearance.gap_outer_v", 10);

    // Set default layout
    SwlLayoutRegistry *layouts = swl_compositor_get_layouts(mgr->comp);
    const char *layout_name = swl_config_get_string(cfg, "appearance.layout", "scroller");
    mon->layout = swl_layout_get(layouts, layout_name);
    if (!mon->layout)
        mon->layout = swl_layout_get(layouts, "scroller");

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
    if (mode)
        wlr_output_state_set_mode(&state, mode);

    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    // Add to layout first - this triggers automatic scene output creation
    wlr_output_layout_add_auto(mgr->layout, output);

    // Get the scene output that was automatically created by wlr_scene_attach_output_layout
    struct wlr_scene *scene = swl_compositor_get_scene(mgr->comp);
    mon->scene_output = wlr_scene_get_scene_output(scene, output);
    if (!mon->scene_output) {
        // Fallback: create manually if automatic creation didn't happen
        mon->scene_output = wlr_scene_output_create(scene, output);
        if (!mon->scene_output) {
            free(mon);
            return;
        }
    }

    struct wlr_output_layout_output *l_output = wlr_output_layout_get(mgr->layout, output);
    if (l_output) {
        mon->x = l_output->x;
        mon->y = l_output->y;
    }
    mon->width = output->width;
    mon->height = output->height;
    mon->usable_x = mon->x;
    mon->usable_y = mon->y;
    mon->usable_width = mon->width;
    mon->usable_height = mon->height;

    // Manually set scene output position to match layout
    wlr_scene_output_set_position(mon->scene_output, mon->x, mon->y);

    fprintf(stderr, "Monitor %s: pos=(%d,%d) size=%dx%d scene_output=(%d,%d)\n",
            output->name, mon->x, mon->y, mon->width, mon->height,
            mon->scene_output->x, mon->scene_output->y);

    mon->frame.notify = handle_frame;
    wl_signal_add(&output->events.frame, &mon->frame);

    mon->destroy.notify = handle_destroy;
    wl_signal_add(&output->events.destroy, &mon->destroy);

    mon->request_state.notify = handle_request_state;
    wl_signal_add(&output->events.request_state, &mon->request_state);

    wl_list_insert(&mgr->monitors, &mon->link);

    // Apply monitor-specific rules from config
    apply_monitor_rules(mon);

    if (!mgr->focused)
        mgr->focused = mon;

    // Restore clients that were previously on this monitor
    SwlClientManager *clients = swl_compositor_get_clients(mgr->comp);
    if (clients) {
        RestoreMonitorData rdata = {
            .mon = mon,
            .output_name = output->name,
            .restored = false
        };

        swl_client_foreach(clients, restore_client_to_monitor, &rdata);

        if (rdata.restored) {
            swl_monitor_arrange(mon);
        }
    }

    SwlEventBus *bus = swl_compositor_get_event_bus(mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_MONITOR_ADD, mon);

    update_output_management(mgr);
}

static void handle_frame(struct wl_listener *listener, void *data)
{
    SwlMonitor *mon = wl_container_of(listener, mon, frame);
    (void)data;

    if (!mon->output->enabled)
        return;

    wlr_scene_output_commit(mon->scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(mon->scene_output, &now);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    SwlMonitor *mon = wl_container_of(listener, mon, destroy);
    (void)data;

    if (mon->mgr && mon->mgr->comp) {
        SwlEventBus *bus = swl_compositor_get_event_bus(mon->mgr->comp);
        if (bus)
            swl_event_bus_emit_simple(bus, SWL_EVENT_MONITOR_REMOVE, mon);
    }

    // Detach layer surfaces before freeing so late unmap handlers
    // don't dereference a freed monitor pointer.
    SwlLayerManager *layers = swl_compositor_get_layer_manager(mon->mgr->comp);
    swl_layer_cleanup_monitor(layers, mon);

    wl_list_remove(&mon->frame.link);
    wl_list_remove(&mon->destroy.link);
    wl_list_remove(&mon->request_state.link);
    wl_list_remove(&mon->link);

    if (mon->mgr->focused == mon) {
        if (!wl_list_empty(&mon->mgr->monitors))
            mon->mgr->focused = wl_container_of(mon->mgr->monitors.next, mon->mgr->focused, link);
        else
            mon->mgr->focused = NULL;
    }

    SwlOutputManager *mgr = mon->mgr;
    free(mon);
    update_output_management(mgr);
}

static void handle_request_state(struct wl_listener *listener, void *data)
{
    SwlMonitor *mon = wl_container_of(listener, mon, request_state);
    struct wlr_output_event_request_state *event = data;

    wlr_output_commit_state(mon->output, event->state);
    update_output_management(mon->mgr);
}

static void handle_layout_change(struct wl_listener *listener, void *data)
{
    SwlOutputManager *mgr = wl_container_of(listener, mgr, layout_change);
    (void)data;

    SwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        struct wlr_output_layout_output *l_output = wlr_output_layout_get(mgr->layout, mon->output);
        if (l_output) {
            mon->x = l_output->x;
            mon->y = l_output->y;
        }
        mon->width = mon->output->width;
        mon->height = mon->output->height;

        // Update usable area to match new geometry
        mon->usable_x = mon->x;
        mon->usable_y = mon->y;
        mon->usable_width = mon->width;
        mon->usable_height = mon->height;

        // Update scene output position to match layout
        if (mon->scene_output) {
            wlr_scene_output_set_position(mon->scene_output, mon->x, mon->y);
            fprintf(stderr, "Layout change: %s pos=(%d,%d) scene_output=(%d,%d)\n",
                    mon->output->name, mon->x, mon->y,
                    mon->scene_output->x, mon->scene_output->y);
        }
    }

    update_output_management(mgr);
}

static struct wlr_output_configuration_v1 *create_output_configuration(
    SwlOutputManager *mgr)
{
    struct wlr_output_configuration_v1 *config =
        wlr_output_configuration_v1_create();
    if (!config)
        return NULL;

    SwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        struct wlr_output_configuration_head_v1 *head =
            wlr_output_configuration_head_v1_create(config, mon->output);
        if (!head) {
            wlr_output_configuration_v1_destroy(config);
            return NULL;
        }
        // Head is pre-filled from output; set position from our layout
        struct wlr_output_layout_output *l_output =
            wlr_output_layout_get(mgr->layout, mon->output);
        if (l_output) {
            head->state.x = l_output->x;
            head->state.y = l_output->y;
        }
    }

    return config;
}

static void update_output_management(SwlOutputManager *mgr)
{
    if (!mgr->output_mgmt)
        return;

    struct wlr_output_configuration_v1 *config =
        create_output_configuration(mgr);
    if (config)
        wlr_output_manager_v1_set_configuration(mgr->output_mgmt, config);
}

static void handle_output_mgmt_apply(struct wl_listener *listener, void *data)
{
    SwlOutputManager *mgr =
        wl_container_of(listener, mgr, output_mgmt_apply);
    struct wlr_output_configuration_v1 *config = data;

    size_t states_len = 0;
    struct wlr_backend_output_state *states =
        wlr_output_configuration_v1_build_state(config, &states_len);
    if (!states) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }

    struct wlr_backend *backend = swl_compositor_get_backend(mgr->comp);
    if (!wlr_backend_test(backend, states, states_len)) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        free(states);
        return;
    }

    if (!wlr_backend_commit(backend, states, states_len)) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        free(states);
        return;
    }

    free(states);

    // Apply positions from the configuration to the output layout
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link) {
        if (head->state.enabled) {
            wlr_output_layout_add(mgr->layout, head->state.output,
                head->state.x, head->state.y);
        } else {
            wlr_output_layout_remove(mgr->layout, head->state.output);
        }
    }

    wlr_output_configuration_v1_send_succeeded(config);
    wlr_output_configuration_v1_destroy(config);

    update_output_management(mgr);
}

static void handle_output_mgmt_test(struct wl_listener *listener, void *data)
{
    SwlOutputManager *mgr =
        wl_container_of(listener, mgr, output_mgmt_test);
    struct wlr_output_configuration_v1 *config = data;

    size_t states_len = 0;
    struct wlr_backend_output_state *states =
        wlr_output_configuration_v1_build_state(config, &states_len);
    if (!states) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }

    struct wlr_backend *backend = swl_compositor_get_backend(mgr->comp);
    if (wlr_backend_test(backend, states, states_len))
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);

    free(states);
    wlr_output_configuration_v1_destroy(config);
}

static void apply_monitor_rules(SwlMonitor *mon)
{
    if (!mon || !mon->mgr || !mon->output)
        return;

    SwlConfig *cfg = swl_compositor_get_config(mon->mgr->comp);
    if (!cfg)
        return;

    const char *name = mon->output->name;
    char key[256];

    // Check if there are rules for this monitor by name
    snprintf(key, sizeof(key), "monitors.%s.scale", name);
    if (!swl_config_has_key(cfg, key)) {
        // No rules for this monitor
        return;
    }

    fprintf(stderr, "Applying monitor rules for %s\n", name);

    // Build output state for mode/scale/transform changes
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    bool needs_commit = false;

    // Scale
    snprintf(key, sizeof(key), "monitors.%s.scale", name);
    if (swl_config_has_key(cfg, key)) {
        float scale = swl_config_get_float(cfg, key, 1.0f);
        if (scale > 0) {
            wlr_output_state_set_scale(&state, scale);
            needs_commit = true;
        }
    }

    // Transform (0-7: normal, 90, 180, 270, flipped, flipped-90, flipped-180, flipped-270)
    snprintf(key, sizeof(key), "monitors.%s.transform", name);
    if (swl_config_has_key(cfg, key)) {
        int transform = swl_config_get_int(cfg, key, 0);
        if (transform >= 0 && transform <= 7) {
            wlr_output_state_set_transform(&state, transform);
            needs_commit = true;
        }
    }

    // Resolution and refresh rate
    snprintf(key, sizeof(key), "monitors.%s.width", name);
    int width = swl_config_get_int(cfg, key, 0);
    snprintf(key, sizeof(key), "monitors.%s.height", name);
    int height = swl_config_get_int(cfg, key, 0);
    snprintf(key, sizeof(key), "monitors.%s.refresh", name);
    int refresh = swl_config_get_int(cfg, key, 0);

    if (width > 0 && height > 0) {
        struct wlr_output_mode *mode;
        wl_list_for_each(mode, &mon->output->modes, link) {
            if (mode->width == width && mode->height == height &&
                (refresh == 0 || mode->refresh == refresh)) {
                wlr_output_state_set_mode(&state, mode);
                needs_commit = true;
                break;
            }
        }
    }

    // Commit output state changes
    if (needs_commit) {
        wlr_output_commit_state(mon->output, &state);
    }
    wlr_output_state_finish(&state);

    // Position (requires removing and re-adding to layout)
    snprintf(key, sizeof(key), "monitors.%s.x", name);
    bool has_x = swl_config_has_key(cfg, key);
    int x = swl_config_get_int(cfg, key, 0);
    snprintf(key, sizeof(key), "monitors.%s.y", name);
    bool has_y = swl_config_has_key(cfg, key);
    int y = swl_config_get_int(cfg, key, 0);

    if (has_x && has_y) {
        wlr_output_layout_remove(mon->mgr->layout, mon->output);
        wlr_output_layout_add(mon->mgr->layout, mon->output, x, y);
    }

    // Layout-specific settings (mfact, nmaster, gaps, layout)
    snprintf(key, sizeof(key), "monitors.%s.mfact", name);
    if (swl_config_has_key(cfg, key)) {
        mon->mfact = swl_config_get_float(cfg, key, mon->mfact);
    }

    snprintf(key, sizeof(key), "monitors.%s.scroller_ratio", name);
    if (swl_config_has_key(cfg, key)) {
        mon->scroller_ratio = swl_config_get_float(cfg, key, mon->scroller_ratio);
    }

    snprintf(key, sizeof(key), "monitors.%s.nmaster", name);
    if (swl_config_has_key(cfg, key)) {
        mon->nmaster = swl_config_get_int(cfg, key, mon->nmaster);
    }

    snprintf(key, sizeof(key), "monitors.%s.gap_inner_h", name);
    if (swl_config_has_key(cfg, key)) {
        mon->gap_inner_h = swl_config_get_int(cfg, key, mon->gap_inner_h);
    }

    snprintf(key, sizeof(key), "monitors.%s.gap_inner_v", name);
    if (swl_config_has_key(cfg, key)) {
        mon->gap_inner_v = swl_config_get_int(cfg, key, mon->gap_inner_v);
    }

    snprintf(key, sizeof(key), "monitors.%s.gap_outer_h", name);
    if (swl_config_has_key(cfg, key)) {
        mon->gap_outer_h = swl_config_get_int(cfg, key, mon->gap_outer_h);
    }

    snprintf(key, sizeof(key), "monitors.%s.gap_outer_v", name);
    if (swl_config_has_key(cfg, key)) {
        mon->gap_outer_v = swl_config_get_int(cfg, key, mon->gap_outer_v);
    }

    snprintf(key, sizeof(key), "monitors.%s.layout", name);
    if (swl_config_has_key(cfg, key)) {
        const char *layout_name = swl_config_get_string(cfg, key, NULL);
        if (layout_name) {
            SwlLayoutRegistry *layouts = swl_compositor_get_layouts(mon->mgr->comp);
            const SwlLayout *layout = swl_layout_get(layouts, layout_name);
            if (layout)
                mon->layout = layout;
        }
    }

    // Enabled/disabled
    snprintf(key, sizeof(key), "monitors.%s.enabled", name);
    if (swl_config_has_key(cfg, key)) {
        bool enabled = swl_config_get_bool(cfg, key, true);
        if (!enabled) {
            struct wlr_output_state disable_state;
            wlr_output_state_init(&disable_state);
            wlr_output_state_set_enabled(&disable_state, false);
            wlr_output_commit_state(mon->output, &disable_state);
            wlr_output_state_finish(&disable_state);
        }
    }
}

size_t swl_monitor_count(SwlOutputManager *mgr)
{
    if (!mgr)
        return 0;

    size_t count = 0;
    SwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        count++;
    }
    return count;
}

SwlMonitor *swl_monitor_get_focused(SwlOutputManager *mgr)
{
    return mgr ? mgr->focused : NULL;
}

SwlMonitor *swl_monitor_at(SwlOutputManager *mgr, double x, double y)
{
    if (!mgr)
        return NULL;

    struct wlr_output *output = wlr_output_layout_output_at(mgr->layout, x, y);
    if (!output)
        return NULL;

    SwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        if (mon->output == output)
            return mon;
    }

    return NULL;
}

SwlMonitor *swl_monitor_by_name(SwlOutputManager *mgr, const char *name)
{
    if (!mgr || !name)
        return NULL;

    SwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        if (strcmp(mon->output->name, name) == 0)
            return mon;
    }

    return NULL;
}

SwlMonitor *swl_monitor_by_index(SwlOutputManager *mgr, size_t index)
{
    if (!mgr)
        return NULL;

    size_t i = 0;
    SwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        if (i == index)
            return mon;
        i++;
    }

    return NULL;
}

SwlMonitor *swl_monitor_in_direction(SwlOutputManager *mgr, SwlMonitor *from, int dir)
{
    if (!mgr || !from)
        return NULL;

    double cx = from->x + from->width / 2.0;
    double cy = from->y + from->height / 2.0;

    switch (dir) {
    case 0: cy -= from->height; break; // up
    case 1: cy += from->height; break; // down
    case 2: cx -= from->width; break;  // left
    case 3: cx += from->width; break;  // right
    }

    return swl_monitor_at(mgr, cx, cy);
}

void swl_monitor_foreach(SwlOutputManager *mgr, SwlMonitorIterator iter, void *data)
{
    if (!mgr || !iter)
        return;

    SwlMonitor *mon, *tmp;
    wl_list_for_each_safe(mon, tmp, &mgr->monitors, link) {
        if (!iter(mon, data))
            break;
    }
}

SwlMonitorInfo swl_monitor_get_info(const SwlMonitor *mon)
{
    SwlMonitorInfo info = {0};
    if (!mon)
        return info;

    info.id = mon->id;
    info.name = mon->output->name;
    info.description = mon->output->description;
    info.x = mon->x;
    info.y = mon->y;
    info.width = mon->width;
    info.height = mon->height;
    info.refresh = mon->output->refresh;
    info.scale = mon->output->scale;
    info.transform = mon->output->transform;
    info.enabled = mon->output->enabled;

    return info;
}

void swl_monitor_get_usable_area(const SwlMonitor *mon, int *x, int *y, int *w, int *h)
{
    if (!mon)
        return;

    if (x) *x = mon->usable_x;
    if (y) *y = mon->usable_y;
    if (w) *w = mon->usable_width;
    if (h) *h = mon->usable_height;
}

SwlError swl_monitor_configure(SwlMonitor *mon, const SwlMonitorConfig *cfg)
{
    if (!mon || !cfg)
        return SWL_ERR_INVALID_ARG;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, cfg->enabled);

    if (cfg->width > 0 && cfg->height > 0) {
        struct wlr_output_mode *mode;
        wl_list_for_each(mode, &mon->output->modes, link) {
            if (mode->width == cfg->width && mode->height == cfg->height &&
                (cfg->refresh == 0 || mode->refresh == cfg->refresh)) {
                wlr_output_state_set_mode(&state, mode);
                break;
            }
        }
    }

    if (cfg->scale > 0)
        wlr_output_state_set_scale(&state, cfg->scale);

    if (cfg->transform >= 0)
        wlr_output_state_set_transform(&state, cfg->transform);

    bool ok = wlr_output_commit_state(mon->output, &state);
    wlr_output_state_finish(&state);

    return ok ? SWL_OK : SWL_ERR_BACKEND;
}

SwlError swl_monitor_set_layout(SwlMonitor *mon, const SwlLayout *layout)
{
    if (!mon)
        return SWL_ERR_INVALID_ARG;

    mon->prev_layout = mon->layout;
    mon->layout = layout;

    SwlEventBus *bus = swl_compositor_get_event_bus(mon->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_LAYOUT_CHANGE, mon);

    return SWL_OK;
}

SwlError swl_monitor_focus(SwlMonitor *mon)
{
    if (!mon || !mon->mgr)
        return SWL_ERR_INVALID_ARG;

    mon->mgr->focused = mon;

    SwlEventBus *bus = swl_compositor_get_event_bus(mon->mgr->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_MONITOR_FOCUS, mon);

    return SWL_OK;
}

const SwlLayout *swl_monitor_get_layout(const SwlMonitor *mon)
{
    return mon ? mon->layout : NULL;
}

struct wlr_output *swl_monitor_get_wlr_output(const SwlMonitor *mon)
{
    return mon ? mon->output : NULL;
}

float swl_monitor_get_mfact(const SwlMonitor *mon)
{
    return mon ? mon->mfact : 0.55f;
}

float swl_monitor_get_scroller_ratio(const SwlMonitor *mon)
{
    return mon ? mon->scroller_ratio : 0.8f;
}

int swl_monitor_get_nmaster(const SwlMonitor *mon)
{
    return mon ? mon->nmaster : 1;
}

SwlError swl_monitor_set_mfact(SwlMonitor *mon, float mfact)
{
    if (!mon)
        return SWL_ERR_INVALID_ARG;

    if (mfact < 0.05f)
        mfact = 0.05f;
    if (mfact > 0.95f)
        mfact = 0.95f;

    mon->mfact = mfact;
    swl_monitor_arrange(mon);
    return SWL_OK;
}

SwlError swl_monitor_set_nmaster(SwlMonitor *mon, int nmaster)
{
    if (!mon)
        return SWL_ERR_INVALID_ARG;

    if (nmaster < 0)
        nmaster = 0;

    mon->nmaster = nmaster;
    swl_monitor_arrange(mon);
    return SWL_OK;
}

SwlError swl_monitor_adjust_mfact(SwlMonitor *mon, float delta)
{
    if (!mon)
        return SWL_ERR_INVALID_ARG;

    return swl_monitor_set_mfact(mon, mon->mfact + delta);
}

SwlError swl_monitor_adjust_nmaster(SwlMonitor *mon, int delta)
{
    if (!mon)
        return SWL_ERR_INVALID_ARG;

    return swl_monitor_set_nmaster(mon, mon->nmaster + delta);
}

void swl_monitor_set_usable_area(SwlMonitor *mon, int x, int y, int w, int h)
{
    if (!mon)
        return;
    mon->usable_x = x;
    mon->usable_y = y;
    mon->usable_width = w;
    mon->usable_height = h;
}

typedef struct {
    SwlClient **clients;
    size_t count;
    size_t capacity;
} ClientCollector;

static bool collect_tiled_client(SwlClient *c, void *data)
{
    ClientCollector *col = data;
    SwlClientInfo info = swl_client_get_info(c);

    if (info.floating || info.fullscreen)
        return true;

    if (col->count >= col->capacity) {
        col->capacity = col->capacity ? col->capacity * 2 : 16;
        col->clients = realloc(col->clients, col->capacity * sizeof(SwlClient *));
    }

    col->clients[col->count++] = c;
    return true;
}

void swl_monitor_arrange(SwlMonitor *mon)
{
    if (!mon)
        return;

    SwlClientManager *clients = swl_compositor_get_clients(mon->mgr->comp);
    if (!clients)
        return;

    ClientCollector col = {0};
    swl_client_foreach_visible(clients, mon, collect_tiled_client, &col);

    if (col.count == 0) {
        free(col.clients);
        return;
    }

    if (mon->layout && mon->layout->arrange) {
        // Build array of column heads only — non-head clients are arranged
        // within their column after the layout positions the heads.
        size_t head_count = 0;
        SwlClient **heads = calloc(col.count, sizeof(SwlClient *));
        for (size_t i = 0; i < col.count; i++) {
            if (swl_client_is_column_head(col.clients[i]))
                heads[head_count++] = col.clients[i];
        }

        // Find focused client index among column heads — fall back to
        // focus stack if global focus is on another monitor
        SwlClient *focused = swl_client_focused(clients);
        if (!focused || swl_client_get_monitor(focused) != mon)
            focused = swl_client_focus_top_on_monitor(clients, mon);
        int focused_index = -1;
        for (size_t i = 0; i < head_count; i++) {
            if (heads[i] == focused) {
                focused_index = (int)i;
                break;
            }
            // Also match if the focused client is inside this column
            SwlClient *m;
            for (m = heads[i]; m; m = swl_client_column_next(m)) {
                if (m == focused) {
                    focused_index = (int)i;
                    break;
                }
            }
            if (focused_index >= 0)
                break;
        }

        // Use scroller_ratio instead of mfact when layout is scroller
        bool is_scroller = mon->layout->name && strcmp(mon->layout->name, "scroller") == 0;
        float layout_mfact = is_scroller ? mon->scroller_ratio : mon->mfact;

        SwlLayoutParams params = {
            .area_x = mon->usable_x,
            .area_y = mon->usable_y,
            .area_width = mon->usable_width,
            .area_height = mon->usable_height,
            .gap_inner_h = mon->gap_inner_h,
            .gap_inner_v = mon->gap_inner_v,
            .gap_outer_h = mon->gap_outer_h,
            .gap_outer_v = mon->gap_outer_v,
            .master_factor = layout_mfact,
            .master_count = mon->nmaster,
            .client_count = head_count,
            .focused_index = focused_index,
            .clients = calloc(head_count, sizeof(SwlLayoutClient)),
        };

        for (size_t i = 0; i < head_count; i++) {
            SwlClientInfo info = swl_client_get_info(heads[i]);
            params.clients[i].id = info.id;
            params.clients[i].width = info.geometry.width;
            params.clients[i].height = info.geometry.height;
            if (is_scroller)
                params.clients[i].column_ratio = swl_client_get_scroller_ratio(heads[i]);
        }

        mon->layout->arrange(&params);

        // Apply layout results — subdivide each column's geometry among its members
        for (size_t i = 0; i < head_count; i++) {
            SwlLayoutClient *lc = &params.clients[i];

            // Count members in this column
            int members = 0;
            SwlClient *m;
            for (m = heads[i]; m; m = swl_client_column_next(m))
                members++;

            if (members <= 1) {
                // Single client column — use layout geometry directly
                fprintf(stderr, "Arrange client on %s: pos=(%d,%d) size=%dx%d\n",
                        mon->output->name, lc->x, lc->y, lc->width, lc->height);
                swl_client_resize(heads[i], lc->x, lc->y, lc->width, lc->height);
            } else {
                // Multi-client column — divide height evenly with inner gaps
                int total_gaps = (members - 1) * mon->gap_inner_v;
                int avail_h = lc->height - total_gaps;
                int member_h = avail_h / members;
                int remainder = avail_h - member_h * members;
                int y = lc->y;
                int idx = 0;

                for (m = heads[i]; m; m = swl_client_column_next(m)) {
                    int h = member_h + (idx < remainder ? 1 : 0);
                    fprintf(stderr, "Arrange stacked client on %s: pos=(%d,%d) size=%dx%d\n",
                            mon->output->name, lc->x, y, lc->width, h);
                    swl_client_resize(m, lc->x, y, lc->width, h);
                    y += h + mon->gap_inner_v;
                    idx++;
                }
            }
        }

        free(params.clients);
        free(heads);
    }

    free(col.clients);

    SwlClient *focused = swl_client_focused(clients);
    if (focused && swl_client_get_monitor(focused) == mon)
        swl_client_focus(focused);
}

void swl_monitor_arrange_all(SwlOutputManager *mgr)
{
    if (!mgr)
        return;

    SwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        swl_monitor_arrange(mon);
    }
}

void swl_monitor_damage_whole(SwlMonitor *mon)
{
    if (!mon || !mon->scene_output)
        return;
    wlr_damage_ring_add_whole(&mon->scene_output->damage_ring);
    wlr_output_schedule_frame(mon->output);
}

void swl_monitor_reload_config(SwlOutputManager *mgr)
{
    if (!mgr)
        return;

    SwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        apply_monitor_rules(mon);
        swl_monitor_arrange(mon);
    }
}
