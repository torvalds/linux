static inline void
add_page_to_active_list(struct zone *zone, struct page *page)
{
	list_add(&page->lru, &zone->active_list);
	__inc_zone_state(zone, NR_ACTIVE);
}

static inline void
add_page_to_inactive_list(struct zone *zone, struct page *page)
{
	list_add(&page->lru, &zone->inactive_list);
	__inc_zone_state(zone, NR_INACTIVE);
}

static inline void
del_page_from_active_list(struct zone *zone, struct page *page)
{
	list_del(&page->lru);
	__dec_zone_state(zone, NR_ACTIVE);
}

static inline void
del_page_from_inactive_list(struct zone *zone, struct page *page)
{
	list_del(&page->lru);
	__dec_zone_state(zone, NR_INACTIVE);
}

static inline void
del_page_from_lru(struct zone *zone, struct page *page)
{
	list_del(&page->lru);
	if (PageActive(page)) {
		__ClearPageActive(page);
		__dec_zone_state(zone, NR_ACTIVE);
	} else {
		__dec_zone_state(zone, NR_INACTIVE);
	}
}

