#define _POSIX_C_SOURCE 200809L
#include "ipc_internal.h"
#include "compositor.h"
#include "client.h"
#include "config.h"
#include "events.h"
#include "input.h"
#include "keybindings.h"
#include "layer.h"
#include "layout.h"
#include "monitor.h"
#include "render.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

typedef struct {
    char *json;
    int offset;
    bool first;
} GetWindowsCtx;

static bool get_windows_iter(SwlClient *c, void *data)
{
    GetWindowsCtx *ctx = data;
    SwlClientInfo info = swl_client_get_info(c);

    if (!ctx->first) ctx->offset += sprintf(ctx->json + ctx->offset, ",");
    ctx->first = false;

    ctx->offset += sprintf(ctx->json + ctx->offset,
        "{\"id\":%u,\"app_id\":\"%s\",\"title\":\"%s\","
        "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
        "\"floating\":%s,\"fullscreen\":%s,\"focused\":%s}",
        info.id,
        info.app_id ? info.app_id : "",
        info.title ? info.title : "",
        info.geometry.x, info.geometry.y,
        info.geometry.width, info.geometry.height,
        info.floating ? "true" : "false",
        info.fullscreen ? "true" : "false",
        info.focused ? "true" : "false");

    return true;
}

static SwlIPCResponse cmd_get_windows(SwlCompositor *comp, const char *args)
{
    (void)args;
    SwlIPCResponse r = {.success = true};

    SwlClientManager *mgr = swl_compositor_get_clients(comp);
    if (!mgr) {
        r.json = strdup("[]");
        return r;
    }

    GetWindowsCtx ctx = {
        .json = malloc(BUFFER_SIZE),
        .offset = 0,
        .first = true,
    };
    ctx.offset += sprintf(ctx.json + ctx.offset, "[");

    swl_client_foreach(mgr, get_windows_iter, &ctx);

    ctx.offset += sprintf(ctx.json + ctx.offset, "]");
    r.json = ctx.json;
    return r;
}

static SwlIPCResponse cmd_quit(SwlCompositor *comp, const char *args)
{
    (void)args;
    SwlIPCResponse r = {.success = true, .json = strdup("ok")};
    swl_compositor_quit(comp);
    return r;
}

static SwlIPCResponse cmd_reload_config(SwlCompositor *comp, const char *args)
{
    (void)args;
    SwlIPCResponse r = {.success = true};

    SwlConfig *cfg = swl_compositor_get_config(comp);
    if (!cfg || swl_config_reload(cfg) != SWL_OK) {
        r.success = false;
        r.error = strdup("failed to reload config");
        return r;
    }

    SwlRenderer *renderer = swl_compositor_get_renderer(comp);
    if (renderer) {
        swl_renderer_reload_config(renderer);
        swl_renderer_damage_whole(renderer);
    }

    SwlInput *input = swl_compositor_get_input(comp);
    if (input) {
        swl_input_reload_config(input);
        SwlKeybindingManager *kb = swl_input_get_keybindings(input);
        if (kb)
            swl_keybinding_reload(kb);
    }

    SwlClientManager *clients = swl_compositor_get_clients(comp);
    if (clients)
        swl_client_manager_load_rules(clients);

    SwlOutputManager *output = swl_compositor_get_output(comp);
    if (output)
        swl_monitor_reload_config(output);

    SwlEventBus *bus = swl_compositor_get_event_bus(comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_CONFIG_RELOAD, NULL);

    r.json = strdup("ok");
    return r;
}

static SwlIPCResponse cmd_get_monitors(SwlCompositor *comp, const char *args)
{
    (void)args;
    SwlIPCResponse r = {.success = true};

    SwlOutputManager *output = swl_compositor_get_output(comp);
    if (!output) {
        r.json = strdup("[]");
        return r;
    }

    char *json = malloc(BUFFER_SIZE);
    int offset = 0;
    offset += sprintf(json + offset, "[");

    bool first = true;
    size_t count = swl_monitor_count(output);
    for (size_t i = 0; i < count; i++) {
        SwlMonitor *mon = swl_monitor_by_index(output, i);
        if (!mon) continue;

        SwlMonitorInfo info = swl_monitor_get_info(mon);

        if (!first) offset += sprintf(json + offset, ",");
        first = false;

        offset += sprintf(json + offset,
            "{\"id\":%u,\"name\":\"%s\",\"x\":%d,\"y\":%d,"
            "\"width\":%d,\"height\":%d,\"scale\":%.2f,"
            "\"enabled\":%s}",
            info.id,
            info.name ? info.name : "",
            info.x, info.y, info.width, info.height,
            info.scale,
            info.enabled ? "true" : "false");
    }

    offset += sprintf(json + offset, "]");
    r.json = json;
    return r;
}

static SwlIPCResponse cmd_focus(SwlCompositor *comp, const char *args)
{
    SwlIPCResponse r = {.success = true};

    if (!args) {
        r.success = false;
        r.error = strdup("missing window id");
        return r;
    }

    uint32_t id = (uint32_t)atoi(args);
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *client = swl_client_by_id(clients, id);

    if (!client) {
        r.success = false;
        r.error = strdup("window not found");
        return r;
    }

    swl_client_focus(client);
    r.json = strdup("ok");
    return r;
}

static SwlIPCResponse cmd_close(SwlCompositor *comp, const char *args)
{
    SwlIPCResponse r = {.success = true};

    if (!args) {
        r.success = false;
        r.error = strdup("missing window id");
        return r;
    }

    uint32_t id = (uint32_t)atoi(args);
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *client = swl_client_by_id(clients, id);

    if (!client) {
        r.success = false;
        r.error = strdup("window not found");
        return r;
    }

    swl_client_close(client);
    r.json = strdup("ok");
    return r;
}

static SwlIPCResponse cmd_layout(SwlCompositor *comp, const char *args)
{
    SwlIPCResponse r = {.success = true};

    if (!args) {
        r.success = false;
        r.error = strdup("missing layout name");
        return r;
    }

    SwlLayoutRegistry *layouts = swl_compositor_get_layouts(comp);
    const SwlLayout *layout = swl_layout_get(layouts, args);

    if (!layout) {
        r.success = false;
        r.error = strdup("layout not found");
        return r;
    }

    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_monitor_get_focused(output);

    if (!mon) {
        r.success = false;
        r.error = strdup("no monitor focused");
        return r;
    }

    swl_monitor_set_layout(mon, layout);
    swl_monitor_arrange(mon);
    r.json = strdup("ok");
    return r;
}

static SwlIPCResponse cmd_get_layouts(SwlCompositor *comp, const char *args)
{
    (void)args;
    SwlIPCResponse r = {.success = true};

    SwlLayoutRegistry *layouts = swl_compositor_get_layouts(comp);
    if (!layouts) {
        r.json = strdup("[]");
        return r;
    }

    size_t count;
    const char **names = swl_layout_list(layouts, &count);

    char *json = malloc(BUFFER_SIZE);
    int offset = 0;
    offset += sprintf(json + offset, "[");

    for (size_t i = 0; i < count; i++) {
        if (i > 0) offset += sprintf(json + offset, ",");
        offset += sprintf(json + offset, "\"%s\"", names[i]);
    }

    offset += sprintf(json + offset, "]");
    free((void *)names);
    r.json = json;
    return r;
}

static SwlIPCResponse cmd_output_power(SwlCompositor *comp, const char *args)
{
    SwlIPCResponse r = {.success = true};

    if (!args) {
        r.success = false;
        r.error = strdup("usage: output-power <name> <on|off>");
        return r;
    }

    // Parse "<name> <on|off>"
    char name[128];
    char mode_str[8];
    if (sscanf(args, "%127s %7s", name, mode_str) != 2) {
        r.success = false;
        r.error = strdup("usage: output-power <name> <on|off>");
        return r;
    }

    bool enabled;
    if (strcmp(mode_str, "on") == 0)
        enabled = true;
    else if (strcmp(mode_str, "off") == 0)
        enabled = false;
    else {
        r.success = false;
        r.error = strdup("mode must be 'on' or 'off'");
        return r;
    }

    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_monitor_by_name(output, name);
    if (!mon) {
        r.success = false;
        r.error = strdup("monitor not found");
        return r;
    }

    SwlMonitorConfig cfg = {0};
    SwlMonitorInfo info = swl_monitor_get_info(mon);
    cfg.enabled = enabled;
    cfg.width = info.width;
    cfg.height = info.height;
    cfg.refresh = info.refresh;
    cfg.scale = info.scale;
    cfg.transform = info.transform;

    SwlError err = swl_monitor_configure(mon, &cfg);
    if (err != SWL_OK) {
        r.success = false;
        r.error = strdup("failed to set output power");
        return r;
    }

    r.json = strdup("ok");
    return r;
}

/* --- Event subscription infrastructure --- */

static const char *event_type_names[] = {
    [SWL_EVENT_CLIENT_CREATE]   = "client_create",
    [SWL_EVENT_CLIENT_DESTROY]  = "client_destroy",
    [SWL_EVENT_CLIENT_FOCUS]    = "client_focus",
    [SWL_EVENT_CLIENT_UNFOCUS]  = "client_unfocus",
    [SWL_EVENT_CLIENT_FULLSCREEN] = "client_fullscreen",
    [SWL_EVENT_CLIENT_FLOAT]    = "client_float",
    [SWL_EVENT_CLIENT_MOVE]     = "client_move",
    [SWL_EVENT_CLIENT_RESIZE]   = "client_resize",
    [SWL_EVENT_CLIENT_URGENT]   = "client_urgent",
    [SWL_EVENT_MONITOR_ADD]     = "monitor_add",
    [SWL_EVENT_MONITOR_REMOVE]  = "monitor_remove",
    [SWL_EVENT_MONITOR_FOCUS]   = "monitor_focus",
    [SWL_EVENT_LAYOUT_CHANGE]   = "layout_change",
    [SWL_EVENT_KEY_PRESS]       = "key_press",
    [SWL_EVENT_KEY_RELEASE]     = "key_release",
    [SWL_EVENT_CONFIG_RELOAD]   = "config_reload",
    [SWL_EVENT_RENDER_START]    = "render_start",
    [SWL_EVENT_RENDER_END]      = "render_end",
    [SWL_EVENT_LAYER_MAP]       = "layer_map",
    [SWL_EVENT_LAYER_UNMAP]     = "layer_unmap",
    [SWL_EVENT_SESSION_LOCK]    = "session_lock",
    [SWL_EVENT_SESSION_UNLOCK]  = "session_unlock",
    [SWL_EVENT_LID_CLOSE]       = "lid_close",
    [SWL_EVENT_LID_OPEN]        = "lid_open",
};

#define EVENT_TYPE_COUNT (sizeof(event_type_names) / sizeof(event_type_names[0]))

static int event_type_from_name(const char *name)
{
    for (size_t i = 0; i < EVENT_TYPE_COUNT; i++) {
        if (event_type_names[i] && strcmp(event_type_names[i], name) == 0)
            return (int)i;
    }
    return -1;
}

static int serialize_client_info(char *buf, size_t bufsize, const SwlClientInfo *info)
{
    return snprintf(buf, bufsize,
        "{\"id\":%u,\"app_id\":\"%s\",\"title\":\"%s\","
        "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
        "\"floating\":%s,\"fullscreen\":%s,\"focused\":%s}",
        info->id,
        info->app_id ? info->app_id : "",
        info->title ? info->title : "",
        info->geometry.x, info->geometry.y,
        info->geometry.width, info->geometry.height,
        info->floating ? "true" : "false",
        info->fullscreen ? "true" : "false",
        info->focused ? "true" : "false");
}

static int serialize_monitor_info(char *buf, size_t bufsize, const SwlMonitorInfo *info)
{
    return snprintf(buf, bufsize,
        "{\"id\":%u,\"name\":\"%s\",\"x\":%d,\"y\":%d,"
        "\"width\":%d,\"height\":%d,\"scale\":%.2f,"
        "\"enabled\":%s}",
        info->id,
        info->name ? info->name : "",
        info->x, info->y, info->width, info->height,
        info->scale,
        info->enabled ? "true" : "false");
}

static int serialize_layer_info(char *buf, size_t bufsize, const SwlLayerSurfaceInfo *info)
{
    return snprintf(buf, bufsize,
        "{\"namespace\":\"%s\",\"x\":%d,\"y\":%d,"
        "\"width\":%d,\"height\":%d,\"layer\":%d,\"mapped\":%s}",
        info->namespace ? info->namespace : "",
        info->x, info->y, info->width, info->height,
        (int)info->layer,
        info->mapped ? "true" : "false");
}

static uint64_t get_timestamp_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void ipc_event_handler(void *ctx, const SwlEvent *event)
{
    SwlIPC *ipc = ctx;
    if (!ipc || ipc->subscriber_count == 0)
        return;

    if ((size_t)event->type >= EVENT_TYPE_COUNT)
        return;

    const char *event_name = event_type_names[event->type];
    if (!event_name)
        return;

    char buf[BUFFER_SIZE];
    int offset = 0;
    uint64_t ts = get_timestamp_ms();

    offset += snprintf(buf + offset, sizeof(buf) - offset,
        "{\"event\":\"%s\",\"data\":", event_name);

    switch (event->type) {
    case SWL_EVENT_CLIENT_CREATE:
    case SWL_EVENT_CLIENT_DESTROY:
    case SWL_EVENT_CLIENT_FOCUS:
    case SWL_EVENT_CLIENT_UNFOCUS:
    case SWL_EVENT_CLIENT_FULLSCREEN:
    case SWL_EVENT_CLIENT_FLOAT:
    case SWL_EVENT_CLIENT_MOVE:
    case SWL_EVENT_CLIENT_RESIZE:
    case SWL_EVENT_CLIENT_URGENT: {
        SwlClient *client = event->data;
        if (client) {
            SwlClientInfo info = swl_client_get_info(client);
            offset += serialize_client_info(buf + offset, sizeof(buf) - offset, &info);
        } else {
            offset += snprintf(buf + offset, sizeof(buf) - offset, "null");
        }
        break;
    }
    case SWL_EVENT_MONITOR_ADD:
    case SWL_EVENT_MONITOR_REMOVE:
    case SWL_EVENT_MONITOR_FOCUS: {
        SwlMonitor *mon = event->data;
        if (mon) {
            SwlMonitorInfo info = swl_monitor_get_info(mon);
            offset += serialize_monitor_info(buf + offset, sizeof(buf) - offset, &info);
        } else {
            offset += snprintf(buf + offset, sizeof(buf) - offset, "null");
        }
        break;
    }
    case SWL_EVENT_LAYER_MAP:
    case SWL_EVENT_LAYER_UNMAP: {
        SwlLayerSurface *layer = event->data;
        if (layer) {
            SwlLayerSurfaceInfo info = swl_layer_surface_get_info(layer);
            offset += serialize_layer_info(buf + offset, sizeof(buf) - offset, &info);
        } else {
            offset += snprintf(buf + offset, sizeof(buf) - offset, "null");
        }
        break;
    }
    default:
        offset += snprintf(buf + offset, sizeof(buf) - offset, "null");
        break;
    }

    offset += snprintf(buf + offset, sizeof(buf) - offset,
        ",\"timestamp\":%" PRIu64 "}\n", ts);

    swl_ipc_broadcast_event(ipc, event->type, buf);
}

static SwlIPCResponse cmd_subscribe(SwlCompositor *comp, const char *args)
{
    (void)comp;
    SwlIPCResponse r = {.success = true};

    uint32_t mask = 0;

    if (!args || *args == '\0') {
        /* No args = subscribe to all */
        mask = SWL_SUBSCRIBE_ALL;
    } else {
        /* Parse space-separated event type names */
        char *copy = strdup(args);
        if (!copy) {
            r.success = false;
            r.error = strdup("out of memory");
            return r;
        }

        char *saveptr = NULL;
        char *token = strtok_r(copy, " ", &saveptr);
        while (token) {
            int type = event_type_from_name(token);
            if (type < 0) {
                r.success = false;
                char err[256];
                snprintf(err, sizeof(err), "unknown event type: %s", token);
                r.error = strdup(err);
                free(copy);
                return r;
            }
            mask |= 1u << (uint32_t)type;
            token = strtok_r(NULL, " ", &saveptr);
        }
        free(copy);
    }

    r.json = strdup("{\"subscribed\":true}\n");
    r.keep_open = true;
    r.event_mask = mask;
    return r;
}

static SwlIPCResponse cmd_dispatch(SwlCompositor *comp, const char *args)
{
    SwlIPCResponse r = {.success = true};

    if (!args || *args == '\0') {
        r.success = false;
        r.error = strdup("usage: dispatch <action> [arg]");
        return r;
    }

    /* Parse "<action> [arg]" */
    char *copy = strdup(args);
    if (!copy) {
        r.success = false;
        r.error = strdup("out of memory");
        return r;
    }

    char *action = copy;
    char *arg = NULL;
    char *space = strchr(copy, ' ');
    if (space) {
        *space = '\0';
        arg = space + 1;
    }

    SwlInput *input = swl_compositor_get_input(comp);
    if (!input) {
        r.success = false;
        r.error = strdup("input subsystem not available");
        free(copy);
        return r;
    }

    SwlKeybindingManager *kb = swl_input_get_keybindings(input);
    if (!kb) {
        r.success = false;
        r.error = strdup("keybinding manager not available");
        free(copy);
        return r;
    }

    SwlError err = swl_action_dispatch(kb, action, arg);
    if (err != SWL_OK) {
        r.success = false;
        char errbuf[256];
        snprintf(errbuf, sizeof(errbuf), "unknown action: %s", action);
        r.error = strdup(errbuf);
        free(copy);
        return r;
    }

    free(copy);
    r.json = strdup("ok");
    return r;
}

void ipc_subscribe_all_events(SwlIPC *ipc)
{
    SwlEventBus *bus = swl_compositor_get_event_bus(ipc->comp);
    if (!bus)
        return;

    ipc->event_sub_count = 0;
    for (size_t i = 0; i < EVENT_TYPE_COUNT; i++) {
        if (!event_type_names[i])
            continue;
        int sub_id = swl_event_bus_subscribe(bus, (SwlEventType)i,
            ipc_event_handler, ipc);
        if (sub_id >= 0 && ipc->event_sub_count < 24) {
            ipc->event_sub_ids[ipc->event_sub_count++] = sub_id;
        }
    }
}

void swl_ipc_register_builtins(SwlIPC *ipc)
{
    swl_ipc_register_command(ipc, "get-windows", cmd_get_windows);
    swl_ipc_register_command(ipc, "get-monitors", cmd_get_monitors);
    swl_ipc_register_command(ipc, "get-layouts", cmd_get_layouts);
    swl_ipc_register_command(ipc, "focus", cmd_focus);
    swl_ipc_register_command(ipc, "close", cmd_close);
    swl_ipc_register_command(ipc, "layout", cmd_layout);
    swl_ipc_register_command(ipc, "quit", cmd_quit);
    swl_ipc_register_command(ipc, "reload-config", cmd_reload_config);
    swl_ipc_register_command(ipc, "output-power", cmd_output_power);
    swl_ipc_register_command(ipc, "subscribe", cmd_subscribe);
    swl_ipc_register_command(ipc, "dispatch", cmd_dispatch);

    ipc_subscribe_all_events(ipc);
}
