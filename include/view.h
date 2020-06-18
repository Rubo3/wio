#ifndef _WIO_VIEW_H
#define _WIO_VIEW_H
#include <wlr/types/wlr_xdg_shell.h>
#include <wayland-server.h>

#define MINWIDTH 100
#define MINHEIGHT 100

struct wio_server;

struct wio_view {
	int x, y;
    int area;
	struct wlr_xdg_surface *xdg_surface;
	struct wio_server *server;
	struct wl_list link;
	struct wl_listener map;
	struct wl_listener destroy;
};

enum wio_view_area {
	VIEW_AREA_BORDER_TOP_LEFT = 0,
	VIEW_AREA_BORDER_TOP,
	VIEW_AREA_BORDER_TOP_RIGHT,
	VIEW_AREA_BORDER_LEFT,
	VIEW_AREA_SURFACE,
	VIEW_AREA_BORDER_RIGHT,
	VIEW_AREA_BORDER_BOTTOM_LEFT,
	VIEW_AREA_BORDER_BOTTOM,
    VIEW_AREA_BORDER_BOTTOM_RIGHT,
};

void server_new_xdg_surface(struct wl_listener *listener, void *data);
void wio_view_focus(struct wio_view *view, struct wlr_surface *surface);
struct wio_view *wio_view_at(struct wio_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy);
void wio_view_move(struct wio_view *view, int x, int y);
struct wlr_box wio_which_box(struct wio_server *server);
struct wlr_box wio_canon_box(struct wio_server *server, struct wlr_box box);

#endif
