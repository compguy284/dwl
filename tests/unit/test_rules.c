#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdlib.h>
#include <string.h>

#include "rules.h"

/* Tests */
static void test_rule_engine_create(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);
    assert_int_equal(dwl_rule_engine_count(engine), 0);
    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_destroy_null(void **state)
{
    (void)state;

    /* Should not crash */
    dwl_rule_engine_destroy(NULL);
}

static void test_rule_engine_add(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rule = {
        .app_id_pattern = "firefox",
        .title_pattern = NULL,
        .floating = false,
        .monitor = -1,
    };

    DwlError err = dwl_rule_engine_add(engine, &rule);
    assert_int_equal(err, DWL_OK);
    assert_int_equal(dwl_rule_engine_count(engine), 1);

    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_add_null_checks(void **state)
{
    (void)state;

    DwlRule rule = {
        .app_id_pattern = "test",
        .title_pattern = NULL,
        .floating = false,
        .monitor = -1,
    };

    assert_int_equal(dwl_rule_engine_add(NULL, &rule), DWL_ERR_INVALID_ARG);

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_int_equal(dwl_rule_engine_add(engine, NULL), DWL_ERR_INVALID_ARG);
    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_add_multiple(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rules[] = {
        { .app_id_pattern = "firefox" },
        { .app_id_pattern = "thunderbird" },
        { .app_id_pattern = "mpv", .floating = true },
    };

    for (size_t i = 0; i < 3; i++) {
        DwlError err = dwl_rule_engine_add(engine, &rules[i]);
        assert_int_equal(err, DWL_OK);
    }

    assert_int_equal(dwl_rule_engine_count(engine), 3);

    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_get(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rule = {
        .app_id_pattern = "firefox",
        .title_pattern = "Mozilla.*",
        .floating = true,
        .monitor = 0,
    };

    dwl_rule_engine_add(engine, &rule);

    const DwlRule *got = dwl_rule_engine_get(engine, 0);
    assert_non_null(got);
    assert_string_equal(got->app_id_pattern, "firefox");
    assert_string_equal(got->title_pattern, "Mozilla.*");
    assert_true(got->floating);
    assert_int_equal(got->monitor, 0);

    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_get_out_of_bounds(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rule = { .app_id_pattern = "test" };
    dwl_rule_engine_add(engine, &rule);

    assert_null(dwl_rule_engine_get(engine, 1));
    assert_null(dwl_rule_engine_get(engine, 100));
    assert_null(dwl_rule_engine_get(NULL, 0));

    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_remove(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rules[] = {
        { .app_id_pattern = "first" },
        { .app_id_pattern = "second" },
        { .app_id_pattern = "third" },
    };

    for (size_t i = 0; i < 3; i++) {
        dwl_rule_engine_add(engine, &rules[i]);
    }

    assert_int_equal(dwl_rule_engine_count(engine), 3);

    /* Remove middle rule */
    DwlError err = dwl_rule_engine_remove(engine, 1);
    assert_int_equal(err, DWL_OK);
    assert_int_equal(dwl_rule_engine_count(engine), 2);

    /* Check remaining rules */
    assert_string_equal(dwl_rule_engine_get(engine, 0)->app_id_pattern, "first");
    assert_string_equal(dwl_rule_engine_get(engine, 1)->app_id_pattern, "third");

    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_remove_invalid_index(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rule = { .app_id_pattern = "test" };
    dwl_rule_engine_add(engine, &rule);

    DwlError err = dwl_rule_engine_remove(engine, 5);
    assert_int_equal(err, DWL_ERR_INVALID_ARG);
    assert_int_equal(dwl_rule_engine_count(engine), 1);

    err = dwl_rule_engine_remove(NULL, 0);
    assert_int_equal(err, DWL_ERR_INVALID_ARG);

    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_clear(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rules[] = {
        { .app_id_pattern = "first" },
        { .app_id_pattern = "second" },
        { .app_id_pattern = "third" },
    };

    for (size_t i = 0; i < 3; i++) {
        dwl_rule_engine_add(engine, &rules[i]);
    }

    assert_int_equal(dwl_rule_engine_count(engine), 3);

    dwl_rule_engine_clear(engine);
    assert_int_equal(dwl_rule_engine_count(engine), 0);

    /* Can still add after clear */
    dwl_rule_engine_add(engine, &rules[0]);
    assert_int_equal(dwl_rule_engine_count(engine), 1);

    dwl_rule_engine_destroy(engine);
}

static void test_rule_engine_clear_null(void **state)
{
    (void)state;

    /* Should not crash */
    dwl_rule_engine_clear(NULL);
}

static void test_rule_engine_count_null(void **state)
{
    (void)state;

    assert_int_equal(dwl_rule_engine_count(NULL), 0);
}

static void test_rule_regex_app_id(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    /* Regex patterns */
    DwlRule rule1 = { .app_id_pattern = "^firefox.*$" };
    DwlRule rule2 = { .app_id_pattern = ".*terminal.*" };

    dwl_rule_engine_add(engine, &rule1);
    dwl_rule_engine_add(engine, &rule2);

    /* Verify patterns were stored */
    const DwlRule *got = dwl_rule_engine_get(engine, 0);
    assert_string_equal(got->app_id_pattern, "^firefox.*$");

    got = dwl_rule_engine_get(engine, 1);
    assert_string_equal(got->app_id_pattern, ".*terminal.*");

    dwl_rule_engine_destroy(engine);
}

static void test_rule_regex_title(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    /* Title pattern */
    DwlRule rule = {
        .app_id_pattern = NULL,
        .title_pattern = ".*YouTube.*",
    };

    dwl_rule_engine_add(engine, &rule);

    const DwlRule *got = dwl_rule_engine_get(engine, 0);
    assert_null(got->app_id_pattern);
    assert_string_equal(got->title_pattern, ".*YouTube.*");

    dwl_rule_engine_destroy(engine);
}

static void test_rule_both_patterns(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rule = {
        .app_id_pattern = "mpv",
        .title_pattern = ".*\\.mp4$",
        .floating = true,
    };

    dwl_rule_engine_add(engine, &rule);

    const DwlRule *got = dwl_rule_engine_get(engine, 0);
    assert_string_equal(got->app_id_pattern, "mpv");
    assert_string_equal(got->title_pattern, ".*\\.mp4$");
    assert_true(got->floating);

    dwl_rule_engine_destroy(engine);
}

static void test_rule_pattern_ownership(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    /* Create rule with patterns on heap to verify engine copies them */
    char *app_id = strdup("test_app");
    char *title = strdup("test_title");

    DwlRule rule = {
        .app_id_pattern = app_id,
        .title_pattern = title,
    };

    dwl_rule_engine_add(engine, &rule);

    /* Free original strings */
    free(app_id);
    free(title);

    /* Engine should still have valid copies */
    const DwlRule *got = dwl_rule_engine_get(engine, 0);
    assert_non_null(got);
    assert_string_equal(got->app_id_pattern, "test_app");
    assert_string_equal(got->title_pattern, "test_title");

    dwl_rule_engine_destroy(engine);
}

static void test_rule_remove_first(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rules[] = {
        { .app_id_pattern = "first" },
        { .app_id_pattern = "second" },
    };

    dwl_rule_engine_add(engine, &rules[0]);
    dwl_rule_engine_add(engine, &rules[1]);

    dwl_rule_engine_remove(engine, 0);
    assert_int_equal(dwl_rule_engine_count(engine), 1);
    assert_string_equal(dwl_rule_engine_get(engine, 0)->app_id_pattern, "second");

    dwl_rule_engine_destroy(engine);
}

static void test_rule_remove_last(void **state)
{
    (void)state;

    DwlRuleEngine *engine = dwl_rule_engine_create();
    assert_non_null(engine);

    DwlRule rules[] = {
        { .app_id_pattern = "first" },
        { .app_id_pattern = "second" },
    };

    dwl_rule_engine_add(engine, &rules[0]);
    dwl_rule_engine_add(engine, &rules[1]);

    dwl_rule_engine_remove(engine, 1);
    assert_int_equal(dwl_rule_engine_count(engine), 1);
    assert_string_equal(dwl_rule_engine_get(engine, 0)->app_id_pattern, "first");

    dwl_rule_engine_destroy(engine);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rule_engine_create),
        cmocka_unit_test(test_rule_engine_destroy_null),
        cmocka_unit_test(test_rule_engine_add),
        cmocka_unit_test(test_rule_engine_add_null_checks),
        cmocka_unit_test(test_rule_engine_add_multiple),
        cmocka_unit_test(test_rule_engine_get),
        cmocka_unit_test(test_rule_engine_get_out_of_bounds),
        cmocka_unit_test(test_rule_engine_remove),
        cmocka_unit_test(test_rule_engine_remove_invalid_index),
        cmocka_unit_test(test_rule_engine_clear),
        cmocka_unit_test(test_rule_engine_clear_null),
        cmocka_unit_test(test_rule_engine_count_null),
        cmocka_unit_test(test_rule_regex_app_id),
        cmocka_unit_test(test_rule_regex_title),
        cmocka_unit_test(test_rule_both_patterns),
        cmocka_unit_test(test_rule_pattern_ownership),
        cmocka_unit_test(test_rule_remove_first),
        cmocka_unit_test(test_rule_remove_last),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
