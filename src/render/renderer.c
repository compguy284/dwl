#include "render.h"
#include "compositor.h"
#include "config.h"
#include "client.h"
#include <stdlib.h>
#include <string.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>
#include <scenefx/types/fx/blur_data.h>

struct SwlRenderer {
    SwlCompositor *comp;
    struct fx_renderer *fx;
    SwlRenderConfig config;
};

SwlRenderer *swl_renderer_create(SwlCompositor *comp)
{
    SwlRenderer *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    r->comp = comp;

    SwlConfig *cfg = swl_compositor_get_config(comp);

    // Blur settings
    r->config.blur_radius = swl_config_get_int(cfg, "scenefx.blur.radius", 5);
    r->config.blur_passes = swl_config_get_int(cfg, "scenefx.blur.passes", 3);
    r->config.blur_optimize = swl_config_get_bool(cfg, "scenefx.blur.optimized", true);
    r->config.blur_ignore_transparent = swl_config_get_bool(cfg, "scenefx.blur.ignore_transparent", true);

    // Shadow settings
    r->config.shadow_enabled = swl_config_get_bool(cfg, "scenefx.shadows.enabled", true);
    r->config.shadow_radius = swl_config_get_int(cfg, "scenefx.shadows.blur_sigma", 20);
    if (swl_config_get_color(cfg, "scenefx.shadows.color", r->config.shadow_color) != SWL_OK) {
        r->config.shadow_color[0] = 0.0f;
        r->config.shadow_color[1] = 0.0f;
        r->config.shadow_color[2] = 0.0f;
        r->config.shadow_color[3] = 0.5f;
    }
    r->config.shadow_offset_x = 0;
    r->config.shadow_offset_y = 0;

    // Corner radius
    r->config.corner_radius = swl_config_get_int(cfg, "scenefx.corners.radius", 10);

    // Opacity settings
    r->config.opacity_active = swl_config_get_float(cfg, "scenefx.opacity.active", 1.0f);
    r->config.opacity_inactive = swl_config_get_float(cfg, "scenefx.opacity.inactive", 0.9f);

    // Animation settings
    r->config.animations_enabled = true;
    r->config.animation_duration_ms = 200;

    // Border settings
    r->config.border_width = swl_config_get_int(cfg, "appearance.border_width", 2);

    // Border colors
    if (swl_config_get_color(cfg, "appearance.colors.focus", r->config.border_color_focused) != SWL_OK) {
        r->config.border_color_focused[0] = 0.0f;
        r->config.border_color_focused[1] = 0.33f;
        r->config.border_color_focused[2] = 0.47f;
        r->config.border_color_focused[3] = 1.0f;
    }

    if (swl_config_get_color(cfg, "appearance.colors.border", r->config.border_color_unfocused) != SWL_OK) {
        r->config.border_color_unfocused[0] = 0.27f;
        r->config.border_color_unfocused[1] = 0.27f;
        r->config.border_color_unfocused[2] = 0.27f;
        r->config.border_color_unfocused[3] = 1.0f;
    }

    if (swl_config_get_color(cfg, "appearance.colors.urgent", r->config.border_color_urgent) != SWL_OK) {
        r->config.border_color_urgent[0] = 1.0f;
        r->config.border_color_urgent[1] = 0.0f;
        r->config.border_color_urgent[2] = 0.0f;
        r->config.border_color_urgent[3] = 1.0f;
    }

    return r;
}

void swl_renderer_destroy(SwlRenderer *r)
{
    free(r);
}

SwlError swl_renderer_configure(SwlRenderer *r, const SwlRenderConfig *cfg)
{
    if (!r || !cfg)
        return SWL_ERR_INVALID_ARG;

    r->config = *cfg;
    return SWL_OK;
}

SwlError swl_renderer_reload_config(SwlRenderer *r)
{
    if (!r)
        return SWL_ERR_INVALID_ARG;

    SwlConfig *cfg = swl_compositor_get_config(r->comp);
    if (!cfg)
        return SWL_ERR_INVALID_ARG;

    r->config.blur_radius = swl_config_get_int(cfg, "scenefx.blur.radius", 5);
    r->config.blur_passes = swl_config_get_int(cfg, "scenefx.blur.passes", 3);
    r->config.blur_optimize = swl_config_get_bool(cfg, "scenefx.blur.optimized", true);
    r->config.blur_ignore_transparent = swl_config_get_bool(cfg, "scenefx.blur.ignore_transparent", true);

    r->config.shadow_enabled = swl_config_get_bool(cfg, "scenefx.shadows.enabled", true);
    r->config.shadow_radius = swl_config_get_int(cfg, "scenefx.shadows.blur_sigma", 20);
    if (swl_config_get_color(cfg, "scenefx.shadows.color", r->config.shadow_color) != SWL_OK) {
        r->config.shadow_color[0] = 0.0f;
        r->config.shadow_color[1] = 0.0f;
        r->config.shadow_color[2] = 0.0f;
        r->config.shadow_color[3] = 0.5f;
    }

    r->config.corner_radius = swl_config_get_int(cfg, "scenefx.corners.radius", 10);

    r->config.opacity_active = swl_config_get_float(cfg, "scenefx.opacity.active", 1.0f);
    r->config.opacity_inactive = swl_config_get_float(cfg, "scenefx.opacity.inactive", 0.9f);

    r->config.border_width = swl_config_get_int(cfg, "appearance.border_width", 2);

    if (swl_config_get_color(cfg, "appearance.colors.focus", r->config.border_color_focused) != SWL_OK) {
        r->config.border_color_focused[0] = 0.0f;
        r->config.border_color_focused[1] = 0.33f;
        r->config.border_color_focused[2] = 0.47f;
        r->config.border_color_focused[3] = 1.0f;
    }

    if (swl_config_get_color(cfg, "appearance.colors.border", r->config.border_color_unfocused) != SWL_OK) {
        r->config.border_color_unfocused[0] = 0.27f;
        r->config.border_color_unfocused[1] = 0.27f;
        r->config.border_color_unfocused[2] = 0.27f;
        r->config.border_color_unfocused[3] = 1.0f;
    }

    if (swl_config_get_color(cfg, "appearance.colors.urgent", r->config.border_color_urgent) != SWL_OK) {
        r->config.border_color_urgent[0] = 1.0f;
        r->config.border_color_urgent[1] = 0.0f;
        r->config.border_color_urgent[2] = 0.0f;
        r->config.border_color_urgent[3] = 1.0f;
    }

    return SWL_OK;
}

SwlRenderConfig swl_renderer_get_config(const SwlRenderer *r)
{
    if (!r) {
        SwlRenderConfig empty = {0};
        return empty;
    }
    return r->config;
}

SwlError swl_renderer_set_client_opacity(SwlRenderer *r, SwlClient *c, float opacity)
{
    if (!r || !c)
        return SWL_ERR_INVALID_ARG;

    (void)opacity;
    // TODO: Set opacity via scenefx
    return SWL_OK;
}

SwlError swl_renderer_set_client_blur(SwlRenderer *r, SwlClient *c, bool blur)
{
    if (!r || !c)
        return SWL_ERR_INVALID_ARG;

    (void)blur;
    // TODO: Set blur via scenefx
    return SWL_OK;
}

SwlError swl_renderer_set_client_shadow(SwlRenderer *r, SwlClient *c, bool shadow)
{
    if (!r || !c)
        return SWL_ERR_INVALID_ARG;

    (void)shadow;
    // TODO: Set shadow via scenefx
    return SWL_OK;
}

SwlError swl_renderer_set_client_corner_radius(SwlRenderer *r, SwlClient *c, int radius)
{
    if (!r || !c)
        return SWL_ERR_INVALID_ARG;

    (void)radius;
    // TODO: Set corner radius via scenefx
    return SWL_OK;
}

float swl_renderer_get_client_opacity(const SwlRenderer *r, const SwlClient *c)
{
    if (!r || !c)
        return 1.0f;

    // TODO: Get from client data
    return 1.0f;
}

bool swl_renderer_get_client_blur(const SwlRenderer *r, const SwlClient *c)
{
    if (!r || !c)
        return false;

    // TODO: Get from client data
    return false;
}

bool swl_renderer_get_client_shadow(const SwlRenderer *r, const SwlClient *c)
{
    if (!r || !c)
        return false;

    // TODO: Get from client data
    return false;
}

int swl_renderer_get_client_corner_radius(const SwlRenderer *r, const SwlClient *c)
{
    if (!r || !c)
        return 0;

    // TODO: Get from client data
    return 0;
}

void swl_renderer_damage_whole(SwlRenderer *r)
{
    if (!r)
        return;

    // TODO: Damage all outputs
}
