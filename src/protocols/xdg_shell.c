#define _POSIX_C_SOURCE 200809L
#include "xdg_shell.h"
#include "compositor.h"
#include "client.h"
#include "monitor.h"
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_output_layout.h>
#include <scenefx/types/wlr_scene.h>

extern SwlClient *swl_client_create_xdg(SwlClientManager *mgr, struct wlr_xdg_toplevel *toplevel);

void swl_xdg_shell_handle_new_toplevel(SwlCompositor *comp, struct wlr_xdg_toplevel *toplevel)
{
    SwlClient *client = swl_client_create_xdg(swl_compositor_get_clients(comp), toplevel);
    if (!client)
        return;

    SwlMonitor *mon = swl_monitor_get_focused(swl_compositor_get_output(comp));
    if (mon)
        swl_client_move_to_monitor(client, mon);
}

struct popup_commit_data {
    struct wl_listener commit;
    SwlCompositor *comp;
};

static void popup_handle_commit(struct wl_listener *listener, void *data)
{
    struct popup_commit_data *pcd = wl_container_of(listener, pcd, commit);
    struct wlr_surface *surface = data;
    struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);

    if (!popup || !popup->base->initial_commit)
        return;

    if (!popup->parent)
        goto cleanup;

    struct wlr_scene_tree *parent_tree = popup->parent->data;
    if (!parent_tree)
        goto cleanup;

    popup->base->surface->data = wlr_scene_xdg_surface_create(
            parent_tree, popup->base);
    if (!popup->base->surface->data) {
        wlr_xdg_popup_destroy(popup);
        goto cleanup;
    }

    /* Unconstrain popup to keep it within the output bounds */
    int lx, ly;
    if (wlr_scene_node_coords(&parent_tree->node, &lx, &ly)) {
        struct wlr_output_layout *layout =
                swl_compositor_get_output_layout(pcd->comp);
        struct wlr_output *output =
                wlr_output_layout_output_at(layout, lx, ly);
        if (output) {
            struct wlr_box box;
            wlr_output_layout_get_box(layout, output, &box);
            box.x -= lx;
            box.y -= ly;
            wlr_xdg_popup_unconstrain_from_box(popup, &box);
        }
    }

cleanup:
    wl_list_remove(&pcd->commit.link);
    free(pcd);
}

void swl_xdg_shell_handle_new_popup(SwlCompositor *comp, struct wlr_xdg_popup *popup)
{
    struct popup_commit_data *pcd = calloc(1, sizeof(*pcd));
    if (!pcd)
        return;

    pcd->comp = comp;
    pcd->commit.notify = popup_handle_commit;
    wl_signal_add(&popup->base->surface->events.commit, &pcd->commit);
}
