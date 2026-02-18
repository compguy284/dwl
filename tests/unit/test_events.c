#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "events.h"

/* Test fixtures */
static int event_count;
static SwlEventType last_event_type;
static void *last_event_data;

static void test_handler(void *ctx, const SwlEvent *event)
{
    (void)ctx;
    event_count++;
    last_event_type = event->type;
    last_event_data = event->data;
}

static void test_handler_with_ctx(void *ctx, const SwlEvent *event)
{
    int *counter = ctx;
    (*counter)++;
    last_event_type = event->type;
}

static int setup(void **state)
{
    (void)state;
    event_count = 0;
    last_event_type = 0;
    last_event_data = NULL;
    return 0;
}

/* Tests */
static void test_event_bus_create(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);
    swl_event_bus_destroy(bus);
}

static void test_event_bus_destroy_null(void **state)
{
    (void)state;

    /* Should not crash */
    swl_event_bus_destroy(NULL);
}

static void test_event_bus_subscribe_returns_valid_id(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    int id = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler, NULL);
    assert_true(id > 0);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_subscribe_null_bus(void **state)
{
    (void)state;

    int id = swl_event_bus_subscribe(NULL, SWL_EVENT_CLIENT_CREATE, test_handler, NULL);
    assert_int_equal(id, -1);
}

static void test_event_bus_subscribe_null_handler(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    int id = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, NULL, NULL);
    assert_int_equal(id, -1);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_subscribe_multiple_ids_unique(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    int id1 = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler, NULL);
    int id2 = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_DESTROY, test_handler, NULL);
    int id3 = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_FOCUS, test_handler, NULL);

    assert_true(id1 > 0);
    assert_true(id2 > 0);
    assert_true(id3 > 0);
    assert_int_not_equal(id1, id2);
    assert_int_not_equal(id2, id3);
    assert_int_not_equal(id1, id3);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_unsubscribe(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    int id = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler, NULL);
    assert_true(id > 0);

    swl_event_bus_unsubscribe(bus, id);

    /* Emit event - handler should not be called */
    SwlEvent event = {
        .type = SWL_EVENT_CLIENT_CREATE,
        .data = NULL,
        .data_size = 0,
        .timestamp = 0,
    };
    swl_event_bus_emit(bus, &event);
    assert_int_equal(event_count, 0);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_unsubscribe_null_bus(void **state)
{
    (void)state;

    /* Should not crash */
    swl_event_bus_unsubscribe(NULL, 1);
}

static void test_event_bus_unsubscribe_invalid_id(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    /* Should not crash */
    swl_event_bus_unsubscribe(bus, 0);
    swl_event_bus_unsubscribe(bus, -1);
    swl_event_bus_unsubscribe(bus, 9999);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_emit_calls_handler(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler, NULL);

    int test_data = 42;
    SwlEvent event = {
        .type = SWL_EVENT_CLIENT_CREATE,
        .data = &test_data,
        .data_size = sizeof(test_data),
        .timestamp = 12345,
    };

    swl_event_bus_emit(bus, &event);

    assert_int_equal(event_count, 1);
    assert_int_equal(last_event_type, SWL_EVENT_CLIENT_CREATE);
    assert_ptr_equal(last_event_data, &test_data);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_emit_only_matching_type(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler, NULL);

    SwlEvent event = {
        .type = SWL_EVENT_CLIENT_DESTROY,  /* Different type */
        .data = NULL,
        .data_size = 0,
        .timestamp = 0,
    };

    swl_event_bus_emit(bus, &event);

    assert_int_equal(event_count, 0);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_emit_multiple_handlers_same_type(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    int counter1 = 0, counter2 = 0;
    swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler_with_ctx, &counter1);
    swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler_with_ctx, &counter2);

    SwlEvent event = {
        .type = SWL_EVENT_CLIENT_CREATE,
        .data = NULL,
        .data_size = 0,
        .timestamp = 0,
    };

    swl_event_bus_emit(bus, &event);

    assert_int_equal(counter1, 1);
    assert_int_equal(counter2, 1);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_emit_no_subscribers(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    SwlEvent event = {
        .type = SWL_EVENT_CLIENT_CREATE,
        .data = NULL,
        .data_size = 0,
        .timestamp = 0,
    };

    /* Should not crash */
    swl_event_bus_emit(bus, &event);
    assert_int_equal(event_count, 0);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_emit_null_bus(void **state)
{
    (void)state;

    SwlEvent event = {
        .type = SWL_EVENT_CLIENT_CREATE,
        .data = NULL,
        .data_size = 0,
        .timestamp = 0,
    };

    /* Should not crash */
    swl_event_bus_emit(NULL, &event);
}

static void test_event_bus_emit_null_event(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    /* Should not crash */
    swl_event_bus_emit(bus, NULL);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_emit_simple(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    swl_event_bus_subscribe(bus, SWL_EVENT_MONITOR_ADD, test_handler, NULL);

    int data = 123;
    swl_event_bus_emit_simple(bus, SWL_EVENT_MONITOR_ADD, &data);

    assert_int_equal(event_count, 1);
    assert_int_equal(last_event_type, SWL_EVENT_MONITOR_ADD);
    assert_ptr_equal(last_event_data, &data);

    swl_event_bus_destroy(bus);
}

static void test_event_bus_resubscribe_after_unsubscribe(void **state)
{
    (void)state;

    SwlEventBus *bus = swl_event_bus_create();
    assert_non_null(bus);

    int id1 = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler, NULL);
    swl_event_bus_unsubscribe(bus, id1);

    int id2 = swl_event_bus_subscribe(bus, SWL_EVENT_CLIENT_CREATE, test_handler, NULL);
    assert_true(id2 > 0);

    SwlEvent event = {
        .type = SWL_EVENT_CLIENT_CREATE,
        .data = NULL,
        .data_size = 0,
        .timestamp = 0,
    };
    swl_event_bus_emit(bus, &event);

    assert_int_equal(event_count, 1);

    swl_event_bus_destroy(bus);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_event_bus_create, setup),
        cmocka_unit_test_setup(test_event_bus_destroy_null, setup),
        cmocka_unit_test_setup(test_event_bus_subscribe_returns_valid_id, setup),
        cmocka_unit_test_setup(test_event_bus_subscribe_null_bus, setup),
        cmocka_unit_test_setup(test_event_bus_subscribe_null_handler, setup),
        cmocka_unit_test_setup(test_event_bus_subscribe_multiple_ids_unique, setup),
        cmocka_unit_test_setup(test_event_bus_unsubscribe, setup),
        cmocka_unit_test_setup(test_event_bus_unsubscribe_null_bus, setup),
        cmocka_unit_test_setup(test_event_bus_unsubscribe_invalid_id, setup),
        cmocka_unit_test_setup(test_event_bus_emit_calls_handler, setup),
        cmocka_unit_test_setup(test_event_bus_emit_only_matching_type, setup),
        cmocka_unit_test_setup(test_event_bus_emit_multiple_handlers_same_type, setup),
        cmocka_unit_test_setup(test_event_bus_emit_no_subscribers, setup),
        cmocka_unit_test_setup(test_event_bus_emit_null_bus, setup),
        cmocka_unit_test_setup(test_event_bus_emit_null_event, setup),
        cmocka_unit_test_setup(test_event_bus_emit_simple, setup),
        cmocka_unit_test_setup(test_event_bus_resubscribe_after_unsubscribe, setup),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
