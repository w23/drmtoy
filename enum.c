//#!cc -Werror -std=c99 -I/usr/include/libdrm -ldrm enum.c -o enum && ./enum /dev/dri/card0
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#define MSG(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)

void enumerateModeResources(int fd, const drmModeResPtr res) {
	MSG("\tcount_fbs = %d", res->count_fbs);
	for (int i = 0; i < res->count_fbs; ++i)
		MSG("\t\t%d: 0x%x", i, res->fbs[i]);

	MSG("\tcount_crtcs = %d", res->count_crtcs);
	for (int i = 0; i < res->count_crtcs; ++i) {
		MSG("\t\t%d: 0x%x", i, res->crtcs[i]);
		drmModeCrtcPtr crtc = drmModeGetCrtc(fd, res->crtcs[i]);
		if (crtc) {
			MSG("\t\t\tbuffer_id = 0x%x gamma_size = %d", crtc->buffer_id, crtc->gamma_size);
			MSG("\t\t\t(%u %u %u %u) %d",
				crtc->x, crtc->y, crtc->width, crtc->height, crtc->mode_valid);
			MSG("\t\t\tmode.name = %s", crtc->mode.name);
			drmModeFreeCrtc(crtc);
		}
	}

	MSG("\tcount_connectors = %d", res->count_connectors);
	for (int i = 0; i < res->count_connectors; ++i) {
		MSG("\t\t%d: 0x%x", i, res->connectors[i]);
		drmModeConnectorPtr conn = drmModeGetConnectorCurrent(fd, res->connectors[i]);
		if (conn) {
			drmModeFreeConnector(conn);
		}
	}

	MSG("\tcount_encoders = %d", res->count_encoders);
	for (int i = 0; i < res->count_encoders; ++i)
		MSG("\t\t%d: 0x%x", i, res->encoders[i]);

	MSG("\twidth: %u .. %u", res->min_width, res->max_width);
	MSG("\theight: %u .. %u", res->min_height, res->max_height);
}

int main(int argc, const char *argv[]) {
	const int available = drmAvailable();
	if (!available)
		return 1;

	const char *card = (argc > 1) ? argv[1] : "/dev/dri/card0";

	const int fd = open(card, O_RDONLY);
	MSG("open = %d", fd);
	if (fd < 2)
		return 2;

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

	{
		const drmVersionPtr ver = drmGetVersion(fd);
		MSG("drm version %d.%d.%d driver=%.*s date=%.*s desc=%.*s",
			ver->version_major, ver->version_minor, ver->version_patchlevel,
			ver->name_len, ver->name,
			ver->date_len, ver->date,
			ver->desc_len, ver->desc);
		drmFreeVersion(ver);
	}

	{
		const drmVersionPtr ver = drmGetLibVersion(fd);
		MSG("drm lib version %d.%d.%d driver=%.*s date=%.*s desc=%.*s",
			ver->version_major, ver->version_minor, ver->version_patchlevel,
			ver->name_len, ver->name,
			ver->date_len, ver->date,
			ver->desc_len, ver->desc);
		drmFreeVersion(ver);
	}

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

	drmModeResPtr res = drmModeGetResources(fd);
	if (res) {
		enumerateModeResources(fd, res);
		drmModeFreeResources(res);
	}

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

	close(fd);
}
