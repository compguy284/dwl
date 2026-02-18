#include "layout.h"

static void floating_arrange(SwlLayoutParams *params)
{
    (void)params;
}

static int floating_focus_next(const SwlLayoutParams *params, int current, int direction)
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

const SwlLayout swl_layout_floating = {
    .name = "floating",
    .symbol = "><>",
    .arrange = floating_arrange,
    .focus_next = floating_focus_next,
    .user_data = NULL,
};
