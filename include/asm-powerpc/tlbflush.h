#ifndef _ASM_POWERPC_TLBFLUSH_H
#define _ASM_POWERPC_TLBFLUSH_H
/*
 * TLB flushing:
 *
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_page_nohash(vma, vmaddr) flushes one page if SW loaded TLB
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_kernel_range(start, end) flushes a range of kernel pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifdef __KERNEL__

#include <linux/config.h>

struct mm_struct;

#ifdef CONFIG_PPC64

#include <linux/percpu.h>
#include <asm/page.h>

#define PPC64_TLB_BATCH_NR 192

struct ppc64_tlb_batch {
	unsigned long index;
	struct mm_struct *mm;
	pte_t pte[PPC64_TLB_BATCH_NR];
	unsigned long vaddr[PPC64_TLB_BATCH_NR];
	unsigned int large;
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

extern void flush_hash_page(unsigned long va, pte_t pte, int local);
void flush_hash_range(unsigned long number, int local);

#else /* CONFIG_PPC64 */

#include <linux/mm.h>

extern void _tlbie(unsigned long address);
extern void _tlbia(void);

/*
 * TODO: (CONFIG_FSL_BOOKE) determine if flush_tlb_range &
 * flush_tlb_kernel_range are best implemented as tlbia vs
 * specific tlbie's
 */

#if (defined(CONFIG_4xx) && !defined(CONFIG_44x)) || defined(CONFIG_8xx)
#define flush_tlb_pending()	asm volatile ("tlbia; sync" : : : "memory")
#elif defined(CONFIG_4xx) || defined(CONFIG_FSL_BOOKE)
#define flush_tlb_pending()	_tlbia()
#endif

/*
 * This gets called at the end of handling a page fault, when
 * the kernel has put a new PTE into the page table for the process.
 * We use it to ensure coherency between the i-cache and d-cache
 * for the page which has just been mapped in.
 * On machines which use an MMU hash table, we use this to put a
 * corresponding HPTE into the hash table ahead of time, instead of
 * waiting for the inevitable extra hash-table miss exception.
 */
extern void update_mmu_cache(struct vm_area_struct *, unsigned long, pte_t);

#endif /* CONFIG_PPC64 */

#if defined(CONFIG_PPC64) || defined(CONFIG_4xx) || \
	defined(CONFIG_FSL_BOOKE) || defined(CONFIG_8xx)

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	flush_tlb_pending();
}

static inline void flush_tlb_page(struct vm_area_struct *vma,
				unsigned long vmaddr)
{
#ifdef CONFIG_PPC64
	flush_tlb_pending();
#else
	_tlbie(vmaddr);
#endif
}

static inline void flush_tlb_page_nohash(struct vm_area_struct *vma,
					 unsigned long vmaddr)
{
#ifndef CONFIG_PPC64
	_tlbie(vmaddr);
#endif
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	flush_tlb_pending();
}

static inline void flush_tlb_kernel_range(unsigned long start,
		unsigned long end)
{
	flush_tlb_pending();
}

#else	/* 6xx, 7xx, 7xxx cpus */

extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void flush_tlb_page_nohash(struct vm_area_struct *vma, unsigned long addr);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			    unsigned long end);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);

#endif

/*
 * This is called in munmap when we have freed up some page-table
 * pages.  We don't need to do anything here, there's nothing special
 * about our page-table pages.  -- paulus
 */
static inline void flush_tlb_pgtables(struct mm_struct *mm,
		unsigned long start, unsigned long end)
{
}

#endif /*__KERNEL__ */
#endif /* _ASM_POWERPC_TLBFLUSH_H */
