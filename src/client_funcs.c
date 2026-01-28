/*
 * client_funcs.c - Client lifecycle and state management
 * See LICENSE file for copyright and license details.
 */
#include "client_funcs.h"
#include "client.h"
#include "layout.h"
#include "visual.h"

void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum possible */
	c->geom.width = MAX(1 + 2 * (int)c->bw, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw, c->geom.height);

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	/* rule matching */
	const char *appid, *title;
	size_t i;
	Monitor *mon = selmon, *m;

	appid = client_get_appid(c);
	title = client_get_title(c);

	for (i = 0; i < cfg.rules_count; i++) {
		const CfgRule *r = &cfg.rules[i];
		int mi;
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			mi = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == mi++)
					mon = m;
			}
		}
	}

	c->isfloating |= client_is_float_type(c);
	setmon(c, mon);
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		/*
		 * Get the monitor this client will be rendered on
		 * Note that if the user set a rule in which the client is placed on
		 * a different monitor based on its title, this will likely select
		 * a wrong monitor.
		 */
		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setmon(c, NULL); /* Make sure to reapply rules in mapnotify() */

		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		return;
	}

	/* For tiled windows, call arrange() to let the layout (especially scroller)
	 * handle positioning and clipping. For floating windows, resize directly. */
	if (c->isfloating || c->isfullscreen) {
		resize(c, c->geom, !c->isfullscreen);
	} else if (c->mon) {
		arrange(c->mon);
	}

	/* mark a pending resize as completed */
	if (c->resize && c->resize <= c->surface.xdg->current.configure_serial)
		c->resize = 0;
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client creates a new toplevel (application window). */
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;

	/* Allocate a Client for this surface */
	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;
	c->bw = cfg.borderpx;

	c->opacity = cfg.opacity_active;
	c->corner_radius = cfg.corner_radius;
	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the xdg_toplevel is destroyed. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
#ifdef XWAYLAND
	if (c->type != XDGShell) {
		wl_list_remove(&c->activate.link);
		wl_list_remove(&c->associate.link);
		wl_list_remove(&c->configure.link);
		wl_list_remove(&c->dissociate.link);
		wl_list_remove(&c->set_hints.link);
	} else
#endif
	{
		wl_list_remove(&c->commit.link);
		wl_list_remove(&c->map.link);
		wl_list_remove(&c->unmap.link);
		wl_list_remove(&c->maximize.link);
	}
	free(c);
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;
	int i;

	/* Create scene tree for this client and its border */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	/* Enabled later by a call to arrange() */
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;

	client_get_geometry(c, &c->geom);

	/* Handle unmanaged clients first so we can return prior create borders */
	if (client_is_unmanaged(c)) {
		/* Unmanaged clients always are floating */
		wlr_scene_node_reparent(&c->scene->node, layers[LyrFloat]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		client_set_size(c, c->geom.width, c->geom.height);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	for (i = 0; i < 4; i++) {
		c->border[i] = wlr_scene_rect_create(c->scene, 0, 0,
				c->isurgent ? cfg.urgentcolor : cfg.bordercolor);
		c->border[i]->node.data = c;
	}

	wlr_scene_node_for_each_buffer(&c->scene_surface->node, iter_xdg_scene_buffers, c);

#ifdef XWAYLAND
	if (!client_is_x11(c)) {
#endif
	if (cfg.corner_radius > 0) {
		c->round_border = wlr_scene_rect_create(c->scene, 0, 0, c->isurgent ? cfg.urgentcolor : cfg.bordercolor);
		c->round_border->node.data = c;
		/* Lower the border below the XDG scene tree */
		wlr_scene_node_lower_to_bottom(&c->round_border->node);

		/* hide original border */
		for (i = 0; i < 4; i++) {
			wlr_scene_rect_set_color(c->border[i], transparent);
		}
	}
#ifdef XWAYLAND
	}
#endif

#ifdef XWAYLAND
	if (!client_is_x11(c)) {
#endif
	if (cfg.shadow) {
		c->shadow = wlr_scene_shadow_create(c->scene, 0, 0, c->corner_radius, cfg.shadow_blur_sigma, cfg.shadow_color);
		/* Lower the shadow below the border */
		wlr_scene_node_lower_to_bottom(&c->shadow->node);
	}
#ifdef XWAYLAND
	}
#endif

	/* Initialize client geometry with room for border */
	client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw;

	/* Insert this client into client lists.
	 * Insert after focused client so new windows appear to its right in scroller. */
	{
		Client *focused = focustop(selmon);
		if (focused && !focused->isfloating && !focused->isfullscreen)
			wl_list_insert(&focused->link, &c->link);
		else
			wl_list_insert(&clients, &c->link);
	}
	wl_list_insert(&fstack, &c->flink);

	/* Assign unique scroller column for new windows */
	c->scroller_col = scroller_col_counter++;

	/* Set initial monitor, floating status, and focus:
	 * we always consider floating, clients that have parent and thus
	 * we set the same monitor as its parent.
	 * If there is no parent, apply rules */
	if ((p = client_get_parent(c))) {
		c->isfloating = 1;
		setmon(c, p->mon);
	} else {
		applyrules(c);
	}

	printstatus();

	update_client_corner_radius(c);
	update_client_shadow_color(c);
	update_client_blur(c);

unset_fullscreen:
	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon)
			setfullscreen(w, 0);
	}
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. macwc doesn't support maximization, but
	 * to conform to xdg-shell protocol we still must send a configure.
	 * Since xdg-shell protocol v5 we should ignore request of unsupported
	 * capabilities, just schedule a empty configure when the client uses <5
	 * protocol version
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized
			&& wl_resource_get_version(c->surface.xdg->toplevel->resource)
					< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	bbox = interact ? &sgeom : &c->mon->w;

	client_set_bounds(c, geo.width, geo.height);
	c->geom = geo;
	applybounds(c, bbox);

	/* Update scene-graph, including borders */
	wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
	wlr_scene_rect_set_size(c->border[0], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[1], c->geom.width, c->bw);
	wlr_scene_rect_set_size(c->border[2], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_rect_set_size(c->border[3], c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_node_set_position(&c->border[1]->node, 0, c->geom.height - c->bw);
	wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
	wlr_scene_node_set_position(&c->border[3]->node, c->geom.width - c->bw, c->bw);

	/* this is a no-op if size hasn't changed */
	c->resize = client_set_size(c, c->geom.width - 2 * c->bw,
			c->geom.height - 2 * c->bw);
	client_get_clip(c, &clip);
	scene_tree_apply_clip(&c->scene_surface->node, &clip);

	if (cfg.corner_radius > 0 && c->round_border) {
		wlr_scene_node_set_position(&c->round_border->node, 0, 0);
		wlr_scene_rect_set_size(c->round_border, c->geom.width, c->geom.height);
		wlr_scene_rect_set_clipped_region(c->round_border, (struct clipped_region) {
			.corners = corner_radii_all(c->corner_radius),
			.area = { c->bw, c->bw, c->geom.width - c->bw * 2, c->geom.height - c->bw * 2 }
		});
	}

	if (cfg.shadow && c->shadow) {
		client_set_shadow_blur_sigma(c, (int)round(c->shadow->blur_sigma));
	}
}

void
setfloating(Client *c, int floating)
{
	Client *p = client_get_parent(c);
	c->isfloating = floating;

	update_client_corner_radius(c);
	update_client_shadow_color(c);
	update_client_blur(c);

	/* If in floating layout do not change the client's layer */
	if (!c->mon || !client_surface(c)->mapped || !c->mon->lt[c->mon->sellt]->arrange)
		return;
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
			(p && p->isfullscreen) ? LyrFS
			: c->isfloating ? LyrFloat : LyrTile]);
	arrange(c->mon);
	printstatus();
}

void
setfullscreen(Client *c, int fullscreen)
{
	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;
	c->bw = fullscreen ? 0 : cfg.borderpx;
	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen
			? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m, 0);
	} else {
		/* restore previous size instead of arrange for floating windows since
		 * client positions are set by the user and cannot be recalculated */
		resize(c, c->prev, 0);
	}

	update_client_corner_radius(c);
	update_client_shadow_color(c);
	update_client_blur(c);

	arrange(c->mon);
	printstatus();
}

void
setmon(Client *c, Monitor *m)
{
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	c->mon = m;
	c->prev = c->geom;

	/* Scene graph sends surface leave/enter events on move and resize */
	if (oldmon)
		arrange(oldmon);
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		resize(c, c->geom, 0);
		c->prev.x = (m->w.width - c->prev.width) / 2 + m->m.x;
		c->prev.y = (m->w.height - c->prev.height) / 2 + m->m.y;
		setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
		setfloating(c, c->isfloating);
	}
	focusclient(focustop(selmon), 1);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			focusclient(focustop(selmon), 1);
		}
	} else {
		wl_list_remove(&c->link);
		setmon(c, NULL);
		wl_list_remove(&c->flink);
	}

	wlr_scene_node_destroy(&c->scene->node);
	printstatus();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);
	if (c == focustop(c->mon))
		printstatus();
}

void
config_update_all_clients(void)
{
	Client *c;
	wl_list_for_each(c, &clients, link) {
		c->bw = c->isfullscreen ? 0 : cfg.borderpx;
		c->corner_radius = cfg.corner_radius;
		update_client_corner_radius(c);
		update_client_shadow_color(c);
		update_client_blur(c);

		if (c == focustop(c->mon)) {
			client_set_border_color(c, cfg.focuscolor);
			update_client_focus_decorations(c, 1, 0);
		} else if (c->isurgent) {
			client_set_border_color(c, cfg.urgentcolor);
			update_client_focus_decorations(c, 0, 1);
		} else {
			client_set_border_color(c, cfg.bordercolor);
			update_client_focus_decorations(c, 0, 0);
		}
	}
}
