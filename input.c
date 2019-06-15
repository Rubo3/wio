#define _POSIX_C_SOURCE 200811L
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include <unistd.h>
#include "server.h"
#include "view.h"

static void keyboard_handle_modifiers(
		struct wl_listener *listener, void *data) {
	struct wio_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->device);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->device->keyboard->modifiers);
}

static void view_end_interactive(struct wio_server *server);
static void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	struct wio_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct wio_server *server = keyboard->server;
	struct wlr_event_keyboard_key *event = data;
	struct wlr_seat *seat = server->seat;
	xkb_keycode_t keycode = event->keycode + 8;
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
		keyboard->device->keyboard->xkb_state,
		keycode, &syms);

	for (int i = 0; i < nsyms; i++) {
		if (syms[i] == XKB_KEY_Escape) {
			switch (server->input_state) {
			case INPUT_STATE_NONE:
			case INPUT_STATE_MENU:
				break;
			default:
				view_end_interactive(server);
				return;
			}
		}
	}

	wlr_seat_set_keyboard(seat, keyboard->device);
	wlr_seat_keyboard_notify_key(seat, event->time_msec,
		event->keycode, event->state);
}

static void server_new_keyboard(
		struct wio_server *server, struct wlr_input_device *device) {
	struct wio_keyboard *keyboard = calloc(1, sizeof(struct wio_keyboard));
	keyboard->server = server;
	keyboard->device = device;

	struct xkb_rule_names rules = { 0 };
	rules.rules = getenv("XKB_DEFAULT_RULES");
	rules.model = getenv("XKB_DEFAULT_MODEL");
	rules.layout = getenv("XKB_DEFAULT_LAYOUT");
	rules.variant = getenv("XKB_DEFAULT_VARIANT");
	rules.options = getenv("XKB_DEFAULT_OPTIONS");
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_map_new_from_names(context, &rules,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(device->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(device->keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&device->keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&device->keyboard->events.key, &keyboard->key);

	wlr_seat_set_keyboard(server->seat, device);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(
		struct wio_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

void server_new_input(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void process_cursor_motion(struct wio_server *server, uint32_t time) {
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct wio_view *view = wio_view_at(
			server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (!view) {
		switch (server->input_state) {
		case INPUT_STATE_MOVE_SELECT:
		case INPUT_STATE_RESIZE_SELECT:
		case INPUT_STATE_DELETE_SELECT:
		case INPUT_STATE_HIDE_SELECT:
			wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
					"hand1", server->cursor);
			break;
		case INPUT_STATE_MOVE:
			wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
					"grabbing", server->cursor);
			break;
		case INPUT_STATE_RESIZE_START:
		case INPUT_STATE_NEW_START:
			wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
					"top_left_corner", server->cursor);
			break;
		case INPUT_STATE_RESIZE_END:
		case INPUT_STATE_NEW_END:
			wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
					"bottom_right_corner", server->cursor);
			break;
		default:
			wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
					"left_ptr", server->cursor);
			break;
		}
	}
	if (surface) {
		bool focus_changed = seat->pointer_state.focused_surface != surface;
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		if (!focus_changed) {
			wlr_seat_pointer_notify_motion(seat, time, sx, sy);
		}
	} else {
		wlr_seat_pointer_clear_focus(seat);
	}
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
	struct wio_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_event_pointer_motion *event = data;
	wlr_cursor_move(server->cursor, event->device,
			event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {
	struct wio_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;
	wlr_cursor_warp_absolute(server->cursor, event->device, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void menu_handle_button(
		struct wio_server *server, struct wlr_event_pointer_button *event) {
	server->menu.x = server->menu.y = -1;
	switch (server->menu.selected) {
	case 0:
		server->input_state = INPUT_STATE_NEW_START;
		wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
				"top_left_corner", server->cursor);
		break;
	case 1:
		server->input_state = INPUT_STATE_RESIZE_SELECT;
		wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
				"hand1", server->cursor);
		break;
	case 2:
		server->input_state = INPUT_STATE_MOVE_SELECT;
		wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
				"hand1", server->cursor);
		break;
	case 3:
		server->input_state = INPUT_STATE_DELETE_SELECT;
		wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
				"hand1", server->cursor);
		break;
	default:
		server->input_state = INPUT_STATE_NONE;
		break;
	}
}

static void view_begin_interactive(struct wio_view *view,
		struct wlr_surface *surface, double sx, double sy,
		const char *cursor, enum wio_input_state state) {
	wio_view_focus(view, surface);
	view->server->interactive.view = view;
	view->server->interactive.sx = (int)sx;
	view->server->interactive.sy = (int)sy;
	view->server->input_state = state;
	wlr_xcursor_manager_set_cursor_image(view->server->cursor_mgr,
			cursor, view->server->cursor);
}

static void view_end_interactive(struct wio_server *server) {
	server->input_state = INPUT_STATE_NONE;
	server->interactive.view = NULL;
	// TODO: Restore previous pointer?
	wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
			"left_ptr", server->cursor);
}

static void new_view(struct wio_server *server) {
	int x1 = server->interactive.sx, x2 = server->cursor->x;
	int y1 = server->interactive.sy, y2 = server->cursor->y;
	if (x2 < x1) {
		int _ = x1;
		x1 = x2;
		x2 = _;
	}
	if (y2 < y1) {
		int _ = y1;
		y1 = y2;
		y2 = _;
	}
	struct wio_new_view *view = calloc(1, sizeof(struct wio_new_view));
	view->box.x = x1;
	view->box.y = y1;
	view->box.width = x2 - x1;
	view->box.height = y2 - y1;
	if (view->box.width < 100){
		view->box.width = 100;
	}
	if (view->box.height < 100){
		view->box.height = 100;
	}
	int fd[2];
	if (pipe(fd) != 0) {
		wlr_log(WLR_ERROR, "Unable to create pipe for fork");
		return;
	}
	char cmd[1024];
	if (snprintf(cmd, sizeof(cmd), "%s -- %s",
			server->cage, server->term) >= (int)sizeof(cmd)) {
		fprintf(stderr, "New view command truncated\n");
		return;
	}
	pid_t pid, child;
	if ((pid = fork()) == 0) {
		setsid();
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);
		close(fd[0]);
		if ((child = fork()) == 0) {
			close(fd[1]);
			execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
			_exit(0);
		}
		ssize_t s = 0;
		while ((size_t)s < sizeof(pid_t)) {
			s += write(fd[1], ((uint8_t *)&child) + s, sizeof(pid_t) - s);
		}
		close(fd[1]);
		_exit(0); // Close child process
	} else if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		wlr_log(WLR_ERROR, "fork failed");
		return;
	}
	close(fd[1]); // close write
	ssize_t s = 0;
	while ((size_t)s < sizeof(pid_t)) {
		s += read(fd[0], ((uint8_t *)&child) + s, sizeof(pid_t) - s);
	}
	close(fd[0]);
	waitpid(pid, NULL, 0);
	if (child > 0) {
		view->pid = child;
		wl_list_insert(&server->new_views, &view->link);
	}
}

static void handle_button_internal(
		struct wio_server *server, struct wlr_event_pointer_button *event) {
	// TODO: open menu if the client doesn't handle the button press
	// will basically involve some serial hacking
	struct wlr_box menu_box = {
		.x = server->menu.x, .y = server->menu.y,
		.width = server->menu.width, .height = server->menu.height,
	};
	int x1, x2, y1, y2;
	switch (server->input_state) {
	case INPUT_STATE_NONE:
		if (event->state == WLR_BUTTON_PRESSED && event->button == BTN_RIGHT) {
			// TODO: Open over the last-used menu item
			server->input_state = INPUT_STATE_MENU;
			server->menu.x = server->cursor->x;
			server->menu.y = server->cursor->y;
		}
		break;
	case INPUT_STATE_MENU:
		if (wlr_box_contains_point(
					&menu_box, server->cursor->x, server->cursor->y)) {
			menu_handle_button(server, event);
		} else {
			if (event->state == WLR_BUTTON_PRESSED) {
				server->input_state = INPUT_STATE_NONE;
				server->menu.x = server->menu.y = -1;
			}
		}
		break;
	case INPUT_STATE_NEW_START:
		if (event->state == WLR_BUTTON_PRESSED) {
			server->interactive.sx = server->cursor->x;
			server->interactive.sy = server->cursor->y;
			server->input_state = INPUT_STATE_NEW_END;
		}
		break;
	case INPUT_STATE_NEW_END:
		new_view(server);
		view_end_interactive(server);
		break;
	case INPUT_STATE_RESIZE_SELECT:
		if (event->state == WLR_BUTTON_PRESSED) {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct wio_view *view = wio_view_at(server,
					server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (view != NULL) {
				view_begin_interactive(view, surface, sx, sy,
						"bottom_right_corner", INPUT_STATE_RESIZE_START);
			} else {
				view_end_interactive(server);
			}
		}
		break;
	case INPUT_STATE_RESIZE_START:
		if (event->state == WLR_BUTTON_PRESSED) {
			server->interactive.sx = server->cursor->x;
			server->interactive.sy = server->cursor->y;
			server->input_state = INPUT_STATE_RESIZE_END;
		}
		break;
	case INPUT_STATE_RESIZE_END:
		x1 = server->interactive.sx, x2 = server->cursor->x;
		y1 = server->interactive.sy, y2 = server->cursor->y;
		if (x2 < x1) {
			int _ = x1;
			x1 = x2;
			x2 = _;
		}
		if (y2 < y1) {
			int _ = y1;
			y1 = y2;
			y2 = _;
		}
		wio_view_move(server->interactive.view,
				x1, y1);
		uint32_t width = x2 - x1, height = y2 - y1;
		if (width < 100) {
			width = 100;
		}
		if (height < 100) {
			height = 100;
		}
		wlr_xdg_toplevel_set_size(
				server->interactive.view->xdg_surface, width, height);
		view_end_interactive(server);
		break;
	case INPUT_STATE_MOVE_SELECT:
		if (event->state == WLR_BUTTON_PRESSED) {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct wio_view *view = wio_view_at(server,
					server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (view != NULL) {
				view_begin_interactive(view, surface, sx, sy,
						"grabbing", INPUT_STATE_MOVE);
			} else {
				view_end_interactive(server);
			}
		}
		break;
	case INPUT_STATE_MOVE:
		wio_view_move(server->interactive.view,
			server->cursor->x - server->interactive.sx,
			server->cursor->y - server->interactive.sy);
		view_end_interactive(server);
		break;
	case INPUT_STATE_DELETE_SELECT:
		if (event->state == WLR_BUTTON_PRESSED) {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct wio_view *view = wio_view_at(server,
					server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (view != NULL) {
				wlr_xdg_toplevel_send_close(view->xdg_surface);
			}
			view_end_interactive(server);
		}
		break;
	default:
		// TODO
		break;
	}
}

void server_cursor_button(struct wl_listener *listener, void *data) {
	struct wio_server *server =
		wl_container_of(listener, server, cursor_button);
	struct wlr_event_pointer_button *event = data;
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct wio_view *view = wio_view_at(
			server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	if (server->input_state == INPUT_STATE_NONE && view) {
		wio_view_focus(view, surface);
		wlr_seat_pointer_notify_button(server->seat,
				event->time_msec, event->button, event->state);
	} else {
		handle_button_internal(server, event);
	}
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_event_pointer_axis *event = data;
	wlr_seat_pointer_notify_axis(server->seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
	struct wio_server *server =
		wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(
			listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client
			&& server->input_state == INPUT_STATE_NONE) {
		wlr_cursor_set_surface(server->cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
	}
}
