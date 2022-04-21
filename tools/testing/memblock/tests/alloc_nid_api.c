// SPDX-License-Identifier: GPL-2.0-or-later
#include "alloc_nid_api.h"

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
 * Expect to allocate a cleared region that ends at max_addr.
 */
static int alloc_try_nid_top_down_simple_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_128;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SMP_CACHE_BYTES * 2;
	max_addr = min_addr + SZ_512;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;
	rgn_end = rgn->base + rgn->size;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == max_addr - size);
	assert(rgn_end == max_addr);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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
 * Expect to allocate a cleared, aligned region that ends before max_addr.
 */
static int alloc_try_nid_top_down_end_misaligned_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_128;
	phys_addr_t misalign = SZ_2;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SMP_CACHE_BYTES * 2;
	max_addr = min_addr + SZ_512 + misalign;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;
	rgn_end = rgn->base + rgn->size;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == max_addr - size - misalign);
	assert(rgn_end < max_addr);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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
 * Expect to allocate a cleared region that starts at min_addr and ends at
 * max_addr, given that min_addr is aligned.
 */
static int alloc_try_nid_exact_address_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_1K;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SMP_CACHE_BYTES;
	max_addr = min_addr + size;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;
	rgn_end = rgn->base + rgn->size;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == min_addr);
	assert(rgn_end == max_addr);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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
 * Expect to drop the lower limit and allocate a cleared memory region which
 * ends at max_addr (if the address is aligned).
 */
static int alloc_try_nid_top_down_narrow_range_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_256;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SZ_512;
	max_addr = min_addr + SMP_CACHE_BYTES;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == max_addr - size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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
static int alloc_try_nid_low_max_generic_check(void)
{
	void *allocated_ptr = NULL;

	phys_addr_t size = SZ_1K;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	setup_memblock();

	min_addr = memblock_start_of_DRAM();
	max_addr = min_addr + SMP_CACHE_BYTES;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);

	assert(!allocated_ptr);

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
static int alloc_try_nid_min_reserved_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t r1_size = SZ_128;
	phys_addr_t r2_size = SZ_64;
	phys_addr_t total_size = r1_size + r2_size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t reserved_base;

	setup_memblock();

	max_addr = memblock_end_of_DRAM();
	min_addr = max_addr - r2_size;
	reserved_base = min_addr - r1_size;

	memblock_reserve(reserved_base, r1_size);

	allocated_ptr = memblock_alloc_try_nid(r2_size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == total_size);
	assert(rgn->base == reserved_base);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == total_size);

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
static int alloc_try_nid_max_reserved_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t r1_size = SZ_64;
	phys_addr_t r2_size = SZ_128;
	phys_addr_t total_size = r1_size + r2_size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	setup_memblock();

	max_addr = memblock_end_of_DRAM() - r1_size;
	min_addr = max_addr - r2_size;

	memblock_reserve(max_addr, r1_size);

	allocated_ptr = memblock_alloc_try_nid(r2_size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == total_size);
	assert(rgn->base == min_addr);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == total_size);

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

static int alloc_try_nid_top_down_reserved_with_space_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[1];
	struct memblock_region *rgn2 = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;
	struct region r1, r2;

	phys_addr_t r3_size = SZ_64;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;
	r1.size = SMP_CACHE_BYTES;

	r2.size = SZ_128;
	r2.base = r1.base - (r3_size + gap_size + r2.size);

	total_size = r1.size + r2.size + r3_size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = memblock_alloc_try_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn1->size == r1.size + r3_size);
	assert(rgn1->base == max_addr - r3_size);

	assert(rgn2->size == r2.size);
	assert(rgn2->base == r2.base);

	assert(memblock.reserved.cnt == 2);
	assert(memblock.reserved.total_size == total_size);

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
static int alloc_try_nid_reserved_full_merge_generic_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;
	struct region r1, r2;

	phys_addr_t r3_size = SZ_64;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;
	r1.size = SMP_CACHE_BYTES;

	r2.size = SZ_128;
	r2.base = r1.base - (r3_size + r2.size);

	total_size = r1.size + r2.size + r3_size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = memblock_alloc_try_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == total_size);
	assert(rgn->base == r2.base);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == total_size);

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
static int alloc_try_nid_top_down_reserved_no_space_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[1];
	struct memblock_region *rgn2 = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;
	struct region r1, r2;

	phys_addr_t r3_size = SZ_256;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

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

	allocated_ptr = memblock_alloc_try_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn1->size == r1.size);
	assert(rgn1->base == r1.base);

	assert(rgn2->size == r2.size + r3_size);
	assert(rgn2->base == r2.base - r3_size);

	assert(memblock.reserved.cnt == 2);
	assert(memblock.reserved.total_size == total_size);

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

static int alloc_try_nid_reserved_all_generic_check(void)
{
	void *allocated_ptr = NULL;
	struct region r1, r2;

	phys_addr_t r3_size = SZ_256;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES;
	r1.size = SMP_CACHE_BYTES;

	r2.size = MEM_SIZE - (r1.size + gap_size);
	r2.base = memblock_start_of_DRAM();

	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = memblock_alloc_try_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);

	assert(!allocated_ptr);

	return 0;
}

/*
 * A test that tries to allocate a memory region, where max_addr is
 * bigger than the end address of the available memory. Expect to allocate
 * a cleared region that ends before the end of the memory.
 */
static int alloc_try_nid_top_down_cap_max_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_256;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	setup_memblock();

	min_addr = memblock_end_of_DRAM() - SZ_1K;
	max_addr = memblock_end_of_DRAM() + SZ_256;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == memblock_end_of_DRAM() - size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

	return 0;
}

/*
 * A test that tries to allocate a memory region, where min_addr is
 * smaller than the start address of the available memory. Expect to allocate
 * a cleared region that ends before the end of the memory.
 */
static int alloc_try_nid_top_down_cap_min_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_1K;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() - SZ_256;
	max_addr = memblock_end_of_DRAM();

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr, NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == memblock_end_of_DRAM() - size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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
 * Expect to allocate a cleared region that ends before max_addr.
 */
static int alloc_try_nid_bottom_up_simple_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_128;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SMP_CACHE_BYTES * 2;
	max_addr = min_addr + SZ_512;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	b = (char *)allocated_ptr;
	rgn_end = rgn->base + rgn->size;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == min_addr);
	assert(rgn_end < max_addr);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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
 * Expect to allocate a cleared, aligned region that ends before max_addr.
 */
static int alloc_try_nid_bottom_up_start_misaligned_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_128;
	phys_addr_t misalign = SZ_2;
	phys_addr_t min_addr;
	phys_addr_t max_addr;
	phys_addr_t rgn_end;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + misalign;
	max_addr = min_addr + SZ_512;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	b = (char *)allocated_ptr;
	rgn_end = rgn->base + rgn->size;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == min_addr + (SMP_CACHE_BYTES - misalign));
	assert(rgn_end < max_addr);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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
 * Expect to drop the lower limit and allocate a cleared memory region which
 * starts at the beginning of the available memory.
 */
static int alloc_try_nid_bottom_up_narrow_range_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_256;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SZ_512;
	max_addr = min_addr + SMP_CACHE_BYTES;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == memblock_start_of_DRAM());

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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

static int alloc_try_nid_bottom_up_reserved_with_space_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[1];
	struct memblock_region *rgn2 = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;
	struct region r1, r2;

	phys_addr_t r3_size = SZ_64;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

	setup_memblock();

	r1.base = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;
	r1.size = SMP_CACHE_BYTES;

	r2.size = SZ_128;
	r2.base = r1.base - (r3_size + gap_size + r2.size);

	total_size = r1.size + r2.size + r3_size;
	min_addr = r2.base + r2.size;
	max_addr = r1.base;

	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	allocated_ptr = memblock_alloc_try_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn1->size == r1.size);
	assert(rgn1->base == max_addr);

	assert(rgn2->size == r2.size + r3_size);
	assert(rgn2->base == r2.base);

	assert(memblock.reserved.cnt == 2);
	assert(memblock.reserved.total_size == total_size);

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

static int alloc_try_nid_bottom_up_reserved_no_space_check(void)
{
	struct memblock_region *rgn1 = &memblock.reserved.regions[2];
	struct memblock_region *rgn2 = &memblock.reserved.regions[1];
	struct memblock_region *rgn3 = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;
	struct region r1, r2;

	phys_addr_t r3_size = SZ_256;
	phys_addr_t gap_size = SMP_CACHE_BYTES;
	phys_addr_t total_size;
	phys_addr_t max_addr;
	phys_addr_t min_addr;

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

	allocated_ptr = memblock_alloc_try_nid(r3_size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn3->size == r3_size);
	assert(rgn3->base == memblock_start_of_DRAM());

	assert(rgn2->size == r2.size);
	assert(rgn2->base == r2.base);

	assert(rgn1->size == r1.size);
	assert(rgn1->base == r1.base);

	assert(memblock.reserved.cnt == 3);
	assert(memblock.reserved.total_size == total_size);

	return 0;
}

/*
 * A test that tries to allocate a memory region, where max_addr is
 * bigger than the end address of the available memory. Expect to allocate
 * a cleared region that starts at the min_addr
 */
static int alloc_try_nid_bottom_up_cap_max_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_256;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	setup_memblock();

	min_addr = memblock_start_of_DRAM() + SZ_1K;
	max_addr = memblock_end_of_DRAM() + SZ_256;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == min_addr);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

	return 0;
}

/*
 * A test that tries to allocate a memory region, where min_addr is
 * smaller than the start address of the available memory. Expect to allocate
 * a cleared region at the beginning of the available memory.
 */
static int alloc_try_nid_bottom_up_cap_min_check(void)
{
	struct memblock_region *rgn = &memblock.reserved.regions[0];
	void *allocated_ptr = NULL;
	char *b;

	phys_addr_t size = SZ_1K;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	setup_memblock();

	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM() - SZ_256;

	allocated_ptr = memblock_alloc_try_nid(size, SMP_CACHE_BYTES,
					       min_addr, max_addr,
					       NUMA_NO_NODE);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == memblock_start_of_DRAM());

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

	return 0;
}

/* Test case wrappers */
static int alloc_try_nid_simple_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_top_down_simple_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_bottom_up_simple_check();

	return 0;
}

static int alloc_try_nid_misaligned_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_top_down_end_misaligned_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_bottom_up_start_misaligned_check();

	return 0;
}

static int alloc_try_nid_narrow_range_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_top_down_narrow_range_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_bottom_up_narrow_range_check();

	return 0;
}

static int alloc_try_nid_reserved_with_space_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_top_down_reserved_with_space_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_bottom_up_reserved_with_space_check();

	return 0;
}

static int alloc_try_nid_reserved_no_space_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_top_down_reserved_no_space_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_bottom_up_reserved_no_space_check();

	return 0;
}

static int alloc_try_nid_cap_max_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_top_down_cap_max_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_bottom_up_cap_max_check();

	return 0;
}

static int alloc_try_nid_cap_min_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_top_down_cap_min_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_bottom_up_cap_min_check();

	return 0;
}

static int alloc_try_nid_min_reserved_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_min_reserved_generic_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_min_reserved_generic_check();

	return 0;
}

static int alloc_try_nid_max_reserved_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_max_reserved_generic_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_max_reserved_generic_check();

	return 0;
}

static int alloc_try_nid_exact_address_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_exact_address_generic_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_exact_address_generic_check();

	return 0;
}

static int alloc_try_nid_reserved_full_merge_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_reserved_full_merge_generic_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_reserved_full_merge_generic_check();

	return 0;
}

static int alloc_try_nid_reserved_all_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_reserved_all_generic_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_reserved_all_generic_check();

	return 0;
}

static int alloc_try_nid_low_max_check(void)
{
	memblock_set_bottom_up(false);
	alloc_try_nid_low_max_generic_check();
	memblock_set_bottom_up(true);
	alloc_try_nid_low_max_generic_check();

	return 0;
}

int memblock_alloc_nid_checks(void)
{
	reset_memblock_attributes();
	dummy_physical_memory_init();

	alloc_try_nid_simple_check();
	alloc_try_nid_misaligned_check();
	alloc_try_nid_narrow_range_check();
	alloc_try_nid_reserved_with_space_check();
	alloc_try_nid_reserved_no_space_check();
	alloc_try_nid_cap_max_check();
	alloc_try_nid_cap_min_check();

	alloc_try_nid_min_reserved_check();
	alloc_try_nid_max_reserved_check();
	alloc_try_nid_exact_address_check();
	alloc_try_nid_reserved_full_merge_check();
	alloc_try_nid_reserved_all_check();
	alloc_try_nid_low_max_check();

	dummy_physical_memory_cleanup();

	return 0;
}
