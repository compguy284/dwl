#ifndef DWL_INPUT_H
#define DWL_INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include "error.h"

typedef struct DwlInput DwlInput;
typedef struct DwlCompositor DwlCompositor;

typedef struct DwlKeyboardConfig {
    const char *xkb_rules;
    const char *xkb_model;
    const char *xkb_layout;
    const char *xkb_variant;
    const char *xkb_options;
    int repeat_rate;
    int repeat_delay;
    bool numlock;
    bool capslock;
} DwlKeyboardConfig;

// Send events mode for pointer devices
typedef enum {
    DWL_SEND_EVENTS_ENABLED = 0,
    DWL_SEND_EVENTS_DISABLED = 1,
    DWL_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE = 2,
} DwlSendEventsMode;

typedef struct DwlPointerConfig {
    double accel_speed;
    bool natural_scroll;
    bool tap_to_click;
    bool tap_and_drag;
    bool drag_lock;
    bool left_handed;
    bool middle_emulation;
    int scroll_method;
    bool disable_while_typing;
    DwlSendEventsMode send_events;
} DwlPointerConfig;

DwlInput *dwl_input_create(DwlCompositor *comp);
void dwl_input_destroy(DwlInput *input);

DwlError dwl_input_configure_keyboard(DwlInput *input, const DwlKeyboardConfig *cfg);
DwlError dwl_input_configure_pointer(DwlInput *input, const DwlPointerConfig *cfg);

DwlError dwl_input_set_cursor_image(DwlInput *input, const char *name);
DwlError dwl_input_warp_cursor(DwlInput *input, double x, double y);
void dwl_input_get_cursor_position(DwlInput *input, double *x, double *y);

uint32_t dwl_input_get_modifiers(DwlInput *input);

struct wlr_cursor *dwl_input_get_cursor(DwlInput *input);
struct wlr_seat *dwl_input_get_seat(DwlInput *input);
DwlCompositor *dwl_input_get_compositor(DwlInput *input);
struct DwlKeybindingManager *dwl_input_get_keybindings(DwlInput *input);

// Move/resize operations (for button bindings)
DwlError dwl_input_start_move(DwlInput *input);
DwlError dwl_input_start_resize(DwlInput *input);
void dwl_input_cancel_grab(DwlInput *input);

#endif /* DWL_INPUT_H */
