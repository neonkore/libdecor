#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "libdecoration.h"
#include "utils.h"

static const size_t default_size = 200;

static const char *vert_shader_text =
	"uniform mat4 rotation;\n"
	"attribute vec4 pos;\n"
	"attribute vec4 color;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_Position = rotation * pos;\n"
	"  v_color = color;\n"
	"}\n";

static const char *frag_shader_text =
	"precision mediump float;\n"
	"varying vec4 v_color;\n"
	"void main() {\n"
	"  gl_FragColor = v_color;\n"
	"}\n";

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
	bool draw_opaque;
	bool wait_for_configure;
	bool open;

	struct {
		GLuint rotation_uniform;
		GLuint pos;
		GLuint col;
	} gl;
	uint32_t benchmark_time, frames;
};

static void
frame_configure(struct libdecor_frame *frame,
		struct libdecor_configuration *configuration,
		void *user_data)
{
	struct window *window = user_data;
	struct libdecor_state *state;
	int width, height;
	enum libdecor_window_state window_state = LIBDECOR_WINDOW_STATE_NONE;

	if (!libdecor_configuration_get_content_size(configuration, frame,
						     &width, &height)) {
		height = width = default_size;
	}

	window->content_width = width;
	window->content_height = height;
	window->wait_for_configure = false;

	wl_egl_window_resize(window->egl_window,
			     window->content_width, window->content_height,
			     0, 0);

	libdecor_configuration_get_window_state(configuration, &window_state);
	if (window_state &  (LIBDECOR_WINDOW_STATE_MAXIMIZED |
			     LIBDECOR_WINDOW_STATE_FULLSCREEN)) {
		struct wl_region *region;

		window->draw_opaque = true;
		region = wl_compositor_create_region(window->client->compositor);
		wl_region_add(region, 0, 0,
			      window->content_width,
			      window->content_height);
		wl_surface_set_opaque_region(window->surface, region);
		wl_region_destroy(region);
	} else {
		window->draw_opaque = false;
		wl_surface_set_opaque_region(window->surface, NULL);
	}

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

static GLuint
create_shader(struct window *window, const char *source, GLenum shader_type)
{
	GLuint shader;
	GLint status;

	shader = glCreateShader(shader_type);

	glShaderSource(shader, 1, (const char **) &source, NULL);
	glCompileShader(shader);

	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetShaderInfoLog(shader, 1000, &len, log);
		fprintf(stderr, "Error: compiling %s: %.*s\n",
			shader_type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			len, log);
		exit(1);
	}

	return shader;
}

static void
init_gl(struct window *window)
{
	GLuint frag, vert;
	GLuint program;
	GLint status;

	frag = create_shader(window, frag_shader_text, GL_FRAGMENT_SHADER);
	vert = create_shader(window, vert_shader_text, GL_VERTEX_SHADER);

	program = glCreateProgram();
	glAttachShader(program, frag);
	glAttachShader(program, vert);
	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(program, 1000, &len, log);
		fprintf(stderr, "Error: linking:\n%.*s\n", len, log);
		exit(1);
	}

	glUseProgram(program);

	window->gl.pos = 0;
	window->gl.col = 1;

	glBindAttribLocation(program, window->gl.pos, "pos");
	glBindAttribLocation(program, window->gl.col, "color");
	glLinkProgram(program);

	window->gl.rotation_uniform =
		glGetUniformLocation(program, "rotation");
}

static bool
setup(struct window *window)
{
	static const EGLint config_attribs[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 1,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	static const EGLint context_attribs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};

	EGLint major, minor;
	EGLint n;
	EGLConfig config;

	window->client->egl_display =
		eglGetDisplay((EGLNativeDisplayType) window->client->display);

	if (eglInitialize(window->client->egl_display,
			  &major, &minor) == EGL_FALSE) {
		fprintf(stderr, "Cannot initialise EGL!\n");
		return false;
	}

	if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
		fprintf(stderr, "Cannot bind EGL API!\n");
		return false;
	}

	if (eglChooseConfig(window->client->egl_display,
			    config_attribs, &config, 1, &n) == EGL_FALSE) {
		fprintf(stderr, "No matching EGL configurations!\n");
		return false;
	}

	window->client->egl_context =
		eglCreateContext(window->client->egl_display,
				 config, EGL_NO_CONTEXT,
				 context_attribs);

	if (window->client->egl_context == EGL_NO_CONTEXT) {
		fprintf(stderr, "No EGL context!\n");
		return false;
	}

	window->surface =
		wl_compositor_create_surface(window->client->compositor);

	window->egl_window = wl_egl_window_create(window->surface,
						  default_size, default_size);

	window->egl_surface = eglCreateWindowSurface(
				      window->client->egl_display, config,
				      (EGLNativeWindowType)window->egl_window,
				      NULL);

	eglMakeCurrent(window->client->egl_display,
		       window->egl_surface,
		       window->egl_surface,
		       window->client->egl_context);

	init_gl(window);

	return true;
}

static void
draw(struct window *window)
{
	static const GLfloat verts[3][2] = {
		{ -0.5, -0.5 },
		{  0.5, -0.5 },
		{  0,    0.5 }
	};
	static const GLfloat colors[3][3] = {
		{ 1, 0, 0 },
		{ 0, 1, 0 },
		{ 0, 0, 1 }
	};
	GLfloat angle;
	GLfloat rotation[4][4] = {
		{ 1, 0, 0, 0 },
		{ 0, 1, 0, 0 },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 }
	};
	static const uint32_t speed_div = 5, benchmark_interval = 5;
	struct timeval tv;
	uint32_t time;

	gettimeofday(&tv, NULL);
	time = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	if (window->frames == 0)
		window->benchmark_time = time;
	if (time - window->benchmark_time > (benchmark_interval * 1000)) {
		printf("%d frames in %d seconds: %f fps\n",
		       window->frames,
		       benchmark_interval,
		       (float) window->frames / benchmark_interval);
		window->benchmark_time = time;
		window->frames = 0;
	}

	angle = (time / speed_div) % 360 * M_PI / 180.0;
	rotation[0][0] =  cos(angle);
	rotation[0][2] =  sin(angle);
	rotation[2][0] = -sin(angle);
	rotation[2][2] =  cos(angle);

	glViewport(0, 0, window->content_width, window->content_height);

	glUniformMatrix4fv(window->gl.rotation_uniform, 1, GL_FALSE,
			   (GLfloat *) rotation);

	if (window->draw_opaque)
		glClearColor(0.0, 0.0, 0.0, 1.0);
	else
		glClearColor(0.0, 0.0, 0.0, 0.5);
	glClear(GL_COLOR_BUFFER_BIT);

	glVertexAttribPointer(window->gl.pos, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glVertexAttribPointer(window->gl.col, 3, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(window->gl.pos);
	glEnableVertexAttribArray(window->gl.col);

	glDrawArrays(GL_TRIANGLES, 0, 3);

	glDisableVertexAttribArray(window->gl.pos);
	glDisableVertexAttribArray(window->gl.col);

	eglSwapBuffers(window->client->egl_display, window->egl_surface);
	window->frames++;
}

int
main(int argc, char *argv[])
{
	struct wl_registry *wl_registry;
	struct libdecor *context;
	struct window *window;
	struct client *client;
	int ret = 0;

	client = zalloc(sizeof *client);

	client->display = wl_display_connect(NULL);
	if (!client->display) {
		fprintf(stderr, "No Wayland connection\n");
		return EXIT_FAILURE;
	}

	wl_registry = wl_display_get_registry(client->display);
	wl_registry_add_listener(wl_registry, &registry_listener, client);
	wl_display_roundtrip(client->display);

	window = zalloc(sizeof *window);
	window->client = client;
	window->open = true;
	window->wait_for_configure = true;

	setup(window);

	context = libdecor_new(client->display, &libdecor_interface);
	window->frame = libdecor_decorate(context, window->surface,
					  &frame_interface, window);
	libdecor_frame_set_app_id(window->frame, "egl-demo");
	libdecor_frame_set_title(window->frame, "EGL demo");
	libdecor_frame_map(window->frame);

	wl_display_roundtrip(client->display);
	wl_display_roundtrip(client->display);

	while (window->open && ret != -1) {
		if (window->wait_for_configure) {
			ret = wl_display_dispatch(client->display);
		} else {
			ret = wl_display_dispatch_pending(client->display);
			draw(window);
		}
	}

	free(window);
	free(client);

	return EXIT_SUCCESS;
}
