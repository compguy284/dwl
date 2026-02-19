#include "input_internal.h"
#include "compositor.h"
#include "config.h"
#include "events.h"
#include <stdlib.h>
#include <unistd.h>
#include <wlr/types/wlr_switch.h>

void handle_switch_toggle(struct wl_listener *listener, void *data)
{
    SwlInput *input = wl_container_of(listener, input, switch_toggle);
    struct wlr_switch_toggle_event *event = data;

    if (event->switch_type != WLR_SWITCH_TYPE_LID)
        return;

    SwlEventBus *bus = swl_compositor_get_event_bus(input->comp);

    if (event->switch_state == WLR_SWITCH_STATE_ON) {
        input->lid_closed = true;

        SwlConfig *cfg = swl_compositor_get_config(input->comp);
        const char *cmd = swl_config_get_string(cfg, "lid.command", "");
        if (cmd && cmd[0] != '\0') {
            if (fork() == 0) {
                setsid();
                execl("/bin/sh", "/bin/sh", "-c", cmd, NULL);
                _exit(1);
            }
        }

        swl_event_bus_emit_simple(bus, SWL_EVENT_LID_CLOSE, NULL);
    } else {
        input->lid_closed = false;
        swl_event_bus_emit_simple(bus, SWL_EVENT_LID_OPEN, NULL);
    }
}

void swl_switch_setup(SwlInput *input, struct wlr_switch *sw)
{
    input->switch_toggle.notify = handle_switch_toggle;
    wl_signal_add(&sw->events.toggle, &input->switch_toggle);
}
