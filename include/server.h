#ifndef _WIO_SERVER_H
#define _WIO_SERVER_H
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_output.h>

struct wio_server {
	struct wl_display *wl_display;

	struct wlr_backend *backend;
	struct wlr_renderer *renderer;

	struct wl_list outputs;

	struct wl_listener new_output;
	struct wl_listener new_input;
};

struct wio_output {
	struct wl_list link;
	struct wio_server *server;
	struct wlr_output *wlr_output;

	struct wl_listener frame;
};

void server_new_output(struct wl_listener *listener, void *data);

#endif
