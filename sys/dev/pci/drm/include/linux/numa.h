/* Public domain. */

#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H

#define NUMA_NO_NODE	(-1)

static inline int
dev_to_node(struct device *dev)
{
	return NUMA_NO_NODE;
}

#endif
