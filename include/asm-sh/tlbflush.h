#ifndef __ASM_SH_TLBFLUSH_H
#define __ASM_SH_TLBFLUSH_H

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 */
extern void local_flush_tlb_all(void);
extern void local_flush_tlb_mm(struct mm_struct *mm);
extern void local_flush_tlb_range(struct vm_area_struct *vma,
				  unsigned long start,
				  unsigned long end);
extern void local_flush_tlb_page(struct vm_area_struct *vma,
				 unsigned long page);
extern void local_flush_tlb_kernel_range(unsigned long start,
					 unsigned long end);
extern void local_flush_tlb_one(unsigned long asid, unsigned long page);

#ifdef CONFIG_SMP

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long page);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
extern void flush_tlb_one(unsigned long asid, unsigned long page);

#else

#define flush_tlb_all()			local_flush_tlb_all()
#define flush_tlb_mm(mm)		local_flush_tlb_mm(mm)
#define flush_tlb_page(vma, page)	local_flush_tlb_page(vma, page)
#define flush_tlb_one(asid, page)	local_flush_tlb_one(asid, page)

#define flush_tlb_range(vma, start, end)	\
	local_flush_tlb_range(vma, start, end)

#define flush_tlb_kernel_range(start, end)	\
	local_flush_tlb_kernel_range(start, end)

#endif /* CONFIG_SMP */

static inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
	/* Nothing to do */
}
#endif /* __ASM_SH_TLBFLUSH_H */
