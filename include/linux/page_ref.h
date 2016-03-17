#ifndef _LINUX_PAGE_REF_H
#define _LINUX_PAGE_REF_H

#include <linux/atomic.h>
#include <linux/mm_types.h>
#include <linux/page-flags.h>

static inline int page_ref_count(struct page *page)
{
	return atomic_read(&page->_count);
}

static inline int page_count(struct page *page)
{
	return atomic_read(&compound_head(page)->_count);
}

static inline void set_page_count(struct page *page, int v)
{
	atomic_set(&page->_count, v);
}

/*
 * Setup the page count before being freed into the page allocator for
 * the first time (boot or memory hotplug)
 */
static inline void init_page_count(struct page *page)
{
	set_page_count(page, 1);
}

static inline void page_ref_add(struct page *page, int nr)
{
	atomic_add(nr, &page->_count);
}

static inline void page_ref_sub(struct page *page, int nr)
{
	atomic_sub(nr, &page->_count);
}

static inline void page_ref_inc(struct page *page)
{
	atomic_inc(&page->_count);
}

static inline void page_ref_dec(struct page *page)
{
	atomic_dec(&page->_count);
}

static inline int page_ref_sub_and_test(struct page *page, int nr)
{
	return atomic_sub_and_test(nr, &page->_count);
}

static inline int page_ref_dec_and_test(struct page *page)
{
	return atomic_dec_and_test(&page->_count);
}

static inline int page_ref_dec_return(struct page *page)
{
	return atomic_dec_return(&page->_count);
}

static inline int page_ref_add_unless(struct page *page, int nr, int u)
{
	return atomic_add_unless(&page->_count, nr, u);
}

static inline int page_ref_freeze(struct page *page, int count)
{
	return likely(atomic_cmpxchg(&page->_count, count, 0) == count);
}

static inline void page_ref_unfreeze(struct page *page, int count)
{
	VM_BUG_ON_PAGE(page_count(page) != 0, page);
	VM_BUG_ON(count == 0);

	atomic_set(&page->_count, count);
}

#endif
