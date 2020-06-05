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

#include <linux/input.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <wayland-cursor.h>

#include "libdecoration-plugin.h"
#include "utils.h"
#include "cursor-settings.h"

#include "xdg-shell-client-protocol.h"

#include <cairo/cairo.h>

#include "libdecoration-cairo-blur.h"

static const size_t SHADOW_MARGIN = 24;	/* graspable part of the border */
static const size_t TITLE_HEIGHT = 24;
static const size_t BUTTON_WIDTH = 32;
static const size_t SYM_DIM = 14;

static const uint32_t COL_TITLE = 0xFF080706;
static const uint32_t COL_BUTTON_MIN = 0xFFFFBB00;
static const uint32_t COL_BUTTON_MAX = 0xFF238823;
static const uint32_t COL_BUTTON_CLOSE = 0xFFFB6542;
static const uint32_t COL_SYM = 0xFFF4F4EF;
static const uint32_t COL_SYM_ACT = 0xFF20322A;

static const uint32_t DOUBLE_CLICK_TIME_MS = 400;

static const char *cursor_names[] = {
	"top_side",
	"bottom_side",
	"left_side",
	"top_left_corner",
	"bottom_left_corner",
	"right_side",
	"top_right_corner",
	"bottom_right_corner"
};


/* color conversion function from 32bit integer to double components */

double
red(const uint32_t *const col) {
	return ((const uint8_t*)(col))[2] / (double)(255);
}

double
green(const uint32_t *const col) {
	return ((const uint8_t*)(col))[1] / (double)(255);
}

double
blue(const uint32_t *const col) {
	return ((const uint8_t*)(col))[0] / (double)(255);
}

double
alpha(const uint32_t *const col) {
	return ((const uint8_t*)(col))[3] / (double)(255);
}

void
cairo_set_rgba32(cairo_t *cr, const uint32_t *const c) {
	cairo_set_source_rgba(cr, red(c), green(c), blue(c), alpha(c));
}

enum decoration_type {
	DECORATION_TYPE_NONE,
	DECORATION_TYPE_ALL,
	DECORATION_TYPE_TITLE_ONLY
};

enum component {
	NONE = 0,
	SHADOW,
	TITLE,
	BUTTON_MIN,
	BUTTON_MAX,
	BUTTON_CLOSE,
};

struct seat {
	struct libdecor_plugin_cairo *plugin_cairo;

	struct wl_seat *wl_seat;
	struct wl_pointer *wl_pointer;

	struct wl_surface *cursor_surface;

	struct wl_surface *pointer_focus;

	int pointer_x, pointer_y;

	uint32_t pointer_button_time_stamp;

	uint32_t serial;

	struct wl_list link;
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
	enum component type;
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

	struct border_component *active;

	bool shadow_showing;
	struct border_component shadow;

	struct {
		bool is_showing;
		struct border_component title;
		struct border_component min;
		struct border_component max;
		struct border_component close;
	} title_bar;

	/* store pre-processed shadow tile */
	cairo_surface_t *shadow_blur;
};

struct libdecor_plugin_cairo {
	struct libdecor_plugin plugin;

	struct wl_callback *globals_callback;
	struct wl_callback *globals_callback_shm;

	struct libdecor *context;

	struct wl_registry *wl_registry;
	struct wl_subcompositor *wl_subcompositor;
	struct wl_compositor *wl_compositor;

	struct wl_shm *wl_shm;
	struct wl_callback *shm_callback;
	bool has_argb;

	struct wl_list seat_list;

	struct wl_cursor_theme *cursor_theme;
	char *cursor_theme_name;
	int cursor_size;

	/* cursors for resize edges and corners */
	struct wl_cursor *cursors[ARRAY_LENGTH(cursor_names)];

	struct wl_cursor *cursor_left_ptr;
};

static const char *libdecoration_cairo_proxy_tag = "libdecoration-cairo";

static bool
own_surface(struct wl_surface *surface)
{
	return (wl_proxy_get_tag((struct wl_proxy *) surface) ==
		&libdecoration_cairo_proxy_tag);
}

struct libdecor_plugin *
libdecor_plugin_new(struct libdecor *context);

static void
buffer_free(struct buffer *buffer);

static void
libdecor_plugin_cairo_destroy(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_cairo *plugin_cairo =
		(struct libdecor_plugin_cairo *) plugin;
	struct seat *seat;

	if (plugin_cairo->globals_callback)
		wl_callback_destroy(plugin_cairo->globals_callback);
	if (plugin_cairo->globals_callback_shm)
		wl_callback_destroy(plugin_cairo->globals_callback_shm);
	if (plugin_cairo->shm_callback)
		wl_callback_destroy(plugin_cairo->shm_callback);
	wl_registry_destroy(plugin_cairo->wl_registry);

	wl_list_for_each(seat, &plugin_cairo->seat_list, link) {
		if (seat->wl_pointer)
			wl_pointer_destroy(seat->wl_pointer);
		if (seat->cursor_surface)
			wl_surface_destroy(seat->cursor_surface);
		wl_seat_destroy(seat->wl_seat);
		free(seat);
	}

	if (plugin_cairo->cursor_theme)
		wl_cursor_theme_destroy(plugin_cairo->cursor_theme);

	free(plugin_cairo->cursor_theme_name);

	wl_shm_destroy(plugin_cairo->wl_shm);

	wl_compositor_destroy(plugin_cairo->wl_compositor);
	wl_subcompositor_destroy(plugin_cairo->wl_subcompositor);

	free(plugin_cairo);
}

static struct libdecor_frame_cairo *
libdecor_frame_cairo_new(struct libdecor_plugin_cairo *plugin_cairo)
{
	struct libdecor_frame_cairo *frame_cairo = zalloc(sizeof *frame_cairo);
	cairo_t *cr;

	static const int size = 128;
	static const int boundary = 32;

	frame_cairo->plugin_cairo = plugin_cairo;
	frame_cairo->shadow_blur = cairo_image_surface_create(
					CAIRO_FORMAT_ARGB32, size, size);

	cr = cairo_create(frame_cairo->shadow_blur);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_rectangle(cr, boundary, boundary, size-2*boundary, size-2*boundary);
	cairo_fill(cr);
	cairo_destroy(cr);
	blur_surface(frame_cairo->shadow_blur, 64);

	return frame_cairo;
}

static struct libdecor_frame *
libdecor_plugin_cairo_frame_new(struct libdecor_plugin *plugin)
{
	struct libdecor_plugin_cairo *plugin_cairo =
		(struct libdecor_plugin_cairo *) plugin;
	struct libdecor_frame_cairo *frame_cairo;

	frame_cairo = libdecor_frame_cairo_new(plugin_cairo);

	return &frame_cairo->frame;
}

static int
create_anonymous_file(off_t size)
{
	int ret;

	int fd;

	fd = memfd_create("libdecor-cairo", MFD_CLOEXEC | MFD_ALLOW_SEALING);

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

static void
toggle_maximized(struct libdecor_frame *const frame)
{
	if (!(libdecor_frame_get_window_state(frame) &
	      LIBDECOR_WINDOW_STATE_MAXIMIZED))
		libdecor_frame_set_maximized(frame);
	else
		libdecor_frame_unset_maximized(frame);
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
	if (border_component->buffer) {
		buffer_free(border_component->buffer);
		border_component->buffer = NULL;
	}
}

static void
libdecor_plugin_cairo_frame_free(struct libdecor_plugin *plugin,
				 struct libdecor_frame *frame)
{
	struct libdecor_frame_cairo *frame_cairo =
		(struct libdecor_frame_cairo *) frame;

	free_border_component(&frame_cairo->title_bar.title);
	free_border_component(&frame_cairo->title_bar.min);
	free_border_component(&frame_cairo->title_bar.max);
	free_border_component(&frame_cairo->title_bar.close);
	free_border_component(&frame_cairo->shadow);
	if (frame_cairo->shadow_blur != NULL) {
		cairo_surface_destroy(frame_cairo->shadow_blur);
		frame_cairo->shadow_blur = NULL;
	}

	frame_cairo->decoration_type = DECORATION_TYPE_NONE;
}

static bool
is_border_surfaces_showing(struct libdecor_frame_cairo *frame_cairo)
{
	return frame_cairo->shadow_showing;
}

static bool
is_title_bar_surfaces_showing(struct libdecor_frame_cairo *frame_cairo)
{
	return frame_cairo->title_bar.is_showing;
}

static void
hide_border_component(struct border_component *border_component)
{
	if (!border_component->wl_surface)
		return;

	wl_surface_attach(border_component->wl_surface, NULL, 0, 0);
	wl_surface_commit(border_component->wl_surface);
}

static void
hide_border_surfaces(struct libdecor_frame_cairo *frame_cairo)
{
	hide_border_component(&frame_cairo->shadow);
	frame_cairo->shadow_showing = false;
}

static void
hide_title_bar_surfaces(struct libdecor_frame_cairo *frame_cairo)
{
	hide_border_component(&frame_cairo->title_bar.title);
	hide_border_component(&frame_cairo->title_bar.min);
	hide_border_component(&frame_cairo->title_bar.max);
	hide_border_component(&frame_cairo->title_bar.close);
	frame_cairo->title_bar.is_showing = false;
}

static void
create_surface_subsurface_pair(struct libdecor_frame_cairo *frame_cairo,
			       struct wl_surface **out_wl_surface,
			       struct wl_subsurface **out_wl_subsurface)
{
	struct libdecor_plugin_cairo *plugin_cairo = frame_cairo->plugin_cairo;
	struct libdecor_frame *frame = &frame_cairo->frame;
	struct wl_compositor *wl_compositor = plugin_cairo->wl_compositor;
	struct wl_subcompositor *wl_subcompositor = plugin_cairo->wl_subcompositor;
	struct wl_surface *wl_surface;
	struct wl_surface *parent;
	struct wl_subsurface *wl_subsurface;

	wl_surface = wl_compositor_create_surface(wl_compositor);
	wl_surface_set_user_data(wl_surface, frame_cairo);
	wl_proxy_set_tag((struct wl_proxy *) wl_surface,
			 &libdecoration_cairo_proxy_tag);

	parent = libdecor_frame_get_wl_surface(frame);
	wl_subsurface = wl_subcompositor_get_subsurface(wl_subcompositor,
							wl_surface,
							parent);

	*out_wl_surface = wl_surface;
	*out_wl_subsurface = wl_subsurface;
}

static void
ensure_component(struct libdecor_frame_cairo *frame_cairo,
		 struct border_component *cmpnt)
{
	if (!cmpnt->wl_surface) {
		create_surface_subsurface_pair(frame_cairo,
					       &cmpnt->wl_surface,
					       &cmpnt->wl_subsurface);
		wl_surface_set_user_data(cmpnt->wl_surface, frame_cairo);
	}
}

static void
ensure_border_surfaces(struct libdecor_frame_cairo *frame_cairo)
{
	if (frame_cairo->shadow.type == NONE) {
		frame_cairo->shadow.type = SHADOW;
		ensure_component(frame_cairo, &frame_cairo->shadow);
	}

	libdecor_frame_set_min_content_size(&frame_cairo->frame,
					    MAX(56, 4 * BUTTON_WIDTH),
					    MAX(56, TITLE_HEIGHT + 1));
}

static void
ensure_title_bar_surfaces(struct libdecor_frame_cairo *frame_cairo)
{
	ensure_component(frame_cairo, &frame_cairo->title_bar.title);
	frame_cairo->title_bar.title.type = TITLE;
	ensure_component(frame_cairo, &frame_cairo->title_bar.min);
	frame_cairo->title_bar.min.type = BUTTON_MIN;
	ensure_component(frame_cairo, &frame_cairo->title_bar.max);
	frame_cairo->title_bar.max.type = BUTTON_MAX;
	ensure_component(frame_cairo, &frame_cairo->title_bar.close);
	frame_cairo->title_bar.close.type = BUTTON_CLOSE;
}

static void
calculate_component_size(struct libdecor_frame_cairo *frame_cairo,
			 enum component component,
			 int *component_x,
			 int *component_y,
			 int *component_width,
			 int *component_height)
{
	struct libdecor_frame *frame = &frame_cairo->frame;
	int content_width, content_height;

	content_width = libdecor_frame_get_content_width(frame);
	content_height = libdecor_frame_get_content_height(frame);

	switch (component) {
	case NONE:
		return;
	case SHADOW:
		*component_x = -(int)SHADOW_MARGIN;
		*component_y = -(int)(SHADOW_MARGIN+TITLE_HEIGHT);
		*component_width = content_width + 2 * SHADOW_MARGIN;
		*component_height = content_height
				    + 2 * SHADOW_MARGIN
				    + TITLE_HEIGHT;
		return;
	case TITLE:
		*component_x = 0;
		*component_y = -(int)TITLE_HEIGHT;
		*component_width = content_width;
		*component_height = TITLE_HEIGHT;
		return;
	case BUTTON_MIN:
		*component_x = content_width - 3 * BUTTON_WIDTH;
		*component_y = -(int)TITLE_HEIGHT;
		*component_width = BUTTON_WIDTH;
		*component_height = TITLE_HEIGHT;
		return;
	case BUTTON_MAX:
		*component_x = content_width - 2 * BUTTON_WIDTH;
		*component_y = -(int)TITLE_HEIGHT;
		*component_width = BUTTON_WIDTH;
		*component_height = TITLE_HEIGHT;
		return;
	case BUTTON_CLOSE:
		*component_x = content_width - BUTTON_WIDTH;
		*component_y = -(int)TITLE_HEIGHT;
		*component_width = BUTTON_WIDTH;
		*component_height = TITLE_HEIGHT;
		return;
	}

	abort();
}

static void
draw_component_content(struct libdecor_frame_cairo *frame_cairo,
		       struct buffer *buffer,
		       enum component component)
{
	cairo_surface_t *surface;
	cairo_t *cr;

	/* button symbol origin */
	const double x = BUTTON_WIDTH / 2 - SYM_DIM / 2 + 0.5;
	const double y = TITLE_HEIGHT / 2 - SYM_DIM / 2 + 0.5;

	enum libdecor_window_state state;

	/* title fade out at buttons */
	const int fade_width = 5 * BUTTON_WIDTH;
	int fade_start;
	cairo_pattern_t *fade;

	/* clear buffer */
	memset(buffer->data, 0, buffer->data_size);

	surface = cairo_image_surface_create_for_data(
			  buffer->data, CAIRO_FORMAT_ARGB32,
			  buffer->width, buffer->height,
			  cairo_format_stride_for_width(
				  CAIRO_FORMAT_ARGB32,
				  buffer->width)
			  );

	cr = cairo_create(surface);

	/* background */
	switch (component) {
	case NONE:
		break;
	case SHADOW:
		render_shadow(cr,
			      frame_cairo->shadow_blur,
			      -(int)SHADOW_MARGIN/2,
			      -(int)SHADOW_MARGIN/2,
			      buffer->width + SHADOW_MARGIN,
			      buffer->height + SHADOW_MARGIN,
			      64,
			      64);
		break;
	case TITLE:
		cairo_set_rgba32(cr, &COL_TITLE);
		cairo_paint(cr);
		break;
	case BUTTON_MIN:
		if (frame_cairo->active == &frame_cairo->title_bar.min)
			cairo_set_rgba32(cr, &COL_BUTTON_MIN);
		else
			cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		break;
	case BUTTON_MAX:
		if (frame_cairo->active == &frame_cairo->title_bar.max)
			cairo_set_rgba32(cr, &COL_BUTTON_MAX);
		else
			cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		break;
	case BUTTON_CLOSE:
		if (frame_cairo->active == &frame_cairo->title_bar.close)
			cairo_set_rgba32(cr, &COL_BUTTON_CLOSE);
		else
			cairo_set_source_rgba(cr, 0, 0, 0, 0);
		cairo_paint(cr);
		break;
	}

	/* button symbols */
	/* https://www.cairographics.org/FAQ/#sharp_lines */
	cairo_set_line_width(cr, 1);

	switch (component) {
	case TITLE:
		cairo_select_font_face(cr, "sans-serif",
				       CAIRO_FONT_SLANT_NORMAL,
				       CAIRO_FONT_WEIGHT_BOLD);
		cairo_set_font_size(cr, SYM_DIM);
		cairo_set_rgba32(cr, &COL_SYM);
		cairo_move_to(cr, BUTTON_WIDTH, y + SYM_DIM - 1);
		cairo_show_text(cr, libdecor_frame_get_title(
					(struct libdecor_frame*)frame_cairo));
		fade_start = libdecor_frame_get_content_width(
				(struct libdecor_frame *)frame_cairo)-fade_width;
		fade = cairo_pattern_create_linear(
			       fade_start, 0, fade_start + 2 * BUTTON_WIDTH, 0);
		cairo_pattern_add_color_stop_rgba(fade, 0, 0, 0, 0, 0);
		cairo_pattern_add_color_stop_rgb(fade, 1,
						 red(&COL_TITLE),
						 green(&COL_TITLE),
						 blue(&COL_TITLE));
		cairo_rectangle(cr, fade_start, 0, fade_width, TITLE_HEIGHT);
		cairo_set_source(cr, fade);
		cairo_fill(cr);
		cairo_pattern_destroy(fade);
		break;
	case BUTTON_MIN:
		if (frame_cairo->active == &frame_cairo->title_bar.min)
			cairo_set_rgba32(cr, &COL_SYM_ACT);
		else
			cairo_set_rgba32(cr, &COL_SYM);
		cairo_move_to(cr, x, y + SYM_DIM - 1);
		cairo_rel_line_to(cr, SYM_DIM - 1, 0);
		cairo_stroke(cr);
		break;
	case BUTTON_MAX:
		if (frame_cairo->active == &frame_cairo->title_bar.max)
			cairo_set_rgba32(cr, &COL_SYM_ACT);
		else
			cairo_set_rgba32(cr, &COL_SYM);

		state = libdecor_frame_get_window_state(
				(struct libdecor_frame*)frame_cairo);
		if (state & LIBDECOR_WINDOW_STATE_MAXIMIZED) {
			const size_t small = 12;
			cairo_rectangle(cr,
					x,
					y + SYM_DIM - small,
					small - 1,
					small - 1);
			cairo_move_to(cr,
				      x + SYM_DIM - small,
				      y + SYM_DIM - small);
			cairo_line_to(cr, x + SYM_DIM - small, y);
			cairo_rel_line_to(cr, small - 1, 0);
			cairo_rel_line_to(cr, 0, small - 1);
			cairo_line_to(cr, x + small - 1, y + small - 1);
		}
		else {
			cairo_rectangle(cr, x, y, SYM_DIM - 1, SYM_DIM - 1);
		}
		cairo_stroke(cr);
		break;
	case BUTTON_CLOSE:
		if (frame_cairo->active == &frame_cairo->title_bar.close)
			cairo_set_rgba32(cr, &COL_SYM_ACT);
		else
			cairo_set_rgba32(cr, &COL_SYM);
		cairo_move_to(cr, x, y);
		cairo_rel_line_to(cr, SYM_DIM - 1, SYM_DIM - 1);
		cairo_move_to(cr, x + SYM_DIM - 1, y);
		cairo_line_to(cr, x, y + SYM_DIM - 1);
		cairo_stroke(cr);
		break;
	default:
		break;
	}

	/* mask the toplevel surface */
	if (component == SHADOW) {
		int component_x, component_y, component_width, component_height;
		calculate_component_size(frame_cairo, component,
					 &component_x, &component_y,
					 &component_width, &component_height);
		cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
		cairo_rectangle(cr, -component_x, -component_y,
				libdecor_frame_get_content_width(
					&frame_cairo->frame),
				libdecor_frame_get_content_height(
					&frame_cairo->frame));
		cairo_fill(cr);
	}

	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

static void
set_component_input_region(struct libdecor_frame_cairo *frame_cairo,
			   struct border_component *border_component)
{
	if (border_component->type == SHADOW && frame_cairo->shadow_showing) {
		struct wl_region *input_region;
		int component_x;
		int component_y;
		int component_width;
		int component_height;

		calculate_component_size(frame_cairo, border_component->type,
					 &component_x, &component_y,
					 &component_width, &component_height);

		/*
		 * the input region is the outer surface size minus the inner
		 * content size
		 */
		input_region = wl_compositor_create_region(
				       frame_cairo->plugin_cairo->wl_compositor);
		wl_region_add(input_region, 0, 0,
			      component_width, component_height);
		wl_region_subtract(input_region, -component_x, -component_y,
			libdecor_frame_get_content_width(&frame_cairo->frame),
			libdecor_frame_get_content_height(&frame_cairo->frame));
		wl_surface_set_input_region(border_component->wl_surface,
					    input_region);
		wl_region_destroy(input_region);
	}
}

static void
draw_border_component(struct libdecor_frame_cairo *frame_cairo,
		      struct border_component *border_component,
		      enum component component)
{
	struct libdecor_plugin_cairo *plugin_cairo = frame_cairo->plugin_cairo;
	struct buffer *old_buffer;
	struct buffer *buffer = NULL;
	int component_x;
	int component_y;
	int component_width;
	int component_height;

	calculate_component_size(frame_cairo, component,
				 &component_x, &component_y,
				 &component_width, &component_height);

	set_component_input_region(frame_cairo, border_component);

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

	if (!buffer)
		buffer = create_shm_buffer(plugin_cairo,
					   component_width,
					   component_height);

	draw_component_content(frame_cairo, buffer, component);

	wl_surface_attach(border_component->wl_surface, buffer->wl_buffer, 0, 0);
	buffer->in_use = true;
	wl_surface_commit(border_component->wl_surface);
	wl_surface_damage(border_component->wl_surface,
			  0, 0, component_width, component_height);
	wl_subsurface_set_position(border_component->wl_subsurface,
				   component_x, component_y);

	border_component->buffer = buffer;
}

static void
draw_border(struct libdecor_frame_cairo *frame_cairo)
{
	draw_border_component(frame_cairo, &frame_cairo->shadow, SHADOW);
	frame_cairo->shadow_showing = true;
}

static void
draw_title_bar(struct libdecor_frame_cairo *frame_cairo)
{
	draw_border_component(frame_cairo,
			      &frame_cairo->title_bar.title, TITLE);
	draw_border_component(frame_cairo,
			      &frame_cairo->title_bar.min, BUTTON_MIN);
	draw_border_component(frame_cairo,
			      &frame_cairo->title_bar.max, BUTTON_MAX);
	draw_border_component(frame_cairo,
			      &frame_cairo->title_bar.close, BUTTON_CLOSE);
	frame_cairo->title_bar.is_showing = true;
}

static void
draw_decoration(struct libdecor_frame_cairo *frame_cairo)
{
	switch (frame_cairo->decoration_type) {
	case DECORATION_TYPE_NONE:
		if (is_border_surfaces_showing(frame_cairo))
			hide_border_surfaces(frame_cairo);
		if (is_title_bar_surfaces_showing(frame_cairo))
			hide_title_bar_surfaces(frame_cairo);
		break;
	case DECORATION_TYPE_ALL:
		/* show borders */
		ensure_border_surfaces(frame_cairo);
		draw_border(frame_cairo);
		/* show title bar */
		ensure_title_bar_surfaces(frame_cairo);
		draw_title_bar(frame_cairo);
		break;
	case DECORATION_TYPE_TITLE_ONLY:
		/* hide borders */
		if (is_border_surfaces_showing(frame_cairo))
			hide_border_surfaces(frame_cairo);
		/* show title bar */
		ensure_title_bar_surfaces(frame_cairo);
		draw_title_bar(frame_cairo);
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
		x = 0;
		y = 0;
		width = frame_cairo->content_width;
		height = frame_cairo->content_height;
		break;
	case DECORATION_TYPE_ALL:
		x = 0;
		y = -(int)TITLE_HEIGHT;
		width = frame_cairo->content_width;
		height = frame_cairo->content_height + TITLE_HEIGHT;
		break;
	case DECORATION_TYPE_TITLE_ONLY:
		x = 0;
		y = -(int)TITLE_HEIGHT;
		width = frame_cairo->content_width;
		height = frame_cairo->content_height + TITLE_HEIGHT;
		break;
	}

	xdg_surface = libdecor_frame_get_xdg_surface(frame);
	xdg_surface_set_window_geometry(xdg_surface, x, y, width, height);
}

static enum decoration_type
window_state_to_decoration_type(enum libdecor_window_state window_state)
{
	if (window_state & LIBDECOR_WINDOW_STATE_FULLSCREEN)
		return DECORATION_TYPE_NONE;
	else if (window_state & LIBDECOR_WINDOW_STATE_MAXIMIZED ||
		 window_state & LIBDECOR_WINDOW_STATE_TILED_LEFT ||
		 window_state & LIBDECOR_WINDOW_STATE_TILED_RIGHT ||
		 window_state & LIBDECOR_WINDOW_STATE_TILED_TOP ||
		 window_state & LIBDECOR_WINDOW_STATE_TILED_BOTTOM)
		/* title bar, no shadows */
		return DECORATION_TYPE_TITLE_ONLY;
	else
		/* title bar, shadows */
		return DECORATION_TYPE_ALL;
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
libdecor_plugin_cairo_configuration_get_content_size(
		struct libdecor_plugin *plugin,
		struct libdecor_configuration *configuration,
		struct libdecor_frame *frame,
		int *content_width,
		int *content_height)
{
	int win_width, win_height;
	if (!libdecor_configuration_get_window_size(configuration,
						    &win_width,
						    &win_height))
		return false;

	/*
	 * 'libdecor_configuration_get_window_state' returns the pending state,
	 * while 'libdecor_frame_get_window_state' gives the current state
	 */
	enum libdecor_window_state state;
	if (!libdecor_configuration_get_window_state(configuration, &state)) {
		return false;
	}

	switch (window_state_to_decoration_type(state)) {
	case DECORATION_TYPE_NONE:
		*content_width = win_width;
		*content_height = win_height;
		break;
	case DECORATION_TYPE_ALL:
		*content_width = win_width;
		*content_height = win_height - TITLE_HEIGHT;
		break;
	case DECORATION_TYPE_TITLE_ONLY:
		*content_width = win_width;
		*content_height = win_height - TITLE_HEIGHT;
		break;
	}

	return true;
}

static struct libdecor_plugin_interface cairo_plugin_iface = {
	.destroy = libdecor_plugin_cairo_destroy,

	.frame_new = libdecor_plugin_cairo_frame_new,
	.frame_free = libdecor_plugin_cairo_frame_free,
	.frame_commit = libdecor_plugin_cairo_frame_commit,

	.configuration_get_content_size =
			libdecor_plugin_cairo_configuration_get_content_size,
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

	wl_callback_destroy(callback);
	plugin_cairo->globals_callback_shm = NULL;

	if (!plugin_cairo->has_argb) {
		libdecor_notify_plugin_error(
				context,
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

	plugin_cairo->globals_callback_shm = wl_display_sync(wl_display);
	wl_callback_add_listener(plugin_cairo->globals_callback_shm,
				 &shm_callback_listener,
				 plugin_cairo);
}

static void
ensure_cursor_surface(struct seat *seat)
{
	struct wl_compositor *wl_compositor = seat->plugin_cairo->wl_compositor;

	if (seat->cursor_surface)
		return;

	seat->cursor_surface = wl_compositor_create_surface(wl_compositor);
}

static void
ensure_cursor_theme(struct libdecor_plugin_cairo *plugin_cairo)
{
	if (!plugin_cairo->cursor_theme)
		plugin_cairo->cursor_theme = wl_cursor_theme_load(
						     plugin_cairo->cursor_theme_name,
						     plugin_cairo->cursor_size,
						     plugin_cairo->wl_shm);

	for (unsigned int i = 0; i < ARRAY_LENGTH(cursor_names); i++) {
		plugin_cairo->cursors[i] = wl_cursor_theme_get_cursor(
						   plugin_cairo->cursor_theme,
						   cursor_names[i]);
	}

	plugin_cairo->cursor_left_ptr =
		wl_cursor_theme_get_cursor(plugin_cairo->cursor_theme,
					   "left_ptr");
}

enum libdecor_resize_edge
component_edge(const struct border_component *cmpnt,
	       const int pointer_x,
	       const int pointer_y,
	       const int margin)
{
	const bool top = pointer_y < margin;
	const bool bottom = pointer_y > (cmpnt->buffer->height - margin);
	const bool left = pointer_x < margin;
	const bool right = pointer_x > (cmpnt->buffer->width - margin);

	if (top)
		if (left)
			return LIBDECOR_RESIZE_EDGE_TOP_LEFT;
		else if (right)
			return LIBDECOR_RESIZE_EDGE_TOP_RIGHT;
		else
			return LIBDECOR_RESIZE_EDGE_TOP;
	else if (bottom)
		if (left)
			return LIBDECOR_RESIZE_EDGE_BOTTOM_LEFT;
		else if (right)
			return LIBDECOR_RESIZE_EDGE_BOTTOM_RIGHT;
		else
			return LIBDECOR_RESIZE_EDGE_BOTTOM;
	else if (left)
		return LIBDECOR_RESIZE_EDGE_LEFT;
	else if (right)
		return LIBDECOR_RESIZE_EDGE_RIGHT;
	else
		return LIBDECOR_RESIZE_EDGE_NONE;
}

void
set_cursor(struct seat *seat, struct wl_pointer *wl_pointer)
{
	if (!seat->pointer_focus)
		return;

	struct libdecor_plugin_cairo *plugin_cairo = seat->plugin_cairo;
	struct libdecor_frame_cairo *frame_cairo =
			wl_surface_get_user_data(seat->pointer_focus);
	struct wl_cursor *wl_cursor = NULL;
	enum libdecor_resize_edge edge = LIBDECOR_RESIZE_EDGE_NONE;
	struct wl_cursor_image *image;
	struct wl_buffer *buffer;

	if (!frame_cairo || !frame_cairo->active)
		return;

	switch (frame_cairo->active->type) {
	case NONE:
		wl_cursor = NULL;
		break;
	case SHADOW:
		edge = component_edge(frame_cairo->active, seat->pointer_x,
				      seat->pointer_y, SHADOW_MARGIN);
		break;
	case TITLE:
	case BUTTON_MIN:
	case BUTTON_MAX:
	case BUTTON_CLOSE:
		wl_cursor = plugin_cairo->cursor_left_ptr;
		break;
	}

	if (edge != LIBDECOR_RESIZE_EDGE_NONE)
		wl_cursor = plugin_cairo->cursors[edge-1];

	if (wl_cursor == NULL)
		return;

	image = wl_cursor->images[0];
	buffer = wl_cursor_image_get_buffer(image);
	wl_pointer_set_cursor(wl_pointer, seat->serial,
			      seat->cursor_surface,
			      image->hotspot_x,
			      image->hotspot_y);
	wl_surface_attach(seat->cursor_surface, buffer, 0, 0);
	wl_surface_damage(seat->cursor_surface, 0, 0,
			  image->width, image->height);
	wl_surface_commit(seat->cursor_surface);
}

static void
pointer_enter(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface,
	      wl_fixed_t surface_x,
	      wl_fixed_t surface_y)
{
	if (!surface)
		return;

	struct seat *seat = data;
	struct libdecor_plugin_cairo *plugin_cairo = seat->plugin_cairo;
	struct libdecor_frame_cairo *frame_cairo;

	if (!own_surface(surface))
		return;

	frame_cairo = wl_surface_get_user_data(surface);

	ensure_cursor_surface(seat);
	ensure_cursor_theme(plugin_cairo);

	seat->pointer_x = wl_fixed_to_int(surface_x);
	seat->pointer_y = wl_fixed_to_int(surface_y);
	seat->serial = serial;
	seat->pointer_focus = surface;

	if (!frame_cairo)
		return;

	frame_cairo->active = NULL;

	if (seat->pointer_focus == frame_cairo->shadow.wl_surface) {
		frame_cairo->active = &frame_cairo->shadow;
	}
	else if (seat->pointer_focus == frame_cairo->title_bar.title.wl_surface) {
		frame_cairo->active = &frame_cairo->title_bar.title;
	}
	else if (seat->pointer_focus == frame_cairo->title_bar.min.wl_surface) {
		frame_cairo->active = &frame_cairo->title_bar.min;
	}
	else if (seat->pointer_focus == frame_cairo->title_bar.max.wl_surface) {
		frame_cairo->active = &frame_cairo->title_bar.max;
	}
	else if (seat->pointer_focus == frame_cairo->title_bar.close.wl_surface) {
		frame_cairo->active = &frame_cairo->title_bar.close;
	}

	/* update decorations */
	if (frame_cairo->active) {
		draw_decoration(frame_cairo);
		libdecor_frame_toplevel_commit(&frame_cairo->frame);
	}

	set_cursor(seat, wl_pointer);
}

static void
pointer_leave(void *data,
	      struct wl_pointer *wl_pointer,
	      uint32_t serial,
	      struct wl_surface *surface)
{
	if (!surface)
		return;

	struct seat *seat = data;
	struct libdecor_frame_cairo *frame_cairo;

	if (!own_surface(surface))
		return;

	frame_cairo = wl_surface_get_user_data(surface);

	seat->pointer_focus = NULL;
	if (frame_cairo) {
		frame_cairo->active = NULL;
		draw_decoration(frame_cairo);
		libdecor_frame_toplevel_commit(&frame_cairo->frame);
	}
}

static void
pointer_motion(void *data,
	       struct wl_pointer *wl_pointer,
	       uint32_t time,
	       wl_fixed_t surface_x,
	       wl_fixed_t surface_y)
{
	struct seat *seat = data;

	seat->pointer_x = wl_fixed_to_int(surface_x);
	seat->pointer_y = wl_fixed_to_int(surface_y);
	set_cursor(seat, wl_pointer);
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
	struct libdecor_frame_cairo *frame_cairo;

	if (!seat->pointer_focus || !own_surface(seat->pointer_focus))
		return;

	frame_cairo = wl_surface_get_user_data(seat->pointer_focus);
	if (!frame_cairo)
		return;

	if (button == BTN_LEFT) {
		if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
			enum libdecor_resize_edge edge =
					LIBDECOR_RESIZE_EDGE_NONE;
			switch (frame_cairo->active->type) {
			case SHADOW:
				edge = component_edge(frame_cairo->active,
						      seat->pointer_x,
						      seat->pointer_y,
						      SHADOW_MARGIN);
				break;
			case TITLE:
				if (time-seat->pointer_button_time_stamp <
				    DOUBLE_CLICK_TIME_MS) {
					toggle_maximized(&frame_cairo->frame);
				}
				else {
					seat->pointer_button_time_stamp = time;
					libdecor_frame_move(&frame_cairo->frame,
							    seat->wl_seat,
							    serial);
				}
				break;
			default:
				break;
			}

			if (edge != LIBDECOR_RESIZE_EDGE_NONE) {
				libdecor_frame_resize(
					&frame_cairo->frame,
					seat->wl_seat,
					serial,
					edge);
			}
		}
		else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
			switch (frame_cairo->active->type) {
			case BUTTON_MIN:
				libdecor_frame_set_minimized(
							&frame_cairo->frame);
				break;
			case BUTTON_MAX:
				toggle_maximized(&frame_cairo->frame);
				break;
			case BUTTON_CLOSE:
				libdecor_frame_close(&frame_cairo->frame);
				break;
			default:
				break;
			}
		}
	}
	else if (button == BTN_RIGHT &&
		 state == WL_POINTER_BUTTON_STATE_PRESSED &&
		 seat->pointer_focus == frame_cairo->title_bar.title.wl_surface) {
			libdecor_frame_show_window_menu(&frame_cairo->frame,
							seat->wl_seat,
							serial,
							seat->pointer_x,
							seat->pointer_y
							-TITLE_HEIGHT);
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

	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) &&
	    !seat->wl_pointer) {
		seat->wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->wl_pointer,
					&pointer_listener, seat);
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
init_wl_seat(struct libdecor_plugin_cairo *plugin_cairo,
	     uint32_t id,
	     uint32_t version)
{
	struct seat *seat;

	seat = zalloc(sizeof *seat);
	seat->plugin_cairo = plugin_cairo;
	wl_list_insert(&plugin_cairo->seat_list, &seat->link);
	seat->wl_seat =
		wl_registry_bind(plugin_cairo->wl_registry,
				 id, &wl_seat_interface, 1);
	wl_seat_add_listener(seat->wl_seat, &seat_listener, seat);
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
	else if (strcmp(interface, "wl_seat") == 0)
		init_wl_seat(plugin_cairo, id, version);
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

	wl_callback_destroy(callback);
	plugin_cairo->globals_callback = NULL;

	if (!has_required_globals(plugin_cairo)) {
		struct libdecor *context = plugin_cairo->context;

		libdecor_notify_plugin_error(
				context,
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
	plugin_cairo->cursor_theme = NULL;

	wl_list_init(&plugin_cairo->seat_list);

	/* fetch cursor theme and size*/
	if (!libdecor_get_cursor_settings(&plugin_cairo->cursor_theme_name,
					  &plugin_cairo->cursor_size)) {
		plugin_cairo->cursor_theme_name = NULL;
		plugin_cairo->cursor_size = 24;
	}

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
