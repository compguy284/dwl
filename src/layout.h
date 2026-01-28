/*
 * layout.h - Layout, focus, and arrangement functions
 * See LICENSE file for copyright and license details.
 */
#ifndef LAYOUT_H
#define LAYOUT_H

#include "macwc.h"

/* Core layout functions */
void arrange(Monitor *m);
void arrangelayers(Monitor *m);

/* Focus functions */
void focusclient(Client *c, int lift);
Client *focustop(Monitor *m);
void focusstack(const Arg *arg);
void focusdir(const Arg *arg);

/* Layout arrange functions (used as function pointers in layouts[]) */
void monocle(Monitor *m);
void scroller(Monitor *m);
void tile(Monitor *m);

/* Keybinding callbacks */
void consume_or_expel(const Arg *arg);
void defaultgaps(const Arg *arg);
void incgaps(const Arg *arg);
void incigaps(const Arg *arg);
void incihgaps(const Arg *arg);
void incivgaps(const Arg *arg);
void incogaps(const Arg *arg);
void incohgaps(const Arg *arg);
void incovgaps(const Arg *arg);
void incnmaster(const Arg *arg);
void scroller_cycle_proportion(const Arg *arg);
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void togglefloating(const Arg *arg);
void togglefullscreen(const Arg *arg);
void togglegaps(const Arg *arg);
void zoom(const Arg *arg);

#endif /* LAYOUT_H */
