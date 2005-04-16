#ifndef _PPC64_CACHEFLUSH_H
#define _PPC64_CACHEFLUSH_H

#include <linux/mm.h>
#include <asm/cputable.h>

/*
 * No cache flushing is required when address mappings are
 * changed, because the caches on PowerPCs are physically
 * addressed.
 */
#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define flush_icache_page(vma, page)		do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

extern void flush_dcache_page(struct page *page);
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

extern void __flush_icache_range(unsigned long, unsigned long);
extern void flush_icache_user_range(struct vm_area_struct *vma,
				    struct page *page, unsigned long addr,
				    int len);

extern void flush_dcache_range(unsigned long start, unsigned long stop);
extern void flush_dcache_phys_range(unsigned long start, unsigned long stop);
extern void flush_inval_dcache_range(unsigned long start, unsigned long stop);

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { memcpy(dst, src, len); \
     flush_icache_user_range(vma, page, vaddr, len); \
} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

extern void __flush_dcache_icache(void *page_va);

static inline void flush_icache_range(unsigned long start, unsigned long stop)
{
	if (!cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		__flush_icache_range(start, stop);
}

#endif /* _PPC64_CACHEFLUSH_H */
