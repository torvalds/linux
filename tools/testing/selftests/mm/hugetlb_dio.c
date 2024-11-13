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
#include "vm_util.h"
#include "../kselftest.h"

void run_dio_using_hugetlb(unsigned int start_off, unsigned int end_off)
{
	int fd;
	char *buffer =  NULL;
	char *orig_buffer = NULL;
	size_t h_pagesize = 0;
	size_t writesize;
	int free_hpage_b = 0;
	int free_hpage_a = 0;
	const int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB;
	const int mmap_prot  = PROT_READ | PROT_WRITE;

	writesize = end_off - start_off;

	/* Get the default huge page size */
	h_pagesize = default_huge_page_size();
	if (!h_pagesize)
		ksft_exit_fail_msg("Unable to determine huge page size\n");

	/* Open the file to DIO */
	fd = open("/tmp", O_TMPFILE | O_RDWR | O_DIRECT, 0664);
	if (fd < 0)
		ksft_exit_fail_perror("Error opening file\n");

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
	close(fd);

	/* Get the free huge pages after unmap*/
	free_hpage_a = get_free_hugepages();

	/*
	 * If the no. of free hugepages before allocation and after unmap does
	 * not match - that means there could still be a page which is pinned.
	 */
	if (free_hpage_a != free_hpage_b) {
		ksft_print_msg("No. Free pages before allocation : %d\n", free_hpage_b);
		ksft_print_msg("No. Free pages after munmap : %d\n", free_hpage_a);
		ksft_test_result_fail(": Huge pages not freed!\n");
	} else {
		ksft_print_msg("No. Free pages before allocation : %d\n", free_hpage_b);
		ksft_print_msg("No. Free pages after munmap : %d\n", free_hpage_a);
		ksft_test_result_pass(": Huge pages freed successfully !\n");
	}
}

int main(void)
{
	size_t pagesize = 0;
	int fd;

	ksft_print_header();

	/* Open the file to DIO */
	fd = open("/tmp", O_TMPFILE | O_RDWR | O_DIRECT, 0664);
	if (fd < 0)
		ksft_exit_skip("Unable to allocate file: %s\n", strerror(errno));
	close(fd);

	/* Check if huge pages are free */
	if (!get_free_hugepages())
		ksft_exit_skip("No free hugepage, exiting\n");

	ksft_set_plan(4);

	/* Get base page size */
	pagesize  = psize();

	/* start and end is aligned to pagesize */
	run_dio_using_hugetlb(0, (pagesize * 3));

	/* start is aligned but end is not aligned */
	run_dio_using_hugetlb(0, (pagesize * 3) - (pagesize / 2));

	/* start is unaligned and end is aligned */
	run_dio_using_hugetlb(pagesize / 2, (pagesize * 3));

	/* both start and end are unaligned */
	run_dio_using_hugetlb(pagesize / 2, (pagesize * 3) + (pagesize / 2));

	ksft_finished();
}
