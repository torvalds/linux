/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGE_EXT_H
#define __LINUX_PAGE_EXT_H

#include <linux/types.h>
#include <linux/stacktrace.h>
#include <linux/stackdepot.h>

struct pglist_data;
struct page_ext_operations {
	size_t offset;
	size_t size;
	bool (*need)(void);
	void (*init)(void);
};

#ifdef CONFIG_PAGE_EXTENSION

enum page_ext_flags {
	PAGE_EXT_OWNER,
	PAGE_EXT_OWNER_ALLOCATED,
#if defined(CONFIG_PAGE_PINNER)
	/* page refcount was increased by GUP or follow_page(FOLL_GET) */
	PAGE_EXT_GET,
	/* page migration failed */
	PAGE_EXT_PINNER_MIGRATION_FAILED,
#endif
#if defined(CONFIG_IDLE_PAGE_TRACKING) && !defined(CONFIG_64BIT)
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

extern unsigned long page_ext_size;
extern void pgdat_page_ext_init(struct pglist_data *pgdat);

#ifdef CONFIG_SPARSEMEM
static inline void page_ext_init_flatmem(void)
{
}
extern void page_ext_init(void);
#else
extern void page_ext_init_flatmem(void);
static inline void page_ext_init(void)
{
}
#endif

struct page_ext *lookup_page_ext(const struct page *page);

static inline struct page_ext *page_ext_next(struct page_ext *curr)
{
	void *next = curr;
	next += page_ext_size;
	return next;
}

#else /* !CONFIG_PAGE_EXTENSION */
struct page_ext;

static inline void pgdat_page_ext_init(struct pglist_data *pgdat)
{
}

static inline struct page_ext *lookup_page_ext(const struct page *page)
{
	return NULL;
}

static inline void page_ext_init(void)
{
}

static inline void page_ext_init_flatmem(void)
{
}
#endif /* CONFIG_PAGE_EXTENSION */
#endif /* __LINUX_PAGE_EXT_H */
