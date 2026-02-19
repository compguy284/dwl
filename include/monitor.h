#ifndef SWL_MONITOR_H
#define SWL_MONITOR_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "error.h"

typedef struct SwlMonitor SwlMonitor;
typedef struct SwlOutputManager SwlOutputManager;
typedef struct SwlCompositor SwlCompositor;
typedef struct SwlLayout SwlLayout;

typedef struct SwlMonitorInfo {
    uint32_t id;
    const char *name;
    const char *description;
    int x, y;
    int width, height;
    int refresh;
    float scale;
    int transform;
    bool enabled;
} SwlMonitorInfo;

typedef struct SwlMonitorConfig {
    int x, y;
    int width, height;
    int refresh;
    float scale;
    int transform;
    bool enabled;
} SwlMonitorConfig;

SwlOutputManager *swl_output_create(SwlCompositor *comp);
void swl_output_destroy(SwlOutputManager *mgr);

size_t swl_monitor_count(SwlOutputManager *mgr);
SwlMonitor *swl_monitor_get_focused(SwlOutputManager *mgr);
SwlMonitor *swl_monitor_at(SwlOutputManager *mgr, double x, double y);
SwlMonitor *swl_monitor_by_name(SwlOutputManager *mgr, const char *name);
SwlMonitor *swl_monitor_by_index(SwlOutputManager *mgr, size_t index);
SwlMonitor *swl_monitor_in_direction(SwlOutputManager *mgr, SwlMonitor *from, int dir);

typedef bool (*SwlMonitorIterator)(SwlMonitor *mon, void *data);
void swl_monitor_foreach(SwlOutputManager *mgr, SwlMonitorIterator iter, void *data);

SwlMonitorInfo swl_monitor_get_info(const SwlMonitor *mon);
void swl_monitor_get_usable_area(const SwlMonitor *mon, int *x, int *y, int *w, int *h);

SwlError swl_monitor_configure(SwlMonitor *mon, const SwlMonitorConfig *cfg);
SwlError swl_monitor_set_layout(SwlMonitor *mon, const SwlLayout *layout);
SwlError swl_monitor_focus(SwlMonitor *mon);

const SwlLayout *swl_monitor_get_layout(const SwlMonitor *mon);
struct wlr_output *swl_monitor_get_wlr_output(const SwlMonitor *mon);

float swl_monitor_get_mfact(const SwlMonitor *mon);
float swl_monitor_get_scroller_ratio(const SwlMonitor *mon);
int swl_monitor_get_nmaster(const SwlMonitor *mon);
SwlError swl_monitor_set_mfact(SwlMonitor *mon, float mfact);
SwlError swl_monitor_set_nmaster(SwlMonitor *mon, int nmaster);
SwlError swl_monitor_adjust_mfact(SwlMonitor *mon, float delta);
SwlError swl_monitor_adjust_nmaster(SwlMonitor *mon, int delta);

void swl_monitor_set_usable_area(SwlMonitor *mon, int x, int y, int w, int h);
void swl_monitor_arrange(SwlMonitor *mon);
void swl_monitor_arrange_all(SwlOutputManager *mgr);
void swl_monitor_reload_config(SwlOutputManager *mgr);

#endif /* SWL_MONITOR_H */
