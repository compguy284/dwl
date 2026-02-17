#ifndef DWL_DECORATION_H
#define DWL_DECORATION_H

struct wl_listener;
struct wlr_xdg_toplevel_decoration_v1;

/* Called from compositor.c when a new xdg decoration is created */
void dwl_decoration_handle_new(struct wlr_xdg_toplevel_decoration_v1 *decoration);

#endif /* DWL_DECORATION_H */
