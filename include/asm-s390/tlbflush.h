#ifndef _S390_TLBFLUSH_H
#define _S390_TLBFLUSH_H

#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/pgalloc.h>

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
 */

/*
 * S/390 has three ways of flushing TLBs
 * 'ptlb' does a flush of the local processor
 * 'csp' flushes the TLBs on all PUs of a SMP
 * 'ipte' invalidates a pte in a page table and flushes that out of
 * the TLBs of all PUs of a SMP
 */

#define local_flush_tlb() \
do {  asm volatile("ptlb": : :"memory"); } while (0)

#ifndef CONFIG_SMP

/*
 * We always need to flush, since s390 does not flush tlb
 * on each context switch
 */

static inline void flush_tlb(void)
{
	local_flush_tlb();
}
static inline void flush_tlb_all(void)
{
	local_flush_tlb();
}
static inline void flush_tlb_mm(struct mm_struct *mm) 
{
	local_flush_tlb();
}
static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long addr)
{
	local_flush_tlb();
}
static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	local_flush_tlb();
}

#define flush_tlb_kernel_range(start, end) \
	local_flush_tlb();

#else

#include <asm/smp.h>

extern void smp_ptlb_all(void);

static inline void global_flush_tlb(void)
{
	register unsigned long reg2 asm("2");
	register unsigned long reg3 asm("3");
	register unsigned long reg4 asm("4");
	long dummy;

#ifndef __s390x__
	if (!MACHINE_HAS_CSP) {
		smp_ptlb_all();
		return;
	}
#endif /* __s390x__ */

	dummy = 0;
	reg2 = reg3 = 0;
	reg4 = ((unsigned long) &dummy) + 1;
	asm volatile(
		"	csp	%0,%2"
		: : "d" (reg2), "d" (reg3), "d" (reg4), "m" (dummy) : "cc" );
}

/*
 * We only have to do global flush of tlb if process run since last
 * flush on any other pu than current. 
 * If we have threads (mm->count > 1) we always do a global flush, 
 * since the process runs on more than one processor at the same time.
 */

static inline void __flush_tlb_mm(struct mm_struct * mm)
{
	cpumask_t local_cpumask;

	if (unlikely(cpus_empty(mm->cpu_vm_mask)))
		return;
	if (MACHINE_HAS_IDTE) {
		pgd_t *shadow_pgd = get_shadow_pgd(mm->pgd);

		if (shadow_pgd) {
			asm volatile(
				"	.insn	rrf,0xb98e0000,0,%0,%1,0"
				: : "a" (2048),
				"a" (__pa(shadow_pgd) & PAGE_MASK) : "cc" );
		}
		asm volatile(
			"	.insn	rrf,0xb98e0000,0,%0,%1,0"
			: : "a" (2048), "a" (__pa(mm->pgd)&PAGE_MASK) : "cc");
		return;
	}
	preempt_disable();
	local_cpumask = cpumask_of_cpu(smp_processor_id());
	if (cpus_equal(mm->cpu_vm_mask, local_cpumask))
		local_flush_tlb();
	else
		global_flush_tlb();
	preempt_enable();
}

static inline void flush_tlb(void)
{
	__flush_tlb_mm(current->mm);
}
static inline void flush_tlb_all(void)
{
	global_flush_tlb();
}
static inline void flush_tlb_mm(struct mm_struct *mm) 
{
	__flush_tlb_mm(mm); 
}
static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long addr)
{
	__flush_tlb_mm(vma->vm_mm);
}
static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	__flush_tlb_mm(vma->vm_mm); 
}

#define flush_tlb_kernel_range(start, end) global_flush_tlb()

#endif

static inline void flush_tlb_pgtables(struct mm_struct *mm,
                                      unsigned long start, unsigned long end)
{
        /* S/390 does not keep any page table caches in TLB */
}

#endif /* _S390_TLBFLUSH_H */
