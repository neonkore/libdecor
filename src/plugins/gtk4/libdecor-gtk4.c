/*
 * Copyright © 2022 Red Hat Inc.
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

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <wayland-client.h>
#include <wayland-server.h>

#include "libdecor-plugin.h"
#include "utils.h"
#include "common/libdecor-process-utils.h"
#include "common/libdecor-os-utils.h"
#include "libdecor-gtk4-tunnels.h"

#include "xdg-shell-client-protocol.h"
#include "xdg-shell-server-protocol.h"
#include "libdecor-shell-server-protocol.h"

#define XDG_WM_BASE_VERSION 3 // really?
#define LIBDECOR_SHELL_VERSION 1

struct dispatch_state {
	bool read_client_display;
	int dispatch_count;
};

struct dispatcher {
	void (* dispatch)(void *user_data, int events, struct dispatch_state *state);
	void *user_data;
};

struct pending_frame {
	uint32_t serial;
	struct libdecor_frame *frame;
	struct wl_list link;
};

struct libdecor_frame_gtk4 {
	struct libdecor_frame frame;

	struct libdecor_plugin_gtk4 *plugin_gtk4;

	uint32_t serial;
};

struct libdecor_plugin_gtk4 {
	struct libdecor_plugin plugin;

	struct libdecor *context;

	int epoll_fd;

	struct {
		struct wl_subcompositor *wl_subcompositor;
		struct wl_display *wl_display;
		struct wl_event_queue *wl_event_queue;
		struct wl_registry *wl_registry;
		bool plugin_queue_only;
		bool prepared_to_read;
	} client;

	struct {
		struct wl_display *wl_display;
		struct wl_global *xdg_wm_base;
		struct wl_global *libdecor_shell;
	} server;

	struct {
		struct wl_resource *resource;
	} shell;

	struct libdecor_gtk4_tunnels *tunnels;

	struct wl_list pending_frames;

	struct wl_client *wl_client;
};

struct frame_surface {
	struct libdecor_plugin_gtk4 *plugin_gtk4;
	struct surface *surface;

	struct wl_resource *xdg_surface_resource;
	struct wl_resource *xdg_toplevel_resource;
	struct wl_resource *frame_surface_resource;
};

static void
libdecor_plugin_gtk4_destroy(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 =
		(struct libdecor_plugin_gtk4 *) plugin;

	wl_registry_destroy(plugin_gtk4->client.wl_registry);

	if (plugin_gtk4->epoll_fd > 0)
		close(plugin_gtk4->epoll_fd);

	libdecor_gtk4_tunnels_free(plugin_gtk4->tunnels);

	libdecor_plugin_release(&plugin_gtk4->plugin);
	free(plugin_gtk4);
}

static int
libdecor_plugin_gtk4_get_fd(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 =
		(struct libdecor_plugin_gtk4 *) plugin;

	return plugin_gtk4->epoll_fd;
}

static void
dispatch_event_loop(void *data, int events, struct dispatch_state *state)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 = data;
	struct wl_event_loop *event_loop;

	fprintf(stderr, ":::: %s:%d %s() - \n", __FILE__, __LINE__, __func__);
	event_loop = wl_display_get_event_loop(plugin_gtk4->server.wl_display);
	if (wl_event_loop_dispatch(event_loop, 0) != 0)
		fprintf(stderr, ":::: %s:%d %s() - FAIL: %m\n", __FILE__, __LINE__, __func__);
}

static void
prepare_client_display(struct libdecor_plugin_gtk4 *plugin_gtk4, struct dispatch_state *state)
{
	struct wl_display *wl_display = libdecor_get_wl_display(plugin_gtk4->context);
	struct wl_event_queue *plugin_queue;

	plugin_queue = plugin_gtk4->client.wl_event_queue;

	while (true) {
		int count;

		if (wl_display_prepare_read_queue(wl_display, plugin_queue) != 0)
			goto dispatch_pending;
		fprintf(stderr, ":::: %s:%d %s() - prep #1\n", __FILE__, __LINE__, __func__);

		if (!plugin_gtk4->client.plugin_queue_only) {
			if (wl_display_prepare_read(wl_display) != 0) {
				wl_display_cancel_read(wl_display);
				fprintf(stderr, ":::: %s:%d %s() - .. cancel that\n", __FILE__, __LINE__, __func__);
				goto dispatch_pending;
			}
		}
		fprintf(stderr, ":::: %s:%d %s() - prep #2\n", __FILE__, __LINE__, __func__);

		break;
dispatch_pending:
		fprintf(stderr, ":::: %s:%d %s() - dispatch plugin queue\n", __FILE__, __LINE__, __func__);
		count = wl_display_dispatch_queue_pending(wl_display, plugin_queue);
		if (count == -1) {
			fprintf(stderr, ":::: %s:%d %s() - failure\n", __FILE__, __LINE__, __func__);
			break;
		}
		state->dispatch_count += count;

		if (!plugin_gtk4->client.plugin_queue_only) {
			fprintf(stderr, ":::: %s:%d %s() - dispatch app queue\n", __FILE__, __LINE__, __func__);
			count = wl_display_dispatch_pending(wl_display);
			if (count == -1) {
				fprintf(stderr, ":::: %s:%d %s() - failure\n", __FILE__, __LINE__, __func__);
				break;
			}
			state->dispatch_count += count;
		}
	}

	plugin_gtk4->client.prepared_to_read = true;
}

static void
dispatch_client_display(void *data, int events, struct dispatch_state *state)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 = data;
	struct wl_display *wl_display = libdecor_get_wl_display(plugin_gtk4->context);
	struct wl_event_queue *plugin_queue;

	if (events & EPOLLIN) {
		fprintf(stderr, ":::: %s:%d %s() - cancel -> read\n", __FILE__, __LINE__, __func__);
		wl_display_cancel_read(wl_display);
		wl_display_read_events(wl_display);
	} else {
		fprintf(stderr, ":::: %s:%d %s() - cancel -> cancel\n", __FILE__, __LINE__, __func__);
		wl_display_cancel_read(wl_display);
		wl_display_cancel_read(wl_display);
	}

	plugin_gtk4->client.prepared_to_read = false;

	plugin_queue = plugin_gtk4->client.wl_event_queue;
	state->dispatch_count += wl_display_dispatch_queue_pending(wl_display,
								   plugin_queue);
	state->dispatch_count += wl_display_dispatch_pending(wl_display);
}

static int
libdecor_plugin_gtk4_dispatch(struct libdecor_plugin *plugin,
			      int timeout)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 =
		(struct libdecor_plugin_gtk4 *) plugin;
	struct wl_display *wl_display = libdecor_get_wl_display(plugin_gtk4->context);
	struct epoll_event ep[16] = {};
	//struct pollfd fds[1];
	//int ret;
	int count;
	int i;
	//int dispatch_count = 0;
	struct dispatch_state state = {};

	fprintf(stderr, ":::: %s:%d %s() - #v#v#v#v#\n", __FILE__, __LINE__, __func__);

	prepare_client_display(plugin_gtk4, &state);

	wl_display_flush_clients(plugin_gtk4->server.wl_display);
	// todo handle error

	if (wl_display_flush(wl_display) < 0 &&
	    errno != EAGAIN) {
		fprintf(stderr, ":::: %s:%d %s() - flush failed, something is broken\n", __FILE__, __LINE__, __func__);
		abort();
		return -errno;
	}

	count = epoll_wait(plugin_gtk4->epoll_fd, ep, ARRAY_LENGTH(ep), timeout);
	for (i = 0; i < count; i++) {
		struct dispatcher *dispatcher = ep[i].data.ptr;

		fprintf(stderr, ":::: %s:%d %s() - vv dispatch\n", __FILE__, __LINE__, __func__);
		dispatcher->dispatch(dispatcher->user_data, ep[i].events, &state);
		fprintf(stderr, ":::: %s:%d %s() - ^^ dispatch\n", __FILE__, __LINE__, __func__);
	}

	if (plugin_gtk4->client.prepared_to_read) {
		fprintf(stderr, ":::: %s:%d %s() - 2 x cancel\n", __FILE__, __LINE__, __func__);
		wl_display_cancel_read(wl_display);
		if (!plugin_gtk4->client.plugin_queue_only)
			wl_display_cancel_read(wl_display);
	}
	else
		fprintf(stderr, ":::: %s:%d %s() - didn't prep or already read/cancelled\n", __FILE__, __LINE__, __func__);

	plugin_gtk4->client.prepared_to_read = false;

	fprintf(stderr, ":::: %s:%d %s() - #^#^#^#^#\n", __FILE__, __LINE__, __func__);
	return state.dispatch_count;
}

static void
dispatch_plugin_only(struct libdecor_plugin_gtk4 *plugin_gtk4)
{
	//plugin_gtk4->client.plugin_queue_only = true;
	libdecor_plugin_gtk4_dispatch(&plugin_gtk4->plugin, -1);
	plugin_gtk4->client.plugin_queue_only = false;
}

static struct libdecor_frame_gtk4 *
libdecor_frame_gtk4_new(struct libdecor_plugin_gtk4 *plugin_gtk4)
{
	struct libdecor_frame_gtk4 *frame_gtk4;
	static uint32_t frame_serial = 0;

	frame_gtk4 = zalloc(sizeof *frame_gtk4);
	frame_gtk4->plugin_gtk4 = plugin_gtk4;
	frame_gtk4->serial = frame_serial++;

	libdecor_shell_send_request_frame(plugin_gtk4->shell.resource,
					  frame_gtk4->serial);

	return frame_gtk4;
}

static struct libdecor_frame *
libdecor_plugin_gtk4_frame_new(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 =
		(struct libdecor_plugin_gtk4 *) plugin;
	struct libdecor_frame_gtk4 *frame_gtk4;

	frame_gtk4 = libdecor_frame_gtk4_new(plugin_gtk4);

	return &frame_gtk4->frame;
}

static void
libdecor_plugin_gtk4_frame_free(struct libdecor_plugin *plugin,
				struct libdecor_frame *frame)
{
	free(frame);
}

static void
libdecor_plugin_gtk4_frame_commit(struct libdecor_plugin *plugin,
				  struct libdecor_frame *frame,
				  struct libdecor_state *state,
				  struct libdecor_configuration *configuration)
{
}

static void
libdecor_plugin_gtk4_frame_property_changed(struct libdecor_plugin *plugin,
					    struct libdecor_frame *frame)
{
}

static void
libdecor_plugin_gtk4_frame_popup_grab(struct libdecor_plugin *plugin,
				      struct libdecor_frame *frame,
				      const char *seat_name)
{
}

static void
libdecor_plugin_gtk4_frame_popup_ungrab(struct libdecor_plugin *plugin,
					struct libdecor_frame *frame,
					const char *seat_name)
{
}

static bool
libdecor_plugin_gtk4_frame_get_border_size(struct libdecor_plugin *plugin,
					   struct libdecor_frame *frame,
					   struct libdecor_configuration *configuration,
					   int *left,
					   int *right,
					   int *top,
					   int *bottom)
{
	return false;
}

static struct libdecor_plugin_interface gtk4_plugin_iface = {
	.destroy = libdecor_plugin_gtk4_destroy,
	.get_fd = libdecor_plugin_gtk4_get_fd,
	.dispatch = libdecor_plugin_gtk4_dispatch,

	.frame_new = libdecor_plugin_gtk4_frame_new,
	.frame_free = libdecor_plugin_gtk4_frame_free,
	.frame_commit = libdecor_plugin_gtk4_frame_commit,
	.frame_property_changed = libdecor_plugin_gtk4_frame_property_changed,
	.frame_popup_grab = libdecor_plugin_gtk4_frame_popup_grab,
	.frame_popup_ungrab = libdecor_plugin_gtk4_frame_popup_ungrab,
	.frame_get_border_size = libdecor_plugin_gtk4_frame_get_border_size,
};

static void
destroy_xdg_toplevel(struct wl_resource *resource)
{
	struct frame_surface *frame_surface = wl_resource_get_user_data(resource);

	frame_surface->xdg_toplevel_resource = NULL;

	// unref
}

static void
xdg_toplevel_handle_destroy(struct wl_client *client,
			    struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
xdg_toplevel_handle_set_parent(struct wl_client *client,
			       struct wl_resource *resource,
			       struct wl_resource *parent)
{
	fprintf(stderr, ":::: %s:%d %s() - \n", __FILE__, __LINE__, __func__);
	/* Ignore */
}

static void
xdg_toplevel_handle_set_title(struct wl_client *client,
			      struct wl_resource *resource,
			      const char *title)
{
	/* Ignore */
}

static void
xdg_toplevel_handle_set_app_id(struct wl_client *client,
			       struct wl_resource *resource,
			       const char *app_id)
{
	/* Ignore */
}

static void
xdg_toplevel_handle_show_window_menu(struct wl_client *client,
				     struct wl_resource *resource,
				     struct wl_resource *seat,
				     uint32_t serial,
				     int32_t x,
				     int32_t y)
{
	// forward
}

static void
xdg_toplevel_handle_move(struct wl_client *client,
			 struct wl_resource *resource,
			 struct wl_resource *seat,
			 uint32_t serial)
{
	// forward
}

static void
xdg_toplevel_handle_resize(struct wl_client *client,
			   struct wl_resource *resource,
			   struct wl_resource *seat,
			   uint32_t serial,
			   uint32_t edges)
{
	// forward
}

static void
xdg_toplevel_handle_set_max_size(struct wl_client *client,
				 struct wl_resource *resource,
				 int32_t width,
				 int32_t height)
{
	/* Ignore */
}

static void
xdg_toplevel_handle_set_min_size(struct wl_client *client,
				 struct wl_resource *resource,
				 int32_t width,
				 int32_t height)
{
	/* Ignore */
}

static void
xdg_toplevel_handle_set_maximized(struct wl_client *client,
				  struct wl_resource *resource)
{
	// forward
}

static void
xdg_toplevel_handle_unset_maximized(struct wl_client *client,
				    struct wl_resource *resource)
{
	// forward
}

static void
xdg_toplevel_handle_set_fullscreen(struct wl_client *client,
				   struct wl_resource *resource,
				   struct wl_resource *output)
{
	// forward
}

static void
xdg_toplevel_handle_unset_fullscreen(struct wl_client *client,
				     struct wl_resource *resource)
{
	// forward
}

static void
xdg_toplevel_handle_set_minimized(struct wl_client *client,
				  struct wl_resource *resource)
{
	// forward
}

static const struct xdg_toplevel_interface xdg_toplevel_implementation = {
	.destroy = xdg_toplevel_handle_destroy,
	.set_parent = xdg_toplevel_handle_set_parent,
	.set_title = xdg_toplevel_handle_set_title,
	.set_app_id = xdg_toplevel_handle_set_app_id,
	.show_window_menu = xdg_toplevel_handle_show_window_menu,
	.move = xdg_toplevel_handle_move,
	.resize = xdg_toplevel_handle_resize,
	.set_max_size = xdg_toplevel_handle_set_max_size,
	.set_min_size = xdg_toplevel_handle_set_min_size,
	.set_maximized = xdg_toplevel_handle_set_maximized,
	.unset_maximized = xdg_toplevel_handle_unset_maximized,
	.set_fullscreen = xdg_toplevel_handle_set_fullscreen,
	.unset_fullscreen = xdg_toplevel_handle_unset_fullscreen,
	.set_minimized = xdg_toplevel_handle_set_minimized,
};

static void
destroy_xdg_surface(struct wl_resource *resource)
{
	struct frame_surface *frame_surface = wl_resource_get_user_data(resource);

	frame_surface->xdg_surface_resource = NULL;

	// unref
}

static void
xdg_surface_handle_destroy(struct wl_client *wl_client,
			   struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
xdg_surface_handle_get_toplevel(struct wl_client *wl_client,
				struct wl_resource *resource,
				uint32_t id)
{
	struct frame_surface *frame_surface = wl_resource_get_user_data(resource);
	//struct libdecor_plugin_gtk4 *plugin_gtk4 = frame_surface->plugin_gtk4;

	fprintf(stderr, ":::: %s:%d %s() - \n", __FILE__, __LINE__, __func__);

	frame_surface->xdg_toplevel_resource =
		wl_resource_create(wl_client, &xdg_toplevel_interface,
				   wl_resource_get_version(resource),
				   id);
	wl_resource_set_implementation(frame_surface->xdg_toplevel_resource,
				       &xdg_toplevel_implementation,
				       frame_surface, destroy_xdg_toplevel);
}

static void
xdg_surface_handle_get_popup(struct wl_client *wl_client,
			     struct wl_resource *resource,
			     uint32_t id,
			     struct wl_resource *parent_resource,
			     struct wl_resource *positioner_resource)
{
	// to be unused...
}

static void
xdg_surface_handle_set_window_geometry(struct wl_client *wl_client,
				       struct wl_resource *resource,
				       int32_t x, int32_t y,
				       int32_t width, int32_t height)
{
	fprintf(stderr, ":::: %s:%d %s() - \n", __FILE__, __LINE__, __func__);
}

static void
xdg_surface_handle_ack_configure(struct wl_client *wl_client,
				 struct wl_resource *resource,
				 uint32_t serial)
{
}

static const struct xdg_surface_interface xdg_surface_implementation = {
	.destroy = xdg_surface_handle_destroy,
	.get_toplevel = xdg_surface_handle_get_toplevel,
	.get_popup = xdg_surface_handle_get_popup,
	.set_window_geometry = xdg_surface_handle_set_window_geometry,
	.ack_configure = xdg_surface_handle_ack_configure,
};

static void
destroy_xdg_wm_base(struct wl_resource *resource)
{
	// todo
}

static void
xdg_wm_base_handle_destroy(struct wl_client *wl_client,
			   struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
xdg_wm_base_handle_create_positioner(struct wl_client *wl_client,
				     struct wl_resource *resource,
				     uint32_t id)
{
	// unused...
}


static void
xdg_wm_base_handle_get_xdg_surface(struct wl_client *wl_client,
				   struct wl_resource *xdg_wm_base_resource,
				   uint32_t id,
				   struct wl_resource *surface_resource)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 =
		wl_resource_get_user_data(xdg_wm_base_resource);
	struct surface *surface = wl_resource_get_user_data(surface_resource);
	struct frame_surface *frame_surface;

	frame_surface = zalloc(sizeof *frame_surface);
	surface->role_data = frame_surface;
	frame_surface->surface = surface;

	frame_surface->plugin_gtk4 = plugin_gtk4;
	frame_surface->xdg_surface_resource =
		wl_resource_create(wl_client, &xdg_surface_interface,
				   wl_resource_get_version(xdg_wm_base_resource),
				   id);
	wl_resource_set_implementation(frame_surface->xdg_surface_resource,
				       &xdg_surface_implementation,
				       frame_surface, destroy_xdg_surface);
}

static void
xdg_wm_base_handle_pong(struct wl_client *wl_client,
			struct wl_resource *resource,
			uint32_t serial)
{
}

static const struct xdg_wm_base_interface xdg_wm_base_implementation = {
	.destroy = xdg_wm_base_handle_destroy,
	.create_positioner = xdg_wm_base_handle_create_positioner,
	.get_xdg_surface = xdg_wm_base_handle_get_xdg_surface,
	.pong = xdg_wm_base_handle_pong,
};

static void
bind_xdg_wm_base(struct wl_client *client,
		 void *data, uint32_t version, uint32_t id)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 = data;
	struct wl_resource *resource;

	resource = wl_resource_create(client, &xdg_wm_base_interface,
				      version, id);
	wl_resource_set_implementation(resource, &xdg_wm_base_implementation,
				       plugin_gtk4, destroy_xdg_wm_base);
	fprintf(stderr, ":::: %s:%d %s() - bound xdg_wm_base\n", __FILE__, __LINE__, __func__);
}

static void
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 = user_data;

	if (strcmp(interface, "wl_subcompositor") == 0) {
		plugin_gtk4->client.wl_subcompositor =
			wl_registry_bind(wl_registry, id,
					 &wl_subcompositor_interface, 1);
	}
#if 0
	if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
		plugin_gtk4->tunnels.linux_dmabuf =
			linux_dmabuf_new(plugin_gtk4->server.wl_display,
					 wl_registry, id, version);
	}
	else if (strcmp(interface, "wl_subcompositor") == 0)
		init_wl_subcompositor(plugin_gtk4, id, version);
	else if (strcmp(interface, "wl_shm") == 0)
		init_wl_shm(plugin_gtk4, id, version);
	else if (strcmp(interface, "wl_seat") == 0)
		init_wl_seat(plugin_gtk4, id, version);
	else if (strcmp(interface, "wl_output") == 0)
		init_wl_output(plugin_gtk4, id, version);
#endif
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

static void
destroy_frame_surface(struct wl_resource *resource)
{
	struct frame_surface *frame_surface = wl_resource_get_user_data(resource);

	frame_surface->frame_surface_resource = NULL;
	// unref
}

static void
libdecor_frame_surface_handle_destroy(struct wl_client *wl_client,
				      struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct frame_surface_interface frame_surface_implementation = {
	.destroy = libdecor_frame_surface_handle_destroy,
};

static void
destroy_libdecor_shell(struct wl_resource *resource)
{
	// todo
}

static struct libdecor_frame *
find_frame(struct libdecor *context,
	   uint32_t serial)
{
	struct libdecor_frame *frame;

	wl_list_for_each(frame, libdecor_get_frames(context), link) {
		struct libdecor_frame_gtk4 *frame_gtk4 =
			(struct libdecor_frame_gtk4 *) frame;

		if (frame_gtk4->serial == serial)
			return frame;
	}

	return NULL;
}

static void
libdecor_shell_handle_create_frame(struct wl_client *wl_client,
				   struct wl_resource *resource,
				   uint32_t id,
				   uint32_t serial,
				   struct wl_resource *wl_surface_resource)
{
	struct surface *surface = wl_resource_get_user_data(wl_surface_resource);
	struct frame_surface *frame_surface = surface->role_data;
	struct libdecor_plugin_gtk4 *plugin_gtk4 = frame_surface->plugin_gtk4;
	struct libdecor_frame *frame;
	//struct libdecor_frame_gtk4 *frame_gtk4;
	struct wl_subsurface *subsurface;
	struct wl_array states;

	fprintf(stderr, ":::: %s:%d %s() - \n", __FILE__, __LINE__, __func__);
	frame_surface->frame_surface_resource =
		wl_resource_create(wl_client, &frame_surface_interface,
				   wl_resource_get_version(resource), id);
	wl_resource_set_implementation(resource, &frame_surface_implementation,
				       frame_surface, destroy_frame_surface);

	frame = find_frame(plugin_gtk4->context, serial);
	//frame_gtk4 = (struct libdecor_frame_gtk4 *) frame;

	subsurface = wl_subcompositor_get_subsurface(plugin_gtk4->client.wl_subcompositor,
						     frame_surface->surface->proxy,
						     libdecor_frame_get_wl_surface(frame));
	wl_subsurface_set_desync(subsurface);
	wl_subsurface_place_below(subsurface, libdecor_frame_get_wl_surface(frame));
	wl_subsurface_set_position(subsurface, -14, -50);

	wl_array_init(&states);
	xdg_toplevel_send_configure(frame_surface->xdg_toplevel_resource,
				    480, 358, &states);
	wl_array_release(&states);
	static uint32_t s;// todo clean up
	xdg_surface_send_configure(frame_surface->xdg_surface_resource,
				   ++s);

	libdecor_frame_toplevel_commit(frame);
}

static const struct libdecor_shell_interface libdecor_shell_implementation = {
	.create_frame = libdecor_shell_handle_create_frame,
};

static void
bind_libdecor_shell(struct wl_client *client,
		    void *data, uint32_t version, uint32_t id)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 = data;

	assert(!plugin_gtk4->shell.resource);

	plugin_gtk4->shell.resource =
		wl_resource_create(client, &libdecor_shell_interface, version, id);
	wl_resource_set_implementation(plugin_gtk4->shell.resource,
				       &libdecor_shell_implementation,
				       plugin_gtk4, destroy_libdecor_shell);
}

static bool
init_compositor(struct libdecor_plugin_gtk4 *plugin_gtk4)
{
	struct wl_display *wl_display = plugin_gtk4->client.wl_display;

	plugin_gtk4->client.wl_registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(plugin_gtk4->client.wl_registry,
				 &registry_listener,
				 plugin_gtk4);

	plugin_gtk4->server.xdg_wm_base =
		wl_global_create(plugin_gtk4->server.wl_display,
				 &xdg_wm_base_interface,
				 XDG_WM_BASE_VERSION,
				 plugin_gtk4, bind_xdg_wm_base);
	plugin_gtk4->server.libdecor_shell =
		wl_global_create(plugin_gtk4->server.wl_display,
				 &libdecor_shell_interface,
				 LIBDECOR_SHELL_VERSION,
				 plugin_gtk4, bind_libdecor_shell);

	wl_display_roundtrip_queue(libdecor_get_wl_display(plugin_gtk4->context),
				   plugin_gtk4->client.wl_event_queue);
	return true;
}

static char *
find_client_path(void)
{
	char *client_path = NULL;
	const char *plugin_dir_env;
	char *all_plugin_dirs;
	char *plugin_dir;
	char *saveptr;

	plugin_dir_env = getenv("LIBDECOR_PLUGIN_DIR");
	if (!plugin_dir_env) {
		plugin_dir_env = LIBDECOR_PLUGIN_DIR;
	}

	all_plugin_dirs = strdup(plugin_dir_env);

	plugin_dir = strtok_r(all_plugin_dirs, ":", &saveptr);
	while (plugin_dir) {
		char *path = NULL;

		asprintf(&path, "%s/libdecor-gtk4-client", plugin_dir);

		if (access(path, R_OK | X_OK) == 0) {
			client_path = path;
			goto out;
		} else {
			free(path);
		}

		plugin_dir = strtok_r(NULL, ":", &saveptr);
	}

out:
	free(all_plugin_dirs);
	return client_path;
}

static struct wl_client *
launch_client(struct libdecor_plugin_gtk4 *plugin_gtk4)
{
	char *path;
	struct custom_env child_env;
	struct fdstr wayland_socket;
	struct wl_client *client;
	char * const *argp;
	char * const *envp;
	sigset_t allsigs;
	pid_t pid;

	path = find_client_path();
	if (!path)
		return NULL;
	fprintf(stderr, ":::: %s:%d %s() - found %s\n", __FILE__, __LINE__, __func__, path);

	libdecor_custom_env_init_from_environ(&child_env);
	libdecor_custom_env_add_arg(&child_env, path);

	if (libdecor_os_socketpair_cloexec(AF_UNIX, SOCK_STREAM, 0,
					   wayland_socket.fds) < 0) {
		fprintf(stderr,
			"socketpair failed while launching '%s': %s\n",
			path, strerror(errno));
		libdecor_custom_env_fini(&child_env);
		return NULL;
	}
	libdecor_fdstr_update_str1(&wayland_socket);
	libdecor_custom_env_set_env_var(&child_env, "WAYLAND_DISPLAY", "");
	libdecor_custom_env_set_env_var(&child_env, "WAYLAND_SOCKET",
					wayland_socket.str1);

	argp = libdecor_custom_env_get_argp(&child_env);
	envp = libdecor_custom_env_get_envp(&child_env);

	pid = fork();
	switch (pid) {
	case 0:
		setsid();

		sigfillset(&allsigs);
		sigprocmask(SIG_UNBLOCK, &allsigs, NULL);

		if (!libdecor_fdstr_clear_cloexec_fd1(&wayland_socket))
			exit(EXIT_FAILURE);

		execve(argp[0], argp, envp);

		exit(EXIT_FAILURE);

	default:
		close(wayland_socket.fds[1]);
		client = wl_client_create(plugin_gtk4->server.wl_display,
					  wayland_socket.fds[0]);
		if (!client) {
			libdecor_custom_env_fini(&child_env);
			close(wayland_socket.fds[0]);
			fprintf(stderr, "Failed to create wl_client for %s\n", path);
			return NULL;
		}

		break;

	case -1:
		libdecor_fdstr_close_all(&wayland_socket);
		fprintf(stderr, "fork failed while launching '%s': %s\n", path,
			strerror(errno));
		break;
	}

	libdecor_custom_env_fini(&child_env);

	return client;
}

static void
sync_handler_handle_callback_done(void *data,
				  struct wl_callback *callback,
				  uint32_t serial)
{
	struct wl_resource *callback_resource = data;

	wl_callback_send_done(callback_resource, serial);
	wl_resource_destroy(callback_resource);
	wl_callback_destroy(callback);
}

static const struct wl_callback_listener sync_handler_callback_listener = {
  sync_handler_handle_callback_done
};

static void
display_sync_handler(struct wl_display *wl_display,
		     struct wl_resource *callback_resource,
		     void *user_data)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4 = user_data;
	struct wl_display *client_wl_display = plugin_gtk4->client.wl_display;
	struct wl_callback *proxy;

	proxy = wl_display_sync(client_wl_display);
	wl_callback_add_listener(proxy,
				 &sync_handler_callback_listener,
				 callback_resource);
}

static struct libdecor_plugin *
libdecor_plugin_new(struct libdecor *context)
{
	struct libdecor_plugin_gtk4 *plugin_gtk4;
	struct wl_event_loop *event_loop;
	int fd;
	struct epoll_event ep;
	struct dispatcher *event_loop_dispatcher;
	struct dispatcher *client_display_dispatcher;

	plugin_gtk4 = zalloc(sizeof *plugin_gtk4);
	libdecor_plugin_init(&plugin_gtk4->plugin,
			     context,
			     &gtk4_plugin_iface);
	plugin_gtk4->context = context;

	plugin_gtk4->client.wl_display =
		wl_proxy_create_wrapper(libdecor_get_wl_display(context));
	plugin_gtk4->client.wl_event_queue =
		wl_display_create_queue(libdecor_get_wl_display(context));
	wl_proxy_set_queue((struct wl_proxy *) plugin_gtk4->client.wl_display,
			   plugin_gtk4->client.wl_event_queue);

	plugin_gtk4->epoll_fd = os_epoll_create_cloexec();
	if (plugin_gtk4->epoll_fd < 0) {
		libdecor_plugin_gtk4_destroy(&plugin_gtk4->plugin);
		return NULL;
	}

	plugin_gtk4->server.wl_display = wl_display_create();
	wl_display_set_sync_handler(plugin_gtk4->server.wl_display,
				    display_sync_handler, plugin_gtk4);

	event_loop = wl_display_get_event_loop(plugin_gtk4->server.wl_display);
	fd = wl_event_loop_get_fd(event_loop);
	event_loop_dispatcher = zalloc(sizeof (struct dispatcher));
	event_loop_dispatcher->dispatch = dispatch_event_loop;
	event_loop_dispatcher->user_data = plugin_gtk4;
	ep.events = EPOLLIN | EPOLLOUT;
	ep.data.ptr = event_loop_dispatcher; // todo free
	epoll_ctl(plugin_gtk4->epoll_fd, EPOLL_CTL_ADD, fd, &ep);

	client_display_dispatcher = zalloc(sizeof (struct dispatcher));
	client_display_dispatcher->dispatch = dispatch_client_display;
	client_display_dispatcher->user_data = plugin_gtk4;
	ep.events = EPOLLIN;
	ep.data.ptr = client_display_dispatcher; // todo free
	epoll_ctl(plugin_gtk4->epoll_fd, EPOLL_CTL_ADD,
		  wl_display_get_fd(libdecor_get_wl_display(plugin_gtk4->context)),
		  &ep);

	plugin_gtk4->tunnels =
		libdecor_gtk4_tunnels_new(plugin_gtk4->client.wl_display,
					  plugin_gtk4->server.wl_display);
	init_compositor(plugin_gtk4);
	wl_list_init(&plugin_gtk4->pending_frames);

	plugin_gtk4->wl_client = launch_client(plugin_gtk4);
	if (!plugin_gtk4->wl_client) {
		libdecor_plugin_gtk4_destroy(&plugin_gtk4->plugin);
		return NULL;
	}

	while (!plugin_gtk4->shell.resource) {
		// ^^ needs to handle client failing
		dispatch_plugin_only(plugin_gtk4);
	}
	fprintf(stderr, ":::: %s:%d %s() - initialized\n", __FILE__, __LINE__, __func__);

	return &plugin_gtk4->plugin;
}

static struct libdecor_plugin_priority priorities[] = {
	{ NULL, LIBDECOR_PLUGIN_PRIORITY_MEDIUM }
};

LIBDECOR_EXPORT const struct libdecor_plugin_description
libdecor_plugin_description = {
	.api_version = LIBDECOR_PLUGIN_API_VERSION,
	.capabilities = LIBDECOR_PLUGIN_CAPABILITY_BASE,
	.description = "libdecor plugin using gtk4",
	.priorities = priorities,
	.constructor = libdecor_plugin_new,
};
