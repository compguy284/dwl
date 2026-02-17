#ifndef DWL_WORKSPACE_H
#define DWL_WORKSPACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "error.h"

typedef struct DwlWorkspaceManager DwlWorkspaceManager;
typedef struct DwlCompositor DwlCompositor;
typedef struct DwlMonitor DwlMonitor;

typedef struct DwlWorkspaceConfig {
    int tag_count;
    bool per_monitor_tags;
    const char **tag_names;
} DwlWorkspaceConfig;

DwlWorkspaceManager *dwl_workspace_create(DwlCompositor *comp);
void dwl_workspace_destroy(DwlWorkspaceManager *mgr);

DwlError dwl_workspace_configure(DwlWorkspaceManager *mgr, const DwlWorkspaceConfig *cfg);

int dwl_workspace_get_tag_count(DwlWorkspaceManager *mgr);
const char *dwl_workspace_get_tag_name(DwlWorkspaceManager *mgr, int tag);

DwlError dwl_workspace_view(DwlMonitor *mon, uint32_t tags);
DwlError dwl_workspace_view_toggle(DwlMonitor *mon, uint32_t tags);
DwlError dwl_workspace_view_all(DwlMonitor *mon);

uint32_t dwl_workspace_get_visible(DwlMonitor *mon);
uint32_t dwl_workspace_get_occupied(DwlMonitor *mon);
uint32_t dwl_workspace_get_urgent(DwlMonitor *mon);

DwlError dwl_workspace_focus_next_tag(DwlMonitor *mon, int direction);
DwlError dwl_workspace_focus_next_occupied(DwlMonitor *mon, int direction);

#endif /* DWL_WORKSPACE_H */
