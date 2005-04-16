/*
 * (C) Copyright 2002, Yoshinori Sato <ysato@users.sourceforge.jp>
 */

#ifndef _ASM_H8300_CACHEFLUSH_H
#define _AMS_H8300_CACHEFLUSH_H

/*
 * Cache handling functions
 * No Cache memory all dummy functions
 */

#define flush_cache_all()
#define	flush_cache_mm(mm)
#define	flush_cache_range(vma,a,b)
#define	flush_cache_page(vma,p,pfn)
#define	flush_dcache_page(page)
#define	flush_dcache_mmap_lock(mapping)
#define	flush_dcache_mmap_unlock(mapping)
#define	flush_icache()
#define	flush_icache_page(vma,page)
#define	flush_icache_range(start,len)
#define flush_cache_vmap(start, end)
#define flush_cache_vunmap(start, end)
#define	cache_push_v(vaddr,len)
#define	cache_push(paddr,len)
#define	cache_clear(paddr,len)

#define	flush_dcache_range(a,b)

#define	flush_icache_user_range(vma,page,addr,len)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

#endif /* _ASM_H8300_CACHEFLUSH_H */
