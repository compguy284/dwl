#ifndef DWL_SCENE_H
#define DWL_SCENE_H

#include <stdbool.h>
#include "error.h"

typedef struct DwlSceneManager DwlSceneManager;
typedef struct DwlCompositor DwlCompositor;
typedef struct DwlClient DwlClient;
typedef struct DwlMonitor DwlMonitor;

typedef enum {
    DWL_LAYER_BACKGROUND,
    DWL_LAYER_BOTTOM,
    DWL_LAYER_TILES,
    DWL_LAYER_FLOAT,
    DWL_LAYER_TOP,
    DWL_LAYER_FULLSCREEN,
    DWL_LAYER_OVERLAY,
    DWL_LAYER_BLOCK,
    DWL_LAYER_COUNT,
} DwlSceneLayer;

DwlSceneManager *dwl_scene_manager_create(DwlCompositor *comp);
void dwl_scene_manager_destroy(DwlSceneManager *mgr);

struct wlr_scene_tree *dwl_scene_get_layer(DwlSceneManager *mgr, DwlSceneLayer layer);

DwlError dwl_scene_client_create(DwlSceneManager *mgr, DwlClient *client);
void dwl_scene_client_destroy(DwlSceneManager *mgr, DwlClient *client);
void dwl_scene_client_set_position(DwlClient *client, int x, int y);
void dwl_scene_client_set_size(DwlClient *client, int width, int height);
void dwl_scene_update_client_size(DwlClient *client, int width, int height);
void dwl_scene_client_apply_geometry(DwlClient *client);
void dwl_scene_client_set_visible(DwlClient *client, bool visible);
void dwl_scene_client_set_layer(DwlSceneManager *mgr, DwlClient *client, DwlSceneLayer layer);
void dwl_scene_client_raise(DwlClient *client);
void dwl_scene_client_set_activated(DwlClient *client, bool activated);

void dwl_scene_update_borders(DwlClient *client, int width, const float color[4]);

// Clip client surface to monitor boundaries (NULL box to clear clip)
void dwl_scene_client_set_clip(DwlClient *client, int clip_x, int clip_y, int clip_w, int clip_h);
void dwl_scene_client_clear_clip(DwlClient *client);

// Scenefx effects
void dwl_scene_client_set_shadow(DwlClient *client, bool enabled, int blur_sigma, const float color[4]);
void dwl_scene_client_set_corner_radius(DwlClient *client, int radius);
void dwl_scene_client_set_opacity(DwlClient *client, float opacity);

#endif /* DWL_SCENE_H */
