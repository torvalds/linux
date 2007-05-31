#ifndef _ASM_SPARC64_TOPOLOGY_H
#define _ASM_SPARC64_TOPOLOGY_H

#include <asm/spitfire.h>
#define smt_capable()	(tlb_type == hypervisor)

#include <asm-generic/topology.h>

#define topology_core_id(cpu)			(cpu_data(cpu).core_id)
#define topology_thread_siblings(cpu)		(cpu_sibling_map[cpu])

#endif /* _ASM_SPARC64_TOPOLOGY_H */
