#ifndef _ASM_S390_TOPOLOGY_H
#define _ASM_S390_TOPOLOGY_H

#include <linux/cpumask.h>

#define mc_capable()	(1)

cpumask_t cpu_coregroup_map(unsigned int cpu);

#ifdef CONFIG_SMP
void s390_init_cpu_topology(void);
#else
static inline void s390_init_cpu_topology(void)
{
};
#endif

#include <asm-generic/topology.h>

#endif /* _ASM_S390_TOPOLOGY_H */
