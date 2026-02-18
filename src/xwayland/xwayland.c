#include "xwayland.h"

#ifdef SWL_XWAYLAND

#include "compositor.h"
#include "client.h"
#include "events.h"
#include "monitor.h"
#include "scene.h"
#include "render.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/xwayland.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

struct SwlXWayland {
    SwlCompositor *comp;
    struct wlr_xwayland *xwayland;
    bool ready;

    struct wl_listener xwl_ready;
    struct wl_listener new_surface;
};

// Forward declaration of X11 client creation
extern SwlClient *swl_client_create_x11(SwlClientManager *mgr, struct wlr_xwayland_surface *surface);

static void handle_ready(struct wl_listener *listener, void *data);
static void handle_new_surface(struct wl_listener *listener, void *data);

SwlXWayland *swl_xwayland_create(SwlCompositor *comp)
{
    SwlXWayland *xwl = calloc(1, sizeof(*xwl));
    if (!xwl)
        return NULL;

    xwl->comp = comp;

    struct wl_display *display = swl_compositor_get_wl_display(comp);
    struct wlr_compositor *wlr_comp = swl_compositor_get_wlr_compositor(comp);

    xwl->xwayland = wlr_xwayland_create(display, wlr_comp, true);
    if (!xwl->xwayland) {
        fprintf(stderr, "Failed to create XWayland\n");
        free(xwl);
        return NULL;
    }

    xwl->xwl_ready.notify = handle_ready;
    wl_signal_add(&xwl->xwayland->events.ready, &xwl->xwl_ready);

    xwl->new_surface.notify = handle_new_surface;
    wl_signal_add(&xwl->xwayland->events.new_surface, &xwl->new_surface);

    fprintf(stderr, "XWayland initialized, display: %s\n", xwl->xwayland->display_name);

    return xwl;
}

void swl_xwayland_destroy(SwlXWayland *xwl)
{
    if (!xwl)
        return;

    wl_list_remove(&xwl->xwl_ready.link);
    wl_list_remove(&xwl->new_surface.link);

    if (xwl->xwayland)
        wlr_xwayland_destroy(xwl->xwayland);

    free(xwl);
}

static void handle_ready(struct wl_listener *listener, void *data)
{
    SwlXWayland *xwl = wl_container_of(listener, xwl, xwl_ready);
    (void)data;

    xwl->ready = true;

    struct wlr_seat *seat = swl_compositor_get_seat(xwl->comp);
    if (seat)
        wlr_xwayland_set_seat(xwl->xwayland, seat);

    fprintf(stderr, "XWayland ready\n");
}

static void handle_new_surface(struct wl_listener *listener, void *data)
{
    SwlXWayland *xwl = wl_container_of(listener, xwl, new_surface);
    struct wlr_xwayland_surface *surface = data;

    // Skip surfaces that want to handle themselves (override_redirect)
    // These are things like menus, tooltips, etc.
    if (surface->override_redirect) {
        fprintf(stderr, "XWayland: new unmanaged surface (override_redirect)\n");
        // For unmanaged surfaces, we still need to create a scene node
        // but we don't manage them as regular clients
        // TODO: Handle unmanaged X11 surfaces
        return;
    }

    fprintf(stderr, "XWayland: new surface class=%s instance=%s\n",
            surface->class ? surface->class : "(null)",
            surface->instance ? surface->instance : "(null)");

    SwlClientManager *clients = swl_compositor_get_clients(xwl->comp);
    SwlClient *client = swl_client_create_x11(clients, surface);
    if (!client) {
        fprintf(stderr, "XWayland: failed to create client\n");
        return;
    }

    SwlOutputManager *output = swl_compositor_get_output(xwl->comp);
    SwlMonitor *mon = swl_monitor_get_focused(output);
    if (mon)
        swl_client_move_to_monitor(client, mon);
}

bool swl_xwayland_is_ready(SwlXWayland *xwl)
{
    return xwl && xwl->ready;
}

const char *swl_xwayland_get_display(SwlXWayland *xwl)
{
    if (!xwl || !xwl->xwayland)
        return NULL;

    return xwl->xwayland->display_name;
}

#endif /* SWL_XWAYLAND */
