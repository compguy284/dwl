#include "layout.h"

static void monocle_arrange(DwlLayoutParams *params)
{
    if (!params || params->client_count == 0)
        return;

    int x = params->area_x + params->gap_outer_h;
    int y = params->area_y + params->gap_outer_v;
    int w = params->area_width - 2 * params->gap_outer_h;
    int h = params->area_height - 2 * params->gap_outer_v;

    for (size_t i = 0; i < params->client_count; i++) {
        DwlLayoutClient *c = &params->clients[i];
        c->x = x;
        c->y = y;
        c->width = w;
        c->height = h;
    }
}

static int monocle_focus_next(const DwlLayoutParams *params, int current, int direction)
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

const DwlLayout dwl_layout_monocle = {
    .name = "monocle",
    .symbol = "[M]",
    .arrange = monocle_arrange,
    .focus_next = monocle_focus_next,
    .user_data = NULL,
};
