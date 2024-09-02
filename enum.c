//#!cc -Werror -std=c99 -I/usr/include/libdrm -ldrm enum.c -o enum && ./enum /dev/dri/card0
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>

#define MSG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define MSGA(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define ASSERT(a) assert(a)

static void printDrmModeType(uint32_t type) {
#define CHECK_MODE(mode) if ((type&(DRM_MODE_TYPE_##mode)) == (DRM_MODE_TYPE_##mode)) MSGA(#mode" ")
	CHECK_MODE(BUILTIN);
	CHECK_MODE(CLOCK_C);
	CHECK_MODE(CRTC_C);
	CHECK_MODE(PREFERRED);
	CHECK_MODE(DEFAULT);
	CHECK_MODE(USERDEF);
	CHECK_MODE(DRIVER);
#undef CHECK_MODE
}

static void printDrmModeFlags(uint32_t flags) {
#define CHECK_FLAG(flag) if ((flags&(DRM_MODE_FLAG_##flag)) == (DRM_MODE_FLAG_##flag)) MSGA(#flag" ")
	CHECK_FLAG(PHSYNC);
	CHECK_FLAG(NHSYNC);
	CHECK_FLAG(PVSYNC);
	CHECK_FLAG(NVSYNC);
	CHECK_FLAG(INTERLACE);
	CHECK_FLAG(DBLSCAN);
	CHECK_FLAG(CSYNC);
	CHECK_FLAG(PCSYNC);
	CHECK_FLAG(NCSYNC);
	CHECK_FLAG(HSKEW);
	CHECK_FLAG(BCAST);
	CHECK_FLAG(PIXMUX);
	CHECK_FLAG(DBLCLK);
	CHECK_FLAG(CLKDIV2);
	CHECK_FLAG(3D_FRAME_PACKING);
	CHECK_FLAG(3D_FIELD_ALTERNATIVE);
	CHECK_FLAG(3D_LINE_ALTERNATIVE);
	CHECK_FLAG(3D_SIDE_BY_SIDE_FULL);
	CHECK_FLAG(3D_L_DEPTH);
	CHECK_FLAG(3D_L_DEPTH_GFX_GFX_DEPTH);
	CHECK_FLAG(3D_TOP_AND_BOTTOM);
	CHECK_FLAG(3D_SIDE_BY_SIDE_HALF);
	CHECK_FLAG(PIC_AR_4_3);
	CHECK_FLAG(PIC_AR_16_9);
	CHECK_FLAG(PIC_AR_64_27);
	CHECK_FLAG(PIC_AR_256_135);
#undef CHECK_FLAG
}

static void enumerateModeResources(int fd, const drmModeResPtr res) {
	MSG("drmModeResPtr:");
	MSG("\t.count_fbs = %d,", res->count_fbs);
	MSG("\t.count_crtcs = %d,", res->count_crtcs);
	MSG("\t.count_connectors = %d,", res->count_connectors);
	MSG("\t.count_encoders = %d", res->count_encoders);
	MSG("\t.width: %u..%u", res->min_width, res->max_width);
	MSG("\t.height: %u..%u", res->min_height, res->max_height);

	for (int i = 0; i < res->count_fbs; ++i)
		MSG("\t.fbs[%d] = 0x%x", i, res->fbs[i]);

	for (int i = 0; i < res->count_crtcs; ++i) {
		drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
		if (!crtc)
			continue;

		MSGA("\t.crtcs[%d] = 0x%x: ", i, res->crtcs[i]);
		MSGA("buffer_id=0x%x gamma_size=%d ", crtc->buffer_id, crtc->gamma_size);
		MSGA("mode=(%u,%u %ux%u) mode_valid=%d ",
			crtc->x, crtc->y, crtc->width, crtc->height, crtc->mode_valid);
		MSG(" mode.name = %s", crtc->mode.name);
		drmModeFreeCrtc(crtc);
	}

	for (int i = 0; i < res->count_connectors; ++i) {
		//drmModeConnectorPtr conn = drmModeGetConnectorCurrent(fd, res->connectors[i]);
		// No "Current" does EDID probe, etc.
		drmModeConnectorPtr conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn)
			continue;

		MSGA("\t.connectors[%d] = 0x%x: ", i, res->connectors[i]);
		MSG("encoder_id=0x%x type=%u type_id=0x%x connnection=%d mm=%ux%u subpixel=%d count_modes=%d count_props=%d count_encoders=%d",
			conn->encoder_id, conn->connector_type, conn->connector_type_id, conn->connection, conn->mmWidth, conn->mmHeight,
			conn->subpixel, conn->count_modes, conn->count_props, conn->count_encoders);

		for (int j = 0; j < conn->count_modes; ++j) {
			drmModeModeInfoPtr m = &conn->modes[j];
			MSGA("\t\t.mode[%d] = %ux%u@%u .name=%s .clock=%ukHz .flags=(%x)",
				j, m->hdisplay, m->vdisplay, m->vrefresh, m->name, m->clock, m->flags);
			printDrmModeFlags(m->flags);
			MSGA(".type=(%u)", m->type);
			printDrmModeType(m->type);
			MSG("");
		}

		drmModeFreeConnector(conn);
	}

	for (int i = 0; i < res->count_encoders; ++i) {
		drmModeEncoderPtr enc = drmModeGetEncoder(fd, res->encoders[i]);
		if (!enc)
			continue;

		MSG("\t.encoders[%d] = 0x%x: type=%u crtc_id=0x%x possible_crtcs=%x possible_clones=%x",
			i, res->encoders[i],
			enc->encoder_type, enc->crtc_id, enc->possible_crtcs, enc->possible_clones
			);
	}
}

void printVer(drmVersionPtr ver) {
	if (!ver) {
		MSG("ver=NULL");
		return;
	}

	MSG("ver=%d.%d.%d driver=%.*s date=%.*s desc=%.*s",
		ver->version_major, ver->version_minor, ver->version_patchlevel,
		ver->name_len, ver->name,
		ver->date_len, ver->date,
		ver->desc_len, ver->desc);
}

typedef struct {
	int fd;
	drmVersionPtr ver, lib_ver;
	drmModeResPtr mode_res;
} DrmDevice;

DrmDevice *ddOpen(const char *card) {
	const int fd = open(card, O_RDONLY);
	DrmDevice dd = {.fd = fd};
	if (fd < 2) {
		MSG("Unable to open card \"%s\"", card);
		return NULL;
	}

	MSG("%s = %d (is KMS=%d)", card, fd, drmIsKMS(fd));

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

	dd.ver = drmGetVersion(fd);
	dd.lib_ver = drmGetLibVersion(fd);

	printVer(dd.ver);
	MSGA("lib "); printVer(dd.lib_ver);

	{
		const char *busid = drmGetBusid(fd);
		MSG("busid=%s", busid);
		drmFreeBusid(busid);
	}

	{
		// TODO whats this
		drmStatsT stats;
		const int result = drmGetStats(fd, &stats);
		MSG("drmGetStats = %d", result);
		MSG("\tcount = %d", (int)stats.count);
	}

	dd.mode_res = drmModeGetResources(fd);
	if (dd.mode_res) {
		enumerateModeResources(fd, dd.mode_res);
	} else {
		MSG("No mode resources?!");
	}

	DrmDevice *ret = malloc(sizeof(*ret));
	memcpy(ret, &dd, sizeof(*ret));
	return ret;
}

void ddClose(DrmDevice *dd) {
	if (!dd)
		return;

	if (dd->mode_res)
		drmModeFreeResources(dd->mode_res);

	if (dd->lib_ver)
		drmFreeVersion(dd->lib_ver);

	if (dd->ver)
		drmFreeVersion(dd->ver);

	free(dd);
}

#if 0
static void enumerateFbs(int fd) {
#define MAX_FBS 16
	uint32_t fbs[MAX_FBS];
	int count_fbs = 0;

	drmModePlaneResPtr planes = drmModeGetPlaneResources(fd);
	if (planes) {
		MSG("count_planes = %u", planes->count_planes);
		for (uint32_t i = 0; i < planes->count_planes; ++i) {
			MSG("\t%u: %#x", i, planes->planes[i]);
			drmModePlanePtr plane = drmModeGetPlane(fd, planes->planes[i]);
			if (plane) {
				MSG("\tcrtc_id=%#x fb_id=%#x crtc_x=%u crtc_y=%u x=%u y=%u possible_crtcs=%#x gamma_size=%u",
						plane->crtc_id, plane->fb_id, plane->crtc_x, plane->crtc_y, plane->x, plane->y,
						plane->possible_crtcs, plane->gamma_size);
				MSG("\tcount_formats = %u", plane->count_formats);
				for (uint32_t j = 0; j < plane->count_formats; ++j) {
					const uint32_t f = plane->formats[j];
					MSG("\t\t%u: %#x %c%c%c%c", j, f, f&0xff, (f>>8)&0xff, (f>>16)&0xff, (f>>24)&0xff);
				}

				if (plane->fb_id) {
					int found = 0;
					for (int k = 0; k < count_fbs; ++k) {
						if (fbs[k] == plane->fb_id) {
							found = 1;
							break;
						}
					}

					if (!found) {
						if (count_fbs == MAX_FBS) {
							MSG("Max number of fbs (%d) exceeded", MAX_FBS);
						} else {
							fbs[count_fbs++] = plane->fb_id;
						}
					}
				}
				drmModeFreePlane(plane);
			}
		}
		drmModeFreePlaneResources(planes);
	}

	MSG("count_fbs = %d", count_fbs);
	for (int i = 0; i < count_fbs; ++i) {
		MSG("\t%d: %#x", i, fbs[i]);
		drmModeFBPtr fb = drmModeGetFB(fd, fbs[i]);
		if (!fb) {
			MSG("\t\tERROR");
			continue;
		}

		MSG("\t\twidth=%u height=%u pitch=%u bpp=%u depth=%u handle=%#x",
			fb->width, fb->height, fb->pitch, fb->bpp, fb->depth, fb->handle);

		drmModeFreeFB(fb);
	}
}
#endif

static void enumerateDevices(void) {
	const int devices_count = drmGetDevices2(0, NULL, 0);
	MSG("Found %d devices", devices_count);

	drmDevicePtr *const devices = malloc(sizeof(drmDevicePtr) * devices_count);
	const int devices_count2 = drmGetDevices2(0, devices, devices_count);
	ASSERT(devices_count2 == devices_count);

	for (int i = 0; i < devices_count2; ++i) {
		const drmDevicePtr dev = devices[i];
		if (!(dev->available_nodes & (1 << DRM_NODE_PRIMARY)))
			continue;

		const char *node = dev->nodes[DRM_NODE_PRIMARY];
		if (!node || node[0] == '\0') {
			MSG("Device %d, avaliable_nodes report DRM_NODE_PRIMARY, but no node name is available", i);
			continue;
		}

		MSG("Trying device %d", i);
		DrmDevice *dd = ddOpen(node);
		if (!dd)
			MSG("Failed");
		if (dd)
			ddClose(dd);
	}
	

	drmFreeDevices(devices, devices_count2);
	free(devices);
}

int main(int argc, const char *argv[]) {
	(void)argc;
	(void)argv;

	const int available = drmAvailable();
	if (!available) {
		MSG("libdrm is not available");
		return 1;
	}

	enumerateDevices();

	return 0;
}
