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
 * It checks if a new entry was created and if region counter and total memory
 * were correctly updated.
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
 * A simple test that adds a memory block of a specified base address, size
 * NUMA node and memory flags to the collection of available memory regions.
 * It checks if the new entry, region counter and total memory size have
 * expected values.
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
 * another. It checks if two correctly initialized entries were added to the
 * collection of available memory regions (memblock.memory) and if this
 * change was reflected in memblock.memory's total size and region counter.
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
 * A test that tries to add two memory blocks, where the second one overlaps
 * with the beginning of the first entry (that is r1.base < r2.base + r2.size).
 * After this, it checks if two entries are merged into one region that starts
 * at r2.base and has size of two regions minus their intersection. It also
 * verifies the reported total size of the available memory and region counter.
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
 * A test that tries to add two memory blocks, where the second one overlaps
 * with the end of the first entry (that is r2.base < r1.base + r1.size).
 * After this, it checks if two entries are merged into one region that starts
 * at r1.base and has size of two regions minus their intersection. It verifies
 * that memblock can still see only one entry and has a correct total size of
 * the available memory.
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
 * A test that tries to add two memory blocks, where the second one is
 * within the range of the first entry (that is r1.base < r2.base &&
 * r2.base + r2.size < r1.base + r1.size). It checks if two entries are merged
 * into one region that stays the same. The counter and total size of available
 * memory are expected to not be updated.
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
 * A simple test that tries to add the same memory block twice. The counter
 * and total size of available memory are expected to not be updated.
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
  * (memblock.reserved). It checks if a new entry was created and if region
  * counter and total memory size were correctly updated.
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
 * A test that tries to mark two memory blocks that don't overlap as reserved
 * and checks if two entries were correctly added to the collection of reserved
 * memory regions (memblock.reserved) and if this change was reflected in
 * memblock.reserved's total size and region counter.
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
 * A test that tries to mark two memory blocks as reserved, where the
 * second one overlaps with the beginning of the first (that is
 * r1.base < r2.base + r2.size).
 * It checks if two entries are merged into one region that starts at r2.base
 * and has size of two regions minus their intersection. The test also verifies
 * that memblock can still see only one entry and has a correct total size of
 * the reserved memory.
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
 * A test that tries to mark two memory blocks as reserved, where the
 * second one overlaps with the end of the first entry (that is
 * r2.base < r1.base + r1.size).
 * It checks if two entries are merged into one region that starts at r1.base
 * and has size of two regions minus their intersection. It verifies that
 * memblock can still see only one entry and has a correct total size of the
 * reserved memory.
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
 * A test that tries to mark two memory blocks as reserved, where the second
 * one is within the range of the first entry (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)).
 * It checks if two entries are merged into one region that stays the
 * same. The counter and total size of available memory are expected to not be
 * updated.
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
 * The region counter and total size of reserved memory are expected to not
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
  * A simple test that tries to remove the first entry of the array of
  * available memory regions. By "removing" a region we mean overwriting it
  * with the next region in memblock.memory. To check this is the case, the
  * test adds two memory blocks and verifies that the value of the latter
  * was used to erase r1 region.  It also checks if the region counter and
  * total size were updated to expected values.
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
  * A test that tries to remove a region that was not registered as available
  * memory (i.e. has no corresponding entry in memblock.memory). It verifies
  * that array, regions counter and total size were not modified.
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
 * A test that tries to remove a region which overlaps with the beginning of
 * the already existing entry r1 (that is r1.base < r2.base + r2.size). It
 * checks if only the intersection of both regions is removed from the available
 * memory pool. The test also checks if the regions counter and total size are
 * updated to expected values.
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
 * A test that tries to remove a region which overlaps with the end of the
 * first entry (that is r2.base < r1.base + r1.size). It checks if only the
 * intersection of both regions is removed from the available memory pool.
 * The test also checks if the regions counter and total size are updated to
 * expected values.
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
 * A test that tries to remove a region which is within the range of the
 * already existing entry (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)).
 * It checks if the region is split into two - one that ends at r2.base and
 * second that starts at r2.base + size, with appropriate sizes. The test
 * also checks if the region counter and total size were updated to
 * expected values.
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
 * A simple test that tries to free a memory block that was marked earlier
 * as reserved. By "freeing" a region we mean overwriting it with the next
 * entry in memblock.reserved. To check this is the case, the test reserves
 * two memory regions and verifies that the value of the latter was used to
 * erase r1 region.
 * The test also checks if the region counter and total size were updated.
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
  * A test that tries to free a region that was not marked as reserved
  * (i.e. has no corresponding entry in memblock.reserved). It verifies
  * that array, regions counter and total size were not modified.
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
 * A test that tries to free a region which overlaps with the beginning of
 * the already existing entry r1 (that is r1.base < r2.base + r2.size). It
 * checks if only the intersection of both regions is freed. The test also
 * checks if the regions counter and total size are updated to expected
 * values.
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
 * A test that tries to free a region which overlaps with the end of the
 * first entry (that is r2.base < r1.base + r1.size). It checks if only the
 * intersection of both regions is freed. The test also checks if the
 * regions counter and total size are updated to expected values.
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
 * A test that tries to free a region which is within the range of the
 * already existing entry (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)).
 * It checks if the region is split into two - one that ends at r2.base and
 * second that starts at r2.base + size, with appropriate sizes. It is
 * expected that the region counter and total size fields were updated t
 * reflect that change.
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
