/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H
#include <linux/types.h>

#ifdef CONFIG_NODES_SHIFT
#define NODES_SHIFT     CONFIG_NODES_SHIFT
#else
#define NODES_SHIFT     0
#endif

#define MAX_NUMNODES    (1 << NODES_SHIFT)

#define	NUMA_NO_NODE	(-1)
#define	NUMA_NO_MEMBLK	(-1)

/* optionally keep NUMA memory info available post init */
#ifdef CONFIG_NUMA_KEEP_MEMINFO
#define __initdata_or_meminfo
#else
#define __initdata_or_meminfo __initdata
#endif

#ifdef CONFIG_NUMA
#include <linux/printk.h>
#include <asm/sparsemem.h>

/* Generic implementation available */
int numa_nearest_node(int node, unsigned int state);

#ifndef memory_add_physaddr_to_nid
static inline int memory_add_physaddr_to_nid(u64 start)
{
	pr_info_once("Unknown online node for memory at 0x%llx, assuming node 0\n",
			start);
	return 0;
}
#endif
#ifndef phys_to_target_node
static inline int phys_to_target_node(u64 start)
{
	pr_info_once("Unknown target node for memory at 0x%llx, assuming node 0\n",
			start);
	return 0;
}
#endif
#ifndef numa_fill_memblks
static inline int __init numa_fill_memblks(u64 start, u64 end)
{
	return NUMA_NO_MEMBLK;
}
#endif
#else /* !CONFIG_NUMA */
static inline int numa_nearest_node(int node, unsigned int state)
{
	return NUMA_NO_NODE;
}

static inline int memory_add_physaddr_to_nid(u64 start)
{
	return 0;
}
static inline int phys_to_target_node(u64 start)
{
	return 0;
}
#endif

#define numa_map_to_online_node(node) numa_nearest_node(node, N_ONLINE)

#ifdef CONFIG_HAVE_ARCH_NODE_DEV_GROUP
extern const struct attribute_group arch_node_dev_group;
#endif

#endif /* _LINUX_NUMA_H */
