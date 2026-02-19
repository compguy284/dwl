#ifndef SWL_INPUT_H
#define SWL_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "error.h"

typedef struct SwlInput SwlInput;
typedef struct SwlCompositor SwlCompositor;

typedef struct SwlKeyboardConfig {
    const char *xkb_rules;
    const char *xkb_model;
    const char *xkb_layout;
    const char *xkb_variant;
    const char *xkb_options;
    int repeat_rate;
    int repeat_delay;
    bool numlock;
    bool capslock;
} SwlKeyboardConfig;

// Send events mode for pointer devices
typedef enum {
    SWL_SEND_EVENTS_ENABLED = 0,
    SWL_SEND_EVENTS_DISABLED = 1,
    SWL_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE = 2,
} SwlSendEventsMode;

typedef struct SwlPointerConfig {
    double accel_speed;
    bool natural_scroll;
    bool tap_to_click;
    bool tap_and_drag;
    bool drag_lock;
    bool left_handed;
    bool middle_emulation;
    int scroll_method;
    int click_method;
    int accel_profile;
    int tap_button_map;
    bool disable_while_typing;
    SwlSendEventsMode send_events;
} SwlPointerConfig;

SwlInput *swl_input_create(SwlCompositor *comp);
void swl_input_destroy(SwlInput *input);
SwlError swl_input_reload_config(SwlInput *input);

SwlError swl_input_configure_keyboard(SwlInput *input, const SwlKeyboardConfig *cfg);
SwlError swl_input_configure_pointer(SwlInput *input, const SwlPointerConfig *cfg);

SwlError swl_input_set_cursor_image(SwlInput *input, const char *name);
SwlError swl_input_warp_cursor(SwlInput *input, double x, double y);
void swl_input_get_cursor_position(SwlInput *input, double *x, double *y);

uint32_t swl_input_get_modifiers(SwlInput *input);

struct wlr_cursor *swl_input_get_cursor(SwlInput *input);
struct wlr_seat *swl_input_get_seat(SwlInput *input);
SwlCompositor *swl_input_get_compositor(SwlInput *input);
struct SwlKeybindingManager *swl_input_get_keybindings(SwlInput *input);

// Move/resize operations (for button bindings)
SwlError swl_input_start_move(SwlInput *input);
SwlError swl_input_start_resize(SwlInput *input);
void swl_input_cancel_grab(SwlInput *input);

#endif /* SWL_INPUT_H */
