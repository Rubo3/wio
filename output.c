#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/render/allocator.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/box.h>
#include <wlr/util/transform.h>

#include "colors.h"
#include "layers.h"
#include "server.h"
#include "view.h"

struct render_data {
	struct wlr_output *output;
	struct wlr_render_pass *render_pass;
	struct wio_view *view;
	struct timespec *when;
};

static int scale_length(int length, int offset, float scale) {
	return round((offset + length) * scale) - round(offset * scale);
}

void scale_box(struct wlr_box *box, float scale) {
	box->width = scale_length(box->width, box->x, scale);
	box->height = scale_length(box->height, box->y, scale);
	box->x = round(box->x * scale);
	box->y = round(box->y * scale);
}

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
		.x = ox,
		.y = oy,
		.width = surface->current.width,
		.height = surface->current.height,
	};
	scale_box(&box, output->scale);
	struct wlr_render_texture_options options = {
		.texture = texture,
		.dst_box = box,
		.transform = wlr_output_transform_invert(surface->current.transform),
	};
	wlr_render_pass_add_texture(rdata->render_pass, &options);
	wlr_surface_send_frame_done(surface, rdata->when);
}

static void render_menu(struct wio_output *output) {
	struct wio_server *server = output->server;
	struct wlr_render_pass *render_pass = server->render_pass;

	size_t ntextures = countof(server->menu.inactive_textures);
	int scale = output->wlr_output->scale;
	int border = 3 * scale, margin = 4 * scale;
	int text_height = 0, text_width = 0;
	for (size_t i = 0; i < ntextures; ++i) {
		int width, height;
		// Assumes inactive/active textures are the same size
		// (they probably are)
		width = server->menu.inactive_textures[i]->width;
		height = server->menu.inactive_textures[i]->height;
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
	struct wlr_render_rect_options options = { 0 };
	// Background
	bg_box.x = ox;
	bg_box.y = oy;
	bg_box.width = text_width;
	bg_box.height = text_height;
	scale_box(&bg_box, scale);
	options.box = bg_box;
	options.color = menu_unselected;
	wlr_render_pass_add_rect(render_pass, &options);
	// Top
	bg_box.x = ox;
	bg_box.y = oy;
	bg_box.width = text_width;
	bg_box.height = border;
	scale_box(&bg_box, scale);
	options.box = bg_box;
	options.color = menu_border;
	wlr_render_pass_add_rect(render_pass, &options);
	// Bottom
	bg_box.x = ox;
	bg_box.y = oy + text_height;
	bg_box.width = text_width + border;
	bg_box.height = border;
	scale_box(&bg_box, scale);
	options.box = bg_box;
	options.color = menu_border;
	wlr_render_pass_add_rect(render_pass, &options);
	// Left
	bg_box.x = ox;
	bg_box.y = oy;
	bg_box.width = border;
	bg_box.height = text_height;
	scale_box(&bg_box, scale);
	options.box = bg_box;
	options.color = menu_border;
	wlr_render_pass_add_rect(render_pass, &options);
	// Right
	bg_box.x = ox + text_width;
	bg_box.y = oy;
	bg_box.width = border;
	bg_box.height = text_height;
	scale_box(&bg_box, scale);
	options.box = bg_box;
	options.color = menu_border;
	wlr_render_pass_add_rect(render_pass, &options);

	double cur_x = server->cursor->x, cur_y = server->cursor->y;
	wlr_output_layout_output_coords(server->output_layout,
			output->wlr_output, &cur_x, &cur_y);
	server->menu.selected = -1;
	ox += margin;
	oy += margin;
	for (size_t i = 0; i < ntextures; ++i) {
		int width, height;
		struct wlr_texture *texture = server->menu.inactive_textures[i];
		width = texture->width;
		height = texture->height;
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
			struct wlr_render_rect_options options = {
				.box = box,
			    .color = menu_selected
			};
			wlr_render_pass_add_rect(render_pass, &options);
		} else {
			width = texture->width;
            height = texture->height;
			width /= scale, height /= scale;
		}
		box.x = (ox + (text_width / 2 - width / 2));
		box.y = oy;
		box.width = width;
		box.height = height;
		scale_box(&box, scale);
		struct wlr_render_texture_options options = {
			.texture = texture,
			.dst_box = box,
			.transform = WL_OUTPUT_TRANSFORM_NORMAL,
		};
		wlr_render_pass_add_texture(render_pass, &options);
		oy += height + margin;
	}

	server->menu.width = text_width;
	server->menu.height = text_height;
}

static void render_view_border(struct wlr_render_pass *render_pass,
							   struct wio_output *output, struct wio_view *view,
							   struct wlr_box box, int selection) {
	struct wlr_render_color color;
	if (selection)
		color = selection_box;
	else if (!view || view->xdg_toplevel->current.activated)
		color = active_border;
	else
		color = inactive_border;

	struct wlr_output *wlr_output = output->wlr_output;
	int scale = wlr_output->scale;
	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(output->server->output_layout, wlr_output, &ox, &oy);
	struct wlr_box borders = { 0 };
	struct wlr_render_rect_options options = { 0 };

	// Top
	borders.x = ox + (box.x - window_border);
	borders.y = oy + (box.y - window_border);
	borders.width = (box.width + window_border * 2) ;
	borders.height = window_border;
	scale_box(&borders, scale);
	options.box = borders;
	options.color = color;
	wlr_render_pass_add_rect(render_pass, &options);

	// Right
	borders.x = ox + (box.x + box.width);
	borders.y = oy + (box.y - window_border);
	borders.width = window_border;
	borders.height = (box.height + window_border * 2);
	scale_box(&borders, scale);
	options.box = borders;
	options.color = color;
	wlr_render_pass_add_rect(render_pass, &options);

	// Bottom
	borders.x = ox + (box.x - window_border);
	borders.y = oy + (box.y + box.height);
	borders.width = (box.width + window_border * 2);
	borders.height = window_border;
	scale_box(&borders, scale);
	options.box = borders;
	options.color = color;
	wlr_render_pass_add_rect(render_pass, &options);

	// Left
	borders.x = ox + (box.x - window_border);
	borders.y = oy + (box.y - window_border);
	borders.width = window_border;
	borders.height = (box.height + window_border * 2);
	scale_box(&borders, scale);
	options.box = borders;
	options.color = color;
	wlr_render_pass_add_rect(render_pass, &options);
}

static void render_layer_surface(struct wlr_surface *surface,
								 int sx, int sy, void *data) {
	struct wio_layer_surface *layer_surface = data;
	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture == NULL)
		return;

	struct wlr_output *output = layer_surface->layer_surface->output;
	double ox = 0, oy = 0;
	wlr_output_layout_output_coords(
			layer_surface->server->output_layout, output, &ox, &oy);
	ox += layer_surface->geo.x + sx, oy += layer_surface->geo.y + sy;
	struct wlr_render_texture_options options = {
		.texture = texture,
		.dst_box = layer_surface->geo,
		.transform = wlr_output_transform_invert(surface->current.transform),
	};
	wlr_render_pass_add_texture(layer_surface->server->render_pass, &options);
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
    struct wlr_box box = { 0 };

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	struct wlr_output *wlr_output = output->wlr_output;
	struct wlr_output_state *wlr_output_state = output->wlr_output_state;
	server->render_pass = wlr_output_begin_render_pass(wlr_output, wlr_output_state, NULL, NULL);
	if (!server->render_pass) {
		return;
	}
	
	struct wlr_box frame_box = {
		.x = 0,
		.y = 0,
		.width = output->wlr_output->width,
		.height = output->wlr_output->height
	};
	struct wlr_render_rect_options clear_options = {
		.box = frame_box,
		.color = background
	};
	wlr_render_pass_add_rect(server->render_pass, &clear_options);

	render_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND]);
	render_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM]);

	struct wio_view *view;
	wl_list_for_each_reverse(view, &server->views, link) {
		if (!view->xdg_toplevel->base->surface->mapped) {
			continue;
		}
		struct wlr_box box = {
			.x = view->x,
			.y = view->y,
			.width = view->xdg_toplevel->current.width,
			.height = view->xdg_toplevel->current.height,
		};
		render_view_border(server->render_pass, output, view, box, 0);
		struct render_data rdata = {
			.output = wlr_output,
			.view = view,
			.render_pass = server->render_pass,
			.when = &now,
		};
		wlr_xdg_surface_for_each_surface(view->xdg_toplevel->base,
				render_surface, &rdata);
	}
	view = server->interactive.view;
	switch (server->input_state) {
    case INPUT_STATE_BORDER_DRAG:
		box = wio_which_box(server);
		box = wio_canon_box(server, box);
		render_view_border(server->render_pass, output, NULL, box, 1);
		break;
	case INPUT_STATE_MOVE:
		struct wlr_box box = {
			.x = server->cursor->x - server->interactive.sx,
			.y = server->cursor->y - server->interactive.sy,
			.width = view->xdg_toplevel->current.width,
			.height = view->xdg_toplevel->current.height,
		};
		render_view_border(server->render_pass, output, view, box, 1);
		break;
	case INPUT_STATE_NEW_END:
	case INPUT_STATE_RESIZE_END:
		box = wio_which_box(server);
		if (box.width > 0 && box.height > 0) {
			struct wlr_render_rect_options options = {
				.box = box,
				.color = surface
			};
			wlr_render_pass_add_rect(server->render_pass, &options);
		}
		render_view_border(server->render_pass, output, NULL, box, 1);
		break;
	default:
		break;
	}

	render_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_TOP]);

	if (server->menu.x != -1 && server->menu.y != -1) {
		render_menu(output);
	}

	render_layer(output, &output->layers[ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY]);

	wlr_output_add_software_cursors_to_render_pass(wlr_output, server->render_pass, NULL);
	wlr_render_pass_submit(server->render_pass);
	wlr_output_commit_state(wlr_output, wlr_output_state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct wio_output *output = wlr_output->data;
	struct wio_server *server = output->server;

	wl_list_remove(&output->link);
	if (wl_list_empty(&server->outputs)) {
		wl_display_terminate(server->wl_display);
	}
}

void server_new_output(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct wio_output *output = calloc(1, sizeof(struct wio_output));
	output->wlr_output = wlr_output;
	output->wlr_output_state = calloc(1, sizeof(struct wlr_output_state));
	// TODO(rubo): also call wlr_output_state_finish(output->wlr_output_state);
	wlr_output_state_init(output->wlr_output_state);
	output->server = server;
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&server->outputs, &output->link);
	wlr_output->data = output;

	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

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
		if (config->x == -1 && config->y == -1)
			wlr_output_layout_add_auto(server->output_layout, wlr_output);
		else {
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
					wlr_output_state_set_mode(output->wlr_output_state, mode);
					modeset = true;
				}
			}
		}
		if (!modeset) {
			struct wlr_output_mode *mode =
				wlr_output_preferred_mode(wlr_output);
			if (mode)
				wlr_output_state_set_mode(output->wlr_output_state, mode);
		}
		if (config->scale)
			wlr_output_state_set_scale(output->wlr_output_state, config->scale);
		if (config->transform)
			wlr_output_state_set_transform(output->wlr_output_state, config->transform);
		wlr_output_state_set_enabled(output->wlr_output_state, true);
	} else {
		struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
		if (mode)
			wlr_output_state_set_mode(output->wlr_output_state, mode);
		wlr_output_state_set_enabled(output->wlr_output_state, true);
		wlr_output_layout_add_auto(server->output_layout, wlr_output);
	}

	wlr_output_commit_state(wlr_output, output->wlr_output_state);
	wlr_output_create_global(wlr_output, server->wl_display);
}
