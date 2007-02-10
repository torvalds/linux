/*
 * include/asm-xtensa/cacheflush.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) 2001 - 2006 Tensilica Inc.
 */

#ifndef _XTENSA_CACHEFLUSH_H
#define _XTENSA_CACHEFLUSH_H

#ifdef __KERNEL__

#include <linux/mm.h>
#include <asm/processor.h>
#include <asm/page.h>

/*
 * flush and invalidate data cache, invalidate instruction cache:
 *
 * __flush_invalidate_cache_all()
 * __flush_invalidate_cache_range(from,sze)
 *
 * invalidate data or instruction cache:
 *
 * __invalidate_icache_all()
 * __invalidate_icache_page(adr)
 * __invalidate_dcache_page(adr)
 * __invalidate_icache_range(from,size)
 * __invalidate_dcache_range(from,size)
 *
 * flush data cache:
 *
 * __flush_dcache_page(adr)
 *
 * flush and invalidate data cache:
 *
 * __flush_invalidate_dcache_all()
 * __flush_invalidate_dcache_page(adr)
 * __flush_invalidate_dcache_range(from,size)
 */

extern void __flush_invalidate_cache_all(void);
extern void __flush_invalidate_cache_range(unsigned long, unsigned long);
extern void __flush_invalidate_dcache_all(void);
extern void __invalidate_icache_all(void);

extern void __invalidate_dcache_page(unsigned long);
extern void __invalidate_icache_page(unsigned long);
extern void __invalidate_icache_range(unsigned long, unsigned long);
extern void __invalidate_dcache_range(unsigned long, unsigned long);

#if XCHAL_DCACHE_IS_WRITEBACK
extern void __flush_dcache_page(unsigned long);
extern void __flush_invalidate_dcache_page(unsigned long);
extern void __flush_invalidate_dcache_range(unsigned long, unsigned long);
#else
# define __flush_dcache_page(p)				do { } while(0)
# define __flush_invalidate_dcache_page(p) 		do { } while(0)
# define __flush_invalidate_dcache_range(p,s)		do { } while(0)
#endif

/*
 * We have physically tagged caches - nothing to do here -
 * unless we have cache aliasing.
 *
 * Pages can get remapped. Because this might change the 'color' of that page,
 * we have to flush the cache before the PTE is changed.
 * (see also Documentation/cachetlb.txt)
 */

#if (DCACHE_WAY_SIZE > PAGE_SIZE) && XCHAL_DCACHE_IS_WRITEBACK

#define flush_cache_all()		__flush_invalidate_cache_all();
#define flush_cache_mm(mm)		__flush_invalidate_cache_all();
#define flush_cache_dup_mm(mm)		__flush_invalidate_cache_all();

#define flush_cache_vmap(start,end)	__flush_invalidate_cache_all();
#define flush_cache_vunmap(start,end)	__flush_invalidate_cache_all();

extern void flush_dcache_page(struct page*);

extern void flush_cache_range(struct vm_area_struct*, ulong, ulong);
extern void flush_cache_page(struct vm_area_struct*, unsigned long, unsigned long);

#else

#define flush_cache_all()				do { } while (0)
#define flush_cache_mm(mm)				do { } while (0)
#define flush_cache_dup_mm(mm)				do { } while (0)

#define flush_cache_vmap(start,end)			do { } while (0)
#define flush_cache_vunmap(start,end)			do { } while (0)

#define flush_dcache_page(page)				do { } while (0)

#define flush_cache_page(vma,addr,pfn)			do { } while (0)
#define flush_cache_range(vma,start,end)		do { } while (0)

#endif

#define flush_icache_range(start,end) 					\
	__invalidate_icache_range(start,(end)-(start))

/* This is not required, see Documentation/cachetlb.txt */

#define	flush_icache_page(vma,page)			do { } while(0)

#define flush_dcache_mmap_lock(mapping)			do { } while (0)
#define flush_dcache_mmap_unlock(mapping)		do { } while (0)


#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif /* __KERNEL__ */

#endif /* _XTENSA_CACHEFLUSH_H */

