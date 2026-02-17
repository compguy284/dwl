#include "render.h"
#include "compositor.h"
#include "config.h"
#include "client.h"
#include <stdlib.h>
#include <string.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/wlr_scene.h>
#include <scenefx/types/fx/blur_data.h>

struct DwlRenderer {
    DwlCompositor *comp;
    struct fx_renderer *fx;
    DwlRenderConfig config;
};

DwlRenderer *dwl_renderer_create(DwlCompositor *comp)
{
    DwlRenderer *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;

    r->comp = comp;

    DwlConfig *cfg = dwl_compositor_get_config(comp);

    // Blur settings
    r->config.blur_radius = dwl_config_get_int(cfg, "scenefx.blur.radius", 5);
    r->config.blur_passes = dwl_config_get_int(cfg, "scenefx.blur.passes", 3);
    r->config.blur_optimize = dwl_config_get_bool(cfg, "scenefx.blur.optimized", true);
    r->config.blur_ignore_transparent = dwl_config_get_bool(cfg, "scenefx.blur.ignore_transparent", true);

    // Shadow settings
    r->config.shadow_enabled = dwl_config_get_bool(cfg, "scenefx.shadows.enabled", true);
    r->config.shadow_radius = dwl_config_get_int(cfg, "scenefx.shadows.blur_sigma", 20);
    if (dwl_config_get_color(cfg, "scenefx.shadows.color", r->config.shadow_color) != DWL_OK) {
        r->config.shadow_color[0] = 0.0f;
        r->config.shadow_color[1] = 0.0f;
        r->config.shadow_color[2] = 0.0f;
        r->config.shadow_color[3] = 0.5f;
    }
    r->config.shadow_offset_x = 0;
    r->config.shadow_offset_y = 0;

    // Corner radius
    r->config.corner_radius = dwl_config_get_int(cfg, "scenefx.corners.radius", 10);

    // Opacity settings
    r->config.opacity_active = dwl_config_get_float(cfg, "scenefx.opacity.active", 1.0f);
    r->config.opacity_inactive = dwl_config_get_float(cfg, "scenefx.opacity.inactive", 0.9f);

    // Animation settings
    r->config.animations_enabled = true;
    r->config.animation_duration_ms = 200;

    // Border settings
    r->config.border_width = dwl_config_get_int(cfg, "appearance.border_width", 2);

    // Border colors
    if (dwl_config_get_color(cfg, "appearance.colors.focus", r->config.border_color_focused) != DWL_OK) {
        r->config.border_color_focused[0] = 0.0f;
        r->config.border_color_focused[1] = 0.33f;
        r->config.border_color_focused[2] = 0.47f;
        r->config.border_color_focused[3] = 1.0f;
    }

    if (dwl_config_get_color(cfg, "appearance.colors.border", r->config.border_color_unfocused) != DWL_OK) {
        r->config.border_color_unfocused[0] = 0.27f;
        r->config.border_color_unfocused[1] = 0.27f;
        r->config.border_color_unfocused[2] = 0.27f;
        r->config.border_color_unfocused[3] = 1.0f;
    }

    if (dwl_config_get_color(cfg, "appearance.colors.urgent", r->config.border_color_urgent) != DWL_OK) {
        r->config.border_color_urgent[0] = 1.0f;
        r->config.border_color_urgent[1] = 0.0f;
        r->config.border_color_urgent[2] = 0.0f;
        r->config.border_color_urgent[3] = 1.0f;
    }

    return r;
}

void dwl_renderer_destroy(DwlRenderer *r)
{
    free(r);
}

DwlError dwl_renderer_configure(DwlRenderer *r, const DwlRenderConfig *cfg)
{
    if (!r || !cfg)
        return DWL_ERR_INVALID_ARG;

    r->config = *cfg;
    return DWL_OK;
}

DwlRenderConfig dwl_renderer_get_config(const DwlRenderer *r)
{
    if (!r) {
        DwlRenderConfig empty = {0};
        return empty;
    }
    return r->config;
}

DwlError dwl_renderer_set_client_opacity(DwlRenderer *r, DwlClient *c, float opacity)
{
    if (!r || !c)
        return DWL_ERR_INVALID_ARG;

    (void)opacity;
    // TODO: Set opacity via scenefx
    return DWL_OK;
}

DwlError dwl_renderer_set_client_blur(DwlRenderer *r, DwlClient *c, bool blur)
{
    if (!r || !c)
        return DWL_ERR_INVALID_ARG;

    (void)blur;
    // TODO: Set blur via scenefx
    return DWL_OK;
}

DwlError dwl_renderer_set_client_shadow(DwlRenderer *r, DwlClient *c, bool shadow)
{
    if (!r || !c)
        return DWL_ERR_INVALID_ARG;

    (void)shadow;
    // TODO: Set shadow via scenefx
    return DWL_OK;
}

DwlError dwl_renderer_set_client_corner_radius(DwlRenderer *r, DwlClient *c, int radius)
{
    if (!r || !c)
        return DWL_ERR_INVALID_ARG;

    (void)radius;
    // TODO: Set corner radius via scenefx
    return DWL_OK;
}

float dwl_renderer_get_client_opacity(const DwlRenderer *r, const DwlClient *c)
{
    if (!r || !c)
        return 1.0f;

    // TODO: Get from client data
    return 1.0f;
}

bool dwl_renderer_get_client_blur(const DwlRenderer *r, const DwlClient *c)
{
    if (!r || !c)
        return false;

    // TODO: Get from client data
    return false;
}

bool dwl_renderer_get_client_shadow(const DwlRenderer *r, const DwlClient *c)
{
    if (!r || !c)
        return false;

    // TODO: Get from client data
    return false;
}

int dwl_renderer_get_client_corner_radius(const DwlRenderer *r, const DwlClient *c)
{
    if (!r || !c)
        return 0;

    // TODO: Get from client data
    return 0;
}

void dwl_renderer_damage_whole(DwlRenderer *r)
{
    if (!r)
        return;

    // TODO: Damage all outputs
}
