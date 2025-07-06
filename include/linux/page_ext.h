/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGE_EXT_H
#define __LINUX_PAGE_EXT_H

#include <linux/types.h>
#include <linux/mmzone.h>
#include <linux/stacktrace.h>

struct pglist_data;

#ifdef CONFIG_PAGE_EXTENSION
/**
 * struct page_ext_operations - per page_ext client operations
 * @offset: Offset to the client's data within page_ext. Offset is returned to
 *          the client by page_ext_init.
 * @size: The size of the client data within page_ext.
 * @need: Function that returns true if client requires page_ext.
 * @init: (optional) Called to initialize client once page_exts are allocated.
 * @need_shared_flags: True when client is using shared page_ext->flags
 *                     field.
 *
 * Each Page Extension client must define page_ext_operations in
 * page_ext_ops array.
 */
struct page_ext_operations {
	size_t offset;
	size_t size;
	bool (*need)(void);
	void (*init)(void);
	bool need_shared_flags;
};

/*
 * The page_ext_flags users must set need_shared_flags to true.
 */
enum page_ext_flags {
	PAGE_EXT_OWNER,
	PAGE_EXT_OWNER_ALLOCATED,
#if defined(CONFIG_PAGE_IDLE_FLAG) && !defined(CONFIG_64BIT)
	PAGE_EXT_YOUNG,
	PAGE_EXT_IDLE,
#endif
};

/*
 * Page Extension can be considered as an extended mem_map.
 * A page_ext page is associated with every page descriptor. The
 * page_ext helps us add more information about the page.
 * All page_ext are allocated at boot or memory hotplug event,
 * then the page_ext for pfn always exists.
 */
struct page_ext {
	unsigned long flags;
};

extern bool early_page_ext;
extern unsigned long page_ext_size;
extern void pgdat_page_ext_init(struct pglist_data *pgdat);

static inline bool early_page_ext_enabled(void)
{
	return early_page_ext;
}

#ifdef CONFIG_SPARSEMEM
static inline void page_ext_init_flatmem(void)
{
}
extern void page_ext_init(void);
static inline void page_ext_init_flatmem_late(void)
{
}

static inline bool page_ext_iter_next_fast_possible(unsigned long next_pfn)
{
	/*
	 * page_ext is allocated per memory section. Once we cross a
	 * memory section, we have to fetch the new pointer.
	 */
	return next_pfn % PAGES_PER_SECTION;
}
#else
extern void page_ext_init_flatmem(void);
extern void page_ext_init_flatmem_late(void);
static inline void page_ext_init(void)
{
}

static inline bool page_ext_iter_next_fast_possible(unsigned long next_pfn)
{
	return true;
}
#endif

extern struct page_ext *page_ext_get(const struct page *page);
extern void page_ext_put(struct page_ext *page_ext);
extern struct page_ext *page_ext_lookup(unsigned long pfn);

static inline void *page_ext_data(struct page_ext *page_ext,
				  struct page_ext_operations *ops)
{
	return (void *)(page_ext) + ops->offset;
}

static inline struct page_ext *page_ext_next(struct page_ext *curr)
{
	void *next = curr;
	next += page_ext_size;
	return next;
}

struct page_ext_iter {
	unsigned long index;
	unsigned long start_pfn;
	struct page_ext *page_ext;
};

/**
 * page_ext_iter_begin() - Prepare for iterating through page extensions.
 * @iter: page extension iterator.
 * @pfn: PFN of the page we're interested in.
 *
 * Must be called with RCU read lock taken.
 *
 * Return: NULL if no page_ext exists for this page.
 */
static inline struct page_ext *page_ext_iter_begin(struct page_ext_iter *iter,
						unsigned long pfn)
{
	iter->index = 0;
	iter->start_pfn = pfn;
	iter->page_ext = page_ext_lookup(pfn);

	return iter->page_ext;
}

/**
 * page_ext_iter_next() - Get next page extension
 * @iter: page extension iterator.
 *
 * Must be called with RCU read lock taken.
 *
 * Return: NULL if no next page_ext exists.
 */
static inline struct page_ext *page_ext_iter_next(struct page_ext_iter *iter)
{
	unsigned long pfn;

	if (WARN_ON_ONCE(!iter->page_ext))
		return NULL;

	iter->index++;
	pfn = iter->start_pfn + iter->index;

	if (page_ext_iter_next_fast_possible(pfn))
		iter->page_ext = page_ext_next(iter->page_ext);
	else
		iter->page_ext = page_ext_lookup(pfn);

	return iter->page_ext;
}

/**
 * page_ext_iter_get() - Get current page extension
 * @iter: page extension iterator.
 *
 * Return: NULL if no page_ext exists for this iterator.
 */
static inline struct page_ext *page_ext_iter_get(const struct page_ext_iter *iter)
{
	return iter->page_ext;
}

/**
 * for_each_page_ext(): iterate through page_ext objects.
 * @__page: the page we're interested in
 * @__pgcount: how many pages to iterate through
 * @__page_ext: struct page_ext pointer where the current page_ext
 *              object is returned
 * @__iter: struct page_ext_iter object (defined in the stack)
 *
 * IMPORTANT: must be called with RCU read lock taken.
 */
#define for_each_page_ext(__page, __pgcount, __page_ext, __iter) \
	for (__page_ext = page_ext_iter_begin(&__iter, page_to_pfn(__page));\
		__page_ext && __iter.index < __pgcount;          \
		__page_ext = page_ext_iter_next(&__iter))

#else /* !CONFIG_PAGE_EXTENSION */
struct page_ext;

static inline bool early_page_ext_enabled(void)
{
	return false;
}

static inline void pgdat_page_ext_init(struct pglist_data *pgdat)
{
}

static inline void page_ext_init(void)
{
}

static inline void page_ext_init_flatmem_late(void)
{
}

static inline void page_ext_init_flatmem(void)
{
}

static inline struct page_ext *page_ext_get(const struct page *page)
{
	return NULL;
}

static inline void page_ext_put(struct page_ext *page_ext)
{
}
#endif /* CONFIG_PAGE_EXTENSION */
#endif /* __LINUX_PAGE_EXT_H */
