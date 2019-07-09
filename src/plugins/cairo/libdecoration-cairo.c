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

#include "config.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

#include "libdecoration-cairo.h"
#include "libdecoration-plugin.h"
#include "utils.h"

#include "xdg-shell-client-protocol.h"

#define BORDER_MARGIN 24
#define BORDER_RADIUS 3

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

struct buffer {
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
	struct buffer *buffer;
};

struct libdecor_frame_cairo {
	struct libdecor_frame frame;

	struct libdecor_plugin_cairo *plugin_cairo;

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

struct libdecor_plugin_cairo {
	struct libdecor_plugin plugin;

	struct wl_callback *globals_callback;

	struct libdecor *context;

	struct wl_registry *wl_registry;
	struct wl_subcompositor *wl_subcompositor;
	struct wl_compositor *wl_compositor;

	struct wl_shm *wl_shm;
	struct wl_callback *shm_callback;
	bool has_argb;
};

struct libdecor_plugin *
libdecor_plugin_new(struct libdecor *context);

static void
buffer_free(struct buffer *buffer);

static void
libdecor_plugin_cairo_destroy(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_cairo *plugin_cairo =
		(struct libdecor_plugin_cairo *) plugin;

	if (plugin_cairo->globals_callback)
		wl_callback_destroy(plugin_cairo->globals_callback);
	if (plugin_cairo->shm_callback)
		wl_callback_destroy(plugin_cairo->shm_callback);
	wl_registry_destroy(plugin_cairo->wl_registry);
}

static struct libdecor_frame *
libdecor_plugin_cairo_frame_new(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_cairo *plugin_cairo =
		(struct libdecor_plugin_cairo *) plugin;
	struct libdecor_frame_cairo *frame_cairo;

	frame_cairo = zalloc(sizeof *frame_cairo);
	frame_cairo->plugin_cairo = plugin_cairo;

	return &frame_cairo->frame;
}

static int
create_tmpfile_cloexec(char *tmpname)
{
	int fd;

	fd = mkostemp(tmpname, O_CLOEXEC);
	if (fd >= 0)
		unlink(tmpname);

	return fd;
}

static int
create_anonymous_file(off_t size)
{
	static const char template[] = "/libdecor-cairo-shared-XXXXXX";
	const char *path;
	char *name;
	int fd;
	int ret;

	path = getenv("XDG_RUNTIME_DIR");
	if (!path) {
		errno = ENOENT;
		return -1;
	}

	name = malloc(strlen(path) + sizeof(template));
	if (!name)
		return -1;

	strcpy(name, path);
	strcat(name, template);

	fd = create_tmpfile_cloexec(name);

	free(name);

	if (fd < 0)
		return -1;

	do {
		ret = posix_fallocate(fd, 0, size);
	} while (ret == EINTR);
	if (ret != 0) {
		close(fd);
		errno = ret;
		return -1;
	}

	return fd;
}

static void
buffer_release(void *user_data,
	       struct wl_buffer *wl_buffer)
{
	struct buffer *buffer = user_data;

	if (buffer->is_detached)
		buffer_free(buffer);
	else
		buffer->in_use = false;
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static struct buffer *
create_shm_buffer(struct libdecor_plugin_cairo *plugin_cairo,
		  int width,
		  int height)
{
	struct wl_shm_pool *pool;
	int fd, size, stride;
	void *data;
	struct buffer *buffer;

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

	pool = wl_shm_create_pool(plugin_cairo->wl_shm, fd, size);
	buffer = zalloc(sizeof *buffer);
	buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0,
						      width, height,
						      stride,
						      WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->data = data;
	buffer->data_size = size;
	buffer->width = width;
	buffer->height = height;

	return buffer;
}

static void
buffer_free(struct buffer *buffer)
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
libdecor_plugin_cairo_frame_free(struct libdecor_plugin *plugin,
				 struct libdecor_frame *frame)
{
	struct libdecor_frame_cairo *frame_cairo =
		(struct libdecor_frame_cairo *) frame;

	free_border_component(&frame_cairo->border.top);
	free_border_component(&frame_cairo->border.right);
	free_border_component(&frame_cairo->border.bottom);
	free_border_component(&frame_cairo->border.left);
}

static bool
is_border_surfaces_showing(struct libdecor_frame_cairo *frame_cairo)
{
	return frame_cairo->border.is_showing;
}

static void
hide_border_component(struct border_component *border_component)
{
	wl_surface_attach(border_component->wl_surface, NULL, 0, 0);
	wl_surface_commit(border_component->wl_surface);
}

static void
hide_border_surfaces(struct libdecor_frame_cairo *frame_cairo)
{
	hide_border_component(&frame_cairo->border.top);
	hide_border_component(&frame_cairo->border.right);
	hide_border_component(&frame_cairo->border.bottom);
	hide_border_component(&frame_cairo->border.left);
	frame_cairo->border.is_showing = false;
}

static void
create_surface_subsurface_pair(struct libdecor_plugin_cairo *plugin_cairo,
			       struct wl_surface *parent,
			       struct wl_surface **out_wl_surface,
			       struct wl_subsurface **out_wl_subsurface)
{
	struct wl_compositor *wl_compositor = plugin_cairo->wl_compositor;
	struct wl_subcompositor *wl_subcompositor = plugin_cairo->wl_subcompositor;
	struct wl_surface *wl_surface;
	struct wl_subsurface *wl_subsurface;

	wl_surface = wl_compositor_create_surface(wl_compositor);
	wl_subsurface = wl_subcompositor_get_subsurface(wl_subcompositor,
							wl_surface,
							parent);

	*out_wl_surface = wl_surface;
	*out_wl_subsurface = wl_subsurface;
}

static void
ensure_border_surfaces(struct libdecor_frame_cairo *frame_cairo)
{
	struct libdecor_plugin_cairo *plugin_cairo = frame_cairo->plugin_cairo;
	struct libdecor_frame *frame = &frame_cairo->frame;
	struct wl_surface *parent = libdecor_frame_get_wl_surface(frame);

	if (frame_cairo->border.top.wl_surface)
		return;

	create_surface_subsurface_pair(plugin_cairo,
				       parent,
				       &frame_cairo->border.top.wl_surface,
				       &frame_cairo->border.top.wl_subsurface);
	create_surface_subsurface_pair(plugin_cairo,
				       parent,
				       &frame_cairo->border.right.wl_surface,
				       &frame_cairo->border.right.wl_subsurface);
	create_surface_subsurface_pair(plugin_cairo,
				       parent,
				       &frame_cairo->border.bottom.wl_surface,
				       &frame_cairo->border.bottom.wl_subsurface);
	create_surface_subsurface_pair(plugin_cairo,
				       parent,
				       &frame_cairo->border.left.wl_surface,
				       &frame_cairo->border.left.wl_subsurface);
}

static void
calculate_component_size(struct libdecor_frame_cairo *frame_cairo,
			 enum border_side border_side,
			 int *component_x,
			 int *component_y,
			 int *component_width,
			 int *component_height)
{
	struct libdecor_frame *frame = &frame_cairo->frame;
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
draw_shadow_content(struct libdecor_plugin_cairo *plugin_cairo,
		    struct buffer *buffer,
		    enum border_side border_side)
{
	uint32_t *pixels = buffer->data;
	uint32_t color = 0x80303030;
	int i;

	for (i = 0; i < buffer->width * buffer->height; i++)
		pixels[i] = color;
}

static void
draw_shadow_component(struct libdecor_frame_cairo *frame_cairo,
		      struct border_component *border_component,
		      enum border_side border_side)
{
	struct libdecor_plugin_cairo *plugin_cairo = frame_cairo->plugin_cairo;
	struct buffer *old_buffer;
	struct buffer *buffer = NULL;
	int component_x;
	int component_y;
	int component_width;
	int component_height;

	calculate_component_size(frame_cairo, border_side,
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
		buffer = create_shm_buffer(plugin_cairo,
					   component_width,
					   component_height);
	}

	draw_shadow_content(plugin_cairo, buffer, border_side);

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
draw_shadow(struct libdecor_frame_cairo *frame_cairo)
{
	draw_shadow_component(frame_cairo,
			      &frame_cairo->border.top,
			      BORDER_SIDE_TOP);
	draw_shadow_component(frame_cairo,
			      &frame_cairo->border.right,
			      BORDER_SIDE_RIGHT);
	draw_shadow_component(frame_cairo,
			      &frame_cairo->border.bottom,
			      BORDER_SIDE_BOTTOM);
	draw_shadow_component(frame_cairo,
			      &frame_cairo->border.left,
			      BORDER_SIDE_LEFT);
	frame_cairo->border.is_showing = true;
}

static void
draw_decoration(struct libdecor_frame_cairo *frame_cairo)
{
	switch (frame_cairo->decoration_type) {
	case DECORATION_TYPE_NONE:
		if (is_border_surfaces_showing(frame_cairo))
			hide_border_surfaces(frame_cairo);
		break;
	case DECORATION_TYPE_SHADOW:
		ensure_border_surfaces(frame_cairo);
		draw_shadow(frame_cairo);
		break;
	}
}

static void
set_window_geometry(struct libdecor_frame_cairo *frame_cairo)
{
	struct libdecor_frame *frame = &frame_cairo->frame;
	struct xdg_surface *xdg_surface;
	int x, y, width, height;

	switch (frame_cairo->decoration_type) {
	case DECORATION_TYPE_NONE:
	case DECORATION_TYPE_SHADOW:
		x = 0;
		y = 0;
		width = frame_cairo->content_width;
		height = frame_cairo->content_height;
		break;
	}

	xdg_surface = libdecor_frame_get_xdg_surface(frame);
	xdg_surface_set_window_geometry(xdg_surface, x, y, width, height);
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
libdecor_plugin_cairo_frame_commit(struct libdecor_plugin *plugin,
				   struct libdecor_frame *frame,
				   struct libdecor_state *state,
				   struct libdecor_configuration *configuration)
{
	struct libdecor_frame_cairo *frame_cairo =
		(struct libdecor_frame_cairo *) frame;
	enum libdecor_window_state new_window_state;
	int old_content_width, old_content_height;
	int new_content_width, new_content_height;
	enum decoration_type old_decoration_type;
	enum decoration_type new_decoration_type;

	new_window_state = libdecor_frame_get_window_state(frame);

	old_content_width = frame_cairo->content_width;
	old_content_height = frame_cairo->content_height;
	new_content_width = libdecor_frame_get_content_width(frame);
	new_content_height = libdecor_frame_get_content_height(frame);

	old_decoration_type = frame_cairo->decoration_type;
	new_decoration_type = window_state_to_decoration_type(new_window_state);

	if (old_decoration_type == new_decoration_type &&
	    old_content_width == new_content_width &&
	    old_content_height == new_content_height)
		return;

	frame_cairo->content_width = new_content_width;
	frame_cairo->content_height = new_content_height;
	frame_cairo->decoration_type = new_decoration_type;

	draw_decoration(frame_cairo);
	set_window_geometry(frame_cairo);
}

static bool
libdecor_plugin_cairo_configuration_get_content_size(struct libdecor_plugin *plugin,
						     struct libdecor_configuration *configuration,
						     struct libdecor_frame *frame,
						     int *content_width,
						     int *content_height)
{
	return libdecor_configuration_get_window_size(configuration,
						      content_width,
						      content_height);
}

static struct libdecor_plugin_interface cairo_plugin_iface = {
	.destroy = libdecor_plugin_cairo_destroy,

	.frame_new = libdecor_plugin_cairo_frame_new,
	.frame_free = libdecor_plugin_cairo_frame_free,
	.frame_commit = libdecor_plugin_cairo_frame_commit,

	.configuration_get_content_size = libdecor_plugin_cairo_configuration_get_content_size,
};

static void
init_wl_compositor(struct libdecor_plugin_cairo *plugin_cairo,
		   uint32_t id,
		   uint32_t version)
{
	plugin_cairo->wl_compositor =
		wl_registry_bind(plugin_cairo->wl_registry,
				 id, &wl_compositor_interface,
				 MIN(version, 4));
}

static void
init_wl_subcompositor(struct libdecor_plugin_cairo *plugin_cairo,
		      uint32_t id,
		      uint32_t version)
{
	plugin_cairo->wl_subcompositor =
		wl_registry_bind(plugin_cairo->wl_registry,
				 id, &wl_subcompositor_interface, 1);
}

static void
shm_format(void *user_data,
	   struct wl_shm *wl_shm,
	   uint32_t format)
{
	struct libdecor_plugin_cairo *plugin_cairo = user_data;

	if (format == WL_SHM_FORMAT_ARGB8888)
		plugin_cairo->has_argb = true;
}

struct wl_shm_listener shm_listener = {
	shm_format
};

static void
shm_callback(void *user_data,
		 struct wl_callback *callback,
		 uint32_t time)
{
	struct libdecor_plugin_cairo *plugin_cairo = user_data;
	struct libdecor *context = plugin_cairo->context;

	if (!plugin_cairo->has_argb) {
		libdecor_notify_plugin_error(context,
					     LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
					     "Compositor is missing required shm format");
		return;
	}

	libdecor_notify_plugin_ready(context);
}

static const struct wl_callback_listener shm_callback_listener = {
	shm_callback
};

static void
init_wl_shm(struct libdecor_plugin_cairo *plugin_cairo,
	    uint32_t id,
	    uint32_t version)
{
	struct libdecor *context = plugin_cairo->context;
	struct wl_display *wl_display = libdecor_get_wl_display(context);

	plugin_cairo->wl_shm =
		wl_registry_bind(plugin_cairo->wl_registry,
				 id, &wl_shm_interface, 1);
	wl_shm_add_listener(plugin_cairo->wl_shm, &shm_listener, plugin_cairo);

	plugin_cairo->globals_callback = wl_display_sync(wl_display);
	wl_callback_add_listener(plugin_cairo->globals_callback,
				 &shm_callback_listener,
				 plugin_cairo);
}

static void
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	struct libdecor_plugin_cairo *plugin_cairo = user_data;

	if (strcmp(interface, "wl_compositor") == 0)
		init_wl_compositor(plugin_cairo, id, version);
	else if (strcmp(interface, "wl_subcompositor") == 0)
		init_wl_subcompositor(plugin_cairo, id, version);
	else if (strcmp(interface, "wl_shm") == 0)
		init_wl_shm(plugin_cairo, id, version);
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
has_required_globals(struct libdecor_plugin_cairo *plugin_cairo)
{
	if (!plugin_cairo->wl_compositor)
		return false;
	if (!plugin_cairo->wl_subcompositor)
		return false;
	if (!plugin_cairo->wl_shm)
		return false;

	return true;
}

static void
globals_callback(void *user_data,
		 struct wl_callback *callback,
		 uint32_t time)
{
	struct libdecor_plugin_cairo *plugin_cairo = user_data;

	if (!has_required_globals(plugin_cairo)) {
		struct libdecor *context = plugin_cairo->context;

		libdecor_notify_plugin_error(context,
					     LIBDECOR_ERROR_COMPOSITOR_INCOMPATIBLE,
					     "Compositor is missing required globals");
	}
}

static const struct wl_callback_listener globals_callback_listener = {
	globals_callback
};

LIBDECOR_EXPORT struct libdecor_plugin *
libdecor_plugin_new(struct libdecor *context)
{
	struct libdecor_plugin_cairo *plugin_cairo;
	struct wl_display *wl_display;

	plugin_cairo = zalloc(sizeof *plugin_cairo);
	plugin_cairo->plugin.iface = &cairo_plugin_iface;
	plugin_cairo->context = context;

	wl_display = libdecor_get_wl_display(context);
	plugin_cairo->wl_registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(plugin_cairo->wl_registry,
				 &registry_listener,
				 plugin_cairo);

	plugin_cairo->globals_callback = wl_display_sync(wl_display);
	wl_callback_add_listener(plugin_cairo->globals_callback,
				 &globals_callback_listener,
				 plugin_cairo);

	return &plugin_cairo->plugin;
}
