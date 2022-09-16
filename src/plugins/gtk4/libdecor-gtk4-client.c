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

#include <gtk/gtk.h>
#include <gdk/wayland/gdkwayland.h>

#include "libdecor-shell-client-protocol.h"

static struct libdecor_shell *libdecor_shell;

static void
libdecor_shell_handle_request_frame(void *data,
				    struct libdecor_shell *libdecor_shell,
				    uint32_t serial)
{
	GtkWidget *window;
	GtkWidget *widget;
	GdkSurface *surface;
	struct wl_surface *wl_surface;

	window = gtk_window_new();
	widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_window_set_child(GTK_WINDOW(window), widget);
	gtk_window_present(GTK_WINDOW(window));

	surface = gtk_native_get_surface(GTK_NATIVE(window));
	wl_surface = gdk_wayland_surface_get_wl_surface(surface);
	
	libdecor_shell_create_frame(libdecor_shell, serial, wl_surface);
}

static struct libdecor_shell_listener libdecor_shell_listener = {
	.request_frame = libdecor_shell_handle_request_frame,
};

static void
registry_handle_global(void *user_data,
		       struct wl_registry *wl_registry,
		       uint32_t id,
		       const char *interface,
		       uint32_t version)
{
	if (strcmp(interface, "libdecor_shell") == 0) {
		libdecor_shell =
			wl_registry_bind(wl_registry, id,
					 &libdecor_shell_interface, 1);
		libdecor_shell_add_listener(libdecor_shell,
					    &libdecor_shell_listener,
					    NULL);
	}
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

int
main(int argc, char **argv) {
	GdkDisplay *display;
	GMainLoop *loop;
	struct wl_display *wl_display;
	struct wl_registry *wl_registry;

	setenv("WAYLAND_DEBUG", "client", 0);

	fprintf(stderr, ":::: %s:%d %s() - WAYLAND_SOCKET: %s\n", __FILE__, __LINE__, __func__,
		getenv("WAYLAND_SOCKET"));
	gdk_set_allowed_backends("wayland");
	gtk_init();

	display = gdk_display_get_default();
	wl_display = gdk_wayland_display_get_wl_display(display);
	wl_registry = wl_display_get_registry(wl_display);
	wl_registry_add_listener(wl_registry,
				 &registry_listener,
				 NULL);
	wl_display_roundtrip(wl_display);
	if (!libdecor_shell) {
		fprintf(stderr, "Missing libdecor_shell global, exiting.\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, ":::: %s:%d %s() - \n", __FILE__, __LINE__, __func__);

	loop = g_main_loop_new(NULL, FALSE);
	g_main_loop_run(loop);
	return EXIT_SUCCESS;
}
