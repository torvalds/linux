/*
 * Memory Migration functionality - linux/mm/migration.c
 *
 * Copyright (C) 2006 Silicon Graphics, Inc., Christoph Lameter
 *
 * Page migration was first developed in the context of the memory hotplug
 * project. The main authors of the migration code are:
 *
 * IWAMOTO Toshihiro <iwamoto@valinux.co.jp>
 * Hirokazu Takahashi <taka@valinux.co.jp>
 * Dave Hansen <haveblue@us.ibm.com>
 * Christoph Lameter
 */

#include <linux/migrate.h>
#include <linux/module.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/pagemap.h>
#include <linux/buffer_head.h>
#include <linux/mm_inline.h>
#include <linux/nsproxy.h>
#include <linux/pagevec.h>
#include <linux/rmap.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/cpuset.h>
#include <linux/writeback.h>
#include <linux/mempolicy.h>
#include <linux/vmalloc.h>
#include <linux/security.h>
#include <linux/memcontrol.h>

#include "internal.h"

#define lru_to_page(_head) (list_entry((_head)->prev, struct page, lru))

/*
 * Isolate one page from the LRU lists. If successful put it onto
 * the indicated list with elevated page count.
 *
 * Result:
 *  -EBUSY: page not on LRU list
 *  0: page removed from LRU list and added to the specified list.
 */
int isolate_lru_page(struct page *page, struct list_head *pagelist)
{
	int ret = -EBUSY;

	if (PageLRU(page)) {
		struct zone *zone = page_zone(page);

		spin_lock_irq(&zone->lru_lock);
		if (PageLRU(page) && get_page_unless_zero(page)) {
			ret = 0;
			ClearPageLRU(page);
			if (PageActive(page))
				del_page_from_active_list(zone, page);
			else
				del_page_from_inactive_list(zone, page);
			list_add_tail(&page->lru, pagelist);
		}
		spin_unlock_irq(&zone->lru_lock);
	}
	return ret;
}

/*
 * migrate_prep() needs to be called before we start compiling a list of pages
 * to be migrated using isolate_lru_page().
 */
int migrate_prep(void)
{
	/*
	 * Clear the LRU lists so pages can be isolated.
	 * Note that pages may be moved off the LRU after we have
	 * drained them. Those pages will fail to migrate like other
	 * pages that may be busy.
	 */
	lru_add_drain_all();

	return 0;
}

static inline void move_to_lru(struct page *page)
{
	if (PageActive(page)) {
		/*
		 * lru_cache_add_active checks that
		 * the PG_active bit is off.
		 */
		ClearPageActive(page);
		lru_cache_add_active(page);
	} else {
		lru_cache_add(page);
	}
	put_page(page);
}

/*
 * Add isolated pages on the list back to the LRU.
 *
 * returns the number of pages put back.
 */
int putback_lru_pages(struct list_head *l)
{
	struct page *page;
	struct page *page2;
	int count = 0;

	list_for_each_entry_safe(page, page2, l, lru) {
		list_del(&page->lru);
		move_to_lru(page);
		count++;
	}
	return count;
}

/*
 * Restore a potential migration pte to a working pte entry
 */
static void remove_migration_pte(struct vm_area_struct *vma,
		struct page *old, struct page *new)
{
	struct mm_struct *mm = vma->vm_mm;
	swp_entry_t entry;
 	pgd_t *pgd;
 	pud_t *pud;
 	pmd_t *pmd;
	pte_t *ptep, pte;
 	spinlock_t *ptl;
	unsigned long addr = page_address_in_vma(new, vma);

	if (addr == -EFAULT)
		return;

 	pgd = pgd_offset(mm, addr);
	if (!pgd_present(*pgd))
                return;

	pud = pud_offset(pgd, addr);
	if (!pud_present(*pud))
                return;

	pmd = pmd_offset(pud, addr);
	if (!pmd_present(*pmd))
		return;

	ptep = pte_offset_map(pmd, addr);

	if (!is_swap_pte(*ptep)) {
		pte_unmap(ptep);
 		return;
 	}

 	ptl = pte_lockptr(mm, pmd);
 	spin_lock(ptl);
	pte = *ptep;
	if (!is_swap_pte(pte))
		goto out;

	entry = pte_to_swp_entry(pte);

	if (!is_migration_entry(entry) || migration_entry_to_page(entry) != old)
		goto out;

	/*
	 * Yes, ignore the return value from a GFP_ATOMIC mem_cgroup_charge.
	 * Failure is not an option here: we're now expected to remove every
	 * migration pte, and will cause crashes otherwise.  Normally this
	 * is not an issue: mem_cgroup_prepare_migration bumped up the old
	 * page_cgroup count for safety, that's now attached to the new page,
	 * so this charge should just be another incrementation of the count,
	 * to keep in balance with rmap.c's mem_cgroup_uncharging.  But if
	 * there's been a force_empty, those reference counts may no longer
	 * be reliable, and this charge can actually fail: oh well, we don't
	 * make the situation any worse by proceeding as if it had succeeded.
	 */
	mem_cgroup_charge(new, mm, GFP_ATOMIC);

	get_page(new);
	pte = pte_mkold(mk_pte(new, vma->vm_page_prot));
	if (is_write_migration_entry(entry))
		pte = pte_mkwrite(pte);
	flush_cache_page(vma, addr, pte_pfn(pte));
	set_pte_at(mm, addr, ptep, pte);

	if (PageAnon(new))
		page_add_anon_rmap(new, vma, addr);
	else
		page_add_file_rmap(new);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(vma, addr, pte);

out:
	pte_unmap_unlock(ptep, ptl);
}

/*
 * Note that remove_file_migration_ptes will only work on regular mappings,
 * Nonlinear mappings do not use migration entries.
 */
static void remove_file_migration_ptes(struct page *old, struct page *new)
{
	struct vm_area_struct *vma;
	struct address_space *mapping = page_mapping(new);
	struct prio_tree_iter iter;
	pgoff_t pgoff = new->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

	if (!mapping)
		return;

	spin_lock(&mapping->i_mmap_lock);

	vma_prio_tree_foreach(vma, &iter, &mapping->i_mmap, pgoff, pgoff)
		remove_migration_pte(vma, old, new);

	spin_unlock(&mapping->i_mmap_lock);
}

/*
 * Must hold mmap_sem lock on at least one of the vmas containing
 * the page so that the anon_vma cannot vanish.
 */
static void remove_anon_migration_ptes(struct page *old, struct page *new)
{
	struct anon_vma *anon_vma;
	struct vm_area_struct *vma;
	unsigned long mapping;

	mapping = (unsigned long)new->mapping;

	if (!mapping || (mapping & PAGE_MAPPING_ANON) == 0)
		return;

	/*
	 * We hold the mmap_sem lock. So no need to call page_lock_anon_vma.
	 */
	anon_vma = (struct anon_vma *) (mapping - PAGE_MAPPING_ANON);
	spin_lock(&anon_vma->lock);

	list_for_each_entry(vma, &anon_vma->head, anon_vma_node)
		remove_migration_pte(vma, old, new);

	spin_unlock(&anon_vma->lock);
}

/*
 * Get rid of all migration entries and replace them by
 * references to the indicated page.
 */
static void remove_migration_ptes(struct page *old, struct page *new)
{
	if (PageAnon(new))
		remove_anon_migration_ptes(old, new);
	else
		remove_file_migration_ptes(old, new);
}

/*
 * Something used the pte of a page under migration. We need to
 * get to the page and wait until migration is finished.
 * When we return from this function the fault will be retried.
 *
 * This function is called from do_swap_page().
 */
void migration_entry_wait(struct mm_struct *mm, pmd_t *pmd,
				unsigned long address)
{
	pte_t *ptep, pte;
	spinlock_t *ptl;
	swp_entry_t entry;
	struct page *page;

	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	pte = *ptep;
	if (!is_swap_pte(pte))
		goto out;

	entry = pte_to_swp_entry(pte);
	if (!is_migration_entry(entry))
		goto out;

	page = migration_entry_to_page(entry);

	get_page(page);
	pte_unmap_unlock(ptep, ptl);
	wait_on_page_locked(page);
	put_page(page);
	return;
out:
	pte_unmap_unlock(ptep, ptl);
}

/*
 * Replace the page in the mapping.
 *
 * The number of remaining references must be:
 * 1 for anonymous pages without a mapping
 * 2 for pages with a mapping
 * 3 for pages with a mapping and PagePrivate set.
 */
static int migrate_page_move_mapping(struct address_space *mapping,
		struct page *newpage, struct page *page)
{
	void **pslot;

	if (!mapping) {
		/* Anonymous page without mapping */
		if (page_count(page) != 1)
			return -EAGAIN;
		return 0;
	}

	write_lock_irq(&mapping->tree_lock);

	pslot = radix_tree_lookup_slot(&mapping->page_tree,
 					page_index(page));

	if (page_count(page) != 2 + !!PagePrivate(page) ||
			(struct page *)radix_tree_deref_slot(pslot) != page) {
		write_unlock_irq(&mapping->tree_lock);
		return -EAGAIN;
	}

	/*
	 * Now we know that no one else is looking at the page.
	 */
	get_page(newpage);	/* add cache reference */
#ifdef CONFIG_SWAP
	if (PageSwapCache(page)) {
		SetPageSwapCache(newpage);
		set_page_private(newpage, page_private(page));
	}
#endif

	radix_tree_replace_slot(pslot, newpage);

	/*
	 * Drop cache reference from old page.
	 * We know this isn't the last reference.
	 */
	__put_page(page);

	/*
	 * If moved to a different zone then also account
	 * the page for that zone. Other VM counters will be
	 * taken care of when we establish references to the
	 * new page and drop references to the old page.
	 *
	 * Note that anonymous pages are accounted for
	 * via NR_FILE_PAGES and NR_ANON_PAGES if they
	 * are mapped to swap space.
	 */
	__dec_zone_page_state(page, NR_FILE_PAGES);
	__inc_zone_page_state(newpage, NR_FILE_PAGES);

	write_unlock_irq(&mapping->tree_lock);

	return 0;
}

/*
 * Copy the page to its new location
 */
static void migrate_page_copy(struct page *newpage, struct page *page)
{
	copy_highpage(newpage, page);

	if (PageError(page))
		SetPageError(newpage);
	if (PageReferenced(page))
		SetPageReferenced(newpage);
	if (PageUptodate(page))
		SetPageUptodate(newpage);
	if (PageActive(page))
		SetPageActive(newpage);
	if (PageChecked(page))
		SetPageChecked(newpage);
	if (PageMappedToDisk(page))
		SetPageMappedToDisk(newpage);

	if (PageDirty(page)) {
		clear_page_dirty_for_io(page);
		/*
		 * Want to mark the page and the radix tree as dirty, and
		 * redo the accounting that clear_page_dirty_for_io undid,
		 * but we can't use set_page_dirty because that function
		 * is actually a signal that all of the page has become dirty.
		 * Wheras only part of our page may be dirty.
		 */
		__set_page_dirty_nobuffers(newpage);
 	}

#ifdef CONFIG_SWAP
	ClearPageSwapCache(page);
#endif
	ClearPageActive(page);
	ClearPagePrivate(page);
	set_page_private(page, 0);
	page->mapping = NULL;

	/*
	 * If any waiters have accumulated on the new page then
	 * wake them up.
	 */
	if (PageWriteback(newpage))
		end_page_writeback(newpage);
}

/************************************************************
 *                    Migration functions
 ***********************************************************/

/* Always fail migration. Used for mappings that are not movable */
int fail_migrate_page(struct address_space *mapping,
			struct page *newpage, struct page *page)
{
	return -EIO;
}
EXPORT_SYMBOL(fail_migrate_page);

/*
 * Common logic to directly migrate a single page suitable for
 * pages that do not use PagePrivate.
 *
 * Pages are locked upon entry and exit.
 */
int migrate_page(struct address_space *mapping,
		struct page *newpage, struct page *page)
{
	int rc;

	BUG_ON(PageWriteback(page));	/* Writeback must be complete */

	rc = migrate_page_move_mapping(mapping, newpage, page);

	if (rc)
		return rc;

	migrate_page_copy(newpage, page);
	return 0;
}
EXPORT_SYMBOL(migrate_page);

#ifdef CONFIG_BLOCK
/*
 * Migration function for pages with buffers. This function can only be used
 * if the underlying filesystem guarantees that no other references to "page"
 * exist.
 */
int buffer_migrate_page(struct address_space *mapping,
		struct page *newpage, struct page *page)
{
	struct buffer_head *bh, *head;
	int rc;

	if (!page_has_buffers(page))
		return migrate_page(mapping, newpage, page);

	head = page_buffers(page);

	rc = migrate_page_move_mapping(mapping, newpage, page);

	if (rc)
		return rc;

	bh = head;
	do {
		get_bh(bh);
		lock_buffer(bh);
		bh = bh->b_this_page;

	} while (bh != head);

	ClearPagePrivate(page);
	set_page_private(newpage, page_private(page));
	set_page_private(page, 0);
	put_page(page);
	get_page(newpage);

	bh = head;
	do {
		set_bh_page(bh, newpage, bh_offset(bh));
		bh = bh->b_this_page;

	} while (bh != head);

	SetPagePrivate(newpage);

	migrate_page_copy(newpage, page);

	bh = head;
	do {
		unlock_buffer(bh);
 		put_bh(bh);
		bh = bh->b_this_page;

	} while (bh != head);

	return 0;
}
EXPORT_SYMBOL(buffer_migrate_page);
#endif

/*
 * Writeback a page to clean the dirty state
 */
static int writeout(struct address_space *mapping, struct page *page)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
		.nr_to_write = 1,
		.range_start = 0,
		.range_end = LLONG_MAX,
		.nonblocking = 1,
		.for_reclaim = 1
	};
	int rc;

	if (!mapping->a_ops->writepage)
		/* No write method for the address space */
		return -EINVAL;

	if (!clear_page_dirty_for_io(page))
		/* Someone else already triggered a write */
		return -EAGAIN;

	/*
	 * A dirty page may imply that the underlying filesystem has
	 * the page on some queue. So the page must be clean for
	 * migration. Writeout may mean we loose the lock and the
	 * page state is no longer what we checked for earlier.
	 * At this point we know that the migration attempt cannot
	 * be successful.
	 */
	remove_migration_ptes(page, page);

	rc = mapping->a_ops->writepage(page, &wbc);
	if (rc < 0)
		/* I/O Error writing */
		return -EIO;

	if (rc != AOP_WRITEPAGE_ACTIVATE)
		/* unlocked. Relock */
		lock_page(page);

	return -EAGAIN;
}

/*
 * Default handling if a filesystem does not provide a migration function.
 */
static int fallback_migrate_page(struct address_space *mapping,
	struct page *newpage, struct page *page)
{
	if (PageDirty(page))
		return writeout(mapping, page);

	/*
	 * Buffers may be managed in a filesystem specific way.
	 * We must have no buffers or drop them.
	 */
	if (PagePrivate(page) &&
	    !try_to_release_page(page, GFP_KERNEL))
		return -EAGAIN;

	return migrate_page(mapping, newpage, page);
}

/*
 * Move a page to a newly allocated page
 * The page is locked and all ptes have been successfully removed.
 *
 * The new page will have replaced the old page if this function
 * is successful.
 */
static int move_to_new_page(struct page *newpage, struct page *page)
{
	struct address_space *mapping;
	int rc;

	/*
	 * Block others from accessing the page when we get around to
	 * establishing additional references. We are the only one
	 * holding a reference to the new page at this point.
	 */
	if (TestSetPageLocked(newpage))
		BUG();

	/* Prepare mapping for the new page.*/
	newpage->index = page->index;
	newpage->mapping = page->mapping;

	mapping = page_mapping(page);
	if (!mapping)
		rc = migrate_page(mapping, newpage, page);
	else if (mapping->a_ops->migratepage)
		/*
		 * Most pages have a mapping and most filesystems
		 * should provide a migration function. Anonymous
		 * pages are part of swap space which also has its
		 * own migration function. This is the most common
		 * path for page migration.
		 */
		rc = mapping->a_ops->migratepage(mapping,
						newpage, page);
	else
		rc = fallback_migrate_page(mapping, newpage, page);

	if (!rc) {
		mem_cgroup_page_migration(page, newpage);
		remove_migration_ptes(page, newpage);
	} else
		newpage->mapping = NULL;

	unlock_page(newpage);

	return rc;
}

/*
 * Obtain the lock on page, remove all ptes and migrate the page
 * to the newly allocated page in newpage.
 */
static int unmap_and_move(new_page_t get_new_page, unsigned long private,
			struct page *page, int force)
{
	int rc = 0;
	int *result = NULL;
	struct page *newpage = get_new_page(page, private, &result);
	int rcu_locked = 0;
	int charge = 0;

	if (!newpage)
		return -ENOMEM;

	if (page_count(page) == 1)
		/* page was freed from under us. So we are done. */
		goto move_newpage;

	rc = -EAGAIN;
	if (TestSetPageLocked(page)) {
		if (!force)
			goto move_newpage;
		lock_page(page);
	}

	if (PageWriteback(page)) {
		if (!force)
			goto unlock;
		wait_on_page_writeback(page);
	}
	/*
	 * By try_to_unmap(), page->mapcount goes down to 0 here. In this case,
	 * we cannot notice that anon_vma is freed while we migrates a page.
	 * This rcu_read_lock() delays freeing anon_vma pointer until the end
	 * of migration. File cache pages are no problem because of page_lock()
	 * File Caches may use write_page() or lock_page() in migration, then,
	 * just care Anon page here.
	 */
	if (PageAnon(page)) {
		rcu_read_lock();
		rcu_locked = 1;
	}

	/*
	 * Corner case handling:
	 * 1. When a new swap-cache page is read into, it is added to the LRU
	 * and treated as swapcache but it has no rmap yet.
	 * Calling try_to_unmap() against a page->mapping==NULL page will
	 * trigger a BUG.  So handle it here.
	 * 2. An orphaned page (see truncate_complete_page) might have
	 * fs-private metadata. The page can be picked up due to memory
	 * offlining.  Everywhere else except page reclaim, the page is
	 * invisible to the vm, so the page can not be migrated.  So try to
	 * free the metadata, so the page can be freed.
	 */
	if (!page->mapping) {
		if (!PageAnon(page) && PagePrivate(page)) {
			/*
			 * Go direct to try_to_free_buffers() here because
			 * a) that's what try_to_release_page() would do anyway
			 * b) we may be under rcu_read_lock() here, so we can't
			 *    use GFP_KERNEL which is what try_to_release_page()
			 *    needs to be effective.
			 */
			try_to_free_buffers(page);
		}
		goto rcu_unlock;
	}

	charge = mem_cgroup_prepare_migration(page);
	/* Establish migration ptes or remove ptes */
	try_to_unmap(page, 1);

	if (!page_mapped(page))
		rc = move_to_new_page(newpage, page);

	if (rc) {
		remove_migration_ptes(page, page);
		if (charge)
			mem_cgroup_end_migration(page);
	} else if (charge)
 		mem_cgroup_end_migration(newpage);
rcu_unlock:
	if (rcu_locked)
		rcu_read_unlock();

unlock:

	unlock_page(page);

	if (rc != -EAGAIN) {
 		/*
 		 * A page that has been migrated has all references
 		 * removed and will be freed. A page that has not been
 		 * migrated will have kepts its references and be
 		 * restored.
 		 */
 		list_del(&page->lru);
 		move_to_lru(page);
	}

move_newpage:
	/*
	 * Move the new page to the LRU. If migration was not successful
	 * then this will free the page.
	 */
	move_to_lru(newpage);
	if (result) {
		if (rc)
			*result = rc;
		else
			*result = page_to_nid(newpage);
	}
	return rc;
}

/*
 * migrate_pages
 *
 * The function takes one list of pages to migrate and a function
 * that determines from the page to be migrated and the private data
 * the target of the move and allocates the page.
 *
 * The function returns after 10 attempts or if no pages
 * are movable anymore because to has become empty
 * or no retryable pages exist anymore. All pages will be
 * returned to the LRU or freed.
 *
 * Return: Number of pages not migrated or error code.
 */
int migrate_pages(struct list_head *from,
		new_page_t get_new_page, unsigned long private)
{
	int retry = 1;
	int nr_failed = 0;
	int pass = 0;
	struct page *page;
	struct page *page2;
	int swapwrite = current->flags & PF_SWAPWRITE;
	int rc;

	if (!swapwrite)
		current->flags |= PF_SWAPWRITE;

	for(pass = 0; pass < 10 && retry; pass++) {
		retry = 0;

		list_for_each_entry_safe(page, page2, from, lru) {
			cond_resched();

			rc = unmap_and_move(get_new_page, private,
						page, pass > 2);

			switch(rc) {
			case -ENOMEM:
				goto out;
			case -EAGAIN:
				retry++;
				break;
			case 0:
				break;
			default:
				/* Permanent failure */
				nr_failed++;
				break;
			}
		}
	}
	rc = 0;
out:
	if (!swapwrite)
		current->flags &= ~PF_SWAPWRITE;

	putback_lru_pages(from);

	if (rc)
		return rc;

	return nr_failed + retry;
}

#ifdef CONFIG_NUMA
/*
 * Move a list of individual pages
 */
struct page_to_node {
	unsigned long addr;
	struct page *page;
	int node;
	int status;
};

static struct page *new_page_node(struct page *p, unsigned long private,
		int **result)
{
	struct page_to_node *pm = (struct page_to_node *)private;

	while (pm->node != MAX_NUMNODES && pm->page != p)
		pm++;

	if (pm->node == MAX_NUMNODES)
		return NULL;

	*result = &pm->status;

	return alloc_pages_node(pm->node,
				GFP_HIGHUSER_MOVABLE | GFP_THISNODE, 0);
}

/*
 * Move a set of pages as indicated in the pm array. The addr
 * field must be set to the virtual address of the page to be moved
 * and the node number must contain a valid target node.
 */
static int do_move_pages(struct mm_struct *mm, struct page_to_node *pm,
				int migrate_all)
{
	int err;
	struct page_to_node *pp;
	LIST_HEAD(pagelist);

	down_read(&mm->mmap_sem);

	/*
	 * Build a list of pages to migrate
	 */
	migrate_prep();
	for (pp = pm; pp->node != MAX_NUMNODES; pp++) {
		struct vm_area_struct *vma;
		struct page *page;

		/*
		 * A valid page pointer that will not match any of the
		 * pages that will be moved.
		 */
		pp->page = ZERO_PAGE(0);

		err = -EFAULT;
		vma = find_vma(mm, pp->addr);
		if (!vma || !vma_migratable(vma))
			goto set_status;

		page = follow_page(vma, pp->addr, FOLL_GET);

		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto set_status;

		err = -ENOENT;
		if (!page)
			goto set_status;

		if (PageReserved(page))		/* Check for zero page */
			goto put_and_set;

		pp->page = page;
		err = page_to_nid(page);

		if (err == pp->node)
			/*
			 * Node already in the right place
			 */
			goto put_and_set;

		err = -EACCES;
		if (page_mapcount(page) > 1 &&
				!migrate_all)
			goto put_and_set;

		err = isolate_lru_page(page, &pagelist);
put_and_set:
		/*
		 * Either remove the duplicate refcount from
		 * isolate_lru_page() or drop the page ref if it was
		 * not isolated.
		 */
		put_page(page);
set_status:
		pp->status = err;
	}

	if (!list_empty(&pagelist))
		err = migrate_pages(&pagelist, new_page_node,
				(unsigned long)pm);
	else
		err = -ENOENT;

	up_read(&mm->mmap_sem);
	return err;
}

/*
 * Determine the nodes of a list of pages. The addr in the pm array
 * must have been set to the virtual address of which we want to determine
 * the node number.
 */
static int do_pages_stat(struct mm_struct *mm, struct page_to_node *pm)
{
	down_read(&mm->mmap_sem);

	for ( ; pm->node != MAX_NUMNODES; pm++) {
		struct vm_area_struct *vma;
		struct page *page;
		int err;

		err = -EFAULT;
		vma = find_vma(mm, pm->addr);
		if (!vma)
			goto set_status;

		page = follow_page(vma, pm->addr, 0);

		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto set_status;

		err = -ENOENT;
		/* Use PageReserved to check for zero page */
		if (!page || PageReserved(page))
			goto set_status;

		err = page_to_nid(page);
set_status:
		pm->status = err;
	}

	up_read(&mm->mmap_sem);
	return 0;
}

/*
 * Move a list of pages in the address space of the currently executing
 * process.
 */
asmlinkage long sys_move_pages(pid_t pid, unsigned long nr_pages,
			const void __user * __user *pages,
			const int __user *nodes,
			int __user *status, int flags)
{
	int err = 0;
	int i;
	struct task_struct *task;
	nodemask_t task_nodes;
	struct mm_struct *mm;
	struct page_to_node *pm = NULL;

	/* Check flags */
	if (flags & ~(MPOL_MF_MOVE|MPOL_MF_MOVE_ALL))
		return -EINVAL;

	if ((flags & MPOL_MF_MOVE_ALL) && !capable(CAP_SYS_NICE))
		return -EPERM;

	/* Find the mm_struct */
	read_lock(&tasklist_lock);
	task = pid ? find_task_by_vpid(pid) : current;
	if (!task) {
		read_unlock(&tasklist_lock);
		return -ESRCH;
	}
	mm = get_task_mm(task);
	read_unlock(&tasklist_lock);

	if (!mm)
		return -EINVAL;

	/*
	 * Check if this process has the right to modify the specified
	 * process. The right exists if the process has administrative
	 * capabilities, superuser privileges or the same
	 * userid as the target process.
	 */
	if ((current->euid != task->suid) && (current->euid != task->uid) &&
	    (current->uid != task->suid) && (current->uid != task->uid) &&
	    !capable(CAP_SYS_NICE)) {
		err = -EPERM;
		goto out2;
	}

 	err = security_task_movememory(task);
 	if (err)
 		goto out2;


	task_nodes = cpuset_mems_allowed(task);

	/* Limit nr_pages so that the multiplication may not overflow */
	if (nr_pages >= ULONG_MAX / sizeof(struct page_to_node) - 1) {
		err = -E2BIG;
		goto out2;
	}

	pm = vmalloc((nr_pages + 1) * sizeof(struct page_to_node));
	if (!pm) {
		err = -ENOMEM;
		goto out2;
	}

	/*
	 * Get parameters from user space and initialize the pm
	 * array. Return various errors if the user did something wrong.
	 */
	for (i = 0; i < nr_pages; i++) {
		const void __user *p;

		err = -EFAULT;
		if (get_user(p, pages + i))
			goto out;

		pm[i].addr = (unsigned long)p;
		if (nodes) {
			int node;

			if (get_user(node, nodes + i))
				goto out;

			err = -ENODEV;
			if (!node_state(node, N_HIGH_MEMORY))
				goto out;

			err = -EACCES;
			if (!node_isset(node, task_nodes))
				goto out;

			pm[i].node = node;
		} else
			pm[i].node = 0;	/* anything to not match MAX_NUMNODES */
	}
	/* End marker */
	pm[nr_pages].node = MAX_NUMNODES;

	if (nodes)
		err = do_move_pages(mm, pm, flags & MPOL_MF_MOVE_ALL);
	else
		err = do_pages_stat(mm, pm);

	if (err >= 0)
		/* Return status information */
		for (i = 0; i < nr_pages; i++)
			if (put_user(pm[i].status, status + i))
				err = -EFAULT;

out:
	vfree(pm);
out2:
	mmput(mm);
	return err;
}
#endif

/*
 * Call migration functions in the vma_ops that may prepare
 * memory in a vm for migration. migration functions may perform
 * the migration for vmas that do not have an underlying page struct.
 */
int migrate_vmas(struct mm_struct *mm, const nodemask_t *to,
	const nodemask_t *from, unsigned long flags)
{
 	struct vm_area_struct *vma;
 	int err = 0;

 	for(vma = mm->mmap; vma->vm_next && !err; vma = vma->vm_next) {
 		if (vma->vm_ops && vma->vm_ops->migrate) {
 			err = vma->vm_ops->migrate(vma, to, from, flags);
 			if (err)
 				break;
 		}
 	}
 	return err;
}
