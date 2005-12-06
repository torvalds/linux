#ifndef _ASM_POWERPC_SPARSEMEM_H
#define _ASM_POWERPC_SPARSEMEM_H 1

#ifdef CONFIG_SPARSEMEM
/*
 * SECTION_SIZE_BITS		2^N: how big each section will be
 * MAX_PHYSADDR_BITS		2^N: how much physical address space we have
 * MAX_PHYSMEM_BITS		2^N: how much memory we can have in that space
 */
#define SECTION_SIZE_BITS       24
#define MAX_PHYSADDR_BITS       44
#define MAX_PHYSMEM_BITS        44

#ifdef CONFIG_MEMORY_HOTPLUG
extern void create_section_mapping(unsigned long start, unsigned long end);
#endif /* CONFIG_MEMORY_HOTPLUG */

#endif /* CONFIG_SPARSEMEM */

#endif /* _ASM_POWERPC_SPARSEMEM_H */
