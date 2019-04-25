#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include "colors.h"
#include "server.h"
#include "view.h"

struct render_data {
	struct wlr_output *output;
	struct wlr_renderer *renderer;
	struct wio_view *view;
	struct timespec *when;
};

static void render_surface(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct render_data *rdata = data;
	struct wio_view *view = rdata->view;
	struct wlr_output *output = rdata->output;
	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture == NULL) {
		return;
	}
	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(
			view->server->output_layout, output, &ox, &oy);
	ox += view->x + sx, oy += view->y + sy;
	struct wlr_box box = {
		.x = ox * output->scale,
		.y = oy * output->scale,
		.width = surface->current.width * output->scale,
		.height = surface->current.height * output->scale,
	};
	float matrix[9];
	enum wl_output_transform transform =
		wlr_output_transform_invert(surface->current.transform);
	wlr_matrix_project_box(matrix, &box, transform, 0,
		output->transform_matrix);
	wlr_render_texture_with_matrix(rdata->renderer, texture, matrix, 1);
	wlr_surface_send_frame_done(surface, rdata->when);
}

static void render_menu(struct wio_output *output) {
	struct wio_server *server = output->server;
	struct wlr_renderer *renderer = server->renderer;

	size_t ntextures =
		sizeof(server->menu.inactive_textures) /
		sizeof(server->menu.inactive_textures[0]);
	const int border = 3, margin = 4;
	int text_height = 0, text_width = 0;
	for (size_t i = 0; i < ntextures; ++i) {
		int width, height;
		// Assumes inactive/active textures are the same size
		// (they probably are)
		wlr_texture_get_size(
				server->menu.inactive_textures[i], &width, &height);
		text_height += height + margin;
		if (width >= text_width) {
			text_width = width;
		}
	}
	text_width += border * 2 + margin;
	text_height += border * 2 - margin;

	double ox = 0, oy = 0;
	//wlr_output_layout_output_coords(
	//		view->server->output_layout, output->wlr_output, &ox, &oy);
	ox += server->menu.x, oy += server->menu.y;
	int scale = output->wlr_output->scale;

	struct wlr_box bg_box = {
		.x = ox * scale,
		.y = oy * scale,
		.width = text_width * scale,
		.height = text_height * scale,
	};
	wlr_render_rect(renderer, &bg_box, menu_unselected,
			output->wlr_output->transform_matrix);
	bg_box.height = border;
	wlr_render_rect(renderer, &bg_box, menu_border,
			output->wlr_output->transform_matrix);
	bg_box.width += border;
	bg_box.y = (oy + text_height) * scale;
	wlr_render_rect(renderer, &bg_box, menu_border,
			output->wlr_output->transform_matrix);
	bg_box.y = oy * scale;
	bg_box.height = text_height * scale;
	bg_box.width = border;
	wlr_render_rect(renderer, &bg_box, menu_border,
			output->wlr_output->transform_matrix);
	bg_box.x = (ox + text_width) * scale;
	wlr_render_rect(renderer, &bg_box, menu_border,
			output->wlr_output->transform_matrix);

	ox += margin;
	oy += margin;
	for (size_t i = 0; i < ntextures; ++i) {
		int width, height;
		struct wlr_texture *texture = server->menu.inactive_textures[i];
		wlr_texture_get_size(texture, &width, &height);
		struct wlr_box box = { 0 };
		box.x = ox - 1 * scale;
		box.y = oy - 1 * scale;
		box.width = (text_width - border) * scale;
		box.height = (height + margin) * scale;
		if (wlr_box_contains_point(
					&box, server->cursor->x, server->cursor->y)) {
			server->menu.selected = i;
			texture = server->menu.active_textures[i];
			wlr_render_rect(renderer, &box, menu_selected,
					output->wlr_output->transform_matrix);
		} else {
			wlr_texture_get_size(texture, &width, &height);
		}
		box.x = (ox + (text_width / 2 - width / 2)) * scale;
		box.y = oy * scale;
		box.width = width * scale;
		box.height = height * scale;
		float matrix[9];
		wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
			output->wlr_output->transform_matrix);
		wlr_render_texture_with_matrix(renderer, texture, matrix, 1);
		oy += height + margin;
	}

	server->menu.width = text_width;
	server->menu.height = text_height;
}

static void render_view_border(struct wlr_renderer *renderer,
		struct wio_output *output, struct wio_view *view, int x, int y) {
	const int border = 5;
	float color[4];
	if (view->xdg_surface->toplevel->current.activated) {
		memcpy(color, active_border, sizeof(color));
	} else {
		memcpy(color, inactive_border, sizeof(color));
	}
	struct wlr_box borders;
	// Top
	borders.x = x - border;
	borders.y = y - border;
	borders.width = view->xdg_surface->surface->current.width + border * 2;
	borders.height = border;
	wlr_render_rect(renderer, &borders, color,
			output->wlr_output->transform_matrix);
	// Right
	borders.x = x + view->xdg_surface->surface->current.width;
	borders.y = y - border;
	borders.width = border;
	borders.height = view->xdg_surface->surface->current.height + border * 2;
	wlr_render_rect(renderer, &borders, color,
			output->wlr_output->transform_matrix);
	// Bottom
	borders.x = x - border;
	borders.y = y + view->xdg_surface->surface->current.height;
	borders.width = view->xdg_surface->surface->current.width + border * 2;
	borders.height = border;
	wlr_render_rect(renderer, &borders, color,
			output->wlr_output->transform_matrix);
	// Left
	borders.x = x - border;
	borders.y = y - border;
	borders.width = border;
	borders.height = view->xdg_surface->surface->current.height + border * 2;
	wlr_render_rect(renderer, &borders, color,
			output->wlr_output->transform_matrix);
}

static void output_frame(struct wl_listener *listener, void *data) {
	struct wio_output *output = wl_container_of(listener, output, frame);
	struct wio_server *server = output->server;
	struct wlr_renderer *renderer = server->renderer;

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (!wlr_output_attach_render(output->wlr_output, NULL)) {
		return;
	}

	int width, height;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);
	wlr_renderer_begin(renderer, width, height);

	wlr_renderer_clear(renderer, background);

	struct wio_view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (!view->xdg_surface->mapped) {
			continue;
		}
		struct render_data rdata = {
			.output = output->wlr_output,
			.view = view,
			.renderer = renderer,
			.when = &now,
		};

		render_view_border(renderer, output, view, view->x, view->y);
		wlr_xdg_surface_for_each_surface(view->xdg_surface,
				render_surface, &rdata);
		if (server->interactive.view == view) {
			render_view_border(renderer, output, view,
				server->cursor->x - server->interactive.sx,
				server->cursor->y - server->interactive.sy);
		}
	}

	if (server->menu.x != -1 && server->menu.y != -1) {
		render_menu(output);
	}

	wlr_output_render_software_cursors(output->wlr_output, NULL);
	wlr_renderer_end(renderer);
	wlr_output_commit(output->wlr_output);
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
