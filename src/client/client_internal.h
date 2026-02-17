#ifndef DWL_CLIENT_INTERNAL_H
#define DWL_CLIENT_INTERNAL_H

#include "client.h"
#include "rules.h"
#include "scene.h"
#include "render.h"
#include "events.h"
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#ifdef DWL_XWAYLAND
#include <wlr/xwayland.h>
#endif

typedef struct {
    struct wlr_scene_tree *tree;
    struct wlr_scene_tree *surface_tree;
    struct wlr_scene_rect *border[4];
    int border_width;
} ClientSceneData;

#define DWL_CLIENT_MAGIC 0xDEADC0DE

struct DwlClient {
    uint32_t magic;  // Must be DWL_CLIENT_MAGIC for valid clients
    uint32_t id;
    DwlClientManager *mgr;
    DwlMonitor *mon;
    char *output_name;  // Remember which output this client was on (for restore-monitor)

    struct wlr_xdg_toplevel *xdg;
    ClientSceneData *scene_data;

    char *app_id;
    char *title;

    int x, y, width, height;
    int border_width;
    uint32_t tags;

    bool floating;
    bool fullscreen;
    bool urgent;
    bool focused;
    bool mapped;

#ifdef DWL_XWAYLAND
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

struct DwlClientManager {
    DwlCompositor *comp;
    struct wl_list clients;
    struct wl_list focus_stack;
    DwlClient *focused;
    uint32_t next_id;
    DwlSceneManager *scene_mgr;
    DwlRuleEngine *rules;
};

/* Accessors used by client_x11.c */
ClientSceneData *dwl_client_get_scene_data(DwlClient *client);
void dwl_client_set_scene_data(DwlClient *client, ClientSceneData *data);
struct wlr_xdg_toplevel *dwl_client_get_xdg_toplevel(DwlClient *client);

#ifdef DWL_XWAYLAND
/* client_x11.c */
DwlClient *dwl_client_create_x11(DwlClientManager *mgr, struct wlr_xwayland_surface *surface);
bool dwl_client_is_x11(const DwlClient *client);
bool dwl_client_is_x11_unmanaged(const DwlClient *client);
const char *dwl_client_get_x11_class(const DwlClient *client);
const char *dwl_client_get_x11_instance(const DwlClient *client);
int dwl_client_get_x11_pid(const DwlClient *client);
struct wlr_xwayland_surface *dwl_client_get_xwayland_surface(DwlClient *client);
#endif

#endif /* DWL_CLIENT_INTERNAL_H */
