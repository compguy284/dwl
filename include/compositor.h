#ifndef DWL_COMPOSITOR_H
#define DWL_COMPOSITOR_H

#include <stdbool.h>
#include "error.h"
#include "events.h"

typedef struct DwlCompositor DwlCompositor;

typedef struct DwlCompositorConfig {
    const char *config_path;
    bool enable_xwayland;
    const char *startup_cmd;
    int log_level;
} DwlCompositorConfig;

DwlError dwl_compositor_create(DwlCompositor **out, const DwlCompositorConfig *cfg);
void dwl_compositor_destroy(DwlCompositor *comp);
DwlError dwl_compositor_run(DwlCompositor *comp);
void dwl_compositor_quit(DwlCompositor *comp);

DwlEventBus *dwl_compositor_get_event_bus(DwlCompositor *comp);

struct DwlInput *dwl_compositor_get_input(DwlCompositor *comp);
struct DwlOutputManager *dwl_compositor_get_output(DwlCompositor *comp);
struct DwlClientManager *dwl_compositor_get_clients(DwlCompositor *comp);
struct DwlConfig *dwl_compositor_get_config(DwlCompositor *comp);
struct DwlWorkspaceManager *dwl_compositor_get_workspaces(DwlCompositor *comp);
struct DwlRenderer *dwl_compositor_get_renderer(DwlCompositor *comp);
struct DwlIPC *dwl_compositor_get_ipc(DwlCompositor *comp);
struct DwlLayoutRegistry *dwl_compositor_get_layouts(DwlCompositor *comp);

struct wl_display *dwl_compositor_get_wl_display(DwlCompositor *comp);
struct wlr_backend *dwl_compositor_get_backend(DwlCompositor *comp);
struct wlr_allocator *dwl_compositor_get_allocator(DwlCompositor *comp);
struct wlr_renderer *dwl_compositor_get_wlr_renderer(DwlCompositor *comp);
struct wlr_scene *dwl_compositor_get_scene(DwlCompositor *comp);
struct wlr_seat *dwl_compositor_get_seat(DwlCompositor *comp);
struct wlr_output_layout *dwl_compositor_get_output_layout(DwlCompositor *comp);
struct wlr_idle_notifier_v1 *dwl_compositor_get_idle_notifier(DwlCompositor *comp);
struct wlr_compositor *dwl_compositor_get_wlr_compositor(DwlCompositor *comp);

struct DwlLayerManager *dwl_compositor_get_layer_manager(DwlCompositor *comp);
struct DwlToplevelManager *dwl_compositor_get_toplevel_manager(DwlCompositor *comp);

#endif /* DWL_COMPOSITOR_H */
