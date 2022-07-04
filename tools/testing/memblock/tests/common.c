// SPDX-License-Identifier: GPL-2.0-or-later
#include "tests/common.h"
#include <string.h>

#define INIT_MEMBLOCK_REGIONS			128
#define INIT_MEMBLOCK_RESERVED_REGIONS		INIT_MEMBLOCK_REGIONS
#define PREFIXES_MAX				15
#define DELIM					": "

static struct test_memory memory_block;
static const char __maybe_unused *prefixes[PREFIXES_MAX];
static int __maybe_unused nr_prefixes;

void reset_memblock_regions(void)
{
	memset(memblock.memory.regions, 0,
	       memblock.memory.cnt * sizeof(struct memblock_region));
	memblock.memory.cnt	= 1;
	memblock.memory.max	= INIT_MEMBLOCK_REGIONS;
	memblock.memory.total_size = 0;

	memset(memblock.reserved.regions, 0,
	       memblock.reserved.cnt * sizeof(struct memblock_region));
	memblock.reserved.cnt	= 1;
	memblock.reserved.max	= INIT_MEMBLOCK_RESERVED_REGIONS;
	memblock.reserved.total_size = 0;
}

void reset_memblock_attributes(void)
{
	memblock.memory.name	= "memory";
	memblock.reserved.name	= "reserved";
	memblock.bottom_up	= false;
	memblock.current_limit	= MEMBLOCK_ALLOC_ANYWHERE;
}

void setup_memblock(void)
{
	reset_memblock_regions();
	memblock_add((phys_addr_t)memory_block.base, MEM_SIZE);
}

void dummy_physical_memory_init(void)
{
	memory_block.base = malloc(MEM_SIZE);
	assert(memory_block.base);
}

void dummy_physical_memory_cleanup(void)
{
	free(memory_block.base);
}

#ifdef VERBOSE
void print_prefixes(const char *postfix)
{
	for (int i = 0; i < nr_prefixes; i++)
		test_print("%s%s", prefixes[i], DELIM);
	test_print(postfix);
}

void test_fail(void)
{
	ksft_test_result_fail(": ");
	print_prefixes("failed\n");
}

void test_pass(void)
{
	ksft_test_result_pass(": ");
	print_prefixes("passed\n");
}

void test_print(const char *fmt, ...)
{
	int saved_errno = errno;
	va_list args;

	va_start(args, fmt);
	errno = saved_errno;
	vprintf(fmt, args);
	va_end(args);
}

void prefix_reset(void)
{
	memset(prefixes, 0, PREFIXES_MAX * sizeof(char *));
	nr_prefixes = 0;
}

void prefix_push(const char *prefix)
{
	assert(nr_prefixes < PREFIXES_MAX);
	prefixes[nr_prefixes] = prefix;
	nr_prefixes++;
}

void prefix_pop(void)
{
	if (nr_prefixes > 0) {
		prefixes[nr_prefixes - 1] = 0;
		nr_prefixes--;
	}
}
#endif /* VERBOSE */
