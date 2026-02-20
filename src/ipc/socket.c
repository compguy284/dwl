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

int swl_ipc_socket_init(SwlIPC *ipc)
{
    char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime) {
        ipc->socket_path = malloc(strlen(runtime) + 16);
        sprintf(ipc->socket_path, "%s/swl.sock", runtime);
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

    struct wl_display *display = swl_compositor_get_wl_display(ipc->comp);
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    ipc->event_source = wl_event_loop_add_fd(loop, ipc->socket_fd,
        WL_EVENT_READABLE, handle_socket, ipc);

    setenv("SWL_SOCKET", ipc->socket_path, 1);

    return 0;
}

void swl_ipc_socket_cleanup(SwlIPC *ipc)
{
    /* Close all active subscribers */
    for (size_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (ipc->subscribers[i].active) {
            if (ipc->subscribers[i].event_source)
                wl_event_source_remove(ipc->subscribers[i].event_source);
            close(ipc->subscribers[i].fd);
            ipc->subscribers[i].active = false;
        }
    }
    ipc->subscriber_count = 0;

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
    SwlIPC *ipc = data;
    (void)mask;

    int client_fd = accept(fd, NULL, NULL);
    if (client_fd < 0)
        return 0;

    int flags = fcntl(client_fd, F_GETFL);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    struct wl_display *display = swl_compositor_get_wl_display(ipc->comp);
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

static void ipc_subscriber_cleanup(SwlIPC *ipc, size_t idx)
{
    IPCSubscriber *sub = &ipc->subscribers[idx];
    if (!sub->active)
        return;

    if (sub->event_source)
        wl_event_source_remove(sub->event_source);
    close(sub->fd);
    sub->active = false;
    ipc->subscriber_count--;
}

static int handle_subscriber(int fd, uint32_t mask, void *data)
{
    SwlIPC *ipc = data;

    /* Find the subscriber by fd */
    for (size_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (ipc->subscribers[i].active && ipc->subscribers[i].fd == fd) {
            if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
                ipc_subscriber_cleanup(ipc, i);
                return 0;
            }
            /* Discard any data sent by subscriber */
            char discard[256];
            ssize_t n = read(fd, discard, sizeof(discard));
            if (n <= 0) {
                ipc_subscriber_cleanup(ipc, i);
            }
            return 0;
        }
    }

    return 0;
}

void swl_ipc_add_subscriber(SwlIPC *ipc, int fd, struct wl_event_source *event_source,
                             uint32_t event_mask)
{
    for (size_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!ipc->subscribers[i].active) {
            ipc->subscribers[i].fd = fd;
            ipc->subscribers[i].event_source = event_source;
            ipc->subscribers[i].event_mask = event_mask;
            ipc->subscribers[i].active = true;
            ipc->subscriber_count++;
            return;
        }
    }

    /* No free slot — close the fd */
    if (event_source)
        wl_event_source_remove(event_source);
    close(fd);
}

void swl_ipc_broadcast_event(SwlIPC *ipc, SwlEventType type, const char *json)
{
    if (!ipc || !json || ipc->subscriber_count == 0)
        return;

    uint32_t type_bit = 1u << (uint32_t)type;
    size_t json_len = strlen(json);

    for (size_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        IPCSubscriber *sub = &ipc->subscribers[i];
        if (!sub->active)
            continue;
        if (!(sub->event_mask & type_bit))
            continue;

        ssize_t written = write(sub->fd, json, json_len);
        if (written < 0) {
            /* EPIPE or other error — subscriber gone */
            ipc_subscriber_cleanup(ipc, i);
        }
    }
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

    SwlIPCResponse response = swl_ipc_execute(client->ipc, cmd, args);

    char *reply;
    if (response.success && response.json)
        reply = response.json;
    else if (response.error)
        reply = response.error;
    else
        reply = response.success ? "ok" : "error";

    if (write(client->fd, reply, strlen(reply)) < 0) {
        swl_ipc_response_free(&response);
        ipc_client_cleanup(client);
        return 0;
    }

    if (response.keep_open) {
        /* Convert this client connection to a persistent subscriber */
        int sub_fd = client->fd;
        SwlIPC *ipc = client->ipc;
        uint32_t event_mask_val = response.event_mask;

        /* Remove the IPCClient event source — subscriber gets its own */
        wl_event_source_remove(client->event_source);
        free(client);

        struct wl_display *display = swl_compositor_get_wl_display(ipc->comp);
        struct wl_event_loop *loop = wl_display_get_event_loop(display);
        struct wl_event_source *sub_source = wl_event_loop_add_fd(loop, sub_fd,
            WL_EVENT_READABLE, handle_subscriber, ipc);

        swl_ipc_add_subscriber(ipc, sub_fd, sub_source, event_mask_val);
    } else {
        ipc_client_cleanup(client);
    }

    swl_ipc_response_free(&response);
    return 0;
}
