// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Authors: Kirill A. Shutemov <kirill.shutemov@linux.intel.com>
 * Authors: Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 */

#include <stdio.h>
#include <sys/mman.h>
#include <string.h>

#include "../kselftest.h"

#ifdef __powerpc64__
#define PAGE_SIZE	(64 << 10)
/*
 * This will work with 16M and 2M hugepage size
 */
#define HUGETLB_SIZE	(16 << 20)
#else
#define PAGE_SIZE	(4 << 10)
#define HUGETLB_SIZE	(2 << 20)
#endif

/*
 * >= 128TB is the hint addr value we used to select
 * large address space.
 */
#define ADDR_SWITCH_HINT (1UL << 47)
#define LOW_ADDR	((void *) (1UL << 30))
#define HIGH_ADDR	((void *) (1UL << 48))

struct testcase {
	void *addr;
	unsigned long size;
	unsigned long flags;
	const char *msg;
	unsigned int low_addr_required:1;
	unsigned int keep_mapped:1;
};

static struct testcase testcases[] = {
	{
		/*
		 * If stack is moved, we could possibly allocate
		 * this at the requested address.
		 */
		.addr = ((void *)(ADDR_SWITCH_HINT - PAGE_SIZE)),
		.size = PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT - PAGE_SIZE, PAGE_SIZE)",
		.low_addr_required = 1,
	},
	{
		/*
		 * We should never allocate at the requested address or above it
		 * The len cross the 128TB boundary. Without MAP_FIXED
		 * we will always search in the lower address space.
		 */
		.addr = ((void *)(ADDR_SWITCH_HINT - PAGE_SIZE)),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT - PAGE_SIZE, (2 * PAGE_SIZE))",
		.low_addr_required = 1,
	},
	{
		/*
		 * Exact mapping at 128TB, the area is free we should get that
		 * even without MAP_FIXED.
		 */
		.addr = ((void *)(ADDR_SWITCH_HINT)),
		.size = PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT, PAGE_SIZE)",
		.keep_mapped = 1,
	},
	{
		.addr = (void *)(ADDR_SWITCH_HINT),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		.msg = "mmap(ADDR_SWITCH_HINT, 2 * PAGE_SIZE, MAP_FIXED)",
	},
	{
		.addr = NULL,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(NULL)",
		.low_addr_required = 1,
	},
	{
		.addr = LOW_ADDR,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(LOW_ADDR)",
		.low_addr_required = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(HIGH_ADDR)",
		.keep_mapped = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(HIGH_ADDR) again",
		.keep_mapped = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		.msg = "mmap(HIGH_ADDR, MAP_FIXED)",
	},
	{
		.addr = (void *) -1,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(-1)",
		.keep_mapped = 1,
	},
	{
		.addr = (void *) -1,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(-1) again",
	},
	{
		.addr = ((void *)(ADDR_SWITCH_HINT - PAGE_SIZE)),
		.size = PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT - PAGE_SIZE, PAGE_SIZE)",
		.low_addr_required = 1,
	},
	{
		.addr = (void *)(ADDR_SWITCH_HINT - PAGE_SIZE),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT - PAGE_SIZE, 2 * PAGE_SIZE)",
		.low_addr_required = 1,
		.keep_mapped = 1,
	},
	{
		.addr = (void *)(ADDR_SWITCH_HINT - PAGE_SIZE / 2),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT - PAGE_SIZE/2 , 2 * PAGE_SIZE)",
		.low_addr_required = 1,
		.keep_mapped = 1,
	},
	{
		.addr = ((void *)(ADDR_SWITCH_HINT)),
		.size = PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT, PAGE_SIZE)",
	},
	{
		.addr = (void *)(ADDR_SWITCH_HINT),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		.msg = "mmap(ADDR_SWITCH_HINT, 2 * PAGE_SIZE, MAP_FIXED)",
	},
};

static struct testcase hugetlb_testcases[] = {
	{
		.addr = NULL,
		.size = HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(NULL, MAP_HUGETLB)",
		.low_addr_required = 1,
	},
	{
		.addr = LOW_ADDR,
		.size = HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(LOW_ADDR, MAP_HUGETLB)",
		.low_addr_required = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(HIGH_ADDR, MAP_HUGETLB)",
		.keep_mapped = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(HIGH_ADDR, MAP_HUGETLB) again",
		.keep_mapped = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		.msg = "mmap(HIGH_ADDR, MAP_FIXED | MAP_HUGETLB)",
	},
	{
		.addr = (void *) -1,
		.size = HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(-1, MAP_HUGETLB)",
		.keep_mapped = 1,
	},
	{
		.addr = (void *) -1,
		.size = HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(-1, MAP_HUGETLB) again",
	},
	{
		.addr = (void *)(ADDR_SWITCH_HINT - PAGE_SIZE),
		.size = 2 * HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT - PAGE_SIZE, 2*HUGETLB_SIZE, MAP_HUGETLB)",
		.low_addr_required = 1,
		.keep_mapped = 1,
	},
	{
		.addr = (void *)(ADDR_SWITCH_HINT),
		.size = 2 * HUGETLB_SIZE,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		.msg = "mmap(ADDR_SWITCH_HINT , 2*HUGETLB_SIZE, MAP_FIXED | MAP_HUGETLB)",
	},
};

static int run_test(struct testcase *test, int count)
{
	void *p;
	int i, ret = 0;

	for (i = 0; i < count; i++) {
		struct testcase *t = test + i;

		p = mmap(t->addr, t->size, PROT_READ | PROT_WRITE, t->flags, -1, 0);

		printf("%s: %p - ", t->msg, p);

		if (p == MAP_FAILED) {
			printf("FAILED\n");
			ret = 1;
			continue;
		}

		if (t->low_addr_required && p >= (void *)(ADDR_SWITCH_HINT)) {
			printf("FAILED\n");
			ret = 1;
		} else {
			/*
			 * Do a dereference of the address returned so that we catch
			 * bugs in page fault handling
			 */
			memset(p, 0, t->size);
			printf("OK\n");
		}
		if (!t->keep_mapped)
			munmap(p, t->size);
	}

	return ret;
}

static int supported_arch(void)
{
#if defined(__powerpc64__)
	return 1;
#elif defined(__x86_64__)
	return 1;
#else
	return 0;
#endif
}

int main(int argc, char **argv)
{
	int ret;

	if (!supported_arch())
		return 0;

	ret = run_test(testcases, ARRAY_SIZE(testcases));
	if (argc == 2 && !strcmp(argv[1], "--run-hugetlb"))
		ret = run_test(hugetlb_testcases, ARRAY_SIZE(hugetlb_testcases));
	return ret;
}
