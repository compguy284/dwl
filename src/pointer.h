/*
 * pointer.h - Pointer/cursor handling
 * See LICENSE file for copyright and license details.
 */
#ifndef POINTER_H
#define POINTER_H

#include "dwl.h"

/* Pointer listener functions */
void axisnotify(struct wl_listener *listener, void *data);
void createpointerconstraint(struct wl_listener *listener, void *data);
void cursorframe(struct wl_listener *listener, void *data);
void destroypointerconstraint(struct wl_listener *listener, void *data);
void destroydragicon(struct wl_listener *listener, void *data);
void motionabsolute(struct wl_listener *listener, void *data);
void motionrelative(struct wl_listener *listener, void *data);
void requeststartdrag(struct wl_listener *listener, void *data);
void setcursor(struct wl_listener *listener, void *data);
void setcursorshape(struct wl_listener *listener, void *data);
void startdrag(struct wl_listener *listener, void *data);
void virtualpointer(struct wl_listener *listener, void *data);

/* Pointer utility functions */
void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
void cursorwarptohint(void);
void pointerfocus(Client *c, struct wlr_surface *surface, double sx, double sy,
		uint32_t time);
void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);

#endif /* POINTER_H */
