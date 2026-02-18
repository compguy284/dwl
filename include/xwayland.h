#ifndef SWL_XWAYLAND_H
#define SWL_XWAYLAND_H

#include <stdbool.h>
#include "error.h"

typedef struct SwlXWayland SwlXWayland;
typedef struct SwlCompositor SwlCompositor;
typedef struct SwlClient SwlClient;

#ifdef SWL_XWAYLAND

SwlXWayland *swl_xwayland_create(SwlCompositor *comp);
void swl_xwayland_destroy(SwlXWayland *xwl);

bool swl_xwayland_is_ready(SwlXWayland *xwl);
const char *swl_xwayland_get_display(SwlXWayland *xwl);

bool swl_client_is_x11(const SwlClient *client);
bool swl_client_is_x11_unmanaged(const SwlClient *client);
const char *swl_client_get_x11_class(const SwlClient *client);
const char *swl_client_get_x11_instance(const SwlClient *client);
int swl_client_get_x11_pid(const SwlClient *client);

#else

static inline SwlXWayland *swl_xwayland_create(SwlCompositor *comp)
{
    (void)comp;
    return NULL;
}

static inline void swl_xwayland_destroy(SwlXWayland *xwl)
{
    (void)xwl;
}

static inline bool swl_xwayland_is_ready(SwlXWayland *xwl)
{
    (void)xwl;
    return false;
}

static inline const char *swl_xwayland_get_display(SwlXWayland *xwl)
{
    (void)xwl;
    return NULL;
}

static inline bool swl_client_is_x11(const SwlClient *client)
{
    (void)client;
    return false;
}

static inline bool swl_client_is_x11_unmanaged(const SwlClient *client)
{
    (void)client;
    return false;
}

static inline const char *swl_client_get_x11_class(const SwlClient *client)
{
    (void)client;
    return NULL;
}

static inline const char *swl_client_get_x11_instance(const SwlClient *client)
{
    (void)client;
    return NULL;
}

static inline int swl_client_get_x11_pid(const SwlClient *client)
{
    (void)client;
    return -1;
}

#endif /* SWL_XWAYLAND */

#endif /* SWL_XWAYLAND_H */
