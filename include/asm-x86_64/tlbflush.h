#ifndef _X8664_TLBFLUSH_H
#define _X8664_TLBFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>
#include <asm/processor.h>

#define __flush_tlb()							\
	do {								\
		unsigned long tmpreg;					\
									\
		__asm__ __volatile__(					\
			"movq %%cr3, %0;  # flush TLB \n"		\
			"movq %0, %%cr3;              \n"		\
			: "=r" (tmpreg)					\
			:: "memory");					\
	} while (0)

/*
 * Global pages have to be flushed a bit differently. Not a real
 * performance problem because this does not happen often.
 */
#define __flush_tlb_global()						\
	do {								\
		unsigned long tmpreg, cr4, cr4_orig;			\
									\
		__asm__ __volatile__(					\
			"movq %%cr4, %2;  # turn off PGE     \n"	\
			"movq %2, %1;                        \n"	\
			"andq %3, %1;                        \n"	\
			"movq %1, %%cr4;                     \n"	\
			"movq %%cr3, %0;  # flush TLB        \n"	\
			"movq %0, %%cr3;                     \n"	\
			"movq %2, %%cr4;  # turn PGE back on \n"	\
			: "=&r" (tmpreg), "=&r" (cr4), "=&r" (cr4_orig)	\
			: "i" (~X86_CR4_PGE)				\
			: "memory");					\
	} while (0)

extern unsigned long pgkern_mask;

#define __flush_tlb_all() __flush_tlb_global()

#define __flush_tlb_one(addr) \
	__asm__ __volatile__("invlpg %0": :"m" (*(char *) addr))


/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
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

#define flush_tlb_kernel_range(start, end) flush_tlb_all()

static inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
	/* x86_64 does not keep any page table caches in a software TLB.
	   The CPUs do in their hardware TLBs, but they are handled
	   by the normal TLB flushing algorithms. */
}

#endif /* _X8664_TLBFLUSH_H */
