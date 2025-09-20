// SPDX-License-Identifier: GPL-2.0-or-later
#include "alloc_exact_nid_api.h"
#include "alloc_nid_api.h"

#define FUNC_NAME			"memblock_alloc_exact_nid_raw"

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

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * has enough memory to allocate a region of the requested size.
 * Expect to allocate an aligned region at the end of the requested node.
 */
static int alloc_exact_nid_top_down_numa_simple_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

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
static int alloc_exact_nid_top_down_numa_part_reserved_check(void)
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
	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, region_end(req_node) - size);
	ASSERT_LE(req_node->base, new_rgn->base);

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
static int alloc_exact_nid_top_down_numa_split_range_low_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

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
static int alloc_exact_nid_top_down_numa_no_overlap_split_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

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
 *  |     +-----+                                                    |
 *  |     | rgn |                                                    |
 *  +-----+-----+----------------------------------------------------+
 *
 * Expect to drop the lower limit and allocate a memory region that ends at
 * the end of the requested node.
 */
static int alloc_exact_nid_top_down_numa_no_overlap_low_check(void)
{
	int nid_req = 0;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

	ASSERT_EQ(new_rgn->size, size);
	ASSERT_EQ(new_rgn->base, region_end(req_node) - size);

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
static int alloc_exact_nid_bottom_up_numa_simple_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

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
static int alloc_exact_nid_bottom_up_numa_part_reserved_check(void)
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
	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

	ASSERT_EQ(new_rgn->size, total_size);
	ASSERT_EQ(new_rgn->base, req_node->base);
	ASSERT_LE(region_end(new_rgn), region_end(req_node));

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

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
static int alloc_exact_nid_bottom_up_numa_split_range_low_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

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
static int alloc_exact_nid_bottom_up_numa_no_overlap_split_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

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
 *  |-----+                                                          |
 *  | rgn |                                                          |
 *  +-----+----------------------------------------------------------+
 *
 * Expect to drop the lower limit and allocate a memory region that starts at
 * the beginning of the requested node.
 */
static int alloc_exact_nid_bottom_up_numa_no_overlap_low_check(void)
{
	int nid_req = 0;
	struct memblock_region *new_rgn = &memblock.reserved.regions[0];
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

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
 *  |   +-----+                            |
 *  |   | req |                            |
 *  +---+-----+----------------------------+
 *
 *  +---------+
 *  |   rgn   |
 *  +---------+
 *
 * Expect no allocation to happen.
 */
static int alloc_exact_nid_numa_small_node_generic_check(void)
{
	int nid_req = 1;
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	void *allocated_ptr = NULL;
	phys_addr_t size;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	size = SZ_2 * req_node->size;
	min_addr = memblock_start_of_DRAM();
	max_addr = memblock_end_of_DRAM();

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * is fully reserved:
 *
 *  |              +---------+             |
 *  |              |requested|             |
 *  +--------------+---------+-------------+
 *
 *  |              +---------+             |
 *  |              | reserved|             |
 *  +--------------+---------+-------------+
 *
 * Expect no allocation to happen.
 */
static int alloc_exact_nid_numa_node_reserved_generic_check(void)
{
	int nid_req = 2;
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
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
	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to allocate a memory region in a specific NUMA node that
 * is partially reserved and does not have enough contiguous memory for the
 * allocated region:
 *
 *  |           +-----------------------+    |
 *  |           |       requested       |    |
 *  +-----------+-----------------------+----+
 *
 *  |                 +----------+           |
 *  |                 | reserved |           |
 *  +-----------------+----------+-----------+
 *
 * Expect no allocation to happen.
 */
static int alloc_exact_nid_numa_part_reserved_fail_generic_check(void)
{
	int nid_req = 4;
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
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
	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_EQ(allocated_ptr, NULL);

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
 *  |      |        first node        |requested|                |
 *  +------+--------------------------+---------+----------------+
 *
 * Expect no allocation to happen.
 */
static int alloc_exact_nid_numa_split_range_high_generic_check(void)
{
	int nid_req = 3;
	struct memblock_region *req_node = &memblock.memory.regions[nid_req];
	void *allocated_ptr = NULL;
	phys_addr_t size = SZ_512;
	phys_addr_t min_addr;
	phys_addr_t max_addr;

	PREFIX_PUSH();
	setup_numa_memblock(node_fractions);

	min_addr = req_node->base - SZ_256;
	max_addr = min_addr + size;

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_EQ(allocated_ptr, NULL);

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
 *
 * Expect no allocation to happen.
 */
static int alloc_exact_nid_numa_no_overlap_high_generic_check(void)
{
	int nid_req = 7;
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_EQ(allocated_ptr, NULL);

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
static int alloc_exact_nid_numa_large_region_generic_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);
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
static int alloc_exact_nid_numa_reserved_full_merge_generic_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     nid_req);

	ASSERT_NE(allocated_ptr, NULL);
	ASSERT_MEM_NE(allocated_ptr, 0, size);

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
static int alloc_exact_nid_numa_split_all_reserved_generic_check(void)
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

	allocated_ptr = memblock_alloc_exact_nid_raw(size, SMP_CACHE_BYTES,
						     min_addr, max_addr,
						     NUMA_NO_NODE);

	ASSERT_EQ(allocated_ptr, NULL);

	test_pass_pop();

	return 0;
}

/* Test case wrappers for NUMA tests */
static int alloc_exact_nid_numa_simple_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_exact_nid_top_down_numa_simple_check();
	memblock_set_bottom_up(true);
	alloc_exact_nid_bottom_up_numa_simple_check();

	return 0;
}

static int alloc_exact_nid_numa_part_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_exact_nid_top_down_numa_part_reserved_check();
	memblock_set_bottom_up(true);
	alloc_exact_nid_bottom_up_numa_part_reserved_check();

	return 0;
}

static int alloc_exact_nid_numa_split_range_low_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_exact_nid_top_down_numa_split_range_low_check();
	memblock_set_bottom_up(true);
	alloc_exact_nid_bottom_up_numa_split_range_low_check();

	return 0;
}

static int alloc_exact_nid_numa_no_overlap_split_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_exact_nid_top_down_numa_no_overlap_split_check();
	memblock_set_bottom_up(true);
	alloc_exact_nid_bottom_up_numa_no_overlap_split_check();

	return 0;
}

static int alloc_exact_nid_numa_no_overlap_low_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	memblock_set_bottom_up(false);
	alloc_exact_nid_top_down_numa_no_overlap_low_check();
	memblock_set_bottom_up(true);
	alloc_exact_nid_bottom_up_numa_no_overlap_low_check();

	return 0;
}

static int alloc_exact_nid_numa_small_node_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_exact_nid_numa_small_node_generic_check);
	run_bottom_up(alloc_exact_nid_numa_small_node_generic_check);

	return 0;
}

static int alloc_exact_nid_numa_node_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_exact_nid_numa_node_reserved_generic_check);
	run_bottom_up(alloc_exact_nid_numa_node_reserved_generic_check);

	return 0;
}

static int alloc_exact_nid_numa_part_reserved_fail_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_exact_nid_numa_part_reserved_fail_generic_check);
	run_bottom_up(alloc_exact_nid_numa_part_reserved_fail_generic_check);

	return 0;
}

static int alloc_exact_nid_numa_split_range_high_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_exact_nid_numa_split_range_high_generic_check);
	run_bottom_up(alloc_exact_nid_numa_split_range_high_generic_check);

	return 0;
}

static int alloc_exact_nid_numa_no_overlap_high_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_exact_nid_numa_no_overlap_high_generic_check);
	run_bottom_up(alloc_exact_nid_numa_no_overlap_high_generic_check);

	return 0;
}

static int alloc_exact_nid_numa_large_region_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_exact_nid_numa_large_region_generic_check);
	run_bottom_up(alloc_exact_nid_numa_large_region_generic_check);

	return 0;
}

static int alloc_exact_nid_numa_reserved_full_merge_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_exact_nid_numa_reserved_full_merge_generic_check);
	run_bottom_up(alloc_exact_nid_numa_reserved_full_merge_generic_check);

	return 0;
}

static int alloc_exact_nid_numa_split_all_reserved_check(void)
{
	test_print("\tRunning %s...\n", __func__);
	run_top_down(alloc_exact_nid_numa_split_all_reserved_generic_check);
	run_bottom_up(alloc_exact_nid_numa_split_all_reserved_generic_check);

	return 0;
}

int __memblock_alloc_exact_nid_numa_checks(void)
{
	test_print("Running %s NUMA tests...\n", FUNC_NAME);

	alloc_exact_nid_numa_simple_check();
	alloc_exact_nid_numa_part_reserved_check();
	alloc_exact_nid_numa_split_range_low_check();
	alloc_exact_nid_numa_no_overlap_split_check();
	alloc_exact_nid_numa_no_overlap_low_check();

	alloc_exact_nid_numa_small_node_check();
	alloc_exact_nid_numa_node_reserved_check();
	alloc_exact_nid_numa_part_reserved_fail_check();
	alloc_exact_nid_numa_split_range_high_check();
	alloc_exact_nid_numa_no_overlap_high_check();
	alloc_exact_nid_numa_large_region_check();
	alloc_exact_nid_numa_reserved_full_merge_check();
	alloc_exact_nid_numa_split_all_reserved_check();

	return 0;
}

int memblock_alloc_exact_nid_checks(void)
{
	prefix_reset();
	prefix_push(FUNC_NAME);

	reset_memblock_attributes();
	dummy_physical_memory_init();

	memblock_alloc_exact_nid_range_checks();
	memblock_alloc_exact_nid_numa_checks();

	dummy_physical_memory_cleanup();

	prefix_pop();

	return 0;
}
