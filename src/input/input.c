#include "input.h"
#include "compositor.h"
#include "config.h"
#include "keybindings.h"
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <scenefx/types/wlr_scene.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#ifdef DWL_XWAYLAND
#include <wlr/xwayland.h>
#endif

enum DwlCursorMode {
    DWL_CURSOR_NORMAL,
    DWL_CURSOR_MOVE,
    DWL_CURSOR_RESIZE,
};

struct DwlInput {
    DwlCompositor *comp;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *xcursor_mgr;
    struct wlr_seat *seat;
    struct wlr_keyboard_group *kb_group;
    DwlKeybindingManager *keybindings;

    struct wl_listener new_input;
    struct wl_listener keyboard_key;
    struct wl_listener keyboard_modifiers;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_abs;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;

    DwlKeyboardConfig kb_config;
    DwlPointerConfig ptr_config;

    uint32_t modifiers;

    // Move/resize state
    enum DwlCursorMode cursor_mode;
    DwlClient *grabbed_client;
    int grab_x, grab_y;          // Cursor offset from window origin
    int grab_width, grab_height; // Original size for resize
};

static void handle_new_input(struct wl_listener *listener, void *data);
static void handle_keyboard_key(struct wl_listener *listener, void *data);
static void handle_keyboard_modifiers(struct wl_listener *listener, void *data);
static void handle_cursor_motion(struct wl_listener *listener, void *data);
static void handle_cursor_motion_abs(struct wl_listener *listener, void *data);
static void handle_cursor_button(struct wl_listener *listener, void *data);
static void handle_cursor_axis(struct wl_listener *listener, void *data);
static void handle_cursor_frame(struct wl_listener *listener, void *data);
static void handle_request_cursor(struct wl_listener *listener, void *data);
static void handle_request_set_selection(struct wl_listener *listener, void *data);

DwlInput *dwl_input_create(DwlCompositor *comp)
{
    DwlInput *input = calloc(1, sizeof(*input));
    if (!input)
        return NULL;

    input->comp = comp;

    // Load keyboard config from config file
    DwlConfig *cfg = dwl_compositor_get_config(comp);
    input->kb_config.repeat_rate = dwl_config_get_int(cfg, "keyboard.repeat_rate", 25);
    input->kb_config.repeat_delay = dwl_config_get_int(cfg, "keyboard.repeat_delay", 600);
    input->kb_config.numlock = dwl_config_get_bool(cfg, "keyboard.numlock", true);

    input->kb_config.xkb_layout = dwl_config_get_string(cfg, "keyboard.xkb.layout", NULL);
    input->kb_config.xkb_rules = dwl_config_get_string(cfg, "keyboard.xkb.rules", NULL);
    input->kb_config.xkb_model = dwl_config_get_string(cfg, "keyboard.xkb.model", NULL);
    input->kb_config.xkb_variant = dwl_config_get_string(cfg, "keyboard.xkb.variant", NULL);
    input->kb_config.xkb_options = dwl_config_get_string(cfg, "keyboard.xkb.options", NULL);

    // Load pointer config from config file
    input->ptr_config.tap_to_click = dwl_config_get_bool(cfg, "pointer.tap_to_click", true);
    input->ptr_config.tap_and_drag = dwl_config_get_bool(cfg, "pointer.tap_and_drag", true);
    input->ptr_config.drag_lock = dwl_config_get_bool(cfg, "pointer.drag_lock", true);
    input->ptr_config.natural_scroll = dwl_config_get_bool(cfg, "pointer.natural_scrolling", false);
    input->ptr_config.disable_while_typing = dwl_config_get_bool(cfg, "pointer.disable_while_typing", true);
    input->ptr_config.left_handed = dwl_config_get_bool(cfg, "pointer.left_handed", false);
    input->ptr_config.middle_emulation = dwl_config_get_bool(cfg, "pointer.middle_button_emulation", false);
    input->ptr_config.accel_speed = dwl_config_get_float(cfg, "pointer.accel_speed", 0.0f);

    // Parse send_events mode
    const char *send_events_str = dwl_config_get_string(cfg, "pointer.send_events", "enabled");
    if (strcasecmp(send_events_str, "disabled") == 0)
        input->ptr_config.send_events = DWL_SEND_EVENTS_DISABLED;
    else if (strcasecmp(send_events_str, "disabled_on_external_mouse") == 0)
        input->ptr_config.send_events = DWL_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
    else
        input->ptr_config.send_events = DWL_SEND_EVENTS_ENABLED;

    struct wlr_backend *backend = dwl_compositor_get_backend(comp);
    struct wlr_output_layout *layout = dwl_compositor_get_output_layout(comp);

    input->cursor = wlr_cursor_create();
    if (!input->cursor) {
        free(input);
        return NULL;
    }

    wlr_cursor_attach_output_layout(input->cursor, layout);

    input->xcursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    if (!input->xcursor_mgr) {
        wlr_cursor_destroy(input->cursor);
        free(input);
        return NULL;
    }

    // Use the compositor's seat instead of creating a new one
    input->seat = dwl_compositor_get_seat(comp);

    input->kb_group = wlr_keyboard_group_create();

    // Set default keymap on the keyboard group
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (ctx) {
        struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, NULL,
            XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (keymap) {
            wlr_keyboard_set_keymap(&input->kb_group->keyboard, keymap);
            xkb_keymap_unref(keymap);
        }
        xkb_context_unref(ctx);
    }

    input->new_input.notify = handle_new_input;
    wl_signal_add(&backend->events.new_input, &input->new_input);

    input->keyboard_key.notify = handle_keyboard_key;
    wl_signal_add(&input->kb_group->keyboard.events.key, &input->keyboard_key);

    input->keyboard_modifiers.notify = handle_keyboard_modifiers;
    wl_signal_add(&input->kb_group->keyboard.events.modifiers, &input->keyboard_modifiers);

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

    input->keybindings = dwl_keybinding_create(input);
    if (input->keybindings) {
        dwl_action_register_builtins(input->keybindings);
    }

    // Set default cursor
    wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "default");

    return input;
}

void dwl_input_destroy(DwlInput *input)
{
    if (!input)
        return;

    wl_list_remove(&input->new_input.link);
    wl_list_remove(&input->keyboard_key.link);
    wl_list_remove(&input->keyboard_modifiers.link);
    wl_list_remove(&input->cursor_motion.link);
    wl_list_remove(&input->cursor_motion_abs.link);
    wl_list_remove(&input->cursor_button.link);
    wl_list_remove(&input->cursor_axis.link);
    wl_list_remove(&input->cursor_frame.link);
    wl_list_remove(&input->request_cursor.link);
    wl_list_remove(&input->request_set_selection.link);

    dwl_keybinding_destroy(input->keybindings);
    wlr_keyboard_group_destroy(input->kb_group);
    wlr_xcursor_manager_destroy(input->xcursor_mgr);
    wlr_cursor_destroy(input->cursor);
    free(input);
}

static void configure_keyboard(DwlInput *input, struct wlr_keyboard *kb)
{
    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!ctx)
        return;

    struct xkb_rule_names rules = {
        .rules = input->kb_config.xkb_rules,
        .model = input->kb_config.xkb_model,
        .layout = input->kb_config.xkb_layout,
        .variant = input->kb_config.xkb_variant,
        .options = input->kb_config.xkb_options,
    };

    struct xkb_keymap *keymap = xkb_keymap_new_from_names(ctx, &rules,
        XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (keymap) {
        wlr_keyboard_set_keymap(kb, keymap);
        xkb_keymap_unref(keymap);
    }

    wlr_keyboard_set_repeat_info(kb,
        input->kb_config.repeat_rate > 0 ? input->kb_config.repeat_rate : 25,
        input->kb_config.repeat_delay > 0 ? input->kb_config.repeat_delay : 600);

    xkb_context_unref(ctx);
}

static void configure_pointer(DwlInput *input, struct wlr_pointer *ptr)
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

static void handle_new_input(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);
        // Set the same keymap as the group so they can be combined
        wlr_keyboard_set_keymap(kb, input->kb_group->keyboard.keymap);
        wlr_keyboard_set_repeat_info(kb,
            input->kb_config.repeat_rate > 0 ? input->kb_config.repeat_rate : 25,
            input->kb_config.repeat_delay > 0 ? input->kb_config.repeat_delay : 600);
        wlr_keyboard_group_add_keyboard(input->kb_group, kb);
        wlr_seat_set_keyboard(input->seat, &input->kb_group->keyboard);
        break;
    }
    case WLR_INPUT_DEVICE_POINTER: {
        struct wlr_pointer *ptr = wlr_pointer_from_input_device(device);
        configure_pointer(input, ptr);
        wlr_cursor_attach_input_device(input->cursor, device);
        break;
    }
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&input->kb_group->devices))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(input->seat, caps);
}

static void handle_keyboard_key(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, keyboard_key);
    struct wlr_keyboard_key_event *event = data;
    struct wlr_keyboard *kb = &input->kb_group->keyboard;

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(kb->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t mods = wlr_keyboard_get_modifiers(kb);

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            if (dwl_keybinding_handle(input->keybindings, mods, syms[i])) {
                handled = true;
                break;
            }
        }
    }

    if (!handled) {
        wlr_seat_set_keyboard(input->seat, kb);
        wlr_seat_keyboard_notify_key(input->seat, event->time_msec,
            event->keycode, event->state);
    }
}

static void handle_keyboard_modifiers(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, keyboard_modifiers);
    struct wlr_keyboard *kb = &input->kb_group->keyboard;
    (void)data;

    wlr_seat_set_keyboard(input->seat, kb);
    wlr_seat_keyboard_notify_modifiers(input->seat, &kb->modifiers);
}

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

static void handle_cursor_motion(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    wlr_cursor_move(input->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(input, event->time_msec);
}

static void handle_cursor_motion_abs(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_motion_abs);
    struct wlr_pointer_motion_absolute_event *event = data;

    wlr_cursor_warp_absolute(input->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(input, event->time_msec);
}

static DwlClient *client_at_cursor(DwlInput *input)
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

static void handle_cursor_button(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_button);
    struct wlr_pointer_button_event *event = data;

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

static void handle_cursor_axis(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_axis);
    struct wlr_pointer_axis_event *event = data;

    wlr_seat_pointer_notify_axis(input->seat, event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source, event->relative_direction);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, cursor_frame);
    (void)data;

    wlr_seat_pointer_notify_frame(input->seat);
}

static void handle_request_cursor(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;

    struct wlr_seat_client *focused = input->seat->pointer_state.focused_client;
    if (focused == event->seat_client)
        wlr_cursor_set_surface(input->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

static void handle_request_set_selection(struct wl_listener *listener, void *data)
{
    DwlInput *input = wl_container_of(listener, input, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;

    wlr_seat_set_selection(input->seat, event->source, event->serial);
}

DwlError dwl_input_configure_keyboard(DwlInput *input, const DwlKeyboardConfig *cfg)
{
    if (!input || !cfg)
        return DWL_ERR_INVALID_ARG;

    input->kb_config = *cfg;

    // Configure the keyboard group itself (all keyboards inherit settings)
    configure_keyboard(input, &input->kb_group->keyboard);

    return DWL_OK;
}

DwlError dwl_input_configure_pointer(DwlInput *input, const DwlPointerConfig *cfg)
{
    if (!input || !cfg)
        return DWL_ERR_INVALID_ARG;

    input->ptr_config = *cfg;
    return DWL_OK;
}

DwlError dwl_input_set_cursor_image(DwlInput *input, const char *name)
{
    if (!input || !name)
        return DWL_ERR_INVALID_ARG;

    wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, name);
    return DWL_OK;
}

DwlError dwl_input_warp_cursor(DwlInput *input, double x, double y)
{
    if (!input)
        return DWL_ERR_INVALID_ARG;

    wlr_cursor_warp(input->cursor, NULL, x, y);
    return DWL_OK;
}

void dwl_input_get_cursor_position(DwlInput *input, double *x, double *y)
{
    if (!input)
        return;

    if (x)
        *x = input->cursor->x;
    if (y)
        *y = input->cursor->y;
}

uint32_t dwl_input_get_modifiers(DwlInput *input)
{
    if (!input || !input->kb_group)
        return 0;

    return wlr_keyboard_get_modifiers(&input->kb_group->keyboard);
}

struct wlr_cursor *dwl_input_get_cursor(DwlInput *input)
{
    return input ? input->cursor : NULL;
}

struct wlr_seat *dwl_input_get_seat(DwlInput *input)
{
    return input ? input->seat : NULL;
}

DwlCompositor *dwl_input_get_compositor(DwlInput *input)
{
    return input ? input->comp : NULL;
}

DwlKeybindingManager *dwl_input_get_keybindings(DwlInput *input)
{
    return input ? input->keybindings : NULL;
}

DwlError dwl_input_start_move(DwlInput *input)
{
    if (!input)
        return DWL_ERR_INVALID_ARG;

    DwlClient *client = client_at_cursor(input);
    if (!client)
        return DWL_ERR_NOT_FOUND;

    DwlClientInfo info = dwl_client_get_info(client);

    // Don't move fullscreen windows
    if (info.fullscreen)
        return DWL_ERR_INVALID_ARG;

    // Make window floating if not already
    if (!info.floating)
        dwl_client_set_floating(client, true);

    dwl_client_focus(client);

    input->cursor_mode = DWL_CURSOR_MOVE;
    input->grabbed_client = client;
    input->grab_x = (int)input->cursor->x - info.geometry.x;
    input->grab_y = (int)input->cursor->y - info.geometry.y;
    wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "grab");

    return DWL_OK;
}

DwlError dwl_input_start_resize(DwlInput *input)
{
    if (!input)
        return DWL_ERR_INVALID_ARG;

    DwlClient *client = client_at_cursor(input);
    if (!client)
        return DWL_ERR_NOT_FOUND;

    DwlClientInfo info = dwl_client_get_info(client);

    // Don't resize fullscreen windows
    if (info.fullscreen)
        return DWL_ERR_INVALID_ARG;

    // Make window floating if not already
    if (!info.floating)
        dwl_client_set_floating(client, true);

    dwl_client_focus(client);

    input->cursor_mode = DWL_CURSOR_RESIZE;
    input->grabbed_client = client;
    input->grab_width = info.geometry.width;
    input->grab_height = info.geometry.height;
    wlr_cursor_warp_closest(input->cursor, NULL,
        info.geometry.x + info.geometry.width,
        info.geometry.y + info.geometry.height);
    wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "se-resize");

    return DWL_OK;
}

void dwl_input_cancel_grab(DwlInput *input)
{
    if (!input)
        return;

    if (input->cursor_mode != DWL_CURSOR_NORMAL) {
        input->cursor_mode = DWL_CURSOR_NORMAL;
        input->grabbed_client = NULL;
        wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "default");
    }
}
