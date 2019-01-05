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

#ifndef LIBDECORATION_PLUGIN_H
#define LIBDECORATION_PLUGIN_H

#include "libdecoration.h"

struct libdecor_frame_private;

struct libdecor_frame {
	struct libdecor_frame_private *priv;
	struct wl_list link;
};

struct libdecor_plugin_private;

struct libdecor_plugin {
	struct libdecor_plugin_interface *iface;
	struct libdecor_plugin_private *private;
};

typedef struct libdecor_plugin * (* libdecor_plugin_constructor)(struct libdecor *context);

struct libdecor_plugin_interface {
	void (* destroy)(struct libdecor_plugin *plugin);
};

void
libdecor_notify_plugin_ready(struct libdecor *context);

void
libdecor_notify_plugin_error(struct libdecor *context,
			     enum libdecor_error error,
			     const char *message);

#endif /* LIBDECORATION_PLUGIN_H */
