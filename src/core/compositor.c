#define _POSIX_C_SOURCE 200809L
#include "compositor.h"
#include "client.h"
#include "config.h"
#include "input.h"
#include "ipc.h"
#include "layer.h"
#include "layout.h"
#include "monitor.h"
#include "render.h"
#include "toplevel.h"
#include "xwayland.h"
#include "../protocols/decoration.h"
#include "../protocols/xdg_shell.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>

extern void swl_signal_init(void);
extern int swl_signal_should_quit(void);
extern void swl_signal_request_quit(void);

struct SwlCompositor {
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wlr_backend *backend;
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_output_layout *output_layout;
    struct wlr_seat *seat;
    struct wlr_xdg_shell *xdg_shell;
    struct wlr_compositor *wlr_compositor;
    struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
    struct wlr_xdg_activation_v1 *activation;
    struct wlr_idle_notifier_v1 *idle_notifier;

    SwlEventBus *event_bus;

    SwlClientManager *clients;
    SwlInput *input;
    SwlOutputManager *output;
    SwlConfig *config;
    SwlRenderer *swl_renderer;
    SwlIPC *ipc;
    SwlLayoutRegistry *layouts;
    SwlLayerManager *layer_mgr;
    SwlToplevelManager *toplevel_mgr;

#ifdef SWL_XWAYLAND
    SwlXWayland *xwayland;
#endif

    struct wlr_output_power_manager_v1 *output_power_mgr;

    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener new_xdg_decoration;
    struct wl_listener request_activate;
    struct wl_listener set_output_power_mode;

    char *startup_cmd;
    char *config_path;
    bool running;
};

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    SwlCompositor *comp = wl_container_of(listener, comp, new_xdg_toplevel);
    swl_xdg_shell_handle_new_toplevel(comp, data);
}

static void handle_new_xdg_popup(struct wl_listener *listener, void *data)
{
    (void)listener;
    swl_xdg_shell_handle_new_popup(data);
}

static void handle_new_xdg_decoration(struct wl_listener *listener, void *data)
{
    (void)listener;
    swl_decoration_handle_new(data);
}

static void handle_request_activate(struct wl_listener *listener, void *data)
{
    SwlCompositor *comp = wl_container_of(listener, comp, request_activate);
    struct wlr_xdg_activation_v1_request_activate_event *event = data;

    // Find the client associated with this surface
    SwlClient *client = swl_client_by_surface(comp->clients, event->surface);
    if (!client)
        return;

    // Don't mark as urgent if already focused
    SwlClient *focused = swl_client_focused(comp->clients);
    if (client == focused)
        return;

    // Mark as urgent
    swl_client_set_urgent(client, true);
}

static void handle_set_output_power_mode(struct wl_listener *listener, void *data)
{
    SwlCompositor *comp = wl_container_of(listener, comp, set_output_power_mode);
    (void)comp;
    struct wlr_output_power_v1_set_mode_event *event = data;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state,
        event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
    wlr_output_commit_state(event->output, &state);
    wlr_output_state_finish(&state);
}

SwlError swl_compositor_create(SwlCompositor **out, const SwlCompositorConfig *cfg)
{
    if (!out)
        return SWL_ERR_INVALID_ARG;

    SwlCompositor *comp = calloc(1, sizeof(*comp));
    if (!comp)
        return SWL_ERR_NOMEM;

    swl_signal_init();

    // Event bus
    comp->event_bus = swl_event_bus_create();
    if (!comp->event_bus) {
        free(comp);
        return SWL_ERR_NOMEM;
    }

    // Wayland display
    comp->display = wl_display_create();
    if (!comp->display) {
        swl_event_bus_destroy(comp->event_bus);
        free(comp);
        return SWL_ERR_WAYLAND;
    }

    comp->event_loop = wl_display_get_event_loop(comp->display);

    // Backend
    comp->backend = wlr_backend_autocreate(comp->event_loop, &comp->session);
    if (!comp->backend) {
        wl_display_destroy(comp->display);
        swl_event_bus_destroy(comp->event_bus);
        free(comp);
        return SWL_ERR_BACKEND;
    }

    // Renderer (scenefx)
    comp->renderer = fx_renderer_create(comp->backend);
    if (!comp->renderer) {
        wlr_backend_destroy(comp->backend);
        wl_display_destroy(comp->display);
        swl_event_bus_destroy(comp->event_bus);
        free(comp);
        return SWL_ERR_BACKEND;
    }

    wlr_renderer_init_wl_display(comp->renderer, comp->display);

    // Allocator
    comp->allocator = wlr_allocator_autocreate(comp->backend, comp->renderer);
    if (!comp->allocator) {
        wlr_renderer_destroy(comp->renderer);
        wlr_backend_destroy(comp->backend);
        wl_display_destroy(comp->display);
        swl_event_bus_destroy(comp->event_bus);
        free(comp);
        return SWL_ERR_BACKEND;
    }

    // Core Wayland protocols
    comp->wlr_compositor = wlr_compositor_create(comp->display, 6, comp->renderer);
    wlr_subcompositor_create(comp->display);
    wlr_data_device_manager_create(comp->display);
    wlr_primary_selection_v1_device_manager_create(comp->display);
    wlr_viewporter_create(comp->display);
    wlr_single_pixel_buffer_manager_v1_create(comp->display);
    wlr_fractional_scale_manager_v1_create(comp->display, 1);

    // Output layout and scene
    comp->output_layout = wlr_output_layout_create(comp->display);
    comp->scene = wlr_scene_create();
    wlr_scene_attach_output_layout(comp->scene, comp->output_layout);

    // Extra protocols
    wlr_xdg_output_manager_v1_create(comp->display, comp->output_layout);
    wlr_export_dmabuf_manager_v1_create(comp->display);
    wlr_screencopy_manager_v1_create(comp->display);
    wlr_gamma_control_manager_v1_create(comp->display);
    wlr_presentation_create(comp->display, comp->backend, CLOCK_MONOTONIC);
    wlr_cursor_shape_manager_v1_create(comp->display, 1);

    comp->idle_notifier = wlr_idle_notifier_v1_create(comp->display);

    // XDG shell
    comp->xdg_shell = wlr_xdg_shell_create(comp->display, 6);
    comp->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
    wl_signal_add(&comp->xdg_shell->events.new_toplevel, &comp->new_xdg_toplevel);
    comp->new_xdg_popup.notify = handle_new_xdg_popup;
    wl_signal_add(&comp->xdg_shell->events.new_popup, &comp->new_xdg_popup);

    // Server decoration (older KDE protocol - used by Firefox, etc.)
    wlr_server_decoration_manager_set_default_mode(
        wlr_server_decoration_manager_create(comp->display),
        WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);

    // XDG decoration (newer protocol)
    comp->xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(comp->display);
    comp->new_xdg_decoration.notify = handle_new_xdg_decoration;
    wl_signal_add(&comp->xdg_decoration_mgr->events.new_toplevel_decoration,
        &comp->new_xdg_decoration);

    // XDG activation (for urgency hints and focus requests)
    comp->activation = wlr_xdg_activation_v1_create(comp->display);
    comp->request_activate.notify = handle_request_activate;
    wl_signal_add(&comp->activation->events.request_activate, &comp->request_activate);

    // Output power management (DPMS via wlopm, etc.)
    comp->output_power_mgr = wlr_output_power_manager_v1_create(comp->display);
    comp->set_output_power_mode.notify = handle_set_output_power_mode;
    wl_signal_add(&comp->output_power_mgr->events.set_mode, &comp->set_output_power_mode);

    // Seat
    comp->seat = wlr_seat_create(comp->display, "seat0");

    // Configuration
    comp->config = swl_config_create();
    if (cfg && cfg->config_path) {
        comp->config_path = strdup(cfg->config_path);
        swl_config_load_file(comp->config, cfg->config_path);
    } else {
        swl_config_load_default(comp->config);
    }

    // Layout registry
    comp->layouts = swl_layout_registry_create();
    swl_layout_register_builtins(comp->layouts);

    // Client manager
    comp->clients = swl_client_manager_create(comp);

    // Output manager
    comp->output = swl_output_create(comp);

    // Input
    comp->input = swl_input_create(comp);

    // Renderer
    comp->swl_renderer = swl_renderer_create(comp);

    // Configure scenefx blur
    SwlRenderConfig rcfg = swl_renderer_get_config(comp->swl_renderer);
    wlr_scene_set_blur_data(comp->scene, rcfg.blur_passes, rcfg.blur_radius,
        0.0f, 1.0f, 1.0f, 1.0f);  // noise, brightness, contrast, saturation

    // IPC
    comp->ipc = swl_ipc_create(comp);
    swl_ipc_register_builtins(comp->ipc);

    // Layer shell
    comp->layer_mgr = swl_layer_manager_create(comp);

    // Foreign toplevel manager
    comp->toplevel_mgr = swl_toplevel_manager_create(comp);

#ifdef SWL_XWAYLAND
    if (!cfg || cfg->enable_xwayland)
        comp->xwayland = swl_xwayland_create(comp);
#endif

    if (cfg && cfg->startup_cmd)
        comp->startup_cmd = strdup(cfg->startup_cmd);

    *out = comp;
    return SWL_OK;
}

void swl_compositor_destroy(SwlCompositor *comp)
{
    if (!comp)
        return;

#ifdef SWL_XWAYLAND
    swl_xwayland_destroy(comp->xwayland);
#endif

    swl_toplevel_manager_destroy(comp->toplevel_mgr);
    swl_layer_manager_destroy(comp->layer_mgr);
    swl_ipc_destroy(comp->ipc);
    swl_renderer_destroy(comp->swl_renderer);
    swl_input_destroy(comp->input);
    swl_output_destroy(comp->output);
    swl_client_manager_destroy(comp->clients);
    swl_layout_registry_destroy(comp->layouts);
    swl_config_destroy(comp->config);

    free(comp->startup_cmd);
    free(comp->config_path);

    wl_list_remove(&comp->new_xdg_toplevel.link);
    wl_list_remove(&comp->new_xdg_popup.link);
    wl_list_remove(&comp->new_xdg_decoration.link);
    wl_list_remove(&comp->request_activate.link);
    wl_list_remove(&comp->set_output_power_mode.link);

    wlr_scene_node_destroy(&comp->scene->tree.node);
    wlr_allocator_destroy(comp->allocator);
    wlr_renderer_destroy(comp->renderer);
    wlr_backend_destroy(comp->backend);
    wl_display_destroy(comp->display);
    swl_event_bus_destroy(comp->event_bus);
    free(comp);
}

SwlError swl_compositor_run(SwlCompositor *comp)
{
    if (!comp)
        return SWL_ERR_INVALID_ARG;

    const char *socket = wl_display_add_socket_auto(comp->display);
    if (!socket)
        return SWL_ERR_WAYLAND;

    if (!wlr_backend_start(comp->backend))
        return SWL_ERR_BACKEND;

    setenv("WAYLAND_DISPLAY", socket, 1);
    setenv("XDG_CURRENT_DESKTOP", "wlroots", 1);

#ifdef SWL_XWAYLAND
    if (comp->xwayland) {
        const char *xdisplay = swl_xwayland_get_display(comp->xwayland);
        if (xdisplay)
            setenv("DISPLAY", xdisplay, 1);
    }
#endif

    // Import environment variables into D-Bus and systemd user session
    // This enables portals, notifications, and other D-Bus services
    if (fork() == 0) {
#ifdef SWL_XWAYLAND
        execlp("dbus-update-activation-environment", "dbus-update-activation-environment",
            "--systemd", "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", "DISPLAY", NULL);
#else
        execlp("dbus-update-activation-environment", "dbus-update-activation-environment",
            "--systemd", "WAYLAND_DISPLAY", "XDG_CURRENT_DESKTOP", NULL);
#endif
        _exit(1);
    }

    if (comp->startup_cmd) {
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "/bin/sh", "-c", comp->startup_cmd, NULL);
            _exit(1);
        }
    }

    comp->running = true;
    wl_display_run(comp->display);

    return SWL_OK;
}

void swl_compositor_quit(SwlCompositor *comp)
{
    if (!comp)
        return;

    comp->running = false;
    swl_signal_request_quit();
    wl_display_terminate(comp->display);
}

SwlEventBus *swl_compositor_get_event_bus(SwlCompositor *comp)
{
    return comp ? comp->event_bus : NULL;
}

SwlInput *swl_compositor_get_input(SwlCompositor *comp)
{
    return comp ? comp->input : NULL;
}

SwlOutputManager *swl_compositor_get_output(SwlCompositor *comp)
{
    return comp ? comp->output : NULL;
}

SwlClientManager *swl_compositor_get_clients(SwlCompositor *comp)
{
    return comp ? comp->clients : NULL;
}

SwlConfig *swl_compositor_get_config(SwlCompositor *comp)
{
    return comp ? comp->config : NULL;
}

SwlRenderer *swl_compositor_get_renderer(SwlCompositor *comp)
{
    return comp ? comp->swl_renderer : NULL;
}

SwlIPC *swl_compositor_get_ipc(SwlCompositor *comp)
{
    return comp ? comp->ipc : NULL;
}

SwlLayoutRegistry *swl_compositor_get_layouts(SwlCompositor *comp)
{
    return comp ? comp->layouts : NULL;
}

struct wl_display *swl_compositor_get_wl_display(SwlCompositor *comp)
{
    return comp ? comp->display : NULL;
}

struct wlr_backend *swl_compositor_get_backend(SwlCompositor *comp)
{
    return comp ? comp->backend : NULL;
}

struct wlr_session *swl_compositor_get_session(SwlCompositor *comp)
{
    return comp ? comp->session : NULL;
}

struct wlr_allocator *swl_compositor_get_allocator(SwlCompositor *comp)
{
    return comp ? comp->allocator : NULL;
}

struct wlr_renderer *swl_compositor_get_wlr_renderer(SwlCompositor *comp)
{
    return comp ? comp->renderer : NULL;
}

struct wlr_scene *swl_compositor_get_scene(SwlCompositor *comp)
{
    return comp ? comp->scene : NULL;
}

struct wlr_seat *swl_compositor_get_seat(SwlCompositor *comp)
{
    return comp ? comp->seat : NULL;
}

struct wlr_output_layout *swl_compositor_get_output_layout(SwlCompositor *comp)
{
    return comp ? comp->output_layout : NULL;
}

struct wlr_idle_notifier_v1 *swl_compositor_get_idle_notifier(SwlCompositor *comp)
{
    return comp ? comp->idle_notifier : NULL;
}

struct wlr_compositor *swl_compositor_get_wlr_compositor(SwlCompositor *comp)
{
    return comp ? comp->wlr_compositor : NULL;
}

SwlLayerManager *swl_compositor_get_layer_manager(SwlCompositor *comp)
{
    return comp ? comp->layer_mgr : NULL;
}

SwlToplevelManager *swl_compositor_get_toplevel_manager(SwlCompositor *comp)
{
    return comp ? comp->toplevel_mgr : NULL;
}
