#ifndef _SPARC64_CACHEFLUSH_H
#define _SPARC64_CACHEFLUSH_H

#include <asm/page.h>

#ifndef __ASSEMBLY__

#include <linux/mm.h>

/* Cache flush operations. */

/* These are the same regardless of whether this is an SMP kernel or not. */
#define flush_cache_mm(__mm) \
	do { if ((__mm) == current->mm) flushw_user(); } while(0)
#define flush_cache_range(vma, start, end) \
	flush_cache_mm((vma)->vm_mm)
#define flush_cache_page(vma, page, pfn) \
	flush_cache_mm((vma)->vm_mm)

/* 
 * On spitfire, the icache doesn't snoop local stores and we don't
 * use block commit stores (which invalidate icache lines) during
 * module load, so we need this.
 */
extern void flush_icache_range(unsigned long start, unsigned long end);
extern void __flush_icache_page(unsigned long);

extern void __flush_dcache_page(void *addr, int flush_icache);
extern void flush_dcache_page_impl(struct page *page);
#ifdef CONFIG_SMP
extern void smp_flush_dcache_page_impl(struct page *page, int cpu);
extern void flush_dcache_page_all(struct mm_struct *mm, struct page *page);
#else
#define smp_flush_dcache_page_impl(page,cpu) flush_dcache_page_impl(page)
#define flush_dcache_page_all(mm,page) flush_dcache_page_impl(page)
#endif

extern void __flush_dcache_range(unsigned long start, unsigned long end);
extern void flush_dcache_page(struct page *page);

#define flush_icache_page(vma, pg)	do { } while(0)
#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)

extern void flush_ptrace_access(struct vm_area_struct *, struct page *,
				unsigned long uaddr, void *kaddr,
				unsigned long len, int write);

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
	do {								\
		flush_cache_page(vma, vaddr, page_to_pfn(page));	\
		memcpy(dst, src, len);					\
		flush_ptrace_access(vma, page, vaddr, src, len, 0);	\
	} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) 		\
	do {								\
		flush_cache_page(vma, vaddr, page_to_pfn(page));	\
		memcpy(dst, src, len);					\
		flush_ptrace_access(vma, page, vaddr, dst, len, 1);	\
	} while (0)

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#ifdef CONFIG_DEBUG_PAGEALLOC
/* internal debugging function */
void kernel_map_pages(struct page *page, int numpages, int enable);
#endif

#endif /* !__ASSEMBLY__ */

#endif /* _SPARC64_CACHEFLUSH_H */
