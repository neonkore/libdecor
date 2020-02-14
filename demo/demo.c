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

#define DEFAULT_WIDTH 800
#define DEFAULT_HEIGHT 600

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
	enum libdecor_window_state window_state;
};

static struct wl_compositor *wl_compositor;
static struct wl_shm *wl_shm;
static struct wl_seat *wl_seat;
static struct wl_pointer *wl_pointer;
static struct wl_cursor_theme *cursor_theme;
static struct wl_cursor *left_ptr_cursor;
static struct wl_surface *cursor_surface;

static bool has_xrgb = false;

static struct window *window;

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
pointer_enter(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface,
	      wl_fixed_t surface_x,
	      wl_fixed_t surface_y)
{
	struct wl_cursor *wl_cursor;
	struct wl_cursor_image *image;
	struct wl_buffer *buffer;

	if (surface != window->wl_surface)
		return;

	wl_cursor = left_ptr_cursor;

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
pointer_leave(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface)
{
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
	if (button == BTN_LEFT && state) {
		libdecor_frame_move(window->frame, wl_seat, serial);
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
	if (capabilities & WL_SEAT_CAPABILITY_POINTER &&
	    !wl_pointer) {
		wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(wl_pointer, &pointer_listener, NULL);
	} else if (!(capabilities & WL_SEAT_CAPABILITY_POINTER) &&
		   wl_pointer) {
		wl_pointer_release(wl_pointer);
		wl_pointer = NULL;
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
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
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
		wl_seat = wl_registry_bind(wl_registry,
					   id, &wl_seat_interface, 1);
		wl_seat_add_listener(wl_seat, &seat_listener, NULL);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
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
	static const char template[] = "/libdecor-demo-shared-XXXXXX";
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
	     enum libdecor_window_state window_state)
{
	uint32_t *pixels = buffer->data;
	uint32_t color;
	int i;

	if (window_state & LIBDECOR_WINDOW_STATE_ACTIVE)
		color = 0xffbcbcbc;
	else
		color = 0xff8e8e8e;

	for (i = 0; i < width * height; i++)
		pixels[i] = color;
}

static void
handle_configure(struct libdecor_frame *frame,
		 struct libdecor_configuration *configuration,
		 void *user_data)
{
	struct window *window = user_data;
	struct buffer *buffer;
	int width, height;
	enum libdecor_window_state window_state;
	struct libdecor_state *state;

	if (!libdecor_configuration_get_content_size(configuration, frame,
						     &width, &height)) {
		width = window->content_width;
		height = window->content_height;
	}

	if (width == 0 || height == 0) {
		width = DEFAULT_WIDTH;
		height = DEFAULT_HEIGHT;
	}

	if (!libdecor_configuration_get_window_state(configuration, &window_state))
		window_state = LIBDECOR_WINDOW_STATE_NONE;

	buffer = create_shm_buffer(width, height, WL_SHM_FORMAT_XRGB8888);
	paint_buffer(buffer, width, height, window_state);

	state = libdecor_state_new(width, height);
	libdecor_frame_commit(frame, state, configuration);
	libdecor_state_free(state);

	wl_surface_attach(window->wl_surface, buffer->wl_buffer, 0, 0);
	wl_surface_damage(window->wl_surface, 0, 0, width, height);
	wl_surface_commit(window->wl_surface);
}

static void
handle_close(struct libdecor_frame *frame,
	     void *user_data)
{
	exit(EXIT_SUCCESS);
}

static struct libdecor_frame_interface libdecor_frame_iface = {
	handle_configure,
	handle_close,
};

static void
init_cursors(void)
{
	cursor_theme = wl_cursor_theme_load(NULL, 24, wl_shm);
	left_ptr_cursor = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
	cursor_surface = wl_compositor_create_surface(wl_compositor);
}

int
main(int argc,
     char **argv)
{
	struct wl_display *wl_display;
	struct wl_registry *wl_registry;
	struct libdecor *context;
	int ret = 0;

	wl_display = wl_display_connect(NULL);
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

	init_cursors();

	window = zalloc(sizeof *window);
	window->wl_surface = wl_compositor_create_surface(wl_compositor);

	context = libdecor_new(wl_display, &libdecor_iface);
	window->frame = libdecor_decorate(context, window->wl_surface,
					  &libdecor_frame_iface, window);
	libdecor_frame_set_app_id(window->frame, "libdecoration-demo");
	libdecor_frame_set_title(window->frame, "libdecoration demo");
	libdecor_frame_map(window->frame);

	while (ret != -1)
		ret = wl_display_dispatch(wl_display);
	
	return EXIT_SUCCESS;
}
