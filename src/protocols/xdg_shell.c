#define _POSIX_C_SOURCE 200809L
#include "xdg_shell.h"
#include "compositor.h"
#include "client.h"
#include "monitor.h"
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <scenefx/types/wlr_scene.h>

extern DwlClient *dwl_client_create_xdg(DwlClientManager *mgr, struct wlr_xdg_toplevel *toplevel);

void dwl_xdg_shell_handle_new_toplevel(DwlCompositor *comp, struct wlr_xdg_toplevel *toplevel)
{
    DwlClient *client = dwl_client_create_xdg(dwl_compositor_get_clients(comp), toplevel);
    if (!client)
        return;

    DwlMonitor *mon = dwl_monitor_get_focused(dwl_compositor_get_output(comp));
    if (mon)
        dwl_client_move_to_monitor(client, mon);
}

void dwl_xdg_shell_handle_new_popup(struct wlr_xdg_popup *popup)
{
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (!parent)
        return;

    struct wlr_scene_tree *parent_tree = parent->data;
    if (parent_tree)
        popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);
}
