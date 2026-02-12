/*
 * config_parser.h - TOML configuration parser for dwl
 */
#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>
#include <libinput.h>

#ifdef SCENEFX
#include <scenefx/types/fx/blur_data.h>
#endif

/* Arg union - must match definition in dwl.c */
typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} CfgArg;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const CfgArg *);
	CfgArg arg;
} KeyBinding;

typedef struct {
	uint32_t mod;
	uint32_t button;
	void (*func)(const CfgArg *);
	CfgArg arg;
} ButtonBinding;

typedef struct {
	char *id;
	char *title;
	uint32_t tags;
	int isfloating;
	int monitor;
} ConfigRule;

typedef struct {
	char *name;
	float mfact;
	int nmaster;
	float scale;
	int layout_index;
	int transform;  /* wl_output_transform */
	int x, y;
} ConfigMonitorRule;

/* Main configuration structure */
typedef struct {
	/* General */
	int tag_count;
	uint32_t modkey;  /* Default modifier key for "mod" alias */

	/* Appearance */
	int sloppyfocus;
	int bypass_surface_visibility;
	int smartgaps;
	int monoclegaps;
	unsigned int borderpx;
	unsigned int gappih;
	unsigned int gappiv;
	unsigned int gappoh;
	unsigned int gappov;
	float scroller_ratio;

	/* Colors (RGBA float arrays) */
	float rootcolor[4];
	float bordercolor[4];
	float focuscolor[4];
	float urgentcolor[4];
	float fullscreen_bg[4];

#ifdef SCENEFX
	/* SceneFX - Rounded corners */
	int corner_radius;
	int corner_radius_inner;
	int corner_floating_only;

	/* SceneFX - Shadows */
	int shadow;
	int shadow_floating_only;
	float shadow_color[4];
	float shadow_color_focus[4];
	int shadow_blur_sigma;
	int shadow_blur_sigma_focus;

	/* SceneFX - Opacity */
	int opacity;
	float opacity_active;
	float opacity_inactive;

	/* SceneFX - Blur */
	int blur;
	int blur_optimized;
	int blur_ignore_transparent;
	struct blur_data blur_data;
#endif

	/* Keyboard */
	int numlock;
	int repeat_rate;
	int repeat_delay;

	/* XKB */
	char *xkb_rules;
	char *xkb_model;
	char *xkb_layout;
	char *xkb_variant;
	char *xkb_options;

	/* Pointer/Trackpad */
	int tap_to_click;
	int tap_and_drag;
	int drag_lock;
	int natural_scrolling;
	int disable_while_typing;
	int left_handed;
	int middle_button_emulation;
	enum libinput_config_scroll_method scroll_method;
	enum libinput_config_click_method click_method;
	uint32_t send_events_mode;
	enum libinput_config_accel_profile accel_profile;
	double accel_speed;
	enum libinput_config_tap_button_map button_map;

	/* Logging */
	int log_level;

	/* Rules */
	ConfigRule *rules;
	size_t rules_count;

	/* Monitor rules */
	ConfigMonitorRule *monrules;
	size_t monrules_count;

	/* Keybindings */
	KeyBinding *keys;
	size_t keys_count;
	int keys_from_config;  /* 1 if [keybindings] section was present */

	/* Button bindings */
	ButtonBinding *buttons;
	size_t buttons_count;
	int buttons_from_config;  /* 1 if [buttons] section was present */

	/* Config file path (for reload) */
	char *config_path;
} DwlConfig;

/* Global config instance */
extern DwlConfig cfg;

/* Action function type - uses CfgArg which is compatible with Arg */
typedef void (*ActionFunc)(const CfgArg *);

/* Action entry for lookup table */
typedef struct {
	const char *name;
	ActionFunc func;
} ActionEntry;

/* Function declarations */

/**
 * Initialize config with default values.
 * Called before loading any config file.
 */
void config_set_defaults(void);

/**
 * Find the config file path.
 * Search order:
 *   1. $XDG_CONFIG_HOME/dwl/config.toml
 *   2. ~/.config/dwl/config.toml
 *   3. /etc/dwl/config.toml
 *
 * @return Allocated string with path, or NULL if not found.
 *         Caller must free.
 */
char *config_find_path(void);

/**
 * Load configuration from file.
 *
 * @param path Path to config file, or NULL to search default locations.
 * @return 0 on success, -1 on error.
 */
int config_load(const char *path);

/**
 * Reload configuration from the previously loaded file.
 * Only reloads values that can be changed at runtime.
 *
 * @return 0 on success, -1 on error.
 */
int config_reload(void);

/**
 * Apply reloaded configuration to running compositor.
 * Updates monitors, clients, and scene.
 */
void config_apply(void);

/**
 * Free all dynamically allocated config data.
 */
void config_free(void);

/**
 * Parse a hex color string into RGBA float array.
 * Supports formats: "#RRGGBB", "#RRGGBBAA", "RRGGBB", "RRGGBBAA"
 *
 * @param str Color string.
 * @param out Output array of 4 floats (RGBA).
 * @return 0 on success, -1 on error.
 */
int parse_color(const char *str, float *out);

/**
 * Parse modifier string to flags.
 * Examples: "super", "super+shift", "ctrl+alt"
 *
 * @param str Modifier string.
 * @return Modifier flags (WLR_MODIFIER_*), or 0 on error.
 */
uint32_t parse_modifiers(const char *str);

/**
 * Parse key name to XKB keysym.
 *
 * @param str Key name (e.g., "Return", "a", "F1").
 * @return XKB keysym, or XKB_KEY_NoSymbol on error.
 */
xkb_keysym_t parse_keysym(const char *str);

/**
 * Parse button name to button code.
 *
 * @param str Button name ("left", "middle", "right").
 * @return Button code (BTN_*), or 0 on error.
 */
uint32_t parse_button(const char *str);

/**
 * Lookup action function by name.
 *
 * @param name Action name.
 * @return Function pointer, or NULL if not found.
 */
ActionFunc parse_action(const char *name);

/**
 * External action lookup - implemented in dwl.c where it has access
 * to all static action functions.
 *
 * @param name Action name.
 * @return Function pointer, or NULL if not found.
 */
ActionFunc action_lookup(const char *name);

#endif /* CONFIG_PARSER_H */
