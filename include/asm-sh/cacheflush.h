#ifndef __ASM_SH_CACHEFLUSH_H
#define __ASM_SH_CACHEFLUSH_H

#ifdef __KERNEL__

#ifdef CONFIG_CACHE_OFF
/*
 * Nothing to do when the cache is disabled, initial flush and explicit
 * disabling is handled at CPU init time.
 *
 * See arch/sh/kernel/cpu/init.c:cache_init().
 */
#define p3_cache_init()				do { } while (0)
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define flush_dcache_page(page)			do { } while (0)
#define flush_icache_range(start, end)		do { } while (0)
#define flush_icache_page(vma,pg)		do { } while (0)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define flush_cache_sigtramp(vaddr)		do { } while (0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)
#define __flush_wback_region(start, size)	do { (void)(start); } while (0)
#define __flush_purge_region(start, size)	do { (void)(start); } while (0)
#define __flush_invalidate_region(start, size)	do { (void)(start); } while (0)
#else
#include <asm/cpu/cacheflush.h>

/*
 * Consistent DMA requires that the __flush_xxx() primitives must be set
 * for any of the enabled non-coherent caches (most of the UP CPUs),
 * regardless of PIPT or VIPT cache configurations.
 */

/* Flush (write-back only) a region (smaller than a page) */
extern void __flush_wback_region(void *start, int size);
/* Flush (write-back & invalidate) a region (smaller than a page) */
extern void __flush_purge_region(void *start, int size);
/* Flush (invalidate only) a region (smaller than a page) */
extern void __flush_invalidate_region(void *start, int size);
#endif

#define ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
static inline void flush_kernel_dcache_page(struct page *page)
{
	flush_dcache_page(page);
}

#if defined(CONFIG_CPU_SH4) && !defined(CONFIG_CACHE_OFF)
extern void copy_to_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len);

extern void copy_from_user_page(struct vm_area_struct *vma,
	struct page *page, unsigned long vaddr, void *dst, const void *src,
	unsigned long len);
#else
#define copy_to_user_page(vma, page, vaddr, dst, src, len)	\
	do {							\
		flush_cache_page(vma, vaddr, page_to_pfn(page));\
		memcpy(dst, src, len);				\
		flush_icache_user_range(vma, page, vaddr, len);	\
	} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len)	\
	do {							\
		flush_cache_page(vma, vaddr, page_to_pfn(page));\
		memcpy(dst, src, len);				\
	} while (0)
#endif

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

#define HAVE_ARCH_UNMAPPED_AREA

#endif /* __KERNEL__ */
#endif /* __ASM_SH_CACHEFLUSH_H */
