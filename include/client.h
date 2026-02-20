#ifndef SWL_CLIENT_H
#define SWL_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "error.h"

typedef struct SwlClient SwlClient;
typedef struct SwlClientManager SwlClientManager;
typedef struct SwlCompositor SwlCompositor;
typedef struct SwlMonitor SwlMonitor;
struct wlr_surface;

typedef struct SwlClientInfo {
    uint32_t id;
    const char *app_id;
    const char *title;
    struct {
        int x, y, width, height;
    } geometry;
    int border_width;
    bool floating;
    bool fullscreen;
    bool urgent;
    bool focused;
    bool x11;
} SwlClientInfo;

SwlClientManager *swl_client_manager_create(SwlCompositor *comp);
void swl_client_manager_destroy(SwlClientManager *mgr);

typedef bool (*SwlClientIterator)(SwlClient *client, void *data);
void swl_client_foreach(SwlClientManager *mgr, SwlClientIterator iter, void *data);
void swl_client_foreach_visible(SwlClientManager *mgr, SwlMonitor *mon,
                                 SwlClientIterator iter, void *data);

SwlClient *swl_client_at(SwlClientManager *mgr, double x, double y);
SwlClient *swl_client_focused(SwlClientManager *mgr);
SwlClient *swl_client_focus_top_on_monitor(SwlClientManager *mgr, SwlMonitor *mon);
SwlClient *swl_client_by_id(SwlClientManager *mgr, uint32_t id);
bool swl_client_is_valid(SwlClient *client);
SwlClient *swl_client_by_surface(SwlClientManager *mgr, struct wlr_surface *surface);
size_t swl_client_count(SwlClientManager *mgr);
size_t swl_client_count_visible(SwlClientManager *mgr, SwlMonitor *mon);

SwlClientInfo swl_client_get_info(const SwlClient *client);
SwlMonitor *swl_client_get_monitor(const SwlClient *client);
struct wlr_surface *swl_client_get_surface(const SwlClient *client);
const char *swl_client_get_output_name(const SwlClient *client);
void swl_client_set_monitor_internal(SwlClient *client, SwlMonitor *mon);

SwlError swl_client_close(SwlClient *client);
SwlError swl_client_focus(SwlClient *client);
SwlError swl_client_set_floating(SwlClient *client, bool floating);
SwlError swl_client_toggle_floating(SwlClient *client);
SwlError swl_client_set_fullscreen(SwlClient *client, bool fullscreen);
SwlError swl_client_toggle_fullscreen(SwlClient *client);
SwlError swl_client_move_to_monitor(SwlClient *client, SwlMonitor *mon);
SwlError swl_client_resize(SwlClient *client, int x, int y, int w, int h);
SwlError swl_client_set_border_color(SwlClient *client, const float color[4]);
SwlError swl_client_set_border_width(SwlClient *client, int width);
SwlError swl_client_set_urgent(SwlClient *client, bool urgent);
SwlError swl_client_zoom(SwlClientManager *mgr);

float swl_client_get_scroller_ratio(const SwlClient *client);
SwlError swl_client_set_scroller_ratio(SwlClient *client, float ratio);

bool swl_client_is_column_head(const SwlClient *client);
void swl_client_unlink_column(SwlClient *client);
SwlClient *swl_client_column_next(const SwlClient *client);
SwlError swl_client_consume_or_expel(SwlClientManager *mgr, SwlClient *focused, int dir);

// Direction: 0=up, 1=down, 2=left, 3=right
SwlClient *swl_client_in_direction(SwlClientManager *mgr, SwlClient *from, int direction);

struct SwlSceneManager *swl_client_manager_get_scene(SwlClientManager *mgr);
struct SwlRuleEngine *swl_client_manager_get_rules(SwlClientManager *mgr);
SwlError swl_client_manager_load_rules(SwlClientManager *mgr);

#endif /* SWL_CLIENT_H */
