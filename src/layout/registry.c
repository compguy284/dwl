#include "layout.h"
#include <stdlib.h>
#include <string.h>

#define MAX_LAYOUTS 32

struct DwlLayoutRegistry {
    DwlLayout layouts[MAX_LAYOUTS];
    size_t count;
};

DwlLayoutRegistry *dwl_layout_registry_create(void)
{
    DwlLayoutRegistry *reg = calloc(1, sizeof(*reg));
    return reg;
}

void dwl_layout_registry_destroy(DwlLayoutRegistry *reg)
{
    free(reg);
}

DwlError dwl_layout_register(DwlLayoutRegistry *reg, const DwlLayout *layout)
{
    if (!reg || !layout || !layout->name)
        return DWL_ERR_INVALID_ARG;

    if (reg->count >= MAX_LAYOUTS)
        return DWL_ERR_NOMEM;

    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->layouts[i].name, layout->name) == 0)
            return DWL_ERR_ALREADY_EXISTS;
    }

    reg->layouts[reg->count++] = *layout;
    return DWL_OK;
}

DwlError dwl_layout_unregister(DwlLayoutRegistry *reg, const char *name)
{
    if (!reg || !name)
        return DWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->layouts[i].name, name) == 0) {
            memmove(&reg->layouts[i], &reg->layouts[i + 1],
                    (reg->count - i - 1) * sizeof(DwlLayout));
            reg->count--;
            return DWL_OK;
        }
    }

    return DWL_ERR_NOT_FOUND;
}

const DwlLayout *dwl_layout_get(DwlLayoutRegistry *reg, const char *name)
{
    if (!reg || !name)
        return NULL;

    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->layouts[i].name, name) == 0)
            return &reg->layouts[i];
    }

    return NULL;
}

size_t dwl_layout_count(const DwlLayoutRegistry *reg)
{
    return reg ? reg->count : 0;
}

const char **dwl_layout_list(const DwlLayoutRegistry *reg, size_t *count)
{
    if (!reg || !count)
        return NULL;

    const char **names = calloc(reg->count, sizeof(char *));
    if (!names)
        return NULL;

    for (size_t i = 0; i < reg->count; i++)
        names[i] = reg->layouts[i].name;

    *count = reg->count;
    return names;
}

void dwl_layout_register_builtins(DwlLayoutRegistry *reg)
{
    dwl_layout_register(reg, &dwl_layout_scroller);
    dwl_layout_register(reg, &dwl_layout_floating);
}
