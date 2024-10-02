// SPDX-License-Identifier: GPL-2.0

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <drm/drm.h>

#define DEVPATH "/dev/dma_heap"

static int check_vgem(int fd)
{
	drm_version_t version = { 0 };
	char name[5];
	int ret;

	version.name_len = 4;
	version.name = name;

	ret = ioctl(fd, DRM_IOCTL_VERSION, &version);
	if (ret || version.name_len != 4)
		return 0;

	name[4] = '\0';

	return !strcmp(name, "vgem");
}

static int open_vgem(void)
{
	int i, fd;
	const char *drmstr = "/dev/dri/card";

	fd = -1;
	for (i = 0; i < 16; i++) {
		char name[80];

		snprintf(name, 80, "%s%u", drmstr, i);

		fd = open(name, O_RDWR);
		if (fd < 0)
			continue;

		if (!check_vgem(fd)) {
			close(fd);
			fd = -1;
			continue;
		} else {
			break;
		}
	}
	return fd;
}

static int import_vgem_fd(int vgem_fd, int dma_buf_fd, uint32_t *handle)
{
	struct drm_prime_handle import_handle = {
		.fd = dma_buf_fd,
		.flags = 0,
		.handle = 0,
	 };
	int ret;

	ret = ioctl(vgem_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &import_handle);
	if (ret == 0)
		*handle = import_handle.handle;
	return ret;
}

static void close_handle(int vgem_fd, uint32_t handle)
{
	struct drm_gem_close close = {
		.handle = handle,
	};

	ioctl(vgem_fd, DRM_IOCTL_GEM_CLOSE, &close);
}

static int dmabuf_heap_open(char *name)
{
	int ret, fd;
	char buf[256];

	ret = snprintf(buf, 256, "%s/%s", DEVPATH, name);
	if (ret < 0) {
		printf("snprintf failed!\n");
		return ret;
	}

	fd = open(buf, O_RDWR);
	if (fd < 0)
		printf("open %s failed!\n", buf);
	return fd;
}

static int dmabuf_heap_alloc_fdflags(int fd, size_t len, unsigned int fd_flags,
				     unsigned int heap_flags, int *dmabuf_fd)
{
	struct dma_heap_allocation_data data = {
		.len = len,
		.fd = 0,
		.fd_flags = fd_flags,
		.heap_flags = heap_flags,
	};
	int ret;

	if (!dmabuf_fd)
		return -EINVAL;

	ret = ioctl(fd, DMA_HEAP_IOCTL_ALLOC, &data);
	if (ret < 0)
		return ret;
	*dmabuf_fd = (int)data.fd;
	return ret;
}

static int dmabuf_heap_alloc(int fd, size_t len, unsigned int flags,
			     int *dmabuf_fd)
{
	return dmabuf_heap_alloc_fdflags(fd, len, O_RDWR | O_CLOEXEC, flags,
					 dmabuf_fd);
}

static int dmabuf_sync(int fd, int start_stop)
{
	struct dma_buf_sync sync = {
		.flags = start_stop | DMA_BUF_SYNC_RW,
	};

	return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync);
}

#define ONE_MEG (1024 * 1024)

static int test_alloc_and_import(char *heap_name)
{
	int heap_fd = -1, dmabuf_fd = -1, importer_fd = -1;
	uint32_t handle = 0;
	void *p = NULL;
	int ret;

	heap_fd = dmabuf_heap_open(heap_name);
	if (heap_fd < 0)
		return -1;

	printf("  Testing allocation and importing:  ");
	ret = dmabuf_heap_alloc(heap_fd, ONE_MEG, 0, &dmabuf_fd);
	if (ret) {
		printf("FAIL (Allocation Failed!)\n");
		ret = -1;
		goto out;
	}
	/* mmap and write a simple pattern */
	p = mmap(NULL,
		 ONE_MEG,
		 PROT_READ | PROT_WRITE,
		 MAP_SHARED,
		 dmabuf_fd,
		 0);
	if (p == MAP_FAILED) {
		printf("FAIL (mmap() failed)\n");
		ret = -1;
		goto out;
	}

	dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_START);
	memset(p, 1, ONE_MEG / 2);
	memset((char *)p + ONE_MEG / 2, 0, ONE_MEG / 2);
	dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_END);

	importer_fd = open_vgem();
	if (importer_fd < 0) {
		ret = importer_fd;
		printf("(Could not open vgem - skipping):  ");
	} else {
		ret = import_vgem_fd(importer_fd, dmabuf_fd, &handle);
		if (ret < 0) {
			printf("FAIL (Failed to import buffer)\n");
			goto out;
		}
	}

	ret = dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_START);
	if (ret < 0) {
		printf("FAIL (DMA_BUF_SYNC_START failed!)\n");
		goto out;
	}

	memset(p, 0xff, ONE_MEG);
	ret = dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_END);
	if (ret < 0) {
		printf("FAIL (DMA_BUF_SYNC_END failed!)\n");
		goto out;
	}

	close_handle(importer_fd, handle);
	ret = 0;
	printf(" OK\n");
out:
	if (p)
		munmap(p, ONE_MEG);
	if (importer_fd >= 0)
		close(importer_fd);
	if (dmabuf_fd >= 0)
		close(dmabuf_fd);
	if (heap_fd >= 0)
		close(heap_fd);

	return ret;
}

static int test_alloc_zeroed(char *heap_name, size_t size)
{
	int heap_fd = -1, dmabuf_fd[32];
	int i, j, ret;
	void *p = NULL;
	char *c;

	printf("  Testing alloced %ldk buffers are zeroed:  ", size / 1024);
	heap_fd = dmabuf_heap_open(heap_name);
	if (heap_fd < 0)
		return -1;

	/* Allocate and fill a bunch of buffers */
	for (i = 0; i < 32; i++) {
		ret = dmabuf_heap_alloc(heap_fd, size, 0, &dmabuf_fd[i]);
		if (ret < 0) {
			printf("FAIL (Allocation (%i) failed)\n", i);
			goto out;
		}
		/* mmap and fill with simple pattern */
		p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd[i], 0);
		if (p == MAP_FAILED) {
			printf("FAIL (mmap() failed!)\n");
			ret = -1;
			goto out;
		}
		dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_START);
		memset(p, 0xff, size);
		dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_END);
		munmap(p, size);
	}
	/* close them all */
	for (i = 0; i < 32; i++)
		close(dmabuf_fd[i]);

	/* Allocate and validate all buffers are zeroed */
	for (i = 0; i < 32; i++) {
		ret = dmabuf_heap_alloc(heap_fd, size, 0, &dmabuf_fd[i]);
		if (ret < 0) {
			printf("FAIL (Allocation (%i) failed)\n", i);
			goto out;
		}

		/* mmap and validate everything is zero */
		p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd[i], 0);
		if (p == MAP_FAILED) {
			printf("FAIL (mmap() failed!)\n");
			ret = -1;
			goto out;
		}
		dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_START);
		c = (char *)p;
		for (j = 0; j < size; j++) {
			if (c[j] != 0) {
				printf("FAIL (Allocated buffer not zeroed @ %i)\n", j);
				break;
			}
		}
		dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_END);
		munmap(p, size);
	}
	/* close them all */
	for (i = 0; i < 32; i++)
		close(dmabuf_fd[i]);

	close(heap_fd);
	printf("OK\n");
	return 0;

out:
	while (i > 0) {
		close(dmabuf_fd[i]);
		i--;
	}
	close(heap_fd);
	return ret;
}

/* Test the ioctl version compatibility w/ a smaller structure then expected */
static int dmabuf_heap_alloc_older(int fd, size_t len, unsigned int flags,
				   int *dmabuf_fd)
{
	int ret;
	unsigned int older_alloc_ioctl;
	struct dma_heap_allocation_data_smaller {
		__u64 len;
		__u32 fd;
		__u32 fd_flags;
	} data = {
		.len = len,
		.fd = 0,
		.fd_flags = O_RDWR | O_CLOEXEC,
	};

	older_alloc_ioctl = _IOWR(DMA_HEAP_IOC_MAGIC, 0x0,
				  struct dma_heap_allocation_data_smaller);
	if (!dmabuf_fd)
		return -EINVAL;

	ret = ioctl(fd, older_alloc_ioctl, &data);
	if (ret < 0)
		return ret;
	*dmabuf_fd = (int)data.fd;
	return ret;
}

/* Test the ioctl version compatibility w/ a larger structure then expected */
static int dmabuf_heap_alloc_newer(int fd, size_t len, unsigned int flags,
				   int *dmabuf_fd)
{
	int ret;
	unsigned int newer_alloc_ioctl;
	struct dma_heap_allocation_data_bigger {
		__u64 len;
		__u32 fd;
		__u32 fd_flags;
		__u64 heap_flags;
		__u64 garbage1;
		__u64 garbage2;
		__u64 garbage3;
	} data = {
		.len = len,
		.fd = 0,
		.fd_flags = O_RDWR | O_CLOEXEC,
		.heap_flags = flags,
		.garbage1 = 0xffffffff,
		.garbage2 = 0x88888888,
		.garbage3 = 0x11111111,
	};

	newer_alloc_ioctl = _IOWR(DMA_HEAP_IOC_MAGIC, 0x0,
				  struct dma_heap_allocation_data_bigger);
	if (!dmabuf_fd)
		return -EINVAL;

	ret = ioctl(fd, newer_alloc_ioctl, &data);
	if (ret < 0)
		return ret;

	*dmabuf_fd = (int)data.fd;
	return ret;
}

static int test_alloc_compat(char *heap_name)
{
	int heap_fd = -1, dmabuf_fd = -1;
	int ret;

	heap_fd = dmabuf_heap_open(heap_name);
	if (heap_fd < 0)
		return -1;

	printf("  Testing (theoretical)older alloc compat:  ");
	ret = dmabuf_heap_alloc_older(heap_fd, ONE_MEG, 0, &dmabuf_fd);
	if (ret) {
		printf("FAIL (Older compat allocation failed!)\n");
		ret = -1;
		goto out;
	}
	close(dmabuf_fd);
	printf("OK\n");

	printf("  Testing (theoretical)newer alloc compat:  ");
	ret = dmabuf_heap_alloc_newer(heap_fd, ONE_MEG, 0, &dmabuf_fd);
	if (ret) {
		printf("FAIL (Newer compat allocation failed!)\n");
		ret = -1;
		goto out;
	}
	printf("OK\n");
out:
	if (dmabuf_fd >= 0)
		close(dmabuf_fd);
	if (heap_fd >= 0)
		close(heap_fd);

	return ret;
}

static int test_alloc_errors(char *heap_name)
{
	int heap_fd = -1, dmabuf_fd = -1;
	int ret;

	heap_fd = dmabuf_heap_open(heap_name);
	if (heap_fd < 0)
		return -1;

	printf("  Testing expected error cases:  ");
	ret = dmabuf_heap_alloc(0, ONE_MEG, 0x111111, &dmabuf_fd);
	if (!ret) {
		printf("FAIL (Did not see expected error (invalid fd)!)\n");
		ret = -1;
		goto out;
	}

	ret = dmabuf_heap_alloc(heap_fd, ONE_MEG, 0x111111, &dmabuf_fd);
	if (!ret) {
		printf("FAIL (Did not see expected error (invalid heap flags)!)\n");
		ret = -1;
		goto out;
	}

	ret = dmabuf_heap_alloc_fdflags(heap_fd, ONE_MEG,
					~(O_RDWR | O_CLOEXEC), 0, &dmabuf_fd);
	if (!ret) {
		printf("FAIL (Did not see expected error (invalid fd flags)!)\n");
		ret = -1;
		goto out;
	}

	printf("OK\n");
	ret = 0;
out:
	if (dmabuf_fd >= 0)
		close(dmabuf_fd);
	if (heap_fd >= 0)
		close(heap_fd);

	return ret;
}

int main(void)
{
	DIR *d;
	struct dirent *dir;
	int ret = -1;

	d = opendir(DEVPATH);
	if (!d) {
		printf("No %s directory?\n", DEVPATH);
		return -1;
	}

	while ((dir = readdir(d)) != NULL) {
		if (!strncmp(dir->d_name, ".", 2))
			continue;
		if (!strncmp(dir->d_name, "..", 3))
			continue;

		printf("Testing heap: %s\n", dir->d_name);
		printf("=======================================\n");
		ret = test_alloc_and_import(dir->d_name);
		if (ret)
			break;

		ret = test_alloc_zeroed(dir->d_name, 4 * 1024);
		if (ret)
			break;

		ret = test_alloc_zeroed(dir->d_name, ONE_MEG);
		if (ret)
			break;

		ret = test_alloc_compat(dir->d_name);
		if (ret)
			break;

		ret = test_alloc_errors(dir->d_name);
		if (ret)
			break;
	}
	closedir(d);

	return ret;
}
