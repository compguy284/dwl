#include "layout.h"
#include <math.h>
#include <stdlib.h>

static void scroller_arrange(DwlLayoutParams *params)
{
    if (!params || params->client_count == 0)
        return;

    int n = (int)params->client_count;
    int focused = params->focused_index;
    if (focused < 0)
        focused = 0;

    // Compute per-client column widths
    // Each client uses its own column_ratio if set, otherwise the layout default (master_factor)
    int *col_w = calloc(n, sizeof(int));
    if (!col_w)
        return;

    for (int i = 0; i < n; i++) {
        float ratio = params->clients[i].column_ratio > 0.0f
            ? params->clients[i].column_ratio
            : params->master_factor;
        col_w[i] = (int)roundf(params->area_width * ratio);
    }

    // Compute accumulated x position for each client (before offset)
    // acc_x[i] = sum of col_w[0..i-1]
    int *acc_x = calloc(n, sizeof(int));
    if (!acc_x) {
        free(col_w);
        return;
    }

    acc_x[0] = 0;
    for (int i = 1; i < n; i++)
        acc_x[i] = acc_x[i - 1] + col_w[i - 1];

    // Center the focused client on screen
    int focused_center = acc_x[focused] + col_w[focused] / 2;
    int screen_center = params->area_x + params->area_width / 2;
    int offset = screen_center - focused_center;

    int total_h = params->area_height - 2 * params->gap_outer_v;

    for (int i = 0; i < n; i++) {
        DwlLayoutClient *c = &params->clients[i];
        c->x = offset + acc_x[i] + params->gap_outer_h;
        c->y = params->area_y + params->gap_outer_v;
        c->width = col_w[i] - params->gap_inner_h;
        c->height = total_h;
    }

    free(col_w);
    free(acc_x);
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
