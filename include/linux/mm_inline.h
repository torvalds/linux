#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

/**
 * page_is_file_cache - should the page be on a file LRU or anon LRU?
 * @page: the page to test
 *
 * Returns !0 if @page is page cache page backed by a regular filesystem,
 * or 0 if @page is anonymous, tmpfs or otherwise ram or swap backed.
 * Used by functions that manipulate the LRU lists, to sort a page
 * onto the right LRU list.
 *
 * We would like to get this info without a page flag, but the state
 * needs to survive until the page is last deleted from the LRU, which
 * could be as far down as __page_cache_release.
 */
static inline int page_is_file_cache(struct page *page)
{
	if (PageSwapBacked(page))
		return 0;

	/* The page is page cache backed by a normal filesystem. */
	return 1;
}

static inline void
add_page_to_lru_list(struct zone *zone, struct page *page, enum lru_list l)
{
	list_add(&page->lru, &zone->lru[l].list);
	__inc_zone_state(zone, NR_LRU_BASE + l);
}

static inline void
del_page_from_lru_list(struct zone *zone, struct page *page, enum lru_list l)
{
	list_del(&page->lru);
	__dec_zone_state(zone, NR_LRU_BASE + l);
}

static inline void
add_page_to_active_list(struct zone *zone, struct page *page)
{
	add_page_to_lru_list(zone, page, LRU_ACTIVE);
}

static inline void
add_page_to_inactive_list(struct zone *zone, struct page *page)
{
	add_page_to_lru_list(zone, page, LRU_INACTIVE);
}

static inline void
del_page_from_active_list(struct zone *zone, struct page *page)
{
	del_page_from_lru_list(zone, page, LRU_ACTIVE);
}

static inline void
del_page_from_inactive_list(struct zone *zone, struct page *page)
{
	del_page_from_lru_list(zone, page, LRU_INACTIVE);
}

static inline void
del_page_from_lru(struct zone *zone, struct page *page)
{
	enum lru_list l = LRU_INACTIVE;

	list_del(&page->lru);
	if (PageActive(page)) {
		__ClearPageActive(page);
		l = LRU_ACTIVE;
	}
	__dec_zone_state(zone, NR_LRU_BASE + l);
}

/**
 * page_lru - which LRU list should a page be on?
 * @page: the page to test
 *
 * Returns the LRU list a page should be on, as an index
 * into the array of LRU lists.
 */
static inline enum lru_list page_lru(struct page *page)
{
	enum lru_list lru = LRU_BASE;

	if (PageActive(page))
		lru += LRU_ACTIVE;

	return lru;
}

#endif
