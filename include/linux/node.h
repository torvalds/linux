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
 *
 * Per-node interfaces can be implemented using a struct device_interface. 
 * See the following for how to do this: 
 * - drivers/base/intf.c 
 * - Documentation/driver-model/interface.txt
 */
#ifndef _LINUX_NODE_H_
#define _LINUX_NODE_H_

#include <linux/sysdev.h>
#include <linux/cpumask.h>
#include <linux/workqueue.h>

struct node {
	struct sys_device	sysdev;

#if defined(CONFIG_MEMORY_HOTPLUG_SPARSE) && defined(CONFIG_HUGETLBFS)
	struct work_struct	node_work;
#endif
};

struct memory_block;
extern struct node node_devices[];
typedef  void (*node_registration_func_t)(struct node *);

extern int register_node(struct node *, int, struct node *);
extern void unregister_node(struct node *node);
#ifdef CONFIG_NUMA
extern int register_one_node(int nid);
extern void unregister_one_node(int nid);
extern int register_cpu_under_node(unsigned int cpu, unsigned int nid);
extern int unregister_cpu_under_node(unsigned int cpu, unsigned int nid);
extern int register_mem_sect_under_node(struct memory_block *mem_blk,
						int nid);
extern int unregister_mem_sect_under_nodes(struct memory_block *mem_blk);

#ifdef CONFIG_HUGETLBFS
extern void register_hugetlbfs_with_node(node_registration_func_t doregister,
					 node_registration_func_t unregister);
#endif
#else
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
							int nid)
{
	return 0;
}
static inline int unregister_mem_sect_under_nodes(struct memory_block *mem_blk)
{
	return 0;
}

static inline void register_hugetlbfs_with_node(node_registration_func_t reg,
						node_registration_func_t unreg)
{
}
#endif

#define to_node(sys_device) container_of(sys_device, struct node, sysdev)

#endif /* _LINUX_NODE_H_ */
