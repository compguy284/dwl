#ifndef DWL_CLIENT_H
#define DWL_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "error.h"

typedef struct DwlClient DwlClient;
typedef struct DwlClientManager DwlClientManager;
typedef struct DwlCompositor DwlCompositor;
typedef struct DwlMonitor DwlMonitor;
struct wlr_surface;

typedef struct DwlClientInfo {
    uint32_t id;
    const char *app_id;
    const char *title;
    struct {
        int x, y, width, height;
    } geometry;
    uint32_t tags;
    bool floating;
    bool fullscreen;
    bool urgent;
    bool focused;
    bool x11;
} DwlClientInfo;

DwlClientManager *dwl_client_manager_create(DwlCompositor *comp);
void dwl_client_manager_destroy(DwlClientManager *mgr);

typedef bool (*DwlClientIterator)(DwlClient *client, void *data);
void dwl_client_foreach(DwlClientManager *mgr, DwlClientIterator iter, void *data);
void dwl_client_foreach_visible(DwlClientManager *mgr, DwlMonitor *mon,
                                 DwlClientIterator iter, void *data);
void dwl_client_foreach_on_tag(DwlClientManager *mgr, uint32_t tags,
                                DwlClientIterator iter, void *data);

DwlClient *dwl_client_at(DwlClientManager *mgr, double x, double y);
DwlClient *dwl_client_focused(DwlClientManager *mgr);
DwlClient *dwl_client_focus_top_on_monitor(DwlClientManager *mgr, DwlMonitor *mon);
DwlClient *dwl_client_by_id(DwlClientManager *mgr, uint32_t id);
bool dwl_client_is_valid(DwlClient *client);
DwlClient *dwl_client_by_surface(DwlClientManager *mgr, struct wlr_surface *surface);
size_t dwl_client_count(DwlClientManager *mgr);
size_t dwl_client_count_visible(DwlClientManager *mgr, DwlMonitor *mon);

DwlClientInfo dwl_client_get_info(const DwlClient *client);
DwlMonitor *dwl_client_get_monitor(const DwlClient *client);
struct wlr_surface *dwl_client_get_surface(const DwlClient *client);
const char *dwl_client_get_output_name(const DwlClient *client);
void dwl_client_set_monitor_internal(DwlClient *client, DwlMonitor *mon);

DwlError dwl_client_close(DwlClient *client);
DwlError dwl_client_focus(DwlClient *client);
DwlError dwl_client_set_tags(DwlClient *client, uint32_t tags);
DwlError dwl_client_toggle_tag(DwlClient *client, uint32_t tag);
DwlError dwl_client_set_floating(DwlClient *client, bool floating);
DwlError dwl_client_toggle_floating(DwlClient *client);
DwlError dwl_client_set_fullscreen(DwlClient *client, bool fullscreen);
DwlError dwl_client_toggle_fullscreen(DwlClient *client);
DwlError dwl_client_move_to_monitor(DwlClient *client, DwlMonitor *mon);
DwlError dwl_client_resize(DwlClient *client, int x, int y, int w, int h);
DwlError dwl_client_set_border_color(DwlClient *client, const float color[4]);
DwlError dwl_client_set_border_width(DwlClient *client, int width);
DwlError dwl_client_set_urgent(DwlClient *client, bool urgent);
DwlError dwl_client_zoom(DwlClientManager *mgr);

// Direction: 0=up, 1=down, 2=left, 3=right
DwlClient *dwl_client_in_direction(DwlClientManager *mgr, DwlClient *from, int direction);

struct DwlSceneManager *dwl_client_manager_get_scene(DwlClientManager *mgr);
struct DwlRuleEngine *dwl_client_manager_get_rules(DwlClientManager *mgr);
DwlError dwl_client_manager_load_rules(DwlClientManager *mgr);

#endif /* DWL_CLIENT_H */
