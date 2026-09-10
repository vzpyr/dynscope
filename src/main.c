#include <signal.h>
#include <stdio.h>
#include <string.h>

#include <wayland-server-core.h>

#include "child.h"
#include "dynscope.h"
#include "host.h"
#include "server.h"

static int handle_signal(int signal_number, void *data) {
	(void)signal_number;
	struct dynscope *ds = data;
	dynscope_close(ds);
	return 1;
}

void dynscope_close(struct dynscope *ds) {
	if (ds->closing)
		return;
	ds->closing = true;
	child_terminate(ds);
}

int main(int argc, char **argv) {
	if (argc < 3 || strcmp(argv[1], "--") != 0) {
		fprintf(stderr, "usage: dynscope -- COMMAND [ARG...]\n");
		return 64;
	}

	signal(SIGPIPE, SIG_IGN);

	struct dynscope ds = {
		.exit_code = 1,
		.running = true,
	};

	if (server_init(&ds) < 0)
		return 1;

	ds.xwayland_display = server_display_name(&ds);

	if (host_open(&ds) < 0) {
		server_finish(&ds);
		return 1;
	}

	wl_event_loop_add_signal(ds.loop, SIGINT, handle_signal, &ds);
	wl_event_loop_add_signal(ds.loop, SIGTERM, handle_signal, &ds);

	if (child_spawn(&ds, &argv[2]) < 0) {
		fprintf(stderr, "dynscope: failed to spawn child process\n");
		host_close(&ds);
		server_finish(&ds);
		return 1;
	}

	while (ds.running) {
		wl_event_loop_dispatch(ds.loop, -1);
		wl_display_flush_clients(ds.server->display);
		host_flush(&ds);
	}

	host_close(&ds);
	server_finish(&ds);
	return ds.exit_code;
}
