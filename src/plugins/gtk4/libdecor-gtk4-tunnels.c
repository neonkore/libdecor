/*
 * Copyright © 2014, 2015 Collabora, Ltd.
 * Copyright © 2022 Red Hat Inc
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

#include "libdecor-gtk4-tunnels.h"
#include "utils.h"

#include <stdio.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-server.h>
#include <unistd.h>

#include "linux-dmabuf-client-protocol.h"
#include "linux-dmabuf-server-protocol.h"
#include "wayland-drm-client-protocol.h"
#include "wayland-drm-server-protocol.h"

#define MAX_COMPOSITOR_VERSION 5
#define MAX_SHM_VERSION 1
#define MAX_LINUX_DMABUF_VERSION 4
#define MAX_WAYLAND_DRM_VERSION 2
#define MAX_OUTPUT_VERSION 3

struct global {
	struct libdecor_gtk4_tunnels *tunnels;

	uint32_t name;
	struct wl_global *wl_global;

	struct wl_list link;
};

struct libdecor_gtk4_tunnels {
	//struct libdecor_plugin_gtk4 *plugin_gtk4;
	struct {
		struct wl_display *wl_display;
		struct wl_registry *wl_registry;
	} client;

	struct {
		struct wl_display *wl_display;
	} server;

	struct {
		struct global compositor;
		struct global shm;
		struct global linux_dmabuf;
		struct global wayland_drm;

		struct wl_list outputs;
		struct wl_list seats;
	} globals;
};

struct buffer {
	struct wl_buffer *proxy;
	struct wl_resource *resource;
};

struct region {
	struct wl_region *proxy;
	struct wl_resource *resource;
};

struct compositor {
	struct wl_compositor *proxy;
	struct wl_resource *resource;
};

struct output {
	struct wl_output *proxy;
	struct wl_resource *resource;
};

struct seat {
	struct wl_seat *proxy;
	struct wl_resource *resource;
};

struct shm_pool {
	struct wl_shm_pool *proxy;
	struct wl_resource *resource;
};

struct shm {
	struct wl_shm *proxy;
	struct wl_resource *resource;
};

struct linux_dmabuf_params {
	struct zwp_linux_buffer_params_v1 *proxy;
	struct wl_resource *resource;
};

struct linux_dmabuf_feedback {
	struct zwp_linux_dmabuf_feedback_v1 *proxy;
	struct wl_resource *resource;
};

struct linux_dmabuf {
	struct zwp_linux_dmabuf_v1 *proxy;
	struct wl_resource *resource;
};

struct wayland_drm {
	struct wl_drm *proxy;
	struct wl_resource *resource;
};

static void
buffer_handle_destroy(struct wl_client *client,
	       struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_buffer_interface buffer_implementation = {
	buffer_handle_destroy
};

static void
destroy_buffer(struct wl_resource *resource)
{
	struct buffer *buffer;

	buffer = wl_resource_get_user_data(resource);
	free(buffer);
}

static void
surface_destroy(struct wl_client *client, struct wl_resource *resource)
{
	// todo
}

static void
surface_attach(struct wl_client *client,
	       struct wl_resource *resource,
	       struct wl_resource *buffer_resource, int32_t sx, int32_t sy)
{
	struct surface *surface = wl_resource_get_user_data(resource);
	struct buffer *buffer = wl_resource_get_user_data(buffer_resource);

	wl_surface_attach(surface->proxy, buffer->proxy, sx, sy);
}

static void
surface_damage(struct wl_client *client,
	       struct wl_resource *resource,
	       int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct surface *surface = wl_resource_get_user_data(resource);

	wl_surface_damage(surface->proxy, x, y, width, height);
}

static void
surface_damage_buffer(struct wl_client *client,
		      struct wl_resource *resource,
		      int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct surface *surface = wl_resource_get_user_data(resource);

	wl_surface_damage_buffer(surface->proxy, x, y, width, height);
}

#if 0
static void
destroy_frame_callback(struct wl_resource *resource)
{
	wl_list_remove(wl_resource_get_link(resource));
}
#endif

static void
surface_frame(struct wl_client *client,
	      struct wl_resource *resource, uint32_t callback)

{
	//struct surface *surface = wl_resource_get_user_data(resource);
	struct wl_resource *cb;
	//struct wl_callback *callback;

	cb = wl_resource_create(client, &wl_callback_interface, 1, callback);
	//wl_resource_set_implementation(cb, NULL, NULL,
				       //destroy_frame_callback);

	wl_callback_send_done(cb, 0);
	wl_resource_destroy (cb);
	// todo needs special handling
	//callback = wl_surface_frame(surface->proxy, region->proxy);
	//wl_callback_send_done(callback, 0);
	//wl_resource_destroy (callback);
}

static void
surface_set_opaque_region(struct wl_client *client,
			  struct wl_resource *resource,
			  struct wl_resource *region_resource)
{
	struct surface *surface = wl_resource_get_user_data(resource);
	struct region *region = wl_resource_get_user_data(region_resource);

	wl_surface_set_opaque_region(surface->proxy, region->proxy);
}

static void
surface_set_input_region(struct wl_client *client,
			 struct wl_resource *resource,
			 struct wl_resource *region_resource)
{
	struct surface *surface = wl_resource_get_user_data(resource);
	struct region *region = wl_resource_get_user_data(region_resource);

	wl_surface_set_input_region(surface->proxy, region->proxy);
}

static void
surface_commit(struct wl_client *client, struct wl_resource *resource)
{
	struct surface *surface = wl_resource_get_user_data(resource);

	wl_surface_commit(surface->proxy);
}

static void
surface_set_buffer_transform(struct wl_client *client,
			     struct wl_resource *resource, int transform)
{
	struct surface *surface = wl_resource_get_user_data(resource);

	wl_surface_set_buffer_transform(surface->proxy, transform);
}

static void
surface_set_buffer_scale(struct wl_client *client,
			 struct wl_resource *resource,
			 int32_t scale)
{
	struct surface *surface = wl_resource_get_user_data(resource);

	wl_surface_set_buffer_scale(surface->proxy, scale);
}

static void
surface_offset(struct wl_client *client,
	       struct wl_resource *resource,
	       int32_t sx,
	       int32_t sy)
{
	struct surface *surface = wl_resource_get_user_data(resource);

	wl_surface_offset(surface->proxy, sx, sy);
}

static const struct wl_surface_interface surface_interface = {
	surface_destroy,
	surface_attach,
	surface_damage,
	surface_frame,
	surface_set_opaque_region,
	surface_set_input_region,
	surface_commit,
	surface_set_buffer_transform,
	surface_set_buffer_scale,
	surface_damage_buffer,
	surface_offset,
};

static void
destroy_surface(struct wl_resource *resource)
{
	struct surface *surface = wl_resource_get_user_data(resource);

	wl_surface_destroy(surface->proxy);
	free(surface);
}

static void
surface_enter(void *data,
	      struct wl_surface *wl_surface, struct wl_output *wl_output)
{
	struct surface *surface = data;
	struct output *output = wl_output_get_user_data(wl_output);

	wl_surface_send_enter(surface->resource, output->resource);
}

static void
surface_leave(void *data,
	      struct wl_surface *wl_surface, struct wl_output *wl_output)
{
	struct surface *surface = data;
	struct output *output = wl_output_get_user_data(wl_output);

	wl_surface_send_leave(surface->resource, output->resource);
}

static const struct wl_surface_listener surface_listener = {
	surface_enter,
	surface_leave
};

static void
compositor_create_surface(struct wl_client *client,
			  struct wl_resource *resource, uint32_t id)
{
	struct compositor *compositor = wl_resource_get_user_data(resource);
	struct surface *surface;

	surface = zalloc(sizeof *surface);

	surface->proxy = wl_compositor_create_surface(compositor->proxy);
	wl_surface_add_listener(surface->proxy, &surface_listener, surface);

	surface->resource =
		wl_resource_create(client, &wl_surface_interface,
				   wl_resource_get_version(resource), id);
	wl_resource_set_implementation(surface->resource, &surface_interface,
				       surface, destroy_surface);
}

static void
destroy_region(struct wl_resource *resource)
{
	struct region *region = wl_resource_get_user_data(resource);

	wl_region_destroy(region->proxy);
	free(region);
}

static void
region_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
region_add(struct wl_client *client, struct wl_resource *resource,
	   int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct region *region = wl_resource_get_user_data(resource);

	wl_region_add(region->proxy, x, y, width, height);
}

static void
region_subtract(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height)
{
	struct region *region = wl_resource_get_user_data(resource);

	wl_region_subtract(region->proxy, x, y, width, height);
}

static const struct wl_region_interface region_implementation = {
	region_destroy,
	region_add,
	region_subtract
};

static void
compositor_create_region(struct wl_client *client,
			 struct wl_resource *resource, uint32_t id)
{
	struct compositor *compositor = wl_resource_get_user_data(resource);
	struct region *region;

	region = zalloc(sizeof *region);

	region->resource =
		wl_resource_create(client, &wl_region_interface,
				   wl_resource_get_version(resource), id);
	wl_resource_set_implementation(region->resource, &region_implementation,
				       region, destroy_region);
	region->proxy = wl_compositor_create_region(compositor->proxy);
}

static const struct wl_compositor_interface compositor_implementation = {
	compositor_create_surface,
	compositor_create_region
};

static void
destroy_compositor(struct wl_resource *resource)
{
	// TODO
}

static void
bind_compositor(struct wl_client *client,
		void *data, uint32_t version, uint32_t id)
{
	struct libdecor_gtk4_tunnels *tunnels = data;
	struct compositor *compositor;

	compositor = zalloc(sizeof *compositor);

	compositor->proxy =
		wl_registry_bind(tunnels->client.wl_registry,
				 tunnels->globals.compositor.name,
				 &wl_compositor_interface,
				 version);

	compositor->resource = wl_resource_create(client, &wl_compositor_interface,
						  version, id);
	wl_resource_set_implementation(compositor->resource, &compositor_implementation,
				       compositor, destroy_compositor);
	fprintf(stderr, ":::: %s:%d %s() - bound compositor\n", __FILE__, __LINE__, __func__);
}

struct wl_global *
compositor_new(struct libdecor_gtk4_tunnels *tunnels,
	       uint32_t version)
{
	uint32_t tunneled_version;

	tunneled_version = MIN(version, MAX_COMPOSITOR_VERSION);
	return wl_global_create(tunnels->server.wl_display,
				&wl_compositor_interface,
				tunneled_version,
				tunnels, bind_compositor);
}

static void
destroy_output(struct wl_resource *resource)
{
	struct output *output = wl_resource_get_user_data(resource);

	if (wl_proxy_get_version((struct wl_proxy *) output->proxy) >=
	    WL_OUTPUT_RELEASE_SINCE_VERSION)
		wl_output_release(output->proxy);
	else
		wl_output_destroy(output->proxy);
	free(output);
}

static void
output_release(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct wl_output_interface output_implementation = {
	output_release,
};

static void
display_handle_geometry(void *data,
			struct wl_output *wl_output,
			int x, int y,
			int physical_width,
			int physical_height,
			int subpixel,
			const char *make,
			const char *model,
			int transform)
{
	/* Don't care */
}

static void
display_handle_done(void *data,
		    struct wl_output *wl_output)
{
	struct output *output = data;

	wl_output_send_done(output->resource);
}

static void
display_handle_scale(void *data,
		     struct wl_output *wl_output,
		     int32_t scale)
{
	struct output *output = data;

	wl_output_send_scale(output->resource, scale);
}

static void
display_handle_mode(void *data,
		    struct wl_output *wl_output,
		    uint32_t flags,
		    int width,
		    int height,
		    int refresh)
{
	/* Don't care */
}

static const struct wl_output_listener output_listener = {
	display_handle_geometry,
	display_handle_mode,
	display_handle_done,
	display_handle_scale
};

static void
bind_output(struct wl_client *client,
	    void *data, uint32_t version, uint32_t id)
{
	struct global *global = data;
	struct libdecor_gtk4_tunnels *tunnels = global->tunnels;
	struct output *output;

	output = zalloc(sizeof *output);

	output->proxy =
		wl_registry_bind(tunnels->client.wl_registry,
				 global->name,
				 &wl_output_interface,
				 version);
	wl_output_add_listener(output->proxy, &output_listener, output);

	output->resource = wl_resource_create(client, &wl_output_interface,
					      version, id);
	wl_resource_set_implementation(output->resource, &output_implementation,
				       output, destroy_output);
	fprintf(stderr, ":::: %s:%d %s() - bound compositor\n", __FILE__, __LINE__, __func__);
}

static struct wl_global *
output_new(struct libdecor_gtk4_tunnels *tunnels,
	   uint32_t version,
	   struct global *global)
{
	uint32_t tunneled_version;

	tunneled_version = MIN(version, MAX_OUTPUT_VERSION);
	return wl_global_create(tunnels->server.wl_display,
				&wl_output_interface,
				tunneled_version,
				global, bind_output);
}

static void
destroy_pool (struct wl_resource *resource)
{
	struct shm_pool *pool = wl_resource_get_user_data(resource);

	free(pool);
}

static void
shm_pool_create_buffer(struct wl_client *client, struct wl_resource *resource,
		       uint32_t id, int32_t offset,
		       int32_t width, int32_t height,
		       int32_t stride, uint32_t format)
{
	struct shm_pool *pool = wl_resource_get_user_data(resource);
	struct buffer *buffer;

	buffer = zalloc(sizeof *buffer);

	buffer->proxy = wl_shm_pool_create_buffer(pool->proxy, offset,
						  width, height, stride,
						  format);
	buffer->resource =
		wl_resource_create(client, &wl_buffer_interface, 1, id);
	wl_resource_set_implementation(buffer->resource,
				       &buffer_implementation,
				       buffer, destroy_buffer);
}

static void
shm_pool_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
shm_pool_resize(struct wl_client *client, struct wl_resource *resource,
		int32_t size)
{
	struct shm_pool *pool = wl_resource_get_user_data(resource);

	wl_shm_pool_resize(pool->proxy, size);
}

static const struct wl_shm_pool_interface shm_pool_interface = {
	shm_pool_create_buffer,
	shm_pool_destroy,
	shm_pool_resize
};

static void
destroy_shm(struct wl_resource *resource)
{
	struct shm *shm = wl_resource_get_user_data(resource);

	wl_shm_destroy(shm->proxy);
	free(shm);
}

static void
shm_handle_create_pool(struct wl_client *client, struct wl_resource *resource,
		       uint32_t id, int fd, int32_t size)
{
	struct shm *shm = wl_resource_get_user_data(resource);
	struct shm_pool *pool;

	pool = zalloc(sizeof *pool);
	pool->proxy = wl_shm_create_pool(shm->proxy, fd, size);
	close(fd);
	pool->resource = wl_resource_create(client, &wl_shm_pool_interface, 1, id);
	wl_resource_set_implementation(pool->resource,
				       &shm_pool_interface,
				       pool, destroy_pool);
}

static const struct wl_shm_interface shm_implementation = {
	shm_handle_create_pool
};

static void
shm_handle_format(void *data, struct wl_shm *wl_shm, uint32_t format)
{
	struct shm *shm = data;

	wl_shm_send_format(shm->resource, format);
}

struct wl_shm_listener shm_listener = {
	shm_handle_format
};

static void
bind_shm(struct wl_client *client,
	 void *data, uint32_t version, uint32_t id)
{
	struct libdecor_gtk4_tunnels *tunnels = data;
	struct shm *shm;

	shm = zalloc(sizeof *shm);
	shm->proxy =
		wl_registry_bind(tunnels->client.wl_registry,
				 tunnels->globals.shm.name,
				 &wl_shm_interface,
				 version);
	wl_shm_add_listener(shm->proxy,
			    &shm_listener,
			    shm);

	shm->resource = wl_resource_create(client,
					   &wl_shm_interface,
					   version, id);
	wl_resource_set_implementation(shm->resource,
				       &shm_implementation,
				       shm, destroy_shm);
}

struct wl_global *
shm_new(struct libdecor_gtk4_tunnels *tunnels,
	uint32_t version)
{
	uint32_t tunneled_version;

	tunneled_version = MIN(version, MAX_SHM_VERSION);
	return wl_global_create(tunnels->server.wl_display,
				&wl_shm_interface,
				tunneled_version,
				tunnels, bind_shm);
}

static void
dmabuf_handle_format(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
		     uint32_t format)
{
	struct linux_dmabuf *linux_dmabuf = data;

	zwp_linux_dmabuf_v1_send_format(linux_dmabuf->resource, format);
}

static void
dmabuf_handle_modifier(void *data, struct zwp_linux_dmabuf_v1 *dmabuf,
                       uint32_t format, uint32_t modifier_hi,
                       uint32_t modifier_lo)
{
	struct linux_dmabuf *linux_dmabuf = data;

	zwp_linux_dmabuf_v1_send_modifier(linux_dmabuf->resource, format,
					  modifier_hi, modifier_lo);
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
   .format = dmabuf_handle_format,
   .modifier = dmabuf_handle_modifier,
};

static void
server_linux_dmabuf_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
linux_dmabuf_params_destroy(struct linux_dmabuf_params *params)
{
	free(params);
}

static void
destroy_params(struct wl_resource *params_resource)
{
	struct linux_dmabuf_params *params;

	params = wl_resource_get_user_data(params_resource);
	if (!params)
		return;

	linux_dmabuf_params_destroy(params);
}

static void
params_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static void
params_add(struct wl_client *client,
	   struct wl_resource *params_resource,
	   int32_t name_fd,
	   uint32_t plane_idx,
	   uint32_t offset,
	   uint32_t stride,
	   uint32_t modifier_hi,
	   uint32_t modifier_lo)
{
	struct linux_dmabuf_params *params =
		wl_resource_get_user_data(params_resource);

	zwp_linux_buffer_params_v1_add(params->proxy,
				       name_fd,
				       plane_idx,
				       offset, stride,
				       modifier_hi, modifier_lo);
}

static void
params_create(struct wl_client *client,
	      struct wl_resource *params_resource,
	      int32_t width,
	      int32_t height,
	      uint32_t format,
	      uint32_t flags)
{
	struct linux_dmabuf_params *params =
		wl_resource_get_user_data(params_resource);

	zwp_linux_buffer_params_v1_create(params->proxy,
					  width, height,
					  format, flags);
	// this needs signal forwarding
}

static void
params_create_immed(struct wl_client *client,
		    struct wl_resource *params_resource,
		    uint32_t buffer_id,
		    int32_t width,
		    int32_t height,
		    uint32_t format,
		    uint32_t flags)
{
	struct linux_dmabuf_params *params =
		wl_resource_get_user_data(params_resource);
	struct buffer *buffer;

	buffer = zalloc(sizeof *buffer);

	buffer->proxy =
		zwp_linux_buffer_params_v1_create_immed(params->proxy,
							width, height,
							format, flags);
	buffer->resource = wl_resource_create(client,
					      &wl_buffer_interface,
					      1, buffer_id);
	wl_resource_set_implementation(buffer->resource,
				       &buffer_implementation,
				       buffer, destroy_buffer);
}

static const struct zwp_linux_buffer_params_v1_interface
zwp_linux_buffer_params_implementation = {
	params_destroy,
	params_add,
	params_create,
	params_create_immed
};

static void
server_linux_dmabuf_create_params(struct wl_client *client,
				  struct wl_resource *resource,
				  uint32_t params_id)
{
	struct linux_dmabuf *linux_dmabuf = wl_resource_get_user_data(resource);
	struct linux_dmabuf_params *params;
	uint32_t version;

	version = wl_resource_get_version(resource);

	params = zalloc(sizeof *params);

	params->resource =
		wl_resource_create(client,
				   &zwp_linux_buffer_params_v1_interface,
				   version, params_id);
	wl_resource_set_implementation(params->resource,
				       &zwp_linux_buffer_params_implementation,
				       params, destroy_params);

	params->proxy = zwp_linux_dmabuf_v1_create_params(linux_dmabuf->proxy);
}

static void
client_dmabuf_feedback_format_table(void *data,
				    struct zwp_linux_dmabuf_feedback_v1 *zwp_linux_dmabuf_feedback_v1,
				    int32_t fd, uint32_t size)
{
	struct linux_dmabuf_feedback *feedback = data;

	zwp_linux_dmabuf_feedback_v1_send_format_table(feedback->resource, fd, size);

	close(fd);
}

static void
client_dmabuf_feedback_main_device(void *data,
                                    struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                    struct wl_array *device)
{
	struct linux_dmabuf_feedback *feedback = data;

	zwp_linux_dmabuf_feedback_v1_send_main_device(feedback->resource, device);
}

static void
client_dmabuf_feedback_tranche_target_device(void *data,
                                              struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                              struct wl_array *device)
{
	struct linux_dmabuf_feedback *feedback = data;

	zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(feedback->resource, device);
}

static void
client_dmabuf_feedback_tranche_flags(void *data,
                                      struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                      uint32_t flags)
{
	struct linux_dmabuf_feedback *feedback = data;

	zwp_linux_dmabuf_feedback_v1_send_tranche_flags(feedback->resource, flags);
}

static void
client_dmabuf_feedback_tranche_formats(void *data,
                                        struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback,
                                        struct wl_array *indices)
{
	struct linux_dmabuf_feedback *feedback = data;

	zwp_linux_dmabuf_feedback_v1_send_tranche_formats(feedback->resource,
							  indices);
}

static void
client_dmabuf_feedback_tranche_done(void *data,
                                     struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
	struct linux_dmabuf_feedback *feedback = data;

	zwp_linux_dmabuf_feedback_v1_send_tranche_done(feedback->resource);
}

static void
client_dmabuf_feedback_done(void *data,
                             struct zwp_linux_dmabuf_feedback_v1 *dmabuf_feedback)
{
	struct linux_dmabuf_feedback *feedback = data;

	zwp_linux_dmabuf_feedback_v1_send_done(feedback->resource);
}

static const struct zwp_linux_dmabuf_feedback_v1_listener
dmabuf_feedback_listener = {
   .format_table = client_dmabuf_feedback_format_table,
   .main_device = client_dmabuf_feedback_main_device,
   .tranche_target_device = client_dmabuf_feedback_tranche_target_device,
   .tranche_flags = client_dmabuf_feedback_tranche_flags,
   .tranche_formats = client_dmabuf_feedback_tranche_formats,
   .tranche_done = client_dmabuf_feedback_tranche_done,
   .done = client_dmabuf_feedback_done,
};

static void
destroy_dmabuf_feedback(struct wl_resource *resource)
{
	struct linux_dmabuf_feedback *feedback =
		wl_resource_get_user_data(resource);

	zwp_linux_dmabuf_feedback_v1_destroy(feedback->proxy);
	free(feedback);
}

static void
dmabuf_feedback_destroy(struct wl_client *client, struct wl_resource *resource)
{
	wl_resource_destroy(resource);
}

static const struct zwp_linux_dmabuf_feedback_v1_interface
linux_dmabuf_feedback_implementation = {
	dmabuf_feedback_destroy
};

static void
server_linux_dmabuf_get_default_feedback(struct wl_client *client,
					 struct wl_resource *resource,
					 uint32_t dmabuf_feedback_id)
{
	struct linux_dmabuf *linux_dmabuf = wl_resource_get_user_data(resource);
	struct linux_dmabuf_feedback *feedback;

	feedback = zalloc(sizeof *feedback);
	feedback->proxy =
		zwp_linux_dmabuf_v1_get_default_feedback(linux_dmabuf->proxy);
	zwp_linux_dmabuf_feedback_v1_add_listener(feedback->proxy,
						  &dmabuf_feedback_listener, feedback);

	feedback->resource =
		wl_resource_create(client,
				   &zwp_linux_dmabuf_feedback_v1_interface,
				   wl_resource_get_version(resource),
				   dmabuf_feedback_id);
	wl_resource_set_implementation(feedback->resource,
				       &linux_dmabuf_feedback_implementation,
				       feedback, destroy_dmabuf_feedback);
}

static void
server_linux_dmabuf_get_surface_feedback(struct wl_client *client,
					 struct wl_resource *resource,
					 uint32_t dmabuf_feedback_id,
					 struct wl_resource *surface_resource)
{
	struct linux_dmabuf *linux_dmabuf = wl_resource_get_user_data(resource);
	struct surface *surface = wl_resource_get_user_data(surface_resource);
	struct linux_dmabuf_feedback *feedback;

	feedback = zalloc(sizeof *feedback);
	feedback->proxy =
		zwp_linux_dmabuf_v1_get_surface_feedback(linux_dmabuf->proxy,
							 surface->proxy);
	zwp_linux_dmabuf_feedback_v1_add_listener(feedback->proxy,
						  &dmabuf_feedback_listener, feedback);

	feedback->resource =
		wl_resource_create(client,
				   &zwp_linux_dmabuf_feedback_v1_interface,
				   wl_resource_get_version(resource),
				   dmabuf_feedback_id);
	//wl_resource_add_destroy_listener(feedback->resource, feedback_destructor);
}

static const struct zwp_linux_dmabuf_v1_interface linux_dmabuf_implementation = {
	server_linux_dmabuf_destroy,
	server_linux_dmabuf_create_params,
	server_linux_dmabuf_get_default_feedback,
	server_linux_dmabuf_get_surface_feedback
};

static void
destroy_linux_dmabuf(struct wl_resource *resource)
{
	struct linux_dmabuf *linux_dmabuf = wl_resource_get_user_data(resource);

	zwp_linux_dmabuf_v1_destroy(linux_dmabuf->proxy);
	free(linux_dmabuf);
}

static void
bind_linux_dmabuf(struct wl_client *client,
		  void *data, uint32_t version, uint32_t id)
{
	struct libdecor_gtk4_tunnels *tunnels = data;
	struct linux_dmabuf *linux_dmabuf;

	linux_dmabuf = zalloc(sizeof *linux_dmabuf);
	linux_dmabuf->proxy =
		wl_registry_bind(tunnels->client.wl_registry,
				 tunnels->globals.linux_dmabuf.name,
				 &zwp_linux_dmabuf_v1_interface,
				 version);
	zwp_linux_dmabuf_v1_add_listener(linux_dmabuf->proxy,
					 &dmabuf_listener,
					 linux_dmabuf);

	linux_dmabuf->resource = wl_resource_create(client,
						    &zwp_linux_dmabuf_v1_interface,
						    version, id);
	wl_resource_set_implementation(linux_dmabuf->resource,
				       &linux_dmabuf_implementation,
				       linux_dmabuf, destroy_linux_dmabuf);
	fprintf(stderr, ":::: %s:%d %s() - bound linux dmabuf\n", __FILE__, __LINE__, __func__);

	//wl_display_roundtrip(tunnels->client.wl_display);
}

static void
wayland_drm_handle_device(void *data, struct wl_drm *wl_drm,
			  const char *device)
{
	struct wayland_drm *wayland_drm = data;

	wl_drm_send_device(wayland_drm->resource, device);
}

static void
wayland_drm_handle_format(void *data, struct wl_drm *wl_drm,
			  uint32_t format)
{
	struct wayland_drm *wayland_drm = data;

	wl_drm_send_format(wayland_drm->resource, format);
}

static void
wayland_drm_handle_authenticated(void *data, struct wl_drm *wl_drm)
{
	struct wayland_drm *wayland_drm = data;

	wl_drm_send_authenticated(wayland_drm->resource);
}

static void
wayland_drm_handle_capabilities(void *data, struct wl_drm *wl_drm,
				uint32_t value)
{
	struct wayland_drm *wayland_drm = data;

	wl_drm_send_capabilities(wayland_drm->resource, value);
}

static const struct wl_drm_listener wayland_drm_listener = {
   .device = wayland_drm_handle_device,
   .format = wayland_drm_handle_format,
   .authenticated = wayland_drm_handle_authenticated,
   .capabilities = wayland_drm_handle_capabilities,
};

static void
destroy_wayland_drm(struct wl_resource *resource)
{
	struct wayland_drm *wayland_drm = wl_resource_get_user_data(resource);

	wl_drm_destroy(wayland_drm->proxy);
	free(wayland_drm);
}

static void
server_wayland_drm_authenticate(struct wl_client *client,
				struct wl_resource *resource,
				uint32_t id)
{
	struct wayland_drm *wayland_drm = wl_resource_get_user_data(resource);

	fprintf(stderr, ":::: %s:%d %s() - \n", __FILE__, __LINE__, __func__);
	wl_drm_authenticate(wayland_drm->proxy, id);
	//wl_display_roundtrip(tunnels->client.wl_display);
}

static void
server_wayland_drm_create_buffer(struct wl_client *client,
				 struct wl_resource *resource,
				 uint32_t id,
				 uint32_t name,
				 int32_t width,
				 int32_t height,
				 uint32_t stride,
				 uint32_t format)
{
	struct wayland_drm *wayland_drm = wl_resource_get_user_data(resource);
	struct buffer *buffer;

	buffer = zalloc(sizeof *buffer);
	buffer->proxy = wl_drm_create_buffer(wayland_drm->proxy, name,
					     width, height, stride,
					     format);
	buffer->resource =
		wl_resource_create(client, &wl_buffer_interface, 1, id);
	wl_resource_set_implementation(buffer->resource,
				       &buffer_implementation,
				       buffer, destroy_buffer);
}

static void
server_wayland_drm_create_planar_buffer(struct wl_client *client,
					struct wl_resource *resource,
					uint32_t id,
					uint32_t name,
					int32_t width,
					int32_t height,
					uint32_t format,
					int32_t offset0,
					int32_t stride0,
					int32_t offset1,
					int32_t stride1,
					int32_t offset2,
					int32_t stride2)
{
	struct wayland_drm *wayland_drm = wl_resource_get_user_data(resource);
	struct buffer *buffer;

	buffer = zalloc(sizeof *buffer);

	buffer->proxy =
		wl_drm_create_planar_buffer(wayland_drm->proxy, name,
					    width, height,
					    format,
					    offset0, stride0,
					    offset1, stride1,
					    offset2, stride2);
	buffer->resource =
		wl_resource_create(client, &wl_buffer_interface, 1, id);
	wl_resource_set_implementation(buffer->resource,
				       &buffer_implementation,
				       buffer, destroy_buffer);
}

static void
server_wayland_drm_create_prime_buffer(struct wl_client *client,
				       struct wl_resource *resource,
				       uint32_t id,
				       int fd,
				       int32_t width,
				       int32_t height,
				       uint32_t format,
				       int32_t offset0,
				       int32_t stride0,
				       int32_t offset1,
				       int32_t stride1,
				       int32_t offset2,
				       int32_t stride2)
{
	struct wayland_drm *wayland_drm = wl_resource_get_user_data(resource);
	struct buffer *buffer;

	buffer = zalloc(sizeof *buffer);

	buffer->proxy =
		wl_drm_create_prime_buffer(wayland_drm->proxy, fd,
					   width, height,
					   format,
					   offset0, stride0,
					   offset1, stride1,
					   offset2, stride2);
	buffer->resource =
		wl_resource_create(client, &wl_buffer_interface, 1, id);
	wl_resource_set_implementation(buffer->resource,
				       &buffer_implementation,
				       buffer, destroy_buffer);
	close(fd);
}

static const struct wl_drm_interface wayland_drm_implementation = {
	.authenticate = server_wayland_drm_authenticate,
	.create_buffer = server_wayland_drm_create_buffer,
	.create_planar_buffer = server_wayland_drm_create_planar_buffer,
	.create_prime_buffer = server_wayland_drm_create_prime_buffer,
};

static void
bind_wayland_drm(struct wl_client *client,
		 void *data, uint32_t version, uint32_t id)
{
	struct libdecor_gtk4_tunnels *tunnels = data;
	struct wayland_drm *wayland_drm;

	wayland_drm = zalloc(sizeof *wayland_drm);
	wayland_drm->proxy =
		wl_registry_bind(tunnels->client.wl_registry,
				 tunnels->globals.wayland_drm.name,
				 &wl_drm_interface,
				 version);
	wl_drm_add_listener(wayland_drm->proxy,
			    &wayland_drm_listener,
			    wayland_drm);

	wayland_drm->resource = wl_resource_create(client,
						    &wl_drm_interface,
						    version, id);
	wl_resource_set_implementation(wayland_drm->resource,
				       &wayland_drm_implementation,
				       wayland_drm, destroy_wayland_drm);

	//wl_display_roundtrip(tunnels->client.wl_display);
}

struct wl_global *
linux_dmabuf_new(struct libdecor_gtk4_tunnels *tunnels,
		 uint32_t version)
{
	uint32_t tunneled_version;

	tunneled_version = MIN(version, MAX_LINUX_DMABUF_VERSION);
	return wl_global_create(tunnels->server.wl_display,
				&zwp_linux_dmabuf_v1_interface,
				tunneled_version,
				tunnels, bind_linux_dmabuf);
}

struct wl_global *
wayland_drm_new(struct libdecor_gtk4_tunnels *tunnels,
		uint32_t version)
{
	uint32_t tunneled_version;

	tunneled_version = MIN(version, MAX_WAYLAND_DRM_VERSION);
	return wl_global_create(tunnels->server.wl_display,
				&wl_drm_interface,
				tunneled_version,
				tunnels, bind_wayland_drm);
}

static void
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	struct libdecor_gtk4_tunnels *tunnels = user_data;

	if (strcmp(interface, "wl_compositor") == 0) {
		tunnels->globals.compositor.wl_global = compositor_new(tunnels, version);
		tunnels->globals.compositor.name = id;
	} else if (strcmp(interface, "wl_shm") == 0) {
		tunnels->globals.shm.wl_global = shm_new(tunnels, version);
		tunnels->globals.shm.name = id;
	} else if (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) {
		tunnels->globals.linux_dmabuf.wl_global = linux_dmabuf_new(tunnels, version);
		tunnels->globals.linux_dmabuf.name = id;
	} else if (strcmp(interface, "wl_drm") == 0) {
		tunnels->globals.wayland_drm.wl_global = wayland_drm_new(tunnels, version);
		tunnels->globals.wayland_drm.name = id;
	} else if (strcmp(interface, "wl_output") == 0) {
		struct global *global;

		global = zalloc(sizeof *global);
		global->tunnels = tunnels;
		global->wl_global = output_new(tunnels, version, global);
		global->name = id;

		wl_list_insert(&tunnels->globals.outputs, &global->link);
	}
#if 0
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

struct libdecor_gtk4_tunnels *
libdecor_gtk4_tunnels_new(struct wl_display *client_wl_display,
			  struct wl_display *server_wl_display)
{
	struct libdecor_gtk4_tunnels *tunnels;
	struct wl_proxy *proxy;

	tunnels = zalloc(sizeof *tunnels);
	tunnels->client.wl_display = client_wl_display;
	tunnels->client.wl_registry = wl_display_get_registry(client_wl_display);
	tunnels->server.wl_display = server_wl_display;
	wl_registry_add_listener(tunnels->client.wl_registry,
				 &registry_listener,
				 tunnels);

	wl_list_init(&tunnels->globals.outputs);
	wl_list_init(&tunnels->globals.seats);

	return tunnels;
}

void
libdecor_gtk4_tunnels_free(struct libdecor_gtk4_tunnels *tunnels)
{
	// todo
	free(tunnels);
}
