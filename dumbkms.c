// Force 64-bit mmap() on 32-bit, otherwise dumb buffer offset gets truncated to 32 bits, and mapping the dumb buffer fails.
#define _FILE_OFFSET_BITS 64

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <sys/mman.h>
#include <errno.h>

#define MSG(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#define ASSERT(a) assert(a)

static int openFirstKmsDevice(void) {
	int fd = -1;
	const int devices_count = drmGetDevices2(0, NULL, 0);
	MSG("Found %d devices", devices_count);
	if (devices_count < 1)
		return -1;

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

		fd = open(node, O_RDWR);
		if (fd < 0) {
			MSG("Device %d, unable to open node \"%s\"", i, node);
			continue;
		}

		if (!drmIsKMS(fd)) {
			MSG("Device %d, node \"%s\" is not suitable for KMS", i, node);
			close(fd);
			continue;
		}

		MSG("Device %d, node \"%s\" selected as fd=%d", i, node, fd);
		break;
	}

	drmFreeDevices(devices, devices_count2);
	free(devices);
	return fd;
}

static drmModeConnectorPtr findFirstConnectedConnector(int fd, const drmModeResPtr res) {
	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnectorPtr c = drmModeGetConnector(fd, res->connectors[i]);

		if (c->connection == DRM_MODE_CONNECTED) {
			MSG("Found first connected connector index=%d", i);
			return c;
		}

		drmModeFreeConnector(c);
	}

	return NULL;
}

static drmModeModeInfoPtr findPreferredMode(const drmModeConnectorPtr conn) {
	for (int i = 0; i < conn->count_modes; ++i) {
		drmModeModeInfoPtr mode = &conn->modes[i];
		if ((mode->type & DRM_MODE_TYPE_PREFERRED) == DRM_MODE_TYPE_PREFERRED) {
			MSG("Found preferred mode %ux%u@%u with index=%d", mode->vdisplay, mode->hdisplay, mode->vrefresh, i);
			return mode;
		}
	}

	return NULL;
}

static uint32_t findEncoderCompatibleCrtcId(int fd, const drmModeResPtr res, uint32_t encoder_id) {
	drmModeEncoderPtr enc = drmModeGetEncoder(fd, encoder_id);

	if (enc->crtc_id)
		return enc->crtc_id;

	for (int i = 0; i < res->count_crtcs; ++i) {
		if (enc->possible_crtcs & (1 << i))
			return res->crtcs[i];
	}

	drmModeFreeEncoder(enc);

	return 0;
}

static uint32_t findCrtcIdForConnector(int fd, const drmModeResPtr res, const drmModeConnectorPtr conn) {
	if (conn->encoder_id) {
		const uint32_t crtc_id = findEncoderCompatibleCrtcId(fd, res, conn->encoder_id);
		if (crtc_id)
			return crtc_id;
	}

	// If no current encoder, find a new one
	for (int i = 0; i < conn->count_encoders; ++i) {
		const uint32_t crtc_id = findEncoderCompatibleCrtcId(fd, res, conn->encoders[i]);
		if (crtc_id)
			return crtc_id;
	}

	return 0;
}

typedef struct {
	struct drm_mode_create_dumb dumb;
	uint32_t id;
	uint8_t *mapped;
} DumbFramebuffer;

static void destroyFramebuffer(int fd, DumbFramebuffer *fb) {
	if (fb->mapped)
		munmap(fb->mapped, fb->dumb.size);

	if (fb->dumb.handle) {
		struct drm_mode_destroy_dumb destroy = { .handle = fb->dumb.handle, };
		const int res = drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
		if (res != 0)
			MSG("Error destroying dumb buffer: %d: %s", res, strerror(-res));
	}

	if (fb->id)
		drmModeRmFB(fd, fb->id);

	*fb = (DumbFramebuffer){0};
}

static DumbFramebuffer createFramebuffer(int fd, const drmModeModeInfoPtr mode) {
	DumbFramebuffer fb;
	fb.dumb = (struct drm_mode_create_dumb) {
		.width = mode->hdisplay,
		.height = mode->vdisplay,
		.bpp = 16,
	};

	int res = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &fb.dumb);
	if (res < 0) {
		MSG("Unable to create dumb buffer: %d", res);
		return (DumbFramebuffer) {0};
	}

	MSG("dumb.handle = %x", fb.dumb.handle);
	MSG("dumb.pitch = %u", fb.dumb.pitch);
	MSG("dumb.size = %llu", (unsigned long long)fb.dumb.size);

	{
		const uint32_t pixel_format = DRM_FORMAT_RGB565;
		const uint32_t handles[4] = {fb.dumb.handle, 0, 0, 0};
		const uint32_t pitches[4] = {fb.dumb.pitch, 0, 0, 0};
		const uint32_t offsets[4] = {0, 0, 0, 0};
		const uint32_t flags = 0;
		res = drmModeAddFB2(fd, fb.dumb.width, fb.dumb.height, pixel_format, handles, pitches, offsets, &fb.id, flags);
		if (res < 0) {
			MSG("Unable to create framebuffer: %d", res);
			goto fail;
		}
	}

	{
		struct drm_mode_map_dumb map = { .handle = fb.dumb.handle, };
		res = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
		if (res != 0) {
			MSG("Failed to map dumb buffer: %d: %s", res, strerror(-res));
			goto fail;
		}
		MSG("map.offset = %llu", (unsigned long long)map.offset);

		fb.mapped = mmap(0, fb.dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
		if (fb.mapped == MAP_FAILED) {
			MSG("Failed to mmap dumb buffer: %d: %s", errno, strerror(errno));
			goto fail;
		}

		MSG("Dumb framebuffer mapped at %p", fb.mapped);
	}

	return fb;

fail:
	destroyFramebuffer(fd, &fb);
	return (DumbFramebuffer) {0};
}

// FIXME destroy fb

static void draw(uint8_t *buf, int w, int h, int pitch, int frame) {
	for (int y = 0; y < h; ++y) {
		const int yoff = pitch * y;
		for (int x = 0; x < w; ++x) {
			const uint64_t color = x * y / (frame + 1);
			memcpy(&buf[yoff + x * sizeof(uint16_t)], &color, sizeof(uint16_t));
		}
	}
}

// Set preferred mode on the first connected output/connector
static int setMode(int fd) {
	int ret = -1;

	drmModeResPtr res = NULL;
	drmModeConnectorPtr conn = NULL;
	drmModeModeInfoPtr mode = NULL;
	DumbFramebuffer fb = {0};

	res = drmModeGetResources(fd);
	if (!res) {
		MSG("Unable to get KMS resources");
		goto cleanup;
	}

	conn = findFirstConnectedConnector(fd, res);
	if (!conn) {
		MSG("No connected connectors found");
		goto cleanup;
	}

	const uint32_t crtc_id = findCrtcIdForConnector(fd, res, conn);
	if (!crtc_id) {
		MSG("No compatible crtc found");
		goto cleanup;
	}
	MSG("Using crtc_id=0x%x", crtc_id);

	uint64_t has_dumb = 0;
	drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &has_dumb);
	MSG("Dumb buffers support: %d", (int)has_dumb);

	mode = findPreferredMode(conn);
	if (!mode) {
		MSG("No preferred mode found");
		goto cleanup;
	}

	fb = createFramebuffer(fd, mode);
	if (!fb.id) {
		MSG("Cannot create dumb framebuffer");
		goto cleanup;
	}

	int result = drmModeSetCrtc(fd, crtc_id, fb.id, 0, 0, &conn->connector_id, 1, mode);
	MSG("drmModeSetCrtc(crtc=0x%x, fb=0x%x, conn=0x%x, mode=%s) returned %d: %s",
		crtc_id, fb.id, conn->connector_id, mode->name,
		result, strerror(-result));

	MSG("Begin drawing");

	for (int i = 0; i < 120; ++i) {
		draw(fb.mapped, fb.dumb.width, fb.dumb.height, fb.dumb.pitch, i);
		//usleep(16 * 1000);
	}

	MSG("End drawing");

cleanup:
	destroyFramebuffer(fd, &fb);

	if (conn)
		drmModeFreeConnector(conn);

	if (res)
		drmModeFreeResources(res);

	return ret;
}

int main(int argc, const char *argv[]) {
	(void)argc;
	(void)argv;

	const int fd = openFirstKmsDevice();
	if (fd < 0) {
		MSG("Couldn't open KMS-suitable DRM render device");
		return 1;
	}

	int result = drmSetMaster(fd);
	MSG("Setting DRM master, result=%d", result);

	setMode(fd);

	close(fd);
	return 0;
}
