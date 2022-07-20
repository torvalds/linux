// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include <linux/memblock.h>
#include "basic_api.h"

#define EXPECTED_MEMBLOCK_REGIONS			128

static int memblock_initialization_check(void)
{
	assert(memblock.memory.regions);
	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.max == EXPECTED_MEMBLOCK_REGIONS);
	assert(strcmp(memblock.memory.name, "memory") == 0);

	assert(memblock.reserved.regions);
	assert(memblock.reserved.cnt == 1);
	assert(memblock.memory.max == EXPECTED_MEMBLOCK_REGIONS);
	assert(strcmp(memblock.reserved.name, "reserved") == 0);

	assert(!memblock.bottom_up);
	assert(memblock.current_limit == MEMBLOCK_ALLOC_ANYWHERE);

	return 0;
}

/*
 * A simple test that adds a memory block of a specified base address
 * and size to the collection of available memory regions (memblock.memory).
 * Expect to create a new entry. The region counter and total memory get
 * updated.
 */
static int memblock_add_simple_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r = {
		.base = SZ_1G,
		.size = SZ_4M
	};

	reset_memblock_regions();
	memblock_add(r.base, r.size);

	assert(rgn->base == r.base);
	assert(rgn->size == r.size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == r.size);

	return 0;
}

/*
 * A simple test that adds a memory block of a specified base address, size,
 * NUMA node and memory flags to the collection of available memory regions.
 * Expect to create a new entry. The region counter and total memory get
 * updated.
 */
static int memblock_add_node_simple_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r = {
		.base = SZ_1M,
		.size = SZ_16M
	};

	reset_memblock_regions();
	memblock_add_node(r.base, r.size, 1, MEMBLOCK_HOTPLUG);

	assert(rgn->base == r.base);
	assert(rgn->size == r.size);
#ifdef CONFIG_NUMA
	assert(rgn->nid == 1);
#endif
	assert(rgn->flags == MEMBLOCK_HOTPLUG);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == r.size);

	return 0;
}

/*
 * A test that tries to add two memory blocks that don't overlap with one
 * another:
 *
 *  |        +--------+        +--------+  |
 *  |        |   r1   |        |   r2   |  |
 *  +--------+--------+--------+--------+--+
 *
 * Expect to add two correctly initialized entries to the collection of
 * available memory regions (memblock.memory). The total size and
 * region counter fields get updated.
 */
static int memblock_add_disjoint_check(void)
{
	struct memblock_region *rgn1, *rgn2;

	rgn1 = &memblock.memory.regions[0];
	rgn2 = &memblock.memory.regions[1];

	struct region r1 = {
		.base = SZ_1G,
		.size = SZ_8K
	};
	struct region r2 = {
		.base = SZ_1G + SZ_16K,
		.size = SZ_8K
	};

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);

	assert(rgn1->base == r1.base);
	assert(rgn1->size == r1.size);

	assert(rgn2->base == r2.base);
	assert(rgn2->size == r2.size);

	assert(memblock.memory.cnt == 2);
	assert(memblock.memory.total_size == r1.size + r2.size);

	return 0;
}

/*
 * A test that tries to add two memory blocks r1 and r2, where r2 overlaps
 * with the beginning of r1 (that is r1.base < r2.base + r2.size):
 *
 *  |    +----+----+------------+          |
 *  |    |    |r2  |   r1       |          |
 *  +----+----+----+------------+----------+
 *       ^    ^
 *       |    |
 *       |    r1.base
 *       |
 *       r2.base
 *
 * Expect to merge the two entries into one region that starts at r2.base
 * and has size of two regions minus their intersection. The total size of
 * the available memory is updated, and the region counter stays the same.
 */
static int memblock_add_overlap_top_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_512M,
		.size = SZ_1G
	};
	struct region r2 = {
		.base = SZ_256M,
		.size = SZ_512M
	};

	total_size = (r1.base - r2.base) + r1.size;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);

	assert(rgn->base == r2.base);
	assert(rgn->size == total_size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == total_size);

	return 0;
}

/*
 * A test that tries to add two memory blocks r1 and r2, where r2 overlaps
 * with the end of r1 (that is r2.base < r1.base + r1.size):
 *
 *  |  +--+------+----------+              |
 *  |  |  | r1   | r2       |              |
 *  +--+--+------+----------+--------------+
 *     ^  ^
 *     |  |
 *     |  r2.base
 *     |
 *     r1.base
 *
 * Expect to merge the two entries into one region that starts at r1.base
 * and has size of two regions minus their intersection. The total size of
 * the available memory is updated, and the region counter stays the same.
 */
static int memblock_add_overlap_bottom_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_128M,
		.size = SZ_512M
	};
	struct region r2 = {
		.base = SZ_256M,
		.size = SZ_1G
	};

	total_size = (r2.base - r1.base) + r2.size;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);

	assert(rgn->base == r1.base);
	assert(rgn->size == total_size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == total_size);

	return 0;
}

/*
 * A test that tries to add two memory blocks r1 and r2, where r2 is
 * within the range of r1 (that is r1.base < r2.base &&
 * r2.base + r2.size < r1.base + r1.size):
 *
 *  |   +-------+--+-----------------------+
 *  |   |       |r2|      r1               |
 *  +---+-------+--+-----------------------+
 *      ^
 *      |
 *      r1.base
 *
 * Expect to merge two entries into one region that stays the same.
 * The counter and total size of available memory are not updated.
 */
static int memblock_add_within_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_8M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_16M,
		.size = SZ_1M
	};

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);

	assert(rgn->base == r1.base);
	assert(rgn->size == r1.size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == r1.size);

	return 0;
}

/*
 * A simple test that tries to add the same memory block twice. Expect
 * the counter and total size of available memory to not be updated.
 */
static int memblock_add_twice_check(void)
{
	struct region r = {
		.base = SZ_16K,
		.size = SZ_2M
	};

	reset_memblock_regions();

	memblock_add(r.base, r.size);
	memblock_add(r.base, r.size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == r.size);

	return 0;
}

static int memblock_add_checks(void)
{
	memblock_add_simple_check();
	memblock_add_node_simple_check();
	memblock_add_disjoint_check();
	memblock_add_overlap_top_check();
	memblock_add_overlap_bottom_check();
	memblock_add_within_check();
	memblock_add_twice_check();

	return 0;
}

/*
 * A simple test that marks a memory block of a specified base address
 * and size as reserved and to the collection of reserved memory regions
 * (memblock.reserved). Expect to create a new entry. The region counter
 * and total memory size are updated.
 */
static int memblock_reserve_simple_check(void)
{
	struct memblock_region *rgn;

	rgn =  &memblock.reserved.regions[0];

	struct region r = {
		.base = SZ_2G,
		.size = SZ_128M
	};

	reset_memblock_regions();
	memblock_reserve(r.base, r.size);

	assert(rgn->base == r.base);
	assert(rgn->size == r.size);

	return 0;
}

/*
 * A test that tries to mark two memory blocks that don't overlap as reserved:
 *
 *  |        +--+      +----------------+  |
 *  |        |r1|      |       r2       |  |
 *  +--------+--+------+----------------+--+
 *
 * Expect to add two entries to the collection of reserved memory regions
 * (memblock.reserved). The total size and region counter for
 * memblock.reserved are updated.
 */
static int memblock_reserve_disjoint_check(void)
{
	struct memblock_region *rgn1, *rgn2;

	rgn1 = &memblock.reserved.regions[0];
	rgn2 = &memblock.reserved.regions[1];

	struct region r1 = {
		.base = SZ_256M,
		.size = SZ_16M
	};
	struct region r2 = {
		.base = SZ_512M,
		.size = SZ_512M
	};

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	assert(rgn1->base == r1.base);
	assert(rgn1->size == r1.size);

	assert(rgn2->base == r2.base);
	assert(rgn2->size == r2.size);

	assert(memblock.reserved.cnt == 2);
	assert(memblock.reserved.total_size == r1.size + r2.size);

	return 0;
}

/*
 * A test that tries to mark two memory blocks r1 and r2 as reserved,
 * where r2 overlaps with the beginning of r1 (that is
 * r1.base < r2.base + r2.size):
 *
 *  |  +--------------+--+--------------+  |
 *  |  |       r2     |  |     r1       |  |
 *  +--+--------------+--+--------------+--+
 *     ^              ^
 *     |              |
 *     |              r1.base
 *     |
 *     r2.base
 *
 * Expect to merge two entries into one region that starts at r2.base and
 * has size of two regions minus their intersection. The total size of the
 * reserved memory is updated, and the region counter is not updated.
 */
static int memblock_reserve_overlap_top_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_1G,
		.size = SZ_1G
	};
	struct region r2 = {
		.base = SZ_128M,
		.size = SZ_1G
	};

	total_size = (r1.base - r2.base) + r1.size;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	assert(rgn->base == r2.base);
	assert(rgn->size == total_size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == total_size);

	return 0;
}

/*
 * A test that tries to mark two memory blocks r1 and r2 as reserved,
 * where r2 overlaps with the end of r1 (that is
 * r2.base < r1.base + r1.size):
 *
 *  |  +--------------+--+--------------+  |
 *  |  |       r1     |  |     r2       |  |
 *  +--+--------------+--+--------------+--+
 *     ^              ^
 *     |              |
 *     |              r2.base
 *     |
 *     r1.base
 *
 * Expect to merge two entries into one region that starts at r1.base and
 * has size of two regions minus their intersection. The total size of the
 * reserved memory is updated, and the region counter is not updated.
 */
static int memblock_reserve_overlap_bottom_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_2K,
		.size = SZ_128K
	};
	struct region r2 = {
		.base = SZ_128K,
		.size = SZ_128K
	};

	total_size = (r2.base - r1.base) + r2.size;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	assert(rgn->base == r1.base);
	assert(rgn->size == total_size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == total_size);

	return 0;
}

/*
 * A test that tries to mark two memory blocks r1 and r2 as reserved,
 * where r2 is within the range of r1 (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)):
 *
 *  | +-----+--+---------------------------|
 *  | |     |r2|          r1               |
 *  +-+-----+--+---------------------------+
 *    ^     ^
 *    |     |
 *    |     r2.base
 *    |
 *    r1.base
 *
 * Expect to merge two entries into one region that stays the same. The
 * counter and total size of available memory are not updated.
 */
static int memblock_reserve_within_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_1M,
		.size = SZ_8M
	};
	struct region r2 = {
		.base = SZ_2M,
		.size = SZ_64K
	};

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	assert(rgn->base == r1.base);
	assert(rgn->size == r1.size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == r1.size);

	return 0;
}

/*
 * A simple test that tries to reserve the same memory block twice.
 * Expect the region counter and total size of reserved memory to not
 * be updated.
 */
static int memblock_reserve_twice_check(void)
{
	struct region r = {
		.base = SZ_16K,
		.size = SZ_2M
	};

	reset_memblock_regions();

	memblock_reserve(r.base, r.size);
	memblock_reserve(r.base, r.size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == r.size);

	return 0;
}

static int memblock_reserve_checks(void)
{
	memblock_reserve_simple_check();
	memblock_reserve_disjoint_check();
	memblock_reserve_overlap_top_check();
	memblock_reserve_overlap_bottom_check();
	memblock_reserve_within_check();
	memblock_reserve_twice_check();

	return 0;
}

/*
 * A simple test that tries to remove a region r1 from the array of
 * available memory regions. By "removing" a region we mean overwriting it
 * with the next region r2 in memblock.memory:
 *
 *  |  ......          +----------------+  |
 *  |  : r1 :          |       r2       |  |
 *  +--+----+----------+----------------+--+
 *                     ^
 *                     |
 *                     rgn.base
 *
 * Expect to add two memory blocks r1 and r2 and then remove r1 so that
 * r2 is the first available region. The region counter and total size
 * are updated.
 */
static int memblock_remove_simple_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_2K,
		.size = SZ_4K
	};
	struct region r2 = {
		.base = SZ_128K,
		.size = SZ_4M
	};

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);
	memblock_remove(r1.base, r1.size);

	assert(rgn->base == r2.base);
	assert(rgn->size == r2.size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == r2.size);

	return 0;
}

/*
 * A test that tries to remove a region r2 that was not registered as
 * available memory (i.e. has no corresponding entry in memblock.memory):
 *
 *                     +----------------+
 *                     |       r2       |
 *                     +----------------+
 *  |  +----+                              |
 *  |  | r1 |                              |
 *  +--+----+------------------------------+
 *     ^
 *     |
 *     rgn.base
 *
 * Expect the array, regions counter and total size to not be modified.
 */
static int memblock_remove_absent_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_512K,
		.size = SZ_4M
	};
	struct region r2 = {
		.base = SZ_64M,
		.size = SZ_1G
	};

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	assert(rgn->base == r1.base);
	assert(rgn->size == r1.size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == r1.size);

	return 0;
}

/*
 * A test that tries to remove a region r2 that overlaps with the
 * beginning of the already existing entry r1
 * (that is r1.base < r2.base + r2.size):
 *
 *           +-----------------+
 *           |       r2        |
 *           +-----------------+
 *  |                 .........+--------+  |
 *  |                 :     r1 |  rgn   |  |
 *  +-----------------+--------+--------+--+
 *                    ^        ^
 *                    |        |
 *                    |        rgn.base
 *                    r1.base
 *
 * Expect that only the intersection of both regions is removed from the
 * available memory pool. The regions counter and total size are updated.
 */
static int memblock_remove_overlap_top_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t r1_end, r2_end, total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_32M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_16M,
		.size = SZ_32M
	};

	r1_end = r1.base + r1.size;
	r2_end = r2.base + r2.size;
	total_size = r1_end - r2_end;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	assert(rgn->base == r1.base + r2.base);
	assert(rgn->size == total_size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == total_size);

	return 0;
}

/*
 * A test that tries to remove a region r2 that overlaps with the end of
 * the already existing region r1 (that is r2.base < r1.base + r1.size):
 *
 *        +--------------------------------+
 *        |               r2               |
 *        +--------------------------------+
 *  | +---+.....                           |
 *  | |rgn| r1 :                           |
 *  +-+---+----+---------------------------+
 *    ^
 *    |
 *    r1.base
 *
 * Expect that only the intersection of both regions is removed from the
 * available memory pool. The regions counter and total size are updated.
 */
static int memblock_remove_overlap_bottom_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_2M,
		.size = SZ_64M
	};
	struct region r2 = {
		.base = SZ_32M,
		.size = SZ_256M
	};

	total_size = r2.base - r1.base;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	assert(rgn->base == r1.base);
	assert(rgn->size == total_size);

	assert(memblock.memory.cnt == 1);
	assert(memblock.memory.total_size == total_size);
	return 0;
}

/*
 * A test that tries to remove a region r2 that is within the range of
 * the already existing entry r1 (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)):
 *
 *                  +----+
 *                  | r2 |
 *                  +----+
 *  | +-------------+....+---------------+ |
 *  | |     rgn1    | r1 |     rgn2      | |
 *  +-+-------------+----+---------------+-+
 *    ^
 *    |
 *    r1.base
 *
 * Expect that the region is split into two - one that ends at r2.base and
 * another that starts at r2.base + r2.size, with appropriate sizes. The
 * region counter and total size are updated.
 */
static int memblock_remove_within_check(void)
{
	struct memblock_region *rgn1, *rgn2;
	phys_addr_t r1_size, r2_size, total_size;

	rgn1 = &memblock.memory.regions[0];
	rgn2 = &memblock.memory.regions[1];

	struct region r1 = {
		.base = SZ_1M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_16M,
		.size = SZ_1M
	};

	r1_size = r2.base - r1.base;
	r2_size = (r1.base + r1.size) - (r2.base + r2.size);
	total_size = r1_size + r2_size;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	assert(rgn1->base == r1.base);
	assert(rgn1->size == r1_size);

	assert(rgn2->base == r2.base + r2.size);
	assert(rgn2->size == r2_size);

	assert(memblock.memory.cnt == 2);
	assert(memblock.memory.total_size == total_size);

	return 0;
}

static int memblock_remove_checks(void)
{
	memblock_remove_simple_check();
	memblock_remove_absent_check();
	memblock_remove_overlap_top_check();
	memblock_remove_overlap_bottom_check();
	memblock_remove_within_check();

	return 0;
}

/*
 * A simple test that tries to free a memory block r1 that was marked
 * earlier as reserved. By "freeing" a region we mean overwriting it with
 * the next entry r2 in memblock.reserved:
 *
 *  |              ......           +----+ |
 *  |              : r1 :           | r2 | |
 *  +--------------+----+-----------+----+-+
 *                                  ^
 *                                  |
 *                                  rgn.base
 *
 * Expect to reserve two memory regions and then erase r1 region with the
 * value of r2. The region counter and total size are updated.
 */
static int memblock_free_simple_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_4M,
		.size = SZ_1M
	};
	struct region r2 = {
		.base = SZ_8M,
		.size = SZ_1M
	};

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);
	memblock_free((void *)r1.base, r1.size);

	assert(rgn->base == r2.base);
	assert(rgn->size == r2.size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == r2.size);

	return 0;
}

/*
 * A test that tries to free a region r2 that was not marked as reserved
 * (i.e. has no corresponding entry in memblock.reserved):
 *
 *                     +----------------+
 *                     |       r2       |
 *                     +----------------+
 *  |  +----+                              |
 *  |  | r1 |                              |
 *  +--+----+------------------------------+
 *     ^
 *     |
 *     rgn.base
 *
 * The array, regions counter and total size are not modified.
 */
static int memblock_free_absent_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_2M,
		.size = SZ_8K
	};
	struct region r2 = {
		.base = SZ_16M,
		.size = SZ_128M
	};

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	assert(rgn->base == r1.base);
	assert(rgn->size == r1.size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == r1.size);

	return 0;
}

/*
 * A test that tries to free a region r2 that overlaps with the beginning
 * of the already existing entry r1 (that is r1.base < r2.base + r2.size):
 *
 *     +----+
 *     | r2 |
 *     +----+
 *  |    ...+--------------+               |
 *  |    :  |    r1        |               |
 *  +----+--+--------------+---------------+
 *       ^  ^
 *       |  |
 *       |  rgn.base
 *       |
 *       r1.base
 *
 * Expect that only the intersection of both regions is freed. The
 * regions counter and total size are updated.
 */
static int memblock_free_overlap_top_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_8M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_1M,
		.size = SZ_8M
	};

	total_size = (r1.size + r1.base) - (r2.base + r2.size);

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	assert(rgn->base == r2.base + r2.size);
	assert(rgn->size == total_size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == total_size);

	return 0;
}

/*
 * A test that tries to free a region r2 that overlaps with the end of
 * the already existing entry r1 (that is r2.base < r1.base + r1.size):
 *
 *                   +----------------+
 *                   |       r2       |
 *                   +----------------+
 *  |    +-----------+.....                |
 *  |    |       r1  |    :                |
 *  +----+-----------+----+----------------+
 *
 * Expect that only the intersection of both regions is freed. The
 * regions counter and total size are updated.
 */
static int memblock_free_overlap_bottom_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_8M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_32M,
		.size = SZ_32M
	};

	total_size = r2.base - r1.base;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	assert(rgn->base == r1.base);
	assert(rgn->size == total_size);

	assert(memblock.reserved.cnt == 1);
	assert(memblock.reserved.total_size == total_size);

	return 0;
}

/*
 * A test that tries to free a region r2 that is within the range of the
 * already existing entry r1 (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)):
 *
 *                    +----+
 *                    | r2 |
 *                    +----+
 *  |    +------------+....+---------------+
 *  |    |    rgn1    | r1 |     rgn2      |
 *  +----+------------+----+---------------+
 *       ^
 *       |
 *       r1.base
 *
 * Expect that the region is split into two - one that ends at r2.base and
 * another that starts at r2.base + r2.size, with appropriate sizes. The
 * region counter and total size fields are updated.
 */
static int memblock_free_within_check(void)
{
	struct memblock_region *rgn1, *rgn2;
	phys_addr_t r1_size, r2_size, total_size;

	rgn1 = &memblock.reserved.regions[0];
	rgn2 = &memblock.reserved.regions[1];

	struct region r1 = {
		.base = SZ_1M,
		.size = SZ_8M
	};
	struct region r2 = {
		.base = SZ_4M,
		.size = SZ_1M
	};

	r1_size = r2.base - r1.base;
	r2_size = (r1.base + r1.size) - (r2.base + r2.size);
	total_size = r1_size + r2_size;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	assert(rgn1->base == r1.base);
	assert(rgn1->size == r1_size);

	assert(rgn2->base == r2.base + r2.size);
	assert(rgn2->size == r2_size);

	assert(memblock.reserved.cnt == 2);
	assert(memblock.reserved.total_size == total_size);

	return 0;
}

static int memblock_free_checks(void)
{
	memblock_free_simple_check();
	memblock_free_absent_check();
	memblock_free_overlap_top_check();
	memblock_free_overlap_bottom_check();
	memblock_free_within_check();

	return 0;
}

int memblock_basic_checks(void)
{
	memblock_initialization_check();
	memblock_add_checks();
	memblock_reserve_checks();
	memblock_remove_checks();
	memblock_free_checks();

	return 0;
}
