// SPDX-License-Identifier: GPL-2.0
/*
 * hugetlb-mmap:
 *
 * Example of using huge page memory in a user application using the mmap
 * system call.  Before running this application, make sure that the
 * administrator has mounted the hugetlbfs filesystem (on some directory
 * like /mnt) using the command mount -t hugetlbfs nodev /mnt. In this
 * example, the app is requesting memory of size 256MB that is backed by
 * huge pages.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include "vm_util.h"
#include "kselftest.h"
#include "hugepage_settings.h"

#define LENGTH (256UL*1024*1024)
#define PROTECTION (PROT_READ | PROT_WRITE)

static void check_bytes(char *addr)
{
	ksft_print_msg("First hex is %x\n", *((unsigned int *)addr));
}

static void write_bytes(char *addr, size_t length)
{
	unsigned long i;

	for (i = 0; i < length; i++)
		*(addr + i) = (char)i;
}

static bool verify_bytes(char *addr, size_t length)
{
	unsigned long i;

	check_bytes(addr);
	for (i = 0; i < length; i++)
		if (*(addr + i) != (char)i) {
			ksft_print_msg("Error: Mismatch at %lu(%p)\n", i, addr);
			return false;
		}

	return true;
}

static void test_mmap(size_t length, int mmap_flags, int fd,
		      const char *test_name)
{
	bool passed = true;
	void *addr;

	addr = mmap(NULL, length, PROTECTION, mmap_flags, fd, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_perror("mmap");

	ksft_print_msg("Returned address is %p\n", addr);
	check_bytes(addr);
	write_bytes(addr, length);
	if (!verify_bytes(addr, length))
		passed = false;

	/* munmap() length of MAP_HUGETLB memory must be hugepage aligned */
	if (munmap(addr, length))
		ksft_exit_fail_perror("munmap");

	ksft_test_result(passed, "%s\n", test_name);
}

static void test_anon_mmap(size_t length, int shift)
{
	const char *test_name = "hugetlb anonymous mmap";
	int mmap_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB;

	if (shift)
		mmap_flags |= (shift & MAP_HUGE_MASK) << MAP_HUGE_SHIFT;

	test_mmap(length, mmap_flags, -1, test_name);
}

static void test_file_mmap(size_t length, int shift)
{
	const char *test_name = "hugetlb file mmap";
	int mfd_flags = MFD_HUGETLB;
	int fd;

	if (shift)
		mfd_flags |= (shift & MFD_HUGE_MASK) << MFD_HUGE_SHIFT;

	fd = memfd_create("hugetlb-mmap", mfd_flags);
	if (fd < 0)
		ksft_exit_fail_perror("memfd_create");

	test_mmap(length, MAP_SHARED, fd, test_name);
	close(fd);
}

int main(int argc, char **argv)
{
	size_t hugepage_size;
	size_t length = LENGTH;
	int shift = 0, nr;

	ksft_print_header();

	if (argc > 1)
		length = atol(argv[1]) << 20;
	if (argc > 2)
		shift = atoi(argv[2]);

	hugetlb_save_settings();
	if (shift) {
		hugepage_size = (1UL << shift);
		ksft_print_msg("%lu kB hugepages\n", 1UL << (shift - 10));
	} else {
		hugepage_size = default_huge_page_size();
		if (!hugepage_size)
			ksft_exit_skip("Could not detect default hugetlb page size.");
		ksft_print_msg("Default size hugepages (%lu kB)\n", hugepage_size >> 10);
	}

	/* munmap will fail if the length is not page aligned */
	length = (length + hugepage_size - 1) & ~(hugepage_size - 1);
	nr = length / hugepage_size;

	hugetlb_set_nr_pages(hugepage_size, nr);
	if (hugetlb_free_pages(hugepage_size) < nr)
		ksft_exit_skip("Not enough %lu Kb pages\n", hugepage_size >> 10);

	ksft_set_plan(2);
	ksft_print_msg("Mapping %lu Mbytes\n", (unsigned long)length >> 20);

	test_anon_mmap(length, shift);
	test_file_mmap(length, shift);

	ksft_finished();
}
