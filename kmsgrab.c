#include <X11/Xlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <xf86drm.h>
#include <libdrm/drm_fourcc.h>
#include <xf86drmMode.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

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

static int width = 1280, height = 720;

typedef struct {
	int width, height;
	uint32_t fourcc;
	int offset, pitch;
	int fd;
} DmaBuf;

void runEGL(const DmaBuf *img) {
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
		0, 0, width, height,
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

	MSG("%s", glGetString(GL_EXTENSIONS));

	// FIXME check for EGL_EXT_image_dma_buf_import
	EGLAttrib eimg_attrs[] = {
		EGL_WIDTH, img->width,
		EGL_HEIGHT, img->height,
		EGL_LINUX_DRM_FOURCC_EXT, img->fourcc,
		EGL_DMA_BUF_PLANE0_FD_EXT, img->fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, img->offset,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, img->pitch,
		EGL_NONE
	};
	EGLImage eimg = eglCreateImage(edisp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, 0,
		eimg_attrs);
	ASSERT(eimg);

	// FIXME check for GL_OES_EGL_image (or alternatives)
	GLuint tex = 1;
	//glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES =
		(PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	ASSERT(glEGLImageTargetTexture2DOES);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, eimg);
	ASSERT(glGetError() == 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

	const char *fragment =
		"#version 130\n"
		"uniform vec2 res;\n"
		"uniform sampler2D tex;\n"
		"void main() {\n"
			"vec2 uv = gl_FragCoord.xy / res;\n"
			"uv.y = 1. - uv.y;\n"
			"gl_FragColor = texture(tex, uv);\n"
		"}\n"
	;
	int prog = ((PFNGLCREATESHADERPROGRAMVPROC)(eglGetProcAddress("glCreateShaderProgramv")))(GL_FRAGMENT_SHADER, 1, &fragment);
	glUseProgram(prog);
	glUniform1i(glGetUniformLocation(prog, "tex"), 0);

	for (;;) {
		while (XPending(xdisp)) {
			XEvent e;
			XNextEvent(xdisp, &e);
			switch (e.type) {
				case ConfigureNotify:
					{
						width = e.xconfigure.width;
						height = e.xconfigure.height;
					}
					break;

				case KeyPress:
					switch(XLookupKeysym(&e.xkey, 0)) {
						case XK_Escape:
						case XK_q:
							goto exit;
							break;
					}
					break;

				case ClientMessage:
				case DestroyNotify:
				case UnmapNotify:
					goto exit;
					break;
			}
		}

		{
			glViewport(0, 0, width, height);
			glClear(GL_COLOR_BUFFER_BIT);

			glUniform2f(glGetUniformLocation(prog, "res"), width, height);
			glRects(-1, -1, 1, 1);

			ASSERT(eglSwapBuffers(edisp, esurf));
		}
	}

exit:
	eglMakeCurrent(edisp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(edisp, ectx);
	eglDestroySurface(xdisp, esurf);
	XDestroyWindow(xdisp, xwin);
	eglTerminate(edisp);
	XCloseDisplay(xdisp);
}

int main(int argc, const char *argv[]) {
#if 1
	if (argc < 2) {
		MSG("Usage: %s socket_filename", argv[0]);
		return 1;
	}

	const char *sockname = argv[1];

	int sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	{
		struct sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		if (strlen(sockname) >= sizeof(addr.sun_path)) {
			MSG("Socket filename '%s' is too long, max %d",
				sockname, (int)sizeof(addr.sun_path));
			goto cleanup;
		}

		strcpy(addr.sun_path, sockname);
		if (-1 == connect(sockfd, (const struct sockaddr*)&addr, sizeof(addr))) {
			perror("Cannot connect to unix socket");
			goto cleanup;
		}

		MSG("connected");
	}

	DmaBuf img = {0};

	{
		struct msghdr msg = {0};

		struct iovec io = {
			.iov_base = &img,
			.iov_len = sizeof(img),
		};
		msg.msg_iov = &io;
		msg.msg_iovlen = 1;

		char cmsg_buf[CMSG_SPACE(sizeof(img.fd))];
		msg.msg_control = cmsg_buf;
		msg.msg_controllen = sizeof(cmsg_buf);
		struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		cmsg->cmsg_len = CMSG_LEN(sizeof(img.fd));

		MSG("recvmsg");
		ssize_t recvd = recvmsg(sockfd, &msg, 0);
		if (recvd <= 0) {
			perror("cannot recvmsg");
			goto cleanup;
		}

		MSG("Received %d", (int)recvd);

		if (io.iov_len == sizeof(img) - sizeof(img.fd)) {
			MSG("Received metadata size mismatch: %d received, %d expected",
				(int)io.iov_len, (int)sizeof(img) - (int)sizeof(img.fd));
			goto cleanup;
		}

		if (cmsg->cmsg_len != CMSG_LEN(sizeof(img.fd))) {
			MSG("Received fd size mismatch: %d received, %d expected",
				(int)cmsg->cmsg_len, (int)CMSG_LEN(sizeof(img.fd)));
			goto cleanup;
		}

		memcpy(&img.fd, CMSG_DATA(cmsg), sizeof(img.fd));
	}

	close(sockfd);
	sockfd = -1;

	MSG("Received width=%d height=%d pitch=%u fourcc=%#x fd=%d",
		img.width, img.height, img.pitch, img.fourcc, img.fd);

	runEGL(&img);

cleanup:
	if (sockfd >= 0)
		close(sockfd);
	return 0;
#else
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

	DmaBuf img;
	img.width = fb->width;
	img.height = fb->height;
	img.pitch = fb->pitch;
	img.offset = 0;
	img.fourcc = DRM_FORMAT_XRGB8888; // FIXME

	const int ret = drmPrimeHandleToFD(drmfd, fb->handle, 0, &dma_buf_fd);
	MSG("drmPrimeHandleToFD = %d, fd = %d", ret, dma_buf_fd);
	img.fd = dma_buf_fd;

	runEGL(&img);

cleanup:
	if (dma_buf_fd >= 0)
		close(dma_buf_fd);
	if (fb)
		drmModeFreeFB(fb);
	close(drmfd);
	return 0;
#endif
}
