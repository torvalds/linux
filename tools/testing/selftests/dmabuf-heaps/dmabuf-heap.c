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
#include "../kselftest.h"

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
	if (ret < 0)
		ksft_exit_fail_msg("snprintf failed! %d\n", ret);

	fd = open(buf, O_RDWR);
	if (fd < 0)
		ksft_exit_fail_msg("open %s failed: %s\n", buf, strerror(errno));

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

static void test_alloc_and_import(char *heap_name)
{
	int heap_fd = -1, dmabuf_fd = -1, importer_fd = -1;
	uint32_t handle = 0;
	void *p = NULL;
	int ret;

	heap_fd = dmabuf_heap_open(heap_name);

	ksft_print_msg("Testing allocation and importing:\n");
	ret = dmabuf_heap_alloc(heap_fd, ONE_MEG, 0, &dmabuf_fd);
	if (ret) {
		ksft_test_result_fail("FAIL (Allocation Failed!) %d\n", ret);
		return;
	}

	/* mmap and write a simple pattern */
	p = mmap(NULL, ONE_MEG, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd, 0);
	if (p == MAP_FAILED) {
		ksft_test_result_fail("FAIL (mmap() failed): %s\n", strerror(errno));
		goto close_and_return;
	}

	dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_START);
	memset(p, 1, ONE_MEG / 2);
	memset((char *)p + ONE_MEG / 2, 0, ONE_MEG / 2);
	dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_END);

	importer_fd = open_vgem();
	if (importer_fd < 0) {
		ksft_test_result_skip("Could not open vgem %d\n", importer_fd);
	} else {
		ret = import_vgem_fd(importer_fd, dmabuf_fd, &handle);
		ksft_test_result(ret >= 0, "Import buffer %d\n", ret);
	}

	ret = dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_START);
	if (ret < 0) {
		ksft_print_msg("FAIL (DMA_BUF_SYNC_START failed!) %d\n", ret);
		goto out;
	}

	memset(p, 0xff, ONE_MEG);
	ret = dmabuf_sync(dmabuf_fd, DMA_BUF_SYNC_END);
	if (ret < 0) {
		ksft_print_msg("FAIL (DMA_BUF_SYNC_END failed!) %d\n", ret);
		goto out;
	}

	close_handle(importer_fd, handle);
	ksft_test_result_pass("%s dmabuf sync succeeded\n", __func__);
	return;

out:
	ksft_test_result_fail("%s dmabuf sync failed\n", __func__);
	munmap(p, ONE_MEG);
	close(importer_fd);

close_and_return:
	close(dmabuf_fd);
	close(heap_fd);
}

static void test_alloc_zeroed(char *heap_name, size_t size)
{
	int heap_fd = -1, dmabuf_fd[32];
	int i, j, k, ret;
	void *p = NULL;
	char *c;

	ksft_print_msg("Testing alloced %ldk buffers are zeroed:\n", size / 1024);
	heap_fd = dmabuf_heap_open(heap_name);

	/* Allocate and fill a bunch of buffers */
	for (i = 0; i < 32; i++) {
		ret = dmabuf_heap_alloc(heap_fd, size, 0, &dmabuf_fd[i]);
		if (ret) {
			ksft_test_result_fail("FAIL (Allocation (%i) failed) %d\n", i, ret);
			goto close_and_return;
		}

		/* mmap and fill with simple pattern */
		p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd[i], 0);
		if (p == MAP_FAILED) {
			ksft_test_result_fail("FAIL (mmap() failed!): %s\n", strerror(errno));
			goto close_and_return;
		}

		dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_START);
		memset(p, 0xff, size);
		dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_END);
		munmap(p, size);
	}
	/* close them all */
	for (i = 0; i < 32; i++)
		close(dmabuf_fd[i]);
	ksft_test_result_pass("Allocate and fill a bunch of buffers\n");

	/* Allocate and validate all buffers are zeroed */
	for (i = 0; i < 32; i++) {
		ret = dmabuf_heap_alloc(heap_fd, size, 0, &dmabuf_fd[i]);
		if (ret < 0) {
			ksft_test_result_fail("FAIL (Allocation (%i) failed) %d\n", i, ret);
			goto close_and_return;
		}

		/* mmap and validate everything is zero */
		p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd[i], 0);
		if (p == MAP_FAILED) {
			ksft_test_result_fail("FAIL (mmap() failed!): %s\n", strerror(errno));
			goto close_and_return;
		}

		dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_START);
		c = (char *)p;
		for (j = 0; j < size; j++) {
			if (c[j] != 0) {
				ksft_print_msg("FAIL (Allocated buffer not zeroed @ %i)\n", j);
				dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_END);
				munmap(p, size);
				goto out;
			}
		}
		dmabuf_sync(dmabuf_fd[i], DMA_BUF_SYNC_END);
		munmap(p, size);
	}

out:
	ksft_test_result(i == 32, "Allocate and validate all buffers are zeroed\n");

close_and_return:
	/* close them all */
	for (k = 0; k < i; k++)
		close(dmabuf_fd[k]);

	close(heap_fd);
	return;
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

static void test_alloc_compat(char *heap_name)
{
	int ret, heap_fd = -1, dmabuf_fd = -1;

	heap_fd = dmabuf_heap_open(heap_name);

	ksft_print_msg("Testing (theoretical) older alloc compat:\n");
	ret = dmabuf_heap_alloc_older(heap_fd, ONE_MEG, 0, &dmabuf_fd);
	if (dmabuf_fd >= 0)
		close(dmabuf_fd);
	ksft_test_result(!ret, "dmabuf_heap_alloc_older\n");

	ksft_print_msg("Testing (theoretical) newer alloc compat:\n");
	ret = dmabuf_heap_alloc_newer(heap_fd, ONE_MEG, 0, &dmabuf_fd);
	if (dmabuf_fd >= 0)
		close(dmabuf_fd);
	ksft_test_result(!ret, "dmabuf_heap_alloc_newer\n");

	close(heap_fd);
}

static void test_alloc_errors(char *heap_name)
{
	int heap_fd = -1, dmabuf_fd = -1;
	int ret;

	heap_fd = dmabuf_heap_open(heap_name);

	ksft_print_msg("Testing expected error cases:\n");
	ret = dmabuf_heap_alloc(0, ONE_MEG, 0x111111, &dmabuf_fd);
	ksft_test_result(ret, "Error expected on invalid fd %d\n", ret);

	ret = dmabuf_heap_alloc(heap_fd, ONE_MEG, 0x111111, &dmabuf_fd);
	ksft_test_result(ret, "Error expected on invalid heap flags %d\n", ret);

	ret = dmabuf_heap_alloc_fdflags(heap_fd, ONE_MEG,
					~(O_RDWR | O_CLOEXEC), 0, &dmabuf_fd);
	ksft_test_result(ret, "Error expected on invalid heap flags %d\n", ret);

	if (dmabuf_fd >= 0)
		close(dmabuf_fd);
	close(heap_fd);
}

static int numer_of_heaps(void)
{
	DIR *d = opendir(DEVPATH);
	struct dirent *dir;
	int heaps = 0;

	while ((dir = readdir(d))) {
		if (!strncmp(dir->d_name, ".", 2))
			continue;
		if (!strncmp(dir->d_name, "..", 3))
			continue;
		heaps++;
	}

	return heaps;
}

int main(void)
{
	struct dirent *dir;
	DIR *d;

	ksft_print_header();

	d = opendir(DEVPATH);
	if (!d) {
		ksft_print_msg("No %s directory?\n", DEVPATH);
		return KSFT_SKIP;
	}

	ksft_set_plan(11 * numer_of_heaps());

	while ((dir = readdir(d))) {
		if (!strncmp(dir->d_name, ".", 2))
			continue;
		if (!strncmp(dir->d_name, "..", 3))
			continue;

		ksft_print_msg("Testing heap: %s\n", dir->d_name);
		ksft_print_msg("=======================================\n");
		test_alloc_and_import(dir->d_name);
		test_alloc_zeroed(dir->d_name, 4 * 1024);
		test_alloc_zeroed(dir->d_name, ONE_MEG);
		test_alloc_compat(dir->d_name);
		test_alloc_errors(dir->d_name);
	}
	closedir(d);

	ksft_finished();
}
