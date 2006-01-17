
static inline void
add_page_to_active_list(struct zone *zone, struct page *page)
{
	list_add(&page->lru, &zone->active_list);
	zone->nr_active++;
}

static inline void
add_page_to_inactive_list(struct zone *zone, struct page *page)
{
	list_add(&page->lru, &zone->inactive_list);
	zone->nr_inactive++;
}

static inline void
del_page_from_active_list(struct zone *zone, struct page *page)
{
	list_del(&page->lru);
	zone->nr_active--;
}

static inline void
del_page_from_inactive_list(struct zone *zone, struct page *page)
{
	list_del(&page->lru);
	zone->nr_inactive--;
}

static inline void
del_page_from_lru(struct zone *zone, struct page *page)
{
	list_del(&page->lru);
	if (PageActive(page)) {
		ClearPageActive(page);
		zone->nr_active--;
	} else {
		zone->nr_inactive--;
	}
}

/*
 * Isolate one page from the LRU lists.
 *
 * - zone->lru_lock must be held
 */
static inline int __isolate_lru_page(struct page *page)
{
	if (unlikely(!TestClearPageLRU(page)))
		return 0;

	if (get_page_testone(page)) {
		/*
		 * It is being freed elsewhere
		 */
		__put_page(page);
		SetPageLRU(page);
		return -ENOENT;
	}

	return 1;
}
