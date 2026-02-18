#ifndef SWL_XDG_SHELL_PROTO_H
#define SWL_XDG_SHELL_PROTO_H

typedef struct SwlCompositor SwlCompositor;
struct wlr_xdg_toplevel;
struct wlr_xdg_popup;

/* Called from compositor.c when new xdg surfaces are created */
void swl_xdg_shell_handle_new_toplevel(SwlCompositor *comp, struct wlr_xdg_toplevel *toplevel);
void swl_xdg_shell_handle_new_popup(SwlCompositor *comp, struct wlr_xdg_popup *popup);

#endif /* SWL_XDG_SHELL_PROTO_H */
