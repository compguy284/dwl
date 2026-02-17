#ifndef DWL_XWAYLAND_H
#define DWL_XWAYLAND_H

#include <stdbool.h>
#include "error.h"

typedef struct DwlXWayland DwlXWayland;
typedef struct DwlCompositor DwlCompositor;
typedef struct DwlClient DwlClient;

#ifdef DWL_XWAYLAND

DwlXWayland *dwl_xwayland_create(DwlCompositor *comp);
void dwl_xwayland_destroy(DwlXWayland *xwl);

bool dwl_xwayland_is_ready(DwlXWayland *xwl);
const char *dwl_xwayland_get_display(DwlXWayland *xwl);

bool dwl_client_is_x11(const DwlClient *client);
bool dwl_client_is_x11_unmanaged(const DwlClient *client);
const char *dwl_client_get_x11_class(const DwlClient *client);
const char *dwl_client_get_x11_instance(const DwlClient *client);
int dwl_client_get_x11_pid(const DwlClient *client);

#else

static inline DwlXWayland *dwl_xwayland_create(DwlCompositor *comp)
{
    (void)comp;
    return NULL;
}

static inline void dwl_xwayland_destroy(DwlXWayland *xwl)
{
    (void)xwl;
}

static inline bool dwl_xwayland_is_ready(DwlXWayland *xwl)
{
    (void)xwl;
    return false;
}

static inline const char *dwl_xwayland_get_display(DwlXWayland *xwl)
{
    (void)xwl;
    return NULL;
}

static inline bool dwl_client_is_x11(const DwlClient *client)
{
    (void)client;
    return false;
}

static inline bool dwl_client_is_x11_unmanaged(const DwlClient *client)
{
    (void)client;
    return false;
}

static inline const char *dwl_client_get_x11_class(const DwlClient *client)
{
    (void)client;
    return NULL;
}

static inline const char *dwl_client_get_x11_instance(const DwlClient *client)
{
    (void)client;
    return NULL;
}

static inline int dwl_client_get_x11_pid(const DwlClient *client)
{
    (void)client;
    return -1;
}

#endif /* DWL_XWAYLAND */

#endif /* DWL_XWAYLAND_H */
