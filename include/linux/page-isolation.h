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

/*
 * Pageblock isolation modes:
 * PB_ISOLATE_MODE_MEM_OFFLINE - isolate to offline (!allocate) memory
 *				 e.g., skip over PageHWPoison() pages and
 *				 PageOffline() pages. Unmovable pages will be
 *				 reported in this mode.
 * PB_ISOLATE_MODE_CMA_ALLOC   - isolate for CMA allocations
 * PB_ISOLATE_MODE_OTHER       - isolate for other purposes
 */
enum pb_isolate_mode {
	PB_ISOLATE_MODE_MEM_OFFLINE,
	PB_ISOLATE_MODE_CMA_ALLOC,
	PB_ISOLATE_MODE_OTHER,
};

void __meminit init_pageblock_migratetype(struct page *page,
					  enum migratetype migratetype,
					  bool isolate);

bool pageblock_isolate_and_move_free_pages(struct zone *zone, struct page *page);
bool pageblock_unisolate_and_move_free_pages(struct zone *zone, struct page *page);

int start_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			     enum pb_isolate_mode mode);

void undo_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn);

int test_pages_isolated(unsigned long start_pfn, unsigned long end_pfn,
			enum pb_isolate_mode mode);
#endif
