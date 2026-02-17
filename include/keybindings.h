#ifndef DWL_KEYBINDINGS_H
#define DWL_KEYBINDINGS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <xkbcommon/xkbcommon.h>
#include "error.h"

typedef struct DwlCompositor DwlCompositor;
typedef struct DwlKeybindingManager DwlKeybindingManager;
typedef struct DwlInput DwlInput;

typedef struct DwlKeybinding {
    uint32_t modifiers;
    xkb_keysym_t keysym;
    const char *action;
    const char *argument;
} DwlKeybinding;

typedef struct DwlButtonBinding {
    uint32_t modifiers;
    uint32_t button;
    const char *action;
    const char *argument;
} DwlButtonBinding;

typedef void (*DwlAction)(DwlCompositor *comp, const char *arg);

DwlKeybindingManager *dwl_keybinding_create(DwlInput *input);
void dwl_keybinding_destroy(DwlKeybindingManager *mgr);

DwlError dwl_keybinding_add(DwlKeybindingManager *mgr, const DwlKeybinding *binding);
DwlError dwl_keybinding_remove(DwlKeybindingManager *mgr, uint32_t mod, xkb_keysym_t key);
void dwl_keybinding_clear(DwlKeybindingManager *mgr);
size_t dwl_keybinding_count(const DwlKeybindingManager *mgr);

DwlError dwl_button_binding_add(DwlKeybindingManager *mgr, const DwlButtonBinding *binding);
DwlError dwl_button_binding_remove(DwlKeybindingManager *mgr, uint32_t mod, uint32_t button);
void dwl_button_binding_clear(DwlKeybindingManager *mgr);

DwlError dwl_action_register(DwlKeybindingManager *mgr, const char *name, DwlAction action);
DwlError dwl_action_unregister(DwlKeybindingManager *mgr, const char *name);
void dwl_action_register_builtins(DwlKeybindingManager *mgr);

bool dwl_keybinding_handle(DwlKeybindingManager *mgr, uint32_t mod, xkb_keysym_t key);
bool dwl_button_binding_handle(DwlKeybindingManager *mgr, uint32_t mod, uint32_t button);

#endif /* DWL_KEYBINDINGS_H */
