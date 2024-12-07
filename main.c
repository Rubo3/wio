#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <cairo/cairo.h>
#include <drm_fourcc.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/util/log.h>

#include "layers.h"
#include "server.h"
#include "view.h"

#define XDG_SHELL_VERSION 2
#define LAYER_SHELL_V1_VERSION 4

static void gen_menu_textures(struct wio_server *server) {
	struct wlr_renderer *renderer = server->renderer;
	// numbers pulled from ass
	cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 128, 128);
	cairo_t *cairo = cairo_create(surf);
	cairo_select_font_face(cairo, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cairo, 14);
	cairo_set_source_rgb(cairo, 0, 0, 0);

	char *text[] = {"New", "Resize", "Move", "Delete", "Hide"};
	for (size_t i = 0; i < countof(text); ++i) {
		cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cairo);
		cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
		cairo_text_extents_t extents;
		cairo_text_extents(cairo, text[i], &extents);
		cairo_move_to(cairo, 0, extents.height);
		cairo_show_text(cairo, text[i]);
		cairo_surface_flush(surf);
		unsigned char *data = cairo_image_surface_get_data(surf);
		server->menu.inactive_textures[i] = wlr_texture_from_pixels(renderer,
				DRM_FORMAT_ARGB8888,
				cairo_image_surface_get_stride(surf),
				extents.width + 2, extents.height + 2, data);
	}

	cairo_set_source_rgb(cairo, 1, 1, 1);
	for (size_t i = 0; i < countof(text); ++i) {
		cairo_set_operator(cairo, CAIRO_OPERATOR_CLEAR);
		cairo_paint(cairo);
		cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
		cairo_text_extents_t extents;
		cairo_text_extents(cairo, text[i], &extents);
		cairo_move_to(cairo, 0, extents.height);
		cairo_show_text(cairo, text[i]);
		cairo_surface_flush(surf);
		unsigned char *data = cairo_image_surface_get_data(surf);
		server->menu.active_textures[i] = wlr_texture_from_pixels(renderer,
				DRM_FORMAT_ARGB8888,
				cairo_image_surface_get_stride(surf),
				extents.width + 2, extents.height + 2, data);
	}

	cairo_destroy(cairo);
	cairo_surface_destroy(surf);
}

static enum wl_output_transform str_to_transform(const char *str) {
	if (strcmp(str, "normal") == 0 || strcmp(str, "0") == 0) {
		return WL_OUTPUT_TRANSFORM_NORMAL;
	} else if (strcmp(str, "90") == 0)
		return WL_OUTPUT_TRANSFORM_90;
	else if (strcmp(str, "180") == 0)
		return WL_OUTPUT_TRANSFORM_180;
	else if (strcmp(str, "270") == 0)
		return WL_OUTPUT_TRANSFORM_270;
	else if (strcmp(str, "flipped") == 0)
		return WL_OUTPUT_TRANSFORM_FLIPPED;
	else if (strcmp(str, "flipped-90") == 0)
		return WL_OUTPUT_TRANSFORM_FLIPPED_90;
	else if (strcmp(str, "flipped-180") == 0)
		return WL_OUTPUT_TRANSFORM_FLIPPED_180;
	else if (strcmp(str, "flipped-270") == 0)
		return WL_OUTPUT_TRANSFORM_FLIPPED_270;
	else {
		fprintf(stderr, "Invalid output transform %s\n", str);
		exit(1);
	}
}

void parse_args(int argc, char *argv[], struct wio_server *server) {
	int c;
	while ((c = getopt(argc, argv, "c:t:o:h")) != -1) {
		switch (c) {
		case 'c':
			server->cage = optarg;
			break;
		case 't':
			server->term = optarg;
			break;
		case 'o':;
			// name:x:y:width:height:scale:transform
			struct wio_output_config *config = calloc(1, sizeof(struct wio_output_config));
			wl_list_insert(&server->output_configs, &config->link);
			const char *tok = strtok(optarg, ":");
			assert(tok);
			config->name = strdup(tok);
			tok = strtok(NULL, ":");
			assert(tok);
			config->x = atoi(tok);
			tok = strtok(NULL, ":");
			assert(tok);
			config->y = atoi(tok);
			tok = strtok(NULL, ":");
			if (!tok)
				break;
			config->width = atoi(tok);
			tok = strtok(NULL, ":");
			assert(tok);
			config->height = atoi(tok);
			tok = strtok(NULL, ":");
			if (!tok)
				break;
			config->scale = atoi(tok);
			tok = strtok(NULL, ":");
			if (!tok)
				break;
			config->transform = str_to_transform(tok);
			break;
		case 'h':
			printf("Usage: %s [-t <term>] [-c <cage>] [-o <output config>...]\n", argv[0]);
			exit(0);
		default:
			fprintf(stderr, "Unrecognized option %c\n", c);
			exit(1);
		}
	}
}

int main(int argc, char *argv[]) {
	struct wio_server server = {0};
	server.cage = "cage -d";
	server.term = "alacritty";

	wlr_log_init(WLR_DEBUG, NULL);
	wl_list_init(&server.output_configs);

	parse_args(argc, argv, &server);

	server.wl_display = wl_display_create();
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display),
	                                        &server.session);
	if (!server.backend) {
		return 1;
	}
	server.renderer = wlr_renderer_autocreate(server.backend);
	server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);

	uint32_t compositor_version = 5;
	wlr_compositor_create(server.wl_display, compositor_version, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display);

	wlr_export_dmabuf_manager_v1_create(server.wl_display);
	wlr_screencopy_manager_v1_create(server.wl_display);
	wlr_data_control_manager_v1_create(server.wl_display);
	wlr_primary_selection_v1_device_manager_create(server.wl_display);

	wlr_gamma_control_manager_v1_create(server.wl_display);
	// wlr_gtk_primary_selection_device_manager_create(server.wl_display);

	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.output_layout = wlr_output_layout_create(server.wl_display);
	wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);

	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	wlr_xcursor_manager_load(server.cursor_mgr, 1);

	struct wio_output_config *config;
	wl_list_for_each(config, &server.output_configs, link) {
		if (config->scale > 1)
			wlr_xcursor_manager_load(server.cursor_mgr, config->scale);
	}

	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute, &server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	wl_list_init(&server.inputs);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);

	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
	wl_list_init(&server.keyboards);
	wl_list_init(&server.pointers);

	wl_list_init(&server.views);
	wl_list_init(&server.new_views);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, XDG_SHELL_VERSION);
	server.xdg_shell_new_toplevel.notify = server_xdg_shell_new_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.xdg_shell_new_toplevel);

	server.xdg_decoration_manager = wlr_xdg_decoration_manager_v1_create(server.wl_display);
	server.new_toplevel_decoration.notify = server_new_toplevel_decoration;
	wl_signal_add(&server.xdg_decoration_manager->events.new_toplevel_decoration, &server.new_toplevel_decoration);

	server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, LAYER_SHELL_V1_VERSION);
	server.new_layer_surface.notify = server_new_layer_surface;
	wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

	server.menu.x = server.menu.y = -1;
	gen_menu_textures(&server);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	if (!wlr_backend_start(server.backend)) {
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
	wl_display_run(server.wl_display);

	wl_display_destroy_clients(server.wl_display);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);
	wl_display_destroy(server.wl_display);
	return 0;
}
