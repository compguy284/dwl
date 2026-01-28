/*
 * client_funcs.h - Client lifecycle and state management
 * See LICENSE file for copyright and license details.
 */
#ifndef CLIENT_FUNCS_H
#define CLIENT_FUNCS_H

#include "macwc.h"

/* Client geometry and bounds */
void applybounds(Client *c, struct wlr_box *bbox);

/* Client rules and configuration */
void applyrules(Client *c);

/* Client lifecycle event handlers */
void commitnotify(struct wl_listener *listener, void *data);
void createnotify(struct wl_listener *listener, void *data);
void destroynotify(struct wl_listener *listener, void *data);
void fullscreennotify(struct wl_listener *listener, void *data);
void mapnotify(struct wl_listener *listener, void *data);
void maximizenotify(struct wl_listener *listener, void *data);
void unmapnotify(struct wl_listener *listener, void *data);
void updatetitle(struct wl_listener *listener, void *data);

/* Client state management */
void resize(Client *c, struct wlr_box geo, int interact);
void setfloating(Client *c, int floating);
void setfullscreen(Client *c, int fullscreen);
void setmon(Client *c, Monitor *m);

/* Update all clients after config reload */
void config_update_all_clients(void);

#endif /* CLIENT_FUNCS_H */
