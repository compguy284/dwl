#define _POSIX_C_SOURCE 200809L
#include "rules.h"
#include "client.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#define MAX_RULES 128

struct SwlRuleEngine {
    SwlRule rules[MAX_RULES];
    regex_t app_id_regex[MAX_RULES];
    regex_t title_regex[MAX_RULES];
    bool has_app_id_regex[MAX_RULES];
    bool has_title_regex[MAX_RULES];
    size_t count;
};

SwlRuleEngine *swl_rule_engine_create(void)
{
    SwlRuleEngine *engine = calloc(1, sizeof(*engine));
    return engine;
}

void swl_rule_engine_destroy(SwlRuleEngine *engine)
{
    if (!engine)
        return;

    for (size_t i = 0; i < engine->count; i++) {
        if (engine->has_app_id_regex[i])
            regfree(&engine->app_id_regex[i]);
        if (engine->has_title_regex[i])
            regfree(&engine->title_regex[i]);
        free((void *)engine->rules[i].app_id_pattern);
        free((void *)engine->rules[i].title_pattern);
    }

    free(engine);
}

SwlError swl_rule_engine_add(SwlRuleEngine *engine, const SwlRule *rule)
{
    if (!engine || !rule)
        return SWL_ERR_INVALID_ARG;

    if (engine->count >= MAX_RULES)
        return SWL_ERR_NOMEM;

    size_t i = engine->count;

    engine->rules[i] = *rule;
    engine->rules[i].app_id_pattern = rule->app_id_pattern ? strdup(rule->app_id_pattern) : NULL;
    engine->rules[i].title_pattern = rule->title_pattern ? strdup(rule->title_pattern) : NULL;

    engine->has_app_id_regex[i] = false;
    engine->has_title_regex[i] = false;

    if (rule->app_id_pattern) {
        if (regcomp(&engine->app_id_regex[i], rule->app_id_pattern, REG_EXTENDED | REG_NOSUB) == 0)
            engine->has_app_id_regex[i] = true;
    }

    if (rule->title_pattern) {
        if (regcomp(&engine->title_regex[i], rule->title_pattern, REG_EXTENDED | REG_NOSUB) == 0)
            engine->has_title_regex[i] = true;
    }

    engine->count++;
    return SWL_OK;
}

SwlError swl_rule_engine_remove(SwlRuleEngine *engine, size_t index)
{
    if (!engine || index >= engine->count)
        return SWL_ERR_INVALID_ARG;

    if (engine->has_app_id_regex[index])
        regfree(&engine->app_id_regex[index]);
    if (engine->has_title_regex[index])
        regfree(&engine->title_regex[index]);

    free((void *)engine->rules[index].app_id_pattern);
    free((void *)engine->rules[index].title_pattern);

    for (size_t i = index; i < engine->count - 1; i++) {
        engine->rules[i] = engine->rules[i + 1];
        engine->app_id_regex[i] = engine->app_id_regex[i + 1];
        engine->title_regex[i] = engine->title_regex[i + 1];
        engine->has_app_id_regex[i] = engine->has_app_id_regex[i + 1];
        engine->has_title_regex[i] = engine->has_title_regex[i + 1];
    }

    engine->count--;
    return SWL_OK;
}

void swl_rule_engine_clear(SwlRuleEngine *engine)
{
    if (!engine)
        return;

    for (size_t i = 0; i < engine->count; i++) {
        if (engine->has_app_id_regex[i])
            regfree(&engine->app_id_regex[i]);
        if (engine->has_title_regex[i])
            regfree(&engine->title_regex[i]);
        free((void *)engine->rules[i].app_id_pattern);
        free((void *)engine->rules[i].title_pattern);
    }

    memset(engine->has_app_id_regex, 0, sizeof(engine->has_app_id_regex));
    memset(engine->has_title_regex, 0, sizeof(engine->has_title_regex));
    engine->count = 0;
}

size_t swl_rule_engine_count(const SwlRuleEngine *engine)
{
    return engine ? engine->count : 0;
}

const SwlRule *swl_rule_engine_get(const SwlRuleEngine *engine, size_t index)
{
    if (!engine || index >= engine->count)
        return NULL;

    return &engine->rules[index];
}

void swl_rule_engine_apply(SwlRuleEngine *engine, SwlClient *client)
{
    if (!engine || !client)
        return;

    SwlClientInfo info = swl_client_get_info(client);

    for (size_t i = 0; i < engine->count; i++) {
        bool match = true;

        if (engine->has_app_id_regex[i] && info.app_id) {
            if (regexec(&engine->app_id_regex[i], info.app_id, 0, NULL, 0) != 0)
                match = false;
        } else if (engine->rules[i].app_id_pattern && !info.app_id) {
            match = false;
        }

        if (match && engine->has_title_regex[i] && info.title) {
            if (regexec(&engine->title_regex[i], info.title, 0, NULL, 0) != 0)
                match = false;
        } else if (engine->rules[i].title_pattern && !info.title) {
            match = false;
        }

        if (match) {
            if (engine->rules[i].floating)
                swl_client_set_floating(client, true);
            break;
        }
    }
}
