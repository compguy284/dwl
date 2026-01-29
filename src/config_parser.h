/*
 * config_parser.h - Runtime configuration parser
 * See LICENSE file for copyright and license details.
 */
#ifndef CONFIG_PARSER_H
#define CONFIG_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include <libinput.h>
#include <scenefx/types/fx/blur_data.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <xkbcommon/xkbcommon.h>

/* Forward declarations - full definitions in dwl.h */
typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct Monitor Monitor;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	Arg arg;
} CfgKey;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	Arg arg;
} CfgButton;

typedef struct {
	char *id;
	char *title;
	int isfloating;
	int monitor;
} CfgRule;

typedef struct {
	char *name;
	float mfact;
	int nmaster;
	float scale;
	int layout_idx;
	enum wl_output_transform rr;
	int x, y;
} CfgMonitorRule;

/* String pool entry for lifetime management */
typedef struct StringPoolEntry {
	char *str;
	struct StringPoolEntry *next;
} StringPoolEntry;

typedef struct {
	/* Appearance */
	unsigned int borderpx;
	int sloppyfocus;
	int bypass_surface_visibility;
	int smartgaps;
	int monoclegaps;
	unsigned int gappih;
	unsigned int gappiv;
	unsigned int gappoh;
	unsigned int gappov;

	/* Colors */
	float rootcolor[4];
	float bordercolor[4];
	float focuscolor[4];
	float urgentcolor[4];
	float fullscreen_bg[4];

	/* Opacity */
	int opacity;
	float opacity_inactive;
	float opacity_active;

	/* Shadows */
	int shadow;
	int shadow_only_floating;
	float shadow_color[4];
	float shadow_color_focus[4];
	int shadow_blur_sigma;
	int shadow_blur_sigma_focus;
	char **shadow_ignore_list;
	size_t shadow_ignore_count;

	/* Corner radius */
	int corner_radius;
	int corner_radius_inner;
	int corner_radius_only_floating;

	/* Blur */
	int blur;
	int blur_xray;
	int blur_ignore_transparent;
	struct blur_data blur_data;

	/* Logging */
	int log_level;

	/* Scroller */
	int scroller_center_mode;
	float *scroller_proportions;
	size_t scroller_proportions_count;
	int scroller_default_proportion;

	/* Rules */
	CfgRule *rules;
	size_t rules_count;

	/* Layouts (fixed array, pointer-stable across reloads) */
	Layout layouts[4];
	size_t layouts_count;

	/* Monitor rules */
	CfgMonitorRule *monrules;
	size_t monrules_count;

	/* Keyboard */
	char *xkb_rules;
	char *xkb_model;
	char *xkb_layout;
	char *xkb_variant;
	char *xkb_options;
	int repeat_rate;
	int repeat_delay;
	int numlock;

	/* Trackpad */
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

	/* Modifier key */
	uint32_t modkey;

	/* Commands */
	char **termcmd;
	size_t termcmd_count;
	char **menucmd;
	size_t menucmd_count;

	/* Keybindings */
	CfgKey *keys;
	size_t keys_count;

	/* Mouse buttons */
	CfgButton *buttons;
	size_t buttons_count;

	/* String pool for lifetime management */
	StringPoolEntry *string_pool;
} Config;

extern Config cfg;

/* Initialize config with defaults, then parse config file */
void config_init(void);

/* Parse config file, return 0 on success, -1 on error */
int config_load(void);

/* SIGHUP handler: re-parse config file and apply to running compositor */
int config_reload_handler(int signal_number, void *data);

/* Free all dynamically allocated config resources */
void config_free(void);

/* Look up a bindable function by name, returns function pointer or NULL */
void (*lookup_func(const char *name))(const Arg *);

/* Reverse lookup: function pointer to name string, returns NULL if not found */
const char *lookup_func_name(void (*func)(const Arg *));

/* Set a single config value at runtime (key/value strings), returns 0 on success */
int config_set_value(const char *key, const char *value);

#endif /* CONFIG_PARSER_H */
