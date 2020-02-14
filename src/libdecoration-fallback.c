/*
 * Copyright © 2019 Jonas Ådahl
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

#include "libdecoration-fallback.h"
#include "utils.h"

static void
libdecor_plugin_fallback_destroy(struct libdecor_plugin *plugin)
{
	free(plugin);
}

static struct libdecor_frame *
libdecor_plugin_fallback_frame_new(struct libdecor_plugin *plugin)
{
	struct libdecor_frame *frame;

	frame = zalloc(sizeof *frame);

	return frame;
}

static void
libdecor_plugin_fallback_frame_free(struct libdecor_plugin *plugin,
				    struct libdecor_frame *frame)
{
}

static void
libdecor_plugin_fallback_frame_commit(struct libdecor_plugin *plugin,
				      struct libdecor_frame *frame,
				      struct libdecor_state *state,
				      struct libdecor_configuration *configuration)
{
}

static bool
libdecor_plugin_fallback_configuration_get_content_size(struct libdecor_plugin *plugin,
							struct libdecor_configuration *configuration,
							struct libdecor_frame *frame,
							int *content_width,
							int *content_height)
{
	return libdecor_configuration_get_window_size(configuration,
						      content_width,
						      content_height);
}

static struct libdecor_plugin_interface fallback_plugin_iface = {
	.destroy = libdecor_plugin_fallback_destroy,
	.frame_new = libdecor_plugin_fallback_frame_new,
	.frame_free = libdecor_plugin_fallback_frame_free,
	.frame_commit = libdecor_plugin_fallback_frame_commit,
	.configuration_get_content_size = libdecor_plugin_fallback_configuration_get_content_size,
};

struct libdecor_plugin *
libdecor_fallback_plugin_new(struct libdecor *context)
{
	struct libdecor_plugin *plugin;

	plugin = zalloc(sizeof *plugin);
	plugin->iface = &fallback_plugin_iface;

	libdecor_notify_plugin_ready(context);

	return plugin;
}