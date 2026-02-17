#define _POSIX_C_SOURCE 200809L
#include "ipc_internal.h"
#include "compositor.h"
#include <stdlib.h>
#include <string.h>

DwlIPC *dwl_ipc_create(DwlCompositor *comp)
{
    DwlIPC *ipc = calloc(1, sizeof(*ipc));
    if (!ipc)
        return NULL;

    ipc->comp = comp;
    ipc->socket_fd = -1;

    if (dwl_ipc_socket_init(ipc) < 0) {
        free(ipc);
        return NULL;
    }

    return ipc;
}

void dwl_ipc_destroy(DwlIPC *ipc)
{
    if (!ipc)
        return;

    dwl_ipc_socket_cleanup(ipc);

    for (size_t i = 0; i < ipc->command_count; i++)
        free(ipc->commands[i].name);

    free(ipc);
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
