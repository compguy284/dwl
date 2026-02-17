#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile sig_atomic_t should_quit = 0;

static void handle_signal(int sig)
{
    switch (sig) {
    case SIGCHLD:
        while (waitpid(-1, NULL, WNOHANG) > 0)
            ;
        break;
    case SIGINT:
    case SIGTERM:
        should_quit = 1;
        break;
    case SIGPIPE:
        break;
    }
}

void dwl_signal_init(void)
{
    struct sigaction sa = {
        .sa_handler = handle_signal,
        .sa_flags = SA_RESTART,
    };
    sigemptyset(&sa.sa_mask);

    sigaction(SIGCHLD, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL);
}

int dwl_signal_should_quit(void)
{
    return should_quit;
}

void dwl_signal_request_quit(void)
{
    should_quit = 1;
}
