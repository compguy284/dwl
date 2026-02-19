#ifndef SWL_RENDER_H
#define SWL_RENDER_H

#include <stdbool.h>
#include "error.h"

typedef struct SwlRenderer SwlRenderer;
typedef struct SwlCompositor SwlCompositor;
typedef struct SwlClient SwlClient;

typedef struct SwlRenderConfig {
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
} SwlRenderConfig;

SwlRenderer *swl_renderer_create(SwlCompositor *comp);
void swl_renderer_destroy(SwlRenderer *r);

SwlError swl_renderer_configure(SwlRenderer *r, const SwlRenderConfig *cfg);
SwlError swl_renderer_reload_config(SwlRenderer *r);
SwlRenderConfig swl_renderer_get_config(const SwlRenderer *r);

SwlError swl_renderer_set_client_opacity(SwlRenderer *r, SwlClient *c, float opacity);
SwlError swl_renderer_set_client_blur(SwlRenderer *r, SwlClient *c, bool blur);
SwlError swl_renderer_set_client_shadow(SwlRenderer *r, SwlClient *c, bool shadow);
SwlError swl_renderer_set_client_corner_radius(SwlRenderer *r, SwlClient *c, int radius);

float swl_renderer_get_client_opacity(const SwlRenderer *r, const SwlClient *c);
bool swl_renderer_get_client_blur(const SwlRenderer *r, const SwlClient *c);
bool swl_renderer_get_client_shadow(const SwlRenderer *r, const SwlClient *c);
int swl_renderer_get_client_corner_radius(const SwlRenderer *r, const SwlClient *c);

void swl_renderer_damage_whole(SwlRenderer *r);

#endif /* SWL_RENDER_H */
