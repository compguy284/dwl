#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdlib.h>
#include <string.h>

#include "layout.h"

/* Helper to create layout params */
static DwlLayoutParams create_params(size_t client_count, int area_w, int area_h)
{
    DwlLayoutParams params = {
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
        params.clients = calloc(client_count, sizeof(DwlLayoutClient));
        for (size_t i = 0; i < client_count; i++) {
            params.clients[i].id = (unsigned int)i;
        }
    }

    return params;
}

static void free_params(DwlLayoutParams *params)
{
    free(params->clients);
    params->clients = NULL;
}

/* Tile layout tests */
static void test_tile_one_client(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(1, 1920, 1080);

    dwl_layout_tile.arrange(&params);

    /* Single client should take full width */
    assert_int_equal(params.clients[0].x, 0);
    assert_int_equal(params.clients[0].y, 0);
    assert_int_equal(params.clients[0].width, 1920);
    assert_int_equal(params.clients[0].height, 1080);

    free_params(&params);
}

static void test_tile_two_clients(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(2, 1920, 1080);
    params.master_factor = 0.5f;

    dwl_layout_tile.arrange(&params);

    /* First client (master) should be left half */
    assert_int_equal(params.clients[0].x, 0);
    assert_int_equal(params.clients[0].y, 0);
    assert_int_equal(params.clients[0].width, 960);
    assert_int_equal(params.clients[0].height, 1080);

    /* Second client (stack) should be right half */
    assert_int_equal(params.clients[1].x, 960);
    assert_int_equal(params.clients[1].y, 0);
    assert_int_equal(params.clients[1].width, 960);
    assert_int_equal(params.clients[1].height, 1080);

    free_params(&params);
}

static void test_tile_five_clients(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(5, 1920, 1080);
    params.master_factor = 0.5f;

    dwl_layout_tile.arrange(&params);

    /* Master should take left half */
    assert_int_equal(params.clients[0].x, 0);
    assert_int_equal(params.clients[0].width, 960);

    /* Stack clients should share right half vertically */
    for (int i = 1; i < 5; i++) {
        assert_int_equal(params.clients[i].x, 960);
        assert_true(params.clients[i].height > 0);
    }

    /* Stack clients should not overlap */
    for (int i = 2; i < 5; i++) {
        assert_true(params.clients[i].y >= params.clients[i-1].y + params.clients[i-1].height);
    }

    free_params(&params);
}

static void test_tile_with_gaps(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(2, 1920, 1080);
    params.master_factor = 0.5f;
    params.gap_outer_h = 10;
    params.gap_outer_v = 10;
    params.gap_inner_h = 5;
    params.gap_inner_v = 5;

    dwl_layout_tile.arrange(&params);

    /* Master should have outer gap */
    assert_int_equal(params.clients[0].x, 10);
    assert_int_equal(params.clients[0].y, 10);

    /* Stack should account for gaps */
    assert_true(params.clients[1].x > params.clients[0].x + params.clients[0].width);

    free_params(&params);
}

static void test_tile_focus_next_wrap_forward(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(3, 1920, 1080);

    int next = dwl_layout_tile.focus_next(&params, 2, 1);
    assert_int_equal(next, 0);

    free_params(&params);
}

static void test_tile_focus_next_wrap_backward(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(3, 1920, 1080);

    int next = dwl_layout_tile.focus_next(&params, 0, -1);
    assert_int_equal(next, 2);

    free_params(&params);
}

static void test_tile_focus_next_normal(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(5, 1920, 1080);

    int next = dwl_layout_tile.focus_next(&params, 2, 1);
    assert_int_equal(next, 3);

    next = dwl_layout_tile.focus_next(&params, 2, -1);
    assert_int_equal(next, 1);

    free_params(&params);
}

static void test_tile_focus_next_no_clients(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(0, 1920, 1080);

    int next = dwl_layout_tile.focus_next(&params, 0, 1);
    assert_int_equal(next, -1);

    free_params(&params);
}

/* Monocle layout tests */
static void test_monocle_all_same_size(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(3, 1920, 1080);

    dwl_layout_monocle.arrange(&params);

    /* All clients should be same size as area */
    for (size_t i = 0; i < params.client_count; i++) {
        assert_int_equal(params.clients[i].x, 0);
        assert_int_equal(params.clients[i].y, 0);
        assert_int_equal(params.clients[i].width, 1920);
        assert_int_equal(params.clients[i].height, 1080);
    }

    free_params(&params);
}

static void test_monocle_with_gaps(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(2, 1920, 1080);
    params.gap_outer_h = 20;
    params.gap_outer_v = 20;

    dwl_layout_monocle.arrange(&params);

    /* All clients should have same position with outer gaps */
    for (size_t i = 0; i < params.client_count; i++) {
        assert_int_equal(params.clients[i].x, 20);
        assert_int_equal(params.clients[i].y, 20);
        assert_int_equal(params.clients[i].width, 1880);
        assert_int_equal(params.clients[i].height, 1040);
    }

    free_params(&params);
}

static void test_monocle_focus_next(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(3, 1920, 1080);

    int next = dwl_layout_monocle.focus_next(&params, 0, 1);
    assert_int_equal(next, 1);

    next = dwl_layout_monocle.focus_next(&params, 2, 1);
    assert_int_equal(next, 0);

    next = dwl_layout_monocle.focus_next(&params, 0, -1);
    assert_int_equal(next, 2);

    free_params(&params);
}

/* Scroller layout tests */
static void test_scroller_horizontal_scroll(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(3, 1920, 1080);
    params.master_factor = 0.5f;
    params.focused_index = 1;

    dwl_layout_scroller.arrange(&params);

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

    DwlLayoutParams params = create_params(5, 1920, 1080);

    int next = dwl_layout_scroller.focus_next(&params, 2, 1);
    assert_int_equal(next, 3);

    next = dwl_layout_scroller.focus_next(&params, 4, 1);
    assert_int_equal(next, 0);

    next = dwl_layout_scroller.focus_next(&params, 0, -1);
    assert_int_equal(next, 4);

    free_params(&params);
}

/* Floating layout tests */
static void test_floating_does_not_modify_positions(void **state)
{
    (void)state;

    DwlLayoutParams params = create_params(2, 1920, 1080);

    /* Set some initial positions */
    params.clients[0].x = 100;
    params.clients[0].y = 100;
    params.clients[0].width = 400;
    params.clients[0].height = 300;

    params.clients[1].x = 500;
    params.clients[1].y = 200;
    params.clients[1].width = 600;
    params.clients[1].height = 400;

    dwl_layout_floating.arrange(&params);

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

    DwlLayoutParams params = create_params(3, 1920, 1080);

    int next = dwl_layout_floating.focus_next(&params, 1, 1);
    assert_int_equal(next, 2);

    next = dwl_layout_floating.focus_next(&params, 2, 1);
    assert_int_equal(next, 0);

    free_params(&params);
}

/* Layout registry tests */
static void test_registry_create_destroy(void **state)
{
    (void)state;

    DwlLayoutRegistry *reg = dwl_layout_registry_create();
    assert_non_null(reg);

    dwl_layout_registry_destroy(reg);
}

static void test_registry_register(void **state)
{
    (void)state;

    DwlLayoutRegistry *reg = dwl_layout_registry_create();
    assert_non_null(reg);

    DwlError err = dwl_layout_register(reg, &dwl_layout_tile);
    assert_int_equal(err, DWL_OK);
    assert_int_equal(dwl_layout_count(reg), 1);

    const DwlLayout *layout = dwl_layout_get(reg, "tile");
    assert_non_null(layout);
    assert_string_equal(layout->name, "tile");

    dwl_layout_registry_destroy(reg);
}

static void test_registry_register_duplicate(void **state)
{
    (void)state;

    DwlLayoutRegistry *reg = dwl_layout_registry_create();
    assert_non_null(reg);

    dwl_layout_register(reg, &dwl_layout_tile);
    DwlError err = dwl_layout_register(reg, &dwl_layout_tile);
    assert_int_equal(err, DWL_ERR_ALREADY_EXISTS);
    assert_int_equal(dwl_layout_count(reg), 1);

    dwl_layout_registry_destroy(reg);
}

static void test_registry_unregister(void **state)
{
    (void)state;

    DwlLayoutRegistry *reg = dwl_layout_registry_create();
    assert_non_null(reg);

    dwl_layout_register(reg, &dwl_layout_tile);
    assert_int_equal(dwl_layout_count(reg), 1);

    DwlError err = dwl_layout_unregister(reg, "tile");
    assert_int_equal(err, DWL_OK);
    assert_int_equal(dwl_layout_count(reg), 0);

    assert_null(dwl_layout_get(reg, "tile"));

    dwl_layout_registry_destroy(reg);
}

static void test_registry_unregister_not_found(void **state)
{
    (void)state;

    DwlLayoutRegistry *reg = dwl_layout_registry_create();
    assert_non_null(reg);

    DwlError err = dwl_layout_unregister(reg, "nonexistent");
    assert_int_equal(err, DWL_ERR_NOT_FOUND);

    dwl_layout_registry_destroy(reg);
}

static void test_registry_builtins(void **state)
{
    (void)state;

    DwlLayoutRegistry *reg = dwl_layout_registry_create();
    assert_non_null(reg);

    dwl_layout_register_builtins(reg);

    assert_int_equal(dwl_layout_count(reg), 4);
    assert_non_null(dwl_layout_get(reg, "tile"));
    assert_non_null(dwl_layout_get(reg, "monocle"));
    assert_non_null(dwl_layout_get(reg, "scroller"));
    assert_non_null(dwl_layout_get(reg, "floating"));

    dwl_layout_registry_destroy(reg);
}

static void test_registry_list(void **state)
{
    (void)state;

    DwlLayoutRegistry *reg = dwl_layout_registry_create();
    assert_non_null(reg);

    dwl_layout_register(reg, &dwl_layout_tile);
    dwl_layout_register(reg, &dwl_layout_monocle);

    size_t count = 0;
    const char **names = dwl_layout_list(reg, &count);
    assert_non_null(names);
    assert_int_equal(count, 2);

    free((void *)names);
    dwl_layout_registry_destroy(reg);
}

static void test_layout_null_params(void **state)
{
    (void)state;

    /* Should not crash with NULL params */
    dwl_layout_tile.arrange(NULL);
    dwl_layout_monocle.arrange(NULL);
    dwl_layout_scroller.arrange(NULL);
    dwl_layout_floating.arrange(NULL);

    assert_int_equal(dwl_layout_tile.focus_next(NULL, 0, 1), -1);
    assert_int_equal(dwl_layout_monocle.focus_next(NULL, 0, 1), -1);
    assert_int_equal(dwl_layout_scroller.focus_next(NULL, 0, 1), -1);
    assert_int_equal(dwl_layout_floating.focus_next(NULL, 0, 1), -1);
}

static void test_layout_symbols(void **state)
{
    (void)state;

    assert_string_equal(dwl_layout_tile.symbol, "[]=");
    assert_string_equal(dwl_layout_monocle.symbol, "[M]");
    assert_string_equal(dwl_layout_scroller.symbol, "[S]");
    assert_string_equal(dwl_layout_floating.symbol, "><>");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        /* Tile layout */
        cmocka_unit_test(test_tile_one_client),
        cmocka_unit_test(test_tile_two_clients),
        cmocka_unit_test(test_tile_five_clients),
        cmocka_unit_test(test_tile_with_gaps),
        cmocka_unit_test(test_tile_focus_next_wrap_forward),
        cmocka_unit_test(test_tile_focus_next_wrap_backward),
        cmocka_unit_test(test_tile_focus_next_normal),
        cmocka_unit_test(test_tile_focus_next_no_clients),

        /* Monocle layout */
        cmocka_unit_test(test_monocle_all_same_size),
        cmocka_unit_test(test_monocle_with_gaps),
        cmocka_unit_test(test_monocle_focus_next),

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
