#define _POSIX_C_SOURCE 200809L
#include "workspace.h"
#include "compositor.h"
#include "monitor.h"
#include "events.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TAGS 32

struct DwlWorkspaceManager {
    DwlCompositor *comp;
    int tag_count;
    bool per_monitor_tags;
    char *tag_names[MAX_TAGS];
};

DwlWorkspaceManager *dwl_workspace_create(DwlCompositor *comp)
{
    DwlWorkspaceManager *mgr = calloc(1, sizeof(*mgr));
    if (!mgr)
        return NULL;

    mgr->comp = comp;
    mgr->tag_count = 9;
    mgr->per_monitor_tags = false;

    for (int i = 0; i < mgr->tag_count; i++) {
        char name[16];
        snprintf(name, sizeof(name), "%d", i + 1);
        mgr->tag_names[i] = strdup(name);
    }

    return mgr;
}

void dwl_workspace_destroy(DwlWorkspaceManager *mgr)
{
    if (!mgr)
        return;

    for (int i = 0; i < MAX_TAGS; i++) {
        free(mgr->tag_names[i]);
    }

    free(mgr);
}

DwlError dwl_workspace_configure(DwlWorkspaceManager *mgr, const DwlWorkspaceConfig *cfg)
{
    if (!mgr || !cfg)
        return DWL_ERR_INVALID_ARG;

    if (cfg->tag_count < 1 || cfg->tag_count > MAX_TAGS)
        return DWL_ERR_INVALID_ARG;

    for (int i = 0; i < MAX_TAGS; i++) {
        free(mgr->tag_names[i]);
        mgr->tag_names[i] = NULL;
    }

    mgr->tag_count = cfg->tag_count;
    mgr->per_monitor_tags = cfg->per_monitor_tags;

    for (int i = 0; i < cfg->tag_count; i++) {
        if (cfg->tag_names && cfg->tag_names[i])
            mgr->tag_names[i] = strdup(cfg->tag_names[i]);
        else {
            char name[16];
            snprintf(name, sizeof(name), "%d", i + 1);
            mgr->tag_names[i] = strdup(name);
        }
    }

    return DWL_OK;
}

int dwl_workspace_get_tag_count(DwlWorkspaceManager *mgr)
{
    return mgr ? mgr->tag_count : 0;
}

const char *dwl_workspace_get_tag_name(DwlWorkspaceManager *mgr, int tag)
{
    if (!mgr || tag < 0 || tag >= mgr->tag_count)
        return NULL;

    return mgr->tag_names[tag];
}

DwlError dwl_workspace_view(DwlMonitor *mon, uint32_t tags)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    return dwl_monitor_set_tags(mon, tags);
}

DwlError dwl_workspace_view_toggle(DwlMonitor *mon, uint32_t tags)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    uint32_t current = dwl_monitor_get_tags(mon);
    uint32_t newtags = current ^ tags;

    if (newtags == 0)
        return DWL_OK;

    return dwl_monitor_set_tags(mon, newtags);
}

DwlError dwl_workspace_view_all(DwlMonitor *mon)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    return dwl_monitor_set_tags(mon, ~0u);
}

uint32_t dwl_workspace_get_visible(DwlMonitor *mon)
{
    return dwl_monitor_get_tags(mon);
}

uint32_t dwl_workspace_get_occupied(DwlMonitor *mon)
{
    (void)mon;
    // TODO: iterate clients and collect tags
    return 0;
}

uint32_t dwl_workspace_get_urgent(DwlMonitor *mon)
{
    (void)mon;
    // TODO: iterate clients and check urgent flag
    return 0;
}

DwlError dwl_workspace_focus_next_tag(DwlMonitor *mon, int direction)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    uint32_t tags = dwl_monitor_get_tags(mon);

    int current = 0;
    for (int i = 0; i < 32; i++) {
        if (tags & (1 << i)) {
            current = i;
            break;
        }
    }

    int next = current + direction;
    if (next < 0)
        next = 8;
    else if (next > 8)
        next = 0;

    return dwl_monitor_set_tags(mon, 1 << next);
}

DwlError dwl_workspace_focus_next_occupied(DwlMonitor *mon, int direction)
{
    if (!mon)
        return DWL_ERR_INVALID_ARG;

    uint32_t occupied = dwl_workspace_get_occupied(mon);
    if (occupied == 0)
        return DWL_OK;

    uint32_t tags = dwl_monitor_get_tags(mon);

    int current = 0;
    for (int i = 0; i < 32; i++) {
        if (tags & (1 << i)) {
            current = i;
            break;
        }
    }

    for (int j = 1; j <= 9; j++) {
        int next = current + (direction * j);
        if (next < 0)
            next += 9;
        else if (next >= 9)
            next -= 9;

        if (occupied & (1 << next))
            return dwl_monitor_set_tags(mon, 1 << next);
    }

    return DWL_OK;
}
