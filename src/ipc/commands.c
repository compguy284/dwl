#define _POSIX_C_SOURCE 200809L
#include "ipc_internal.h"
#include "compositor.h"
#include "client.h"
#include "config.h"
#include "events.h"
#include "input.h"
#include "keybindings.h"
#include "layout.h"
#include "monitor.h"
#include "render.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
}
