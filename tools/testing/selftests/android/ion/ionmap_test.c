#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <linux/dma-buf.h>

#include <drm/drm.h>

#include "ion.h"
#include "ionutils.h"

int check_vgem(int fd)
{
	drm_version_t version = { 0 };
	char name[5];
	int ret;

	version.name_len = 4;
	version.name = name;

	ret = ioctl(fd, DRM_IOCTL_VERSION, &version);
	if (ret)
		return 1;

	return strcmp(name, "vgem");
}

int open_vgem(void)
{
	int i, fd;
	const char *drmstr = "/dev/dri/card";

	fd = -1;
	for (i = 0; i < 16; i++) {
		char name[80];

		sprintf(name, "%s%u", drmstr, i);

		fd = open(name, O_RDWR);
		if (fd < 0)
			continue;

		if (check_vgem(fd)) {
			close(fd);
			continue;
		} else {
			break;
		}

	}
	return fd;
}

int import_vgem_fd(int vgem_fd, int dma_buf_fd, uint32_t *handle)
{
	struct drm_prime_handle import_handle = { 0 };
	int ret;

	import_handle.fd = dma_buf_fd;
	import_handle.flags = 0;
	import_handle.handle = 0;

	ret = ioctl(vgem_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &import_handle);
	if (ret == 0)
		*handle = import_handle.handle;
	return ret;
}

void close_handle(int vgem_fd, uint32_t handle)
{
	struct drm_gem_close close = { 0 };

	close.handle = handle;
	ioctl(vgem_fd, DRM_IOCTL_GEM_CLOSE, &close);
}

int main()
{
	int ret, vgem_fd;
	struct ion_buffer_info info;
	uint32_t handle = 0;
	struct dma_buf_sync sync = { 0 };

	info.heap_type = ION_HEAP_TYPE_SYSTEM;
	info.heap_size = 4096;
	info.flag_type = ION_FLAG_CACHED;

	ret = ion_export_buffer_fd(&info);
	if (ret < 0) {
		printf("ion buffer alloc failed\n");
		return -1;
	}

	vgem_fd = open_vgem();
	if (vgem_fd < 0) {
		ret = vgem_fd;
		printf("Failed to open vgem\n");
		goto out_ion;
	}

	ret = import_vgem_fd(vgem_fd, info.buffd, &handle);

	if (ret < 0) {
		printf("Failed to import buffer\n");
		goto out_vgem;
	}

	sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
	ret = ioctl(info.buffd, DMA_BUF_IOCTL_SYNC, &sync);
	if (ret)
		printf("sync start failed %d\n", errno);

	memset(info.buffer, 0xff, 4096);

	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
	ret = ioctl(info.buffd, DMA_BUF_IOCTL_SYNC, &sync);
	if (ret)
		printf("sync end failed %d\n", errno);

	close_handle(vgem_fd, handle);
	ret = 0;

out_vgem:
	close(vgem_fd);
out_ion:
	ion_close_buffer_fd(&info);
	printf("done.\n");
	return ret;
}
