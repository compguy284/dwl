#include "layout.h"
#include <math.h>

static void scroller_arrange(DwlLayoutParams *params)
{
    if (!params || params->client_count == 0)
        return;

    // Match dwl_mac's scroller calculation exactly:
    // col_width = roundf(m->w.width * m->scroller_ratio)
    // geo.width = col_width - gappiv
    // geo.height = m->w.height - 2 * gappoh
    int col_width = (int)roundf(params->area_width * params->master_factor);

    // Total dimensions (including borders) - dwl_client_resize will subtract borders
    int total_w = col_width - params->gap_inner_h;
    int total_h = params->area_height - 2 * params->gap_outer_v;

    // Calculate scroll offset to center focused window
    int focused = params->focused_index;
    if (focused < 0)
        focused = 0;

    // Position focused window at center of screen
    int center_x = params->area_x + (params->area_width - col_width) / 2;
    int scroll_offset = center_x - (focused * col_width);

    for (size_t i = 0; i < params->client_count; i++) {
        DwlLayoutClient *c = &params->clients[i];
        c->x = scroll_offset + (int)i * col_width + params->gap_outer_h;
        c->y = params->area_y + params->gap_outer_v;
        c->width = total_w;
        c->height = total_h;
    }
}

static int scroller_focus_next(const DwlLayoutParams *params, int current, int direction)
{
    if (!params || params->client_count == 0)
        return -1;

    int n = (int)params->client_count;

    if (direction == -1 || direction == 1) {
        int next = current + direction;
        if (next < 0)
            next = n - 1;
        else if (next >= n)
            next = 0;
        return next;
    }

    return current;
}

const DwlLayout dwl_layout_scroller = {
    .name = "scroller",
    .symbol = "[S]",
    .arrange = scroller_arrange,
    .focus_next = scroller_focus_next,
    .user_data = NULL,
};
