/* tlbflush.h: TLB flushing functions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_TLBFLUSH_H
#define _ASM_TLBFLUSH_H

#include <linux/mm.h>
#include <asm/processor.h>

#ifdef CONFIG_MMU

#ifndef __ASSEMBLY__
extern void asmlinkage __flush_tlb_all(void);
extern void asmlinkage __flush_tlb_mm(unsigned long contextid);
extern void asmlinkage __flush_tlb_page(unsigned long contextid, unsigned long start);
extern void asmlinkage __flush_tlb_range(unsigned long contextid,
					 unsigned long start, unsigned long end);
#endif /* !__ASSEMBLY__ */

#define flush_tlb_all()				\
do {						\
	preempt_disable();			\
	__flush_tlb_all();			\
	preempt_enable();			\
} while(0)

#define flush_tlb_mm(mm)			\
do {						\
	preempt_disable();			\
	__flush_tlb_mm((mm)->context.id);	\
	preempt_enable();			\
} while(0)

#define flush_tlb_range(vma,start,end)					\
do {									\
	preempt_disable();						\
	__flush_tlb_range((vma)->vm_mm->context.id, start, end);	\
	preempt_enable();						\
} while(0)

#define flush_tlb_page(vma,addr)				\
do {								\
	preempt_disable();					\
	__flush_tlb_page((vma)->vm_mm->context.id, addr);	\
	preempt_enable();					\
} while(0)


#define __flush_tlb_global()			flush_tlb_all()
#define flush_tlb()				flush_tlb_all()
#define flush_tlb_kernel_range(start, end)	flush_tlb_all()
#define flush_tlb_pgtables(mm,start,end)	do { } while(0)

#else

#define flush_tlb()				BUG()
#define flush_tlb_all()				BUG()
#define flush_tlb_mm(mm)			BUG()
#define flush_tlb_page(vma,addr)		BUG()
#define flush_tlb_range(mm,start,end)		BUG()
#define flush_tlb_pgtables(mm,start,end)	BUG()
#define flush_tlb_kernel_range(start, end)	BUG()

#endif


#endif /* _ASM_TLBFLUSH_H */
