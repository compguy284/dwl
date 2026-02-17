#include "input_internal.h"
#include "compositor.h"
#include "config.h"
#include "client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#ifdef DWL_XWAYLAND
#include <wlr/xwayland.h>
#endif

static void handle_new_input(struct wl_listener *listener, void *data);

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

    dwl_keyboard_setup(input, input->kb_group);
    dwl_pointer_setup(input);

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
    dwl_keyboard_cleanup(input);
    dwl_pointer_cleanup(input);

    dwl_keybinding_destroy(input->keybindings);
    wlr_keyboard_group_destroy(input->kb_group);
    wlr_xcursor_manager_destroy(input->xcursor_mgr);
    wlr_cursor_destroy(input->cursor);
    free(input);
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
