/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __UM_TLBFLUSH_H
#define __UM_TLBFLUSH_H

#include <linux/mm.h>

/*
 * TLB flushing:
 *
 *  - flush_tlb() flushes the current mm struct TLBs
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_kernel_vm() flushes the kernel vm area
 *  - flush_tlb_range(vma, start, end) flushes a range of pages
 *  - flush_tlb_pgtables(mm, start, end) flushes a range of page tables
 */

extern void flush_tlb_all(void);
extern void flush_tlb_mm(struct mm_struct *mm);
extern void flush_tlb_range(struct vm_area_struct *vma, unsigned long start, 
			    unsigned long end);
extern void flush_tlb_page_skas(struct vm_area_struct *vma,
				unsigned long address);

static inline void flush_tlb_page(struct vm_area_struct *vma,
				  unsigned long address)
{
	flush_tlb_page_skas(vma, address & PAGE_MASK);
}

extern void flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr);
extern void flush_tlb_kernel_vm(void);
extern void flush_tlb_kernel_range(unsigned long start, unsigned long end);
extern void __flush_tlb_one(unsigned long addr);

static inline void flush_tlb_pgtables(struct mm_struct *mm,
				      unsigned long start, unsigned long end)
{
}

#endif
