/*
 * config_parser.c - TOML configuration parser for dwl
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/util/log.h>
#include <wayland-server-core.h>

#include "config_parser.h"
#include "toml.h"
#include "util.h"

/* Global config instance */
DwlConfig cfg;

/* Layout struct - must match dwl.c definition */
typedef struct {
	const char *symbol;
	void (*arrange)(void *);
} Layout;
extern const Layout layouts[];

/* Direction constants - must match dwl.c */
enum { UP, DOWN, LEFT, RIGHT };

/* Cursor mode constants for moveresize */
enum { CurNormal, CurPressed, CurMove, CurResize };

/* Action lookup function - implemented in dwl.c */
extern ActionFunc action_lookup(const char *name);

/* Helper macros */
#define COLOR_DEFAULT(hex) { ((hex >> 24) & 0xFF) / 255.0f, \
                             ((hex >> 16) & 0xFF) / 255.0f, \
                             ((hex >> 8) & 0xFF) / 255.0f, \
                             (hex & 0xFF) / 255.0f }

static char *xstrdup(const char *s)
{
	if (!s) return NULL;
	char *r = strdup(s);
	if (!r) die("strdup:");
	return r;
}

void config_set_defaults(void)
{
	memset(&cfg, 0, sizeof(cfg));

	/* General */
	cfg.tag_count = 9;
	cfg.modkey = WLR_MODIFIER_ALT;  /* Default: Alt key (matches dwl default) */

	/* Appearance */
	cfg.sloppyfocus = 1;
	cfg.bypass_surface_visibility = 0;
	cfg.smartgaps = 0;
	cfg.monoclegaps = 0;
	cfg.borderpx = 1;
	cfg.gappih = 10;
	cfg.gappiv = 10;
	cfg.gappoh = 10;
	cfg.gappov = 10;
	cfg.scroller_ratio = 0.8f;

	/* Colors */
	float root[] = COLOR_DEFAULT(0x222222ff);
	float border[] = COLOR_DEFAULT(0x444444ff);
	float focus[] = COLOR_DEFAULT(0x005577ff);
	float urgent[] = COLOR_DEFAULT(0xff0000ff);
	float fs_bg[] = {0.0f, 0.0f, 0.0f, 1.0f};

	memcpy(cfg.rootcolor, root, sizeof(root));
	memcpy(cfg.bordercolor, border, sizeof(border));
	memcpy(cfg.focuscolor, focus, sizeof(focus));
	memcpy(cfg.urgentcolor, urgent, sizeof(urgent));
	memcpy(cfg.fullscreen_bg, fs_bg, sizeof(fs_bg));

#ifdef SCENEFX
	/* SceneFX defaults */
	cfg.corner_radius = 10;
	cfg.corner_radius_inner = 10;
	cfg.corner_floating_only = 0;

	cfg.shadow = 1;
	cfg.shadow_floating_only = 0;
	float shadow_col[] = COLOR_DEFAULT(0x00000088);
	float shadow_col_focus[] = COLOR_DEFAULT(0x000000aa);
	memcpy(cfg.shadow_color, shadow_col, sizeof(shadow_col));
	memcpy(cfg.shadow_color_focus, shadow_col_focus, sizeof(shadow_col_focus));
	cfg.shadow_blur_sigma = 20;
	cfg.shadow_blur_sigma_focus = 30;

	cfg.opacity = 0;
	cfg.opacity_active = 1.0f;
	cfg.opacity_inactive = 0.8f;

	cfg.blur = 1;
	cfg.blur_optimized = 1;
	cfg.blur_ignore_transparent = 1;
	cfg.blur_data.num_passes = 3;
	cfg.blur_data.radius = 5;
	cfg.blur_data.noise = 0.02f;
	cfg.blur_data.brightness = 0.9f;
	cfg.blur_data.contrast = 0.9f;
	cfg.blur_data.saturation = 1.1f;
#endif

	/* Keyboard */
	cfg.numlock = 1;
	cfg.repeat_rate = 25;
	cfg.repeat_delay = 600;

	/* XKB - all NULL means use system defaults */
	cfg.xkb_rules = NULL;
	cfg.xkb_model = NULL;
	cfg.xkb_layout = NULL;
	cfg.xkb_variant = NULL;
	cfg.xkb_options = NULL;

	/* Pointer/Trackpad */
	cfg.tap_to_click = 1;
	cfg.tap_and_drag = 1;
	cfg.drag_lock = 1;
	cfg.natural_scrolling = 0;
	cfg.disable_while_typing = 1;
	cfg.left_handed = 0;
	cfg.middle_button_emulation = 0;
	cfg.scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
	cfg.click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
	cfg.send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
	cfg.accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
	cfg.accel_speed = 0.0;
	cfg.button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

	/* Logging */
	cfg.log_level = WLR_ERROR;

	/* Empty arrays - will be populated from config or use defaults */
	cfg.rules = NULL;
	cfg.rules_count = 0;
	cfg.monrules = NULL;
	cfg.monrules_count = 0;
	cfg.keys = NULL;
	cfg.keys_count = 0;
	cfg.keys_from_config = 0;
	cfg.buttons = NULL;
	cfg.buttons_count = 0;
	cfg.buttons_from_config = 0;
	cfg.config_path = NULL;
}

int parse_color(const char *str, float *out)
{
	if (!str || !out) return -1;

	/* Skip leading # if present */
	if (*str == '#') str++;

	size_t len = strlen(str);
	uint32_t hex;
	int r, g, b, a = 255;

	if (len == 6) {
		if (sscanf(str, "%06x", &hex) != 1) return -1;
		r = (hex >> 16) & 0xFF;
		g = (hex >> 8) & 0xFF;
		b = hex & 0xFF;
	} else if (len == 8) {
		if (sscanf(str, "%08x", &hex) != 1) return -1;
		r = (hex >> 24) & 0xFF;
		g = (hex >> 16) & 0xFF;
		b = (hex >> 8) & 0xFF;
		a = hex & 0xFF;
	} else {
		return -1;
	}

	out[0] = r / 255.0f;
	out[1] = g / 255.0f;
	out[2] = b / 255.0f;
	out[3] = a / 255.0f;
	return 0;
}

uint32_t parse_modifiers(const char *str)
{
	if (!str) return 0;

	uint32_t mods = 0;
	char *copy = xstrdup(str);
	char *token = strtok(copy, "+");

	while (token) {
		/* Skip leading/trailing whitespace */
		while (*token && isspace(*token)) token++;
		char *end = token + strlen(token) - 1;
		while (end > token && isspace(*end)) *end-- = '\0';

		/* Convert to lowercase for comparison */
		for (char *p = token; *p; p++) *p = tolower(*p);

		if (strcmp(token, "mod") == 0) {
			/* "mod" is an alias for the configured modkey */
			mods |= cfg.modkey;
		} else if (strcmp(token, "super") == 0 || strcmp(token, "mod4") == 0 ||
		    strcmp(token, "logo") == 0 || strcmp(token, "win") == 0) {
			mods |= WLR_MODIFIER_LOGO;
		} else if (strcmp(token, "shift") == 0) {
			mods |= WLR_MODIFIER_SHIFT;
		} else if (strcmp(token, "ctrl") == 0 || strcmp(token, "control") == 0) {
			mods |= WLR_MODIFIER_CTRL;
		} else if (strcmp(token, "alt") == 0 || strcmp(token, "mod1") == 0) {
			mods |= WLR_MODIFIER_ALT;
		} else if (strcmp(token, "mod2") == 0) {
			mods |= WLR_MODIFIER_MOD2;
		} else if (strcmp(token, "mod3") == 0) {
			mods |= WLR_MODIFIER_MOD3;
		} else if (strcmp(token, "mod5") == 0) {
			mods |= WLR_MODIFIER_MOD5;
		}
		/* Ignore unrecognized modifiers - they might be the key name */

		token = strtok(NULL, "+");
	}

	free(copy);
	return mods;
}

xkb_keysym_t parse_keysym(const char *str)
{
	if (!str) return XKB_KEY_NoSymbol;

	/* Try XKB lookup first */
	xkb_keysym_t sym = xkb_keysym_from_name(str, XKB_KEYSYM_CASE_INSENSITIVE);
	if (sym != XKB_KEY_NoSymbol) return sym;

	/* Handle common aliases */
	if (strcasecmp(str, "enter") == 0) return XKB_KEY_Return;
	if (strcasecmp(str, "esc") == 0) return XKB_KEY_Escape;
	if (strcasecmp(str, "del") == 0) return XKB_KEY_Delete;
	if (strcasecmp(str, "backspace") == 0) return XKB_KEY_BackSpace;

	return XKB_KEY_NoSymbol;
}

uint32_t parse_button(const char *str)
{
	if (!str) return 0;

	if (strcasecmp(str, "left") == 0 || strcasecmp(str, "btn_left") == 0)
		return BTN_LEFT;
	if (strcasecmp(str, "middle") == 0 || strcasecmp(str, "btn_middle") == 0)
		return BTN_MIDDLE;
	if (strcasecmp(str, "right") == 0 || strcasecmp(str, "btn_right") == 0)
		return BTN_RIGHT;
	if (strcasecmp(str, "side") == 0 || strcasecmp(str, "btn_side") == 0)
		return BTN_SIDE;
	if (strcasecmp(str, "extra") == 0 || strcasecmp(str, "btn_extra") == 0)
		return BTN_EXTRA;

	return 0;
}

ActionFunc parse_action(const char *name)
{
	if (!name) return NULL;
	return action_lookup(name);
}

char *config_find_path(void)
{
	char path[PATH_MAX];
	struct stat st;

	/* 1. $XDG_CONFIG_HOME/dwl/config.toml */
	const char *xdg = getenv("XDG_CONFIG_HOME");
	if (xdg && *xdg) {
		snprintf(path, sizeof(path), "%s/dwl/config.toml", xdg);
		if (stat(path, &st) == 0)
			return xstrdup(path);
	}

	/* 2. ~/.config/dwl/config.toml */
	const char *home = getenv("HOME");
	if (home && *home) {
		snprintf(path, sizeof(path), "%s/.config/dwl/config.toml", home);
		if (stat(path, &st) == 0)
			return xstrdup(path);
	}

	/* 3. /etc/dwl/config.toml */
	if (stat("/etc/dwl/config.toml", &st) == 0)
		return xstrdup("/etc/dwl/config.toml");

	return NULL;
}

/* Parse helper: get optional string */
static char *toml_string_or_default(toml_table_t *table, const char *key, const char *def)
{
	toml_datum_t d = toml_string_in(table, key);
	if (d.ok) return d.u.s; /* caller must free */
	return def ? xstrdup(def) : NULL;
}

/* Parse helper: get optional int */
static int toml_int_or_default(toml_table_t *table, const char *key, int def)
{
	toml_datum_t d = toml_int_in(table, key);
	return d.ok ? (int)d.u.i : def;
}

/* Parse helper: get optional bool */
static int toml_bool_or_default(toml_table_t *table, const char *key, int def)
{
	toml_datum_t d = toml_bool_in(table, key);
	return d.ok ? d.u.b : def;
}

/* Parse helper: get optional double */
static double toml_double_or_default(toml_table_t *table, const char *key, double def)
{
	toml_datum_t d = toml_double_in(table, key);
	return d.ok ? d.u.d : def;
}

/* Parse a single modifier name to flags */
static uint32_t parse_single_modifier(const char *name)
{
	if (!name) return 0;
	if (strcasecmp(name, "super") == 0 || strcasecmp(name, "mod4") == 0 ||
	    strcasecmp(name, "logo") == 0 || strcasecmp(name, "win") == 0) {
		return WLR_MODIFIER_LOGO;
	} else if (strcasecmp(name, "alt") == 0 || strcasecmp(name, "mod1") == 0) {
		return WLR_MODIFIER_ALT;
	} else if (strcasecmp(name, "ctrl") == 0 || strcasecmp(name, "control") == 0) {
		return WLR_MODIFIER_CTRL;
	} else if (strcasecmp(name, "shift") == 0) {
		return WLR_MODIFIER_SHIFT;
	}
	return WLR_MODIFIER_LOGO;  /* Default to super */
}

/* Parse [general] section */
static void parse_general_section(toml_table_t *general)
{
	toml_datum_t d;
	if (!general) return;
	cfg.tag_count = toml_int_or_default(general, "tag_count", cfg.tag_count);

	/* Parse modkey */
	d = toml_string_in(general, "modkey");
	if (d.ok) {
		cfg.modkey = parse_single_modifier(d.u.s);
		free(d.u.s);
	}
}

/* Parse [appearance] section */
static void parse_appearance_section(toml_table_t *appearance)
{
	if (!appearance) return;

	cfg.sloppyfocus = toml_bool_or_default(appearance, "sloppyfocus", cfg.sloppyfocus);
	cfg.bypass_surface_visibility = toml_bool_or_default(appearance, "bypass_surface_visibility", cfg.bypass_surface_visibility);
	cfg.smartgaps = toml_bool_or_default(appearance, "smartgaps", cfg.smartgaps);
	cfg.monoclegaps = toml_bool_or_default(appearance, "monoclegaps", cfg.monoclegaps);
	cfg.borderpx = toml_int_or_default(appearance, "border_width", cfg.borderpx);
	cfg.gappih = toml_int_or_default(appearance, "gap_inner_h", cfg.gappih);
	cfg.gappiv = toml_int_or_default(appearance, "gap_inner_v", cfg.gappiv);
	cfg.gappoh = toml_int_or_default(appearance, "gap_outer_h", cfg.gappoh);
	cfg.gappov = toml_int_or_default(appearance, "gap_outer_v", cfg.gappov);
	cfg.scroller_ratio = (float)toml_double_or_default(appearance, "scroller_ratio", cfg.scroller_ratio);

	/* Parse [appearance.colors] sub-table */
	toml_table_t *colors = toml_table_in(appearance, "colors");
	if (colors) {
		toml_datum_t d;

		d = toml_string_in(colors, "root");
		if (d.ok) { parse_color(d.u.s, cfg.rootcolor); free(d.u.s); }

		d = toml_string_in(colors, "border");
		if (d.ok) { parse_color(d.u.s, cfg.bordercolor); free(d.u.s); }

		d = toml_string_in(colors, "focus");
		if (d.ok) { parse_color(d.u.s, cfg.focuscolor); free(d.u.s); }

		d = toml_string_in(colors, "urgent");
		if (d.ok) { parse_color(d.u.s, cfg.urgentcolor); free(d.u.s); }

		d = toml_string_in(colors, "fullscreen_bg");
		if (d.ok) { parse_color(d.u.s, cfg.fullscreen_bg); free(d.u.s); }
	}
}

#ifdef SCENEFX
/* Parse [scenefx] section */
static void parse_scenefx_section(toml_table_t *scenefx)
{
	if (!scenefx) return;

	/* Corners */
	toml_table_t *corners = toml_table_in(scenefx, "corners");
	if (corners) {
		cfg.corner_radius = toml_int_or_default(corners, "radius", cfg.corner_radius);
		cfg.corner_radius_inner = toml_int_or_default(corners, "radius_inner", cfg.corner_radius_inner);
		cfg.corner_floating_only = toml_bool_or_default(corners, "floating_only", cfg.corner_floating_only);
	}

	/* Shadows */
	toml_table_t *shadows = toml_table_in(scenefx, "shadows");
	if (shadows) {
		cfg.shadow = toml_bool_or_default(shadows, "enabled", cfg.shadow);
		cfg.shadow_floating_only = toml_bool_or_default(shadows, "floating_only", cfg.shadow_floating_only);
		cfg.shadow_blur_sigma = toml_int_or_default(shadows, "blur_sigma", cfg.shadow_blur_sigma);
		cfg.shadow_blur_sigma_focus = toml_int_or_default(shadows, "blur_sigma_focus", cfg.shadow_blur_sigma_focus);

		toml_datum_t d;
		d = toml_string_in(shadows, "color");
		if (d.ok) { parse_color(d.u.s, cfg.shadow_color); free(d.u.s); }
		d = toml_string_in(shadows, "color_focus");
		if (d.ok) { parse_color(d.u.s, cfg.shadow_color_focus); free(d.u.s); }
	}

	/* Opacity */
	toml_table_t *opacity = toml_table_in(scenefx, "opacity");
	if (opacity) {
		cfg.opacity = toml_bool_or_default(opacity, "enabled", cfg.opacity);
		cfg.opacity_active = (float)toml_double_or_default(opacity, "active", cfg.opacity_active);
		cfg.opacity_inactive = (float)toml_double_or_default(opacity, "inactive", cfg.opacity_inactive);
	}

	/* Blur */
	toml_table_t *blur = toml_table_in(scenefx, "blur");
	if (blur) {
		cfg.blur = toml_bool_or_default(blur, "enabled", cfg.blur);
		cfg.blur_optimized = toml_bool_or_default(blur, "optimized", cfg.blur_optimized);
		cfg.blur_ignore_transparent = toml_bool_or_default(blur, "ignore_transparent", cfg.blur_ignore_transparent);
		cfg.blur_data.num_passes = toml_int_or_default(blur, "passes", cfg.blur_data.num_passes);
		cfg.blur_data.radius = toml_int_or_default(blur, "radius", cfg.blur_data.radius);
		cfg.blur_data.noise = (float)toml_double_or_default(blur, "noise", cfg.blur_data.noise);
		cfg.blur_data.brightness = (float)toml_double_or_default(blur, "brightness", cfg.blur_data.brightness);
		cfg.blur_data.contrast = (float)toml_double_or_default(blur, "contrast", cfg.blur_data.contrast);
		cfg.blur_data.saturation = (float)toml_double_or_default(blur, "saturation", cfg.blur_data.saturation);
	}
}
#endif

/* Parse [keyboard] section */
static void parse_keyboard_section(toml_table_t *keyboard)
{
	if (!keyboard) return;

	cfg.numlock = toml_bool_or_default(keyboard, "numlock", cfg.numlock);
	cfg.repeat_rate = toml_int_or_default(keyboard, "repeat_rate", cfg.repeat_rate);
	cfg.repeat_delay = toml_int_or_default(keyboard, "repeat_delay", cfg.repeat_delay);

	/* Parse [keyboard.xkb] sub-table */
	toml_table_t *xkb = toml_table_in(keyboard, "xkb");
	if (xkb) {
		free(cfg.xkb_rules);
		free(cfg.xkb_model);
		free(cfg.xkb_layout);
		free(cfg.xkb_variant);
		free(cfg.xkb_options);

		cfg.xkb_rules = toml_string_or_default(xkb, "rules", NULL);
		cfg.xkb_model = toml_string_or_default(xkb, "model", NULL);
		cfg.xkb_layout = toml_string_or_default(xkb, "layout", NULL);
		cfg.xkb_variant = toml_string_or_default(xkb, "variant", NULL);
		cfg.xkb_options = toml_string_or_default(xkb, "options", NULL);
	}
}

/* Parse [pointer] section */
static void parse_pointer_section(toml_table_t *pointer)
{
	if (!pointer) return;

	cfg.tap_to_click = toml_bool_or_default(pointer, "tap_to_click", cfg.tap_to_click);
	cfg.tap_and_drag = toml_bool_or_default(pointer, "tap_and_drag", cfg.tap_and_drag);
	cfg.drag_lock = toml_bool_or_default(pointer, "drag_lock", cfg.drag_lock);
	cfg.natural_scrolling = toml_bool_or_default(pointer, "natural_scrolling", cfg.natural_scrolling);
	cfg.disable_while_typing = toml_bool_or_default(pointer, "disable_while_typing", cfg.disable_while_typing);
	cfg.left_handed = toml_bool_or_default(pointer, "left_handed", cfg.left_handed);
	cfg.middle_button_emulation = toml_bool_or_default(pointer, "middle_button_emulation", cfg.middle_button_emulation);
	cfg.accel_speed = toml_double_or_default(pointer, "accel_speed", cfg.accel_speed);

	/* Scroll method */
	toml_datum_t d = toml_string_in(pointer, "scroll_method");
	if (d.ok) {
		if (strcmp(d.u.s, "two_finger") == 0 || strcmp(d.u.s, "2fg") == 0)
			cfg.scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
		else if (strcmp(d.u.s, "edge") == 0)
			cfg.scroll_method = LIBINPUT_CONFIG_SCROLL_EDGE;
		else if (strcmp(d.u.s, "button") == 0 || strcmp(d.u.s, "on_button") == 0)
			cfg.scroll_method = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
		else if (strcmp(d.u.s, "none") == 0 || strcmp(d.u.s, "disabled") == 0)
			cfg.scroll_method = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
		free(d.u.s);
	}

	/* Click method */
	d = toml_string_in(pointer, "click_method");
	if (d.ok) {
		if (strcmp(d.u.s, "button_areas") == 0 || strcmp(d.u.s, "areas") == 0)
			cfg.click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
		else if (strcmp(d.u.s, "clickfinger") == 0 || strcmp(d.u.s, "finger") == 0)
			cfg.click_method = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER;
		else if (strcmp(d.u.s, "none") == 0 || strcmp(d.u.s, "disabled") == 0)
			cfg.click_method = LIBINPUT_CONFIG_CLICK_METHOD_NONE;
		free(d.u.s);
	}

	/* Accel profile */
	d = toml_string_in(pointer, "accel_profile");
	if (d.ok) {
		if (strcmp(d.u.s, "adaptive") == 0)
			cfg.accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
		else if (strcmp(d.u.s, "flat") == 0)
			cfg.accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
		free(d.u.s);
	}

	/* Button map */
	d = toml_string_in(pointer, "button_map");
	if (d.ok) {
		if (strcmp(d.u.s, "lrm") == 0 || strcmp(d.u.s, "LRM") == 0)
			cfg.button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;
		else if (strcmp(d.u.s, "lmr") == 0 || strcmp(d.u.s, "LMR") == 0)
			cfg.button_map = LIBINPUT_CONFIG_TAP_MAP_LMR;
		free(d.u.s);
	}

	/* Send events mode */
	d = toml_string_in(pointer, "send_events");
	if (d.ok) {
		if (strcmp(d.u.s, "enabled") == 0)
			cfg.send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
		else if (strcmp(d.u.s, "disabled") == 0)
			cfg.send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED;
		else if (strcmp(d.u.s, "disabled_on_external_mouse") == 0)
			cfg.send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE;
		free(d.u.s);
	}
}

/* Parse [[rules]] array */
static void parse_rules_array(toml_array_t *arr)
{
	if (!arr) return;

	int n = toml_array_nelem(arr);
	if (n == 0) return;

	/* Free old rules */
	for (size_t i = 0; i < cfg.rules_count; i++) {
		free(cfg.rules[i].id);
		free(cfg.rules[i].title);
	}
	free(cfg.rules);

	cfg.rules = ecalloc(n, sizeof(ConfigRule));
	cfg.rules_count = n;

	for (int i = 0; i < n; i++) {
		toml_table_t *rule = toml_table_at(arr, i);
		if (!rule) continue;

		cfg.rules[i].id = toml_string_or_default(rule, "app_id", NULL);
		cfg.rules[i].title = toml_string_or_default(rule, "title", NULL);
		cfg.rules[i].tags = (uint32_t)toml_int_or_default(rule, "tags", 0);
		cfg.rules[i].isfloating = toml_bool_or_default(rule, "floating", 0);
		cfg.rules[i].monitor = toml_int_or_default(rule, "monitor", -1);
	}
}

/* Parse [[monitors]] array */
static void parse_monitors_array(toml_array_t *arr)
{
	if (!arr) return;

	int n = toml_array_nelem(arr);
	if (n == 0) return;

	/* Free old monitor rules */
	for (size_t i = 0; i < cfg.monrules_count; i++) {
		free(cfg.monrules[i].name);
	}
	free(cfg.monrules);

	cfg.monrules = ecalloc(n, sizeof(ConfigMonitorRule));
	cfg.monrules_count = n;

	for (int i = 0; i < n; i++) {
		toml_table_t *mon = toml_table_at(arr, i);
		if (!mon) continue;

		cfg.monrules[i].name = toml_string_or_default(mon, "name", NULL);
		cfg.monrules[i].mfact = (float)toml_double_or_default(mon, "mfact", 0.55);
		cfg.monrules[i].nmaster = toml_int_or_default(mon, "nmaster", 1);
		cfg.monrules[i].scale = (float)toml_double_or_default(mon, "scale", 1.0);
		cfg.monrules[i].x = toml_int_or_default(mon, "x", -1);
		cfg.monrules[i].y = toml_int_or_default(mon, "y", -1);

		/* Layout - default to 0 (tile) */
		cfg.monrules[i].layout_index = 0;
		toml_datum_t d = toml_string_in(mon, "layout");
		if (d.ok) {
			if (strcmp(d.u.s, "tile") == 0) cfg.monrules[i].layout_index = 0;
			else if (strcmp(d.u.s, "float") == 0 || strcmp(d.u.s, "floating") == 0) cfg.monrules[i].layout_index = 1;
			else if (strcmp(d.u.s, "monocle") == 0) cfg.monrules[i].layout_index = 2;
			else if (strcmp(d.u.s, "scroller") == 0) cfg.monrules[i].layout_index = 3;
			free(d.u.s);
		}

		/* Transform - default to normal */
		cfg.monrules[i].transform = 0; /* WL_OUTPUT_TRANSFORM_NORMAL */
		d = toml_string_in(mon, "transform");
		if (d.ok) {
			if (strcmp(d.u.s, "normal") == 0 || strcmp(d.u.s, "0") == 0) cfg.monrules[i].transform = 0;
			else if (strcmp(d.u.s, "90") == 0) cfg.monrules[i].transform = 1;
			else if (strcmp(d.u.s, "180") == 0) cfg.monrules[i].transform = 2;
			else if (strcmp(d.u.s, "270") == 0) cfg.monrules[i].transform = 3;
			else if (strcmp(d.u.s, "flipped") == 0) cfg.monrules[i].transform = 4;
			else if (strcmp(d.u.s, "flipped_90") == 0) cfg.monrules[i].transform = 5;
			else if (strcmp(d.u.s, "flipped_180") == 0) cfg.monrules[i].transform = 6;
			else if (strcmp(d.u.s, "flipped_270") == 0) cfg.monrules[i].transform = 7;
			free(d.u.s);
		}
	}
}

/* Extract the key name from a key string like "super+shift+Return" */
static const char *extract_keyname(const char *keystr)
{
	const char *last_plus = strrchr(keystr, '+');
	if (last_plus) {
		return last_plus + 1;
	}
	return keystr;
}

/* Parse action argument from TOML value */
static CfgArg parse_arg(toml_table_t *binding, const char *action_name)
{
	CfgArg arg = {0};

	/* Check for "arg" field */
	toml_datum_t d = toml_int_in(binding, "arg");
	if (d.ok) {
		/* Handle special values for moveresize */
		if (strcmp(action_name, "moveresize") == 0) {
			arg.ui = (uint32_t)d.u.i;  /* CurMove=2, CurResize=3 */
		} else {
			arg.i = (int)d.u.i;
		}
		return arg;
	}

	d = toml_double_in(binding, "arg");
	if (d.ok) {
		arg.f = (float)d.u.d;
		return arg;
	}

	d = toml_string_in(binding, "arg");
	if (d.ok) {
		/* Handle special string args */
		if (strcmp(action_name, "moveresize") == 0) {
			if (strcmp(d.u.s, "move") == 0) arg.ui = CurMove;
			else if (strcmp(d.u.s, "resize") == 0) arg.ui = CurResize;
		} else if (strcmp(action_name, "focusdir") == 0) {
			if (strcmp(d.u.s, "up") == 0) arg.i = UP;
			else if (strcmp(d.u.s, "down") == 0) arg.i = DOWN;
			else if (strcmp(d.u.s, "left") == 0) arg.i = LEFT;
			else if (strcmp(d.u.s, "right") == 0) arg.i = RIGHT;
		} else if (strcmp(action_name, "focusmon") == 0 || strcmp(action_name, "tagmon") == 0) {
			/* WLR_DIRECTION_LEFT = 1 << 0 = 1, WLR_DIRECTION_RIGHT = 1 << 1 = 2 */
			if (strcmp(d.u.s, "left") == 0) arg.i = 1;  /* WLR_DIRECTION_LEFT */
			else if (strcmp(d.u.s, "right") == 0) arg.i = 2;  /* WLR_DIRECTION_RIGHT */
		} else if (strcmp(action_name, "setlayout") == 0) {
			/* Layout by name */
			if (strcmp(d.u.s, "tile") == 0) arg.v = &layouts[0];
			else if (strcmp(d.u.s, "float") == 0 || strcmp(d.u.s, "floating") == 0) arg.v = &layouts[1];
			else if (strcmp(d.u.s, "monocle") == 0) arg.v = &layouts[2];
			else if (strcmp(d.u.s, "scroller") == 0) arg.v = &layouts[3];
		}
		free(d.u.s);
		return arg;
	}

	/* Check for command array for spawn */
	if (strcmp(action_name, "spawn") == 0) {
		toml_array_t *cmd_arr = toml_array_in(binding, "command");
		if (cmd_arr) {
			int cmd_len = toml_array_nelem(cmd_arr);
			/* Allocate command array with NULL terminator */
			const char **cmd = ecalloc(cmd_len + 1, sizeof(char *));
			int i;
			for (i = 0; i < cmd_len; i++) {
				toml_datum_t s = toml_string_at(cmd_arr, i);
				if (s.ok) {
					cmd[i] = s.u.s;  /* Transfer ownership */
				}
			}
			cmd[cmd_len] = NULL;
			arg.v = cmd;
			return arg;
		}
	}

	/* Check for layout_index for setlayout */
	d = toml_int_in(binding, "layout_index");
	if (d.ok && strcmp(action_name, "setlayout") == 0) {
		arg.v = &layouts[d.u.i];
		return arg;
	}

	return arg;
}

/* Parse [keybindings] section */
static void parse_keybindings_section(toml_table_t *keybindings)
{
	if (!keybindings) return;

	cfg.keys_from_config = 1;

	/* Count keys */
	int n = 0;
	const char *key;
	for (int i = 0; (key = toml_key_in(keybindings, i)) != NULL; i++) {
		n++;
	}

	if (n == 0) return;

	/* Free old keybindings */
	free(cfg.keys);
	cfg.keys = ecalloc(n, sizeof(KeyBinding));
	cfg.keys_count = 0;

	for (int i = 0; (key = toml_key_in(keybindings, i)) != NULL; i++) {
		toml_table_t *binding = toml_table_in(keybindings, key);
		if (!binding) continue;

		/* Get action name */
		toml_datum_t d = toml_string_in(binding, "action");
		if (!d.ok) {
			wlr_log(WLR_ERROR, "Keybinding '%s' missing action", key);
			continue;
		}

		ActionFunc func = parse_action(d.u.s);
		if (!func) {
			wlr_log(WLR_ERROR, "Unknown action '%s' for keybinding '%s'", d.u.s, key);
			free(d.u.s);
			continue;
		}

		/* Parse modifiers and keysym from key string */
		uint32_t mods = parse_modifiers(key);
		const char *keyname = extract_keyname(key);
		xkb_keysym_t keysym = parse_keysym(keyname);

		if (keysym == XKB_KEY_NoSymbol) {
			wlr_log(WLR_ERROR, "Unknown keysym '%s' in keybinding '%s'", keyname, key);
			free(d.u.s);
			continue;
		}

		CfgArg arg = parse_arg(binding, d.u.s);
		free(d.u.s);

		cfg.keys[cfg.keys_count].mod = mods;
		cfg.keys[cfg.keys_count].keysym = keysym;
		cfg.keys[cfg.keys_count].func = func;
		cfg.keys[cfg.keys_count].arg = arg;
		cfg.keys_count++;
	}
}

/* Parse [buttons] section */
static void parse_buttons_section(toml_table_t *buttons)
{
	if (!buttons) return;

	cfg.buttons_from_config = 1;

	/* Count buttons */
	int n = 0;
	const char *key;
	for (int i = 0; (key = toml_key_in(buttons, i)) != NULL; i++) {
		n++;
	}

	if (n == 0) return;

	/* Free old button bindings */
	free(cfg.buttons);
	cfg.buttons = ecalloc(n, sizeof(ButtonBinding));
	cfg.buttons_count = 0;

	for (int i = 0; (key = toml_key_in(buttons, i)) != NULL; i++) {
		toml_table_t *binding = toml_table_in(buttons, key);
		if (!binding) continue;

		/* Get action name */
		toml_datum_t d = toml_string_in(binding, "action");
		if (!d.ok) {
			wlr_log(WLR_ERROR, "Button binding '%s' missing action", key);
			continue;
		}

		ActionFunc func = parse_action(d.u.s);
		if (!func) {
			wlr_log(WLR_ERROR, "Unknown action '%s' for button binding '%s'", d.u.s, key);
			free(d.u.s);
			continue;
		}

		/* Parse modifiers and button from key string */
		uint32_t mods = parse_modifiers(key);
		const char *btnname = extract_keyname(key);
		uint32_t button = parse_button(btnname);

		if (button == 0) {
			wlr_log(WLR_ERROR, "Unknown button '%s' in binding '%s'", btnname, key);
			free(d.u.s);
			continue;
		}

		CfgArg arg = parse_arg(binding, d.u.s);
		free(d.u.s);

		cfg.buttons[cfg.buttons_count].mod = mods;
		cfg.buttons[cfg.buttons_count].button = button;
		cfg.buttons[cfg.buttons_count].func = func;
		cfg.buttons[cfg.buttons_count].arg = arg;
		cfg.buttons_count++;
	}
}

int config_load(const char *path)
{
	char errbuf[256];
	FILE *fp;
	toml_table_t *conf;

	/* Set defaults first */
	config_set_defaults();

	/* Find config file */
	char *config_path = path ? xstrdup(path) : config_find_path();
	if (!config_path) {
		wlr_log(WLR_INFO, "No config file found, using defaults");
		return 0;  /* Not an error - just use defaults */
	}

	wlr_log(WLR_INFO, "Loading config from %s", config_path);

	fp = fopen(config_path, "r");
	if (!fp) {
		wlr_log(WLR_ERROR, "Cannot open config file %s: %s", config_path, strerror(errno));
		free(config_path);
		return -1;
	}

	conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (!conf) {
		wlr_log(WLR_ERROR, "Error parsing config file %s: %s", config_path, errbuf);
		free(config_path);
		return -1;
	}

	/* Store config path for reload */
	free(cfg.config_path);
	cfg.config_path = config_path;

	/* Parse sections */
	parse_general_section(toml_table_in(conf, "general"));
	parse_appearance_section(toml_table_in(conf, "appearance"));
#ifdef SCENEFX
	parse_scenefx_section(toml_table_in(conf, "scenefx"));
#endif
	parse_keyboard_section(toml_table_in(conf, "keyboard"));
	parse_pointer_section(toml_table_in(conf, "pointer"));
	parse_rules_array(toml_array_in(conf, "rules"));
	parse_monitors_array(toml_array_in(conf, "monitors"));
	parse_keybindings_section(toml_table_in(conf, "keybindings"));
	parse_buttons_section(toml_table_in(conf, "buttons"));

	toml_free(conf);

	wlr_log(WLR_INFO, "Config loaded successfully");
	return 0;
}

int config_reload(void)
{
	if (!cfg.config_path) {
		wlr_log(WLR_INFO, "No config file to reload");
		return -1;
	}

	wlr_log(WLR_INFO, "Reloading config from %s", cfg.config_path);

	char errbuf[256];
	FILE *fp = fopen(cfg.config_path, "r");
	if (!fp) {
		wlr_log(WLR_ERROR, "Cannot open config file %s: %s", cfg.config_path, strerror(errno));
		return -1;
	}

	toml_table_t *conf = toml_parse_file(fp, errbuf, sizeof(errbuf));
	fclose(fp);

	if (!conf) {
		wlr_log(WLR_ERROR, "Error parsing config file %s: %s", cfg.config_path, errbuf);
		return -1;
	}

	/* Only reload certain sections - things that can change at runtime */
	parse_appearance_section(toml_table_in(conf, "appearance"));
#ifdef SCENEFX
	parse_scenefx_section(toml_table_in(conf, "scenefx"));
#endif
	parse_rules_array(toml_array_in(conf, "rules"));
	parse_keybindings_section(toml_table_in(conf, "keybindings"));
	parse_buttons_section(toml_table_in(conf, "buttons"));

	toml_free(conf);

	wlr_log(WLR_INFO, "Config reloaded successfully");
	return 0;
}

/* config_apply is implemented in dwl.c where it has access to internal structures */
extern void config_apply_internal(void);

void config_apply(void)
{
	config_apply_internal();
	wlr_log(WLR_INFO, "Config applied");
}

void config_free(void)
{
	/* Free rules */
	for (size_t i = 0; i < cfg.rules_count; i++) {
		free(cfg.rules[i].id);
		free(cfg.rules[i].title);
	}
	free(cfg.rules);

	/* Free monitor rules */
	for (size_t i = 0; i < cfg.monrules_count; i++) {
		free(cfg.monrules[i].name);
	}
	free(cfg.monrules);

	/* Free keybindings */
	free(cfg.keys);

	/* Free button bindings */
	free(cfg.buttons);

	/* Free XKB strings */
	free(cfg.xkb_rules);
	free(cfg.xkb_model);
	free(cfg.xkb_layout);
	free(cfg.xkb_variant);
	free(cfg.xkb_options);

	/* Free config path */
	free(cfg.config_path);

	memset(&cfg, 0, sizeof(cfg));
}

/* The reload_config action - called from keybinding */
void reload_config(const CfgArg *arg)
{
	(void)arg;
	if (config_reload() == 0) {
		config_apply();
	}
}
