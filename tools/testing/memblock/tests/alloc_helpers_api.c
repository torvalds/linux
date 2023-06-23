// SPDX-License-Identifier: GPL-2.0-or-later
#include "alloc_helpers_api.h"

/*
 * A simple test that tries to allocate a memory region above a specified,
 * aligned address:
 *
 *             +
 *  |          +-----------+         |
 *  |          |    rgn    |         |
 *  +----------+-----------+---------+
 *             ^
 *             |
 *             Aligned min_addr
 *
 * Expect to allocate a cleared region at the minimal memory address.
 */
static int alloc_from_simple_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	PREFIX_PUSH();

	phys_addr_t size = SZ_16;
	phys_addr_t min_addr;

	setup_memblock();

	min_addr = memblock_end_of_DRAM() - SMP_CACHE_BYTES;

	allocated_ptr = memblock_alloc_from(size, SMP_CACHE_BYTES, min_addr);
	b = (char *)allocated_ptr;

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ(*b, 0);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, min_addr);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region above a certain address.
 * The minimal address here is not aligned:
 *
 *         +      +
 *  |      +      +---------+            |
 *  |      |      |   rgn   |            |
 *  +------+------+---------+------------+
 *         ^      ^------.
 *         |             |
 *       min_addr        Aligned address
 *                       boundary
 *
 * Expect to allocate a cleared region at the closest aligned memory address.
 */
static int alloc_from_misaligned_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	PREFIX_PUSH();

	phys_addr_t size = SZ_32;
	phys_addr_t min_addr;

	setup_memblock();

	/* A misaligned address */
	min_addr = memblock_end_of_DRAM() - (SMP_CACHE_BYTES * 2 - 1);

	allocated_ptr = memblock_alloc_from(size, SMP_CACHE_BYTES, min_addr);
	b = (char *)allocated_ptr;

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ(*b, 0);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, memblock_end_of_DRAM() - SMP_CACHE_BYTES);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region above an address that is too
 * close to the end of the memory:
 *
 *              +        +
 *  |           +--------+---+      |
 *  |           |   rgn  +   |      |
 *  +-----------+--------+---+------+
 *              ^        ^
 *              |        |
 *              |        min_addr
 *              |
 *              Aligned address
 *              boundary
 *
 * Expect to prioritize granting memory over satisfying the minimal address
 * requirement.
 */
static int alloc_from_top_down_high_addr_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();

	phys_addr_t size = SZ_32;
	phys_addr_t min_addr;

	setup_memblock();

	/* The address is too close to the end of the memory */
	min_addr = memblock_end_of_DRAM() - SZ_16;

	allocated_ptr = memblock_alloc_from(size, SMP_CACHE_BYTES, min_addr);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, memblock_end_of_DRAM() - SMP_CACHE_BYTES);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region when there is no space
 * available above the minimal address above a certain address:
 *
 *                     +
 *  |        +---------+-------------|
 *  |        |   rgn   |             |
 *  +--------+---------+-------------+
 *                     ^
 *                     |
 *                     min_addr
 *
 * Expect to prioritize granting memory over satisfying the minimal address
 * requirement and to allocate next to the previously reserved region. The
 * regions get merged into one.
 */
static int alloc_from_top_down_no_space_above_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();

	phys_addr_t r1_size = SZ_64;
	phys_addr_t r2_size = SZ_2;
	phys_addr_t total_size = r1_size + r2_size;
	phys_addr_t min_addr;

	setup_memblock();

	min_addr = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;

	/* No space above this address */
	memblock_reserve(min_addr, r2_size);

	allocated_ptr = memblock_alloc_from(r1_size, SMP_CACHE_BYTES, min_addr);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ(rgn->base, min_addr - r1_size);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region with a minimal address below
 * the start address of the available memory. As the allocation is top-down,
 * first reserve a region that will force allocation near the start.
 * Expect successful allocation and merge of both regions.
 */
static int alloc_from_top_down_min_addr_cap_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();

	phys_addr_t r1_size = SZ_64;
	phys_addr_t min_addr;
	phys_addr_t start_addr;

	setup_memblock();

	start_addr = (phys_addr_t)memblock_start_of_DRAM();
	min_addr = start_addr - SMP_CACHE_BYTES * 3;

	memblock_reserve(start_addr + r1_size, MEM_SIZE - r1_size);

	allocated_ptr = memblock_alloc_from(r1_size, SMP_CACHE_BYTES, min_addr);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ(rgn->base, start_addr);
	ASSERT_EQ(rgn->size, MEM_SIZE);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, MEM_SIZE);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region above an address that is too
 * close to the end of the memory:
 *
 *                             +
 *  |-----------+              +     |
 *  |    rgn    |              |     |
 *  +-----------+--------------+-----+
 *  ^                          ^
 *  |                          |
 *  Aligned address            min_addr
 *  boundary
 *
 * Expect to prioritize granting memory over satisfying the minimal address
 * requirement. Allocation happens at beginning of the available memory.
 */
static int alloc_from_bottom_up_high_addr_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();

	phys_addr_t size = SZ_32;
	phys_addr_t min_addr;

	setup_memblock();

	/* The address is too close to the end of the memory */
	min_addr = memblock_end_of_DRAM() - SZ_8;

	allocated_ptr = memblock_alloc_from(size, SMP_CACHE_BYTES, min_addr);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM());

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region when there is no space
 * available above the minimal address above a certain address:
 *
 *                   +
 *  |-----------+    +-------------------|
 *  |    rgn    |    |                   |
 *  +-----------+----+-------------------+
 *                   ^
 *                   |
 *                   min_addr
 *
 * Expect to prioritize granting memory over satisfying the minimal address
 * requirement and to allocate at the beginning of the available memory.
 */
static int alloc_from_bottom_up_no_space_above_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();

	phys_addr_t r1_size = SZ_64;
	phys_addr_t min_addr;
	phys_addr_t r2_size;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SZ_128;
	r2_size = memblock_end_of_DRAM() - min_addr;

	/* No space above this address */
	memblock_reserve(min_addr - SMP_CACHE_BYTES, r2_size);

	allocated_ptr = memblock_alloc_from(r1_size, SMP_CACHE_BYTES, min_addr);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM());
	ASSERT_EQ(rgn->size, r1_size);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, r1_size + r2_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region with a minimal address below
 * the start address of the available memory. Expect to allocate a region
 * at the beginning of the available memory.
 */
static int alloc_from_bottom_up_min_addr_cap_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();

	phys_addr_t r1_size = SZ_64;
	phys_addr_t min_addr;
	phys_addr_t start_addr;

	setup_memblock();

	start_addr = (phys_addr_t)memblock_start_of_DRAM();
	min_addr = start_addr - SMP_CACHE_BYTES * 3;

	allocated_ptr = memblock_alloc_from(r1_size, SMP_CACHE_BYTES, min_addr);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_EQ(rgn->base, start_addr);
	ASSERT_EQ(rgn->size, r1_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, r1_size);

	test_pass_pop();

	return 0;
}

/* Test case wrappers */
static int alloc_from_simple_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_from_simple_generic_check();
	memblock_set_bottom_up(true);
	alloc_from_simple_generic_check();

	return 0;
}

static int alloc_from_misaligned_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_from_misaligned_generic_check();
	memblock_set_bottom_up(true);
	alloc_from_misaligned_generic_check();

	return 0;
}

static int alloc_from_high_addr_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_from_top_down_high_addr_check();
	memblock_set_bottom_up(true);
	alloc_from_bottom_up_high_addr_check();

	return 0;
}

static int alloc_from_no_space_above_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_from_top_down_no_space_above_check();
	memblock_set_bottom_up(true);
	alloc_from_bottom_up_no_space_above_check();

	return 0;
}

static int alloc_from_min_addr_cap_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_from_top_down_min_addr_cap_check();
	memblock_set_bottom_up(true);
	alloc_from_bottom_up_min_addr_cap_check();

	return 0;
}

int memblock_alloc_helpers_checks(void)
{
	const char *func_testing = "memblock_alloc_from";

	prefix_reset();
	prefix_push(func_testing);
	test_print("Running %s tests...\n", func_testing);

	reset_memblock_attributes();
	dummy_physical_memory_init();

	alloc_from_simple_check();
	alloc_from_misaligned_check();
	alloc_from_high_addr_check();
	alloc_from_no_space_above_check();
	alloc_from_min_addr_cap_check();

	dummy_physical_memory_cleanup();

	prefix_pop();

	return 0;
}
