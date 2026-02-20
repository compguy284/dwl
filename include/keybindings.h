#ifndef SWL_KEYBINDINGS_H
#define SWL_KEYBINDINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <xkbcommon/xkbcommon.h>
#include "error.h"

typedef struct SwlCompositor SwlCompositor;
typedef struct SwlKeybindingManager SwlKeybindingManager;
typedef struct SwlInput SwlInput;

typedef struct SwlKeybinding {
    uint32_t modifiers;
    xkb_keysym_t keysym;
    const char *action;
    const char *argument;
} SwlKeybinding;

typedef struct SwlButtonBinding {
    uint32_t modifiers;
    uint32_t button;
    const char *action;
    const char *argument;
} SwlButtonBinding;

typedef void (*SwlAction)(SwlCompositor *comp, const char *arg);

SwlKeybindingManager *swl_keybinding_create(SwlInput *input);
void swl_keybinding_destroy(SwlKeybindingManager *mgr);

SwlError swl_keybinding_add(SwlKeybindingManager *mgr, const SwlKeybinding *binding);
SwlError swl_keybinding_remove(SwlKeybindingManager *mgr, uint32_t mod, xkb_keysym_t key);
void swl_keybinding_clear(SwlKeybindingManager *mgr);
size_t swl_keybinding_count(const SwlKeybindingManager *mgr);

SwlError swl_button_binding_add(SwlKeybindingManager *mgr, const SwlButtonBinding *binding);
SwlError swl_button_binding_remove(SwlKeybindingManager *mgr, uint32_t mod, uint32_t button);
void swl_button_binding_clear(SwlKeybindingManager *mgr);

SwlError swl_action_register(SwlKeybindingManager *mgr, const char *name, SwlAction action);
SwlError swl_action_unregister(SwlKeybindingManager *mgr, const char *name);
void swl_action_register_builtins(SwlKeybindingManager *mgr);

bool swl_keybinding_handle(SwlKeybindingManager *mgr, uint32_t mod, xkb_keysym_t key);
bool swl_button_binding_handle(SwlKeybindingManager *mgr, uint32_t mod, uint32_t button);
void swl_keybinding_reload(SwlKeybindingManager *mgr);

SwlError swl_action_dispatch(SwlKeybindingManager *mgr, const char *action, const char *arg);

#endif /* SWL_KEYBINDINGS_H */
