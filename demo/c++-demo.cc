/*
 * Copyright © 2011 Benjamin Franzke
 * Copyright © 2010 Intel Corporation
 * Copyright © 2018-2021 Jonas Ådahl
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
#include <xkbcommon/xkbcommon.h>
#include <iostream>

#include "libdecor.h"
#include "utils.h"
#include "cursor-settings.h"
extern "C" {
#include "os-compatibility.h"
}

#include "xdg-shell-client-protocol.h"

static const int DEFAULT_WIDTH = 400;
static const int DEFAULT_HEIGHT = 400;

static struct wl_compositor *wl_compositor;
static struct wl_shm *wl_shm;

static bool has_xrgb = false;

using std::cerr;
using std::endl;

class Buffer {
public:
	Buffer(struct wl_buffer *wl_buffer,
	       int width,
	       int height,
	       void *data,
	       size_t data_size) :
		wl_buffer(wl_buffer),
		width(width),
		height(height),
		data(data),
		data_size(data_size)
	{
		wl_buffer_add_listener(this->wl_buffer, &this->buffer_listener, this);
		this->buffer_listener.release = buffer_release;
	}

	virtual ~Buffer()
	{
		wl_buffer_destroy(this->wl_buffer);
		munmap(this->data, this->data_size);
	}

	static Buffer * create_shm_buffer(int width,
					  int height,
					  uint32_t format)
	{
		struct wl_shm_pool *pool;
		int fd, size, stride;
		void *data;
		struct wl_buffer *wl_buffer;

		stride = width * 4;
		size = stride * height;

		fd = os_create_anonymous_file(size);
		if (fd < 0) {
			cerr << "Creating a buffer file for " << size <<
				" B failed: " << strerror(errno) << endl;
			return NULL;
		}

		data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (data == MAP_FAILED) {
			cerr << "mmap failed: " << strerror(errno) << endl;
			close(fd);
			return NULL;
		}

		pool = wl_shm_create_pool(wl_shm, fd, size);


		wl_buffer = wl_shm_pool_create_buffer(pool, 0,
						      width, height,
						      stride, format);
		wl_shm_pool_destroy(pool);
		close(fd);

		return new Buffer(wl_buffer, width, height, data, size);
	}

	void paint_buffer(enum libdecor_window_state window_state)
	{
		uint32_t *pixels = reinterpret_cast<uint32_t *>(this->data);
		uint32_t color;
		int y, x;
		size_t off;

		if (window_state & LIBDECOR_WINDOW_STATE_ACTIVE) {
			color = 0xffbcbcbc;
		} else {
			color = 0xff8e8e8e;
		}

		for (y = 0; y < this->height; y++) {
			for (x = 0; x < this->width; x++) {
				off = x + y * this->width;
				pixels[off] = color;
			}
		}
	}

	struct wl_buffer * get_buffer()
	{
		return this->wl_buffer;
	}

private:
	static void buffer_release(void *user_data,
				   struct wl_buffer *wl_buffer)
	{
		Buffer *buffer = reinterpret_cast<Buffer *>(user_data);

		delete buffer;
	}

	struct wl_buffer *wl_buffer;
	struct wl_buffer_listener buffer_listener;
	int width;
	int height;
	void *data;
	size_t data_size;
};

class Window {
public:
	Window(struct libdecor *context,
	       struct wl_compositor *wl_compositor)
		: floating_width(DEFAULT_WIDTH), floating_height(DEFAULT_HEIGHT)
	{
		this->wl_surface = wl_compositor_create_surface(wl_compositor);

		this->libdecor_frame_iface.configure = handle_configure;
		this->libdecor_frame_iface.close = handle_close;
		this->libdecor_frame_iface.commit = handle_commit;
		this->libdecor_frame_iface.dismiss_popup = handle_dismiss_popup;

		this->frame = libdecor_decorate(context, this->wl_surface,
						&libdecor_frame_iface, this);
		libdecor_frame_set_app_id(this->frame, "libdecor-c++-demo");
		libdecor_frame_set_title(this->frame, "libdecor C++ demo");
		libdecor_frame_map(this->frame);
 	}

	void redraw()
	{
		Buffer *buffer;

		buffer = Buffer::create_shm_buffer(this->configured_width,
						   this->configured_height,
						   WL_SHM_FORMAT_XRGB8888);
		buffer->paint_buffer(this->window_state);

		wl_surface_attach(this->wl_surface, buffer->get_buffer(), 0, 0);
		wl_surface_damage_buffer(this->wl_surface, 0, 0,
					 this->configured_width,
					 this->configured_height);
		wl_surface_commit(this->wl_surface);
	}

private:
	void configure(struct libdecor_frame *frame,
		       struct libdecor_configuration *configuration)
	{
		int width = 0, height = 0;
		enum libdecor_window_state window_state;
		struct libdecor_state *state;

		libdecor_configuration_get_content_size(configuration, frame,
							&width, &height);

		width = (width == 0) ? this->floating_width : width;
		height = (height == 0) ? this->floating_height : height;

		this->configured_width = width;
		this->configured_height = height;

		if (!libdecor_configuration_get_window_state(configuration,
							     &window_state))
			window_state = LIBDECOR_WINDOW_STATE_NONE;

		this->window_state = window_state;

		state = libdecor_state_new(width, height);
		libdecor_frame_commit(frame, state, configuration);
		libdecor_state_free(state);

		/* store floating dimensions */
		if (libdecor_frame_is_floating(this->frame)) {
			this->floating_width = width;
			this->floating_height = height;
		}

		this->redraw();
	}

	static void handle_configure(struct libdecor_frame *frame,
				     struct libdecor_configuration *configuration,
				     void *user_data)
	{
		Window *window = reinterpret_cast<Window *>(user_data);

		window->configure(frame, configuration);
	}

	static void handle_close(struct libdecor_frame *frame,
				 void *user_data)
	{
		exit(EXIT_SUCCESS);
	}

	void commit()
	{
		wl_surface_commit(this->wl_surface);
	}

	static void handle_commit(struct libdecor_frame *frame,
			          void *user_data)
	{
		Window *window = reinterpret_cast<Window *>(user_data);

		window->commit();
	}

	static void handle_dismiss_popup(struct libdecor_frame *frame,
					 const char *seat_name,
					 void *user_data)
	{
	}

	struct wl_surface *wl_surface;
	struct libdecor_frame *frame;
	struct libdecor_frame_interface libdecor_frame_iface;

	int configured_width;
	int configured_height;
	enum libdecor_window_state window_state;

	int floating_width;
	int floating_height;
};

static Window *window;

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
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	if (strcmp(interface, "wl_compositor") == 0) {
		if (version < 4) {
			cerr << "wl_compositor version >= 4 required" << endl;
			exit(EXIT_FAILURE);
		}
		wl_compositor =
			reinterpret_cast<struct wl_compositor *>(
				wl_registry_bind(wl_registry,
						 id, &wl_compositor_interface, 4));
	} else if (strcmp(interface, "wl_shm") == 0) {
		wl_shm = reinterpret_cast<struct wl_shm *>(
			wl_registry_bind(wl_registry,
					 id, &wl_shm_interface, 1));
		wl_shm_add_listener(wl_shm, &shm_listener, NULL);
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
	cerr << "Caught error (" << error << "): " << message << endl;
	exit(EXIT_FAILURE);
}

static struct libdecor_interface libdecor_iface = {
	.error = handle_error,
};

int
main(int argc,
     char **argv)
{
	struct wl_display *wl_display;
	struct wl_registry *wl_registry;
	struct libdecor *context;

	wl_display = wl_display_connect(NULL);
	if (!wl_display) {
		cerr << "No Wayland connection" << endl;
		return EXIT_FAILURE;
	}

	wl_registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(wl_registry,
				 &registry_listener,
				 NULL);
	wl_display_roundtrip(wl_display);
	wl_display_roundtrip(wl_display);
	if (!has_xrgb) {
		cerr << "No XRGB shm format" << endl;
		return EXIT_FAILURE;
	}

	context = libdecor_new(wl_display, &libdecor_iface);
	window = new Window(context, wl_compositor);

	while (libdecor_dispatch(context, -1) >= 0);

	delete window;

	return EXIT_SUCCESS;
}
