// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/memblock.h>
#include <linux/printk.h>
#include <linux/numa.h>
#include <linux/numa_memblks.h>

struct pglist_data *node_data[MAX_NUMNODES];
EXPORT_SYMBOL(node_data);

/* Allocate NODE_DATA for a node on the local memory */
void __init alloc_node_data(int nid)
{
	const size_t nd_size = roundup(sizeof(pg_data_t), SMP_CACHE_BYTES);
	u64 nd_pa;
	void *nd;
	int tnid;

	/* Allocate node data.  Try node-local memory and then any node. */
	nd_pa = memblock_phys_alloc_try_nid(nd_size, SMP_CACHE_BYTES, nid);
	if (!nd_pa)
		panic("Cannot allocate %zu bytes for node %d data\n",
		      nd_size, nid);
	nd = __va(nd_pa);

	/* report and initialize */
	pr_info("NODE_DATA(%d) allocated [mem %#010Lx-%#010Lx]\n", nid,
		nd_pa, nd_pa + nd_size - 1);
	tnid = early_pfn_to_nid(nd_pa >> PAGE_SHIFT);
	if (tnid != nid)
		pr_info("    NODE_DATA(%d) on node %d\n", nid, tnid);

	node_data[nid] = nd;
	memset(NODE_DATA(nid), 0, sizeof(pg_data_t));
}

void __init alloc_offline_node_data(int nid)
{
	pg_data_t *pgdat;
	node_data[nid] = memblock_alloc_or_panic(sizeof(*pgdat), SMP_CACHE_BYTES);
}

/* Stub functions: */

#ifndef memory_add_physaddr_to_nid
int memory_add_physaddr_to_nid(u64 start)
{
	pr_info_once("Unknown online node for memory at 0x%llx, assuming node 0\n",
			start);
	return 0;
}
EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif

#ifndef phys_to_target_node
int phys_to_target_node(u64 start)
{
	pr_info_once("Unknown target node for memory at 0x%llx, assuming node 0\n",
			start);
	return 0;
}
EXPORT_SYMBOL_GPL(phys_to_target_node);
#endif
