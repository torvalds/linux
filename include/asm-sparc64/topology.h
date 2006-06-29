#ifndef _ASM_SPARC64_TOPOLOGY_H
#define _ASM_SPARC64_TOPOLOGY_H

#include <asm/spitfire.h>
#define smt_capable()	(tlb_type == hypervisor)

#include <asm-generic/topology.h>

#endif /* _ASM_SPARC64_TOPOLOGY_H */
