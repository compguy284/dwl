#include "scene.h"
#include "compositor.h"
#include "client.h"
#include "render.h"
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <scenefx/types/wlr_scene.h>
#include <scenefx/types/fx/blur_data.h>
#include <scenefx/types/fx/corner_location.h>
#include <scenefx/types/fx/clipped_region.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>
#ifdef DWL_XWAYLAND
#include <wlr/xwayland.h>
#endif

struct DwlSceneManager {
    DwlCompositor *comp;
    struct wlr_scene *scene;
    struct wlr_scene_tree *layers[DWL_LAYER_COUNT];
};

DwlSceneManager *dwl_scene_manager_create(DwlCompositor *comp)
{
    DwlSceneManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->comp = comp;
    mgr->scene = dwl_compositor_get_scene(comp);

    for (int i = 0; i < DWL_LAYER_COUNT; i++) {
        mgr->layers[i] = wlr_scene_tree_create(&mgr->scene->tree);
    }

    return mgr;
}

void dwl_scene_manager_destroy(DwlSceneManager *mgr)
{
    if (!mgr)
        return;

    free(mgr);
}

struct wlr_scene_tree *dwl_scene_get_layer(DwlSceneManager *mgr, DwlSceneLayer layer)
{
    if (!mgr || layer < 0 || layer >= DWL_LAYER_COUNT)
        return NULL;

    return mgr->layers[layer];
}

typedef struct {
    struct wlr_scene_tree *tree;
    struct wlr_scene_tree *surface_tree;
    struct wlr_scene_rect *border;  // Single rect with clipped interior for hollow border
    struct wlr_scene_shadow *shadow;
    int border_width;
    int corner_radius;
    float opacity;
} ClientSceneData;

extern ClientSceneData *dwl_client_get_scene_data(DwlClient *client);
extern void dwl_client_set_scene_data(DwlClient *client, ClientSceneData *data);
extern struct wlr_xdg_toplevel *dwl_client_get_xdg_toplevel(DwlClient *client);
#ifdef DWL_XWAYLAND
extern struct wlr_xwayland_surface *dwl_client_get_xwayland_surface(DwlClient *client);
#endif

static void set_corner_radius_recursive(struct wlr_scene_node *node, int radius,
                                         enum corner_location corners);

DwlError dwl_scene_client_create(DwlSceneManager *mgr, DwlClient *client)
{
    if (!mgr || !client)
        return DWL_ERR_INVALID_ARG;

    ClientSceneData *data = calloc(1, sizeof(*data));
    if (!data)
        return DWL_ERR_NOMEM;

    DwlClientInfo info = dwl_client_get_info(client);
    DwlSceneLayer layer = info.floating ? DWL_LAYER_FLOAT : DWL_LAYER_TILES;

    data->tree = wlr_scene_tree_create(mgr->layers[layer]);
    if (!data->tree) {
        free(data);
        return DWL_ERR_NOMEM;
    }

    struct wlr_xdg_toplevel *toplevel = dwl_client_get_xdg_toplevel(client);
    if (toplevel && toplevel->base->initialized) {
        data->surface_tree = wlr_scene_xdg_surface_create(data->tree, toplevel->base);
        toplevel->base->data = data->tree;
    }
#ifdef DWL_XWAYLAND
    else {
        struct wlr_xwayland_surface *xsurface = dwl_client_get_xwayland_surface(client);
        if (xsurface && xsurface->surface) {
            data->surface_tree = wlr_scene_subsurface_tree_create(data->tree, xsurface->surface);
            xsurface->surface->data = data->tree;
        }
    }
#endif

    data->tree->node.data = client;

    DwlRenderer *renderer = dwl_compositor_get_renderer(mgr->comp);
    DwlRenderConfig cfg = dwl_renderer_get_config(renderer);

    data->border_width = cfg.border_width;
    data->corner_radius = cfg.corner_radius;
    data->opacity = 1.0f;
    float *color = cfg.border_color_unfocused;

    // Create shadow if enabled (will be lowered to bottom)
    if (cfg.shadow_enabled) {
        data->shadow = wlr_scene_shadow_create(data->tree, 100, 100,
            cfg.corner_radius, (float)cfg.shadow_radius, cfg.shadow_color);
        if (data->shadow) {
            wlr_scene_node_lower_to_bottom(&data->shadow->node);
        }
    }

    // Create border rect with hollow interior (in front of surface)
    data->border = wlr_scene_rect_create(data->tree, 0, 0, color);
    if (data->border) {
        if (cfg.corner_radius > 0)
            wlr_scene_rect_set_corner_radius(data->border, cfg.corner_radius, CORNER_LOCATION_ALL);
    }

    // Apply inner corner radius to surface buffers (radius - border_width for proper alignment)
    if (data->surface_tree && cfg.corner_radius > 0) {
        int inner_radius = cfg.corner_radius > cfg.border_width ? cfg.corner_radius - cfg.border_width : 0;
        set_corner_radius_recursive(&data->surface_tree->node, inner_radius, CORNER_LOCATION_ALL);
    }

    dwl_client_set_scene_data(client, data);

    return DWL_OK;
}

void dwl_scene_client_destroy(DwlSceneManager *mgr, DwlClient *client)
{
    (void)mgr;
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data)
        return;

    if (data->tree)
        wlr_scene_node_destroy(&data->tree->node);

    free(data);
    dwl_client_set_scene_data(client, NULL);
}

void dwl_scene_client_set_position(DwlClient *client, int x, int y)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data || !data->tree)
        return;

    wlr_scene_node_set_position(&data->tree->node, x, y);
}

void dwl_scene_client_set_size(DwlClient *client, int width, int height)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data)
        return;

    int bw = data->border_width;

    if (data->surface_tree)
        wlr_scene_node_set_position(&data->surface_tree->node, bw, bw);

    // Update shadow size
    if (data->shadow) {
        wlr_scene_shadow_set_size(data->shadow, width + 2 * bw, height + 2 * bw);
        wlr_scene_shadow_set_corner_radius(data->shadow, data->corner_radius);
    }

    // Update border rect - hollow frame in front of surface
    int inner_radius = data->corner_radius > bw ? data->corner_radius - bw : 0;
    if (data->border) {
        wlr_scene_rect_set_size(data->border, width + 2 * bw, height + 2 * bw);
        wlr_scene_node_set_position(&data->border->node, 0, 0);

        // Clip out interior to create hollow border frame
        struct clipped_region clip = {
            .area = { bw, bw, width, height },
            .corner_radius = inner_radius,
            .corners = CORNER_LOCATION_ALL,
        };
        wlr_scene_rect_set_clipped_region(data->border, clip);
    }

    // Clip surface like dwl_mac's client_get_clip: (geo.x, geo.y, geom - bw)
    // Since our width/height is content size, clip = (0, 0, width + bw, height + bw)
    if (data->surface_tree) {
        struct wlr_box surface_clip = {
            .x = 0,
            .y = 0,
            .width = width + bw,
            .height = height + bw,
        };
        wlr_scene_subsurface_tree_set_clip(&data->surface_tree->node, &surface_clip);

        // Update corner radius for properly clipped surface
        set_corner_radius_recursive(&data->surface_tree->node, inner_radius, CORNER_LOCATION_ALL);
    }

    struct wlr_xdg_toplevel *toplevel = dwl_client_get_xdg_toplevel(client);
    if (toplevel && toplevel->base->initialized)
        wlr_xdg_toplevel_set_size(toplevel, width, height);
}

void dwl_scene_update_client_size(DwlClient *client, int width, int height)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data)
        return;

    int bw = data->border_width;
    int inner_radius = data->corner_radius > bw ? data->corner_radius - bw : 0;

    if (data->surface_tree)
        wlr_scene_node_set_position(&data->surface_tree->node, bw, bw);

    // Update shadow size
    if (data->shadow) {
        wlr_scene_shadow_set_size(data->shadow, width + 2 * bw, height + 2 * bw);
        wlr_scene_shadow_set_corner_radius(data->shadow, data->corner_radius);
    }

    // Update border rect - full window size with clipped interior
    if (data->border) {
        wlr_scene_rect_set_size(data->border, width + 2 * bw, height + 2 * bw);
        wlr_scene_node_set_position(&data->border->node, 0, 0);

        struct clipped_region clip = {
            .area = { bw, bw, width, height },
            .corner_radius = inner_radius,
            .corners = CORNER_LOCATION_ALL,
        };
        wlr_scene_rect_set_clipped_region(data->border, clip);
    }

    // Update surface buffer corner radius
    if (data->surface_tree) {
        set_corner_radius_recursive(&data->surface_tree->node, inner_radius, CORNER_LOCATION_ALL);
    }

    // Note: Does NOT send configure to client - used for client-initiated resizes
}

void dwl_scene_client_apply_geometry(DwlClient *client)
{
    // Like dwl_mac: on every commit, apply clip and send configure.
    // Do NOT resize borders - they stay at assigned size.
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data)
        return;

    struct wlr_xdg_toplevel *toplevel = dwl_client_get_xdg_toplevel(client);
    if (!toplevel || !toplevel->base->initialized)
        return;

    DwlClientInfo info = dwl_client_get_info(client);
    int w = info.geometry.width;
    int h = info.geometry.height;
    int bw = data->border_width;

    // Apply surface clip (matches dwl_mac's client_get_clip)
    if (data->surface_tree) {
        struct wlr_box geo = toplevel->base->geometry;
        struct wlr_box surface_clip = {
            .x = geo.x,
            .y = geo.y,
            .width = w + bw,
            .height = h + bw,
        };
        wlr_scene_subsurface_tree_set_clip(&data->surface_tree->node, &surface_clip);
    }

    // Send configure with assigned size
    wlr_xdg_toplevel_set_size(toplevel, w, h);
}

void dwl_scene_client_set_visible(DwlClient *client, bool visible)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data || !data->tree)
        return;

    wlr_scene_node_set_enabled(&data->tree->node, visible);
}

void dwl_scene_client_set_layer(DwlSceneManager *mgr, DwlClient *client, DwlSceneLayer layer)
{
    if (!mgr || !client || layer < 0 || layer >= DWL_LAYER_COUNT)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data || !data->tree)
        return;

    wlr_scene_node_reparent(&data->tree->node, mgr->layers[layer]);
}

void dwl_scene_client_raise(DwlClient *client)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data || !data->tree)
        return;

    wlr_scene_node_raise_to_top(&data->tree->node);
}

void dwl_scene_client_set_activated(DwlClient *client, bool activated)
{
    if (!client)
        return;

    struct wlr_xdg_toplevel *toplevel = dwl_client_get_xdg_toplevel(client);
    if (toplevel && toplevel->base->initialized)
        wlr_xdg_toplevel_set_activated(toplevel, activated);
}

void dwl_scene_update_borders(DwlClient *client, int width, const float color[4])
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data)
        return;

    data->border_width = width;

    if (data->border)
        wlr_scene_rect_set_color(data->border, color);
}

void dwl_scene_client_set_clip(DwlClient *client, int clip_x, int clip_y, int clip_w, int clip_h)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data || !data->surface_tree)
        return;

    int bw = data->border_width;
    DwlClientInfo info = dwl_client_get_info(client);
    int total_w = info.geometry.width + 2 * bw;
    int total_h = info.geometry.height + 2 * bw;

    // Determine which edges are clipped and which corners should remain rounded
    // Corners at monitor edges should be square (not rounded)
    enum corner_location corners = CORNER_LOCATION_ALL;
    if (clip_x > 0)  // Left edge clipped
        corners &= ~CORNER_LOCATION_LEFT;
    if (clip_y > 0)  // Top edge clipped
        corners &= ~CORNER_LOCATION_TOP;
    if (clip_x + clip_w < total_w)  // Right edge clipped
        corners &= ~CORNER_LOCATION_RIGHT;
    if (clip_y + clip_h < total_h)  // Bottom edge clipped
        corners &= ~CORNER_LOCATION_BOTTOM;

    // Hide shadow when clipping (shadow extends beyond window bounds)
    if (data->shadow)
        wlr_scene_node_set_enabled(&data->shadow->node, false);

    // Clip the surface (clip box is in surface-local coords)
    struct wlr_box surface_clip = {
        .x = clip_x - bw,
        .y = clip_y - bw,
        .width = clip_w,
        .height = clip_h,
    };
    wlr_scene_subsurface_tree_set_clip(&data->surface_tree->node, &surface_clip);

    // Update surface buffer corner radius to match clipped edges
    // Use inner radius (outer - border_width) for proper curve alignment
    int inner_radius = data->corner_radius > bw ? data->corner_radius - bw : 0;
    set_corner_radius_recursive(&data->surface_tree->node, inner_radius, corners);

    // Update border rect to visible region
    if (data->border) {
        wlr_scene_rect_set_size(data->border, clip_w, clip_h);
        wlr_scene_node_set_position(&data->border->node, clip_x, clip_y);
        wlr_scene_rect_set_corner_radius(data->border, data->corner_radius, corners);

        // Calculate the interior clip region relative to the border rect position
        // The interior (content area) must be clamped to the visible portion
        int content_w = info.geometry.width;
        int content_h = info.geometry.height;

        // Interior bounds in border-rect-local coordinates
        int inner_left = (clip_x < bw) ? (bw - clip_x) : 0;
        int inner_top = (clip_y < bw) ? (bw - clip_y) : 0;

        // Right edge of content in border-rect-local coords: (bw + content_w - clip_x)
        // But clamp to border rect width (clip_w)
        int inner_right = bw + content_w - clip_x;
        if (inner_right > clip_w) inner_right = clip_w;

        // Bottom edge of content in border-rect-local coords
        int inner_bottom = bw + content_h - clip_y;
        if (inner_bottom > clip_h) inner_bottom = clip_h;

        int inner_w = inner_right - inner_left;
        int inner_h = inner_bottom - inner_top;

        if (inner_w > 0 && inner_h > 0) {
            struct clipped_region interior_clip = {
                .area = { inner_left, inner_top, inner_w, inner_h },
                .corner_radius = inner_radius,
                .corners = corners,
            };
            wlr_scene_rect_set_clipped_region(data->border, interior_clip);
        }
    }
}

void dwl_scene_client_clear_clip(DwlClient *client)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data || !data->surface_tree)
        return;

    int bw = data->border_width;
    int inner_radius = data->corner_radius > bw ? data->corner_radius - bw : 0;

    // Restore surface buffer corner radius (uses inner radius)
    set_corner_radius_recursive(&data->surface_tree->node, inner_radius, CORNER_LOCATION_ALL);

    // Re-enable shadow
    if (data->shadow)
        wlr_scene_node_set_enabled(&data->shadow->node, true);

    // Restore border to full size
    DwlClientInfo info = dwl_client_get_info(client);
    int w = info.geometry.width;
    int h = info.geometry.height;

    // Clip surface like dwl_mac's client_get_clip
    struct wlr_box surface_clip = {
        .x = 0,
        .y = 0,
        .width = w + bw,
        .height = h + bw,
    };
    wlr_scene_subsurface_tree_set_clip(&data->surface_tree->node, &surface_clip);

    if (data->border) {
        wlr_scene_node_set_enabled(&data->border->node, true);
        wlr_scene_rect_set_size(data->border, w + 2 * bw, h + 2 * bw);
        wlr_scene_node_set_position(&data->border->node, 0, 0);
        // Outer border uses full radius
        wlr_scene_rect_set_corner_radius(data->border, data->corner_radius, CORNER_LOCATION_ALL);

        // Inner clipped region uses inner radius
        struct clipped_region clip = {
            .area = { bw, bw, w, h },
            .corner_radius = inner_radius,
            .corners = CORNER_LOCATION_ALL,
        };
        wlr_scene_rect_set_clipped_region(data->border, clip);
    }
}

void dwl_scene_client_set_shadow(DwlClient *client, bool enabled, int blur_sigma, const float color[4])
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data)
        return;

    if (!enabled) {
        if (data->shadow)
            wlr_scene_node_set_enabled(&data->shadow->node, false);
        return;
    }

    if (data->shadow) {
        wlr_scene_node_set_enabled(&data->shadow->node, true);
        wlr_scene_shadow_set_blur_sigma(data->shadow, (float)blur_sigma);
        wlr_scene_shadow_set_color(data->shadow, color);
    }
}

// Recursively set corner radius on all buffer nodes in a tree
static void set_corner_radius_recursive(struct wlr_scene_node *node, int radius,
                                         enum corner_location corners)
{
    if (!node)
        return;

    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
        wlr_scene_buffer_set_corner_radius(buf, radius, corners);
    } else if (node->type == WLR_SCENE_NODE_TREE) {
        struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
        struct wlr_scene_node *child;
        wl_list_for_each(child, &tree->children, link) {
            set_corner_radius_recursive(child, radius, corners);
        }
    }
}

void dwl_scene_client_set_corner_radius(DwlClient *client, int radius)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data)
        return;

    data->corner_radius = radius;
    int bw = data->border_width;
    int inner_radius = radius > bw ? radius - bw : 0;

    // Update shadow corner radius (uses outer radius)
    if (data->shadow)
        wlr_scene_shadow_set_corner_radius(data->shadow, radius);

    // Update border corner radius and clipped region
    if (data->border) {
        // Outer border uses full radius
        wlr_scene_rect_set_corner_radius(data->border, radius, CORNER_LOCATION_ALL);

        // Inner clipped region uses inner radius for proper curve alignment
        DwlClientInfo info = dwl_client_get_info(client);
        struct clipped_region clip = {
            .area = { bw, bw, info.geometry.width, info.geometry.height },
            .corner_radius = inner_radius,
            .corners = CORNER_LOCATION_ALL,
        };
        wlr_scene_rect_set_clipped_region(data->border, clip);
    }

    // Recursively update surface buffer corner radius (uses inner radius)
    if (data->surface_tree) {
        set_corner_radius_recursive(&data->surface_tree->node, inner_radius, CORNER_LOCATION_ALL);
    }
}

void dwl_scene_client_set_opacity(DwlClient *client, float opacity)
{
    if (!client)
        return;

    ClientSceneData *data = dwl_client_get_scene_data(client);
    if (!data)
        return;

    if (data->opacity == opacity)
        return;

    data->opacity = opacity;

    // Update surface buffer opacity
    if (data->surface_tree) {
        struct wlr_scene_node *node;
        wl_list_for_each(node, &data->surface_tree->children, link) {
            if (node->type == WLR_SCENE_NODE_BUFFER) {
                struct wlr_scene_buffer *buf = wlr_scene_buffer_from_node(node);
                wlr_scene_buffer_set_opacity(buf, opacity);
            }
        }
    }
}
