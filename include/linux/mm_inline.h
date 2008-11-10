#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

/**
 * page_is_file_cache - should the page be on a file LRU or anon LRU?
 * @page: the page to test
 *
 * Returns LRU_FILE if @page is page cache page backed by a regular filesystem,
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
	return LRU_FILE;
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
del_page_from_lru(struct zone *zone, struct page *page)
{
	enum lru_list l = LRU_BASE;

	list_del(&page->lru);
	if (PageUnevictable(page)) {
		__ClearPageUnevictable(page);
		l = LRU_UNEVICTABLE;
	} else {
		if (PageActive(page)) {
			__ClearPageActive(page);
			l += LRU_ACTIVE;
		}
		l += page_is_file_cache(page);
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

	if (PageUnevictable(page))
		lru = LRU_UNEVICTABLE;
	else {
		if (PageActive(page))
			lru += LRU_ACTIVE;
		lru += page_is_file_cache(page);
	}

	return lru;
}

/**
 * inactive_anon_is_low - check if anonymous pages need to be deactivated
 * @zone: zone to check
 *
 * Returns true if the zone does not have enough inactive anon pages,
 * meaning some active anon pages need to be deactivated.
 */
static inline int inactive_anon_is_low(struct zone *zone)
{
	unsigned long active, inactive;

	active = zone_page_state(zone, NR_ACTIVE_ANON);
	inactive = zone_page_state(zone, NR_INACTIVE_ANON);

	if (inactive * zone->inactive_ratio < active)
		return 1;

	return 0;
}
#endif
