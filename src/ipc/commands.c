#define _POSIX_C_SOURCE 200809L
#include "ipc_internal.h"
#include "compositor.h"
#include "client.h"
#include "config.h"
#include "layout.h"
#include "monitor.h"
#include "workspace.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static DwlIPCResponse cmd_get_windows(DwlCompositor *comp, const char *args)
{
    (void)args;
    DwlIPCResponse r = {.success = true};

    DwlClientManager *mgr = dwl_compositor_get_clients(comp);
    if (!mgr) {
        r.json = strdup("[]");
        return r;
    }

    char *json = malloc(BUFFER_SIZE);
    int offset = 0;
    offset += sprintf(json + offset, "[");

    bool first = true;
    size_t count = dwl_client_count(mgr);
    for (size_t i = 0; i < count; i++) {
        DwlClient *c = dwl_client_by_id(mgr, i + 1);
        if (!c) continue;

        DwlClientInfo info = dwl_client_get_info(c);

        if (!first) offset += sprintf(json + offset, ",");
        first = false;

        offset += sprintf(json + offset,
            "{\"id\":%u,\"app_id\":\"%s\",\"title\":\"%s\","
            "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,"
            "\"tags\":%u,\"floating\":%s,\"fullscreen\":%s,\"focused\":%s}",
            info.id,
            info.app_id ? info.app_id : "",
            info.title ? info.title : "",
            info.geometry.x, info.geometry.y,
            info.geometry.width, info.geometry.height,
            info.tags,
            info.floating ? "true" : "false",
            info.fullscreen ? "true" : "false",
            info.focused ? "true" : "false");
    }

    offset += sprintf(json + offset, "]");
    r.json = json;
    return r;
}

static DwlIPCResponse cmd_quit(DwlCompositor *comp, const char *args)
{
    (void)args;
    DwlIPCResponse r = {.success = true, .json = strdup("ok")};
    dwl_compositor_quit(comp);
    return r;
}

static DwlIPCResponse cmd_reload_config(DwlCompositor *comp, const char *args)
{
    (void)args;
    DwlIPCResponse r = {.success = true};

    DwlConfig *cfg = dwl_compositor_get_config(comp);
    if (cfg && dwl_config_reload(cfg) == DWL_OK)
        r.json = strdup("ok");
    else {
        r.success = false;
        r.error = strdup("failed to reload config");
    }

    return r;
}

static DwlIPCResponse cmd_get_monitors(DwlCompositor *comp, const char *args)
{
    (void)args;
    DwlIPCResponse r = {.success = true};

    DwlOutputManager *output = dwl_compositor_get_output(comp);
    if (!output) {
        r.json = strdup("[]");
        return r;
    }

    char *json = malloc(BUFFER_SIZE);
    int offset = 0;
    offset += sprintf(json + offset, "[");

    bool first = true;
    size_t count = dwl_monitor_count(output);
    for (size_t i = 0; i < count; i++) {
        DwlMonitor *mon = dwl_monitor_by_index(output, i);
        if (!mon) continue;

        DwlMonitorInfo info = dwl_monitor_get_info(mon);

        if (!first) offset += sprintf(json + offset, ",");
        first = false;

        offset += sprintf(json + offset,
            "{\"id\":%u,\"name\":\"%s\",\"x\":%d,\"y\":%d,"
            "\"width\":%d,\"height\":%d,\"scale\":%.2f,"
            "\"tags\":%u,\"enabled\":%s}",
            info.id,
            info.name ? info.name : "",
            info.x, info.y, info.width, info.height,
            info.scale,
            dwl_monitor_get_tags(mon),
            info.enabled ? "true" : "false");
    }

    offset += sprintf(json + offset, "]");
    r.json = json;
    return r;
}

static DwlIPCResponse cmd_get_tags(DwlCompositor *comp, const char *args)
{
    (void)args;
    DwlIPCResponse r = {.success = true};

    DwlOutputManager *output = dwl_compositor_get_output(comp);
    DwlMonitor *mon = dwl_monitor_get_focused(output);

    if (!mon) {
        r.json = strdup("{\"visible\":0,\"occupied\":0,\"urgent\":0}");
        return r;
    }

    uint32_t visible = dwl_workspace_get_visible(mon);
    uint32_t occupied = dwl_workspace_get_occupied(mon);
    uint32_t urgent = dwl_workspace_get_urgent(mon);

    char *json = malloc(256);
    sprintf(json, "{\"visible\":%u,\"occupied\":%u,\"urgent\":%u}",
        visible, occupied, urgent);
    r.json = json;
    return r;
}

static DwlIPCResponse cmd_focus(DwlCompositor *comp, const char *args)
{
    DwlIPCResponse r = {.success = true};

    if (!args) {
        r.success = false;
        r.error = strdup("missing window id");
        return r;
    }

    uint32_t id = (uint32_t)atoi(args);
    DwlClientManager *clients = dwl_compositor_get_clients(comp);
    DwlClient *client = dwl_client_by_id(clients, id);

    if (!client) {
        r.success = false;
        r.error = strdup("window not found");
        return r;
    }

    dwl_client_focus(client);
    r.json = strdup("ok");
    return r;
}

static DwlIPCResponse cmd_close(DwlCompositor *comp, const char *args)
{
    DwlIPCResponse r = {.success = true};

    if (!args) {
        r.success = false;
        r.error = strdup("missing window id");
        return r;
    }

    uint32_t id = (uint32_t)atoi(args);
    DwlClientManager *clients = dwl_compositor_get_clients(comp);
    DwlClient *client = dwl_client_by_id(clients, id);

    if (!client) {
        r.success = false;
        r.error = strdup("window not found");
        return r;
    }

    dwl_client_close(client);
    r.json = strdup("ok");
    return r;
}

static DwlIPCResponse cmd_view(DwlCompositor *comp, const char *args)
{
    DwlIPCResponse r = {.success = true};

    if (!args) {
        r.success = false;
        r.error = strdup("missing tags");
        return r;
    }

    uint32_t tags = (uint32_t)atoi(args);
    DwlOutputManager *output = dwl_compositor_get_output(comp);
    DwlMonitor *mon = dwl_monitor_get_focused(output);

    if (!mon) {
        r.success = false;
        r.error = strdup("no monitor focused");
        return r;
    }

    dwl_workspace_view(mon, tags);
    r.json = strdup("ok");
    return r;
}

static DwlIPCResponse cmd_layout(DwlCompositor *comp, const char *args)
{
    DwlIPCResponse r = {.success = true};

    if (!args) {
        r.success = false;
        r.error = strdup("missing layout name");
        return r;
    }

    DwlLayoutRegistry *layouts = dwl_compositor_get_layouts(comp);
    const DwlLayout *layout = dwl_layout_get(layouts, args);

    if (!layout) {
        r.success = false;
        r.error = strdup("layout not found");
        return r;
    }

    DwlOutputManager *output = dwl_compositor_get_output(comp);
    DwlMonitor *mon = dwl_monitor_get_focused(output);

    if (!mon) {
        r.success = false;
        r.error = strdup("no monitor focused");
        return r;
    }

    dwl_monitor_set_layout(mon, layout);
    dwl_monitor_arrange(mon);
    r.json = strdup("ok");
    return r;
}

static DwlIPCResponse cmd_get_layouts(DwlCompositor *comp, const char *args)
{
    (void)args;
    DwlIPCResponse r = {.success = true};

    DwlLayoutRegistry *layouts = dwl_compositor_get_layouts(comp);
    if (!layouts) {
        r.json = strdup("[]");
        return r;
    }

    size_t count;
    const char **names = dwl_layout_list(layouts, &count);

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

void dwl_ipc_register_builtins(DwlIPC *ipc)
{
    dwl_ipc_register_command(ipc, "get-windows", cmd_get_windows);
    dwl_ipc_register_command(ipc, "get-monitors", cmd_get_monitors);
    dwl_ipc_register_command(ipc, "get-tags", cmd_get_tags);
    dwl_ipc_register_command(ipc, "get-layouts", cmd_get_layouts);
    dwl_ipc_register_command(ipc, "focus", cmd_focus);
    dwl_ipc_register_command(ipc, "close", cmd_close);
    dwl_ipc_register_command(ipc, "view", cmd_view);
    dwl_ipc_register_command(ipc, "layout", cmd_layout);
    dwl_ipc_register_command(ipc, "quit", cmd_quit);
    dwl_ipc_register_command(ipc, "reload-config", cmd_reload_config);
}
