#ifndef _WIO_SERVER_H
#define _WIO_SERVER_H
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>

struct wio_server {
	struct wl_display *wl_display;

	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
};

#endif
