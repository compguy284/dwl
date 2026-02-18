#ifndef SWL_IPC_INTERNAL_H
#define SWL_IPC_INTERNAL_H

#include "ipc.h"
#include <wayland-server-core.h>

#define MAX_COMMANDS 64
#define SOCKET_PATH "/tmp/swl.sock"
#define BUFFER_SIZE 8192

typedef struct {
    char *name;
    SwlIPCHandler handler;
} IPCCommand;

typedef struct {
    int fd;
    SwlIPC *ipc;
    struct wl_event_source *event_source;
} IPCClient;

struct SwlIPC {
    SwlCompositor *comp;
    int socket_fd;
    char *socket_path;
    struct wl_event_source *event_source;

    IPCCommand commands[MAX_COMMANDS];
    size_t command_count;

    SwlStatusHandler status_handler;
    void *status_ctx;
};

/* socket.c */
int swl_ipc_socket_init(SwlIPC *ipc);
void swl_ipc_socket_cleanup(SwlIPC *ipc);

/* commands.c */
void swl_ipc_register_builtins(SwlIPC *ipc);

#endif /* SWL_IPC_INTERNAL_H */
