// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Authors: Kirill A. Shutemov <kirill.shutemov@linux.intel.com>
 * Authors: Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 */

#include <stdio.h>
#include <sys/mman.h>
#include <string.h>

#include "vm_util.h"
#include "../kselftest.h"

/*
 * The hint addr value is used to allocate addresses
 * beyond the high address switch boundary.
 */

#define ADDR_MARK_128TB	(1UL << 47)
#define ADDR_MARK_256TB	(1UL << 48)

#define HIGH_ADDR_128TB	(1UL << 48)
#define HIGH_ADDR_256TB	(1UL << 49)

struct testcase {
	void *addr;
	unsigned long size;
	unsigned long flags;
	const char *msg;
	unsigned int low_addr_required:1;
	unsigned int keep_mapped:1;
};

static struct testcase *testcases;
static struct testcase *hugetlb_testcases;
static int sz_testcases, sz_hugetlb_testcases;
static unsigned long switch_hint;

/* Initialize testcases inside a function to compute parameters at runtime */
void testcases_init(void)
{
	unsigned long pagesize = getpagesize();
	unsigned long hugepagesize = default_huge_page_size();
	unsigned long low_addr = (1UL << 30);
	unsigned long addr_switch_hint = ADDR_MARK_128TB;
	unsigned long high_addr = HIGH_ADDR_128TB;

#ifdef __aarch64__

	/* Post LPA2, the lower userspace VA on a 16K pagesize is 47 bits. */
	if (pagesize != (16UL << 10)) {
		addr_switch_hint = ADDR_MARK_256TB;
		high_addr = HIGH_ADDR_256TB;
	}
#endif

	struct testcase t[] = {
		{
			/*
			 * If stack is moved, we could possibly allocate
			 * this at the requested address.
			 */
			.addr = ((void *)(addr_switch_hint - pagesize)),
			.size = pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(addr_switch_hint - pagesize, pagesize)",
			.low_addr_required = 1,
		},
		{
			/*
			 * Unless MAP_FIXED is specified, allocation based on hint
			 * addr is never at requested address or above it, which is
			 * beyond high address switch boundary in this case. Instead,
			 * a suitable allocation is found in lower address space.
			 */
			.addr = ((void *)(addr_switch_hint - pagesize)),
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(addr_switch_hint - pagesize, (2 * pagesize))",
			.low_addr_required = 1,
		},
		{
			/*
			 * Exact mapping at high address switch boundary, should
			 * be obtained even without MAP_FIXED as area is free.
			 */
			.addr = ((void *)(addr_switch_hint)),
			.size = pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(addr_switch_hint, pagesize)",
			.keep_mapped = 1,
		},
		{
			.addr = (void *)(addr_switch_hint),
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			.msg = "mmap(addr_switch_hint, 2 * pagesize, MAP_FIXED)",
		},
		{
			.addr = NULL,
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(NULL)",
			.low_addr_required = 1,
		},
		{
			.addr = (void *)low_addr,
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(low_addr)",
			.low_addr_required = 1,
		},
		{
			.addr = (void *)high_addr,
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(high_addr)",
			.keep_mapped = 1,
		},
		{
			.addr = (void *)high_addr,
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(high_addr) again",
			.keep_mapped = 1,
		},
		{
			.addr = (void *)high_addr,
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			.msg = "mmap(high_addr, MAP_FIXED)",
		},
		{
			.addr = (void *) -1,
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(-1)",
			.keep_mapped = 1,
		},
		{
			.addr = (void *) -1,
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(-1) again",
		},
		{
			.addr = ((void *)(addr_switch_hint - pagesize)),
			.size = pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(addr_switch_hint - pagesize, pagesize)",
			.low_addr_required = 1,
		},
		{
			.addr = (void *)(addr_switch_hint - pagesize),
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(addr_switch_hint - pagesize, 2 * pagesize)",
			.low_addr_required = 1,
			.keep_mapped = 1,
		},
		{
			.addr = (void *)(addr_switch_hint - pagesize / 2),
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(addr_switch_hint - pagesize/2 , 2 * pagesize)",
			.low_addr_required = 1,
			.keep_mapped = 1,
		},
		{
			.addr = ((void *)(addr_switch_hint)),
			.size = pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(addr_switch_hint, pagesize)",
		},
		{
			.addr = (void *)(addr_switch_hint),
			.size = 2 * pagesize,
			.flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			.msg = "mmap(addr_switch_hint, 2 * pagesize, MAP_FIXED)",
		},
	};

	struct testcase ht[] = {
		{
			.addr = NULL,
			.size = hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(NULL, MAP_HUGETLB)",
			.low_addr_required = 1,
		},
		{
			.addr = (void *)low_addr,
			.size = hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(low_addr, MAP_HUGETLB)",
			.low_addr_required = 1,
		},
		{
			.addr = (void *)high_addr,
			.size = hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(high_addr, MAP_HUGETLB)",
			.keep_mapped = 1,
		},
		{
			.addr = (void *)high_addr,
			.size = hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(high_addr, MAP_HUGETLB) again",
			.keep_mapped = 1,
		},
		{
			.addr = (void *)high_addr,
			.size = hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			.msg = "mmap(high_addr, MAP_FIXED | MAP_HUGETLB)",
		},
		{
			.addr = (void *) -1,
			.size = hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(-1, MAP_HUGETLB)",
			.keep_mapped = 1,
		},
		{
			.addr = (void *) -1,
			.size = hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(-1, MAP_HUGETLB) again",
		},
		{
			.addr = (void *)(addr_switch_hint - pagesize),
			.size = 2 * hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
			.msg = "mmap(addr_switch_hint - pagesize, 2*hugepagesize, MAP_HUGETLB)",
			.low_addr_required = 1,
			.keep_mapped = 1,
		},
		{
			.addr = (void *)(addr_switch_hint),
			.size = 2 * hugepagesize,
			.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
			.msg = "mmap(addr_switch_hint , 2*hugepagesize, MAP_FIXED | MAP_HUGETLB)",
		},
	};

	testcases = malloc(sizeof(t));
	hugetlb_testcases = malloc(sizeof(ht));

	/* Copy into global arrays */
	memcpy(testcases, t, sizeof(t));
	memcpy(hugetlb_testcases, ht, sizeof(ht));

	sz_testcases = ARRAY_SIZE(t);
	sz_hugetlb_testcases = ARRAY_SIZE(ht);
	switch_hint = addr_switch_hint;
}

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

		if (t->low_addr_required && p >= (void *)(switch_hint)) {
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
	return 1;
#else
	return 0;
#endif
}

int main(int argc, char **argv)
{
	int ret;

	if (!supported_arch())
		return KSFT_SKIP;

	testcases_init();

	ret = run_test(testcases, sz_testcases);
	if (argc == 2 && !strcmp(argv[1], "--run-hugetlb"))
		ret = run_test(hugetlb_testcases, sz_hugetlb_testcases);
	return ret;
}
