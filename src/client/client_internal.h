#ifndef SWL_CLIENT_INTERNAL_H
#define SWL_CLIENT_INTERNAL_H

#include "client.h"
#include "rules.h"
#include "scene.h"
#include "render.h"
#include "events.h"
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#ifdef SWL_XWAYLAND
#include <wlr/xwayland.h>
#endif

typedef struct {
    struct wlr_scene_tree *tree;
    struct wlr_scene_tree *surface_tree;
    struct wlr_scene_rect *border[4];
    int border_width;
} ClientSceneData;

#define SWL_CLIENT_MAGIC 0xDEADC0DE

struct SwlClient {
    uint32_t magic;  // Must be SWL_CLIENT_MAGIC for valid clients
    uint32_t id;
    SwlClientManager *mgr;
    SwlMonitor *mon;
    char *output_name;  // Remember which output this client was on (for restore-monitor)

    struct wlr_xdg_toplevel *xdg;
    ClientSceneData *scene_data;

    char *app_id;
    char *title;

    int x, y, width, height;
    int border_width;
    bool floating;
    bool fullscreen;
    bool urgent;
    bool focused;
    bool mapped;

    float scroller_ratio;  // Per-client scroller column ratio (0.0 = use default)

#ifdef SWL_XWAYLAND
    struct wlr_xwayland_surface *xwayland;
    bool is_x11;
    struct wl_listener associate;
    struct wl_listener dissociate;
#endif

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    struct wl_listener request_fullscreen;
    struct wl_listener set_title;
    struct wl_listener set_app_id;

    struct wl_list link;
    struct wl_list flink;
};

struct SwlClientManager {
    SwlCompositor *comp;
    struct wl_list clients;
    struct wl_list focus_stack;
    SwlClient *focused;
    uint32_t next_id;
    SwlSceneManager *scene_mgr;
    SwlRuleEngine *rules;
};

/* Accessors used by client_x11.c */
ClientSceneData *swl_client_get_scene_data(SwlClient *client);
void swl_client_set_scene_data(SwlClient *client, ClientSceneData *data);
struct wlr_xdg_toplevel *swl_client_get_xdg_toplevel(SwlClient *client);

#ifdef SWL_XWAYLAND
/* client_x11.c */
SwlClient *swl_client_create_x11(SwlClientManager *mgr, struct wlr_xwayland_surface *surface);
bool swl_client_is_x11(const SwlClient *client);
bool swl_client_is_x11_unmanaged(const SwlClient *client);
const char *swl_client_get_x11_class(const SwlClient *client);
const char *swl_client_get_x11_instance(const SwlClient *client);
int swl_client_get_x11_pid(const SwlClient *client);
struct wlr_xwayland_surface *swl_client_get_xwayland_surface(SwlClient *client);
#endif

#endif /* SWL_CLIENT_INTERNAL_H */
