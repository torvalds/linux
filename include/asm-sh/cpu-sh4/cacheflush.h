/*
 * include/asm-sh/cpu-sh4/cacheflush.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 * Copyright (C) 2003 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_CACHEFLUSH_H
#define __ASM_CPU_SH4_CACHEFLUSH_H

/*
 *  Caches are broken on SH-4 (unless we use write-through
 *  caching; in which case they're only semi-broken),
 *  so we need them.
 */

/* Page is 4K, OC size is 16K, there are four lines. */
#define CACHE_ALIAS 0x00003000

struct page;
struct mm_struct;
struct vm_area_struct;

extern void flush_cache_all(void);
extern void flush_cache_mm(struct mm_struct *mm);
extern void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
			      unsigned long end);
extern void flush_cache_page(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn);
extern void flush_dcache_page(struct page *pg);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

extern void flush_icache_range(unsigned long start, unsigned long end);
extern void flush_cache_sigtramp(unsigned long addr);
extern void flush_icache_user_range(struct vm_area_struct *vma,
				    struct page *page, unsigned long addr,
				    int len);

#define flush_icache_page(vma,pg)		do { } while (0)

/* Initialization of P3 area for copy_user_page */
extern void p3_cache_init(void);

#define PG_mapped	PG_arch_1

/* We provide our own get_unmapped_area to avoid cache alias issue */
#define HAVE_ARCH_UNMAPPED_AREA

#ifdef CONFIG_MMU
extern int remap_area_pages(unsigned long addr, unsigned long phys_addr,
			    unsigned long size, unsigned long flags);
#else /* CONFIG_MMU */
static inline int remap_area_pages(unsigned long addr, unsigned long phys_addr,
				   unsigned long size, unsigned long flags)
{
	return 0;
}
#endif /* CONFIG_MMU */
#endif /* __ASM_CPU_SH4_CACHEFLUSH_H */

