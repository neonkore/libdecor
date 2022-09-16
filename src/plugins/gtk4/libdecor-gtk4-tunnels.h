/*
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

#ifndef LIBDECOR_GTK4_TUNNEL_H
#define LIBDECOR_GTK4_TUNNEL_H

struct wl_display;

struct libdecor_gtk4_tunnels;

struct surface {
	struct wl_surface *proxy;
	struct wl_resource *resource;

	void *role_data;
};

struct libdecor_gtk4_tunnels *
libdecor_gtk4_tunnels_new(struct wl_display *client_wl_display,
			  struct wl_display *server_wl_display);

void
libdecor_gtk4_tunnels_free(struct libdecor_gtk4_tunnels *tunnels);

#endif /* LIBDECOR_GTK4_TUNNEL_H */
