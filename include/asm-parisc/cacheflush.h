#ifndef _PARISC_CACHEFLUSH_H
#define _PARISC_CACHEFLUSH_H

#include <linux/mm.h>

/* The usual comment is "Caches aren't brain-dead on the <architecture>".
 * Unfortunately, that doesn't apply to PA-RISC. */

/* Internal implementation */
void flush_data_cache_local(void *);  /* flushes local data-cache only */
void flush_instruction_cache_local(void *); /* flushes local code-cache only */
#ifdef CONFIG_SMP
void flush_data_cache(void); /* flushes data-cache only (all processors) */
void flush_instruction_cache(void); /* flushes i-cache only (all processors) */
#else
#define flush_data_cache() flush_data_cache_local(NULL)
#define flush_instruction_cache() flush_instruction_cache_local(NULL)
#endif

#define flush_cache_dup_mm(mm) flush_cache_mm(mm)

void flush_user_icache_range_asm(unsigned long, unsigned long);
void flush_kernel_icache_range_asm(unsigned long, unsigned long);
void flush_user_dcache_range_asm(unsigned long, unsigned long);
void flush_kernel_dcache_range_asm(unsigned long, unsigned long);
void flush_kernel_dcache_page_asm(void *);
void flush_kernel_icache_page(void *);
void flush_user_dcache_page(unsigned long);
void flush_user_icache_page(unsigned long);
void flush_user_dcache_range(unsigned long, unsigned long);
void flush_user_icache_range(unsigned long, unsigned long);

/* Cache flush operations */

void flush_cache_all_local(void);
void flush_cache_all(void);
void flush_cache_mm(struct mm_struct *mm);

#define flush_kernel_dcache_range(start,size) \
	flush_kernel_dcache_range_asm((start), (start)+(size));

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

extern void flush_dcache_page(struct page *page);

#define flush_dcache_mmap_lock(mapping) \
	write_lock_irq(&(mapping)->tree_lock)
#define flush_dcache_mmap_unlock(mapping) \
	write_unlock_irq(&(mapping)->tree_lock)

#define flush_icache_page(vma,page)	do { 		\
	flush_kernel_dcache_page(page);			\
	flush_kernel_icache_page(page_address(page)); 	\
} while (0)

#define flush_icache_range(s,e)		do { 		\
	flush_kernel_dcache_range_asm(s,e); 		\
	flush_kernel_icache_range_asm(s,e); 		\
} while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
do { \
	flush_cache_page(vma, vaddr, page_to_pfn(page)); \
	memcpy(dst, src, len); \
	flush_kernel_dcache_range_asm((unsigned long)dst, (unsigned long)dst + len); \
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
do { \
	flush_cache_page(vma, vaddr, page_to_pfn(page)); \
	memcpy(dst, src, len); \
} while (0)

void flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr, unsigned long pfn);
void flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end);

#define ARCH_HAS_FLUSH_ANON_PAGE
static inline void
flush_anon_page(struct vm_area_struct *vma, struct page *page, unsigned long vmaddr)
{
	if (PageAnon(page))
		flush_user_dcache_page(vmaddr);
}

#define ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
void flush_kernel_dcache_page_addr(void *addr);
static inline void flush_kernel_dcache_page(struct page *page)
{
	flush_kernel_dcache_page_addr(page_address(page));
}

#ifdef CONFIG_DEBUG_RODATA
void mark_rodata_ro(void);
#endif

#ifdef CONFIG_PA8X00
/* Only pa8800, pa8900 needs this */
#define ARCH_HAS_KMAP

void kunmap_parisc(void *addr);

static inline void *kmap(struct page *page)
{
	might_sleep();
	return page_address(page);
}

#define kunmap(page)			kunmap_parisc(page_address(page))

#define kmap_atomic(page, idx)		page_address(page)

#define kunmap_atomic(addr, idx)	kunmap_parisc(addr)

#define kmap_atomic_pfn(pfn, idx)	page_address(pfn_to_page(pfn))
#define kmap_atomic_to_page(ptr)	virt_to_page(ptr)
#endif

#endif /* _PARISC_CACHEFLUSH_H */

