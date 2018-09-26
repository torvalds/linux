/* SPDX-License-Identifier: GPL-2.0 */
/*
 * include/linux/node.h - generic node definition
 *
 * This is mainly for topological representation. We define the 
 * basic 'struct node' here, which can be embedded in per-arch 
 * definitions of processors.
 *
 * Basic handling of the devices is done in drivers/base/node.c
 * and system devices are handled in drivers/base/sys.c. 
 *
 * Nodes are exported via driverfs in the class/node/devices/
 * directory. 
 */
#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/device.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>

struct node {
	struct device	dev;

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) && defined(CONFIG_HUGETLBFS)
	struct work_struct	node_work;
#endif
};

struct memory_block;
extern struct node *node_devices[];
typedef  void (*node_registration_func_t)(struct node *);

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) && defined(CONFIG_NUMA)
extern int link_mem_sections(int nid, unsigned long start_pfn,
			     unsigned long end_pfn);
#else
static inline int link_mem_sections(int nid, unsigned long start_pfn,
				    unsigned long end_pfn)
{
	return 0;
}
#endif

extern void unregister_node(struct node *node);
#ifdef CONFIG_NUMA
/* Core of the node registration - only memory hotplug should use this */
extern int __register_one_node(int nid);

/* Registers an online node */
static inline int register_one_node(int nid)
{
	int error = 0;

	if (node_online(nid)) {
		struct pglist_data *pgdat = NODE_DATA(nid);
		unsigned long start_pfn = pgdat->node_start_pfn;
		unsigned long end_pfn = start_pfn + pgdat->node_spanned_pages;

		error = __register_one_node(nid);
		if (error)
			return error;
		/* link memory sections under this node */
		error = link_mem_sections(nid, start_pfn, end_pfn);
	}

	return error;
}

extern void unregister_one_node(int nid);
extern int register_cpu_under_node(unsigned int cpu, unsigned int nid);
extern int unregister_cpu_under_node(unsigned int cpu, unsigned int nid);
extern int register_mem_sect_under_node(struct memory_block *mem_blk,
						void *arg);
extern int unregister_mem_sect_under_nodes(struct memory_block *mem_blk,
					   unsigned long phys_index);

#ifdef CONFIG_HUGETLBFS
extern void register_hugetlbfs_with_node(node_registration_func_t doregister,
					 node_registration_func_t unregister);
#endif
#else
static inline int __register_one_node(int nid)
{
	return 0;
}
static inline int register_one_node(int nid)
{
	return 0;
}
static inline int unregister_one_node(int nid)
{
	return 0;
}
static inline int register_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline int unregister_cpu_under_node(unsigned int cpu, unsigned int nid)
{
	return 0;
}
static inline int register_mem_sect_under_node(struct memory_block *mem_blk,
							void *arg)
{
	return 0;
}
static inline int unregister_mem_sect_under_nodes(struct memory_block *mem_blk,
						  unsigned long phys_index)
{
	return 0;
}

static inline void register_hugetlbfs_with_node(node_registration_func_t reg,
						node_registration_func_t unreg)
{
}
#endif

#define to_node(device) container_of(device, struct node, dev)

#endif /* _LINUX_NODE_H_ */
