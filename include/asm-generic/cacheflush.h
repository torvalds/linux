/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_CACHEFLUSH_H
#define _ASM_GENERIC_CACHEFLUSH_H

struct mm_struct;
struct vm_area_struct;
struct page;
struct address_space;

/*
 * The cache doesn't need to be flushed when TLB entries change when
 * the cache is mapped to physical memory, not virtual memory
 */
#ifndef flush_cache_all
static inline void flush_cache_all(void)
{
}
#endif

#ifndef flush_cache_mm
static inline void flush_cache_mm(struct mm_struct *mm)
{
}
#endif

#ifndef flush_cache_dup_mm
static inline void flush_cache_dup_mm(struct mm_struct *mm)
{
}
#endif

#ifndef flush_cache_range
static inline void flush_cache_range(struct vm_area_struct *vma,
				     unsigned long start,
				     unsigned long end)
{
}
#endif

#ifndef flush_cache_page
static inline void flush_cache_page(struct vm_area_struct *vma,
				    unsigned long vmaddr,
				    unsigned long pfn)
{
}
#endif

#ifndef ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE
static inline void flush_dcache_page(struct page *page)
{
}

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
#endif

#ifndef flush_dcache_mmap_lock
static inline void flush_dcache_mmap_lock(struct address_space *mapping)
{
}
#endif

#ifndef flush_dcache_mmap_unlock
static inline void flush_dcache_mmap_unlock(struct address_space *mapping)
{
}
#endif

#ifndef flush_icache_range
static inline void flush_icache_range(unsigned long start, unsigned long end)
{
}
#endif

#ifndef flush_icache_user_range
#define flush_icache_user_range flush_icache_range
#endif

#ifndef flush_icache_page
static inline void flush_icache_page(struct vm_area_struct *vma,
				     struct page *page)
{
}
#endif

#ifndef flush_icache_user_page
static inline void flush_icache_user_page(struct vm_area_struct *vma,
					   struct page *page,
					   unsigned long addr, int len)
{
}
#endif

#ifndef flush_cache_vmap
static inline void flush_cache_vmap(unsigned long start, unsigned long end)
{
}
#endif

#ifndef flush_cache_vunmap
static inline void flush_cache_vunmap(unsigned long start, unsigned long end)
{
}
#endif

#ifndef copy_to_user_page
#define copy_to_user_page(vma, page, vaddr, dst, src, len)	\
	do { \
		memcpy(dst, src, len); \
		flush_icache_user_page(vma, page, vaddr, len); \
	} while (0)
#endif

#ifndef copy_from_user_page
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)
#endif

#endif /* _ASM_GENERIC_CACHEFLUSH_H */
