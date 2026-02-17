#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "config.h"

/* Test fixtures */
static int watch_count;
static const char *last_watch_key;

static void watch_handler(void *ctx, const char *key)
{
    (void)ctx;
    watch_count++;
    last_watch_key = key;
}

static void watch_handler_with_ctx(void *ctx, const char *key)
{
    int *counter = ctx;
    (*counter)++;
    last_watch_key = key;
}

static int setup(void **state)
{
    (void)state;
    watch_count = 0;
    last_watch_key = NULL;
    return 0;
}

/* Tests */
static void test_config_create(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);
    dwl_config_destroy(cfg);
}

static void test_config_destroy_null(void **state)
{
    (void)state;

    /* Should not crash */
    dwl_config_destroy(NULL);
}

static void test_config_set_get_int(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_set_int(cfg, "test.value", 42);
    assert_int_equal(err, DWL_OK);

    int val = dwl_config_get_int(cfg, "test.value", -1);
    assert_int_equal(val, 42);

    dwl_config_destroy(cfg);
}

static void test_config_get_int_default(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    int val = dwl_config_get_int(cfg, "nonexistent", 99);
    assert_int_equal(val, 99);

    dwl_config_destroy(cfg);
}

static void test_config_set_get_float(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_set_float(cfg, "test.ratio", 0.55f);
    assert_int_equal(err, DWL_OK);

    float val = dwl_config_get_float(cfg, "test.ratio", 0.0f);
    assert_float_equal(val, 0.55f, 0.001f);

    dwl_config_destroy(cfg);
}

static void test_config_get_float_default(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    float val = dwl_config_get_float(cfg, "nonexistent", 1.5f);
    assert_float_equal(val, 1.5f, 0.001f);

    dwl_config_destroy(cfg);
}

static void test_config_set_get_bool(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_set_bool(cfg, "test.enabled", true);
    assert_int_equal(err, DWL_OK);

    bool val = dwl_config_get_bool(cfg, "test.enabled", false);
    assert_true(val);

    err = dwl_config_set_bool(cfg, "test.disabled", false);
    assert_int_equal(err, DWL_OK);

    val = dwl_config_get_bool(cfg, "test.disabled", true);
    assert_false(val);

    dwl_config_destroy(cfg);
}

static void test_config_get_bool_default(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    bool val = dwl_config_get_bool(cfg, "nonexistent", true);
    assert_true(val);

    val = dwl_config_get_bool(cfg, "nonexistent", false);
    assert_false(val);

    dwl_config_destroy(cfg);
}

static void test_config_set_get_string(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_set_string(cfg, "test.name", "hello world");
    assert_int_equal(err, DWL_OK);

    const char *val = dwl_config_get_string(cfg, "test.name", NULL);
    assert_non_null(val);
    assert_string_equal(val, "hello world");

    dwl_config_destroy(cfg);
}

static void test_config_get_string_default(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    const char *val = dwl_config_get_string(cfg, "nonexistent", "default");
    assert_string_equal(val, "default");

    dwl_config_destroy(cfg);
}

static void test_config_set_get_color(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    float input[4] = {1.0f, 0.5f, 0.25f, 1.0f};
    DwlError err = dwl_config_set_color(cfg, "test.color", input);
    assert_int_equal(err, DWL_OK);

    float output[4] = {0};
    err = dwl_config_get_color(cfg, "test.color", output);
    assert_int_equal(err, DWL_OK);

    assert_float_equal(output[0], 1.0f, 0.001f);
    assert_float_equal(output[1], 0.5f, 0.001f);
    assert_float_equal(output[2], 0.25f, 0.001f);
    assert_float_equal(output[3], 1.0f, 0.001f);

    dwl_config_destroy(cfg);
}

static void test_config_get_color_not_found(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    float output[4] = {0};
    DwlError err = dwl_config_get_color(cfg, "nonexistent", output);
    assert_int_equal(err, DWL_ERR_NOT_FOUND);

    dwl_config_destroy(cfg);
}

static void test_config_has_key(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    assert_false(dwl_config_has_key(cfg, "test.key"));

    dwl_config_set_int(cfg, "test.key", 1);
    assert_true(dwl_config_has_key(cfg, "test.key"));

    dwl_config_destroy(cfg);
}

static void test_config_remove(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    dwl_config_set_int(cfg, "test.key", 42);
    assert_true(dwl_config_has_key(cfg, "test.key"));

    DwlError err = dwl_config_remove(cfg, "test.key");
    assert_int_equal(err, DWL_OK);

    assert_false(dwl_config_has_key(cfg, "test.key"));

    dwl_config_destroy(cfg);
}

static void test_config_remove_not_found(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_remove(cfg, "nonexistent");
    assert_int_equal(err, DWL_ERR_NOT_FOUND);

    dwl_config_destroy(cfg);
}

static void test_config_watch_notifications(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    int id = dwl_config_watch(cfg, "test", watch_handler, NULL);
    assert_true(id > 0);

    dwl_config_set_int(cfg, "test.value", 1);
    assert_int_equal(watch_count, 1);
    assert_string_equal(last_watch_key, "test.value");

    dwl_config_set_string(cfg, "test.name", "hello");
    assert_int_equal(watch_count, 2);
    assert_string_equal(last_watch_key, "test.name");

    /* Different prefix - should not trigger */
    dwl_config_set_int(cfg, "other.value", 2);
    assert_int_equal(watch_count, 2);

    dwl_config_destroy(cfg);
}

static void test_config_watch_null_prefix(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    int id = dwl_config_watch(cfg, NULL, watch_handler, NULL);
    assert_true(id > 0);

    /* NULL prefix should match all keys */
    dwl_config_set_int(cfg, "any.key", 1);
    assert_int_equal(watch_count, 1);

    dwl_config_set_int(cfg, "another.key", 2);
    assert_int_equal(watch_count, 2);

    dwl_config_destroy(cfg);
}

static void test_config_unwatch(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    int id = dwl_config_watch(cfg, "test", watch_handler, NULL);
    dwl_config_unwatch(cfg, id);

    dwl_config_set_int(cfg, "test.value", 1);
    assert_int_equal(watch_count, 0);

    dwl_config_destroy(cfg);
}

static void test_config_watch_with_context(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    int counter = 0;
    int id = dwl_config_watch(cfg, "test", watch_handler_with_ctx, &counter);
    assert_true(id > 0);

    dwl_config_set_int(cfg, "test.value", 1);
    assert_int_equal(counter, 1);

    dwl_config_set_int(cfg, "test.other", 2);
    assert_int_equal(counter, 2);

    dwl_config_destroy(cfg);
}

static void test_config_keys(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    dwl_config_set_int(cfg, "layout.gaps", 10);
    dwl_config_set_float(cfg, "layout.ratio", 0.5f);
    dwl_config_set_int(cfg, "input.rate", 60);

    size_t count = 0;
    const char **keys = dwl_config_keys(cfg, "layout", &count);
    assert_non_null(keys);
    assert_int_equal(count, 2);

    dwl_config_keys_free(keys, count);

    keys = dwl_config_keys(cfg, NULL, &count);
    assert_non_null(keys);
    assert_int_equal(count, 3);

    dwl_config_keys_free(keys, count);
    dwl_config_destroy(cfg);
}

static void test_config_load_file(void **state)
{
    (void)state;

    /* Create a temporary config file */
    char tmpfile[] = "/tmp/dwl_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert_true(fd >= 0);

    const char *content =
        "[general]\n"
        "gaps = 10\n"
        "border = 2\n"
        "smart_gaps = true\n"
        "terminal = \"foot\"\n"
        "\n"
        "[layout]\n"
        "master_ratio = 0.55\n";

    write(fd, content, strlen(content));
    close(fd);

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_OK);

    assert_int_equal(dwl_config_get_int(cfg, "general.gaps", -1), 10);
    assert_int_equal(dwl_config_get_int(cfg, "general.border", -1), 2);
    assert_true(dwl_config_get_bool(cfg, "general.smart_gaps", false));
    assert_string_equal(dwl_config_get_string(cfg, "general.terminal", ""), "foot");
    assert_float_equal(dwl_config_get_float(cfg, "layout.master_ratio", 0.0f), 0.55f, 0.001f);

    dwl_config_destroy(cfg);
    unlink(tmpfile);
}

static void test_config_load_file_not_found(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, "/nonexistent/path/config.toml");
    assert_int_equal(err, DWL_ERR_IO);

    dwl_config_destroy(cfg);
}

static void test_config_set_null_checks(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    assert_int_equal(dwl_config_set_int(NULL, "key", 1), DWL_ERR_INVALID_ARG);
    assert_int_equal(dwl_config_set_int(cfg, NULL, 1), DWL_ERR_INVALID_ARG);
    assert_int_equal(dwl_config_set_float(NULL, "key", 1.0f), DWL_ERR_INVALID_ARG);
    assert_int_equal(dwl_config_set_bool(NULL, "key", true), DWL_ERR_INVALID_ARG);
    assert_int_equal(dwl_config_set_string(NULL, "key", "val"), DWL_ERR_INVALID_ARG);

    dwl_config_destroy(cfg);
}

static void test_config_overwrite_value(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    dwl_config_set_int(cfg, "test", 1);
    assert_int_equal(dwl_config_get_int(cfg, "test", -1), 1);

    dwl_config_set_int(cfg, "test", 2);
    assert_int_equal(dwl_config_get_int(cfg, "test", -1), 2);

    dwl_config_destroy(cfg);
}

static void test_config_type_mismatch(void **state)
{
    (void)state;

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    dwl_config_set_int(cfg, "test", 42);

    /* Getting as different type should return default */
    assert_float_equal(dwl_config_get_float(cfg, "test", 1.5f), 1.5f, 0.001f);
    assert_true(dwl_config_get_bool(cfg, "test", true));
    assert_string_equal(dwl_config_get_string(cfg, "test", "default"), "default");

    dwl_config_destroy(cfg);
}

static void test_config_hex_colors(void **state)
{
    (void)state;

    char tmpfile[] = "/tmp/dwl_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert_true(fd >= 0);

    const char *content =
        "[appearance.colors]\n"
        "border = \"#445566\"\n"
        "focus = \"#005577ff\"\n"
        "urgent = \"#ff0000\"\n";

    write(fd, content, strlen(content));
    close(fd);

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_OK);

    float rgba[4] = {0};

    /* #445566 -> 0x44/255, 0x55/255, 0x66/255, 1.0 */
    err = dwl_config_get_color(cfg, "appearance.colors.border", rgba);
    assert_int_equal(err, DWL_OK);
    assert_float_equal(rgba[0], 0x44 / 255.0f, 0.002f);
    assert_float_equal(rgba[1], 0x55 / 255.0f, 0.002f);
    assert_float_equal(rgba[2], 0x66 / 255.0f, 0.002f);
    assert_float_equal(rgba[3], 1.0f, 0.002f);

    /* #005577ff -> 0x00, 0x55, 0x77, 0xff */
    err = dwl_config_get_color(cfg, "appearance.colors.focus", rgba);
    assert_int_equal(err, DWL_OK);
    assert_float_equal(rgba[0], 0.0f, 0.002f);
    assert_float_equal(rgba[1], 0x55 / 255.0f, 0.002f);
    assert_float_equal(rgba[2], 0x77 / 255.0f, 0.002f);
    assert_float_equal(rgba[3], 1.0f, 0.002f);

    dwl_config_destroy(cfg);
    unlink(tmpfile);
}

static void test_config_array_of_tables(void **state)
{
    (void)state;

    char tmpfile[] = "/tmp/dwl_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert_true(fd >= 0);

    const char *content =
        "[[rules]]\n"
        "app_id = \"firefox\"\n"
        "tags = 256\n"
        "floating = false\n"
        "\n"
        "[[rules]]\n"
        "app_id = \"mpv\"\n"
        "floating = true\n";

    write(fd, content, strlen(content));
    close(fd);

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_OK);

    /* rules.0.app_id = "firefox" */
    assert_string_equal(
        dwl_config_get_string(cfg, "rules.0.app_id", ""), "firefox");
    assert_int_equal(dwl_config_get_int(cfg, "rules.0.tags", -1), 256);
    assert_false(dwl_config_get_bool(cfg, "rules.0.floating", true));

    /* rules.1.app_id = "mpv" */
    assert_string_equal(
        dwl_config_get_string(cfg, "rules.1.app_id", ""), "mpv");
    assert_true(dwl_config_get_bool(cfg, "rules.1.floating", false));

    dwl_config_destroy(cfg);
    unlink(tmpfile);
}

static void test_config_keybinding_inline_table(void **state)
{
    (void)state;

    char tmpfile[] = "/tmp/dwl_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert_true(fd >= 0);

    const char *content =
        "[keybindings]\n"
        "\"mod+p\" = { action = \"spawn\", command = [\"wmenu-run\"] }\n"
        "\"mod+shift+Return\" = { action = \"spawn\", command = [\"foot\"] }\n"
        "\"mod+j\" = { action = \"focusstack\", arg = 1 }\n"
        "\"mod+h\" = { action = \"setmfact\", arg = -0.05 }\n"
        "\"mod+shift+q\" = { action = \"quit\" }\n";

    write(fd, content, strlen(content));
    close(fd);

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_OK);

    /* spawn with command array -> "spawn:wmenu-run" */
    assert_string_equal(
        dwl_config_get_string(cfg, "keybindings.mod+p", ""), "spawn:wmenu-run");
    assert_string_equal(
        dwl_config_get_string(cfg, "keybindings.mod+shift+Return", ""),
        "spawn:foot");

    /* int arg -> "focusstack:1" */
    assert_string_equal(
        dwl_config_get_string(cfg, "keybindings.mod+j", ""), "focusstack:1");

    /* float arg -> "setmfact:-0.05" */
    assert_string_equal(
        dwl_config_get_string(cfg, "keybindings.mod+h", ""), "setmfact:-0.05");

    /* no arg -> just action name */
    assert_string_equal(
        dwl_config_get_string(cfg, "keybindings.mod+shift+q", ""), "quit");

    dwl_config_destroy(cfg);
    unlink(tmpfile);
}

static void test_config_quoted_keys(void **state)
{
    (void)state;

    char tmpfile[] = "/tmp/dwl_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert_true(fd >= 0);

    const char *content =
        "[keybindings]\n"
        "\"mod+shift+1\" = \"tag:1\"\n"
        "\"mod+0\" = \"view-all\"\n";

    write(fd, content, strlen(content));
    close(fd);

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_OK);

    assert_string_equal(
        dwl_config_get_string(cfg, "keybindings.mod+shift+1", ""), "tag:1");
    assert_string_equal(
        dwl_config_get_string(cfg, "keybindings.mod+0", ""), "view-all");

    dwl_config_destroy(cfg);
    unlink(tmpfile);
}

static void test_config_parse_error(void **state)
{
    (void)state;

    char tmpfile[] = "/tmp/dwl_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert_true(fd >= 0);

    const char *content = "[invalid\nthis is not valid toml ===\n";
    write(fd, content, strlen(content));
    close(fd);

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_ERR_CONFIG);

    dwl_config_destroy(cfg);
    unlink(tmpfile);
}

static void test_config_monitors_array_of_tables(void **state)
{
    (void)state;

    char tmpfile[] = "/tmp/dwl_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert_true(fd >= 0);

    const char *content =
        "[[monitors]]\n"
        "name = \"eDP-1\"\n"
        "scale = 2.0\n"
        "layout = \"monocle\"\n"
        "\n"
        "[[monitors]]\n"
        "name = \"HDMI-A-1\"\n"
        "scale = 1.0\n"
        "x = 1920\n";

    write(fd, content, strlen(content));
    close(fd);

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_OK);

    /* monitors.eDP-1.scale = 2.0 */
    assert_float_equal(
        dwl_config_get_float(cfg, "monitors.eDP-1.scale", 0.0f), 2.0f, 0.001f);
    assert_string_equal(
        dwl_config_get_string(cfg, "monitors.eDP-1.layout", ""), "monocle");

    /* monitors.HDMI-A-1.scale = 1.0 */
    assert_float_equal(
        dwl_config_get_float(cfg, "monitors.HDMI-A-1.scale", 0.0f), 1.0f, 0.001f);
    assert_int_equal(
        dwl_config_get_int(cfg, "monitors.HDMI-A-1.x", -1), 1920);

    dwl_config_destroy(cfg);
    unlink(tmpfile);
}

static void test_config_reload_clears_old(void **state)
{
    (void)state;

    char tmpfile[] = "/tmp/dwl_test_config_XXXXXX";
    int fd = mkstemp(tmpfile);
    assert_true(fd >= 0);

    const char *content1 =
        "[general]\n"
        "gaps = 10\n"
        "old_key = \"should_disappear\"\n";

    write(fd, content1, strlen(content1));
    close(fd);

    DwlConfig *cfg = dwl_config_create();
    assert_non_null(cfg);

    DwlError err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_OK);
    assert_int_equal(dwl_config_get_int(cfg, "general.gaps", -1), 10);
    assert_string_equal(
        dwl_config_get_string(cfg, "general.old_key", ""), "should_disappear");

    /* Overwrite with new content that drops old_key */
    FILE *f = fopen(tmpfile, "w");
    assert_non_null(f);
    const char *content2 =
        "[general]\n"
        "gaps = 20\n";
    fwrite(content2, 1, strlen(content2), f);
    fclose(f);

    err = dwl_config_load_file(cfg, tmpfile);
    assert_int_equal(err, DWL_OK);
    assert_int_equal(dwl_config_get_int(cfg, "general.gaps", -1), 20);

    /* old_key should be gone after reload */
    assert_false(dwl_config_has_key(cfg, "general.old_key"));

    dwl_config_destroy(cfg);
    unlink(tmpfile);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_config_create, setup),
        cmocka_unit_test_setup(test_config_destroy_null, setup),
        cmocka_unit_test_setup(test_config_set_get_int, setup),
        cmocka_unit_test_setup(test_config_get_int_default, setup),
        cmocka_unit_test_setup(test_config_set_get_float, setup),
        cmocka_unit_test_setup(test_config_get_float_default, setup),
        cmocka_unit_test_setup(test_config_set_get_bool, setup),
        cmocka_unit_test_setup(test_config_get_bool_default, setup),
        cmocka_unit_test_setup(test_config_set_get_string, setup),
        cmocka_unit_test_setup(test_config_get_string_default, setup),
        cmocka_unit_test_setup(test_config_set_get_color, setup),
        cmocka_unit_test_setup(test_config_get_color_not_found, setup),
        cmocka_unit_test_setup(test_config_has_key, setup),
        cmocka_unit_test_setup(test_config_remove, setup),
        cmocka_unit_test_setup(test_config_remove_not_found, setup),
        cmocka_unit_test_setup(test_config_watch_notifications, setup),
        cmocka_unit_test_setup(test_config_watch_null_prefix, setup),
        cmocka_unit_test_setup(test_config_unwatch, setup),
        cmocka_unit_test_setup(test_config_watch_with_context, setup),
        cmocka_unit_test_setup(test_config_keys, setup),
        cmocka_unit_test_setup(test_config_load_file, setup),
        cmocka_unit_test_setup(test_config_load_file_not_found, setup),
        cmocka_unit_test_setup(test_config_set_null_checks, setup),
        cmocka_unit_test_setup(test_config_overwrite_value, setup),
        cmocka_unit_test_setup(test_config_type_mismatch, setup),
        cmocka_unit_test_setup(test_config_hex_colors, setup),
        cmocka_unit_test_setup(test_config_array_of_tables, setup),
        cmocka_unit_test_setup(test_config_keybinding_inline_table, setup),
        cmocka_unit_test_setup(test_config_quoted_keys, setup),
        cmocka_unit_test_setup(test_config_parse_error, setup),
        cmocka_unit_test_setup(test_config_monitors_array_of_tables, setup),
        cmocka_unit_test_setup(test_config_reload_clears_old, setup),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
