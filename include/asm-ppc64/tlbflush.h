#ifndef _PPC64_TLBFLUSH_H
#define _PPC64_TLBFLUSH_H

/*
 * TLB flushing:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_page_nohash(vma, vmaddr) flushes one page if SW loaded TLB
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 */

#include <linux/percpu.h>
#include <asm/page.h>

#define PPC64_TLB_BATCH_NR 192

struct mm_struct;
struct ppc64_tlb_batch {
	unsigned long index;
	unsigned long context;
	struct mm_struct *mm;
	pte_t pte[PPC64_TLB_BATCH_NR];
	unsigned long addr[PPC64_TLB_BATCH_NR];
	unsigned long vaddr[PPC64_TLB_BATCH_NR];
};
DECLARE_PER_CPU(struct ppc64_tlb_batch, ppc64_tlb_batch);

extern void __flush_tlb_pending(struct ppc64_tlb_batch *batch);

static inline void flush_tlb_pending(void)
{
	struct ppc64_tlb_batch *batch = &get_cpu_var(ppc64_tlb_batch);

	if (batch->index)
		__flush_tlb_pending(batch);
	put_cpu_var(ppc64_tlb_batch);
}

#define flush_tlb_mm(mm)			flush_tlb_pending()
#define flush_tlb_page(vma, addr)		flush_tlb_pending()
#define flush_tlb_page_nohash(vma, addr)       	do { } while (0)
#define flush_tlb_range(vma, start, end) \
		do { (void)(start); flush_tlb_pending(); } while (0)
#define flush_tlb_kernel_range(start, end)	flush_tlb_pending()
#define flush_tlb_pgtables(mm, start, end)	do { } while (0)

extern void flush_hash_page(unsigned long context, unsigned long ea, pte_t pte,
			    int local);
void flush_hash_range(unsigned long context, unsigned long number, int local);

#endif /* _PPC64_TLBFLUSH_H */
