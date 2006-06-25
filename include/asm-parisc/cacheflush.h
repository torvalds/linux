#ifndef _PARISC_CACHEFLUSH_H
#define _PARISC_CACHEFLUSH_H

#include <linux/mm.h>
#include <asm/cache.h>	/* for flush_user_dcache_range_asm() proto */

/* The usual comment is "Caches aren't brain-dead on the <architecture>".
 * Unfortunately, that doesn't apply to PA-RISC. */

/* Cache flush operations */

#ifdef CONFIG_SMP
#define flush_cache_mm(mm) flush_cache_all()
#else
#define flush_cache_mm(mm) flush_cache_all_local()
#endif

#define flush_kernel_dcache_range(start,size) \
	flush_kernel_dcache_range_asm((start), (start)+(size));

extern void flush_cache_all_local(void);

static inline void cacheflush_h_tmp_function(void *dummy)
{
	flush_cache_all_local();
}

static inline void flush_cache_all(void)
{
	on_each_cpu(cacheflush_h_tmp_function, NULL, 1, 1);
}

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

extern int parisc_cache_flush_threshold;
void parisc_setup_cache_timing(void);

static inline void
flush_user_dcache_range(unsigned long start, unsigned long end)
{
	if ((end - start) < parisc_cache_flush_threshold)
		flush_user_dcache_range_asm(start,end);
	else
		flush_data_cache();
}

static inline void
flush_user_icache_range(unsigned long start, unsigned long end)
{
	if ((end - start) < parisc_cache_flush_threshold)
		flush_user_icache_range_asm(start,end);
	else
		flush_instruction_cache();
}

extern void flush_dcache_page(struct page *page);

#define flush_dcache_mmap_lock(mapping) \
	write_lock_irq(&(mapping)->tree_lock)
#define flush_dcache_mmap_unlock(mapping) \
	write_unlock_irq(&(mapping)->tree_lock)

#define flush_icache_page(vma,page)	do { flush_kernel_dcache_page(page); flush_kernel_icache_page(page_address(page)); } while (0)

#define flush_icache_range(s,e)		do { flush_kernel_dcache_range_asm(s,e); flush_kernel_icache_range_asm(s,e); } while (0)

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

static inline void flush_cache_range(struct vm_area_struct *vma,
		unsigned long start, unsigned long end)
{
	int sr3;

	if (!vma->vm_mm->context) {
		BUG();
		return;
	}

	sr3 = mfsp(3);
	if (vma->vm_mm->context == sr3) {
		flush_user_dcache_range(start,end);
		flush_user_icache_range(start,end);
	} else {
		flush_cache_all();
	}
}

/* Simple function to work out if we have an existing address translation
 * for a user space vma. */
static inline int translation_exists(struct vm_area_struct *vma,
				unsigned long addr, unsigned long pfn)
{
	pgd_t *pgd = pgd_offset(vma->vm_mm, addr);
	pmd_t *pmd;
	pte_t pte;

	if(pgd_none(*pgd))
		return 0;

	pmd = pmd_offset(pgd, addr);
	if(pmd_none(*pmd) || pmd_bad(*pmd))
		return 0;

	/* We cannot take the pte lock here: flush_cache_page is usually
	 * called with pte lock already held.  Whereas flush_dcache_page
	 * takes flush_dcache_mmap_lock, which is lower in the hierarchy:
	 * the vma itself is secure, but the pte might come or go racily.
	 */
	pte = *pte_offset_map(pmd, addr);
	/* But pte_unmap() does nothing on this architecture */

	/* Filter out coincidental file entries and swap entries */
	if (!(pte_val(pte) & (_PAGE_FLUSH|_PAGE_PRESENT)))
		return 0;

	return pte_pfn(pte) == pfn;
}

/* Private function to flush a page from the cache of a non-current
 * process.  cr25 contains the Page Directory of the current user
 * process; we're going to hijack both it and the user space %sr3 to
 * temporarily make the non-current process current.  We have to do
 * this because cache flushing may cause a non-access tlb miss which
 * the handlers have to fill in from the pgd of the non-current
 * process. */
static inline void
flush_user_cache_page_non_current(struct vm_area_struct *vma,
				  unsigned long vmaddr)
{
	/* save the current process space and pgd */
	unsigned long space = mfsp(3), pgd = mfctl(25);

	/* we don't mind taking interrups since they may not
	 * do anything with user space, but we can't
	 * be preempted here */
	preempt_disable();

	/* make us current */
	mtctl(__pa(vma->vm_mm->pgd), 25);
	mtsp(vma->vm_mm->context, 3);

	flush_user_dcache_page(vmaddr);
	if(vma->vm_flags & VM_EXEC)
		flush_user_icache_page(vmaddr);

	/* put the old current process back */
	mtsp(space, 3);
	mtctl(pgd, 25);
	preempt_enable();
}

static inline void
__flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	if (likely(vma->vm_mm->context == mfsp(3))) {
		flush_user_dcache_page(vmaddr);
		if (vma->vm_flags & VM_EXEC)
			flush_user_icache_page(vmaddr);
	} else {
		flush_user_cache_page_non_current(vma, vmaddr);
	}
}

static inline void
flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr, unsigned long pfn)
{
	BUG_ON(!vma->vm_mm->context);

	if (likely(translation_exists(vma, vmaddr, pfn)))
		__flush_cache_page(vma, vmaddr);

}

static inline void
flush_anon_page(struct page *page, unsigned long vmaddr)
{
	if (PageAnon(page))
		flush_user_dcache_page(vmaddr);
}
#define ARCH_HAS_FLUSH_ANON_PAGE

static inline void
flush_kernel_dcache_page(struct page *page)
{
	flush_kernel_dcache_page_asm(page_address(page));
}
#define ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE

#ifdef CONFIG_DEBUG_RODATA
void mark_rodata_ro(void);
#endif

#endif /* _PARISC_CACHEFLUSH_H */

