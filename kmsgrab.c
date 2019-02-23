#include <X11/Xlib.h>
#include <EGL/egl.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#define MSG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

#define ASSERT(cond) \
	if (!(cond)) { \
		MSG("ERROR @ %s:%d: (%s) failed", __FILE__, __LINE__, #cond); \
		return; \
	}

void runEGL() {
	Display *xdisp;
	ASSERT(xdisp = XOpenDisplay(NULL));
	eglBindAPI(EGL_OPENGL_API);
	EGLDisplay edisp = eglGetDisplay(xdisp);
	EGLint ver_min, ver_maj;
	eglInitialize(edisp, &ver_maj, &ver_min);
	MSG("EGL: version %d.%d", ver_maj, ver_min);
	MSG("EGL: EGL_VERSION: '%s'", eglQueryString(edisp, EGL_VERSION));
	MSG("EGL: EGL_VENDOR: '%s'", eglQueryString(edisp, EGL_VENDOR));
	MSG("EGL: EGL_CLIENT_APIS: '%s'", eglQueryString(edisp, EGL_CLIENT_APIS));
	MSG("EGL: client EGL_EXTENSIONS: '%s'", eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
	MSG("EGL: EGL_EXTENSIONS: '%s'", eglQueryString(edisp, EGL_EXTENSIONS));

	static const EGLint econfattrs[] = {
		EGL_BUFFER_SIZE, 32,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,

		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,

		EGL_NONE
	};
	EGLConfig config;
	EGLint num_config;
	eglChooseConfig(edisp, econfattrs, &config, 1, &num_config);

	XVisualInfo *vinfo = NULL;
	{
		XVisualInfo xvisual_info = {0};
		int num_visuals;
		ASSERT(eglGetConfigAttrib(edisp, config, EGL_NATIVE_VISUAL_ID, (EGLint*)&xvisual_info.visualid));
		ASSERT(vinfo = XGetVisualInfo(xdisp, VisualScreenMask | VisualIDMask, &xvisual_info, &num_visuals));
	}

	XSetWindowAttributes winattrs = {0};
	winattrs.event_mask = KeyPressMask | KeyReleaseMask |
		ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
		ExposureMask | VisibilityChangeMask | StructureNotifyMask;
	winattrs.border_pixel = 0;
	winattrs.bit_gravity = StaticGravity;
	winattrs.colormap = XCreateColormap(xdisp,
		RootWindow(xdisp, vinfo->screen),
		vinfo->visual, AllocNone);
	ASSERT(winattrs.colormap != None);
	winattrs.override_redirect = False;

	Window xwin = XCreateWindow(xdisp, RootWindow(xdisp, vinfo->screen),
		0, 0, 1280, 720,
		0, vinfo->depth, InputOutput, vinfo->visual,
		CWBorderPixel | CWBitGravity | CWEventMask | CWColormap,
		&winattrs);
	ASSERT(xwin);

	XStoreName(xdisp, xwin, "kmsgrab");

	{
		Atom delete_message = XInternAtom(xdisp, "WM_DELETE_WINDOW", True);
		XSetWMProtocols(xdisp, xwin, &delete_message, 1);
	}

	XMapWindow(xdisp, xwin);

	static const EGLint ectx_attrs[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLContext ectx = eglCreateContext(edisp, config,
		EGL_NO_CONTEXT, ectx_attrs);
	ASSERT(EGL_NO_CONTEXT != ectx);

	EGLSurface esurf = eglCreateWindowSurface(edisp, config, xwin, 0);
	ASSERT(EGL_NO_SURFACE != esurf);

	ASSERT(eglMakeCurrent(edisp, esurf,
		esurf, ectx));
}

int main(int argc, const char *argv[]) {

	uint32_t fb_id = 0;

	if (argc < 2) {
		MSG("usage: %s fb_id", argv[0]);
		return 1;
	}

	{
		char *endptr;
		fb_id = strtol(argv[1], &endptr, 0);
		if (*endptr != '\0') {
			MSG("%s is not valid framebuffer id", argv[1]);
			return 1;
		}
	}

	const char *card = (argc > 2) ? argv[2] : "/dev/dri/card0";

	MSG("Opening card %s", card);
	const int drmfd = open(card, O_RDONLY);
	if (drmfd < 0) {
		perror("Cannot open card");
		return 1;
	}

	int dma_buf_fd = -1;
	drmModeFBPtr fb = drmModeGetFB(drmfd, fb_id);
	if (!fb) {
		MSG("Cannot open fb %#x", fb_id);
		goto cleanup;
	}

	MSG("fb_id=%#x width=%u height=%u pitch=%u bpp=%u depth=%u handle=%#x",
		fb_id, fb->width, fb->height, fb->pitch, fb->bpp, fb->depth, fb->handle);

	if (!fb->handle) {
		MSG("Not permitted to get fb handles. Run either with sudo, or setcap cap_sys_admin+ep %s", argv[0]);
		goto cleanup;
	}

	const int ret = drmPrimeHandleToFD(drmfd, fb->handle, 0, &dma_buf_fd);
	MSG("drmPrimeHandleToFD = %d, fd = %d", ret, dma_buf_fd);

	runEGL();

cleanup:
	if (dma_buf_fd >= 0)
		close(dma_buf_fd);
	if (fb)
		drmModeFreeFB(fb);
	close(drmfd);
	return 0;
}
