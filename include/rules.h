#ifndef SWL_RULES_H
#define SWL_RULES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "error.h"

typedef struct SwlRule {
    const char *app_id_pattern;
    const char *title_pattern;
    bool floating;
    int monitor;
} SwlRule;

typedef struct SwlRuleEngine SwlRuleEngine;
typedef struct SwlClient SwlClient;

SwlRuleEngine *swl_rule_engine_create(void);
void swl_rule_engine_destroy(SwlRuleEngine *engine);
SwlError swl_rule_engine_add(SwlRuleEngine *engine, const SwlRule *rule);
SwlError swl_rule_engine_remove(SwlRuleEngine *engine, size_t index);
void swl_rule_engine_clear(SwlRuleEngine *engine);
size_t swl_rule_engine_count(const SwlRuleEngine *engine);
const SwlRule *swl_rule_engine_get(const SwlRuleEngine *engine, size_t index);
void swl_rule_engine_apply(SwlRuleEngine *engine, SwlClient *client);

#endif /* SWL_RULES_H */
