#ifndef _WIO_SERVER_H
#define _WIO_SERVER_H
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>

struct wio_server {
	struct wl_display *wl_display;

	struct wlr_backend *backend;
	struct wlr_cursor *cursor;
	struct wlr_output_layout *output_layout;
	struct wlr_renderer *renderer;
	struct wlr_seat *seat;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wlr_xdg_shell *xdg_shell;

	struct wl_list outputs;
	struct wl_list inputs;
	struct wl_list pointers;
	struct wl_list keyboards;
	struct wl_list views;

	struct wl_listener new_output;
	struct wl_listener new_input;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wl_listener new_xdg_surface;

	struct {
		int x, y;
		struct wlr_texture *active_textures[5];
		struct wlr_texture *inactive_textures[5];
	} menu;
};

struct wio_output {
	struct wl_list link;

	struct wio_server *server;
	struct wlr_output *wlr_output;

	struct wl_listener frame;
};

struct wio_keyboard {
	struct wl_list link;

	struct wio_server *server;
	struct wlr_input_device *device;

	struct wl_listener modifiers;
	struct wl_listener key;
};

void server_new_output(struct wl_listener *listener, void *data);
void server_new_input(struct wl_listener *listener, void *data);
void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);

#endif
