/*
 * Entries per page directory level.  The PTE level must use a 64b record
 * for each page table entry.  The PMD and PGD level use a 32b record for
 * each entry by assuming that each entry is page aligned.
 */
#define PTE_INDEX_SIZE  9
#define PMD_INDEX_SIZE  7
#define PUD_INDEX_SIZE  7
#define PGD_INDEX_SIZE  9

#define PTE_TABLE_SIZE	(sizeof(pte_t) << PTE_INDEX_SIZE)
#define PMD_TABLE_SIZE	(sizeof(pmd_t) << PMD_INDEX_SIZE)
#define PUD_TABLE_SIZE	(sizeof(pud_t) << PUD_INDEX_SIZE)
#define PGD_TABLE_SIZE	(sizeof(pgd_t) << PGD_INDEX_SIZE)

#define PTRS_PER_PTE	(1 << PTE_INDEX_SIZE)
#define PTRS_PER_PMD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PUD	(1 << PMD_INDEX_SIZE)
#define PTRS_PER_PGD	(1 << PGD_INDEX_SIZE)

/* PMD_SHIFT determines what a second-level page table entry can map */
#define PMD_SHIFT	(PAGE_SHIFT + PTE_INDEX_SIZE)
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE-1))

/* With 4k base page size, hugepage PTEs go at the PMD level */
#define MIN_HUGEPTE_SHIFT	PMD_SHIFT

/* PUD_SHIFT determines what a third-level page table entry can map */
#define PUD_SHIFT	(PMD_SHIFT + PMD_INDEX_SIZE)
#define PUD_SIZE	(1UL << PUD_SHIFT)
#define PUD_MASK	(~(PUD_SIZE-1))

/* PGDIR_SHIFT determines what a fourth-level page table entry can map */
#define PGDIR_SHIFT	(PUD_SHIFT + PUD_INDEX_SIZE)
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/* PTE bits */
#define _PAGE_SECONDARY 0x8000 /* software: HPTE is in secondary group */
#define _PAGE_GROUP_IX  0x7000 /* software: HPTE index within group */
#define _PAGE_F_SECOND  _PAGE_SECONDARY
#define _PAGE_F_GIX     _PAGE_GROUP_IX

/* PTE flags to conserve for HPTE identification */
#define _PAGE_HPTEFLAGS (_PAGE_BUSY | _PAGE_HASHPTE | \
			 _PAGE_SECONDARY | _PAGE_GROUP_IX)

/* PAGE_MASK gives the right answer below, but only by accident */
/* It should be preserving the high 48 bits and then specifically */
/* preserving _PAGE_SECONDARY | _PAGE_GROUP_IX */
#define _PAGE_CHG_MASK	(PAGE_MASK | _PAGE_ACCESSED | _PAGE_DIRTY | \
                         _PAGE_HPTEFLAGS)

/* Bits to mask out from a PMD to get to the PTE page */
#define PMD_MASKED_BITS		0
/* Bits to mask out from a PUD to get to the PMD page */
#define PUD_MASKED_BITS		0
/* Bits to mask out from a PGD to get to the PUD page */
#define PGD_MASKED_BITS		0

/* shift to put page number into pte */
#define PTE_RPN_SHIFT	(17)

#ifdef STRICT_MM_TYPECHECKS
#define __real_pte(e,p)		((real_pte_t){(e)})
#define __rpte_to_pte(r)	((r).pte)
#else
#define __real_pte(e,p)		(e)
#define __rpte_to_pte(r)	(__pte(r))
#endif
#define __rpte_to_hidx(r,index)	(pte_val(__rpte_to_pte(r)) >> 12)

#define pte_iterate_hashed_subpages(rpte, psize, va, index, shift)       \
	do {							         \
		index = 0;					         \
		shift = mmu_psize_defs[psize].shift;		         \

#define pte_iterate_hashed_end() } while(0)

#define pte_pagesize_index(pte)	MMU_PAGE_4K

/*
 * 4-level page tables related bits
 */

#define pgd_none(pgd)		(!pgd_val(pgd))
#define pgd_bad(pgd)		(pgd_val(pgd) == 0)
#define pgd_present(pgd)	(pgd_val(pgd) != 0)
#define pgd_clear(pgdp)		(pgd_val(*(pgdp)) = 0)
#define pgd_page_vaddr(pgd)	(pgd_val(pgd) & ~PGD_MASKED_BITS)
#define pgd_page(pgd)		virt_to_page(pgd_page_vaddr(pgd))

#define pud_offset(pgdp, addr)	\
  (((pud_t *) pgd_page_vaddr(*(pgdp))) + \
    (((addr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1)))

#define pud_ERROR(e) \
	printk("%s:%d: bad pud %08lx.\n", __FILE__, __LINE__, pud_val(e))

#define remap_4k_pfn(vma, addr, pfn, prot)	\
	remap_pfn_range((vma), (addr), (pfn), PAGE_SIZE, (prot))
