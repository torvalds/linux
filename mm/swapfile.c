// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/swapfile.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 */

#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/shmem_fs.h>
#include <linux/blk-cgroup.h>
#include <linux/random.h>
#include <linux/writeback.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/security.h>
#include <linux/backing-dev.h>
#include <linux/mutex.h>
#include <linux/capability.h>
#include <linux/syscalls.h>
#include <linux/memcontrol.h>
#include <linux/poll.h>
#include <linux/oom.h>
#include <linux/swapfile.h>
#include <linux/export.h>
#include <linux/swap_slots.h>
#include <linux/sort.h>
#include <linux/completion.h>
#include <linux/suspend.h>
#include <linux/zswap.h>
#include <linux/plist.h>

#include <asm/tlbflush.h>
#include <linux/swapops.h>
#include <linux/swap_cgroup.h>
#include "internal.h"
#include "swap.h"

static bool swap_count_continued(struct swap_info_struct *, pgoff_t,
				 unsigned char);
static void free_swap_count_continuations(struct swap_info_struct *);
static void swap_entry_range_free(struct swap_info_struct *si, swp_entry_t entry,
				  unsigned int nr_pages);
static void swap_range_alloc(struct swap_info_struct *si, unsigned long offset,
			     unsigned int nr_entries);
static bool folio_swapcache_freeable(struct folio *folio);
static struct swap_cluster_info *lock_cluster_or_swap_info(
		struct swap_info_struct *si, unsigned long offset);
static void unlock_cluster_or_swap_info(struct swap_info_struct *si,
					struct swap_cluster_info *ci);

static DEFINE_SPINLOCK(swap_lock);
static unsigned int nr_swapfiles;
atomic_long_t nr_swap_pages;
/*
 * Some modules use swappable objects and may try to swap them out under
 * memory pressure (via the shrinker). Before doing so, they may wish to
 * check to see if any swap space is available.
 */
EXPORT_SYMBOL_GPL(nr_swap_pages);
/* protected with swap_lock. reading in vm_swap_full() doesn't need lock */
long total_swap_pages;
static int least_priority = -1;
unsigned long swapfile_maximum_size;
#ifdef CONFIG_MIGRATION
bool swap_migration_ad_supported;
#endif	/* CONFIG_MIGRATION */

static const char Bad_file[] = "Bad swap file entry ";
static const char Unused_file[] = "Unused swap file entry ";
static const char Bad_offset[] = "Bad swap offset entry ";
static const char Unused_offset[] = "Unused swap offset entry ";

/*
 * all active swap_info_structs
 * protected with swap_lock, and ordered by priority.
 */
static PLIST_HEAD(swap_active_head);

/*
 * all available (active, not full) swap_info_structs
 * protected with swap_avail_lock, ordered by priority.
 * This is used by folio_alloc_swap() instead of swap_active_head
 * because swap_active_head includes all swap_info_structs,
 * but folio_alloc_swap() doesn't need to look at full ones.
 * This uses its own lock instead of swap_lock because when a
 * swap_info_struct changes between not-full/full, it needs to
 * add/remove itself to/from this list, but the swap_info_struct->lock
 * is held and the locking order requires swap_lock to be taken
 * before any swap_info_struct->lock.
 */
static struct plist_head *swap_avail_heads;
static DEFINE_SPINLOCK(swap_avail_lock);

static struct swap_info_struct *swap_info[MAX_SWAPFILES];

static DEFINE_MUTEX(swapon_mutex);

static DECLARE_WAIT_QUEUE_HEAD(proc_poll_wait);
/* Activity counter to indicate that a swapon or swapoff has occurred */
static atomic_t proc_poll_event = ATOMIC_INIT(0);

atomic_t nr_rotate_swap = ATOMIC_INIT(0);

static struct swap_info_struct *swap_type_to_swap_info(int type)
{
	if (type >= MAX_SWAPFILES)
		return NULL;

	return READ_ONCE(swap_info[type]); /* rcu_dereference() */
}

static inline unsigned char swap_count(unsigned char ent)
{
	return ent & ~SWAP_HAS_CACHE;	/* may include COUNT_CONTINUED flag */
}

/* Reclaim the swap entry anyway if possible */
#define TTRS_ANYWAY		0x1
/*
 * Reclaim the swap entry if there are no more mappings of the
 * corresponding page
 */
#define TTRS_UNMAPPED		0x2
/* Reclaim the swap entry if swap is getting full */
#define TTRS_FULL		0x4
/* Reclaim directly, bypass the slot cache and don't touch device lock */
#define TTRS_DIRECT		0x8

static bool swap_is_has_cache(struct swap_info_struct *si,
			      unsigned long offset, int nr_pages)
{
	unsigned char *map = si->swap_map + offset;
	unsigned char *map_end = map + nr_pages;

	do {
		VM_BUG_ON(!(*map & SWAP_HAS_CACHE));
		if (*map != SWAP_HAS_CACHE)
			return false;
	} while (++map < map_end);

	return true;
}

static bool swap_is_last_map(struct swap_info_struct *si,
		unsigned long offset, int nr_pages, bool *has_cache)
{
	unsigned char *map = si->swap_map + offset;
	unsigned char *map_end = map + nr_pages;
	unsigned char count = *map;

	if (swap_count(count) != 1)
		return false;

	while (++map < map_end) {
		if (*map != count)
			return false;
	}

	*has_cache = !!(count & SWAP_HAS_CACHE);
	return true;
}

/*
 * returns number of pages in the folio that backs the swap entry. If positive,
 * the folio was reclaimed. If negative, the folio was not reclaimed. If 0, no
 * folio was associated with the swap entry.
 */
static int __try_to_reclaim_swap(struct swap_info_struct *si,
				 unsigned long offset, unsigned long flags)
{
	swp_entry_t entry = swp_entry(si->type, offset);
	struct address_space *address_space = swap_address_space(entry);
	struct swap_cluster_info *ci;
	struct folio *folio;
	int ret, nr_pages;
	bool need_reclaim;

	folio = filemap_get_folio(address_space, swap_cache_index(entry));
	if (IS_ERR(folio))
		return 0;

	nr_pages = folio_nr_pages(folio);
	ret = -nr_pages;

	/*
	 * When this function is called from scan_swap_map_slots() and it's
	 * called by vmscan.c at reclaiming folios. So we hold a folio lock
	 * here. We have to use trylock for avoiding deadlock. This is a special
	 * case and you should use folio_free_swap() with explicit folio_lock()
	 * in usual operations.
	 */
	if (!folio_trylock(folio))
		goto out;

	/* offset could point to the middle of a large folio */
	entry = folio->swap;
	offset = swp_offset(entry);

	need_reclaim = ((flags & TTRS_ANYWAY) ||
			((flags & TTRS_UNMAPPED) && !folio_mapped(folio)) ||
			((flags & TTRS_FULL) && mem_cgroup_swap_full(folio)));
	if (!need_reclaim || !folio_swapcache_freeable(folio))
		goto out_unlock;

	/*
	 * It's safe to delete the folio from swap cache only if the folio's
	 * swap_map is HAS_CACHE only, which means the slots have no page table
	 * reference or pending writeback, and can't be allocated to others.
	 */
	ci = lock_cluster_or_swap_info(si, offset);
	need_reclaim = swap_is_has_cache(si, offset, nr_pages);
	unlock_cluster_or_swap_info(si, ci);
	if (!need_reclaim)
		goto out_unlock;

	if (!(flags & TTRS_DIRECT)) {
		/* Free through slot cache */
		delete_from_swap_cache(folio);
		folio_set_dirty(folio);
		ret = nr_pages;
		goto out_unlock;
	}

	xa_lock_irq(&address_space->i_pages);
	__delete_from_swap_cache(folio, entry, NULL);
	xa_unlock_irq(&address_space->i_pages);
	folio_ref_sub(folio, nr_pages);
	folio_set_dirty(folio);

	spin_lock(&si->lock);
	/* Only sinple page folio can be backed by zswap */
	if (nr_pages == 1)
		zswap_invalidate(entry);
	swap_entry_range_free(si, entry, nr_pages);
	spin_unlock(&si->lock);
	ret = nr_pages;
out_unlock:
	folio_unlock(folio);
out:
	folio_put(folio);
	return ret;
}

static inline struct swap_extent *first_se(struct swap_info_struct *sis)
{
	struct rb_node *rb = rb_first(&sis->swap_extent_root);
	return rb_entry(rb, struct swap_extent, rb_node);
}

static inline struct swap_extent *next_se(struct swap_extent *se)
{
	struct rb_node *rb = rb_next(&se->rb_node);
	return rb ? rb_entry(rb, struct swap_extent, rb_node) : NULL;
}

/*
 * swapon tell device that all the old swap contents can be discarded,
 * to allow the swap device to optimize its wear-levelling.
 */
static int discard_swap(struct swap_info_struct *si)
{
	struct swap_extent *se;
	sector_t start_block;
	sector_t nr_blocks;
	int err = 0;

	/* Do not discard the swap header page! */
	se = first_se(si);
	start_block = (se->start_block + 1) << (PAGE_SHIFT - 9);
	nr_blocks = ((sector_t)se->nr_pages - 1) << (PAGE_SHIFT - 9);
	if (nr_blocks) {
		err = blkdev_issue_discard(si->bdev, start_block,
				nr_blocks, GFP_KERNEL);
		if (err)
			return err;
		cond_resched();
	}

	for (se = next_se(se); se; se = next_se(se)) {
		start_block = se->start_block << (PAGE_SHIFT - 9);
		nr_blocks = (sector_t)se->nr_pages << (PAGE_SHIFT - 9);

		err = blkdev_issue_discard(si->bdev, start_block,
				nr_blocks, GFP_KERNEL);
		if (err)
			break;

		cond_resched();
	}
	return err;		/* That will often be -EOPNOTSUPP */
}

static struct swap_extent *
offset_to_swap_extent(struct swap_info_struct *sis, unsigned long offset)
{
	struct swap_extent *se;
	struct rb_node *rb;

	rb = sis->swap_extent_root.rb_node;
	while (rb) {
		se = rb_entry(rb, struct swap_extent, rb_node);
		if (offset < se->start_page)
			rb = rb->rb_left;
		else if (offset >= se->start_page + se->nr_pages)
			rb = rb->rb_right;
		else
			return se;
	}
	/* It *must* be present */
	BUG();
}

sector_t swap_folio_sector(struct folio *folio)
{
	struct swap_info_struct *sis = swp_swap_info(folio->swap);
	struct swap_extent *se;
	sector_t sector;
	pgoff_t offset;

	offset = swp_offset(folio->swap);
	se = offset_to_swap_extent(sis, offset);
	sector = se->start_block + (offset - se->start_page);
	return sector << (PAGE_SHIFT - 9);
}

/*
 * swap allocation tell device that a cluster of swap can now be discarded,
 * to allow the swap device to optimize its wear-levelling.
 */
static void discard_swap_cluster(struct swap_info_struct *si,
				 pgoff_t start_page, pgoff_t nr_pages)
{
	struct swap_extent *se = offset_to_swap_extent(si, start_page);

	while (nr_pages) {
		pgoff_t offset = start_page - se->start_page;
		sector_t start_block = se->start_block + offset;
		sector_t nr_blocks = se->nr_pages - offset;

		if (nr_blocks > nr_pages)
			nr_blocks = nr_pages;
		start_page += nr_blocks;
		nr_pages -= nr_blocks;

		start_block <<= PAGE_SHIFT - 9;
		nr_blocks <<= PAGE_SHIFT - 9;
		if (blkdev_issue_discard(si->bdev, start_block,
					nr_blocks, GFP_NOIO))
			break;

		se = next_se(se);
	}
}

#ifdef CONFIG_THP_SWAP
#define SWAPFILE_CLUSTER	HPAGE_PMD_NR

#define swap_entry_order(order)	(order)
#else
#define SWAPFILE_CLUSTER	256

/*
 * Define swap_entry_order() as constant to let compiler to optimize
 * out some code if !CONFIG_THP_SWAP
 */
#define swap_entry_order(order)	0
#endif
#define LATENCY_LIMIT		256

static inline bool cluster_is_free(struct swap_cluster_info *info)
{
	return info->flags & CLUSTER_FLAG_FREE;
}

static inline unsigned int cluster_index(struct swap_info_struct *si,
					 struct swap_cluster_info *ci)
{
	return ci - si->cluster_info;
}

static inline unsigned int cluster_offset(struct swap_info_struct *si,
					  struct swap_cluster_info *ci)
{
	return cluster_index(si, ci) * SWAPFILE_CLUSTER;
}

static inline struct swap_cluster_info *lock_cluster(struct swap_info_struct *si,
						     unsigned long offset)
{
	struct swap_cluster_info *ci;

	ci = si->cluster_info;
	if (ci) {
		ci += offset / SWAPFILE_CLUSTER;
		spin_lock(&ci->lock);
	}
	return ci;
}

static inline void unlock_cluster(struct swap_cluster_info *ci)
{
	if (ci)
		spin_unlock(&ci->lock);
}

/*
 * Determine the locking method in use for this device.  Return
 * swap_cluster_info if SSD-style cluster-based locking is in place.
 */
static inline struct swap_cluster_info *lock_cluster_or_swap_info(
		struct swap_info_struct *si, unsigned long offset)
{
	struct swap_cluster_info *ci;

	/* Try to use fine-grained SSD-style locking if available: */
	ci = lock_cluster(si, offset);
	/* Otherwise, fall back to traditional, coarse locking: */
	if (!ci)
		spin_lock(&si->lock);

	return ci;
}

static inline void unlock_cluster_or_swap_info(struct swap_info_struct *si,
					       struct swap_cluster_info *ci)
{
	if (ci)
		unlock_cluster(ci);
	else
		spin_unlock(&si->lock);
}

/* Add a cluster to discard list and schedule it to do discard */
static void swap_cluster_schedule_discard(struct swap_info_struct *si,
		struct swap_cluster_info *ci)
{
	unsigned int idx = cluster_index(si, ci);
	/*
	 * If scan_swap_map_slots() can't find a free cluster, it will check
	 * si->swap_map directly. To make sure the discarding cluster isn't
	 * taken by scan_swap_map_slots(), mark the swap entries bad (occupied).
	 * It will be cleared after discard
	 */
	memset(si->swap_map + idx * SWAPFILE_CLUSTER,
			SWAP_MAP_BAD, SWAPFILE_CLUSTER);

	VM_BUG_ON(ci->flags & CLUSTER_FLAG_FREE);
	list_move_tail(&ci->list, &si->discard_clusters);
	ci->flags = 0;
	schedule_work(&si->discard_work);
}

static void __free_cluster(struct swap_info_struct *si, struct swap_cluster_info *ci)
{
	lockdep_assert_held(&si->lock);
	lockdep_assert_held(&ci->lock);

	if (ci->flags)
		list_move_tail(&ci->list, &si->free_clusters);
	else
		list_add_tail(&ci->list, &si->free_clusters);
	ci->flags = CLUSTER_FLAG_FREE;
	ci->order = 0;
}

/*
 * Doing discard actually. After a cluster discard is finished, the cluster
 * will be added to free cluster list. caller should hold si->lock.
*/
static void swap_do_scheduled_discard(struct swap_info_struct *si)
{
	struct swap_cluster_info *ci;
	unsigned int idx;

	while (!list_empty(&si->discard_clusters)) {
		ci = list_first_entry(&si->discard_clusters, struct swap_cluster_info, list);
		list_del(&ci->list);
		idx = cluster_index(si, ci);
		spin_unlock(&si->lock);

		discard_swap_cluster(si, idx * SWAPFILE_CLUSTER,
				SWAPFILE_CLUSTER);

		spin_lock(&si->lock);
		spin_lock(&ci->lock);
		__free_cluster(si, ci);
		memset(si->swap_map + idx * SWAPFILE_CLUSTER,
				0, SWAPFILE_CLUSTER);
		spin_unlock(&ci->lock);
	}
}

static void swap_discard_work(struct work_struct *work)
{
	struct swap_info_struct *si;

	si = container_of(work, struct swap_info_struct, discard_work);

	spin_lock(&si->lock);
	swap_do_scheduled_discard(si);
	spin_unlock(&si->lock);
}

static void swap_users_ref_free(struct percpu_ref *ref)
{
	struct swap_info_struct *si;

	si = container_of(ref, struct swap_info_struct, users);
	complete(&si->comp);
}

static void free_cluster(struct swap_info_struct *si, struct swap_cluster_info *ci)
{
	VM_BUG_ON(ci->count != 0);
	lockdep_assert_held(&si->lock);
	lockdep_assert_held(&ci->lock);

	if (ci->flags & CLUSTER_FLAG_FRAG)
		si->frag_cluster_nr[ci->order]--;

	/*
	 * If the swap is discardable, prepare discard the cluster
	 * instead of free it immediately. The cluster will be freed
	 * after discard.
	 */
	if ((si->flags & (SWP_WRITEOK | SWP_PAGE_DISCARD)) ==
	    (SWP_WRITEOK | SWP_PAGE_DISCARD)) {
		swap_cluster_schedule_discard(si, ci);
		return;
	}

	__free_cluster(si, ci);
}

/*
 * The cluster corresponding to page_nr will be used. The cluster will not be
 * added to free cluster list and its usage counter will be increased by 1.
 * Only used for initialization.
 */
static void inc_cluster_info_page(struct swap_info_struct *si,
	struct swap_cluster_info *cluster_info, unsigned long page_nr)
{
	unsigned long idx = page_nr / SWAPFILE_CLUSTER;
	struct swap_cluster_info *ci;

	if (!cluster_info)
		return;

	ci = cluster_info + idx;
	ci->count++;

	VM_BUG_ON(ci->count > SWAPFILE_CLUSTER);
	VM_BUG_ON(ci->flags);
}

/*
 * The cluster ci decreases @nr_pages usage. If the usage counter becomes 0,
 * which means no page in the cluster is in use, we can optionally discard
 * the cluster and add it to free cluster list.
 */
static void dec_cluster_info_page(struct swap_info_struct *si,
				  struct swap_cluster_info *ci, int nr_pages)
{
	if (!si->cluster_info)
		return;

	VM_BUG_ON(ci->count < nr_pages);
	VM_BUG_ON(cluster_is_free(ci));
	lockdep_assert_held(&si->lock);
	lockdep_assert_held(&ci->lock);
	ci->count -= nr_pages;

	if (!ci->count) {
		free_cluster(si, ci);
		return;
	}

	if (!(ci->flags & CLUSTER_FLAG_NONFULL)) {
		VM_BUG_ON(ci->flags & CLUSTER_FLAG_FREE);
		if (ci->flags & CLUSTER_FLAG_FRAG)
			si->frag_cluster_nr[ci->order]--;
		list_move_tail(&ci->list, &si->nonfull_clusters[ci->order]);
		ci->flags = CLUSTER_FLAG_NONFULL;
	}
}

static bool cluster_reclaim_range(struct swap_info_struct *si,
				  struct swap_cluster_info *ci,
				  unsigned long start, unsigned long end)
{
	unsigned char *map = si->swap_map;
	unsigned long offset;

	spin_unlock(&ci->lock);
	spin_unlock(&si->lock);

	for (offset = start; offset < end; offset++) {
		switch (READ_ONCE(map[offset])) {
		case 0:
			continue;
		case SWAP_HAS_CACHE:
			if (__try_to_reclaim_swap(si, offset, TTRS_ANYWAY | TTRS_DIRECT) > 0)
				continue;
			goto out;
		default:
			goto out;
		}
	}
out:
	spin_lock(&si->lock);
	spin_lock(&ci->lock);

	/*
	 * Recheck the range no matter reclaim succeeded or not, the slot
	 * could have been be freed while we are not holding the lock.
	 */
	for (offset = start; offset < end; offset++)
		if (READ_ONCE(map[offset]))
			return false;

	return true;
}

static bool cluster_scan_range(struct swap_info_struct *si,
			       struct swap_cluster_info *ci,
			       unsigned long start, unsigned int nr_pages)
{
	unsigned long offset, end = start + nr_pages;
	unsigned char *map = si->swap_map;
	bool need_reclaim = false;

	for (offset = start; offset < end; offset++) {
		switch (READ_ONCE(map[offset])) {
		case 0:
			continue;
		case SWAP_HAS_CACHE:
			if (!vm_swap_full())
				return false;
			need_reclaim = true;
			continue;
		default:
			return false;
		}
	}

	if (need_reclaim)
		return cluster_reclaim_range(si, ci, start, end);

	return true;
}

static bool cluster_alloc_range(struct swap_info_struct *si, struct swap_cluster_info *ci,
				unsigned int start, unsigned char usage,
				unsigned int order)
{
	unsigned int nr_pages = 1 << order;

	if (!(si->flags & SWP_WRITEOK))
		return false;

	if (cluster_is_free(ci)) {
		if (nr_pages < SWAPFILE_CLUSTER) {
			list_move_tail(&ci->list, &si->nonfull_clusters[order]);
			ci->flags = CLUSTER_FLAG_NONFULL;
		}
		ci->order = order;
	}

	memset(si->swap_map + start, usage, nr_pages);
	swap_range_alloc(si, start, nr_pages);
	ci->count += nr_pages;

	if (ci->count == SWAPFILE_CLUSTER) {
		VM_BUG_ON(!(ci->flags &
			  (CLUSTER_FLAG_FREE | CLUSTER_FLAG_NONFULL | CLUSTER_FLAG_FRAG)));
		if (ci->flags & CLUSTER_FLAG_FRAG)
			si->frag_cluster_nr[ci->order]--;
		list_move_tail(&ci->list, &si->full_clusters);
		ci->flags = CLUSTER_FLAG_FULL;
	}

	return true;
}

static unsigned int alloc_swap_scan_cluster(struct swap_info_struct *si, unsigned long offset,
					    unsigned int *foundp, unsigned int order,
					    unsigned char usage)
{
	unsigned long start = offset & ~(SWAPFILE_CLUSTER - 1);
	unsigned long end = min(start + SWAPFILE_CLUSTER, si->max);
	unsigned int nr_pages = 1 << order;
	struct swap_cluster_info *ci;

	if (end < nr_pages)
		return SWAP_NEXT_INVALID;
	end -= nr_pages;

	ci = lock_cluster(si, offset);
	if (ci->count + nr_pages > SWAPFILE_CLUSTER) {
		offset = SWAP_NEXT_INVALID;
		goto done;
	}

	while (offset <= end) {
		if (cluster_scan_range(si, ci, offset, nr_pages)) {
			if (!cluster_alloc_range(si, ci, offset, usage, order)) {
				offset = SWAP_NEXT_INVALID;
				goto done;
			}
			*foundp = offset;
			if (ci->count == SWAPFILE_CLUSTER) {
				offset = SWAP_NEXT_INVALID;
				goto done;
			}
			offset += nr_pages;
			break;
		}
		offset += nr_pages;
	}
	if (offset > end)
		offset = SWAP_NEXT_INVALID;
done:
	unlock_cluster(ci);
	return offset;
}

/* Return true if reclaimed a whole cluster */
static void swap_reclaim_full_clusters(struct swap_info_struct *si, bool force)
{
	long to_scan = 1;
	unsigned long offset, end;
	struct swap_cluster_info *ci;
	unsigned char *map = si->swap_map;
	int nr_reclaim;

	if (force)
		to_scan = si->inuse_pages / SWAPFILE_CLUSTER;

	while (!list_empty(&si->full_clusters)) {
		ci = list_first_entry(&si->full_clusters, struct swap_cluster_info, list);
		list_move_tail(&ci->list, &si->full_clusters);
		offset = cluster_offset(si, ci);
		end = min(si->max, offset + SWAPFILE_CLUSTER);
		to_scan--;

		spin_unlock(&si->lock);
		while (offset < end) {
			if (READ_ONCE(map[offset]) == SWAP_HAS_CACHE) {
				nr_reclaim = __try_to_reclaim_swap(si, offset,
								   TTRS_ANYWAY | TTRS_DIRECT);
				if (nr_reclaim) {
					offset += abs(nr_reclaim);
					continue;
				}
			}
			offset++;
		}
		spin_lock(&si->lock);

		if (to_scan <= 0)
			break;
	}
}

static void swap_reclaim_work(struct work_struct *work)
{
	struct swap_info_struct *si;

	si = container_of(work, struct swap_info_struct, reclaim_work);

	spin_lock(&si->lock);
	swap_reclaim_full_clusters(si, true);
	spin_unlock(&si->lock);
}

/*
 * Try to get swap entries with specified order from current cpu's swap entry
 * pool (a cluster). This might involve allocating a new cluster for current CPU
 * too.
 */
static unsigned long cluster_alloc_swap_entry(struct swap_info_struct *si, int order,
					      unsigned char usage)
{
	struct percpu_cluster *cluster;
	struct swap_cluster_info *ci;
	unsigned int offset, found = 0;

new_cluster:
	lockdep_assert_held(&si->lock);
	cluster = this_cpu_ptr(si->percpu_cluster);
	offset = cluster->next[order];
	if (offset) {
		offset = alloc_swap_scan_cluster(si, offset, &found, order, usage);
		if (found)
			goto done;
	}

	if (!list_empty(&si->free_clusters)) {
		ci = list_first_entry(&si->free_clusters, struct swap_cluster_info, list);
		offset = alloc_swap_scan_cluster(si, cluster_offset(si, ci), &found, order, usage);
		/*
		 * Either we didn't touch the cluster due to swapoff,
		 * or the allocation must success.
		 */
		VM_BUG_ON((si->flags & SWP_WRITEOK) && !found);
		goto done;
	}

	/* Try reclaim from full clusters if free clusters list is drained */
	if (vm_swap_full())
		swap_reclaim_full_clusters(si, false);

	if (order < PMD_ORDER) {
		unsigned int frags = 0;

		while (!list_empty(&si->nonfull_clusters[order])) {
			ci = list_first_entry(&si->nonfull_clusters[order],
					      struct swap_cluster_info, list);
			list_move_tail(&ci->list, &si->frag_clusters[order]);
			ci->flags = CLUSTER_FLAG_FRAG;
			si->frag_cluster_nr[order]++;
			offset = alloc_swap_scan_cluster(si, cluster_offset(si, ci),
							 &found, order, usage);
			frags++;
			if (found)
				break;
		}

		if (!found) {
			/*
			 * Nonfull clusters are moved to frag tail if we reached
			 * here, count them too, don't over scan the frag list.
			 */
			while (frags < si->frag_cluster_nr[order]) {
				ci = list_first_entry(&si->frag_clusters[order],
						      struct swap_cluster_info, list);
				/*
				 * Rotate the frag list to iterate, they were all failing
				 * high order allocation or moved here due to per-CPU usage,
				 * this help keeping usable cluster ahead.
				 */
				list_move_tail(&ci->list, &si->frag_clusters[order]);
				offset = alloc_swap_scan_cluster(si, cluster_offset(si, ci),
								 &found, order, usage);
				frags++;
				if (found)
					break;
			}
		}
	}

	if (found)
		goto done;

	if (!list_empty(&si->discard_clusters)) {
		/*
		 * we don't have free cluster but have some clusters in
		 * discarding, do discard now and reclaim them, then
		 * reread cluster_next_cpu since we dropped si->lock
		 */
		swap_do_scheduled_discard(si);
		goto new_cluster;
	}

	if (order)
		goto done;

	/* Order 0 stealing from higher order */
	for (int o = 1; o < SWAP_NR_ORDERS; o++) {
		/*
		 * Clusters here have at least one usable slots and can't fail order 0
		 * allocation, but reclaim may drop si->lock and race with another user.
		 */
		while (!list_empty(&si->frag_clusters[o])) {
			ci = list_first_entry(&si->frag_clusters[o],
					      struct swap_cluster_info, list);
			offset = alloc_swap_scan_cluster(si, cluster_offset(si, ci),
							 &found, 0, usage);
			if (found)
				goto done;
		}

		while (!list_empty(&si->nonfull_clusters[o])) {
			ci = list_first_entry(&si->nonfull_clusters[o],
					      struct swap_cluster_info, list);
			offset = alloc_swap_scan_cluster(si, cluster_offset(si, ci),
							 &found, 0, usage);
			if (found)
				goto done;
		}
	}

done:
	cluster->next[order] = offset;
	return found;
}

static void __del_from_avail_list(struct swap_info_struct *si)
{
	int nid;

	assert_spin_locked(&si->lock);
	for_each_node(nid)
		plist_del(&si->avail_lists[nid], &swap_avail_heads[nid]);
}

static void del_from_avail_list(struct swap_info_struct *si)
{
	spin_lock(&swap_avail_lock);
	__del_from_avail_list(si);
	spin_unlock(&swap_avail_lock);
}

static void swap_range_alloc(struct swap_info_struct *si, unsigned long offset,
			     unsigned int nr_entries)
{
	unsigned int end = offset + nr_entries - 1;

	if (offset == si->lowest_bit)
		si->lowest_bit += nr_entries;
	if (end == si->highest_bit)
		WRITE_ONCE(si->highest_bit, si->highest_bit - nr_entries);
	WRITE_ONCE(si->inuse_pages, si->inuse_pages + nr_entries);
	if (si->inuse_pages == si->pages) {
		si->lowest_bit = si->max;
		si->highest_bit = 0;
		del_from_avail_list(si);

		if (si->cluster_info && vm_swap_full())
			schedule_work(&si->reclaim_work);
	}
}

static void add_to_avail_list(struct swap_info_struct *si)
{
	int nid;

	spin_lock(&swap_avail_lock);
	for_each_node(nid)
		plist_add(&si->avail_lists[nid], &swap_avail_heads[nid]);
	spin_unlock(&swap_avail_lock);
}

static void swap_range_free(struct swap_info_struct *si, unsigned long offset,
			    unsigned int nr_entries)
{
	unsigned long begin = offset;
	unsigned long end = offset + nr_entries - 1;
	void (*swap_slot_free_notify)(struct block_device *, unsigned long);
	unsigned int i;

	/*
	 * Use atomic clear_bit operations only on zeromap instead of non-atomic
	 * bitmap_clear to prevent adjacent bits corruption due to simultaneous writes.
	 */
	for (i = 0; i < nr_entries; i++)
		clear_bit(offset + i, si->zeromap);

	if (offset < si->lowest_bit)
		si->lowest_bit = offset;
	if (end > si->highest_bit) {
		bool was_full = !si->highest_bit;

		WRITE_ONCE(si->highest_bit, end);
		if (was_full && (si->flags & SWP_WRITEOK))
			add_to_avail_list(si);
	}
	if (si->flags & SWP_BLKDEV)
		swap_slot_free_notify =
			si->bdev->bd_disk->fops->swap_slot_free_notify;
	else
		swap_slot_free_notify = NULL;
	while (offset <= end) {
		arch_swap_invalidate_page(si->type, offset);
		if (swap_slot_free_notify)
			swap_slot_free_notify(si->bdev, offset);
		offset++;
	}
	clear_shadow_from_swap_cache(si->type, begin, end);

	/*
	 * Make sure that try_to_unuse() observes si->inuse_pages reaching 0
	 * only after the above cleanups are done.
	 */
	smp_wmb();
	atomic_long_add(nr_entries, &nr_swap_pages);
	WRITE_ONCE(si->inuse_pages, si->inuse_pages - nr_entries);
}

static void set_cluster_next(struct swap_info_struct *si, unsigned long next)
{
	unsigned long prev;

	if (!(si->flags & SWP_SOLIDSTATE)) {
		si->cluster_next = next;
		return;
	}

	prev = this_cpu_read(*si->cluster_next_cpu);
	/*
	 * Cross the swap address space size aligned trunk, choose
	 * another trunk randomly to avoid lock contention on swap
	 * address space if possible.
	 */
	if ((prev >> SWAP_ADDRESS_SPACE_SHIFT) !=
	    (next >> SWAP_ADDRESS_SPACE_SHIFT)) {
		/* No free swap slots available */
		if (si->highest_bit <= si->lowest_bit)
			return;
		next = get_random_u32_inclusive(si->lowest_bit, si->highest_bit);
		next = ALIGN_DOWN(next, SWAP_ADDRESS_SPACE_PAGES);
		next = max_t(unsigned int, next, si->lowest_bit);
	}
	this_cpu_write(*si->cluster_next_cpu, next);
}

static bool swap_offset_available_and_locked(struct swap_info_struct *si,
					     unsigned long offset)
{
	if (data_race(!si->swap_map[offset])) {
		spin_lock(&si->lock);
		return true;
	}

	if (vm_swap_full() && READ_ONCE(si->swap_map[offset]) == SWAP_HAS_CACHE) {
		spin_lock(&si->lock);
		return true;
	}

	return false;
}

static int cluster_alloc_swap(struct swap_info_struct *si,
			     unsigned char usage, int nr,
			     swp_entry_t slots[], int order)
{
	int n_ret = 0;

	VM_BUG_ON(!si->cluster_info);

	si->flags += SWP_SCANNING;

	while (n_ret < nr) {
		unsigned long offset = cluster_alloc_swap_entry(si, order, usage);

		if (!offset)
			break;
		slots[n_ret++] = swp_entry(si->type, offset);
	}

	si->flags -= SWP_SCANNING;

	return n_ret;
}

static int scan_swap_map_slots(struct swap_info_struct *si,
			       unsigned char usage, int nr,
			       swp_entry_t slots[], int order)
{
	unsigned long offset;
	unsigned long scan_base;
	unsigned long last_in_cluster = 0;
	int latency_ration = LATENCY_LIMIT;
	unsigned int nr_pages = 1 << order;
	int n_ret = 0;
	bool scanned_many = false;

	/*
	 * We try to cluster swap pages by allocating them sequentially
	 * in swap.  Once we've allocated SWAPFILE_CLUSTER pages this
	 * way, however, we resort to first-free allocation, starting
	 * a new cluster.  This prevents us from scattering swap pages
	 * all over the entire swap partition, so that we reduce
	 * overall disk seek times between swap pages.  -- sct
	 * But we do now try to find an empty cluster.  -Andrea
	 * And we let swap pages go all over an SSD partition.  Hugh
	 */

	if (order > 0) {
		/*
		 * Should not even be attempting large allocations when huge
		 * page swap is disabled.  Warn and fail the allocation.
		 */
		if (!IS_ENABLED(CONFIG_THP_SWAP) ||
		    nr_pages > SWAPFILE_CLUSTER) {
			VM_WARN_ON_ONCE(1);
			return 0;
		}

		/*
		 * Swapfile is not block device or not using clusters so unable
		 * to allocate large entries.
		 */
		if (!(si->flags & SWP_BLKDEV) || !si->cluster_info)
			return 0;
	}

	if (si->cluster_info)
		return cluster_alloc_swap(si, usage, nr, slots, order);

	si->flags += SWP_SCANNING;

	/* For HDD, sequential access is more important. */
	scan_base = si->cluster_next;
	offset = scan_base;

	if (unlikely(!si->cluster_nr--)) {
		if (si->pages - si->inuse_pages < SWAPFILE_CLUSTER) {
			si->cluster_nr = SWAPFILE_CLUSTER - 1;
			goto checks;
		}

		spin_unlock(&si->lock);

		/*
		 * If seek is expensive, start searching for new cluster from
		 * start of partition, to minimize the span of allocated swap.
		 */
		scan_base = offset = si->lowest_bit;
		last_in_cluster = offset + SWAPFILE_CLUSTER - 1;

		/* Locate the first empty (unaligned) cluster */
		for (; last_in_cluster <= READ_ONCE(si->highest_bit); offset++) {
			if (si->swap_map[offset])
				last_in_cluster = offset + SWAPFILE_CLUSTER;
			else if (offset == last_in_cluster) {
				spin_lock(&si->lock);
				offset -= SWAPFILE_CLUSTER - 1;
				si->cluster_next = offset;
				si->cluster_nr = SWAPFILE_CLUSTER - 1;
				goto checks;
			}
			if (unlikely(--latency_ration < 0)) {
				cond_resched();
				latency_ration = LATENCY_LIMIT;
			}
		}

		offset = scan_base;
		spin_lock(&si->lock);
		si->cluster_nr = SWAPFILE_CLUSTER - 1;
	}

checks:
	if (!(si->flags & SWP_WRITEOK))
		goto no_page;
	if (!si->highest_bit)
		goto no_page;
	if (offset > si->highest_bit)
		scan_base = offset = si->lowest_bit;

	/* reuse swap entry of cache-only swap if not busy. */
	if (vm_swap_full() && si->swap_map[offset] == SWAP_HAS_CACHE) {
		int swap_was_freed;
		spin_unlock(&si->lock);
		swap_was_freed = __try_to_reclaim_swap(si, offset, TTRS_ANYWAY | TTRS_DIRECT);
		spin_lock(&si->lock);
		/* entry was freed successfully, try to use this again */
		if (swap_was_freed > 0)
			goto checks;
		goto scan; /* check next one */
	}

	if (si->swap_map[offset]) {
		if (!n_ret)
			goto scan;
		else
			goto done;
	}
	memset(si->swap_map + offset, usage, nr_pages);

	swap_range_alloc(si, offset, nr_pages);
	slots[n_ret++] = swp_entry(si->type, offset);

	/* got enough slots or reach max slots? */
	if ((n_ret == nr) || (offset >= si->highest_bit))
		goto done;

	/* search for next available slot */

	/* time to take a break? */
	if (unlikely(--latency_ration < 0)) {
		if (n_ret)
			goto done;
		spin_unlock(&si->lock);
		cond_resched();
		spin_lock(&si->lock);
		latency_ration = LATENCY_LIMIT;
	}

	if (si->cluster_nr && !si->swap_map[++offset]) {
		/* non-ssd case, still more slots in cluster? */
		--si->cluster_nr;
		goto checks;
	}

	/*
	 * Even if there's no free clusters available (fragmented),
	 * try to scan a little more quickly with lock held unless we
	 * have scanned too many slots already.
	 */
	if (!scanned_many) {
		unsigned long scan_limit;

		if (offset < scan_base)
			scan_limit = scan_base;
		else
			scan_limit = si->highest_bit;
		for (; offset <= scan_limit && --latency_ration > 0;
		     offset++) {
			if (!si->swap_map[offset])
				goto checks;
		}
	}

done:
	if (order == 0)
		set_cluster_next(si, offset + 1);
	si->flags -= SWP_SCANNING;
	return n_ret;

scan:
	VM_WARN_ON(order > 0);
	spin_unlock(&si->lock);
	while (++offset <= READ_ONCE(si->highest_bit)) {
		if (unlikely(--latency_ration < 0)) {
			cond_resched();
			latency_ration = LATENCY_LIMIT;
			scanned_many = true;
		}
		if (swap_offset_available_and_locked(si, offset))
			goto checks;
	}
	offset = si->lowest_bit;
	while (offset < scan_base) {
		if (unlikely(--latency_ration < 0)) {
			cond_resched();
			latency_ration = LATENCY_LIMIT;
			scanned_many = true;
		}
		if (swap_offset_available_and_locked(si, offset))
			goto checks;
		offset++;
	}
	spin_lock(&si->lock);

no_page:
	si->flags -= SWP_SCANNING;
	return n_ret;
}

int get_swap_pages(int n_goal, swp_entry_t swp_entries[], int entry_order)
{
	int order = swap_entry_order(entry_order);
	unsigned long size = 1 << order;
	struct swap_info_struct *si, *next;
	long avail_pgs;
	int n_ret = 0;
	int node;

	spin_lock(&swap_avail_lock);

	avail_pgs = atomic_long_read(&nr_swap_pages) / size;
	if (avail_pgs <= 0) {
		spin_unlock(&swap_avail_lock);
		goto noswap;
	}

	n_goal = min3((long)n_goal, (long)SWAP_BATCH, avail_pgs);

	atomic_long_sub(n_goal * size, &nr_swap_pages);

start_over:
	node = numa_node_id();
	plist_for_each_entry_safe(si, next, &swap_avail_heads[node], avail_lists[node]) {
		/* requeue si to after same-priority siblings */
		plist_requeue(&si->avail_lists[node], &swap_avail_heads[node]);
		spin_unlock(&swap_avail_lock);
		spin_lock(&si->lock);
		if (!si->highest_bit || !(si->flags & SWP_WRITEOK)) {
			spin_lock(&swap_avail_lock);
			if (plist_node_empty(&si->avail_lists[node])) {
				spin_unlock(&si->lock);
				goto nextsi;
			}
			WARN(!si->highest_bit,
			     "swap_info %d in list but !highest_bit\n",
			     si->type);
			WARN(!(si->flags & SWP_WRITEOK),
			     "swap_info %d in list but !SWP_WRITEOK\n",
			     si->type);
			__del_from_avail_list(si);
			spin_unlock(&si->lock);
			goto nextsi;
		}
		n_ret = scan_swap_map_slots(si, SWAP_HAS_CACHE,
					    n_goal, swp_entries, order);
		spin_unlock(&si->lock);
		if (n_ret || size > 1)
			goto check_out;
		cond_resched();

		spin_lock(&swap_avail_lock);
nextsi:
		/*
		 * if we got here, it's likely that si was almost full before,
		 * and since scan_swap_map_slots() can drop the si->lock,
		 * multiple callers probably all tried to get a page from the
		 * same si and it filled up before we could get one; or, the si
		 * filled up between us dropping swap_avail_lock and taking
		 * si->lock. Since we dropped the swap_avail_lock, the
		 * swap_avail_head list may have been modified; so if next is
		 * still in the swap_avail_head list then try it, otherwise
		 * start over if we have not gotten any slots.
		 */
		if (plist_node_empty(&next->avail_lists[node]))
			goto start_over;
	}

	spin_unlock(&swap_avail_lock);

check_out:
	if (n_ret < n_goal)
		atomic_long_add((long)(n_goal - n_ret) * size,
				&nr_swap_pages);
noswap:
	return n_ret;
}

static struct swap_info_struct *_swap_info_get(swp_entry_t entry)
{
	struct swap_info_struct *si;
	unsigned long offset;

	if (!entry.val)
		goto out;
	si = swp_swap_info(entry);
	if (!si)
		goto bad_nofile;
	if (data_race(!(si->flags & SWP_USED)))
		goto bad_device;
	offset = swp_offset(entry);
	if (offset >= si->max)
		goto bad_offset;
	if (data_race(!si->swap_map[swp_offset(entry)]))
		goto bad_free;
	return si;

bad_free:
	pr_err("%s: %s%08lx\n", __func__, Unused_offset, entry.val);
	goto out;
bad_offset:
	pr_err("%s: %s%08lx\n", __func__, Bad_offset, entry.val);
	goto out;
bad_device:
	pr_err("%s: %s%08lx\n", __func__, Unused_file, entry.val);
	goto out;
bad_nofile:
	pr_err("%s: %s%08lx\n", __func__, Bad_file, entry.val);
out:
	return NULL;
}

static struct swap_info_struct *swap_info_get_cont(swp_entry_t entry,
					struct swap_info_struct *q)
{
	struct swap_info_struct *p;

	p = _swap_info_get(entry);

	if (p != q) {
		if (q != NULL)
			spin_unlock(&q->lock);
		if (p != NULL)
			spin_lock(&p->lock);
	}
	return p;
}

static unsigned char __swap_entry_free_locked(struct swap_info_struct *si,
					      unsigned long offset,
					      unsigned char usage)
{
	unsigned char count;
	unsigned char has_cache;

	count = si->swap_map[offset];

	has_cache = count & SWAP_HAS_CACHE;
	count &= ~SWAP_HAS_CACHE;

	if (usage == SWAP_HAS_CACHE) {
		VM_BUG_ON(!has_cache);
		has_cache = 0;
	} else if (count == SWAP_MAP_SHMEM) {
		/*
		 * Or we could insist on shmem.c using a special
		 * swap_shmem_free() and free_shmem_swap_and_cache()...
		 */
		count = 0;
	} else if ((count & ~COUNT_CONTINUED) <= SWAP_MAP_MAX) {
		if (count == COUNT_CONTINUED) {
			if (swap_count_continued(si, offset, count))
				count = SWAP_MAP_MAX | COUNT_CONTINUED;
			else
				count = SWAP_MAP_MAX;
		} else
			count--;
	}

	usage = count | has_cache;
	if (usage)
		WRITE_ONCE(si->swap_map[offset], usage);
	else
		WRITE_ONCE(si->swap_map[offset], SWAP_HAS_CACHE);

	return usage;
}

/*
 * When we get a swap entry, if there aren't some other ways to
 * prevent swapoff, such as the folio in swap cache is locked, RCU
 * reader side is locked, etc., the swap entry may become invalid
 * because of swapoff.  Then, we need to enclose all swap related
 * functions with get_swap_device() and put_swap_device(), unless the
 * swap functions call get/put_swap_device() by themselves.
 *
 * RCU reader side lock (including any spinlock) is sufficient to
 * prevent swapoff, because synchronize_rcu() is called in swapoff()
 * before freeing data structures.
 *
 * Check whether swap entry is valid in the swap device.  If so,
 * return pointer to swap_info_struct, and keep the swap entry valid
 * via preventing the swap device from being swapoff, until
 * put_swap_device() is called.  Otherwise return NULL.
 *
 * Notice that swapoff or swapoff+swapon can still happen before the
 * percpu_ref_tryget_live() in get_swap_device() or after the
 * percpu_ref_put() in put_swap_device() if there isn't any other way
 * to prevent swapoff.  The caller must be prepared for that.  For
 * example, the following situation is possible.
 *
 *   CPU1				CPU2
 *   do_swap_page()
 *     ...				swapoff+swapon
 *     __read_swap_cache_async()
 *       swapcache_prepare()
 *         __swap_duplicate()
 *           // check swap_map
 *     // verify PTE not changed
 *
 * In __swap_duplicate(), the swap_map need to be checked before
 * changing partly because the specified swap entry may be for another
 * swap device which has been swapoff.  And in do_swap_page(), after
 * the page is read from the swap device, the PTE is verified not
 * changed with the page table locked to check whether the swap device
 * has been swapoff or swapoff+swapon.
 */
struct swap_info_struct *get_swap_device(swp_entry_t entry)
{
	struct swap_info_struct *si;
	unsigned long offset;

	if (!entry.val)
		goto out;
	si = swp_swap_info(entry);
	if (!si)
		goto bad_nofile;
	if (!percpu_ref_tryget_live(&si->users))
		goto out;
	/*
	 * Guarantee the si->users are checked before accessing other
	 * fields of swap_info_struct.
	 *
	 * Paired with the spin_unlock() after setup_swap_info() in
	 * enable_swap_info().
	 */
	smp_rmb();
	offset = swp_offset(entry);
	if (offset >= si->max)
		goto put_out;

	return si;
bad_nofile:
	pr_err("%s: %s%08lx\n", __func__, Bad_file, entry.val);
out:
	return NULL;
put_out:
	pr_err("%s: %s%08lx\n", __func__, Bad_offset, entry.val);
	percpu_ref_put(&si->users);
	return NULL;
}

static unsigned char __swap_entry_free(struct swap_info_struct *si,
				       swp_entry_t entry)
{
	struct swap_cluster_info *ci;
	unsigned long offset = swp_offset(entry);
	unsigned char usage;

	ci = lock_cluster_or_swap_info(si, offset);
	usage = __swap_entry_free_locked(si, offset, 1);
	unlock_cluster_or_swap_info(si, ci);
	if (!usage)
		free_swap_slot(entry);

	return usage;
}

static bool __swap_entries_free(struct swap_info_struct *si,
		swp_entry_t entry, int nr)
{
	unsigned long offset = swp_offset(entry);
	unsigned int type = swp_type(entry);
	struct swap_cluster_info *ci;
	bool has_cache = false;
	unsigned char count;
	int i;

	if (nr <= 1 || swap_count(data_race(si->swap_map[offset])) != 1)
		goto fallback;
	/* cross into another cluster */
	if (nr > SWAPFILE_CLUSTER - offset % SWAPFILE_CLUSTER)
		goto fallback;

	ci = lock_cluster_or_swap_info(si, offset);
	if (!swap_is_last_map(si, offset, nr, &has_cache)) {
		unlock_cluster_or_swap_info(si, ci);
		goto fallback;
	}
	for (i = 0; i < nr; i++)
		WRITE_ONCE(si->swap_map[offset + i], SWAP_HAS_CACHE);
	unlock_cluster_or_swap_info(si, ci);

	if (!has_cache) {
		for (i = 0; i < nr; i++)
			zswap_invalidate(swp_entry(si->type, offset + i));
		spin_lock(&si->lock);
		swap_entry_range_free(si, entry, nr);
		spin_unlock(&si->lock);
	}
	return has_cache;

fallback:
	for (i = 0; i < nr; i++) {
		if (data_race(si->swap_map[offset + i])) {
			count = __swap_entry_free(si, swp_entry(type, offset + i));
			if (count == SWAP_HAS_CACHE)
				has_cache = true;
		} else {
			WARN_ON_ONCE(1);
		}
	}
	return has_cache;
}

/*
 * Drop the last HAS_CACHE flag of swap entries, caller have to
 * ensure all entries belong to the same cgroup.
 */
static void swap_entry_range_free(struct swap_info_struct *si, swp_entry_t entry,
				  unsigned int nr_pages)
{
	unsigned long offset = swp_offset(entry);
	unsigned char *map = si->swap_map + offset;
	unsigned char *map_end = map + nr_pages;
	struct swap_cluster_info *ci;

	ci = lock_cluster(si, offset);
	do {
		VM_BUG_ON(*map != SWAP_HAS_CACHE);
		*map = 0;
	} while (++map < map_end);
	dec_cluster_info_page(si, ci, nr_pages);
	unlock_cluster(ci);

	mem_cgroup_uncharge_swap(entry, nr_pages);
	swap_range_free(si, offset, nr_pages);
}

static void cluster_swap_free_nr(struct swap_info_struct *si,
		unsigned long offset, int nr_pages,
		unsigned char usage)
{
	struct swap_cluster_info *ci;
	DECLARE_BITMAP(to_free, BITS_PER_LONG) = { 0 };
	int i, nr;

	ci = lock_cluster_or_swap_info(si, offset);
	while (nr_pages) {
		nr = min(BITS_PER_LONG, nr_pages);
		for (i = 0; i < nr; i++) {
			if (!__swap_entry_free_locked(si, offset + i, usage))
				bitmap_set(to_free, i, 1);
		}
		if (!bitmap_empty(to_free, BITS_PER_LONG)) {
			unlock_cluster_or_swap_info(si, ci);
			for_each_set_bit(i, to_free, BITS_PER_LONG)
				free_swap_slot(swp_entry(si->type, offset + i));
			if (nr == nr_pages)
				return;
			bitmap_clear(to_free, 0, BITS_PER_LONG);
			ci = lock_cluster_or_swap_info(si, offset);
		}
		offset += nr;
		nr_pages -= nr;
	}
	unlock_cluster_or_swap_info(si, ci);
}

/*
 * Caller has made sure that the swap device corresponding to entry
 * is still around or has not been recycled.
 */
void swap_free_nr(swp_entry_t entry, int nr_pages)
{
	int nr;
	struct swap_info_struct *sis;
	unsigned long offset = swp_offset(entry);

	sis = _swap_info_get(entry);
	if (!sis)
		return;

	while (nr_pages) {
		nr = min_t(int, nr_pages, SWAPFILE_CLUSTER - offset % SWAPFILE_CLUSTER);
		cluster_swap_free_nr(sis, offset, nr, 1);
		offset += nr;
		nr_pages -= nr;
	}
}

/*
 * Called after dropping swapcache to decrease refcnt to swap entries.
 */
void put_swap_folio(struct folio *folio, swp_entry_t entry)
{
	unsigned long offset = swp_offset(entry);
	struct swap_cluster_info *ci;
	struct swap_info_struct *si;
	int size = 1 << swap_entry_order(folio_order(folio));

	si = _swap_info_get(entry);
	if (!si)
		return;

	ci = lock_cluster_or_swap_info(si, offset);
	if (size > 1 && swap_is_has_cache(si, offset, size)) {
		unlock_cluster_or_swap_info(si, ci);
		spin_lock(&si->lock);
		swap_entry_range_free(si, entry, size);
		spin_unlock(&si->lock);
		return;
	}
	for (int i = 0; i < size; i++, entry.val++) {
		if (!__swap_entry_free_locked(si, offset + i, SWAP_HAS_CACHE)) {
			unlock_cluster_or_swap_info(si, ci);
			free_swap_slot(entry);
			if (i == size - 1)
				return;
			lock_cluster_or_swap_info(si, offset);
		}
	}
	unlock_cluster_or_swap_info(si, ci);
}

static int swp_entry_cmp(const void *ent1, const void *ent2)
{
	const swp_entry_t *e1 = ent1, *e2 = ent2;

	return (int)swp_type(*e1) - (int)swp_type(*e2);
}

void swapcache_free_entries(swp_entry_t *entries, int n)
{
	struct swap_info_struct *p, *prev;
	int i;

	if (n <= 0)
		return;

	prev = NULL;
	p = NULL;

	/*
	 * Sort swap entries by swap device, so each lock is only taken once.
	 * nr_swapfiles isn't absolutely correct, but the overhead of sort() is
	 * so low that it isn't necessary to optimize further.
	 */
	if (nr_swapfiles > 1)
		sort(entries, n, sizeof(entries[0]), swp_entry_cmp, NULL);
	for (i = 0; i < n; ++i) {
		p = swap_info_get_cont(entries[i], prev);
		if (p)
			swap_entry_range_free(p, entries[i], 1);
		prev = p;
	}
	if (p)
		spin_unlock(&p->lock);
}

int __swap_count(swp_entry_t entry)
{
	struct swap_info_struct *si = swp_swap_info(entry);
	pgoff_t offset = swp_offset(entry);

	return swap_count(si->swap_map[offset]);
}

/*
 * How many references to @entry are currently swapped out?
 * This does not give an exact answer when swap count is continued,
 * but does include the high COUNT_CONTINUED flag to allow for that.
 */
int swap_swapcount(struct swap_info_struct *si, swp_entry_t entry)
{
	pgoff_t offset = swp_offset(entry);
	struct swap_cluster_info *ci;
	int count;

	ci = lock_cluster_or_swap_info(si, offset);
	count = swap_count(si->swap_map[offset]);
	unlock_cluster_or_swap_info(si, ci);
	return count;
}

/*
 * How many references to @entry are currently swapped out?
 * This considers COUNT_CONTINUED so it returns exact answer.
 */
int swp_swapcount(swp_entry_t entry)
{
	int count, tmp_count, n;
	struct swap_info_struct *si;
	struct swap_cluster_info *ci;
	struct page *page;
	pgoff_t offset;
	unsigned char *map;

	si = _swap_info_get(entry);
	if (!si)
		return 0;

	offset = swp_offset(entry);

	ci = lock_cluster_or_swap_info(si, offset);

	count = swap_count(si->swap_map[offset]);
	if (!(count & COUNT_CONTINUED))
		goto out;

	count &= ~COUNT_CONTINUED;
	n = SWAP_MAP_MAX + 1;

	page = vmalloc_to_page(si->swap_map + offset);
	offset &= ~PAGE_MASK;
	VM_BUG_ON(page_private(page) != SWP_CONTINUED);

	do {
		page = list_next_entry(page, lru);
		map = kmap_local_page(page);
		tmp_count = map[offset];
		kunmap_local(map);

		count += (tmp_count & ~COUNT_CONTINUED) * n;
		n *= (SWAP_CONT_MAX + 1);
	} while (tmp_count & COUNT_CONTINUED);
out:
	unlock_cluster_or_swap_info(si, ci);
	return count;
}

static bool swap_page_trans_huge_swapped(struct swap_info_struct *si,
					 swp_entry_t entry, int order)
{
	struct swap_cluster_info *ci;
	unsigned char *map = si->swap_map;
	unsigned int nr_pages = 1 << order;
	unsigned long roffset = swp_offset(entry);
	unsigned long offset = round_down(roffset, nr_pages);
	int i;
	bool ret = false;

	ci = lock_cluster_or_swap_info(si, offset);
	if (!ci || nr_pages == 1) {
		if (swap_count(map[roffset]))
			ret = true;
		goto unlock_out;
	}
	for (i = 0; i < nr_pages; i++) {
		if (swap_count(map[offset + i])) {
			ret = true;
			break;
		}
	}
unlock_out:
	unlock_cluster_or_swap_info(si, ci);
	return ret;
}

static bool folio_swapped(struct folio *folio)
{
	swp_entry_t entry = folio->swap;
	struct swap_info_struct *si = _swap_info_get(entry);

	if (!si)
		return false;

	if (!IS_ENABLED(CONFIG_THP_SWAP) || likely(!folio_test_large(folio)))
		return swap_swapcount(si, entry) != 0;

	return swap_page_trans_huge_swapped(si, entry, folio_order(folio));
}

static bool folio_swapcache_freeable(struct folio *folio)
{
	VM_BUG_ON_FOLIO(!folio_test_locked(folio), folio);

	if (!folio_test_swapcache(folio))
		return false;
	if (folio_test_writeback(folio))
		return false;

	/*
	 * Once hibernation has begun to create its image of memory,
	 * there's a danger that one of the calls to folio_free_swap()
	 * - most probably a call from __try_to_reclaim_swap() while
	 * hibernation is allocating its own swap pages for the image,
	 * but conceivably even a call from memory reclaim - will free
	 * the swap from a folio which has already been recorded in the
	 * image as a clean swapcache folio, and then reuse its swap for
	 * another page of the image.  On waking from hibernation, the
	 * original folio might be freed under memory pressure, then
	 * later read back in from swap, now with the wrong data.
	 *
	 * Hibernation suspends storage while it is writing the image
	 * to disk so check that here.
	 */
	if (pm_suspended_storage())
		return false;

	return true;
}

/**
 * folio_free_swap() - Free the swap space used for this folio.
 * @folio: The folio to remove.
 *
 * If swap is getting full, or if there are no more mappings of this folio,
 * then call folio_free_swap to free its swap space.
 *
 * Return: true if we were able to release the swap space.
 */
bool folio_free_swap(struct folio *folio)
{
	if (!folio_swapcache_freeable(folio))
		return false;
	if (folio_swapped(folio))
		return false;

	delete_from_swap_cache(folio);
	folio_set_dirty(folio);
	return true;
}

/**
 * free_swap_and_cache_nr() - Release reference on range of swap entries and
 *                            reclaim their cache if no more references remain.
 * @entry: First entry of range.
 * @nr: Number of entries in range.
 *
 * For each swap entry in the contiguous range, release a reference. If any swap
 * entries become free, try to reclaim their underlying folios, if present. The
 * offset range is defined by [entry.offset, entry.offset + nr).
 */
void free_swap_and_cache_nr(swp_entry_t entry, int nr)
{
	const unsigned long start_offset = swp_offset(entry);
	const unsigned long end_offset = start_offset + nr;
	struct swap_info_struct *si;
	bool any_only_cache = false;
	unsigned long offset;

	if (non_swap_entry(entry))
		return;

	si = get_swap_device(entry);
	if (!si)
		return;

	if (WARN_ON(end_offset > si->max))
		goto out;

	/*
	 * First free all entries in the range.
	 */
	any_only_cache = __swap_entries_free(si, entry, nr);

	/*
	 * Short-circuit the below loop if none of the entries had their
	 * reference drop to zero.
	 */
	if (!any_only_cache)
		goto out;

	/*
	 * Now go back over the range trying to reclaim the swap cache. This is
	 * more efficient for large folios because we will only try to reclaim
	 * the swap once per folio in the common case. If we do
	 * __swap_entry_free() and __try_to_reclaim_swap() in the same loop, the
	 * latter will get a reference and lock the folio for every individual
	 * page but will only succeed once the swap slot for every subpage is
	 * zero.
	 */
	for (offset = start_offset; offset < end_offset; offset += nr) {
		nr = 1;
		if (READ_ONCE(si->swap_map[offset]) == SWAP_HAS_CACHE) {
			/*
			 * Folios are always naturally aligned in swap so
			 * advance forward to the next boundary. Zero means no
			 * folio was found for the swap entry, so advance by 1
			 * in this case. Negative value means folio was found
			 * but could not be reclaimed. Here we can still advance
			 * to the next boundary.
			 */
			nr = __try_to_reclaim_swap(si, offset,
						   TTRS_UNMAPPED | TTRS_FULL);
			if (nr == 0)
				nr = 1;
			else if (nr < 0)
				nr = -nr;
			nr = ALIGN(offset + 1, nr) - offset;
		}
	}

out:
	put_swap_device(si);
}

#ifdef CONFIG_HIBERNATION

swp_entry_t get_swap_page_of_type(int type)
{
	struct swap_info_struct *si = swap_type_to_swap_info(type);
	swp_entry_t entry = {0};

	if (!si)
		goto fail;

	/* This is called for allocating swap entry, not cache */
	spin_lock(&si->lock);
	if ((si->flags & SWP_WRITEOK) && scan_swap_map_slots(si, 1, 1, &entry, 0))
		atomic_long_dec(&nr_swap_pages);
	spin_unlock(&si->lock);
fail:
	return entry;
}

/*
 * Find the swap type that corresponds to given device (if any).
 *
 * @offset - number of the PAGE_SIZE-sized block of the device, starting
 * from 0, in which the swap header is expected to be located.
 *
 * This is needed for the suspend to disk (aka swsusp).
 */
int swap_type_of(dev_t device, sector_t offset)
{
	int type;

	if (!device)
		return -1;

	spin_lock(&swap_lock);
	for (type = 0; type < nr_swapfiles; type++) {
		struct swap_info_struct *sis = swap_info[type];

		if (!(sis->flags & SWP_WRITEOK))
			continue;

		if (device == sis->bdev->bd_dev) {
			struct swap_extent *se = first_se(sis);

			if (se->start_block == offset) {
				spin_unlock(&swap_lock);
				return type;
			}
		}
	}
	spin_unlock(&swap_lock);
	return -ENODEV;
}

int find_first_swap(dev_t *device)
{
	int type;

	spin_lock(&swap_lock);
	for (type = 0; type < nr_swapfiles; type++) {
		struct swap_info_struct *sis = swap_info[type];

		if (!(sis->flags & SWP_WRITEOK))
			continue;
		*device = sis->bdev->bd_dev;
		spin_unlock(&swap_lock);
		return type;
	}
	spin_unlock(&swap_lock);
	return -ENODEV;
}

/*
 * Get the (PAGE_SIZE) block corresponding to given offset on the swapdev
 * corresponding to given index in swap_info (swap type).
 */
sector_t swapdev_block(int type, pgoff_t offset)
{
	struct swap_info_struct *si = swap_type_to_swap_info(type);
	struct swap_extent *se;

	if (!si || !(si->flags & SWP_WRITEOK))
		return 0;
	se = offset_to_swap_extent(si, offset);
	return se->start_block + (offset - se->start_page);
}

/*
 * Return either the total number of swap pages of given type, or the number
 * of free pages of that type (depending on @free)
 *
 * This is needed for software suspend
 */
unsigned int count_swap_pages(int type, int free)
{
	unsigned int n = 0;

	spin_lock(&swap_lock);
	if ((unsigned int)type < nr_swapfiles) {
		struct swap_info_struct *sis = swap_info[type];

		spin_lock(&sis->lock);
		if (sis->flags & SWP_WRITEOK) {
			n = sis->pages;
			if (free)
				n -= sis->inuse_pages;
		}
		spin_unlock(&sis->lock);
	}
	spin_unlock(&swap_lock);
	return n;
}
#endif /* CONFIG_HIBERNATION */

static inline int pte_same_as_swp(pte_t pte, pte_t swp_pte)
{
	return pte_same(pte_swp_clear_flags(pte), swp_pte);
}

/*
 * No need to decide whether this PTE shares the swap entry with others,
 * just let do_wp_page work it out if a write is requested later - to
 * force COW, vm_page_prot omits write permission from any private vma.
 */
static int unuse_pte(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, swp_entry_t entry, struct folio *folio)
{
	struct page *page;
	struct folio *swapcache;
	spinlock_t *ptl;
	pte_t *pte, new_pte, old_pte;
	bool hwpoisoned = false;
	int ret = 1;

	swapcache = folio;
	folio = ksm_might_need_to_copy(folio, vma, addr);
	if (unlikely(!folio))
		return -ENOMEM;
	else if (unlikely(folio == ERR_PTR(-EHWPOISON))) {
		hwpoisoned = true;
		folio = swapcache;
	}

	page = folio_file_page(folio, swp_offset(entry));
	if (PageHWPoison(page))
		hwpoisoned = true;

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (unlikely(!pte || !pte_same_as_swp(ptep_get(pte),
						swp_entry_to_pte(entry)))) {
		ret = 0;
		goto out;
	}

	old_pte = ptep_get(pte);

	if (unlikely(hwpoisoned || !folio_test_uptodate(folio))) {
		swp_entry_t swp_entry;

		dec_mm_counter(vma->vm_mm, MM_SWAPENTS);
		if (hwpoisoned) {
			swp_entry = make_hwpoison_entry(page);
		} else {
			swp_entry = make_poisoned_swp_entry();
		}
		new_pte = swp_entry_to_pte(swp_entry);
		ret = 0;
		goto setpte;
	}

	/*
	 * Some architectures may have to restore extra metadata to the page
	 * when reading from swap. This metadata may be indexed by swap entry
	 * so this must be called before swap_free().
	 */
	arch_swap_restore(folio_swap(entry, folio), folio);

	dec_mm_counter(vma->vm_mm, MM_SWAPENTS);
	inc_mm_counter(vma->vm_mm, MM_ANONPAGES);
	folio_get(folio);
	if (folio == swapcache) {
		rmap_t rmap_flags = RMAP_NONE;

		/*
		 * See do_swap_page(): writeback would be problematic.
		 * However, we do a folio_wait_writeback() just before this
		 * call and have the folio locked.
		 */
		VM_BUG_ON_FOLIO(folio_test_writeback(folio), folio);
		if (pte_swp_exclusive(old_pte))
			rmap_flags |= RMAP_EXCLUSIVE;
		/*
		 * We currently only expect small !anon folios, which are either
		 * fully exclusive or fully shared. If we ever get large folios
		 * here, we have to be careful.
		 */
		if (!folio_test_anon(folio)) {
			VM_WARN_ON_ONCE(folio_test_large(folio));
			VM_WARN_ON_FOLIO(!folio_test_locked(folio), folio);
			folio_add_new_anon_rmap(folio, vma, addr, rmap_flags);
		} else {
			folio_add_anon_rmap_pte(folio, page, vma, addr, rmap_flags);
		}
	} else { /* ksm created a completely new copy */
		folio_add_new_anon_rmap(folio, vma, addr, RMAP_EXCLUSIVE);
		folio_add_lru_vma(folio, vma);
	}
	new_pte = pte_mkold(mk_pte(page, vma->vm_page_prot));
	if (pte_swp_soft_dirty(old_pte))
		new_pte = pte_mksoft_dirty(new_pte);
	if (pte_swp_uffd_wp(old_pte))
		new_pte = pte_mkuffd_wp(new_pte);
setpte:
	set_pte_at(vma->vm_mm, addr, pte, new_pte);
	swap_free(entry);
out:
	if (pte)
		pte_unmap_unlock(pte, ptl);
	if (folio != swapcache) {
		folio_unlock(folio);
		folio_put(folio);
	}
	return ret;
}

static int unuse_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
			unsigned long addr, unsigned long end,
			unsigned int type)
{
	pte_t *pte = NULL;
	struct swap_info_struct *si;

	si = swap_info[type];
	do {
		struct folio *folio;
		unsigned long offset;
		unsigned char swp_count;
		swp_entry_t entry;
		int ret;
		pte_t ptent;

		if (!pte++) {
			pte = pte_offset_map(pmd, addr);
			if (!pte)
				break;
		}

		ptent = ptep_get_lockless(pte);

		if (!is_swap_pte(ptent))
			continue;

		entry = pte_to_swp_entry(ptent);
		if (swp_type(entry) != type)
			continue;

		offset = swp_offset(entry);
		pte_unmap(pte);
		pte = NULL;

		folio = swap_cache_get_folio(entry, vma, addr);
		if (!folio) {
			struct vm_fault vmf = {
				.vma = vma,
				.address = addr,
				.real_address = addr,
				.pmd = pmd,
			};

			folio = swapin_readahead(entry, GFP_HIGHUSER_MOVABLE,
						&vmf);
		}
		if (!folio) {
			swp_count = READ_ONCE(si->swap_map[offset]);
			if (swp_count == 0 || swp_count == SWAP_MAP_BAD)
				continue;
			return -ENOMEM;
		}

		folio_lock(folio);
		folio_wait_writeback(folio);
		ret = unuse_pte(vma, pmd, addr, entry, folio);
		if (ret < 0) {
			folio_unlock(folio);
			folio_put(folio);
			return ret;
		}

		folio_free_swap(folio);
		folio_unlock(folio);
		folio_put(folio);
	} while (addr += PAGE_SIZE, addr != end);

	if (pte)
		pte_unmap(pte);
	return 0;
}

static inline int unuse_pmd_range(struct vm_area_struct *vma, pud_t *pud,
				unsigned long addr, unsigned long end,
				unsigned int type)
{
	pmd_t *pmd;
	unsigned long next;
	int ret;

	pmd = pmd_offset(pud, addr);
	do {
		cond_resched();
		next = pmd_addr_end(addr, end);
		ret = unuse_pte_range(vma, pmd, addr, next, type);
		if (ret)
			return ret;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int unuse_pud_range(struct vm_area_struct *vma, p4d_t *p4d,
				unsigned long addr, unsigned long end,
				unsigned int type)
{
	pud_t *pud;
	unsigned long next;
	int ret;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		ret = unuse_pmd_range(vma, pud, addr, next, type);
		if (ret)
			return ret;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static inline int unuse_p4d_range(struct vm_area_struct *vma, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				unsigned int type)
{
	p4d_t *p4d;
	unsigned long next;
	int ret;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		ret = unuse_pud_range(vma, p4d, addr, next, type);
		if (ret)
			return ret;
	} while (p4d++, addr = next, addr != end);
	return 0;
}

static int unuse_vma(struct vm_area_struct *vma, unsigned int type)
{
	pgd_t *pgd;
	unsigned long addr, end, next;
	int ret;

	addr = vma->vm_start;
	end = vma->vm_end;

	pgd = pgd_offset(vma->vm_mm, addr);
	do {
		next = pgd_addr_end(addr, end);
		if (pgd_none_or_clear_bad(pgd))
			continue;
		ret = unuse_p4d_range(vma, pgd, addr, next, type);
		if (ret)
			return ret;
	} while (pgd++, addr = next, addr != end);
	return 0;
}

static int unuse_mm(struct mm_struct *mm, unsigned int type)
{
	struct vm_area_struct *vma;
	int ret = 0;
	VMA_ITERATOR(vmi, mm, 0);

	mmap_read_lock(mm);
	for_each_vma(vmi, vma) {
		if (vma->anon_vma && !is_vm_hugetlb_page(vma)) {
			ret = unuse_vma(vma, type);
			if (ret)
				break;
		}

		cond_resched();
	}
	mmap_read_unlock(mm);
	return ret;
}

/*
 * Scan swap_map from current position to next entry still in use.
 * Return 0 if there are no inuse entries after prev till end of
 * the map.
 */
static unsigned int find_next_to_unuse(struct swap_info_struct *si,
					unsigned int prev)
{
	unsigned int i;
	unsigned char count;

	/*
	 * No need for swap_lock here: we're just looking
	 * for whether an entry is in use, not modifying it; false
	 * hits are okay, and sys_swapoff() has already prevented new
	 * allocations from this area (while holding swap_lock).
	 */
	for (i = prev + 1; i < si->max; i++) {
		count = READ_ONCE(si->swap_map[i]);
		if (count && swap_count(count) != SWAP_MAP_BAD)
			break;
		if ((i % LATENCY_LIMIT) == 0)
			cond_resched();
	}

	if (i == si->max)
		i = 0;

	return i;
}

static int try_to_unuse(unsigned int type)
{
	struct mm_struct *prev_mm;
	struct mm_struct *mm;
	struct list_head *p;
	int retval = 0;
	struct swap_info_struct *si = swap_info[type];
	struct folio *folio;
	swp_entry_t entry;
	unsigned int i;

	if (!READ_ONCE(si->inuse_pages))
		goto success;

retry:
	retval = shmem_unuse(type);
	if (retval)
		return retval;

	prev_mm = &init_mm;
	mmget(prev_mm);

	spin_lock(&mmlist_lock);
	p = &init_mm.mmlist;
	while (READ_ONCE(si->inuse_pages) &&
	       !signal_pending(current) &&
	       (p = p->next) != &init_mm.mmlist) {

		mm = list_entry(p, struct mm_struct, mmlist);
		if (!mmget_not_zero(mm))
			continue;
		spin_unlock(&mmlist_lock);
		mmput(prev_mm);
		prev_mm = mm;
		retval = unuse_mm(mm, type);
		if (retval) {
			mmput(prev_mm);
			return retval;
		}

		/*
		 * Make sure that we aren't completely killing
		 * interactive performance.
		 */
		cond_resched();
		spin_lock(&mmlist_lock);
	}
	spin_unlock(&mmlist_lock);

	mmput(prev_mm);

	i = 0;
	while (READ_ONCE(si->inuse_pages) &&
	       !signal_pending(current) &&
	       (i = find_next_to_unuse(si, i)) != 0) {

		entry = swp_entry(type, i);
		folio = filemap_get_folio(swap_address_space(entry), swap_cache_index(entry));
		if (IS_ERR(folio))
			continue;

		/*
		 * It is conceivable that a racing task removed this folio from
		 * swap cache just before we acquired the page lock. The folio
		 * might even be back in swap cache on another swap area. But
		 * that is okay, folio_free_swap() only removes stale folios.
		 */
		folio_lock(folio);
		folio_wait_writeback(folio);
		folio_free_swap(folio);
		folio_unlock(folio);
		folio_put(folio);
	}

	/*
	 * Lets check again to see if there are still swap entries in the map.
	 * If yes, we would need to do retry the unuse logic again.
	 * Under global memory pressure, swap entries can be reinserted back
	 * into process space after the mmlist loop above passes over them.
	 *
	 * Limit the number of retries? No: when mmget_not_zero()
	 * above fails, that mm is likely to be freeing swap from
	 * exit_mmap(), which proceeds at its own independent pace;
	 * and even shmem_writepage() could have been preempted after
	 * folio_alloc_swap(), temporarily hiding that swap.  It's easy
	 * and robust (though cpu-intensive) just to keep retrying.
	 */
	if (READ_ONCE(si->inuse_pages)) {
		if (!signal_pending(current))
			goto retry;
		return -EINTR;
	}

success:
	/*
	 * Make sure that further cleanups after try_to_unuse() returns happen
	 * after swap_range_free() reduces si->inuse_pages to 0.
	 */
	smp_mb();
	return 0;
}

/*
 * After a successful try_to_unuse, if no swap is now in use, we know
 * we can empty the mmlist.  swap_lock must be held on entry and exit.
 * Note that mmlist_lock nests inside swap_lock, and an mm must be
 * added to the mmlist just after page_duplicate - before would be racy.
 */
static void drain_mmlist(void)
{
	struct list_head *p, *next;
	unsigned int type;

	for (type = 0; type < nr_swapfiles; type++)
		if (swap_info[type]->inuse_pages)
			return;
	spin_lock(&mmlist_lock);
	list_for_each_safe(p, next, &init_mm.mmlist)
		list_del_init(p);
	spin_unlock(&mmlist_lock);
}

/*
 * Free all of a swapdev's extent information
 */
static void destroy_swap_extents(struct swap_info_struct *sis)
{
	while (!RB_EMPTY_ROOT(&sis->swap_extent_root)) {
		struct rb_node *rb = sis->swap_extent_root.rb_node;
		struct swap_extent *se = rb_entry(rb, struct swap_extent, rb_node);

		rb_erase(rb, &sis->swap_extent_root);
		kfree(se);
	}

	if (sis->flags & SWP_ACTIVATED) {
		struct file *swap_file = sis->swap_file;
		struct address_space *mapping = swap_file->f_mapping;

		sis->flags &= ~SWP_ACTIVATED;
		if (mapping->a_ops->swap_deactivate)
			mapping->a_ops->swap_deactivate(swap_file);
	}
}

/*
 * Add a block range (and the corresponding page range) into this swapdev's
 * extent tree.
 *
 * This function rather assumes that it is called in ascending page order.
 */
int
add_swap_extent(struct swap_info_struct *sis, unsigned long start_page,
		unsigned long nr_pages, sector_t start_block)
{
	struct rb_node **link = &sis->swap_extent_root.rb_node, *parent = NULL;
	struct swap_extent *se;
	struct swap_extent *new_se;

	/*
	 * place the new node at the right most since the
	 * function is called in ascending page order.
	 */
	while (*link) {
		parent = *link;
		link = &parent->rb_right;
	}

	if (parent) {
		se = rb_entry(parent, struct swap_extent, rb_node);
		BUG_ON(se->start_page + se->nr_pages != start_page);
		if (se->start_block + se->nr_pages == start_block) {
			/* Merge it */
			se->nr_pages += nr_pages;
			return 0;
		}
	}

	/* No merge, insert a new extent. */
	new_se = kmalloc(sizeof(*se), GFP_KERNEL);
	if (new_se == NULL)
		return -ENOMEM;
	new_se->start_page = start_page;
	new_se->nr_pages = nr_pages;
	new_se->start_block = start_block;

	rb_link_node(&new_se->rb_node, parent, link);
	rb_insert_color(&new_se->rb_node, &sis->swap_extent_root);
	return 1;
}
EXPORT_SYMBOL_GPL(add_swap_extent);

/*
 * A `swap extent' is a simple thing which maps a contiguous range of pages
 * onto a contiguous range of disk blocks.  A rbtree of swap extents is
 * built at swapon time and is then used at swap_writepage/swap_read_folio
 * time for locating where on disk a page belongs.
 *
 * If the swapfile is an S_ISBLK block device, a single extent is installed.
 * This is done so that the main operating code can treat S_ISBLK and S_ISREG
 * swap files identically.
 *
 * Whether the swapdev is an S_ISREG file or an S_ISBLK blockdev, the swap
 * extent rbtree operates in PAGE_SIZE disk blocks.  Both S_ISREG and S_ISBLK
 * swapfiles are handled *identically* after swapon time.
 *
 * For S_ISREG swapfiles, setup_swap_extents() will walk all the file's blocks
 * and will parse them into a rbtree, in PAGE_SIZE chunks.  If some stray
 * blocks are found which do not fall within the PAGE_SIZE alignment
 * requirements, they are simply tossed out - we will never use those blocks
 * for swapping.
 *
 * For all swap devices we set S_SWAPFILE across the life of the swapon.  This
 * prevents users from writing to the swap device, which will corrupt memory.
 *
 * The amount of disk space which a single swap extent represents varies.
 * Typically it is in the 1-4 megabyte range.  So we can have hundreds of
 * extents in the rbtree. - akpm.
 */
static int setup_swap_extents(struct swap_info_struct *sis, sector_t *span)
{
	struct file *swap_file = sis->swap_file;
	struct address_space *mapping = swap_file->f_mapping;
	struct inode *inode = mapping->host;
	int ret;

	if (S_ISBLK(inode->i_mode)) {
		ret = add_swap_extent(sis, 0, sis->max, 0);
		*span = sis->pages;
		return ret;
	}

	if (mapping->a_ops->swap_activate) {
		ret = mapping->a_ops->swap_activate(sis, swap_file, span);
		if (ret < 0)
			return ret;
		sis->flags |= SWP_ACTIVATED;
		if ((sis->flags & SWP_FS_OPS) &&
		    sio_pool_init() != 0) {
			destroy_swap_extents(sis);
			return -ENOMEM;
		}
		return ret;
	}

	return generic_swapfile_activate(sis, swap_file, span);
}

static int swap_node(struct swap_info_struct *si)
{
	struct block_device *bdev;

	if (si->bdev)
		bdev = si->bdev;
	else
		bdev = si->swap_file->f_inode->i_sb->s_bdev;

	return bdev ? bdev->bd_disk->node_id : NUMA_NO_NODE;
}

static void setup_swap_info(struct swap_info_struct *si, int prio,
			    unsigned char *swap_map,
			    struct swap_cluster_info *cluster_info,
			    unsigned long *zeromap)
{
	int i;

	if (prio >= 0)
		si->prio = prio;
	else
		si->prio = --least_priority;
	/*
	 * the plist prio is negated because plist ordering is
	 * low-to-high, while swap ordering is high-to-low
	 */
	si->list.prio = -si->prio;
	for_each_node(i) {
		if (si->prio >= 0)
			si->avail_lists[i].prio = -si->prio;
		else {
			if (swap_node(si) == i)
				si->avail_lists[i].prio = 1;
			else
				si->avail_lists[i].prio = -si->prio;
		}
	}
	si->swap_map = swap_map;
	si->cluster_info = cluster_info;
	si->zeromap = zeromap;
}

static void _enable_swap_info(struct swap_info_struct *si)
{
	si->flags |= SWP_WRITEOK;
	atomic_long_add(si->pages, &nr_swap_pages);
	total_swap_pages += si->pages;

	assert_spin_locked(&swap_lock);
	/*
	 * both lists are plists, and thus priority ordered.
	 * swap_active_head needs to be priority ordered for swapoff(),
	 * which on removal of any swap_info_struct with an auto-assigned
	 * (i.e. negative) priority increments the auto-assigned priority
	 * of any lower-priority swap_info_structs.
	 * swap_avail_head needs to be priority ordered for folio_alloc_swap(),
	 * which allocates swap pages from the highest available priority
	 * swap_info_struct.
	 */
	plist_add(&si->list, &swap_active_head);

	/* add to available list iff swap device is not full */
	if (si->highest_bit)
		add_to_avail_list(si);
}

static void enable_swap_info(struct swap_info_struct *si, int prio,
				unsigned char *swap_map,
				struct swap_cluster_info *cluster_info,
				unsigned long *zeromap)
{
	spin_lock(&swap_lock);
	spin_lock(&si->lock);
	setup_swap_info(si, prio, swap_map, cluster_info, zeromap);
	spin_unlock(&si->lock);
	spin_unlock(&swap_lock);
	/*
	 * Finished initializing swap device, now it's safe to reference it.
	 */
	percpu_ref_resurrect(&si->users);
	spin_lock(&swap_lock);
	spin_lock(&si->lock);
	_enable_swap_info(si);
	spin_unlock(&si->lock);
	spin_unlock(&swap_lock);
}

static void reinsert_swap_info(struct swap_info_struct *si)
{
	spin_lock(&swap_lock);
	spin_lock(&si->lock);
	setup_swap_info(si, si->prio, si->swap_map, si->cluster_info, si->zeromap);
	_enable_swap_info(si);
	spin_unlock(&si->lock);
	spin_unlock(&swap_lock);
}

static bool __has_usable_swap(void)
{
	return !plist_head_empty(&swap_active_head);
}

bool has_usable_swap(void)
{
	bool ret;

	spin_lock(&swap_lock);
	ret = __has_usable_swap();
	spin_unlock(&swap_lock);
	return ret;
}

SYSCALL_DEFINE1(swapoff, const char __user *, specialfile)
{
	struct swap_info_struct *p = NULL;
	unsigned char *swap_map;
	unsigned long *zeromap;
	struct swap_cluster_info *cluster_info;
	struct file *swap_file, *victim;
	struct address_space *mapping;
	struct inode *inode;
	struct filename *pathname;
	int err, found = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	BUG_ON(!current->mm);

	pathname = getname(specialfile);
	if (IS_ERR(pathname))
		return PTR_ERR(pathname);

	victim = file_open_name(pathname, O_RDWR|O_LARGEFILE, 0);
	err = PTR_ERR(victim);
	if (IS_ERR(victim))
		goto out;

	mapping = victim->f_mapping;
	spin_lock(&swap_lock);
	plist_for_each_entry(p, &swap_active_head, list) {
		if (p->flags & SWP_WRITEOK) {
			if (p->swap_file->f_mapping == mapping) {
				found = 1;
				break;
			}
		}
	}
	if (!found) {
		err = -EINVAL;
		spin_unlock(&swap_lock);
		goto out_dput;
	}
	if (!security_vm_enough_memory_mm(current->mm, p->pages))
		vm_unacct_memory(p->pages);
	else {
		err = -ENOMEM;
		spin_unlock(&swap_lock);
		goto out_dput;
	}
	spin_lock(&p->lock);
	del_from_avail_list(p);
	if (p->prio < 0) {
		struct swap_info_struct *si = p;
		int nid;

		plist_for_each_entry_continue(si, &swap_active_head, list) {
			si->prio++;
			si->list.prio--;
			for_each_node(nid) {
				if (si->avail_lists[nid].prio != 1)
					si->avail_lists[nid].prio--;
			}
		}
		least_priority++;
	}
	plist_del(&p->list, &swap_active_head);
	atomic_long_sub(p->pages, &nr_swap_pages);
	total_swap_pages -= p->pages;
	p->flags &= ~SWP_WRITEOK;
	spin_unlock(&p->lock);
	spin_unlock(&swap_lock);

	disable_swap_slots_cache_lock();

	set_current_oom_origin();
	err = try_to_unuse(p->type);
	clear_current_oom_origin();

	if (err) {
		/* re-insert swap space back into swap_list */
		reinsert_swap_info(p);
		reenable_swap_slots_cache_unlock();
		goto out_dput;
	}

	reenable_swap_slots_cache_unlock();

	/*
	 * Wait for swap operations protected by get/put_swap_device()
	 * to complete.  Because of synchronize_rcu() here, all swap
	 * operations protected by RCU reader side lock (including any
	 * spinlock) will be waited too.  This makes it easy to
	 * prevent folio_test_swapcache() and the following swap cache
	 * operations from racing with swapoff.
	 */
	percpu_ref_kill(&p->users);
	synchronize_rcu();
	wait_for_completion(&p->comp);

	flush_work(&p->discard_work);
	flush_work(&p->reclaim_work);

	destroy_swap_extents(p);
	if (p->flags & SWP_CONTINUED)
		free_swap_count_continuations(p);

	if (!p->bdev || !bdev_nonrot(p->bdev))
		atomic_dec(&nr_rotate_swap);

	mutex_lock(&swapon_mutex);
	spin_lock(&swap_lock);
	spin_lock(&p->lock);
	drain_mmlist();

	/* wait for anyone still in scan_swap_map_slots */
	p->highest_bit = 0;		/* cuts scans short */
	while (p->flags >= SWP_SCANNING) {
		spin_unlock(&p->lock);
		spin_unlock(&swap_lock);
		schedule_timeout_uninterruptible(1);
		spin_lock(&swap_lock);
		spin_lock(&p->lock);
	}

	swap_file = p->swap_file;
	p->swap_file = NULL;
	p->max = 0;
	swap_map = p->swap_map;
	p->swap_map = NULL;
	zeromap = p->zeromap;
	p->zeromap = NULL;
	cluster_info = p->cluster_info;
	p->cluster_info = NULL;
	spin_unlock(&p->lock);
	spin_unlock(&swap_lock);
	arch_swap_invalidate_area(p->type);
	zswap_swapoff(p->type);
	mutex_unlock(&swapon_mutex);
	free_percpu(p->percpu_cluster);
	p->percpu_cluster = NULL;
	free_percpu(p->cluster_next_cpu);
	p->cluster_next_cpu = NULL;
	vfree(swap_map);
	kvfree(zeromap);
	kvfree(cluster_info);
	/* Destroy swap account information */
	swap_cgroup_swapoff(p->type);
	exit_swap_address_space(p->type);

	inode = mapping->host;

	inode_lock(inode);
	inode->i_flags &= ~S_SWAPFILE;
	inode_unlock(inode);
	filp_close(swap_file, NULL);

	/*
	 * Clear the SWP_USED flag after all resources are freed so that swapon
	 * can reuse this swap_info in alloc_swap_info() safely.  It is ok to
	 * not hold p->lock after we cleared its SWP_WRITEOK.
	 */
	spin_lock(&swap_lock);
	p->flags = 0;
	spin_unlock(&swap_lock);

	err = 0;
	atomic_inc(&proc_poll_event);
	wake_up_interruptible(&proc_poll_wait);

out_dput:
	filp_close(victim, NULL);
out:
	putname(pathname);
	return err;
}

#ifdef CONFIG_PROC_FS
static __poll_t swaps_poll(struct file *file, poll_table *wait)
{
	struct seq_file *seq = file->private_data;

	poll_wait(file, &proc_poll_wait, wait);

	if (seq->poll_event != atomic_read(&proc_poll_event)) {
		seq->poll_event = atomic_read(&proc_poll_event);
		return EPOLLIN | EPOLLRDNORM | EPOLLERR | EPOLLPRI;
	}

	return EPOLLIN | EPOLLRDNORM;
}

/* iterator */
static void *swap_start(struct seq_file *swap, loff_t *pos)
{
	struct swap_info_struct *si;
	int type;
	loff_t l = *pos;

	mutex_lock(&swapon_mutex);

	if (!l)
		return SEQ_START_TOKEN;

	for (type = 0; (si = swap_type_to_swap_info(type)); type++) {
		if (!(si->flags & SWP_USED) || !si->swap_map)
			continue;
		if (!--l)
			return si;
	}

	return NULL;
}

static void *swap_next(struct seq_file *swap, void *v, loff_t *pos)
{
	struct swap_info_struct *si = v;
	int type;

	if (v == SEQ_START_TOKEN)
		type = 0;
	else
		type = si->type + 1;

	++(*pos);
	for (; (si = swap_type_to_swap_info(type)); type++) {
		if (!(si->flags & SWP_USED) || !si->swap_map)
			continue;
		return si;
	}

	return NULL;
}

static void swap_stop(struct seq_file *swap, void *v)
{
	mutex_unlock(&swapon_mutex);
}

static int swap_show(struct seq_file *swap, void *v)
{
	struct swap_info_struct *si = v;
	struct file *file;
	int len;
	unsigned long bytes, inuse;

	if (si == SEQ_START_TOKEN) {
		seq_puts(swap, "Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n");
		return 0;
	}

	bytes = K(si->pages);
	inuse = K(READ_ONCE(si->inuse_pages));

	file = si->swap_file;
	len = seq_file_path(swap, file, " \t\n\\");
	seq_printf(swap, "%*s%s\t%lu\t%s%lu\t%s%d\n",
			len < 40 ? 40 - len : 1, " ",
			S_ISBLK(file_inode(file)->i_mode) ?
				"partition" : "file\t",
			bytes, bytes < 10000000 ? "\t" : "",
			inuse, inuse < 10000000 ? "\t" : "",
			si->prio);
	return 0;
}

static const struct seq_operations swaps_op = {
	.start =	swap_start,
	.next =		swap_next,
	.stop =		swap_stop,
	.show =		swap_show
};

static int swaps_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &swaps_op);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->poll_event = atomic_read(&proc_poll_event);
	return 0;
}

static const struct proc_ops swaps_proc_ops = {
	.proc_flags	= PROC_ENTRY_PERMANENT,
	.proc_open	= swaps_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
	.proc_poll	= swaps_poll,
};

static int __init procswaps_init(void)
{
	proc_create("swaps", 0, NULL, &swaps_proc_ops);
	return 0;
}
__initcall(procswaps_init);
#endif /* CONFIG_PROC_FS */

#ifdef MAX_SWAPFILES_CHECK
static int __init max_swapfiles_check(void)
{
	MAX_SWAPFILES_CHECK();
	return 0;
}
late_initcall(max_swapfiles_check);
#endif

static struct swap_info_struct *alloc_swap_info(void)
{
	struct swap_info_struct *p;
	struct swap_info_struct *defer = NULL;
	unsigned int type;
	int i;

	p = kvzalloc(struct_size(p, avail_lists, nr_node_ids), GFP_KERNEL);
	if (!p)
		return ERR_PTR(-ENOMEM);

	if (percpu_ref_init(&p->users, swap_users_ref_free,
			    PERCPU_REF_INIT_DEAD, GFP_KERNEL)) {
		kvfree(p);
		return ERR_PTR(-ENOMEM);
	}

	spin_lock(&swap_lock);
	for (type = 0; type < nr_swapfiles; type++) {
		if (!(swap_info[type]->flags & SWP_USED))
			break;
	}
	if (type >= MAX_SWAPFILES) {
		spin_unlock(&swap_lock);
		percpu_ref_exit(&p->users);
		kvfree(p);
		return ERR_PTR(-EPERM);
	}
	if (type >= nr_swapfiles) {
		p->type = type;
		/*
		 * Publish the swap_info_struct after initializing it.
		 * Note that kvzalloc() above zeroes all its fields.
		 */
		smp_store_release(&swap_info[type], p); /* rcu_assign_pointer() */
		nr_swapfiles++;
	} else {
		defer = p;
		p = swap_info[type];
		/*
		 * Do not memset this entry: a racing procfs swap_next()
		 * would be relying on p->type to remain valid.
		 */
	}
	p->swap_extent_root = RB_ROOT;
	plist_node_init(&p->list, 0);
	for_each_node(i)
		plist_node_init(&p->avail_lists[i], 0);
	p->flags = SWP_USED;
	spin_unlock(&swap_lock);
	if (defer) {
		percpu_ref_exit(&defer->users);
		kvfree(defer);
	}
	spin_lock_init(&p->lock);
	spin_lock_init(&p->cont_lock);
	init_completion(&p->comp);

	return p;
}

static int claim_swapfile(struct swap_info_struct *si, struct inode *inode)
{
	if (S_ISBLK(inode->i_mode)) {
		si->bdev = I_BDEV(inode);
		/*
		 * Zoned block devices contain zones that have a sequential
		 * write only restriction.  Hence zoned block devices are not
		 * suitable for swapping.  Disallow them here.
		 */
		if (bdev_is_zoned(si->bdev))
			return -EINVAL;
		si->flags |= SWP_BLKDEV;
	} else if (S_ISREG(inode->i_mode)) {
		si->bdev = inode->i_sb->s_bdev;
	}

	return 0;
}


/*
 * Find out how many pages are allowed for a single swap device. There
 * are two limiting factors:
 * 1) the number of bits for the swap offset in the swp_entry_t type, and
 * 2) the number of bits in the swap pte, as defined by the different
 * architectures.
 *
 * In order to find the largest possible bit mask, a swap entry with
 * swap type 0 and swap offset ~0UL is created, encoded to a swap pte,
 * decoded to a swp_entry_t again, and finally the swap offset is
 * extracted.
 *
 * This will mask all the bits from the initial ~0UL mask that can't
 * be encoded in either the swp_entry_t or the architecture definition
 * of a swap pte.
 */
unsigned long generic_max_swapfile_size(void)
{
	return swp_offset(pte_to_swp_entry(
			swp_entry_to_pte(swp_entry(0, ~0UL)))) + 1;
}

/* Can be overridden by an architecture for additional checks. */
__weak unsigned long arch_max_swapfile_size(void)
{
	return generic_max_swapfile_size();
}

static unsigned long read_swap_header(struct swap_info_struct *si,
					union swap_header *swap_header,
					struct inode *inode)
{
	int i;
	unsigned long maxpages;
	unsigned long swapfilepages;
	unsigned long last_page;

	if (memcmp("SWAPSPACE2", swap_header->magic.magic, 10)) {
		pr_err("Unable to find swap-space signature\n");
		return 0;
	}

	/* swap partition endianness hack... */
	if (swab32(swap_header->info.version) == 1) {
		swab32s(&swap_header->info.version);
		swab32s(&swap_header->info.last_page);
		swab32s(&swap_header->info.nr_badpages);
		if (swap_header->info.nr_badpages > MAX_SWAP_BADPAGES)
			return 0;
		for (i = 0; i < swap_header->info.nr_badpages; i++)
			swab32s(&swap_header->info.badpages[i]);
	}
	/* Check the swap header's sub-version */
	if (swap_header->info.version != 1) {
		pr_warn("Unable to handle swap header version %d\n",
			swap_header->info.version);
		return 0;
	}

	si->lowest_bit  = 1;
	si->cluster_next = 1;
	si->cluster_nr = 0;

	maxpages = swapfile_maximum_size;
	last_page = swap_header->info.last_page;
	if (!last_page) {
		pr_warn("Empty swap-file\n");
		return 0;
	}
	if (last_page > maxpages) {
		pr_warn("Truncating oversized swap area, only using %luk out of %luk\n",
			K(maxpages), K(last_page));
	}
	if (maxpages > last_page) {
		maxpages = last_page + 1;
		/* p->max is an unsigned int: don't overflow it */
		if ((unsigned int)maxpages == 0)
			maxpages = UINT_MAX;
	}
	si->highest_bit = maxpages - 1;

	if (!maxpages)
		return 0;
	swapfilepages = i_size_read(inode) >> PAGE_SHIFT;
	if (swapfilepages && maxpages > swapfilepages) {
		pr_warn("Swap area shorter than signature indicates\n");
		return 0;
	}
	if (swap_header->info.nr_badpages && S_ISREG(inode->i_mode))
		return 0;
	if (swap_header->info.nr_badpages > MAX_SWAP_BADPAGES)
		return 0;

	return maxpages;
}

#define SWAP_CLUSTER_INFO_COLS						\
	DIV_ROUND_UP(L1_CACHE_BYTES, sizeof(struct swap_cluster_info))
#define SWAP_CLUSTER_SPACE_COLS						\
	DIV_ROUND_UP(SWAP_ADDRESS_SPACE_PAGES, SWAPFILE_CLUSTER)
#define SWAP_CLUSTER_COLS						\
	max_t(unsigned int, SWAP_CLUSTER_INFO_COLS, SWAP_CLUSTER_SPACE_COLS)

static int setup_swap_map_and_extents(struct swap_info_struct *si,
					union swap_header *swap_header,
					unsigned char *swap_map,
					unsigned long maxpages,
					sector_t *span)
{
	unsigned int nr_good_pages;
	unsigned long i;
	int nr_extents;

	nr_good_pages = maxpages - 1;	/* omit header page */

	for (i = 0; i < swap_header->info.nr_badpages; i++) {
		unsigned int page_nr = swap_header->info.badpages[i];
		if (page_nr == 0 || page_nr > swap_header->info.last_page)
			return -EINVAL;
		if (page_nr < maxpages) {
			swap_map[page_nr] = SWAP_MAP_BAD;
			nr_good_pages--;
		}
	}

	if (nr_good_pages) {
		swap_map[0] = SWAP_MAP_BAD;
		si->max = maxpages;
		si->pages = nr_good_pages;
		nr_extents = setup_swap_extents(si, span);
		if (nr_extents < 0)
			return nr_extents;
		nr_good_pages = si->pages;
	}
	if (!nr_good_pages) {
		pr_warn("Empty swap-file\n");
		return -EINVAL;
	}

	return nr_extents;
}

static struct swap_cluster_info *setup_clusters(struct swap_info_struct *si,
						union swap_header *swap_header,
						unsigned long maxpages)
{
	unsigned long nr_clusters = DIV_ROUND_UP(maxpages, SWAPFILE_CLUSTER);
	unsigned long col = si->cluster_next / SWAPFILE_CLUSTER % SWAP_CLUSTER_COLS;
	struct swap_cluster_info *cluster_info;
	unsigned long i, j, k, idx;
	int cpu, err = -ENOMEM;

	cluster_info = kvcalloc(nr_clusters, sizeof(*cluster_info), GFP_KERNEL);
	if (!cluster_info)
		goto err;

	for (i = 0; i < nr_clusters; i++)
		spin_lock_init(&cluster_info[i].lock);

	si->cluster_next_cpu = alloc_percpu(unsigned int);
	if (!si->cluster_next_cpu)
		goto err_free;

	/* Random start position to help with wear leveling */
	for_each_possible_cpu(cpu)
		per_cpu(*si->cluster_next_cpu, cpu) =
		get_random_u32_inclusive(1, si->highest_bit);

	si->percpu_cluster = alloc_percpu(struct percpu_cluster);
	if (!si->percpu_cluster)
		goto err_free;

	for_each_possible_cpu(cpu) {
		struct percpu_cluster *cluster;

		cluster = per_cpu_ptr(si->percpu_cluster, cpu);
		for (i = 0; i < SWAP_NR_ORDERS; i++)
			cluster->next[i] = SWAP_NEXT_INVALID;
	}

	/*
	 * Mark unusable pages as unavailable. The clusters aren't
	 * marked free yet, so no list operations are involved yet.
	 *
	 * See setup_swap_map_and_extents(): header page, bad pages,
	 * and the EOF part of the last cluster.
	 */
	inc_cluster_info_page(si, cluster_info, 0);
	for (i = 0; i < swap_header->info.nr_badpages; i++)
		inc_cluster_info_page(si, cluster_info,
				      swap_header->info.badpages[i]);
	for (i = maxpages; i < round_up(maxpages, SWAPFILE_CLUSTER); i++)
		inc_cluster_info_page(si, cluster_info, i);

	INIT_LIST_HEAD(&si->free_clusters);
	INIT_LIST_HEAD(&si->full_clusters);
	INIT_LIST_HEAD(&si->discard_clusters);

	for (i = 0; i < SWAP_NR_ORDERS; i++) {
		INIT_LIST_HEAD(&si->nonfull_clusters[i]);
		INIT_LIST_HEAD(&si->frag_clusters[i]);
		si->frag_cluster_nr[i] = 0;
	}

	/*
	 * Reduce false cache line sharing between cluster_info and
	 * sharing same address space.
	 */
	for (k = 0; k < SWAP_CLUSTER_COLS; k++) {
		j = (k + col) % SWAP_CLUSTER_COLS;
		for (i = 0; i < DIV_ROUND_UP(nr_clusters, SWAP_CLUSTER_COLS); i++) {
			struct swap_cluster_info *ci;
			idx = i * SWAP_CLUSTER_COLS + j;
			ci = cluster_info + idx;
			if (idx >= nr_clusters)
				continue;
			if (ci->count) {
				ci->flags = CLUSTER_FLAG_NONFULL;
				list_add_tail(&ci->list, &si->nonfull_clusters[0]);
				continue;
			}
			ci->flags = CLUSTER_FLAG_FREE;
			list_add_tail(&ci->list, &si->free_clusters);
		}
	}

	return cluster_info;

err_free:
	kvfree(cluster_info);
err:
	return ERR_PTR(err);
}

SYSCALL_DEFINE2(swapon, const char __user *, specialfile, int, swap_flags)
{
	struct swap_info_struct *si;
	struct filename *name;
	struct file *swap_file = NULL;
	struct address_space *mapping;
	struct dentry *dentry;
	int prio;
	int error;
	union swap_header *swap_header;
	int nr_extents;
	sector_t span;
	unsigned long maxpages;
	unsigned char *swap_map = NULL;
	unsigned long *zeromap = NULL;
	struct swap_cluster_info *cluster_info = NULL;
	struct folio *folio = NULL;
	struct inode *inode = NULL;
	bool inced_nr_rotate_swap = false;

	if (swap_flags & ~SWAP_FLAGS_VALID)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!swap_avail_heads)
		return -ENOMEM;

	si = alloc_swap_info();
	if (IS_ERR(si))
		return PTR_ERR(si);

	INIT_WORK(&si->discard_work, swap_discard_work);
	INIT_WORK(&si->reclaim_work, swap_reclaim_work);

	name = getname(specialfile);
	if (IS_ERR(name)) {
		error = PTR_ERR(name);
		name = NULL;
		goto bad_swap;
	}
	swap_file = file_open_name(name, O_RDWR | O_LARGEFILE | O_EXCL, 0);
	if (IS_ERR(swap_file)) {
		error = PTR_ERR(swap_file);
		swap_file = NULL;
		goto bad_swap;
	}

	si->swap_file = swap_file;
	mapping = swap_file->f_mapping;
	dentry = swap_file->f_path.dentry;
	inode = mapping->host;

	error = claim_swapfile(si, inode);
	if (unlikely(error))
		goto bad_swap;

	inode_lock(inode);
	if (d_unlinked(dentry) || cant_mount(dentry)) {
		error = -ENOENT;
		goto bad_swap_unlock_inode;
	}
	if (IS_SWAPFILE(inode)) {
		error = -EBUSY;
		goto bad_swap_unlock_inode;
	}

	/*
	 * Read the swap header.
	 */
	if (!mapping->a_ops->read_folio) {
		error = -EINVAL;
		goto bad_swap_unlock_inode;
	}
	folio = read_mapping_folio(mapping, 0, swap_file);
	if (IS_ERR(folio)) {
		error = PTR_ERR(folio);
		goto bad_swap_unlock_inode;
	}
	swap_header = kmap_local_folio(folio, 0);

	maxpages = read_swap_header(si, swap_header, inode);
	if (unlikely(!maxpages)) {
		error = -EINVAL;
		goto bad_swap_unlock_inode;
	}

	/* OK, set up the swap map and apply the bad block list */
	swap_map = vzalloc(maxpages);
	if (!swap_map) {
		error = -ENOMEM;
		goto bad_swap_unlock_inode;
	}

	error = swap_cgroup_swapon(si->type, maxpages);
	if (error)
		goto bad_swap_unlock_inode;

	nr_extents = setup_swap_map_and_extents(si, swap_header, swap_map,
						maxpages, &span);
	if (unlikely(nr_extents < 0)) {
		error = nr_extents;
		goto bad_swap_unlock_inode;
	}

	/*
	 * Use kvmalloc_array instead of bitmap_zalloc as the allocation order might
	 * be above MAX_PAGE_ORDER incase of a large swap file.
	 */
	zeromap = kvmalloc_array(BITS_TO_LONGS(maxpages), sizeof(long),
				    GFP_KERNEL | __GFP_ZERO);
	if (!zeromap) {
		error = -ENOMEM;
		goto bad_swap_unlock_inode;
	}

	if (si->bdev && bdev_stable_writes(si->bdev))
		si->flags |= SWP_STABLE_WRITES;

	if (si->bdev && bdev_synchronous(si->bdev))
		si->flags |= SWP_SYNCHRONOUS_IO;

	if (si->bdev && bdev_nonrot(si->bdev)) {
		si->flags |= SWP_SOLIDSTATE;

		cluster_info = setup_clusters(si, swap_header, maxpages);
		if (IS_ERR(cluster_info)) {
			error = PTR_ERR(cluster_info);
			cluster_info = NULL;
			goto bad_swap_unlock_inode;
		}
	} else {
		atomic_inc(&nr_rotate_swap);
		inced_nr_rotate_swap = true;
	}

	if ((swap_flags & SWAP_FLAG_DISCARD) &&
	    si->bdev && bdev_max_discard_sectors(si->bdev)) {
		/*
		 * When discard is enabled for swap with no particular
		 * policy flagged, we set all swap discard flags here in
		 * order to sustain backward compatibility with older
		 * swapon(8) releases.
		 */
		si->flags |= (SWP_DISCARDABLE | SWP_AREA_DISCARD |
			     SWP_PAGE_DISCARD);

		/*
		 * By flagging sys_swapon, a sysadmin can tell us to
		 * either do single-time area discards only, or to just
		 * perform discards for released swap page-clusters.
		 * Now it's time to adjust the p->flags accordingly.
		 */
		if (swap_flags & SWAP_FLAG_DISCARD_ONCE)
			si->flags &= ~SWP_PAGE_DISCARD;
		else if (swap_flags & SWAP_FLAG_DISCARD_PAGES)
			si->flags &= ~SWP_AREA_DISCARD;

		/* issue a swapon-time discard if it's still required */
		if (si->flags & SWP_AREA_DISCARD) {
			int err = discard_swap(si);
			if (unlikely(err))
				pr_err("swapon: discard_swap(%p): %d\n",
					si, err);
		}
	}

	error = init_swap_address_space(si->type, maxpages);
	if (error)
		goto bad_swap_unlock_inode;

	error = zswap_swapon(si->type, maxpages);
	if (error)
		goto free_swap_address_space;

	/*
	 * Flush any pending IO and dirty mappings before we start using this
	 * swap device.
	 */
	inode->i_flags |= S_SWAPFILE;
	error = inode_drain_writes(inode);
	if (error) {
		inode->i_flags &= ~S_SWAPFILE;
		goto free_swap_zswap;
	}

	mutex_lock(&swapon_mutex);
	prio = -1;
	if (swap_flags & SWAP_FLAG_PREFER)
		prio =
		  (swap_flags & SWAP_FLAG_PRIO_MASK) >> SWAP_FLAG_PRIO_SHIFT;
	enable_swap_info(si, prio, swap_map, cluster_info, zeromap);

	pr_info("Adding %uk swap on %s.  Priority:%d extents:%d across:%lluk %s%s%s%s\n",
		K(si->pages), name->name, si->prio, nr_extents,
		K((unsigned long long)span),
		(si->flags & SWP_SOLIDSTATE) ? "SS" : "",
		(si->flags & SWP_DISCARDABLE) ? "D" : "",
		(si->flags & SWP_AREA_DISCARD) ? "s" : "",
		(si->flags & SWP_PAGE_DISCARD) ? "c" : "");

	mutex_unlock(&swapon_mutex);
	atomic_inc(&proc_poll_event);
	wake_up_interruptible(&proc_poll_wait);

	error = 0;
	goto out;
free_swap_zswap:
	zswap_swapoff(si->type);
free_swap_address_space:
	exit_swap_address_space(si->type);
bad_swap_unlock_inode:
	inode_unlock(inode);
bad_swap:
	free_percpu(si->percpu_cluster);
	si->percpu_cluster = NULL;
	free_percpu(si->cluster_next_cpu);
	si->cluster_next_cpu = NULL;
	inode = NULL;
	destroy_swap_extents(si);
	swap_cgroup_swapoff(si->type);
	spin_lock(&swap_lock);
	si->swap_file = NULL;
	si->flags = 0;
	spin_unlock(&swap_lock);
	vfree(swap_map);
	kvfree(zeromap);
	kvfree(cluster_info);
	if (inced_nr_rotate_swap)
		atomic_dec(&nr_rotate_swap);
	if (swap_file)
		filp_close(swap_file, NULL);
out:
	if (!IS_ERR_OR_NULL(folio))
		folio_release_kmap(folio, swap_header);
	if (name)
		putname(name);
	if (inode)
		inode_unlock(inode);
	if (!error)
		enable_swap_slots_cache();
	return error;
}

void si_swapinfo(struct sysinfo *val)
{
	unsigned int type;
	unsigned long nr_to_be_unused = 0;

	spin_lock(&swap_lock);
	for (type = 0; type < nr_swapfiles; type++) {
		struct swap_info_struct *si = swap_info[type];

		if ((si->flags & SWP_USED) && !(si->flags & SWP_WRITEOK))
			nr_to_be_unused += READ_ONCE(si->inuse_pages);
	}
	val->freeswap = atomic_long_read(&nr_swap_pages) + nr_to_be_unused;
	val->totalswap = total_swap_pages + nr_to_be_unused;
	spin_unlock(&swap_lock);
}

/*
 * Verify that nr swap entries are valid and increment their swap map counts.
 *
 * Returns error code in following case.
 * - success -> 0
 * - swp_entry is invalid -> EINVAL
 * - swp_entry is migration entry -> EINVAL
 * - swap-cache reference is requested but there is already one. -> EEXIST
 * - swap-cache reference is requested but the entry is not used. -> ENOENT
 * - swap-mapped reference requested but needs continued swap count. -> ENOMEM
 */
static int __swap_duplicate(swp_entry_t entry, unsigned char usage, int nr)
{
	struct swap_info_struct *si;
	struct swap_cluster_info *ci;
	unsigned long offset;
	unsigned char count;
	unsigned char has_cache;
	int err, i;

	si = swp_swap_info(entry);

	offset = swp_offset(entry);
	VM_WARN_ON(nr > SWAPFILE_CLUSTER - offset % SWAPFILE_CLUSTER);
	VM_WARN_ON(usage == 1 && nr > 1);
	ci = lock_cluster_or_swap_info(si, offset);

	err = 0;
	for (i = 0; i < nr; i++) {
		count = si->swap_map[offset + i];

		/*
		 * swapin_readahead() doesn't check if a swap entry is valid, so the
		 * swap entry could be SWAP_MAP_BAD. Check here with lock held.
		 */
		if (unlikely(swap_count(count) == SWAP_MAP_BAD)) {
			err = -ENOENT;
			goto unlock_out;
		}

		has_cache = count & SWAP_HAS_CACHE;
		count &= ~SWAP_HAS_CACHE;

		if (!count && !has_cache) {
			err = -ENOENT;
		} else if (usage == SWAP_HAS_CACHE) {
			if (has_cache)
				err = -EEXIST;
		} else if ((count & ~COUNT_CONTINUED) > SWAP_MAP_MAX) {
			err = -EINVAL;
		}

		if (err)
			goto unlock_out;
	}

	for (i = 0; i < nr; i++) {
		count = si->swap_map[offset + i];
		has_cache = count & SWAP_HAS_CACHE;
		count &= ~SWAP_HAS_CACHE;

		if (usage == SWAP_HAS_CACHE)
			has_cache = SWAP_HAS_CACHE;
		else if ((count & ~COUNT_CONTINUED) < SWAP_MAP_MAX)
			count += usage;
		else if (swap_count_continued(si, offset + i, count))
			count = COUNT_CONTINUED;
		else {
			/*
			 * Don't need to rollback changes, because if
			 * usage == 1, there must be nr == 1.
			 */
			err = -ENOMEM;
			goto unlock_out;
		}

		WRITE_ONCE(si->swap_map[offset + i], count | has_cache);
	}

unlock_out:
	unlock_cluster_or_swap_info(si, ci);
	return err;
}

/*
 * Help swapoff by noting that swap entry belongs to shmem/tmpfs
 * (in which case its reference count is never incremented).
 */
void swap_shmem_alloc(swp_entry_t entry, int nr)
{
	__swap_duplicate(entry, SWAP_MAP_SHMEM, nr);
}

/*
 * Increase reference count of swap entry by 1.
 * Returns 0 for success, or -ENOMEM if a swap_count_continuation is required
 * but could not be atomically allocated.  Returns 0, just as if it succeeded,
 * if __swap_duplicate() fails for another reason (-EINVAL or -ENOENT), which
 * might occur if a page table entry has got corrupted.
 */
int swap_duplicate(swp_entry_t entry)
{
	int err = 0;

	while (!err && __swap_duplicate(entry, 1, 1) == -ENOMEM)
		err = add_swap_count_continuation(entry, GFP_ATOMIC);
	return err;
}

/*
 * @entry: first swap entry from which we allocate nr swap cache.
 *
 * Called when allocating swap cache for existing swap entries,
 * This can return error codes. Returns 0 at success.
 * -EEXIST means there is a swap cache.
 * Note: return code is different from swap_duplicate().
 */
int swapcache_prepare(swp_entry_t entry, int nr)
{
	return __swap_duplicate(entry, SWAP_HAS_CACHE, nr);
}

void swapcache_clear(struct swap_info_struct *si, swp_entry_t entry, int nr)
{
	unsigned long offset = swp_offset(entry);

	cluster_swap_free_nr(si, offset, nr, SWAP_HAS_CACHE);
}

struct swap_info_struct *swp_swap_info(swp_entry_t entry)
{
	return swap_type_to_swap_info(swp_type(entry));
}

/*
 * out-of-line methods to avoid include hell.
 */
struct address_space *swapcache_mapping(struct folio *folio)
{
	return swp_swap_info(folio->swap)->swap_file->f_mapping;
}
EXPORT_SYMBOL_GPL(swapcache_mapping);

pgoff_t __folio_swap_cache_index(struct folio *folio)
{
	return swap_cache_index(folio->swap);
}
EXPORT_SYMBOL_GPL(__folio_swap_cache_index);

/*
 * add_swap_count_continuation - called when a swap count is duplicated
 * beyond SWAP_MAP_MAX, it allocates a new page and links that to the entry's
 * page of the original vmalloc'ed swap_map, to hold the continuation count
 * (for that entry and for its neighbouring PAGE_SIZE swap entries).  Called
 * again when count is duplicated beyond SWAP_MAP_MAX * SWAP_CONT_MAX, etc.
 *
 * These continuation pages are seldom referenced: the common paths all work
 * on the original swap_map, only referring to a continuation page when the
 * low "digit" of a count is incremented or decremented through SWAP_MAP_MAX.
 *
 * add_swap_count_continuation(, GFP_ATOMIC) can be called while holding
 * page table locks; if it fails, add_swap_count_continuation(, GFP_KERNEL)
 * can be called after dropping locks.
 */
int add_swap_count_continuation(swp_entry_t entry, gfp_t gfp_mask)
{
	struct swap_info_struct *si;
	struct swap_cluster_info *ci;
	struct page *head;
	struct page *page;
	struct page *list_page;
	pgoff_t offset;
	unsigned char count;
	int ret = 0;

	/*
	 * When debugging, it's easier to use __GFP_ZERO here; but it's better
	 * for latency not to zero a page while GFP_ATOMIC and holding locks.
	 */
	page = alloc_page(gfp_mask | __GFP_HIGHMEM);

	si = get_swap_device(entry);
	if (!si) {
		/*
		 * An acceptable race has occurred since the failing
		 * __swap_duplicate(): the swap device may be swapoff
		 */
		goto outer;
	}
	spin_lock(&si->lock);

	offset = swp_offset(entry);

	ci = lock_cluster(si, offset);

	count = swap_count(si->swap_map[offset]);

	if ((count & ~COUNT_CONTINUED) != SWAP_MAP_MAX) {
		/*
		 * The higher the swap count, the more likely it is that tasks
		 * will race to add swap count continuation: we need to avoid
		 * over-provisioning.
		 */
		goto out;
	}

	if (!page) {
		ret = -ENOMEM;
		goto out;
	}

	head = vmalloc_to_page(si->swap_map + offset);
	offset &= ~PAGE_MASK;

	spin_lock(&si->cont_lock);
	/*
	 * Page allocation does not initialize the page's lru field,
	 * but it does always reset its private field.
	 */
	if (!page_private(head)) {
		BUG_ON(count & COUNT_CONTINUED);
		INIT_LIST_HEAD(&head->lru);
		set_page_private(head, SWP_CONTINUED);
		si->flags |= SWP_CONTINUED;
	}

	list_for_each_entry(list_page, &head->lru, lru) {
		unsigned char *map;

		/*
		 * If the previous map said no continuation, but we've found
		 * a continuation page, free our allocation and use this one.
		 */
		if (!(count & COUNT_CONTINUED))
			goto out_unlock_cont;

		map = kmap_local_page(list_page) + offset;
		count = *map;
		kunmap_local(map);

		/*
		 * If this continuation count now has some space in it,
		 * free our allocation and use this one.
		 */
		if ((count & ~COUNT_CONTINUED) != SWAP_CONT_MAX)
			goto out_unlock_cont;
	}

	list_add_tail(&page->lru, &head->lru);
	page = NULL;			/* now it's attached, don't free it */
out_unlock_cont:
	spin_unlock(&si->cont_lock);
out:
	unlock_cluster(ci);
	spin_unlock(&si->lock);
	put_swap_device(si);
outer:
	if (page)
		__free_page(page);
	return ret;
}

/*
 * swap_count_continued - when the original swap_map count is incremented
 * from SWAP_MAP_MAX, check if there is already a continuation page to carry
 * into, carry if so, or else fail until a new continuation page is allocated;
 * when the original swap_map count is decremented from 0 with continuation,
 * borrow from the continuation and report whether it still holds more.
 * Called while __swap_duplicate() or swap_entry_free() holds swap or cluster
 * lock.
 */
static bool swap_count_continued(struct swap_info_struct *si,
				 pgoff_t offset, unsigned char count)
{
	struct page *head;
	struct page *page;
	unsigned char *map;
	bool ret;

	head = vmalloc_to_page(si->swap_map + offset);
	if (page_private(head) != SWP_CONTINUED) {
		BUG_ON(count & COUNT_CONTINUED);
		return false;		/* need to add count continuation */
	}

	spin_lock(&si->cont_lock);
	offset &= ~PAGE_MASK;
	page = list_next_entry(head, lru);
	map = kmap_local_page(page) + offset;

	if (count == SWAP_MAP_MAX)	/* initial increment from swap_map */
		goto init_map;		/* jump over SWAP_CONT_MAX checks */

	if (count == (SWAP_MAP_MAX | COUNT_CONTINUED)) { /* incrementing */
		/*
		 * Think of how you add 1 to 999
		 */
		while (*map == (SWAP_CONT_MAX | COUNT_CONTINUED)) {
			kunmap_local(map);
			page = list_next_entry(page, lru);
			BUG_ON(page == head);
			map = kmap_local_page(page) + offset;
		}
		if (*map == SWAP_CONT_MAX) {
			kunmap_local(map);
			page = list_next_entry(page, lru);
			if (page == head) {
				ret = false;	/* add count continuation */
				goto out;
			}
			map = kmap_local_page(page) + offset;
init_map:		*map = 0;		/* we didn't zero the page */
		}
		*map += 1;
		kunmap_local(map);
		while ((page = list_prev_entry(page, lru)) != head) {
			map = kmap_local_page(page) + offset;
			*map = COUNT_CONTINUED;
			kunmap_local(map);
		}
		ret = true;			/* incremented */

	} else {				/* decrementing */
		/*
		 * Think of how you subtract 1 from 1000
		 */
		BUG_ON(count != COUNT_CONTINUED);
		while (*map == COUNT_CONTINUED) {
			kunmap_local(map);
			page = list_next_entry(page, lru);
			BUG_ON(page == head);
			map = kmap_local_page(page) + offset;
		}
		BUG_ON(*map == 0);
		*map -= 1;
		if (*map == 0)
			count = 0;
		kunmap_local(map);
		while ((page = list_prev_entry(page, lru)) != head) {
			map = kmap_local_page(page) + offset;
			*map = SWAP_CONT_MAX | count;
			count = COUNT_CONTINUED;
			kunmap_local(map);
		}
		ret = count == COUNT_CONTINUED;
	}
out:
	spin_unlock(&si->cont_lock);
	return ret;
}

/*
 * free_swap_count_continuations - swapoff free all the continuation pages
 * appended to the swap_map, after swap_map is quiesced, before vfree'ing it.
 */
static void free_swap_count_continuations(struct swap_info_struct *si)
{
	pgoff_t offset;

	for (offset = 0; offset < si->max; offset += PAGE_SIZE) {
		struct page *head;
		head = vmalloc_to_page(si->swap_map + offset);
		if (page_private(head)) {
			struct page *page, *next;

			list_for_each_entry_safe(page, next, &head->lru, lru) {
				list_del(&page->lru);
				__free_page(page);
			}
		}
	}
}

#if defined(CONFIG_MEMCG) && defined(CONFIG_BLK_CGROUP)
void __folio_throttle_swaprate(struct folio *folio, gfp_t gfp)
{
	struct swap_info_struct *si, *next;
	int nid = folio_nid(folio);

	if (!(gfp & __GFP_IO))
		return;

	if (!__has_usable_swap())
		return;

	if (!blk_cgroup_congested())
		return;

	/*
	 * We've already scheduled a throttle, avoid taking the global swap
	 * lock.
	 */
	if (current->throttle_disk)
		return;

	spin_lock(&swap_avail_lock);
	plist_for_each_entry_safe(si, next, &swap_avail_heads[nid],
				  avail_lists[nid]) {
		if (si->bdev) {
			blkcg_schedule_throttle(si->bdev->bd_disk, true);
			break;
		}
	}
	spin_unlock(&swap_avail_lock);
}
#endif

static int __init swapfile_init(void)
{
	int nid;

	swap_avail_heads = kmalloc_array(nr_node_ids, sizeof(struct plist_head),
					 GFP_KERNEL);
	if (!swap_avail_heads) {
		pr_emerg("Not enough memory for swap heads, swap is disabled\n");
		return -ENOMEM;
	}

	for_each_node(nid)
		plist_head_init(&swap_avail_heads[nid]);

	swapfile_maximum_size = arch_max_swapfile_size();

#ifdef CONFIG_MIGRATION
	if (swapfile_maximum_size >= (1UL << SWP_MIG_TOTAL_BITS))
		swap_migration_ad_supported = true;
#endif	/* CONFIG_MIGRATION */

	return 0;
}
subsys_initcall(swapfile_init);
