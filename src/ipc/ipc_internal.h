#ifndef DWL_IPC_INTERNAL_H
#define DWL_IPC_INTERNAL_H

#include "ipc.h"
#include <wayland-server-core.h>

#define MAX_COMMANDS 64
#define SOCKET_PATH "/tmp/dwl.sock"
#define BUFFER_SIZE 8192

typedef struct {
    char *name;
    DwlIPCHandler handler;
} IPCCommand;

typedef struct {
    int fd;
    DwlIPC *ipc;
    struct wl_event_source *event_source;
} IPCClient;

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

/* socket.c */
int dwl_ipc_socket_init(DwlIPC *ipc);
void dwl_ipc_socket_cleanup(DwlIPC *ipc);

/* commands.c */
void dwl_ipc_register_builtins(DwlIPC *ipc);

#endif /* DWL_IPC_INTERNAL_H */
