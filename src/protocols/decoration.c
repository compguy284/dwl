#define _POSIX_C_SOURCE 200809L
#include "decoration.h"
#include <stdbool.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>

typedef struct {
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wl_listener request_mode;
    struct wl_listener destroy;
    struct wl_listener surface_commit;
    bool mode_pending;
} DecorationState;

static void decoration_handle_request_mode(struct wl_listener *listener, void *data);

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

void swl_decoration_handle_new(struct wlr_xdg_toplevel_decoration_v1 *decoration)
{
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
