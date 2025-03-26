/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NUMA_MEMBLKS_H
#define __NUMA_MEMBLKS_H

#ifdef CONFIG_NUMA_MEMBLKS
#include <linux/types.h>

#define NR_NODE_MEMBLKS		(MAX_NUMNODES * 2)

void __init numa_set_distance(int from, int to, int distance);
void __init numa_reset_distance(void);

struct numa_memblk {
	u64			start;
	u64			end;
	int			nid;
};

struct numa_meminfo {
	int			nr_blks;
	struct numa_memblk	blk[NR_NODE_MEMBLKS];
};

int __init numa_add_memblk(int nodeid, u64 start, u64 end);
void __init numa_remove_memblk_from(int idx, struct numa_meminfo *mi);

int __init numa_cleanup_meminfo(struct numa_meminfo *mi);

int __init numa_memblks_init(int (*init_func)(void),
			     bool memblock_force_top_down);

extern int numa_distance_cnt;

#ifdef CONFIG_NUMA_EMU
extern int emu_nid_to_phys[MAX_NUMNODES];
int numa_emu_cmdline(char *str);
void __init numa_emu_update_cpu_to_node(int *emu_nid_to_phys,
					unsigned int nr_emu_nids);
u64 __init numa_emu_dma_end(void);
void __init numa_emulation(struct numa_meminfo *numa_meminfo,
			   int numa_dist_cnt);
#else
static inline void numa_emulation(struct numa_meminfo *numa_meminfo,
				  int numa_dist_cnt)
{ }
static inline int numa_emu_cmdline(char *str)
{
	return -EINVAL;
}
#endif /* CONFIG_NUMA_EMU */

#ifdef CONFIG_NUMA_KEEP_MEMINFO
extern int phys_to_target_node(u64 start);
#define phys_to_target_node phys_to_target_node
extern int memory_add_physaddr_to_nid(u64 start);
#define memory_add_physaddr_to_nid memory_add_physaddr_to_nid
#endif /* CONFIG_NUMA_KEEP_MEMINFO */

#endif /* CONFIG_NUMA_MEMBLKS */

#endif	/* __NUMA_MEMBLKS_H */
