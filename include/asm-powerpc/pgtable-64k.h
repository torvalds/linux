#ifndef _ASM_POWERPC_PGTABLE_64K_H
#define _ASM_POWERPC_PGTABLE_64K_H
#ifdef __KERNEL__

#include <asm-generic/pgtable-nopud.h>


#define PTE_INDEX_SIZE  12
#define PMD_INDEX_SIZE  12
#define PUD_INDEX_SIZE	0
#define PGD_INDEX_SIZE  4

#define PTE_TABLE_SIZE	(sizeof(real_pte_t) << PTE_INDEX_SIZE)
#define PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* With 4k base page size, hugepage PTEs go at the PMD level */
#define MIN_HUGEPTE_SHIFT	PAGE_SHIFT

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* PGDIR_SHIFT determines what a third-level page table entry can map */
#define PGDIR_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* Additional PTE bits (don't change without checking asm in hash_low.S) */
#define _PAGE_HPTE_SUB	0x0ffff000 /* combo only: sub pages HPTE bits */
#define _PAGE_HPTE_SUB0	0x08000000 /* combo only: first sub page */
#define _PAGE_COMBO	0x10000000 /* this is a combo 4k page */
#define _PAGE_F_SECOND  0x00008000 /* full page: hidx bits */
#define _PAGE_F_GIX     0x00007000 /* full page: hidx bits */

/* PTE flags to conserve for HPTE identification */
#define _PAGE_HPTEFLAGS (_PAGE_BUSY | _PAGE_HASHPTE | _PAGE_HPTE_SUB |\
                         _PAGE_COMBO)

/* Shift to put page number into pte.
 *
 * That gives us a max RPN of 32 bits, which means a max of 48 bits
 * of addressable physical space.
 * We could get 3 more bits here by setting PTE_RPN_SHIFT to 29 but
 * 32 makes PTEs more readable for debugging for now :)
 */
#define PTE_RPN_SHIFT	(32)
#define PTE_RPN_MAX	(1UL << (64 - PTE_RPN_SHIFT))
#define PTE_RPN_MASK	(~((1UL<<PTE_RPN_SHIFT)-1))

/* _PAGE_CHG_MASK masks of bits that are to be preserved accross
 * pgprot changes
 */
#define _PAGE_CHG_MASK	(PTE_RPN_MASK | _PAGE_HPTEFLAGS | _PAGE_DIRTY | \
                         _PAGE_ACCESSED)

/* Bits to mask out from a PMD to get to the PTE page */
#define PMD_MASKED_BITS		0x1ff
/* Bits to mask out from a PGD/PUD to get to the PMD page */
#define PUD_MASKED_BITS		0x1ff

#ifndef __ASSEMBLY__

/* Manipulate "rpte" values */
#define __real_pte(e,p) 	((real_pte_t) { \
	(e), pte_val(*((p) + PTRS_PER_PTE)) })
#define __rpte_to_hidx(r,index)	((pte_val((r).pte) & _PAGE_COMBO) ? \
        (((r).hidx >> ((index)<<2)) & 0xf) : ((pte_val((r).pte) >> 12) & 0xf))
#define __rpte_to_pte(r)	((r).pte)
#define __rpte_sub_valid(rpte, index) \
	(pte_val(rpte.pte) & (_PAGE_HPTE_SUB0 >> (index)))


/* Trick: we set __end to va + 64k, which happens works for
 * a 16M page as well as we want only one iteration
 */
#define pte_iterate_hashed_subpages(rpte, psize, va, index, shift)	    \
        do {                                                                \
                unsigned long __end = va + PAGE_SIZE;                       \
                unsigned __split = (psize == MMU_PAGE_4K ||                 \
				    psize == MMU_PAGE_64K_AP);              \
                shift = mmu_psize_defs[psize].shift;                        \
	        for (index = 0; va < __end; index++, va += (1 << shift)) {  \
		        if (!__split || __rpte_sub_valid(rpte, index)) do { \

#define pte_iterate_hashed_end() } while(0); } } while(0)


#endif /*  __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PGTABLE_64K_H */
