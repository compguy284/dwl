#define _POSIX_C_SOURCE 200809L
#include "keybindings.h"
#include "compositor.h"
#include "client.h"
#include "config.h"
#include "input.h"
#include "layout.h"
#include "monitor.h"
#include <ctype.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_keyboard.h>
#include <xkbcommon/xkbcommon.h>

#define MAX_KEYBINDINGS 256
#define MAX_BUTTON_BINDINGS 32
#define MAX_ACTIONS 128

typedef struct {
    char *name;
    SwlAction action;
} ActionEntry;

struct SwlKeybindingManager {
    SwlInput *input;
    SwlCompositor *comp;

    SwlKeybinding keys[MAX_KEYBINDINGS];
    size_t key_count;

    SwlButtonBinding buttons[MAX_BUTTON_BINDINGS];
    size_t button_count;

    ActionEntry actions[MAX_ACTIONS];
    size_t action_count;
};

SwlKeybindingManager *swl_keybinding_create(SwlInput *input)
{
    SwlKeybindingManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->input = input;
    mgr->comp = swl_input_get_compositor(input);
    return mgr;
}

void swl_keybinding_destroy(SwlKeybindingManager *mgr)
{
    if (!mgr)
        return;

    for (size_t i = 0; i < mgr->key_count; i++) {
        free((void *)mgr->keys[i].action);
        free((void *)mgr->keys[i].argument);
    }

    for (size_t i = 0; i < mgr->button_count; i++) {
        free((void *)mgr->buttons[i].action);
        free((void *)mgr->buttons[i].argument);
    }

    for (size_t i = 0; i < mgr->action_count; i++) {
        free(mgr->actions[i].name);
    }

    free(mgr);
}

SwlError swl_keybinding_add(SwlKeybindingManager *mgr, const SwlKeybinding *binding)
{
    if (!mgr || !binding || !binding->action)
        return SWL_ERR_INVALID_ARG;

    if (mgr->key_count >= MAX_KEYBINDINGS)
        return SWL_ERR_NOMEM;

    SwlKeybinding *k = &mgr->keys[mgr->key_count];
    k->modifiers = binding->modifiers;
    k->keysym = binding->keysym;
    k->action = strdup(binding->action);
    k->argument = binding->argument ? strdup(binding->argument) : NULL;

    mgr->key_count++;
    return SWL_OK;
}

SwlError swl_keybinding_remove(SwlKeybindingManager *mgr, uint32_t mod, xkb_keysym_t key)
{
    if (!mgr)
        return SWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < mgr->key_count; i++) {
        if (mgr->keys[i].modifiers == mod && mgr->keys[i].keysym == key) {
            free((void *)mgr->keys[i].action);
            free((void *)mgr->keys[i].argument);

            memmove(&mgr->keys[i], &mgr->keys[i + 1],
                    (mgr->key_count - i - 1) * sizeof(SwlKeybinding));
            mgr->key_count--;
            return SWL_OK;
        }
    }

    return SWL_ERR_NOT_FOUND;
}

void swl_keybinding_clear(SwlKeybindingManager *mgr)
{
    if (!mgr)
        return;

    for (size_t i = 0; i < mgr->key_count; i++) {
        free((void *)mgr->keys[i].action);
        free((void *)mgr->keys[i].argument);
    }

    mgr->key_count = 0;
}

size_t swl_keybinding_count(const SwlKeybindingManager *mgr)
{
    return mgr ? mgr->key_count : 0;
}

SwlError swl_button_binding_add(SwlKeybindingManager *mgr, const SwlButtonBinding *binding)
{
    if (!mgr || !binding || !binding->action)
        return SWL_ERR_INVALID_ARG;

    if (mgr->button_count >= MAX_BUTTON_BINDINGS)
        return SWL_ERR_NOMEM;

    SwlButtonBinding *b = &mgr->buttons[mgr->button_count];
    b->modifiers = binding->modifiers;
    b->button = binding->button;
    b->action = strdup(binding->action);
    b->argument = binding->argument ? strdup(binding->argument) : NULL;

    mgr->button_count++;
    return SWL_OK;
}

SwlError swl_button_binding_remove(SwlKeybindingManager *mgr, uint32_t mod, uint32_t button)
{
    if (!mgr)
        return SWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < mgr->button_count; i++) {
        if (mgr->buttons[i].modifiers == mod && mgr->buttons[i].button == button) {
            free((void *)mgr->buttons[i].action);
            free((void *)mgr->buttons[i].argument);

            memmove(&mgr->buttons[i], &mgr->buttons[i + 1],
                    (mgr->button_count - i - 1) * sizeof(SwlButtonBinding));
            mgr->button_count--;
            return SWL_OK;
        }
    }

    return SWL_ERR_NOT_FOUND;
}

void swl_button_binding_clear(SwlKeybindingManager *mgr)
{
    if (!mgr)
        return;

    for (size_t i = 0; i < mgr->button_count; i++) {
        free((void *)mgr->buttons[i].action);
        free((void *)mgr->buttons[i].argument);
    }

    mgr->button_count = 0;
}

SwlError swl_action_register(SwlKeybindingManager *mgr, const char *name, SwlAction action)
{
    if (!mgr || !name || !action)
        return SWL_ERR_INVALID_ARG;

    if (mgr->action_count >= MAX_ACTIONS)
        return SWL_ERR_NOMEM;

    for (size_t i = 0; i < mgr->action_count; i++) {
        if (strcmp(mgr->actions[i].name, name) == 0)
            return SWL_ERR_ALREADY_EXISTS;
    }

    mgr->actions[mgr->action_count].name = strdup(name);
    mgr->actions[mgr->action_count].action = action;
    mgr->action_count++;

    return SWL_OK;
}

SwlError swl_action_unregister(SwlKeybindingManager *mgr, const char *name)
{
    if (!mgr || !name)
        return SWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < mgr->action_count; i++) {
        if (strcmp(mgr->actions[i].name, name) == 0) {
            free(mgr->actions[i].name);
            memmove(&mgr->actions[i], &mgr->actions[i + 1],
                    (mgr->action_count - i - 1) * sizeof(ActionEntry));
            mgr->action_count--;
            return SWL_OK;
        }
    }

    return SWL_ERR_NOT_FOUND;
}

static SwlAction find_action(SwlKeybindingManager *mgr, const char *name)
{
    for (size_t i = 0; i < mgr->action_count; i++) {
        if (strcmp(mgr->actions[i].name, name) == 0)
            return mgr->actions[i].action;
    }
    return NULL;
}

bool swl_keybinding_handle(SwlKeybindingManager *mgr, uint32_t mod, xkb_keysym_t key)
{
    if (!mgr)
        return false;

    // Normalize keysym to lowercase for comparison
    // (Shift+q gives XKB_KEY_Q, but bindings use XKB_KEY_q)
    xkb_keysym_t key_lower = xkb_keysym_to_lower(key);

    for (size_t i = 0; i < mgr->key_count; i++) {
        if (mgr->keys[i].modifiers == mod && mgr->keys[i].keysym == key_lower) {
            SwlAction action = find_action(mgr, mgr->keys[i].action);
            if (action) {
                action(mgr->comp, mgr->keys[i].argument);
                return true;
            }
        }
    }

    return false;
}

bool swl_button_binding_handle(SwlKeybindingManager *mgr, uint32_t mod, uint32_t button)
{
    if (!mgr)
        return false;

    for (size_t i = 0; i < mgr->button_count; i++) {
        if (mgr->buttons[i].modifiers == mod && mgr->buttons[i].button == button) {
            SwlAction action = find_action(mgr, mgr->buttons[i].action);
            if (action) {
                action(mgr->comp, mgr->buttons[i].argument);
                return true;
            }
        }
    }

    return false;
}

static void action_quit(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    swl_compositor_quit(comp);
}

static void action_spawn(SwlCompositor *comp, const char *arg)
{
    (void)comp;
    if (!arg)
        return;

    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "/bin/sh", "-c", arg, NULL);
        _exit(1);
    }
}

static void action_close(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *focused = swl_client_focused(clients);
    if (focused)
        swl_client_close(focused);
}

static bool focus_first_client(SwlClient *client, void *data)
{
    SwlClient **first = data;
    if (!*first) {
        *first = client;
        return false;  // Stop iteration
    }
    return true;
}

static void action_focus_next(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *focused = swl_client_focused(clients);

    if (!focused) {
        SwlOutputManager *output = swl_compositor_get_output(comp);
        SwlMonitor *mon = swl_monitor_get_focused(output);
        SwlClient *first = NULL;
        swl_client_foreach_visible(clients, mon, focus_first_client, &first);
        if (first)
            swl_client_focus(first);
        return;
    }

    // TODO: implement proper focus cycling
}

static void action_focus_prev(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    (void)comp;
    // TODO: implement
}

static void action_toggle_floating(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *focused = swl_client_focused(clients);
    if (focused)
        swl_client_toggle_floating(focused);
}

static void action_toggle_fullscreen(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *focused = swl_client_focused(clients);
    if (focused)
        swl_client_toggle_fullscreen(focused);
}

static void action_set_layout(SwlCompositor *comp, const char *arg)
{
    if (!arg)
        return;

    SwlLayoutRegistry *layouts = swl_compositor_get_layouts(comp);
    const SwlLayout *layout = swl_layout_get(layouts, arg);
    if (!layout)
        return;

    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_monitor_get_focused(output);
    if (mon) {
        swl_monitor_set_layout(mon, layout);
        swl_monitor_arrange(mon);
    }
}

// Parse a monitor direction argument: "left"/"right" strings or numeric -1/+1
static int parse_monitor_direction(const char *arg)
{
    if (strcmp(arg, "right") == 0)
        return 3;
    if (strcmp(arg, "left") == 0)
        return 2;
    if (strcmp(arg, "down") == 0)
        return 1;
    if (strcmp(arg, "up") == 0)
        return 0;
    // Numeric: positive = right, negative/zero = left
    int dir = atoi(arg);
    return dir > 0 ? 3 : 2;
}

static void action_focus_monitor(SwlCompositor *comp, const char *arg)
{
    if (!arg)
        return;

    int dir = parse_monitor_direction(arg);
    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_monitor_get_focused(output);
    if (!mon)
        return;

    SwlMonitor *next = swl_monitor_in_direction(output, mon, dir);
    if (next)
        swl_monitor_focus(next);
}

static void action_send_monitor(SwlCompositor *comp, const char *arg)
{
    if (!arg)
        return;

    int dir = parse_monitor_direction(arg);
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *focused = swl_client_focused(clients);
    if (!focused)
        return;

    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_client_get_monitor(focused);
    if (!mon)
        mon = swl_monitor_get_focused(output);
    if (!mon)
        return;

    SwlMonitor *next = swl_monitor_in_direction(output, mon, dir);
    if (next)
        swl_client_move_to_monitor(focused, next);
}

static void action_reload_config(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    SwlConfig *config = swl_compositor_get_config(comp);
    if (config)
        swl_config_reload(config);
}

static void action_zoom(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    swl_client_zoom(clients);
}

static void action_inc_mfact(SwlCompositor *comp, const char *arg)
{
    float delta = arg ? strtof(arg, NULL) : 0.05f;
    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_monitor_get_focused(output);
    if (mon)
        swl_monitor_adjust_mfact(mon, delta);
}

static void action_dec_mfact(SwlCompositor *comp, const char *arg)
{
    float delta = arg ? strtof(arg, NULL) : 0.05f;
    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_monitor_get_focused(output);
    if (mon)
        swl_monitor_adjust_mfact(mon, -delta);
}

static void action_inc_nmaster(SwlCompositor *comp, const char *arg)
{
    int delta = arg ? atoi(arg) : 1;
    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_monitor_get_focused(output);
    if (mon)
        swl_monitor_adjust_nmaster(mon, delta);
}

static void action_dec_nmaster(SwlCompositor *comp, const char *arg)
{
    int delta = arg ? atoi(arg) : 1;
    SwlOutputManager *output = swl_compositor_get_output(comp);
    SwlMonitor *mon = swl_monitor_get_focused(output);
    if (mon)
        swl_monitor_adjust_nmaster(mon, -delta);
}

static void action_focusdir(SwlCompositor *comp, const char *arg)
{
    if (!arg)
        return;

    int dir;
    if (strcmp(arg, "up") == 0)
        dir = 0;
    else if (strcmp(arg, "down") == 0)
        dir = 1;
    else if (strcmp(arg, "left") == 0)
        dir = 2;
    else if (strcmp(arg, "right") == 0)
        dir = 3;
    else
        dir = atoi(arg);

    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *focused = swl_client_focused(clients);
    if (!focused)
        return;

    SwlClient *next = swl_client_in_direction(clients, focused, dir);
    if (next)
        swl_client_focus(next);
}

static void action_moveresize(SwlCompositor *comp, const char *arg)
{
    if (!arg)
        return;

    SwlInput *input = swl_compositor_get_input(comp);
    if (!input)
        return;

    if (strcmp(arg, "move") == 0) {
        swl_input_start_move(input);
    } else if (strcmp(arg, "resize") == 0) {
        swl_input_start_resize(input);
    }
}

static void action_cycle_ratio(SwlCompositor *comp, const char *arg)
{
    (void)arg;
    SwlClientManager *clients = swl_compositor_get_clients(comp);
    SwlClient *focused = swl_client_focused(clients);
    if (!focused)
        return;

    SwlConfig *cfg = swl_compositor_get_config(comp);
    const char *ratios_str = swl_config_get_string(cfg, "appearance.scroller_ratios",
                                                    "0.4,0.6,0.8,1.0");

    // Parse comma-separated float list
    float ratios[32];
    int count = 0;
    char *copy = strdup(ratios_str);
    if (!copy)
        return;

    char *saveptr = NULL;
    char *token = strtok_r(copy, ",", &saveptr);
    while (token && count < 32) {
        while (*token == ' ') token++;
        ratios[count] = strtof(token, NULL);
        if (ratios[count] > 0.0f && ratios[count] <= 1.0f)
            count++;
        token = strtok_r(NULL, ",", &saveptr);
    }
    free(copy);

    if (count == 0)
        return;

    // Get the client's current ratio; 0.0 means "default"
    float current = swl_client_get_scroller_ratio(focused);

    // Get the monitor's default scroller_ratio for matching 0.0
    SwlMonitor *mon = swl_client_get_monitor(focused);
    if (!mon) {
        SwlOutputManager *output = swl_compositor_get_output(comp);
        mon = swl_monitor_get_focused(output);
    }
    float default_ratio = mon ? swl_monitor_get_scroller_ratio(mon) : 0.8f;
    float effective = (current > 0.0f) ? current : default_ratio;

    // Find the closest match in the list
    int best = 0;
    float best_diff = 2.0f;
    for (int i = 0; i < count; i++) {
        float diff = (ratios[i] - effective > 0) ? (ratios[i] - effective) : (effective - ratios[i]);
        if (diff < best_diff) {
            best_diff = diff;
            best = i;
        }
    }

    // Advance to next (wrap)
    int next = (best + 1) % count;
    swl_client_set_scroller_ratio(focused, ratios[next]);

    // Re-arrange the monitor
    if (mon)
        swl_monitor_arrange(mon);
}

static void action_chvt(SwlCompositor *comp, const char *arg)
{
    if (!arg)
        return;

    int vt = atoi(arg);
    if (vt < 1 || vt > 12)
        return;

    struct wlr_session *session = swl_compositor_get_session(comp);
    if (session)
        wlr_session_change_vt(session, vt);
}

// Parse modifier string like "mod+shift+ctrl" and return modifier mask
static uint32_t parse_modifiers(const char *str, uint32_t modkey)
{
    if (!str) return 0;

    uint32_t mods = 0;
    char *copy = strdup(str);
    if (!copy) return 0;

    char *token = strtok(copy, "+");
    while (token) {
        // Trim whitespace
        while (*token && isspace(*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) *end-- = '\0';

        // Convert to lowercase
        for (char *p = token; *p; p++) *p = tolower(*p);

        if (strcmp(token, "mod") == 0) {
            mods |= modkey;
        } else if (strcmp(token, "super") == 0 || strcmp(token, "logo") == 0 ||
                   strcmp(token, "mod4") == 0 || strcmp(token, "win") == 0) {
            mods |= WLR_MODIFIER_LOGO;
        } else if (strcmp(token, "shift") == 0) {
            mods |= WLR_MODIFIER_SHIFT;
        } else if (strcmp(token, "ctrl") == 0 || strcmp(token, "control") == 0) {
            mods |= WLR_MODIFIER_CTRL;
        } else if (strcmp(token, "alt") == 0 || strcmp(token, "mod1") == 0) {
            mods |= WLR_MODIFIER_ALT;
        }
        // Unrecognized tokens are assumed to be the key name

        token = strtok(NULL, "+");
    }

    free(copy);
    return mods;
}

// Extract the key name from a binding string like "mod+shift+Return"
static const char *extract_keyname(const char *str)
{
    if (!str) return NULL;

    // Find the last '+' - everything after is the key name
    const char *last_plus = strrchr(str, '+');
    if (last_plus)
        return last_plus + 1;
    return str;
}

// Parse a keysym from a string
static xkb_keysym_t parse_keysym(const char *str)
{
    if (!str) return XKB_KEY_NoSymbol;

    // Try XKB lookup
    xkb_keysym_t sym = xkb_keysym_from_name(str, XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym != XKB_KEY_NoSymbol) return sym;

    // Handle common aliases
    if (strcasecmp(str, "enter") == 0) return XKB_KEY_Return;
    if (strcasecmp(str, "esc") == 0) return XKB_KEY_Escape;
    if (strcasecmp(str, "del") == 0) return XKB_KEY_Delete;
    if (strcasecmp(str, "backspace") == 0) return XKB_KEY_BackSpace;

    return XKB_KEY_NoSymbol;
}

// Parse a button name
static uint32_t parse_button(const char *str)
{
    if (!str) return 0;

    if (strcasecmp(str, "left") == 0) return BTN_LEFT;
    if (strcasecmp(str, "middle") == 0) return BTN_MIDDLE;
    if (strcasecmp(str, "right") == 0) return BTN_RIGHT;
    if (strcasecmp(str, "side") == 0) return BTN_SIDE;
    if (strcasecmp(str, "extra") == 0) return BTN_EXTRA;

    return 0;
}

// Load keybindings from config
// Format: keybindings."mod+key" = "action" or "action:argument"
static void load_keybindings_from_config(SwlKeybindingManager *mgr)
{
    SwlConfig *cfg = swl_compositor_get_config(mgr->comp);
    if (!cfg)
        return;

    // Get modkey setting
    const char *modkey_str = swl_config_get_string(cfg, "general.modkey", "alt");
    uint32_t modkey = WLR_MODIFIER_ALT;
    if (strcasecmp(modkey_str, "super") == 0 || strcasecmp(modkey_str, "logo") == 0)
        modkey = WLR_MODIFIER_LOGO;
    else if (strcasecmp(modkey_str, "ctrl") == 0)
        modkey = WLR_MODIFIER_CTRL;

    // Get all keybinding keys
    size_t count = 0;
    const char **keys = swl_config_keys(cfg, "keybindings.", &count);
    if (!keys || count == 0)
        return;

    // Process each keybinding
    // Format: keybindings."mod+shift+Return" = "action:argument"
    for (size_t i = 0; i < count; i++) {
        const char *key = keys[i];

        // Extract binding key from config key (e.g., "mod+p" from "keybindings.mod+p")
        const char *binding_key = key + strlen("keybindings.");

        // Get value (action:argument format)
        const char *value = swl_config_get_string(cfg, key, NULL);
        if (!value)
            continue;

        // Parse action and argument from value
        char *action = strdup(value);
        if (!action)
            continue;

        char *arg_str = NULL;
        char *colon = strchr(action, ':');
        if (colon) {
            *colon = '\0';
            arg_str = colon + 1;
        }

        // Parse modifiers and keysym
        uint32_t mods = parse_modifiers(binding_key, modkey);
        const char *keyname = extract_keyname(binding_key);
        xkb_keysym_t keysym = parse_keysym(keyname);

        if (keysym != XKB_KEY_NoSymbol) {
            SwlKeybinding binding = {
                .modifiers = mods,
                .keysym = keysym,
                .action = action,
                .argument = arg_str,
            };
            swl_keybinding_add(mgr, &binding);
        } else {
            free(action);
        }
    }

    swl_config_keys_free(keys, count);
}

// Load button bindings from config
// Format: buttons."mod+button" = "action" or "action:argument"
static void load_buttons_from_config(SwlKeybindingManager *mgr)
{
    SwlConfig *cfg = swl_compositor_get_config(mgr->comp);
    if (!cfg)
        return;

    // Get modkey setting
    const char *modkey_str = swl_config_get_string(cfg, "general.modkey", "alt");
    uint32_t modkey = WLR_MODIFIER_ALT;
    if (strcasecmp(modkey_str, "super") == 0 || strcasecmp(modkey_str, "logo") == 0)
        modkey = WLR_MODIFIER_LOGO;
    else if (strcasecmp(modkey_str, "ctrl") == 0)
        modkey = WLR_MODIFIER_CTRL;

    // Get all button binding keys
    size_t count = 0;
    const char **keys = swl_config_keys(cfg, "buttons.", &count);
    if (!keys || count == 0)
        return;

    for (size_t i = 0; i < count; i++) {
        const char *key = keys[i];

        const char *binding_key = key + strlen("buttons.");

        const char *value = swl_config_get_string(cfg, key, NULL);
        if (!value)
            continue;

        char *action = strdup(value);
        if (!action)
            continue;

        char *arg_str = NULL;
        char *colon = strchr(action, ':');
        if (colon) {
            *colon = '\0';
            arg_str = colon + 1;
        }

        uint32_t mods = parse_modifiers(binding_key, modkey);
        const char *buttonname = extract_keyname(binding_key);
        uint32_t button = parse_button(buttonname);

        if (button != 0) {
            SwlButtonBinding binding = {
                .modifiers = mods,
                .button = button,
                .action = action,
                .argument = arg_str,
            };
            swl_button_binding_add(mgr, &binding);
        } else {
            free(action);
        }
    }

    swl_config_keys_free(keys, count);
}

void swl_action_register_builtins(SwlKeybindingManager *mgr)
{
    // Register all action handlers
    swl_action_register(mgr, "quit", action_quit);
    swl_action_register(mgr, "spawn", action_spawn);
    swl_action_register(mgr, "close", action_close);
    swl_action_register(mgr, "killclient", action_close);  // Alias
    swl_action_register(mgr, "focus-next", action_focus_next);
    swl_action_register(mgr, "focus-prev", action_focus_prev);
    swl_action_register(mgr, "focusstack", action_focus_next);  // Alias with direction
    swl_action_register(mgr, "toggle-floating", action_toggle_floating);
    swl_action_register(mgr, "togglefloating", action_toggle_floating);  // Alias
    swl_action_register(mgr, "toggle-fullscreen", action_toggle_fullscreen);
    swl_action_register(mgr, "togglefullscreen", action_toggle_fullscreen);  // Alias
    swl_action_register(mgr, "setlayout", action_set_layout);
    swl_action_register(mgr, "set-layout", action_set_layout);
    swl_action_register(mgr, "focus-monitor", action_focus_monitor);
    swl_action_register(mgr, "focusmon", action_focus_monitor);  // Alias
    swl_action_register(mgr, "send-monitor", action_send_monitor);
    swl_action_register(mgr, "sendmon", action_send_monitor);  // Alias
    swl_action_register(mgr, "tag-monitor", action_send_monitor);  // Compat alias
    swl_action_register(mgr, "tagmon", action_send_monitor);  // Compat alias
    swl_action_register(mgr, "reload-config", action_reload_config);
    swl_action_register(mgr, "reload_config", action_reload_config);  // Alias
    swl_action_register(mgr, "zoom", action_zoom);
    swl_action_register(mgr, "incnmaster", action_inc_nmaster);
    swl_action_register(mgr, "inc-nmaster", action_inc_nmaster);
    swl_action_register(mgr, "dec-nmaster", action_dec_nmaster);
    swl_action_register(mgr, "setmfact", action_inc_mfact);  // Uses delta from arg
    swl_action_register(mgr, "inc-mfact", action_inc_mfact);
    swl_action_register(mgr, "dec-mfact", action_dec_mfact);
    swl_action_register(mgr, "focusdir", action_focusdir);
    swl_action_register(mgr, "moveresize", action_moveresize);
    swl_action_register(mgr, "cycle-ratio", action_cycle_ratio);
    swl_action_register(mgr, "chvt", action_chvt);

    // Try to load keybindings from config
    SwlConfig *cfg = swl_compositor_get_config(mgr->comp);
    bool has_config_keybindings = false;

    if (cfg) {
        size_t count = 0;
        const char **keys = swl_config_keys(cfg, "keybindings.", &count);
        if (keys && count > 0) {
            has_config_keybindings = true;
            swl_config_keys_free(keys, count);
        }
    }

    if (has_config_keybindings) {
        // Load keybindings from config
        load_keybindings_from_config(mgr);
        load_buttons_from_config(mgr);
        goto hardcoded_chvt;
    }

    // Default keybindings (Mod = Alt)
    #define MOD WLR_MODIFIER_ALT
    #define SHIFT WLR_MODIFIER_SHIFT

    swl_keybinding_add(mgr, &(SwlKeybinding){MOD | SHIFT, XKB_KEY_q, "quit", NULL});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD | SHIFT, XKB_KEY_Return, "spawn", "foot"});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_Return, "spawn", "foot"});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD | SHIFT, XKB_KEY_c, "close", NULL});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_j, "focus-next", NULL});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_k, "focus-prev", NULL});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_space, "toggle-floating", NULL});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_f, "toggle-fullscreen", NULL});
    // Layout keybindings
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_s, "set-layout", "scroller"});

    // Zoom (swap with master)
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_z, "zoom", NULL});

    // Master factor adjustment
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_l, "inc-mfact", NULL});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_h, "dec-mfact", NULL});

    // Master count adjustment
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_i, "inc-nmaster", NULL});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_d, "dec-nmaster", NULL});

    // Monitor focus/move (left = -1, right = 1)
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_comma, "focus-monitor", "-1"});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_period, "focus-monitor", "1"});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD | SHIFT, XKB_KEY_comma, "send-monitor", "-1"});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD | SHIFT, XKB_KEY_period, "send-monitor", "1"});

    // Directional focus (arrow keys)
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_Up, "focusdir", "up"});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_Down, "focusdir", "down"});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_Left, "focusdir", "left"});
    swl_keybinding_add(mgr, &(SwlKeybinding){MOD, XKB_KEY_Right, "focusdir", "right"});

    // Default button bindings (Mod+click = move/resize)
    swl_button_binding_add(mgr, &(SwlButtonBinding){MOD, BTN_LEFT, "moveresize", "move"});
    swl_button_binding_add(mgr, &(SwlButtonBinding){MOD, BTN_MIDDLE, "toggle-floating", NULL});
    swl_button_binding_add(mgr, &(SwlButtonBinding){MOD, BTN_RIGHT, "moveresize", "resize"});

    #undef MOD
    #undef SHIFT

hardcoded_chvt:
    // Hardcoded VT switching keybindings (Ctrl+Alt+F1-F12)
    // These are always registered regardless of config
    #define CHVT_MODS (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT)
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_1, "chvt", "1"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_2, "chvt", "2"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_3, "chvt", "3"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_4, "chvt", "4"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_5, "chvt", "5"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_6, "chvt", "6"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_7, "chvt", "7"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_8, "chvt", "8"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_9, "chvt", "9"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_10, "chvt", "10"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_11, "chvt", "11"});
    swl_keybinding_add(mgr, &(SwlKeybinding){CHVT_MODS, XKB_KEY_XF86Switch_VT_12, "chvt", "12"});
    #undef CHVT_MODS
}
