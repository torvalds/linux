#ifndef _ASM_TOPOLOGY_H
#define _ASM_TOPOLOGY_H

#ifdef CONFIG_NUMA

#error NUMA not supported yet

#else /* !CONFIG_NUMA */

#include <asm-generic/topology.h>

#endif /* CONFIG_NUMA */

#endif /* _ASM_TOPOLOGY_H */
