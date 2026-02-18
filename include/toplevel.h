#ifndef SWL_TOPLEVEL_H
#define SWL_TOPLEVEL_H

#include <stdbool.h>
#include <stdint.h>
#include "error.h"

typedef struct SwlToplevelManager SwlToplevelManager;
typedef struct SwlCompositor SwlCompositor;
typedef struct SwlClient SwlClient;
typedef struct SwlMonitor SwlMonitor;

SwlToplevelManager *swl_toplevel_manager_create(SwlCompositor *comp);
void swl_toplevel_manager_destroy(SwlToplevelManager *mgr);

// Called when client state changes
void swl_toplevel_client_create(SwlToplevelManager *mgr, SwlClient *client);
void swl_toplevel_client_destroy(SwlToplevelManager *mgr, SwlClient *client);
void swl_toplevel_client_set_title(SwlToplevelManager *mgr, SwlClient *client, const char *title);
void swl_toplevel_client_set_app_id(SwlToplevelManager *mgr, SwlClient *client, const char *app_id);
void swl_toplevel_client_set_output(SwlToplevelManager *mgr, SwlClient *client, SwlMonitor *mon);
void swl_toplevel_client_set_activated(SwlToplevelManager *mgr, SwlClient *client, bool activated);
void swl_toplevel_client_set_maximized(SwlToplevelManager *mgr, SwlClient *client, bool maximized);
void swl_toplevel_client_set_minimized(SwlToplevelManager *mgr, SwlClient *client, bool minimized);
void swl_toplevel_client_set_fullscreen(SwlToplevelManager *mgr, SwlClient *client, bool fullscreen);

#endif /* SWL_TOPLEVEL_H */
