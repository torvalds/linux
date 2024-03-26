// SPDX-License-Identifier: GPL-2.0

/*
 * Test that MAP_FIXED_NOREPLACE works.
 *
 * Copyright 2018, Jann Horn <jannh@google.com>
 * Copyright 2018, Michael Ellerman, IBM Corporation.
 */

#include <sys/mman.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../kselftest.h"

static void dump_maps(void)
{
	char cmd[32];

	snprintf(cmd, sizeof(cmd), "cat /proc/%d/maps", getpid());
	system(cmd);
}

static unsigned long find_base_addr(unsigned long size)
{
	void *addr;
	unsigned long flags;

	flags = MAP_PRIVATE | MAP_ANONYMOUS;
	addr = mmap(NULL, size, PROT_NONE, flags, -1, 0);
	if (addr == MAP_FAILED)
		ksft_exit_fail_msg("Error: couldn't map the space we need for the test\n");

	if (munmap(addr, size) != 0)
		ksft_exit_fail_msg("Error: munmap failed\n");

	return (unsigned long)addr;
}

int main(void)
{
	unsigned long base_addr;
	unsigned long flags, addr, size, page_size;
	char *p;

	ksft_print_header();
	ksft_set_plan(9);

	page_size = sysconf(_SC_PAGE_SIZE);

	/* let's find a base addr that is free before we start the tests */
	size = 5 * page_size;
	base_addr = find_base_addr(size);

	flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;

	/* Check we can map all the areas we need below */
	addr = base_addr;
	size = 5 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	if (p == MAP_FAILED) {
		dump_maps();
		ksft_exit_fail_msg("Error: couldn't map the space we need for the test\n");
	}
	if (munmap((void *)addr, 5 * page_size) != 0) {
		dump_maps();
		ksft_exit_fail_msg("Error: munmap failed!?\n");
	}
	ksft_test_result_pass("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	addr = base_addr + page_size;
	size = 3 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	if (p == MAP_FAILED) {
		dump_maps();
		ksft_exit_fail_msg("Error: first mmap() failed unexpectedly\n");
	}
	ksft_test_result_pass("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	/*
	 * Exact same mapping again:
	 *   base |  free  | new
	 *     +1 | mapped | new
	 *     +2 | mapped | new
	 *     +3 | mapped | new
	 *     +4 |  free  | new
	 */
	addr = base_addr;
	size = 5 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	if (p != MAP_FAILED) {
		dump_maps();
		ksft_exit_fail_msg("Error:1: mmap() succeeded when it shouldn't have\n");
	}
	ksft_test_result_pass("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	/*
	 * Second mapping contained within first:
	 *
	 *   base |  free  |
	 *     +1 | mapped |
	 *     +2 | mapped | new
	 *     +3 | mapped |
	 *     +4 |  free  |
	 */
	addr = base_addr + (2 * page_size);
	size = page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	if (p != MAP_FAILED) {
		dump_maps();
		ksft_exit_fail_msg("Error:2: mmap() succeeded when it shouldn't have\n");
	}
	ksft_test_result_pass("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	/*
	 * Overlap end of existing mapping:
	 *   base |  free  |
	 *     +1 | mapped |
	 *     +2 | mapped |
	 *     +3 | mapped | new
	 *     +4 |  free  | new
	 */
	addr = base_addr + (3 * page_size);
	size = 2 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	if (p != MAP_FAILED) {
		dump_maps();
		ksft_exit_fail_msg("Error:3: mmap() succeeded when it shouldn't have\n");
	}
	ksft_test_result_pass("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	/*
	 * Overlap start of existing mapping:
	 *   base |  free  | new
	 *     +1 | mapped | new
	 *     +2 | mapped |
	 *     +3 | mapped |
	 *     +4 |  free  |
	 */
	addr = base_addr;
	size = 2 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	if (p != MAP_FAILED) {
		dump_maps();
		ksft_exit_fail_msg("Error:4: mmap() succeeded when it shouldn't have\n");
	}
	ksft_test_result_pass("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	/*
	 * Adjacent to start of existing mapping:
	 *   base |  free  | new
	 *     +1 | mapped |
	 *     +2 | mapped |
	 *     +3 | mapped |
	 *     +4 |  free  |
	 */
	addr = base_addr;
	size = page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	if (p == MAP_FAILED) {
		dump_maps();
		ksft_exit_fail_msg("Error:5: mmap() failed when it shouldn't have\n");
	}
	ksft_test_result_pass("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	/*
	 * Adjacent to end of existing mapping:
	 *   base |  free  |
	 *     +1 | mapped |
	 *     +2 | mapped |
	 *     +3 | mapped |
	 *     +4 |  free  |  new
	 */
	addr = base_addr + (4 * page_size);
	size = page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	if (p == MAP_FAILED) {
		dump_maps();
		ksft_exit_fail_msg("Error:6: mmap() failed when it shouldn't have\n");
	}
	ksft_test_result_pass("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	addr = base_addr;
	size = 5 * page_size;
	if (munmap((void *)addr, size) != 0) {
		dump_maps();
		ksft_exit_fail_msg("Error: munmap failed!?\n");
	}
	ksft_test_result_pass("Base Address unmap() successful\n");

	ksft_finished();
}
