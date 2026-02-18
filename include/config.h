#ifndef SWL_CONFIG_H
#define SWL_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include "error.h"

typedef struct SwlConfig SwlConfig;

SwlConfig *swl_config_create(void);
void swl_config_destroy(SwlConfig *cfg);

SwlError swl_config_load_file(SwlConfig *cfg, const char *path);
SwlError swl_config_load_default(SwlConfig *cfg);
SwlError swl_config_reload(SwlConfig *cfg);
SwlError swl_config_save(SwlConfig *cfg, const char *path);

int swl_config_get_int(SwlConfig *cfg, const char *key, int default_val);
float swl_config_get_float(SwlConfig *cfg, const char *key, float default_val);
bool swl_config_get_bool(SwlConfig *cfg, const char *key, bool default_val);
const char *swl_config_get_string(SwlConfig *cfg, const char *key, const char *default_val);
SwlError swl_config_get_color(SwlConfig *cfg, const char *key, float rgba[4]);

SwlError swl_config_set_int(SwlConfig *cfg, const char *key, int value);
SwlError swl_config_set_float(SwlConfig *cfg, const char *key, float value);
SwlError swl_config_set_bool(SwlConfig *cfg, const char *key, bool value);
SwlError swl_config_set_string(SwlConfig *cfg, const char *key, const char *value);
SwlError swl_config_set_color(SwlConfig *cfg, const char *key, const float rgba[4]);

bool swl_config_has_key(SwlConfig *cfg, const char *key);
SwlError swl_config_remove(SwlConfig *cfg, const char *key);

typedef void (*SwlConfigChangeHandler)(void *ctx, const char *key);
int swl_config_watch(SwlConfig *cfg, const char *key_prefix,
                     SwlConfigChangeHandler handler, void *ctx);
void swl_config_unwatch(SwlConfig *cfg, int watch_id);

const char **swl_config_keys(SwlConfig *cfg, const char *prefix, size_t *count);
void swl_config_keys_free(const char **keys, size_t count);

#endif /* SWL_CONFIG_H */
