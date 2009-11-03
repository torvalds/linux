#ifndef LINUX_MM_INLINE_H
#define LINUX_MM_INLINE_H

/**
 * page_is_file_cache - should the page be on a file LRU or anon LRU?
 * @page: the page to test
 *
 * Returns 1 if @page is page cache page backed by a regular filesystem,
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
	return !PageSwapBacked(page);
}

static inline void
add_page_to_lru_list(struct zone *zone, struct page *page, enum lru_list l)
{
	list_add(&page->lru, &zone->lru[l].list);
	__inc_zone_state(zone, NR_LRU_BASE + l);
	mem_cgroup_add_lru_list(page, l);
}

static inline void
del_page_from_lru_list(struct zone *zone, struct page *page, enum lru_list l)
{
	list_del(&page->lru);
	__dec_zone_state(zone, NR_LRU_BASE + l);
	mem_cgroup_del_lru_list(page, l);
}

/**
 * page_lru_base_type - which LRU list type should a page be on?
 * @page: the page to test
 *
 * Used for LRU list index arithmetic.
 *
 * Returns the base LRU type - file or anon - @page should be on.
 */
static inline enum lru_list page_lru_base_type(struct page *page)
{
	if (page_is_file_cache(page))
		return LRU_INACTIVE_FILE;
	return LRU_INACTIVE_ANON;
}

static inline void
del_page_from_lru(struct zone *zone, struct page *page)
{
	enum lru_list l;

	list_del(&page->lru);
	if (PageUnevictable(page)) {
		__ClearPageUnevictable(page);
		l = LRU_UNEVICTABLE;
	} else {
		l = page_lru_base_type(page);
		if (PageActive(page)) {
			__ClearPageActive(page);
			l += LRU_ACTIVE;
		}
	}
	__dec_zone_state(zone, NR_LRU_BASE + l);
	mem_cgroup_del_lru_list(page, l);
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
	enum lru_list lru;

	if (PageUnevictable(page))
		lru = LRU_UNEVICTABLE;
	else {
		lru = page_lru_base_type(page);
		if (PageActive(page))
			lru += LRU_ACTIVE;
	}

	return lru;
}

#endif
