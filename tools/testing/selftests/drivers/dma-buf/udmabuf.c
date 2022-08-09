// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#define __EXPORTED_HEADERS__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>

#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <linux/udmabuf.h>

#define TEST_PREFIX	"drivers/dma-buf/udmabuf"
#define NUM_PAGES       4

static int memfd_create(const char *name, unsigned int flags)
{
	return syscall(__NR_memfd_create, name, flags);
}

int main(int argc, char *argv[])
{
	struct udmabuf_create create;
	int devfd, memfd, buf, ret;
	off_t size;
	void *mem;

	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		printf("%s: [skip,no-udmabuf]\n", TEST_PREFIX);
		exit(77);
	}

	memfd = memfd_create("udmabuf-test", MFD_ALLOW_SEALING);
	if (memfd < 0) {
		printf("%s: [skip,no-memfd]\n", TEST_PREFIX);
		exit(77);
	}

	ret = fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret < 0) {
		printf("%s: [skip,fcntl-add-seals]\n", TEST_PREFIX);
		exit(77);
	}


	size = getpagesize() * NUM_PAGES;
	ret = ftruncate(memfd, size);
	if (ret == -1) {
		printf("%s: [FAIL,memfd-truncate]\n", TEST_PREFIX);
		exit(1);
	}

	memset(&create, 0, sizeof(create));

	/* should fail (offset not page aligned) */
	create.memfd  = memfd;
	create.offset = getpagesize()/2;
	create.size   = getpagesize();
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0) {
		printf("%s: [FAIL,test-1]\n", TEST_PREFIX);
		exit(1);
	}

	/* should fail (size not multiple of page) */
	create.memfd  = memfd;
	create.offset = 0;
	create.size   = getpagesize()/2;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0) {
		printf("%s: [FAIL,test-2]\n", TEST_PREFIX);
		exit(1);
	}

	/* should fail (not memfd) */
	create.memfd  = 0; /* stdin */
	create.offset = 0;
	create.size   = size;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0) {
		printf("%s: [FAIL,test-3]\n", TEST_PREFIX);
		exit(1);
	}

	/* should work */
	create.memfd  = memfd;
	create.offset = 0;
	create.size   = size;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf < 0) {
		printf("%s: [FAIL,test-4]\n", TEST_PREFIX);
		exit(1);
	}

	fprintf(stderr, "%s: ok\n", TEST_PREFIX);
	close(buf);
	close(memfd);
	close(devfd);
	return 0;
}
