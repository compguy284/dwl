#ifndef DWL_MONITOR_H
#define DWL_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "error.h"

typedef struct DwlMonitor DwlMonitor;
typedef struct DwlOutputManager DwlOutputManager;
typedef struct DwlCompositor DwlCompositor;
typedef struct DwlLayout DwlLayout;

typedef struct DwlMonitorInfo {
    uint32_t id;
    const char *name;
    const char *description;
    int x, y;
    int width, height;
    int refresh;
    float scale;
    int transform;
    bool enabled;
} DwlMonitorInfo;

typedef struct DwlMonitorConfig {
    int x, y;
    int width, height;
    int refresh;
    float scale;
    int transform;
    bool enabled;
} DwlMonitorConfig;

DwlOutputManager *dwl_output_create(DwlCompositor *comp);
void dwl_output_destroy(DwlOutputManager *mgr);

size_t dwl_monitor_count(DwlOutputManager *mgr);
DwlMonitor *dwl_monitor_get_focused(DwlOutputManager *mgr);
DwlMonitor *dwl_monitor_at(DwlOutputManager *mgr, double x, double y);
DwlMonitor *dwl_monitor_by_name(DwlOutputManager *mgr, const char *name);
DwlMonitor *dwl_monitor_by_index(DwlOutputManager *mgr, size_t index);
DwlMonitor *dwl_monitor_in_direction(DwlOutputManager *mgr, DwlMonitor *from, int dir);

typedef bool (*DwlMonitorIterator)(DwlMonitor *mon, void *data);
void dwl_monitor_foreach(DwlOutputManager *mgr, DwlMonitorIterator iter, void *data);

DwlMonitorInfo dwl_monitor_get_info(const DwlMonitor *mon);
void dwl_monitor_get_usable_area(const DwlMonitor *mon, int *x, int *y, int *w, int *h);

DwlError dwl_monitor_configure(DwlMonitor *mon, const DwlMonitorConfig *cfg);
DwlError dwl_monitor_set_layout(DwlMonitor *mon, const DwlLayout *layout);
DwlError dwl_monitor_set_tags(DwlMonitor *mon, uint32_t tags);
DwlError dwl_monitor_focus(DwlMonitor *mon);

uint32_t dwl_monitor_get_tags(const DwlMonitor *mon);
const DwlLayout *dwl_monitor_get_layout(const DwlMonitor *mon);
struct wlr_output *dwl_monitor_get_wlr_output(const DwlMonitor *mon);

float dwl_monitor_get_mfact(const DwlMonitor *mon);
int dwl_monitor_get_nmaster(const DwlMonitor *mon);
DwlError dwl_monitor_set_mfact(DwlMonitor *mon, float mfact);
DwlError dwl_monitor_set_nmaster(DwlMonitor *mon, int nmaster);
DwlError dwl_monitor_adjust_mfact(DwlMonitor *mon, float delta);
DwlError dwl_monitor_adjust_nmaster(DwlMonitor *mon, int delta);

void dwl_monitor_set_usable_area(DwlMonitor *mon, int x, int y, int w, int h);
void dwl_monitor_arrange(DwlMonitor *mon);
void dwl_monitor_arrange_all(DwlOutputManager *mgr);

#endif /* DWL_MONITOR_H */
