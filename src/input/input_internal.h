#ifndef DWL_INPUT_INTERNAL_H
#define DWL_INPUT_INTERNAL_H

#include "input.h"
#include "keybindings.h"
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_keyboard_group.h>

typedef struct DwlClient DwlClient;

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
    struct wl_listener request_set_primary_selection;

    DwlKeyboardConfig kb_config;
    DwlPointerConfig ptr_config;

    uint32_t locked_mods;
    uint32_t modifiers;

    // Move/resize state
    enum DwlCursorMode cursor_mode;
    DwlClient *grabbed_client;
    int grab_x, grab_y;          // Cursor offset from window origin
    int grab_width, grab_height; // Original size for resize
};

/* keyboard.c */
void dwl_keyboard_setup(DwlInput *input, struct wlr_keyboard_group *kb_group);
void dwl_keyboard_cleanup(DwlInput *input);
void configure_keyboard(DwlInput *input, struct wlr_keyboard *kb);
void handle_keyboard_key(struct wl_listener *listener, void *data);
void handle_keyboard_modifiers(struct wl_listener *listener, void *data);

/* pointer.c */
void dwl_pointer_setup(DwlInput *input);
void dwl_pointer_cleanup(DwlInput *input);
void configure_pointer(DwlInput *input, struct wlr_pointer *ptr);
void handle_cursor_motion(struct wl_listener *listener, void *data);
void handle_cursor_motion_abs(struct wl_listener *listener, void *data);
void handle_cursor_button(struct wl_listener *listener, void *data);
void handle_cursor_axis(struct wl_listener *listener, void *data);
void handle_cursor_frame(struct wl_listener *listener, void *data);
void handle_request_cursor(struct wl_listener *listener, void *data);
void handle_request_set_selection(struct wl_listener *listener, void *data);
void handle_request_set_primary_selection(struct wl_listener *listener, void *data);
DwlClient *client_at_cursor(DwlInput *input);

#endif /* DWL_INPUT_INTERNAL_H */
