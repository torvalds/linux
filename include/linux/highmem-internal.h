/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HIGHMEM_INTERNAL_H
#define _LINUX_HIGHMEM_INTERNAL_H

/*
 * Outside of CONFIG_HIGHMEM to support X86 32bit iomap_atomic() cruft.
 */
#ifdef CONFIG_KMAP_LOCAL
void *__kmap_local_pfn_prot(unsigned long pfn, pgprot_t prot);
void *__kmap_local_page_prot(struct page *page, pgprot_t prot);
void kunmap_local_indexed(const void *vaddr);
void kmap_local_fork(struct task_struct *tsk);
void __kmap_local_sched_out(void);
void __kmap_local_sched_in(void);
static inline void kmap_assert_nomap(void)
{
	DEBUG_LOCKS_WARN_ON(current->kmap_ctrl.idx);
}
#else
static inline void kmap_local_fork(struct task_struct *tsk) { }
static inline void kmap_assert_nomap(void) { }
#endif

#ifdef CONFIG_HIGHMEM
#include <asm/highmem.h>

#ifndef ARCH_HAS_KMAP_FLUSH_TLB
static inline void kmap_flush_tlb(unsigned long addr) { }
#endif

#ifndef kmap_prot
#define kmap_prot PAGE_KERNEL
#endif

void *kmap_high(struct page *page);
void kunmap_high(struct page *page);
void __kmap_flush_unused(void);
struct page *__kmap_to_page(void *addr);

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

static inline void kunmap(struct page *page)
{
	might_sleep();
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}

static inline struct page *kmap_to_page(void *addr)
{
	return __kmap_to_page(addr);
}

static inline void kmap_flush_unused(void)
{
	__kmap_flush_unused();
}

static inline void *kmap_local_page(struct page *page)
{
	return __kmap_local_page_prot(page, kmap_prot);
}

static inline void *kmap_local_folio(struct folio *folio, size_t offset)
{
	struct page *page = folio_page(folio, offset / PAGE_SIZE);
	return __kmap_local_page_prot(page, kmap_prot) + offset % PAGE_SIZE;
}

static inline void *kmap_local_page_prot(struct page *page, pgprot_t prot)
{
	return __kmap_local_page_prot(page, prot);
}

static inline void *kmap_local_pfn(unsigned long pfn)
{
	return __kmap_local_pfn_prot(pfn, kmap_prot);
}

static inline void __kunmap_local(const void *vaddr)
{
	kunmap_local_indexed(vaddr);
}

static inline void *kmap_atomic_prot(struct page *page, pgprot_t prot)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		migrate_disable();
	else
		preempt_disable();

	pagefault_disable();
	return __kmap_local_page_prot(page, prot);
}

static inline void *kmap_atomic(struct page *page)
{
	return kmap_atomic_prot(page, kmap_prot);
}

static inline void *kmap_atomic_pfn(unsigned long pfn)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		migrate_disable();
	else
		preempt_disable();

	pagefault_disable();
	return __kmap_local_pfn_prot(pfn, kmap_prot);
}

static inline void __kunmap_atomic(const void *addr)
{
	kunmap_local_indexed(addr);
	pagefault_enable();
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		migrate_enable();
	else
		preempt_enable();
}

unsigned long __nr_free_highpages(void);
unsigned long __totalhigh_pages(void);

static inline unsigned long nr_free_highpages(void)
{
	return __nr_free_highpages();
}

static inline unsigned long totalhigh_pages(void)
{
	return __totalhigh_pages();
}

static inline bool is_kmap_addr(const void *x)
{
	unsigned long addr = (unsigned long)x;

	return (addr >= PKMAP_ADDR(0) && addr < PKMAP_ADDR(LAST_PKMAP)) ||
		(addr >= __fix_to_virt(FIX_KMAP_END) &&
		 addr < __fix_to_virt(FIX_KMAP_BEGIN));
}
#else /* CONFIG_HIGHMEM */

static inline struct page *kmap_to_page(void *addr)
{
	return virt_to_page(addr);
}

static inline void *kmap(struct page *page)
{
	might_sleep();
	return page_address(page);
}

static inline void kunmap_high(struct page *page) { }
static inline void kmap_flush_unused(void) { }

static inline void kunmap(struct page *page)
{
#ifdef ARCH_HAS_FLUSH_ON_KUNMAP
	kunmap_flush_on_unmap(page_address(page));
#endif
}

static inline void *kmap_local_page(struct page *page)
{
	return page_address(page);
}

static inline void *kmap_local_folio(struct folio *folio, size_t offset)
{
	return page_address(&folio->page) + offset;
}

static inline void *kmap_local_page_prot(struct page *page, pgprot_t prot)
{
	return kmap_local_page(page);
}

static inline void *kmap_local_pfn(unsigned long pfn)
{
	return kmap_local_page(pfn_to_page(pfn));
}

static inline void __kunmap_local(const void *addr)
{
#ifdef ARCH_HAS_FLUSH_ON_KUNMAP
	kunmap_flush_on_unmap(PTR_ALIGN_DOWN(addr, PAGE_SIZE));
#endif
}

static inline void *kmap_atomic(struct page *page)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		migrate_disable();
	else
		preempt_disable();
	pagefault_disable();
	return page_address(page);
}

static inline void *kmap_atomic_prot(struct page *page, pgprot_t prot)
{
	return kmap_atomic(page);
}

static inline void *kmap_atomic_pfn(unsigned long pfn)
{
	return kmap_atomic(pfn_to_page(pfn));
}

static inline void __kunmap_atomic(const void *addr)
{
#ifdef ARCH_HAS_FLUSH_ON_KUNMAP
	kunmap_flush_on_unmap(PTR_ALIGN_DOWN(addr, PAGE_SIZE));
#endif
	pagefault_enable();
	if (IS_ENABLED(CONFIG_PREEMPT_RT))
		migrate_enable();
	else
		preempt_enable();
}

static inline unsigned long nr_free_highpages(void) { return 0; }
static inline unsigned long totalhigh_pages(void) { return 0; }

static inline bool is_kmap_addr(const void *x)
{
	return false;
}

#endif /* CONFIG_HIGHMEM */

/**
 * kunmap_atomic - Unmap the virtual address mapped by kmap_atomic() - deprecated!
 * @__addr:       Virtual address to be unmapped
 *
 * Unmaps an address previously mapped by kmap_atomic() and re-enables
 * pagefaults. Depending on PREEMP_RT configuration, re-enables also
 * migration and preemption. Users should not count on these side effects.
 *
 * Mappings should be unmapped in the reverse order that they were mapped.
 * See kmap_local_page() for details on nesting.
 *
 * @__addr can be any address within the mapped page, so there is no need
 * to subtract any offset that has been added. In contrast to kunmap(),
 * this function takes the address returned from kmap_atomic(), not the
 * page passed to it. The compiler will warn you if you pass the page.
 */
#define kunmap_atomic(__addr)					\
do {								\
	BUILD_BUG_ON(__same_type((__addr), struct page *));	\
	__kunmap_atomic(__addr);				\
} while (0)

/**
 * kunmap_local - Unmap a page mapped via kmap_local_page().
 * @__addr: An address within the page mapped
 *
 * @__addr can be any address within the mapped page.  Commonly it is the
 * address return from kmap_local_page(), but it can also include offsets.
 *
 * Unmapping should be done in the reverse order of the mapping.  See
 * kmap_local_page() for details.
 */
#define kunmap_local(__addr)					\
do {								\
	BUILD_BUG_ON(__same_type((__addr), struct page *));	\
	__kunmap_local(__addr);					\
} while (0)

#endif
