// SPDX-License-Identifier: GPL-2.0+
//
// Copyright 2019, Michael Ellerman, IBM Corp.
//
// Test that allocating memory beyond the memory limit and then forking is
// handled correctly, ie. the child is able to access the mappings beyond the
// memory limit and the child's writes are not visible to the parent.

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "utils.h"


#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE	MAP_FIXED	// "Should be safe" above 512TB
#endif


static int test(void)
{
	int p2c[2], c2p[2], rc, status, c, *p;
	unsigned long page_size;
	pid_t pid;

	page_size = sysconf(_SC_PAGESIZE);
	SKIP_IF(page_size != 65536);

	// Create a mapping at 512TB to allocate an extended_id
	p = mmap((void *)(512ul << 40), page_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		printf("Error: couldn't mmap(), confirm kernel has 4TB support?\n");
		return 1;
	}

	printf("parent writing %p = 1\n", p);
	*p = 1;

	FAIL_IF(pipe(p2c) == -1 || pipe(c2p) == -1);

	pid = fork();
	if (pid == 0) {
		FAIL_IF(read(p2c[0], &c, 1) != 1);

		pid = getpid();
		printf("child writing  %p = %d\n", p, pid);
		*p = pid;

		FAIL_IF(write(c2p[1], &c, 1) != 1);
		FAIL_IF(read(p2c[0], &c, 1) != 1);
		exit(0);
	}

	c = 0;
	FAIL_IF(write(p2c[1], &c, 1) != 1);
	FAIL_IF(read(c2p[0], &c, 1) != 1);

	// Prevent compiler optimisation
	barrier();

	rc = 0;
	printf("parent reading %p = %d\n", p, *p);
	if (*p != 1) {
		printf("Error: BUG! parent saw child's write! *p = %d\n", *p);
		rc = 1;
	}

	FAIL_IF(write(p2c[1], &c, 1) != 1);
	FAIL_IF(waitpid(pid, &status, 0) == -1);
	FAIL_IF(!WIFEXITED(status) || WEXITSTATUS(status));

	if (rc == 0)
		printf("success: test completed OK\n");

	return rc;
}

int main(void)
{
	return test_harness(test, "large_vm_fork_separation");
}
