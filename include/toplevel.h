#ifndef DWL_TOPLEVEL_H
#define DWL_TOPLEVEL_H

#include <stdbool.h>
#include <stdint.h>
#include "error.h"

typedef struct DwlToplevelManager DwlToplevelManager;
typedef struct DwlCompositor DwlCompositor;
typedef struct DwlClient DwlClient;
typedef struct DwlMonitor DwlMonitor;

DwlToplevelManager *dwl_toplevel_manager_create(DwlCompositor *comp);
void dwl_toplevel_manager_destroy(DwlToplevelManager *mgr);

// Called when client state changes
void dwl_toplevel_client_create(DwlToplevelManager *mgr, DwlClient *client);
void dwl_toplevel_client_destroy(DwlToplevelManager *mgr, DwlClient *client);
void dwl_toplevel_client_set_title(DwlToplevelManager *mgr, DwlClient *client, const char *title);
void dwl_toplevel_client_set_app_id(DwlToplevelManager *mgr, DwlClient *client, const char *app_id);
void dwl_toplevel_client_set_output(DwlToplevelManager *mgr, DwlClient *client, DwlMonitor *mon);
void dwl_toplevel_client_set_activated(DwlToplevelManager *mgr, DwlClient *client, bool activated);
void dwl_toplevel_client_set_maximized(DwlToplevelManager *mgr, DwlClient *client, bool maximized);
void dwl_toplevel_client_set_minimized(DwlToplevelManager *mgr, DwlClient *client, bool minimized);
void dwl_toplevel_client_set_fullscreen(DwlToplevelManager *mgr, DwlClient *client, bool fullscreen);

#endif /* DWL_TOPLEVEL_H */
