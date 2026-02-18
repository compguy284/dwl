#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdlib.h>
#include <string.h>

#include "layout.h"

/* Helper to create layout params */
static SwlLayoutParams create_params(size_t client_count, int area_w, int area_h)
{
    SwlLayoutParams params = {
        .area_x = 0,
        .area_y = 0,
        .area_width = area_w,
        .area_height = area_h,
        .gap_inner_h = 0,
        .gap_inner_v = 0,
        .gap_outer_h = 0,
        .gap_outer_v = 0,
        .master_factor = 0.55f,
        .master_count = 1,
        .client_count = client_count,
        .focused_index = 0,
        .clients = NULL,
    };

    if (client_count > 0) {
        params.clients = calloc(client_count, sizeof(SwlLayoutClient));
        for (size_t i = 0; i < client_count; i++) {
            params.clients[i].id = (unsigned int)i;
        }
    }

    return params;
}

static void free_params(SwlLayoutParams *params)
{
    free(params->clients);
    params->clients = NULL;
}

/* Scroller layout tests */
static void test_scroller_horizontal_scroll(void **state)
{
    (void)state;

    SwlLayoutParams params = create_params(3, 1920, 1080);
    params.master_factor = 0.5f;
    params.focused_index = 1;

    swl_layout_scroller.arrange(&params);

    /* Focused window should be somewhat centered */
    /* All windows should have same size */
    int width = params.clients[0].width;
    for (size_t i = 1; i < params.client_count; i++) {
        assert_int_equal(params.clients[i].width, width);
    }

    /* Windows should be arranged horizontally */
    assert_true(params.clients[1].x > params.clients[0].x);
    assert_true(params.clients[2].x > params.clients[1].x);

    free_params(&params);
}

static void test_scroller_focus_next(void **state)
{
    (void)state;

    SwlLayoutParams params = create_params(5, 1920, 1080);

    int next = swl_layout_scroller.focus_next(&params, 2, 1);
    assert_int_equal(next, 3);

    next = swl_layout_scroller.focus_next(&params, 4, 1);
    assert_int_equal(next, 0);

    next = swl_layout_scroller.focus_next(&params, 0, -1);
    assert_int_equal(next, 4);

    free_params(&params);
}

/* Floating layout tests */
static void test_floating_does_not_modify_positions(void **state)
{
    (void)state;

    SwlLayoutParams params = create_params(2, 1920, 1080);

    /* Set some initial positions */
    params.clients[0].x = 100;
    params.clients[0].y = 100;
    params.clients[0].width = 400;
    params.clients[0].height = 300;

    params.clients[1].x = 500;
    params.clients[1].y = 200;
    params.clients[1].width = 600;
    params.clients[1].height = 400;

    swl_layout_floating.arrange(&params);

    /* Floating layout should not modify client positions */
    assert_int_equal(params.clients[0].x, 100);
    assert_int_equal(params.clients[0].y, 100);
    assert_int_equal(params.clients[0].width, 400);
    assert_int_equal(params.clients[0].height, 300);

    assert_int_equal(params.clients[1].x, 500);
    assert_int_equal(params.clients[1].y, 200);
    assert_int_equal(params.clients[1].width, 600);
    assert_int_equal(params.clients[1].height, 400);

    free_params(&params);
}

static void test_floating_focus_next(void **state)
{
    (void)state;

    SwlLayoutParams params = create_params(3, 1920, 1080);

    int next = swl_layout_floating.focus_next(&params, 1, 1);
    assert_int_equal(next, 2);

    next = swl_layout_floating.focus_next(&params, 2, 1);
    assert_int_equal(next, 0);

    free_params(&params);
}

/* Layout registry tests */
static void test_registry_create_destroy(void **state)
{
    (void)state;

    SwlLayoutRegistry *reg = swl_layout_registry_create();
    assert_non_null(reg);

    swl_layout_registry_destroy(reg);
}

static void test_registry_register(void **state)
{
    (void)state;

    SwlLayoutRegistry *reg = swl_layout_registry_create();
    assert_non_null(reg);

    SwlError err = swl_layout_register(reg, &swl_layout_scroller);
    assert_int_equal(err, SWL_OK);
    assert_int_equal(swl_layout_count(reg), 1);

    const SwlLayout *layout = swl_layout_get(reg, "scroller");
    assert_non_null(layout);
    assert_string_equal(layout->name, "scroller");

    swl_layout_registry_destroy(reg);
}

static void test_registry_register_duplicate(void **state)
{
    (void)state;

    SwlLayoutRegistry *reg = swl_layout_registry_create();
    assert_non_null(reg);

    swl_layout_register(reg, &swl_layout_scroller);
    SwlError err = swl_layout_register(reg, &swl_layout_scroller);
    assert_int_equal(err, SWL_ERR_ALREADY_EXISTS);
    assert_int_equal(swl_layout_count(reg), 1);

    swl_layout_registry_destroy(reg);
}

static void test_registry_unregister(void **state)
{
    (void)state;

    SwlLayoutRegistry *reg = swl_layout_registry_create();
    assert_non_null(reg);

    swl_layout_register(reg, &swl_layout_scroller);
    assert_int_equal(swl_layout_count(reg), 1);

    SwlError err = swl_layout_unregister(reg, "scroller");
    assert_int_equal(err, SWL_OK);
    assert_int_equal(swl_layout_count(reg), 0);

    assert_null(swl_layout_get(reg, "scroller"));

    swl_layout_registry_destroy(reg);
}

static void test_registry_unregister_not_found(void **state)
{
    (void)state;

    SwlLayoutRegistry *reg = swl_layout_registry_create();
    assert_non_null(reg);

    SwlError err = swl_layout_unregister(reg, "nonexistent");
    assert_int_equal(err, SWL_ERR_NOT_FOUND);

    swl_layout_registry_destroy(reg);
}

static void test_registry_builtins(void **state)
{
    (void)state;

    SwlLayoutRegistry *reg = swl_layout_registry_create();
    assert_non_null(reg);

    swl_layout_register_builtins(reg);

    assert_int_equal(swl_layout_count(reg), 2);
    assert_non_null(swl_layout_get(reg, "scroller"));
    assert_non_null(swl_layout_get(reg, "floating"));

    swl_layout_registry_destroy(reg);
}

static void test_registry_list(void **state)
{
    (void)state;

    SwlLayoutRegistry *reg = swl_layout_registry_create();
    assert_non_null(reg);

    swl_layout_register(reg, &swl_layout_scroller);
    swl_layout_register(reg, &swl_layout_floating);

    size_t count = 0;
    const char **names = swl_layout_list(reg, &count);
    assert_non_null(names);
    assert_int_equal(count, 2);

    free((void *)names);
    swl_layout_registry_destroy(reg);
}

static void test_layout_null_params(void **state)
{
    (void)state;

    /* Should not crash with NULL params */
    swl_layout_scroller.arrange(NULL);
    swl_layout_floating.arrange(NULL);

    assert_int_equal(swl_layout_scroller.focus_next(NULL, 0, 1), -1);
    assert_int_equal(swl_layout_floating.focus_next(NULL, 0, 1), -1);
}

static void test_layout_symbols(void **state)
{
    (void)state;

    assert_string_equal(swl_layout_scroller.symbol, "[S]");
    assert_string_equal(swl_layout_floating.symbol, "><>");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Scroller layout */
        cmocka_unit_test(test_scroller_horizontal_scroll),
        cmocka_unit_test(test_scroller_focus_next),

        /* Floating layout */
        cmocka_unit_test(test_floating_does_not_modify_positions),
        cmocka_unit_test(test_floating_focus_next),

        /* Registry */
        cmocka_unit_test(test_registry_create_destroy),
        cmocka_unit_test(test_registry_register),
        cmocka_unit_test(test_registry_register_duplicate),
        cmocka_unit_test(test_registry_unregister),
        cmocka_unit_test(test_registry_unregister_not_found),
        cmocka_unit_test(test_registry_builtins),
        cmocka_unit_test(test_registry_list),

        /* General */
        cmocka_unit_test(test_layout_null_params),
        cmocka_unit_test(test_layout_symbols),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
