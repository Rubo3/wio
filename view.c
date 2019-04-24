#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "server.h"
#include "view.h"

static void xdg_surface_destroy(struct wl_listener *listener, void *data) {
	struct wio_view *view = wl_container_of(listener, view, destroy);
	wl_list_remove(&view->link);
	free(view);
}

void server_new_xdg_surface(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(
			listener, server, new_xdg_surface);
	struct wlr_xdg_surface *xdg_surface = data;
	if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
		return;
	}

	struct wio_view *view = calloc(1, sizeof(struct wio_view));
	view->server = server;
	view->xdg_surface = xdg_surface;

	view->destroy.notify = xdg_surface_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &view->destroy);

	wl_list_insert(&server->views, &view->link);
}
