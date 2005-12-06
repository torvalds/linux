#ifndef __ASMARM_TLB_H
#define __ASMARM_TLB_H

#include <asm/pgalloc.h>
#include <asm/tlbflush.h>

/*
 * TLB handling.  This allows us to remove pages from the page
 * tables, and efficiently handle the TLB issues.
 */
struct mmu_gather {
        struct mm_struct        *mm;
        unsigned int            need_flush;
        unsigned int            fullmm;
};

DECLARE_PER_CPU(struct mmu_gather, mmu_gathers);

static inline struct mmu_gather *
tlb_gather_mmu(struct mm_struct *mm, unsigned int full_mm_flush)
{
        struct mmu_gather *tlb = &get_cpu_var(mmu_gathers);

        tlb->mm = mm;
        tlb->need_flush = 0;
        tlb->fullmm = full_mm_flush;

        return tlb;
}

static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
        if (tlb->need_flush)
                flush_tlb_mm(tlb->mm);

        /* keep the page table cache within bounds */
        check_pgt_cache();

        put_cpu_var(mmu_gathers);
}

#define tlb_remove_tlb_entry(tlb,ptep,address)  do { } while (0)
//#define tlb_start_vma(tlb,vma)                  do { } while (0)
//FIXME - ARM32 uses this now that things changed in the kernel. seems like it may be pointless on arm26, however to get things compiling...
#define tlb_start_vma(tlb,vma)                                          \
        do {                                                            \
                if (!tlb->fullmm)                                       \
                        flush_cache_range(vma, vma->vm_start, vma->vm_end); \
        } while (0)
#define tlb_end_vma(tlb,vma)                    do { } while (0)

static inline void
tlb_remove_page(struct mmu_gather *tlb, struct page *page)
{
        tlb->need_flush = 1;
        free_page_and_swap_cache(page);
}

#define pte_free_tlb(tlb,ptep)          pte_free(ptep)
#define pmd_free_tlb(tlb,pmdp)          pmd_free(pmdp)

#endif
