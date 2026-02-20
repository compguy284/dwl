#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_PATH_ENV "SWL_SOCKET"
#define DEFAULT_SOCKET_PATH "/tmp/swl.sock"
#define BUFFER_SIZE 8192

static void usage(const char *name)
{
    fprintf(stderr, "Usage: %s <command> [args...]\n", name);
    fprintf(stderr, "\nCommands:\n");
    fprintf(stderr, "  get-windows       List all windows as JSON\n");
    fprintf(stderr, "  get-monitors      List all monitors as JSON\n");
    fprintf(stderr, "  get-layouts       List available layouts as JSON\n");
    fprintf(stderr, "  focus <id>        Focus window by ID\n");
    fprintf(stderr, "  close <id>        Close window by ID\n");
    fprintf(stderr, "  layout <name>     Set layout\n");
    fprintf(stderr, "  reload-config     Reload configuration\n");
    fprintf(stderr, "  output-power <name> <on|off>  Set monitor power state\n");
    fprintf(stderr, "  subscribe [event-types...]  Stream events as NDJSON\n");
    fprintf(stderr, "  dispatch <action> [arg]    Invoke a keybinding action\n");
    fprintf(stderr, "  quit              Quit compositor\n");
    fprintf(stderr, "\nEnvironment:\n");
    fprintf(stderr, "  SWL_SOCKET        Socket path (default: %s)\n", DEFAULT_SOCKET_PATH);
}

static int connect_socket(const char *socket_path)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
    };
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    return sock;
}

static int subscribe_events(const char *socket_path, const char *cmd)
{
    int sock = connect_socket(socket_path);
    if (sock < 0) {
        fprintf(stderr, "Failed to connect to swl\n");
        return 1;
    }

    if (write(sock, cmd, strlen(cmd)) < 0) {
        perror("write");
        close(sock);
        return 1;
    }

    /* Read initial response (the {subscribed:true} ack) and print */
    char buf[BUFFER_SIZE];
    ssize_t n = read(sock, buf, sizeof(buf) - 1);
    if (n <= 0) {
        fprintf(stderr, "Failed to read subscription response\n");
        close(sock);
        return 1;
    }
    buf[n] = '\0';

    /* Check for error response (no keep_open) */
    if (strstr(buf, "unknown event type") || strstr(buf, "error")) {
        fprintf(stderr, "%s\n", buf);
        close(sock);
        return 1;
    }

    /* Blocking read loop — print events as they arrive */
    while ((n = read(sock, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fputs(buf, stdout);
        fflush(stdout);
    }

    close(sock);
    return 0;
}

static int send_command(const char *socket_path, const char *cmd, char *response, size_t response_size)
{
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct sockaddr_un addr = {
        .sun_family = AF_UNIX,
    };
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }

    if (write(sock, cmd, strlen(cmd)) < 0) {
        perror("write");
        close(sock);
        return -1;
    }

    ssize_t n = read(sock, response, response_size - 1);
    if (n < 0) {
        perror("read");
        close(sock);
        return -1;
    }
    response[n] = '\0';

    close(sock);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return 0;
    }

    const char *socket_path = getenv(SOCKET_PATH_ENV);
    if (!socket_path)
        socket_path = DEFAULT_SOCKET_PATH;

    char cmd[BUFFER_SIZE] = {0};
    int offset = 0;

    for (int i = 1; i < argc; i++) {
        int written = snprintf(cmd + offset, sizeof(cmd) - offset,
                               "%s%s", i > 1 ? " " : "", argv[i]);
        if (written < 0 || (size_t)written >= sizeof(cmd) - offset) {
            fprintf(stderr, "Command too long\n");
            return 1;
        }
        offset += written;
    }

    /* Use persistent mode for subscribe */
    if (strcmp(argv[1], "subscribe") == 0)
        return subscribe_events(socket_path, cmd);

    char response[BUFFER_SIZE];
    if (send_command(socket_path, cmd, response, sizeof(response)) < 0) {
        fprintf(stderr, "Failed to communicate with swl\n");
        return 1;
    }

    printf("%s\n", response);
    return 0;
}
