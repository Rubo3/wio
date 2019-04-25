#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "server.h"
#include "view.h"

static void xdg_surface_map(struct wl_listener *listener, void *data) {
	struct wio_view *view = wl_container_of(listener, view, map);
	wio_view_focus(view, view->xdg_surface->surface);
}

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
	view->map.notify = xdg_surface_map;
	wl_signal_add(&xdg_surface->events.map, &view->map);

	wl_list_insert(&server->views, &view->link);
}

void wio_view_focus(struct wio_view *view, struct wlr_surface *surface) {
	if (view == NULL) {
		return;
	}
	struct wio_server *server = view->server;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
	if (prev_surface == surface) {
		return;
	}
	if (prev_surface) {
		struct wlr_xdg_surface *previous = wlr_xdg_surface_from_wlr_surface(
				seat->keyboard_state.focused_surface);
		wlr_xdg_toplevel_set_activated(previous, false);
	}
	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
	wlr_xdg_toplevel_set_activated(view->xdg_surface, true);
	wlr_seat_keyboard_notify_enter(seat, view->xdg_surface->surface,
			keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	/* bring to front */
	wl_list_remove(&view->link);
	wl_list_insert(&view->server->views, &view->link);
}

static bool view_at(struct wio_view *view,
		double lx, double ly, struct wlr_surface **surface,
		double *sx, double *sy) {
	double view_sx = lx - view->x;
	double view_sy = ly - view->y;

	double _sx, _sy;
	struct wlr_surface *_surface = NULL;
	_surface = wlr_xdg_surface_surface_at(
			view->xdg_surface, view_sx, view_sy, &_sx, &_sy);

	if (_surface != NULL) {
		*sx = _sx;
		*sy = _sy;
		*surface = _surface;
		return true;
	}

	return false;
}

struct wio_view *wio_view_at(struct wio_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct wio_view *view;
	wl_list_for_each(view, &server->views, link) {
		if (view_at(view, lx, ly, surface, sx, sy)) {
			return view;
		}
	}
	return NULL;
}
