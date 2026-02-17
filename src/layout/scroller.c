#include "layout.h"

static void scroller_arrange(DwlLayoutParams *params)
{
    if (!params || params->client_count == 0)
        return;

    int x = params->area_x + params->gap_outer_h;
    int y = params->area_y + params->gap_outer_v;
    int w = params->area_width - 2 * params->gap_outer_h;
    int h = params->area_height - 2 * params->gap_outer_v;

    int col_width = (int)(w * params->master_factor);

    // Calculate scroll offset to center focused window
    int focused = params->focused_index;
    if (focused < 0)
        focused = 0;

    // Position focused window at center of screen
    int focused_x = x + (w - col_width) / 2;
    int scroll_offset = focused_x - (focused * col_width);

    for (size_t i = 0; i < params->client_count; i++) {
        DwlLayoutClient *c = &params->clients[i];
        c->x = scroll_offset + (int)i * col_width;
        c->y = y;
        c->width = col_width - params->gap_inner_h;
        c->height = h;
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
