#ifndef DWL_RULES_H
#define DWL_RULES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "error.h"

typedef struct DwlRule {
    const char *app_id_pattern;
    const char *title_pattern;
    bool floating;
    int monitor;
} DwlRule;

typedef struct DwlRuleEngine DwlRuleEngine;
typedef struct DwlClient DwlClient;

DwlRuleEngine *dwl_rule_engine_create(void);
void dwl_rule_engine_destroy(DwlRuleEngine *engine);
DwlError dwl_rule_engine_add(DwlRuleEngine *engine, const DwlRule *rule);
DwlError dwl_rule_engine_remove(DwlRuleEngine *engine, size_t index);
void dwl_rule_engine_clear(DwlRuleEngine *engine);
size_t dwl_rule_engine_count(const DwlRuleEngine *engine);
const DwlRule *dwl_rule_engine_get(const DwlRuleEngine *engine, size_t index);
void dwl_rule_engine_apply(DwlRuleEngine *engine, DwlClient *client);

#endif /* DWL_RULES_H */
