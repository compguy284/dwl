#ifndef DWL_RENDER_H
#define DWL_RENDER_H

#include <stdbool.h>
#include "error.h"

typedef struct DwlRenderer DwlRenderer;
typedef struct DwlCompositor DwlCompositor;
typedef struct DwlClient DwlClient;

typedef struct DwlRenderConfig {
    int blur_radius;
    int blur_passes;
    bool blur_optimize;
    bool blur_ignore_transparent;

    bool shadow_enabled;
    int shadow_radius;
    float shadow_color[4];
    int shadow_offset_x;
    int shadow_offset_y;

    int corner_radius;

    float opacity_active;
    float opacity_inactive;

    bool animations_enabled;
    int animation_duration_ms;

    int border_width;
    float border_color_focused[4];
    float border_color_unfocused[4];
    float border_color_urgent[4];
} DwlRenderConfig;

DwlRenderer *dwl_renderer_create(DwlCompositor *comp);
void dwl_renderer_destroy(DwlRenderer *r);

DwlError dwl_renderer_configure(DwlRenderer *r, const DwlRenderConfig *cfg);
DwlRenderConfig dwl_renderer_get_config(const DwlRenderer *r);

DwlError dwl_renderer_set_client_opacity(DwlRenderer *r, DwlClient *c, float opacity);
DwlError dwl_renderer_set_client_blur(DwlRenderer *r, DwlClient *c, bool blur);
DwlError dwl_renderer_set_client_shadow(DwlRenderer *r, DwlClient *c, bool shadow);
DwlError dwl_renderer_set_client_corner_radius(DwlRenderer *r, DwlClient *c, int radius);

float dwl_renderer_get_client_opacity(const DwlRenderer *r, const DwlClient *c);
bool dwl_renderer_get_client_blur(const DwlRenderer *r, const DwlClient *c);
bool dwl_renderer_get_client_shadow(const DwlRenderer *r, const DwlClient *c);
int dwl_renderer_get_client_corner_radius(const DwlRenderer *r, const DwlClient *c);

void dwl_renderer_damage_whole(DwlRenderer *r);

#endif /* DWL_RENDER_H */
