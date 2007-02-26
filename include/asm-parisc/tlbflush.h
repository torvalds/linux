#ifndef _PARISC_TLBFLUSH_H
#define _PARISC_TLBFLUSH_H

/* TLB flushing routines.... */

#include <linux/mm.h>
#include <asm/mmu_context.h>


/* This is for the serialisation of PxTLB broadcasts.  At least on the
 * N class systems, only one PxTLB inter processor broadcast can be
 * active at any one time on the Merced bus.  This tlb purge
 * synchronisation is fairly lightweight and harmless so we activate
 * it on all SMP systems not just the N class.  We also need to have
 * preemption disabled on uniprocessor machines, and spin_lock does that
 * nicely.
 */
extern spinlock_t pa_tlb_lock;

#define purge_tlb_start(x) spin_lock(&pa_tlb_lock)
#define purge_tlb_end(x) spin_unlock(&pa_tlb_lock)

extern void flush_tlb_all(void);
extern void flush_tlb_all_local(void *);

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

void __flush_tlb_range(unsigned long sid,
	unsigned long start, unsigned long end);

#define flush_tlb_range(vma,start,end) __flush_tlb_range((vma)->vm_mm->context,start,end)

#define flush_tlb_kernel_range(start, end) __flush_tlb_range(0,start,end)

#endif
