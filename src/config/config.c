#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_ENTRIES 512
#define MAX_WATCHES 64

typedef enum {
    CONFIG_INT,
    CONFIG_FLOAT,
    CONFIG_BOOL,
    CONFIG_STRING,
    CONFIG_COLOR,
} ConfigType;

typedef struct {
    char *key;
    ConfigType type;
    union {
        int i;
        float f;
        bool b;
        char *s;
        float color[4];
    } value;
} ConfigEntry;

typedef struct {
    int id;
    char *prefix;
    DwlConfigChangeHandler handler;
    void *ctx;
    bool active;
} ConfigWatch;

struct DwlConfig {
    ConfigEntry entries[MAX_ENTRIES];
    size_t count;
    ConfigWatch watches[MAX_WATCHES];
    int next_watch_id;
    char *path;
};

DwlConfig *dwl_config_create(void)
{
    DwlConfig *cfg = calloc(1, sizeof(*cfg));
    if (!cfg)
        return NULL;

    cfg->next_watch_id = 1;
    return cfg;
}

void dwl_config_destroy(DwlConfig *cfg)
{
    if (!cfg)
        return;

    for (size_t i = 0; i < cfg->count; i++) {
        free(cfg->entries[i].key);
        if (cfg->entries[i].type == CONFIG_STRING)
            free(cfg->entries[i].value.s);
    }

    for (int i = 0; i < MAX_WATCHES; i++) {
        free(cfg->watches[i].prefix);
    }

    free(cfg->path);
    free(cfg);
}

static ConfigEntry *find_entry(DwlConfig *cfg, const char *key)
{
    for (size_t i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return &cfg->entries[i];
    }
    return NULL;
}

static ConfigEntry *create_entry(DwlConfig *cfg, const char *key, ConfigType type)
{
    if (cfg->count >= MAX_ENTRIES)
        return NULL;

    ConfigEntry *e = &cfg->entries[cfg->count];
    e->key = strdup(key);
    e->type = type;
    cfg->count++;
    return e;
}

static void notify_watches(DwlConfig *cfg, const char *key)
{
    for (int i = 0; i < MAX_WATCHES; i++) {
        if (!cfg->watches[i].active)
            continue;

        if (cfg->watches[i].prefix == NULL ||
            strncmp(key, cfg->watches[i].prefix, strlen(cfg->watches[i].prefix)) == 0) {
            cfg->watches[i].handler(cfg->watches[i].ctx, key);
        }
    }
}

DwlError dwl_config_load_file(DwlConfig *cfg, const char *path)
{
    if (!cfg || !path)
        return DWL_ERR_INVALID_ARG;

    FILE *f = fopen(path, "r");
    if (!f)
        return DWL_ERR_IO;

    free(cfg->path);
    cfg->path = strdup(path);

    char line[1024];
    char section[256] = "";

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '#' || *p == '\n' || *p == '\0')
            continue;

        if (*p == '[') {
            char *end = strchr(p, ']');
            if (end) {
                *end = '\0';
                strncpy(section, p + 1, sizeof(section) - 1);
            }
            continue;
        }

        char *eq = strchr(p, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key_part = p;
        char *val_part = eq + 1;

        while (*key_part == ' ') key_part++;
        char *key_end = eq - 1;
        while (key_end > key_part && (*key_end == ' ' || *key_end == '\t')) key_end--;
        *(key_end + 1) = '\0';

        while (*val_part == ' ' || *val_part == '\t') val_part++;
        char *val_end = val_part + strlen(val_part) - 1;
        while (val_end > val_part && (*val_end == '\n' || *val_end == ' ')) val_end--;
        *(val_end + 1) = '\0';

        char full_key[1536];
        if (section[0])
            snprintf(full_key, sizeof(full_key), "%s.%s", section, key_part);
        else
            snprintf(full_key, sizeof(full_key), "%s", key_part);

        if (strcmp(val_part, "true") == 0 || strcmp(val_part, "false") == 0) {
            dwl_config_set_bool(cfg, full_key, strcmp(val_part, "true") == 0);
        } else if (val_part[0] == '"') {
            val_part++;
            char *quote_end = strrchr(val_part, '"');
            if (quote_end) *quote_end = '\0';
            dwl_config_set_string(cfg, full_key, val_part);
        } else if (strchr(val_part, '.')) {
            dwl_config_set_float(cfg, full_key, strtof(val_part, NULL));
        } else {
            dwl_config_set_int(cfg, full_key, atoi(val_part));
        }
    }

    fclose(f);
    return DWL_OK;
}

DwlError dwl_config_load_default(DwlConfig *cfg)
{
    if (!cfg)
        return DWL_ERR_INVALID_ARG;

    const char *home = getenv("HOME");
    const char *xdg_config = getenv("XDG_CONFIG_HOME");

    char path[512];

    if (xdg_config) {
        snprintf(path, sizeof(path), "%s/dwl/config.toml", xdg_config);
        if (dwl_config_load_file(cfg, path) == DWL_OK)
            return DWL_OK;
    }

    if (home) {
        snprintf(path, sizeof(path), "%s/.config/dwl/config.toml", home);
        if (dwl_config_load_file(cfg, path) == DWL_OK)
            return DWL_OK;
    }

    if (dwl_config_load_file(cfg, "/etc/dwl/config.toml") == DWL_OK)
        return DWL_OK;

    return DWL_ERR_NOT_FOUND;
}

DwlError dwl_config_reload(DwlConfig *cfg)
{
    if (!cfg || !cfg->path)
        return DWL_ERR_INVALID_ARG;

    char *path = strdup(cfg->path);
    DwlError err = dwl_config_load_file(cfg, path);
    free(path);
    return err;
}

DwlError dwl_config_save(DwlConfig *cfg, const char *path)
{
    if (!cfg || !path)
        return DWL_ERR_INVALID_ARG;

    FILE *f = fopen(path, "w");
    if (!f)
        return DWL_ERR_IO;

    for (size_t i = 0; i < cfg->count; i++) {
        ConfigEntry *e = &cfg->entries[i];
        switch (e->type) {
        case CONFIG_INT:
            fprintf(f, "%s = %d\n", e->key, e->value.i);
            break;
        case CONFIG_FLOAT:
            fprintf(f, "%s = %f\n", e->key, e->value.f);
            break;
        case CONFIG_BOOL:
            fprintf(f, "%s = %s\n", e->key, e->value.b ? "true" : "false");
            break;
        case CONFIG_STRING:
            fprintf(f, "%s = \"%s\"\n", e->key, e->value.s ? e->value.s : "");
            break;
        case CONFIG_COLOR:
            fprintf(f, "%s = [%f, %f, %f, %f]\n", e->key,
                e->value.color[0], e->value.color[1],
                e->value.color[2], e->value.color[3]);
            break;
        }
    }

    fclose(f);
    return DWL_OK;
}

int dwl_config_get_int(DwlConfig *cfg, const char *key, int default_val)
{
    if (!cfg || !key)
        return default_val;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_INT)
        return default_val;

    return e->value.i;
}

float dwl_config_get_float(DwlConfig *cfg, const char *key, float default_val)
{
    if (!cfg || !key)
        return default_val;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_FLOAT)
        return default_val;

    return e->value.f;
}

bool dwl_config_get_bool(DwlConfig *cfg, const char *key, bool default_val)
{
    if (!cfg || !key)
        return default_val;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_BOOL)
        return default_val;

    return e->value.b;
}

const char *dwl_config_get_string(DwlConfig *cfg, const char *key, const char *default_val)
{
    if (!cfg || !key)
        return default_val;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_STRING)
        return default_val;

    return e->value.s;
}

DwlError dwl_config_get_color(DwlConfig *cfg, const char *key, float rgba[4])
{
    if (!cfg || !key || !rgba)
        return DWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_COLOR)
        return DWL_ERR_NOT_FOUND;

    memcpy(rgba, e->value.color, sizeof(float) * 4);
    return DWL_OK;
}

DwlError dwl_config_set_int(DwlConfig *cfg, const char *key, int value)
{
    if (!cfg || !key)
        return DWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_INT);
    if (!e)
        return DWL_ERR_NOMEM;

    e->type = CONFIG_INT;
    e->value.i = value;
    notify_watches(cfg, key);
    return DWL_OK;
}

DwlError dwl_config_set_float(DwlConfig *cfg, const char *key, float value)
{
    if (!cfg || !key)
        return DWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_FLOAT);
    if (!e)
        return DWL_ERR_NOMEM;

    e->type = CONFIG_FLOAT;
    e->value.f = value;
    notify_watches(cfg, key);
    return DWL_OK;
}

DwlError dwl_config_set_bool(DwlConfig *cfg, const char *key, bool value)
{
    if (!cfg || !key)
        return DWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_BOOL);
    if (!e)
        return DWL_ERR_NOMEM;

    e->type = CONFIG_BOOL;
    e->value.b = value;
    notify_watches(cfg, key);
    return DWL_OK;
}

DwlError dwl_config_set_string(DwlConfig *cfg, const char *key, const char *value)
{
    if (!cfg || !key)
        return DWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_STRING);
    if (!e)
        return DWL_ERR_NOMEM;

    if (e->type == CONFIG_STRING)
        free(e->value.s);

    e->type = CONFIG_STRING;
    e->value.s = value ? strdup(value) : NULL;
    notify_watches(cfg, key);
    return DWL_OK;
}

DwlError dwl_config_set_color(DwlConfig *cfg, const char *key, const float rgba[4])
{
    if (!cfg || !key || !rgba)
        return DWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_COLOR);
    if (!e)
        return DWL_ERR_NOMEM;

    e->type = CONFIG_COLOR;
    memcpy(e->value.color, rgba, sizeof(float) * 4);
    notify_watches(cfg, key);
    return DWL_OK;
}

bool dwl_config_has_key(DwlConfig *cfg, const char *key)
{
    return find_entry(cfg, key) != NULL;
}

DwlError dwl_config_remove(DwlConfig *cfg, const char *key)
{
    if (!cfg || !key)
        return DWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            free(cfg->entries[i].key);
            if (cfg->entries[i].type == CONFIG_STRING)
                free(cfg->entries[i].value.s);

            memmove(&cfg->entries[i], &cfg->entries[i + 1],
                    (cfg->count - i - 1) * sizeof(ConfigEntry));
            cfg->count--;
            return DWL_OK;
        }
    }

    return DWL_ERR_NOT_FOUND;
}

int dwl_config_watch(DwlConfig *cfg, const char *key_prefix,
                     DwlConfigChangeHandler handler, void *ctx)
{
    if (!cfg || !handler)
        return -1;

    for (int i = 0; i < MAX_WATCHES; i++) {
        if (!cfg->watches[i].active) {
            cfg->watches[i].id = cfg->next_watch_id++;
            cfg->watches[i].prefix = key_prefix ? strdup(key_prefix) : NULL;
            cfg->watches[i].handler = handler;
            cfg->watches[i].ctx = ctx;
            cfg->watches[i].active = true;
            return cfg->watches[i].id;
        }
    }

    return -1;
}

void dwl_config_unwatch(DwlConfig *cfg, int watch_id)
{
    if (!cfg || watch_id <= 0)
        return;

    for (int i = 0; i < MAX_WATCHES; i++) {
        if (cfg->watches[i].active && cfg->watches[i].id == watch_id) {
            free(cfg->watches[i].prefix);
            cfg->watches[i].prefix = NULL;
            cfg->watches[i].active = false;
            return;
        }
    }
}

const char **dwl_config_keys(DwlConfig *cfg, const char *prefix, size_t *count)
{
    if (!cfg || !count)
        return NULL;

    size_t matching = 0;
    for (size_t i = 0; i < cfg->count; i++) {
        if (!prefix || strncmp(cfg->entries[i].key, prefix, strlen(prefix)) == 0)
            matching++;
    }

    if (matching == 0) {
        *count = 0;
        return NULL;
    }

    const char **keys = calloc(matching, sizeof(char *));
    if (!keys)
        return NULL;

    size_t j = 0;
    for (size_t i = 0; i < cfg->count; i++) {
        if (!prefix || strncmp(cfg->entries[i].key, prefix, strlen(prefix)) == 0)
            keys[j++] = cfg->entries[i].key;
    }

    *count = matching;
    return keys;
}

void dwl_config_keys_free(const char **keys, size_t count)
{
    (void)count;
    free((void *)keys);
}
