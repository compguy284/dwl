#define _POSIX_C_SOURCE 200809L
#include "ipc_internal.h"
#include "compositor.h"
#include <stdlib.h>
#include <string.h>

SwlIPC *swl_ipc_create(SwlCompositor *comp)
{
    SwlIPC *ipc = calloc(1, sizeof(*ipc));
    if (!ipc)
        return NULL;

    ipc->comp = comp;
    ipc->socket_fd = -1;

    if (swl_ipc_socket_init(ipc) < 0) {
        free(ipc);
        return NULL;
    }

    return ipc;
}

void swl_ipc_destroy(SwlIPC *ipc)
{
    if (!ipc)
        return;

    swl_ipc_socket_cleanup(ipc);

    for (size_t i = 0; i < ipc->command_count; i++)
        free(ipc->commands[i].name);

    free(ipc);
}

SwlError swl_ipc_register_command(SwlIPC *ipc, const char *name, SwlIPCHandler handler)
{
    if (!ipc || !name || !handler)
        return SWL_ERR_INVALID_ARG;

    if (ipc->command_count >= MAX_COMMANDS)
        return SWL_ERR_NOMEM;

    ipc->commands[ipc->command_count].name = strdup(name);
    ipc->commands[ipc->command_count].handler = handler;
    ipc->command_count++;

    return SWL_OK;
}

SwlError swl_ipc_unregister_command(SwlIPC *ipc, const char *name)
{
    if (!ipc || !name)
        return SWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < ipc->command_count; i++) {
        if (strcmp(ipc->commands[i].name, name) == 0) {
            free(ipc->commands[i].name);
            memmove(&ipc->commands[i], &ipc->commands[i + 1],
                    (ipc->command_count - i - 1) * sizeof(IPCCommand));
            ipc->command_count--;
            return SWL_OK;
        }
    }

    return SWL_ERR_NOT_FOUND;
}

SwlIPCResponse swl_ipc_execute(SwlIPC *ipc, const char *command, const char *args)
{
    SwlIPCResponse response = {0};

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

void swl_ipc_response_free(SwlIPCResponse *response)
{
    if (!response)
        return;

    free(response->json);
    free(response->error);
    response->json = NULL;
    response->error = NULL;
}

void swl_ipc_set_status_handler(SwlIPC *ipc, SwlStatusHandler handler, void *ctx)
{
    if (!ipc)
        return;

    ipc->status_handler = handler;
    ipc->status_ctx = ctx;
}

void swl_ipc_emit_status(SwlIPC *ipc)
{
    if (!ipc || !ipc->status_handler)
        return;

    // TODO: Build status JSON
    ipc->status_handler(ipc->status_ctx, "{}");
}

const char *swl_ipc_get_socket_path(SwlIPC *ipc)
{
    return ipc ? ipc->socket_path : NULL;
}

size_t swl_ipc_command_count(const SwlIPC *ipc)
{
    return ipc ? ipc->command_count : 0;
}

const char **swl_ipc_command_list(const SwlIPC *ipc, size_t *count)
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
