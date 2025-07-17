// SPDX-License-Identifier: GPL-2.0-or-later
#include "alloc_nid_api.h"

static int alloc_nid_test_flags = TEST_F_NONE;

/*
 * contains the fraction of MEM_SIZE contained in each node in basis point
 * units (one hundredth of 1% or 1/10000)
 */
static const unsigned int node_fractions[] = {
	2500, /* 1/4  */
	 625, /* 1/16 */
	1250, /* 1/8  */
	1250, /* 1/8  */
	 625, /* 1/16 */
	 625, /* 1/16 */
	2500, /* 1/4  */
	 625, /* 1/16 */
};

static inline const char * const get_memblock_alloc_nid_name(int flags)
{
	if (flags & TEST_F_EXACT)
		return "memblock_alloc_exact_nid_raw";
	if (flags & TEST_F_RAW)
		return "memblock_alloc_try_nid_raw";
	return "memblock_alloc_try_nid";
}

static inline void *run_memblock_alloc_nid(phys_addr_t size,
					   phys_addr_t align,
					   phys_addr_t min_addr,
					   phys_addr_t max_addr, int nid)
{
	assert(!(alloc_nid_test_flags & TEST_F_EXACT) ||
	       (alloc_nid_test_flags & TEST_F_RAW));
	/*
	 * TEST_F_EXACT should be checked before TEST_F_RAW since
	 * memblock_alloc_exact_nid_raw() performs raw allocations.
	 */
	if (alloc_nid_test_flags & TEST_F_EXACT)
		return memblock_alloc_exact_nid_raw(size, align, min_addr,
						    max_addr, nid);
	if (alloc_nid_test_flags & TEST_F_RAW)
		return memblock_alloc_try_nid_raw(size, align, min_addr,
						  max_addr, nid);
	return memblock_alloc_try_nid(size, align, min_addr, max_addr, nid);
}

/*
 * A simple test that tries to allocate a memory region within min_addr and
 * max_addr range:
 *
 *        +                   +
 *   |    +       +-----------+      |
 *   |    |       |    rgn    |      |
 *   +----+-------+-----------+------+
 *        ^                   ^
 *        |                   |
 *        min_addr           max_addr
 *
 * Expect to allocate a region that ends at max_addr.
 */
static int alloc_nid_top_down_simple_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_128;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SMP_CACHE_BYTES * 2;
	max_addr = min_addr + SZ_512;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	rgn_end = rgn->base + rgn->size;

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, max_addr - size);
	ASSERT_EQ(rgn_end, max_addr);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to allocate a memory region within min_addr and
 * max_addr range, where the end address is misaligned:
 *
 *         +       +            +
 *  |      +       +---------+  +    |
 *  |      |       |   rgn   |  |    |
 *  +------+-------+---------+--+----+
 *         ^       ^            ^
 *         |       |            |
 *       min_add   |            max_addr
 *                 |
 *                 Aligned address
 *                 boundary
 *
 * Expect to allocate an aligned region that ends before max_addr.
 */
static int alloc_nid_top_down_end_misaligned_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_128;
	phys_addr_t misalign = SZ_2;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SMP_CACHE_BYTES * 2;
	max_addr = min_addr + SZ_512 + misalign;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	rgn_end = rgn->base + rgn->size;

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, max_addr - size - misalign);
	ASSERT_LT(rgn_end, max_addr);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to allocate a memory region, which spans over the
 * min_addr and max_addr range:
 *
 *         +               +
 *  |      +---------------+       |
 *  |      |      rgn      |       |
 *  +------+---------------+-------+
 *         ^               ^
 *         |               |
 *         min_addr        max_addr
 *
 * Expect to allocate a region that starts at min_addr and ends at
 * max_addr, given that min_addr is aligned.
 */
static int alloc_nid_exact_address_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_1K;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SMP_CACHE_BYTES;
	max_addr = min_addr + size;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	rgn_end = rgn->base + rgn->size;

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, min_addr);
	ASSERT_EQ(rgn_end, max_addr);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region, which can't fit into
 * min_addr and max_addr range:
 *
 *           +          +     +
 *  |        +----------+-----+    |
 *  |        |   rgn    +     |    |
 *  +--------+----------+-----+----+
 *           ^          ^     ^
 *           |          |     |
 *           Aligned    |    max_addr
 *           address    |
 *           boundary   min_add
 *
 * Expect to drop the lower limit and allocate a memory region which
 * ends at max_addr (if the address is aligned).
 */
static int alloc_nid_top_down_narrow_range_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_256;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SZ_512;
	max_addr = min_addr + SMP_CACHE_BYTES;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, max_addr - size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region, which can't fit into
 * min_addr and max_addr range, with the latter being too close to the beginning
 * of the available memory:
 *
 *   +-------------+
 *   |     new     |
 *   +-------------+
 *         +       +
 *         |       +              |
 *         |       |              |
 *         +-------+--------------+
 *         ^       ^
 *         |       |
 *         |       max_addr
 *         |
 *         min_addr
 *
 * Expect no allocation to happen.
 */
static int alloc_nid_low_max_generic_check(void)
{
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_1K;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM();
	max_addr = min_addr + SMP_CACHE_BYTES;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region within min_addr min_addr range,
 * with min_addr being so close that it's next to an allocated region:
 *
 *          +                        +
 *  |       +--------+---------------|
 *  |       |   r1   |      rgn      |
 *  +-------+--------+---------------+
 *          ^                        ^
 *          |                        |
 *          min_addr                 max_addr
 *
 * Expect a merge of both regions. Only the region size gets updated.
 */
static int alloc_nid_min_reserved_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t r1_size = SZ_128;
	phys_addr_t r2_size = SZ_64;
	phys_addr_t total_size = r1_size + r2_size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t reserved_base;

	PREFIX_PUSH();
	setup_memblock();

	max_addr = memblock_end_of_DRAM();
	min_addr = max_addr - r2_size;
	reserved_base = min_addr - r1_size;

	memblock_reserve_kern(reserved_base, r1_size);

	allocated_ptr = run_memblock_alloc_nid(r2_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r2_size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, total_size);
	ASSERT_EQ(rgn->base, reserved_base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region within min_addr and max_addr,
 * with max_addr being so close that it's next to an allocated region:
 *
 *             +             +
 *  |          +-------------+--------|
 *  |          |     rgn     |   r1   |
 *  +----------+-------------+--------+
 *             ^             ^
 *             |             |
 *             min_addr      max_addr
 *
 * Expect a merge of regions. Only the region size gets updated.
 */
static int alloc_nid_max_reserved_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t r1_size = SZ_64;
	phys_addr_t r2_size = SZ_128;
	phys_addr_t total_size = r1_size + r2_size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_memblock();

	max_addr = memblock_end_of_DRAM() - r1_size;
	min_addr = max_addr - r2_size;

	memblock_reserve_kern(max_addr, r1_size);

	allocated_ptr = run_memblock_alloc_nid(r2_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r2_size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, total_size);
	ASSERT_EQ(rgn->base, min_addr);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range, when
 * there are two reserved regions at the borders, with a gap big enough to fit
 * a new region:
 *
 *                +           +
 *  |    +--------+   +-------+------+  |
 *  |    |   r2   |   |  rgn  |  r1  |  |
 *  +----+--------+---+-------+------+--+
 *                ^           ^
 *                |           |
 *                min_addr    max_addr
 *
 * Expect to merge the new region with r1. The second region does not get
 * updated. The total size field gets updated.
 */

static int alloc_nid_top_down_reserved_with_space_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[1];
	struct memblock_region *rgn2 = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	struct region r1, r2;
	phys_addr_t r3_size = SZ_64;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;
	r1.size = SMP_CACHE_BYTES;

	r2.size = SZ_128;
	r2.base = r1.base - (r3_size + gap_size + r2.size);

	total_size = r1.size + r2.size + r3_size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve_kern(r1.base, r1.size);
	memblock_reserve_kern(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r3_size, alloc_nid_test_flags);

	ASSERT_EQ(rgn1->size, r1.size + r3_size);
	ASSERT_EQ(rgn1->base, max_addr - r3_size);

	ASSERT_EQ(rgn2->size, r2.size);
	ASSERT_EQ(rgn2->base, r2.base);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range, when
 * there are two reserved regions at the borders, with a gap of a size equal to
 * the size of the new region:
 *
 *                 +        +
 *  |     +--------+--------+--------+     |
 *  |     |   r2   |   r3   |   r1   |     |
 *  +-----+--------+--------+--------+-----+
 *                 ^        ^
 *                 |        |
 *                 min_addr max_addr
 *
 * Expect to merge all of the regions into one. The region counter and total
 * size fields get updated.
 */
static int alloc_nid_reserved_full_merge_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	struct region r1, r2;
	phys_addr_t r3_size = SZ_64;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;
	r1.size = SMP_CACHE_BYTES;

	r2.size = SZ_128;
	r2.base = r1.base - (r3_size + r2.size);

	total_size = r1.size + r2.size + r3_size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve_kern(r1.base, r1.size);
	memblock_reserve_kern(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r3_size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, total_size);
	ASSERT_EQ(rgn->base, r2.base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range, when
 * there are two reserved regions at the borders, with a gap that can't fit
 * a new region:
 *
 *                       +    +
 *  |  +----------+------+    +------+   |
 *  |  |    r3    |  r2  |    |  r1  |   |
 *  +--+----------+------+----+------+---+
 *                       ^    ^
 *                       |    |
 *                       |    max_addr
 *                       |
 *                       min_addr
 *
 * Expect to merge the new region with r2. The second region does not get
 * updated. The total size counter gets updated.
 */
static int alloc_nid_top_down_reserved_no_space_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[1];
	struct memblock_region *rgn2 = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	struct region r1, r2;
	phys_addr_t r3_size = SZ_256;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;
	r1.size = SMP_CACHE_BYTES;

	r2.size = SZ_128;
	r2.base = r1.base - (r2.size + gap_size);

	total_size = r1.size + r2.size + r3_size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve_kern(r1.base, r1.size);
	memblock_reserve_kern(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r3_size, alloc_nid_test_flags);

	ASSERT_EQ(rgn1->size, r1.size);
	ASSERT_EQ(rgn1->base, r1.base);

	ASSERT_EQ(rgn2->size, r2.size + r3_size);
	ASSERT_EQ(rgn2->base, r2.base - r3_size);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range, but
 * it's too narrow and everything else is reserved:
 *
 *            +-----------+
 *            |    new    |
 *            +-----------+
 *                 +      +
 *  |--------------+      +----------|
 *  |      r2      |      |    r1    |
 *  +--------------+------+----------+
 *                 ^      ^
 *                 |      |
 *                 |      max_addr
 *                 |
 *                 min_addr
 *
 * Expect no allocation to happen.
 */

static int alloc_nid_reserved_all_generic_check(void)
{
	void *allocated_ptr = NULL;
	struct region r1, r2;
	phys_addr_t r3_size = SZ_256;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES;
	r1.size = SMP_CACHE_BYTES;

	r2.size = MEM_SIZE - (r1.size + gap_size);
	r2.base = memblock_start_of_DRAM();

	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region, where max_addr is
 * bigger than the end address of the available memory. Expect to allocate
 * a region that ends before the end of the memory.
 */
static int alloc_nid_top_down_cap_max_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_256;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_end_of_DRAM() - SZ_1K;
	max_addr = memblock_end_of_DRAM() + SZ_256;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, memblock_end_of_DRAM() - size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region, where min_addr is
 * smaller than the start address of the available memory. Expect to allocate
 * a region that ends before the end of the memory.
 */
static int alloc_nid_top_down_cap_min_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_1K;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() - SZ_256;
	max_addr = memblock_end_of_DRAM();

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, memblock_end_of_DRAM() - size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to allocate a memory region within min_addr and
 * max_addr range:
 *
 *        +                       +
 *   |    +-----------+           |      |
 *   |    |    rgn    |           |      |
 *   +----+-----------+-----------+------+
 *        ^                       ^
 *        |                       |
 *        min_addr                max_addr
 *
 * Expect to allocate a region that ends before max_addr.
 */
static int alloc_nid_bottom_up_simple_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_128;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SMP_CACHE_BYTES * 2;
	max_addr = min_addr + SZ_512;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	rgn_end = rgn->base + rgn->size;

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, min_addr);
	ASSERT_LT(rgn_end, max_addr);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to allocate a memory region within min_addr and
 * max_addr range, where the start address is misaligned:
 *
 *        +                     +
 *  |     +   +-----------+     +     |
 *  |     |   |    rgn    |     |     |
 *  +-----+---+-----------+-----+-----+
 *        ^   ^----.            ^
 *        |        |            |
 *     min_add     |            max_addr
 *                 |
 *                 Aligned address
 *                 boundary
 *
 * Expect to allocate an aligned region that ends before max_addr.
 */
static int alloc_nid_bottom_up_start_misaligned_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_128;
	phys_addr_t misalign = SZ_2;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() + misalign;
	max_addr = min_addr + SZ_512;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	rgn_end = rgn->base + rgn->size;

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, min_addr + (SMP_CACHE_BYTES - misalign));
	ASSERT_LT(rgn_end, max_addr);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region, which can't fit into min_addr
 * and max_addr range:
 *
 *                      +    +
 *  |---------+         +    +      |
 *  |   rgn   |         |    |      |
 *  +---------+---------+----+------+
 *                      ^    ^
 *                      |    |
 *                      |    max_addr
 *                      |
 *                      min_add
 *
 * Expect to drop the lower limit and allocate a memory region which
 * starts at the beginning of the available memory.
 */
static int alloc_nid_bottom_up_narrow_range_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_256;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SZ_512;
	max_addr = min_addr + SMP_CACHE_BYTES;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM());

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range, when
 * there are two reserved regions at the borders, with a gap big enough to fit
 * a new region:
 *
 *                +           +
 *  |    +--------+-------+   +------+  |
 *  |    |   r2   |  rgn  |   |  r1  |  |
 *  +----+--------+-------+---+------+--+
 *                ^           ^
 *                |           |
 *                min_addr    max_addr
 *
 * Expect to merge the new region with r2. The second region does not get
 * updated. The total size field gets updated.
 */

static int alloc_nid_bottom_up_reserved_with_space_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[1];
	struct memblock_region *rgn2 = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	struct region r1, r2;
	phys_addr_t r3_size = SZ_64;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;
	r1.size = SMP_CACHE_BYTES;

	r2.size = SZ_128;
	r2.base = r1.base - (r3_size + gap_size + r2.size);

	total_size = r1.size + r2.size + r3_size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve_kern(r1.base, r1.size);
	memblock_reserve_kern(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r3_size, alloc_nid_test_flags);

	ASSERT_EQ(rgn1->size, r1.size);
	ASSERT_EQ(rgn1->base, max_addr);

	ASSERT_EQ(rgn2->size, r2.size + r3_size);
	ASSERT_EQ(rgn2->base, r2.base);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range, when
 * there are two reserved regions at the borders, with a gap of a size equal to
 * the size of the new region:
 *
 *                         +   +
 *  |----------+    +------+   +----+  |
 *  |    r3    |    |  r2  |   | r1 |  |
 *  +----------+----+------+---+----+--+
 *                         ^   ^
 *                         |   |
 *                         |  max_addr
 *                         |
 *                         min_addr
 *
 * Expect to drop the lower limit and allocate memory at the beginning of the
 * available memory. The region counter and total size fields get updated.
 * Other regions are not modified.
 */

static int alloc_nid_bottom_up_reserved_no_space_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[2];
	struct memblock_region *rgn2 = &memblock.reserved.regions[1];
	struct memblock_region *rgn3 = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	struct region r1, r2;
	phys_addr_t r3_size = SZ_256;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;
	r1.size = SMP_CACHE_BYTES;

	r2.size = SZ_128;
	r2.base = r1.base - (r2.size + gap_size);

	total_size = r1.size + r2.size + r3_size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, r3_size, alloc_nid_test_flags);

	ASSERT_EQ(rgn3->size, r3_size);
	ASSERT_EQ(rgn3->base, memblock_start_of_DRAM());

	ASSERT_EQ(rgn2->size, r2.size);
	ASSERT_EQ(rgn2->base, r2.base);

	ASSERT_EQ(rgn1->size, r1.size);
	ASSERT_EQ(rgn1->base, r1.base);

	ASSERT_EQ(memblock.reserved.cnt, 3);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region, where max_addr is
 * bigger than the end address of the available memory. Expect to allocate
 * a region that starts at the min_addr.
 */
static int alloc_nid_bottom_up_cap_max_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_256;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SZ_1K;
	max_addr = memblock_end_of_DRAM() + SZ_256;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, min_addr);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region, where min_addr is
 * smaller than the start address of the available memory. Expect to allocate
 * a region at the beginning of the available memory.
 */
static int alloc_nid_bottom_up_cap_min_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_1K;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_memblock();

	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM() - SZ_256;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(rgn->size, size);
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM());

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/* Test case wrappers for range tests */
static int alloc_nid_simple_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_simple_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_simple_check();

	return 0;
}

static int alloc_nid_misaligned_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_end_misaligned_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_start_misaligned_check();

	return 0;
}

static int alloc_nid_narrow_range_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_narrow_range_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_narrow_range_check();

	return 0;
}

static int alloc_nid_reserved_with_space_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_reserved_with_space_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_reserved_with_space_check();

	return 0;
}

static int alloc_nid_reserved_no_space_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_reserved_no_space_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_reserved_no_space_check();

	return 0;
}

static int alloc_nid_cap_max_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_cap_max_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_cap_max_check();

	return 0;
}

static int alloc_nid_cap_min_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_cap_min_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_cap_min_check();

	return 0;
}

static int alloc_nid_min_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_min_reserved_generic_check);
	run_bottom_up(alloc_nid_min_reserved_generic_check);

	return 0;
}

static int alloc_nid_max_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_max_reserved_generic_check);
	run_bottom_up(alloc_nid_max_reserved_generic_check);

	return 0;
}

static int alloc_nid_exact_address_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_exact_address_generic_check);
	run_bottom_up(alloc_nid_exact_address_generic_check);

	return 0;
}

static int alloc_nid_reserved_full_merge_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_reserved_full_merge_generic_check);
	run_bottom_up(alloc_nid_reserved_full_merge_generic_check);

	return 0;
}

static int alloc_nid_reserved_all_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_reserved_all_generic_check);
	run_bottom_up(alloc_nid_reserved_all_generic_check);

	return 0;
}

static int alloc_nid_low_max_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_low_max_generic_check);
	run_bottom_up(alloc_nid_low_max_generic_check);

	return 0;
}

static int memblock_alloc_nid_range_checks(void)
{
	test_print("Running %s range tests...\n",
		   get_memblock_alloc_nid_name(alloc_nid_test_flags));

	alloc_nid_simple_check();
	alloc_nid_misaligned_check();
	alloc_nid_narrow_range_check();
	alloc_nid_reserved_with_space_check();
	alloc_nid_reserved_no_space_check();
	alloc_nid_cap_max_check();
	alloc_nid_cap_min_check();

	alloc_nid_min_reserved_check();
	alloc_nid_max_reserved_check();
	alloc_nid_exact_address_check();
	alloc_nid_reserved_full_merge_check();
	alloc_nid_reserved_all_check();
	alloc_nid_low_max_check();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * has enough memory to allocate a region of the requested size.
 * Expect to allocate an aligned region at the end of the requested node.
 */
static int alloc_nid_top_down_numa_simple_check(void)
{
	int nid_req = 3;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	ASSERT_LE(SZ_4, req_node->size);
	size = req_node->size / SZ_4;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, region_end(req_node) - size);
	ASSERT_LE(req_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * does not have enough memory to allocate a region of the requested size:
 *
 *  |   +-----+          +------------------+     |
 *  |   | req |          |     expected     |     |
 *  +---+-----+----------+------------------+-----+
 *
 *  |                             +---------+     |
 *  |                             |   rgn   |     |
 *  +-----------------------------+---------+-----+
 *
 * Expect to allocate an aligned region at the end of the last node that has
 * enough memory (in this case, nid = 6) after falling back to NUMA_NO_NODE.
 */
static int alloc_nid_top_down_numa_small_node_check(void)
{
	int nid_req = 1;
	int nid_exp = 6;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *exp_node = &memblock.memory.regions[nid_exp];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	size = SZ_2 * req_node->size;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, region_end(exp_node) - size);
	ASSERT_LE(exp_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * is fully reserved:
 *
 *  |              +---------+            +------------------+     |
 *  |              |requested|            |     expected     |     |
 *  +--------------+---------+------------+------------------+-----+
 *
 *  |              +---------+                     +---------+     |
 *  |              | reserved|                     |   new   |     |
 *  +--------------+---------+---------------------+---------+-----+
 *
 * Expect to allocate an aligned region at the end of the last node that is
 * large enough and has enough unreserved memory (in this case, nid = 6) after
 * falling back to NUMA_NO_NODE. The region count and total size get updated.
 */
static int alloc_nid_top_down_numa_node_reserved_check(void)
{
	int nid_req = 2;
	int nid_exp = 6;
	struct memblock_region *new_rgn = &memblock.reserved.regions[1];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *exp_node = &memblock.memory.regions[nid_exp];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	size = req_node->size;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	memblock_reserve(req_node->base, req_node->size);
	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, region_end(exp_node) - size);
	ASSERT_LE(exp_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, size + req_node->size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * is partially reserved but has enough memory for the allocated region:
 *
 *  |           +---------------------------------------+          |
 *  |           |               requested               |          |
 *  +-----------+---------------------------------------+----------+
 *
 *  |           +------------------+              +-----+          |
 *  |           |     reserved     |              | new |          |
 *  +-----------+------------------+--------------+-----+----------+
 *
 * Expect to allocate an aligned region at the end of the requested node. The
 * region count and total size get updated.
 */
static int alloc_nid_top_down_numa_part_reserved_check(void)
{
	int nid_req = 4;
	struct memblock_region *new_rgn = &memblock.reserved.regions[1];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	void *allocated_ptr = NULL;
	struct region r1;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	ASSERT_LE(SZ_8, req_node->size);
	r1.base = req_node->base;
	r1.size = req_node->size / SZ_2;
	size = r1.size / SZ_4;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	memblock_reserve(r1.base, r1.size);
	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, region_end(req_node) - size);
	ASSERT_LE(req_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, size + r1.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * is partially reserved and does not have enough contiguous memory for the
 * allocated region:
 *
 *  |           +-----------------------+         +----------------------|
 *  |           |       requested       |         |       expected       |
 *  +-----------+-----------------------+---------+----------------------+
 *
 *  |                 +----------+                           +-----------|
 *  |                 | reserved |                           |    new    |
 *  +-----------------+----------+---------------------------+-----------+
 *
 * Expect to allocate an aligned region at the end of the last node that is
 * large enough and has enough unreserved memory (in this case,
 * nid = NUMA_NODES - 1) after falling back to NUMA_NO_NODE. The region count
 * and total size get updated.
 */
static int alloc_nid_top_down_numa_part_reserved_fallback_check(void)
{
	int nid_req = 4;
	int nid_exp = NUMA_NODES - 1;
	struct memblock_region *new_rgn = &memblock.reserved.regions[1];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *exp_node = &memblock.memory.regions[nid_exp];
	void *allocated_ptr = NULL;
	struct region r1;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	ASSERT_LE(SZ_4, req_node->size);
	size = req_node->size / SZ_2;
	r1.base = req_node->base + (size / SZ_2);
	r1.size = size;

	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	memblock_reserve(r1.base, r1.size);
	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, region_end(exp_node) - size);
	ASSERT_LE(exp_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, size + r1.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region that spans over the min_addr
 * and max_addr range and overlaps with two different nodes, where the first
 * node is the requested node:
 *
 *                                min_addr
 *                                |           max_addr
 *                                |           |
 *                                v           v
 *  |           +-----------------------+-----------+              |
 *  |           |       requested       |   node3   |              |
 *  +-----------+-----------------------+-----------+--------------+
 *                                +           +
 *  |                       +-----------+                          |
 *  |                       |    rgn    |                          |
 *  +-----------------------+-----------+--------------------------+
 *
 * Expect to drop the lower limit and allocate a memory region that ends at
 * the end of the requested node.
 */
static int alloc_nid_top_down_numa_split_range_low_check(void)
{
	int nid_req = 2;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_512;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t req_node_end;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	req_node_end = region_end(req_node);
	min_addr = req_node_end - SZ_256;
	max_addr = min_addr + size;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, req_node_end - size);
	ASSERT_LE(req_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region that spans over the min_addr
 * and max_addr range and overlaps with two different nodes, where the second
 * node is the requested node:
 *
 *                               min_addr
 *                               |         max_addr
 *                               |         |
 *                               v         v
 *  |      +--------------------------+---------+                |
 *  |      |         expected         |requested|                |
 *  +------+--------------------------+---------+----------------+
 *                               +         +
 *  |                       +---------+                          |
 *  |                       |   rgn   |                          |
 *  +-----------------------+---------+--------------------------+
 *
 * Expect to drop the lower limit and allocate a memory region that
 * ends at the end of the first node that overlaps with the range.
 */
static int alloc_nid_top_down_numa_split_range_high_check(void)
{
	int nid_req = 3;
	int nid_exp = nid_req - 1;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *exp_node = &memblock.memory.regions[nid_exp];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_512;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t exp_node_end;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	exp_node_end = region_end(exp_node);
	min_addr = exp_node_end - SZ_256;
	max_addr = min_addr + size;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, exp_node_end - size);
	ASSERT_LE(exp_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region that spans over the min_addr
 * and max_addr range and overlaps with two different nodes, where the requested
 * node ends before min_addr:
 *
 *                                         min_addr
 *                                         |         max_addr
 *                                         |         |
 *                                         v         v
 *  |    +---------------+        +-------------+---------+          |
 *  |    |   requested   |        |    node1    |  node2  |          |
 *  +----+---------------+--------+-------------+---------+----------+
 *                                         +         +
 *  |          +---------+                                           |
 *  |          |   rgn   |                                           |
 *  +----------+---------+-------------------------------------------+
 *
 * Expect to drop the lower limit and allocate a memory region that ends at
 * the end of the requested node.
 */
static int alloc_nid_top_down_numa_no_overlap_split_check(void)
{
	int nid_req = 2;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *node2 = &memblock.memory.regions[6];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	size = SZ_512;
	min_addr = node2->base - SZ_256;
	max_addr = min_addr + size;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, region_end(req_node) - size);
	ASSERT_LE(req_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range when
 * the requested node and the range do not overlap, and requested node ends
 * before min_addr. The range overlaps with multiple nodes along node
 * boundaries:
 *
 *                          min_addr
 *                          |                                 max_addr
 *                          |                                 |
 *                          v                                 v
 *  |-----------+           +----------+----...----+----------+      |
 *  | requested |           | min node |    ...    | max node |      |
 *  +-----------+-----------+----------+----...----+----------+------+
 *                          +                                 +
 *  |                                                   +-----+      |
 *  |                                                   | rgn |      |
 *  +---------------------------------------------------+-----+------+
 *
 * Expect to allocate a memory region at the end of the final node in
 * the range after falling back to NUMA_NO_NODE.
 */
static int alloc_nid_top_down_numa_no_overlap_low_check(void)
{
	int nid_req = 0;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *min_node = &memblock.memory.regions[2];
	struct memblock_region *max_node = &memblock.memory.regions[5];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_64;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	min_addr = min_node->base;
	max_addr = region_end(max_node);

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, max_addr - size);
	ASSERT_LE(max_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range when
 * the requested node and the range do not overlap, and requested node starts
 * after max_addr. The range overlaps with multiple nodes along node
 * boundaries:
 *
 *        min_addr
 *        |                                 max_addr
 *        |                                 |
 *        v                                 v
 *  |     +----------+----...----+----------+        +-----------+   |
 *  |     | min node |    ...    | max node |        | requested |   |
 *  +-----+----------+----...----+----------+--------+-----------+---+
 *        +                                 +
 *  |                                 +-----+                        |
 *  |                                 | rgn |                        |
 *  +---------------------------------+-----+------------------------+
 *
 * Expect to allocate a memory region at the end of the final node in
 * the range after falling back to NUMA_NO_NODE.
 */
static int alloc_nid_top_down_numa_no_overlap_high_check(void)
{
	int nid_req = 7;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *min_node = &memblock.memory.regions[2];
	struct memblock_region *max_node = &memblock.memory.regions[5];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_64;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	min_addr = min_node->base;
	max_addr = region_end(max_node);

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, max_addr - size);
	ASSERT_LE(max_node->base, new_rgn->base);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * has enough memory to allocate a region of the requested size.
 * Expect to allocate an aligned region at the beginning of the requested node.
 */
static int alloc_nid_bottom_up_numa_simple_check(void)
{
	int nid_req = 3;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	ASSERT_LE(SZ_4, req_node->size);
	size = req_node->size / SZ_4;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, req_node->base);
	ASSERT_LE(region_end(new_rgn), region_end(req_node));

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * does not have enough memory to allocate a region of the requested size:
 *
 *  |----------------------+-----+                |
 *  |       expected       | req |                |
 *  +----------------------+-----+----------------+
 *
 *  |---------+                                   |
 *  |   rgn   |                                   |
 *  +---------+-----------------------------------+
 *
 * Expect to allocate an aligned region at the beginning of the first node that
 * has enough memory (in this case, nid = 0) after falling back to NUMA_NO_NODE.
 */
static int alloc_nid_bottom_up_numa_small_node_check(void)
{
	int nid_req = 1;
	int nid_exp = 0;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *exp_node = &memblock.memory.regions[nid_exp];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	size = SZ_2 * req_node->size;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, exp_node->base);
	ASSERT_LE(region_end(new_rgn), region_end(exp_node));

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * is fully reserved:
 *
 *  |----------------------+     +-----------+                    |
 *  |       expected       |     | requested |                    |
 *  +----------------------+-----+-----------+--------------------+
 *
 *  |-----------+                +-----------+                    |
 *  |    new    |                |  reserved |                    |
 *  +-----------+----------------+-----------+--------------------+
 *
 * Expect to allocate an aligned region at the beginning of the first node that
 * is large enough and has enough unreserved memory (in this case, nid = 0)
 * after falling back to NUMA_NO_NODE. The region count and total size get
 * updated.
 */
static int alloc_nid_bottom_up_numa_node_reserved_check(void)
{
	int nid_req = 2;
	int nid_exp = 0;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *exp_node = &memblock.memory.regions[nid_exp];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	size = req_node->size;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	memblock_reserve(req_node->base, req_node->size);
	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, exp_node->base);
	ASSERT_LE(region_end(new_rgn), region_end(exp_node));

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, size + req_node->size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * is partially reserved but has enough memory for the allocated region:
 *
 *  |           +---------------------------------------+         |
 *  |           |               requested               |         |
 *  +-----------+---------------------------------------+---------+
 *
 *  |           +------------------+-----+                        |
 *  |           |     reserved     | new |                        |
 *  +-----------+------------------+-----+------------------------+
 *
 * Expect to allocate an aligned region in the requested node that merges with
 * the existing reserved region. The total size gets updated.
 */
static int alloc_nid_bottom_up_numa_part_reserved_check(void)
{
	int nid_req = 4;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	void *allocated_ptr = NULL;
	struct region r1;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t total_size;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	ASSERT_LE(SZ_8, req_node->size);
	r1.base = req_node->base;
	r1.size = req_node->size / SZ_2;
	size = r1.size / SZ_4;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();
	total_size = size + r1.size;

	memblock_reserve(r1.base, r1.size);
	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, total_size);
	ASSERT_EQ(new_rgn->base, req_node->base);
	ASSERT_LE(region_end(new_rgn), region_end(req_node));

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * is partially reserved and does not have enough contiguous memory for the
 * allocated region:
 *
 *  |----------------------+       +-----------------------+         |
 *  |       expected       |       |       requested       |         |
 *  +----------------------+-------+-----------------------+---------+
 *
 *  |-----------+                        +----------+                |
 *  |    new    |                        | reserved |                |
 *  +-----------+------------------------+----------+----------------+
 *
 * Expect to allocate an aligned region at the beginning of the first
 * node that is large enough and has enough unreserved memory (in this case,
 * nid = 0) after falling back to NUMA_NO_NODE. The region count and total size
 * get updated.
 */
static int alloc_nid_bottom_up_numa_part_reserved_fallback_check(void)
{
	int nid_req = 4;
	int nid_exp = 0;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *exp_node = &memblock.memory.regions[nid_exp];
	void *allocated_ptr = NULL;
	struct region r1;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	ASSERT_LE(SZ_4, req_node->size);
	size = req_node->size / SZ_2;
	r1.base = req_node->base + (size / SZ_2);
	r1.size = size;

	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	memblock_reserve(r1.base, r1.size);
	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, exp_node->base);
	ASSERT_LE(region_end(new_rgn), region_end(exp_node));

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, size + r1.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region that spans over the min_addr
 * and max_addr range and overlaps with two different nodes, where the first
 * node is the requested node:
 *
 *                                min_addr
 *                                |           max_addr
 *                                |           |
 *                                v           v
 *  |           +-----------------------+-----------+              |
 *  |           |       requested       |   node3   |              |
 *  +-----------+-----------------------+-----------+--------------+
 *                                +           +
 *  |           +-----------+                                      |
 *  |           |    rgn    |                                      |
 *  +-----------+-----------+--------------------------------------+
 *
 * Expect to drop the lower limit and allocate a memory region at the beginning
 * of the requested node.
 */
static int alloc_nid_bottom_up_numa_split_range_low_check(void)
{
	int nid_req = 2;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_512;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t req_node_end;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	req_node_end = region_end(req_node);
	min_addr = req_node_end - SZ_256;
	max_addr = min_addr + size;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, req_node->base);
	ASSERT_LE(region_end(new_rgn), req_node_end);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region that spans over the min_addr
 * and max_addr range and overlaps with two different nodes, where the second
 * node is the requested node:
 *
 *                                                min_addr
 *                                                |         max_addr
 *                                                |         |
 *                                                v         v
 *  |------------------+        +----------------------+---------+      |
 *  |     expected     |        |       previous       |requested|      |
 *  +------------------+--------+----------------------+---------+------+
 *                                                +         +
 *  |---------+                                                         |
 *  |   rgn   |                                                         |
 *  +---------+---------------------------------------------------------+
 *
 * Expect to drop the lower limit and allocate a memory region at the beginning
 * of the first node that has enough memory.
 */
static int alloc_nid_bottom_up_numa_split_range_high_check(void)
{
	int nid_req = 3;
	int nid_exp = 0;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *exp_node = &memblock.memory.regions[nid_exp];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_512;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t exp_node_end;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	exp_node_end = region_end(req_node);
	min_addr = req_node->base - SZ_256;
	max_addr = min_addr + size;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, exp_node->base);
	ASSERT_LE(region_end(new_rgn), exp_node_end);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region that spans over the min_addr
 * and max_addr range and overlaps with two different nodes, where the requested
 * node ends before min_addr:
 *
 *                                          min_addr
 *                                         |         max_addr
 *                                         |         |
 *                                         v         v
 *  |    +---------------+        +-------------+---------+         |
 *  |    |   requested   |        |    node1    |  node2  |         |
 *  +----+---------------+--------+-------------+---------+---------+
 *                                         +         +
 *  |    +---------+                                                |
 *  |    |   rgn   |                                                |
 *  +----+---------+------------------------------------------------+
 *
 * Expect to drop the lower limit and allocate a memory region that starts at
 * the beginning of the requested node.
 */
static int alloc_nid_bottom_up_numa_no_overlap_split_check(void)
{
	int nid_req = 2;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *node2 = &memblock.memory.regions[6];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	size = SZ_512;
	min_addr = node2->base - SZ_256;
	max_addr = min_addr + size;

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, req_node->base);
	ASSERT_LE(region_end(new_rgn), region_end(req_node));

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range when
 * the requested node and the range do not overlap, and requested node ends
 * before min_addr. The range overlaps with multiple nodes along node
 * boundaries:
 *
 *                          min_addr
 *                          |                                 max_addr
 *                          |                                 |
 *                          v                                 v
 *  |-----------+           +----------+----...----+----------+      |
 *  | requested |           | min node |    ...    | max node |      |
 *  +-----------+-----------+----------+----...----+----------+------+
 *                          +                                 +
 *  |                       +-----+                                  |
 *  |                       | rgn |                                  |
 *  +-----------------------+-----+----------------------------------+
 *
 * Expect to allocate a memory region at the beginning of the first node
 * in the range after falling back to NUMA_NO_NODE.
 */
static int alloc_nid_bottom_up_numa_no_overlap_low_check(void)
{
	int nid_req = 0;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *min_node = &memblock.memory.regions[2];
	struct memblock_region *max_node = &memblock.memory.regions[5];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_64;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	min_addr = min_node->base;
	max_addr = region_end(max_node);

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, min_addr);
	ASSERT_LE(region_end(new_rgn), region_end(min_node));

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range when
 * the requested node and the range do not overlap, and requested node starts
 * after max_addr. The range overlaps with multiple nodes along node
 * boundaries:
 *
 *        min_addr
 *        |                                 max_addr
 *        |                                 |
 *        v                                 v
 *  |     +----------+----...----+----------+         +---------+   |
 *  |     | min node |    ...    | max node |         |requested|   |
 *  +-----+----------+----...----+----------+---------+---------+---+
 *        +                                 +
 *  |     +-----+                                                   |
 *  |     | rgn |                                                   |
 *  +-----+-----+---------------------------------------------------+
 *
 * Expect to allocate a memory region at the beginning of the first node
 * in the range after falling back to NUMA_NO_NODE.
 */
static int alloc_nid_bottom_up_numa_no_overlap_high_check(void)
{
	int nid_req = 7;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *min_node = &memblock.memory.regions[2];
	struct memblock_region *max_node = &memblock.memory.regions[5];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_64;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	min_addr = min_node->base;
	max_addr = region_end(max_node);

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, min_addr);
	ASSERT_LE(region_end(new_rgn), region_end(min_node));

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * does not have enough memory to allocate a region of the requested size.
 * Additionally, none of the nodes have enough memory to allocate the region:
 *
 * +-----------------------------------+
 * |                new                |
 * +-----------------------------------+
 *     |-------+-------+-------+-------+-------+-------+-------+-------|
 *     | node0 | node1 | node2 | node3 | node4 | node5 | node6 | node7 |
 *     +-------+-------+-------+-------+-------+-------+-------+-------+
 *
 * Expect no allocation to happen.
 */
static int alloc_nid_numa_large_region_generic_check(void)
{
	int nid_req = 3;
	void *allocated_ptr = NULL;
	phys_addr_t size = MEM_SIZE / SZ_2;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);
	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_addr range when
 * there are two reserved regions at the borders. The requested node starts at
 * min_addr and ends at max_addr and is the same size as the region to be
 * allocated:
 *
 *                     min_addr
 *                     |                       max_addr
 *                     |                       |
 *                     v                       v
 *  |      +-----------+-----------------------+-----------------------|
 *  |      |   node5   |       requested       |         node7         |
 *  +------+-----------+-----------------------+-----------------------+
 *                     +                       +
 *  |             +----+-----------------------+----+                  |
 *  |             | r2 |          new          | r1 |                  |
 *  +-------------+----+-----------------------+----+------------------+
 *
 * Expect to merge all of the regions into one. The region counter and total
 * size fields get updated.
 */
static int alloc_nid_numa_reserved_full_merge_generic_check(void)
{
	int nid_req = 6;
	int nid_next = nid_req + 1;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	struct memblock_region *next_node = &memblock.memory.regions[nid_next];
	void *allocated_ptr = NULL;
	struct region r1, r2;
	phys_addr_t size = req_node->size;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	r1.base = next_node->base;
	r1.size = SZ_128;

	r2.size = SZ_128;
	r2.base = r1.base - (size + r2.size);

	total_size = r1.size + r2.size + size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	assert_mem_content(allocated_ptr, size, alloc_nid_test_flags);

	ASSERT_EQ(new_rgn->size, total_size);
	ASSERT_EQ(new_rgn->base, r2.base);

	ASSERT_LE(new_rgn->base, req_node->base);
	ASSERT_LE(region_end(req_node), region_end(new_rgn));

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate memory within min_addr and max_add range,
 * where the total range can fit the region, but it is split between two nodes
 * and everything else is reserved. Additionally, nid is set to NUMA_NO_NODE
 * instead of requesting a specific node:
 *
 *                         +-----------+
 *                         |    new    |
 *                         +-----------+
 *  |      +---------------------+-----------|
 *  |      |      prev node      | next node |
 *  +------+---------------------+-----------+
 *                         +           +
 *  |----------------------+           +-----|
 *  |          r1          |           |  r2 |
 *  +----------------------+-----------+-----+
 *                         ^           ^
 *                         |           |
 *                         |           max_addr
 *                         |
 *                         min_addr
 *
 * Expect no allocation to happen.
 */
static int alloc_nid_numa_split_all_reserved_generic_check(void)
{
	void *allocated_ptr = NULL;
	struct memblock_region *next_node = &memblock.memory.regions[7];
	struct region r1, r2;
	phys_addr_t size = SZ_256;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	r2.base = next_node->base + SZ_128;
	r2.size = memblock_end_of_DRAM() - r2.base;

	r1.size = MEM_SIZE - (r2.size + size);
	r1.base = memblock_start_of_DRAM();

	min_addr = r1.base + r1.size;
	max_addr = r2.base;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = run_memblock_alloc_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to allocate a memory region through the
 * memblock_alloc_node() on a NUMA node with id `nid`. Expected to have the
 * correct NUMA node set for the new region.
 */
static int alloc_node_on_correct_nid(void)
{
	int nid_req = 2;
	void *allocated_ptr = NULL;
#ifdef CONFIG_NUMA
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
#endif
	phys_addr_t size = SZ_512;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	allocated_ptr = memblock_alloc_node(size, SMP_CACHE_BYTES, nid_req);

	ASSERT_NE(allocated_ptr, NULL);
#ifdef CONFIG_NUMA
	ASSERT_EQ(nid_req, req_node->nid);
#endif

	test_pass_pop();

	return 0;
}

/* Test case wrappers for NUMA tests */
static int alloc_nid_numa_simple_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_simple_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_simple_check();

	return 0;
}

static int alloc_nid_numa_small_node_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_small_node_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_small_node_check();

	return 0;
}

static int alloc_nid_numa_node_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_node_reserved_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_node_reserved_check();

	return 0;
}

static int alloc_nid_numa_part_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_part_reserved_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_part_reserved_check();

	return 0;
}

static int alloc_nid_numa_part_reserved_fallback_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_part_reserved_fallback_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_part_reserved_fallback_check();

	return 0;
}

static int alloc_nid_numa_split_range_low_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_split_range_low_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_split_range_low_check();

	return 0;
}

static int alloc_nid_numa_split_range_high_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_split_range_high_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_split_range_high_check();

	return 0;
}

static int alloc_nid_numa_no_overlap_split_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_no_overlap_split_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_no_overlap_split_check();

	return 0;
}

static int alloc_nid_numa_no_overlap_low_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_no_overlap_low_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_no_overlap_low_check();

	return 0;
}

static int alloc_nid_numa_no_overlap_high_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_nid_top_down_numa_no_overlap_high_check();
	memblock_set_bottom_up(true);
	alloc_nid_bottom_up_numa_no_overlap_high_check();

	return 0;
}

static int alloc_nid_numa_large_region_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_numa_large_region_generic_check);
	run_bottom_up(alloc_nid_numa_large_region_generic_check);

	return 0;
}

static int alloc_nid_numa_reserved_full_merge_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_numa_reserved_full_merge_generic_check);
	run_bottom_up(alloc_nid_numa_reserved_full_merge_generic_check);

	return 0;
}

static int alloc_nid_numa_split_all_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_nid_numa_split_all_reserved_generic_check);
	run_bottom_up(alloc_nid_numa_split_all_reserved_generic_check);

	return 0;
}

static int alloc_node_numa_on_correct_nid(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_node_on_correct_nid);
	run_bottom_up(alloc_node_on_correct_nid);

	return 0;
}

int __memblock_alloc_nid_numa_checks(void)
{
	test_print("Running %s NUMA tests...\n",
		   get_memblock_alloc_nid_name(alloc_nid_test_flags));

	alloc_nid_numa_simple_check();
	alloc_nid_numa_small_node_check();
	alloc_nid_numa_node_reserved_check();
	alloc_nid_numa_part_reserved_check();
	alloc_nid_numa_part_reserved_fallback_check();
	alloc_nid_numa_split_range_low_check();
	alloc_nid_numa_split_range_high_check();

	alloc_nid_numa_no_overlap_split_check();
	alloc_nid_numa_no_overlap_low_check();
	alloc_nid_numa_no_overlap_high_check();
	alloc_nid_numa_large_region_check();
	alloc_nid_numa_reserved_full_merge_check();
	alloc_nid_numa_split_all_reserved_check();

	alloc_node_numa_on_correct_nid();

	return 0;
}

static int memblock_alloc_nid_checks_internal(int flags)
{
	alloc_nid_test_flags = flags;

	prefix_reset();
	prefix_push(get_memblock_alloc_nid_name(flags));

	reset_memblock_attributes();
	dummy_physical_memory_init();

	memblock_alloc_nid_range_checks();
	memblock_alloc_nid_numa_checks();

	dummy_physical_memory_cleanup();

	prefix_pop();

	return 0;
}

int memblock_alloc_nid_checks(void)
{
	memblock_alloc_nid_checks_internal(TEST_F_NONE);
	memblock_alloc_nid_checks_internal(TEST_F_RAW);

	return 0;
}

int memblock_alloc_exact_nid_range_checks(void)
{
	alloc_nid_test_flags = (TEST_F_RAW | TEST_F_EXACT);

	memblock_alloc_nid_range_checks();

	return 0;
}
