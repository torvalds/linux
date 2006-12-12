/*
 * include/asm-sh/cpu-sh3/cacheflush.h
 *
 * Copyright (C) 1999 Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH3_CACHEFLUSH_H
#define __ASM_CPU_SH3_CACHEFLUSH_H

/*
 * Cache flushing:
 *
 *  - flush_cache_all() flushes entire cache
 *  - flush_cache_mm(mm) flushes the specified mm context's cache lines
 *  - flush_cache_page(mm, vmaddr, pfn) flushes a single page
 *  - flush_cache_range(vma, start, end) flushes a range of pages
 *
 *  - flush_dcache_page(pg) flushes(wback&invalidates) a page for dcache
 *  - flush_icache_range(start, end) flushes(invalidates) a range for icache
 *  - flush_icache_page(vma, pg) flushes(invalidates) a page for icache
 *
 *  Caches are indexed (effectively) by physical address on SH-3, so
 *  we don't need them.
 */

#if defined(CONFIG_SH7705_CACHE_32KB)

/* SH7705 is an SH3 processor with 32KB cache. This has alias issues like the
 * SH4. Unlike the SH4 this is a unified cache so we need to do some work
 * in mmap when 'exec'ing a new binary
 */
 /* 32KB cache, 4kb PAGE sizes need to check bit 12 */
#define CACHE_ALIAS 0x00001000

#define PG_mapped	PG_arch_1

void flush_cache_all(void);
void flush_cache_mm(struct mm_struct *mm);
void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
                              unsigned long end);
void flush_cache_page(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn);
void flush_dcache_page(struct page *pg);
void flush_icache_range(unsigned long start, unsigned long end);
void flush_icache_page(struct vm_area_struct *vma, struct page *page);
#else
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define flush_dcache_page(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)
#define flush_icache_page(vma,pg)		do { } while (0)
#endif

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

/* SH3 has unified cache so no special action needed here */
#define flush_cache_sigtramp(vaddr)		do { } while (0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)

#define p3_cache_init()				do { } while (0)

#endif /* __ASM_CPU_SH3_CACHEFLUSH_H */
