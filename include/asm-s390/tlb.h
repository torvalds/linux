#ifndef _S390_TLB_H
#define _S390_TLB_H

/*
 * TLB flushing on s390 is complicated. The following requirement
 * from the principles of operation is the most arduous:
 *
 * "A valid table entry must not be changed while it is attached
 * to any CPU and may be used for translation by that CPU except to
 * (1) invalidate the entry by using INVALIDATE PAGE TABLE ENTRY,
 * or INVALIDATE DAT TABLE ENTRY, (2) alter bits 56-63 of a page
 * table entry, or (3) make a change by means of a COMPARE AND SWAP
 * AND PURGE instruction that purges the TLB."
 *
 * The modification of a pte of an active mm struct therefore is
 * a two step process: i) invalidate the pte, ii) store the new pte.
 * This is true for the page protection bit as well.
 * The only possible optimization is to flush at the beginning of
 * a tlb_gather_mmu cycle if the mm_struct is currently not in use.
 *
 * Pages used for the page tables is a different story. FIXME: more
 */

#include <linux/mm.h>
#include <linux/swap.h>
#include <asm/processor.h>
#include <asm/pgalloc.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

#ifndef CONFIG_SMP
#define TLB_NR_PTRS	1
#else
#define TLB_NR_PTRS	508
#endif

struct mmu_gather {
	struct mm_struct *mm;
	unsigned int fullmm;
	unsigned int nr_ptes;
	unsigned int nr_pmds;
	void *array[TLB_NR_PTRS];
};

DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

static inline struct mmu_gather *tlb_gather_mmu(struct mm_struct *mm,
						unsigned int full_mm_flush)
{
	struct mmu_gather *tlb = &get_cpu_var(mmu_gathers);

	tlb->mm = mm;
	tlb->fullmm = full_mm_flush || (num_online_cpus() == 1) ||
		(atomic_read(&mm->mm_users) <= 1 && mm == current->active_mm);
	tlb->nr_ptes = 0;
	tlb->nr_pmds = TLB_NR_PTRS;
	if (tlb->fullmm)
		__tlb_flush_mm(mm);
	return tlb;
}

static inline void tlb_flush_mmu(struct mmu_gather *tlb,
				 unsigned long start, unsigned long end)
{
	if (!tlb->fullmm && (tlb->nr_ptes > 0 || tlb->nr_pmds < TLB_NR_PTRS))
		__tlb_flush_mm(tlb->mm);
	while (tlb->nr_ptes > 0)
		pte_free(tlb->mm, tlb->array[--tlb->nr_ptes]);
	while (tlb->nr_pmds < TLB_NR_PTRS)
		pmd_free(tlb->mm, (pmd_t *) tlb->array[tlb->nr_pmds++]);
}

static inline void tlb_finish_mmu(struct mmu_gather *tlb,
				  unsigned long start, unsigned long end)
{
	tlb_flush_mmu(tlb, start, end);

	/* keep the page table cache within bounds */
	check_pgt_cache();

	put_cpu_var(mmu_gathers);
}

/*
 * Release the page cache reference for a pte removed by
 * tlb_ptep_clear_flush. In both flush modes the tlb fo a page cache page
 * has already been freed, so just do free_page_and_swap_cache.
 */
static inline void tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
	free_page_and_swap_cache(page);
}

/*
 * pte_free_tlb frees a pte table and clears the CRSTE for the
 * page table from the tlb.
 */
static inline void pte_free_tlb(struct mmu_gather *tlb, pgtable_t page)
{
	if (!tlb->fullmm) {
		tlb->array[tlb->nr_ptes++] = page;
		if (tlb->nr_ptes >= tlb->nr_pmds)
			tlb_flush_mmu(tlb, 0, 0);
	} else
		pte_free(tlb->mm, page);
}

/*
 * pmd_free_tlb frees a pmd table and clears the CRSTE for the
 * segment table entry from the tlb.
 */
static inline void pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd)
{
#ifdef __s390x__
	if (!tlb->fullmm) {
		tlb->array[--tlb->nr_pmds] = (struct page *) pmd;
		if (tlb->nr_ptes >= tlb->nr_pmds)
			tlb_flush_mmu(tlb, 0, 0);
	} else
		pmd_free(tlb->mm, pmd);
#endif
}

#define pud_free_tlb(tlb, pud)			do { } while (0)

#define tlb_start_vma(tlb, vma)			do { } while (0)
#define tlb_end_vma(tlb, vma)			do { } while (0)
#define tlb_remove_tlb_entry(tlb, ptep, addr)	do { } while (0)
#define tlb_migrate_finish(mm)			do { } while (0)

#endif /* _S390_TLB_H */
