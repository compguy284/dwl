#define _POSIX_C_SOURCE 200809L
#include "input_internal.h"
#include "compositor.h"
#include "config.h"
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libinput.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_switch.h>
#ifdef SWL_XWAYLAND
#include <wlr/xwayland.h>
#endif

static void handle_new_input(struct wl_listener *listener, void *data);

SwlInput *swl_input_create(SwlCompositor *comp)
{
    SwlInput *input = calloc(1, sizeof(*input));
    if (!input)
        return NULL;

    input->comp = comp;

    // Load keyboard config from config file
    SwlConfig *cfg = swl_compositor_get_config(comp);
    input->kb_config.repeat_rate = swl_config_get_int(cfg, "keyboard.repeat_rate", 25);
    input->kb_config.repeat_delay = swl_config_get_int(cfg, "keyboard.repeat_delay", 600);
    input->kb_config.numlock = swl_config_get_bool(cfg, "keyboard.numlock", true);

    input->kb_config.xkb_layout = swl_config_get_string(cfg, "keyboard.xkb.layout", NULL);
    input->kb_config.xkb_rules = swl_config_get_string(cfg, "keyboard.xkb.rules", NULL);
    input->kb_config.xkb_model = swl_config_get_string(cfg, "keyboard.xkb.model", NULL);
    input->kb_config.xkb_variant = swl_config_get_string(cfg, "keyboard.xkb.variant", NULL);
    input->kb_config.xkb_options = swl_config_get_string(cfg, "keyboard.xkb.options", NULL);

    // Load pointer config from config file
    input->ptr_config.tap_to_click = swl_config_get_bool(cfg, "pointer.tap_to_click", true);
    input->ptr_config.tap_and_drag = swl_config_get_bool(cfg, "pointer.tap_and_drag", true);
    input->ptr_config.drag_lock = swl_config_get_bool(cfg, "pointer.drag_lock", true);
    input->ptr_config.natural_scroll = swl_config_get_bool(cfg, "pointer.natural_scrolling", false);
    input->ptr_config.disable_while_typing = swl_config_get_bool(cfg, "pointer.disable_while_typing", true);
    input->ptr_config.left_handed = swl_config_get_bool(cfg, "pointer.left_handed", false);
    input->ptr_config.middle_emulation = swl_config_get_bool(cfg, "pointer.middle_button_emulation", false);
    input->ptr_config.accel_speed = swl_config_get_float(cfg, "pointer.accel_speed", 0.0f);

    // Parse scroll method
    const char *scroll_str = swl_config_get_string(cfg, "pointer.scroll_method", "two_finger");
    if (strcasecmp(scroll_str, "edge") == 0)
        input->ptr_config.scroll_method = LIBINPUT_CONFIG_SCROLL_EDGE;
    else if (strcasecmp(scroll_str, "button") == 0)
        input->ptr_config.scroll_method = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
    else if (strcasecmp(scroll_str, "none") == 0)
        input->ptr_config.scroll_method = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
    else
        input->ptr_config.scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

    // Parse click method
    const char *click_str = swl_config_get_string(cfg, "pointer.click_method", "button_areas");
    if (strcasecmp(click_str, "clickfinger") == 0)
        input->ptr_config.click_method = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
    else if (strcasecmp(click_str, "none") == 0)
        input->ptr_config.click_method = LIBINPUT_CONFIG_CLICK_METHOD_NONE;
    else
        input->ptr_config.click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

    // Parse acceleration profile
    const char *accel_str = swl_config_get_string(cfg, "pointer.accel_profile", "adaptive");
    if (strcasecmp(accel_str, "flat") == 0)
        input->ptr_config.accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
    else
        input->ptr_config.accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;

    // Parse tap button map
    const char *map_str = swl_config_get_string(cfg, "pointer.button_map", "lrm");
    if (strcasecmp(map_str, "lmr") == 0)
        input->ptr_config.tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LMR;
    else
        input->ptr_config.tap_button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

    // Parse send_events mode
    const char *send_events_str = swl_config_get_string(cfg, "pointer.send_events", "enabled");
    if (strcasecmp(send_events_str, "disabled") == 0)
        input->ptr_config.send_events = SWL_SEND_EVENTS_DISABLED;
    else if (strcasecmp(send_events_str, "disabled_on_external_mouse") == 0)
        input->ptr_config.send_events = SWL_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
    else
        input->ptr_config.send_events = SWL_SEND_EVENTS_ENABLED;

    struct wlr_backend *backend = swl_compositor_get_backend(comp);
    struct wlr_output_layout *layout = swl_compositor_get_output_layout(comp);

    input->cursor = wlr_cursor_create();
    if (!input->cursor) {
        free(input);
        return NULL;
    }

    wlr_cursor_attach_output_layout(input->cursor, layout);

    const char *cursor_theme = swl_config_get_string(cfg, "pointer.cursor_theme", NULL);
    int cursor_size = swl_config_get_int(cfg, "pointer.cursor_size", 24);

    // Export cursor theme settings so child processes (GTK, Qt) use them
    if (cursor_theme)
        setenv("XCURSOR_THEME", cursor_theme, 1);
    char size_str[16];
    snprintf(size_str, sizeof(size_str), "%d", cursor_size);
    setenv("XCURSOR_SIZE", size_str, 1);

    input->xcursor_mgr = wlr_xcursor_manager_create(cursor_theme, cursor_size);
    if (!input->xcursor_mgr) {
        wlr_cursor_destroy(input->cursor);
        free(input);
        return NULL;
    }

    // Use the compositor's seat instead of creating a new one
    input->seat = swl_compositor_get_seat(comp);

    input->kb_group = wlr_keyboard_group_create();

    // Apply user keyboard config (XKB layout, repeat rate, numlock)
    configure_keyboard(input, &input->kb_group->keyboard);

    input->new_input.notify = handle_new_input;
    wl_signal_add(&backend->events.new_input, &input->new_input);

    swl_keyboard_setup(input, input->kb_group);
    swl_pointer_setup(input);

    input->keybindings = swl_keybinding_create(input);
    if (input->keybindings) {
        swl_action_register_builtins(input->keybindings);
    }

    // Set default cursor
    wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "default");

    return input;
}

void swl_input_destroy(SwlInput *input)
{
    if (!input)
        return;

    wl_list_remove(&input->new_input.link);
    swl_keyboard_cleanup(input);
    swl_pointer_cleanup(input);

    swl_keybinding_destroy(input->keybindings);
    wlr_keyboard_group_destroy(input->kb_group);
    wlr_xcursor_manager_destroy(input->xcursor_mgr);
    wlr_cursor_destroy(input->cursor);
    free(input);
}

static void handle_new_input(struct wl_listener *listener, void *data)
{
    SwlInput *input = wl_container_of(listener, input, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD: {
        struct wlr_keyboard *kb = wlr_keyboard_from_input_device(device);
        // Set the same keymap as the group so they can be combined
        wlr_keyboard_set_keymap(kb, input->kb_group->keyboard.keymap);
        wlr_keyboard_set_repeat_info(kb,
            input->kb_config.repeat_rate > 0 ? input->kb_config.repeat_rate : 25,
            input->kb_config.repeat_delay > 0 ? input->kb_config.repeat_delay : 600);
        // Apply locked modifiers (numlock) before adding to group
        if (input->locked_mods)
            wlr_keyboard_notify_modifiers(kb, 0, 0, input->locked_mods, 0);
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
    case WLR_INPUT_DEVICE_SWITCH: {
        struct wlr_switch *sw = wlr_switch_from_input_device(device);
        swl_switch_setup(input, sw);
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

SwlError swl_input_configure_keyboard(SwlInput *input, const SwlKeyboardConfig *cfg)
{
    if (!input || !cfg)
        return SWL_ERR_INVALID_ARG;

    input->kb_config = *cfg;

    // Configure the keyboard group itself (all keyboards inherit settings)
    configure_keyboard(input, &input->kb_group->keyboard);

    return SWL_OK;
}

SwlError swl_input_configure_pointer(SwlInput *input, const SwlPointerConfig *cfg)
{
    if (!input || !cfg)
        return SWL_ERR_INVALID_ARG;

    input->ptr_config = *cfg;
    return SWL_OK;
}

SwlError swl_input_set_cursor_image(SwlInput *input, const char *name)
{
    if (!input || !name)
        return SWL_ERR_INVALID_ARG;

    wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, name);
    return SWL_OK;
}

SwlError swl_input_warp_cursor(SwlInput *input, double x, double y)
{
    if (!input)
        return SWL_ERR_INVALID_ARG;

    wlr_cursor_warp(input->cursor, NULL, x, y);
    return SWL_OK;
}

void swl_input_get_cursor_position(SwlInput *input, double *x, double *y)
{
    if (!input)
        return;

    if (x)
        *x = input->cursor->x;
    if (y)
        *y = input->cursor->y;
}

uint32_t swl_input_get_modifiers(SwlInput *input)
{
    if (!input || !input->kb_group)
        return 0;

    return wlr_keyboard_get_modifiers(&input->kb_group->keyboard);
}

struct wlr_cursor *swl_input_get_cursor(SwlInput *input)
{
    return input ? input->cursor : NULL;
}

struct wlr_seat *swl_input_get_seat(SwlInput *input)
{
    return input ? input->seat : NULL;
}

SwlCompositor *swl_input_get_compositor(SwlInput *input)
{
    return input ? input->comp : NULL;
}

SwlKeybindingManager *swl_input_get_keybindings(SwlInput *input)
{
    return input ? input->keybindings : NULL;
}

SwlError swl_input_start_move(SwlInput *input)
{
    if (!input)
        return SWL_ERR_INVALID_ARG;

    SwlClient *client = client_at_cursor(input);
    if (!client)
        return SWL_ERR_NOT_FOUND;

    SwlClientInfo info = swl_client_get_info(client);

    // Don't move fullscreen windows
    if (info.fullscreen)
        return SWL_ERR_INVALID_ARG;

    // Make window floating if not already
    if (!info.floating)
        swl_client_set_floating(client, true);

    swl_client_focus(client);

    input->cursor_mode = SWL_CURSOR_MOVE;
    input->grabbed_client = client;
    input->grab_x = (int)input->cursor->x - info.geometry.x;
    input->grab_y = (int)input->cursor->y - info.geometry.y;
    wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "grab");

    return SWL_OK;
}

SwlError swl_input_start_resize(SwlInput *input)
{
    if (!input)
        return SWL_ERR_INVALID_ARG;

    SwlClient *client = client_at_cursor(input);
    if (!client)
        return SWL_ERR_NOT_FOUND;

    SwlClientInfo info = swl_client_get_info(client);

    // Don't resize fullscreen windows
    if (info.fullscreen)
        return SWL_ERR_INVALID_ARG;

    // Make window floating if not already
    if (!info.floating)
        swl_client_set_floating(client, true);

    swl_client_focus(client);

    input->cursor_mode = SWL_CURSOR_RESIZE;
    input->grabbed_client = client;
    input->grab_width = info.geometry.width;
    input->grab_height = info.geometry.height;
    wlr_cursor_warp_closest(input->cursor, NULL,
        info.geometry.x + info.geometry.width,
        info.geometry.y + info.geometry.height);
    wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "se-resize");

    return SWL_OK;
}

void swl_input_cancel_grab(SwlInput *input)
{
    if (!input)
        return;

    if (input->cursor_mode != SWL_CURSOR_NORMAL) {
        input->cursor_mode = SWL_CURSOR_NORMAL;
        input->grabbed_client = NULL;
        wlr_cursor_set_xcursor(input->cursor, input->xcursor_mgr, "default");
    }
}
