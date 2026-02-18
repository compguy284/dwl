#include "layout.h"
#include <stdlib.h>
#include <string.h>

#define MAX_LAYOUTS 32

struct SwlLayoutRegistry {
    SwlLayout layouts[MAX_LAYOUTS];
    size_t count;
};

SwlLayoutRegistry *swl_layout_registry_create(void)
{
    SwlLayoutRegistry *reg = calloc(1, sizeof(*reg));
    return reg;
}

void swl_layout_registry_destroy(SwlLayoutRegistry *reg)
{
    free(reg);
}

SwlError swl_layout_register(SwlLayoutRegistry *reg, const SwlLayout *layout)
{
    if (!reg || !layout || !layout->name)
        return SWL_ERR_INVALID_ARG;

    if (reg->count >= MAX_LAYOUTS)
        return SWL_ERR_NOMEM;

    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->layouts[i].name, layout->name) == 0)
            return SWL_ERR_ALREADY_EXISTS;
    }

    reg->layouts[reg->count++] = *layout;
    return SWL_OK;
}

SwlError swl_layout_unregister(SwlLayoutRegistry *reg, const char *name)
{
    if (!reg || !name)
        return SWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->layouts[i].name, name) == 0) {
            memmove(&reg->layouts[i], &reg->layouts[i + 1],
                    (reg->count - i - 1) * sizeof(SwlLayout));
            reg->count--;
            return SWL_OK;
        }
    }

    return SWL_ERR_NOT_FOUND;
}

const SwlLayout *swl_layout_get(SwlLayoutRegistry *reg, const char *name)
{
    if (!reg || !name)
        return NULL;

    for (size_t i = 0; i < reg->count; i++) {
        if (strcmp(reg->layouts[i].name, name) == 0)
            return &reg->layouts[i];
    }

    return NULL;
}

size_t swl_layout_count(const SwlLayoutRegistry *reg)
{
    return reg ? reg->count : 0;
}

const char **swl_layout_list(const SwlLayoutRegistry *reg, size_t *count)
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

void swl_layout_register_builtins(SwlLayoutRegistry *reg)
{
    swl_layout_register(reg, &swl_layout_scroller);
    swl_layout_register(reg, &swl_layout_floating);
}
