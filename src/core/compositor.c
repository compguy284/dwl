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
#include "workspace.h"
#include "xwayland.h"
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
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>

extern void dwl_signal_init(void);
extern int dwl_signal_should_quit(void);
extern void dwl_signal_request_quit(void);

extern DwlClient *dwl_client_create_xdg(DwlClientManager *mgr, struct wlr_xdg_toplevel *toplevel);

struct DwlCompositor {
    struct wl_display *display;
    struct wl_event_loop *event_loop;
    struct wlr_backend *backend;
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

    DwlEventBus *event_bus;

    DwlClientManager *clients;
    DwlInput *input;
    DwlOutputManager *output;
    DwlConfig *config;
    DwlWorkspaceManager *workspaces;
    DwlRenderer *dwl_renderer;
    DwlIPC *ipc;
    DwlLayoutRegistry *layouts;
    DwlLayerManager *layer_mgr;
    DwlToplevelManager *toplevel_mgr;

#ifdef DWL_XWAYLAND
    DwlXWayland *xwayland;
#endif

    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener new_xdg_decoration;
    struct wl_listener request_activate;

    char *startup_cmd;
    char *config_path;
    bool running;
};

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data)
{
    DwlCompositor *comp = wl_container_of(listener, comp, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;

    DwlClient *client = dwl_client_create_xdg(comp->clients, toplevel);
    if (!client)
        return;

    DwlMonitor *mon = dwl_monitor_get_focused(comp->output);
    if (mon)
        dwl_client_move_to_monitor(client, mon);
}

static void handle_new_xdg_popup(struct wl_listener *listener, void *data)
{
    DwlCompositor *comp = wl_container_of(listener, comp, new_xdg_popup);
    struct wlr_xdg_popup *popup = data;
    (void)comp;

    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (!parent)
        return;

    struct wlr_scene_tree *parent_tree = parent->data;
    if (parent_tree)
        popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);
}

static void decoration_handle_request_mode(struct wl_listener *listener, void *data);

typedef struct {
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wl_listener request_mode;
    struct wl_listener destroy;
    struct wl_listener surface_commit;
    bool mode_pending;
} DecorationState;

static void decoration_handle_surface_commit(struct wl_listener *listener, void *data)
{
    DecorationState *state = wl_container_of(listener, state, surface_commit);
    (void)data;

    if (state->mode_pending && state->decoration->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(state->decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        state->mode_pending = false;
        wl_list_remove(&state->surface_commit.link);
    }
}

static void decoration_handle_destroy(struct wl_listener *listener, void *data)
{
    DecorationState *state = wl_container_of(listener, state, destroy);
    (void)data;
    wl_list_remove(&state->request_mode.link);
    wl_list_remove(&state->destroy.link);
    if (state->mode_pending)
        wl_list_remove(&state->surface_commit.link);
    free(state);
}

static void decoration_handle_request_mode(struct wl_listener *listener, void *data)
{
    DecorationState *state = wl_container_of(listener, state, request_mode);
    (void)data;

    if (state->decoration->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(state->decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        // Defer until surface is initialized
        state->mode_pending = true;
        state->surface_commit.notify = decoration_handle_surface_commit;
        wl_signal_add(&state->decoration->toplevel->base->surface->events.commit,
            &state->surface_commit);
    }
}

static void handle_new_xdg_decoration(struct wl_listener *listener, void *data)
{
    DwlCompositor *comp = wl_container_of(listener, comp, new_xdg_decoration);
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
    (void)comp;

    DecorationState *state = calloc(1, sizeof(*state));
    if (!state)
        return;

    state->decoration = decoration;

    state->request_mode.notify = decoration_handle_request_mode;
    wl_signal_add(&decoration->events.request_mode, &state->request_mode);

    state->destroy.notify = decoration_handle_destroy;
    wl_signal_add(&decoration->events.destroy, &state->destroy);

    // If already initialized, set mode immediately
    if (decoration->toplevel->base->initialized) {
        wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

static void handle_request_activate(struct wl_listener *listener, void *data)
{
    DwlCompositor *comp = wl_container_of(listener, comp, request_activate);
    struct wlr_xdg_activation_v1_request_activate_event *event = data;

    // Find the client associated with this surface
    DwlClient *client = dwl_client_by_surface(comp->clients, event->surface);
    if (!client)
        return;

    // Don't mark as urgent if already focused
    DwlClient *focused = dwl_client_focused(comp->clients);
    if (client == focused)
        return;

    // Mark as urgent
    dwl_client_set_urgent(client, true);
}

DwlError dwl_compositor_create(DwlCompositor **out, const DwlCompositorConfig *cfg)
{
    if (!out)
        return DWL_ERR_INVALID_ARG;

    DwlCompositor *comp = calloc(1, sizeof(*comp));
    if (!comp)
        return DWL_ERR_NOMEM;

    dwl_signal_init();

    // Event bus
    comp->event_bus = dwl_event_bus_create();
    if (!comp->event_bus) {
        free(comp);
        return DWL_ERR_NOMEM;
    }

    // Wayland display
    comp->display = wl_display_create();
    if (!comp->display) {
        dwl_event_bus_destroy(comp->event_bus);
        free(comp);
        return DWL_ERR_WAYLAND;
    }

    comp->event_loop = wl_display_get_event_loop(comp->display);

    // Backend
    comp->backend = wlr_backend_autocreate(comp->event_loop, NULL);
    if (!comp->backend) {
        wl_display_destroy(comp->display);
        dwl_event_bus_destroy(comp->event_bus);
        free(comp);
        return DWL_ERR_BACKEND;
    }

    // Renderer (scenefx)
    comp->renderer = fx_renderer_create(comp->backend);
    if (!comp->renderer) {
        wlr_backend_destroy(comp->backend);
        wl_display_destroy(comp->display);
        dwl_event_bus_destroy(comp->event_bus);
        free(comp);
        return DWL_ERR_BACKEND;
    }

    wlr_renderer_init_wl_display(comp->renderer, comp->display);

    // Allocator
    comp->allocator = wlr_allocator_autocreate(comp->backend, comp->renderer);
    if (!comp->allocator) {
        wlr_renderer_destroy(comp->renderer);
        wlr_backend_destroy(comp->backend);
        wl_display_destroy(comp->display);
        dwl_event_bus_destroy(comp->event_bus);
        free(comp);
        return DWL_ERR_BACKEND;
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

    // XDG decoration
    comp->xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(comp->display);
    comp->new_xdg_decoration.notify = handle_new_xdg_decoration;
    wl_signal_add(&comp->xdg_decoration_mgr->events.new_toplevel_decoration,
        &comp->new_xdg_decoration);

    // XDG activation (for urgency hints and focus requests)
    comp->activation = wlr_xdg_activation_v1_create(comp->display);
    comp->request_activate.notify = handle_request_activate;
    wl_signal_add(&comp->activation->events.request_activate, &comp->request_activate);

    // Seat
    comp->seat = wlr_seat_create(comp->display, "seat0");

    // Configuration
    comp->config = dwl_config_create();
    if (cfg && cfg->config_path) {
        comp->config_path = strdup(cfg->config_path);
        dwl_config_load_file(comp->config, cfg->config_path);
    } else {
        dwl_config_load_default(comp->config);
    }

    // Layout registry
    comp->layouts = dwl_layout_registry_create();
    dwl_layout_register_builtins(comp->layouts);

    // Client manager
    comp->clients = dwl_client_manager_create(comp);

    // Output manager
    comp->output = dwl_output_create(comp);

    // Input
    comp->input = dwl_input_create(comp);

    // Workspace manager
    comp->workspaces = dwl_workspace_create(comp);

    // Renderer
    comp->dwl_renderer = dwl_renderer_create(comp);

    // Configure scenefx blur
    DwlRenderConfig rcfg = dwl_renderer_get_config(comp->dwl_renderer);
    wlr_scene_set_blur_data(comp->scene, rcfg.blur_passes, rcfg.blur_radius,
        0.0f, 1.0f, 1.0f, 1.0f);  // noise, brightness, contrast, saturation

    // IPC
    comp->ipc = dwl_ipc_create(comp);
    dwl_ipc_register_builtins(comp->ipc);

    // Layer shell
    comp->layer_mgr = dwl_layer_manager_create(comp);

    // Foreign toplevel manager
    comp->toplevel_mgr = dwl_toplevel_manager_create(comp);

#ifdef DWL_XWAYLAND
    if (!cfg || cfg->enable_xwayland)
        comp->xwayland = dwl_xwayland_create(comp);
#endif

    if (cfg && cfg->startup_cmd)
        comp->startup_cmd = strdup(cfg->startup_cmd);

    *out = comp;
    return DWL_OK;
}

void dwl_compositor_destroy(DwlCompositor *comp)
{
    if (!comp)
        return;

#ifdef DWL_XWAYLAND
    dwl_xwayland_destroy(comp->xwayland);
#endif

    dwl_toplevel_manager_destroy(comp->toplevel_mgr);
    dwl_layer_manager_destroy(comp->layer_mgr);
    dwl_ipc_destroy(comp->ipc);
    dwl_renderer_destroy(comp->dwl_renderer);
    dwl_workspace_destroy(comp->workspaces);
    dwl_input_destroy(comp->input);
    dwl_output_destroy(comp->output);
    dwl_client_manager_destroy(comp->clients);
    dwl_layout_registry_destroy(comp->layouts);
    dwl_config_destroy(comp->config);

    free(comp->startup_cmd);
    free(comp->config_path);

    wl_list_remove(&comp->new_xdg_toplevel.link);
    wl_list_remove(&comp->new_xdg_popup.link);
    wl_list_remove(&comp->new_xdg_decoration.link);
    wl_list_remove(&comp->request_activate.link);

    wlr_scene_node_destroy(&comp->scene->tree.node);
    wlr_allocator_destroy(comp->allocator);
    wlr_renderer_destroy(comp->renderer);
    wlr_backend_destroy(comp->backend);
    wl_display_destroy(comp->display);
    dwl_event_bus_destroy(comp->event_bus);
    free(comp);
}

DwlError dwl_compositor_run(DwlCompositor *comp)
{
    if (!comp)
        return DWL_ERR_INVALID_ARG;

    const char *socket = wl_display_add_socket_auto(comp->display);
    if (!socket)
        return DWL_ERR_WAYLAND;

    if (!wlr_backend_start(comp->backend))
        return DWL_ERR_BACKEND;

    setenv("WAYLAND_DISPLAY", socket, 1);

#ifdef DWL_XWAYLAND
    if (comp->xwayland) {
        const char *xdisplay = dwl_xwayland_get_display(comp->xwayland);
        if (xdisplay)
            setenv("DISPLAY", xdisplay, 1);
    }
#endif

    if (comp->startup_cmd) {
        if (fork() == 0) {
            setsid();
            execl("/bin/sh", "/bin/sh", "-c", comp->startup_cmd, NULL);
            _exit(1);
        }
    }

    comp->running = true;
    wl_display_run(comp->display);

    return DWL_OK;
}

void dwl_compositor_quit(DwlCompositor *comp)
{
    if (!comp)
        return;

    comp->running = false;
    dwl_signal_request_quit();
    wl_display_terminate(comp->display);
}

DwlEventBus *dwl_compositor_get_event_bus(DwlCompositor *comp)
{
    return comp ? comp->event_bus : NULL;
}

DwlInput *dwl_compositor_get_input(DwlCompositor *comp)
{
    return comp ? comp->input : NULL;
}

DwlOutputManager *dwl_compositor_get_output(DwlCompositor *comp)
{
    return comp ? comp->output : NULL;
}

DwlClientManager *dwl_compositor_get_clients(DwlCompositor *comp)
{
    return comp ? comp->clients : NULL;
}

DwlConfig *dwl_compositor_get_config(DwlCompositor *comp)
{
    return comp ? comp->config : NULL;
}

DwlWorkspaceManager *dwl_compositor_get_workspaces(DwlCompositor *comp)
{
    return comp ? comp->workspaces : NULL;
}

DwlRenderer *dwl_compositor_get_renderer(DwlCompositor *comp)
{
    return comp ? comp->dwl_renderer : NULL;
}

DwlIPC *dwl_compositor_get_ipc(DwlCompositor *comp)
{
    return comp ? comp->ipc : NULL;
}

DwlLayoutRegistry *dwl_compositor_get_layouts(DwlCompositor *comp)
{
    return comp ? comp->layouts : NULL;
}

struct wl_display *dwl_compositor_get_wl_display(DwlCompositor *comp)
{
    return comp ? comp->display : NULL;
}

struct wlr_backend *dwl_compositor_get_backend(DwlCompositor *comp)
{
    return comp ? comp->backend : NULL;
}

struct wlr_allocator *dwl_compositor_get_allocator(DwlCompositor *comp)
{
    return comp ? comp->allocator : NULL;
}

struct wlr_renderer *dwl_compositor_get_wlr_renderer(DwlCompositor *comp)
{
    return comp ? comp->renderer : NULL;
}

struct wlr_scene *dwl_compositor_get_scene(DwlCompositor *comp)
{
    return comp ? comp->scene : NULL;
}

struct wlr_seat *dwl_compositor_get_seat(DwlCompositor *comp)
{
    return comp ? comp->seat : NULL;
}

struct wlr_output_layout *dwl_compositor_get_output_layout(DwlCompositor *comp)
{
    return comp ? comp->output_layout : NULL;
}

struct wlr_idle_notifier_v1 *dwl_compositor_get_idle_notifier(DwlCompositor *comp)
{
    return comp ? comp->idle_notifier : NULL;
}

struct wlr_compositor *dwl_compositor_get_wlr_compositor(DwlCompositor *comp)
{
    return comp ? comp->wlr_compositor : NULL;
}

DwlLayerManager *dwl_compositor_get_layer_manager(DwlCompositor *comp)
{
    return comp ? comp->layer_mgr : NULL;
}

DwlToplevelManager *dwl_compositor_get_toplevel_manager(DwlCompositor *comp)
{
    return comp ? comp->toplevel_mgr : NULL;
}
