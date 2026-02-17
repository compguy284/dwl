#include "input_internal.h"
#include "compositor.h"
#include "client.h"
#include <wayland-server-core.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_pointer.h>
#include <scenefx/types/wlr_scene.h>
#include <libinput.h>

static void process_cursor_motion(DwlInput *input, uint32_t time)
{
    // Handle move/resize if active
    if (input->cursor_mode == DWL_CURSOR_MOVE) {
        DwlClient *c = input->grabbed_client;
        if (c) {
            int new_x = (int)input->cursor->x - input->grab_x;
            int new_y = (int)input->cursor->y - input->grab_y;
            DwlClientInfo info = dwl_client_get_info(c);
            dwl_client_resize(c, new_x, new_y, info.geometry.width, info.geometry.height);
        }
        return;
    } else if (input->cursor_mode == DWL_CURSOR_RESIZE) {
        DwlClient *c = input->grabbed_client;
        if (c) {
            DwlClientInfo info = dwl_client_get_info(c);
            int new_width = (int)input->cursor->x - info.geometry.x;
            int new_height = (int)input->cursor->y - info.geometry.y;
            if (new_width < 50) new_width = 50;
            if (new_height < 50) new_height = 50;
            dwl_client_resize(c, info.geometry.x, info.geometry.y, new_width, new_height);
        }
        return;
    }

    // Normal cursor motion handling
    struct wlr_scene *scene = dwl_compositor_get_scene(input->comp);
    double sx, sy;
    struct wlr_scene_node *node = wlr_scene_node_at(&scene->tree.node,
        input->cursor->x, input->cursor->y, &sx, &sy);

    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "default");
        wlr_seat_pointer_clear_focus(input->seat);
        return;
    }

    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);

    if (!scene_surface) {
        wlr_seat_pointer_clear_focus(input->seat);
        return;
    }

    struct wlr_surface *surface = scene_surface->surface;
    wlr_seat_pointer_notify_enter(input->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(input->seat, time, sx, sy);
}

void handle_cursor_motion(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    wlr_idle_notifier_v1_notify_activity(
        dwl_compositor_get_idle_notifier(input->comp), input->seat);
    wlr_cursor_move(input->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(input, event->time_msec);
}

void handle_cursor_motion_abs(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_motion_abs);
    struct wlr_pointer_motion_absolute_event *event = data;

    wlr_idle_notifier_v1_notify_activity(
        dwl_compositor_get_idle_notifier(input->comp), input->seat);
    wlr_cursor_warp_absolute(input->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(input, event->time_msec);
}

DwlClient *client_at_cursor(DwlInput *input)
{
    struct wlr_scene *scene = dwl_compositor_get_scene(input->comp);
    double sx, sy;
    struct wlr_scene_node *node = wlr_scene_node_at(&scene->tree.node,
        input->cursor->x, input->cursor->y, &sx, &sy);

    // Walk up to find client
    while (node && !node->data) {
        node = &node->parent->node;
    }

    if (!node)
        return NULL;

    // Validate that node->data is actually a valid DwlClient
    DwlClient *client = node->data;
    if (!dwl_client_is_valid(client))
        return NULL;

    return client;
}

void handle_cursor_button(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_button);
    struct wlr_pointer_button_event *event = data;

    wlr_idle_notifier_v1_notify_activity(
        dwl_compositor_get_idle_notifier(input->comp), input->seat);

    // End move/resize on button release
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (input->cursor_mode != DWL_CURSOR_NORMAL) {
            input->cursor_mode = DWL_CURSOR_NORMAL;
            input->grabbed_client = NULL;
            wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "default");
        }
        wlr_seat_pointer_notify_button(input->seat, event->time_msec, event->button, event->state);
        return;
    }

    // Button pressed - check for configured button bindings
    struct wlr_keyboard *kb = wlr_seat_get_keyboard(input->seat);
    uint32_t mods = kb ? wlr_keyboard_get_modifiers(kb) : 0;

    // Check button bindings (e.g., mod+left = moveresize:move)
    if (input->keybindings && dwl_button_binding_handle(input->keybindings, mods, event->button)) {
        // Button binding handled - don't pass to client
        return;
    }

    // Normal click - focus window and pass to client
    wlr_seat_pointer_notify_button(input->seat, event->time_msec, event->button, event->state);

    DwlClient *client = client_at_cursor(input);
    if (client) {
        dwl_client_focus(client);
    }
}

void handle_cursor_axis(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_axis);
    struct wlr_pointer_axis_event *event = data;

    wlr_idle_notifier_v1_notify_activity(
        dwl_compositor_get_idle_notifier(input->comp), input->seat);
    wlr_seat_pointer_notify_axis(input->seat, event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source, event->relative_direction);
}

void handle_cursor_frame(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_frame);
    (void)data;

    wlr_seat_pointer_notify_frame(input->seat);
}

void handle_request_cursor(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    struct wlr_seat_client *focused = input->seat->pointer_state.focused_client;
    if (focused == event->seat_client)
        wlr_cursor_set_surface(input->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

void handle_request_set_selection(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;

    wlr_seat_set_selection(input->seat, event->source, event->serial);
}

void handle_request_set_primary_selection(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;

    wlr_seat_set_primary_selection(input->seat, event->source, event->serial);
}

void configure_pointer(DwlInput *input, struct wlr_pointer *ptr)
{
    if (!wlr_input_device_is_libinput(&ptr->base))
        return;
    struct libinput_device *dev = wlr_libinput_get_device_handle(&ptr->base);
    if (!dev)
        return;

    if (libinput_device_config_accel_is_available(dev))
        libinput_device_config_accel_set_speed(dev, input->ptr_config.accel_speed);

    if (libinput_device_config_scroll_has_natural_scroll(dev))
        libinput_device_config_scroll_set_natural_scroll_enabled(dev,
            input->ptr_config.natural_scroll);

    if (libinput_device_config_tap_get_finger_count(dev)) {
        libinput_device_config_tap_set_enabled(dev,
            input->ptr_config.tap_to_click ? LIBINPUT_CONFIG_TAP_ENABLED
                                           : LIBINPUT_CONFIG_TAP_DISABLED);
        libinput_device_config_tap_set_drag_enabled(dev,
            input->ptr_config.tap_and_drag ? LIBINPUT_CONFIG_DRAG_ENABLED
                                           : LIBINPUT_CONFIG_DRAG_DISABLED);
        libinput_device_config_tap_set_drag_lock_enabled(dev,
            input->ptr_config.drag_lock ? LIBINPUT_CONFIG_DRAG_LOCK_ENABLED
                                        : LIBINPUT_CONFIG_DRAG_LOCK_DISABLED);
    }

    if (libinput_device_config_left_handed_is_available(dev))
        libinput_device_config_left_handed_set(dev, input->ptr_config.left_handed);

    if (libinput_device_config_middle_emulation_is_available(dev))
        libinput_device_config_middle_emulation_set_enabled(dev,
            input->ptr_config.middle_emulation ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED
                                               : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);

    if (libinput_device_config_scroll_get_methods(dev) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
        libinput_device_config_scroll_set_method(dev, input->ptr_config.scroll_method);

    if (libinput_device_config_dwt_is_available(dev))
        libinput_device_config_dwt_set_enabled(dev,
            input->ptr_config.disable_while_typing ? LIBINPUT_CONFIG_DWT_ENABLED
                                                   : LIBINPUT_CONFIG_DWT_DISABLED);

    // Set send events mode (for disabling trackpad on external mouse)
    uint32_t supported = libinput_device_config_send_events_get_modes(dev);
    if (supported != LIBINPUT_CONFIG_SEND_EVENTS_ENABLED) {
        enum libinput_config_send_events_mode mode;
        switch (input->ptr_config.send_events) {
        case DWL_SEND_EVENTS_DISABLED:
            mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
            break;
        case DWL_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE:
            if (supported & LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE)
                mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
            else
                mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
            break;
        default:
            mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
            break;
        }
        libinput_device_config_send_events_set_mode(dev, mode);
    }
}

void dwl_pointer_setup(DwlInput *input)
{
    input->cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&input->cursor->events.motion, &input->cursor_motion);

    input->cursor_motion_abs.notify = handle_cursor_motion_abs;
    wl_signal_add(&input->cursor->events.motion_absolute, &input->cursor_motion_abs);

    input->cursor_button.notify = handle_cursor_button;
    wl_signal_add(&input->cursor->events.button, &input->cursor_button);

    input->cursor_axis.notify = handle_cursor_axis;
    wl_signal_add(&input->cursor->events.axis, &input->cursor_axis);

    input->cursor_frame.notify = handle_cursor_frame;
    wl_signal_add(&input->cursor->events.frame, &input->cursor_frame);

    input->request_cursor.notify = handle_request_cursor;
    wl_signal_add(&input->seat->events.request_set_cursor, &input->request_cursor);

    input->request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&input->seat->events.request_set_selection, &input->request_set_selection);

    input->request_set_primary_selection.notify = handle_request_set_primary_selection;
    wl_signal_add(&input->seat->events.request_set_primary_selection, &input->request_set_primary_selection);
}

void dwl_pointer_cleanup(DwlInput *input)
{
    wl_list_remove(&input->cursor_motion.link);
    wl_list_remove(&input->cursor_motion_abs.link);
    wl_list_remove(&input->cursor_button.link);
    wl_list_remove(&input->cursor_axis.link);
    wl_list_remove(&input->cursor_frame.link);
    wl_list_remove(&input->request_cursor.link);
    wl_list_remove(&input->request_set_selection.link);
    wl_list_remove(&input->request_set_primary_selection.link);
}
