/*
 * include/asm-v850/cacheflush.h
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

#ifndef __V850_CACHEFLUSH_H__
#define __V850_CACHEFLUSH_H__

/* Somebody depends on this; sigh...  */
#include <linux/mm.h>

#include <asm/machdep.h>


/* The following are all used by the kernel in ways that only affect
   systems with MMUs, so we don't need them.  */
#define flush_cache_all()			((void)0)
#define flush_cache_mm(mm)			((void)0)
#define flush_cache_dup_mm(mm)			((void)0)
#define flush_cache_range(vma, start, end)	((void)0)
#define flush_cache_page(vma, vmaddr, pfn)	((void)0)
#define flush_dcache_page(page)			((void)0)
#define flush_dcache_mmap_lock(mapping)		((void)0)
#define flush_dcache_mmap_unlock(mapping)	((void)0)
#define flush_cache_vmap(start, end)		((void)0)
#define flush_cache_vunmap(start, end)		((void)0)

#ifdef CONFIG_NO_CACHE

/* Some systems have no cache at all, in which case we don't need these
   either.  */
#define flush_icache()				((void)0)
#define flush_icache_range(start, end)		((void)0)
#define flush_icache_page(vma,pg)		((void)0)
#define flush_icache_user_range(vma,pg,adr,len)	((void)0)
#define flush_cache_sigtramp(vaddr)		((void)0)

#else /* !CONFIG_NO_CACHE */

struct page;
struct mm_struct;
struct vm_area_struct;

/* Otherwise, somebody had better define them.  */
extern void flush_icache (void);
extern void flush_icache_range (unsigned long start, unsigned long end);
extern void flush_icache_page (struct vm_area_struct *vma, struct page *page);
extern void flush_icache_user_range (struct vm_area_struct *vma,
				     struct page *page,
				     unsigned long adr, int len);
extern void flush_cache_sigtramp (unsigned long addr);

#endif /* CONFIG_NO_CACHE */

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { memcpy(dst, src, len); \
     flush_icache_user_range(vma, page, vaddr, len); \
} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif /* __V850_CACHEFLUSH_H__ */
