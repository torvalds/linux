#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H

#include <linux/config.h>

#ifdef CONFIG_NODES_SHIFT
#define NODES_SHIFT     CONFIG_NODES_SHIFT
#else
#define NODES_SHIFT     0
#endif

#define MAX_NUMNODES    (1 << NODES_SHIFT)

#endif /* _LINUX_NUMA_H */
