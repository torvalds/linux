// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "utils.h"

/* This must match the huge page & THP size */
#define SIZE	(16 * 1024 * 1024)

static int test_body(void)
{
	void *addr;
	char *p;

	addr = (void *)0xa0000000;

	p = mmap(addr, SIZE, PROT_READ | PROT_WRITE,
		 MAP_HUGETLB | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p != MAP_FAILED) {
		/*
		 * Typically the mmap will fail because no huge pages are
		 * allocated on the system. But if there are huge pages
		 * allocated the mmap will succeed. That's fine too, we just
		 * munmap here before continuing.  munmap() length of
		 * MAP_HUGETLB memory must be hugepage aligned.
		 */
		if (munmap(addr, SIZE)) {
			perror("munmap");
			return 1;
		}
	}

	p = mmap(addr, SIZE, PROT_READ | PROT_WRITE,
		 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (p == MAP_FAILED) {
		printf("Mapping failed @ %p\n", addr);
		perror("mmap");
		return 1;
	}

	/*
	 * Either a user or kernel access is sufficient to trigger the bug.
	 * A kernel access is easier to spot & debug, as it will trigger the
	 * softlockup or RCU stall detectors, and when the system is kicked
	 * into xmon we get a backtrace in the kernel.
	 *
	 * A good option is:
	 *  getcwd(p, SIZE);
	 *
	 * For the purposes of this testcase it's preferable to spin in
	 * userspace, so the harness can kill us if we get stuck. That way we
	 * see a test failure rather than a dead system.
	 */
	*p = 0xf;

	munmap(addr, SIZE);

	return 0;
}

static int test_main(void)
{
	int i;

	/* 10,000 because it's a "bunch", and completes reasonably quickly */
	for (i = 0; i < 10000; i++)
		if (test_body())
			return 1;

	return 0;
}

int main(void)
{
	return test_harness(test_main, "hugetlb_vs_thp");
}
