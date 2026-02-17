#ifndef DWL_LAYER_H
#define DWL_LAYER_H

#include <stdbool.h>
#include <stdint.h>
#include "error.h"

typedef struct DwlLayerManager DwlLayerManager;
typedef struct DwlLayerSurface DwlLayerSurface;
typedef struct DwlCompositor DwlCompositor;
typedef struct DwlMonitor DwlMonitor;

typedef enum {
    DWL_LAYER_SHELL_BACKGROUND,
    DWL_LAYER_SHELL_BOTTOM,
    DWL_LAYER_SHELL_TOP,
    DWL_LAYER_SHELL_OVERLAY,
} DwlLayerShellLayer;

typedef struct DwlLayerSurfaceInfo {
    const char *namespace;
    int x, y, width, height;
    DwlLayerShellLayer layer;
    bool mapped;
    bool keyboard_interactive;
    uint32_t anchor;
    int32_t exclusive_zone;
} DwlLayerSurfaceInfo;

DwlLayerManager *dwl_layer_manager_create(DwlCompositor *comp);
void dwl_layer_manager_destroy(DwlLayerManager *mgr);

void dwl_layer_arrange(DwlLayerManager *mgr, DwlMonitor *mon);
void dwl_layer_get_exclusive_zone(DwlLayerManager *mgr, DwlMonitor *mon,
                                   int *top, int *bottom, int *left, int *right);

typedef bool (*DwlLayerSurfaceIterator)(DwlLayerSurface *surface, void *data);
void dwl_layer_foreach(DwlLayerManager *mgr, DwlLayerSurfaceIterator iter, void *data);
void dwl_layer_foreach_on_monitor(DwlLayerManager *mgr, DwlMonitor *mon,
                                   DwlLayerSurfaceIterator iter, void *data);

DwlLayerSurfaceInfo dwl_layer_surface_get_info(const DwlLayerSurface *surface);
struct wlr_surface *dwl_layer_surface_get_wlr_surface(const DwlLayerSurface *surface);
DwlMonitor *dwl_layer_surface_get_monitor(const DwlLayerSurface *surface);

#endif /* DWL_LAYER_H */
