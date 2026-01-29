/*
 * visual.c - Visual effects (blur, shadow, corner radius, opacity)
 * See LICENSE file for copyright and license details.
 */
#include "visual.h"
#include "client.h"

/* Forward declaration for function still in dwl.c */
Client *focustop(Monitor *m);

void
iter_xdg_scene_buffers(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data)
{
	Client *c = user_data;
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	struct wlr_xdg_surface *xdg_surface;

	if (!scene_surface) {
		return;
	}

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);

	if (c && xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		if (cfg.opacity) {
			wlr_scene_buffer_set_opacity(buffer, c->opacity);
		}

		if (!wlr_subsurface_try_from_wlr_surface(xdg_surface->surface)) {
			update_buffer_corner_radius(c, buffer);
		}
	}
}

void
iter_xdg_scene_buffers_blur(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data)
{
	/* Blur is handled via scene blur nodes in scenefx 0.4, not per-buffer */
	(void)buffer;
	(void)sx;
	(void)sy;
	(void)user_data;
}

void
iter_xdg_scene_buffers_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data)
{
	Client *c = user_data;
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	struct wlr_xdg_surface *xdg_surface;

	if (!scene_surface) {
		return;
	}

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);

	if (c && xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		if (cfg.opacity) {
			wlr_scene_buffer_set_opacity(buffer, c->opacity);
		}
	}
}

void
iter_xdg_scene_buffers_corner_radius(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data)
{
	Client *c = user_data;
	struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
	struct wlr_xdg_surface *xdg_surface;

	if (!scene_surface) {
		return;
	}

	xdg_surface = wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);

	if (c && xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		update_buffer_corner_radius(c, buffer);
	}
}

/*
 * Apply a clip region to a scene tree using scenefx's built-in function.
 */
void
scene_tree_apply_clip(struct wlr_scene_node *node, const struct wlr_box *clip)
{
	wlr_scene_subsurface_tree_set_clip(node, clip);
}

void
output_configure_scene(struct wlr_scene_node *node, Client *c)
{
	Client *_c;
	struct wlr_xdg_surface *xdg_surface;
	struct wlr_scene_node *_node;

	if (!node->enabled) {
		return;
	}

	_c = node->data;
	if (_c) {
		c = _c;
	}

	if (node->type == WLR_SCENE_NODE_BUFFER) {
		struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);

		struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(buffer);
		if (!scene_surface) {
			return;
		}

		xdg_surface = wlr_xdg_surface_try_from_wlr_surface(scene_surface->surface);

		if (c && xdg_surface && xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
			if (cfg.opacity) {
				wlr_scene_buffer_set_opacity(buffer, c->opacity);
			}

			if (!wlr_subsurface_try_from_wlr_surface(xdg_surface->surface)) {
				update_buffer_corner_radius(c, buffer);
			}
		}
	} else if (node->type == WLR_SCENE_NODE_TREE) {
		struct wlr_scene_tree *tree = wl_container_of(node, tree, node);
		wl_list_for_each(_node, &tree->children, link) {
			output_configure_scene(_node, c);
		}
	}
}

int
in_shadow_ignore_list(const char *str)
{
	int i;
	for (i = 0; cfg.shadow_ignore_list[i] != NULL; i++) {
		if (strcmp(cfg.shadow_ignore_list[i], str) == 0) {
			return 1;
		}
	}
	return 0;
}

void
client_set_shadow_blur_sigma(Client *c, int blur_sigma)
{
	wlr_scene_shadow_set_blur_sigma(c->shadow, blur_sigma);
	wlr_scene_node_set_position(&c->shadow->node, -blur_sigma, -blur_sigma);
	wlr_scene_shadow_set_size(c->shadow, c->geom.width + blur_sigma * 2, c->geom.height + blur_sigma * 2);
	wlr_scene_shadow_set_clipped_region(c->shadow, (struct clipped_region) {
		.corners = corner_radii_all(c->corner_radius + c->bw),
		.area = { .x = blur_sigma, .y = blur_sigma, .width = c->geom.width, .height = c->geom.height }
	});
}

void
update_client_corner_radius(Client *c)
{
	if (cfg.corner_radius && c->round_border) {
		int radius = c->corner_radius + c->bw;
		if ((cfg.corner_radius_only_floating && !c->isfloating) || c->isfullscreen) {
			radius = 0;
		}
		wlr_scene_rect_set_corner_radius(c->round_border, radius);
	}

#ifdef XWAYLAND
	if (!client_is_x11(c)) {
#endif
	if (cfg.corner_radius_inner > 0 && c->scene) {
		wlr_scene_node_for_each_buffer(&c->scene_surface->node, iter_xdg_scene_buffers_corner_radius, c);
	}
#ifdef XWAYLAND
	}
#endif
}

void
update_client_blur(Client *c)
{
	if (!cfg.blur) {
		return;
	}

	if (c->scene) {
		wlr_scene_node_for_each_buffer(&c->scene_surface->node, iter_xdg_scene_buffers_blur, c);
	}
}

void
update_buffer_corner_radius(Client *c, struct wlr_scene_buffer *buffer)
{
	int radius;

#ifdef XWAYLAND
	if (client_is_x11(c)) {
		return;
	}
#endif

	if (!cfg.corner_radius_inner) {
		return;
	}

	radius = cfg.corner_radius_inner;
	if ((cfg.corner_radius_only_floating && !c->isfloating) || c->isfullscreen) {
		radius = 0;
	}

	wlr_scene_buffer_set_corner_radius(buffer, radius);
}

void
update_client_shadow_color(Client *c)
{
	int has_shadow_enabled = 1;
	const float *color;

	if (!cfg.shadow || !c->shadow) {
		return;
	}

	color = focustop(c->mon) == c ? cfg.shadow_color_focus : cfg.shadow_color;

	if ((cfg.shadow_only_floating && !c->isfloating) ||
		in_shadow_ignore_list(client_get_appid(c)) ||
		c->isfullscreen) {
		color = transparent;
		has_shadow_enabled = 0;
	}

	wlr_scene_shadow_set_color(c->shadow, color);
	c->has_shadow_enabled = has_shadow_enabled;
}

void
update_client_focus_decorations(Client *c, int focused, int urgent)
{
	if (cfg.corner_radius > 0 && c->round_border) {
		wlr_scene_rect_set_color(c->round_border, urgent ? cfg.urgentcolor : (focused ? cfg.focuscolor : cfg.bordercolor));
	}
	if (cfg.shadow && c->shadow) {
		client_set_shadow_blur_sigma(c, (int)round(focused ? cfg.shadow_blur_sigma_focus : cfg.shadow_blur_sigma));
		if (c->has_shadow_enabled) {
			wlr_scene_shadow_set_color(c->shadow, focused ? cfg.shadow_color_focus : cfg.shadow_color);
		}
	}
	if (cfg.opacity) {
		c->opacity = focused ? cfg.opacity_active : cfg.opacity_inactive;
		wlr_scene_node_for_each_buffer(&c->scene_surface->node, iter_xdg_scene_buffers_opacity, c);
	}
}
