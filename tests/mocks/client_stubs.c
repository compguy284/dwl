/* Mock stubs for client.h functions used by rules.c
 * These stubs allow testing rules.c without depending on the full client implementation
 */

#include "client.h"

SwlClientInfo swl_client_get_info(const SwlClient *client)
{
    (void)client;
    SwlClientInfo info = {
        .id = 0,
        .app_id = NULL,
        .title = NULL,
        .geometry = { 0, 0, 0, 0 },
        .floating = false,
        .fullscreen = false,
        .urgent = false,
        .focused = false,
        .x11 = false,
    };
    return info;
}

SwlError swl_client_set_floating(SwlClient *client, bool floating)
{
    (void)client;
    (void)floating;
    return SWL_OK;
}
