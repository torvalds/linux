#ifndef _SPARC64_TLB_H
#define _SPARC64_TLB_H

#include <linux/config.h>
#include <linux/swap.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/mmu_context.h>

#define TLB_BATCH_NR	192

/*
 * For UP we don't need to worry about TLB flush
 * and page free order so much..
 */
#ifdef CONFIG_SMP
  #define FREE_PTE_NR	506
  #define tlb_fast_mode(bp) ((bp)->pages_nr == ~0U)
#else
  #define FREE_PTE_NR	1
  #define tlb_fast_mode(bp) 1
#endif

struct mmu_gather {
	struct mm_struct *mm;
	unsigned int pages_nr;
	unsigned int need_flush;
	unsigned int fullmm;
	unsigned int tlb_nr;
	unsigned long vaddrs[TLB_BATCH_NR];
	struct page *pages[FREE_PTE_NR];
};

DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

#ifdef CONFIG_SMP
extern void smp_flush_tlb_pending(struct mm_struct *,
				  unsigned long, unsigned long *);
#endif

extern void __flush_tlb_pending(unsigned long, unsigned long, unsigned long *);
extern void flush_tlb_pending(void);

static inline struct mmu_gather *tlb_gather_mmu(struct mm_struct *mm, unsigned int full_mm_flush)
{
	struct mmu_gather *mp = &get_cpu_var(mmu_gathers);

	BUG_ON(mp->tlb_nr);

	mp->mm = mm;
	mp->pages_nr = num_online_cpus() > 1 ? 0U : ~0U;
	mp->fullmm = full_mm_flush;

	return mp;
}


static inline void tlb_flush_mmu(struct mmu_gather *mp)
{
	if (mp->need_flush) {
		free_pages_and_swap_cache(mp->pages, mp->pages_nr);
		mp->pages_nr = 0;
		mp->need_flush = 0;
	}

}

#ifdef CONFIG_SMP
extern void smp_flush_tlb_mm(struct mm_struct *mm);
#define do_flush_tlb_mm(mm) smp_flush_tlb_mm(mm)
#else
#define do_flush_tlb_mm(mm) __flush_tlb_mm(CTX_HWBITS(mm->context), SECONDARY_CONTEXT)
#endif

static inline void tlb_finish_mmu(struct mmu_gather *mp, unsigned long start, unsigned long end)
{
	tlb_flush_mmu(mp);

	if (mp->fullmm)
		mp->fullmm = 0;
	else
		flush_tlb_pending();

	/* keep the page table cache within bounds */
	check_pgt_cache();

	put_cpu_var(mmu_gathers);
}

static inline void tlb_remove_page(struct mmu_gather *mp, struct page *page)
{
	if (tlb_fast_mode(mp)) {
		free_page_and_swap_cache(page);
		return;
	}
	mp->need_flush = 1;
	mp->pages[mp->pages_nr++] = page;
	if (mp->pages_nr >= FREE_PTE_NR)
		tlb_flush_mmu(mp);
}

#define tlb_remove_tlb_entry(mp,ptep,addr) do { } while (0)
#define pte_free_tlb(mp,ptepage) pte_free(ptepage)
#define pmd_free_tlb(mp,pmdp) pmd_free(pmdp)
#define pud_free_tlb(tlb,pudp) __pud_free_tlb(tlb,pudp)

#define tlb_migrate_finish(mm)	do { } while (0)
#define tlb_start_vma(tlb, vma) do { } while (0)
#define tlb_end_vma(tlb, vma)	do { } while (0)

#endif /* _SPARC64_TLB_H */
