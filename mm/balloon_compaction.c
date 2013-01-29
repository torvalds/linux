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
 * balloon_devinfo_alloc - allocates a balloon device information descriptor.
 * @balloon_dev_descriptor: pointer to reference the balloon device which
 *                          this struct balloon_dev_info will be servicing.
 *
 * Driver must call it to properly allocate and initialize an instance of
 * struct balloon_dev_info which will be used to reference a balloon device
 * as well as to keep track of the balloon device page list.
 */
struct balloon_dev_info *balloon_devinfo_alloc(void *balloon_dev_descriptor)
{
	struct balloon_dev_info *b_dev_info;
	b_dev_info = kmalloc(sizeof(*b_dev_info), GFP_KERNEL);
	if (!b_dev_info)
		return ERR_PTR(-ENOMEM);

	b_dev_info->balloon_device = balloon_dev_descriptor;
	b_dev_info->mapping = NULL;
	b_dev_info->isolated_pages = 0;
	spin_lock_init(&b_dev_info->pages_lock);
	INIT_LIST_HEAD(&b_dev_info->pages);

	return b_dev_info;
}
EXPORT_SYMBOL_GPL(balloon_devinfo_alloc);

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
	balloon_page_insert(page, b_dev_info->mapping, &b_dev_info->pages);
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
			spin_lock_irqsave(&b_dev_info->pages_lock, flags);
			/*
			 * Raise the page refcount here to prevent any wrong
			 * attempt to isolate this page, in case of coliding
			 * with balloon_page_isolate() just after we release
			 * the page lock.
			 *
			 * balloon_page_free() will take care of dropping
			 * this extra refcount later.
			 */
			get_page(page);
			balloon_page_delete(page);
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
/*
 * balloon_mapping_alloc - allocates a special ->mapping for ballooned pages.
 * @b_dev_info: holds the balloon device information descriptor.
 * @a_ops: balloon_mapping address_space_operations descriptor.
 *
 * Driver must call it to properly allocate and initialize an instance of
 * struct address_space which will be used as the special page->mapping for
 * balloon device enlisted page instances.
 */
struct address_space *balloon_mapping_alloc(struct balloon_dev_info *b_dev_info,
				const struct address_space_operations *a_ops)
{
	struct address_space *mapping;

	mapping = kmalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping)
		return ERR_PTR(-ENOMEM);

	/*
	 * Give a clean 'zeroed' status to all elements of this special
	 * balloon page->mapping struct address_space instance.
	 */
	address_space_init_once(mapping);

	/*
	 * Set mapping->flags appropriately, to allow balloon pages
	 * ->mapping identification.
	 */
	mapping_set_balloon(mapping);
	mapping_set_gfp_mask(mapping, balloon_mapping_gfp_mask());

	/* balloon's page->mapping->a_ops callback descriptor */
	mapping->a_ops = a_ops;

	/*
	 * Establish a pointer reference back to the balloon device descriptor
	 * this particular page->mapping will be servicing.
	 * This is used by compaction / migration procedures to identify and
	 * access the balloon device pageset while isolating / migrating pages.
	 *
	 * As some balloon drivers can register multiple balloon devices
	 * for a single guest, this also helps compaction / migration to
	 * properly deal with multiple balloon pagesets, when required.
	 */
	mapping->private_data = b_dev_info;
	b_dev_info->mapping = mapping;

	return mapping;
}
EXPORT_SYMBOL_GPL(balloon_mapping_alloc);

static inline void __isolate_balloon_page(struct page *page)
{
	struct balloon_dev_info *b_dev_info = page->mapping->private_data;
	unsigned long flags;
	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_del(&page->lru);
	b_dev_info->isolated_pages++;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
}

static inline void __putback_balloon_page(struct page *page)
{
	struct balloon_dev_info *b_dev_info = page->mapping->private_data;
	unsigned long flags;
	spin_lock_irqsave(&b_dev_info->pages_lock, flags);
	list_add(&page->lru, &b_dev_info->pages);
	b_dev_info->isolated_pages--;
	spin_unlock_irqrestore(&b_dev_info->pages_lock, flags);
}

static inline int __migrate_balloon_page(struct address_space *mapping,
		struct page *newpage, struct page *page, enum migrate_mode mode)
{
	return page->mapping->a_ops->migratepage(mapping, newpage, page, mode);
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
			 * A ballooned page, by default, has just one refcount.
			 * Prevent concurrent compaction threads from isolating
			 * an already isolated balloon page by refcount check.
			 */
			if (__is_movable_balloon_page(page) &&
			    page_count(page) == 2) {
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
		dump_page(page);
	}
	unlock_page(page);
}

/* move_to_new_page() counterpart for a ballooned page */
int balloon_page_migrate(struct page *newpage,
			 struct page *page, enum migrate_mode mode)
{
	struct address_space *mapping;
	int rc = -EAGAIN;

	/*
	 * Block others from accessing the 'newpage' when we get around to
	 * establishing additional references. We should be the only one
	 * holding a reference to the 'newpage' at this point.
	 */
	BUG_ON(!trylock_page(newpage));

	if (WARN_ON(!__is_movable_balloon_page(page))) {
		dump_page(page);
		unlock_page(newpage);
		return rc;
	}

	mapping = page->mapping;
	if (mapping)
		rc = __migrate_balloon_page(mapping, newpage, page, mode);

	unlock_page(newpage);
	return rc;
}
#endif /* CONFIG_BALLOON_COMPACTION */
