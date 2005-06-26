#ifndef _I386_SPARSEMEM_H
#define _I386_SPARSEMEM_H
#ifdef CONFIG_SPARSEMEM

/*
 * generic non-linear memory support:
 *
 * 1) we will not split memory into more chunks than will fit into the
 *    flags field of the struct page
 */

/*
 * SECTION_SIZE_BITS		2^N: how big each section will be
 * MAX_PHYSADDR_BITS		2^N: how much physical address space we have
 * MAX_PHYSMEM_BITS		2^N: how much memory we can have in that space
 */
#ifdef CONFIG_X86_PAE
#define SECTION_SIZE_BITS       30
#define MAX_PHYSADDR_BITS       36
#define MAX_PHYSMEM_BITS	36
#else
#define SECTION_SIZE_BITS       26
#define MAX_PHYSADDR_BITS       32
#define MAX_PHYSMEM_BITS	32
#endif

/* XXX: FIXME -- wli */
#define kern_addr_valid(kaddr)  (0)

#endif /* CONFIG_SPARSEMEM */
#endif /* _I386_SPARSEMEM_H */
