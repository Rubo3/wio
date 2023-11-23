#define _POSIX_C_SOURCE 200811L

#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "server.h"
#include "view.h"

static char *corners[9] = {
	"top_left_corner", "top_side", "top_right_corner",
	"left_side", NULL, "right_side",
	"bottom_left_corner", "bottom_side", "bottom_right_corner",
};
static char *corner = NULL;

static void
change_vt(struct wio_server *server, unsigned int vt) {
	if (!wlr_backend_is_multi(server->backend)) {
		return;
	}
	if (server->session) {
		wlr_session_change_vt(server->session, vt);
	}
}

static bool
server_handle_shortcut(struct wl_listener *listener, struct wlr_keyboard_key_event *event) {
	struct wio_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct wio_server   *server   = keyboard->server;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *symsv;
	int symsc = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &symsv);

	/* Catch C-A-F1 to C-A-F12 to change tty */
	if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < symsc; i++) {
			unsigned int vt = symsv[i] - XKB_KEY_XF86Switch_VT_1 + 1;
			if (vt >= 1 && vt <= 12) {
				change_vt(server, vt);
				/* don't send any key events to clients when changing tty */
				return true;
			}
		}
	}

	return false;
}

static void
keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
	struct wio_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
									   &keyboard->wlr_keyboard->modifiers);
}

static void
keyboard_handle_key(struct wl_listener *listener, void *data) {
	struct wio_keyboard *keyboard = wl_container_of(listener, keyboard, key);
	struct wio_server   *server   = keyboard->server;
	struct wlr_seat     *seat     = server->seat;
	struct wlr_keyboard_key_event *event = data;

	if (server_handle_shortcut(listener, event)) {
		return;
	}

	wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
	wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
}

static void
server_new_keyboard(struct wio_server *server, struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct wio_keyboard *keyboard = calloc(1, sizeof(struct wio_keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	struct xkb_rule_names rules = {0};
	rules.rules   = getenv("XKB_DEFAULT_RULES");
	rules.model   = getenv("XKB_DEFAULT_MODEL");
	rules.layout  = getenv("XKB_DEFAULT_LAYOUT");
	rules.variant = getenv("XKB_DEFAULT_VARIANT");
	rules.options = getenv("XKB_DEFAULT_OPTIONS");
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap  *keymap  = xkb_map_new_from_names(context, &rules,
												         XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

	wlr_seat_set_keyboard(server->seat, wlr_keyboard);
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void
server_new_pointer(struct wio_server *server, struct wlr_input_device *device) {
	wlr_cursor_attach_input_device(server->cursor, device);
}

void
server_new_input(struct wl_listener *listener, void *data) {
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

static void
process_cursor_motion(struct wio_server *server, uint32_t time) {
	double sx, sy;
	struct wlr_seat *seat = server->seat;
	struct wlr_surface *surface = NULL;
	struct wio_view *view = NULL;
	if (server->input_state == INPUT_STATE_NONE) {
		view = wio_view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	}
	if (view) {
		goto End;
	}
	switch (server->input_state) {
	case INPUT_STATE_MOVE_SELECT:
	case INPUT_STATE_RESIZE_SELECT:
	case INPUT_STATE_DELETE_SELECT:
	case INPUT_STATE_HIDE_SELECT:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "hand1");
		break;
	case INPUT_STATE_MOVE:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "grabbing");
		break;
	case INPUT_STATE_BORDER_DRAG:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, corner);
		break;
	case INPUT_STATE_RESIZE_START:
	case INPUT_STATE_NEW_START:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "top_left_corner");
		break;
	case INPUT_STATE_RESIZE_END:
	case INPUT_STATE_NEW_END:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "grabbing");
		break;
	default:
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
		break;
 	}
End:
	if (surface) {
		bool focus_changed = seat->pointer_state.focused_surface != surface;
		wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
		if (!focus_changed) {
			wlr_seat_pointer_notify_motion(seat, time, sx, sy);
		}
		return;
	}
	if (view) {
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, corners[view->area]);
	}
	wlr_seat_pointer_clear_focus(seat);
}

void
server_cursor_motion(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

void
server_cursor_motion_absolute( struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
	process_cursor_motion(server, event->time_msec);
}

static void
menu_handle_button(struct wio_server *server, struct wlr_pointer_button_event *event) {
	server->menu.x = server->menu.y = -1;
	switch (server->menu.selected) {
	case 0:
		server->input_state = INPUT_STATE_NEW_START;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "grabbing");
		break;
	case 1:
		server->input_state = INPUT_STATE_RESIZE_SELECT;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "hand1");
		break;
	case 2:
		server->input_state = INPUT_STATE_MOVE_SELECT;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "hand1");
		break;
	case 3:
		server->input_state = INPUT_STATE_DELETE_SELECT;
		wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "hand1");
		break;
	default:
		server->input_state = INPUT_STATE_NONE;
		break;
	}
}

static void
view_begin_interactive(struct wio_view *view, struct wlr_surface *surface, double sx, double sy,
					   const char *cursor, enum wio_input_state state) {
	wio_view_focus(view, surface);
	view->server->interactive.view = view;
	view->server->interactive.sx = (int)sx;
	view->server->interactive.sy = (int)sy;
	view->server->input_state = state;
	wlr_cursor_set_xcursor(view->server->cursor, view->server->cursor_mgr, cursor);
}

static void
view_end_interactive(struct wio_server *server) {
	server->input_state = INPUT_STATE_NONE;
	server->interactive.view = NULL;
	// TODO: Restore previous pointer?
	wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
}

static void
new_view(struct wio_server *server) {
	struct wlr_box box = wio_which_box(server);
	if (box.width < MINWIDTH || box.height < MINHEIGHT) {
		return;
 	}
	struct wio_new_view *view = calloc(1, sizeof(struct wio_new_view));
    view->box = box;
	int fd[2];
	if (pipe(fd) != 0) {
		wlr_log(WLR_ERROR, "Unable to create pipe for fork");
		return;
	}
	char cmd[1024];
	if (snprintf(cmd, sizeof(cmd), "%s -- %s", server->cage, server->term) >= (int)sizeof(cmd)) {
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

static void
handle_button_internal(struct wio_server *server, struct wlr_pointer_button_event *event) {
	// TODO: open menu if the client doesn't handle the button press
	// will basically involve some serial hacking
	struct wlr_box menu_box = {
		.x = server->menu.x, .y = server->menu.y,
		.width = server->menu.width, .height = server->menu.height,
	};
    struct wlr_box box;
    double sx, sy;
	struct wlr_surface *surface = NULL;
	struct wio_view *view;
	switch (server->input_state) {
	case INPUT_STATE_NONE:
		if (event->state == WLR_BUTTON_PRESSED) {
			// TODO: Open over the last-used menu item
			server->input_state = INPUT_STATE_MENU;
			server->menu.x = server->cursor->x;
			server->menu.y = server->cursor->y;
		}
		break;
	case INPUT_STATE_MENU:
		if (wlr_box_contains_point( &menu_box, server->cursor->x, server->cursor->y)) {
			menu_handle_button(server, event);
			break;
		}
		if (event->state == WLR_BUTTON_PRESSED) {
			server->input_state = INPUT_STATE_NONE;
			server->menu.x = server->menu.y = -1;
		}
		break;
	case INPUT_STATE_NEW_START:
		if (event->state != WLR_BUTTON_PRESSED) {
			break;
		}
		server->interactive.sx = server->cursor->x;
		server->interactive.sy = server->cursor->y;
		server->input_state = INPUT_STATE_NEW_END;
		break;
	case INPUT_STATE_NEW_END:
		new_view(server);
		view_end_interactive(server);
		break;
	case INPUT_STATE_RESIZE_SELECT:
		if (event->state != WLR_BUTTON_PRESSED) {
			break;
		}
		view = wio_view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		if (view) {
			view_begin_interactive(view, surface, sx, sy, "grabbing", INPUT_STATE_RESIZE_START);
		} else {
			view_end_interactive(server);
		}
		break;
	case INPUT_STATE_RESIZE_START:
		if (event->state != WLR_BUTTON_PRESSED) {
			break;
		}
		server->interactive.sx = server->cursor->x;
		server->interactive.sy = server->cursor->y;
		server->interactive.view->area = VIEW_AREA_BORDER_BOTTOM_RIGHT;
		server->input_state = INPUT_STATE_RESIZE_END;
		break;
	case INPUT_STATE_BORDER_DRAG:
		box = wio_which_box(server);
		box = wio_canon_box(server, box);
		goto Done;
	case INPUT_STATE_RESIZE_END:
		box = wio_which_box(server);
		if (box.width < MINWIDTH || box.height < MINHEIGHT) {
			view_end_interactive(server);
		break; // TODO: should this be inside or outside the if?
		}
	Done:
		wio_view_move(server->interactive.view, box.x, box.y);
		wlr_xdg_toplevel_set_size(server->interactive.view->xdg_surface->toplevel, box.width, box.height);

		view_end_interactive(server);
		break;
	case INPUT_STATE_MOVE_SELECT:
		if (event->state != WLR_BUTTON_PRESSED) {
			break;
		}
		view = wio_view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		if (view) {
			view_begin_interactive(view, surface, sx, sy, "grabbing", INPUT_STATE_MOVE);
		} else {
			view_end_interactive(server);
		}
		break;
	case INPUT_STATE_MOVE:
		wio_view_move(server->interactive.view,
					  server->cursor->x - server->interactive.sx,
					  server->cursor->y - server->interactive.sy);
		view_end_interactive(server);
		break;
	case INPUT_STATE_DELETE_SELECT:
		if (event->state != WLR_BUTTON_PRESSED) {
			break;
		}
		view = wio_view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
		if (view) {
			wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
		}
		view_end_interactive(server);
		break;
	default:
		// TODO
		break;
	}
}

void
server_cursor_button(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, cursor_button);
	struct wlr_pointer_button_event *event = data;
	double sx, sy;
	struct wlr_surface *surface = NULL;
	struct wio_view *view = NULL;
	if (server->input_state == INPUT_STATE_NONE) {
		view = wio_view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
	}
    if (!view) {
        if (event->state == WLR_BUTTON_PRESSED && event->button != BTN_RIGHT) {
			view_end_interactive(server);
			return;
		}
		handle_button_internal(server, event);
        return;
	}
	wio_view_focus(view, surface);
	switch (view->area) {
	case VIEW_AREA_SURFACE:
		wlr_seat_pointer_notify_button(server->seat,
				event->time_msec, event->button, event->state);
		break;
	default:
        if (event->button == BTN_RIGHT) {
			view_begin_interactive(view, surface, sx, sy, "grabbing", INPUT_STATE_MOVE);
			break;
		}
		corner = corners[view->area];
		view_begin_interactive(view, surface, view->x, view->y, corner, INPUT_STATE_BORDER_DRAG);
		break;
	}
}

void
server_cursor_axis(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	wlr_seat_pointer_notify_axis(server->seat, event->time_msec,
							     event->orientation, event->delta,
								 event->delta_discrete, event->source);
}

void
server_cursor_frame(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, cursor_frame);
	wlr_seat_pointer_notify_frame(server->seat);
}

void
seat_request_cursor(struct wl_listener *listener, void *data) {
	struct wio_server *server = wl_container_of(listener, server, request_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;
	if (focused_client == event->seat_client && server->input_state == INPUT_STATE_NONE) {
		wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
	}
}
