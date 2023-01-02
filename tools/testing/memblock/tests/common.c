// SPDX-License-Identifier: GPL-2.0-or-later
#include "tests/common.h"
#include <string.h>
#include <getopt.h>
#include <linux/memory_hotplug.h>
#include <linux/build_bug.h>

#define PREFIXES_MAX				15
#define DELIM					": "
#define BASIS					10000

static struct test_memory memory_block;
static const char __maybe_unused *prefixes[PREFIXES_MAX];
static int __maybe_unused nr_prefixes;

static const char *short_opts = "hmv";
static const struct option long_opts[] = {
	{"help", 0, NULL, 'h'},
	{"movable-node", 0, NULL, 'm'},
	{"verbose", 0, NULL, 'v'},
	{NULL, 0, NULL, 0}
};

static const char * const help_opts[] = {
	"display this help message and exit",
	"disallow allocations from regions marked as hotplugged\n\t\t\t"
		"by simulating enabling the \"movable_node\" kernel\n\t\t\t"
		"parameter",
	"enable verbose output, which includes the name of the\n\t\t\t"
		"memblock function being tested, the name of the test,\n\t\t\t"
		"and whether the test passed or failed."
};

static int verbose;

/* sets global variable returned by movable_node_is_enabled() stub */
bool movable_node_enabled;

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

static inline void fill_memblock(void)
{
	memset(memory_block.base, 1, MEM_SIZE);
}

void setup_memblock(void)
{
	reset_memblock_regions();
	memblock_add((phys_addr_t)memory_block.base, MEM_SIZE);
	fill_memblock();
}

/**
 * setup_numa_memblock:
 * Set up a memory layout with multiple NUMA nodes in a previously allocated
 * dummy physical memory.
 * @node_fracs: an array representing the fraction of MEM_SIZE contained in
 *              each node in basis point units (one hundredth of 1% or 1/10000).
 *              For example, if node 0 should contain 1/8 of MEM_SIZE,
 *              node_fracs[0] = 1250.
 *
 * The nids will be set to 0 through NUMA_NODES - 1.
 */
void setup_numa_memblock(const unsigned int node_fracs[])
{
	phys_addr_t base;
	int flags;

	reset_memblock_regions();
	base = (phys_addr_t)memory_block.base;
	flags = (movable_node_is_enabled()) ? MEMBLOCK_NONE : MEMBLOCK_HOTPLUG;

	for (int i = 0; i < NUMA_NODES; i++) {
		assert(node_fracs[i] <= BASIS);
		phys_addr_t size = MEM_SIZE * node_fracs[i] / BASIS;

		memblock_add_node(base, size, i, flags);
		base += size;
	}
	fill_memblock();
}

void dummy_physical_memory_init(void)
{
	memory_block.base = malloc(MEM_SIZE);
	assert(memory_block.base);
	fill_memblock();
}

void dummy_physical_memory_cleanup(void)
{
	free(memory_block.base);
}

phys_addr_t dummy_physical_memory_base(void)
{
	return (phys_addr_t)memory_block.base;
}

static void usage(const char *prog)
{
	BUILD_BUG_ON(ARRAY_SIZE(help_opts) != ARRAY_SIZE(long_opts) - 1);

	printf("Usage: %s [-%s]\n", prog, short_opts);

	for (int i = 0; long_opts[i].name; i++) {
		printf("  -%c, --%-12s\t%s\n", long_opts[i].val,
		       long_opts[i].name, help_opts[i]);
	}

	exit(1);
}

void parse_args(int argc, char **argv)
{
	int c;

	while ((c = getopt_long_only(argc, argv, short_opts, long_opts,
				     NULL)) != -1) {
		switch (c) {
		case 'm':
			movable_node_enabled = true;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage(argv[0]);
		}
	}
}

void print_prefixes(const char *postfix)
{
	for (int i = 0; i < nr_prefixes; i++)
		test_print("%s%s", prefixes[i], DELIM);
	test_print(postfix);
}

void test_fail(void)
{
	if (verbose) {
		ksft_test_result_fail(": ");
		print_prefixes("failed\n");
	}
}

void test_pass(void)
{
	if (verbose) {
		ksft_test_result_pass(": ");
		print_prefixes("passed\n");
	}
}

void test_print(const char *fmt, ...)
{
	if (verbose) {
		int saved_errno = errno;
		va_list args;

		va_start(args, fmt);
		errno = saved_errno;
		vprintf(fmt, args);
		va_end(args);
	}
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
