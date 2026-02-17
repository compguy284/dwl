#ifndef DWL_XDG_SHELL_PROTO_H
#define DWL_XDG_SHELL_PROTO_H

typedef struct DwlCompositor DwlCompositor;
struct wlr_xdg_toplevel;
struct wlr_xdg_popup;

/* Called from compositor.c when new xdg surfaces are created */
void dwl_xdg_shell_handle_new_toplevel(DwlCompositor *comp, struct wlr_xdg_toplevel *toplevel);
void dwl_xdg_shell_handle_new_popup(struct wlr_xdg_popup *popup);

#endif /* DWL_XDG_SHELL_PROTO_H */
