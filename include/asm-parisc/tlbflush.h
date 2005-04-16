#ifndef _PARISC_TLBFLUSH_H
#define _PARISC_TLBFLUSH_H

/* TLB flushing routines.... */

#include <linux/config.h>
#include <linux/mm.h>
#include <asm/mmu_context.h>

extern void flush_tlb_all(void);

/*
 * flush_tlb_mm()
 *
 * XXX This code is NOT valid for HP-UX compatibility processes,
 * (although it will probably work 99% of the time). HP-UX
 * processes are free to play with the space id's and save them
 * over long periods of time, etc. so we have to preserve the
 * space and just flush the entire tlb. We need to check the
 * personality in order to do that, but the personality is not
 * currently being set correctly.
 *
 * Of course, Linux processes could do the same thing, but
 * we don't support that (and the compilers, dynamic linker,
 * etc. do not do that).
 */

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	BUG_ON(mm == &init_mm); /* Should never happen */

#ifdef CONFIG_SMP
	flush_tlb_all();
#else
	if (mm) {
		if (mm->context != 0)
			free_sid(mm->context);
		mm->context = alloc_sid();
		if (mm == current->active_mm)
			load_context(mm->context);
	}
#endif
}

extern __inline__ void flush_tlb_pgtables(struct mm_struct *mm, unsigned long start, unsigned long end)
{
}
 
static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
	/* For one page, it's not worth testing the split_tlb variable */

	mb();
	mtsp(vma->vm_mm->context,1);
	purge_tlb_start();
	pdtlb(addr);
	pitlb(addr);
	purge_tlb_end();
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end)
{
	unsigned long npages;

	
	npages = ((end - (start & PAGE_MASK)) + (PAGE_SIZE - 1)) >> PAGE_SHIFT;
	if (npages >= 512)  /* XXX arbitrary, should be tuned */
		flush_tlb_all();
	else {

		mtsp(vma->vm_mm->context,1);
		if (split_tlb) {
			purge_tlb_start();
			while (npages--) {
				pdtlb(start);
				pitlb(start);
				start += PAGE_SIZE;
			}
			purge_tlb_end();
		} else {
			purge_tlb_start();
			while (npages--) {
				pdtlb(start);
				start += PAGE_SIZE;
			}
			purge_tlb_end();
		}
	}
}

#define flush_tlb_kernel_range(start, end) flush_tlb_all()

#endif
