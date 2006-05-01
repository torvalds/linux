#ifndef _SPARC64_TLBFLUSH_H
#define _SPARC64_TLBFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>
#include <asm/mmu_context.h>

/* TSB flush operations. */
struct mmu_gather;
extern void flush_tsb_kernel_range(unsigned long start, unsigned long end);
extern void flush_tsb_user(struct mmu_gather *mp);

/* TLB flush operations. */

extern void flush_tlb_pending(void);

#define flush_tlb_range(vma,start,end)	\
	do { (void)(start); flush_tlb_pending(); } while (0)
#define flush_tlb_page(vma,addr)	flush_tlb_pending()
#define flush_tlb_mm(mm)		flush_tlb_pending()

/* Local cpu only.  */
extern void __flush_tlb_all(void);

extern void __flush_tlb_kernel_range(unsigned long start, unsigned long end);

#ifndef CONFIG_SMP

#define flush_tlb_kernel_range(start,end) \
do {	flush_tsb_kernel_range(start,end); \
	__flush_tlb_kernel_range(start,end); \
} while (0)

#else /* CONFIG_SMP */

extern void smp_flush_tlb_kernel_range(unsigned long start, unsigned long end);

#define flush_tlb_kernel_range(start, end) \
do {	flush_tsb_kernel_range(start,end); \
	smp_flush_tlb_kernel_range(start, end); \
} while (0)

#endif /* ! CONFIG_SMP */

static inline void flush_tlb_pgtables(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	/* We don't use virtual page tables for TLB miss processing
	 * any more.  Nowadays we use the TSB.
	 */
}

#endif /* _SPARC64_TLBFLUSH_H */
