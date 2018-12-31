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

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include "libdecoration.h"
#include "utils.h"

#include "xdg-shell-client-protocol.h"

struct libdecor {
	int ref_count;

	struct libdecor_interface *iface;

	struct wl_display *wl_display;
	struct wl_registry *wl_registry;
	struct wl_subcompositor *wl_subcompositor;
	struct xdg_wm_base *xdg_wm_base;

	struct wl_callback *init_callback;
	bool init_done;
	bool has_error;

	struct wl_list frames;
};

struct libdecor_state {
	int content_width;
	int content_height;
};

struct libdecor_configuration {
	uint32_t serial;

	bool has_window_state;
	enum libdecor_window_state window_state;

	bool has_size;
	int window_width;
	int window_height;
};

struct libdecor_frame {
	int ref_count;

	struct libdecor *context;
	struct wl_list link;

	struct wl_surface *wl_surface;

	struct libdecor_frame_interface *iface;
	void *user_data;

	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;

	bool pending_map;

	struct libdecor_configuration *pending_configuration;
};

static void
do_map(struct libdecor_frame *frame);

struct libdecor_state *
libdecor_state_new(int width,
		   int height)
{
	struct libdecor_state *state;

	state = zalloc(sizeof *state);
	state->content_width = width;
	state->content_height = height;

	return state;
}

void
libdecor_state_free(struct libdecor_state *state)
{
	free(state);
}

static struct libdecor_configuration *
libdecor_configuration_new(void)
{
	struct libdecor_configuration *configuration;

	configuration = zalloc(sizeof *configuration);

	return configuration;
}

static void
libdecor_configuration_free(struct libdecor_configuration *configuration)
{
	free(configuration);
}

static void
window_size_to_content_size(int window_width,
			    int window_height,
			    int *content_width,
			    int *content_height)
{
	*content_width = window_width;
	*content_height = window_height;
}

bool
libdecor_configuration_get_content_size(struct libdecor_configuration *configuration,
					int *width,
					int *height)
{
	int content_width;
	int content_height;

	if (!configuration->has_size)
		return false;

	if (configuration->window_width == 0 || configuration->window_height == 0)
		return false;

	window_size_to_content_size(configuration->window_width,
				    configuration->window_height,
				    &content_width,
				    &content_height);
	*width = content_width;
	*height = content_height;
	return true;
}

bool
libdecor_configuration_get_window_state(struct libdecor_configuration *configuration,
					enum libdecor_window_state *window_state)
{
	if (!configuration->has_window_state)
		return false;

	*window_state = configuration->window_state;
	return true;
}

static void
xdg_surface_configure(void *user_data,
		      struct xdg_surface *xdg_surface,
		      uint32_t serial)
{
	struct libdecor_frame *frame = user_data;
	struct libdecor_configuration *configuration;

	configuration = frame->pending_configuration;
	frame->pending_configuration = NULL;

	if (!configuration)
		configuration = libdecor_configuration_new();

	configuration->serial = serial;

	frame->iface->configure(frame,
				configuration,
				frame->user_data);

	libdecor_configuration_free(configuration);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	xdg_surface_configure,
};

static enum libdecor_window_state
parse_states(struct wl_array *states)
{
	enum libdecor_window_state pending_state = LIBDECOR_WINDOW_STATE_NONE;
	uint32_t *p;

	wl_array_for_each(p, states) {
		enum xdg_toplevel_state state = *p;

		switch (state) {
		case XDG_TOPLEVEL_STATE_FULLSCREEN:
			pending_state |= LIBDECOR_WINDOW_STATE_FULLSCREEN;
			break;
		case XDG_TOPLEVEL_STATE_MAXIMIZED:
			pending_state |= LIBDECOR_WINDOW_STATE_MAXIMIZED;
			break;
		case XDG_TOPLEVEL_STATE_ACTIVATED:
			pending_state |= LIBDECOR_WINDOW_STATE_ACTIVE;
			break;
		default:
			break;
		}
	}

	return pending_state;
}

static void
xdg_toplevel_configure(void *user_data,
		       struct xdg_toplevel *xdg_toplevel,
		       int32_t width,
		       int32_t height,
		       struct wl_array *states)
{
	struct libdecor_frame *frame = user_data;
	enum libdecor_window_state window_state;

	frame->pending_configuration = libdecor_configuration_new();

	frame->pending_configuration->has_size = true;
	frame->pending_configuration->window_width = width;
	frame->pending_configuration->window_height = height;

	window_state = parse_states(states);
	frame->pending_configuration->has_window_state = true;
	frame->pending_configuration->window_state = window_state;
}

static void
xdg_toplevel_close(void *user_data,
		   struct xdg_toplevel *xdg_toplevel)
{
	struct libdecor_frame *frame = user_data;

	frame->iface->close(frame, frame->user_data);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
	xdg_toplevel_configure,
	xdg_toplevel_close,
};

static void
init_shell_surface(struct libdecor_frame *frame)
{
	struct libdecor *context = frame->context;

	frame->xdg_surface = xdg_wm_base_get_xdg_surface(context->xdg_wm_base,
							 frame->wl_surface);
	xdg_surface_add_listener(frame->xdg_surface,
				 &xdg_surface_listener,
				 frame);

	frame->xdg_toplevel = xdg_surface_get_toplevel(frame->xdg_surface);
	xdg_toplevel_add_listener(frame->xdg_toplevel,
				  &xdg_toplevel_listener,
				  frame);

	if (frame->pending_map)
		do_map(frame);
}

struct libdecor_frame *
libdecor_decorate(struct libdecor *context,
		  struct wl_surface *wl_surface,
		  struct libdecor_frame_interface *iface,
		  void *user_data)
{
	struct libdecor_frame *frame;

	if (context->has_error)
		return NULL;

	frame = zalloc(sizeof *frame);
	frame->ref_count = 1;
	frame->context = context;

	frame->wl_surface = wl_surface;
	frame->iface = iface;
	frame->user_data = user_data;

	wl_list_insert(&context->frames, &frame->link);

	if (context->init_done)
		init_shell_surface(frame);

	return frame;
}

void
libdecor_frame_ref(struct libdecor_frame *frame)
{
	frame->ref_count++;
}

void
libdecor_frame_unref(struct libdecor_frame *frame)
{
	frame->ref_count--;
	if (frame->ref_count == 0) {
		free(frame);
	}
}

enum xdg_toplevel_resize_edge
edge_to_xdg_edge(enum libdecor_resize_edge edge)
{
	switch (edge) {
	case LIBDECOR_RESIZE_EDGE_NONE:
		return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
	case LIBDECOR_RESIZE_EDGE_TOP:
		return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
	case LIBDECOR_RESIZE_EDGE_BOTTOM:
		return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
	case LIBDECOR_RESIZE_EDGE_LEFT:
		return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
	case LIBDECOR_RESIZE_EDGE_TOP_LEFT:
		return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
	case LIBDECOR_RESIZE_EDGE_BOTTOM_LEFT:
		return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
	case LIBDECOR_RESIZE_EDGE_RIGHT:
		return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
	case LIBDECOR_RESIZE_EDGE_TOP_RIGHT:
		return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
	case LIBDECOR_RESIZE_EDGE_BOTTOM_RIGHT:
		return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
	}

	abort();
}

void
libdecor_frame_request_interactive_resize(struct libdecor_frame *frame,
					  struct wl_seat *wl_seat,
					  uint32_t serial,
					  enum libdecor_resize_edge edge)
{
	enum xdg_toplevel_resize_edge xdg_edge;

	xdg_edge = edge_to_xdg_edge(edge);
	xdg_toplevel_resize(frame->xdg_toplevel, wl_seat, serial, xdg_edge);
}

void
libdecor_frame_request_interactive_move(struct libdecor_frame *frame,
					struct wl_seat *wl_seat,
					uint32_t serial)
{
	xdg_toplevel_move(frame->xdg_toplevel, wl_seat, serial);
}

void
libdecor_frame_request_maximize(struct libdecor_frame *frame)
{
	xdg_toplevel_set_maximized(frame->xdg_toplevel);
}

void
libdecor_frame_request_unmaximize(struct libdecor_frame *frame)
{
	xdg_toplevel_unset_maximized(frame->xdg_toplevel);
}

void
libdecor_frame_commit(struct libdecor_frame *frame,
		      struct libdecor_state *state,
		      struct libdecor_configuration *configuration)
{
	if (configuration) {
		xdg_surface_ack_configure(frame->xdg_surface,
					  configuration->serial);
	}
}

static void
do_map(struct libdecor_frame *frame)
{
	frame->pending_map = false;
	wl_surface_commit(frame->wl_surface);
}

void
libdecor_frame_map(struct libdecor_frame *frame)
{
	if (!frame->xdg_surface) {
		frame->pending_map = true;
		return;
	}

	do_map(frame);
}

static void
xdg_wm_base_ping(void *user_data,
		 struct xdg_wm_base *xdg_wm_base,
		 uint32_t serial)
{
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
	xdg_wm_base_ping,
};

static void
init_xdg_wm_base(struct libdecor *context,
		 uint32_t id,
		 uint32_t version)
{
	context->xdg_wm_base = wl_registry_bind(context->wl_registry,
						id,
						&xdg_wm_base_interface,
						1);
	xdg_wm_base_add_listener(context->xdg_wm_base,
				 &xdg_wm_base_listener,
				 context);
}

static void
init_wl_subcompositor(struct libdecor *context,
		      uint32_t id,
		      uint32_t version)
{
	context->wl_subcompositor = wl_registry_bind(context->wl_registry,
						     id,
						     &wl_subcompositor_interface,
						     1);
}

static void
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	struct libdecor *context = user_data;

	if (strcmp(interface, "xdg_wm_base") == 0)
		init_xdg_wm_base(context, id, version);
	else if (strcmp(interface, "wl_subcompositor") == 0)
		init_wl_subcompositor(context, id, version);
}

static void
registry_handle_global_remove(void *user_data,
			      struct wl_registry *wl_registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static bool
is_compositor_compatible(struct libdecor *context)
{
	if (!context->xdg_wm_base)
		return false;

	if (!context->wl_subcompositor)
		return false;

	return true;
}

static void
init_wl_display_callback(void *user_data,
			 struct wl_callback *callback,
			 uint32_t time)
{
	struct libdecor *context = user_data;
	struct libdecor_frame *frame;

	context->init_done = true;
	context->init_callback = NULL;

	if (!is_compositor_compatible(context)) {
		context->has_error = true;
		context->iface->error(context,
				      LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
				      "Compositor is missing required interfaces");
	}

	wl_list_for_each(frame, &context->frames, link)
		init_shell_surface(frame);
}

static const struct wl_callback_listener init_wl_display_callback_listener = {
	init_wl_display_callback
};

void
libdecor_unref(struct libdecor *context)
{
	context->ref_count--;
	if (context->ref_count == 0) {
		if (context->init_callback)
			wl_callback_destroy(context->init_callback);
		wl_registry_destroy(context->wl_registry);
		free(context);
	}
}

struct libdecor *
libdecor_new(struct wl_display *wl_display,
	     struct libdecor_interface *iface)
{
	struct libdecor *context;

	context = zalloc(sizeof *context);

	context->ref_count = 1;
	context->iface = iface;
	context->wl_display = wl_display;
	context->wl_registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(context->wl_registry,
				 &registry_listener,
				 context);
	context->init_callback = wl_display_sync(context->wl_display);
	wl_callback_add_listener(context->init_callback,
				 &init_wl_display_callback_listener,
				 context);

	wl_list_init(&context->frames);

	wl_display_flush(wl_display);

	return context;
}
