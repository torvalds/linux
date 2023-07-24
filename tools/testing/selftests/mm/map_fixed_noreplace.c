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
	if (addr == MAP_FAILED) {
		printf("Error: couldn't map the space we need for the test\n");
		return 0;
	}

	if (munmap(addr, size) != 0) {
		printf("Error: couldn't map the space we need for the test\n");
		return 0;
	}
	return (unsigned long)addr;
}

int main(void)
{
	unsigned long base_addr;
	unsigned long flags, addr, size, page_size;
	char *p;

	page_size = sysconf(_SC_PAGE_SIZE);

	//let's find a base addr that is free before we start the tests
	size = 5 * page_size;
	base_addr = find_base_addr(size);
	if (!base_addr) {
		printf("Error: couldn't map the space we need for the test\n");
		return 1;
	}

	flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE;

	// Check we can map all the areas we need below
	errno = 0;
	addr = base_addr;
	size = 5 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);

	printf("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	if (p == MAP_FAILED) {
		dump_maps();
		printf("Error: couldn't map the space we need for the test\n");
		return 1;
	}

	errno = 0;
	if (munmap((void *)addr, 5 * page_size) != 0) {
		dump_maps();
		printf("Error: munmap failed!?\n");
		return 1;
	}
	printf("unmap() successful\n");

	errno = 0;
	addr = base_addr + page_size;
	size = 3 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	printf("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	if (p == MAP_FAILED) {
		dump_maps();
		printf("Error: first mmap() failed unexpectedly\n");
		return 1;
	}

	/*
	 * Exact same mapping again:
	 *   base |  free  | new
	 *     +1 | mapped | new
	 *     +2 | mapped | new
	 *     +3 | mapped | new
	 *     +4 |  free  | new
	 */
	errno = 0;
	addr = base_addr;
	size = 5 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	printf("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	if (p != MAP_FAILED) {
		dump_maps();
		printf("Error:1: mmap() succeeded when it shouldn't have\n");
		return 1;
	}

	/*
	 * Second mapping contained within first:
	 *
	 *   base |  free  |
	 *     +1 | mapped |
	 *     +2 | mapped | new
	 *     +3 | mapped |
	 *     +4 |  free  |
	 */
	errno = 0;
	addr = base_addr + (2 * page_size);
	size = page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	printf("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	if (p != MAP_FAILED) {
		dump_maps();
		printf("Error:2: mmap() succeeded when it shouldn't have\n");
		return 1;
	}

	/*
	 * Overlap end of existing mapping:
	 *   base |  free  |
	 *     +1 | mapped |
	 *     +2 | mapped |
	 *     +3 | mapped | new
	 *     +4 |  free  | new
	 */
	errno = 0;
	addr = base_addr + (3 * page_size);
	size = 2 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	printf("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	if (p != MAP_FAILED) {
		dump_maps();
		printf("Error:3: mmap() succeeded when it shouldn't have\n");
		return 1;
	}

	/*
	 * Overlap start of existing mapping:
	 *   base |  free  | new
	 *     +1 | mapped | new
	 *     +2 | mapped |
	 *     +3 | mapped |
	 *     +4 |  free  |
	 */
	errno = 0;
	addr = base_addr;
	size = 2 * page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	printf("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	if (p != MAP_FAILED) {
		dump_maps();
		printf("Error:4: mmap() succeeded when it shouldn't have\n");
		return 1;
	}

	/*
	 * Adjacent to start of existing mapping:
	 *   base |  free  | new
	 *     +1 | mapped |
	 *     +2 | mapped |
	 *     +3 | mapped |
	 *     +4 |  free  |
	 */
	errno = 0;
	addr = base_addr;
	size = page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	printf("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	if (p == MAP_FAILED) {
		dump_maps();
		printf("Error:5: mmap() failed when it shouldn't have\n");
		return 1;
	}

	/*
	 * Adjacent to end of existing mapping:
	 *   base |  free  |
	 *     +1 | mapped |
	 *     +2 | mapped |
	 *     +3 | mapped |
	 *     +4 |  free  |  new
	 */
	errno = 0;
	addr = base_addr + (4 * page_size);
	size = page_size;
	p = mmap((void *)addr, size, PROT_NONE, flags, -1, 0);
	printf("mmap() @ 0x%lx-0x%lx p=%p result=%m\n", addr, addr + size, p);

	if (p == MAP_FAILED) {
		dump_maps();
		printf("Error:6: mmap() failed when it shouldn't have\n");
		return 1;
	}

	addr = base_addr;
	size = 5 * page_size;
	if (munmap((void *)addr, size) != 0) {
		dump_maps();
		printf("Error: munmap failed!?\n");
		return 1;
	}
	printf("unmap() successful\n");

	printf("OK\n");
	return 0;
}
