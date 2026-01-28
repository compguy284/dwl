/*
 * session.h - Session lock and layer shell handling
 * See LICENSE file for copyright and license details.
 */
#ifndef SESSION_H
#define SESSION_H

#include "macwc.h"

/* Layer surface functions */
void commitlayersurfacenotify(struct wl_listener *listener, void *data);
void createlayersurface(struct wl_listener *listener, void *data);
void destroylayersurfacenotify(struct wl_listener *listener, void *data);
void unmaplayersurfacenotify(struct wl_listener *listener, void *data);

/* Session lock functions */
void createlocksurface(struct wl_listener *listener, void *data);
void destroylock(SessionLock *lock, int unlock);
void destroylocksurface(struct wl_listener *listener, void *data);
void destroysessionlock(struct wl_listener *listener, void *data);
void locksession(struct wl_listener *listener, void *data);
void unlocksession(struct wl_listener *listener, void *data);

#endif /* SESSION_H */
