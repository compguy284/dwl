#ifndef SWL_COMPOSITOR_H
#define SWL_COMPOSITOR_H

#include <stdbool.h>
#include "error.h"
#include "events.h"

typedef struct SwlCompositor SwlCompositor;

typedef struct SwlCompositorConfig {
    const char *config_path;
    bool enable_xwayland;
    const char *startup_cmd;
    int log_level;
} SwlCompositorConfig;

SwlError swl_compositor_create(SwlCompositor **out, const SwlCompositorConfig *cfg);
void swl_compositor_destroy(SwlCompositor *comp);
SwlError swl_compositor_run(SwlCompositor *comp);
void swl_compositor_quit(SwlCompositor *comp);

SwlEventBus *swl_compositor_get_event_bus(SwlCompositor *comp);

struct SwlInput *swl_compositor_get_input(SwlCompositor *comp);
struct SwlOutputManager *swl_compositor_get_output(SwlCompositor *comp);
struct SwlClientManager *swl_compositor_get_clients(SwlCompositor *comp);
struct SwlConfig *swl_compositor_get_config(SwlCompositor *comp);
struct SwlRenderer *swl_compositor_get_renderer(SwlCompositor *comp);
struct SwlIPC *swl_compositor_get_ipc(SwlCompositor *comp);
struct SwlLayoutRegistry *swl_compositor_get_layouts(SwlCompositor *comp);

struct wl_display *swl_compositor_get_wl_display(SwlCompositor *comp);
struct wlr_backend *swl_compositor_get_backend(SwlCompositor *comp);
struct wlr_session *swl_compositor_get_session(SwlCompositor *comp);
struct wlr_allocator *swl_compositor_get_allocator(SwlCompositor *comp);
struct wlr_renderer *swl_compositor_get_wlr_renderer(SwlCompositor *comp);
struct wlr_scene *swl_compositor_get_scene(SwlCompositor *comp);
struct wlr_seat *swl_compositor_get_seat(SwlCompositor *comp);
struct wlr_output_layout *swl_compositor_get_output_layout(SwlCompositor *comp);
struct wlr_idle_notifier_v1 *swl_compositor_get_idle_notifier(SwlCompositor *comp);
struct wlr_compositor *swl_compositor_get_wlr_compositor(SwlCompositor *comp);

struct SwlLayerManager *swl_compositor_get_layer_manager(SwlCompositor *comp);
struct SwlToplevelManager *swl_compositor_get_toplevel_manager(SwlCompositor *comp);

#endif /* SWL_COMPOSITOR_H */
