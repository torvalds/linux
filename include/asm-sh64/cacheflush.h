#ifndef __ASM_SH64_CACHEFLUSH_H
#define __ASM_SH64_CACHEFLUSH_H

#ifndef __ASSEMBLY__

#include <asm/page.h>

struct vm_area_struct;
struct page;
struct mm_struct;

extern void flush_cache_all(void);
extern void flush_cache_mm(struct mm_struct *mm);
extern void flush_cache_sigtramp(unsigned long start, unsigned long end);
extern void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
			      unsigned long end);
extern void flush_cache_page(struct vm_area_struct *vma, unsigned long addr, unsigned long pfn);
extern void flush_dcache_page(struct page *pg);
extern void flush_icache_range(unsigned long start, unsigned long end);
extern void flush_icache_user_range(struct vm_area_struct *vma,
				    struct page *page, unsigned long addr,
				    int len);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

#define flush_icache_page(vma, page)	do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	do {							\
		flush_cache_page(vma, vaddr, page_to_pfn(page));\
		memcpy(dst, src, len);				\
		flush_icache_user_range(vma, page, vaddr, len);	\
	} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	do {							\
		flush_cache_page(vma, vaddr, page_to_pfn(page));\
		memcpy(dst, src, len);				\
	} while (0)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_SH64_CACHEFLUSH_H */

