#include <stdlib.h>
#include <linux/input-event-codes.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <xkbcommon/xkbcommon.h>
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

static void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	struct wio_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct wio_server *server = keyboard->server;
	struct wlr_event_keyboard_key *event = data;
	struct wlr_seat *seat = server->seat;
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
	case 2:
		server->input_state = INPUT_STATE_MOVE_SELECT;
		wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
				"hand1", server->cursor);
		break;
	default:
		server->input_state = INPUT_STATE_NONE;
		break;
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
	case INPUT_STATE_MOVE_SELECT:
		if (event->state == WLR_BUTTON_PRESSED) {
			double sx, sy;
			struct wlr_surface *surface = NULL;
			struct wio_view *view = wio_view_at(server,
					server->cursor->x, server->cursor->y, &surface, &sx, &sy);
			if (view != NULL) {
				wio_view_focus(view, surface);
				server->interactive.view = view;
				server->interactive.sx = (int)sx;
				server->interactive.sy = (int)sy;
				server->input_state = INPUT_STATE_MOVE;
				wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
						"grabbing", server->cursor);
			} else {
				server->input_state = INPUT_STATE_NONE;
				wlr_xcursor_manager_set_cursor_image(server->cursor_mgr,
						"left_ptr", server->cursor);
			}
		}
		break;
	case INPUT_STATE_MOVE:
		server->interactive.view->x =
			server->cursor->x - server->interactive.sx;
		server->interactive.view->y =
			server->cursor->y - server->interactive.sy;
		server->input_state = INPUT_STATE_NONE;
		server->interactive.view = NULL;
		wlr_cursor_set_surface(server->cursor, server->interactive.cursor,
				server->interactive.hotspot_x, server->interactive.hotspot_y);
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
		server->interactive.cursor = event->surface;
		server->interactive.hotspot_x = event->hotspot_x;
		server->interactive.hotspot_y = event->hotspot_y;
	}
}
