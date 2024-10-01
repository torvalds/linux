// SPDX-License-Identifier: GPL-2.0
/*
 * Example of using hugepage memory in a user application using the mmap
 * system call with MAP_HUGETLB flag.  Before running this program make
 * sure the administrator has allocated enough default sized huge pages
 * to cover the 256 MB allocation.
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "vm_util.h"
#include "../kselftest.h"

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

static void read_bytes(char *addr, size_t length)
{
	unsigned long i;

	check_bytes(addr);
	for (i = 0; i < length; i++)
		if (*(addr + i) != (char)i)
			ksft_exit_fail_msg("Mismatch at %lu\n", i);

	ksft_test_result_pass("Read correct data\n");
}

int main(int argc, char **argv)
{
	void *addr;
	size_t hugepage_size;
	size_t length = LENGTH;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB;
	int shift = 0;

	hugepage_size = default_huge_page_size();
	/* munmap with fail if the length is not page aligned */
	if (hugepage_size > length)
		length = hugepage_size;

	ksft_print_header();
	ksft_set_plan(1);

	if (argc > 1)
		length = atol(argv[1]) << 20;
	if (argc > 2) {
		shift = atoi(argv[2]);
		if (shift)
			flags |= (shift & MAP_HUGE_MASK) << MAP_HUGE_SHIFT;
	}

	if (shift)
		ksft_print_msg("%u kB hugepages\n", 1 << (shift - 10));
	else
		ksft_print_msg("Default size hugepages\n");
	ksft_print_msg("Mapping %lu Mbytes\n", (unsigned long)length >> 20);

	addr = mmap(NULL, length, PROTECTION, flags, -1, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("mmap: %s\n", strerror(errno));

	ksft_print_msg("Returned address is %p\n", addr);
	check_bytes(addr);
	write_bytes(addr, length);
	read_bytes(addr, length);

	/* munmap() length of MAP_HUGETLB memory must be hugepage aligned */
	if (munmap(addr, length))
		ksft_exit_fail_msg("munmap: %s\n", strerror(errno));

	ksft_finished();
}
