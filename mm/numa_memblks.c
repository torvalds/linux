// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/array_size.h>
#include <linux/sort.h>
#include <linux/printk.h>
#include <linux/memblock.h>
#include <linux/numa.h>
#include <linux/numa_memblks.h>

static int numa_distance_cnt;
static u8 *numa_distance;

nodemask_t numa_nodes_parsed __initdata;

static struct numa_meminfo numa_meminfo __initdata_or_meminfo;
static struct numa_meminfo numa_reserved_meminfo __initdata_or_meminfo;

/*
 * Set nodes, which have memory in @mi, in *@nodemask.
 */
static void __init numa_nodemask_from_meminfo(nodemask_t *nodemask,
					      const struct numa_meminfo *mi)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mi->blk); i++)
		if (mi->blk[i].start != mi->blk[i].end &&
		    mi->blk[i].nid != NUMA_NO_NODE)
			node_set(mi->blk[i].nid, *nodemask);
}

/**
 * numa_reset_distance - Reset NUMA distance table
 *
 * The current table is freed.  The next numa_set_distance() call will
 * create a new one.
 */
void __init numa_reset_distance(void)
{
	size_t size = numa_distance_cnt * numa_distance_cnt * sizeof(numa_distance[0]);

	/* numa_distance could be 1LU marking allocation failure, test cnt */
	if (numa_distance_cnt)
		memblock_free(numa_distance, size);
	numa_distance_cnt = 0;
	numa_distance = NULL;	/* enable table creation */
}

static int __init numa_alloc_distance(void)
{
	nodemask_t nodes_parsed;
	size_t size;
	int i, j, cnt = 0;

	/* size the new table and allocate it */
	nodes_parsed = numa_nodes_parsed;
	numa_nodemask_from_meminfo(&nodes_parsed, &numa_meminfo);

	for_each_node_mask(i, nodes_parsed)
		cnt = i;
	cnt++;
	size = cnt * cnt * sizeof(numa_distance[0]);

	numa_distance = memblock_alloc(size, PAGE_SIZE);
	if (!numa_distance) {
		pr_warn("Warning: can't allocate distance table!\n");
		/* don't retry until explicitly reset */
		numa_distance = (void *)1LU;
		return -ENOMEM;
	}

	numa_distance_cnt = cnt;

	/* fill with the default distances */
	for (i = 0; i < cnt; i++)
		for (j = 0; j < cnt; j++)
			numa_distance[i * cnt + j] = i == j ?
				LOCAL_DISTANCE : REMOTE_DISTANCE;
	printk(KERN_DEBUG "NUMA: Initialized distance table, cnt=%d\n", cnt);

	return 0;
}

/**
 * numa_set_distance - Set NUMA distance from one NUMA to another
 * @from: the 'from' node to set distance
 * @to: the 'to'  node to set distance
 * @distance: NUMA distance
 *
 * Set the distance from node @from to @to to @distance.  If distance table
 * doesn't exist, one which is large enough to accommodate all the currently
 * known nodes will be created.
 *
 * If such table cannot be allocated, a warning is printed and further
 * calls are ignored until the distance table is reset with
 * numa_reset_distance().
 *
 * If @from or @to is higher than the highest known node or lower than zero
 * at the time of table creation or @distance doesn't make sense, the call
 * is ignored.
 * This is to allow simplification of specific NUMA config implementations.
 */
void __init numa_set_distance(int from, int to, int distance)
{
	if (!numa_distance && numa_alloc_distance() < 0)
		return;

	if (from >= numa_distance_cnt || to >= numa_distance_cnt ||
			from < 0 || to < 0) {
		pr_warn_once("Warning: node ids are out of bound, from=%d to=%d distance=%d\n",
			     from, to, distance);
		return;
	}

	if ((u8)distance != distance ||
	    (from == to && distance != LOCAL_DISTANCE)) {
		pr_warn_once("Warning: invalid distance parameter, from=%d to=%d distance=%d\n",
			     from, to, distance);
		return;
	}

	numa_distance[from * numa_distance_cnt + to] = distance;
}

int __node_distance(int from, int to)
{
	if (from >= numa_distance_cnt || to >= numa_distance_cnt)
		return from == to ? LOCAL_DISTANCE : REMOTE_DISTANCE;
	return numa_distance[from * numa_distance_cnt + to];
}
EXPORT_SYMBOL(__node_distance);

static int __init numa_add_memblk_to(int nid, u64 start, u64 end,
				     struct numa_meminfo *mi)
{
	/* ignore zero length blks */
	if (start == end)
		return 0;

	/* whine about and ignore invalid blks */
	if (start > end || nid < 0 || nid >= MAX_NUMNODES) {
		pr_warn("Warning: invalid memblk node %d [mem %#010Lx-%#010Lx]\n",
			nid, start, end - 1);
		return 0;
	}

	if (mi->nr_blks >= NR_NODE_MEMBLKS) {
		pr_err("too many memblk ranges\n");
		return -EINVAL;
	}

	mi->blk[mi->nr_blks].start = start;
	mi->blk[mi->nr_blks].end = end;
	mi->blk[mi->nr_blks].nid = nid;
	mi->nr_blks++;
	return 0;
}

/**
 * numa_remove_memblk_from - Remove one numa_memblk from a numa_meminfo
 * @idx: Index of memblk to remove
 * @mi: numa_meminfo to remove memblk from
 *
 * Remove @idx'th numa_memblk from @mi by shifting @mi->blk[] and
 * decrementing @mi->nr_blks.
 */
void __init numa_remove_memblk_from(int idx, struct numa_meminfo *mi)
{
	mi->nr_blks--;
	memmove(&mi->blk[idx], &mi->blk[idx + 1],
		(mi->nr_blks - idx) * sizeof(mi->blk[0]));
}

/**
 * numa_move_tail_memblk - Move a numa_memblk from one numa_meminfo to another
 * @dst: numa_meminfo to append block to
 * @idx: Index of memblk to remove
 * @src: numa_meminfo to remove memblk from
 */
static void __init numa_move_tail_memblk(struct numa_meminfo *dst, int idx,
					 struct numa_meminfo *src)
{
	dst->blk[dst->nr_blks++] = src->blk[idx];
	numa_remove_memblk_from(idx, src);
}

/**
 * numa_add_memblk - Add one numa_memblk to numa_meminfo
 * @nid: NUMA node ID of the new memblk
 * @start: Start address of the new memblk
 * @end: End address of the new memblk
 *
 * Add a new memblk to the default numa_meminfo.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init numa_add_memblk(int nid, u64 start, u64 end)
{
	return numa_add_memblk_to(nid, start, end, &numa_meminfo);
}

/**
 * numa_cleanup_meminfo - Cleanup a numa_meminfo
 * @mi: numa_meminfo to clean up
 *
 * Sanitize @mi by merging and removing unnecessary memblks.  Also check for
 * conflicts and clear unused memblks.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int __init numa_cleanup_meminfo(struct numa_meminfo *mi)
{
	const u64 low = memblock_start_of_DRAM();
	const u64 high = memblock_end_of_DRAM();
	int i, j, k;

	/* first, trim all entries */
	for (i = 0; i < mi->nr_blks; i++) {
		struct numa_memblk *bi = &mi->blk[i];

		/* move / save reserved memory ranges */
		if (!memblock_overlaps_region(&memblock.memory,
					bi->start, bi->end - bi->start)) {
			numa_move_tail_memblk(&numa_reserved_meminfo, i--, mi);
			continue;
		}

		/* make sure all non-reserved blocks are inside the limits */
		bi->start = max(bi->start, low);

		/* preserve info for non-RAM areas above 'max_pfn': */
		if (bi->end > high) {
			numa_add_memblk_to(bi->nid, high, bi->end,
					   &numa_reserved_meminfo);
			bi->end = high;
		}

		/* and there's no empty block */
		if (bi->start >= bi->end)
			numa_remove_memblk_from(i--, mi);
	}

	/* merge neighboring / overlapping entries */
	for (i = 0; i < mi->nr_blks; i++) {
		struct numa_memblk *bi = &mi->blk[i];

		for (j = i + 1; j < mi->nr_blks; j++) {
			struct numa_memblk *bj = &mi->blk[j];
			u64 start, end;

			/*
			 * See whether there are overlapping blocks.  Whine
			 * about but allow overlaps of the same nid.  They
			 * will be merged below.
			 */
			if (bi->end > bj->start && bi->start < bj->end) {
				if (bi->nid != bj->nid) {
					pr_err("node %d [mem %#010Lx-%#010Lx] overlaps with node %d [mem %#010Lx-%#010Lx]\n",
					       bi->nid, bi->start, bi->end - 1,
					       bj->nid, bj->start, bj->end - 1);
					return -EINVAL;
				}
				pr_warn("Warning: node %d [mem %#010Lx-%#010Lx] overlaps with itself [mem %#010Lx-%#010Lx]\n",
					bi->nid, bi->start, bi->end - 1,
					bj->start, bj->end - 1);
			}

			/*
			 * Join together blocks on the same node, holes
			 * between which don't overlap with memory on other
			 * nodes.
			 */
			if (bi->nid != bj->nid)
				continue;
			start = min(bi->start, bj->start);
			end = max(bi->end, bj->end);
			for (k = 0; k < mi->nr_blks; k++) {
				struct numa_memblk *bk = &mi->blk[k];

				if (bi->nid == bk->nid)
					continue;
				if (start < bk->end && end > bk->start)
					break;
			}
			if (k < mi->nr_blks)
				continue;
			pr_info("NUMA: Node %d [mem %#010Lx-%#010Lx] + [mem %#010Lx-%#010Lx] -> [mem %#010Lx-%#010Lx]\n",
			       bi->nid, bi->start, bi->end - 1, bj->start,
			       bj->end - 1, start, end - 1);
			bi->start = start;
			bi->end = end;
			numa_remove_memblk_from(j--, mi);
		}
	}

	/* clear unused ones */
	for (i = mi->nr_blks; i < ARRAY_SIZE(mi->blk); i++) {
		mi->blk[i].start = mi->blk[i].end = 0;
		mi->blk[i].nid = NUMA_NO_NODE;
	}

	return 0;
}

/*
 * Mark all currently memblock-reserved physical memory (which covers the
 * kernel's own memory ranges) as hot-unswappable.
 */
static void __init numa_clear_kernel_node_hotplug(void)
{
	nodemask_t reserved_nodemask = NODE_MASK_NONE;
	struct memblock_region *mb_region;
	int i;

	/*
	 * We have to do some preprocessing of memblock regions, to
	 * make them suitable for reservation.
	 *
	 * At this time, all memory regions reserved by memblock are
	 * used by the kernel, but those regions are not split up
	 * along node boundaries yet, and don't necessarily have their
	 * node ID set yet either.
	 *
	 * So iterate over all parsed memory blocks and use those ranges to
	 * set the nid in memblock.reserved.  This will split up the
	 * memblock regions along node boundaries and will set the node IDs
	 * as well.
	 */
	for (i = 0; i < numa_meminfo.nr_blks; i++) {
		struct numa_memblk *mb = numa_meminfo.blk + i;
		int ret;

		ret = memblock_set_node(mb->start, mb->end - mb->start,
					&memblock.reserved, mb->nid);
		WARN_ON_ONCE(ret);
	}

	/*
	 * Now go over all reserved memblock regions, to construct a
	 * node mask of all kernel reserved memory areas.
	 *
	 * [ Note, when booting with mem=nn[kMG] or in a kdump kernel,
	 *   numa_meminfo might not include all memblock.reserved
	 *   memory ranges, because quirks such as trim_snb_memory()
	 *   reserve specific pages for Sandy Bridge graphics. ]
	 */
	for_each_reserved_mem_region(mb_region) {
		int nid = memblock_get_region_node(mb_region);

		if (nid != MAX_NUMNODES)
			node_set(nid, reserved_nodemask);
	}

	/*
	 * Finally, clear the MEMBLOCK_HOTPLUG flag for all memory
	 * belonging to the reserved node mask.
	 *
	 * Note that this will include memory regions that reside
	 * on nodes that contain kernel memory - entire nodes
	 * become hot-unpluggable:
	 */
	for (i = 0; i < numa_meminfo.nr_blks; i++) {
		struct numa_memblk *mb = numa_meminfo.blk + i;

		if (!node_isset(mb->nid, reserved_nodemask))
			continue;

		memblock_clear_hotplug(mb->start, mb->end - mb->start);
	}
}

static int __init numa_register_meminfo(struct numa_meminfo *mi)
{
	int i;

	/* Account for nodes with cpus and no memory */
	node_possible_map = numa_nodes_parsed;
	numa_nodemask_from_meminfo(&node_possible_map, mi);
	if (WARN_ON(nodes_empty(node_possible_map)))
		return -EINVAL;

	for (i = 0; i < mi->nr_blks; i++) {
		struct numa_memblk *mb = &mi->blk[i];

		memblock_set_node(mb->start, mb->end - mb->start,
				  &memblock.memory, mb->nid);
	}

	/*
	 * At very early time, the kernel have to use some memory such as
	 * loading the kernel image. We cannot prevent this anyway. So any
	 * node the kernel resides in should be un-hotpluggable.
	 *
	 * And when we come here, alloc node data won't fail.
	 */
	numa_clear_kernel_node_hotplug();

	/*
	 * If sections array is gonna be used for pfn -> nid mapping, check
	 * whether its granularity is fine enough.
	 */
	if (IS_ENABLED(NODE_NOT_IN_PAGE_FLAGS)) {
		unsigned long pfn_align = node_map_pfn_alignment();

		if (pfn_align && pfn_align < PAGES_PER_SECTION) {
			pr_warn("Node alignment %LuMB < min %LuMB, rejecting NUMA config\n",
				PFN_PHYS(pfn_align) >> 20,
				PFN_PHYS(PAGES_PER_SECTION) >> 20);
			return -EINVAL;
		}
	}

	return 0;
}

int __init numa_memblks_init(int (*init_func)(void),
			     bool memblock_force_top_down)
{
	int ret;

	nodes_clear(numa_nodes_parsed);
	nodes_clear(node_possible_map);
	nodes_clear(node_online_map);
	memset(&numa_meminfo, 0, sizeof(numa_meminfo));
	WARN_ON(memblock_set_node(0, ULLONG_MAX, &memblock.memory,
				  NUMA_NO_NODE));
	WARN_ON(memblock_set_node(0, ULLONG_MAX, &memblock.reserved,
				  NUMA_NO_NODE));
	/* In case that parsing SRAT failed. */
	WARN_ON(memblock_clear_hotplug(0, ULLONG_MAX));
	numa_reset_distance();

	ret = init_func();
	if (ret < 0)
		return ret;

	/*
	 * We reset memblock back to the top-down direction
	 * here because if we configured ACPI_NUMA, we have
	 * parsed SRAT in init_func(). It is ok to have the
	 * reset here even if we did't configure ACPI_NUMA
	 * or acpi numa init fails and fallbacks to dummy
	 * numa init.
	 */
	if (memblock_force_top_down)
		memblock_set_bottom_up(false);

	ret = numa_cleanup_meminfo(&numa_meminfo);
	if (ret < 0)
		return ret;

	numa_emulation(&numa_meminfo, numa_distance_cnt);

	return numa_register_meminfo(&numa_meminfo);
}

static int __init cmp_memblk(const void *a, const void *b)
{
	const struct numa_memblk *ma = *(const struct numa_memblk **)a;
	const struct numa_memblk *mb = *(const struct numa_memblk **)b;

	return (ma->start > mb->start) - (ma->start < mb->start);
}

static struct numa_memblk *numa_memblk_list[NR_NODE_MEMBLKS] __initdata;

/**
 * numa_fill_memblks - Fill gaps in numa_meminfo memblks
 * @start: address to begin fill
 * @end: address to end fill
 *
 * Find and extend numa_meminfo memblks to cover the physical
 * address range @start-@end
 *
 * RETURNS:
 * 0		  : Success
 * NUMA_NO_MEMBLK : No memblks exist in address range @start-@end
 */

int __init numa_fill_memblks(u64 start, u64 end)
{
	struct numa_memblk **blk = &numa_memblk_list[0];
	struct numa_meminfo *mi = &numa_meminfo;
	int count = 0;
	u64 prev_end;

	/*
	 * Create a list of pointers to numa_meminfo memblks that
	 * overlap start, end. The list is used to make in-place
	 * changes that fill out the numa_meminfo memblks.
	 */
	for (int i = 0; i < mi->nr_blks; i++) {
		struct numa_memblk *bi = &mi->blk[i];

		if (memblock_addrs_overlap(start, end - start, bi->start,
					   bi->end - bi->start)) {
			blk[count] = &mi->blk[i];
			count++;
		}
	}
	if (!count)
		return NUMA_NO_MEMBLK;

	/* Sort the list of pointers in memblk->start order */
	sort(&blk[0], count, sizeof(blk[0]), cmp_memblk, NULL);

	/* Make sure the first/last memblks include start/end */
	blk[0]->start = min(blk[0]->start, start);
	blk[count - 1]->end = max(blk[count - 1]->end, end);

	/*
	 * Fill any gaps by tracking the previous memblks
	 * end address and backfilling to it if needed.
	 */
	prev_end = blk[0]->end;
	for (int i = 1; i < count; i++) {
		struct numa_memblk *curr = blk[i];

		if (prev_end >= curr->start) {
			if (prev_end < curr->end)
				prev_end = curr->end;
		} else {
			curr->start = prev_end;
			prev_end = curr->end;
		}
	}
	return 0;
}
