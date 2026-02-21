#ifndef SWL_INPUT_INTERNAL_H
#define SWL_INPUT_INTERNAL_H

#include "input.h"
#include "keybindings.h"
#include <wayland-server-core.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_switch.h>

typedef struct SwlClient SwlClient;

enum SwlCursorMode {
    SWL_CURSOR_NORMAL,
    SWL_CURSOR_MOVE,
    SWL_CURSOR_RESIZE,
};

struct SwlInput {
    SwlCompositor *comp;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *xcursor_mgr;
    struct wlr_seat *seat;
    struct wlr_keyboard_group *kb_group;
    SwlKeybindingManager *keybindings;

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
    struct wl_listener request_start_drag;
    struct wl_listener start_drag;
    struct wl_listener switch_toggle;

    int monitor_add_sub;
    int client_focus_sub;
    bool lid_closed;
    SwlKeyboardConfig kb_config;
    SwlPointerConfig ptr_config;

    uint32_t locked_mods;
    uint32_t modifiers;

    struct wl_list pointer_devices; // SwlPointerDevice.link

    // Move/resize state
    enum SwlCursorMode cursor_mode;
    SwlClient *grabbed_client;
    int grab_x, grab_y;          // Cursor offset from window origin
    int grab_width, grab_height; // Original size for resize
};

/* keyboard.c */
void swl_keyboard_setup(SwlInput *input, struct wlr_keyboard_group *kb_group);
void swl_keyboard_cleanup(SwlInput *input);
void configure_keyboard(SwlInput *input, struct wlr_keyboard *kb);
void handle_keyboard_key(struct wl_listener *listener, void *data);
void handle_keyboard_modifiers(struct wl_listener *listener, void *data);

/* pointer.c */
struct SwlPointerDevice {
    struct wlr_pointer *ptr;
    struct wl_listener destroy;
    struct wl_list link;
};

void swl_pointer_setup(SwlInput *input);
void swl_pointer_cleanup(SwlInput *input);
void configure_pointer(SwlInput *input, struct wlr_pointer *ptr);
void swl_pointer_track(SwlInput *input, struct wlr_pointer *ptr);
void swl_pointer_reconfigure_all(SwlInput *input);
void handle_cursor_motion(struct wl_listener *listener, void *data);
void handle_cursor_motion_abs(struct wl_listener *listener, void *data);
void handle_cursor_button(struct wl_listener *listener, void *data);
void handle_cursor_axis(struct wl_listener *listener, void *data);
void handle_cursor_frame(struct wl_listener *listener, void *data);
void handle_request_cursor(struct wl_listener *listener, void *data);
void handle_request_set_selection(struct wl_listener *listener, void *data);
void handle_request_set_primary_selection(struct wl_listener *listener, void *data);
void handle_request_start_drag(struct wl_listener *listener, void *data);
void handle_start_drag(struct wl_listener *listener, void *data);
SwlClient *client_at_cursor(SwlInput *input);

/* switch.c */
void swl_switch_setup(SwlInput *input, struct wlr_switch *sw);
void handle_switch_toggle(struct wl_listener *listener, void *data);

#endif /* SWL_INPUT_INTERNAL_H */
