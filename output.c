#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include "colors.h"
#include "layers.h"
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

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
}

static void render_menu(struct wio_output *output) {
	struct wio_server *server = output->server;
	struct wlr_renderer *renderer = server->renderer;

	size_t ntextures =
		sizeof(server->menu.inactive_textures) /
		sizeof(server->menu.inactive_textures[0]);
	int scale = output->wlr_output->scale;
	int border = 3 * scale, margin = 4 * scale;
	int text_height = 0, text_width = 0;
	for (size_t i = 0; i < ntextures; ++i) {
		int width, height;
		// Assumes inactive/active textures are the same size
		// (they probably are)
		wlr_texture_get_size(
				server->menu.inactive_textures[i], &width, &height);
		width /= scale, height /= scale;
		text_height += height + margin;
		if (width >= text_width) {
			text_width = width;
		}
	}
	text_width += border * 2 + margin;
	text_height += border * 2 - margin;

	double ox = server->menu.x, oy = server->menu.y;
	wlr_output_layout_output_coords(
			server->output_layout, output->wlr_output, &ox, &oy);

	struct wlr_box bg_box = { 0 };
	// Background
	bg_box.x = ox;
	bg_box.y = oy;
	bg_box.width = text_width;
	bg_box.height = text_height;
	scale_box(&bg_box, scale);
	wlr_render_rect(renderer, &bg_box, menu_unselected,
			output->wlr_output->transform_matrix);
	// Top
	bg_box.x = ox;
	bg_box.y = oy;
	bg_box.width = text_width;
	bg_box.height = border;
	scale_box(&bg_box, scale);
	wlr_render_rect(renderer, &bg_box, menu_border,
			output->wlr_output->transform_matrix);
	// Bottom
	bg_box.x = ox;
	bg_box.y = oy + text_height;
	bg_box.width = text_width + border;
	bg_box.height = border;
	scale_box(&bg_box, scale);
	wlr_render_rect(renderer, &bg_box, menu_border,
			output->wlr_output->transform_matrix);
	// Left
	bg_box.x = ox;
	bg_box.y = oy;
	bg_box.width = border;
	bg_box.height = text_height;
	scale_box(&bg_box, scale);
	wlr_render_rect(renderer, &bg_box, menu_border,
			output->wlr_output->transform_matrix);
	// Right
	bg_box.x = ox + text_width;
	bg_box.y = oy;
	bg_box.width = border;
	bg_box.height = text_height;
	scale_box(&bg_box, scale);
	wlr_render_rect(renderer, &bg_box, menu_border,
			output->wlr_output->transform_matrix);

	double cur_x = server->cursor->x, cur_y = server->cursor->y;
	wlr_output_layout_output_coords(server->output_layout,
			output->wlr_output, &cur_x, &cur_y);
	server->menu.selected = -1;
	ox += margin;
	oy += margin;
	for (size_t i = 0; i < ntextures; ++i) {
		int width, height;
		struct wlr_texture *texture = server->menu.inactive_textures[i];
		wlr_texture_get_size(texture, &width, &height);
		width /= scale, height /= scale;
		struct wlr_box box = { 0 };
		box.x = ox - scale /* fudge */;
		box.y = oy - scale /* fudge */;
		box.width = text_width - border;
		box.height = height + margin;
		if (wlr_box_contains_point(&box, cur_x, cur_y)) {
			server->menu.selected = i;
			texture = server->menu.active_textures[i];
			scale_box(&box, scale);
			wlr_render_rect(renderer, &box, menu_selected,
					output->wlr_output->transform_matrix);
		} else {
			wlr_texture_get_size(texture, &width, &height);
			width /= scale, height /= scale;
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
		struct wio_output *output, struct wio_view *view,
		int x, int y, int width, int height) {
	float color[4];
	if (!view || view->xdg_surface->toplevel->current.activated) {
		memcpy(color, active_border, sizeof(color));
	} else {
		memcpy(color, inactive_border, sizeof(color));
	}
	struct wlr_output *wlr_output = output->wlr_output;
	int scale = wlr_output->scale;
	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(
			output->server->output_layout, wlr_output, &ox, &oy);
	ox *= scale, oy *= scale;
	struct wlr_box borders;
	// Top
	borders.x = (x - window_border) * scale;
	borders.x += ox;
	borders.y = (y - window_border) * scale;
	borders.y += oy;
	borders.width = (width + window_border * 2) * scale;
	borders.height = window_border * scale;
	wlr_render_rect(renderer, &borders, color, wlr_output->transform_matrix);
	// Right
	borders.x = (x + width) * scale;
	borders.x += ox;
	borders.y = (y - window_border) * scale;
	borders.y += oy;
	borders.width = window_border * scale;
	borders.height = (height + window_border * 2) * scale;
	wlr_render_rect(renderer, &borders, color, wlr_output->transform_matrix);
	// Bottom
	borders.x = (x - window_border) * scale;
	borders.x += ox;
	borders.y = (y + height) * scale;
	borders.y += oy;
	borders.width = (width + window_border * 2) * scale;
	borders.height = window_border * scale;
	wlr_render_rect(renderer, &borders, color, wlr_output->transform_matrix);
	// Left
	borders.x = (x - window_border) * scale;
	borders.x += ox;
	borders.y = (y - window_border) * scale;
	borders.y += oy;
	borders.width = window_border * scale;
	borders.height = (height + window_border * 2) * scale;
	wlr_render_rect(renderer, &borders, color, wlr_output->transform_matrix);
}

struct render_data_layer {
	struct wlr_output *output;
	struct wlr_renderer *renderer;
	struct wio_view *view;
	struct timespec *when;
};

static void render_layer_surface(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct wio_layer_surface *layer_surface = data;
	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture == NULL) {
		return;
	}
	struct wlr_output *output = layer_surface->layer_surface->output;
	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(
			layer_surface->server->output_layout, output, &ox, &oy);
	ox += layer_surface->geo.x + sx, oy += layer_surface->geo.y + sy;
	float matrix[9];
	enum wl_output_transform transform =
		wlr_output_transform_invert(surface->current.transform);
	struct wlr_box box;
	memcpy(&box, &layer_surface->geo, sizeof(struct wlr_box));
	wlr_matrix_project_box(matrix, &box, transform, 0,
		output->transform_matrix);
	wlr_render_texture_with_matrix(layer_surface->server->renderer,
			texture, matrix, 1);
	// Hack because I'm too lazy to fish through a new rdata struct
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_surface_send_frame_done(surface, &now);
}

static void render_layer(
		struct wio_output *output, struct wl_list *layer_surfaces) {
	struct wio_layer_surface *layer_surface;
	wl_list_for_each(layer_surface, layer_surfaces, link) {
		struct wlr_layer_surface_v1 *wlr_layer_surface_v1 =
			layer_surface->layer_surface;
		wlr_surface_for_each_surface(wlr_layer_surface_v1->surface,
			render_layer_surface, layer_surface);
	}
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

	struct wlr_output *wlr_output = output->wlr_output;
	wlr_renderer_begin(renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(renderer, background);

	render_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
	render_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);

	struct wio_view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (!view->xdg_surface->mapped) {
			continue;
		}
		struct render_data rdata = {
			.output = wlr_output,
			.view = view,
			.renderer = renderer,
			.when = &now,
		};

		render_view_border(renderer, output, view, view->x, view->y,
				view->xdg_surface->surface->current.width,
				view->xdg_surface->surface->current.height);
		wlr_xdg_surface_for_each_surface(view->xdg_surface,
				render_surface, &rdata);
	}
	switch (server->input_state) {
	case INPUT_STATE_MOVE:;
		struct wio_view *view = server->interactive.view;
		render_view_border(renderer, output, view,
			server->cursor->x - server->interactive.sx,
			server->cursor->y - server->interactive.sy,
			view->xdg_surface->surface->current.width,
			view->xdg_surface->surface->current.height);
		break;
	case INPUT_STATE_NEW_END:
	case INPUT_STATE_RESIZE_END:
		render_view_border(renderer, output, NULL,
			server->interactive.sx, server->interactive.sy,
			server->cursor->x - server->interactive.sx,
			server->cursor->y - server->interactive.sy);
		break;
	default:
		break;
	}

	render_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);

	if (server->menu.x != -1 && server->menu.y != -1) {
		render_menu(output);
	}

	render_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);

	wlr_output_render_software_cursors(wlr_output, NULL);
	wlr_renderer_end(renderer);
	wlr_output_commit(wlr_output);
}

void server_new_output(struct wl_listener *listener, void *data) {
	struct wio_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	struct wio_output *output = calloc(1, sizeof(struct wio_output));
	output->wlr_output = wlr_output;
	output->server = server;
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&server->outputs, &output->link);
	wlr_output->data = output;

	wl_list_init(&output->layers[0]);
	wl_list_init(&output->layers[1]);
	wl_list_init(&output->layers[2]);
	wl_list_init(&output->layers[3]);

	struct wio_output_config *_config, *config = NULL;
	wl_list_for_each(_config, &server->output_configs, link) {
		if (strcmp(_config->name, wlr_output->name) == 0) {
			config = _config;
			break;
		}
	}

	if (config) {
		if (config->x == -1 && config->y == -1) {
			wlr_output_layout_add_auto(server->output_layout, wlr_output);
		} else {
			wlr_output_layout_add(server->output_layout, wlr_output,
					config->x, config->y);
		}
		bool modeset = false;
		if (config->width && config->height
				&& !wl_list_empty(&wlr_output->modes)) {
			struct wlr_output_mode *mode;
			wl_list_for_each(mode, &wlr_output->modes, link) {
				if (mode->width == config->width
						&& mode->height == config->height) {
					wlr_output_set_mode(wlr_output, mode);
					modeset = true;
				}
			}
		}
		if (!modeset) {
			struct wlr_output_mode *mode =
				wlr_output_preferred_mode(wlr_output);
			if (mode) {
				wlr_output_set_mode(wlr_output, mode);
			}
		}
		if (config->scale) {
			wlr_output_set_scale(wlr_output, config->scale);
		}
		if (config->transform) {
			wlr_output_set_transform(wlr_output, config->transform);
		}
	} else {
		struct wlr_output_mode *mode =
			wlr_output_preferred_mode(wlr_output);
		if (mode) {
			wlr_output_set_mode(wlr_output, mode);
		}
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	}

	wlr_output_create_global(wlr_output);
}
