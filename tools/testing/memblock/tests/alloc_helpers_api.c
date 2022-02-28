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

	phys_addr_t size = SZ_16;
	phys_addr_t min_addr;

	setup_memblock();

	min_addr = memblock_end_of_DRAM() - SMP_CACHE_BYTES;

	allocated_ptr = memblock_alloc_from(size, SMP_CACHE_BYTES, min_addr);
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

	phys_addr_t size = SZ_32;
	phys_addr_t min_addr;

	setup_memblock();

	/* A misaligned address */
	min_addr = memblock_end_of_DRAM() - (SMP_CACHE_BYTES * 2 - 1);

	allocated_ptr = memblock_alloc_from(size, SMP_CACHE_BYTES, min_addr);
	b = (char *)allocated_ptr;

	assert(allocated_ptr);
	assert(*b == 0);

	assert(rgn->size == size);
	assert(rgn->base == memblock_end_of_DRAM() - SMP_CACHE_BYTES);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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

	phys_addr_t size = SZ_32;
	phys_addr_t min_addr;

	setup_memblock();

	/* The address is too close to the end of the memory */
	min_addr = memblock_end_of_DRAM() - SZ_16;

	allocated_ptr = memblock_alloc_from(size, SMP_CACHE_BYTES, min_addr);

	assert(allocated_ptr);
	assert(rgn->size == size);
	assert(rgn->base == memblock_end_of_DRAM() - SMP_CACHE_BYTES);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == size);

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

	phys_addr_t r1_size = SZ_64;
	phys_addr_t r2_size = SZ_2;
	phys_addr_t total_size = r1_size + r2_size;
	phys_addr_t min_addr;

	setup_memblock();

	min_addr = memblock_end_of_DRAM() - SMP_CACHE_BYTES * 2;

	/* No space above this address */
	memblock_reserve(min_addr, r2_size);

	allocated_ptr = memblock_alloc_from(r1_size, SMP_CACHE_BYTES, min_addr);

	assert(allocated_ptr);
	assert(rgn->base == min_addr - r1_size);
	assert(rgn->size == total_size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == total_size);

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

	phys_addr_t r1_size = SZ_64;
	phys_addr_t min_addr;
	phys_addr_t start_addr;

	setup_memblock();

	start_addr = (phys_addr_t)memblock_start_of_DRAM();
	min_addr = start_addr - SMP_CACHE_BYTES * 3;

	memblock_reserve(start_addr + r1_size, MEM_SIZE - r1_size);

	allocated_ptr = memblock_alloc_from(r1_size, SMP_CACHE_BYTES, min_addr);

	assert(allocated_ptr);
	assert(rgn->base == start_addr);
	assert(rgn->size == MEM_SIZE);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == MEM_SIZE);

	return 0;
}

int memblock_alloc_helpers_checks(void)
{
	reset_memblock_attributes();
	dummy_physical_memory_init();

	alloc_from_simple_generic_check();
	alloc_from_misaligned_generic_check();
	alloc_from_top_down_high_addr_check();
	alloc_from_top_down_min_addr_cap_check();
	alloc_from_top_down_no_space_above_check();

	dummy_physical_memory_cleanup();

	return 0;
}
