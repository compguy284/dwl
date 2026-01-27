/*
 * visual.h - Visual effects (blur, shadow, corner radius, opacity)
 * See LICENSE file for copyright and license details.
 */
#ifndef VISUAL_H
#define VISUAL_H

#include "dwl.h"

/* Scene buffer iteration callbacks */
void iter_xdg_scene_buffers(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);
void iter_xdg_scene_buffers_blur(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);
void iter_xdg_scene_buffers_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);
void iter_xdg_scene_buffers_corner_radius(struct wlr_scene_buffer *buffer, int sx, int sy, void *user_data);

/* Scene tree utilities */
void scene_tree_apply_clip(struct wlr_scene_node *node, const struct wlr_box *clip);
void output_configure_scene(struct wlr_scene_node *node, Client *c);

/* Shadow utilities */
int in_shadow_ignore_list(const char *str);
void client_set_shadow_blur_sigma(Client *c, int blur_sigma);

/* Client visual update functions */
void update_client_corner_radius(Client *c);
void update_client_blur(Client *c);
void update_buffer_corner_radius(Client *c, struct wlr_scene_buffer *buffer);
void update_client_shadow_color(Client *c);
void update_client_focus_decorations(Client *c, int focused, int urgent);

#endif /* VISUAL_H */
