/*
 * mm/balloon_compaction.c
 *
 * Common interface for making balloon pages movable by compaction.
 *
 * Copyright (C) 2012, Red Hat, Inc.  Rafael Aquini <aquini@redhat.com>
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/balloon_compaction.h>

/*
 * balloon_page_alloc - allocates a new page for insertion into the balloon
 *			  page list.
 *
 * Driver must call it to properly allocate a new enlisted balloon page.
 * Driver must call balloon_page_enqueue before definitively removing it from
 * the guest system.  This function returns the page address for the recently
 * allocated page or NULL in the case we fail to allocate a new page this turn.
 */
struct page *balloon_page_alloc(void)
{
	struct page *page = alloc_page(balloon_mapping_gfp_mask() |
				       __GFP_NOMEMALLOC | __GFP_NORETRY);
	return page;
}
EXPORT_SYMBOL_GPL(balloon_page_alloc);

/*
 * balloon_page_enqueue - allocates a new page and inserts it into the balloon
 *			  page list.
 * @b_dev_info: balloon device descriptor where we will insert a new page to
 * @page: new page to enqueue - allocated using balloon_page_alloc.
 *
 * Driver must call it to properly enqueue a new allocated balloon page
 * before definitively removing it from the guest system.
 * This function returns the page address for the recently enqueued page or
 * NULL in the case we fail to allocate a new page this turn.
 */
void balloon_page_enqueue(struct balloon_dev_info *b_dev_info,
			  struct page *page)
{
	unsigned long flags;

	/*
	 * Block others from accessing the 'page' when we get around to
	 * establishing additional references. We should be the only one
	 * holding a reference to the 'page' at this point.
	 */
	BUG_ON(!trylock_page(page));
	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	balloon_page_insert(b_dev_info, page);
	__count_vm_event(BALLOON_INFLATE);
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
	unlock_page(page);
}
EXPORT_SYMBOL_GPL(balloon_page_enqueue);

/*
 * balloon_page_dequeue - removes a page from balloon's page list and returns
 *			  the its address to allow the driver release the page.
 * @b_dev_info: balloon device decriptor where we will grab a page from.
 *
 * Driver must call it to properly de-allocate a previous enlisted balloon page
 * before definetively releasing it back to the guest system.
 * This function returns the page address for the recently dequeued page or
 * NULL in the case we find balloon's page list temporarily empty due to
 * compaction isolated pages.
 */
struct page *balloon_page_dequeue(struct balloon_dev_info *b_dev_info)
{
	struct page *page, *tmp;
	unsigned long flags;
	bool dequeued_page;

	dequeued_page = false;
	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_for_each_entry_safe(page, tmp, &b_dev_info->pages, lru) {
		/*
		 * Block others from accessing the 'page' while we get around
		 * establishing additional references and preparing the 'page'
		 * to be released by the balloon driver.
		 */
		if (trylock_page(page)) {
#ifdef CONFIG_BALLOON_COMPACTION
			if (PageIsolated(page)) {
				/* raced with isolation */
				unlock_page(page);
				continue;
			}
#endif
			balloon_page_delete(page);
			__count_vm_event(BALLOON_DEFLATE);
			unlock_page(page);
			dequeued_page = true;
			break;
		}
	}
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);

	if (!dequeued_page) {
		/*
		 * If we are unable to dequeue a balloon page because the page
		 * list is empty and there is no isolated pages, then something
		 * went out of track and some balloon pages are lost.
		 * BUG() here, otherwise the balloon driver may get stuck into
		 * an infinite loop while attempting to release all its pages.
		 */
		spin_lock_irqsave(&b_dev_info->pages_lock, flags);
		if (unlikely(list_empty(&b_dev_info->pages) &&
			     !b_dev_info->isolated_pages))
			BUG();
		spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
		page = NULL;
	}
	return page;
}
EXPORT_SYMBOL_GPL(balloon_page_dequeue);

#ifdef CONFIG_BALLOON_COMPACTION

bool balloon_page_isolate(struct page *page, isolate_mode_t mode)

{
	struct balloon_dev_info *b_dev_info = balloon_page_device(page);
	unsigned long flags;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_del(&page->lru);
	b_dev_info->isolated_pages++;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);

	return true;
}

void balloon_page_putback(struct page *page)
{
	struct balloon_dev_info *b_dev_info = balloon_page_device(page);
	unsigned long flags;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_add(&page->lru, &b_dev_info->pages);
	b_dev_info->isolated_pages--;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
}


/* move_to_new_page() counterpart for a ballooned page */
int balloon_page_migrate(struct address_space *mapping,
		struct page *newpage, struct page *page,
		enum migrate_mode mode)
{
	struct balloon_dev_info *balloon = balloon_page_device(page);

	/*
	 * We can not easily support the no copy case here so ignore it as it
	 * is unlikely to be use with ballon pages. See include/linux/hmm.h for
	 * user of the MIGRATE_SYNC_NO_COPY mode.
	 */
	if (mode == MIGRATE_SYNC_NO_COPY)
		return -EINVAL;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	VM_BUG_ON_PAGE(!PageLocked(newpage), newpage);

	return balloon->migratepage(balloon, newpage, page, mode);
}

const struct address_space_operations balloon_aops = {
	.migratepage = balloon_page_migrate,
	.isolate_page = balloon_page_isolate,
	.putback_page = balloon_page_putback,
};
EXPORT_SYMBOL_GPL(balloon_aops);

#endif /* CONFIG_BALLOON_COMPACTION */
