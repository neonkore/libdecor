/*
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

#include <linux/input.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <wayland-cursor.h>

#include "libdecoration-plugin.h"

#define BORDER_MARGIN 24

/* global variables defined in demo */
static struct wl_shm *wl_shm;
static struct wl_cursor_theme *cursor_theme;
static struct wl_seat *wl_seat;
static struct wl_surface *pointer_focus;
static struct wl_surface *cursor_surface;
static struct wl_compositor *wl_compositor;

enum decoration_type {
	DECORATION_TYPE_NONE,
	DECORATION_TYPE_SHADOW,
};

enum border_side {
	BORDER_SIDE_TOP,
	BORDER_SIDE_RIGHT,
	BORDER_SIDE_BOTTOM,
	BORDER_SIDE_LEFT,
};

struct custom_buffer {
	struct wl_buffer *wl_buffer;
	bool in_use;
	bool is_detached;

	void *data;
	size_t data_size;
	int width;
	int height;
};

struct border_component {
	struct wl_surface *wl_surface;
	struct wl_subsurface *wl_subsurface;
	struct custom_buffer *buffer;
};

struct libdecor_frame_custom {
	struct libdecor_frame frame;

	struct libdecor_plugin_custom *plugin_custom;

	int content_width;
	int content_height;

	enum decoration_type decoration_type;

	struct {
		bool is_showing;

		struct border_component top;
		struct border_component right;
		struct border_component bottom;
		struct border_component left;
	} border;
};

struct libdecor_plugin_custom {
	struct libdecor_plugin plugin;

	struct libdecor *context;

	struct wl_registry *wl_registry;
	struct wl_subcompositor *wl_subcompositor;
	struct wl_pointer *wl_pointer;

	struct {
		struct wl_cursor *top_side;
		struct wl_cursor *right_side;
		struct wl_cursor *bottom_side;
		struct wl_cursor *left_side;
	} cursors;
};

static const char *plugin_proxy_tag = "plugin-custom";

static void
buffer_free(struct custom_buffer *custom_buffer);

static void
libdecor_plugin_custom_destroy(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_custom *plugin_custom =
		(struct libdecor_plugin_custom *) plugin;

	wl_pointer_destroy(plugin_custom->wl_pointer);

	wl_registry_destroy(plugin_custom->wl_registry);
}

static struct libdecor_frame *
libdecor_plugin_custom_frame_new(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_custom *plugin_custom =
		(struct libdecor_plugin_custom *) plugin;
	struct libdecor_frame_custom *frame_custom;

	frame_custom = zalloc(sizeof *frame_custom);
	frame_custom->plugin_custom = plugin_custom;

	return &frame_custom->frame;
}

static int
create_anonymous_file(off_t size);

static void
custom_buffer_release(void *user_data,
	       struct wl_buffer *wl_buffer)
{
	struct custom_buffer *custom_buffer = user_data;

	if (custom_buffer->is_detached)
		buffer_free(custom_buffer);
	else
		custom_buffer->in_use = false;
}

static const struct wl_buffer_listener custom_buffer_listener = {
	custom_buffer_release
};

static struct custom_buffer *
custom_create_shm_buffer(struct libdecor_plugin_custom *plugin_custom,
		  int width,
		  int height)
{
	struct wl_shm_pool *pool;
	int fd, size, stride;
	void *data;
	struct custom_buffer *buffer;

	stride = width * 4;
	size = stride * height;

	fd = create_anonymous_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %m\n",
			size);
		return NULL;
	}

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %m\n");
		close(fd);
		return NULL;
	}

	pool = wl_shm_create_pool(wl_shm, fd, size);
	buffer = zalloc(sizeof *buffer);
	buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0,
						      width, height,
						      stride,
						      WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer->wl_buffer, &custom_buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->data = data;
	buffer->data_size = size;
	buffer->width = width;
	buffer->height = height;

	return buffer;
}

static void
buffer_free(struct custom_buffer *buffer)
{
	if (buffer->wl_buffer) {
		wl_buffer_destroy(buffer->wl_buffer);
		munmap(buffer->data, buffer->data_size);
		buffer->wl_buffer = NULL;
		buffer->in_use = false;
	}
	free(buffer);
}

static void
free_border_component(struct border_component *border_component)
{
	if (border_component->wl_surface) {
		wl_subsurface_destroy(border_component->wl_subsurface);
		border_component->wl_subsurface = NULL;
		wl_surface_destroy(border_component->wl_surface);
		border_component->wl_surface = NULL;
	}
	if (border_component->buffer)
		buffer_free(border_component->buffer);
}

static void
libdecor_plugin_custom_frame_free(struct libdecor_plugin *plugin,
				 struct libdecor_frame *frame)
{
	struct libdecor_frame_custom *frame_custom =
		(struct libdecor_frame_custom *) frame;

	free_border_component(&frame_custom->border.top);
	free_border_component(&frame_custom->border.right);
	free_border_component(&frame_custom->border.bottom);
	free_border_component(&frame_custom->border.left);
}

static bool
is_border_surfaces_showing(struct libdecor_frame_custom *frame_custom)
{
	return frame_custom->border.is_showing;
}

static void
hide_border_component(struct border_component *border_component)
{
	wl_surface_attach(border_component->wl_surface, NULL, 0, 0);
	wl_surface_commit(border_component->wl_surface);
}

static void
hide_border_surfaces(struct libdecor_frame_custom *frame_custom)
{
	hide_border_component(&frame_custom->border.top);
	hide_border_component(&frame_custom->border.right);
	hide_border_component(&frame_custom->border.bottom);
	hide_border_component(&frame_custom->border.left);
	frame_custom->border.is_showing = false;
}

static void
create_surface_subsurface_pair(struct libdecor_frame_custom *frame_custom,
			       struct wl_surface **out_wl_surface,
			       struct wl_subsurface **out_wl_subsurface)
{
	struct libdecor_plugin_custom *plugin_custom = frame_custom->plugin_custom;
	struct libdecor_frame *frame = &frame_custom->frame;
	struct wl_subcompositor *wl_subcompositor = plugin_custom->wl_subcompositor;
	struct wl_surface *wl_surface;
	struct wl_surface *parent;
	struct wl_subsurface *wl_subsurface;

	wl_surface = wl_compositor_create_surface(wl_compositor);
	wl_surface_set_user_data(wl_surface, frame_custom);
	wl_proxy_set_tag((struct wl_proxy *) wl_surface,
			 &plugin_proxy_tag);

	parent = libdecor_frame_get_wl_surface(frame);
	wl_subsurface = wl_subcompositor_get_subsurface(wl_subcompositor,
							wl_surface,
							parent);

	*out_wl_surface = wl_surface;
	*out_wl_subsurface = wl_subsurface;
}

static void
ensure_border_surfaces(struct libdecor_frame_custom *frame_custom)
{
	if (frame_custom->border.top.wl_surface)
		return;

	create_surface_subsurface_pair(frame_custom,
				       &frame_custom->border.top.wl_surface,
				       &frame_custom->border.top.wl_subsurface);
	create_surface_subsurface_pair(frame_custom,
				       &frame_custom->border.right.wl_surface,
				       &frame_custom->border.right.wl_subsurface);
	create_surface_subsurface_pair(frame_custom,
				       &frame_custom->border.bottom.wl_surface,
				       &frame_custom->border.bottom.wl_subsurface);
	create_surface_subsurface_pair(frame_custom,
				       &frame_custom->border.left.wl_surface,
				       &frame_custom->border.left.wl_subsurface);
}

static void
calculate_component_size(struct libdecor_frame_custom *frame_custom,
			 enum border_side border_side,
			 int *component_x,
			 int *component_y,
			 int *component_width,
			 int *component_height)
{
	struct libdecor_frame *frame = &frame_custom->frame;
	int content_width, content_height;

	content_width = libdecor_frame_get_content_width(frame);
	content_height = libdecor_frame_get_content_height(frame);

	switch (border_side) {
	case BORDER_SIDE_TOP:
		*component_x = -BORDER_MARGIN;
		*component_y = -BORDER_MARGIN;
		*component_width = content_width + (2 * BORDER_MARGIN);
		*component_height = BORDER_MARGIN;
		return;
	case BORDER_SIDE_RIGHT:
		*component_x = content_width;
		*component_y = 0;
		*component_width = BORDER_MARGIN;
		*component_height = content_height;
		return;
	case BORDER_SIDE_BOTTOM:
		*component_x = -BORDER_MARGIN;
		*component_y = content_height;
		*component_width = content_width + (2 * BORDER_MARGIN);
		*component_height = BORDER_MARGIN;
		return;
	case BORDER_SIDE_LEFT:
		*component_x = -BORDER_MARGIN;
		*component_y = 0;
		*component_width = BORDER_MARGIN;
		*component_height = content_height;
		return;
	}

	abort();
}

static void
draw_shadow_content(struct libdecor_plugin_custom *plugin_custom,
		    struct custom_buffer *buffer,
		    enum border_side border_side)
{
	uint32_t *pixels = buffer->data;
	uint32_t color = 0x80303030;
	int i;

	for (i = 0; i < buffer->width * buffer->height; i++)
		pixels[i] = color;
}

static void
draw_shadow_component(struct libdecor_frame_custom *frame_custom,
		      struct border_component *border_component,
		      enum border_side border_side)
{
	struct libdecor_plugin_custom *plugin_custom = frame_custom->plugin_custom;
	struct custom_buffer *old_buffer;
	struct custom_buffer *buffer = NULL;
	int component_x;
	int component_y;
	int component_width;
	int component_height;

	calculate_component_size(frame_custom, border_side,
				 &component_x, &component_y,
				 &component_width, &component_height);

	old_buffer = border_component->buffer;
	if (old_buffer) {
		if (!old_buffer->in_use &&
		    old_buffer->width == component_width &&
		    old_buffer->height == component_height) {
			buffer = old_buffer;
		} else {
			buffer_free(old_buffer);
			border_component->buffer = NULL;
		}
	}

	if (!buffer) {
		buffer = custom_create_shm_buffer(plugin_custom,
					   component_width,
					   component_height);
	}

	draw_shadow_content(plugin_custom, buffer, border_side);

	wl_surface_attach(border_component->wl_surface,
			  buffer->wl_buffer,
			  0, 0);
	buffer->in_use = true;
	wl_surface_commit(border_component->wl_surface);
	wl_surface_damage(border_component->wl_surface,
			  0, 0,
			  component_width, component_height);
	wl_subsurface_set_position(border_component->wl_subsurface,
				   component_x, component_y);

	border_component->buffer = buffer;
}

static void
draw_shadow(struct libdecor_frame_custom *frame_custom)
{
	draw_shadow_component(frame_custom,
			      &frame_custom->border.top,
			      BORDER_SIDE_TOP);
	draw_shadow_component(frame_custom,
			      &frame_custom->border.right,
			      BORDER_SIDE_RIGHT);
	draw_shadow_component(frame_custom,
			      &frame_custom->border.bottom,
			      BORDER_SIDE_BOTTOM);
	draw_shadow_component(frame_custom,
			      &frame_custom->border.left,
			      BORDER_SIDE_LEFT);
	frame_custom->border.is_showing = true;
}

static void
draw_decoration(struct libdecor_frame_custom *frame_custom)
{
	switch (frame_custom->decoration_type) {
	case DECORATION_TYPE_NONE:
		if (is_border_surfaces_showing(frame_custom))
			hide_border_surfaces(frame_custom);
		break;
	case DECORATION_TYPE_SHADOW:
		ensure_border_surfaces(frame_custom);
		draw_shadow(frame_custom);
		break;
	}
}

static enum decoration_type
window_state_to_decoration_type(enum libdecor_window_state window_state)
{
	if ((window_state & LIBDECOR_WINDOW_STATE_MAXIMIZED) ||
	    (window_state & LIBDECOR_WINDOW_STATE_FULLSCREEN))
		return DECORATION_TYPE_NONE;
	else
		return DECORATION_TYPE_SHADOW;
}

static void
libdecor_plugin_custom_frame_commit(struct libdecor_plugin *plugin,
				   struct libdecor_frame *frame,
				   struct libdecor_state *state,
				   struct libdecor_configuration *configuration)
{
	struct libdecor_frame_custom *frame_custom =
		(struct libdecor_frame_custom *) frame;
	enum libdecor_window_state new_window_state;
	int old_content_width, old_content_height;
	int new_content_width, new_content_height;
	enum decoration_type old_decoration_type;
	enum decoration_type new_decoration_type;

	new_window_state = libdecor_frame_get_window_state(frame);

	old_content_width = frame_custom->content_width;
	old_content_height = frame_custom->content_height;
	new_content_width = libdecor_frame_get_content_width(frame);
	new_content_height = libdecor_frame_get_content_height(frame);

	old_decoration_type = frame_custom->decoration_type;
	new_decoration_type = window_state_to_decoration_type(new_window_state);

	if (old_decoration_type == new_decoration_type &&
	    old_content_width == new_content_width &&
	    old_content_height == new_content_height)
		return;

	frame_custom->content_width = new_content_width;
	frame_custom->content_height = new_content_height;
	frame_custom->decoration_type = new_decoration_type;

	draw_decoration(frame_custom);
	libdecor_frame_set_window_geometry(&frame_custom->frame, 0, 0,
					   frame_custom->content_width,
					   frame_custom->content_height);
}

static bool
libdecor_plugin_custom_configuration_get_content_size(struct libdecor_plugin *plugin,
						     struct libdecor_configuration *configuration,
						     struct libdecor_frame *frame,
						     int *content_width,
						     int *content_height)
{
	return libdecor_configuration_get_window_size(configuration,
						      content_width,
						      content_height);
}

static struct libdecor_plugin_interface custom_plugin_iface = {
	.destroy = libdecor_plugin_custom_destroy,

	.frame_new = libdecor_plugin_custom_frame_new,
	.frame_free = libdecor_plugin_custom_frame_free,
	.frame_commit = libdecor_plugin_custom_frame_commit,

	.configuration_get_content_size = libdecor_plugin_custom_configuration_get_content_size,
};

static void
ensure_cursor_theme(struct libdecor_plugin_custom *plugin_custom)
{
	plugin_custom->cursors.top_side =
		wl_cursor_theme_get_cursor(cursor_theme, "top_side");
	plugin_custom->cursors.right_side =
		wl_cursor_theme_get_cursor(cursor_theme, "right_side");
	plugin_custom->cursors.bottom_side =
		wl_cursor_theme_get_cursor(cursor_theme, "bottom_side");
	plugin_custom->cursors.left_side =
		wl_cursor_theme_get_cursor(cursor_theme, "left_side");
}

static void
custom_pointer_enter(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface,
	      wl_fixed_t surface_x,
	      wl_fixed_t surface_y)
{
	struct libdecor_plugin_custom *plugin_custom = data;
	struct libdecor_frame_custom *frame_custom;
	struct wl_cursor *wl_cursor;
	struct wl_cursor_image *image;
	struct wl_buffer *buffer;
	const char * const *tag;

	tag = wl_proxy_get_tag((struct wl_proxy *) surface);
	if (tag != &plugin_proxy_tag)
		return;

	cursor_surface = wl_compositor_create_surface(wl_compositor);

	ensure_cursor_theme(plugin_custom);

	pointer_focus = surface;

	frame_custom = wl_surface_get_user_data(pointer_focus);
	if (!frame_custom)
		return;

	if (pointer_focus == frame_custom->border.top.wl_surface)
		wl_cursor = plugin_custom->cursors.top_side;
	else if (pointer_focus == frame_custom->border.right.wl_surface)
		wl_cursor = plugin_custom->cursors.right_side;
	else if (pointer_focus == frame_custom->border.bottom.wl_surface)
		wl_cursor = plugin_custom->cursors.bottom_side;
	else if (pointer_focus == frame_custom->border.left.wl_surface)
		wl_cursor = plugin_custom->cursors.left_side;
	else
		return;

	image = wl_cursor->images[0];
	buffer = wl_cursor_image_get_buffer(image);
	wl_pointer_set_cursor(wl_pointer, serial,
			      cursor_surface,
			      image->hotspot_x,
			      image->hotspot_y);
	wl_surface_attach(cursor_surface, buffer, 0, 0);
	wl_surface_damage(cursor_surface, 0, 0,
			  image->width, image->height);
	wl_surface_commit(cursor_surface);
}

static void
custom_pointer_leave(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface)
{
	pointer_focus = NULL;
}

static void
custom_pointer_motion(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t time,
	       wl_fixed_t surface_x,
	       wl_fixed_t surface_y)
{
}

static void
custom_pointer_button(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t serial,
	       uint32_t time,
	       uint32_t button,
	       uint32_t state)
{
	struct libdecor_frame_custom *frame_custom;

	if (!pointer_focus)
		return;

	frame_custom = wl_surface_get_user_data(pointer_focus);
	if (!frame_custom)
		return;

	if (button == BTN_LEFT && state) {
		enum libdecor_resize_edge edge;

		if (pointer_focus == frame_custom->border.top.wl_surface)
			edge = LIBDECOR_RESIZE_EDGE_TOP;
		else if (pointer_focus == frame_custom->border.right.wl_surface)
			edge = LIBDECOR_RESIZE_EDGE_RIGHT;
		else if (pointer_focus == frame_custom->border.bottom.wl_surface)
			edge = LIBDECOR_RESIZE_EDGE_BOTTOM;
		else if (pointer_focus == frame_custom->border.left.wl_surface)
			edge = LIBDECOR_RESIZE_EDGE_LEFT;
		else
			return;

		libdecor_frame_resize(&frame_custom->frame,
				      wl_seat,
				      serial,
				      edge);
	}
}

static void
custom_pointer_axis(void *data,
	     struct wl_pointer *wl_pointer,
	     uint32_t time,
	     uint32_t axis,
	     wl_fixed_t value)
{
}

static struct wl_pointer_listener custom_pointer_listener = {
	custom_pointer_enter,
	custom_pointer_leave,
	custom_pointer_motion,
	custom_pointer_button,
	custom_pointer_axis
};

static void
custom_registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	struct libdecor_plugin_custom *plugin_custom = user_data;

	if (strcmp(interface, "wl_subcompositor") == 0) {
		plugin_custom->wl_subcompositor =
			wl_registry_bind(plugin_custom->wl_registry,
					 id, &wl_subcompositor_interface, 1);
	}
}

static void
custom_registry_handle_global_remove(void *user_data,
			      struct wl_registry *wl_registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener custom_registry_listener = {
	custom_registry_handle_global,
	custom_registry_handle_global_remove
};

static struct libdecor_plugin *
custom_plugin_new(struct libdecor *context)
{
	struct libdecor_plugin_custom *plugin_custom;
	struct wl_display *wl_display;

	plugin_custom = zalloc(sizeof *plugin_custom);
	plugin_custom->plugin.iface = &custom_plugin_iface;
	plugin_custom->context = context;

	wl_display = libdecor_get_wl_display(context);
	plugin_custom->wl_registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(plugin_custom->wl_registry,
				 &custom_registry_listener,
				 plugin_custom);

	plugin_custom->wl_pointer = wl_seat_get_pointer(wl_seat);
	wl_pointer_add_listener(plugin_custom->wl_pointer,
				&custom_pointer_listener, plugin_custom);

	libdecor_notify_plugin_ready(context);

	return &plugin_custom->plugin;
}
