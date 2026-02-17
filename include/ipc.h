#ifndef DWL_IPC_H
#define DWL_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include "error.h"

typedef struct DwlIPC DwlIPC;
typedef struct DwlCompositor DwlCompositor;

typedef struct DwlIPCResponse {
    bool success;
    char *json;
    char *error;
} DwlIPCResponse;

typedef DwlIPCResponse (*DwlIPCHandler)(DwlCompositor *comp, const char *args);

DwlIPC *dwl_ipc_create(DwlCompositor *comp);
void dwl_ipc_destroy(DwlIPC *ipc);

DwlError dwl_ipc_register_command(DwlIPC *ipc, const char *name, DwlIPCHandler handler);
DwlError dwl_ipc_unregister_command(DwlIPC *ipc, const char *name);
void dwl_ipc_register_builtins(DwlIPC *ipc);

DwlIPCResponse dwl_ipc_execute(DwlIPC *ipc, const char *command, const char *args);
void dwl_ipc_response_free(DwlIPCResponse *response);

typedef void (*DwlStatusHandler)(void *ctx, const char *status_json);
void dwl_ipc_set_status_handler(DwlIPC *ipc, DwlStatusHandler handler, void *ctx);
void dwl_ipc_emit_status(DwlIPC *ipc);

const char *dwl_ipc_get_socket_path(DwlIPC *ipc);
size_t dwl_ipc_command_count(const DwlIPC *ipc);
const char **dwl_ipc_command_list(const DwlIPC *ipc, size_t *count);

#endif /* DWL_IPC_H */
