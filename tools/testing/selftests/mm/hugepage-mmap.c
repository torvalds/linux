// SPDX-License-Identifier: GPL-2.0
/*
 * hugepage-mmap:
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
#include "../kselftest.h"

#define LENGTH (256UL*1024*1024)
#define PROTECTION (PROT_READ | PROT_WRITE)

static void check_bytes(char *addr)
{
	ksft_print_msg("First hex is %x\n", *((unsigned int *)addr));
}

static void write_bytes(char *addr)
{
	unsigned long i;

	for (i = 0; i < LENGTH; i++)
		*(addr + i) = (char)i;
}

static int read_bytes(char *addr)
{
	unsigned long i;

	check_bytes(addr);
	for (i = 0; i < LENGTH; i++)
		if (*(addr + i) != (char)i) {
			ksft_print_msg("Error: Mismatch at %lu\n", i);
			return 1;
		}
	return 0;
}

int main(void)
{
	void *addr;
	int fd, ret;

	ksft_print_header();
	ksft_set_plan(1);

	fd = memfd_create("hugepage-mmap", MFD_HUGETLB);
	if (fd < 0)
		ksft_exit_fail_msg("memfd_create() failed: %s\n", strerror(errno));

	addr = mmap(NULL, LENGTH, PROTECTION, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED) {
		close(fd);
		ksft_exit_fail_msg("mmap(): %s\n", strerror(errno));
	}

	ksft_print_msg("Returned address is %p\n", addr);
	check_bytes(addr);
	write_bytes(addr);
	ret = read_bytes(addr);

	munmap(addr, LENGTH);
	close(fd);

	ksft_test_result(!ret, "Read same data\n");

	ksft_exit(!ret);
}
