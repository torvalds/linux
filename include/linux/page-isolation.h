/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_PAGEISOLATION_H
#define __LINUX_PAGEISOLATION_H

#ifdef CONFIG_MEMORY_ISOLATION
static inline bool is_migrate_isolate_page(struct page *page)
{
	return get_pageblock_migratetype(page) == MIGRATE_ISOLATE;
}
static inline bool is_migrate_isolate(int migratetype)
{
	return migratetype == MIGRATE_ISOLATE;
}
#define get_pageblock_isolate(page) \
	get_pfnblock_bit(page, page_to_pfn(page), PB_migrate_isolate)
#define clear_pageblock_isolate(page) \
	clear_pfnblock_bit(page, page_to_pfn(page), PB_migrate_isolate)
#define set_pageblock_isolate(page) \
	set_pfnblock_bit(page, page_to_pfn(page), PB_migrate_isolate)
#else
static inline bool is_migrate_isolate_page(struct page *page)
{
	return false;
}
static inline bool is_migrate_isolate(int migratetype)
{
	return false;
}
static inline bool get_pageblock_isolate(struct page *page)
{
	return false;
}
static inline void clear_pageblock_isolate(struct page *page)
{
}
static inline void set_pageblock_isolate(struct page *page)
{
}
#endif

#define MEMORY_OFFLINE	0x1
#define REPORT_FAILURE	0x2

void set_pageblock_migratetype(struct page *page, enum migratetype migratetype);

bool move_freepages_block_isolate(struct zone *zone, struct page *page,
				  int migratetype);

int start_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			     int migratetype, int flags);

void undo_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			     int migratetype);

int test_pages_isolated(unsigned long start_pfn, unsigned long end_pfn,
			int isol_flags);
#endif
