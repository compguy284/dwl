#ifndef SWL_SCENE_H
#define SWL_SCENE_H

#include <stdbool.h>
#include "error.h"

typedef struct SwlSceneManager SwlSceneManager;
typedef struct SwlCompositor SwlCompositor;
typedef struct SwlClient SwlClient;
typedef struct SwlMonitor SwlMonitor;

typedef enum {
    SWL_LAYER_BACKGROUND,
    SWL_LAYER_BOTTOM,
    SWL_LAYER_TILES,
    SWL_LAYER_FLOAT,
    SWL_LAYER_TOP,
    SWL_LAYER_FULLSCREEN,
    SWL_LAYER_OVERLAY,
    SWL_LAYER_BLOCK,
    SWL_LAYER_COUNT,
} SwlSceneLayer;

SwlSceneManager *swl_scene_manager_create(SwlCompositor *comp);
void swl_scene_manager_destroy(SwlSceneManager *mgr);

struct wlr_scene_tree *swl_scene_get_layer(SwlSceneManager *mgr, SwlSceneLayer layer);

SwlError swl_scene_client_create(SwlSceneManager *mgr, SwlClient *client);
void swl_scene_client_destroy(SwlSceneManager *mgr, SwlClient *client);
void swl_scene_client_set_position(SwlClient *client, int x, int y);
void swl_scene_client_set_size(SwlClient *client, int width, int height);
void swl_scene_update_client_size(SwlClient *client, int width, int height);
void swl_scene_client_apply_geometry(SwlClient *client);
void swl_scene_client_set_visible(SwlClient *client, bool visible);
void swl_scene_client_set_layer(SwlSceneManager *mgr, SwlClient *client, SwlSceneLayer layer);
void swl_scene_client_raise(SwlClient *client);
void swl_scene_client_set_activated(SwlClient *client, bool activated);

void swl_scene_update_borders(SwlClient *client, int width, const float color[4]);

// Clip client surface to monitor boundaries (NULL box to clear clip)
void swl_scene_client_set_clip(SwlClient *client, int clip_x, int clip_y, int clip_w, int clip_h);
void swl_scene_client_clear_clip(SwlClient *client);

// Scenefx effects
void swl_scene_client_set_shadow(SwlClient *client, bool enabled, int blur_sigma, const float color[4]);
void swl_scene_client_set_corner_radius(SwlClient *client, int radius);
void swl_scene_client_set_opacity(SwlClient *client, float opacity);

#endif /* SWL_SCENE_H */
