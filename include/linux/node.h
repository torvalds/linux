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

struct node {
	struct sys_device	sysdev;
};

extern struct node node_devices[];

extern int register_node(struct node *, int, struct node *);
extern void unregister_node(struct node *node);
#ifdef CONFIG_NUMA
extern int register_one_node(int nid);
extern void unregister_one_node(int nid);
extern int register_cpu_under_node(unsigned int cpu, unsigned int nid);
extern int unregister_cpu_under_node(unsigned int cpu, unsigned int nid);
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
#endif

#define to_node(sys_device) container_of(sys_device, struct node, sysdev)

#endif /* _LINUX_NODE_H_ */
