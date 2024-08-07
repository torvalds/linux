/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NUMA_MEMBLKS_H
#define __NUMA_MEMBLKS_H

#ifdef CONFIG_NUMA_MEMBLKS
#include <linux/types.h>

#define NR_NODE_MEMBLKS		(MAX_NUMNODES * 2)

struct numa_memblk {
	u64			start;
	u64			end;
	int			nid;
};

struct numa_meminfo {
	int			nr_blks;
	struct numa_memblk	blk[NR_NODE_MEMBLKS];
};

extern struct numa_meminfo numa_meminfo __initdata_or_meminfo;
extern struct numa_meminfo numa_reserved_meminfo __initdata_or_meminfo;

int __init numa_add_memblk(int nodeid, u64 start, u64 end);
void __init numa_remove_memblk_from(int idx, struct numa_meminfo *mi);

int __init numa_cleanup_meminfo(struct numa_meminfo *mi);
int __init numa_register_meminfo(struct numa_meminfo *mi);

void __init numa_nodemask_from_meminfo(nodemask_t *nodemask,
				       const struct numa_meminfo *mi);

#endif /* CONFIG_NUMA_MEMBLKS */

#endif	/* __NUMA_MEMBLKS_H */
