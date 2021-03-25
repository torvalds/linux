// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/mm/swapfile.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 */

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
#include <linux/blkdev.h>
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
#include <linux/frontswap.h>
#include <linux/swapfile.h>
#include <linux/export.h>
#include <linux/swap_slots.h>
#include <linux/sort.h>

#include <asm/tlbflush.h>
#include <linux/swapops.h>
#include <linux/swap_cgroup.h>

static bool swap_count_continued(struct swap_info_struct *, pgoff_t,
				 unsigned char);
static void free_swap_count_continuations(struct swap_info_struct *);

DEFINE_SPINLOCK(swap_lock);
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

static const char Bad_file[] = "Bad swap file entry ";
static const char Unused_file[] = "Unused swap file entry ";
static const char Bad_offset[] = "Bad swap offset entry ";
static const char Unused_offset[] = "Unused swap offset entry ";

/*
 * all active swap_info_structs
 * protected with swap_lock, and ordered by priority.
 */
PLIST_HEAD(swap_active_head);

/*
 * all available (active, not full) swap_info_structs
 * protected with swap_avail_lock, ordered by priority.
 * This is used by get_swap_page() instead of swap_active_head
 * because swap_active_head includes all swap_info_structs,
 * but get_swap_page() doesn't need to look at full ones.
 * This uses its own lock instead of swap_lock because when a
 * swap_info_struct changes between not-full/full, it needs to
 * add/remove itself to/from this list, but the swap_info_struct->lock
 * is held and the locking order requires swap_lock to be taken
 * before any swap_info_struct->lock.
 */
static struct plist_head *swap_avail_heads;
static DEFINE_SPINLOCK(swap_avail_lock);

struct swap_info_struct *swap_info[MAX_SWAPFILES];

static DEFINE_MUTEX(swapon_mutex);

static DECLARE_WAIT_QUEUE_HEAD(proc_poll_wait);
/* Activity counter to indicate that a swapon or swapoff has occurred */
static atomic_t proc_poll_event = ATOMIC_INIT(0);

atomic_t nr_rotate_swap = ATOMIC_INIT(0);

static struct swap_info_struct *swap_type_to_swap_info(int type)
{
	if (type >= READ_ONCE(nr_swapfiles))
		return NULL;

	smp_rmb();	/* Pairs with smp_wmb in alloc_swap_info. */
	return READ_ONCE(swap_info[type]);
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
/* Reclaim the swap entry if swap is getting full*/
#define TTRS_FULL		0x4

/* returns 1 if swap entry is freed */
static int __try_to_reclaim_swap(struct swap_info_struct *si,
				 unsigned long offset, unsigned long flags)
{
	swp_entry_t entry = swp_entry(si->type, offset);
	struct page *page;
	int ret = 0;

	page = find_get_page(swap_address_space(entry), offset);
	if (!page)
		return 0;
	/*
	 * When this function is called from scan_swap_map_slots() and it's
	 * called by vmscan.c at reclaiming pages. So, we hold a lock on a page,
	 * here. We have to use trylock for avoiding deadlock. This is a special
	 * case and you should use try_to_free_swap() with explicit lock_page()
	 * in usual operations.
	 */
	if (trylock_page(page)) {
		if ((flags & TTRS_ANYWAY) ||
		    ((flags & TTRS_UNMAPPED) && !page_mapped(page)) ||
		    ((flags & TTRS_FULL) && mem_cgroup_swap_full(page)))
			ret = try_to_free_swap(page);
		unlock_page(page);
	}
	put_page(page);
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
				nr_blocks, GFP_KERNEL, 0);
		if (err)
			return err;
		cond_resched();
	}

	for (se = next_se(se); se; se = next_se(se)) {
		start_block = se->start_block << (PAGE_SHIFT - 9);
		nr_blocks = (sector_t)se->nr_pages << (PAGE_SHIFT - 9);

		err = blkdev_issue_discard(si->bdev, start_block,
				nr_blocks, GFP_KERNEL, 0);
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

sector_t swap_page_sector(struct page *page)
{
	struct swap_info_struct *sis = page_swap_info(page);
	struct swap_extent *se;
	sector_t sector;
	pgoff_t offset;

	offset = __page_file_index(page);
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
					nr_blocks, GFP_NOIO, 0))
			break;

		se = next_se(se);
	}
}

#ifdef CONFIG_THP_SWAP
#define SWAPFILE_CLUSTER	HPAGE_PMD_NR

#define swap_entry_size(size)	(size)
#else
#define SWAPFILE_CLUSTER	256

/*
 * Define swap_entry_size() as constant to let compiler to optimize
 * out some code if !CONFIG_THP_SWAP
 */
#define swap_entry_size(size)	1
#endif
#define LATENCY_LIMIT		256

static inline void cluster_set_flag(struct swap_cluster_info *info,
	unsigned int flag)
{
	info->flags = flag;
}

static inline unsigned int cluster_count(struct swap_cluster_info *info)
{
	return info->data;
}

static inline void cluster_set_count(struct swap_cluster_info *info,
				     unsigned int c)
{
	info->data = c;
}

static inline void cluster_set_count_flag(struct swap_cluster_info *info,
					 unsigned int c, unsigned int f)
{
	info->flags = f;
	info->data = c;
}

static inline unsigned int cluster_next(struct swap_cluster_info *info)
{
	return info->data;
}

static inline void cluster_set_next(struct swap_cluster_info *info,
				    unsigned int n)
{
	info->data = n;
}

static inline void cluster_set_next_flag(struct swap_cluster_info *info,
					 unsigned int n, unsigned int f)
{
	info->flags = f;
	info->data = n;
}

static inline bool cluster_is_free(struct swap_cluster_info *info)
{
	return info->flags & CLUSTER_FLAG_FREE;
}

static inline bool cluster_is_null(struct swap_cluster_info *info)
{
	return info->flags & CLUSTER_FLAG_NEXT_NULL;
}

static inline void cluster_set_null(struct swap_cluster_info *info)
{
	info->flags = CLUSTER_FLAG_NEXT_NULL;
	info->data = 0;
}

static inline bool cluster_is_huge(struct swap_cluster_info *info)
{
	if (IS_ENABLED(CONFIG_THP_SWAP))
		return info->flags & CLUSTER_FLAG_HUGE;
	return false;
}

static inline void cluster_clear_huge(struct swap_cluster_info *info)
{
	info->flags &= ~CLUSTER_FLAG_HUGE;
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

static inline bool cluster_list_empty(struct swap_cluster_list *list)
{
	return cluster_is_null(&list->head);
}

static inline unsigned int cluster_list_first(struct swap_cluster_list *list)
{
	return cluster_next(&list->head);
}

static void cluster_list_init(struct swap_cluster_list *list)
{
	cluster_set_null(&list->head);
	cluster_set_null(&list->tail);
}

static void cluster_list_add_tail(struct swap_cluster_list *list,
				  struct swap_cluster_info *ci,
				  unsigned int idx)
{
	if (cluster_list_empty(list)) {
		cluster_set_next_flag(&list->head, idx, 0);
		cluster_set_next_flag(&list->tail, idx, 0);
	} else {
		struct swap_cluster_info *ci_tail;
		unsigned int tail = cluster_next(&list->tail);

		/*
		 * Nested cluster lock, but both cluster locks are
		 * only acquired when we held swap_info_struct->lock
		 */
		ci_tail = ci + tail;
		spin_lock_nested(&ci_tail->lock, SINGLE_DEPTH_NESTING);
		cluster_set_next(ci_tail, idx);
		spin_unlock(&ci_tail->lock);
		cluster_set_next_flag(&list->tail, idx, 0);
	}
}

static unsigned int cluster_list_del_first(struct swap_cluster_list *list,
					   struct swap_cluster_info *ci)
{
	unsigned int idx;

	idx = cluster_next(&list->head);
	if (cluster_next(&list->tail) == idx) {
		cluster_set_null(&list->head);
		cluster_set_null(&list->tail);
	} else
		cluster_set_next_flag(&list->head,
				      cluster_next(&ci[idx]), 0);

	return idx;
}

/* Add a cluster to discard list and schedule it to do discard */
static void swap_cluster_schedule_discard(struct swap_info_struct *si,
		unsigned int idx)
{
	/*
	 * If scan_swap_map() can't find a free cluster, it will check
	 * si->swap_map directly. To make sure the discarding cluster isn't
	 * taken by scan_swap_map(), mark the swap entries bad (occupied). It
	 * will be cleared after discard
	 */
	memset(si->swap_map + idx * SWAPFILE_CLUSTER,
			SWAP_MAP_BAD, SWAPFILE_CLUSTER);

	cluster_list_add_tail(&si->discard_clusters, si->cluster_info, idx);

	schedule_work(&si->discard_work);
}

static void __free_cluster(struct swap_info_struct *si, unsigned long idx)
{
	struct swap_cluster_info *ci = si->cluster_info;

	cluster_set_flag(ci + idx, CLUSTER_FLAG_FREE);
	cluster_list_add_tail(&si->free_clusters, ci, idx);
}

/*
 * Doing discard actually. After a cluster discard is finished, the cluster
 * will be added to free cluster list. caller should hold si->lock.
*/
static void swap_do_scheduled_discard(struct swap_info_struct *si)
{
	struct swap_cluster_info *info, *ci;
	unsigned int idx;

	info = si->cluster_info;

	while (!cluster_list_empty(&si->discard_clusters)) {
		idx = cluster_list_del_first(&si->discard_clusters, info);
		spin_unlock(&si->lock);

		discard_swap_cluster(si, idx * SWAPFILE_CLUSTER,
				SWAPFILE_CLUSTER);

		spin_lock(&si->lock);
		ci = lock_cluster(si, idx * SWAPFILE_CLUSTER);
		__free_cluster(si, idx);
		memset(si->swap_map + idx * SWAPFILE_CLUSTER,
				0, SWAPFILE_CLUSTER);
		unlock_cluster(ci);
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

static void alloc_cluster(struct swap_info_struct *si, unsigned long idx)
{
	struct swap_cluster_info *ci = si->cluster_info;

	VM_BUG_ON(cluster_list_first(&si->free_clusters) != idx);
	cluster_list_del_first(&si->free_clusters, ci);
	cluster_set_count_flag(ci + idx, 0, 0);
}

static void free_cluster(struct swap_info_struct *si, unsigned long idx)
{
	struct swap_cluster_info *ci = si->cluster_info + idx;

	VM_BUG_ON(cluster_count(ci) != 0);
	/*
	 * If the swap is discardable, prepare discard the cluster
	 * instead of free it immediately. The cluster will be freed
	 * after discard.
	 */
	if ((si->flags & (SWP_WRITEOK | SWP_PAGE_DISCARD)) ==
	    (SWP_WRITEOK | SWP_PAGE_DISCARD)) {
		swap_cluster_schedule_discard(si, idx);
		return;
	}

	__free_cluster(si, idx);
}

/*
 * The cluster corresponding to page_nr will be used. The cluster will be
 * removed from free cluster list and its usage counter will be increased.
 */
static void inc_cluster_info_page(struct swap_info_struct *p,
	struct swap_cluster_info *cluster_info, unsigned long page_nr)
{
	unsigned long idx = page_nr / SWAPFILE_CLUSTER;

	if (!cluster_info)
		return;
	if (cluster_is_free(&cluster_info[idx]))
		alloc_cluster(p, idx);

	VM_BUG_ON(cluster_count(&cluster_info[idx]) >= SWAPFILE_CLUSTER);
	cluster_set_count(&cluster_info[idx],
		cluster_count(&cluster_info[idx]) + 1);
}

/*
 * The cluster corresponding to page_nr decreases one usage. If the usage
 * counter becomes 0, which means no page in the cluster is in using, we can
 * optionally discard the cluster and add it to free cluster list.
 */
static void dec_cluster_info_page(struct swap_info_struct *p,
	struct swap_cluster_info *cluster_info, unsigned long page_nr)
{
	unsigned long idx = page_nr / SWAPFILE_CLUSTER;

	if (!cluster_info)
		return;

	VM_BUG_ON(cluster_count(&cluster_info[idx]) == 0);
	cluster_set_count(&cluster_info[idx],
		cluster_count(&cluster_info[idx]) - 1);

	if (cluster_count(&cluster_info[idx]) == 0)
		free_cluster(p, idx);
}

/*
 * It's possible scan_swap_map() uses a free cluster in the middle of free
 * cluster list. Avoiding such abuse to avoid list corruption.
 */
static bool
scan_swap_map_ssd_cluster_conflict(struct swap_info_struct *si,
	unsigned long offset)
{
	struct percpu_cluster *percpu_cluster;
	bool conflict;

	offset /= SWAPFILE_CLUSTER;
	conflict = !cluster_list_empty(&si->free_clusters) &&
		offset != cluster_list_first(&si->free_clusters) &&
		cluster_is_free(&si->cluster_info[offset]);

	if (!conflict)
		return false;

	percpu_cluster = this_cpu_ptr(si->percpu_cluster);
	cluster_set_null(&percpu_cluster->index);
	return true;
}

/*
 * Try to get a swap entry from current cpu's swap entry pool (a cluster). This
 * might involve allocating a new cluster for current CPU too.
 */
static bool scan_swap_map_try_ssd_cluster(struct swap_info_struct *si,
	unsigned long *offset, unsigned long *scan_base)
{
	struct percpu_cluster *cluster;
	struct swap_cluster_info *ci;
	unsigned long tmp, max;

new_cluster:
	cluster = this_cpu_ptr(si->percpu_cluster);
	if (cluster_is_null(&cluster->index)) {
		if (!cluster_list_empty(&si->free_clusters)) {
			cluster->index = si->free_clusters.head;
			cluster->next = cluster_next(&cluster->index) *
					SWAPFILE_CLUSTER;
		} else if (!cluster_list_empty(&si->discard_clusters)) {
			/*
			 * we don't have free cluster but have some clusters in
			 * discarding, do discard now and reclaim them, then
			 * reread cluster_next_cpu since we dropped si->lock
			 */
			swap_do_scheduled_discard(si);
			*scan_base = this_cpu_read(*si->cluster_next_cpu);
			*offset = *scan_base;
			goto new_cluster;
		} else
			return false;
	}

	/*
	 * Other CPUs can use our cluster if they can't find a free cluster,
	 * check if there is still free entry in the cluster
	 */
	tmp = cluster->next;
	max = min_t(unsigned long, si->max,
		    (cluster_next(&cluster->index) + 1) * SWAPFILE_CLUSTER);
	if (tmp < max) {
		ci = lock_cluster(si, tmp);
		while (tmp < max) {
			if (!si->swap_map[tmp])
				break;
			tmp++;
		}
		unlock_cluster(ci);
	}
	if (tmp >= max) {
		cluster_set_null(&cluster->index);
		goto new_cluster;
	}
	cluster->next = tmp + 1;
	*offset = tmp;
	*scan_base = tmp;
	return true;
}

static void __del_from_avail_list(struct swap_info_struct *p)
{
	int nid;

	for_each_node(nid)
		plist_del(&p->avail_lists[nid], &swap_avail_heads[nid]);
}

static void del_from_avail_list(struct swap_info_struct *p)
{
	spin_lock(&swap_avail_lock);
	__del_from_avail_list(p);
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
	si->inuse_pages += nr_entries;
	if (si->inuse_pages == si->pages) {
		si->lowest_bit = si->max;
		si->highest_bit = 0;
		del_from_avail_list(si);
	}
}

static void add_to_avail_list(struct swap_info_struct *p)
{
	int nid;

	spin_lock(&swap_avail_lock);
	for_each_node(nid) {
		WARN_ON(!plist_node_empty(&p->avail_lists[nid]));
		plist_add(&p->avail_lists[nid], &swap_avail_heads[nid]);
	}
	spin_unlock(&swap_avail_lock);
}

static void swap_range_free(struct swap_info_struct *si, unsigned long offset,
			    unsigned int nr_entries)
{
	unsigned long begin = offset;
	unsigned long end = offset + nr_entries - 1;
	void (*swap_slot_free_notify)(struct block_device *, unsigned long);

	if (offset < si->lowest_bit)
		si->lowest_bit = offset;
	if (end > si->highest_bit) {
		bool was_full = !si->highest_bit;

		WRITE_ONCE(si->highest_bit, end);
		if (was_full && (si->flags & SWP_WRITEOK))
			add_to_avail_list(si);
	}
	atomic_long_add(nr_entries, &nr_swap_pages);
	si->inuse_pages -= nr_entries;
	if (si->flags & SWP_BLKDEV)
		swap_slot_free_notify =
			si->bdev->bd_disk->fops->swap_slot_free_notify;
	else
		swap_slot_free_notify = NULL;
	while (offset <= end) {
		arch_swap_invalidate_page(si->type, offset);
		frontswap_invalidate_page(si->type, offset);
		if (swap_slot_free_notify)
			swap_slot_free_notify(si->bdev, offset);
		offset++;
	}
	clear_shadow_from_swap_cache(si->type, begin, end);
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
		next = si->lowest_bit +
			prandom_u32_max(si->highest_bit - si->lowest_bit + 1);
		next = ALIGN_DOWN(next, SWAP_ADDRESS_SPACE_PAGES);
		next = max_t(unsigned int, next, si->lowest_bit);
	}
	this_cpu_write(*si->cluster_next_cpu, next);
}

static int scan_swap_map_slots(struct swap_info_struct *si,
			       unsigned char usage, int nr,
			       swp_entry_t slots[])
{
	struct swap_cluster_info *ci;
	unsigned long offset;
	unsigned long scan_base;
	unsigned long last_in_cluster = 0;
	int latency_ration = LATENCY_LIMIT;
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

	si->flags += SWP_SCANNING;
	/*
	 * Use percpu scan base for SSD to reduce lock contention on
	 * cluster and swap cache.  For HDD, sequential access is more
	 * important.
	 */
	if (si->flags & SWP_SOLIDSTATE)
		scan_base = this_cpu_read(*si->cluster_next_cpu);
	else
		scan_base = si->cluster_next;
	offset = scan_base;

	/* SSD algorithm */
	if (si->cluster_info) {
		if (!scan_swap_map_try_ssd_cluster(si, &offset, &scan_base))
			goto scan;
	} else if (unlikely(!si->cluster_nr--)) {
		if (si->pages - si->inuse_pages < SWAPFILE_CLUSTER) {
			si->cluster_nr = SWAPFILE_CLUSTER - 1;
			goto checks;
		}

		spin_unlock(&si->lock);

		/*
		 * If seek is expensive, start searching for new cluster from
		 * start of partition, to minimize the span of allocated swap.
		 * If seek is cheap, that is the SWP_SOLIDSTATE si->cluster_info
		 * case, just handled by scan_swap_map_try_ssd_cluster() above.
		 */
		scan_base = offset = si->lowest_bit;
		last_in_cluster = offset + SWAPFILE_CLUSTER - 1;

		/* Locate the first empty (unaligned) cluster */
		for (; last_in_cluster <= si->highest_bit; offset++) {
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
	if (si->cluster_info) {
		while (scan_swap_map_ssd_cluster_conflict(si, offset)) {
		/* take a break if we already got some slots */
			if (n_ret)
				goto done;
			if (!scan_swap_map_try_ssd_cluster(si, &offset,
							&scan_base))
				goto scan;
		}
	}
	if (!(si->flags & SWP_WRITEOK))
		goto no_page;
	if (!si->highest_bit)
		goto no_page;
	if (offset > si->highest_bit)
		scan_base = offset = si->lowest_bit;

	ci = lock_cluster(si, offset);
	/* reuse swap entry of cache-only swap if not busy. */
	if (vm_swap_full() && si->swap_map[offset] == SWAP_HAS_CACHE) {
		int swap_was_freed;
		unlock_cluster(ci);
		spin_unlock(&si->lock);
		swap_was_freed = __try_to_reclaim_swap(si, offset, TTRS_ANYWAY);
		spin_lock(&si->lock);
		/* entry was freed successfully, try to use this again */
		if (swap_was_freed)
			goto checks;
		goto scan; /* check next one */
	}

	if (si->swap_map[offset]) {
		unlock_cluster(ci);
		if (!n_ret)
			goto scan;
		else
			goto done;
	}
	WRITE_ONCE(si->swap_map[offset], usage);
	inc_cluster_info_page(si, si->cluster_info, offset);
	unlock_cluster(ci);

	swap_range_alloc(si, offset, 1);
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

	/* try to get more slots in cluster */
	if (si->cluster_info) {
		if (scan_swap_map_try_ssd_cluster(si, &offset, &scan_base))
			goto checks;
	} else if (si->cluster_nr && !si->swap_map[++offset]) {
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
	set_cluster_next(si, offset + 1);
	si->flags -= SWP_SCANNING;
	return n_ret;

scan:
	spin_unlock(&si->lock);
	while (++offset <= READ_ONCE(si->highest_bit)) {
		if (data_race(!si->swap_map[offset])) {
			spin_lock(&si->lock);
			goto checks;
		}
		if (vm_swap_full() &&
		    READ_ONCE(si->swap_map[offset]) == SWAP_HAS_CACHE) {
			spin_lock(&si->lock);
			goto checks;
		}
		if (unlikely(--latency_ration < 0)) {
			cond_resched();
			latency_ration = LATENCY_LIMIT;
			scanned_many = true;
		}
	}
	offset = si->lowest_bit;
	while (offset < scan_base) {
		if (data_race(!si->swap_map[offset])) {
			spin_lock(&si->lock);
			goto checks;
		}
		if (vm_swap_full() &&
		    READ_ONCE(si->swap_map[offset]) == SWAP_HAS_CACHE) {
			spin_lock(&si->lock);
			goto checks;
		}
		if (unlikely(--latency_ration < 0)) {
			cond_resched();
			latency_ration = LATENCY_LIMIT;
			scanned_many = true;
		}
		offset++;
	}
	spin_lock(&si->lock);

no_page:
	si->flags -= SWP_SCANNING;
	return n_ret;
}

static int swap_alloc_cluster(struct swap_info_struct *si, swp_entry_t *slot)
{
	unsigned long idx;
	struct swap_cluster_info *ci;
	unsigned long offset;

	/*
	 * Should not even be attempting cluster allocations when huge
	 * page swap is disabled.  Warn and fail the allocation.
	 */
	if (!IS_ENABLED(CONFIG_THP_SWAP)) {
		VM_WARN_ON_ONCE(1);
		return 0;
	}

	if (cluster_list_empty(&si->free_clusters))
		return 0;

	idx = cluster_list_first(&si->free_clusters);
	offset = idx * SWAPFILE_CLUSTER;
	ci = lock_cluster(si, offset);
	alloc_cluster(si, idx);
	cluster_set_count_flag(ci, SWAPFILE_CLUSTER, CLUSTER_FLAG_HUGE);

	memset(si->swap_map + offset, SWAP_HAS_CACHE, SWAPFILE_CLUSTER);
	unlock_cluster(ci);
	swap_range_alloc(si, offset, SWAPFILE_CLUSTER);
	*slot = swp_entry(si->type, offset);

	return 1;
}

static void swap_free_cluster(struct swap_info_struct *si, unsigned long idx)
{
	unsigned long offset = idx * SWAPFILE_CLUSTER;
	struct swap_cluster_info *ci;

	ci = lock_cluster(si, offset);
	memset(si->swap_map + offset, 0, SWAPFILE_CLUSTER);
	cluster_set_count_flag(ci, 0, 0);
	free_cluster(si, idx);
	unlock_cluster(ci);
	swap_range_free(si, offset, SWAPFILE_CLUSTER);
}

static unsigned long scan_swap_map(struct swap_info_struct *si,
				   unsigned char usage)
{
	swp_entry_t entry;
	int n_ret;

	n_ret = scan_swap_map_slots(si, usage, 1, &entry);

	if (n_ret)
		return swp_offset(entry);
	else
		return 0;

}

int get_swap_pages(int n_goal, swp_entry_t swp_entries[], int entry_size)
{
	unsigned long size = swap_entry_size(entry_size);
	struct swap_info_struct *si, *next;
	long avail_pgs;
	int n_ret = 0;
	int node;

	/* Only single cluster request supported */
	WARN_ON_ONCE(n_goal > 1 && size == SWAPFILE_CLUSTER);

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
		if (size == SWAPFILE_CLUSTER) {
			if (si->flags & SWP_BLKDEV)
				n_ret = swap_alloc_cluster(si, swp_entries);
		} else
			n_ret = scan_swap_map_slots(si, SWAP_HAS_CACHE,
						    n_goal, swp_entries);
		spin_unlock(&si->lock);
		if (n_ret || size == SWAPFILE_CLUSTER)
			goto check_out;
		pr_debug("scan_swap_map of si %d failed to find offset\n",
			si->type);

		spin_lock(&swap_avail_lock);
nextsi:
		/*
		 * if we got here, it's likely that si was almost full before,
		 * and since scan_swap_map() can drop the si->lock, multiple
		 * callers probably all tried to get a page from the same si
		 * and it filled up before we could get one; or, the si filled
		 * up between us dropping swap_avail_lock and taking si->lock.
		 * Since we dropped the swap_avail_lock, the swap_avail_head
		 * list may have been modified; so if next is still in the
		 * swap_avail_head list then try it, otherwise start over
		 * if we have not gotten any slots.
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

/* The only caller of this function is now suspend routine */
swp_entry_t get_swap_page_of_type(int type)
{
	struct swap_info_struct *si = swap_type_to_swap_info(type);
	pgoff_t offset;

	if (!si)
		goto fail;

	spin_lock(&si->lock);
	if (si->flags & SWP_WRITEOK) {
		/* This is called for allocating swap entry, not cache */
		offset = scan_swap_map(si, 1);
		if (offset) {
			atomic_long_dec(&nr_swap_pages);
			spin_unlock(&si->lock);
			return swp_entry(type, offset);
		}
	}
	spin_unlock(&si->lock);
fail:
	return (swp_entry_t) {0};
}

static struct swap_info_struct *__swap_info_get(swp_entry_t entry)
{
	struct swap_info_struct *p;
	unsigned long offset;

	if (!entry.val)
		goto out;
	p = swp_swap_info(entry);
	if (!p)
		goto bad_nofile;
	if (data_race(!(p->flags & SWP_USED)))
		goto bad_device;
	offset = swp_offset(entry);
	if (offset >= p->max)
		goto bad_offset;
	return p;

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

static struct swap_info_struct *_swap_info_get(swp_entry_t entry)
{
	struct swap_info_struct *p;

	p = __swap_info_get(entry);
	if (!p)
		goto out;
	if (data_race(!p->swap_map[swp_offset(entry)]))
		goto bad_free;
	return p;

bad_free:
	pr_err("%s: %s%08lx\n", __func__, Unused_offset, entry.val);
out:
	return NULL;
}

static struct swap_info_struct *swap_info_get(swp_entry_t entry)
{
	struct swap_info_struct *p;

	p = _swap_info_get(entry);
	if (p)
		spin_lock(&p->lock);
	return p;
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

static unsigned char __swap_entry_free_locked(struct swap_info_struct *p,
					      unsigned long offset,
					      unsigned char usage)
{
	unsigned char count;
	unsigned char has_cache;

	count = p->swap_map[offset];

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
			if (swap_count_continued(p, offset, count))
				count = SWAP_MAP_MAX | COUNT_CONTINUED;
			else
				count = SWAP_MAP_MAX;
		} else
			count--;
	}

	usage = count | has_cache;
	if (usage)
		WRITE_ONCE(p->swap_map[offset], usage);
	else
		WRITE_ONCE(p->swap_map[offset], SWAP_HAS_CACHE);

	return usage;
}

/*
 * Check whether swap entry is valid in the swap device.  If so,
 * return pointer to swap_info_struct, and keep the swap entry valid
 * via preventing the swap device from being swapoff, until
 * put_swap_device() is called.  Otherwise return NULL.
 *
 * The entirety of the RCU read critical section must come before the
 * return from or after the call to synchronize_rcu() in
 * enable_swap_info() or swapoff().  So if "si->flags & SWP_VALID" is
 * true, the si->map, si->cluster_info, etc. must be valid in the
 * critical section.
 *
 * Notice that swapoff or swapoff+swapon can still happen before the
 * rcu_read_lock() in get_swap_device() or after the rcu_read_unlock()
 * in put_swap_device() if there isn't any other way to prevent
 * swapoff, such as page lock, page table lock, etc.  The caller must
 * be prepared for that.  For example, the following situation is
 * possible.
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

	rcu_read_lock();
	if (data_race(!(si->flags & SWP_VALID)))
		goto unlock_out;
	offset = swp_offset(entry);
	if (offset >= si->max)
		goto unlock_out;

	return si;
bad_nofile:
	pr_err("%s: %s%08lx\n", __func__, Bad_file, entry.val);
out:
	return NULL;
unlock_out:
	rcu_read_unlock();
	return NULL;
}

static unsigned char __swap_entry_free(struct swap_info_struct *p,
				       swp_entry_t entry)
{
	struct swap_cluster_info *ci;
	unsigned long offset = swp_offset(entry);
	unsigned char usage;

	ci = lock_cluster_or_swap_info(p, offset);
	usage = __swap_entry_free_locked(p, offset, 1);
	unlock_cluster_or_swap_info(p, ci);
	if (!usage)
		free_swap_slot(entry);

	return usage;
}

static void swap_entry_free(struct swap_info_struct *p, swp_entry_t entry)
{
	struct swap_cluster_info *ci;
	unsigned long offset = swp_offset(entry);
	unsigned char count;

	ci = lock_cluster(p, offset);
	count = p->swap_map[offset];
	VM_BUG_ON(count != SWAP_HAS_CACHE);
	p->swap_map[offset] = 0;
	dec_cluster_info_page(p, p->cluster_info, offset);
	unlock_cluster(ci);

	mem_cgroup_uncharge_swap(entry, 1);
	swap_range_free(p, offset, 1);
}

/*
 * Caller has made sure that the swap device corresponding to entry
 * is still around or has not been recycled.
 */
void swap_free(swp_entry_t entry)
{
	struct swap_info_struct *p;

	p = _swap_info_get(entry);
	if (p)
		__swap_entry_free(p, entry);
}

/*
 * Called after dropping swapcache to decrease refcnt to swap entries.
 */
void put_swap_page(struct page *page, swp_entry_t entry)
{
	unsigned long offset = swp_offset(entry);
	unsigned long idx = offset / SWAPFILE_CLUSTER;
	struct swap_cluster_info *ci;
	struct swap_info_struct *si;
	unsigned char *map;
	unsigned int i, free_entries = 0;
	unsigned char val;
	int size = swap_entry_size(thp_nr_pages(page));

	si = _swap_info_get(entry);
	if (!si)
		return;

	ci = lock_cluster_or_swap_info(si, offset);
	if (size == SWAPFILE_CLUSTER) {
		VM_BUG_ON(!cluster_is_huge(ci));
		map = si->swap_map + offset;
		for (i = 0; i < SWAPFILE_CLUSTER; i++) {
			val = map[i];
			VM_BUG_ON(!(val & SWAP_HAS_CACHE));
			if (val == SWAP_HAS_CACHE)
				free_entries++;
		}
		cluster_clear_huge(ci);
		if (free_entries == SWAPFILE_CLUSTER) {
			unlock_cluster_or_swap_info(si, ci);
			spin_lock(&si->lock);
			mem_cgroup_uncharge_swap(entry, SWAPFILE_CLUSTER);
			swap_free_cluster(si, idx);
			spin_unlock(&si->lock);
			return;
		}
	}
	for (i = 0; i < size; i++, entry.val++) {
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

#ifdef CONFIG_THP_SWAP
int split_swap_cluster(swp_entry_t entry)
{
	struct swap_info_struct *si;
	struct swap_cluster_info *ci;
	unsigned long offset = swp_offset(entry);

	si = _swap_info_get(entry);
	if (!si)
		return -EBUSY;
	ci = lock_cluster(si, offset);
	cluster_clear_huge(ci);
	unlock_cluster(ci);
	return 0;
}
#endif

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
			swap_entry_free(p, entries[i]);
		prev = p;
	}
	if (p)
		spin_unlock(&p->lock);
}

/*
 * How many references to page are currently swapped out?
 * This does not give an exact answer when swap count is continued,
 * but does include the high COUNT_CONTINUED flag to allow for that.
 */
int page_swapcount(struct page *page)
{
	int count = 0;
	struct swap_info_struct *p;
	struct swap_cluster_info *ci;
	swp_entry_t entry;
	unsigned long offset;

	entry.val = page_private(page);
	p = _swap_info_get(entry);
	if (p) {
		offset = swp_offset(entry);
		ci = lock_cluster_or_swap_info(p, offset);
		count = swap_count(p->swap_map[offset]);
		unlock_cluster_or_swap_info(p, ci);
	}
	return count;
}

int __swap_count(swp_entry_t entry)
{
	struct swap_info_struct *si;
	pgoff_t offset = swp_offset(entry);
	int count = 0;

	si = get_swap_device(entry);
	if (si) {
		count = swap_count(si->swap_map[offset]);
		put_swap_device(si);
	}
	return count;
}

static int swap_swapcount(struct swap_info_struct *si, swp_entry_t entry)
{
	int count = 0;
	pgoff_t offset = swp_offset(entry);
	struct swap_cluster_info *ci;

	ci = lock_cluster_or_swap_info(si, offset);
	count = swap_count(si->swap_map[offset]);
	unlock_cluster_or_swap_info(si, ci);
	return count;
}

/*
 * How many references to @entry are currently swapped out?
 * This does not give an exact answer when swap count is continued,
 * but does include the high COUNT_CONTINUED flag to allow for that.
 */
int __swp_swapcount(swp_entry_t entry)
{
	int count = 0;
	struct swap_info_struct *si;

	si = get_swap_device(entry);
	if (si) {
		count = swap_swapcount(si, entry);
		put_swap_device(si);
	}
	return count;
}

/*
 * How many references to @entry are currently swapped out?
 * This considers COUNT_CONTINUED so it returns exact answer.
 */
int swp_swapcount(swp_entry_t entry)
{
	int count, tmp_count, n;
	struct swap_info_struct *p;
	struct swap_cluster_info *ci;
	struct page *page;
	pgoff_t offset;
	unsigned char *map;

	p = _swap_info_get(entry);
	if (!p)
		return 0;

	offset = swp_offset(entry);

	ci = lock_cluster_or_swap_info(p, offset);

	count = swap_count(p->swap_map[offset]);
	if (!(count & COUNT_CONTINUED))
		goto out;

	count &= ~COUNT_CONTINUED;
	n = SWAP_MAP_MAX + 1;

	page = vmalloc_to_page(p->swap_map + offset);
	offset &= ~PAGE_MASK;
	VM_BUG_ON(page_private(page) != SWP_CONTINUED);

	do {
		page = list_next_entry(page, lru);
		map = kmap_atomic(page);
		tmp_count = map[offset];
		kunmap_atomic(map);

		count += (tmp_count & ~COUNT_CONTINUED) * n;
		n *= (SWAP_CONT_MAX + 1);
	} while (tmp_count & COUNT_CONTINUED);
out:
	unlock_cluster_or_swap_info(p, ci);
	return count;
}

static bool swap_page_trans_huge_swapped(struct swap_info_struct *si,
					 swp_entry_t entry)
{
	struct swap_cluster_info *ci;
	unsigned char *map = si->swap_map;
	unsigned long roffset = swp_offset(entry);
	unsigned long offset = round_down(roffset, SWAPFILE_CLUSTER);
	int i;
	bool ret = false;

	ci = lock_cluster_or_swap_info(si, offset);
	if (!ci || !cluster_is_huge(ci)) {
		if (swap_count(map[roffset]))
			ret = true;
		goto unlock_out;
	}
	for (i = 0; i < SWAPFILE_CLUSTER; i++) {
		if (swap_count(map[offset + i])) {
			ret = true;
			break;
		}
	}
unlock_out:
	unlock_cluster_or_swap_info(si, ci);
	return ret;
}

static bool page_swapped(struct page *page)
{
	swp_entry_t entry;
	struct swap_info_struct *si;

	if (!IS_ENABLED(CONFIG_THP_SWAP) || likely(!PageTransCompound(page)))
		return page_swapcount(page) != 0;

	page = compound_head(page);
	entry.val = page_private(page);
	si = _swap_info_get(entry);
	if (si)
		return swap_page_trans_huge_swapped(si, entry);
	return false;
}

static int page_trans_huge_map_swapcount(struct page *page, int *total_mapcount,
					 int *total_swapcount)
{
	int i, map_swapcount, _total_mapcount, _total_swapcount;
	unsigned long offset = 0;
	struct swap_info_struct *si;
	struct swap_cluster_info *ci = NULL;
	unsigned char *map = NULL;
	int mapcount, swapcount = 0;

	/* hugetlbfs shouldn't call it */
	VM_BUG_ON_PAGE(PageHuge(page), page);

	if (!IS_ENABLED(CONFIG_THP_SWAP) || likely(!PageTransCompound(page))) {
		mapcount = page_trans_huge_mapcount(page, total_mapcount);
		if (PageSwapCache(page))
			swapcount = page_swapcount(page);
		if (total_swapcount)
			*total_swapcount = swapcount;
		return mapcount + swapcount;
	}

	page = compound_head(page);

	_total_mapcount = _total_swapcount = map_swapcount = 0;
	if (PageSwapCache(page)) {
		swp_entry_t entry;

		entry.val = page_private(page);
		si = _swap_info_get(entry);
		if (si) {
			map = si->swap_map;
			offset = swp_offset(entry);
		}
	}
	if (map)
		ci = lock_cluster(si, offset);
	for (i = 0; i < HPAGE_PMD_NR; i++) {
		mapcount = atomic_read(&page[i]._mapcount) + 1;
		_total_mapcount += mapcount;
		if (map) {
			swapcount = swap_count(map[offset + i]);
			_total_swapcount += swapcount;
		}
		map_swapcount = max(map_swapcount, mapcount + swapcount);
	}
	unlock_cluster(ci);
	if (PageDoubleMap(page)) {
		map_swapcount -= 1;
		_total_mapcount -= HPAGE_PMD_NR;
	}
	mapcount = compound_mapcount(page);
	map_swapcount += mapcount;
	_total_mapcount += mapcount;
	if (total_mapcount)
		*total_mapcount = _total_mapcount;
	if (total_swapcount)
		*total_swapcount = _total_swapcount;

	return map_swapcount;
}

/*
 * We can write to an anon page without COW if there are no other references
 * to it.  And as a side-effect, free up its swap: because the old content
 * on disk will never be read, and seeking back there to write new content
 * later would only waste time away from clustering.
 *
 * NOTE: total_map_swapcount should not be relied upon by the caller if
 * reuse_swap_page() returns false, but it may be always overwritten
 * (see the other implementation for CONFIG_SWAP=n).
 */
bool reuse_swap_page(struct page *page, int *total_map_swapcount)
{
	int count, total_mapcount, total_swapcount;

	VM_BUG_ON_PAGE(!PageLocked(page), page);
	if (unlikely(PageKsm(page)))
		return false;
	count = page_trans_huge_map_swapcount(page, &total_mapcount,
					      &total_swapcount);
	if (total_map_swapcount)
		*total_map_swapcount = total_mapcount + total_swapcount;
	if (count == 1 && PageSwapCache(page) &&
	    (likely(!PageTransCompound(page)) ||
	     /* The remaining swap count will be freed soon */
	     total_swapcount == page_swapcount(page))) {
		if (!PageWriteback(page)) {
			page = compound_head(page);
			delete_from_swap_cache(page);
			SetPageDirty(page);
		} else {
			swp_entry_t entry;
			struct swap_info_struct *p;

			entry.val = page_private(page);
			p = swap_info_get(entry);
			if (p->flags & SWP_STABLE_WRITES) {
				spin_unlock(&p->lock);
				return false;
			}
			spin_unlock(&p->lock);
		}
	}

	return count <= 1;
}

/*
 * If swap is getting full, or if there are no more mappings of this page,
 * then try_to_free_swap is called to free its swap space.
 */
int try_to_free_swap(struct page *page)
{
	VM_BUG_ON_PAGE(!PageLocked(page), page);

	if (!PageSwapCache(page))
		return 0;
	if (PageWriteback(page))
		return 0;
	if (page_swapped(page))
		return 0;

	/*
	 * Once hibernation has begun to create its image of memory,
	 * there's a danger that one of the calls to try_to_free_swap()
	 * - most probably a call from __try_to_reclaim_swap() while
	 * hibernation is allocating its own swap pages for the image,
	 * but conceivably even a call from memory reclaim - will free
	 * the swap from a page which has already been recorded in the
	 * image as a clean swapcache page, and then reuse its swap for
	 * another page of the image.  On waking from hibernation, the
	 * original page might be freed under memory pressure, then
	 * later read back in from swap, now with the wrong data.
	 *
	 * Hibernation suspends storage while it is writing the image
	 * to disk so check that here.
	 */
	if (pm_suspended_storage())
		return 0;

	page = compound_head(page);
	delete_from_swap_cache(page);
	SetPageDirty(page);
	return 1;
}

/*
 * Free the swap entry like above, but also try to
 * free the page cache entry if it is the last user.
 */
int free_swap_and_cache(swp_entry_t entry)
{
	struct swap_info_struct *p;
	unsigned char count;

	if (non_swap_entry(entry))
		return 1;

	p = _swap_info_get(entry);
	if (p) {
		count = __swap_entry_free(p, entry);
		if (count == SWAP_HAS_CACHE &&
		    !swap_page_trans_huge_swapped(p, entry))
			__try_to_reclaim_swap(p, swp_offset(entry),
					      TTRS_UNMAPPED | TTRS_FULL);
	}
	return p != NULL;
}

#ifdef CONFIG_HIBERNATION
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
	return pte_same(pte_swp_clear_soft_dirty(pte), swp_pte);
}

/*
 * No need to decide whether this PTE shares the swap entry with others,
 * just let do_wp_page work it out if a write is requested later - to
 * force COW, vm_page_prot omits write permission from any private vma.
 */
static int unuse_pte(struct vm_area_struct *vma, pmd_t *pmd,
		unsigned long addr, swp_entry_t entry, struct page *page)
{
	struct page *swapcache;
	spinlock_t *ptl;
	pte_t *pte;
	int ret = 1;

	swapcache = page;
	page = ksm_might_need_to_copy(page, vma, addr);
	if (unlikely(!page))
		return -ENOMEM;

	pte = pte_offset_map_lock(vma->vm_mm, pmd, addr, &ptl);
	if (unlikely(!pte_same_as_swp(*pte, swp_entry_to_pte(entry)))) {
		ret = 0;
		goto out;
	}

	dec_mm_counter(vma->vm_mm, MM_SWAPENTS);
	inc_mm_counter(vma->vm_mm, MM_ANONPAGES);
	get_page(page);
	set_pte_at(vma->vm_mm, addr, pte,
		   pte_mkold(mk_pte(page, vma->vm_page_prot)));
	if (page == swapcache) {
		page_add_anon_rmap(page, vma, addr, false);
	} else { /* ksm created a completely new copy */
		page_add_new_anon_rmap(page, vma, addr, false);
		lru_cache_add_inactive_or_unevictable(page, vma);
	}
	swap_free(entry);
out:
	pte_unmap_unlock(pte, ptl);
	if (page != swapcache) {
		unlock_page(page);
		put_page(page);
	}
	return ret;
}

static int unuse_pte_range(struct vm_area_struct *vma, pmd_t *pmd,
			unsigned long addr, unsigned long end,
			unsigned int type, bool frontswap,
			unsigned long *fs_pages_to_unuse)
{
	struct page *page;
	swp_entry_t entry;
	pte_t *pte;
	struct swap_info_struct *si;
	unsigned long offset;
	int ret = 0;
	volatile unsigned char *swap_map;

	si = swap_info[type];
	pte = pte_offset_map(pmd, addr);
	do {
		if (!is_swap_pte(*pte))
			continue;

		entry = pte_to_swp_entry(*pte);
		if (swp_type(entry) != type)
			continue;

		offset = swp_offset(entry);
		if (frontswap && !frontswap_test(si, offset))
			continue;

		pte_unmap(pte);
		swap_map = &si->swap_map[offset];
		page = lookup_swap_cache(entry, vma, addr);
		if (!page) {
			struct vm_fault vmf = {
				.vma = vma,
				.address = addr,
				.pmd = pmd,
			};

			page = swapin_readahead(entry, GFP_HIGHUSER_MOVABLE,
						&vmf);
		}
		if (!page) {
			if (*swap_map == 0 || *swap_map == SWAP_MAP_BAD)
				goto try_next;
			return -ENOMEM;
		}

		lock_page(page);
		wait_on_page_writeback(page);
		ret = unuse_pte(vma, pmd, addr, entry, page);
		if (ret < 0) {
			unlock_page(page);
			put_page(page);
			goto out;
		}

		try_to_free_swap(page);
		unlock_page(page);
		put_page(page);

		if (*fs_pages_to_unuse && !--(*fs_pages_to_unuse)) {
			ret = FRONTSWAP_PAGES_UNUSED;
			goto out;
		}
try_next:
		pte = pte_offset_map(pmd, addr);
	} while (pte++, addr += PAGE_SIZE, addr != end);
	pte_unmap(pte - 1);

	ret = 0;
out:
	return ret;
}

static inline int unuse_pmd_range(struct vm_area_struct *vma, pud_t *pud,
				unsigned long addr, unsigned long end,
				unsigned int type, bool frontswap,
				unsigned long *fs_pages_to_unuse)
{
	pmd_t *pmd;
	unsigned long next;
	int ret;

	pmd = pmd_offset(pud, addr);
	do {
		cond_resched();
		next = pmd_addr_end(addr, end);
		if (pmd_none_or_trans_huge_or_clear_bad(pmd))
			continue;
		ret = unuse_pte_range(vma, pmd, addr, next, type,
				      frontswap, fs_pages_to_unuse);
		if (ret)
			return ret;
	} while (pmd++, addr = next, addr != end);
	return 0;
}

static inline int unuse_pud_range(struct vm_area_struct *vma, p4d_t *p4d,
				unsigned long addr, unsigned long end,
				unsigned int type, bool frontswap,
				unsigned long *fs_pages_to_unuse)
{
	pud_t *pud;
	unsigned long next;
	int ret;

	pud = pud_offset(p4d, addr);
	do {
		next = pud_addr_end(addr, end);
		if (pud_none_or_clear_bad(pud))
			continue;
		ret = unuse_pmd_range(vma, pud, addr, next, type,
				      frontswap, fs_pages_to_unuse);
		if (ret)
			return ret;
	} while (pud++, addr = next, addr != end);
	return 0;
}

static inline int unuse_p4d_range(struct vm_area_struct *vma, pgd_t *pgd,
				unsigned long addr, unsigned long end,
				unsigned int type, bool frontswap,
				unsigned long *fs_pages_to_unuse)
{
	p4d_t *p4d;
	unsigned long next;
	int ret;

	p4d = p4d_offset(pgd, addr);
	do {
		next = p4d_addr_end(addr, end);
		if (p4d_none_or_clear_bad(p4d))
			continue;
		ret = unuse_pud_range(vma, p4d, addr, next, type,
				      frontswap, fs_pages_to_unuse);
		if (ret)
			return ret;
	} while (p4d++, addr = next, addr != end);
	return 0;
}

static int unuse_vma(struct vm_area_struct *vma, unsigned int type,
		     bool frontswap, unsigned long *fs_pages_to_unuse)
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
		ret = unuse_p4d_range(vma, pgd, addr, next, type,
				      frontswap, fs_pages_to_unuse);
		if (ret)
			return ret;
	} while (pgd++, addr = next, addr != end);
	return 0;
}

static int unuse_mm(struct mm_struct *mm, unsigned int type,
		    bool frontswap, unsigned long *fs_pages_to_unuse)
{
	struct vm_area_struct *vma;
	int ret = 0;

	mmap_read_lock(mm);
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		if (vma->anon_vma) {
			ret = unuse_vma(vma, type, frontswap,
					fs_pages_to_unuse);
			if (ret)
				break;
		}
		cond_resched();
	}
	mmap_read_unlock(mm);
	return ret;
}

/*
 * Scan swap_map (or frontswap_map if frontswap parameter is true)
 * from current position to next entry still in use. Return 0
 * if there are no inuse entries after prev till end of the map.
 */
static unsigned int find_next_to_unuse(struct swap_info_struct *si,
					unsigned int prev, bool frontswap)
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
			if (!frontswap || frontswap_test(si, i))
				break;
		if ((i % LATENCY_LIMIT) == 0)
			cond_resched();
	}

	if (i == si->max)
		i = 0;

	return i;
}

/*
 * If the boolean frontswap is true, only unuse pages_to_unuse pages;
 * pages_to_unuse==0 means all pages; ignored if frontswap is false
 */
int try_to_unuse(unsigned int type, bool frontswap,
		 unsigned long pages_to_unuse)
{
	struct mm_struct *prev_mm;
	struct mm_struct *mm;
	struct list_head *p;
	int retval = 0;
	struct swap_info_struct *si = swap_info[type];
	struct page *page;
	swp_entry_t entry;
	unsigned int i;

	if (!READ_ONCE(si->inuse_pages))
		return 0;

	if (!frontswap)
		pages_to_unuse = 0;

retry:
	retval = shmem_unuse(type, frontswap, &pages_to_unuse);
	if (retval)
		goto out;

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
		retval = unuse_mm(mm, type, frontswap, &pages_to_unuse);

		if (retval) {
			mmput(prev_mm);
			goto out;
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
	       (i = find_next_to_unuse(si, i, frontswap)) != 0) {

		entry = swp_entry(type, i);
		page = find_get_page(swap_address_space(entry), i);
		if (!page)
			continue;

		/*
		 * It is conceivable that a racing task removed this page from
		 * swap cache just before we acquired the page lock. The page
		 * might even be back in swap cache on another swap area. But
		 * that is okay, try_to_free_swap() only removes stale pages.
		 */
		lock_page(page);
		wait_on_page_writeback(page);
		try_to_free_swap(page);
		unlock_page(page);
		put_page(page);

		/*
		 * For frontswap, we just need to unuse pages_to_unuse, if
		 * it was specified. Need not check frontswap again here as
		 * we already zeroed out pages_to_unuse if not frontswap.
		 */
		if (pages_to_unuse && --pages_to_unuse == 0)
			goto out;
	}

	/*
	 * Lets check again to see if there are still swap entries in the map.
	 * If yes, we would need to do retry the unuse logic again.
	 * Under global memory pressure, swap entries can be reinserted back
	 * into process space after the mmlist loop above passes over them.
	 *
	 * Limit the number of retries? No: when mmget_not_zero() above fails,
	 * that mm is likely to be freeing swap from exit_mmap(), which proceeds
	 * at its own independent pace; and even shmem_writepage() could have
	 * been preempted after get_swap_page(), temporarily hiding that swap.
	 * It's easy and robust (though cpu-intensive) just to keep retrying.
	 */
	if (READ_ONCE(si->inuse_pages)) {
		if (!signal_pending(current))
			goto retry;
		retval = -EINTR;
	}
out:
	return (retval == FRONTSWAP_PAGES_UNUSED) ? 0 : retval;
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
 * onto a contiguous range of disk blocks.  An ordered list of swap extents
 * is built at swapon time and is then used at swap_writepage/swap_readpage
 * time for locating where on disk a page belongs.
 *
 * If the swapfile is an S_ISBLK block device, a single extent is installed.
 * This is done so that the main operating code can treat S_ISBLK and S_ISREG
 * swap files identically.
 *
 * Whether the swapdev is an S_ISREG file or an S_ISBLK blockdev, the swap
 * extent list operates in PAGE_SIZE disk blocks.  Both S_ISREG and S_ISBLK
 * swapfiles are handled *identically* after swapon time.
 *
 * For S_ISREG swapfiles, setup_swap_extents() will walk all the file's blocks
 * and will parse them into an ordered extent list, in PAGE_SIZE chunks.  If
 * some stray blocks are found which do not fall within the PAGE_SIZE alignment
 * requirements, they are simply tossed out - we will never use those blocks
 * for swapping.
 *
 * For all swap devices we set S_SWAPFILE across the life of the swapon.  This
 * prevents users from writing to the swap device, which will corrupt memory.
 *
 * The amount of disk space which a single swap extent represents varies.
 * Typically it is in the 1-4 megabyte range.  So we can have hundreds of
 * extents in the list.  To avoid much list walking, we cache the previous
 * search location in `curr_swap_extent', and start new searches from there.
 * This is extremely effective.  The average number of iterations in
 * map_swap_page() has been measured at about 0.3 per page.  - akpm.
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
		if (ret >= 0)
			sis->flags |= SWP_ACTIVATED;
		if (!ret) {
			sis->flags |= SWP_FS_OPS;
			ret = add_swap_extent(sis, 0, sis->max, 0);
			*span = sis->pages;
		}
		return ret;
	}

	return generic_swapfile_activate(sis, swap_file, span);
}

static int swap_node(struct swap_info_struct *p)
{
	struct block_device *bdev;

	if (p->bdev)
		bdev = p->bdev;
	else
		bdev = p->swap_file->f_inode->i_sb->s_bdev;

	return bdev ? bdev->bd_disk->node_id : NUMA_NO_NODE;
}

static void setup_swap_info(struct swap_info_struct *p, int prio,
			    unsigned char *swap_map,
			    struct swap_cluster_info *cluster_info)
{
	int i;

	if (prio >= 0)
		p->prio = prio;
	else
		p->prio = --least_priority;
	/*
	 * the plist prio is negated because plist ordering is
	 * low-to-high, while swap ordering is high-to-low
	 */
	p->list.prio = -p->prio;
	for_each_node(i) {
		if (p->prio >= 0)
			p->avail_lists[i].prio = -p->prio;
		else {
			if (swap_node(p) == i)
				p->avail_lists[i].prio = 1;
			else
				p->avail_lists[i].prio = -p->prio;
		}
	}
	p->swap_map = swap_map;
	p->cluster_info = cluster_info;
}

static void _enable_swap_info(struct swap_info_struct *p)
{
	p->flags |= SWP_WRITEOK | SWP_VALID;
	atomic_long_add(p->pages, &nr_swap_pages);
	total_swap_pages += p->pages;

	assert_spin_locked(&swap_lock);
	/*
	 * both lists are plists, and thus priority ordered.
	 * swap_active_head needs to be priority ordered for swapoff(),
	 * which on removal of any swap_info_struct with an auto-assigned
	 * (i.e. negative) priority increments the auto-assigned priority
	 * of any lower-priority swap_info_structs.
	 * swap_avail_head needs to be priority ordered for get_swap_page(),
	 * which allocates swap pages from the highest available priority
	 * swap_info_struct.
	 */
	plist_add(&p->list, &swap_active_head);
	add_to_avail_list(p);
}

static void enable_swap_info(struct swap_info_struct *p, int prio,
				unsigned char *swap_map,
				struct swap_cluster_info *cluster_info,
				unsigned long *frontswap_map)
{
	frontswap_init(p->type, frontswap_map);
	spin_lock(&swap_lock);
	spin_lock(&p->lock);
	setup_swap_info(p, prio, swap_map, cluster_info);
	spin_unlock(&p->lock);
	spin_unlock(&swap_lock);
	/*
	 * Guarantee swap_map, cluster_info, etc. fields are valid
	 * between get/put_swap_device() if SWP_VALID bit is set
	 */
	synchronize_rcu();
	spin_lock(&swap_lock);
	spin_lock(&p->lock);
	_enable_swap_info(p);
	spin_unlock(&p->lock);
	spin_unlock(&swap_lock);
}

static void reinsert_swap_info(struct swap_info_struct *p)
{
	spin_lock(&swap_lock);
	spin_lock(&p->lock);
	setup_swap_info(p, p->prio, p->swap_map, p->cluster_info);
	_enable_swap_info(p);
	spin_unlock(&p->lock);
	spin_unlock(&swap_lock);
}

bool has_usable_swap(void)
{
	bool ret = true;

	spin_lock(&swap_lock);
	if (plist_head_empty(&swap_active_head))
		ret = false;
	spin_unlock(&swap_lock);
	return ret;
}

SYSCALL_DEFINE1(swapoff, const char __user *, specialfile)
{
	struct swap_info_struct *p = NULL;
	unsigned char *swap_map;
	struct swap_cluster_info *cluster_info;
	unsigned long *frontswap_map;
	struct file *swap_file, *victim;
	struct address_space *mapping;
	struct inode *inode;
	struct filename *pathname;
	int err, found = 0;
	unsigned int old_block_size;

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
	del_from_avail_list(p);
	spin_lock(&p->lock);
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
	err = try_to_unuse(p->type, false, 0); /* force unuse all pages */
	clear_current_oom_origin();

	if (err) {
		/* re-insert swap space back into swap_list */
		reinsert_swap_info(p);
		reenable_swap_slots_cache_unlock();
		goto out_dput;
	}

	reenable_swap_slots_cache_unlock();

	spin_lock(&swap_lock);
	spin_lock(&p->lock);
	p->flags &= ~SWP_VALID;		/* mark swap device as invalid */
	spin_unlock(&p->lock);
	spin_unlock(&swap_lock);
	/*
	 * wait for swap operations protected by get/put_swap_device()
	 * to complete
	 */
	synchronize_rcu();

	flush_work(&p->discard_work);

	destroy_swap_extents(p);
	if (p->flags & SWP_CONTINUED)
		free_swap_count_continuations(p);

	if (!p->bdev || !blk_queue_nonrot(bdev_get_queue(p->bdev)))
		atomic_dec(&nr_rotate_swap);

	mutex_lock(&swapon_mutex);
	spin_lock(&swap_lock);
	spin_lock(&p->lock);
	drain_mmlist();

	/* wait for anyone still in scan_swap_map */
	p->highest_bit = 0;		/* cuts scans short */
	while (p->flags >= SWP_SCANNING) {
		spin_unlock(&p->lock);
		spin_unlock(&swap_lock);
		schedule_timeout_uninterruptible(1);
		spin_lock(&swap_lock);
		spin_lock(&p->lock);
	}

	swap_file = p->swap_file;
	old_block_size = p->old_block_size;
	p->swap_file = NULL;
	p->max = 0;
	swap_map = p->swap_map;
	p->swap_map = NULL;
	cluster_info = p->cluster_info;
	p->cluster_info = NULL;
	frontswap_map = frontswap_map_get(p);
	spin_unlock(&p->lock);
	spin_unlock(&swap_lock);
	arch_swap_invalidate_area(p->type);
	frontswap_invalidate_area(p->type);
	frontswap_map_set(p, NULL);
	mutex_unlock(&swapon_mutex);
	free_percpu(p->percpu_cluster);
	p->percpu_cluster = NULL;
	free_percpu(p->cluster_next_cpu);
	p->cluster_next_cpu = NULL;
	vfree(swap_map);
	kvfree(cluster_info);
	kvfree(frontswap_map);
	/* Destroy swap account information */
	swap_cgroup_swapoff(p->type);
	exit_swap_address_space(p->type);

	inode = mapping->host;
	if (S_ISBLK(inode->i_mode)) {
		struct block_device *bdev = I_BDEV(inode);

		set_blocksize(bdev, old_block_size);
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
	}

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
	unsigned int bytes, inuse;

	if (si == SEQ_START_TOKEN) {
		seq_puts(swap,"Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n");
		return 0;
	}

	bytes = si->pages << (PAGE_SHIFT - 10);
	inuse = si->inuse_pages << (PAGE_SHIFT - 10);

	file = si->swap_file;
	len = seq_file_path(swap, file, " \t\n\\");
	seq_printf(swap, "%*s%s\t%u\t%s%u\t%s%d\n",
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

	spin_lock(&swap_lock);
	for (type = 0; type < nr_swapfiles; type++) {
		if (!(swap_info[type]->flags & SWP_USED))
			break;
	}
	if (type >= MAX_SWAPFILES) {
		spin_unlock(&swap_lock);
		kvfree(p);
		return ERR_PTR(-EPERM);
	}
	if (type >= nr_swapfiles) {
		p->type = type;
		WRITE_ONCE(swap_info[type], p);
		/*
		 * Write swap_info[type] before nr_swapfiles, in case a
		 * racing procfs swap_start() or swap_next() is reading them.
		 * (We never shrink nr_swapfiles, we never free this entry.)
		 */
		smp_wmb();
		WRITE_ONCE(nr_swapfiles, nr_swapfiles + 1);
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
	kvfree(defer);
	spin_lock_init(&p->lock);
	spin_lock_init(&p->cont_lock);

	return p;
}

static int claim_swapfile(struct swap_info_struct *p, struct inode *inode)
{
	int error;

	if (S_ISBLK(inode->i_mode)) {
		p->bdev = blkdev_get_by_dev(inode->i_rdev,
				   FMODE_READ | FMODE_WRITE | FMODE_EXCL, p);
		if (IS_ERR(p->bdev)) {
			error = PTR_ERR(p->bdev);
			p->bdev = NULL;
			return error;
		}
		p->old_block_size = block_size(p->bdev);
		error = set_blocksize(p->bdev, PAGE_SIZE);
		if (error < 0)
			return error;
		/*
		 * Zoned block devices contain zones that have a sequential
		 * write only restriction.  Hence zoned block devices are not
		 * suitable for swapping.  Disallow them here.
		 */
		if (blk_queue_is_zoned(p->bdev->bd_disk->queue))
			return -EINVAL;
		p->flags |= SWP_BLKDEV;
	} else if (S_ISREG(inode->i_mode)) {
		p->bdev = inode->i_sb->s_bdev;
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
__weak unsigned long max_swapfile_size(void)
{
	return generic_max_swapfile_size();
}

static unsigned long read_swap_header(struct swap_info_struct *p,
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

	/* swap partition endianess hack... */
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

	p->lowest_bit  = 1;
	p->cluster_next = 1;
	p->cluster_nr = 0;

	maxpages = max_swapfile_size();
	last_page = swap_header->info.last_page;
	if (!last_page) {
		pr_warn("Empty swap-file\n");
		return 0;
	}
	if (last_page > maxpages) {
		pr_warn("Truncating oversized swap area, only using %luk out of %luk\n",
			maxpages << (PAGE_SHIFT - 10),
			last_page << (PAGE_SHIFT - 10));
	}
	if (maxpages > last_page) {
		maxpages = last_page + 1;
		/* p->max is an unsigned int: don't overflow it */
		if ((unsigned int)maxpages == 0)
			maxpages = UINT_MAX;
	}
	p->highest_bit = maxpages - 1;

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

static int setup_swap_map_and_extents(struct swap_info_struct *p,
					union swap_header *swap_header,
					unsigned char *swap_map,
					struct swap_cluster_info *cluster_info,
					unsigned long maxpages,
					sector_t *span)
{
	unsigned int j, k;
	unsigned int nr_good_pages;
	int nr_extents;
	unsigned long nr_clusters = DIV_ROUND_UP(maxpages, SWAPFILE_CLUSTER);
	unsigned long col = p->cluster_next / SWAPFILE_CLUSTER % SWAP_CLUSTER_COLS;
	unsigned long i, idx;

	nr_good_pages = maxpages - 1;	/* omit header page */

	cluster_list_init(&p->free_clusters);
	cluster_list_init(&p->discard_clusters);

	for (i = 0; i < swap_header->info.nr_badpages; i++) {
		unsigned int page_nr = swap_header->info.badpages[i];
		if (page_nr == 0 || page_nr > swap_header->info.last_page)
			return -EINVAL;
		if (page_nr < maxpages) {
			swap_map[page_nr] = SWAP_MAP_BAD;
			nr_good_pages--;
			/*
			 * Haven't marked the cluster free yet, no list
			 * operation involved
			 */
			inc_cluster_info_page(p, cluster_info, page_nr);
		}
	}

	/* Haven't marked the cluster free yet, no list operation involved */
	for (i = maxpages; i < round_up(maxpages, SWAPFILE_CLUSTER); i++)
		inc_cluster_info_page(p, cluster_info, i);

	if (nr_good_pages) {
		swap_map[0] = SWAP_MAP_BAD;
		/*
		 * Not mark the cluster free yet, no list
		 * operation involved
		 */
		inc_cluster_info_page(p, cluster_info, 0);
		p->max = maxpages;
		p->pages = nr_good_pages;
		nr_extents = setup_swap_extents(p, span);
		if (nr_extents < 0)
			return nr_extents;
		nr_good_pages = p->pages;
	}
	if (!nr_good_pages) {
		pr_warn("Empty swap-file\n");
		return -EINVAL;
	}

	if (!cluster_info)
		return nr_extents;


	/*
	 * Reduce false cache line sharing between cluster_info and
	 * sharing same address space.
	 */
	for (k = 0; k < SWAP_CLUSTER_COLS; k++) {
		j = (k + col) % SWAP_CLUSTER_COLS;
		for (i = 0; i < DIV_ROUND_UP(nr_clusters, SWAP_CLUSTER_COLS); i++) {
			idx = i * SWAP_CLUSTER_COLS + j;
			if (idx >= nr_clusters)
				continue;
			if (cluster_count(&cluster_info[idx]))
				continue;
			cluster_set_flag(&cluster_info[idx], CLUSTER_FLAG_FREE);
			cluster_list_add_tail(&p->free_clusters, cluster_info,
					      idx);
		}
	}
	return nr_extents;
}

/*
 * Helper to sys_swapon determining if a given swap
 * backing device queue supports DISCARD operations.
 */
static bool swap_discardable(struct swap_info_struct *si)
{
	struct request_queue *q = bdev_get_queue(si->bdev);

	if (!q || !blk_queue_discard(q))
		return false;

	return true;
}

SYSCALL_DEFINE2(swapon, const char __user *, specialfile, int, swap_flags)
{
	struct swap_info_struct *p;
	struct filename *name;
	struct file *swap_file = NULL;
	struct address_space *mapping;
	int prio;
	int error;
	union swap_header *swap_header;
	int nr_extents;
	sector_t span;
	unsigned long maxpages;
	unsigned char *swap_map = NULL;
	struct swap_cluster_info *cluster_info = NULL;
	unsigned long *frontswap_map = NULL;
	struct page *page = NULL;
	struct inode *inode = NULL;
	bool inced_nr_rotate_swap = false;

	if (swap_flags & ~SWAP_FLAGS_VALID)
		return -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (!swap_avail_heads)
		return -ENOMEM;

	p = alloc_swap_info();
	if (IS_ERR(p))
		return PTR_ERR(p);

	INIT_WORK(&p->discard_work, swap_discard_work);

	name = getname(specialfile);
	if (IS_ERR(name)) {
		error = PTR_ERR(name);
		name = NULL;
		goto bad_swap;
	}
	swap_file = file_open_name(name, O_RDWR|O_LARGEFILE, 0);
	if (IS_ERR(swap_file)) {
		error = PTR_ERR(swap_file);
		swap_file = NULL;
		goto bad_swap;
	}

	p->swap_file = swap_file;
	mapping = swap_file->f_mapping;
	inode = mapping->host;

	error = claim_swapfile(p, inode);
	if (unlikely(error))
		goto bad_swap;

	inode_lock(inode);
	if (IS_SWAPFILE(inode)) {
		error = -EBUSY;
		goto bad_swap_unlock_inode;
	}

	/*
	 * Read the swap header.
	 */
	if (!mapping->a_ops->readpage) {
		error = -EINVAL;
		goto bad_swap_unlock_inode;
	}
	page = read_mapping_page(mapping, 0, swap_file);
	if (IS_ERR(page)) {
		error = PTR_ERR(page);
		goto bad_swap_unlock_inode;
	}
	swap_header = kmap(page);

	maxpages = read_swap_header(p, swap_header, inode);
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

	if (p->bdev && blk_queue_stable_writes(p->bdev->bd_disk->queue))
		p->flags |= SWP_STABLE_WRITES;

	if (p->bdev && p->bdev->bd_disk->fops->rw_page)
		p->flags |= SWP_SYNCHRONOUS_IO;

	if (p->bdev && blk_queue_nonrot(bdev_get_queue(p->bdev))) {
		int cpu;
		unsigned long ci, nr_cluster;

		p->flags |= SWP_SOLIDSTATE;
		p->cluster_next_cpu = alloc_percpu(unsigned int);
		if (!p->cluster_next_cpu) {
			error = -ENOMEM;
			goto bad_swap_unlock_inode;
		}
		/*
		 * select a random position to start with to help wear leveling
		 * SSD
		 */
		for_each_possible_cpu(cpu) {
			per_cpu(*p->cluster_next_cpu, cpu) =
				1 + prandom_u32_max(p->highest_bit);
		}
		nr_cluster = DIV_ROUND_UP(maxpages, SWAPFILE_CLUSTER);

		cluster_info = kvcalloc(nr_cluster, sizeof(*cluster_info),
					GFP_KERNEL);
		if (!cluster_info) {
			error = -ENOMEM;
			goto bad_swap_unlock_inode;
		}

		for (ci = 0; ci < nr_cluster; ci++)
			spin_lock_init(&((cluster_info + ci)->lock));

		p->percpu_cluster = alloc_percpu(struct percpu_cluster);
		if (!p->percpu_cluster) {
			error = -ENOMEM;
			goto bad_swap_unlock_inode;
		}
		for_each_possible_cpu(cpu) {
			struct percpu_cluster *cluster;
			cluster = per_cpu_ptr(p->percpu_cluster, cpu);
			cluster_set_null(&cluster->index);
		}
	} else {
		atomic_inc(&nr_rotate_swap);
		inced_nr_rotate_swap = true;
	}

	error = swap_cgroup_swapon(p->type, maxpages);
	if (error)
		goto bad_swap_unlock_inode;

	nr_extents = setup_swap_map_and_extents(p, swap_header, swap_map,
		cluster_info, maxpages, &span);
	if (unlikely(nr_extents < 0)) {
		error = nr_extents;
		goto bad_swap_unlock_inode;
	}
	/* frontswap enabled? set up bit-per-page map for frontswap */
	if (IS_ENABLED(CONFIG_FRONTSWAP))
		frontswap_map = kvcalloc(BITS_TO_LONGS(maxpages),
					 sizeof(long),
					 GFP_KERNEL);

	if (p->bdev &&(swap_flags & SWAP_FLAG_DISCARD) && swap_discardable(p)) {
		/*
		 * When discard is enabled for swap with no particular
		 * policy flagged, we set all swap discard flags here in
		 * order to sustain backward compatibility with older
		 * swapon(8) releases.
		 */
		p->flags |= (SWP_DISCARDABLE | SWP_AREA_DISCARD |
			     SWP_PAGE_DISCARD);

		/*
		 * By flagging sys_swapon, a sysadmin can tell us to
		 * either do single-time area discards only, or to just
		 * perform discards for released swap page-clusters.
		 * Now it's time to adjust the p->flags accordingly.
		 */
		if (swap_flags & SWAP_FLAG_DISCARD_ONCE)
			p->flags &= ~SWP_PAGE_DISCARD;
		else if (swap_flags & SWAP_FLAG_DISCARD_PAGES)
			p->flags &= ~SWP_AREA_DISCARD;

		/* issue a swapon-time discard if it's still required */
		if (p->flags & SWP_AREA_DISCARD) {
			int err = discard_swap(p);
			if (unlikely(err))
				pr_err("swapon: discard_swap(%p): %d\n",
					p, err);
		}
	}

	error = init_swap_address_space(p->type, maxpages);
	if (error)
		goto bad_swap_unlock_inode;

	/*
	 * Flush any pending IO and dirty mappings before we start using this
	 * swap device.
	 */
	inode->i_flags |= S_SWAPFILE;
	error = inode_drain_writes(inode);
	if (error) {
		inode->i_flags &= ~S_SWAPFILE;
		goto free_swap_address_space;
	}

	mutex_lock(&swapon_mutex);
	prio = -1;
	if (swap_flags & SWAP_FLAG_PREFER)
		prio =
		  (swap_flags & SWAP_FLAG_PRIO_MASK) >> SWAP_FLAG_PRIO_SHIFT;
	enable_swap_info(p, prio, swap_map, cluster_info, frontswap_map);

	pr_info("Adding %uk swap on %s.  Priority:%d extents:%d across:%lluk %s%s%s%s%s\n",
		p->pages<<(PAGE_SHIFT-10), name->name, p->prio,
		nr_extents, (unsigned long long)span<<(PAGE_SHIFT-10),
		(p->flags & SWP_SOLIDSTATE) ? "SS" : "",
		(p->flags & SWP_DISCARDABLE) ? "D" : "",
		(p->flags & SWP_AREA_DISCARD) ? "s" : "",
		(p->flags & SWP_PAGE_DISCARD) ? "c" : "",
		(frontswap_map) ? "FS" : "");

	mutex_unlock(&swapon_mutex);
	atomic_inc(&proc_poll_event);
	wake_up_interruptible(&proc_poll_wait);

	error = 0;
	goto out;
free_swap_address_space:
	exit_swap_address_space(p->type);
bad_swap_unlock_inode:
	inode_unlock(inode);
bad_swap:
	free_percpu(p->percpu_cluster);
	p->percpu_cluster = NULL;
	free_percpu(p->cluster_next_cpu);
	p->cluster_next_cpu = NULL;
	if (inode && S_ISBLK(inode->i_mode) && p->bdev) {
		set_blocksize(p->bdev, p->old_block_size);
		blkdev_put(p->bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);
	}
	inode = NULL;
	destroy_swap_extents(p);
	swap_cgroup_swapoff(p->type);
	spin_lock(&swap_lock);
	p->swap_file = NULL;
	p->flags = 0;
	spin_unlock(&swap_lock);
	vfree(swap_map);
	kvfree(cluster_info);
	kvfree(frontswap_map);
	if (inced_nr_rotate_swap)
		atomic_dec(&nr_rotate_swap);
	if (swap_file)
		filp_close(swap_file, NULL);
out:
	if (page && !IS_ERR(page)) {
		kunmap(page);
		put_page(page);
	}
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
			nr_to_be_unused += si->inuse_pages;
	}
	val->freeswap = atomic_long_read(&nr_swap_pages) + nr_to_be_unused;
	val->totalswap = total_swap_pages + nr_to_be_unused;
	spin_unlock(&swap_lock);
}

/*
 * Verify that a swap entry is valid and increment its swap map count.
 *
 * Returns error code in following case.
 * - success -> 0
 * - swp_entry is invalid -> EINVAL
 * - swp_entry is migration entry -> EINVAL
 * - swap-cache reference is requested but there is already one. -> EEXIST
 * - swap-cache reference is requested but the entry is not used. -> ENOENT
 * - swap-mapped reference requested but needs continued swap count. -> ENOMEM
 */
static int __swap_duplicate(swp_entry_t entry, unsigned char usage)
{
	struct swap_info_struct *p;
	struct swap_cluster_info *ci;
	unsigned long offset;
	unsigned char count;
	unsigned char has_cache;
	int err;

	p = get_swap_device(entry);
	if (!p)
		return -EINVAL;

	offset = swp_offset(entry);
	ci = lock_cluster_or_swap_info(p, offset);

	count = p->swap_map[offset];

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
	err = 0;

	if (usage == SWAP_HAS_CACHE) {

		/* set SWAP_HAS_CACHE if there is no cache and entry is used */
		if (!has_cache && count)
			has_cache = SWAP_HAS_CACHE;
		else if (has_cache)		/* someone else added cache */
			err = -EEXIST;
		else				/* no users remaining */
			err = -ENOENT;

	} else if (count || has_cache) {

		if ((count & ~COUNT_CONTINUED) < SWAP_MAP_MAX)
			count += usage;
		else if ((count & ~COUNT_CONTINUED) > SWAP_MAP_MAX)
			err = -EINVAL;
		else if (swap_count_continued(p, offset, count))
			count = COUNT_CONTINUED;
		else
			err = -ENOMEM;
	} else
		err = -ENOENT;			/* unused swap entry */

	WRITE_ONCE(p->swap_map[offset], count | has_cache);

unlock_out:
	unlock_cluster_or_swap_info(p, ci);
	if (p)
		put_swap_device(p);
	return err;
}

/*
 * Help swapoff by noting that swap entry belongs to shmem/tmpfs
 * (in which case its reference count is never incremented).
 */
void swap_shmem_alloc(swp_entry_t entry)
{
	__swap_duplicate(entry, SWAP_MAP_SHMEM);
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

	while (!err && __swap_duplicate(entry, 1) == -ENOMEM)
		err = add_swap_count_continuation(entry, GFP_ATOMIC);
	return err;
}

/*
 * @entry: swap entry for which we allocate swap cache.
 *
 * Called when allocating swap cache for existing swap entry,
 * This can return error codes. Returns 0 at success.
 * -EEXIST means there is a swap cache.
 * Note: return code is different from swap_duplicate().
 */
int swapcache_prepare(swp_entry_t entry)
{
	return __swap_duplicate(entry, SWAP_HAS_CACHE);
}

struct swap_info_struct *swp_swap_info(swp_entry_t entry)
{
	return swap_type_to_swap_info(swp_type(entry));
}

struct swap_info_struct *page_swap_info(struct page *page)
{
	swp_entry_t entry = { .val = page_private(page) };
	return swp_swap_info(entry);
}

/*
 * out-of-line __page_file_ methods to avoid include hell.
 */
struct address_space *__page_file_mapping(struct page *page)
{
	return page_swap_info(page)->swap_file->f_mapping;
}
EXPORT_SYMBOL_GPL(__page_file_mapping);

pgoff_t __page_file_index(struct page *page)
{
	swp_entry_t swap = { .val = page_private(page) };
	return swp_offset(swap);
}
EXPORT_SYMBOL_GPL(__page_file_index);

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

	/*
	 * We are fortunate that although vmalloc_to_page uses pte_offset_map,
	 * no architecture is using highmem pages for kernel page tables: so it
	 * will not corrupt the GFP_ATOMIC caller's atomic page table kmaps.
	 */
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

		map = kmap_atomic(list_page) + offset;
		count = *map;
		kunmap_atomic(map);

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
	map = kmap_atomic(page) + offset;

	if (count == SWAP_MAP_MAX)	/* initial increment from swap_map */
		goto init_map;		/* jump over SWAP_CONT_MAX checks */

	if (count == (SWAP_MAP_MAX | COUNT_CONTINUED)) { /* incrementing */
		/*
		 * Think of how you add 1 to 999
		 */
		while (*map == (SWAP_CONT_MAX | COUNT_CONTINUED)) {
			kunmap_atomic(map);
			page = list_next_entry(page, lru);
			BUG_ON(page == head);
			map = kmap_atomic(page) + offset;
		}
		if (*map == SWAP_CONT_MAX) {
			kunmap_atomic(map);
			page = list_next_entry(page, lru);
			if (page == head) {
				ret = false;	/* add count continuation */
				goto out;
			}
			map = kmap_atomic(page) + offset;
init_map:		*map = 0;		/* we didn't zero the page */
		}
		*map += 1;
		kunmap_atomic(map);
		while ((page = list_prev_entry(page, lru)) != head) {
			map = kmap_atomic(page) + offset;
			*map = COUNT_CONTINUED;
			kunmap_atomic(map);
		}
		ret = true;			/* incremented */

	} else {				/* decrementing */
		/*
		 * Think of how you subtract 1 from 1000
		 */
		BUG_ON(count != COUNT_CONTINUED);
		while (*map == COUNT_CONTINUED) {
			kunmap_atomic(map);
			page = list_next_entry(page, lru);
			BUG_ON(page == head);
			map = kmap_atomic(page) + offset;
		}
		BUG_ON(*map == 0);
		*map -= 1;
		if (*map == 0)
			count = 0;
		kunmap_atomic(map);
		while ((page = list_prev_entry(page, lru)) != head) {
			map = kmap_atomic(page) + offset;
			*map = SWAP_CONT_MAX | count;
			count = COUNT_CONTINUED;
			kunmap_atomic(map);
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
void cgroup_throttle_swaprate(struct page *page, gfp_t gfp_mask)
{
	struct swap_info_struct *si, *next;
	int nid = page_to_nid(page);

	if (!(gfp_mask & __GFP_IO))
		return;

	if (!blk_cgroup_congested())
		return;

	/*
	 * We've already scheduled a throttle, avoid taking the global swap
	 * lock.
	 */
	if (current->throttle_queue)
		return;

	spin_lock(&swap_avail_lock);
	plist_for_each_entry_safe(si, next, &swap_avail_heads[nid],
				  avail_lists[nid]) {
		if (si->bdev) {
			blkcg_schedule_throttle(bdev_get_queue(si->bdev), true);
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

	return 0;
}
subsys_initcall(swapfile_init);
