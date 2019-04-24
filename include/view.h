#ifndef _WIO_VIEW_H
#define _WIO_VIEW_H
#include <wlr/types/wlr_xdg_shell.h>
#include <wayland-server.h>

struct wio_server;

struct wio_view {
	int x, y;
	struct wlr_xdg_surface *xdg_surface;
	struct wio_server *server;
	struct wl_list link;
	struct wl_listener destroy;
};

void server_new_xdg_surface(struct wl_listener *listener, void *data);

#endif
