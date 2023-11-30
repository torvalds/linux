/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/cacheflush.h>
#include <linux/kmsan.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>

#include "highmem-internal.h"

/**
 * kmap - Map a page for long term usage
 * @page:	Pointer to the page to be mapped
 *
 * Returns: The virtual address of the mapping
 *
 * Can only be invoked from preemptible task context because on 32bit
 * systems with CONFIG_HIGHMEM enabled this function might sleep.
 *
 * For systems with CONFIG_HIGHMEM=n and for pages in the low memory area
 * this returns the virtual address of the direct kernel mapping.
 *
 * The returned virtual address is globally visible and valid up to the
 * point where it is unmapped via kunmap(). The pointer can be handed to
 * other contexts.
 *
 * For highmem pages on 32bit systems this can be slow as the mapping space
 * is limited and protected by a global lock. In case that there is no
 * mapping slot available the function blocks until a slot is released via
 * kunmap().
 */
static inline void *kmap(struct page *page);

/**
 * kunmap - Unmap the virtual address mapped by kmap()
 * @page:	Pointer to the page which was mapped by kmap()
 *
 * Counterpart to kmap(). A NOOP for CONFIG_HIGHMEM=n and for mappings of
 * pages in the low memory area.
 */
static inline void kunmap(struct page *page);

/**
 * kmap_to_page - Get the page for a kmap'ed address
 * @addr:	The address to look up
 *
 * Returns: The page which is mapped to @addr.
 */
static inline struct page *kmap_to_page(void *addr);

/**
 * kmap_flush_unused - Flush all unused kmap mappings in order to
 *		       remove stray mappings
 */
static inline void kmap_flush_unused(void);

/**
 * kmap_local_page - Map a page for temporary usage
 * @page: Pointer to the page to be mapped
 *
 * Returns: The virtual address of the mapping
 *
 * Can be invoked from any context, including interrupts.
 *
 * Requires careful handling when nesting multiple mappings because the map
 * management is stack based. The unmap has to be in the reverse order of
 * the map operation:
 *
 * addr1 = kmap_local_page(page1);
 * addr2 = kmap_local_page(page2);
 * ...
 * kunmap_local(addr2);
 * kunmap_local(addr1);
 *
 * Unmapping addr1 before addr2 is invalid and causes malfunction.
 *
 * Contrary to kmap() mappings the mapping is only valid in the context of
 * the caller and cannot be handed to other contexts.
 *
 * On CONFIG_HIGHMEM=n kernels and for low memory pages this returns the
 * virtual address of the direct mapping. Only real highmem pages are
 * temporarily mapped.
 *
 * While kmap_local_page() is significantly faster than kmap() for the highmem
 * case it comes with restrictions about the pointer validity.
 *
 * On HIGHMEM enabled systems mapping a highmem page has the side effect of
 * disabling migration in order to keep the virtual address stable across
 * preemption. No caller of kmap_local_page() can rely on this side effect.
 */
static inline void *kmap_local_page(struct page *page);

/**
 * kmap_local_folio - Map a page in this folio for temporary usage
 * @folio: The folio containing the page.
 * @offset: The byte offset within the folio which identifies the page.
 *
 * Requires careful handling when nesting multiple mappings because the map
 * management is stack based. The unmap has to be in the reverse order of
 * the map operation::
 *
 *   addr1 = kmap_local_folio(folio1, offset1);
 *   addr2 = kmap_local_folio(folio2, offset2);
 *   ...
 *   kunmap_local(addr2);
 *   kunmap_local(addr1);
 *
 * Unmapping addr1 before addr2 is invalid and causes malfunction.
 *
 * Contrary to kmap() mappings the mapping is only valid in the context of
 * the caller and cannot be handed to other contexts.
 *
 * On CONFIG_HIGHMEM=n kernels and for low memory pages this returns the
 * virtual address of the direct mapping. Only real highmem pages are
 * temporarily mapped.
 *
 * While it is significantly faster than kmap() for the highmem case it
 * comes with restrictions about the pointer validity.
 *
 * On HIGHMEM enabled systems mapping a highmem page has the side effect of
 * disabling migration in order to keep the virtual address stable across
 * preemption. No caller of kmap_local_folio() can rely on this side effect.
 *
 * Context: Can be invoked from any context.
 * Return: The virtual address of @offset.
 */
static inline void *kmap_local_folio(struct folio *folio, size_t offset);

/**
 * kmap_atomic - Atomically map a page for temporary usage - Deprecated!
 * @page:	Pointer to the page to be mapped
 *
 * Returns: The virtual address of the mapping
 *
 * In fact a wrapper around kmap_local_page() which also disables pagefaults
 * and, depending on PREEMPT_RT configuration, also CPU migration and
 * preemption. Therefore users should not count on the latter two side effects.
 *
 * Mappings should always be released by kunmap_atomic().
 *
 * Do not use in new code. Use kmap_local_page() instead.
 *
 * It is used in atomic context when code wants to access the contents of a
 * page that might be allocated from high memory (see __GFP_HIGHMEM), for
 * example a page in the pagecache.  The API has two functions, and they
 * can be used in a manner similar to the following::
 *
 *   // Find the page of interest.
 *   struct page *page = find_get_page(mapping, offset);
 *
 *   // Gain access to the contents of that page.
 *   void *vaddr = kmap_atomic(page);
 *
 *   // Do something to the contents of that page.
 *   memset(vaddr, 0, PAGE_SIZE);
 *
 *   // Unmap that page.
 *   kunmap_atomic(vaddr);
 *
 * Note that the kunmap_atomic() call takes the result of the kmap_atomic()
 * call, not the argument.
 *
 * If you need to map two pages because you want to copy from one page to
 * another you need to keep the kmap_atomic calls strictly nested, like:
 *
 * vaddr1 = kmap_atomic(page1);
 * vaddr2 = kmap_atomic(page2);
 *
 * memcpy(vaddr1, vaddr2, PAGE_SIZE);
 *
 * kunmap_atomic(vaddr2);
 * kunmap_atomic(vaddr1);
 */
static inline void *kmap_atomic(struct page *page);

/* Highmem related interfaces for management code */
static inline unsigned int nr_free_highpages(void);
static inline unsigned long totalhigh_pages(void);

#ifndef ARCH_HAS_FLUSH_ANON_PAGE
static inline void flush_anon_page(struct vm_area_struct *vma, struct page *page, unsigned long vmaddr)
{
}
#endif

#ifndef ARCH_IMPLEMENTS_FLUSH_KERNEL_VMAP_RANGE
static inline void flush_kernel_vmap_range(void *vaddr, int size)
{
}
static inline void invalidate_kernel_vmap_range(void *vaddr, int size)
{
}
#endif

/* when CONFIG_HIGHMEM is not set these will be plain clear/copy_page */
#ifndef clear_user_highpage
static inline void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *addr = kmap_local_page(page);
	clear_user_page(addr, vaddr, page);
	kunmap_local(addr);
}
#endif

#ifndef vma_alloc_zeroed_movable_folio
/**
 * vma_alloc_zeroed_movable_folio - Allocate a zeroed page for a VMA.
 * @vma: The VMA the page is to be allocated for.
 * @vaddr: The virtual address the page will be inserted into.
 *
 * This function will allocate a page suitable for inserting into this
 * VMA at this virtual address.  It may be allocated from highmem or
 * the movable zone.  An architecture may provide its own implementation.
 *
 * Return: A folio containing one allocated and zeroed page or NULL if
 * we are out of memory.
 */
static inline
struct folio *vma_alloc_zeroed_movable_folio(struct vm_area_struct *vma,
				   unsigned long vaddr)
{
	struct folio *folio;

	folio = vma_alloc_folio(GFP_HIGHUSER_MOVABLE, 0, vma, vaddr, false);
	if (folio)
		clear_user_highpage(&folio->page, vaddr);

	return folio;
}
#endif

static inline void clear_highpage(struct page *page)
{
	void *kaddr = kmap_local_page(page);
	clear_page(kaddr);
	kunmap_local(kaddr);
}

static inline void clear_highpage_kasan_tagged(struct page *page)
{
	void *kaddr = kmap_local_page(page);

	clear_page(kasan_reset_tag(kaddr));
	kunmap_local(kaddr);
}

#ifndef __HAVE_ARCH_TAG_CLEAR_HIGHPAGE

static inline void tag_clear_highpage(struct page *page)
{
}

#endif

/*
 * If we pass in a base or tail page, we can zero up to PAGE_SIZE.
 * If we pass in a head page, we can zero up to the size of the compound page.
 */
#ifdef CONFIG_HIGHMEM
void zero_user_segments(struct page *page, unsigned start1, unsigned end1,
		unsigned start2, unsigned end2);
#else
static inline void zero_user_segments(struct page *page,
		unsigned start1, unsigned end1,
		unsigned start2, unsigned end2)
{
	void *kaddr = kmap_local_page(page);
	unsigned int i;

	BUG_ON(end1 > page_size(page) || end2 > page_size(page));

	if (end1 > start1)
		memset(kaddr + start1, 0, end1 - start1);

	if (end2 > start2)
		memset(kaddr + start2, 0, end2 - start2);

	kunmap_local(kaddr);
	for (i = 0; i < compound_nr(page); i++)
		flush_dcache_page(page + i);
}
#endif

static inline void zero_user_segment(struct page *page,
	unsigned start, unsigned end)
{
	zero_user_segments(page, start, end, 0, 0);
}

static inline void zero_user(struct page *page,
	unsigned start, unsigned size)
{
	zero_user_segments(page, start, start + size, 0, 0);
}

#ifndef __HAVE_ARCH_COPY_USER_HIGHPAGE

static inline void copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	char *vfrom, *vto;

	vfrom = kmap_local_page(from);
	vto = kmap_local_page(to);
	copy_user_page(vto, vfrom, vaddr, to);
	kmsan_unpoison_memory(page_address(to), PAGE_SIZE);
	kunmap_local(vto);
	kunmap_local(vfrom);
}

#endif

#ifndef __HAVE_ARCH_COPY_HIGHPAGE

static inline void copy_highpage(struct page *to, struct page *from)
{
	char *vfrom, *vto;

	vfrom = kmap_local_page(from);
	vto = kmap_local_page(to);
	copy_page(vto, vfrom);
	kmsan_copy_page_meta(to, from);
	kunmap_local(vto);
	kunmap_local(vfrom);
}

#endif

#ifdef copy_mc_to_kernel
/*
 * If architecture supports machine check exception handling, define the
 * #MC versions of copy_user_highpage and copy_highpage. They copy a memory
 * page with #MC in source page (@from) handled, and return the number
 * of bytes not copied if there was a #MC, otherwise 0 for success.
 */
static inline int copy_mc_user_highpage(struct page *to, struct page *from,
					unsigned long vaddr, struct vm_area_struct *vma)
{
	unsigned long ret;
	char *vfrom, *vto;

	vfrom = kmap_local_page(from);
	vto = kmap_local_page(to);
	ret = copy_mc_to_kernel(vto, vfrom, PAGE_SIZE);
	if (!ret)
		kmsan_unpoison_memory(page_address(to), PAGE_SIZE);
	kunmap_local(vto);
	kunmap_local(vfrom);

	return ret;
}

static inline int copy_mc_highpage(struct page *to, struct page *from)
{
	unsigned long ret;
	char *vfrom, *vto;

	vfrom = kmap_local_page(from);
	vto = kmap_local_page(to);
	ret = copy_mc_to_kernel(vto, vfrom, PAGE_SIZE);
	if (!ret)
		kmsan_copy_page_meta(to, from);
	kunmap_local(vto);
	kunmap_local(vfrom);

	return ret;
}
#else
static inline int copy_mc_user_highpage(struct page *to, struct page *from,
					unsigned long vaddr, struct vm_area_struct *vma)
{
	copy_user_highpage(to, from, vaddr, vma);
	return 0;
}

static inline int copy_mc_highpage(struct page *to, struct page *from)
{
	copy_highpage(to, from);
	return 0;
}
#endif

static inline void memcpy_page(struct page *dst_page, size_t dst_off,
			       struct page *src_page, size_t src_off,
			       size_t len)
{
	char *dst = kmap_local_page(dst_page);
	char *src = kmap_local_page(src_page);

	VM_BUG_ON(dst_off + len > PAGE_SIZE || src_off + len > PAGE_SIZE);
	memcpy(dst + dst_off, src + src_off, len);
	kunmap_local(src);
	kunmap_local(dst);
}

static inline void memset_page(struct page *page, size_t offset, int val,
			       size_t len)
{
	char *addr = kmap_local_page(page);

	VM_BUG_ON(offset + len > PAGE_SIZE);
	memset(addr + offset, val, len);
	kunmap_local(addr);
}

static inline void memcpy_from_page(char *to, struct page *page,
				    size_t offset, size_t len)
{
	char *from = kmap_local_page(page);

	VM_BUG_ON(offset + len > PAGE_SIZE);
	memcpy(to, from + offset, len);
	kunmap_local(from);
}

static inline void memcpy_to_page(struct page *page, size_t offset,
				  const char *from, size_t len)
{
	char *to = kmap_local_page(page);

	VM_BUG_ON(offset + len > PAGE_SIZE);
	memcpy(to + offset, from, len);
	flush_dcache_page(page);
	kunmap_local(to);
}

static inline void memzero_page(struct page *page, size_t offset, size_t len)
{
	char *addr = kmap_local_page(page);

	VM_BUG_ON(offset + len > PAGE_SIZE);
	memset(addr + offset, 0, len);
	flush_dcache_page(page);
	kunmap_local(addr);
}

static inline void memcpy_from_folio(char *to, struct folio *folio,
		size_t offset, size_t len)
{
	VM_BUG_ON(offset + len > folio_size(folio));

	do {
		const char *from = kmap_local_folio(folio, offset);
		size_t chunk = len;

		if (folio_test_highmem(folio) &&
		    chunk > PAGE_SIZE - offset_in_page(offset))
			chunk = PAGE_SIZE - offset_in_page(offset);
		memcpy(to, from, chunk);
		kunmap_local(from);

		to += chunk;
		offset += chunk;
		len -= chunk;
	} while (len > 0);
}

static inline void memcpy_to_folio(struct folio *folio, size_t offset,
		const char *from, size_t len)
{
	VM_BUG_ON(offset + len > folio_size(folio));

	do {
		char *to = kmap_local_folio(folio, offset);
		size_t chunk = len;

		if (folio_test_highmem(folio) &&
		    chunk > PAGE_SIZE - offset_in_page(offset))
			chunk = PAGE_SIZE - offset_in_page(offset);
		memcpy(to, from, chunk);
		kunmap_local(to);

		from += chunk;
		offset += chunk;
		len -= chunk;
	} while (len > 0);

	flush_dcache_folio(folio);
}

/**
 * memcpy_from_file_folio - Copy some bytes from a file folio.
 * @to: The destination buffer.
 * @folio: The folio to copy from.
 * @pos: The position in the file.
 * @len: The maximum number of bytes to copy.
 *
 * Copy up to @len bytes from this folio.  This may be limited by PAGE_SIZE
 * if the folio comes from HIGHMEM, and by the size of the folio.
 *
 * Return: The number of bytes copied from the folio.
 */
static inline size_t memcpy_from_file_folio(char *to, struct folio *folio,
		loff_t pos, size_t len)
{
	size_t offset = offset_in_folio(folio, pos);
	char *from = kmap_local_folio(folio, offset);

	if (folio_test_highmem(folio)) {
		offset = offset_in_page(offset);
		len = min_t(size_t, len, PAGE_SIZE - offset);
	} else
		len = min(len, folio_size(folio) - offset);

	memcpy(to, from, len);
	kunmap_local(from);

	return len;
}

/**
 * folio_zero_segments() - Zero two byte ranges in a folio.
 * @folio: The folio to write to.
 * @start1: The first byte to zero.
 * @xend1: One more than the last byte in the first range.
 * @start2: The first byte to zero in the second range.
 * @xend2: One more than the last byte in the second range.
 */
static inline void folio_zero_segments(struct folio *folio,
		size_t start1, size_t xend1, size_t start2, size_t xend2)
{
	zero_user_segments(&folio->page, start1, xend1, start2, xend2);
}

/**
 * folio_zero_segment() - Zero a byte range in a folio.
 * @folio: The folio to write to.
 * @start: The first byte to zero.
 * @xend: One more than the last byte to zero.
 */
static inline void folio_zero_segment(struct folio *folio,
		size_t start, size_t xend)
{
	zero_user_segments(&folio->page, start, xend, 0, 0);
}

/**
 * folio_zero_range() - Zero a byte range in a folio.
 * @folio: The folio to write to.
 * @start: The first byte to zero.
 * @length: The number of bytes to zero.
 */
static inline void folio_zero_range(struct folio *folio,
		size_t start, size_t length)
{
	zero_user_segments(&folio->page, start, start + length, 0, 0);
}

static inline void unmap_and_put_page(struct page *page, void *addr)
{
	kunmap_local(addr);
	put_page(page);
}

#endif /* _LINUX_HIGHMEM_H */
