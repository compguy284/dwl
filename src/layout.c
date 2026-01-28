/*
 * layout.c - Layout, focus, and arrangement functions
 * See LICENSE file for copyright and license details.
 */
#include "layout.h"
#include "client.h"
#include "client_funcs.h"
#include "visual.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
static void setgaps(int oh, int ov, int ih, int iv);

void
arrange(Monitor *m)
{
	Client *c;

	if (!m->wlr_output->enabled)
		return;

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			client_set_suspended(c, !VISIBLEON(c, m));
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			(c = focustop(m)) && c->isfullscreen);

	if (cfg.blur) {
		wlr_scene_node_set_enabled(&m->blur_layer->node, 1);
	}

	snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "%s", m->lt[m->sellt]->symbol);

	/* We move all clients (except fullscreen and unmanaged) to LyrTile while
	 * in floating layout to avoid "real" floating clients be always on top */
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->scene->node.parent == layers[LyrFS])
			continue;

		wlr_scene_node_reparent(&c->scene->node,
				(!m->lt[m->sellt]->arrange && c->isfloating)
						? layers[LyrTile]
						: (m->lt[m->sellt]->arrange && c->isfloating)
								? layers[LyrFloat]
								: c->scene->node.parent);
	}

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *l;
	struct wlr_box full_area = m->m;

	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

		if (!layer_surface->initialized)
			continue;

		if (exclusive != (layer_surface->current.exclusive_zone > 0))
			continue;

		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void
arrangelayers(Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->m;
	LayerSurface *l;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	if (!m->wlr_output->enabled)
		return;

	/* Arrange exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	if (!wlr_box_equal(&usable_area, &m->w)) {
		m->w = usable_area;
		arrange(m);
	}

	/* Arrange non-exlusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	/* Find topmost keyboard interactive layer, if such a layer exists */
	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (locked || !l->layer_surface->current.keyboard_interactive || !l->mapped)
				continue;
			/* Deactivate the focused client. */
			focusclient(NULL, 0);
			exclusive_focus = l;
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
			return;
		}
	}
}

void
focusclient(Client *c, int lift)
{
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;

	if (locked)
		return;

	/* Raise client in stacking order if requested */
	if (c && lift)
		wlr_scene_node_raise_to_top(&c->scene->node);

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	/* Put the new client atop the focus stack and select its monitor */
	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;

		/* Don't change border color if there is an exclusive focus or we are
		 * handling a drag operation */
		if (!exclusive_focus && !seat->drag) {
			client_set_border_color(c, cfg.focuscolor);

			update_client_focus_decorations(c, 1, 0);
		}

		/* Update foreign toplevel activated state */
		if (c->foreign_toplevel)
			wlr_foreign_toplevel_handle_v1_set_activated(c->foreign_toplevel, 1);
	}

	/* Deactivate old client if focus is changing */
	if (old && (!c || client_surface(c) != old)) {
		/* If an overlay is focused, don't focus or activate the client,
		 * but only update its position in fstack to render its border with focuscolor
		 * and focus it after the overlay is closed. */
		if (old_client_type == LayerShell && wlr_scene_node_coords(
					&old_l->scene->node, &unused_lx, &unused_ly)
				&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
			return;
		} else if (old_c && old_c == exclusive_focus && client_wants_focus(old_c)) {
			return;
		/* Don't deactivate old client if the new one wants focus, as this causes issues with winecfg
		 * and probably other clients */
		} else if (old_c && !client_is_unmanaged(old_c) && (!c || !client_wants_focus(c))) {
			client_set_border_color(old_c, cfg.bordercolor);

			update_client_focus_decorations(old_c, 0, 0);

			client_activate_surface(old, 0);

			/* Update foreign toplevel deactivated state */
			if (old_c->foreign_toplevel)
				wlr_foreign_toplevel_handle_v1_set_activated(old_c->foreign_toplevel, 0);
		}
	}
	printstatus();

	if (!c) {
		/* With no client, all we have left is to clear focus */
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Change cursor surface */
	motionnotify(0, NULL, 0, 0, 0, 0);

	/* Have a client, so focus its top-level wlr_surface */
	client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

	/* Activate the new client */
	client_activate_surface(client_surface(c), 1);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, 1);
}

void focusdir(const Arg *arg)
{
	/* Focus the left, right, up, down client relative to the current focused client on selmon */
	Client *c, *sel = focustop(selmon);
	Client *newsel = NULL;
	Client *prev = NULL;
	int dist = INT_MAX;
	int newdist = INT_MAX;

	if (!sel || sel->isfullscreen)
		return;

	/* For left/right in scroller layout, use list order instead of geometry */
	if (selmon->lt[selmon->sellt]->arrange == scroller && (arg->ui == 0 || arg->ui == 1)) {
		wl_list_for_each(c, &clients, link) {
			if (!VISIBLEON(c, selmon) || c->isfloating || c->isfullscreen)
				continue;
			if (c == sel) {
				if (arg->ui == 0 && prev) /* left */
					newsel = prev;
				else if (arg->ui == 1) { /* right - get next */
					c = wl_container_of(c->link.next, c, link);
					while (&c->link != &clients) {
						if (VISIBLEON(c, selmon) && !c->isfloating && !c->isfullscreen) {
							newsel = c;
							break;
						}
						c = wl_container_of(c->link.next, c, link);
					}
				}
				break;
			}
			prev = c;
		}
	} else {
		/* Use geometry for other layouts */
		wl_list_for_each(c, &clients, link) {
			if (!VISIBLEON(c, selmon))
				continue; /* skip non visible windows */
			if (!c->scene->node.enabled)
				continue; /* skip windows hidden by layout */

			if (arg->ui == 0 && sel->geom.x <= c->geom.x)
				continue; /* Client isn't on our left */
			if (arg->ui == 1 && sel->geom.x >= c->geom.x)
				continue; /* Client isn't on our right */
			if (arg->ui == 2 && sel->geom.y <= c->geom.y)
				continue; /* Client isn't above us */
			if (arg->ui == 3 && sel->geom.y >= c->geom.y)
				continue; /* Client isn't below us */

			dist = abs(sel->geom.x - c->geom.x) + abs(sel->geom.y - c->geom.y);
			if (dist < newdist) {
				newdist = dist;
				newsel = c;
			}
		}
	}

	if (newsel) {
		focusclient(newsel, 1);
		arrange(selmon);  /* Update scroller viewport to show focused window */
	}
}


/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client *
focustop(Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &fstack, flink) {
		if (VISIBLEON(c, m))
			return c;
	}
	return NULL;
}

void
monocle(Monitor *m)
{
	Client *c;
	int n = 0;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		n++;
		if (!cfg.monoclegaps)
			resize(c, m->w, 0);
		else
			resize(c, (struct wlr_box){.x = m->w.x + cfg.gappoh, .y = m->w.y + cfg.gappov,
				.width = m->w.width - 2 * cfg.gappoh, .height = m->w.height - 2 * cfg.gappov}, 0);
	}
	if (n)
		snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
	if ((c = focustop(m)))
		wlr_scene_node_raise_to_top(&c->scene->node);
}

void
scroller(Monitor *m)
{
	Client *c, *focused;
	int n = 0, j, found;
	int colwidth, viewport_x;
	int focus_col, focus_x_start, focus_x_end;
	int col_visible_start, col_visible_end;
	int oe = enablegaps;
	float proportion;
	/* Column grouping: map scroller_col to display columns */
	int col_ids[128];      /* scroller_col values in order */
	int col_counts[128];   /* windows per display column */
	int num_cols = 0;
	int col_pos[128] = {0}; /* current position within each column */

	/* Count visible tiled clients and build column mapping */
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		n++;
		/* Check if this scroller_col is already mapped */
		found = 0;
		for (j = 0; j < num_cols; j++) {
			if (col_ids[j] == c->scroller_col) {
				col_counts[j]++;
				found = 1;
				break;
			}
		}
		if (!found && num_cols < 128) {
			col_ids[num_cols] = c->scroller_col;
			col_counts[num_cols] = 1;
			num_cols++;
		}
	}

	if (n == 0)
		return;

	/* Calculate column width from proportion */
	proportion = cfg.scroller_proportions[m->scroller_proportion_idx];
	colwidth = (int)(m->w.width * proportion);

	/* Find focused window's display column */
	focused = focustop(m);
	focus_col = 0;
	if (focused && !focused->isfloating && !focused->isfullscreen) {
		for (j = 0; j < num_cols; j++) {
			if (col_ids[j] == focused->scroller_col) {
				focus_col = j;
				break;
			}
		}
	}
	focus_x_start = focus_col * colwidth;
	focus_x_end = focus_x_start + colwidth;

	/* Calculate viewport based on centering mode */
	if (cfg.scroller_center_mode == ScrollerCenterAlways) {
		viewport_x = focus_x_start - (m->w.width - colwidth) / 2;
	} else { /* ScrollerCenterOnOverflow */
		col_visible_start = m->scroller_viewport_x;
		col_visible_end = m->scroller_viewport_x + m->w.width;

		if (focus_x_start < col_visible_start) {
			/* Scroll left: center the focused column */
			viewport_x = focus_x_start - (m->w.width - colwidth) / 2;
		} else if (focus_x_end > col_visible_end) {
			/* Scroll right: center the focused column */
			viewport_x = focus_x_start - (m->w.width - colwidth) / 2;
		} else {
			/* Keep current viewport */
			viewport_x = m->scroller_viewport_x;
		}
	}

	/* Save viewport for next time */
	m->scroller_viewport_x = viewport_x;

	/* Position each client */
	wl_list_for_each(c, &clients, link) {
		int col, win_x, win_y, win_width, win_height, visible, clipped;
		int render_x, content_offset, visible_width;
		int col_win_count, pos_in_col, total_height, per_win_height;
		struct wlr_box clip;
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;

		/* Find display column for this window */
		col = 0;
		col_win_count = 1;
		for (j = 0; j < num_cols; j++) {
			if (col_ids[j] == c->scroller_col) {
				col = j;
				col_win_count = col_counts[j];
				break;
			}
		}
		pos_in_col = col_pos[col]++;

		/* Calculate window geometry with vertical stacking */
		total_height = m->w.height - 2*m->gappoh*oe;
		per_win_height = (total_height - (col_win_count - 1) * m->gappiv*oe) / col_win_count;
		win_x = m->w.x + (col * colwidth) - viewport_x + m->gappov*oe;
		win_y = m->w.y + m->gappoh*oe + pos_in_col * (per_win_height + m->gappiv*oe);
		win_width = colwidth - 2*m->gappov*oe;
		win_height = per_win_height;

		/* Check if window overlaps with monitor at all */
		visible = (win_x + win_width > m->w.x) && (win_x < m->w.x + m->w.width);
		wlr_scene_node_set_enabled(&c->scene->node, visible);

		/* Skip further processing for completely invisible windows */
		if (!visible) {
			/* Clear any existing clip on hidden windows */
			scene_tree_apply_clip(&c->scene_surface->node, NULL);
			continue;
		}

		/* Calculate render position and content offset for clipped windows */
		clipped = (win_x < m->w.x || win_x + win_width > m->w.x + m->w.width);
		render_x = win_x;
		content_offset = 0;
		visible_width = win_width;
		if (win_x < m->w.x) {
			/* Left overflow: render at monitor edge, offset content to show right portion */
			content_offset = m->w.x - win_x;
			render_x = m->w.x;
			visible_width -= content_offset;
		}
		if (win_x + win_width > m->w.x + m->w.width) {
			/* Right overflow: reduce visible width */
			visible_width -= (win_x + win_width) - (m->w.x + m->w.width);
		}

		/* Resize window to its full logical size and position */
		resize(c, (struct wlr_box){
			.x = win_x,
			.y = win_y,
			.width = win_width,
			.height = win_height
		}, 0);

		if (clipped && visible_width > 0) {
			int left_visible = (content_offset == 0);
			int right_visible = (win_x + win_width <= m->w.x + m->w.width);
			int border_left = left_visible ? c->bw : 0;
			int border_right = right_visible ? c->bw : 0;
			int total_vis_width = visible_width;
			int visible_content = visible_width - border_left - border_right;
			int radius = c->corner_radius + c->bw;

			/* Configure shadow for visible portion - no blur extension on clipped edges */
			if (c->shadow) {
				int shadow_blur = (int)round(c->shadow->blur_sigma);
				int blur_left = left_visible ? shadow_blur : 0;
				int blur_right = right_visible ? shadow_blur : 0;
				int shadow_w = total_vis_width + blur_left + blur_right;
				int shadow_h = win_height + shadow_blur * 2;

				wlr_scene_node_set_enabled(&c->shadow->node, 1);
				wlr_scene_node_set_position(&c->shadow->node, -blur_left, -shadow_blur);
				wlr_scene_shadow_set_size(c->shadow, shadow_w, shadow_h);
				wlr_scene_shadow_set_clipped_region(c->shadow, (struct clipped_region) {
					.corners = corner_radii_new(
						left_visible ? radius : 0,
						right_visible ? radius : 0,
						right_visible ? radius : 0,
						left_visible ? radius : 0
					),
					.area = { blur_left, shadow_blur, total_vis_width, win_height }
				});
			}

			/* Get base clip to find xdg geometry offset */
			client_get_clip(c, &clip);

			/* Position scene at the visible window edge */
			wlr_scene_node_set_position(&c->scene->node, render_x, win_y);

			/* Calculate final clip: crop to visible content area.
			 * content_offset is from the window edge (including border),
			 * so subtract c->bw to get the offset within the surface. */
			clip.x += content_offset > (int)c->bw ? content_offset - c->bw : 0;
			clip.width = visible_content;

			/* Position scene_surface to compensate for clip offset, plus border inset.
			 * wlr_scene_subsurface_tree_set_clip shows the clipped region at its
			 * original position within the surface. We shift scene_surface so the
			 * clipped content appears at the correct position with border space. */
			wlr_scene_node_set_position(&c->scene_surface->node, border_left - clip.x, c->bw - clip.y);
			scene_tree_apply_clip(&c->scene_surface->node, &clip);

			/* Use round_border if available, otherwise fall back to regular borders */
			if (c->round_border) {
				const float *color = c->isurgent ? cfg.urgentcolor :
					(c == focused ? cfg.focuscolor : cfg.bordercolor);
				int b;

				/* Hide regular borders */
				for (b = 0; b < 4; b++)
					wlr_scene_node_set_enabled(&c->border[b]->node, 0);

				/* Configure round_border for visible portion with appropriate corner radii */
				wlr_scene_node_set_enabled(&c->round_border->node, 1);
				wlr_scene_node_set_position(&c->round_border->node, 0, 0);
				wlr_scene_rect_set_size(c->round_border, total_vis_width, win_height);
				wlr_scene_rect_set_color(c->round_border, color);

				/* Set corner radii: 0 for clipped edges, radius for visible edges */
				wlr_scene_rect_set_clipped_region(c->round_border, (struct clipped_region) {
					.corners = corner_radii_new(
						left_visible ? radius : 0,   /* top_left */
						right_visible ? radius : 0,  /* top_right */
						right_visible ? radius : 0,  /* bottom_right */
						left_visible ? radius : 0    /* bottom_left */
					),
					.area = { border_left, c->bw, visible_content, win_height - 2 * c->bw }
				});
			} else {
				/* Fall back to regular borders */
				const float *color = c->isurgent ? cfg.urgentcolor :
					(c == focused ? cfg.focuscolor : cfg.bordercolor);
				int b;
				for (b = 0; b < 4; b++)
					wlr_scene_rect_set_color(c->border[b], color);

				/* Top border */
				wlr_scene_node_set_enabled(&c->border[0]->node, 1);
				wlr_scene_rect_set_size(c->border[0], total_vis_width, c->bw);
				wlr_scene_node_set_position(&c->border[0]->node, 0, 0);

				/* Bottom border */
				wlr_scene_node_set_enabled(&c->border[1]->node, 1);
				wlr_scene_rect_set_size(c->border[1], total_vis_width, c->bw);
				wlr_scene_node_set_position(&c->border[1]->node, 0, win_height - c->bw);

				/* Left border - only if left edge visible */
				wlr_scene_node_set_enabled(&c->border[2]->node, left_visible);
				if (left_visible) {
					wlr_scene_rect_set_size(c->border[2], c->bw, win_height - 2 * c->bw);
					wlr_scene_node_set_position(&c->border[2]->node, 0, c->bw);
				}

				/* Right border - only if right edge visible */
				wlr_scene_node_set_enabled(&c->border[3]->node, right_visible);
				if (right_visible) {
					wlr_scene_rect_set_size(c->border[3], c->bw, win_height - 2 * c->bw);
					wlr_scene_node_set_position(&c->border[3]->node, total_vis_width - c->bw, c->bw);
				}
			}
		} else if (visible) {
			/* Fully visible: normal positioning with decorations */
			int b;
			for (b = 0; b < 4; b++)
				wlr_scene_node_set_enabled(&c->border[b]->node, 1);
			if (c->round_border)
				wlr_scene_node_set_enabled(&c->round_border->node, 1);
			if (c->shadow)
				wlr_scene_node_set_enabled(&c->shadow->node, 1);

			/* Reset surface position and clear any clip */
			wlr_scene_node_set_position(&c->scene_surface->node, c->bw, c->bw);
			scene_tree_apply_clip(&c->scene_surface->node, NULL);
		}
	}
}

void
scroller_cycle_proportion(const Arg *arg)
{
	int len = (int)cfg.scroller_proportions_count;
	if (!selmon)
		return;
	/* arg->i: +1 to cycle forward, -1 to cycle backward */
	selmon->scroller_proportion_idx = (selmon->scroller_proportion_idx + arg->i + len) % len;
	arrange(selmon);
}

void
tile(Monitor *m)
{
	unsigned int mw, my, ty, h, r, oe = enablegaps, ie = enablegaps;
	int i, n = 0;
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	if (cfg.smartgaps == n) {
		oe = 0; // outer gaps disabled
	}

	if (n > m->nmaster)
		mw = m->nmaster ? (int)roundf((m->w.width + m->gappiv*ie) * m->mfact) : 0;
	else
		mw = m->w.width - 2*m->gappov*oe + m->gappiv*ie;
	i = 0;
	my = ty = m->gappoh*oe;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
			continue;
		if (i < m->nmaster) {
			r = MIN(n, m->nmaster) - i;
			h = (m->w.height - my - m->gappoh*oe - m->gappih*ie * (r - 1)) / r;
			resize(c, (struct wlr_box){.x = m->w.x + m->gappov*oe, .y = m->w.y + my,
				.width = mw - m->gappiv*ie, .height = h}, 0);
			my += c->geom.height + m->gappih*ie;
		} else {
			r = n - i;
			h = (m->w.height - ty - m->gappoh*oe - m->gappih*ie * (r - 1)) / r;
			resize(c, (struct wlr_box){.x = m->w.x + mw + m->gappov*oe, .y = m->w.y + ty,
				.width = m->w.width - mw - 2*m->gappov*oe, .height = h}, 0);
			ty += c->geom.height + m->gappih*ie;
		}
		i++;
	}
}

void
consume_or_expel(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);
	int target_col = -1;
	int sel_col_count = 0;
	int col_ids[128];
	int num_cols = 0;
	int sel_display_col = -1;
	int j, found;

	if (!sel || !selmon || selmon->lt[selmon->sellt]->arrange != scroller ||
	    sel->isfloating || sel->isfullscreen)
		return;

	/* Build list of unique columns in order and count windows in selected column */
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, selmon) || c->isfloating || c->isfullscreen)
			continue;
		if (c->scroller_col == sel->scroller_col)
			sel_col_count++;
		found = 0;
		for (j = 0; j < num_cols; j++) {
			if (col_ids[j] == c->scroller_col) {
				found = 1;
				break;
			}
		}
		if (!found && num_cols < 128) {
			if (c->scroller_col == sel->scroller_col)
				sel_display_col = num_cols;
			col_ids[num_cols++] = c->scroller_col;
		}
	}

	if (sel_col_count > 1) {
		/* Expel: move focused window to its own column in the specified direction */
		int new_col = scroller_col_counter++;

		/* Reorder: remove sel from list and reinsert at appropriate position */
		wl_list_remove(&sel->link);

		if (arg->i == 0) {
			/* Expel left: insert before first window of current column */
			wl_list_for_each(c, &clients, link) {
				if (!VISIBLEON(c, selmon) || c->isfloating || c->isfullscreen)
					continue;
				if (c->scroller_col == sel->scroller_col) {
					wl_list_insert(c->link.prev, &sel->link);
					break;
				}
			}
		} else {
			/* Expel right: insert after last window of current column */
			Client *last = NULL;
			wl_list_for_each(c, &clients, link) {
				if (!VISIBLEON(c, selmon) || c->isfloating || c->isfullscreen)
					continue;
				if (c->scroller_col == sel->scroller_col)
					last = c;
			}
			if (last)
				wl_list_insert(&last->link, &sel->link);
			else
				wl_list_insert(&clients, &sel->link);
		}

		sel->scroller_col = new_col;
		arrange(selmon);
		return;
	}

	/* Consume: move focused window into adjacent column at bottom of stack */
	if (arg->i == 0 && sel_display_col > 0) {
		/* Consume left: move focused window into left column */
		target_col = col_ids[sel_display_col - 1];
	} else if (arg->i == 1 && sel_display_col < num_cols - 1) {
		/* Consume right: move focused window into right column */
		target_col = col_ids[sel_display_col + 1];
	}

	if (target_col == -1)
		return; /* No adjacent column in that direction */

	/* Find last window in target column and insert focused window after it */
	{
		Client *last = NULL;
		wl_list_for_each(c, &clients, link) {
			if (!VISIBLEON(c, selmon) || c->isfloating || c->isfullscreen)
				continue;
			if (c->scroller_col == target_col)
				last = c;
		}
		if (last) {
			wl_list_remove(&sel->link);
			wl_list_insert(&last->link, &sel->link);
		}
	}

	sel->scroller_col = target_col;
	arrange(selmon);
}

void
defaultgaps(const Arg *arg)
{
	setgaps(cfg.gappoh, cfg.gappov, cfg.gappih, cfg.gappiv);
}

void
incnmaster(const Arg *arg)
{
	if (!arg || !selmon)
		return;
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

void
incgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov + arg->i,
		selmon->gappih + arg->i,
		selmon->gappiv + arg->i
	);
}

void
incigaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih + arg->i,
		selmon->gappiv + arg->i
	);
}

void
incihgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih + arg->i,
		selmon->gappiv
	);
}

void
incivgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov,
		selmon->gappih,
		selmon->gappiv + arg->i
	);
}

void
incogaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov + arg->i,
		selmon->gappih,
		selmon->gappiv
	);
}

void
incohgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh + arg->i,
		selmon->gappov,
		selmon->gappih,
		selmon->gappiv
	);
}

void
incovgaps(const Arg *arg)
{
	setgaps(
		selmon->gappoh,
		selmon->gappov + arg->i,
		selmon->gappih,
		selmon->gappiv
	);
}

void
setgaps(int oh, int ov, int ih, int iv)
{
	selmon->gappoh = MAX(oh, 0);
	selmon->gappov = MAX(ov, 0);
	selmon->gappih = MAX(ih, 0);
	selmon->gappiv = MAX(iv, 0);
	arrange(selmon);
}

void
setlayout(const Arg *arg)
{
	if (!selmon)
		return;
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	snprintf(selmon->ltsymbol, LENGTH(selmon->ltsymbol), "%s", selmon->lt[selmon->sellt]->symbol);
	arrange(selmon);
	printstatus();
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
	if (f < 0.1 || f > 0.9)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
togglefloating(const Arg *arg)
{
	Client *sel = focustop(selmon);
	/* return if fullscreen */
	if (sel && !sel->isfullscreen)
		setfloating(sel, !sel->isfloating);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

void
togglegaps(const Arg *arg)
{
	enablegaps = !enablegaps;
	arrange(selmon);
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}
	}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}
