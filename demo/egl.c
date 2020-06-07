#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <libdecoration.h>
#include <GL/gl.h>

static const size_t default_size = 200;

struct client {
	struct wl_display *display;
	struct wl_compositor *compositor;
	EGLDisplay egl_display;
	EGLContext egl_context;
};

struct window {
	struct client *client;
	struct wl_surface *surface;
	struct libdecor_frame *frame;
	struct wl_egl_window *egl_window;
	EGLSurface egl_surface;
	int content_width;
	int content_height;
	bool open;
};

static void
draw(struct window *window);

static void
frame_configure(struct libdecor_frame *frame,
		struct libdecor_configuration *configuration,
		void *user_data)
{
	struct window *window = user_data;
	struct libdecor_state *state;
	int width, height;

	if (!libdecor_configuration_get_content_size(configuration, frame,
						     &width, &height)) {
		height = width = default_size;
	}

	window->content_width = width;
	window->content_height = height;

	wl_egl_window_resize(window->egl_window,
			     window->content_width, window->content_height,
			     0, 0);

	draw(window);

	state = libdecor_state_new(width, height);
	libdecor_frame_commit(frame, state, configuration);
	libdecor_state_free(state);
}

static void
frame_close(struct libdecor_frame *frame,
	    void *user_data)
{
	struct window *window = user_data;

	window->open = false;
}

static void
frame_commit(void *user_data)
{
	struct window *window = user_data;

	eglSwapBuffers(window->client->display, window->egl_surface);
}

static struct libdecor_frame_interface frame_interface = {
	frame_configure,
	frame_close,
	frame_commit,
};

static void
libdecor_error(struct libdecor *context,
	       enum libdecor_error error,
	       const char *message)
{
	fprintf(stderr, "Caught error (%d): %s\n", error, message);
	exit(EXIT_FAILURE);
}

static struct libdecor_interface libdecor_interface = {
	libdecor_error,
};

static void
registry_global(void *data,
		struct wl_registry *wl_registry,
		uint32_t name,
		const char *interface,
		uint32_t version)
{
	struct client *client = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		client->compositor = wl_registry_bind(wl_registry, name,
					     &wl_compositor_interface, 1);
	}
}

static void
registry_global_remove(void *data,
		       struct wl_registry *wl_registry,
		       uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_global,
	registry_global_remove
};

static bool
setup(struct window *window)
{
	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};

	EGLint major, minor;
	EGLint n;
	EGLConfig config;

	window->client->egl_display = eglGetDisplay((EGLNativeDisplayType)window->client->display);

	if (eglInitialize(window->client->egl_display, &major, &minor) == EGL_FALSE) {
		fprintf(stderr, "Cannot initialise EGL!\n");
		return false;
	}

	if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
		fprintf(stderr, "Cannot bind EGL API!\n");
		return false;
	}

	if (eglChooseConfig(window->client->egl_display, config_attribs, &config, 1, &n) == EGL_FALSE) {
		fprintf(stderr, "No matching EGL configurations!\n");
		return false;
	}

	window->client->egl_context = eglCreateContext(window->client->egl_display,
						       config, EGL_NO_CONTEXT, NULL);

	if (window->client->egl_context == EGL_NO_CONTEXT) {
		fprintf(stderr, "No EGL context!\n");
		return false;
	}

	window->surface = wl_compositor_create_surface(window->client->compositor);

	window->egl_window = wl_egl_window_create(window->surface,
						  default_size, default_size);

	window->egl_surface = eglCreateWindowSurface(
				      window->client->egl_display, config,
				      (EGLNativeWindowType)window->egl_window,
				      NULL);

	eglMakeCurrent(window->client->egl_display, window->egl_surface,
		       window->egl_surface, window->client->egl_context);

	return true;
}

static void
draw(struct window *window)
{
	glClearColor(1,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(window->client->display, window->egl_surface);
}

int
main(int argc, char *argv[])
{
	struct wl_registry *wl_registry;
	struct libdecor *context;
	struct window *window;
	struct client *client;

	client = calloc(1, sizeof(struct client));

	client->display = wl_display_connect(NULL);
	if (!client->display) {
		fprintf(stderr, "No Wayland connection\n");
		return EXIT_FAILURE;
	}

	wl_registry = wl_display_get_registry(client->display);
	wl_registry_add_listener(wl_registry, &registry_listener, client);
	wl_display_roundtrip(client->display);

	window = calloc(1, sizeof(struct window));
	window->client = client;
	window->open = true;

	setup(window);

	context = libdecor_new(client->display, &libdecor_interface);
	window->frame = libdecor_decorate(context, window->surface,
					  &frame_interface, window);
	libdecor_frame_set_app_id(window->frame, "egl-demo");
	libdecor_frame_set_title(window->frame, "EGL demo");
	libdecor_frame_map(window->frame);

	wl_display_roundtrip(client->display);
	wl_display_roundtrip(client->display);

	// wait for the first configure event
	wl_display_dispatch(client->display);

	while (window->open) {
		wl_display_dispatch_pending(client->display);
		draw(window);
	}

	free(window);
	free(client);

	return EXIT_SUCCESS;
}
