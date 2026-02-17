#define _POSIX_C_SOURCE 200809L
#include "monitor.h"
#include "compositor.h"
#include "config.h"
#include "layout.h"
#include "client.h"
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
#include <scenefx/types/wlr_scene.h>

struct DwlMonitor {
    uint32_t id;
    DwlOutputManager *mgr;
    struct wlr_output *output;
    struct wlr_scene_output *scene_output;

    int x, y, width, height;
    int usable_x, usable_y, usable_width, usable_height;

    uint32_t tags;
    const DwlLayout *layout;
    const DwlLayout *prev_layout;

    float mfact;
    int nmaster;
    int gap_inner_h, gap_inner_v;
    int gap_outer_h, gap_outer_v;

    struct wl_listener frame;
    struct wl_listener destroy;
    struct wl_listener request_state;

    struct wl_list link;
};

struct DwlOutputManager {
    DwlCompositor *comp;
    struct wlr_output_layout *layout;
    struct wl_list monitors;
    DwlMonitor *focused;
    uint32_t next_id;

    struct wl_listener new_output;
    struct wl_listener layout_change;
};

static void handle_frame(struct wl_listener *listener, void *data);
static void handle_destroy(struct wl_listener *listener, void *data);
static void handle_request_state(struct wl_listener *listener, void *data);
static void handle_new_output(struct wl_listener *listener, void *data);
static void handle_layout_change(struct wl_listener *listener, void *data);
static void apply_monitor_rules(DwlMonitor *mon);

DwlOutputManager *dwl_output_create(DwlCompositor *comp)
{
    DwlOutputManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->comp = comp;
    mgr->next_id = 1;
    wl_list_init(&mgr->monitors);

    // Use the compositor's output layout (shared with XDG output manager)
    mgr->layout = dwl_compositor_get_output_layout(comp);

    struct wlr_backend *backend = dwl_compositor_get_backend(comp);
    mgr->new_output.notify = handle_new_output;
    wl_signal_add(&backend->events.new_output, &mgr->new_output);

    mgr->layout_change.notify = handle_layout_change;
    wl_signal_add(&mgr->layout->events.change, &mgr->layout_change);

    return mgr;
}

void dwl_output_destroy(DwlOutputManager *mgr)
{
    if (!mgr)
        return;

    wl_list_remove(&mgr->new_output.link);
    wl_list_remove(&mgr->layout_change.link);

    DwlMonitor *mon, *tmp;
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
    DwlOutputManager *mgr = wl_container_of(listener, mgr, new_output);
    struct wlr_output *output = data;

    wlr_output_init_render(output,
        dwl_compositor_get_allocator(mgr->comp),
        dwl_compositor_get_wlr_renderer(mgr->comp));

    DwlMonitor *mon = calloc(1, sizeof(*mon));
    if (!mon)
        return;

    mon->id = mgr->next_id++;
    mon->mgr = mgr;
    mon->output = output;
    mon->tags = 1;

    DwlConfig *cfg = dwl_compositor_get_config(mgr->comp);
    mon->mfact = dwl_config_get_float(cfg, "appearance.mfact", 0.55f);
    mon->nmaster = dwl_config_get_int(cfg, "appearance.nmaster", 1);
    mon->gap_inner_h = dwl_config_get_int(cfg, "appearance.gap_inner_h", 10);
    mon->gap_inner_v = dwl_config_get_int(cfg, "appearance.gap_inner_v", 10);
    mon->gap_outer_h = dwl_config_get_int(cfg, "appearance.gap_outer_h", 10);
    mon->gap_outer_v = dwl_config_get_int(cfg, "appearance.gap_outer_v", 10);

    // Set default layout
    DwlLayoutRegistry *layouts = dwl_compositor_get_layouts(mgr->comp);
    const char *layout_name = dwl_config_get_string(cfg, "appearance.layout", "tile");
    mon->layout = dwl_layout_get(layouts, layout_name);
    if (!mon->layout)
        mon->layout = dwl_layout_get(layouts, "tile");

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
    struct wlr_scene *scene = dwl_compositor_get_scene(mgr->comp);
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

    DwlEventBus *bus = dwl_compositor_get_event_bus(mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_MONITOR_ADD, mon);
}

static void handle_frame(struct wl_listener *listener, void *data)
{
    DwlMonitor *mon = wl_container_of(listener, mon, frame);
    (void)data;

    wlr_scene_output_commit(mon->scene_output, NULL);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(mon->scene_output, &now);
}

static void handle_destroy(struct wl_listener *listener, void *data)
{
    DwlMonitor *mon = wl_container_of(listener, mon, destroy);
    (void)data;

    if (mon->mgr && mon->mgr->comp) {
        DwlEventBus *bus = dwl_compositor_get_event_bus(mon->mgr->comp);
        if (bus)
            dwl_event_bus_emit_simple(bus, DWL_EVENT_MONITOR_REMOVE, mon);
    }

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

    free(mon);
}

static void handle_request_state(struct wl_listener *listener, void *data)
{
    DwlMonitor *mon = wl_container_of(listener, mon, request_state);
    struct wlr_output_event_request_state *event = data;

    wlr_output_commit_state(mon->output, event->state);
}

static void handle_layout_change(struct wl_listener *listener, void *data)
{
    DwlOutputManager *mgr = wl_container_of(listener, mgr, layout_change);
    (void)data;

    DwlMonitor *mon;
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
}

static void apply_monitor_rules(DwlMonitor *mon)
{
    if (!mon || !mon->mgr || !mon->output)
        return;

    DwlConfig *cfg = dwl_compositor_get_config(mon->mgr->comp);
    if (!cfg)
        return;

    const char *name = mon->output->name;
    char key[256];

    // Check if there are rules for this monitor by name
    snprintf(key, sizeof(key), "monitors.%s.scale", name);
    if (!dwl_config_has_key(cfg, key)) {
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
    if (dwl_config_has_key(cfg, key)) {
        float scale = dwl_config_get_float(cfg, key, 1.0f);
        if (scale > 0) {
            wlr_output_state_set_scale(&state, scale);
            needs_commit = true;
        }
    }

    // Transform (0-7: normal, 90, 180, 270, flipped, flipped-90, flipped-180, flipped-270)
    snprintf(key, sizeof(key), "monitors.%s.transform", name);
    if (dwl_config_has_key(cfg, key)) {
        int transform = dwl_config_get_int(cfg, key, 0);
        if (transform >= 0 && transform <= 7) {
            wlr_output_state_set_transform(&state, transform);
            needs_commit = true;
        }
    }

    // Resolution and refresh rate
    snprintf(key, sizeof(key), "monitors.%s.width", name);
    int width = dwl_config_get_int(cfg, key, 0);
    snprintf(key, sizeof(key), "monitors.%s.height", name);
    int height = dwl_config_get_int(cfg, key, 0);
    snprintf(key, sizeof(key), "monitors.%s.refresh", name);
    int refresh = dwl_config_get_int(cfg, key, 0);

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
    bool has_x = dwl_config_has_key(cfg, key);
    int x = dwl_config_get_int(cfg, key, 0);
    snprintf(key, sizeof(key), "monitors.%s.y", name);
    bool has_y = dwl_config_has_key(cfg, key);
    int y = dwl_config_get_int(cfg, key, 0);

    if (has_x && has_y) {
        wlr_output_layout_remove(mon->mgr->layout, mon->output);
        wlr_output_layout_add(mon->mgr->layout, mon->output, x, y);
    }

    // Layout-specific settings (mfact, nmaster, gaps, layout)
    snprintf(key, sizeof(key), "monitors.%s.mfact", name);
    if (dwl_config_has_key(cfg, key)) {
        mon->mfact = dwl_config_get_float(cfg, key, mon->mfact);
    }

    snprintf(key, sizeof(key), "monitors.%s.nmaster", name);
    if (dwl_config_has_key(cfg, key)) {
        mon->nmaster = dwl_config_get_int(cfg, key, mon->nmaster);
    }

    snprintf(key, sizeof(key), "monitors.%s.gap_inner_h", name);
    if (dwl_config_has_key(cfg, key)) {
        mon->gap_inner_h = dwl_config_get_int(cfg, key, mon->gap_inner_h);
    }

    snprintf(key, sizeof(key), "monitors.%s.gap_inner_v", name);
    if (dwl_config_has_key(cfg, key)) {
        mon->gap_inner_v = dwl_config_get_int(cfg, key, mon->gap_inner_v);
    }

    snprintf(key, sizeof(key), "monitors.%s.gap_outer_h", name);
    if (dwl_config_has_key(cfg, key)) {
        mon->gap_outer_h = dwl_config_get_int(cfg, key, mon->gap_outer_h);
    }

    snprintf(key, sizeof(key), "monitors.%s.gap_outer_v", name);
    if (dwl_config_has_key(cfg, key)) {
        mon->gap_outer_v = dwl_config_get_int(cfg, key, mon->gap_outer_v);
    }

    snprintf(key, sizeof(key), "monitors.%s.layout", name);
    if (dwl_config_has_key(cfg, key)) {
        const char *layout_name = dwl_config_get_string(cfg, key, NULL);
        if (layout_name) {
            DwlLayoutRegistry *layouts = dwl_compositor_get_layouts(mon->mgr->comp);
            const DwlLayout *layout = dwl_layout_get(layouts, layout_name);
            if (layout)
                mon->layout = layout;
        }
    }

    // Enabled/disabled
    snprintf(key, sizeof(key), "monitors.%s.enabled", name);
    if (dwl_config_has_key(cfg, key)) {
        bool enabled = dwl_config_get_bool(cfg, key, true);
        if (!enabled) {
            struct wlr_output_state disable_state;
            wlr_output_state_init(&disable_state);
            wlr_output_state_set_enabled(&disable_state, false);
            wlr_output_commit_state(mon->output, &disable_state);
            wlr_output_state_finish(&disable_state);
        }
    }
}

size_t dwl_monitor_count(DwlOutputManager *mgr)
{
    if (!mgr)
        return 0;

    size_t count = 0;
    DwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        count++;
    }
    return count;
}

DwlMonitor *dwl_monitor_get_focused(DwlOutputManager *mgr)
{
    return mgr ? mgr->focused : NULL;
}

DwlMonitor *dwl_monitor_at(DwlOutputManager *mgr, double x, double y)
{
    if (!mgr)
        return NULL;

    struct wlr_output *output = wlr_output_layout_output_at(mgr->layout, x, y);
    if (!output)
        return NULL;

    DwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        if (mon->output == output)
            return mon;
    }

    return NULL;
}

DwlMonitor *dwl_monitor_by_name(DwlOutputManager *mgr, const char *name)
{
    if (!mgr || !name)
        return NULL;

    DwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        if (strcmp(mon->output->name, name) == 0)
            return mon;
    }

    return NULL;
}

DwlMonitor *dwl_monitor_by_index(DwlOutputManager *mgr, size_t index)
{
    if (!mgr)
        return NULL;

    size_t i = 0;
    DwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        if (i == index)
            return mon;
        i++;
    }

    return NULL;
}

DwlMonitor *dwl_monitor_in_direction(DwlOutputManager *mgr, DwlMonitor *from, int dir)
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

    return dwl_monitor_at(mgr, cx, cy);
}

void dwl_monitor_foreach(DwlOutputManager *mgr, DwlMonitorIterator iter, void *data)
{
    if (!mgr || !iter)
        return;

    DwlMonitor *mon, *tmp;
    wl_list_for_each_safe(mon, tmp, &mgr->monitors, link) {
        if (!iter(mon, data))
            break;
    }
}

DwlMonitorInfo dwl_monitor_get_info(const DwlMonitor *mon)
{
    DwlMonitorInfo info = {0};
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

void dwl_monitor_get_usable_area(const DwlMonitor *mon, int *x, int *y, int *w, int *h)
{
    if (!mon)
        return;

    if (x) *x = mon->usable_x;
    if (y) *y = mon->usable_y;
    if (w) *w = mon->usable_width;
    if (h) *h = mon->usable_height;
}

DwlError dwl_monitor_configure(DwlMonitor *mon, const DwlMonitorConfig *cfg)
{
    if (!mon || !cfg)
        return DWL_ERR_INVALID_ARG;

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

    return ok ? DWL_OK : DWL_ERR_BACKEND;
}

DwlError dwl_monitor_set_layout(DwlMonitor *mon, const DwlLayout *layout)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    mon->prev_layout = mon->layout;
    mon->layout = layout;

    DwlEventBus *bus = dwl_compositor_get_event_bus(mon->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_LAYOUT_CHANGE, mon);

    return DWL_OK;
}

DwlError dwl_monitor_set_tags(DwlMonitor *mon, uint32_t tags)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    if (tags == 0)
        tags = 1;

    mon->tags = tags;

    DwlEventBus *bus = dwl_compositor_get_event_bus(mon->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_TAG_CHANGE, mon);

    return DWL_OK;
}

DwlError dwl_monitor_focus(DwlMonitor *mon)
{
    if (!mon || !mon->mgr)
        return DWL_ERR_INVALID_ARG;

    mon->mgr->focused = mon;

    DwlEventBus *bus = dwl_compositor_get_event_bus(mon->mgr->comp);
    dwl_event_bus_emit_simple(bus, DWL_EVENT_MONITOR_FOCUS, mon);

    return DWL_OK;
}

uint32_t dwl_monitor_get_tags(const DwlMonitor *mon)
{
    return mon ? mon->tags : 0;
}

const DwlLayout *dwl_monitor_get_layout(const DwlMonitor *mon)
{
    return mon ? mon->layout : NULL;
}

struct wlr_output *dwl_monitor_get_wlr_output(const DwlMonitor *mon)
{
    return mon ? mon->output : NULL;
}

float dwl_monitor_get_mfact(const DwlMonitor *mon)
{
    return mon ? mon->mfact : 0.55f;
}

int dwl_monitor_get_nmaster(const DwlMonitor *mon)
{
    return mon ? mon->nmaster : 1;
}

DwlError dwl_monitor_set_mfact(DwlMonitor *mon, float mfact)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    if (mfact < 0.05f)
        mfact = 0.05f;
    if (mfact > 0.95f)
        mfact = 0.95f;

    mon->mfact = mfact;
    dwl_monitor_arrange(mon);
    return DWL_OK;
}

DwlError dwl_monitor_set_nmaster(DwlMonitor *mon, int nmaster)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    if (nmaster < 0)
        nmaster = 0;

    mon->nmaster = nmaster;
    dwl_monitor_arrange(mon);
    return DWL_OK;
}

DwlError dwl_monitor_adjust_mfact(DwlMonitor *mon, float delta)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    return dwl_monitor_set_mfact(mon, mon->mfact + delta);
}

DwlError dwl_monitor_adjust_nmaster(DwlMonitor *mon, int delta)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    return dwl_monitor_set_nmaster(mon, mon->nmaster + delta);
}

void dwl_monitor_set_usable_area(DwlMonitor *mon, int x, int y, int w, int h)
{
    if (!mon)
        return;
    mon->usable_x = x;
    mon->usable_y = y;
    mon->usable_width = w;
    mon->usable_height = h;
}

typedef struct {
    DwlClient **clients;
    size_t count;
    size_t capacity;
} ClientCollector;

static bool collect_tiled_client(DwlClient *c, void *data)
{
    ClientCollector *col = data;
    DwlClientInfo info = dwl_client_get_info(c);

    if (info.floating || info.fullscreen)
        return true;

    if (col->count >= col->capacity) {
        col->capacity = col->capacity ? col->capacity * 2 : 16;
        col->clients = realloc(col->clients, col->capacity * sizeof(DwlClient *));
    }

    col->clients[col->count++] = c;
    return true;
}

void dwl_monitor_arrange(DwlMonitor *mon)
{
    if (!mon)
        return;

    DwlClientManager *clients = dwl_compositor_get_clients(mon->mgr->comp);
    if (!clients)
        return;

    ClientCollector col = {0};
    dwl_client_foreach_visible(clients, mon, collect_tiled_client, &col);

    if (col.count == 0) {
        free(col.clients);
        return;
    }

    if (mon->layout && mon->layout->arrange) {
        // Find focused client index
        DwlClient *focused = dwl_client_focused(clients);
        int focused_index = -1;
        for (size_t i = 0; i < col.count; i++) {
            if (col.clients[i] == focused) {
                focused_index = (int)i;
                break;
            }
        }

        DwlLayoutParams params = {
            .area_x = mon->usable_x,
            .area_y = mon->usable_y,
            .area_width = mon->usable_width,
            .area_height = mon->usable_height,
            .gap_inner_h = mon->gap_inner_h,
            .gap_inner_v = mon->gap_inner_v,
            .gap_outer_h = mon->gap_outer_h,
            .gap_outer_v = mon->gap_outer_v,
            .master_factor = mon->mfact,
            .master_count = mon->nmaster,
            .client_count = col.count,
            .focused_index = focused_index,
            .clients = calloc(col.count, sizeof(DwlLayoutClient)),
        };

        for (size_t i = 0; i < col.count; i++) {
            DwlClientInfo info = dwl_client_get_info(col.clients[i]);
            params.clients[i].id = info.id;
            params.clients[i].width = info.geometry.width;
            params.clients[i].height = info.geometry.height;
        }

        mon->layout->arrange(&params);

        for (size_t i = 0; i < col.count; i++) {
            DwlLayoutClient *lc = &params.clients[i];
            fprintf(stderr, "Arrange client on %s: pos=(%d,%d) size=%dx%d\n",
                    mon->output->name, lc->x, lc->y, lc->width, lc->height);
            dwl_client_resize(col.clients[i], lc->x, lc->y, lc->width, lc->height);
        }

        free(params.clients);
    }

    free(col.clients);

    DwlClient *focused = dwl_client_focused(clients);
    if (focused)
        dwl_client_focus(focused);
}

void dwl_monitor_arrange_all(DwlOutputManager *mgr)
{
    if (!mgr)
        return;

    DwlMonitor *mon;
    wl_list_for_each(mon, &mgr->monitors, link) {
        dwl_monitor_arrange(mon);
    }
}
