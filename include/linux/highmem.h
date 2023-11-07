/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HIGHMEM_H
#define _LINUX_HIGHMEM_H

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/bug.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>

#include <asm/cacheflush.h>

#ifndef ARCH_HAS_FLUSH_ANON_PAGE
static inline void flush_anon_page(struct vm_area_struct *vma, struct page *page, unsigned long vmaddr)
{
}
#endif

#ifndef ARCH_HAS_FLUSH_KERNEL_DCACHE_PAGE
static inline void flush_kernel_dcache_page(struct page *page)
{
}
static inline void flush_kernel_vmap_range(void *vaddr, int size)
{
}
static inline void invalidate_kernel_vmap_range(void *vaddr, int size)
{
}
#endif

#include <asm/kmap_types.h>

#ifdef CONFIG_HIGHMEM
extern void *kmap_atomic_high_prot(struct page *page, pgprot_t prot);
extern void kunmap_atomic_high(void *kvaddr);
#include <asm/highmem.h>

#ifndef ARCH_HAS_KMAP_FLUSH_TLB
static inline void kmap_flush_tlb(unsigned long addr) { }
#endif

#ifndef kmap_prot
#define kmap_prot PAGE_KERNEL
#endif

void *kmap_high(struct page *page);
static inline void *kmap(struct page *page)
{
	void *addr;

	might_sleep();
	if (!PageHighMem(page))
		addr = page_address(page);
	else
		addr = kmap_high(page);
	kmap_flush_tlb((unsigned long)addr);
	return addr;
}

void kunmap_high(struct page *page);

static inline void kunmap(struct page *page)
{
	might_sleep();
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}

/*
 * kmap_atomic/kunmap_atomic is significantly faster than kmap/kunmap because
 * no global lock is needed and because the kmap code must perform a global TLB
 * invalidation when the kmap pool wraps.
 *
 * However when holding an atomic kmap it is not legal to sleep, so atomic
 * kmaps are appropriate for short, tight code paths only.
 *
 * The use of kmap_atomic/kunmap_atomic is discouraged - kmap/kunmap
 * gives a more generic (and caching) interface. But kmap_atomic can
 * be used in IRQ contexts, so in some (very limited) cases we need
 * it.
 */
static inline void *kmap_atomic_prot(struct page *page, pgprot_t prot)
{
	preempt_disable();
	pagefault_disable();
	if (!PageHighMem(page))
		return page_address(page);
	return kmap_atomic_high_prot(page, prot);
}
#define kmap_atomic(page)	kmap_atomic_prot(page, kmap_prot)

/* declarations for linux/mm/highmem.c */
unsigned int nr_free_highpages(void);
extern atomic_long_t _totalhigh_pages;
static inline unsigned long totalhigh_pages(void)
{
	return (unsigned long)atomic_long_read(&_totalhigh_pages);
}

static inline void totalhigh_pages_inc(void)
{
	atomic_long_inc(&_totalhigh_pages);
}

static inline void totalhigh_pages_dec(void)
{
	atomic_long_dec(&_totalhigh_pages);
}

static inline void totalhigh_pages_add(long count)
{
	atomic_long_add(count, &_totalhigh_pages);
}

static inline void totalhigh_pages_set(long val)
{
	atomic_long_set(&_totalhigh_pages, val);
}

void kmap_flush_unused(void);

struct page *kmap_to_page(void *addr);

#else /* CONFIG_HIGHMEM */

static inline unsigned int nr_free_highpages(void) { return 0; }

static inline struct page *kmap_to_page(void *addr)
{
	return virt_to_page(addr);
}

static inline unsigned long totalhigh_pages(void) { return 0UL; }

static inline void *kmap(struct page *page)
{
	might_sleep();
	return page_address(page);
}

static inline void kunmap_high(struct page *page)
{
}

static inline void kunmap(struct page *page)
{
#ifdef ARCH_HAS_FLUSH_ON_KUNMAP
	kunmap_flush_on_unmap(page_address(page));
#endif
}

static inline void *kmap_atomic(struct page *page)
{
	preempt_disable();
	pagefault_disable();
	return page_address(page);
}
#define kmap_atomic_prot(page, prot)	kmap_atomic(page)

static inline void kunmap_atomic_high(void *addr)
{
	/*
	 * Mostly nothing to do in the CONFIG_HIGHMEM=n case as kunmap_atomic()
	 * handles re-enabling faults + preemption
	 */
#ifdef ARCH_HAS_FLUSH_ON_KUNMAP
	kunmap_flush_on_unmap(addr);
#endif
}

#define kmap_atomic_pfn(pfn)	kmap_atomic(pfn_to_page(pfn))

#define kmap_flush_unused()	do {} while(0)

#endif /* CONFIG_HIGHMEM */

#if defined(CONFIG_HIGHMEM) || defined(CONFIG_X86_32)

DECLARE_PER_CPU(int, __kmap_atomic_idx);

static inline int kmap_atomic_idx_push(void)
{
	int idx = __this_cpu_inc_return(__kmap_atomic_idx) - 1;

#ifdef CONFIG_DEBUG_HIGHMEM
	WARN_ON_ONCE(in_irq() && !irqs_disabled());
	BUG_ON(idx >= KM_TYPE_NR);
#endif
	return idx;
}

static inline int kmap_atomic_idx(void)
{
	return __this_cpu_read(__kmap_atomic_idx) - 1;
}

static inline void kmap_atomic_idx_pop(void)
{
#ifdef CONFIG_DEBUG_HIGHMEM
	int idx = __this_cpu_dec_return(__kmap_atomic_idx);

	BUG_ON(idx < 0);
#else
	__this_cpu_dec(__kmap_atomic_idx);
#endif
}

#endif

/*
 * Prevent people trying to call kunmap_atomic() as if it were kunmap()
 * kunmap_atomic() should get the return value of kmap_atomic, not the page.
 */
#define kunmap_atomic(addr)                                     \
do {                                                            \
	BUILD_BUG_ON(__same_type((addr), struct page *));       \
	kunmap_atomic_high(addr);                                  \
	pagefault_enable();                                     \
	preempt_enable();                                       \
} while (0)


/* when CONFIG_HIGHMEM is not set these will be plain clear/copy_page */
#ifndef clear_user_highpage
static inline void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *addr = kmap_atomic(page);
	clear_user_page(addr, vaddr, page);
	kunmap_atomic(addr);
}
#endif

#ifndef __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE_MOVABLE
/**
 * alloc_zeroed_user_highpage_movable - Allocate a zeroed HIGHMEM page for a VMA that the caller knows can move
 * @vma: The VMA the page is to be allocated for
 * @vaddr: The virtual address the page will be inserted into
 *
 * This function will allocate a page for a VMA that the caller knows will
 * be able to migrate in the future using move_pages() or reclaimed
 *
 * An architecture may override this function by defining
 * __HAVE_ARCH_ALLOC_ZEROED_USER_HIGHPAGE_MOVABLE and providing their own
 * implementation.
 */
static inline struct page *
alloc_zeroed_user_highpage_movable(struct vm_area_struct *vma,
				   unsigned long vaddr)
{
	struct page *page = alloc_page_vma(GFP_HIGHUSER_MOVABLE | __GFP_CMA, vma, vaddr);

	if (page)
		clear_user_highpage(page, vaddr);

	return page;
}
#endif

static inline void clear_highpage(struct page *page)
{
	void *kaddr = kmap_atomic(page);
	clear_page(kaddr);
	kunmap_atomic(kaddr);
}

#ifndef __HAVE_ARCH_TAG_CLEAR_HIGHPAGE

static inline void tag_clear_highpage(struct page *page)
{
}

#endif

static inline void zero_user_segments(struct page *page,
	unsigned start1, unsigned end1,
	unsigned start2, unsigned end2)
{
	void *kaddr = kmap_atomic(page);

	BUG_ON(end1 > PAGE_SIZE || end2 > PAGE_SIZE);

	if (end1 > start1)
		memset(kaddr + start1, 0, end1 - start1);

	if (end2 > start2)
		memset(kaddr + start2, 0, end2 - start2);

	kunmap_atomic(kaddr);
	flush_dcache_page(page);
}

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

	vfrom = kmap_atomic(from);
	vto = kmap_atomic(to);
	copy_user_page(vto, vfrom, vaddr, to);
	kunmap_atomic(vto);
	kunmap_atomic(vfrom);
}

#endif

#ifndef __HAVE_ARCH_COPY_HIGHPAGE

static inline void copy_highpage(struct page *to, struct page *from)
{
	char *vfrom, *vto;

	vfrom = kmap_atomic(from);
	vto = kmap_atomic(to);
	copy_page(vto, vfrom);
	kunmap_atomic(vto);
	kunmap_atomic(vfrom);
}

#endif

static inline void memcpy_from_page(char *to, struct page *page,
				    size_t offset, size_t len)
{
	char *from = kmap_atomic(page);

	memcpy(to, from + offset, len);
	kunmap_atomic(from);
}

static inline void memcpy_to_page(struct page *page, size_t offset,
				  const char *from, size_t len)
{
	char *to = kmap_atomic(page);

	memcpy(to + offset, from, len);
	kunmap_atomic(to);
}

#endif /* _LINUX_HIGHMEM_H */
