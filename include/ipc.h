#ifndef SWL_IPC_H
#define SWL_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "error.h"

typedef struct SwlIPC SwlIPC;
typedef struct SwlCompositor SwlCompositor;

typedef struct SwlIPCResponse {
    bool success;
    char *json;
    char *error;
    bool keep_open;
    uint32_t event_mask;
} SwlIPCResponse;

typedef SwlIPCResponse (*SwlIPCHandler)(SwlCompositor *comp, const char *args);

SwlIPC *swl_ipc_create(SwlCompositor *comp);
void swl_ipc_destroy(SwlIPC *ipc);

SwlError swl_ipc_register_command(SwlIPC *ipc, const char *name, SwlIPCHandler handler);
SwlError swl_ipc_unregister_command(SwlIPC *ipc, const char *name);
void swl_ipc_register_builtins(SwlIPC *ipc);

SwlIPCResponse swl_ipc_execute(SwlIPC *ipc, const char *command, const char *args);
void swl_ipc_response_free(SwlIPCResponse *response);

typedef void (*SwlStatusHandler)(void *ctx, const char *status_json);
void swl_ipc_set_status_handler(SwlIPC *ipc, SwlStatusHandler handler, void *ctx);
void swl_ipc_emit_status(SwlIPC *ipc);

const char *swl_ipc_get_socket_path(SwlIPC *ipc);
size_t swl_ipc_command_count(const SwlIPC *ipc);
const char **swl_ipc_command_list(const SwlIPC *ipc, size_t *count);

#endif /* SWL_IPC_H */
