#ifndef _WIO_LAYERS_H
#define _WIO_LAYERS_H
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wayland-server.h>

struct wio_server;

struct wio_layer_surface {
	struct wlr_layer_surface_v1 *layer_surface;
	struct wio_server *server;
	struct wl_list link;

	struct wl_listener destroy;
	struct wl_listener map;
	struct wl_listener surface_commit;
	struct wl_listener output_destroy;

	struct wlr_box geo;
};

void server_new_layer_surface(struct wl_listener *listener, void *data);

#endif
