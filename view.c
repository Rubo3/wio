#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_xdg_shell.h>
#include "xdg-shell-protocol.h"
#include "server.h"
#include "view.h"

static void xdg_surface_map(struct wl_listener *listener, void *data) {
	struct wio_view *view = wl_container_of(listener, view, map);
	struct wio_server *server = view->server;
	wio_view_focus(view, view->xdg_surface->surface);

	struct wlr_output *output = wlr_output_layout_output_at(
			server->output_layout, server->cursor->x, server->cursor->y);
	struct wlr_output_layout_output *layout = wlr_output_layout_get(
			server->output_layout, output);
	if (view->x == -1 || view->y == -1) {
		struct wlr_surface_state *current =
			&view->xdg_surface->surface->current;
		int owidth, oheight;
		wlr_output_effective_resolution(output, &owidth, &oheight);
		wio_view_move(view,
				layout->x + (owidth / 2 - current->width / 2),
				layout->y + (oheight / 2 - current->height / 2));
	} else {
		// Sends wl_surface_enter
		wio_view_move(view, view->x, view->y);
	}
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
	view->x = view->y = -1;

	view->destroy.notify = xdg_surface_destroy;
	wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
	view->map.notify = xdg_surface_map;
	wl_signal_add(&xdg_surface->events.map, &view->map);

	wlr_xdg_toplevel_set_tiled(view->xdg_surface,
		WLR_EDGE_LEFT | WLR_EDGE_RIGHT | WLR_EDGE_TOP | WLR_EDGE_BOTTOM);

	pid_t pid;
	uid_t uid;
	gid_t gid;
	struct wl_client *client = wl_resource_get_client(xdg_surface->resource);
	wl_client_get_credentials(client, &pid, &uid, &gid);
	struct wio_new_view *new_view;
	wl_list_for_each(new_view, &server->new_views, link) {
		if (new_view->pid != pid) {
			continue;
		}
		view->x = new_view->box.x;
		view->y = new_view->box.y;
		wlr_xdg_toplevel_set_size(xdg_surface,
				new_view->box.width, new_view->box.height);
		wl_list_remove(&new_view->link);
		free(new_view);
		break;
	}

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

static int portion(int x, int lo, int width) {
	x -= lo;
	if (x < 20) {
		return 0;
	}
	if (x > width-20) {
		return 2;
	}
	return 1;
}

static int which_corner(struct wlr_box *box, int x, int y) {
	int i, j;

	i = portion(x, box->x, box->width);
	j = portion(y, box->y, box->height);
	return 3*j+i;
}

struct wlr_box which_box(struct wio_server *server) {
	struct wlr_box box;
	int x1, x2, y1, y2, width, height;
	switch (server->interactive.view->area) {
	case VIEW_AREA_BORDER_TOP_LEFT:
		y1 = server->cursor->y;
		y2 = server->interactive.view->y + server->interactive.view->xdg_surface->surface->current.height;
		x1 = server->cursor->x;
		x2 = server->interactive.view->x + server->interactive.view->xdg_surface->surface->current.width;
		less_swap1(y2, y1);
		less_swap1(x2, x1);
		width = x2 - x1;
		if (width < MINWIDTH
			&& (x1 - server->interactive.view->x) < (server->interactive.view->xdg_surface->surface->current.width)) {
			x1 -= MINWIDTH - width;
		}
		height = y2 - y1;
		if (height < MINHEIGHT
			&& (y1 - server->interactive.view->y) < (server->interactive.view->xdg_surface->surface->current.height)) {
			y1 -= MINHEIGHT - height;
		}
		break;
	case VIEW_AREA_BORDER_TOP:
		y1 = server->cursor->y;
		y2 = server->interactive.view->y + server->interactive.view->xdg_surface->surface->current.height;
		x1 = server->interactive.view->x;
		less_swap1(y2, y1);
		width = server->interactive.view->xdg_surface->surface->current.width;
		height = y2 - y1;
		if (height < MINHEIGHT
			&& (y1 - server->interactive.view->y) < (server->interactive.view->xdg_surface->surface->current.height)) {
			y1 -= MINHEIGHT - height;
		}
		break;
	case VIEW_AREA_BORDER_TOP_RIGHT:
		y1 = server->cursor->y;
		y2 = server->interactive.view->y + server->interactive.view->xdg_surface->surface->current.height;
		x1 = server->interactive.view->x;
		x2 = server->cursor->x;
		less_swap1(y2, y1);
		less_swap2(x2, x1);
		width = x2 - x1;
		if (width < MINWIDTH && (x1 - server->interactive.view->x) < 0) {
			x1 -= MINWIDTH - width;
		}
		height = y2 - y1;
		if (height < MINHEIGHT
			&& (y1 - server->interactive.view->y) < (server->interactive.view->xdg_surface->surface->current.height)) {
			y1 -= MINHEIGHT - height;
		}
		break;
	case VIEW_AREA_BORDER_LEFT:
		x1 = server->cursor->x;
		x2 = server->interactive.view->x + server->interactive.view->xdg_surface->surface->current.width;
		y1 = server->interactive.view->y;
		less_swap1(x2, x1);
		width = x2 - x1;
		if (width < MINWIDTH
			&& (x1 - server->interactive.view->x) < (server->interactive.view->xdg_surface->surface->current.width)) {
			x1 -= MINWIDTH - width;
		}
		height = server->interactive.view->xdg_surface->surface->current.height;
		break;
	case VIEW_AREA_BORDER_RIGHT:
		x1 = server->interactive.view->x;
		x2 = server->cursor->x;
		y1 = server->interactive.view->y;
		less_swap2(x2, x1);
		width = x2 - x1;
		if (width < MINWIDTH && (x1 - server->interactive.view->x) < 0) {
			x1 -= MINWIDTH - width;
		}
		height = server->interactive.view->xdg_surface->surface->current.height;
		break;
	case VIEW_AREA_BORDER_BOTTOM_LEFT:
		x1 = server->cursor->x;
		x2 = server->interactive.view->x + server->interactive.view->xdg_surface->surface->current.width;
		y1 = server->interactive.view->y;
		y2 = server->cursor->y;
		less_swap1(x2, x1);
		less_swap2(y2, y1);
		width = x2 - x1;
		if (width < MINWIDTH
			&& (x1 - server->interactive.view->x) < (server->interactive.view->xdg_surface->surface->current.width)) {
			x1 -= MINWIDTH - width;
		}
		height = y2 - y1;
		if (height < MINHEIGHT && (y1 - server->interactive.view->y) < 0) {
			y1 -= MINHEIGHT - height;
		}
		break;
	case VIEW_AREA_BORDER_BOTTOM:
		x1 = server->interactive.view->x;
		y1 = server->interactive.view->y;
		y2 = server->cursor->y;
		less_swap2(y2, y1);
		width = server->interactive.view->xdg_surface->surface->current.width;
		height = y2 - y1;
		if (height < MINHEIGHT && (y1 - server->interactive.view->y) < 0) {
			y1 -= MINHEIGHT - height;
		}
		break;
	case VIEW_AREA_BORDER_BOTTOM_RIGHT:
		x1 = server->interactive.view->x;
		x2 = server->cursor->x;
		y1 = server->interactive.view->y;
		y2 = server->cursor->y;
		less_swap2(x2, x1);
		less_swap2(y2, y1);
		width = x2 - x1;
		if (width < MINWIDTH && (x1 - server->interactive.view->x) < 0) {
			x1 -= MINWIDTH - width;
		}
		height = y2 - y1;
		if (height < MINHEIGHT && (y1 - server->interactive.view->y) < 0) {
			y1 -= MINHEIGHT - height;
		}
		break;
	}
	if (width < MINWIDTH) {
		width = MINWIDTH;
	}
	if (height < MINHEIGHT) {
		height = MINHEIGHT;
	}
	box.x = x1;
	box.y = y1;
	box.width = width;
	box.height = height;
	return box;
}

struct wio_view *wio_view_at(struct wio_server *server, double lx, double ly,
		struct wlr_surface **surface, double *sx, double *sy) {
	struct wlr_box border_box = {
		.x = 0, .y = 0,
		.width = 0, .height = 0,
	};
	struct wio_view *view;
	wl_list_for_each(view, &server->views, link) {
		// Surface
		if (view_at(view, lx, ly, surface, sx, sy)) {
			view->area = VIEW_AREA_SURFACE;
			return view;
		}
		// Border
		border_box.x = view->x - window_border;
		border_box.y = view->y - window_border;
        border_box.width = view->xdg_surface->surface->current.width + window_border * 2;
		border_box.height = view->xdg_surface->surface->current.height + window_border * 2;
        
		if (wlr_box_contains_point(&border_box, lx, ly)) {
			view->area = which_corner(&border_box, lx, ly);
			*sx = lx - view->x;
			*sy = ly - view->y;
			return view;
		}
	}
	return NULL;
}

void wio_view_move(struct wio_view *view, int x, int y) {
	view->x = x;
	view->y = y;

	// Cheating as FUCK because I'm lazy
	struct wio_output *output;
	wl_list_for_each(output, &view->server->outputs, link) {
		wlr_surface_send_enter(view->xdg_surface->surface, output->wlr_output);
	}
}
