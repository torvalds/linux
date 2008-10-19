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
