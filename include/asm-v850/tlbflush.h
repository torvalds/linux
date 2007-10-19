/*
 * include/asm-v850/tlbflush.h
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_TLBFLUSH_H__
#define __V850_TLBFLUSH_H__

#include <asm/machdep.h>


/*
 * flush all user-space atc entries.
 */
static inline void __flush_tlb(void)
{
	BUG ();
}

static inline void __flush_tlb_one(unsigned long addr)
{
	BUG ();
}

#define flush_tlb() __flush_tlb()

/*
 * flush all atc entries (both kernel and user-space entries).
 */
static inline void flush_tlb_all(void)
{
	BUG ();
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	BUG ();
}

static inline void flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	BUG ();
}

static inline void flush_tlb_range(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end)
{
	BUG ();
}

static inline void flush_tlb_kernel_page(unsigned long addr)
{
	BUG ();
}

#endif /* __V850_TLBFLUSH_H__ */
