#ifndef DWL_CONFIG_H
#define DWL_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include "error.h"

typedef struct DwlConfig DwlConfig;

DwlConfig *dwl_config_create(void);
void dwl_config_destroy(DwlConfig *cfg);

DwlError dwl_config_load_file(DwlConfig *cfg, const char *path);
DwlError dwl_config_load_default(DwlConfig *cfg);
DwlError dwl_config_reload(DwlConfig *cfg);
DwlError dwl_config_save(DwlConfig *cfg, const char *path);

int dwl_config_get_int(DwlConfig *cfg, const char *key, int default_val);
float dwl_config_get_float(DwlConfig *cfg, const char *key, float default_val);
bool dwl_config_get_bool(DwlConfig *cfg, const char *key, bool default_val);
const char *dwl_config_get_string(DwlConfig *cfg, const char *key, const char *default_val);
DwlError dwl_config_get_color(DwlConfig *cfg, const char *key, float rgba[4]);

DwlError dwl_config_set_int(DwlConfig *cfg, const char *key, int value);
DwlError dwl_config_set_float(DwlConfig *cfg, const char *key, float value);
DwlError dwl_config_set_bool(DwlConfig *cfg, const char *key, bool value);
DwlError dwl_config_set_string(DwlConfig *cfg, const char *key, const char *value);
DwlError dwl_config_set_color(DwlConfig *cfg, const char *key, const float rgba[4]);

bool dwl_config_has_key(DwlConfig *cfg, const char *key);
DwlError dwl_config_remove(DwlConfig *cfg, const char *key);

typedef void (*DwlConfigChangeHandler)(void *ctx, const char *key);
int dwl_config_watch(DwlConfig *cfg, const char *key_prefix,
                     DwlConfigChangeHandler handler, void *ctx);
void dwl_config_unwatch(DwlConfig *cfg, int watch_id);

const char **dwl_config_keys(DwlConfig *cfg, const char *prefix, size_t *count);
void dwl_config_keys_free(const char **keys, size_t count);

#endif /* DWL_CONFIG_H */
