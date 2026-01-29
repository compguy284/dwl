/*
 * monitor.h - Monitor lifecycle, rendering, and output management
 * See LICENSE file for copyright and license details.
 */
#ifndef MONITOR_H
#define MONITOR_H

#include "dwl.h"

/* Monitor lifecycle - used as listener callbacks */
void createmon(struct wl_listener *listener, void *data);
void updatemons(struct wl_listener *listener, void *data);

/* Output management - used as listener callbacks */
void outputmgrapply(struct wl_listener *listener, void *data);
void outputmgrtest(struct wl_listener *listener, void *data);
void powermgrsetmode(struct wl_listener *listener, void *data);

/* Keybinding callbacks */
void focusmon(const Arg *arg);
void movetomon(const Arg *arg);

#endif /* MONITOR_H */
