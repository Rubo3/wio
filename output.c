#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include "colors.h"
#include "server.h"

static void output_frame(struct wl_listener *listener, void *data) {
	struct wio_output *output = wl_container_of(listener, output, frame);
	struct wlr_renderer *renderer = output->server->renderer;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (!wlr_output_make_current(output->wlr_output, NULL)) {
		return;
	}

	int width, height;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	wlr_renderer_begin(renderer, width, height);

	wlr_renderer_clear(renderer, background);
	// TODO: other stuff
	wlr_renderer_end(renderer);
	wlr_output_swap_buffers(output->wlr_output, NULL, NULL);
}

void server_new_output(struct wl_listener *listener, void *data) {
	struct wio_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	if (!wl_list_empty(&wlr_output->modes)) {
		struct wlr_output_mode *mode =
			wl_container_of(wlr_output->modes.prev, mode, link);
		wlr_output_set_mode(wlr_output, mode);
	}

	struct wio_output *output = calloc(1, sizeof(struct wio_output));
	output->wlr_output = wlr_output;
	output->server = server;
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&server->outputs, &output->link);

	wlr_output_layout_add_auto(server->output_layout, wlr_output);
	wlr_output_create_global(wlr_output);
}
