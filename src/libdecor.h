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

#ifndef LIBDECOR_H
#define LIBDECOR_H

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

struct xdg_toplevel;

struct libdecor;
struct libdecor_frame;
struct libdecor_configuration;
struct libdecor_state;

enum libdecor_error {
	LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
	LIBDECOR_ERROR_INVALID_FRAME_CONFIGURATION,
};

enum libdecor_window_state {
	LIBDECOR_WINDOW_STATE_NONE = 0,
	LIBDECOR_WINDOW_STATE_ACTIVE = 1 << 0,
	LIBDECOR_WINDOW_STATE_MAXIMIZED = 1 << 1,
	LIBDECOR_WINDOW_STATE_FULLSCREEN = 1 << 2,
	LIBDECOR_WINDOW_STATE_TILED_LEFT = 1 << 3,
	LIBDECOR_WINDOW_STATE_TILED_RIGHT = 1 << 4,
	LIBDECOR_WINDOW_STATE_TILED_TOP = 1 << 5,
	LIBDECOR_WINDOW_STATE_TILED_BOTTOM = 1 << 6,
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

enum libdecor_capabilities {
	LIBDECOR_ACTION_MOVE = 1 << 0,
	LIBDECOR_ACTION_RESIZE = 1 << 1,
	LIBDECOR_ACTION_MINIMIZE = 1 << 2,
	LIBDECOR_ACTION_FULLSCREEN = 1 << 3,
	LIBDECOR_ACTION_CLOSE = 1 << 4,
};

struct libdecor_interface {
	void (* error)(struct libdecor *context,
		       enum libdecor_error error,
		       const char *message);

	/* Reserved */
	void (* reserved0)(void);
	void (* reserved1)(void);
	void (* reserved2)(void);
	void (* reserved3)(void);
	void (* reserved4)(void);
	void (* reserved5)(void);
	void (* reserved6)(void);
	void (* reserved7)(void);
	void (* reserved8)(void);
	void (* reserved9)(void);
};

struct libdecor_frame_interface {
	void (* configure)(struct libdecor_frame *frame,
			   struct libdecor_configuration *configuration,
			   void *user_data);
	void (* close)(struct libdecor_frame *frame,
		       void *user_data);
	void (* commit)(struct libdecor_frame *frame,
			void *user_data);
	void (* dismiss_popup)(struct libdecor_frame *frame,
			       const char *seat_name,
			       void *user_data);

	/* Reserved */
	void (* reserved0)(void);
	void (* reserved1)(void);
	void (* reserved2)(void);
	void (* reserved3)(void);
	void (* reserved4)(void);
	void (* reserved5)(void);
	void (* reserved6)(void);
	void (* reserved7)(void);
	void (* reserved8)(void);
	void (* reserved9)(void);
};

void
libdecor_unref(struct libdecor *context);

struct libdecor *
libdecor_new(struct wl_display *display,
	     struct libdecor_interface *iface);

int
libdecor_get_fd(struct libdecor *context);

int
libdecor_dispatch(struct libdecor *context,
		  int timeout);

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
			  struct libdecor_frame *parent);

void
libdecor_frame_set_title(struct libdecor_frame *frame,
			 const char *title);

const char *
libdecor_frame_get_title(struct libdecor_frame *frame);

void
libdecor_frame_set_app_id(struct libdecor_frame *frame,
			  const char *app_id);

void
libdecor_frame_set_capabilities(struct libdecor_frame *frame,
				enum libdecor_capabilities capabilities);

void
libdecor_frame_unset_capabilities(struct libdecor_frame *frame,
				  enum libdecor_capabilities capabilities);

bool
libdecor_frame_has_capability(struct libdecor_frame *frame,
			      enum libdecor_capabilities capability);

void
libdecor_frame_show_window_menu(struct libdecor_frame *frame,
				struct wl_seat *wl_seat,
				uint32_t serial,
				int x,
				int y);

void
libdecor_frame_popup_grab(struct libdecor_frame *frame,
			  const char *seat_name);

void
libdecor_frame_popup_ungrab(struct libdecor_frame *frame,
			    const char *seat_name);

void
libdecor_frame_translate_coordinate(struct libdecor_frame *frame,
				    int surface_x,
				    int surface_y,
				    int *frame_x,
				    int *frame_y);

void
libdecor_frame_set_max_content_size(struct libdecor_frame *frame,
				    int content_width,
				    int content_height);

void
libdecor_frame_set_min_content_size(struct libdecor_frame *frame,
				    int content_width,
				    int content_height);

void
libdecor_frame_resize(struct libdecor_frame *frame,
		      struct wl_seat *wl_seat,
		      uint32_t serial,
		      enum libdecor_resize_edge edge);

void
libdecor_frame_move(struct libdecor_frame *frame,
		    struct wl_seat *wl_seat,
		    uint32_t serial);

void
libdecor_frame_toplevel_commit(struct libdecor_frame *frame);

void
libdecor_frame_commit(struct libdecor_frame *frame,
		      struct libdecor_state *state,
		      struct libdecor_configuration *configuration);

void
libdecor_frame_set_minimized(struct libdecor_frame *frame);

void
libdecor_frame_set_maximized(struct libdecor_frame *frame);

void
libdecor_frame_unset_maximized(struct libdecor_frame *frame);

void
libdecor_frame_set_fullscreen(struct libdecor_frame *frame,
			      struct wl_output *output);

void
libdecor_frame_unset_fullscreen(struct libdecor_frame *frame);

bool
libdecor_frame_is_floating(struct libdecor_frame *frame);

void
libdecor_frame_close(struct libdecor_frame *frame);

void
libdecor_frame_map(struct libdecor_frame *frame);

struct xdg_surface *
libdecor_frame_get_xdg_surface(struct libdecor_frame *frame);

struct xdg_toplevel *
libdecor_frame_get_xdg_toplevel(struct libdecor_frame *frame);

struct libdecor_state *
libdecor_state_new(int width,
		   int height);


void
libdecor_state_free(struct libdecor_state *state);

bool
libdecor_configuration_get_content_size(struct libdecor_configuration *configuration,
					struct libdecor_frame *frame,
					int *width,
					int *height);

bool
libdecor_configuration_get_window_size(struct libdecor_configuration *configuration,
				       int *width,
				       int *height);

bool
libdecor_configuration_get_window_state(struct libdecor_configuration *configuration,
					enum libdecor_window_state *window_state);

#ifdef __cplusplus
}
#endif

#endif /* LIBDECOR_H */
