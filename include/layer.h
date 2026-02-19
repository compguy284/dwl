#ifndef SWL_LAYER_H
#define SWL_LAYER_H

#include <stdbool.h>
#include <stdint.h>
#include "error.h"

typedef struct SwlLayerManager SwlLayerManager;
typedef struct SwlLayerSurface SwlLayerSurface;
typedef struct SwlCompositor SwlCompositor;
typedef struct SwlMonitor SwlMonitor;

typedef enum {
    SWL_LAYER_SHELL_BACKGROUND,
    SWL_LAYER_SHELL_BOTTOM,
    SWL_LAYER_SHELL_TOP,
    SWL_LAYER_SHELL_OVERLAY,
} SwlLayerShellLayer;

typedef struct SwlLayerSurfaceInfo {
    const char *namespace;
    int x, y, width, height;
    SwlLayerShellLayer layer;
    bool mapped;
    bool keyboard_interactive;
    uint32_t anchor;
    int32_t exclusive_zone;
} SwlLayerSurfaceInfo;

SwlLayerManager *swl_layer_manager_create(SwlCompositor *comp);
void swl_layer_manager_destroy(SwlLayerManager *mgr);

void swl_layer_arrange(SwlLayerManager *mgr, SwlMonitor *mon);
void swl_layer_get_exclusive_zone(SwlLayerManager *mgr, SwlMonitor *mon,
                                   int *top, int *bottom, int *left, int *right);

typedef bool (*SwlLayerSurfaceIterator)(SwlLayerSurface *surface, void *data);
void swl_layer_foreach(SwlLayerManager *mgr, SwlLayerSurfaceIterator iter, void *data);
void swl_layer_foreach_on_monitor(SwlLayerManager *mgr, SwlMonitor *mon,
                                   SwlLayerSurfaceIterator iter, void *data);

void swl_layer_cleanup_monitor(SwlLayerManager *mgr, SwlMonitor *mon);

SwlLayerSurfaceInfo swl_layer_surface_get_info(const SwlLayerSurface *surface);
struct wlr_surface *swl_layer_surface_get_wlr_surface(const SwlLayerSurface *surface);
SwlMonitor *swl_layer_surface_get_monitor(const SwlLayerSurface *surface);

#endif /* SWL_LAYER_H */
