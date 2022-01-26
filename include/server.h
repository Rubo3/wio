#ifndef _WIO_SERVER_H
#define _WIO_SERVER_H
#include <signal.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/box.h>

static const int window_border = 5;

enum wio_input_state {
	INPUT_STATE_NONE = 0,
	INPUT_STATE_MENU,
	INPUT_STATE_NEW_START,
	INPUT_STATE_NEW_END,
	INPUT_STATE_MOVE_SELECT,
	INPUT_STATE_MOVE,
	INPUT_STATE_RESIZE_SELECT,
	INPUT_STATE_RESIZE_START,
	INPUT_STATE_RESIZE_END,
	INPUT_STATE_BORDER_DRAG,
	INPUT_STATE_DELETE_SELECT,
	INPUT_STATE_HIDE_SELECT,
};

struct wio_server {
	struct wl_display *wl_display;

	const char *cage, *term;

	struct wlr_allocator *allocator;
	struct wlr_backend *backend;
	struct wlr_cursor *cursor;
	struct wlr_output_layout *output_layout;
	struct wlr_renderer *renderer;
	struct wlr_seat *seat;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wlr_xdg_shell *xdg_shell;
	struct wlr_layer_shell_v1 *layer_shell;

	struct wl_list outputs;
	struct wl_list output_configs;
	struct wl_list inputs;
	struct wl_list pointers;
	struct wl_list keyboards;
	struct wl_list views;
	struct wl_list new_views;

	struct wl_listener new_output;
	struct wl_listener new_input;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;
	struct wl_listener request_cursor;

	struct wl_listener new_xdg_surface;
	struct wl_listener new_layer_surface;

	struct {
		int x, y;
		int width, height;
		struct wlr_texture *active_textures[5];
		struct wlr_texture *inactive_textures[5];
		int selected;
	} menu;

	struct {
		int sx, sy;
		struct wio_view *view;
	} interactive;

	enum wio_input_state input_state;
};

struct wio_output {
	struct wl_list link;

	struct wio_server *server;
	struct wlr_output *wlr_output;
	struct wl_list layers[4];

	struct wl_listener frame;
};

struct wio_output_config {
	const char *name;
	int x, y;
	int width, height;
	int scale;
	enum wl_output_transform transform;
	struct wl_list link;
};

struct wio_keyboard {
	struct wl_list link;

	struct wio_server *server;
	struct wlr_input_device *device;

	struct wl_listener modifiers;
	struct wl_listener key;
};

struct wio_new_view {
	pid_t pid;
	struct wlr_box box;
	struct wl_list link;
};

void server_new_output(struct wl_listener *listener, void *data);
void server_new_input(struct wl_listener *listener, void *data);
void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);
void seat_request_cursor(struct wl_listener *listener, void *data);

#endif
