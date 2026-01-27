/*
 * input.h - Keyboard input handling
 * See LICENSE file for copyright and license details.
 */
#ifndef INPUT_H
#define INPUT_H

#include "dwl.h"

void createkeyboard(struct wlr_keyboard *keyboard);
KeyboardGroup *createkeyboardgroup(void);
void destroykeyboardgroup(struct wl_listener *listener, void *data);
void keypress(struct wl_listener *listener, void *data);
void keypressmod(struct wl_listener *listener, void *data);
int keyrepeat(void *data);
void virtualkeyboard(struct wl_listener *listener, void *data);

#endif /* INPUT_H */
