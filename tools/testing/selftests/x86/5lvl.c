#include <stdio.h>
#include <sys/mman.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#define PAGE_SIZE	4096
#define LOW_ADDR	((void *) (1UL << 30))
#define HIGH_ADDR	((void *) (1UL << 50))

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
		.addr = (void*) -1,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(-1)",
		.keep_mapped = 1,
	},
	{
		.addr = (void*) -1,
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(-1) again",
	},
	{
		.addr = (void *)((1UL << 47) - PAGE_SIZE),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap((1UL << 47), 2 * PAGE_SIZE)",
		.low_addr_required = 1,
		.keep_mapped = 1,
	},
	{
		.addr = (void *)((1UL << 47) - PAGE_SIZE / 2),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap((1UL << 47), 2 * PAGE_SIZE / 2)",
		.low_addr_required = 1,
		.keep_mapped = 1,
	},
	{
		.addr = (void *)((1UL << 47) - PAGE_SIZE),
		.size = 2 * PAGE_SIZE,
		.flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		.msg = "mmap((1UL << 47) - PAGE_SIZE, 2 * PAGE_SIZE, MAP_FIXED)",
	},
	{
		.addr = NULL,
		.size = 2UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(NULL, MAP_HUGETLB)",
		.low_addr_required = 1,
	},
	{
		.addr = LOW_ADDR,
		.size = 2UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(LOW_ADDR, MAP_HUGETLB)",
		.low_addr_required = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = 2UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(HIGH_ADDR, MAP_HUGETLB)",
		.keep_mapped = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = 2UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(HIGH_ADDR, MAP_HUGETLB) again",
		.keep_mapped = 1,
	},
	{
		.addr = HIGH_ADDR,
		.size = 2UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		.msg = "mmap(HIGH_ADDR, MAP_FIXED | MAP_HUGETLB)",
	},
	{
		.addr = (void*) -1,
		.size = 2UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(-1, MAP_HUGETLB)",
		.keep_mapped = 1,
	},
	{
		.addr = (void*) -1,
		.size = 2UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap(-1, MAP_HUGETLB) again",
	},
	{
		.addr = (void *)((1UL << 47) - PAGE_SIZE),
		.size = 4UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS,
		.msg = "mmap((1UL << 47), 4UL << 20, MAP_HUGETLB)",
		.low_addr_required = 1,
		.keep_mapped = 1,
	},
	{
		.addr = (void *)((1UL << 47) - (2UL << 20)),
		.size = 4UL << 20,
		.flags = MAP_HUGETLB | MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
		.msg = "mmap((1UL << 47) - (2UL << 20), 4UL << 20, MAP_FIXED | MAP_HUGETLB)",
	},
};

int main(int argc, char **argv)
{
	int i;
	void *p;

	for (i = 0; i < ARRAY_SIZE(testcases); i++) {
		struct testcase *t = testcases + i;

		p = mmap(t->addr, t->size, PROT_NONE, t->flags, -1, 0);

		printf("%s: %p - ", t->msg, p);

		if (p == MAP_FAILED) {
			printf("FAILED\n");
			continue;
		}

		if (t->low_addr_required && p >= (void *)(1UL << 47))
			printf("FAILED\n");
		else
			printf("OK\n");
		if (!t->keep_mapped)
			munmap(p, t->size);
	}
	return 0;
}
