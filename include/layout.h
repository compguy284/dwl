#ifndef DWL_LAYOUT_H
#define DWL_LAYOUT_H

#include <stdbool.h>
#include <stddef.h>
#include "error.h"

typedef struct DwlMonitor DwlMonitor;

typedef struct DwlLayoutClient {
    unsigned int id;
    int x, y, width, height;
    bool preserve_aspect;
    int min_width, min_height;
    int max_width, max_height;
    float column_ratio;  // Per-client width ratio for scroller (0.0 = use layout default)
} DwlLayoutClient;

typedef struct DwlLayoutParams {
    int area_x, area_y, area_width, area_height;
    int gap_inner_h, gap_inner_v;
    int gap_outer_h, gap_outer_v;
    float master_factor;
    int master_count;
    size_t client_count;
    int focused_index;  // Index of focused client (-1 if none)
    DwlLayoutClient *clients;
} DwlLayoutParams;

typedef struct DwlLayout {
    const char *name;
    const char *symbol;
    void (*arrange)(DwlLayoutParams *params);
    int (*focus_next)(const DwlLayoutParams *params, int current, int direction);
    void *user_data;
} DwlLayout;

extern const DwlLayout dwl_layout_tile;
extern const DwlLayout dwl_layout_monocle;
extern const DwlLayout dwl_layout_scroller;
extern const DwlLayout dwl_layout_floating;

typedef struct DwlLayoutRegistry DwlLayoutRegistry;

DwlLayoutRegistry *dwl_layout_registry_create(void);
void dwl_layout_registry_destroy(DwlLayoutRegistry *reg);
DwlError dwl_layout_register(DwlLayoutRegistry *reg, const DwlLayout *layout);
DwlError dwl_layout_unregister(DwlLayoutRegistry *reg, const char *name);
const DwlLayout *dwl_layout_get(DwlLayoutRegistry *reg, const char *name);
size_t dwl_layout_count(const DwlLayoutRegistry *reg);
const char **dwl_layout_list(const DwlLayoutRegistry *reg, size_t *count);

void dwl_layout_register_builtins(DwlLayoutRegistry *reg);

#endif /* DWL_LAYOUT_H */
