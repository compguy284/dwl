/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const int smartgaps                 = 0;  /* 1 means no outer gap when there is only one window */
static const int monoclegaps               = 0;  /* 1 means outer gaps in monocle layout */
static const unsigned int borderpx         = 1;  /* border pixel of windows */
static const unsigned int gappih           = 10; /* horiz inner gap between windows */
static const unsigned int gappiv           = 10; /* vert inner gap between windows */
static const unsigned int gappoh           = 10; /* horiz outer gap between windows and screen edge */
static const unsigned int gappov           = 10; /* vert outer gap between windows and screen edge */
static const float rootcolor[]             = COLOR(0x222222ff);
static const float bordercolor[]           = COLOR(0x444444ff);
static const float focuscolor[]            = COLOR(0x005577ff);
static const float urgentcolor[]           = COLOR(0xff0000ff);
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 0.0f}; /* You can also use glsl colors */

static const int opacity = 0; /* flag to enable opacity */
static const float opacity_inactive = 0.5;
static const float opacity_active = 1.0;

static const int shadow = 1; /* flag to enable shadow */
static const int shadow_only_floating = 0; /* only apply shadow to floating windows */
static const float shadow_color[4] = COLOR(0x0000FFff);
static const float shadow_color_focus[4] = COLOR(0xFF0000ff);
static const int shadow_blur_sigma = 20;
static const int shadow_blur_sigma_focus = 40;
static const char *const shadow_ignore_list[] = { NULL }; /* list of app-id to ignore */

static const int corner_radius = 8; /* 0 disables corner_radius */
static const int corner_radius_inner = 9; /* 0 disables corner_radius */
static const int corner_radius_only_floating = 0; /* only apply corner_radius to floating windows */

static const int blur = 1; /* flag to enable blur */
static const int blur_xray = 0; /* flag to make transparent fs and floating windows display your background */
static const int blur_ignore_transparent = 1;
static const struct blur_data blur_data = {
	.num_passes = 3,
	.radius = 5.0f,
	.noise = 0.02f,
	.brightness = 0.9f,
	.contrast = 0.9f,
	.saturation = 1.1f,
};

/* logging */
static int log_level = WLR_ERROR;

/* Scroller layout */
enum { ScrollerCenterAlways, ScrollerCenterOnOverflow };  /* Centering modes */
static const int scroller_center_mode = ScrollerCenterOnOverflow;  /* Centering behavior */
static const float scroller_proportions[] = { 0.5f, 0.66f, 0.8f, 1.0f };  /* Column width as fraction of screen */
static const int scroller_default_proportion = 0;  /* Index into scroller_proportions */

static const Rule rules[] = {
	/* app_id             title       isfloating   monitor */
	{ "Gimp_EXAMPLE",     NULL,       1,           -1 }, /* Start floating, not tiled */
	{ "firefox_EXAMPLE",  NULL,       0,           -1 },
};

/* layout(s) */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
	{ "|S|",      scroller },
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator
 * WARNING: negative values other than (-1, -1) cause problems with Xwayland clients due to
 * https://gitlab.freedesktop.org/xorg/xserver/-/issues/899 */
static const MonitorRule monrules[] = {
   /* name        mfact  nmaster scale layout       rotate/reflect                x    y
    * example of a HiDPI laptop monitor:
    { "eDP-1",    0.5f,  1,      2,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 }, */
	{ "HDMI-A-1",  0.5f,  1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,    0,    0 },
	{ "DP-1",      0.5f,  1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL, 1920,    0 },
	{ NULL,       0.55f,  1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,   -1 },
	/* default monitor rule: can be changed but cannot be eliminated; at least one monitor rule must exist */
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	/* example:
	.options = "ctrl:nocaps",
	*/
	.options = NULL,
};

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* numlock */
static const int numlock = 1;

/* Trackpad */
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static const int natural_scrolling = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
/* You can choose between:
LIBINPUT_CONFIG_SCROLL_NO_SCROLL
LIBINPUT_CONFIG_SCROLL_2FG
LIBINPUT_CONFIG_SCROLL_EDGE
LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN
*/
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;

/* You can choose between:
LIBINPUT_CONFIG_CLICK_METHOD_NONE
LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS
LIBINPUT_CONFIG_CLICK_METHOD_CLICKFINGER
*/
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;

/* You can choose between:
LIBINPUT_CONFIG_SEND_EVENTS_ENABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED
LIBINPUT_CONFIG_SEND_EVENTS_DISABLED_ON_EXTERNAL_MOUSE
*/
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;

/* You can choose between:
LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT
LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
*/
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;

/* You can choose between:
LIBINPUT_CONFIG_TAP_MAP_LRM -- 1/2/3 finger tap maps to left/right/middle
LIBINPUT_CONFIG_TAP_MAP_LMR -- 1/2/3 finger tap maps to left/middle/right
*/
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* If you want to use the windows key for MODKEY, use WLR_MODIFIER_LOGO */
#define MODKEY WLR_MODIFIER_ALT

/* helper for spawning shell commands in the pre dwm-5.0 fashion */
#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

/* commands */
static const char *termcmd[] = { "alacritty", NULL };
static const char *menucmd[] = { "wmenu-run", NULL };

static const Key keys[] = {
	/* Note that Shift changes certain key codes: 2 -> at, etc. */
	/* modifier                  key                  function          argument */
	{ MODKEY,                    XKB_KEY_p,           spawn,            {.v = menucmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Return,      spawn,            {.v = termcmd} },
	{ MODKEY,                    XKB_KEY_j,           focusstack,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_k,           focusstack,       {.i = -1} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_Left,       focusdir,         {.ui = 0} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_Right,      focusdir,         {.ui = 1} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_Up,         focusdir,         {.ui = 2} },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_Down,       focusdir,         {.ui = 3} },
	{ MODKEY,                    XKB_KEY_i,           incnmaster,       {.i = +1} },
	{ MODKEY,                    XKB_KEY_d,           incnmaster,       {.i = -1} },
	{ MODKEY,                    XKB_KEY_h,           setmfact,         {.f = -0.05f} },
	{ MODKEY,                    XKB_KEY_l,           setmfact,         {.f = +0.05f} },
	{ MODKEY|WLR_MODIFIER_LOGO,  XKB_KEY_h,          incgaps,       {.i = +1 } },
	{ MODKEY|WLR_MODIFIER_LOGO,  XKB_KEY_l,          incgaps,       {.i = -1 } },
	{ MODKEY|WLR_MODIFIER_LOGO|WLR_MODIFIER_SHIFT,   XKB_KEY_H,      incogaps,      {.i = +1 } },
	{ MODKEY|WLR_MODIFIER_LOGO|WLR_MODIFIER_SHIFT,   XKB_KEY_L,      incogaps,      {.i = -1 } },
	{ MODKEY|WLR_MODIFIER_LOGO|WLR_MODIFIER_CTRL,    XKB_KEY_h,      incigaps,      {.i = +1 } },
	{ MODKEY|WLR_MODIFIER_LOGO|WLR_MODIFIER_CTRL,    XKB_KEY_l,      incigaps,      {.i = -1 } },
	{ MODKEY|WLR_MODIFIER_LOGO,  XKB_KEY_0,          togglegaps,     {0} },
	{ MODKEY|WLR_MODIFIER_LOGO|WLR_MODIFIER_SHIFT,   XKB_KEY_parenright,defaultgaps,    {0} },
	{ MODKEY,                    XKB_KEY_y,          incihgaps,     {.i = +1 } },
	{ MODKEY,                    XKB_KEY_o,          incihgaps,     {.i = -1 } },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_y,          incivgaps,     {.i = +1 } },
	{ MODKEY|WLR_MODIFIER_CTRL,  XKB_KEY_o,          incivgaps,     {.i = -1 } },
	{ MODKEY|WLR_MODIFIER_LOGO,  XKB_KEY_y,          incohgaps,     {.i = +1 } },
	{ MODKEY|WLR_MODIFIER_LOGO,  XKB_KEY_o,          incohgaps,     {.i = -1 } },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Y,          incovgaps,     {.i = +1 } },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_O,          incovgaps,     {.i = -1 } },
	{ MODKEY,                    XKB_KEY_Return,      zoom,             {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_c,           killclient,       {0} },
	{ MODKEY,                    XKB_KEY_t,           setlayout,        {.v = &layouts[0]} },
	{ MODKEY,                    XKB_KEY_f,           setlayout,        {.v = &layouts[1]} },
	{ MODKEY,                    XKB_KEY_m,           setlayout,        {.v = &layouts[2]} },
	{ MODKEY,                    XKB_KEY_s,           setlayout,        {.v = &layouts[3]} },
	{ MODKEY,                    XKB_KEY_minus,       scroller_cycle_proportion, {.i = -1} },
	{ MODKEY,                    XKB_KEY_equal,       scroller_cycle_proportion, {.i = +1} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Left,        consume_or_expel, {.i = 0} }, /* consume/expel left */
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_Right,       consume_or_expel, {.i = 1} }, /* consume/expel right */
	{ MODKEY,                    XKB_KEY_space,       setlayout,        {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_space,       togglefloating,   {0} },
	{ MODKEY,                    XKB_KEY_e,           togglefullscreen, {0} },
	{ MODKEY,                    XKB_KEY_comma,       focusmon,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,                    XKB_KEY_period,      focusmon,         {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_less,        movetomon,        {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_greater,     movetomon,        {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|WLR_MODIFIER_SHIFT, XKB_KEY_q,           quit,             {0} },

	/* Ctrl-Alt-Backspace and Ctrl-Alt-Fx used to be handled by X server */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_Terminate_Server, quit, {0} },
	/* Ctrl-Alt-Fx is used to switch to another VT, if you don't know what a VT is
	 * do not remove them.
	 */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
};
