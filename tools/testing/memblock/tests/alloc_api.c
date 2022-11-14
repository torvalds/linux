// SPDX-License-Identifier: GPL-2.0-or-later
#include "alloc_api.h"

static int alloc_test_flags = TEST_F_NONE;

static inline const char * const get_memblock_alloc_name(int flags)
{
	if (flags & TEST_F_RAW)
		return "memblock_alloc_raw";
	return "memblock_alloc";
}

static inline void *run_memblock_alloc(phys_addr_t size, phys_addr_t align)
{
	if (alloc_test_flags & TEST_F_RAW)
		return memblock_alloc_raw(size, align);
	return memblock_alloc(size, align);
}

/*
 * A simple test that tries to allocate a small memory region.
 * Expect to allocate an aligned region near the end of the available memory.
 */
static int alloc_top_down_simple_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_2;
	phys_addr_t expected_start;

	PREFIX_PUSH();
	setup_memblock();

	expected_start = memblock_end_of_DRAM() - SMP_CACHE_BYTES;

	allocated_ptr = run_memblock_alloc(size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, expected_start);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory next to a reserved region that starts at
 * the misaligned address. Expect to create two separate entries, with the new
 * entry aligned to the provided alignment:
 *
 *              +
 * |            +--------+         +--------|
 * |            |  rgn2  |         |  rgn1  |
 * +------------+--------+---------+--------+
 *              ^
 *              |
 *              Aligned address boundary
 *
 * The allocation direction is top-down and region arrays are sorted from lower
 * to higher addresses, so the new region will be the first entry in
 * memory.reserved array. The previously reserved region does not get modified.
 * Region counter and total size get updated.
 */
static int alloc_top_down_disjoint_check(void)
{
	/* After allocation, this will point to the "old" region */
	struct memblock_region *rgn1 = &memblock.reserved.regions[1];
	struct memblock_region *rgn2 = &memblock.reserved.regions[0];
	struct region r1;
	void *allocated_ptr = NULL;
	phys_addr_t r2_size = SZ_16;
	/* Use custom alignment */
	phys_addr_t alignment = SMP_CACHE_BYTES * 2;
	phys_addr_t total_size;
	phys_addr_t expected_start;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SZ_2;
	r1.size = SZ_2;

	total_size = r1.size + r2_size;
	expected_start = memblock_end_of_DRAM() - alignment;

	memblock_reserve(r1.base, r1.size);

	allocated_ptr = run_memblock_alloc(r2_size, alignment);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r2_size, alloc_test_flags);

	ASSERT_EQ(rgn1->size, r1.size);
	ASSERT_EQ(rgn1->base, r1.base);

	ASSERT_EQ(rgn2->size, r2_size);
	ASSERT_EQ(rgn2->base, expected_start);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when there is enough space at the end
 * of the previously reserved block (i.e. first fit):
 *
 *  |              +--------+--------------|
 *  |              |   r1   |      r2      |
 *  +--------------+--------+--------------+
 *
 * Expect a merge of both regions. Only the region size gets updated.
 */
static int alloc_top_down_before_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	/*
	 * The first region ends at the aligned address to test region merging
	 */
	phys_addr_t r1_size = SMP_CACHE_BYTES;
	phys_addr_t r2_size = SZ_512;
	phys_addr_t total_size = r1_size + r2_size;

	PREFIX_PUSH();
	setup_memblock();

	memblock_reserve(memblock_end_of_DRAM() - total_size, r1_size);

	allocated_ptr = run_memblock_alloc(r2_size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r2_size, alloc_test_flags);

	ASSERT_EQ(rgn->size, total_size);
	ASSERT_EQ(rgn->base, memblock_end_of_DRAM() - total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when there is not enough space at the
 * end of the previously reserved block (i.e. second fit):
 *
 *  |            +-----------+------+     |
 *  |            |     r2    |  r1  |     |
 *  +------------+-----------+------+-----+
 *
 * Expect a merge of both regions. Both the base address and size of the region
 * get updated.
 */
static int alloc_top_down_after_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	struct region r1;
	void *allocated_ptr = NULL;
	phys_addr_t r2_size = SZ_512;
	phys_addr_t total_size;

	PREFIX_PUSH();
	setup_memblock();

	/*
	 * The first region starts at the aligned address to test region merging
	 */
	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES;
	r1.size = SZ_8;

	total_size = r1.size + r2_size;

	memblock_reserve(r1.base, r1.size);

	allocated_ptr = run_memblock_alloc(r2_size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r2_size, alloc_test_flags);

	ASSERT_EQ(rgn->size, total_size);
	ASSERT_EQ(rgn->base, r1.base - r2_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when there are two reserved regions with
 * a gap too small to fit the new region:
 *
 *  |       +--------+----------+   +------|
 *  |       |   r3   |    r2    |   |  r1  |
 *  +-------+--------+----------+---+------+
 *
 * Expect to allocate a region before the one that starts at the lower address,
 * and merge them into one. The region counter and total size fields get
 * updated.
 */
static int alloc_top_down_second_fit_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	struct region r1, r2;
	void *allocated_ptr = NULL;
	phys_addr_t r3_size = SZ_1K;
	phys_addr_t total_size;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SZ_512;
	r1.size = SZ_512;

	r2.base = r1.base - SZ_512;
	r2.size = SZ_256;

	total_size = r1.size + r2.size + r3_size;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc(r3_size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r3_size, alloc_test_flags);

	ASSERT_EQ(rgn->size, r2.size + r3_size);
	ASSERT_EQ(rgn->base, r2.base - r3_size);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when there are two reserved regions with
 * a gap big enough to accommodate the new region:
 *
 *  |     +--------+--------+--------+     |
 *  |     |   r2   |   r3   |   r1   |     |
 *  +-----+--------+--------+--------+-----+
 *
 * Expect to merge all of them, creating one big entry in memblock.reserved
 * array. The region counter and total size fields get updated.
 */
static int alloc_in_between_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	struct region r1, r2;
	void *allocated_ptr = NULL;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t r3_size = SZ_64;
	/*
	 * Calculate regions size so there's just enough space for the new entry
	 */
	phys_addr_t rgn_size = (MEM_SIZE - (2 * gap_size + r3_size)) / 2;
	phys_addr_t total_size;

	PREFIX_PUSH();
	setup_memblock();

	r1.size = rgn_size;
	r1.base = memblock_end_of_DRAM() - (gap_size + rgn_size);

	r2.size = rgn_size;
	r2.base = memblock_start_of_DRAM() + gap_size;

	total_size = r1.size + r2.size + r3_size;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc(r3_size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r3_size, alloc_test_flags);

	ASSERT_EQ(rgn->size, total_size);
	ASSERT_EQ(rgn->base, r1.base - r2.size - r3_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when the memory is filled with reserved
 * regions with memory gaps too small to fit the new region:
 *
 * +-------+
 * |  new  |
 * +--+----+
 *    |    +-----+    +-----+    +-----+    |
 *    |    | res |    | res |    | res |    |
 *    +----+-----+----+-----+----+-----+----+
 *
 * Expect no allocation to happen.
 */
static int alloc_small_gaps_generic_check(void)
{
	void *allocated_ptr = NULL;
	phys_addr_t region_size = SZ_1K;
	phys_addr_t gap_size = SZ_256;
	phys_addr_t region_end;

	PREFIX_PUSH();
	setup_memblock();

	region_end = memblock_start_of_DRAM();

	while (region_end < memblock_end_of_DRAM()) {
		memblock_reserve(region_end + gap_size, region_size);
		region_end += gap_size + region_size;
	}

	allocated_ptr = run_memblock_alloc(region_size, SMP_CACHE_BYTES);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when all memory is reserved.
 * Expect no allocation to happen.
 */
static int alloc_all_reserved_generic_check(void)
{
	void *allocated_ptr = NULL;

	PREFIX_PUSH();
	setup_memblock();

	/* Simulate full memory */
	memblock_reserve(memblock_start_of_DRAM(), MEM_SIZE);

	allocated_ptr = run_memblock_alloc(SZ_256, SMP_CACHE_BYTES);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when the memory is almost full,
 * with not enough space left for the new region:
 *
 *                                +-------+
 *                                |  new  |
 *                                +-------+
 *  |-----------------------------+   |
 *  |          reserved           |   |
 *  +-----------------------------+---+
 *
 * Expect no allocation to happen.
 */
static int alloc_no_space_generic_check(void)
{
	void *allocated_ptr = NULL;
	phys_addr_t available_size = SZ_256;
	phys_addr_t reserved_size = MEM_SIZE - available_size;

	PREFIX_PUSH();
	setup_memblock();

	/* Simulate almost-full memory */
	memblock_reserve(memblock_start_of_DRAM(), reserved_size);

	allocated_ptr = run_memblock_alloc(SZ_1K, SMP_CACHE_BYTES);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when the memory is almost full,
 * but there is just enough space left:
 *
 *  |---------------------------+---------|
 *  |          reserved         |   new   |
 *  +---------------------------+---------+
 *
 * Expect to allocate memory and merge all the regions. The total size field
 * gets updated.
 */
static int alloc_limited_space_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t available_size = SZ_256;
	phys_addr_t reserved_size = MEM_SIZE - available_size;

	PREFIX_PUSH();
	setup_memblock();

	/* Simulate almost-full memory */
	memblock_reserve(memblock_start_of_DRAM(), reserved_size);

	allocated_ptr = run_memblock_alloc(available_size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, available_size, alloc_test_flags);

	ASSERT_EQ(rgn->size, MEM_SIZE);
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM());

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, MEM_SIZE);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when there is no available memory
 * registered (i.e. memblock.memory has only a dummy entry).
 * Expect no allocation to happen.
 */
static int alloc_no_memory_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();

	reset_memblock_regions();

	allocated_ptr = run_memblock_alloc(SZ_1K, SMP_CACHE_BYTES);

	ASSERT_EQ(allocated_ptr, NULL);
	ASSERT_EQ(rgn->size, 0);
	ASSERT_EQ(rgn->base, 0);
	ASSERT_EQ(memblock.reserved.total_size, 0);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a region that is larger than the total size of
 * available memory (memblock.memory):
 *
 *  +-----------------------------------+
 *  |                 new               |
 *  +-----------------------------------+
 *  |                                 |
 *  |                                 |
 *  +---------------------------------+
 *
 * Expect no allocation to happen.
 */
static int alloc_too_large_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();
	setup_memblock();

	allocated_ptr = run_memblock_alloc(MEM_SIZE + SZ_2, SMP_CACHE_BYTES);

	ASSERT_EQ(allocated_ptr, NULL);
	ASSERT_EQ(rgn->size, 0);
	ASSERT_EQ(rgn->base, 0);
	ASSERT_EQ(memblock.reserved.total_size, 0);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to allocate a small memory region.
 * Expect to allocate an aligned region at the beginning of the available
 * memory.
 */
static int alloc_bottom_up_simple_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;

	PREFIX_PUSH();
	setup_memblock();

	allocated_ptr = run_memblock_alloc(SZ_2, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, SZ_2, alloc_test_flags);

	ASSERT_EQ(rgn->size, SZ_2);
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM());

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, SZ_2);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory next to a reserved region that starts at
 * the misaligned address. Expect to create two separate entries, with the new
 * entry aligned to the provided alignment:
 *
 *                      +
 *  |    +----------+   +----------+     |
 *  |    |   rgn1   |   |   rgn2   |     |
 *  +----+----------+---+----------+-----+
 *                      ^
 *                      |
 *                      Aligned address boundary
 *
 * The allocation direction is bottom-up, so the new region will be the second
 * entry in memory.reserved array. The previously reserved region does not get
 * modified. Region counter and total size get updated.
 */
static int alloc_bottom_up_disjoint_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[0];
	struct memblock_region *rgn2 = &memblock.reserved.regions[1];
	struct region r1;
	void *allocated_ptr = NULL;
	phys_addr_t r2_size = SZ_16;
	/* Use custom alignment */
	phys_addr_t alignment = SMP_CACHE_BYTES * 2;
	phys_addr_t total_size;
	phys_addr_t expected_start;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_start_of_DRAM() + SZ_2;
	r1.size = SZ_2;

	total_size = r1.size + r2_size;
	expected_start = memblock_start_of_DRAM() + alignment;

	memblock_reserve(r1.base, r1.size);

	allocated_ptr = run_memblock_alloc(r2_size, alignment);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r2_size, alloc_test_flags);

	ASSERT_EQ(rgn1->size, r1.size);
	ASSERT_EQ(rgn1->base, r1.base);

	ASSERT_EQ(rgn2->size, r2_size);
	ASSERT_EQ(rgn2->base, expected_start);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when there is enough space at
 * the beginning of the previously reserved block (i.e. first fit):
 *
 *  |------------------+--------+         |
 *  |        r1        |   r2   |         |
 *  +------------------+--------+---------+
 *
 * Expect a merge of both regions. Only the region size gets updated.
 */
static int alloc_bottom_up_before_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t r1_size = SZ_512;
	phys_addr_t r2_size = SZ_128;
	phys_addr_t total_size = r1_size + r2_size;

	PREFIX_PUSH();
	setup_memblock();

	memblock_reserve(memblock_start_of_DRAM() + r1_size, r2_size);

	allocated_ptr = run_memblock_alloc(r1_size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r1_size, alloc_test_flags);

	ASSERT_EQ(rgn->size, total_size);
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM());

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when there is not enough space at
 * the beginning of the previously reserved block (i.e. second fit):
 *
 *  |    +--------+--------------+         |
 *  |    |   r1   |      r2      |         |
 *  +----+--------+--------------+---------+
 *
 * Expect a merge of both regions. Only the region size gets updated.
 */
static int alloc_bottom_up_after_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	struct region r1;
	void *allocated_ptr = NULL;
	phys_addr_t r2_size = SZ_512;
	phys_addr_t total_size;

	PREFIX_PUSH();
	setup_memblock();

	/*
	 * The first region starts at the aligned address to test region merging
	 */
	r1.base = memblock_start_of_DRAM() + SMP_CACHE_BYTES;
	r1.size = SZ_64;

	total_size = r1.size + r2_size;

	memblock_reserve(r1.base, r1.size);

	allocated_ptr = run_memblock_alloc(r2_size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r2_size, alloc_test_flags);

	ASSERT_EQ(rgn->size, total_size);
	ASSERT_EQ(rgn->base, r1.base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory when there are two reserved regions, the
 * first one starting at the beginning of the available memory, with a gap too
 * small to fit the new region:
 *
 *  |------------+     +--------+--------+  |
 *  |     r1     |     |   r2   |   r3   |  |
 *  +------------+-----+--------+--------+--+
 *
 * Expect to allocate after the second region, which starts at the higher
 * address, and merge them into one. The region counter and total size fields
 * get updated.
 */
static int alloc_bottom_up_second_fit_check(void)
{
	struct memblock_region *rgn  = &memblock.reserved.regions[1];
	struct region r1, r2;
	void *allocated_ptr = NULL;
	phys_addr_t r3_size = SZ_1K;
	phys_addr_t total_size;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_start_of_DRAM();
	r1.size = SZ_512;

	r2.base = r1.base + r1.size + SZ_512;
	r2.size = SZ_256;

	total_size = r1.size + r2.size + r3_size;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc(r3_size, SMP_CACHE_BYTES);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r3_size, alloc_test_flags);

	ASSERT_EQ(rgn->size, r2.size + r3_size);
	ASSERT_EQ(rgn->base, r2.base);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/* Test case wrappers */
static int alloc_simple_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_top_down_simple_check();
	memblock_set_bottom_up(true);
	alloc_bottom_up_simple_check();

	return 0;
}

static int alloc_disjoint_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_top_down_disjoint_check();
	memblock_set_bottom_up(true);
	alloc_bottom_up_disjoint_check();

	return 0;
}

static int alloc_before_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_top_down_before_check();
	memblock_set_bottom_up(true);
	alloc_bottom_up_before_check();

	return 0;
}

static int alloc_after_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_top_down_after_check();
	memblock_set_bottom_up(true);
	alloc_bottom_up_after_check();

	return 0;
}

static int alloc_in_between_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_in_between_generic_check);
	run_bottom_up(alloc_in_between_generic_check);

	return 0;
}

static int alloc_second_fit_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_top_down_second_fit_check();
	memblock_set_bottom_up(true);
	alloc_bottom_up_second_fit_check();

	return 0;
}

static int alloc_small_gaps_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_small_gaps_generic_check);
	run_bottom_up(alloc_small_gaps_generic_check);

	return 0;
}

static int alloc_all_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_all_reserved_generic_check);
	run_bottom_up(alloc_all_reserved_generic_check);

	return 0;
}

static int alloc_no_space_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_no_space_generic_check);
	run_bottom_up(alloc_no_space_generic_check);

	return 0;
}

static int alloc_limited_space_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_limited_space_generic_check);
	run_bottom_up(alloc_limited_space_generic_check);

	return 0;
}

static int alloc_no_memory_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_no_memory_generic_check);
	run_bottom_up(alloc_no_memory_generic_check);

	return 0;
}

static int alloc_too_large_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_too_large_generic_check);
	run_bottom_up(alloc_too_large_generic_check);

	return 0;
}

static int memblock_alloc_checks_internal(int flags)
{
	const char *func = get_memblock_alloc_name(flags);

	alloc_test_flags = flags;
	prefix_reset();
	prefix_push(func);
	test_print("Running %s tests...\n", func);

	reset_memblock_attributes();
	dummy_physical_memory_init();

	alloc_simple_check();
	alloc_disjoint_check();
	alloc_before_check();
	alloc_after_check();
	alloc_second_fit_check();
	alloc_small_gaps_check();
	alloc_in_between_check();
	alloc_all_reserved_check();
	alloc_no_space_check();
	alloc_limited_space_check();
	alloc_no_memory_check();
	alloc_too_large_check();

	dummy_physical_memory_cleanup();

	prefix_pop();

	return 0;
}

int memblock_alloc_checks(void)
{
	memblock_alloc_checks_internal(TEST_F_NONE);
	memblock_alloc_checks_internal(TEST_F_RAW);

	return 0;
}
