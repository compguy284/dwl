#define _POSIX_C_SOURCE 200809L
#include "session_lock.h"
#include "client.h"
#include "compositor.h"
#include "events.h"
#include "monitor.h"
#include "scene.h"
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_output.h>
#include <scenefx/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>

struct SwlLockSurface {
    SwlSessionLock *lock;
    struct wlr_session_lock_surface_v1 *lock_surface;
    struct wlr_scene_tree *scene_tree;
    SwlMonitor *mon;

    struct wl_listener map;
    struct wl_listener destroy;
    struct wl_listener surface_commit;
    struct wl_list link;
};

struct SwlSessionLock {
    SwlCompositor *comp;
    struct wlr_session_lock_manager_v1 *manager;
    struct wlr_session_lock_v1 *active_lock;
    struct wl_list surfaces; // SwlLockSurface.link
    bool locked;

    struct wl_listener new_lock;
    struct wl_listener manager_destroy;

    // Listeners on the active lock
    struct wl_listener lock_new_surface;
    struct wl_listener lock_unlock;
    struct wl_listener lock_destroy;
};

static void focus_lock_surface(SwlSessionLock *lock)
{
    struct wlr_seat *seat = swl_compositor_get_seat(lock->comp);

    // Focus the first mapped lock surface
    struct SwlLockSurface *ls;
    wl_list_for_each(ls, &lock->surfaces, link) {
        if (ls->lock_surface->surface->mapped) {
            struct wlr_keyboard *kb = wlr_seat_get_keyboard(seat);
            if (kb)
                wlr_seat_keyboard_notify_enter(seat, ls->lock_surface->surface,
                    kb->keycodes, kb->num_keycodes, &kb->modifiers);
            return;
        }
    }

    // No mapped surface — clear focus
    wlr_seat_keyboard_notify_clear_focus(seat);
}

static bool all_outputs_have_surface(SwlSessionLock *lock)
{
    SwlOutputManager *output_mgr = swl_compositor_get_output(lock->comp);
    size_t count = swl_monitor_count(output_mgr);

    for (size_t i = 0; i < count; i++) {
        SwlMonitor *mon = swl_monitor_by_index(output_mgr, i);
        struct wlr_output *output = swl_monitor_get_wlr_output(mon);
        bool found = false;

        struct SwlLockSurface *ls;
        wl_list_for_each(ls, &lock->surfaces, link) {
            if (ls->lock_surface->output == output && ls->lock_surface->surface->mapped) {
                found = true;
                break;
            }
        }

        if (!found)
            return false;
    }

    return true;
}

static void lock_surface_handle_map(struct wl_listener *listener, void *data)
{
    struct SwlLockSurface *surface = wl_container_of(listener, surface, map);
    (void)data;

    focus_lock_surface(surface->lock);

    // If all outputs now have a mapped lock surface, notify the lock client
    if (all_outputs_have_surface(surface->lock))
        wlr_session_lock_v1_send_locked(surface->lock->active_lock);
}

static void lock_surface_handle_destroy(struct wl_listener *listener, void *data)
{
    struct SwlLockSurface *surface = wl_container_of(listener, surface, destroy);
    (void)data;

    wl_list_remove(&surface->map.link);
    wl_list_remove(&surface->destroy.link);
    wl_list_remove(&surface->surface_commit.link);
    wl_list_remove(&surface->link);

    if (surface->scene_tree)
        wlr_scene_node_destroy(&surface->scene_tree->node);

    free(surface);
}

static void lock_surface_handle_commit(struct wl_listener *listener, void *data)
{
    struct SwlLockSurface *surface = wl_container_of(listener, surface, surface_commit);
    (void)data;

    // Re-configure if monitor dimensions changed
    if (surface->mon) {
        SwlMonitorInfo info = swl_monitor_get_info(surface->mon);
        wlr_session_lock_surface_v1_configure(surface->lock_surface,
            info.width, info.height);
    }
}

static void handle_lock_new_surface(struct wl_listener *listener, void *data)
{
    SwlSessionLock *lock = wl_container_of(listener, lock, lock_new_surface);
    struct wlr_session_lock_surface_v1 *lock_surface = data;

    struct SwlLockSurface *surface = calloc(1, sizeof(*surface));
    if (!surface)
        return;

    surface->lock = lock;
    surface->lock_surface = lock_surface;

    // Find the monitor for this output
    SwlOutputManager *output_mgr = swl_compositor_get_output(lock->comp);
    for (size_t i = 0; i < swl_monitor_count(output_mgr); i++) {
        SwlMonitor *mon = swl_monitor_by_index(output_mgr, i);
        if (swl_monitor_get_wlr_output(mon) == lock_surface->output) {
            surface->mon = mon;
            break;
        }
    }

    if (!surface->mon) {
        free(surface);
        return;
    }

    // Create scene tree under SWL_LAYER_BLOCK
    SwlClientManager *clients = swl_compositor_get_clients(lock->comp);
    SwlSceneManager *scene_mgr = swl_client_manager_get_scene(clients);
    struct wlr_scene_tree *block_tree = swl_scene_get_layer(scene_mgr, SWL_LAYER_BLOCK);

    surface->scene_tree = wlr_scene_subsurface_tree_create(block_tree,
        lock_surface->surface);
    if (!surface->scene_tree) {
        free(surface);
        return;
    }

    // Position the lock surface at the monitor's origin
    SwlMonitorInfo info = swl_monitor_get_info(surface->mon);
    wlr_scene_node_set_position(&surface->scene_tree->node, info.x, info.y);

    // Configure with monitor dimensions
    wlr_session_lock_surface_v1_configure(lock_surface, info.width, info.height);

    // Set up listeners
    surface->map.notify = lock_surface_handle_map;
    wl_signal_add(&lock_surface->surface->events.map, &surface->map);

    surface->destroy.notify = lock_surface_handle_destroy;
    wl_signal_add(&lock_surface->events.destroy, &surface->destroy);

    surface->surface_commit.notify = lock_surface_handle_commit;
    wl_signal_add(&lock_surface->surface->events.commit, &surface->surface_commit);

    wl_list_insert(&lock->surfaces, &surface->link);
}

static void handle_lock_unlock(struct wl_listener *listener, void *data)
{
    SwlSessionLock *lock = wl_container_of(listener, lock, lock_unlock);
    (void)data;

    lock->locked = false;
    lock->active_lock = NULL;

    wl_list_remove(&lock->lock_new_surface.link);
    wl_list_remove(&lock->lock_unlock.link);
    wl_list_remove(&lock->lock_destroy.link);

    // Lock surfaces are destroyed by wlroots when the lock is destroyed,
    // which triggers our lock_surface_handle_destroy for cleanup.

    // Restore focus to previously focused client
    SwlClientManager *clients = swl_compositor_get_clients(lock->comp);
    SwlClient *focused = swl_client_focused(clients);
    if (focused)
        swl_client_focus(focused);
    else {
        struct wlr_seat *seat = swl_compositor_get_seat(lock->comp);
        wlr_seat_keyboard_notify_clear_focus(seat);
    }

    SwlEventBus *bus = swl_compositor_get_event_bus(lock->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_SESSION_UNLOCK, NULL);
}

static void handle_lock_destroy(struct wl_listener *listener, void *data)
{
    SwlSessionLock *lock = wl_container_of(listener, lock, lock_destroy);
    (void)data;

    // If still locked, keep the session locked for security.
    // The SWL_LAYER_BLOCK content stays — the desktop is not revealed.

    lock->active_lock = NULL;

    wl_list_remove(&lock->lock_new_surface.link);
    wl_list_remove(&lock->lock_unlock.link);
    wl_list_remove(&lock->lock_destroy.link);

    // If we're still locked, clear keyboard focus so no input leaks
    if (lock->locked) {
        struct wlr_seat *seat = swl_compositor_get_seat(lock->comp);
        wlr_seat_keyboard_notify_clear_focus(seat);
    }
}

static void handle_new_lock(struct wl_listener *listener, void *data)
{
    SwlSessionLock *lock = wl_container_of(listener, lock, new_lock);
    struct wlr_session_lock_v1 *wlr_lock = data;

    if (lock->active_lock) {
        wlr_session_lock_v1_destroy(wlr_lock);
        return;
    }

    lock->active_lock = wlr_lock;
    lock->locked = true;

    // Clear keyboard focus from regular clients
    struct wlr_seat *seat = swl_compositor_get_seat(lock->comp);
    wlr_seat_keyboard_notify_clear_focus(seat);

    lock->lock_new_surface.notify = handle_lock_new_surface;
    wl_signal_add(&wlr_lock->events.new_surface, &lock->lock_new_surface);

    lock->lock_unlock.notify = handle_lock_unlock;
    wl_signal_add(&wlr_lock->events.unlock, &lock->lock_unlock);

    lock->lock_destroy.notify = handle_lock_destroy;
    wl_signal_add(&wlr_lock->events.destroy, &lock->lock_destroy);

    SwlEventBus *bus = swl_compositor_get_event_bus(lock->comp);
    swl_event_bus_emit_simple(bus, SWL_EVENT_SESSION_LOCK, NULL);
}

static void handle_manager_destroy(struct wl_listener *listener, void *data)
{
    SwlSessionLock *lock = wl_container_of(listener, lock, manager_destroy);
    (void)data;

    wl_list_remove(&lock->new_lock.link);
    wl_list_remove(&lock->manager_destroy.link);

    lock->manager = NULL;
}

SwlSessionLock *swl_session_lock_create(SwlCompositor *comp)
{
    SwlSessionLock *lock = calloc(1, sizeof(*lock));
    if (!lock)
        return NULL;

    lock->comp = comp;
    wl_list_init(&lock->surfaces);

    struct wl_display *display = swl_compositor_get_wl_display(comp);
    lock->manager = wlr_session_lock_manager_v1_create(display);
    if (!lock->manager) {
        free(lock);
        return NULL;
    }

    lock->new_lock.notify = handle_new_lock;
    wl_signal_add(&lock->manager->events.new_lock, &lock->new_lock);

    lock->manager_destroy.notify = handle_manager_destroy;
    wl_signal_add(&lock->manager->events.destroy, &lock->manager_destroy);

    return lock;
}

void swl_session_lock_destroy(SwlSessionLock *lock)
{
    if (!lock)
        return;

    if (lock->manager) {
        wl_list_remove(&lock->new_lock.link);
        wl_list_remove(&lock->manager_destroy.link);
    }

    free(lock);
}

bool swl_session_lock_is_locked(const SwlSessionLock *lock)
{
    return lock ? lock->locked : false;
}
