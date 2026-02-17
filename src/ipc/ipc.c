#define _POSIX_C_SOURCE 200809L
#include "ipc.h"
#include "compositor.h"
#include "client.h"
#include "config.h"
#include "layout.h"
#include "monitor.h"
#include "workspace.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <errno.h>
#include <wayland-server-core.h>

#define MAX_COMMANDS 64
#define SOCKET_PATH "/tmp/dwl.sock"
#define BUFFER_SIZE 8192

typedef struct {
    char *name;
    DwlIPCHandler handler;
} IPCCommand;

struct DwlIPC {
    DwlCompositor *comp;
    int socket_fd;
    char *socket_path;
    struct wl_event_source *event_source;

    IPCCommand commands[MAX_COMMANDS];
    size_t command_count;

    DwlStatusHandler status_handler;
    void *status_ctx;
};

static int handle_client(int fd, uint32_t mask, void *data);
static int handle_socket(int fd, uint32_t mask, void *data);

DwlIPC *dwl_ipc_create(DwlCompositor *comp)
{
    DwlIPC *ipc = calloc(1, sizeof(*ipc));
    if (!ipc)
        return NULL;

    ipc->comp = comp;
    ipc->socket_fd = -1;

    char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime) {
        ipc->socket_path = malloc(strlen(runtime) + 16);
        sprintf(ipc->socket_path, "%s/dwl.sock", runtime);
    } else {
        ipc->socket_path = strdup(SOCKET_PATH);
    }

    unlink(ipc->socket_path);

    ipc->socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ipc->socket_fd < 0) {
        free(ipc->socket_path);
        free(ipc);
        return NULL;
    }

    int flags = fcntl(ipc->socket_fd, F_GETFL);
    fcntl(ipc->socket_fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
    };
    strncpy(addr.sun_path, ipc->socket_path, sizeof(addr.sun_path) - 1);

    if (bind(ipc->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(ipc->socket_fd);
        free(ipc->socket_path);
        free(ipc);
        return NULL;
    }

    if (listen(ipc->socket_fd, 5) < 0) {
        close(ipc->socket_fd);
        unlink(ipc->socket_path);
        free(ipc->socket_path);
        free(ipc);
        return NULL;
    }

    struct wl_display *display = dwl_compositor_get_wl_display(comp);
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    ipc->event_source = wl_event_loop_add_fd(loop, ipc->socket_fd,
        WL_EVENT_READABLE, handle_socket, ipc);

    setenv("DWL_SOCKET", ipc->socket_path, 1);

    return ipc;
}

void dwl_ipc_destroy(DwlIPC *ipc)
{
    if (!ipc)
        return;

    if (ipc->event_source)
        wl_event_source_remove(ipc->event_source);

    if (ipc->socket_fd >= 0) {
        close(ipc->socket_fd);
        unlink(ipc->socket_path);
    }

    for (size_t i = 0; i < ipc->command_count; i++)
        free(ipc->commands[i].name);

    free(ipc->socket_path);
    free(ipc);
}

static int handle_socket(int fd, uint32_t mask, void *data)
{
    DwlIPC *ipc = data;
    (void)mask;

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0)
        return 0;

    int flags = fcntl(client_fd, F_GETFL);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    struct wl_display *display = dwl_compositor_get_wl_display(ipc->comp);
    struct wl_event_loop *loop = wl_display_get_event_loop(display);

    int *client_data = malloc(sizeof(int) * 2);
    client_data[0] = client_fd;
    client_data[1] = (int)(intptr_t)ipc;

    wl_event_loop_add_fd(loop, client_fd, WL_EVENT_READABLE, handle_client, client_data);

    return 0;
}

static int handle_client(int fd, uint32_t mask, void *data)
{
    (void)fd;
    int *client_data = data;
    int client_fd = client_data[0];
    DwlIPC *ipc = (DwlIPC *)(intptr_t)client_data[1];

    if (mask & WL_EVENT_HANGUP) {
        close(client_fd);
        free(client_data);
        return 0;
    }

    char buffer[BUFFER_SIZE];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(client_fd);
        free(client_data);
        return 0;
    }
    buffer[n] = '\0';

    char *space = strchr(buffer, ' ');
    char *cmd = buffer;
    char *args = NULL;
    if (space) {
        *space = '\0';
        args = space + 1;
    }

    DwlIPCResponse response = dwl_ipc_execute(ipc, cmd, args);

    char *reply;
    if (response.success && response.json)
        reply = response.json;
    else if (response.error)
        reply = response.error;
    else
        reply = response.success ? "ok" : "error";

    write(client_fd, reply, strlen(reply));

    dwl_ipc_response_free(&response);
    close(client_fd);
    free(client_data);

    return 0;
}

DwlError dwl_ipc_register_command(DwlIPC *ipc, const char *name, DwlIPCHandler handler)
{
    if (!ipc || !name || !handler)
        return DWL_ERR_INVALID_ARG;

    if (ipc->command_count >= MAX_COMMANDS)
        return DWL_ERR_NOMEM;

    ipc->commands[ipc->command_count].name = strdup(name);
    ipc->commands[ipc->command_count].handler = handler;
    ipc->command_count++;

    return DWL_OK;
}

DwlError dwl_ipc_unregister_command(DwlIPC *ipc, const char *name)
{
    if (!ipc || !name)
        return DWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < ipc->command_count; i++) {
        if (strcmp(ipc->commands[i].name, name) == 0) {
            free(ipc->commands[i].name);
            memmove(&ipc->commands[i], &ipc->commands[i + 1],
                    (ipc->command_count - i - 1) * sizeof(IPCCommand));
            ipc->command_count--;
            return DWL_OK;
        }
    }

    return DWL_ERR_NOT_FOUND;
}

DwlIPCResponse dwl_ipc_execute(DwlIPC *ipc, const char *command, const char *args)
{
    DwlIPCResponse response = {0};

    if (!ipc || !command) {
        response.success = false;
        response.error = strdup("invalid arguments");
        return response;
    }

    for (size_t i = 0; i < ipc->command_count; i++) {
        if (strcmp(ipc->commands[i].name, command) == 0) {
            return ipc->commands[i].handler(ipc->comp, args);
        }
    }

    response.success = false;
    response.error = strdup("unknown command");
    return response;
}

void dwl_ipc_response_free(DwlIPCResponse *response)
{
    if (!response)
        return;

    free(response->json);
    free(response->error);
    response->json = NULL;
    response->error = NULL;
}

void dwl_ipc_set_status_handler(DwlIPC *ipc, DwlStatusHandler handler, void *ctx)
{
    if (!ipc)
        return;

    ipc->status_handler = handler;
    ipc->status_ctx = ctx;
}

void dwl_ipc_emit_status(DwlIPC *ipc)
{
    if (!ipc || !ipc->status_handler)
        return;

    // TODO: Build status JSON
    ipc->status_handler(ipc->status_ctx, "{}");
}

const char *dwl_ipc_get_socket_path(DwlIPC *ipc)
{
    return ipc ? ipc->socket_path : NULL;
}

size_t dwl_ipc_command_count(const DwlIPC *ipc)
{
    return ipc ? ipc->command_count : 0;
}

const char **dwl_ipc_command_list(const DwlIPC *ipc, size_t *count)
{
    if (!ipc || !count)
        return NULL;

    const char **names = calloc(ipc->command_count, sizeof(char *));
    if (!names)
        return NULL;

    for (size_t i = 0; i < ipc->command_count; i++)
        names[i] = ipc->commands[i].name;

    *count = ipc->command_count;
    return names;
}

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
