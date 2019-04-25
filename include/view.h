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
	struct wl_listener map;
	struct wl_listener destroy;
};

void server_new_xdg_surface(struct wl_listener *listener, void *data);

void wio_view_focus(struct wio_view *view, struct wlr_surface *surface);
struct wio_view *wio_view_at(struct wio_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);
void wio_view_move(struct wio_view *view, int x, int y);

#endif
