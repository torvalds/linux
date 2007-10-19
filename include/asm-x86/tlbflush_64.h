#ifndef _X8664_TLBFLUSH_H
#define _X8664_TLBFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>
#include <asm/processor.h>
#include <asm/system.h>

static inline void __flush_tlb(void)
{
	write_cr3(read_cr3());
}

static inline void __flush_tlb_all(void)
{
	unsigned long cr4 = read_cr4();
	write_cr4(cr4 & ~X86_CR4_PGE);	/* clear PGE */
	write_cr4(cr4);			/* write old PGE again and flush TLBs */
}

#define __flush_tlb_one(addr) \
	__asm__ __volatile__("invlpg (%0)" :: "r" (addr) : "memory")


/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *
 * x86-64 can only flush individual pages or full VMs. For a range flush
 * we always do the full VM. Might be worth trying if for a small
 * range a few INVLPGs in a row are a win.
 */

#ifndef CONFIG_SMP

#define flush_tlb() __flush_tlb()
#define flush_tlb_all() __flush_tlb_all()
#define local_flush_tlb() __flush_tlb()

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	if (mm == current->active_mm)
		__flush_tlb();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
	unsigned long addr)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb_one(addr);
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
	unsigned long start, unsigned long end)
{
	if (vma->vm_mm == current->active_mm)
		__flush_tlb();
}

#else

#include <asm/smp.h>

#define local_flush_tlb() \
	__flush_tlb()

extern void flush_tlb_all(void);
extern void flush_tlb_current_task(void);
extern void flush_tlb_mm(struct mm_struct *);
extern void flush_tlb_page(struct vm_area_struct *, unsigned long);

#define flush_tlb()	flush_tlb_current_task()

static inline void flush_tlb_range(struct vm_area_struct * vma, unsigned long start, unsigned long end)
{
	flush_tlb_mm(vma->vm_mm);
}

#define TLBSTATE_OK	1
#define TLBSTATE_LAZY	2

/* Roughly an IPI every 20MB with 4k pages for freeing page table
   ranges. Cost is about 42k of memory for each CPU. */
#define ARCH_FREE_PTE_NR 5350	

#endif

static inline void flush_tlb_kernel_range(unsigned long start,
					unsigned long end)
{
	flush_tlb_all();
}

#endif /* _X8664_TLBFLUSH_H */
