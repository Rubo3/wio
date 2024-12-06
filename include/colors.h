#ifndef _WIO_COLORS_H
#define _WIO_COLORS_H

#include <wlr/render/pass.h>

static const struct wlr_render_color background = {
	0x77 / 255.0f, 0x77 / 255.0f, 0x77 / 255.0f, 1.0f,
};

static const struct wlr_render_color selection_box = {
	0xFF / 255.0f, 0x0 / 255.0f, 0x0 / 255.0f, 1.0f,
};

static const struct wlr_render_color active_border = {
	0x50 / 255.0f, 0xA1 / 255.0f, 0xAD / 255.0f, 1.0f,
};

static const struct wlr_render_color inactive_border = {
	0x9C / 255.0f, 0xE9 / 255.0f, 0xE9 / 255.0f, 1.0f,
};

static const struct wlr_render_color menu_selected = {
	0x3D / 255.0f, 0x7D / 255.0f, 0x42 / 255.0f, 1.0f,
};

static const struct wlr_render_color menu_unselected = {
	0xEB / 255.0f, 0xFF / 255.0f, 0xEC / 255.0f, 1.0f,
};

static const struct wlr_render_color menu_border = {
	0x78 / 255.0f, 0xAD / 255.0f, 0x84 / 255.0f, 1.0f,
};

static const struct wlr_render_color surface = {
	0xEE / 255.0f, 0xEE / 255.0f, 0xEE / 255.0f, 1.0f,
};

#endif
