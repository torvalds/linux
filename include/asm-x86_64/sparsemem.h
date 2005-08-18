#ifndef _ASM_X86_64_SPARSEMEM_H
#define _ASM_X86_64_SPARSEMEM_H 1

#ifdef CONFIG_SPARSEMEM

/*
 * generic non-linear memory support:
 *
 * 1) we will not split memory into more chunks than will fit into the flags
 *    field of the struct page
 *
 * SECTION_SIZE_BITS		2^n: size of each section
 * MAX_PHYSADDR_BITS		2^n: max size of physical address space
 * MAX_PHYSMEM_BITS		2^n: how much memory we can have in that space
 *
 */

#define SECTION_SIZE_BITS	27 /* matt - 128 is convenient right now */
#define MAX_PHYSADDR_BITS	40
#define MAX_PHYSMEM_BITS	40

extern int early_pfn_to_nid(unsigned long pfn);

#endif /* CONFIG_SPARSEMEM */

#endif /* _ASM_X86_64_SPARSEMEM_H */
