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
 * balloon_page_enqueue - allocates a new page and inserts it into the balloon
 *			  page list.
 * @b_dev_info: balloon device decriptor where we will insert a new page to
 *
 * Driver must call it to properly allocate a new enlisted balloon page
 * before definetively removing it from the guest system.
 * This function returns the page address for the recently enqueued page or
 * NULL in the case we fail to allocate a new page this turn.
 */
struct page *balloon_page_enqueue(struct balloon_dev_info *b_dev_info)
{
	unsigned long flags;
	struct page *page = alloc_page(balloon_mapping_gfp_mask() |
					__GFP_NOMEMALLOC | __GFP_NORETRY);
	if (!page)
		return NULL;

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
	return page;
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
	list_for_each_entry_safe(page, tmp, &b_dev_info->pages, lru) {
		/*
		 * Block others from accessing the 'page' while we get around
		 * establishing additional references and preparing the 'page'
		 * to be released by the balloon driver.
		 */
		if (trylock_page(page)) {
			if (!PagePrivate(page)) {
				/* raced with isolation */
				unlock_page(page);
				continue;
			}
			spin_lock_irqsave(&b_dev_info->pages_lock, flags);
			balloon_page_delete(page);
			__count_vm_event(BALLOON_DEFLATE);
			spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
			unlock_page(page);
			dequeued_page = true;
			break;
		}
	}

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

static inline void __isolate_balloon_page(struct page *page)
{
	struct balloon_dev_info *b_dev_info = balloon_page_device(page);
	unsigned long flags;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	ClearPagePrivate(page);
	list_del(&page->lru);
	b_dev_info->isolated_pages++;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
}

static inline void __putback_balloon_page(struct page *page)
{
	struct balloon_dev_info *b_dev_info = balloon_page_device(page);
	unsigned long flags;

	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	SetPagePrivate(page);
	list_add(&page->lru, &b_dev_info->pages);
	b_dev_info->isolated_pages--;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
}

/* __isolate_lru_page() counterpart for a ballooned page */
bool balloon_page_isolate(struct page *page)
{
	/*
	 * Avoid burning cycles with pages that are yet under __free_pages(),
	 * or just got freed under us.
	 *
	 * In case we 'win' a race for a balloon page being freed under us and
	 * raise its refcount preventing __free_pages() from doing its job
	 * the put_page() at the end of this block will take care of
	 * release this page, thus avoiding a nasty leakage.
	 */
	if (likely(get_page_unless_zero(page))) {
		/*
		 * As balloon pages are not isolated from LRU lists, concurrent
		 * compaction threads can race against page migration functions
		 * as well as race against the balloon driver releasing a page.
		 *
		 * In order to avoid having an already isolated balloon page
		 * being (wrongly) re-isolated while it is under migration,
		 * or to avoid attempting to isolate pages being released by
		 * the balloon driver, lets be sure we have the page lock
		 * before proceeding with the balloon page isolation steps.
		 */
		if (likely(trylock_page(page))) {
			/*
			 * A ballooned page, by default, has PagePrivate set.
			 * Prevent concurrent compaction threads from isolating
			 * an already isolated balloon page by clearing it.
			 */
			if (balloon_page_movable(page)) {
				__isolate_balloon_page(page);
				unlock_page(page);
				return true;
			}
			unlock_page(page);
		}
		put_page(page);
	}
	return false;
}

/* putback_lru_page() counterpart for a ballooned page */
void balloon_page_putback(struct page *page)
{
	/*
	 * 'lock_page()' stabilizes the page and prevents races against
	 * concurrent isolation threads attempting to re-isolate it.
	 */
	lock_page(page);

	if (__is_movable_balloon_page(page)) {
		__putback_balloon_page(page);
		/* drop the extra ref count taken for page isolation */
		put_page(page);
	} else {
		WARN_ON(1);
		dump_page(page, "not movable balloon page");
	}
	unlock_page(page);
}

/* move_to_new_page() counterpart for a ballooned page */
int balloon_page_migrate(struct page *newpage,
			 struct page *page, enum migrate_mode mode)
{
	struct balloon_dev_info *balloon = balloon_page_device(page);
	int rc = -EAGAIN;

	/*
	 * Block others from accessing the 'newpage' when we get around to
	 * establishing additional references. We should be the only one
	 * holding a reference to the 'newpage' at this point.
	 */
	BUG_ON(!trylock_page(newpage));

	if (WARN_ON(!__is_movable_balloon_page(page))) {
		dump_page(page, "not movable balloon page");
		unlock_page(newpage);
		return rc;
	}

	if (balloon && balloon->migratepage)
		rc = balloon->migratepage(balloon, newpage, page, mode);

	unlock_page(newpage);
	return rc;
}
#endif /* CONFIG_BALLOON_COMPACTION */
