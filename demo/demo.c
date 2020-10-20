/*
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010 Intel Corporation
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

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-cursor.h>

#include "libdecoration.h"
#include "utils.h"
#include "cursor-settings.h"

static const size_t chk = 16;
static const int DEFAULT_WIDTH = 30*chk;
static const int DEFAULT_HEIGHT = 20*chk;

static const char *proxy_tag = "libdecoration-demo";

static bool
own_proxy(struct wl_proxy *proxy)
{
	return (wl_proxy_get_tag(proxy) == &proxy_tag);
}

static bool
own_output(struct wl_output *output)
{
	return own_proxy((struct wl_proxy *) output);
}

struct buffer {
	struct wl_buffer *wl_buffer;
	void *data;
	size_t data_size;
};

struct window {
	struct wl_surface *wl_surface;
	struct buffer *buffer;
	struct libdecor_frame *frame;
	int content_width;
	int content_height;
	int configured_width;
	int configured_height;
	enum libdecor_window_state window_state;
	struct wl_list outputs;
	int scale;
};

struct seat {
	struct wl_seat *wl_seat;
	struct wl_pointer *wl_pointer;
	struct wl_list link;
	struct wl_list pointer_outputs;
	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *left_ptr_cursor;
	struct wl_surface *cursor_surface;
	struct wl_surface *pointer_focus;
	int pointer_scale;
	uint32_t serial;
};

struct output {
	uint32_t id;
	struct wl_output *wl_output;
	int scale;
	struct wl_list link;
};

struct window_output {
	struct output* output;
	struct wl_list link;
};

struct pointer_output {
	struct output* output;
	struct wl_list link;
};

static struct wl_compositor *wl_compositor;
static struct wl_shm *wl_shm;
static struct wl_list seats;
static struct wl_list outputs;

static bool has_xrgb = false;

static struct window *window;

static void
redraw(struct window *window);

static void
update_scale(struct window *window)
{
	int scale = 1;
	struct window_output *window_output;

	wl_list_for_each(window_output, &window->outputs, link) {
		scale = MAX(scale, window_output->output->scale);
	}
	if (scale != window->scale) {
		window->scale = scale;
		redraw(window);
	}
}

static void
shm_format(void *data,
	   struct wl_shm *wl_shm,
	   uint32_t format)
{
	if (format == WL_SHM_FORMAT_XRGB8888)
		has_xrgb = true;
}

static struct wl_shm_listener shm_listener = {
	shm_format
};

static void
try_update_cursor(struct seat *seat);

static void
cursor_surface_enter(void *data,
	      struct wl_surface *wl_surface,
	      struct wl_output *wl_output)
{
	struct seat *seat = data;
	struct pointer_output *pointer_output;

	if (!own_output(wl_output))
		return;

	pointer_output = zalloc(sizeof *pointer_output);
	pointer_output->output = wl_output_get_user_data(wl_output);
	wl_list_insert(&seat->pointer_outputs, &pointer_output->link);
	try_update_cursor(seat);
}

static void
cursor_surface_leave(void *data,
	      struct wl_surface *wl_surface,
	      struct wl_output *wl_output)
{
	struct seat *seat = data;
	struct pointer_output *pointer_output, *tmp;

	wl_list_for_each_safe(pointer_output, tmp, &seat->pointer_outputs, link) {
		if (pointer_output->output->wl_output == wl_output) {
			wl_list_remove(&pointer_output->link);
			free(pointer_output);
		}
	}
}

static struct wl_surface_listener cursor_surface_listener = {
	cursor_surface_enter,
	cursor_surface_leave,
};

static void
init_cursors(struct seat *seat)
{
	char *name;
	int size;
	struct wl_cursor_theme *theme;

	if (!libdecor_get_cursor_settings(&name, &size)) {
		name = NULL;
		size = 24;
	}
	size *= seat->pointer_scale;

	theme = wl_cursor_theme_load(name, size, wl_shm);
	free(name);
	if (theme != NULL) {
		if (seat->cursor_theme)
			wl_cursor_theme_destroy(seat->cursor_theme);
		seat->cursor_theme = theme;
	}
	if (seat->cursor_theme)
		seat->left_ptr_cursor
		  = wl_cursor_theme_get_cursor(seat->cursor_theme, "left_ptr");
	if (!seat->cursor_surface) {
		seat->cursor_surface = wl_compositor_create_surface(
								wl_compositor);
		wl_surface_add_listener(seat->cursor_surface,
					&cursor_surface_listener, seat);
	}
}

static void
set_cursor(struct seat *seat)
{
	struct wl_cursor *wl_cursor;
	struct wl_cursor_image *image;
	struct wl_buffer *buffer;
	const int scale = seat->pointer_scale;

	if (!seat->cursor_theme)
		return;

	wl_cursor = seat->left_ptr_cursor;

	image = wl_cursor->images[0];
	buffer = wl_cursor_image_get_buffer(image);
	wl_pointer_set_cursor(seat->wl_pointer, seat->serial,
			      seat->cursor_surface,
			      image->hotspot_x / scale,
			      image->hotspot_y / scale);
	wl_surface_attach(seat->cursor_surface, buffer, 0, 0);
	wl_surface_set_buffer_scale(seat->cursor_surface, scale);
	wl_surface_damage_buffer(seat->cursor_surface, 0, 0,
				 image->width, image->height);
	wl_surface_commit(seat->cursor_surface);
}

static void
try_update_cursor(struct seat *seat)
{
	struct pointer_output *pointer_output;
	int scale = 1;

	wl_list_for_each(pointer_output, &seat->pointer_outputs, link) {
		scale = MAX(scale, pointer_output->output->scale);
	}

	if (scale != seat->pointer_scale) {
		seat->pointer_scale = scale;
		init_cursors(seat);
		set_cursor(seat);
	}
}

static void
pointer_enter(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface,
	      wl_fixed_t surface_x,
	      wl_fixed_t surface_y)
{
	struct seat *seat = data;

	seat->pointer_focus = surface;
	seat->serial = serial;

	if (surface != window->wl_surface)
		return;

	set_cursor(seat);
}

static void
pointer_leave(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface)
{
	struct seat *seat = data;
	if (seat->pointer_focus == surface)
		seat->pointer_focus = NULL;
}

static void
pointer_motion(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t time,
	       wl_fixed_t surface_x,
	       wl_fixed_t surface_y)
{
}

static void
pointer_button(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t serial,
	       uint32_t time,
	       uint32_t button,
	       uint32_t state)
{
	struct seat *seat = data;
	if (button == BTN_LEFT &&
	    state == WL_POINTER_BUTTON_STATE_PRESSED &&
	    seat->pointer_focus == window->wl_surface) {
		libdecor_frame_move(window->frame, seat->wl_seat, serial);
	}
}

static void
pointer_axis(void *data,
	     struct wl_pointer *wl_pointer,
	     uint32_t time,
	     uint32_t axis,
	     wl_fixed_t value)
{
}

static struct wl_pointer_listener pointer_listener = {
	pointer_enter,
	pointer_leave,
	pointer_motion,
	pointer_button,
	pointer_axis
};

static void
seat_capabilities(void *data,
		  struct wl_seat *wl_seat,
		  uint32_t capabilities)
{
	struct seat *seat = data;
	if (capabilities & WL_SEAT_CAPABILITY_POINTER &&
	    !seat->wl_pointer) {
		seat->wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->wl_pointer, &pointer_listener,
					seat);
		seat->pointer_scale = 1;
		init_cursors(seat);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) &&
		   seat->wl_pointer) {
		wl_pointer_release(seat->wl_pointer);
		seat->wl_pointer = NULL;
	}
}

static void
seat_name(void *data,
	  struct wl_seat *wl_seat,
	  const char *name)
{
}

static struct wl_seat_listener seat_listener = {
	seat_capabilities,
	seat_name
};

static void
output_geometry(void *data,
		struct wl_output *wl_output,
		int32_t x,
		int32_t y,
		int32_t physical_width,
		int32_t physical_height,
		int32_t subpixel,
		const char *make,
		const char *model,
		int32_t transform)
{
}

static void
output_mode(void *data,
	    struct wl_output *wl_output,
	    uint32_t flags,
	    int32_t width,
	    int32_t height,
	    int32_t refresh)
{
}

static void
output_done(void *data,
	    struct wl_output *wl_output)
{
	struct output *output = data;
	struct seat *seat;

	if (window) {
		if (output->scale != window->scale)
			update_scale(window);
	}

	wl_list_for_each(seat, &seats, link) {
		try_update_cursor(seat);
	}
}

static void
output_scale(void *data,
	     struct wl_output *wl_output,
	     int32_t factor)
{
	struct output *output = data;

	output->scale = factor;
}

static struct wl_output_listener output_listener = {
	output_geometry,
	output_mode,
	output_done,
	output_scale
};

static void
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	struct seat *seat;
	struct output *output;

	if (strcmp(interface, "wl_compositor") == 0) {
		if (version < 4) {
			fprintf(stderr, "wl_compositor version >= 4 required");
			exit(EXIT_FAILURE);
		}
		wl_compositor =
			wl_registry_bind(wl_registry,
					 id, &wl_compositor_interface, 4);
	} else if (strcmp(interface, "wl_shm") == 0) {
		wl_shm = wl_registry_bind(wl_registry,
					  id, &wl_shm_interface, 1);
		wl_shm_add_listener(wl_shm, &shm_listener, NULL);
	} else if (strcmp(interface, "wl_seat") == 0) {
		if (version < 3) {
			fprintf(stderr, "%s version 3 required but only version "
					"%i is available\n", interface, version);
			exit(EXIT_FAILURE);
		}
		seat = zalloc(sizeof *seat);
		wl_list_init(&seat->pointer_outputs);
		seat->wl_seat = wl_registry_bind(wl_registry,
						 id, &wl_seat_interface, 3);
		wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
	} else if (strcmp(interface, "wl_output") == 0) {
		if (version < 2) {
			fprintf(stderr, "%s version 3 required but only version "
					"%i is available\n", interface, version);
			exit(EXIT_FAILURE);
		}
		output = zalloc(sizeof *output);
		output->id = id;
		output->scale = 1;
		output->wl_output = wl_registry_bind(wl_registry,
						     id, &wl_output_interface,
						     2);
		wl_proxy_set_tag((struct wl_proxy *) output->wl_output,
				 &proxy_tag);
		wl_output_add_listener(output->wl_output, &output_listener,
				       output);
		wl_list_insert(&outputs, &output->link);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
	struct output *output;
	struct window_output *window_output;

	wl_list_for_each(output, &outputs, link) {
		if (output->id == name) {
			wl_list_for_each(window_output, &window->outputs,
					 link) {
				if (window_output->output == output) {
					wl_list_remove(&window_output->link);
					free(window_output);
				}
			}
			wl_list_remove(&output->link);
			wl_output_destroy(output->wl_output);
			free(output);
			break;
		}
	}
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static void
handle_error(struct libdecor *context,
	     enum libdecor_error error,
	     const char *message)
{
	fprintf(stderr, "Caught error (%d): %s\n", error, message);
	exit(EXIT_FAILURE);
}

static struct libdecor_interface libdecor_iface = {
	.error = handle_error,
};

static void
buffer_release(void *user_data,
	       struct wl_buffer *wl_buffer)
{
	struct buffer *buffer = user_data;

	wl_buffer_destroy(buffer->wl_buffer);
	munmap(buffer->data, buffer->data_size);
	free(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
	buffer_release
};

static int
create_anonymous_file(off_t size)
{
	int fd;

	int ret;

	fd = memfd_create("libdecor-demo", MFD_CLOEXEC | MFD_ALLOW_SEALING);

	if (fd < 0)
		return -1;

	fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);

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

static struct buffer *
create_shm_buffer(int width,
		  int height,
		  uint32_t format)
{
	struct wl_shm_pool *pool;
	int fd, size, stride;
	void *data;
	struct buffer *buffer;

	stride = width * 4;
	size = stride * height;

	fd = create_anonymous_file(size);
	if (fd < 0) {
		fprintf(stderr, "creating a buffer file for %d B failed: %s\n",
			size, strerror(errno));
		return NULL;
	}

	data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "mmap failed: %s\n", strerror(errno));
		close(fd);
		return NULL;
	}

	buffer = zalloc(sizeof *buffer);

	pool = wl_shm_create_pool(wl_shm, fd, size);
	buffer->wl_buffer = wl_shm_pool_create_buffer(pool, 0,
						      width, height,
						      stride, format);
	wl_buffer_add_listener(buffer->wl_buffer, &buffer_listener, buffer);
	wl_shm_pool_destroy(pool);
	close(fd);

	buffer->data = data;
	buffer->data_size = size;

	return buffer;
}

static void
paint_buffer(struct buffer *buffer,
	     int width,
	     int height,
	     int scale,
	     enum libdecor_window_state window_state)
{
	uint32_t *pixels = buffer->data;
	uint32_t bg, fg, color;
	int y, x, sx, sy;
	size_t off;
	int stride = width * scale;

	if (window_state & LIBDECOR_WINDOW_STATE_ACTIVE) {
		fg = 0xffbcbcbc;
		bg = 0xff8e8e8e;
	} else {
		fg = 0xff8e8e8e;
		bg = 0xff484848;
	}

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			color = (x & chk) ^ (y & chk) ? fg : bg;
			for (sx = 0; sx < scale; sx++) {
				for (sy = 0; sy < scale; sy++) {
					off = x * scale + sx
					      + (y * scale + sy) * stride;
					pixels[off] = color;
				}
			}
		}
	}
}

static void
redraw(struct window *window)
{
	struct buffer *buffer;

	buffer = create_shm_buffer(window->configured_width * window->scale,
				   window->configured_height * window->scale,
				   WL_SHM_FORMAT_XRGB8888);
	paint_buffer(buffer, window->configured_width,
		     window->configured_height, window->scale,
		     window->window_state);

	wl_surface_attach(window->wl_surface, buffer->wl_buffer, 0, 0);
	wl_surface_set_buffer_scale(window->wl_surface, window->scale);
	wl_surface_damage_buffer(window->wl_surface, 0, 0,
				 window->configured_width * window->scale,
				 window->configured_height * window->scale);
	wl_surface_commit(window->wl_surface);
}

static void
handle_configure(struct libdecor_frame *frame,
		 struct libdecor_configuration *configuration,
		 void *user_data)
{
	struct window *window = user_data;
	int width, height;
	enum libdecor_window_state window_state;
	struct libdecor_state *state;

	if (!libdecor_configuration_get_content_size(configuration, frame,
						     &width, &height)) {
		width = window->content_width;
		height = window->content_height;
	}

	width = (width == 0) ? DEFAULT_WIDTH : width;
	height = (height == 0) ? DEFAULT_HEIGHT : height;

	window->configured_width = width;
	window->configured_height = height;

	if (!libdecor_configuration_get_window_state(configuration,
						     &window_state))
		window_state = LIBDECOR_WINDOW_STATE_NONE;

	window->window_state = window_state;

	state = libdecor_state_new(width, height);
	libdecor_frame_commit(frame, state, configuration);
	libdecor_state_free(state);

	redraw(window);
}

static void
handle_close(struct libdecor_frame *frame,
	     void *user_data)
{
	exit(EXIT_SUCCESS);
}

static void
handle_commit(void *user_data)
{
	wl_surface_commit(window->wl_surface);
}

static struct libdecor_frame_interface libdecor_frame_iface = {
	handle_configure,
	handle_close,
	handle_commit,
};

static void
surface_enter(void *data,
	      struct wl_surface *wl_surface,
	      struct wl_output *wl_output)
{
	struct window *window = data;
	struct output *output;
	struct window_output *window_output;

	if (!own_output(wl_output))
		return;

	output = wl_output_get_user_data(wl_output);

	if (output == NULL)
		return;

	window_output = zalloc(sizeof *window_output);
	window_output->output = output;
	wl_list_insert(&window->outputs, &window_output->link);
	update_scale(window);
}

static void
surface_leave(void *data,
	      struct wl_surface *wl_surface,
	      struct wl_output *wl_output)
{
	struct window *window = data;
	struct window_output *window_output;

	wl_list_for_each(window_output, &window->outputs, link) {
		if (window_output->output->wl_output == wl_output) {
			wl_list_remove(&window_output->link);
			free(window_output);
			update_scale(window);
			break;
		}
	}
}

static struct wl_surface_listener surface_listener = {
	surface_enter,
	surface_leave,
};

int
main(int argc,
     char **argv)
{
	struct wl_display *wl_display;
	struct wl_registry *wl_registry;
	struct libdecor *context;
	struct output *output;

	wl_display = wl_display_connect(NULL);
	if (!wl_display) {
		fprintf(stderr, "No Wayland connection\n");
		return EXIT_FAILURE;
	}

	wl_list_init(&seats);
	wl_list_init(&outputs);

	wl_registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(wl_registry,
				 &registry_listener,
				 NULL);
	wl_display_roundtrip(wl_display);
	wl_display_roundtrip(wl_display);
	if (!has_xrgb) {
		fprintf(stderr, "No XRGB shm format\n");
		return EXIT_FAILURE;
	}

	window = zalloc(sizeof *window);
	window->scale = 1;
	wl_list_for_each(output, &outputs, link) {
		window->scale = MAX(window->scale, output->scale);
	}
	wl_list_init(&window->outputs);
	window->wl_surface = wl_compositor_create_surface(wl_compositor);
	wl_surface_add_listener(window->wl_surface, &surface_listener, window);

	context = libdecor_new(wl_display, &libdecor_iface);
	window->frame = libdecor_decorate(context, window->wl_surface,
					  &libdecor_frame_iface, window);
	libdecor_frame_set_app_id(window->frame, "libdecoration-demo");
	libdecor_frame_set_title(window->frame, "libdecoration demo");
	libdecor_frame_map(window->frame);

	while (wl_display_dispatch(wl_display) != -1);

	free(window);

	return EXIT_SUCCESS;
}
