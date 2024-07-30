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
#include <stdbool.h>

#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/memfd.h>
#include <linux/udmabuf.h>
#include "../../kselftest.h"

#define TEST_PREFIX	"drivers/dma-buf/udmabuf"
#define NUM_PAGES       4
#define NUM_ENTRIES     4
#define MEMFD_SIZE      1024 /* in pages */

static unsigned int page_size;

static int create_memfd_with_seals(off64_t size, bool hpage)
{
	int memfd, ret;
	unsigned int flags = MFD_ALLOW_SEALING;

	if (hpage)
		flags |= MFD_HUGETLB;

	memfd = memfd_create("udmabuf-test", flags);
	if (memfd < 0) {
		ksft_print_msg("%s: [skip,no-memfd]\n", TEST_PREFIX);
		exit(KSFT_SKIP);
	}

	ret = fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret < 0) {
		ksft_print_msg("%s: [skip,fcntl-add-seals]\n", TEST_PREFIX);
		exit(KSFT_SKIP);
	}

	ret = ftruncate(memfd, size);
	if (ret == -1) {
		ksft_print_msg("%s: [FAIL,memfd-truncate]\n", TEST_PREFIX);
		exit(KSFT_FAIL);
	}

	return memfd;
}

static int create_udmabuf_list(int devfd, int memfd, off64_t memfd_size)
{
	struct udmabuf_create_list *list;
	int ubuf_fd, i;

	list = malloc(sizeof(struct udmabuf_create_list) +
		      sizeof(struct udmabuf_create_item) * NUM_ENTRIES);
	if (!list) {
		ksft_print_msg("%s: [FAIL, udmabuf-malloc]\n", TEST_PREFIX);
		exit(KSFT_FAIL);
	}

	for (i = 0; i < NUM_ENTRIES; i++) {
		list->list[i].memfd  = memfd;
		list->list[i].offset = i * (memfd_size / NUM_ENTRIES);
		list->list[i].size   = getpagesize() * NUM_PAGES;
	}

	list->count = NUM_ENTRIES;
	list->flags = UDMABUF_FLAGS_CLOEXEC;
	ubuf_fd = ioctl(devfd, UDMABUF_CREATE_LIST, list);
	free(list);
	if (ubuf_fd < 0) {
		ksft_print_msg("%s: [FAIL, udmabuf-create]\n", TEST_PREFIX);
		exit(KSFT_FAIL);
	}

	return ubuf_fd;
}

static void write_to_memfd(void *addr, off64_t size, char chr)
{
	int i;

	for (i = 0; i < size / page_size; i++) {
		*((char *)addr + (i * page_size)) = chr;
	}
}

static void *mmap_fd(int fd, off64_t size)
{
	void *addr;

	addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		ksft_print_msg("%s: ubuf_fd mmap fail\n", TEST_PREFIX);
		exit(KSFT_FAIL);
	}

	return addr;
}

static int compare_chunks(void *addr1, void *addr2, off64_t memfd_size)
{
	off64_t off;
	int i = 0, j, k = 0, ret = 0;
	char char1, char2;

	while (i < NUM_ENTRIES) {
		off = i * (memfd_size / NUM_ENTRIES);
		for (j = 0; j < NUM_PAGES; j++, k++) {
			char1 = *((char *)addr1 + off + (j * getpagesize()));
			char2 = *((char *)addr2 + (k * getpagesize()));
			if (char1 != char2) {
				ret = -1;
				goto err;
			}
		}
		i++;
	}
err:
	munmap(addr1, memfd_size);
	munmap(addr2, NUM_ENTRIES * NUM_PAGES * getpagesize());
	return ret;
}

int main(int argc, char *argv[])
{
	struct udmabuf_create create;
	int devfd, memfd, buf, ret;
	off64_t size;
	void *addr1, *addr2;

	ksft_print_header();
	ksft_set_plan(6);

	devfd = open("/dev/udmabuf", O_RDWR);
	if (devfd < 0) {
		ksft_print_msg(
			"%s: [skip,no-udmabuf: Unable to access DMA buffer device file]\n",
			TEST_PREFIX);
		exit(KSFT_SKIP);
	}

	memfd = memfd_create("udmabuf-test", MFD_ALLOW_SEALING);
	if (memfd < 0) {
		ksft_print_msg("%s: [skip,no-memfd]\n", TEST_PREFIX);
		exit(KSFT_SKIP);
	}

	ret = fcntl(memfd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret < 0) {
		ksft_print_msg("%s: [skip,fcntl-add-seals]\n", TEST_PREFIX);
		exit(KSFT_SKIP);
	}

	size = getpagesize() * NUM_PAGES;
	ret = ftruncate(memfd, size);
	if (ret == -1) {
		ksft_print_msg("%s: [FAIL,memfd-truncate]\n", TEST_PREFIX);
		exit(KSFT_FAIL);
	}

	memset(&create, 0, sizeof(create));

	/* should fail (offset not page aligned) */
	create.memfd  = memfd;
	create.offset = getpagesize()/2;
	create.size   = getpagesize();
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0)
		ksft_test_result_fail("%s: [FAIL,test-1]\n", TEST_PREFIX);
	else
		ksft_test_result_pass("%s: [PASS,test-1]\n", TEST_PREFIX);

	/* should fail (size not multiple of page) */
	create.memfd  = memfd;
	create.offset = 0;
	create.size   = getpagesize()/2;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0)
		ksft_test_result_fail("%s: [FAIL,test-2]\n", TEST_PREFIX);
	else
		ksft_test_result_pass("%s: [PASS,test-2]\n", TEST_PREFIX);

	/* should fail (not memfd) */
	create.memfd  = 0; /* stdin */
	create.offset = 0;
	create.size   = size;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf >= 0)
		ksft_test_result_fail("%s: [FAIL,test-3]\n", TEST_PREFIX);
	else
		ksft_test_result_pass("%s: [PASS,test-3]\n", TEST_PREFIX);

	/* should work */
	page_size = getpagesize();
	addr1 = mmap_fd(memfd, size);
	write_to_memfd(addr1, size, 'a');
	create.memfd  = memfd;
	create.offset = 0;
	create.size   = size;
	buf = ioctl(devfd, UDMABUF_CREATE, &create);
	if (buf < 0)
		ksft_test_result_fail("%s: [FAIL,test-4]\n", TEST_PREFIX);
	else
		ksft_test_result_pass("%s: [PASS,test-4]\n", TEST_PREFIX);

	munmap(addr1, size);
	close(buf);
	close(memfd);

	/* should work (migration of 4k size pages)*/
	size = MEMFD_SIZE * page_size;
	memfd = create_memfd_with_seals(size, false);
	addr1 = mmap_fd(memfd, size);
	write_to_memfd(addr1, size, 'a');
	buf = create_udmabuf_list(devfd, memfd, size);
	addr2 = mmap_fd(buf, NUM_PAGES * NUM_ENTRIES * getpagesize());
	write_to_memfd(addr1, size, 'b');
	ret = compare_chunks(addr1, addr2, size);
	if (ret < 0)
		ksft_test_result_fail("%s: [FAIL,test-5]\n", TEST_PREFIX);
	else
		ksft_test_result_pass("%s: [PASS,test-5]\n", TEST_PREFIX);

	close(buf);
	close(memfd);

	/* should work (migration of 2MB size huge pages)*/
	page_size = getpagesize() * 512; /* 2 MB */
	size = MEMFD_SIZE * page_size;
	memfd = create_memfd_with_seals(size, true);
	addr1 = mmap_fd(memfd, size);
	write_to_memfd(addr1, size, 'a');
	buf = create_udmabuf_list(devfd, memfd, size);
	addr2 = mmap_fd(buf, NUM_PAGES * NUM_ENTRIES * getpagesize());
	write_to_memfd(addr1, size, 'b');
	ret = compare_chunks(addr1, addr2, size);
	if (ret < 0)
		ksft_test_result_fail("%s: [FAIL,test-6]\n", TEST_PREFIX);
	else
		ksft_test_result_pass("%s: [PASS,test-6]\n", TEST_PREFIX);

	close(buf);
	close(memfd);
	close(devfd);

	ksft_print_msg("%s: ok\n", TEST_PREFIX);
	ksft_print_cnts();

	return 0;
}
