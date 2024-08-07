// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/printk.h>
#include <linux/numa.h>

struct pglist_data *node_data[MAX_NUMNODES];
EXPORT_SYMBOL(node_data);

void __init alloc_offline_node_data(int nid)
{
	pg_data_t *pgdat;

	pgdat = memblock_alloc(sizeof(*pgdat), SMP_CACHE_BYTES);
	if (!pgdat)
		panic("Cannot allocate %zuB for node %d.\n",
		      sizeof(*pgdat), nid);

	node_data[nid] = pgdat;
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
