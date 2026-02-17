#include "layout.h"

static void tile_arrange(DwlLayoutParams *params)
{
    if (!params || params->client_count == 0)
        return;

    int n = (int)params->client_count;
    int mw, my, ty;
    int x = params->area_x + params->gap_outer_h;
    int y = params->area_y + params->gap_outer_v;
    int w = params->area_width - 2 * params->gap_outer_h;
    int h = params->area_height - 2 * params->gap_outer_v;

    if (n > params->master_count)
        mw = params->master_count ? (int)(w * params->master_factor) : 0;
    else
        mw = w;

    my = ty = 0;
    for (int i = 0; i < n; i++) {
        DwlLayoutClient *c = &params->clients[i];

        if (i < params->master_count) {
            int nh = (h - my) / (params->master_count - i) - params->gap_inner_v;
            c->x = x;
            c->y = y + my;
            c->width = mw - params->gap_inner_h;
            c->height = nh;
            my += nh + params->gap_inner_v;
        } else {
            int nw = n > params->master_count ? w - mw : w;
            int nh = (h - ty) / (n - i) - params->gap_inner_v;
            c->x = x + mw + (params->master_count > 0 ? params->gap_inner_h : 0);
            c->y = y + ty;
            c->width = nw - params->gap_inner_h;
            c->height = nh;
            ty += nh + params->gap_inner_v;
        }
    }
}

static int tile_focus_next(const DwlLayoutParams *params, int current, int direction)
{
    if (!params || params->client_count == 0)
        return -1;

    int n = (int)params->client_count;
    int next = current + direction;

    if (next < 0)
        next = n - 1;
    else if (next >= n)
        next = 0;

    return next;
}

const DwlLayout dwl_layout_tile = {
    .name = "tile",
    .symbol = "[]=",
    .arrange = tile_arrange,
    .focus_next = tile_focus_next,
    .user_data = NULL,
};
