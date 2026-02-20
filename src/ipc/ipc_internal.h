#ifndef SWL_IPC_INTERNAL_H
#define SWL_IPC_INTERNAL_H

#include "ipc.h"
#include "events.h"
#include <wayland-server-core.h>

#define MAX_COMMANDS 64
#define SOCKET_PATH "/tmp/swl.sock"
#define BUFFER_SIZE 8192
#define MAX_SUBSCRIBERS 32
#define SWL_SUBSCRIBE_ALL ((uint32_t)0xFFFFFFFF)

typedef struct {
    char *name;
    SwlIPCHandler handler;
} IPCCommand;

typedef struct {
    int fd;
    SwlIPC *ipc;
    struct wl_event_source *event_source;
} IPCClient;

typedef struct {
    int fd;
    struct wl_event_source *event_source;
    uint32_t event_mask;
    bool active;
} IPCSubscriber;

struct SwlIPC {
    SwlCompositor *comp;
    int socket_fd;
    char *socket_path;
    struct wl_event_source *event_source;

    IPCCommand commands[MAX_COMMANDS];
    size_t command_count;

    SwlStatusHandler status_handler;
    void *status_ctx;

    IPCSubscriber subscribers[MAX_SUBSCRIBERS];
    size_t subscriber_count;

    int event_sub_ids[24];
    size_t event_sub_count;
};

/* socket.c */
int swl_ipc_socket_init(SwlIPC *ipc);
void swl_ipc_socket_cleanup(SwlIPC *ipc);
void swl_ipc_add_subscriber(SwlIPC *ipc, int fd, struct wl_event_source *event_source,
                             uint32_t event_mask);
void swl_ipc_broadcast_event(SwlIPC *ipc, SwlEventType type, const char *json);

/* commands.c */
void swl_ipc_register_builtins(SwlIPC *ipc);
void ipc_subscribe_all_events(SwlIPC *ipc);

#endif /* SWL_IPC_INTERNAL_H */
