/*
 * include/asm-ppc/tlbflush.h
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifdef __KERNEL__
#ifndef _PPC_TLBFLUSH_H
#define _PPC_TLBFLUSH_H

#include <linux/config.h>
#include <linux/mm.h>

extern void _tlbie(unsigned long address);
extern void _tlbia(void);

#if defined(CONFIG_4xx)

#ifndef CONFIG_44x
#define __tlbia()	asm volatile ("sync; tlbia; isync" : : : "memory")
#else
#define __tlbia		_tlbia
#endif

static inline void flush_tlb_mm(struct mm_struct *mm)
	{ __tlbia(); }
static inline void flush_tlb_page(struct vm_area_struct *vma,
				unsigned long vmaddr)
	{ _tlbie(vmaddr); }
static inline void flush_tlb_page_nohash(struct vm_area_struct *vma,
					 unsigned long vmaddr)
	{ _tlbie(vmaddr); }
static inline void flush_tlb_range(struct vm_area_struct *vma,
				unsigned long start, unsigned long end)
	{ __tlbia(); }
static inline void flush_tlb_kernel_range(unsigned long start,
				unsigned long end)
	{ __tlbia(); }

#elif defined(CONFIG_FSL_BOOKE)

/* TODO: determine if flush_tlb_range & flush_tlb_kernel_range
 * are best implemented as tlbia vs specific tlbie's */

#define __tlbia()	_tlbia()

static inline void flush_tlb_mm(struct mm_struct *mm)
	{ __tlbia(); }
static inline void flush_tlb_page(struct vm_area_struct *vma,
				unsigned long vmaddr)
	{ _tlbie(vmaddr); }
static inline void flush_tlb_page_nohash(struct vm_area_struct *vma,
					 unsigned long vmaddr)
	{ _tlbie(vmaddr); }
static inline void flush_tlb_range(struct vm_area_struct *vma,
				unsigned long start, unsigned long end)
	{ __tlbia(); }
static inline void flush_tlb_kernel_range(unsigned long start,
				unsigned long end)
	{ __tlbia(); }

#elif defined(CONFIG_8xx)
#define __tlbia()	asm volatile ("tlbia; sync" : : : "memory")

static inline void flush_tlb_mm(struct mm_struct *mm)
	{ __tlbia(); }
static inline void flush_tlb_page(struct vm_area_struct *vma,
				unsigned long vmaddr)
	{ _tlbie(vmaddr); }
static inline void flush_tlb_page_nohash(struct vm_area_struct *vma,
					 unsigned long vmaddr)
	{ _tlbie(vmaddr); }
static inline void flush_tlb_range(struct mm_struct *mm,
				unsigned long start, unsigned long end)
	{ __tlbia(); }
static inline void flush_tlb_kernel_range(unsigned long start,
				unsigned long end)
	{ __tlbia(); }

#else	/* 6xx, 7xx, 7xxx cpus */
struct mm_struct;
struct vm_area_struct;
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

#endif /* _PPC_TLBFLUSH_H */
#endif /*__KERNEL__ */
