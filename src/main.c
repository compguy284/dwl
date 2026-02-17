#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#include "compositor.h"
#include "error.h"

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s [options]\n", name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -s <cmd>    Startup command\n");
    fprintf(stderr, "  -c <file>   Config file path\n");
    fprintf(stderr, "  -d          Debug logging\n");
    fprintf(stderr, "  -v          Print version and exit\n");
    fprintf(stderr, "  -h          Show this help\n");
}

static void version(void)
{
    printf("dwl 1.0.0\n");
}

int main(int argc, char *argv[])
{
    DwlCompositorConfig cfg = {
        .config_path = NULL,
        .enable_xwayland = true,
        .startup_cmd = NULL,
        .log_level = 0,
    };

    int opt;
    while ((opt = getopt(argc, argv, "s:c:dvh")) != -1) {
        switch (opt) {
        case 's':
            cfg.startup_cmd = optarg;
            break;
        case 'c':
            cfg.config_path = optarg;
            break;
        case 'd':
            cfg.log_level = 1;
            break;
        case 'v':
            version();
            return 0;
        case 'h':
        default:
            usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    DwlCompositor *comp;
    DwlError err = dwl_compositor_create(&comp, &cfg);
    if (err != DWL_OK) {
        fprintf(stderr, "Failed to create compositor: %s\n", dwl_error_string(err));
        return 1;
    }

    err = dwl_compositor_run(comp);
    if (err != DWL_OK) {
        fprintf(stderr, "Compositor error: %s\n", dwl_error_string(err));
        dwl_compositor_destroy(comp);
        return 1;
    }

    dwl_compositor_destroy(comp);
    return 0;
}
