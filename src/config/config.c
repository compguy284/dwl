#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "toml.h"

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
    SwlConfigChangeHandler handler;
    void *ctx;
    bool active;
} ConfigWatch;

struct SwlConfig {
    ConfigEntry entries[MAX_ENTRIES];
    size_t count;
    ConfigWatch watches[MAX_WATCHES];
    int next_watch_id;
    char *path;
};

SwlConfig *swl_config_create(void)
{
    SwlConfig *cfg = calloc(1, sizeof(*cfg));
    if (!cfg)
        return NULL;

    cfg->next_watch_id = 1;
    return cfg;
}

void swl_config_destroy(SwlConfig *cfg)
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

static ConfigEntry *find_entry(SwlConfig *cfg, const char *key)
{
    for (size_t i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return &cfg->entries[i];
    }
    return NULL;
}

static ConfigEntry *create_entry(SwlConfig *cfg, const char *key, ConfigType type)
{
    if (cfg->count >= MAX_ENTRIES)
        return NULL;

    ConfigEntry *e = &cfg->entries[cfg->count];
    e->key = strdup(key);
    e->type = type;
    cfg->count++;
    return e;
}

static void notify_watches(SwlConfig *cfg, const char *key)
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

static void clear_entries(SwlConfig *cfg)
{
    for (size_t i = 0; i < cfg->count; i++) {
        free(cfg->entries[i].key);
        if (cfg->entries[i].type == CONFIG_STRING)
            free(cfg->entries[i].value.s);
    }
    memset(cfg->entries, 0, cfg->count * sizeof(ConfigEntry));
    cfg->count = 0;
}

static int parse_hex_color(const char *s, float rgba[4])
{
    if (!s || s[0] != '#')
        return 0;

    size_t len = strlen(s + 1);
    unsigned int r, g, b, a = 255;

    if (len == 6) {
        if (sscanf(s + 1, "%02x%02x%02x", &r, &g, &b) != 3)
            return 0;
    } else if (len == 8) {
        if (sscanf(s + 1, "%02x%02x%02x%02x", &r, &g, &b, &a) != 4)
            return 0;
    } else {
        return 0;
    }

    rgba[0] = r / 255.0f;
    rgba[1] = g / 255.0f;
    rgba[2] = b / 255.0f;
    rgba[3] = a / 255.0f;
    return 1;
}

static void flatten_keybinding_value(SwlConfig *cfg, const char *full_key,
                                     toml_table_t *tbl)
{
    /* Convert inline table { action = "spawn", command = ["wmenu-run"] }
     * or { action = "view", arg = 1 } to "action:argument" string */
    toml_datum_t action = toml_string_in(tbl, "action");
    if (!action.ok)
        return;

    char value[1024];

    /* Check for "command" array (for spawn) */
    toml_array_t *cmd = toml_array_in(tbl, "command");
    if (cmd) {
        int n = toml_array_nelem(cmd);
        /* Join command array elements with spaces */
        char cmd_str[512] = "";
        size_t offset = 0;
        for (int i = 0; i < n && offset < sizeof(cmd_str) - 1; i++) {
            toml_datum_t elem = toml_string_at(cmd, i);
            if (elem.ok) {
                if (i > 0 && offset < sizeof(cmd_str) - 1)
                    cmd_str[offset++] = ' ';
                size_t elen = strlen(elem.u.s);
                if (offset + elen < sizeof(cmd_str)) {
                    memcpy(cmd_str + offset, elem.u.s, elen);
                    offset += elen;
                }
                free(elem.u.s);
            }
        }
        cmd_str[offset] = '\0';
        snprintf(value, sizeof(value), "%s:%s", action.u.s, cmd_str);
    } else {
        /* Check for "arg" (int, float, or string) */
        toml_datum_t arg_s = toml_string_in(tbl, "arg");
        if (arg_s.ok) {
            snprintf(value, sizeof(value), "%s:%s", action.u.s, arg_s.u.s);
            free(arg_s.u.s);
        } else {
            toml_datum_t arg_i = toml_int_in(tbl, "arg");
            if (arg_i.ok) {
                snprintf(value, sizeof(value), "%s:%ld", action.u.s,
                         (long)arg_i.u.i);
            } else {
                toml_datum_t arg_d = toml_double_in(tbl, "arg");
                if (arg_d.ok) {
                    snprintf(value, sizeof(value), "%s:%g", action.u.s,
                             arg_d.u.d);
                } else {
                    /* No argument — just the action name */
                    snprintf(value, sizeof(value), "%s", action.u.s);
                }
            }
        }
    }

    free(action.u.s);
    swl_config_set_string(cfg, full_key, value);
}

static void flatten_table(SwlConfig *cfg, toml_table_t *tbl, const char *prefix);

static void flatten_array(SwlConfig *cfg, toml_array_t *arr, const char *prefix)
{
    char kind = toml_array_kind(arr);

    if (kind == 't') {
        /* Array of tables: [[rules]], [[monitors]] */
        int n = toml_array_nelem(arr);
        for (int i = 0; i < n; i++) {
            toml_table_t *elem = toml_table_at(arr, i);
            if (!elem)
                continue;

            /* For monitors, use the "name" field as key if present */
            if (strcmp(prefix, "monitors") == 0) {
                toml_datum_t name = toml_string_in(elem, "name");
                if (name.ok) {
                    char sub_prefix[2048];
                    snprintf(sub_prefix, sizeof(sub_prefix), "%s.%s",
                             prefix, name.u.s);
                    free(name.u.s);
                    flatten_table(cfg, elem, sub_prefix);
                    continue;
                }
            }

            /* Default: use numeric index */
            char sub_prefix[2048];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s.%d", prefix, i);
            flatten_table(cfg, elem, sub_prefix);
        }
    }
    /* Value arrays are not flattened to the key-value store (not needed) */
}

static void flatten_table(SwlConfig *cfg, toml_table_t *tbl, const char *prefix)
{
    bool is_keybinding = prefix &&
        (strcmp(prefix, "keybindings") == 0 || strcmp(prefix, "buttons") == 0);

    /* Iterate key-values */
    for (int i = 0; ; i++) {
        const char *key = toml_key_in(tbl, i);
        if (!key)
            break;

        char full_key[1536];
        if (prefix && prefix[0])
            snprintf(full_key, sizeof(full_key), "%s.%s", prefix, key);
        else
            snprintf(full_key, sizeof(full_key), "%s", key);

        /* Check if this key has a sub-table */
        toml_table_t *sub = toml_table_in(tbl, key);
        if (sub) {
            if (is_keybinding) {
                /* Under keybindings/buttons, inline tables are binding values */
                flatten_keybinding_value(cfg, full_key, sub);
            } else {
                flatten_table(cfg, sub, full_key);
            }
            continue;
        }

        /* Check if this key has an array */
        toml_array_t *arr = toml_array_in(tbl, key);
        if (arr) {
            flatten_array(cfg, arr, full_key);
            continue;
        }

        /* Try string first (for hex color detection) */
        toml_datum_t s = toml_string_in(tbl, key);
        if (s.ok) {
            float rgba[4];
            if (parse_hex_color(s.u.s, rgba)) {
                swl_config_set_color(cfg, full_key, rgba);
            } else {
                swl_config_set_string(cfg, full_key, s.u.s);
            }
            free(s.u.s);
            continue;
        }

        /* Try bool */
        toml_datum_t b = toml_bool_in(tbl, key);
        if (b.ok) {
            swl_config_set_bool(cfg, full_key, b.u.b != 0);
            continue;
        }

        /* Try int */
        toml_datum_t iv = toml_int_in(tbl, key);
        if (iv.ok) {
            swl_config_set_int(cfg, full_key, (int)iv.u.i);
            continue;
        }

        /* Try double */
        toml_datum_t d = toml_double_in(tbl, key);
        if (d.ok) {
            swl_config_set_float(cfg, full_key, (float)d.u.d);
            continue;
        }
    }
}

SwlError swl_config_load_file(SwlConfig *cfg, const char *path)
{
    if (!cfg || !path)
        return SWL_ERR_INVALID_ARG;

    FILE *f = fopen(path, "r");
    if (!f)
        return SWL_ERR_IO;

    char errbuf[256];
    toml_table_t *root = toml_parse_file(f, errbuf, sizeof(errbuf));
    fclose(f);

    if (!root) {
        fprintf(stderr, "config: %s: %s\n", path, errbuf);
        return SWL_ERR_CONFIG;
    }

    free(cfg->path);
    cfg->path = strdup(path);

    clear_entries(cfg);
    flatten_table(cfg, root, "");
    toml_free(root);

    return SWL_OK;
}

SwlError swl_config_load_default(SwlConfig *cfg)
{
    if (!cfg)
        return SWL_ERR_INVALID_ARG;

    const char *home = getenv("HOME");
    const char *xdg_config = getenv("XDG_CONFIG_HOME");

    char path[512];

    if (xdg_config) {
        snprintf(path, sizeof(path), "%s/swl/config.toml", xdg_config);
        if (swl_config_load_file(cfg, path) == SWL_OK)
            return SWL_OK;
    }

    if (home) {
        snprintf(path, sizeof(path), "%s/.config/swl/config.toml", home);
        if (swl_config_load_file(cfg, path) == SWL_OK)
            return SWL_OK;
    }

    if (swl_config_load_file(cfg, "/etc/swl/config.toml") == SWL_OK)
        return SWL_OK;

    return SWL_ERR_NOT_FOUND;
}

SwlError swl_config_reload(SwlConfig *cfg)
{
    if (!cfg || !cfg->path)
        return SWL_ERR_INVALID_ARG;

    char *path = strdup(cfg->path);
    SwlError err = swl_config_load_file(cfg, path);
    free(path);
    return err;
}

SwlError swl_config_save(SwlConfig *cfg, const char *path)
{
    if (!cfg || !path)
        return SWL_ERR_INVALID_ARG;

    FILE *f = fopen(path, "w");
    if (!f)
        return SWL_ERR_IO;

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
    return SWL_OK;
}

int swl_config_get_int(SwlConfig *cfg, const char *key, int default_val)
{
    if (!cfg || !key)
        return default_val;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_INT)
        return default_val;

    return e->value.i;
}

float swl_config_get_float(SwlConfig *cfg, const char *key, float default_val)
{
    if (!cfg || !key)
        return default_val;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_FLOAT)
        return default_val;

    return e->value.f;
}

bool swl_config_get_bool(SwlConfig *cfg, const char *key, bool default_val)
{
    if (!cfg || !key)
        return default_val;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_BOOL)
        return default_val;

    return e->value.b;
}

const char *swl_config_get_string(SwlConfig *cfg, const char *key, const char *default_val)
{
    if (!cfg || !key)
        return default_val;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_STRING)
        return default_val;

    return e->value.s;
}

SwlError swl_config_get_color(SwlConfig *cfg, const char *key, float rgba[4])
{
    if (!cfg || !key || !rgba)
        return SWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e || e->type != CONFIG_COLOR)
        return SWL_ERR_NOT_FOUND;

    memcpy(rgba, e->value.color, sizeof(float) * 4);
    return SWL_OK;
}

SwlError swl_config_set_int(SwlConfig *cfg, const char *key, int value)
{
    if (!cfg || !key)
        return SWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_INT);
    if (!e)
        return SWL_ERR_NOMEM;

    e->type = CONFIG_INT;
    e->value.i = value;
    notify_watches(cfg, key);
    return SWL_OK;
}

SwlError swl_config_set_float(SwlConfig *cfg, const char *key, float value)
{
    if (!cfg || !key)
        return SWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_FLOAT);
    if (!e)
        return SWL_ERR_NOMEM;

    e->type = CONFIG_FLOAT;
    e->value.f = value;
    notify_watches(cfg, key);
    return SWL_OK;
}

SwlError swl_config_set_bool(SwlConfig *cfg, const char *key, bool value)
{
    if (!cfg || !key)
        return SWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_BOOL);
    if (!e)
        return SWL_ERR_NOMEM;

    e->type = CONFIG_BOOL;
    e->value.b = value;
    notify_watches(cfg, key);
    return SWL_OK;
}

SwlError swl_config_set_string(SwlConfig *cfg, const char *key, const char *value)
{
    if (!cfg || !key)
        return SWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_STRING);
    if (!e)
        return SWL_ERR_NOMEM;

    if (e->type == CONFIG_STRING)
        free(e->value.s);

    e->type = CONFIG_STRING;
    e->value.s = value ? strdup(value) : NULL;
    notify_watches(cfg, key);
    return SWL_OK;
}

SwlError swl_config_set_color(SwlConfig *cfg, const char *key, const float rgba[4])
{
    if (!cfg || !key || !rgba)
        return SWL_ERR_INVALID_ARG;

    ConfigEntry *e = find_entry(cfg, key);
    if (!e)
        e = create_entry(cfg, key, CONFIG_COLOR);
    if (!e)
        return SWL_ERR_NOMEM;

    e->type = CONFIG_COLOR;
    memcpy(e->value.color, rgba, sizeof(float) * 4);
    notify_watches(cfg, key);
    return SWL_OK;
}

bool swl_config_has_key(SwlConfig *cfg, const char *key)
{
    return find_entry(cfg, key) != NULL;
}

SwlError swl_config_remove(SwlConfig *cfg, const char *key)
{
    if (!cfg || !key)
        return SWL_ERR_INVALID_ARG;

    for (size_t i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            free(cfg->entries[i].key);
            if (cfg->entries[i].type == CONFIG_STRING)
                free(cfg->entries[i].value.s);

            memmove(&cfg->entries[i], &cfg->entries[i + 1],
                    (cfg->count - i - 1) * sizeof(ConfigEntry));
            cfg->count--;
            return SWL_OK;
        }
    }

    return SWL_ERR_NOT_FOUND;
}

int swl_config_watch(SwlConfig *cfg, const char *key_prefix,
                     SwlConfigChangeHandler handler, void *ctx)
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

void swl_config_unwatch(SwlConfig *cfg, int watch_id)
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

const char **swl_config_keys(SwlConfig *cfg, const char *prefix, size_t *count)
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

void swl_config_keys_free(const char **keys, size_t count)
{
    (void)count;
    free((void *)keys);
}
