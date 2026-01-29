/*
 * ipc.c - IPC server for dwl
 * See LICENSE file for copyright and license details.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include "ipc.h"
#include "dwl.h"
#include "client.h"
#include "layout.h"
#include "monitor.h"

#ifndef VERSION
#define VERSION "unknown"
#endif

#define IPC_BUF_CAP  65536

/* --- JSON builder (append-to-buffer) --- */

typedef struct {
	char *buf;
	size_t len;
	size_t cap;
	int depth;
	int need_comma[32]; /* per nesting level */
} JsonBuf;

static void
jb_init(JsonBuf *jb)
{
	jb->cap = 4096;
	jb->buf = malloc(jb->cap);
	jb->len = 0;
	jb->depth = 0;
	memset(jb->need_comma, 0, sizeof(jb->need_comma));
}

static void
jb_ensure(JsonBuf *jb, size_t extra)
{
	while (jb->len + extra >= jb->cap) {
		jb->cap *= 2;
		jb->buf = realloc(jb->buf, jb->cap);
	}
}

static void
jb_raw(JsonBuf *jb, const char *s, size_t n)
{
	jb_ensure(jb, n);
	memcpy(jb->buf + jb->len, s, n);
	jb->len += n;
}

static void
jb_comma(JsonBuf *jb)
{
	if (jb->need_comma[jb->depth]) {
		jb_raw(jb, ",", 1);
	}
	jb->need_comma[jb->depth] = 1;
}

static void
jb_obj_begin(JsonBuf *jb)
{
	jb_comma(jb);
	jb_raw(jb, "{", 1);
	jb->depth++;
	jb->need_comma[jb->depth] = 0;
}

static void
jb_obj_end(JsonBuf *jb)
{
	jb->depth--;
	jb_raw(jb, "}", 1);
}

static void
jb_arr_begin(JsonBuf *jb)
{
	jb_comma(jb);
	jb_raw(jb, "[", 1);
	jb->depth++;
	jb->need_comma[jb->depth] = 0;
}

static void
jb_arr_end(JsonBuf *jb)
{
	jb->depth--;
	jb_raw(jb, "]", 1);
}

static void
jb_escape_string(JsonBuf *jb, const char *s)
{
	const char *p;
	jb_raw(jb, "\"", 1);
	for (p = s; *p; p++) {
		switch (*p) {
		case '"':  jb_raw(jb, "\\\"", 2); break;
		case '\\': jb_raw(jb, "\\\\", 2); break;
		case '\n': jb_raw(jb, "\\n", 2);  break;
		case '\r': jb_raw(jb, "\\r", 2);  break;
		case '\t': jb_raw(jb, "\\t", 2);  break;
		default:
			if ((unsigned char)*p < 0x20) {
				char esc[8];
				int n = snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
				jb_raw(jb, esc, (size_t)n);
			} else {
				jb_raw(jb, p, 1);
			}
		}
	}
	jb_raw(jb, "\"", 1);
}

static void
jb_key(JsonBuf *jb, const char *key)
{
	jb_comma(jb);
	jb_escape_string(jb, key);
	jb_raw(jb, ":", 1);
	jb->need_comma[jb->depth] = 0;
}

static void
jb_key_string(JsonBuf *jb, const char *key, const char *val)
{
	jb_key(jb, key);
	jb_escape_string(jb, val ? val : "");
	jb->need_comma[jb->depth] = 1;
}

static void
jb_key_int(JsonBuf *jb, const char *key, int val)
{
	char tmp[32];
	int n = snprintf(tmp, sizeof(tmp), "%d", val);
	jb_key(jb, key);
	jb_raw(jb, tmp, (size_t)n);
	jb->need_comma[jb->depth] = 1;
}

static void
jb_key_uint(JsonBuf *jb, const char *key, unsigned int val)
{
	char tmp[32];
	int n = snprintf(tmp, sizeof(tmp), "%u", val);
	jb_key(jb, key);
	jb_raw(jb, tmp, (size_t)n);
	jb->need_comma[jb->depth] = 1;
}

static void
jb_key_float(JsonBuf *jb, const char *key, float val)
{
	char tmp[64];
	int n = snprintf(tmp, sizeof(tmp), "%.6g", (double)val);
	jb_key(jb, key);
	jb_raw(jb, tmp, (size_t)n);
	jb->need_comma[jb->depth] = 1;
}

static void
jb_key_double(JsonBuf *jb, const char *key, double val)
{
	char tmp[64];
	int n = snprintf(tmp, sizeof(tmp), "%.6g", val);
	jb_key(jb, key);
	jb_raw(jb, tmp, (size_t)n);
	jb->need_comma[jb->depth] = 1;
}

static void
jb_key_bool(JsonBuf *jb, const char *key, int val)
{
	jb_key(jb, key);
	if (val)
		jb_raw(jb, "true", 4);
	else
		jb_raw(jb, "false", 5);
	jb->need_comma[jb->depth] = 1;
}

static void
jb_key_null(JsonBuf *jb, const char *key)
{
	jb_key(jb, key);
	jb_raw(jb, "null", 4);
	jb->need_comma[jb->depth] = 1;
}

static void
jb_string(JsonBuf *jb, const char *val)
{
	jb_comma(jb);
	jb_escape_string(jb, val ? val : "");
}

static char *
jb_finish(JsonBuf *jb)
{
	jb_raw(jb, "\n", 1);
	jb_ensure(jb, 1);
	jb->buf[jb->len] = '\0';
	return jb->buf;
}

/* --- Minimal JSON request parser --- */

/* Extract a string value for a given key from a flat JSON object.
 * Returns malloc'd string or NULL. */
static char *
json_get_string(const char *json, const char *key)
{
	char pattern[256];
	const char *p, *start, *end;
	char *result;
	size_t len;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return NULL;
	p += strlen(pattern);

	/* skip whitespace and colon */
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;

	if (*p != '"')
		return NULL;
	p++; /* skip opening quote */
	start = p;

	/* find closing quote, handling escapes */
	while (*p && *p != '"') {
		if (*p == '\\' && *(p+1))
			p++;
		p++;
	}
	end = p;

	len = (size_t)(end - start);
	result = malloc(len + 1);
	memcpy(result, start, len);
	result[len] = '\0';
	return result;
}

/* Extract an integer value for a given key */
static int
json_get_int(const char *json, const char *key, int *out)
{
	char pattern[256];
	const char *p;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return -1;
	p += strlen(pattern);
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;

	if (*p == '-' || (*p >= '0' && *p <= '9')) {
		*out = atoi(p);
		return 0;
	}
	return -1;
}

/* Extract a float value for a given key */
static int
json_get_float(const char *json, const char *key, float *out)
{
	char pattern[256];
	const char *p;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return -1;
	p += strlen(pattern);
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;

	if (*p == '-' || *p == '.' || (*p >= '0' && *p <= '9')) {
		*out = strtof(p, NULL);
		return 0;
	}
	return -1;
}

/* Extract an args sub-object as a raw string for further parsing.
 * Returns pointer into json (not malloc'd), sets *len. */
static const char *
json_get_object(const char *json, const char *key, size_t *len)
{
	char pattern[256];
	const char *p, *start;
	int depth = 0;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return NULL;
	p += strlen(pattern);
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;

	if (*p != '{')
		return NULL;

	start = p;
	do {
		if (*p == '{') depth++;
		else if (*p == '}') depth--;
		else if (*p == '"') {
			p++;
			while (*p && *p != '"') {
				if (*p == '\\' && *(p+1)) p++;
				p++;
			}
		}
		p++;
	} while (*p && depth > 0);

	*len = (size_t)(p - start);
	return start;
}

/* Extract a JSON array value as string array. Returns malloc'd array, sets *count.
 * Caller must free each element and the array. */
static char **
json_get_string_array(const char *json, const char *key, size_t *count)
{
	char pattern[256];
	const char *p;
	char **arr;
	size_t cap = 8;
	*count = 0;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	p = strstr(json, pattern);
	if (!p)
		return NULL;
	p += strlen(pattern);
	while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;

	if (*p != '[')
		return NULL;
	p++; /* skip [ */

	arr = malloc(cap * sizeof(char *));

	while (*p) {
		while (*p && (*p == ' ' || *p == '\t' || *p == ',')) p++;
		if (*p == ']') break;
		if (*p == '"') {
			const char *start, *end;
			size_t len;
			p++; /* skip opening quote */
			start = p;
			while (*p && *p != '"') {
				if (*p == '\\' && *(p+1)) p++;
				p++;
			}
			end = p;
			if (*p == '"') p++;
			len = (size_t)(end - start);
			if (*count >= cap) {
				cap *= 2;
				arr = realloc(arr, cap * sizeof(char *));
			}
			arr[*count] = malloc(len + 1);
			memcpy(arr[*count], start, len);
			arr[*count][len] = '\0';
			(*count)++;
		} else {
			break;
		}
	}

	return arr;
}

/* --- IPC state --- */

typedef struct IpcClient {
	int fd;
	struct wl_event_source *event_source;
	char buf[IPC_BUF_CAP];
	size_t buf_len;
	struct IpcClient *next;
} IpcClient;

static int ipc_listen_fd = -1;
static struct wl_event_source *ipc_listen_source;
static char ipc_sock_path[108]; /* matches sun_path size */
static IpcClient *ipc_clients;

/* --- Forward declarations --- */
static int ipc_accept(int fd, uint32_t mask, void *data);
static int ipc_client_readable(int fd, uint32_t mask, void *data);
static char *ipc_dispatch(const char *request);
static void ipc_client_destroy(IpcClient *ic);

/* --- Modifier name helper --- */
static const char *
mod_name(uint32_t mod)
{
	if (mod == (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL))
		return "alt+logo+shift+ctrl";
	if (mod == (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT))
		return "alt+logo+shift";
	if (mod == (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL))
		return "alt+logo+ctrl";
	if (mod == (WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL))
		return "alt+shift+ctrl";
	if (mod == (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL))
		return "logo+shift+ctrl";
	if (mod == (WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO))
		return "alt+logo";
	if (mod == (WLR_MODIFIER_ALT | WLR_MODIFIER_SHIFT))
		return "alt+shift";
	if (mod == (WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL))
		return "alt+ctrl";
	if (mod == (WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT))
		return "logo+shift";
	if (mod == (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL))
		return "logo+ctrl";
	if (mod == (WLR_MODIFIER_SHIFT | WLR_MODIFIER_CTRL))
		return "shift+ctrl";
	if (mod == WLR_MODIFIER_ALT) return "alt";
	if (mod == WLR_MODIFIER_LOGO) return "logo";
	if (mod == WLR_MODIFIER_CTRL) return "ctrl";
	if (mod == WLR_MODIFIER_SHIFT) return "shift";
	return "none";
}

/* --- Color to hex string --- */
static void
color_to_hex(const float c[4], char *out, size_t outsz)
{
	snprintf(out, outsz, "%02x%02x%02x%02x",
		(unsigned)(c[0] * 255.0f + 0.5f),
		(unsigned)(c[1] * 255.0f + 0.5f),
		(unsigned)(c[2] * 255.0f + 0.5f),
		(unsigned)(c[3] * 255.0f + 0.5f));
}

/* --- Command handlers --- */

static char *
cmd_get_monitors(void)
{
	JsonBuf jb;
	Monitor *m;
	Client *focused;

	jb_init(&jb);
	jb_obj_begin(&jb);
	jb_key_bool(&jb, "success", 1);
	jb_key(&jb, "data");
	jb_arr_begin(&jb);

	wl_list_for_each(m, &mons, link) {
		focused = focustop(m);
		jb_obj_begin(&jb);
		jb_key_string(&jb, "name", m->wlr_output->name);
		jb_key_string(&jb, "layout", m->ltsymbol);
		jb_key_float(&jb, "mfact", m->mfact);
		jb_key_int(&jb, "nmaster", m->nmaster);
		jb_key(&jb, "area");
		jb_obj_begin(&jb);
		jb_key_int(&jb, "x", m->m.x);
		jb_key_int(&jb, "y", m->m.y);
		jb_key_int(&jb, "width", m->m.width);
		jb_key_int(&jb, "height", m->m.height);
		jb_obj_end(&jb);
		jb_key(&jb, "window_area");
		jb_obj_begin(&jb);
		jb_key_int(&jb, "x", m->w.x);
		jb_key_int(&jb, "y", m->w.y);
		jb_key_int(&jb, "width", m->w.width);
		jb_key_int(&jb, "height", m->w.height);
		jb_obj_end(&jb);
		jb_key_int(&jb, "gappih", m->gappih);
		jb_key_int(&jb, "gappiv", m->gappiv);
		jb_key_int(&jb, "gappoh", m->gappoh);
		jb_key_int(&jb, "gappov", m->gappov);
		jb_key_bool(&jb, "focused", m == selmon);
		jb_key_bool(&jb, "asleep", m->asleep);
		jb_key_bool(&jb, "has_focused_client", focused != NULL);
		jb_obj_end(&jb);
	}

	jb_arr_end(&jb);
	jb_obj_end(&jb);
	return jb_finish(&jb);
}

static char *
cmd_get_clients(void)
{
	JsonBuf jb;
	Client *c, *focused;

	focused = selmon ? focustop(selmon) : NULL;

	jb_init(&jb);
	jb_obj_begin(&jb);
	jb_key_bool(&jb, "success", 1);
	jb_key(&jb, "data");
	jb_arr_begin(&jb);

	wl_list_for_each(c, &clients, link) {
		jb_obj_begin(&jb);
		jb_key_string(&jb, "app_id", client_get_appid(c));
		jb_key_string(&jb, "title", client_get_title(c));
		jb_key_string(&jb, "monitor", c->mon ? c->mon->wlr_output->name : "");
		jb_key(&jb, "geometry");
		jb_obj_begin(&jb);
		jb_key_int(&jb, "x", c->geom.x);
		jb_key_int(&jb, "y", c->geom.y);
		jb_key_int(&jb, "width", c->geom.width);
		jb_key_int(&jb, "height", c->geom.height);
		jb_obj_end(&jb);
		jb_key_bool(&jb, "floating", c->isfloating);
		jb_key_bool(&jb, "fullscreen", c->isfullscreen);
		jb_key_bool(&jb, "urgent", c->isurgent);
		jb_key_bool(&jb, "focused", c == focused);
		jb_key_float(&jb, "opacity", c->opacity);
		jb_key_int(&jb, "corner_radius", c->corner_radius);
		jb_key_int(&jb, "scroller_col", c->scroller_col);
		jb_obj_end(&jb);
	}

	jb_arr_end(&jb);
	jb_obj_end(&jb);
	return jb_finish(&jb);
}

static char *
cmd_get_focused(void)
{
	JsonBuf jb;
	Client *c;

	c = selmon ? focustop(selmon) : NULL;

	jb_init(&jb);
	jb_obj_begin(&jb);
	jb_key_bool(&jb, "success", 1);

	if (!c) {
		jb_key_null(&jb, "data");
	} else {
		jb_key(&jb, "data");
		jb_obj_begin(&jb);
		jb_key_string(&jb, "app_id", client_get_appid(c));
		jb_key_string(&jb, "title", client_get_title(c));
		jb_key_string(&jb, "monitor", c->mon ? c->mon->wlr_output->name : "");
		jb_key(&jb, "geometry");
		jb_obj_begin(&jb);
		jb_key_int(&jb, "x", c->geom.x);
		jb_key_int(&jb, "y", c->geom.y);
		jb_key_int(&jb, "width", c->geom.width);
		jb_key_int(&jb, "height", c->geom.height);
		jb_obj_end(&jb);
		jb_key_bool(&jb, "floating", c->isfloating);
		jb_key_bool(&jb, "fullscreen", c->isfullscreen);
		jb_key_bool(&jb, "urgent", c->isurgent);
		jb_key_float(&jb, "opacity", c->opacity);
		jb_key_int(&jb, "corner_radius", c->corner_radius);
		jb_key_int(&jb, "scroller_col", c->scroller_col);
		jb_obj_end(&jb);
	}

	jb_obj_end(&jb);
	return jb_finish(&jb);
}

static void
jb_color(JsonBuf *jb, const char *key, const float c[4])
{
	char hex[16];
	color_to_hex(c, hex, sizeof(hex));
	jb_key_string(jb, key, hex);
}

static char *
cmd_get_config(void)
{
	JsonBuf jb;
	size_t i;

	jb_init(&jb);
	jb_obj_begin(&jb);
	jb_key_bool(&jb, "success", 1);
	jb_key(&jb, "data");
	jb_obj_begin(&jb);

	/* Appearance */
	jb_key_uint(&jb, "borderpx", cfg.borderpx);
	jb_key_int(&jb, "sloppyfocus", cfg.sloppyfocus);
	jb_key_int(&jb, "bypass_surface_visibility", cfg.bypass_surface_visibility);
	jb_key_int(&jb, "smartgaps", cfg.smartgaps);
	jb_key_int(&jb, "monoclegaps", cfg.monoclegaps);
	jb_key_uint(&jb, "gappih", cfg.gappih);
	jb_key_uint(&jb, "gappiv", cfg.gappiv);
	jb_key_uint(&jb, "gappoh", cfg.gappoh);
	jb_key_uint(&jb, "gappov", cfg.gappov);

	/* Colors */
	jb_color(&jb, "rootcolor", cfg.rootcolor);
	jb_color(&jb, "bordercolor", cfg.bordercolor);
	jb_color(&jb, "focuscolor", cfg.focuscolor);
	jb_color(&jb, "urgentcolor", cfg.urgentcolor);
	jb_color(&jb, "fullscreen_bg", cfg.fullscreen_bg);

	/* Opacity */
	jb_key_int(&jb, "opacity", cfg.opacity);
	jb_key_float(&jb, "opacity_inactive", cfg.opacity_inactive);
	jb_key_float(&jb, "opacity_active", cfg.opacity_active);

	/* Shadows */
	jb_key_int(&jb, "shadow", cfg.shadow);
	jb_key_int(&jb, "shadow_only_floating", cfg.shadow_only_floating);
	jb_color(&jb, "shadow_color", cfg.shadow_color);
	jb_color(&jb, "shadow_color_focus", cfg.shadow_color_focus);
	jb_key_int(&jb, "shadow_blur_sigma", cfg.shadow_blur_sigma);
	jb_key_int(&jb, "shadow_blur_sigma_focus", cfg.shadow_blur_sigma_focus);

	/* Corner radius */
	jb_key_int(&jb, "corner_radius", cfg.corner_radius);
	jb_key_int(&jb, "corner_radius_inner", cfg.corner_radius_inner);
	jb_key_int(&jb, "corner_radius_only_floating", cfg.corner_radius_only_floating);

	/* Blur */
	jb_key_int(&jb, "blur", cfg.blur);
	jb_key_int(&jb, "blur_xray", cfg.blur_xray);
	jb_key_int(&jb, "blur_ignore_transparent", cfg.blur_ignore_transparent);
	jb_key_int(&jb, "blur_num_passes", cfg.blur_data.num_passes);
	jb_key_float(&jb, "blur_radius", cfg.blur_data.radius);
	jb_key_float(&jb, "blur_noise", cfg.blur_data.noise);
	jb_key_float(&jb, "blur_brightness", cfg.blur_data.brightness);
	jb_key_float(&jb, "blur_contrast", cfg.blur_data.contrast);
	jb_key_float(&jb, "blur_saturation", cfg.blur_data.saturation);

	/* Scroller */
	jb_key_int(&jb, "scroller_center_mode", cfg.scroller_center_mode);
	jb_key_int(&jb, "scroller_default_proportion", cfg.scroller_default_proportion);
	jb_key(&jb, "scroller_proportions");
	jb_arr_begin(&jb);
	for (i = 0; i < cfg.scroller_proportions_count; i++) {
		char tmp[32];
		int n = snprintf(tmp, sizeof(tmp), "%.6g", (double)cfg.scroller_proportions[i]);
		jb_comma(&jb);
		jb_raw(&jb, tmp, (size_t)n);
		jb.need_comma[jb.depth] = 1;
	}
	jb_arr_end(&jb);

	/* Keyboard */
	jb_key_string(&jb, "xkb_rules", cfg.xkb_rules ? cfg.xkb_rules : "");
	jb_key_string(&jb, "xkb_model", cfg.xkb_model ? cfg.xkb_model : "");
	jb_key_string(&jb, "xkb_layout", cfg.xkb_layout ? cfg.xkb_layout : "");
	jb_key_string(&jb, "xkb_variant", cfg.xkb_variant ? cfg.xkb_variant : "");
	jb_key_string(&jb, "xkb_options", cfg.xkb_options ? cfg.xkb_options : "");
	jb_key_int(&jb, "repeat_rate", cfg.repeat_rate);
	jb_key_int(&jb, "repeat_delay", cfg.repeat_delay);
	jb_key_int(&jb, "numlock", cfg.numlock);

	/* Trackpad */
	jb_key_int(&jb, "tap_to_click", cfg.tap_to_click);
	jb_key_int(&jb, "tap_and_drag", cfg.tap_and_drag);
	jb_key_int(&jb, "drag_lock", cfg.drag_lock);
	jb_key_int(&jb, "natural_scrolling", cfg.natural_scrolling);
	jb_key_int(&jb, "disable_while_typing", cfg.disable_while_typing);
	jb_key_int(&jb, "left_handed", cfg.left_handed);
	jb_key_int(&jb, "middle_button_emulation", cfg.middle_button_emulation);
	jb_key_double(&jb, "accel_speed", cfg.accel_speed);

	/* Modifier */
	jb_key_string(&jb, "modkey", mod_name(cfg.modkey));

	/* Commands */
	jb_key(&jb, "termcmd");
	jb_arr_begin(&jb);
	if (cfg.termcmd) {
		for (i = 0; cfg.termcmd[i]; i++)
			jb_string(&jb, cfg.termcmd[i]);
	}
	jb_arr_end(&jb);
	jb_key(&jb, "menucmd");
	jb_arr_begin(&jb);
	if (cfg.menucmd) {
		for (i = 0; cfg.menucmd[i]; i++)
			jb_string(&jb, cfg.menucmd[i]);
	}
	jb_arr_end(&jb);

	jb_obj_end(&jb);
	jb_obj_end(&jb);
	return jb_finish(&jb);
}

static char *
cmd_get_keybinds(void)
{
	JsonBuf jb;
	size_t i;

	jb_init(&jb);
	jb_obj_begin(&jb);
	jb_key_bool(&jb, "success", 1);
	jb_key(&jb, "data");
	jb_arr_begin(&jb);

	for (i = 0; i < cfg.keys_count; i++) {
		char keysym_name[64];
		const char *fname;

		xkb_keysym_get_name(cfg.keys[i].keysym, keysym_name, sizeof(keysym_name));
		fname = lookup_func_name(cfg.keys[i].func);

		jb_obj_begin(&jb);
		jb_key_uint(&jb, "mod", cfg.keys[i].mod);
		jb_key_string(&jb, "mod_name", mod_name(cfg.keys[i].mod));
		jb_key_string(&jb, "keysym", keysym_name);
		jb_key_string(&jb, "function", fname ? fname : "unknown");

		/* Serialize the arg based on function type */
		if (cfg.keys[i].func == spawn && cfg.keys[i].arg.v) {
			const char *const *argv = cfg.keys[i].arg.v;
			jb_key(&jb, "arg");
			jb_arr_begin(&jb);
			while (*argv) {
				jb_string(&jb, *argv);
				argv++;
			}
			jb_arr_end(&jb);
		} else if (cfg.keys[i].func == setlayout && cfg.keys[i].arg.v) {
			const Layout *lt = cfg.keys[i].arg.v;
			jb_key_string(&jb, "arg", lt->symbol);
		} else if (cfg.keys[i].func == setmfact) {
			jb_key_float(&jb, "arg", cfg.keys[i].arg.f);
		} else {
			jb_key_int(&jb, "arg", cfg.keys[i].arg.i);
		}

		jb_obj_end(&jb);
	}

	jb_arr_end(&jb);
	jb_obj_end(&jb);
	return jb_finish(&jb);
}

static char *
cmd_get_layouts(void)
{
	JsonBuf jb;
	size_t i;
	const char *names[] = {"tile", "floating", "monocle", "scroller"};

	jb_init(&jb);
	jb_obj_begin(&jb);
	jb_key_bool(&jb, "success", 1);
	jb_key(&jb, "data");
	jb_arr_begin(&jb);

	for (i = 0; i < cfg.layouts_count; i++) {
		jb_obj_begin(&jb);
		jb_key_string(&jb, "symbol", cfg.layouts[i].symbol);
		jb_key_string(&jb, "name", i < 4 ? names[i] : "unknown");
		jb_obj_end(&jb);
	}

	jb_arr_end(&jb);
	jb_obj_end(&jb);
	return jb_finish(&jb);
}

static char *
cmd_version(void)
{
	JsonBuf jb;
	jb_init(&jb);
	jb_obj_begin(&jb);
	jb_key_bool(&jb, "success", 1);
	jb_key_string(&jb, "data", VERSION);
	jb_obj_end(&jb);
	return jb_finish(&jb);
}

static char *
cmd_action(const char *args_json)
{
	char *name;
	void (*func)(const Arg *);
	Arg arg = {0};

	if (!args_json) {
		JsonBuf jb;
		jb_init(&jb);
		jb_obj_begin(&jb);
		jb_key_bool(&jb, "success", 0);
		jb_key_string(&jb, "error", "missing args");
		jb_obj_end(&jb);
		return jb_finish(&jb);
	}

	name = json_get_string(args_json, "name");
	if (!name) {
		JsonBuf jb;
		jb_init(&jb);
		jb_obj_begin(&jb);
		jb_key_bool(&jb, "success", 0);
		jb_key_string(&jb, "error", "missing action name");
		jb_obj_end(&jb);
		return jb_finish(&jb);
	}

	func = lookup_func(name);
	if (!func) {
		JsonBuf jb;
		jb_init(&jb);
		jb_obj_begin(&jb);
		jb_key_bool(&jb, "success", 0);
		jb_key_string(&jb, "error", "unknown action");
		jb_obj_end(&jb);
		free(name);
		return jb_finish(&jb);
	}

	/* Parse arg based on function type */
	if (func == spawn) {
		size_t count = 0;
		char **argv = json_get_string_array(args_json, "value", &count);
		if (argv) {
			/* Add NULL terminator */
			argv = realloc(argv, (count + 1) * sizeof(char *));
			argv[count] = NULL;
			arg.v = (const void *)argv;
		} else {
			/* Try single string value */
			char *val = json_get_string(args_json, "value");
			if (val) {
				argv = malloc(3 * sizeof(char *));
				argv[0] = val;
				argv[1] = NULL;
				arg.v = (const void *)argv;
			}
		}
	} else if (func == setlayout) {
		char *val = json_get_string(args_json, "value");
		if (val) {
			if (strcmp(val, "tile") == 0) arg.v = &cfg.layouts[0];
			else if (strcmp(val, "floating") == 0) arg.v = &cfg.layouts[1];
			else if (strcmp(val, "monocle") == 0) arg.v = &cfg.layouts[2];
			else if (strcmp(val, "scroller") == 0) arg.v = &cfg.layouts[3];
			else {
				/* Try by symbol */
				size_t i;
				for (i = 0; i < cfg.layouts_count; i++) {
					if (strcmp(cfg.layouts[i].symbol, val) == 0) {
						arg.v = &cfg.layouts[i];
						break;
					}
				}
			}
			free(val);
		}
	} else if (func == setmfact) {
		float fval;
		if (json_get_float(args_json, "value", &fval) == 0)
			arg.f = fval;
	} else if (func == moveresize) {
		char *val = json_get_string(args_json, "value");
		if (val) {
			if (strcmp(val, "move") == 0) arg.ui = CurMove;
			else if (strcmp(val, "resize") == 0) arg.ui = CurResize;
			else arg.ui = (uint32_t)atoi(val);
			free(val);
		} else {
			int ival;
			if (json_get_int(args_json, "value", &ival) == 0)
				arg.ui = (uint32_t)ival;
		}
	} else if (func == focusmon || func == movetomon) {
		char *val = json_get_string(args_json, "value");
		if (val) {
			if (strcmp(val, "left") == 0) arg.i = WLR_DIRECTION_LEFT;
			else if (strcmp(val, "right") == 0) arg.i = WLR_DIRECTION_RIGHT;
			else arg.i = atoi(val);
			free(val);
		} else {
			int ival;
			if (json_get_int(args_json, "value", &ival) == 0)
				arg.i = ival;
		}
	} else if (func == chvt) {
		int ival;
		if (json_get_int(args_json, "value", &ival) == 0)
			arg.ui = (uint32_t)ival;
	} else {
		/* Generic: try float first, then int */
		float fval;
		int ival;
		if (json_get_float(args_json, "value", &fval) == 0) {
			/* Check if it looks like a float (has decimal point) */
			char *val = json_get_string(args_json, "value");
			if (val && strchr(val, '.'))
				arg.f = fval;
			else
				arg.i = (int)fval;
			free(val);
		} else if (json_get_int(args_json, "value", &ival) == 0) {
			arg.i = ival;
		}
	}

	func(&arg);

	/* Free spawn argv if allocated */
	if (func == spawn && arg.v) {
		char **argv = (char **)arg.v;
		size_t i;
		for (i = 0; argv[i]; i++)
			free(argv[i]);
		free(argv);
	}

	free(name);

	{
		JsonBuf jb;
		jb_init(&jb);
		jb_obj_begin(&jb);
		jb_key_bool(&jb, "success", 1);
		jb_key_null(&jb, "data");
		jb_obj_end(&jb);
		return jb_finish(&jb);
	}
}

static char *
cmd_config_set(const char *args_json)
{
	char *key, *value;
	int ret;

	if (!args_json) {
		JsonBuf jb;
		jb_init(&jb);
		jb_obj_begin(&jb);
		jb_key_bool(&jb, "success", 0);
		jb_key_string(&jb, "error", "missing args");
		jb_obj_end(&jb);
		return jb_finish(&jb);
	}

	key = json_get_string(args_json, "key");
	value = json_get_string(args_json, "value");

	if (!key || !value) {
		JsonBuf jb;
		free(key);
		free(value);
		jb_init(&jb);
		jb_obj_begin(&jb);
		jb_key_bool(&jb, "success", 0);
		jb_key_string(&jb, "error", "missing key or value");
		jb_obj_end(&jb);
		return jb_finish(&jb);
	}

	ret = config_set_value(key, value);
	free(key);
	free(value);

	{
		JsonBuf jb;
		jb_init(&jb);
		jb_obj_begin(&jb);
		if (ret == 0) {
			jb_key_bool(&jb, "success", 1);
			jb_key_null(&jb, "data");
		} else {
			jb_key_bool(&jb, "success", 0);
			jb_key_string(&jb, "error", "failed to set config value");
		}
		jb_obj_end(&jb);
		return jb_finish(&jb);
	}
}

static char *
cmd_config_reload(void)
{
	config_reload_handler(0, NULL);

	{
		JsonBuf jb;
		jb_init(&jb);
		jb_obj_begin(&jb);
		jb_key_bool(&jb, "success", 1);
		jb_key_null(&jb, "data");
		jb_obj_end(&jb);
		return jb_finish(&jb);
	}
}

static char *
ipc_error(const char *msg)
{
	JsonBuf jb;
	jb_init(&jb);
	jb_obj_begin(&jb);
	jb_key_bool(&jb, "success", 0);
	jb_key_string(&jb, "error", msg);
	jb_obj_end(&jb);
	return jb_finish(&jb);
}

/* --- Command dispatch --- */
static char *
ipc_dispatch(const char *request)
{
	char *command;
	char *result;

	command = json_get_string(request, "command");
	if (!command)
		return ipc_error("missing command");

	if (strcmp(command, "get_monitors") == 0) {
		result = cmd_get_monitors();
	} else if (strcmp(command, "get_clients") == 0) {
		result = cmd_get_clients();
	} else if (strcmp(command, "get_focused") == 0) {
		result = cmd_get_focused();
	} else if (strcmp(command, "get_config") == 0) {
		result = cmd_get_config();
	} else if (strcmp(command, "get_keybinds") == 0) {
		result = cmd_get_keybinds();
	} else if (strcmp(command, "get_layouts") == 0) {
		result = cmd_get_layouts();
	} else if (strcmp(command, "version") == 0) {
		result = cmd_version();
	} else if (strcmp(command, "action") == 0) {
		size_t args_len;
		const char *args = json_get_object(request, "args", &args_len);
		if (args) {
			/* Make a null-terminated copy */
			char *args_copy = malloc(args_len + 1);
			memcpy(args_copy, args, args_len);
			args_copy[args_len] = '\0';
			result = cmd_action(args_copy);
			free(args_copy);
		} else {
			result = cmd_action(NULL);
		}
	} else if (strcmp(command, "config_set") == 0) {
		size_t args_len;
		const char *args = json_get_object(request, "args", &args_len);
		if (args) {
			char *args_copy = malloc(args_len + 1);
			memcpy(args_copy, args, args_len);
			args_copy[args_len] = '\0';
			result = cmd_config_set(args_copy);
			free(args_copy);
		} else {
			result = cmd_config_set(NULL);
		}
	} else if (strcmp(command, "config_reload") == 0) {
		result = cmd_config_reload();
	} else {
		result = ipc_error("unknown command");
	}

	free(command);
	return result;
}

/* --- Socket lifecycle --- */

static void
ipc_client_destroy(IpcClient *ic)
{
	IpcClient **pp;

	if (ic->event_source)
		wl_event_source_remove(ic->event_source);
	if (ic->fd >= 0)
		close(ic->fd);

	/* Remove from linked list */
	for (pp = &ipc_clients; *pp; pp = &(*pp)->next) {
		if (*pp == ic) {
			*pp = ic->next;
			break;
		}
	}

	free(ic);
}

static int
ipc_client_readable(int fd, uint32_t mask, void *data)
{
	IpcClient *ic = data;
	ssize_t n;

	(void)fd;

	if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
		ipc_client_destroy(ic);
		return 0;
	}

	n = read(ic->fd, ic->buf + ic->buf_len, IPC_BUF_CAP - ic->buf_len - 1);
	if (n <= 0) {
		ipc_client_destroy(ic);
		return 0;
	}
	ic->buf_len += (size_t)n;
	ic->buf[ic->buf_len] = '\0';

	/* Check for newline delimiter */
	if (memchr(ic->buf, '\n', ic->buf_len)) {
		char *response = ipc_dispatch(ic->buf);
		if (response) {
			size_t resp_len = strlen(response);
			size_t written = 0;
			while (written < resp_len) {
				ssize_t w = write(ic->fd, response + written, resp_len - written);
				if (w <= 0) break;
				written += (size_t)w;
			}
			free(response);
		}
		ipc_client_destroy(ic);
		return 0;
	}

	/* Buffer overflow protection */
	if (ic->buf_len >= IPC_BUF_CAP - 1) {
		char *response = ipc_error("request too large");
		if (response) {
			if (write(ic->fd, response, strlen(response)) < 0) {
				/* best-effort error response */
			}
			free(response);
		}
		ipc_client_destroy(ic);
		return 0;
	}

	return 0;
}

static int
ipc_accept(int fd, uint32_t mask, void *data)
{
	IpcClient *ic;
	int client_fd;

	(void)mask;
	(void)data;

	client_fd = accept(fd, NULL, NULL);
	if (client_fd < 0)
		return 0;

	fd_set_nonblock(client_fd);

	ic = calloc(1, sizeof(*ic));
	ic->fd = client_fd;
	ic->event_source = wl_event_loop_add_fd(event_loop, client_fd,
		WL_EVENT_READABLE, ipc_client_readable, ic);

	/* Add to linked list */
	ic->next = ipc_clients;
	ipc_clients = ic;

	return 0;
}

/* --- Public API --- */

void
ipc_init(void)
{
	struct sockaddr_un addr;
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	const char *wayland_display = getenv("WAYLAND_DISPLAY");

	if (!runtime_dir || !wayland_display) {
		fprintf(stderr, "ipc: cannot determine socket path (missing XDG_RUNTIME_DIR or WAYLAND_DISPLAY)\n");
		return;
	}

	snprintf(ipc_sock_path, sizeof(ipc_sock_path),
		"%s/dwl-ipc.%s.sock", runtime_dir, wayland_display);

	/* Remove stale socket */
	unlink(ipc_sock_path);

	ipc_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (ipc_listen_fd < 0) {
		perror("ipc: socket");
		return;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", ipc_sock_path);

	if (bind(ipc_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("ipc: bind");
		close(ipc_listen_fd);
		ipc_listen_fd = -1;
		return;
	}

	if (listen(ipc_listen_fd, 8) < 0) {
		perror("ipc: listen");
		close(ipc_listen_fd);
		unlink(ipc_sock_path);
		ipc_listen_fd = -1;
		return;
	}

	fd_set_nonblock(ipc_listen_fd);

	ipc_listen_source = wl_event_loop_add_fd(event_loop, ipc_listen_fd,
		WL_EVENT_READABLE, ipc_accept, NULL);

	setenv("DWL_SOCK", ipc_sock_path, 1);
	fprintf(stderr, "ipc: listening on %s\n", ipc_sock_path);
}

void
ipc_cleanup(void)
{
	/* Close all client connections */
	while (ipc_clients)
		ipc_client_destroy(ipc_clients);

	if (ipc_listen_source) {
		wl_event_source_remove(ipc_listen_source);
		ipc_listen_source = NULL;
	}

	if (ipc_listen_fd >= 0) {
		close(ipc_listen_fd);
		ipc_listen_fd = -1;
	}

	if (ipc_sock_path[0]) {
		unlink(ipc_sock_path);
		ipc_sock_path[0] = '\0';
	}
}
