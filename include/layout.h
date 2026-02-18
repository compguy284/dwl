#ifndef SWL_LAYOUT_H
#define SWL_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include "error.h"

typedef struct SwlMonitor SwlMonitor;

typedef struct SwlLayoutClient {
    unsigned int id;
    int x, y, width, height;
    bool preserve_aspect;
    int min_width, min_height;
    int max_width, max_height;
    float column_ratio;  // Per-client width ratio for scroller (0.0 = use layout default)
} SwlLayoutClient;

typedef struct SwlLayoutParams {
    int area_x, area_y, area_width, area_height;
    int gap_inner_h, gap_inner_v;
    int gap_outer_h, gap_outer_v;
    float master_factor;
    int master_count;
    size_t client_count;
    int focused_index;  // Index of focused client (-1 if none)
    SwlLayoutClient *clients;
} SwlLayoutParams;

typedef struct SwlLayout {
    const char *name;
    const char *symbol;
    void (*arrange)(SwlLayoutParams *params);
    int (*focus_next)(const SwlLayoutParams *params, int current, int direction);
    void *user_data;
} SwlLayout;

extern const SwlLayout swl_layout_scroller;
extern const SwlLayout swl_layout_floating;

typedef struct SwlLayoutRegistry SwlLayoutRegistry;

SwlLayoutRegistry *swl_layout_registry_create(void);
void swl_layout_registry_destroy(SwlLayoutRegistry *reg);
SwlError swl_layout_register(SwlLayoutRegistry *reg, const SwlLayout *layout);
SwlError swl_layout_unregister(SwlLayoutRegistry *reg, const char *name);
const SwlLayout *swl_layout_get(SwlLayoutRegistry *reg, const char *name);
size_t swl_layout_count(const SwlLayoutRegistry *reg);
const char **swl_layout_list(const SwlLayoutRegistry *reg, size_t *count);

void swl_layout_register_builtins(SwlLayoutRegistry *reg);

#endif /* SWL_LAYOUT_H */
