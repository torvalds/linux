#ifndef _ASM_SPARC64_TOPOLOGY_H
#define _ASM_SPARC64_TOPOLOGY_H

#ifdef CONFIG_SMP
#define topology_physical_package_id(cpu)	(cpu_data(cpu).proc_id)
#define topology_core_id(cpu)			(cpu_data(cpu).core_id)
#define topology_core_siblings(cpu)		(cpu_core_map[cpu])
#define topology_thread_siblings(cpu)		(per_cpu(cpu_sibling_map, cpu))
#define mc_capable()				(sparc64_multi_core)
#define smt_capable()				(sparc64_multi_core)
#endif /* CONFIG_SMP */

#include <asm-generic/topology.h>

#define cpu_coregroup_map(cpu)			(cpu_core_map[cpu])

#endif /* _ASM_SPARC64_TOPOLOGY_H */
