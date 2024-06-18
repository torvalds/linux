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
#elif __aarch64__
/*
 * The default hugepage size for 64k base pagesize
 * is 512MB.
 */
#define PAGE_SIZE	(64 << 10)
#define HUGETLB_SIZE	(512 << 20)
#else
#define PAGE_SIZE	(4 << 10)
#define HUGETLB_SIZE	(2 << 20)
#endif

/*
 * The hint addr value is used to allocate addresses
 * beyond the high address switch boundary.
 */

#define ADDR_MARK_128TB	(1UL << 47)
#define ADDR_MARK_256TB	(1UL << 48)

#define HIGH_ADDR_128TB	((void *) (1UL << 48))
#define HIGH_ADDR_256TB	((void *) (1UL << 49))

#define LOW_ADDR	((void *) (1UL << 30))

#ifdef __aarch64__
#define ADDR_SWITCH_HINT ADDR_MARK_256TB
#define HIGH_ADDR	 HIGH_ADDR_256TB
#else
#define ADDR_SWITCH_HINT ADDR_MARK_128TB
#define HIGH_ADDR	 HIGH_ADDR_128TB
#endif

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
		 * Unless MAP_FIXED is specified, allocation based on hint
		 * addr is never at requested address or above it, which is
		 * beyond high address switch boundary in this case. Instead,
		 * a suitable allocation is found in lower address space.
		 */
		.addr = ((void *)(ADDR_SWITCH_HINT - PAGE_SIZE)),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(ADDR_SWITCH_HINT - PAGE_SIZE, (2 * PAGE_SIZE))",
		.low_addr_required = 1,
	},
	{
		/*
		 * Exact mapping at high address switch boundary, should
		 * be obtained even without MAP_FIXED as area is free.
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
	int i, ret = KSFT_PASS;

	for (i = 0; i < count; i++) {
		struct testcase *t = test + i;

		p = mmap(t->addr, t->size, PROT_READ | PROT_WRITE, t->flags, -1, 0);

		printf("%s: %p - ", t->msg, p);

		if (p == MAP_FAILED) {
			printf("FAILED\n");
			ret = KSFT_FAIL;
			continue;
		}

		if (t->low_addr_required && p >= (void *)(ADDR_SWITCH_HINT)) {
			printf("FAILED\n");
			ret = KSFT_FAIL;
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
#elif defined(__aarch64__)
	return getpagesize() == PAGE_SIZE;
#else
	return 0;
#endif
}

int main(int argc, char **argv)
{
	int ret;

	if (!supported_arch())
		return KSFT_SKIP;

	ret = run_test(testcases, ARRAY_SIZE(testcases));
	if (argc == 2 && !strcmp(argv[1], "--run-hugetlb"))
		ret = run_test(hugetlb_testcases, ARRAY_SIZE(hugetlb_testcases));
	return ret;
}
