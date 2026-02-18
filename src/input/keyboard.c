#include "input_internal.h"
#include "compositor.h"
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>

void configure_keyboard(SwlInput *input, struct wlr_keyboard *kb)
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

    input->locked_mods = 0;
    if (input->kb_config.numlock && kb->keymap) {
        xkb_mod_index_t mod = xkb_keymap_mod_get_index(kb->keymap,
            XKB_MOD_NAME_NUM);
        if (mod != XKB_MOD_INVALID)
            input->locked_mods |= (uint32_t)1 << mod;
    }

    if (input->locked_mods)
        wlr_keyboard_notify_modifiers(kb, 0, 0, input->locked_mods, 0);

    xkb_context_unref(ctx);
}

void handle_keyboard_key(struct wl_listener *listener, void *data)
{
    SwlInput *input = wl_container_of(listener, input, keyboard_key);
    struct wlr_keyboard_key_event *event = data;
    struct wlr_keyboard *kb = &input->kb_group->keyboard;

    wlr_idle_notifier_v1_notify_activity(
        swl_compositor_get_idle_notifier(input->comp), input->seat);

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(kb->xkb_state, keycode, &syms);

    bool handled = false;
    uint32_t mods = wlr_keyboard_get_modifiers(kb);

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        for (int i = 0; i < nsyms; i++) {
            if (swl_keybinding_handle(input->keybindings, mods, syms[i])) {
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

void handle_keyboard_modifiers(struct wl_listener *listener, void *data)
{
    SwlInput *input = wl_container_of(listener, input, keyboard_modifiers);
    struct wlr_keyboard *kb = &input->kb_group->keyboard;
    (void)data;

    wlr_seat_set_keyboard(input->seat, kb);
    wlr_seat_keyboard_notify_modifiers(input->seat, &kb->modifiers);
}

void swl_keyboard_setup(SwlInput *input, struct wlr_keyboard_group *kb_group)
{
    input->keyboard_key.notify = handle_keyboard_key;
    wl_signal_add(&kb_group->keyboard.events.key, &input->keyboard_key);

    input->keyboard_modifiers.notify = handle_keyboard_modifiers;
    wl_signal_add(&kb_group->keyboard.events.modifiers, &input->keyboard_modifiers);
}

void swl_keyboard_cleanup(SwlInput *input)
{
    wl_list_remove(&input->keyboard_key.link);
    wl_list_remove(&input->keyboard_modifiers.link);
}
