/*
 * dwlctl - CLI client for dwl IPC
 * See LICENSE file for copyright and license details.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static void
usage(void)
{
	fprintf(stderr,
		"Usage: dwlctl [options] <command> [args...]\n"
		"\n"
		"Commands:\n"
		"  get monitors|clients|focused|config|keybinds|layouts\n"
		"  action <name> [value...]\n"
		"  set <key> <value>\n"
		"  reload\n"
		"  version\n"
		"\n"
		"Options:\n"
		"  -s <path>   Socket path override\n"
		"  -h          Show this help\n"
	);
}

static char *
discover_socket(const char *override)
{
	static char path[256];
	const char *env_sock, *runtime_dir, *wayland_display;

	if (override)
		return (char *)override;

	env_sock = getenv("DWL_SOCK");
	if (env_sock && *env_sock)
		return (char *)env_sock;

	runtime_dir = getenv("XDG_RUNTIME_DIR");
	wayland_display = getenv("WAYLAND_DISPLAY");
	if (runtime_dir && wayland_display) {
		snprintf(path, sizeof(path), "%s/dwl-ipc.%s.sock",
			runtime_dir, wayland_display);
		return path;
	}

	return NULL;
}

/* Escape a string for JSON output */
static void
json_escape(char *out, size_t outsz, const char *s)
{
	size_t i = 0;
	out[i++] = '"';
	while (*s && i < outsz - 2) {
		if (*s == '"' || *s == '\\') {
			if (i + 2 >= outsz - 1) break;
			out[i++] = '\\';
			out[i++] = *s;
		} else {
			out[i++] = *s;
		}
		s++;
	}
	out[i++] = '"';
	out[i] = '\0';
}

static int
ipc_send_recv(const char *sock_path, const char *request, char **response)
{
	struct sockaddr_un addr;
	int fd;
	ssize_t n;
	size_t req_len, resp_len = 0, resp_cap = 4096;
	char *resp;

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (strlen(sock_path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "dwlctl: socket path too long\n");
		close(fd);
		return -1;
	}
	memcpy(addr.sun_path, sock_path, strlen(sock_path) + 1);

	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "dwlctl: cannot connect to %s: %s\n",
			sock_path, strerror(errno));
		close(fd);
		return -1;
	}

	req_len = strlen(request);
	{
		size_t written = 0;
		while (written < req_len) {
			n = write(fd, request + written, req_len - written);
			if (n <= 0) {
				perror("write");
				close(fd);
				return -1;
			}
			written += (size_t)n;
		}
	}

	resp = malloc(resp_cap);
	while (1) {
		if (resp_len + 1 >= resp_cap) {
			resp_cap *= 2;
			resp = realloc(resp, resp_cap);
		}
		n = read(fd, resp + resp_len, resp_cap - resp_len - 1);
		if (n <= 0)
			break;
		resp_len += (size_t)n;
	}
	resp[resp_len] = '\0';

	close(fd);
	*response = resp;
	return 0;
}

/* Check if response contains "success":true */
static int
check_success(const char *resp)
{
	return strstr(resp, "\"success\":true") != NULL ||
	       strstr(resp, "\"success\": true") != NULL;
}

int
main(int argc, char *argv[])
{
	const char *sock_override = NULL;
	char *sock_path;
	char request[8192];
	char *response = NULL;
	int opt, ret;

	while ((opt = getopt(argc, argv, "hs:")) != -1) {
		switch (opt) {
		case 's':
			sock_override = optarg;
			break;
		case 'h':
			usage();
			return 0;
		default:
			usage();
			return 1;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		usage();
		return 1;
	}

	sock_path = discover_socket(sock_override);
	if (!sock_path) {
		fprintf(stderr, "dwlctl: cannot determine socket path\n");
		return 1;
	}

	if (strcmp(argv[0], "get") == 0) {
		if (argc < 2) {
			fprintf(stderr, "dwlctl: get requires a target (monitors, clients, focused, config, keybinds, layouts)\n");
			return 1;
		}
		snprintf(request, sizeof(request),
			"{\"command\":\"get_%s\"}\n", argv[1]);
	} else if (strcmp(argv[0], "action") == 0) {
		if (argc < 2) {
			fprintf(stderr, "dwlctl: action requires a name\n");
			return 1;
		}
		if (argc == 2) {
			/* No value */
			snprintf(request, sizeof(request),
				"{\"command\":\"action\",\"args\":{\"name\":\"%s\"}}\n",
				argv[1]);
		} else if (argc == 3) {
			/* Single value - check if it's numeric */
			char *end;
			double dval = strtod(argv[2], &end);
			if (*end == '\0') {
				/* Numeric value */
				if (strchr(argv[2], '.')) {
					snprintf(request, sizeof(request),
						"{\"command\":\"action\",\"args\":{\"name\":\"%s\",\"value\":%s}}\n",
						argv[1], argv[2]);
				} else {
					snprintf(request, sizeof(request),
						"{\"command\":\"action\",\"args\":{\"name\":\"%s\",\"value\":%s}}\n",
						argv[1], argv[2]);
				}
				(void)dval;
			} else {
				/* String value */
				char escaped[1024];
				json_escape(escaped, sizeof(escaped), argv[2]);
				snprintf(request, sizeof(request),
					"{\"command\":\"action\",\"args\":{\"name\":\"%s\",\"value\":%s}}\n",
					argv[1], escaped);
			}
		} else {
			/* Multiple values - build array for spawn */
			int i;
			size_t pos = 0;
			pos += (size_t)snprintf(request + pos, sizeof(request) - pos,
				"{\"command\":\"action\",\"args\":{\"name\":\"%s\",\"value\":[",
				argv[1]);
			for (i = 2; i < argc + (int)optind; i++) {
				/* We already adjusted argc/argv so use the right values */
				break;
			}
			/* Re-do: argc and argv were shifted by optind */
			for (i = 2; i < argc; i++) {
				char escaped[1024];
				json_escape(escaped, sizeof(escaped), argv[i]);
				if (i > 2)
					pos += (size_t)snprintf(request + pos, sizeof(request) - pos, ",");
				pos += (size_t)snprintf(request + pos, sizeof(request) - pos, "%s", escaped);
			}
			pos += (size_t)snprintf(request + pos, sizeof(request) - pos, "]}}\n");
		}
	} else if (strcmp(argv[0], "set") == 0) {
		if (argc < 3) {
			fprintf(stderr, "dwlctl: set requires <key> <value>\n");
			return 1;
		}
		{
			char escaped_key[512], escaped_val[512];
			json_escape(escaped_key, sizeof(escaped_key), argv[1]);
			json_escape(escaped_val, sizeof(escaped_val), argv[2]);
			snprintf(request, sizeof(request),
				"{\"command\":\"config_set\",\"args\":{\"key\":%s,\"value\":%s}}\n",
				escaped_key, escaped_val);
		}
	} else if (strcmp(argv[0], "reload") == 0) {
		snprintf(request, sizeof(request), "{\"command\":\"config_reload\"}\n");
	} else if (strcmp(argv[0], "version") == 0) {
		snprintf(request, sizeof(request), "{\"command\":\"version\"}\n");
	} else {
		fprintf(stderr, "dwlctl: unknown command '%s'\n", argv[0]);
		usage();
		return 1;
	}

	ret = ipc_send_recv(sock_path, request, &response);
	if (ret < 0)
		return 1;

	if (response) {
		printf("%s", response);
		ret = check_success(response) ? 0 : 1;
		free(response);
	}

	return ret;
}
