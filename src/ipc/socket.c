#define _POSIX_C_SOURCE 200809L
#include "ipc_internal.h"
#include "compositor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <stdint.h>

static int handle_client(int fd, uint32_t mask, void *data);
static int handle_socket(int fd, uint32_t mask, void *data);

int dwl_ipc_socket_init(DwlIPC *ipc)
{
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
        ipc->socket_path = NULL;
        return -1;
    }

    int flags = fcntl(ipc->socket_fd, F_GETFL);
    fcntl(ipc->socket_fd, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
    };
    strncpy(addr.sun_path, ipc->socket_path, sizeof(addr.sun_path) - 1);

    if (bind(ipc->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(ipc->socket_fd);
        ipc->socket_fd = -1;
        free(ipc->socket_path);
        ipc->socket_path = NULL;
        return -1;
    }

    if (listen(ipc->socket_fd, 5) < 0) {
        close(ipc->socket_fd);
        unlink(ipc->socket_path);
        ipc->socket_fd = -1;
        free(ipc->socket_path);
        ipc->socket_path = NULL;
        return -1;
    }

    struct wl_display *display = dwl_compositor_get_wl_display(ipc->comp);
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    ipc->event_source = wl_event_loop_add_fd(loop, ipc->socket_fd,
        WL_EVENT_READABLE, handle_socket, ipc);

    setenv("DWL_SOCKET", ipc->socket_path, 1);

    return 0;
}

void dwl_ipc_socket_cleanup(DwlIPC *ipc)
{
    if (ipc->event_source) {
        wl_event_source_remove(ipc->event_source);
        ipc->event_source = NULL;
    }

    if (ipc->socket_fd >= 0) {
        close(ipc->socket_fd);
        if (ipc->socket_path)
            unlink(ipc->socket_path);
        ipc->socket_fd = -1;
    }

    free(ipc->socket_path);
    ipc->socket_path = NULL;
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

    IPCClient *client = malloc(sizeof(*client));
    client->fd = client_fd;
    client->ipc = ipc;
    client->event_source = wl_event_loop_add_fd(loop, client_fd,
        WL_EVENT_READABLE, handle_client, client);

    return 0;
}

static void ipc_client_cleanup(IPCClient *client)
{
    wl_event_source_remove(client->event_source);
    close(client->fd);
    free(client);
}

static int handle_client(int fd, uint32_t mask, void *data)
{
    (void)fd;
    IPCClient *client = data;

    if (mask & WL_EVENT_HANGUP) {
        ipc_client_cleanup(client);
        return 0;
    }

    char buffer[BUFFER_SIZE];
    ssize_t n = read(client->fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        ipc_client_cleanup(client);
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

    DwlIPCResponse response = dwl_ipc_execute(client->ipc, cmd, args);

    char *reply;
    if (response.success && response.json)
        reply = response.json;
    else if (response.error)
        reply = response.error;
    else
        reply = response.success ? "ok" : "error";

    if (write(client->fd, reply, strlen(reply)) < 0) {
        // Client disconnected or error - nothing to do
    }

    dwl_ipc_response_free(&response);
    ipc_client_cleanup(client);

    return 0;
}
