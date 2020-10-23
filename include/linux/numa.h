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

/* optionally keep NUMA memory info available post init */
#ifdef CONFIG_NUMA_KEEP_MEMINFO
#define __initdata_or_meminfo
#else
#define __initdata_or_meminfo __initdata
#endif

#ifdef CONFIG_NUMA
/* Generic implementation available */
int numa_map_to_online_node(int node);
#else
static inline int numa_map_to_online_node(int node)
{
	return NUMA_NO_NODE;
}
#endif

#endif /* _LINUX_NUMA_H */
