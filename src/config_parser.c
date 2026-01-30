/*
 * config_parser.c - Runtime configuration parser
 * See LICENSE file for copyright and license details.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>

#include "config_parser.h"
#include "dwl.h"
#include "layout.h"
#include "monitor.h"
#include "input.h"
#include "visual.h"
#include "client_funcs.h"

Config cfg;

/* --- String pool management --- */
static char *
pool_strdup(const char *s)
{
	StringPoolEntry *e;
	char *dup;
	if (!s)
		return NULL;
	dup = strdup(s);
	if (!dup)
		return NULL;
	e = malloc(sizeof(*e));
	if (!e) {
		free(dup);
		return NULL;
	}
	e->str = dup;
	e->next = cfg.string_pool;
	cfg.string_pool = e;
	return dup;
}

static void
pool_free_all(void)
{
	StringPoolEntry *e, *next;
	for (e = cfg.string_pool; e; e = next) {
		next = e->next;
		free(e->str);
		free(e);
	}
	cfg.string_pool = NULL;
}

/* --- Color parsing --- */
static void
color_from_hex(uint32_t hex, float color[4])
{
	color[0] = ((hex >> 24) & 0xFF) / 255.0f;
	color[1] = ((hex >> 16) & 0xFF) / 255.0f;
	color[2] = ((hex >> 8) & 0xFF) / 255.0f;
	color[3] = (hex & 0xFF) / 255.0f;
}

static int
parse_color(const char *s, float color[4])
{
	char *end;
	unsigned long val;

	while (isspace((unsigned char)*s)) s++;

	val = strtoul(s, &end, 16);
	if (end == s)
		return -1;
	color_from_hex((uint32_t)val, color);
	return 0;
}

/* --- Function lookup table --- */
typedef struct {
	const char *name;
	void (*func)(const Arg *);
} FuncEntry;

static const FuncEntry func_table[] = {
	{ "spawn",                   spawn },
	{ "killclient",              killclient },
	{ "quit",                    quit },
	{ "moveresize",              moveresize },
	{ "chvt",                    chvt },
	{ "focusstack",              focusstack },
	{ "focusdir",                focusdir },
	{ "setmfact",                setmfact },
	{ "setlayout",               setlayout },
	{ "zoom",                    zoom },
	{ "togglefloating",          togglefloating },
	{ "togglefullscreen",        togglefullscreen },
	{ "incnmaster",              incnmaster },
	{ "incgaps",                 incgaps },
	{ "incigaps",                incigaps },
	{ "incihgaps",               incihgaps },
	{ "incivgaps",               incivgaps },
	{ "incogaps",                incogaps },
	{ "incohgaps",               incohgaps },
	{ "incovgaps",               incovgaps },
	{ "togglegaps",              togglegaps },
	{ "defaultgaps",             defaultgaps },
	{ "scroller_cycle_proportion", scroller_cycle_proportion },
	{ "consume_or_expel",        consume_or_expel },
	{ "focusmon",                focusmon },
	{ "movetomon",               movetomon },
	{ NULL, NULL }
};

void (*lookup_func(const char *name))(const Arg *)
{
	const FuncEntry *f;
	for (f = func_table; f->name; f++) {
		if (strcmp(f->name, name) == 0)
			return f->func;
	}
	return NULL;
}

const char *
lookup_func_name(void (*func)(const Arg *))
{
	const FuncEntry *f;
	for (f = func_table; f->name; f++) {
		if (f->func == func)
			return f->name;
	}
	return NULL;
}

/* --- Modifier parsing --- */
static uint32_t
parse_modifier(const char *s)
{
	if (strcmp(s, "mod") == 0)
		return cfg.modkey;
	if (strcmp(s, "alt") == 0)
		return WLR_MODIFIER_ALT;
	if (strcmp(s, "super") == 0 || strcmp(s, "logo") == 0)
		return WLR_MODIFIER_LOGO;
	if (strcmp(s, "ctrl") == 0 || strcmp(s, "control") == 0)
		return WLR_MODIFIER_CTRL;
	if (strcmp(s, "shift") == 0)
		return WLR_MODIFIER_SHIFT;
	return 0;
}

static uint32_t
parse_modifiers(const char *s)
{
	/* Parse "mod+shift+ctrl" style modifier strings */
	char buf[256];
	char *tok, *save;
	uint32_t mods = 0;

	snprintf(buf, sizeof(buf), "%s", s);
	tok = strtok_r(buf, "+", &save);
	while (tok) {
		mods |= parse_modifier(tok);
		tok = strtok_r(NULL, "+", &save);
	}
	return mods;
}

/* --- Enum parsing --- */
static int
parse_scroll_method(const char *s, enum libinput_config_scroll_method *out)
{
	if (strcmp(s, "no_scroll") == 0 || strcmp(s, "none") == 0) {
		*out = LIBINPUT_CONFIG_SCROLL_NO_SCROLL; return 0;
	}
	if (strcmp(s, "2fg") == 0 || strcmp(s, "two_finger") == 0) {
		*out = LIBINPUT_CONFIG_SCROLL_2FG; return 0;
	}
	if (strcmp(s, "edge") == 0) {
		*out = LIBINPUT_CONFIG_SCROLL_EDGE; return 0;
	}
	if (strcmp(s, "button") == 0 || strcmp(s, "on_button_down") == 0) {
		*out = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN; return 0;
	}
	return -1;
}

static int
parse_click_method(const char *s, enum libinput_config_click_method *out)
{
	if (strcmp(s, "none") == 0) {
		*out = LIBINPUT_CONFIG_CLICK_METHOD_NONE; return 0;
	}
	if (strcmp(s, "button_areas") == 0) {
		*out = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS; return 0;
	}
	if (strcmp(s, "clickfinger") == 0) {
		*out = LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER; return 0;
	}
	return -1;
}

static int
parse_accel_profile(const char *s, enum libinput_config_accel_profile *out)
{
	if (strcmp(s, "flat") == 0) {
		*out = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT; return 0;
	}
	if (strcmp(s, "adaptive") == 0) {
		*out = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE; return 0;
	}
	return -1;
}

static int
parse_tap_button_map(const char *s, enum libinput_config_tap_button_map *out)
{
	if (strcmp(s, "lrm") == 0) {
		*out = LIBINPUT_CONFIG_TAP_MAP_LRM; return 0;
	}
	if (strcmp(s, "lmr") == 0) {
		*out = LIBINPUT_CONFIG_TAP_MAP_LMR; return 0;
	}
	return -1;
}

static int
parse_send_events_mode(const char *s, uint32_t *out)
{
	if (strcmp(s, "enabled") == 0) {
		*out = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED; return 0;
	}
	if (strcmp(s, "disabled") == 0) {
		*out = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED; return 0;
	}
	if (strcmp(s, "disabled_on_external_mouse") == 0) {
		*out = LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE; return 0;
	}
	return -1;
}

static int
parse_output_transform(const char *s, enum wl_output_transform *out)
{
	if (strcmp(s, "normal") == 0) { *out = WL_OUTPUT_TRANSFORM_NORMAL; return 0; }
	if (strcmp(s, "90") == 0) { *out = WL_OUTPUT_TRANSFORM_90; return 0; }
	if (strcmp(s, "180") == 0) { *out = WL_OUTPUT_TRANSFORM_180; return 0; }
	if (strcmp(s, "270") == 0) { *out = WL_OUTPUT_TRANSFORM_270; return 0; }
	if (strcmp(s, "flipped") == 0) { *out = WL_OUTPUT_TRANSFORM_FLIPPED; return 0; }
	if (strcmp(s, "flipped_90") == 0) { *out = WL_OUTPUT_TRANSFORM_FLIPPED_90; return 0; }
	if (strcmp(s, "flipped_180") == 0) { *out = WL_OUTPUT_TRANSFORM_FLIPPED_180; return 0; }
	if (strcmp(s, "flipped_270") == 0) { *out = WL_OUTPUT_TRANSFORM_FLIPPED_270; return 0; }
	return -1;
}

static int
parse_scroller_center_mode(const char *s, int *out)
{
	if (strcmp(s, "always") == 0) { *out = 0; return 0; } /* ScrollerCenterAlways */
	if (strcmp(s, "on_overflow") == 0) { *out = 1; return 0; } /* ScrollerCenterOnOverflow */
	if (strcmp(s, "prefer_center") == 0) { *out = 2; return 0; } /* ScrollerCenterPreferCenter */
	return -1;
}

/* --- Button code parsing --- */
static unsigned int
parse_button_code(const char *s)
{
	if (strcmp(s, "BTN_LEFT") == 0) return BTN_LEFT;
	if (strcmp(s, "BTN_RIGHT") == 0) return BTN_RIGHT;
	if (strcmp(s, "BTN_MIDDLE") == 0) return BTN_MIDDLE;
	if (strcmp(s, "BTN_SIDE") == 0) return BTN_SIDE;
	if (strcmp(s, "BTN_EXTRA") == 0) return BTN_EXTRA;
	return 0;
}

/* --- Token helpers --- */
static char *
skip_whitespace(char *s)
{
	while (*s && isspace((unsigned char)*s)) s++;
	return s;
}

static char *
next_token(char **pos)
{
	char *s = skip_whitespace(*pos);
	char *start;
	if (!*s)
		return NULL;
	start = s;
	while (*s && !isspace((unsigned char)*s)) s++;
	if (*s) {
		*s = '\0';
		s++;
	}
	*pos = s;
	return start;
}

/* --- Forward declarations for internal functions --- */
static void config_set_default_keys(void);
static void config_set_default_buttons(void);
static void add_key(CfgKey **keys, size_t *count, size_t *cap,
        uint32_t mod, xkb_keysym_t keysym, void (*func)(const Arg *), Arg arg);

/* Reset static parse state (needed for reload) */
static int parse_first_bind;
static int parse_first_button;
static int parse_first_rule;
static int parse_first_monrule;

static void
reset_parse_state(void)
{
	parse_first_bind = 1;
	parse_first_button = 1;
	parse_first_rule = 1;
	parse_first_monrule = 1;
}

/* --- Argument parsing for keybindings --- */
static Arg
parse_bind_arg(void (*func)(const Arg *), char *rest)
{
	Arg arg = {0};
	char *tok;

	if (!rest || !*rest)
		return arg;

	rest = skip_whitespace(rest);
	if (!*rest)
		return arg;

	if (func == spawn) {
		/* Build a NULL-terminated argv array from the rest of the line */
		char **argv;
		size_t argc = 0, cap = 8;
		char *p = rest;

		argv = calloc(cap, sizeof(char *));
		while ((tok = next_token(&p))) {
			if (argc + 1 >= cap) {
				cap *= 2;
				argv = realloc(argv, cap * sizeof(char *));
			}
			argv[argc++] = pool_strdup(tok);
		}
		argv[argc] = NULL;

		/* Store in string pool tracking */
		{
			StringPoolEntry *e = malloc(sizeof(*e));
			e->str = (char *)argv;
			e->next = cfg.string_pool;
			cfg.string_pool = e;
		}
		arg.v = argv;
	} else if (func == setlayout) {
		/* Layout name → pointer to cfg.layouts[] */
		tok = next_token(&rest);
		if (tok) {
			size_t i;
			for (i = 0; i < cfg.layouts_count; i++) {
				if (strcmp(cfg.layouts[i].symbol, tok) == 0) {
					arg.v = &cfg.layouts[i];
					return arg;
				}
			}
			/* Try by name: tile, floating, monocle, scroller */
			if (strcmp(tok, "tile") == 0) arg.v = &cfg.layouts[0];
			else if (strcmp(tok, "floating") == 0) arg.v = &cfg.layouts[1];
			else if (strcmp(tok, "monocle") == 0) arg.v = &cfg.layouts[2];
			else if (strcmp(tok, "scroller") == 0) arg.v = &cfg.layouts[3];
		}
	} else if (func == chvt) {
		tok = next_token(&rest);
		if (tok) arg.ui = (uint32_t)atoi(tok);
	} else if (func == moveresize) {
		tok = next_token(&rest);
		if (tok) {
			if (strcmp(tok, "move") == 0) arg.ui = CurMove;
			else if (strcmp(tok, "resize") == 0) arg.ui = CurResize;
			else arg.ui = (uint32_t)atoi(tok);
		}
	} else if (func == focusmon || func == movetomon) {
		tok = next_token(&rest);
		if (tok) {
			if (strcmp(tok, "left") == 0) arg.i = WLR_DIRECTION_LEFT;
			else if (strcmp(tok, "right") == 0) arg.i = WLR_DIRECTION_RIGHT;
			else arg.i = atoi(tok);
		}
	} else if (func == setmfact) {
		tok = next_token(&rest);
		if (tok) arg.f = strtof(tok, NULL);
	} else {
		/* Generic: try int first, then uint, then float */
		tok = next_token(&rest);
		if (tok) {
			if (strchr(tok, '.'))
				arg.f = strtof(tok, NULL);
			else
				arg.i = atoi(tok);
		}
	}
	return arg;
}

/* --- Default configuration --- */
static void
config_set_defaults(void)
{
	memset(&cfg, 0, sizeof(cfg));

	/* Appearance */
	cfg.borderpx = 4;
	cfg.sloppyfocus = 1;
	cfg.bypass_surface_visibility = 0;
	cfg.smartgaps = 0;
	cfg.monoclegaps = 0;
	cfg.gappih = 10;
	cfg.gappiv = 10;
	cfg.gappoh = 10;
	cfg.gappov = 10;

	/* Colors */
	color_from_hex(0x222222ff, cfg.rootcolor);
	color_from_hex(0x444444ff, cfg.bordercolor);
	color_from_hex(0x005577ff, cfg.focuscolor);
	color_from_hex(0xff0000ff, cfg.urgentcolor);
	cfg.fullscreen_bg[0] = 0.0f;
	cfg.fullscreen_bg[1] = 0.0f;
	cfg.fullscreen_bg[2] = 0.0f;
	cfg.fullscreen_bg[3] = 0.0f;

	/* Opacity */
	cfg.opacity = 0;
	cfg.opacity_inactive = 0.5f;
	cfg.opacity_active = 1.0f;

	/* Shadows */
	cfg.shadow = 0;
	cfg.shadow_only_floating = 0;
	color_from_hex(0x0000FFff, cfg.shadow_color);
	color_from_hex(0xFF0000ff, cfg.shadow_color_focus);
	cfg.shadow_blur_sigma = 20;
	cfg.shadow_blur_sigma_focus = 40;
	cfg.shadow_ignore_list = calloc(1, sizeof(char *));
	cfg.shadow_ignore_list[0] = NULL;
	cfg.shadow_ignore_count = 0;

	/* Corner radius */
	cfg.corner_radius = 8;
	cfg.corner_radius_inner = 9;
	cfg.corner_radius_only_floating = 0;

	/* Blur */
	cfg.blur = 1;
	cfg.blur_xray = 0;
	cfg.blur_ignore_transparent = 1;
	cfg.blur_data.num_passes = 3;
	cfg.blur_data.radius = 5.0f;
	cfg.blur_data.noise = 0.02f;
	cfg.blur_data.brightness = 0.9f;
	cfg.blur_data.contrast = 0.9f;
	cfg.blur_data.saturation = 1.1f;

	/* Logging */
	cfg.log_level = WLR_ERROR;

	/* Scroller */
	cfg.scroller_center_mode = 1; /* ScrollerCenterOnOverflow */
	cfg.scroller_proportions = calloc(4, sizeof(float));
	cfg.scroller_proportions[0] = 0.5f;
	cfg.scroller_proportions[1] = 0.66f;
	cfg.scroller_proportions[2] = 0.8f;
	cfg.scroller_proportions[3] = 1.0f;
	cfg.scroller_proportions_count = 4;
	cfg.scroller_default_proportion = 0;

	/* Layouts (fixed array, pointer-stable) */
	cfg.layouts[0] = (Layout){ "[]=", tile };
	cfg.layouts[1] = (Layout){ "><>", NULL };
	cfg.layouts[2] = (Layout){ "[M]", monocle };
	cfg.layouts[3] = (Layout){ "|S|", scroller };
	cfg.layouts_count = 4;

	/* Rules - default examples */
	cfg.rules = calloc(2, sizeof(CfgRule));
	cfg.rules[0] = (CfgRule){ pool_strdup("Gimp_EXAMPLE"), NULL, 1, -1 };
	cfg.rules[1] = (CfgRule){ pool_strdup("firefox_EXAMPLE"), NULL, 0, -1 };
	cfg.rules_count = 2;

	/* Monitor rules */
	cfg.monrules = calloc(3, sizeof(CfgMonitorRule));
	cfg.monrules[0] = (CfgMonitorRule){ pool_strdup("HDMI-A-1"), 0.5f, 1, 1, 0, WL_OUTPUT_TRANSFORM_NORMAL, 0, 0 };
	cfg.monrules[1] = (CfgMonitorRule){ pool_strdup("DP-1"), 0.5f, 1, 1, 0, WL_OUTPUT_TRANSFORM_NORMAL, 1920, 0 };
	cfg.monrules[2] = (CfgMonitorRule){ NULL, 0.55f, 1, 1, 0, WL_OUTPUT_TRANSFORM_NORMAL, -1, -1 };
	cfg.monrules_count = 3;

	/* Keyboard */
	cfg.xkb_rules = NULL;
	cfg.xkb_model = NULL;
	cfg.xkb_layout = NULL;
	cfg.xkb_variant = NULL;
	cfg.xkb_options = NULL;
	cfg.repeat_rate = 25;
	cfg.repeat_delay = 600;
	cfg.numlock = 1;

	/* Trackpad */
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

	/* Modifier key */
	cfg.modkey = WLR_MODIFIER_ALT;

	/* Commands */
	cfg.termcmd = calloc(2, sizeof(char *));
	cfg.termcmd[0] = pool_strdup("alacritty");
	cfg.termcmd[1] = NULL;
	cfg.termcmd_count = 2;

	cfg.menucmd = calloc(2, sizeof(char *));
	cfg.menucmd[0] = pool_strdup("wmenu-run");
	cfg.menucmd[1] = NULL;
	cfg.menucmd_count = 2;

	/* Default keybindings */
	config_set_default_keys();

	/* Default buttons */
	config_set_default_buttons();
}

/* --- Default keybindings --- */
static void
add_key(CfgKey **keys, size_t *count, size_t *cap,
        uint32_t mod, xkb_keysym_t keysym, void (*func)(const Arg *), Arg arg)
{
	if (*count >= *cap) {
		*cap = *cap ? *cap * 2 : 64;
		*keys = realloc(*keys, *cap * sizeof(CfgKey));
	}
	(*keys)[*count] = (CfgKey){ mod, keysym, func, arg };
	(*count)++;
}

static void
config_set_default_keys(void)
{
	size_t cap = 64;
	uint32_t M = cfg.modkey;
	uint32_t MS = M | WLR_MODIFIER_SHIFT;
	uint32_t MC = M | WLR_MODIFIER_CTRL;
	uint32_t ML = M | WLR_MODIFIER_LOGO;
	uint32_t MLS = ML | WLR_MODIFIER_SHIFT;
	uint32_t MLC = ML | WLR_MODIFIER_CTRL;
	int i;

	cfg.keys = calloc(cap, sizeof(CfgKey));
	cfg.keys_count = 0;

	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_p, spawn, (Arg){.v = cfg.menucmd});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_Return, spawn, (Arg){.v = cfg.termcmd});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_j, focusstack, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_k, focusstack, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MC, XKB_KEY_Left, focusdir, (Arg){.ui = 0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MC, XKB_KEY_Right, focusdir, (Arg){.ui = 1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MC, XKB_KEY_Up, focusdir, (Arg){.ui = 2});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MC, XKB_KEY_Down, focusdir, (Arg){.ui = 3});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_i, incnmaster, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_d, incnmaster, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_h, setmfact, (Arg){.f = -0.05f});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_l, setmfact, (Arg){.f = +0.05f});
	add_key(&cfg.keys, &cfg.keys_count, &cap, ML, XKB_KEY_h, incgaps, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, ML, XKB_KEY_l, incgaps, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MLS, XKB_KEY_H, incogaps, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MLS, XKB_KEY_L, incogaps, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MLC, XKB_KEY_h, incigaps, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MLC, XKB_KEY_l, incigaps, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, ML, XKB_KEY_0, togglegaps, (Arg){0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MLS, XKB_KEY_parenright, defaultgaps, (Arg){0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_y, incihgaps, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_o, incihgaps, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MC, XKB_KEY_y, incivgaps, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MC, XKB_KEY_o, incivgaps, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, ML, XKB_KEY_y, incohgaps, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, ML, XKB_KEY_o, incohgaps, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_Y, incovgaps, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_O, incovgaps, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_Return, zoom, (Arg){0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_c, killclient, (Arg){0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_t, setlayout, (Arg){.v = &cfg.layouts[0]});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_f, setlayout, (Arg){.v = &cfg.layouts[1]});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_m, setlayout, (Arg){.v = &cfg.layouts[2]});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_s, setlayout, (Arg){.v = &cfg.layouts[3]});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_minus, scroller_cycle_proportion, (Arg){.i = -1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_equal, scroller_cycle_proportion, (Arg){.i = +1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_Left, consume_or_expel, (Arg){.i = 0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_Right, consume_or_expel, (Arg){.i = 1});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_space, setlayout, (Arg){0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_space, togglefloating, (Arg){0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_e, togglefullscreen, (Arg){0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_comma, focusmon, (Arg){.i = WLR_DIRECTION_LEFT});
	add_key(&cfg.keys, &cfg.keys_count, &cap, M, XKB_KEY_period, focusmon, (Arg){.i = WLR_DIRECTION_RIGHT});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_less, movetomon, (Arg){.i = WLR_DIRECTION_LEFT});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_greater, movetomon, (Arg){.i = WLR_DIRECTION_RIGHT});
	add_key(&cfg.keys, &cfg.keys_count, &cap, MS, XKB_KEY_q, quit, (Arg){0});
	add_key(&cfg.keys, &cfg.keys_count, &cap, WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, XKB_KEY_Terminate_Server, quit, (Arg){0});

	/* CHVT bindings - always appended */
	for (i = 1; i <= 12; i++) {
		char name[32];
		xkb_keysym_t sym;
		snprintf(name, sizeof(name), "XF86Switch_VT_%d", i);
		sym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
		if (sym != XKB_KEY_NoSymbol)
			add_key(&cfg.keys, &cfg.keys_count, &cap,
			        WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT, sym, chvt, (Arg){.ui = (uint32_t)i});
	}
}

static void
config_set_default_buttons(void)
{
	cfg.buttons = calloc(3, sizeof(CfgButton));
	cfg.buttons_count = 3;
	cfg.buttons[0] = (CfgButton){ cfg.modkey, BTN_LEFT, moveresize, {.ui = CurMove} };
	cfg.buttons[1] = (CfgButton){ cfg.modkey, BTN_MIDDLE, togglefloating, {0} };
	cfg.buttons[2] = (CfgButton){ cfg.modkey, BTN_RIGHT, moveresize, {.ui = CurResize} };
}

/* --- Config file path --- */
static char *
config_path(void)
{
	static char path[4096];
	const char *config_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");

	if (config_home && *config_home)
		snprintf(path, sizeof(path), "%s/dwl/config", config_home);
	else if (home && *home)
		snprintf(path, sizeof(path), "%s/.config/dwl/config", home);
	else
		return NULL;

	return path;
}

/* --- Line parser --- */
static int
parse_line(const char *line, int lineno)
{
	char buf[4096];
	char *key, *value, *eq;

	/* Skip leading whitespace */
	while (isspace((unsigned char)*line)) line++;

	/* Skip empty lines and comments */
	if (!*line || *line == '#')
		return 0;

	snprintf(buf, sizeof(buf), "%s", line);

	/* Trim trailing newline/whitespace */
	{
		size_t len = strlen(buf);
		while (len > 0 && isspace((unsigned char)buf[len-1]))
			buf[--len] = '\0';
	}

	/* Find = separator */
	eq = strchr(buf, '=');
	if (!eq) {
		fprintf(stderr, "config:%d: missing '=' in '%s'\n", lineno, buf);
		return -1;
	}
	*eq = '\0';
	key = buf;
	value = eq + 1;

	/* Trim key and value */
	{
		char *end = key + strlen(key) - 1;
		while (end > key && isspace((unsigned char)*end)) *end-- = '\0';
	}
	while (isspace((unsigned char)*value)) value++;

	/* --- Scalar config values --- */
	if (strcmp(key, "borderpx") == 0) { cfg.borderpx = (unsigned int)atoi(value); return 0; }
	if (strcmp(key, "sloppyfocus") == 0) { cfg.sloppyfocus = atoi(value); return 0; }
	if (strcmp(key, "bypass_surface_visibility") == 0) { cfg.bypass_surface_visibility = atoi(value); return 0; }
	if (strcmp(key, "smartgaps") == 0) { cfg.smartgaps = atoi(value); return 0; }
	if (strcmp(key, "monoclegaps") == 0) { cfg.monoclegaps = atoi(value); return 0; }
	if (strcmp(key, "gappih") == 0) { cfg.gappih = (unsigned int)atoi(value); return 0; }
	if (strcmp(key, "gappiv") == 0) { cfg.gappiv = (unsigned int)atoi(value); return 0; }
	if (strcmp(key, "gappoh") == 0) { cfg.gappoh = (unsigned int)atoi(value); return 0; }
	if (strcmp(key, "gappov") == 0) { cfg.gappov = (unsigned int)atoi(value); return 0; }

	/* Colors */
	if (strcmp(key, "rootcolor") == 0) return parse_color(value, cfg.rootcolor);
	if (strcmp(key, "bordercolor") == 0) return parse_color(value, cfg.bordercolor);
	if (strcmp(key, "focuscolor") == 0) return parse_color(value, cfg.focuscolor);
	if (strcmp(key, "urgentcolor") == 0) return parse_color(value, cfg.urgentcolor);
	if (strcmp(key, "fullscreen_bg") == 0) return parse_color(value, cfg.fullscreen_bg);

	/* Opacity */
	if (strcmp(key, "opacity") == 0) { cfg.opacity = atoi(value); return 0; }
	if (strcmp(key, "opacity_inactive") == 0) { cfg.opacity_inactive = strtof(value, NULL); return 0; }
	if (strcmp(key, "opacity_active") == 0) { cfg.opacity_active = strtof(value, NULL); return 0; }

	/* Shadows */
	if (strcmp(key, "shadow") == 0) { cfg.shadow = atoi(value); return 0; }
	if (strcmp(key, "shadow_only_floating") == 0) { cfg.shadow_only_floating = atoi(value); return 0; }
	if (strcmp(key, "shadow_color") == 0) return parse_color(value, cfg.shadow_color);
	if (strcmp(key, "shadow_color_focus") == 0) return parse_color(value, cfg.shadow_color_focus);
	if (strcmp(key, "shadow_blur_sigma") == 0) { cfg.shadow_blur_sigma = atoi(value); return 0; }
	if (strcmp(key, "shadow_blur_sigma_focus") == 0) { cfg.shadow_blur_sigma_focus = atoi(value); return 0; }

	/* Corner radius */
	if (strcmp(key, "corner_radius") == 0) { cfg.corner_radius = atoi(value); return 0; }
	if (strcmp(key, "corner_radius_inner") == 0) { cfg.corner_radius_inner = atoi(value); return 0; }
	if (strcmp(key, "corner_radius_only_floating") == 0) { cfg.corner_radius_only_floating = atoi(value); return 0; }

	/* Blur */
	if (strcmp(key, "blur") == 0) { cfg.blur = atoi(value); return 0; }
	if (strcmp(key, "blur_xray") == 0) { cfg.blur_xray = atoi(value); return 0; }
	if (strcmp(key, "blur_ignore_transparent") == 0) { cfg.blur_ignore_transparent = atoi(value); return 0; }
	if (strcmp(key, "blur_num_passes") == 0) { cfg.blur_data.num_passes = atoi(value); return 0; }
	if (strcmp(key, "blur_radius") == 0) { cfg.blur_data.radius = strtof(value, NULL); return 0; }
	if (strcmp(key, "blur_noise") == 0) { cfg.blur_data.noise = strtof(value, NULL); return 0; }
	if (strcmp(key, "blur_brightness") == 0) { cfg.blur_data.brightness = strtof(value, NULL); return 0; }
	if (strcmp(key, "blur_contrast") == 0) { cfg.blur_data.contrast = strtof(value, NULL); return 0; }
	if (strcmp(key, "blur_saturation") == 0) { cfg.blur_data.saturation = strtof(value, NULL); return 0; }

	/* Log level */
	if (strcmp(key, "log_level") == 0) {
		if (strcmp(value, "debug") == 0) cfg.log_level = WLR_DEBUG;
		else if (strcmp(value, "info") == 0) cfg.log_level = WLR_INFO;
		else if (strcmp(value, "error") == 0) cfg.log_level = WLR_ERROR;
		else if (strcmp(value, "silent") == 0) cfg.log_level = WLR_SILENT;
		else cfg.log_level = atoi(value);
		return 0;
	}

	/* Scroller */
	if (strcmp(key, "scroller_center_mode") == 0) return parse_scroller_center_mode(value, &cfg.scroller_center_mode);
	if (strcmp(key, "scroller_default_proportion") == 0) { cfg.scroller_default_proportion = atoi(value); return 0; }

	/* Keyboard */
	if (strcmp(key, "xkb_rules") == 0) { cfg.xkb_rules = pool_strdup(value); return 0; }
	if (strcmp(key, "xkb_model") == 0) { cfg.xkb_model = pool_strdup(value); return 0; }
	if (strcmp(key, "xkb_layout") == 0) { cfg.xkb_layout = pool_strdup(value); return 0; }
	if (strcmp(key, "xkb_variant") == 0) { cfg.xkb_variant = pool_strdup(value); return 0; }
	if (strcmp(key, "xkb_options") == 0) { cfg.xkb_options = pool_strdup(value); return 0; }
	if (strcmp(key, "repeat_rate") == 0) { cfg.repeat_rate = atoi(value); return 0; }
	if (strcmp(key, "repeat_delay") == 0) { cfg.repeat_delay = atoi(value); return 0; }
	if (strcmp(key, "numlock") == 0) { cfg.numlock = atoi(value); return 0; }

	/* Trackpad */
	if (strcmp(key, "tap_to_click") == 0) { cfg.tap_to_click = atoi(value); return 0; }
	if (strcmp(key, "tap_and_drag") == 0) { cfg.tap_and_drag = atoi(value); return 0; }
	if (strcmp(key, "drag_lock") == 0) { cfg.drag_lock = atoi(value); return 0; }
	if (strcmp(key, "natural_scrolling") == 0) { cfg.natural_scrolling = atoi(value); return 0; }
	if (strcmp(key, "disable_while_typing") == 0) { cfg.disable_while_typing = atoi(value); return 0; }
	if (strcmp(key, "left_handed") == 0) { cfg.left_handed = atoi(value); return 0; }
	if (strcmp(key, "middle_button_emulation") == 0) { cfg.middle_button_emulation = atoi(value); return 0; }
	if (strcmp(key, "scroll_method") == 0) return parse_scroll_method(value, &cfg.scroll_method);
	if (strcmp(key, "click_method") == 0) return parse_click_method(value, &cfg.click_method);
	if (strcmp(key, "send_events_mode") == 0) return parse_send_events_mode(value, &cfg.send_events_mode);
	if (strcmp(key, "accel_profile") == 0) return parse_accel_profile(value, &cfg.accel_profile);
	if (strcmp(key, "accel_speed") == 0) { cfg.accel_speed = strtod(value, NULL); return 0; }
	if (strcmp(key, "button_map") == 0) return parse_tap_button_map(value, &cfg.button_map);

	/* Modifier key */
	if (strcmp(key, "modkey") == 0) {
		if (strcmp(value, "alt") == 0) cfg.modkey = WLR_MODIFIER_ALT;
		else if (strcmp(value, "super") == 0 || strcmp(value, "logo") == 0) cfg.modkey = WLR_MODIFIER_LOGO;
		else if (strcmp(value, "ctrl") == 0) cfg.modkey = WLR_MODIFIER_CTRL;
		else { fprintf(stderr, "config:%d: unknown modkey '%s'\n", lineno, value); return -1; }
		return 0;
	}

	/* Commands */
	if (strcmp(key, "termcmd") == 0) {
		char *p = value;
		char *tok;
		size_t count = 0, cap = 4;
		cfg.termcmd = calloc(cap, sizeof(char *));
		while ((tok = next_token(&p))) {
			if (count + 1 >= cap) { cap *= 2; cfg.termcmd = realloc(cfg.termcmd, cap * sizeof(char *)); }
			cfg.termcmd[count++] = pool_strdup(tok);
		}
		cfg.termcmd[count] = NULL;
		cfg.termcmd_count = count + 1;
		return 0;
	}
	if (strcmp(key, "menucmd") == 0) {
		char *p = value;
		char *tok;
		size_t count = 0, cap = 4;
		cfg.menucmd = calloc(cap, sizeof(char *));
		while ((tok = next_token(&p))) {
			if (count + 1 >= cap) { cap *= 2; cfg.menucmd = realloc(cfg.menucmd, cap * sizeof(char *)); }
			cfg.menucmd[count++] = pool_strdup(tok);
		}
		cfg.menucmd[count] = NULL;
		cfg.menucmd_count = count + 1;
		return 0;
	}

	/* --- Multi-value keys (append) --- */

	/* scroller_proportions = 0.5 0.66 0.8 1.0 */
	if (strcmp(key, "scroller_proportions") == 0) {
		char *p = value;
		char *tok;
		size_t count = 0, cap = 8;
		float *arr = calloc(cap, sizeof(float));
		while ((tok = next_token(&p))) {
			if (count >= cap) { cap *= 2; arr = realloc(arr, cap * sizeof(float)); }
			arr[count++] = strtof(tok, NULL);
		}
		free(cfg.scroller_proportions);
		cfg.scroller_proportions = arr;
		cfg.scroller_proportions_count = count;
		return 0;
	}

	/* shadow_ignore = app_id1 app_id2 ... */
	if (strcmp(key, "shadow_ignore") == 0) {
		char *p = value;
		char *tok;
		size_t count = 0, cap = 8;
		char **arr = calloc(cap, sizeof(char *));
		while ((tok = next_token(&p))) {
			if (count + 1 >= cap) { cap *= 2; arr = realloc(arr, cap * sizeof(char *)); }
			arr[count++] = pool_strdup(tok);
		}
		arr[count] = NULL;
		free(cfg.shadow_ignore_list);
		cfg.shadow_ignore_list = arr;
		cfg.shadow_ignore_count = count;
		return 0;
	}

	/* bind = mod+shift Return spawn alacritty */
	if (strcmp(key, "bind") == 0) {
		char *p = value;
		char *mod_str, *key_str, *func_str;
		uint32_t mods;
		xkb_keysym_t keysym;
		void (*func)(const Arg *);
		Arg arg;
		size_t cap;

		if (parse_first_bind) {
			/* Clear default bindings on first bind line, keep CHVT */
			/* Re-allocate with just CHVT bindings */
			CfgKey *chvt_keys = NULL;
			size_t chvt_count = 0;
			size_t i;
			for (i = 0; i < cfg.keys_count; i++) {
				if (cfg.keys[i].func == chvt) {
					chvt_keys = realloc(chvt_keys, (chvt_count + 1) * sizeof(CfgKey));
					chvt_keys[chvt_count++] = cfg.keys[i];
				}
			}
			free(cfg.keys);
			cap = chvt_count + 64;
			cfg.keys = calloc(cap, sizeof(CfgKey));
			if (chvt_keys) {
				memcpy(cfg.keys, chvt_keys, chvt_count * sizeof(CfgKey));
				free(chvt_keys);
			}
			cfg.keys_count = chvt_count;
			parse_first_bind = 0;
		}

		mod_str = next_token(&p);
		key_str = next_token(&p);
		func_str = next_token(&p);

		if (!mod_str || !key_str || !func_str) {
			fprintf(stderr, "config:%d: bind requires: modifiers keysym function [arg]\n", lineno);
			return -1;
		}

		mods = parse_modifiers(mod_str);
		keysym = xkb_keysym_from_name(key_str, XKB_KEYSYM_CASE_INSENSITIVE);
		if (keysym == XKB_KEY_NoSymbol) {
			fprintf(stderr, "config:%d: unknown keysym '%s'\n", lineno, key_str);
			return -1;
		}

		func = lookup_func(func_str);
		if (!func) {
			fprintf(stderr, "config:%d: unknown function '%s'\n", lineno, func_str);
			return -1;
		}

		arg = parse_bind_arg(func, p);

		cap = cfg.keys_count + 1;
		cfg.keys = realloc(cfg.keys, cap * sizeof(CfgKey));
		cfg.keys[cfg.keys_count++] = (CfgKey){ mods, keysym, func, arg };
		return 0;
	}

	/* button = mod BTN_LEFT moveresize move */
	if (strcmp(key, "button") == 0) {
		char *p = value;
		char *mod_str, *btn_str, *func_str;
		uint32_t mods;
		unsigned int button_code;
		void (*func)(const Arg *);
		Arg arg;
		if (parse_first_button) {
			free(cfg.buttons);
			cfg.buttons = NULL;
			cfg.buttons_count = 0;
			parse_first_button = 0;
		}

		mod_str = next_token(&p);
		btn_str = next_token(&p);
		func_str = next_token(&p);

		if (!mod_str || !btn_str || !func_str) {
			fprintf(stderr, "config:%d: button requires: modifiers button function [arg]\n", lineno);
			return -1;
		}

		mods = parse_modifiers(mod_str);
		button_code = parse_button_code(btn_str);
		if (!button_code) {
			fprintf(stderr, "config:%d: unknown button '%s'\n", lineno, btn_str);
			return -1;
		}

		func = lookup_func(func_str);
		if (!func) {
			fprintf(stderr, "config:%d: unknown function '%s'\n", lineno, func_str);
			return -1;
		}

		arg = parse_bind_arg(func, p);

		cfg.buttons = realloc(cfg.buttons, (cfg.buttons_count + 1) * sizeof(CfgButton));
		cfg.buttons[cfg.buttons_count++] = (CfgButton){ mods, button_code, func, arg };
		return 0;
	}

	/* rule = app_id, title, isfloating, monitor */
	if (strcmp(key, "rule") == 0) {
		char *fields[4];
		int nfields = 0;
		char *tok, *p = value;
		if (parse_first_rule) {
			size_t i;
			for (i = 0; i < cfg.rules_count; i++) {
				/* strings are in pool, freed with pool_free_all */
			}
			free(cfg.rules);
			cfg.rules = NULL;
			cfg.rules_count = 0;
			parse_first_rule = 0;
		}

		/* Split on commas */
		while (nfields < 4 && (tok = strsep(&p, ","))) {
			while (isspace((unsigned char)*tok)) tok++;
			{
				char *end = tok + strlen(tok) - 1;
				while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';
			}
			fields[nfields++] = tok;
		}

		if (nfields < 4) {
			fprintf(stderr, "config:%d: rule requires: app_id, title, isfloating, monitor\n", lineno);
			return -1;
		}

		cfg.rules = realloc(cfg.rules, (cfg.rules_count + 1) * sizeof(CfgRule));
		cfg.rules[cfg.rules_count] = (CfgRule){
			.id = (*fields[0] && strcmp(fields[0], "NULL") != 0 && strcmp(fields[0], "*") != 0) ? pool_strdup(fields[0]) : NULL,
			.title = (*fields[1] && strcmp(fields[1], "NULL") != 0 && strcmp(fields[1], "*") != 0) ? pool_strdup(fields[1]) : NULL,
			.isfloating = atoi(fields[2]),
			.monitor = atoi(fields[3]),
		};
		cfg.rules_count++;
		return 0;
	}

	/* monrule = name, mfact, nmaster, scale, layout, transform, x, y */
	if (strcmp(key, "monrule") == 0) {
		char *fields[8];
		int nfields = 0;
		char *tok, *p = value;
		if (parse_first_monrule) {
			free(cfg.monrules);
			cfg.monrules = NULL;
			cfg.monrules_count = 0;
			parse_first_monrule = 0;
		}

		while (nfields < 8 && (tok = strsep(&p, ","))) {
			while (isspace((unsigned char)*tok)) tok++;
			{
				char *end = tok + strlen(tok) - 1;
				while (end > tok && isspace((unsigned char)*end)) *end-- = '\0';
			}
			fields[nfields++] = tok;
		}

		if (nfields < 8) {
			fprintf(stderr, "config:%d: monrule requires: name, mfact, nmaster, scale, layout, transform, x, y\n", lineno);
			return -1;
		}

		{
			CfgMonitorRule mr;
			enum wl_output_transform rr = WL_OUTPUT_TRANSFORM_NORMAL;
			int layout_idx = 0;
			size_t li;

			mr.name = (*fields[0] && strcmp(fields[0], "NULL") != 0 && strcmp(fields[0], "*") != 0) ? pool_strdup(fields[0]) : NULL;
			mr.mfact = strtof(fields[1], NULL);
			mr.nmaster = atoi(fields[2]);
			mr.scale = strtof(fields[3], NULL);

			/* Layout: try by name, symbol, or index */
			for (li = 0; li < cfg.layouts_count; li++) {
				if (strcmp(cfg.layouts[li].symbol, fields[4]) == 0) {
					layout_idx = (int)li;
					break;
				}
			}
			if (strcmp(fields[4], "tile") == 0) layout_idx = 0;
			else if (strcmp(fields[4], "floating") == 0) layout_idx = 1;
			else if (strcmp(fields[4], "monocle") == 0) layout_idx = 2;
			else if (strcmp(fields[4], "scroller") == 0) layout_idx = 3;
			mr.layout_idx = layout_idx;

			parse_output_transform(fields[5], &rr);
			mr.rr = rr;
			mr.x = atoi(fields[6]);
			mr.y = atoi(fields[7]);

			cfg.monrules = realloc(cfg.monrules, (cfg.monrules_count + 1) * sizeof(CfgMonitorRule));
			cfg.monrules[cfg.monrules_count++] = mr;
		}
		return 0;
	}

	fprintf(stderr, "config:%d: unknown key '%s'\n", lineno, key);
	return -1;
}

/* --- Public API --- */

void
config_init(void)
{
	config_set_defaults();
	config_load();
}

int
config_load(void)
{
	char *path = config_path();
	FILE *f;
	char line[4096];
	int lineno = 0;
	int errors = 0;

	reset_parse_state();

	if (!path)
		return -1;

	f = fopen(path, "r");
	if (!f) {
		/* No config file is not an error - use defaults */
		if (errno == ENOENT)
			return 0;
		fprintf(stderr, "config: cannot open '%s': %s\n", path, strerror(errno));
		return -1;
	}

	fprintf(stderr, "config: loading '%s'\n", path);

	while (fgets(line, sizeof(line), f)) {
		lineno++;
		if (parse_line(line, lineno) < 0)
			errors++;
	}

	fclose(f);

	if (errors)
		fprintf(stderr, "config: %d error(s) in '%s'\n", errors, path);

	return errors ? -1 : 0;
}

int
config_reload_handler(int signal_number, void *data)
{
	Config old_cfg;
	Monitor *m;

	(void)signal_number;
	(void)data;

	fprintf(stderr, "config: reloading (SIGHUP)\n");

	/* Save old config */
	old_cfg = cfg;

	/* Clear string pool - will be rebuilt */
	cfg.string_pool = NULL;

	/* Re-set defaults and re-parse */
	config_set_defaults();
	if (config_load() < 0) {
		/* Restore old config on failure */
		fprintf(stderr, "config: reload failed, keeping old config\n");
		/* Free the new (failed) allocations */
		pool_free_all();
		free(cfg.scroller_proportions);
		free(cfg.rules);
		free(cfg.monrules);
		free(cfg.keys);
		free(cfg.buttons);
		free(cfg.termcmd);
		free(cfg.menucmd);
		free(cfg.shadow_ignore_list);
		cfg = old_cfg;
		return 0;
	}

	/* Free old config allocations */
	{
		StringPoolEntry *e, *next;
		for (e = old_cfg.string_pool; e; e = next) {
			next = e->next;
			free(e->str);
			free(e);
		}
	}
	free(old_cfg.scroller_proportions);
	free(old_cfg.rules);
	free(old_cfg.monrules);
	free(old_cfg.keys);
	free(old_cfg.buttons);
	free(old_cfg.termcmd);
	free(old_cfg.menucmd);
	free(old_cfg.shadow_ignore_list);

	/* Apply to running compositor */
	/* Update root background */
	if (root_bg)
		wlr_scene_rect_set_color(root_bg, cfg.rootcolor);

	/* Update blur scene data */
	if (cfg.blur && scene) {
		wlr_scene_set_blur_data(scene, cfg.blur_data.num_passes,
			(int)cfg.blur_data.radius, cfg.blur_data.noise,
			cfg.blur_data.brightness, cfg.blur_data.contrast,
			cfg.blur_data.saturation);
	}

	/* Update all clients */
	config_update_all_clients();

	/* Re-init keyboard */
	if (kb_group) {
		struct xkb_context *context;
		struct xkb_keymap *keymap;
		struct xkb_rule_names xkb_names = {
			.rules = cfg.xkb_rules,
			.model = cfg.xkb_model,
			.layout = cfg.xkb_layout,
			.variant = cfg.xkb_variant,
			.options = cfg.xkb_options,
		};

		context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
		keymap = xkb_keymap_new_from_names(context, &xkb_names, XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (keymap) {
			wlr_keyboard_set_keymap(&kb_group->wlr_group->keyboard, keymap);
			wlr_keyboard_set_repeat_info(&kb_group->wlr_group->keyboard, cfg.repeat_rate, cfg.repeat_delay);
			xkb_keymap_unref(keymap);
		}
		xkb_context_unref(context);
	}

	/* Re-arrange all monitors */
	wl_list_for_each(m, &mons, link) {
		arrange(m);
	}

	fprintf(stderr, "config: reload complete\n");
	return 0;
}

void
config_free(void)
{
	pool_free_all();
	free(cfg.scroller_proportions);
	free(cfg.rules);
	free(cfg.monrules);
	free(cfg.keys);
	free(cfg.buttons);
	free(cfg.termcmd);
	free(cfg.menucmd);
	free(cfg.shadow_ignore_list);
	memset(&cfg, 0, sizeof(cfg));
}

int
config_set_value(const char *key, const char *value)
{
	char line[4096];
	Monitor *m;
	int ret;

	snprintf(line, sizeof(line), "%s = %s", key, value);
	ret = parse_line(line, 0);
	if (ret < 0)
		return ret;

	/* Apply visual changes */
	if (root_bg)
		wlr_scene_rect_set_color(root_bg, cfg.rootcolor);

	if (cfg.blur && scene) {
		wlr_scene_set_blur_data(scene, cfg.blur_data.num_passes,
			(int)cfg.blur_data.radius, cfg.blur_data.noise,
			cfg.blur_data.brightness, cfg.blur_data.contrast,
			cfg.blur_data.saturation);
	}

	config_update_all_clients();

	wl_list_for_each(m, &mons, link) {
		arrange(m);
	}

	return 0;
}
