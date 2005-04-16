#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H

#include <linux/config.h>

#ifdef CONFIG_DISCONTIGMEM
#include <asm/numnodes.h>
#endif

#ifndef NODES_SHIFT
#define NODES_SHIFT     0
#endif

#define MAX_NUMNODES    (1 << NODES_SHIFT)

#endif /* _LINUX_NUMA_H */
