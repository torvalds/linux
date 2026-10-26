// SPDX-License-Identifier: GPL-2.0-or-later
#include "alloc_low_api.h"
#include <linux/align.h>

/* Allocate at the first or last aligned address below the low limit. */
static int alloc_low_simple_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	phys_addr_t size = SZ_64;
	phys_addr_t expected;
	void *allocated_ptr;

	PREFIX_PUSH();
	setup_memblock();

	/* Simulated physical RAM is not necessarily SMP_CACHE_BYTES aligned. */
	if (memblock_bottom_up())
		expected = ALIGN(memblock_start_of_DRAM(), SMP_CACHE_BYTES);
	else
		expected = ALIGN_DOWN(ARCH_LOW_ADDRESS_LIMIT - size,
				      SMP_CACHE_BYTES);

	allocated_ptr = memblock_alloc_low(size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ((phys_addr_t)(uintptr_t)allocated_ptr, expected);
	ASSERT_MEM_EQ(allocated_ptr, 0, size);
	ASSERT_EQ(rgn->base, expected);
	ASSERT_EQ(rgn->size, size);
	ASSERT_LE(region_end(rgn), ARCH_LOW_ADDRESS_LIMIT);
	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();
	return 0;
}

/* The last byte of the allocation is immediately below the low limit. */
static int alloc_low_exact_limit_check(void)
{
	phys_addr_t limit = ARCH_LOW_ADDRESS_LIMIT;
	phys_addr_t base = ALIGN_DOWN(limit - SZ_64, SMP_CACHE_BYTES);
	phys_addr_t size = limit - base;
	void *allocated_ptr;

	PREFIX_PUSH();
	setup_memblock();
	ASSERT_EQ(memblock_remove(memblock_start_of_DRAM(),
				  base - memblock_start_of_DRAM()), 0);

	allocated_ptr = memblock_alloc_low(size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ((phys_addr_t)(uintptr_t)allocated_ptr, base);
	ASSERT_MEM_EQ(allocated_ptr, 0, size);
	ASSERT_EQ(memblock.reserved.regions[0].base, base);
	ASSERT_EQ(region_end(&memblock.reserved.regions[0]), limit);
	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();
	return 0;
}

/*
 * There are size bytes below the limit, but aligning the start makes the
 * allocation cross it. Memory above the limit must not satisfy the request.
 */
static int alloc_low_alignment_crosses_limit_check(void)
{
	phys_addr_t limit = ARCH_LOW_ADDRESS_LIMIT;
	phys_addr_t base = ALIGN_DOWN(limit, SMP_CACHE_BYTES) - 1;
	phys_addr_t size = limit - base;
	void *allocated_ptr;

	PREFIX_PUSH();
	setup_memblock();
	ASSERT_EQ(memblock_remove(memblock_start_of_DRAM(),
				  base - memblock_start_of_DRAM()), 0);

	allocated_ptr = memblock_alloc_low(size, SMP_CACHE_BYTES);

	ASSERT_EQ(allocated_ptr, NULL);
	ASSERT_EQ(memblock.reserved.cnt, 0);
	ASSERT_EQ(memblock.reserved.total_size, 0);
	ASSERT_MEM_EQ((void *)(uintptr_t)base, 1, memblock_end_of_DRAM() - base);

	test_pass_pop();
	return 0;
}

/* Allocation must fail after reserving all low memory, with high memory free. */
static int alloc_low_reserved_check(void)
{
	phys_addr_t limit = ARCH_LOW_ADDRESS_LIMIT;
	phys_addr_t base = dummy_physical_memory_base();
	phys_addr_t size = SZ_64;
	void *allocated_ptr;

	PREFIX_PUSH();
	setup_memblock();
	ASSERT_EQ(memblock_reserve(base, limit - base), 0);

	allocated_ptr = memblock_alloc_low(size, SMP_CACHE_BYTES);

	ASSERT_EQ(allocated_ptr, NULL);
	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.regions[0].base, base);
	ASSERT_EQ(memblock.reserved.regions[0].size, limit - base);
	ASSERT_EQ(memblock.reserved.total_size, limit - base);
	ASSERT_MEM_EQ((void *)(uintptr_t)base, 1, MEM_SIZE);

	allocated_ptr = memblock_alloc(size, SMP_CACHE_BYTES);
	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_LE(limit, (phys_addr_t)(uintptr_t)allocated_ptr);
	ASSERT_MEM_EQ(allocated_ptr, 0, size);

	test_pass_pop();
	return 0;
}

static int alloc_low_checks(void)
{
	alloc_low_simple_check();
	alloc_low_exact_limit_check();
	alloc_low_alignment_crosses_limit_check();
	alloc_low_reserved_check();

	return 0;
}

int memblock_alloc_low_checks(void)
{
	prefix_reset();
	prefix_push("memblock_alloc_low");
	test_print("Running memblock_alloc_low tests...\n");

	reset_memblock_attributes();
	dummy_physical_memory_init();

	run_top_down(alloc_low_checks);
	run_bottom_up(alloc_low_checks);

	dummy_physical_memory_cleanup();
	prefix_pop();

	return 0;
}
