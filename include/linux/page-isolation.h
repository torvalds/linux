#ifndef __LINUX_PAGEISOLATION_H
#define __LINUX_PAGEISOLATION_H


bool has_unmovable_pages(struct zone *zone, struct page *page, int count);
void set_pageblock_migratetype(struct page *page, int migratetype);
int move_freepages_block(struct zone *zone, struct page *page,
				int migratetype);
int move_freepages(struct zone *zone,
			  struct page *start_page, struct page *end_page,
			  int migratetype);

/*
 * Changes migrate type in [start_pfn, end_pfn) to be MIGRATE_ISOLATE.
 * If specified range includes migrate types other than MOVABLE or CMA,
 * this will fail with -EBUSY.
 *
 * For isolating all pages in the range finally, the caller have to
 * free all pages in the range. test_page_isolated() can be used for
 * test it.
 */
int
start_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			 unsigned migratetype);

/*
 * Changes MIGRATE_ISOLATE to MIGRATE_MOVABLE.
 * target range is [start_pfn, end_pfn)
 */
int
undo_isolate_page_range(unsigned long start_pfn, unsigned long end_pfn,
			unsigned migratetype);

/*
 * Test all pages in [start_pfn, end_pfn) are isolated or not.
 */
int test_pages_isolated(unsigned long start_pfn, unsigned long end_pfn);

/*
 * Internal functions. Changes pageblock's migrate type.
 */
int set_migratetype_isolate(struct page *page);
void unset_migratetype_isolate(struct page *page, unsigned migratetype);
struct page *alloc_migrate_target(struct page *page, unsigned long private,
				int **resultp);

#endif
