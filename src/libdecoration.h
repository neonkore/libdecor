/*
 * Copyright © 2017-2018 Red Hat Inc.
 * Copyright © 2018 Jonas Ådahl
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LIBDECORATION_H
#define LIBDECORATION_H

#include <stdbool.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) && __GNUC__ >= 4
#define LIBDECOR_EXPORT __attribute__ ((visibility("default")))
#else
#define LIBDECOR_EXPORT
#endif

struct xdg_toplevel *parent;

struct libdecor;
struct libdecor_frame;
struct libdecor_configuration;
struct libdecor_state;

enum libdecor_error {
	LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
};

enum libdecor_window_state {
	LIBDECOR_WINDOW_STATE_NONE = 0,
	LIBDECOR_WINDOW_STATE_ACTIVE = 1 << 0,
	LIBDECOR_WINDOW_STATE_MAXIMIZED = 1 << 1,
	LIBDECOR_WINDOW_STATE_FULLSCREEN = 1 << 2,
};

enum libdecor_resize_edge {
	LIBDECOR_RESIZE_EDGE_NONE,
	LIBDECOR_RESIZE_EDGE_TOP,
	LIBDECOR_RESIZE_EDGE_BOTTOM,
	LIBDECOR_RESIZE_EDGE_LEFT,
	LIBDECOR_RESIZE_EDGE_TOP_LEFT,
	LIBDECOR_RESIZE_EDGE_BOTTOM_LEFT,
	LIBDECOR_RESIZE_EDGE_RIGHT,
	LIBDECOR_RESIZE_EDGE_TOP_RIGHT,
	LIBDECOR_RESIZE_EDGE_BOTTOM_RIGHT,
};

struct libdecor_interface {
	void (* error)(struct libdecor *context,
		       enum libdecor_error error,
		       const char *message);
};

struct libdecor_frame_interface {
	void (* configure)(struct libdecor_frame *frame,
			   struct libdecor_configuration *configuration,
			   void *user_data);
	void (* close)(struct libdecor_frame *frame,
		       void *user_data);
};

struct libdecor *
libdecor_new(struct wl_display *display,
	     struct libdecor_interface *iface);

struct libdecor_frame *
libdecor_decorate(struct libdecor *context,
		  struct wl_surface *surface,
		  struct libdecor_frame_interface *iface,
		  void *user_data);

void
libdecor_frame_ref(struct libdecor_frame *frame);

void
libdecor_frame_unref(struct libdecor_frame *frame);

void
libdecor_frame_set_parent(struct libdecor_frame *frame,
			  struct xdg_toplevel *parent);

void
libdecor_frame_set_title(struct libdecor_frame *frame,
			 const char *title);

void
libdecor_frame_set_app_id(struct libdecor_frame *frame,
			  const char *app_id);

void
libdecor_frame_show_window_menu(struct libdecor_frame *frame,
				struct wl_seat *wl_seat,
				uint32_t serial,
				int x,
				int y);

void
libdecor_frame_set_max_content_size(struct libdecor_frame *frame,
				    int content_width,
				    int content_height);

void
libdecor_frame_set_min_content_size(struct libdecor_frame *frame,
				    int content_width,
				    int content_height);

void
libdecor_frame_request_interactive_resize(struct libdecor_frame *frame,
					  struct wl_seat *wl_seat,
					  uint32_t serial,
					  enum libdecor_resize_edge edge);

void
libdecor_frame_request_interactive_move(struct libdecor_frame *frame,
					struct wl_seat *wl_seat,
					uint32_t serial);

void
libdecor_frame_commit(struct libdecor_frame *frame,
		      struct libdecor_state *state,
		      struct libdecor_configuration *configuration);

void
libdecor_frame_map(struct libdecor_frame *frame);

struct libdecor_state *
libdecor_state_new(int width,
		   int height);


void
libdecor_state_free(struct libdecor_state *state);

bool
libdecor_configuration_get_content_size(struct libdecor_configuration *configuration,
					int *width,
					int *height);

bool
libdecor_configuration_get_window_state(struct libdecor_configuration *configuration,
					enum libdecor_window_state *window_state);

#ifdef __cplusplus
}
#endif

#endif /* LIBDECORATION_H */
