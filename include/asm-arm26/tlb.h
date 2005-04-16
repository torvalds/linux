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
        unsigned int            freed;
	unsigned int            fullmm;

        unsigned int            flushes;
        unsigned int            avoided_flushes;
};

extern struct mmu_gather mmu_gathers[NR_CPUS];

static inline struct mmu_gather *
tlb_gather_mmu(struct mm_struct *mm, unsigned int full_mm_flush)
{
        int cpu = smp_processor_id();
        struct mmu_gather *tlb = &mmu_gathers[cpu];

        tlb->mm = mm;
        tlb->freed = 0;
	tlb->fullmm = full_mm_flush;

        return tlb;
}

static inline void
tlb_finish_mmu(struct mmu_gather *tlb, unsigned long start, unsigned long end)
{
        struct mm_struct *mm = tlb->mm;
        unsigned long freed = tlb->freed;
        int rss = get_mm_counter(mm, rss);

        if (rss < freed)
                freed = rss;
        add_mm_counter(mm, rss, -freed);

        if (freed) {
                flush_tlb_mm(mm);
                tlb->flushes++;
        } else {
                tlb->avoided_flushes++;
        }

        /* keep the page table cache within bounds */
        check_pgt_cache();
}


static inline unsigned int
tlb_is_full_mm(struct mmu_gather *tlb)
{
     return tlb->fullmm;
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

#define tlb_remove_page(tlb,page)       free_page_and_swap_cache(page)
#define pte_free_tlb(tlb,ptep)          pte_free(ptep)
#define pmd_free_tlb(tlb,pmdp)          pmd_free(pmdp)

#endif
