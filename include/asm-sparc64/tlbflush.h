#ifndef _SPARC64_TLBFLUSH_H
#define _SPARC64_TLBFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>
#include <asm/mmu_context.h>

/* TLB flush operations. */

extern void flush_tlb_pending(void);

#define flush_tlb_range(vma,start,end)	\
	do { (void)(start); flush_tlb_pending(); } while (0)
#define flush_tlb_page(vma,addr)	flush_tlb_pending()
#define flush_tlb_mm(mm)		flush_tlb_pending()

extern void __flush_tlb_all(void);
extern void __flush_tlb_page(unsigned long context, unsigned long page, unsigned long r);

extern void __flush_tlb_kernel_range(unsigned long start, unsigned long end);

#ifndef CONFIG_SMP

#define flush_tlb_all()		__flush_tlb_all()
#define flush_tlb_kernel_range(start,end) \
	__flush_tlb_kernel_range(start,end)

#else /* CONFIG_SMP */

extern void smp_flush_tlb_all(void);
extern void smp_flush_tlb_kernel_range(unsigned long start, unsigned long end);

#define flush_tlb_all()		smp_flush_tlb_all()
#define flush_tlb_kernel_range(start, end) \
	smp_flush_tlb_kernel_range(start, end)

#endif /* ! CONFIG_SMP */

extern void flush_tlb_pgtables(struct mm_struct *, unsigned long, unsigned long);

#endif /* _SPARC64_TLBFLUSH_H */
