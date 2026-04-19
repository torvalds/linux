// SPDX-License-Identifier: GPL-2.0
/*
 * This program tests for hugepage leaks after DIO writes to a file using a
 * hugepage as the user buffer. During DIO, the user buffer is pinned and
 * should be properly unpinned upon completion. This patch verifies that the
 * kernel correctly unpins the buffer at DIO completion for both aligned and
 * unaligned user buffer offsets (w.r.t page boundary), ensuring the hugepage
 * is freed upon unmapping.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include "vm_util.h"
#include "kselftest.h"

#ifndef STATX_DIOALIGN
#define STATX_DIOALIGN		0x00002000U
#endif

static int get_dio_alignment(int fd)
{
	struct statx stx;
	int ret;

	ret = syscall(__NR_statx, fd, "", AT_EMPTY_PATH, STATX_DIOALIGN, &stx);
	if (ret < 0)
		return -1;

	/*
	 * If STATX_DIOALIGN is unsupported, assume no alignment
	 * constraint and let the test proceed.
	 */
	if (!(stx.stx_mask & STATX_DIOALIGN) || !stx.stx_dio_offset_align)
		return 1;

	return stx.stx_dio_offset_align;
}

static bool check_dio_alignment(unsigned int start_off,
				unsigned int end_off, unsigned int align)
{
	unsigned int writesize = end_off - start_off;

	/*
	 * The kernel's DIO path checks that file offset, length, and
	 * buffer address are all multiples of dio_offset_align.  When
	 * this test case's parameters don't satisfy that, the write
	 * would fail with -EINVAL before exercising the hugetlb unpin
	 * path, so skip.
	 */
	if (start_off % align != 0 || writesize % align != 0) {
		ksft_test_result_skip("DIO align=%u incompatible with offset %u writesize %u\n",
				align, start_off, writesize);
		return false;
	}

	return true;
}

static void run_dio_using_hugetlb(int fd, unsigned int start_off,
				unsigned int end_off, unsigned int align)
{
	char *buffer =  NULL;
	char *orig_buffer = NULL;
	size_t h_pagesize = 0;
	size_t writesize;
	int free_hpage_b = 0;
	int free_hpage_a = 0;
	const int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB;
	const int mmap_prot  = PROT_READ | PROT_WRITE;

	if (!check_dio_alignment(start_off, end_off, align))
		return;

	writesize = end_off - start_off;

	/* Get the default huge page size */
	h_pagesize = default_huge_page_size();
	if (!h_pagesize)
		ksft_exit_fail_msg("Unable to determine huge page size\n");

	/* Reset file position since fd is shared across tests */
	if (lseek(fd, 0, SEEK_SET) < 0)
		ksft_exit_fail_perror("lseek failed\n");

	/* Get the free huge pages before allocation */
	free_hpage_b = get_free_hugepages();
	if (free_hpage_b == 0) {
		close(fd);
		ksft_exit_skip("No free hugepage, exiting!\n");
	}

	/* Allocate a hugetlb page */
	orig_buffer = mmap(NULL, h_pagesize, mmap_prot, mmap_flags, -1, 0);
	if (orig_buffer == MAP_FAILED) {
		close(fd);
		ksft_exit_fail_perror("Error mapping memory\n");
	}
	buffer = orig_buffer;
	buffer += start_off;

	memset(buffer, 'A', writesize);

	/* Write the buffer to the file */
	if (write(fd, buffer, writesize) != (writesize)) {
		munmap(orig_buffer, h_pagesize);
		close(fd);
		ksft_exit_fail_perror("Error writing to file\n");
	}

	/* unmap the huge page */
	munmap(orig_buffer, h_pagesize);

	/* Get the free huge pages after unmap*/
	free_hpage_a = get_free_hugepages();

	ksft_print_msg("No. Free pages before allocation : %d\n", free_hpage_b);
	ksft_print_msg("No. Free pages after munmap : %d\n", free_hpage_a);

	/*
	 * If the no. of free hugepages before allocation and after unmap does
	 * not match - that means there could still be a page which is pinned.
	 */
	ksft_test_result(free_hpage_a == free_hpage_b,
			 "free huge pages from %u-%u\n", start_off, end_off);
}

int main(void)
{
	int fd, align;
	const size_t pagesize = psize();

	ksft_print_header();

	/* Check if huge pages are free */
	if (!get_free_hugepages())
		ksft_exit_skip("No free hugepage, exiting\n");

	fd = open("/tmp", O_TMPFILE | O_RDWR | O_DIRECT, 0664);
	if (fd < 0)
		ksft_exit_skip("Unable to allocate file: %s\n", strerror(errno));

	align = get_dio_alignment(fd);
	if (align < 0)
		ksft_exit_skip("Unable to obtain DIO alignment: %s\n",
				strerror(errno));
	ksft_set_plan(4);

	/* start and end is aligned to pagesize */
	run_dio_using_hugetlb(fd, 0, (pagesize * 3), align);

	/* start is aligned but end is not aligned */
	run_dio_using_hugetlb(fd, 0, (pagesize * 3) - (pagesize / 2), align);

	/* start is unaligned and end is aligned */
	run_dio_using_hugetlb(fd, pagesize / 2, (pagesize * 3), align);

	/* both start and end are unaligned */
	run_dio_using_hugetlb(fd, pagesize / 2, (pagesize * 3) + (pagesize / 2), align);

	close(fd);

	ksft_finished();
}
