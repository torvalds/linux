/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGE_EXT_H
#define __LINUX_PAGE_EXT_H

#include <linux/types.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>

struct pglist_data;

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

#ifdef CONFIG_PAGE_EXTENSION

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
#else
extern void page_ext_init_flatmem(void);
extern void page_ext_init_flatmem_late(void);
static inline void page_ext_init(void)
{
}
#endif

extern struct page_ext *page_ext_get(struct page *page);
extern void page_ext_put(struct page_ext *page_ext);

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

static inline struct page_ext *page_ext_get(struct page *page)
{
	return NULL;
}

static inline void page_ext_put(struct page_ext *page_ext)
{
}
#endif /* CONFIG_PAGE_EXTENSION */
#endif /* __LINUX_PAGE_EXT_H */
