#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>

#include <xf86drm.h>
#include <libdrm/drm_fourcc.h>
#include <xf86drmMode.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include <X11/Xatom.h>
#include <string.h>


#define MSG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

#define ASSERT(cond) \
	if (!(cond)) { \
		MSG("ERROR @ %s:%d: (%s) failed", __FILE__, __LINE__, #cond); \
		return 0; \
	}

static EGLImageKHR
create_dmabuf_egl_image(EGLDisplay egl_display, unsigned int width,
			unsigned int height, uint32_t drm_format,
			uint32_t n_planes, const int *fds,
			const uint32_t *strides, const uint32_t *offsets,
			const uint64_t *modifiers)
{
	EGLAttrib attribs[47];
	int atti = 0;

	/* This requires the Mesa commit in
	 * Mesa 10.3 (08264e5dad4df448e7718e782ad9077902089a07) or
	 * Mesa 10.2.7 (55d28925e6109a4afd61f109e845a8a51bd17652).
	 * Otherwise Mesa closes the fd behind our back and re-importing
	 * will fail.
	 * https://bugs.freedesktop.org/show_bug.cgi?id=76188
	 * */

	attribs[atti++] = EGL_WIDTH;
	attribs[atti++] = width;
	attribs[atti++] = EGL_HEIGHT;
	attribs[atti++] = height;
	attribs[atti++] = EGL_LINUX_DRM_FOURCC_EXT;
	attribs[atti++] = drm_format;

	if (n_planes > 0) {
		attribs[atti++] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attribs[atti++] = fds[0];
		attribs[atti++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attribs[atti++] = offsets[0];
		attribs[atti++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attribs[atti++] = strides[0];
		if (modifiers) {
			attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT;
			attribs[atti++] = modifiers[0] & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT;
			attribs[atti++] = modifiers[0] >> 32;
		}
	}

	if (n_planes > 1) {
		attribs[atti++] = EGL_DMA_BUF_PLANE1_FD_EXT;
		attribs[atti++] = fds[1];
		attribs[atti++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
		attribs[atti++] = offsets[1];
		attribs[atti++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
		attribs[atti++] = strides[1];
		if (modifiers) {
			attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT;
			attribs[atti++] = modifiers[1] & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT;
			attribs[atti++] = modifiers[1] >> 32;
		}
	}

	if (n_planes > 2) {
		attribs[atti++] = EGL_DMA_BUF_PLANE2_FD_EXT;
		attribs[atti++] = fds[2];
		attribs[atti++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
		attribs[atti++] = offsets[2];
		attribs[atti++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
		attribs[atti++] = strides[2];
		if (modifiers) {
			attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT;
			attribs[atti++] = modifiers[2] & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT;
			attribs[atti++] = modifiers[2] >> 32;
		}
	}

	if (n_planes > 3) {
		attribs[atti++] = EGL_DMA_BUF_PLANE3_FD_EXT;
		attribs[atti++] = fds[3];
		attribs[atti++] = EGL_DMA_BUF_PLANE3_OFFSET_EXT;
		attribs[atti++] = offsets[3];
		attribs[atti++] = EGL_DMA_BUF_PLANE3_PITCH_EXT;
		attribs[atti++] = strides[3];
		if (modifiers) {
			attribs[atti++] = EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT;
			attribs[atti++] = modifiers[3] & 0xFFFFFFFF;
			attribs[atti++] = EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT;
			attribs[atti++] = modifiers[3] >> 32;
		}
	}

	attribs[atti++] = EGL_NONE;

	return eglCreateImage(egl_display, EGL_NO_CONTEXT,
			      EGL_LINUX_DMA_BUF_EXT, 0, attribs);
}


typedef struct {
	int width, height;
	uint32_t fourcc;
	int fd, offset, pitch;
} DmaBuf;

uint32_t lastGoodPlane = 0;

int handle_id = 0;
uint32_t prepareImage(const int fd, int cursor) {
	
	drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
	
	// Check the first plane (or last good)
	drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[lastGoodPlane]);
	uint32_t fb_id = plane->fb_id;
	drmModeFreePlane(plane);
	
	// Find a good plane
	if (fb_id == 0) {
		for (uint32_t i = 0; i < planes->count_planes; ++i) {
			drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[i]);
			
			if (plane->fb_id != 0) {
				drmModeFB2Ptr fb = drmModeGetFB2(fd, plane->fb_id);
				if (fb == NULL) {
					//ctx->lastGoodPlane = 0;
					continue;
				}
				if (fb->handles[handle_id]) {
					// most likely cursor	
					//MSG("%d\t%d\n", fb->width, fb->height);
					if (cursor) {
						if (fb->width != 256 && fb->height != 256)
							continue;
					}
					else {
						if (fb->width == 256 && fb->height == 256)
							continue;
					}
				}
				drmModeFreeFB2(fb);
				
				lastGoodPlane = i;
				fb_id = plane->fb_id;
				//MSG("%d, %#x", i, fb_id);
				
				drmModeFreePlane(plane);
				break;
			}
			else {
				drmModeFreePlane(plane);
			}
		}
	}
	
	drmModeFreePlaneResources(planes);
	
	//MSG("%#x", fb_id);
	return fb_id;
}

//static int width = 1280, height = 720;
int main(int argc, const char *argv[]) {

	//const char *card = (argc > 2) ? argv[2] : "/dev/dri/card0";
	const char *card = "/dev/dri/card0";
	
	int cursor = 0;
	int width = 1280;
	int height = 720;
	int fullscreen = 0;
	//const char *window_name = (argc > 3) ? argv[3] : "kmsgrab";
	
	if (argc == 2) {
		sscanf (argv[1], "%i", &width);
		if (width == -1)
			fullscreen = 1;
		width = 1280;
	}
	
	if (argc > 2) {
		sscanf (argv[1], "%i", &width);
		sscanf (argv[2], "%i", &height);
	}


	
	// render all
	Display *xdisp;
	ASSERT(xdisp = XOpenDisplay(NULL));
	eglBindAPI(EGL_OPENGL_API);
	EGLDisplay edisp = eglGetDisplay(xdisp);
	EGLint ver_min, ver_maj;
	eglInitialize(edisp, &ver_maj, &ver_min);\

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
	
	
	// class window
	XClassHint* class_hints = XAllocClassHint();
	class_hints->res_name = (char*)malloc(sizeof(char)*8);
	class_hints->res_class = (char*)malloc(sizeof(char)*8);
	strcpy(class_hints->res_name, "kmsgrab");
	strcpy(class_hints->res_class, "kmsgrab");
	XSetClassHint(xdisp, xwin, class_hints);

	
	if (fullscreen) {
		Atom wm_state   = XInternAtom (xdisp, "_NET_WM_STATE", true );
		Atom wm_fullscreen = XInternAtom (xdisp, "_NET_WM_STATE_FULLSCREEN", true );
		XChangeProperty(xdisp, xwin, wm_state, XA_ATOM, 32, PropModeReplace, (unsigned char *)&wm_fullscreen, 1);
	}

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
	// Set half framerate
	//eglSwapInterval(edisp, 2);

	//MSG("%s", glGetString(GL_EXTENSIONS));

	// FIXME check for EGL_EXT_image_dma_buf_import
	/*EGLAttrib eimg_attrs[] = {
		EGL_WIDTH, img.width,
		EGL_HEIGHT, img.height,
		EGL_LINUX_DRM_FOURCC_EXT, img.fourcc,
		EGL_DMA_BUF_PLANE0_FD_EXT, img.fd,
		EGL_DMA_BUF_PLANE0_OFFSET_EXT, img.offset,
		EGL_DMA_BUF_PLANE0_PITCH_EXT, img.pitch,
		EGL_NONE
	};
	EGLImage eimg = eglCreateImage(edisp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, 0, eimg_attrs);*/
	

	MSG("Opening card %s", card);
	const int drmfd = open(card, O_RDONLY);
	if (drmfd < 0) {
		perror("Cannot open card");
		return 1;
	}
	drmSetClientCap(drmfd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	
	const int available = drmAvailable();
	if (!available)
		return 0;
	
	
	// Find DRM video source
	uint32_t fb_id = prepareImage(drmfd, cursor);

	if (fb_id == 0) {
		MSG("Not found fb_id");
		return 1;
	}

	int dma_buf_fd[4] = {-1, -1, -1, -1};
	drmModeFB2Ptr fb = drmModeGetFB2(drmfd, fb_id);
	if (!fb->handles[handle_id]) {
		MSG("Not permitted to get fb handles. Run either with sudo, or setcap cap_sys_admin+ep %s", argv[0]);
		
		//if (dma_buf_fd >= 0)
		//	close(dma_buf_fd);
		if (fb)
			drmModeFreeFB2(fb);
		close(drmfd);
		return 0;
	}

	//drmPrimeHandleToFD(drmfd, fb->handles[0], O_RDONLY, (dma_buf_fd + 0));
	drmPrimeHandleToFD(drmfd, fb->handles[0], 0, (dma_buf_fd + 0));
	drmPrimeHandleToFD(drmfd, fb->handles[1], 0, (dma_buf_fd + 1));
	drmPrimeHandleToFD(drmfd, fb->handles[2], 0, (dma_buf_fd + 2));
	drmPrimeHandleToFD(drmfd, fb->handles[3], 0, (dma_buf_fd + 3));
	
	PFNEGLQUERYDMABUFMODIFIERSEXTPROC eglQueryDmaBufModifiersEXT =
		(PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
	
	
	EGLint max_modifiers;
	if (!eglQueryDmaBufModifiersEXT(edisp, DRM_FORMAT_XRGB8888, 0, NULL,
					     NULL, &max_modifiers)) {
		MSG("Cannot query the number of modifiers");
		return false;
	}
	EGLuint64KHR *modifier_list =
		malloc(max_modifiers * sizeof(EGLuint64KHR));
	EGLBoolean *external_only = NULL;
	if (!modifier_list) {
		MSG("Unable to allocate memory");
		return false;
	}
	if (!eglQueryDmaBufModifiersEXT(edisp, DRM_FORMAT_XRGB8888,
					     max_modifiers, modifier_list,
					     external_only, &max_modifiers)) {
		MSG("Cannot query a list of modifiers:");
		free(modifier_list);
		return false;
	}
	//modifier_list[0] = fb->modifier;
	
	if (fb->flags & DRM_MODE_FB_MODIFIERS) {
		MSG("Modifiers supposed to work");
		MSG("FB Modifier: %ld", fb->modifier);
	}
	
	for (int i = 0; i < max_modifiers; i++)
		MSG("Query Modifier %d = %ld", i, modifier_list[i]);
	
	for (int i = 0; i < max_modifiers; i++)
		modifier_list[i] = fb->modifier;
	
	
	// *modifiers = modifier_list;
	// *n_modifiers = (EGLuint64KHR)max_modifiers;
	
	EGLImage eimg = create_dmabuf_egl_image(edisp, fb->width, fb->height,
					    DRM_FORMAT_XRGB8888, 3, dma_buf_fd, fb->pitches,
					    fb->offsets, modifier_list);
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
	//glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

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

	
			// Find DRM video source
			/*uint32_t fb_id = prepareImage(drmfd, cursor);

			if (fb_id == 0) {
				MSG("Not found fb_id");
			}
			else {
				//if (dma_buf_fd >= 0)
				//	close(dma_buf_fd);
				if (fb)
					drmModeFreeFB2(fb);
					
				fb = drmModeGetFB2(drmfd, fb_id);
				if (!fb->handles[handle_id]) {
					MSG("Not permitted to get fb handles. Run either with sudo, or setcap cap_sys_admin+ep %s", argv[0]);
					
					if (fb)
						drmModeFreeFB2(fb);
					close(drmfd);
					return 0;
				}

				drmPrimeHandleToFD(drmfd, fb->handles[handle_id], 0, &dma_buf_fd);
				
				eglDestroyImage(edisp, eimg);
				eimg = eglCreateImage(edisp, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, 0,
					eimg_attrs);
				ASSERT(eimg);
				
				glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, eimg);
			}
			//MSG("%#x", img.fd);*/
			
			// rebind texture
			glBindTexture(GL_TEXTURE_2D, tex);
			
			glViewport(0, 0, width, height);
			glClear(GL_COLOR_BUFFER_BIT);

			glUniform2f(glGetUniformLocation(prog, "res"), width, height);
			glRects(-1, -1, 1, 1);

			ASSERT(eglSwapBuffers(edisp, esurf));
		}
	}

exit:
	//if (dma_buf_fd >= 0)
	//	close(dma_buf_fd);
	if (fb)
		drmModeFreeFB2(fb);
	eglMakeCurrent(edisp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroyContext(edisp, ectx);
	eglDestroySurface(xdisp, esurf);
	XDestroyWindow(xdisp, xwin);
	free(vinfo);
	eglTerminate(edisp);
	XCloseDisplay(xdisp);
}
