#ifndef _ASM_IA64_CACHEFLUSH_H
#define _ASM_IA64_CACHEFLUSH_H

/*
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/page-flags.h>

#include <asm/bitops.h>
#include <asm/page.h>

/*
 * Cache flushing routines.  This is the kind of stuff that can be very expensive, so try
 * to avoid them whenever possible.
 */

#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define flush_icache_page(vma,page)		do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#define flush_dcache_page(page)			\
do {						\
	clear_bit(PG_arch_1, &(page)->flags);	\
} while (0)

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

extern void flush_icache_range (unsigned long start, unsigned long end);

#define flush_icache_user_range(vma, page, user_addr, len)					\
do {												\
	unsigned long _addr = (unsigned long) page_address(page) + ((user_addr) & ~PAGE_MASK);	\
	flush_icache_range(_addr, _addr + (len));						\
} while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { memcpy(dst, src, len); \
     flush_icache_user_range(vma, page, vaddr, len); \
} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif /* _ASM_IA64_CACHEFLUSH_H */
