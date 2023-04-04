// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011, 2012 STRATO.  All rights reserved.
 */

#include <linux/blkdev.h>
#include <linux/ratelimit.h>
#include <linux/sched/mm.h>
#include <crypto/hash.h>
#include "ctree.h"
#include "discard.h"
#include "volumes.h"
#include "disk-io.h"
#include "ordered-data.h"
#include "transaction.h"
#include "backref.h"
#include "extent_io.h"
#include "dev-replace.h"
#include "check-integrity.h"
#include "rcu-string.h"
#include "raid56.h"
#include "block-group.h"
#include "zoned.h"

/*
 * This is only the first step towards a full-features scrub. It reads all
 * extent and super block and verifies the checksums. In case a bad checksum
 * is found or the extent cannot be read, good data will be written back if
 * any can be found.
 *
 * Future enhancements:
 *  - In case an unrepairable extent is encountered, track which files are
 *    affected and report them
 *  - track and record media errors, throw out bad devices
 *  - add a mode to also read unallocated space
 */

struct scrub_block;
struct scrub_ctx;

/*
 * The following three values only influence the performance.
 *
 * The last one configures the number of parallel and outstanding I/O
 * operations. The first one configures an upper limit for the number
 * of (dynamically allocated) pages that are added to a bio.
 */
#define SCRUB_SECTORS_PER_BIO	32	/* 128KiB per bio for 4KiB pages */
#define SCRUB_BIOS_PER_SCTX	64	/* 8MiB per device in flight for 4KiB pages */

/*
 * The following value times PAGE_SIZE needs to be large enough to match the
 * largest node/leaf/sector size that shall be supported.
 */
#define SCRUB_MAX_SECTORS_PER_BLOCK	(BTRFS_MAX_METADATA_BLOCKSIZE / SZ_4K)

#define SCRUB_MAX_PAGES			(DIV_ROUND_UP(BTRFS_MAX_METADATA_BLOCKSIZE, PAGE_SIZE))

struct scrub_recover {
	refcount_t		refs;
	struct btrfs_io_context	*bioc;
	u64			map_length;
};

struct scrub_sector {
	struct scrub_block	*sblock;
	struct list_head	list;
	u64			flags;  /* extent flags */
	u64			generation;
	/* Offset in bytes to @sblock. */
	u32			offset;
	atomic_t		refs;
	unsigned int		have_csum:1;
	unsigned int		io_error:1;
	u8			csum[BTRFS_CSUM_SIZE];

	struct scrub_recover	*recover;
};

struct scrub_bio {
	int			index;
	struct scrub_ctx	*sctx;
	struct btrfs_device	*dev;
	struct bio		*bio;
	blk_status_t		status;
	u64			logical;
	u64			physical;
	struct scrub_sector	*sectors[SCRUB_SECTORS_PER_BIO];
	int			sector_count;
	int			next_free;
	struct work_struct	work;
};

struct scrub_block {
	/*
	 * Each page will have its page::private used to record the logical
	 * bytenr.
	 */
	struct page		*pages[SCRUB_MAX_PAGES];
	struct scrub_sector	*sectors[SCRUB_MAX_SECTORS_PER_BLOCK];
	struct btrfs_device	*dev;
	/* Logical bytenr of the sblock */
	u64			logical;
	u64			physical;
	u64			physical_for_dev_replace;
	/* Length of sblock in bytes */
	u32			len;
	int			sector_count;
	int			mirror_num;

	atomic_t		outstanding_sectors;
	refcount_t		refs; /* free mem on transition to zero */
	struct scrub_ctx	*sctx;
	struct scrub_parity	*sparity;
	struct {
		unsigned int	header_error:1;
		unsigned int	checksum_error:1;
		unsigned int	no_io_error_seen:1;
		unsigned int	generation_error:1; /* also sets header_error */

		/* The following is for the data used to check parity */
		/* It is for the data with checksum */
		unsigned int	data_corrected:1;
	};
	struct work_struct	work;
};

/* Used for the chunks with parity stripe such RAID5/6 */
struct scrub_parity {
	struct scrub_ctx	*sctx;

	struct btrfs_device	*scrub_dev;

	u64			logic_start;

	u64			logic_end;

	int			nsectors;

	u32			stripe_len;

	refcount_t		refs;

	struct list_head	sectors_list;

	/* Work of parity check and repair */
	struct work_struct	work;

	/* Mark the parity blocks which have data */
	unsigned long		dbitmap;

	/*
	 * Mark the parity blocks which have data, but errors happen when
	 * read data or check data
	 */
	unsigned long		ebitmap;
};

struct scrub_ctx {
	struct scrub_bio	*bios[SCRUB_BIOS_PER_SCTX];
	struct btrfs_fs_info	*fs_info;
	int			first_free;
	int			curr;
	atomic_t		bios_in_flight;
	atomic_t		workers_pending;
	spinlock_t		list_lock;
	wait_queue_head_t	list_wait;
	struct list_head	csum_list;
	atomic_t		cancel_req;
	int			readonly;
	int			sectors_per_bio;

	/* State of IO submission throttling affecting the associated device */
	ktime_t			throttle_deadline;
	u64			throttle_sent;

	int			is_dev_replace;
	u64			write_pointer;

	struct scrub_bio        *wr_curr_bio;
	struct mutex            wr_lock;
	struct btrfs_device     *wr_tgtdev;
	bool                    flush_all_writes;

	/*
	 * statistics
	 */
	struct btrfs_scrub_progress stat;
	spinlock_t		stat_lock;

	/*
	 * Use a ref counter to avoid use-after-free issues. Scrub workers
	 * decrement bios_in_flight and workers_pending and then do a wakeup
	 * on the list_wait wait queue. We must ensure the main scrub task
	 * doesn't free the scrub context before or while the workers are
	 * doing the wakeup() call.
	 */
	refcount_t              refs;
};

struct scrub_warning {
	struct btrfs_path	*path;
	u64			extent_item_size;
	const char		*errstr;
	u64			physical;
	u64			logical;
	struct btrfs_device	*dev;
};

struct full_stripe_lock {
	struct rb_node node;
	u64 logical;
	u64 refs;
	struct mutex mutex;
};

#ifndef CONFIG_64BIT
/* This structure is for archtectures whose (void *) is smaller than u64 */
struct scrub_page_private {
	u64 logical;
};
#endif

static int attach_scrub_page_private(struct page *page, u64 logical)
{
#ifdef CONFIG_64BIT
	attach_page_private(page, (void *)logical);
	return 0;
#else
	struct scrub_page_private *spp;

	spp = kmalloc(sizeof(*spp), GFP_KERNEL);
	if (!spp)
		return -ENOMEM;
	spp->logical = logical;
	attach_page_private(page, (void *)spp);
	return 0;
#endif
}

static void detach_scrub_page_private(struct page *page)
{
#ifdef CONFIG_64BIT
	detach_page_private(page);
	return;
#else
	struct scrub_page_private *spp;

	spp = detach_page_private(page);
	kfree(spp);
	return;
#endif
}

static struct scrub_block *alloc_scrub_block(struct scrub_ctx *sctx,
					     struct btrfs_device *dev,
					     u64 logical, u64 physical,
					     u64 physical_for_dev_replace,
					     int mirror_num)
{
	struct scrub_block *sblock;

	sblock = kzalloc(sizeof(*sblock), GFP_KERNEL);
	if (!sblock)
		return NULL;
	refcount_set(&sblock->refs, 1);
	sblock->sctx = sctx;
	sblock->logical = logical;
	sblock->physical = physical;
	sblock->physical_for_dev_replace = physical_for_dev_replace;
	sblock->dev = dev;
	sblock->mirror_num = mirror_num;
	sblock->no_io_error_seen = 1;
	/*
	 * Scrub_block::pages will be allocated at alloc_scrub_sector() when
	 * the corresponding page is not allocated.
	 */
	return sblock;
}

/*
 * Allocate a new scrub sector and attach it to @sblock.
 *
 * Will also allocate new pages for @sblock if needed.
 */
static struct scrub_sector *alloc_scrub_sector(struct scrub_block *sblock,
					       u64 logical, gfp_t gfp)
{
	const pgoff_t page_index = (logical - sblock->logical) >> PAGE_SHIFT;
	struct scrub_sector *ssector;

	/* We must never have scrub_block exceed U32_MAX in size. */
	ASSERT(logical - sblock->logical < U32_MAX);

	ssector = kzalloc(sizeof(*ssector), gfp);
	if (!ssector)
		return NULL;

	/* Allocate a new page if the slot is not allocated */
	if (!sblock->pages[page_index]) {
		int ret;

		sblock->pages[page_index] = alloc_page(gfp);
		if (!sblock->pages[page_index]) {
			kfree(ssector);
			return NULL;
		}
		ret = attach_scrub_page_private(sblock->pages[page_index],
				sblock->logical + (page_index << PAGE_SHIFT));
		if (ret < 0) {
			kfree(ssector);
			__free_page(sblock->pages[page_index]);
			sblock->pages[page_index] = NULL;
			return NULL;
		}
	}

	atomic_set(&ssector->refs, 1);
	ssector->sblock = sblock;
	/* The sector to be added should not be used */
	ASSERT(sblock->sectors[sblock->sector_count] == NULL);
	ssector->offset = logical - sblock->logical;

	/* The sector count must be smaller than the limit */
	ASSERT(sblock->sector_count < SCRUB_MAX_SECTORS_PER_BLOCK);

	sblock->sectors[sblock->sector_count] = ssector;
	sblock->sector_count++;
	sblock->len += sblock->sctx->fs_info->sectorsize;

	return ssector;
}

static struct page *scrub_sector_get_page(struct scrub_sector *ssector)
{
	struct scrub_block *sblock = ssector->sblock;
	pgoff_t index;
	/*
	 * When calling this function, ssector must be alreaday attached to the
	 * parent sblock.
	 */
	ASSERT(sblock);

	/* The range should be inside the sblock range */
	ASSERT(ssector->offset < sblock->len);

	index = ssector->offset >> PAGE_SHIFT;
	ASSERT(index < SCRUB_MAX_PAGES);
	ASSERT(sblock->pages[index]);
	ASSERT(PagePrivate(sblock->pages[index]));
	return sblock->pages[index];
}

static unsigned int scrub_sector_get_page_offset(struct scrub_sector *ssector)
{
	struct scrub_block *sblock = ssector->sblock;

	/*
	 * When calling this function, ssector must be already attached to the
	 * parent sblock.
	 */
	ASSERT(sblock);

	/* The range should be inside the sblock range */
	ASSERT(ssector->offset < sblock->len);

	return offset_in_page(ssector->offset);
}

static char *scrub_sector_get_kaddr(struct scrub_sector *ssector)
{
	return page_address(scrub_sector_get_page(ssector)) +
	       scrub_sector_get_page_offset(ssector);
}

static int bio_add_scrub_sector(struct bio *bio, struct scrub_sector *ssector,
				unsigned int len)
{
	return bio_add_page(bio, scrub_sector_get_page(ssector), len,
			    scrub_sector_get_page_offset(ssector));
}

static int scrub_setup_recheck_block(struct scrub_block *original_sblock,
				     struct scrub_block *sblocks_for_recheck[]);
static void scrub_recheck_block(struct btrfs_fs_info *fs_info,
				struct scrub_block *sblock,
				int retry_failed_mirror);
static void scrub_recheck_block_checksum(struct scrub_block *sblock);
static int scrub_repair_block_from_good_copy(struct scrub_block *sblock_bad,
					     struct scrub_block *sblock_good);
static int scrub_repair_sector_from_good_copy(struct scrub_block *sblock_bad,
					    struct scrub_block *sblock_good,
					    int sector_num, int force_write);
static void scrub_write_block_to_dev_replace(struct scrub_block *sblock);
static int scrub_write_sector_to_dev_replace(struct scrub_block *sblock,
					     int sector_num);
static int scrub_checksum_data(struct scrub_block *sblock);
static int scrub_checksum_tree_block(struct scrub_block *sblock);
static int scrub_checksum_super(struct scrub_block *sblock);
static void scrub_block_put(struct scrub_block *sblock);
static void scrub_sector_get(struct scrub_sector *sector);
static void scrub_sector_put(struct scrub_sector *sector);
static void scrub_parity_get(struct scrub_parity *sparity);
static void scrub_parity_put(struct scrub_parity *sparity);
static int scrub_sectors(struct scrub_ctx *sctx, u64 logical, u32 len,
			 u64 physical, struct btrfs_device *dev, u64 flags,
			 u64 gen, int mirror_num, u8 *csum,
			 u64 physical_for_dev_replace);
static void scrub_bio_end_io(struct bio *bio);
static void scrub_bio_end_io_worker(struct work_struct *work);
static void scrub_block_complete(struct scrub_block *sblock);
static void scrub_find_good_copy(struct btrfs_fs_info *fs_info,
				 u64 extent_logical, u32 extent_len,
				 u64 *extent_physical,
				 struct btrfs_device **extent_dev,
				 int *extent_mirror_num);
static int scrub_add_sector_to_wr_bio(struct scrub_ctx *sctx,
				      struct scrub_sector *sector);
static void scrub_wr_submit(struct scrub_ctx *sctx);
static void scrub_wr_bio_end_io(struct bio *bio);
static void scrub_wr_bio_end_io_worker(struct work_struct *work);
static void scrub_put_ctx(struct scrub_ctx *sctx);

static inline int scrub_is_page_on_raid56(struct scrub_sector *sector)
{
	return sector->recover &&
	       (sector->recover->bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK);
}

static void scrub_pending_bio_inc(struct scrub_ctx *sctx)
{
	refcount_inc(&sctx->refs);
	atomic_inc(&sctx->bios_in_flight);
}

static void scrub_pending_bio_dec(struct scrub_ctx *sctx)
{
	atomic_dec(&sctx->bios_in_flight);
	wake_up(&sctx->list_wait);
	scrub_put_ctx(sctx);
}

static void __scrub_blocked_if_needed(struct btrfs_fs_info *fs_info)
{
	while (atomic_read(&fs_info->scrub_pause_req)) {
		mutex_unlock(&fs_info->scrub_lock);
		wait_event(fs_info->scrub_pause_wait,
		   atomic_read(&fs_info->scrub_pause_req) == 0);
		mutex_lock(&fs_info->scrub_lock);
	}
}

static void scrub_pause_on(struct btrfs_fs_info *fs_info)
{
	atomic_inc(&fs_info->scrubs_paused);
	wake_up(&fs_info->scrub_pause_wait);
}

static void scrub_pause_off(struct btrfs_fs_info *fs_info)
{
	mutex_lock(&fs_info->scrub_lock);
	__scrub_blocked_if_needed(fs_info);
	atomic_dec(&fs_info->scrubs_paused);
	mutex_unlock(&fs_info->scrub_lock);

	wake_up(&fs_info->scrub_pause_wait);
}

static void scrub_blocked_if_needed(struct btrfs_fs_info *fs_info)
{
	scrub_pause_on(fs_info);
	scrub_pause_off(fs_info);
}

/*
 * Insert new full stripe lock into full stripe locks tree
 *
 * Return pointer to existing or newly inserted full_stripe_lock structure if
 * everything works well.
 * Return ERR_PTR(-ENOMEM) if we failed to allocate memory
 *
 * NOTE: caller must hold full_stripe_locks_root->lock before calling this
 * function
 */
static struct full_stripe_lock *insert_full_stripe_lock(
		struct btrfs_full_stripe_locks_tree *locks_root,
		u64 fstripe_logical)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct full_stripe_lock *entry;
	struct full_stripe_lock *ret;

	lockdep_assert_held(&locks_root->lock);

	p = &locks_root->root.rb_node;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct full_stripe_lock, node);
		if (fstripe_logical < entry->logical) {
			p = &(*p)->rb_left;
		} else if (fstripe_logical > entry->logical) {
			p = &(*p)->rb_right;
		} else {
			entry->refs++;
			return entry;
		}
	}

	/*
	 * Insert new lock.
	 */
	ret = kmalloc(sizeof(*ret), GFP_KERNEL);
	if (!ret)
		return ERR_PTR(-ENOMEM);
	ret->logical = fstripe_logical;
	ret->refs = 1;
	mutex_init(&ret->mutex);

	rb_link_node(&ret->node, parent, p);
	rb_insert_color(&ret->node, &locks_root->root);
	return ret;
}

/*
 * Search for a full stripe lock of a block group
 *
 * Return pointer to existing full stripe lock if found
 * Return NULL if not found
 */
static struct full_stripe_lock *search_full_stripe_lock(
		struct btrfs_full_stripe_locks_tree *locks_root,
		u64 fstripe_logical)
{
	struct rb_node *node;
	struct full_stripe_lock *entry;

	lockdep_assert_held(&locks_root->lock);

	node = locks_root->root.rb_node;
	while (node) {
		entry = rb_entry(node, struct full_stripe_lock, node);
		if (fstripe_logical < entry->logical)
			node = node->rb_left;
		else if (fstripe_logical > entry->logical)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

/*
 * Helper to get full stripe logical from a normal bytenr.
 *
 * Caller must ensure @cache is a RAID56 block group.
 */
static u64 get_full_stripe_logical(struct btrfs_block_group *cache, u64 bytenr)
{
	u64 ret;

	/*
	 * Due to chunk item size limit, full stripe length should not be
	 * larger than U32_MAX. Just a sanity check here.
	 */
	WARN_ON_ONCE(cache->full_stripe_len >= U32_MAX);

	/*
	 * round_down() can only handle power of 2, while RAID56 full
	 * stripe length can be 64KiB * n, so we need to manually round down.
	 */
	ret = div64_u64(bytenr - cache->start, cache->full_stripe_len) *
			cache->full_stripe_len + cache->start;
	return ret;
}

/*
 * Lock a full stripe to avoid concurrency of recovery and read
 *
 * It's only used for profiles with parities (RAID5/6), for other profiles it
 * does nothing.
 *
 * Return 0 if we locked full stripe covering @bytenr, with a mutex held.
 * So caller must call unlock_full_stripe() at the same context.
 *
 * Return <0 if encounters error.
 */
static int lock_full_stripe(struct btrfs_fs_info *fs_info, u64 bytenr,
			    bool *locked_ret)
{
	struct btrfs_block_group *bg_cache;
	struct btrfs_full_stripe_locks_tree *locks_root;
	struct full_stripe_lock *existing;
	u64 fstripe_start;
	int ret = 0;

	*locked_ret = false;
	bg_cache = btrfs_lookup_block_group(fs_info, bytenr);
	if (!bg_cache) {
		ASSERT(0);
		return -ENOENT;
	}

	/* Profiles not based on parity don't need full stripe lock */
	if (!(bg_cache->flags & BTRFS_BLOCK_GROUP_RAID56_MASK))
		goto out;
	locks_root = &bg_cache->full_stripe_locks_root;

	fstripe_start = get_full_stripe_logical(bg_cache, bytenr);

	/* Now insert the full stripe lock */
	mutex_lock(&locks_root->lock);
	existing = insert_full_stripe_lock(locks_root, fstripe_start);
	mutex_unlock(&locks_root->lock);
	if (IS_ERR(existing)) {
		ret = PTR_ERR(existing);
		goto out;
	}
	mutex_lock(&existing->mutex);
	*locked_ret = true;
out:
	btrfs_put_block_group(bg_cache);
	return ret;
}

/*
 * Unlock a full stripe.
 *
 * NOTE: Caller must ensure it's the same context calling corresponding
 * lock_full_stripe().
 *
 * Return 0 if we unlock full stripe without problem.
 * Return <0 for error
 */
static int unlock_full_stripe(struct btrfs_fs_info *fs_info, u64 bytenr,
			      bool locked)
{
	struct btrfs_block_group *bg_cache;
	struct btrfs_full_stripe_locks_tree *locks_root;
	struct full_stripe_lock *fstripe_lock;
	u64 fstripe_start;
	bool freeit = false;
	int ret = 0;

	/* If we didn't acquire full stripe lock, no need to continue */
	if (!locked)
		return 0;

	bg_cache = btrfs_lookup_block_group(fs_info, bytenr);
	if (!bg_cache) {
		ASSERT(0);
		return -ENOENT;
	}
	if (!(bg_cache->flags & BTRFS_BLOCK_GROUP_RAID56_MASK))
		goto out;

	locks_root = &bg_cache->full_stripe_locks_root;
	fstripe_start = get_full_stripe_logical(bg_cache, bytenr);

	mutex_lock(&locks_root->lock);
	fstripe_lock = search_full_stripe_lock(locks_root, fstripe_start);
	/* Unpaired unlock_full_stripe() detected */
	if (!fstripe_lock) {
		WARN_ON(1);
		ret = -ENOENT;
		mutex_unlock(&locks_root->lock);
		goto out;
	}

	if (fstripe_lock->refs == 0) {
		WARN_ON(1);
		btrfs_warn(fs_info, "full stripe lock at %llu refcount underflow",
			fstripe_lock->logical);
	} else {
		fstripe_lock->refs--;
	}

	if (fstripe_lock->refs == 0) {
		rb_erase(&fstripe_lock->node, &locks_root->root);
		freeit = true;
	}
	mutex_unlock(&locks_root->lock);

	mutex_unlock(&fstripe_lock->mutex);
	if (freeit)
		kfree(fstripe_lock);
out:
	btrfs_put_block_group(bg_cache);
	return ret;
}

static void scrub_free_csums(struct scrub_ctx *sctx)
{
	while (!list_empty(&sctx->csum_list)) {
		struct btrfs_ordered_sum *sum;
		sum = list_first_entry(&sctx->csum_list,
				       struct btrfs_ordered_sum, list);
		list_del(&sum->list);
		kfree(sum);
	}
}

static noinline_for_stack void scrub_free_ctx(struct scrub_ctx *sctx)
{
	int i;

	if (!sctx)
		return;

	/* this can happen when scrub is cancelled */
	if (sctx->curr != -1) {
		struct scrub_bio *sbio = sctx->bios[sctx->curr];

		for (i = 0; i < sbio->sector_count; i++)
			scrub_block_put(sbio->sectors[i]->sblock);
		bio_put(sbio->bio);
	}

	for (i = 0; i < SCRUB_BIOS_PER_SCTX; ++i) {
		struct scrub_bio *sbio = sctx->bios[i];

		if (!sbio)
			break;
		kfree(sbio);
	}

	kfree(sctx->wr_curr_bio);
	scrub_free_csums(sctx);
	kfree(sctx);
}

static void scrub_put_ctx(struct scrub_ctx *sctx)
{
	if (refcount_dec_and_test(&sctx->refs))
		scrub_free_ctx(sctx);
}

static noinline_for_stack struct scrub_ctx *scrub_setup_ctx(
		struct btrfs_fs_info *fs_info, int is_dev_replace)
{
	struct scrub_ctx *sctx;
	int		i;

	sctx = kzalloc(sizeof(*sctx), GFP_KERNEL);
	if (!sctx)
		goto nomem;
	refcount_set(&sctx->refs, 1);
	sctx->is_dev_replace = is_dev_replace;
	sctx->sectors_per_bio = SCRUB_SECTORS_PER_BIO;
	sctx->curr = -1;
	sctx->fs_info = fs_info;
	INIT_LIST_HEAD(&sctx->csum_list);
	for (i = 0; i < SCRUB_BIOS_PER_SCTX; ++i) {
		struct scrub_bio *sbio;

		sbio = kzalloc(sizeof(*sbio), GFP_KERNEL);
		if (!sbio)
			goto nomem;
		sctx->bios[i] = sbio;

		sbio->index = i;
		sbio->sctx = sctx;
		sbio->sector_count = 0;
		INIT_WORK(&sbio->work, scrub_bio_end_io_worker);

		if (i != SCRUB_BIOS_PER_SCTX - 1)
			sctx->bios[i]->next_free = i + 1;
		else
			sctx->bios[i]->next_free = -1;
	}
	sctx->first_free = 0;
	atomic_set(&sctx->bios_in_flight, 0);
	atomic_set(&sctx->workers_pending, 0);
	atomic_set(&sctx->cancel_req, 0);

	spin_lock_init(&sctx->list_lock);
	spin_lock_init(&sctx->stat_lock);
	init_waitqueue_head(&sctx->list_wait);
	sctx->throttle_deadline = 0;

	WARN_ON(sctx->wr_curr_bio != NULL);
	mutex_init(&sctx->wr_lock);
	sctx->wr_curr_bio = NULL;
	if (is_dev_replace) {
		WARN_ON(!fs_info->dev_replace.tgtdev);
		sctx->wr_tgtdev = fs_info->dev_replace.tgtdev;
		sctx->flush_all_writes = false;
	}

	return sctx;

nomem:
	scrub_free_ctx(sctx);
	return ERR_PTR(-ENOMEM);
}

static int scrub_print_warning_inode(u64 inum, u64 offset, u64 root,
				     void *warn_ctx)
{
	u32 nlink;
	int ret;
	int i;
	unsigned nofs_flag;
	struct extent_buffer *eb;
	struct btrfs_inode_item *inode_item;
	struct scrub_warning *swarn = warn_ctx;
	struct btrfs_fs_info *fs_info = swarn->dev->fs_info;
	struct inode_fs_paths *ipath = NULL;
	struct btrfs_root *local_root;
	struct btrfs_key key;

	local_root = btrfs_get_fs_root(fs_info, root, true);
	if (IS_ERR(local_root)) {
		ret = PTR_ERR(local_root);
		goto err;
	}

	/*
	 * this makes the path point to (inum INODE_ITEM ioff)
	 */
	key.objectid = inum;
	key.type = BTRFS_INODE_ITEM_KEY;
	key.offset = 0;

	ret = btrfs_search_slot(NULL, local_root, &key, swarn->path, 0, 0);
	if (ret) {
		btrfs_put_root(local_root);
		btrfs_release_path(swarn->path);
		goto err;
	}

	eb = swarn->path->nodes[0];
	inode_item = btrfs_item_ptr(eb, swarn->path->slots[0],
					struct btrfs_inode_item);
	nlink = btrfs_inode_nlink(eb, inode_item);
	btrfs_release_path(swarn->path);

	/*
	 * init_path might indirectly call vmalloc, or use GFP_KERNEL. Scrub
	 * uses GFP_NOFS in this context, so we keep it consistent but it does
	 * not seem to be strictly necessary.
	 */
	nofs_flag = memalloc_nofs_save();
	ipath = init_ipath(4096, local_root, swarn->path);
	memalloc_nofs_restore(nofs_flag);
	if (IS_ERR(ipath)) {
		btrfs_put_root(local_root);
		ret = PTR_ERR(ipath);
		ipath = NULL;
		goto err;
	}
	ret = paths_from_inode(inum, ipath);

	if (ret < 0)
		goto err;

	/*
	 * we deliberately ignore the bit ipath might have been too small to
	 * hold all of the paths here
	 */
	for (i = 0; i < ipath->fspath->elem_cnt; ++i)
		btrfs_warn_in_rcu(fs_info,
"%s at logical %llu on dev %s, physical %llu, root %llu, inode %llu, offset %llu, length %u, links %u (path: %s)",
				  swarn->errstr, swarn->logical,
				  rcu_str_deref(swarn->dev->name),
				  swarn->physical,
				  root, inum, offset,
				  fs_info->sectorsize, nlink,
				  (char *)(unsigned long)ipath->fspath->val[i]);

	btrfs_put_root(local_root);
	free_ipath(ipath);
	return 0;

err:
	btrfs_warn_in_rcu(fs_info,
			  "%s at logical %llu on dev %s, physical %llu, root %llu, inode %llu, offset %llu: path resolving failed with ret=%d",
			  swarn->errstr, swarn->logical,
			  rcu_str_deref(swarn->dev->name),
			  swarn->physical,
			  root, inum, offset, ret);

	free_ipath(ipath);
	return 0;
}

static void scrub_print_warning(const char *errstr, struct scrub_block *sblock)
{
	struct btrfs_device *dev;
	struct btrfs_fs_info *fs_info;
	struct btrfs_path *path;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	struct btrfs_extent_item *ei;
	struct scrub_warning swarn;
	unsigned long ptr = 0;
	u64 extent_item_pos;
	u64 flags = 0;
	u64 ref_root;
	u32 item_size;
	u8 ref_level = 0;
	int ret;

	WARN_ON(sblock->sector_count < 1);
	dev = sblock->dev;
	fs_info = sblock->sctx->fs_info;

	/* Super block error, no need to search extent tree. */
	if (sblock->sectors[0]->flags & BTRFS_EXTENT_FLAG_SUPER) {
		btrfs_warn_in_rcu(fs_info, "%s on device %s, physical %llu",
			errstr, rcu_str_deref(dev->name),
			sblock->physical);
		return;
	}
	path = btrfs_alloc_path();
	if (!path)
		return;

	swarn.physical = sblock->physical;
	swarn.logical = sblock->logical;
	swarn.errstr = errstr;
	swarn.dev = NULL;

	ret = extent_from_logical(fs_info, swarn.logical, path, &found_key,
				  &flags);
	if (ret < 0)
		goto out;

	extent_item_pos = swarn.logical - found_key.objectid;
	swarn.extent_item_size = found_key.offset;

	eb = path->nodes[0];
	ei = btrfs_item_ptr(eb, path->slots[0], struct btrfs_extent_item);
	item_size = btrfs_item_size(eb, path->slots[0]);

	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		do {
			ret = tree_backref_for_extent(&ptr, eb, &found_key, ei,
						      item_size, &ref_root,
						      &ref_level);
			btrfs_warn_in_rcu(fs_info,
"%s at logical %llu on dev %s, physical %llu: metadata %s (level %d) in tree %llu",
				errstr, swarn.logical,
				rcu_str_deref(dev->name),
				swarn.physical,
				ref_level ? "node" : "leaf",
				ret < 0 ? -1 : ref_level,
				ret < 0 ? -1 : ref_root);
		} while (ret != 1);
		btrfs_release_path(path);
	} else {
		btrfs_release_path(path);
		swarn.path = path;
		swarn.dev = dev;
		iterate_extent_inodes(fs_info, found_key.objectid,
					extent_item_pos, 1,
					scrub_print_warning_inode, &swarn, false);
	}

out:
	btrfs_free_path(path);
}

static inline void scrub_get_recover(struct scrub_recover *recover)
{
	refcount_inc(&recover->refs);
}

static inline void scrub_put_recover(struct btrfs_fs_info *fs_info,
				     struct scrub_recover *recover)
{
	if (refcount_dec_and_test(&recover->refs)) {
		btrfs_bio_counter_dec(fs_info);
		btrfs_put_bioc(recover->bioc);
		kfree(recover);
	}
}

/*
 * scrub_handle_errored_block gets called when either verification of the
 * sectors failed or the bio failed to read, e.g. with EIO. In the latter
 * case, this function handles all sectors in the bio, even though only one
 * may be bad.
 * The goal of this function is to repair the errored block by using the
 * contents of one of the mirrors.
 */
static int scrub_handle_errored_block(struct scrub_block *sblock_to_check)
{
	struct scrub_ctx *sctx = sblock_to_check->sctx;
	struct btrfs_device *dev = sblock_to_check->dev;
	struct btrfs_fs_info *fs_info;
	u64 logical;
	unsigned int failed_mirror_index;
	unsigned int is_metadata;
	unsigned int have_csum;
	/* One scrub_block for each mirror */
	struct scrub_block *sblocks_for_recheck[BTRFS_MAX_MIRRORS] = { 0 };
	struct scrub_block *sblock_bad;
	int ret;
	int mirror_index;
	int sector_num;
	int success;
	bool full_stripe_locked;
	unsigned int nofs_flag;
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	BUG_ON(sblock_to_check->sector_count < 1);
	fs_info = sctx->fs_info;
	if (sblock_to_check->sectors[0]->flags & BTRFS_EXTENT_FLAG_SUPER) {
		/*
		 * If we find an error in a super block, we just report it.
		 * They will get written with the next transaction commit
		 * anyway
		 */
		scrub_print_warning("super block error", sblock_to_check);
		spin_lock(&sctx->stat_lock);
		++sctx->stat.super_errors;
		spin_unlock(&sctx->stat_lock);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_CORRUPTION_ERRS);
		return 0;
	}
	logical = sblock_to_check->logical;
	ASSERT(sblock_to_check->mirror_num);
	failed_mirror_index = sblock_to_check->mirror_num - 1;
	is_metadata = !(sblock_to_check->sectors[0]->flags &
			BTRFS_EXTENT_FLAG_DATA);
	have_csum = sblock_to_check->sectors[0]->have_csum;

	if (!sctx->is_dev_replace && btrfs_repair_one_zone(fs_info, logical))
		return 0;

	/*
	 * We must use GFP_NOFS because the scrub task might be waiting for a
	 * worker task executing this function and in turn a transaction commit
	 * might be waiting the scrub task to pause (which needs to wait for all
	 * the worker tasks to complete before pausing).
	 * We do allocations in the workers through insert_full_stripe_lock()
	 * and scrub_add_sector_to_wr_bio(), which happens down the call chain of
	 * this function.
	 */
	nofs_flag = memalloc_nofs_save();
	/*
	 * For RAID5/6, race can happen for a different device scrub thread.
	 * For data corruption, Parity and Data threads will both try
	 * to recovery the data.
	 * Race can lead to doubly added csum error, or even unrecoverable
	 * error.
	 */
	ret = lock_full_stripe(fs_info, logical, &full_stripe_locked);
	if (ret < 0) {
		memalloc_nofs_restore(nofs_flag);
		spin_lock(&sctx->stat_lock);
		if (ret == -ENOMEM)
			sctx->stat.malloc_errors++;
		sctx->stat.read_errors++;
		sctx->stat.uncorrectable_errors++;
		spin_unlock(&sctx->stat_lock);
		return ret;
	}

	/*
	 * read all mirrors one after the other. This includes to
	 * re-read the extent or metadata block that failed (that was
	 * the cause that this fixup code is called) another time,
	 * sector by sector this time in order to know which sectors
	 * caused I/O errors and which ones are good (for all mirrors).
	 * It is the goal to handle the situation when more than one
	 * mirror contains I/O errors, but the errors do not
	 * overlap, i.e. the data can be repaired by selecting the
	 * sectors from those mirrors without I/O error on the
	 * particular sectors. One example (with blocks >= 2 * sectorsize)
	 * would be that mirror #1 has an I/O error on the first sector,
	 * the second sector is good, and mirror #2 has an I/O error on
	 * the second sector, but the first sector is good.
	 * Then the first sector of the first mirror can be repaired by
	 * taking the first sector of the second mirror, and the
	 * second sector of the second mirror can be repaired by
	 * copying the contents of the 2nd sector of the 1st mirror.
	 * One more note: if the sectors of one mirror contain I/O
	 * errors, the checksum cannot be verified. In order to get
	 * the best data for repairing, the first attempt is to find
	 * a mirror without I/O errors and with a validated checksum.
	 * Only if this is not possible, the sectors are picked from
	 * mirrors with I/O errors without considering the checksum.
	 * If the latter is the case, at the end, the checksum of the
	 * repaired area is verified in order to correctly maintain
	 * the statistics.
	 */
	for (mirror_index = 0; mirror_index < BTRFS_MAX_MIRRORS; mirror_index++) {
		/*
		 * Note: the two members refs and outstanding_sectors are not
		 * used in the blocks that are used for the recheck procedure.
		 *
		 * But alloc_scrub_block() will initialize sblock::ref anyway,
		 * so we can use scrub_block_put() to clean them up.
		 *
		 * And here we don't setup the physical/dev for the sblock yet,
		 * they will be correctly initialized in scrub_setup_recheck_block().
		 */
		sblocks_for_recheck[mirror_index] = alloc_scrub_block(sctx, NULL,
							logical, 0, 0, mirror_index);
		if (!sblocks_for_recheck[mirror_index]) {
			spin_lock(&sctx->stat_lock);
			sctx->stat.malloc_errors++;
			sctx->stat.read_errors++;
			sctx->stat.uncorrectable_errors++;
			spin_unlock(&sctx->stat_lock);
			btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
			goto out;
		}
	}

	/* Setup the context, map the logical blocks and alloc the sectors */
	ret = scrub_setup_recheck_block(sblock_to_check, sblocks_for_recheck);
	if (ret) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.read_errors++;
		sctx->stat.uncorrectable_errors++;
		spin_unlock(&sctx->stat_lock);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
		goto out;
	}
	BUG_ON(failed_mirror_index >= BTRFS_MAX_MIRRORS);
	sblock_bad = sblocks_for_recheck[failed_mirror_index];

	/* build and submit the bios for the failed mirror, check checksums */
	scrub_recheck_block(fs_info, sblock_bad, 1);

	if (!sblock_bad->header_error && !sblock_bad->checksum_error &&
	    sblock_bad->no_io_error_seen) {
		/*
		 * The error disappeared after reading sector by sector, or
		 * the area was part of a huge bio and other parts of the
		 * bio caused I/O errors, or the block layer merged several
		 * read requests into one and the error is caused by a
		 * different bio (usually one of the two latter cases is
		 * the cause)
		 */
		spin_lock(&sctx->stat_lock);
		sctx->stat.unverified_errors++;
		sblock_to_check->data_corrected = 1;
		spin_unlock(&sctx->stat_lock);

		if (sctx->is_dev_replace)
			scrub_write_block_to_dev_replace(sblock_bad);
		goto out;
	}

	if (!sblock_bad->no_io_error_seen) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.read_errors++;
		spin_unlock(&sctx->stat_lock);
		if (__ratelimit(&rs))
			scrub_print_warning("i/o error", sblock_to_check);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
	} else if (sblock_bad->checksum_error) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.csum_errors++;
		spin_unlock(&sctx->stat_lock);
		if (__ratelimit(&rs))
			scrub_print_warning("checksum error", sblock_to_check);
		btrfs_dev_stat_inc_and_print(dev,
					     BTRFS_DEV_STAT_CORRUPTION_ERRS);
	} else if (sblock_bad->header_error) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.verify_errors++;
		spin_unlock(&sctx->stat_lock);
		if (__ratelimit(&rs))
			scrub_print_warning("checksum/header error",
					    sblock_to_check);
		if (sblock_bad->generation_error)
			btrfs_dev_stat_inc_and_print(dev,
				BTRFS_DEV_STAT_GENERATION_ERRS);
		else
			btrfs_dev_stat_inc_and_print(dev,
				BTRFS_DEV_STAT_CORRUPTION_ERRS);
	}

	if (sctx->readonly) {
		ASSERT(!sctx->is_dev_replace);
		goto out;
	}

	/*
	 * now build and submit the bios for the other mirrors, check
	 * checksums.
	 * First try to pick the mirror which is completely without I/O
	 * errors and also does not have a checksum error.
	 * If one is found, and if a checksum is present, the full block
	 * that is known to contain an error is rewritten. Afterwards
	 * the block is known to be corrected.
	 * If a mirror is found which is completely correct, and no
	 * checksum is present, only those sectors are rewritten that had
	 * an I/O error in the block to be repaired, since it cannot be
	 * determined, which copy of the other sectors is better (and it
	 * could happen otherwise that a correct sector would be
	 * overwritten by a bad one).
	 */
	for (mirror_index = 0; ;mirror_index++) {
		struct scrub_block *sblock_other;

		if (mirror_index == failed_mirror_index)
			continue;

		/* raid56's mirror can be more than BTRFS_MAX_MIRRORS */
		if (!scrub_is_page_on_raid56(sblock_bad->sectors[0])) {
			if (mirror_index >= BTRFS_MAX_MIRRORS)
				break;
			if (!sblocks_for_recheck[mirror_index]->sector_count)
				break;

			sblock_other = sblocks_for_recheck[mirror_index];
		} else {
			struct scrub_recover *r = sblock_bad->sectors[0]->recover;
			int max_allowed = r->bioc->num_stripes - r->bioc->num_tgtdevs;

			if (mirror_index >= max_allowed)
				break;
			if (!sblocks_for_recheck[1]->sector_count)
				break;

			ASSERT(failed_mirror_index == 0);
			sblock_other = sblocks_for_recheck[1];
			sblock_other->mirror_num = 1 + mirror_index;
		}

		/* build and submit the bios, check checksums */
		scrub_recheck_block(fs_info, sblock_other, 0);

		if (!sblock_other->header_error &&
		    !sblock_other->checksum_error &&
		    sblock_other->no_io_error_seen) {
			if (sctx->is_dev_replace) {
				scrub_write_block_to_dev_replace(sblock_other);
				goto corrected_error;
			} else {
				ret = scrub_repair_block_from_good_copy(
						sblock_bad, sblock_other);
				if (!ret)
					goto corrected_error;
			}
		}
	}

	if (sblock_bad->no_io_error_seen && !sctx->is_dev_replace)
		goto did_not_correct_error;

	/*
	 * In case of I/O errors in the area that is supposed to be
	 * repaired, continue by picking good copies of those sectors.
	 * Select the good sectors from mirrors to rewrite bad sectors from
	 * the area to fix. Afterwards verify the checksum of the block
	 * that is supposed to be repaired. This verification step is
	 * only done for the purpose of statistic counting and for the
	 * final scrub report, whether errors remain.
	 * A perfect algorithm could make use of the checksum and try
	 * all possible combinations of sectors from the different mirrors
	 * until the checksum verification succeeds. For example, when
	 * the 2nd sector of mirror #1 faces I/O errors, and the 2nd sector
	 * of mirror #2 is readable but the final checksum test fails,
	 * then the 2nd sector of mirror #3 could be tried, whether now
	 * the final checksum succeeds. But this would be a rare
	 * exception and is therefore not implemented. At least it is
	 * avoided that the good copy is overwritten.
	 * A more useful improvement would be to pick the sectors
	 * without I/O error based on sector sizes (512 bytes on legacy
	 * disks) instead of on sectorsize. Then maybe 512 byte of one
	 * mirror could be repaired by taking 512 byte of a different
	 * mirror, even if other 512 byte sectors in the same sectorsize
	 * area are unreadable.
	 */
	success = 1;
	for (sector_num = 0; sector_num < sblock_bad->sector_count;
	     sector_num++) {
		struct scrub_sector *sector_bad = sblock_bad->sectors[sector_num];
		struct scrub_block *sblock_other = NULL;

		/* Skip no-io-error sectors in scrub */
		if (!sector_bad->io_error && !sctx->is_dev_replace)
			continue;

		if (scrub_is_page_on_raid56(sblock_bad->sectors[0])) {
			/*
			 * In case of dev replace, if raid56 rebuild process
			 * didn't work out correct data, then copy the content
			 * in sblock_bad to make sure target device is identical
			 * to source device, instead of writing garbage data in
			 * sblock_for_recheck array to target device.
			 */
			sblock_other = NULL;
		} else if (sector_bad->io_error) {
			/* Try to find no-io-error sector in mirrors */
			for (mirror_index = 0;
			     mirror_index < BTRFS_MAX_MIRRORS &&
			     sblocks_for_recheck[mirror_index]->sector_count > 0;
			     mirror_index++) {
				if (!sblocks_for_recheck[mirror_index]->
				    sectors[sector_num]->io_error) {
					sblock_other = sblocks_for_recheck[mirror_index];
					break;
				}
			}
			if (!sblock_other)
				success = 0;
		}

		if (sctx->is_dev_replace) {
			/*
			 * Did not find a mirror to fetch the sector from.
			 * scrub_write_sector_to_dev_replace() handles this
			 * case (sector->io_error), by filling the block with
			 * zeros before submitting the write request
			 */
			if (!sblock_other)
				sblock_other = sblock_bad;

			if (scrub_write_sector_to_dev_replace(sblock_other,
							      sector_num) != 0) {
				atomic64_inc(
					&fs_info->dev_replace.num_write_errors);
				success = 0;
			}
		} else if (sblock_other) {
			ret = scrub_repair_sector_from_good_copy(sblock_bad,
								 sblock_other,
								 sector_num, 0);
			if (0 == ret)
				sector_bad->io_error = 0;
			else
				success = 0;
		}
	}

	if (success && !sctx->is_dev_replace) {
		if (is_metadata || have_csum) {
			/*
			 * need to verify the checksum now that all
			 * sectors on disk are repaired (the write
			 * request for data to be repaired is on its way).
			 * Just be lazy and use scrub_recheck_block()
			 * which re-reads the data before the checksum
			 * is verified, but most likely the data comes out
			 * of the page cache.
			 */
			scrub_recheck_block(fs_info, sblock_bad, 1);
			if (!sblock_bad->header_error &&
			    !sblock_bad->checksum_error &&
			    sblock_bad->no_io_error_seen)
				goto corrected_error;
			else
				goto did_not_correct_error;
		} else {
corrected_error:
			spin_lock(&sctx->stat_lock);
			sctx->stat.corrected_errors++;
			sblock_to_check->data_corrected = 1;
			spin_unlock(&sctx->stat_lock);
			btrfs_err_rl_in_rcu(fs_info,
				"fixed up error at logical %llu on dev %s",
				logical, rcu_str_deref(dev->name));
		}
	} else {
did_not_correct_error:
		spin_lock(&sctx->stat_lock);
		sctx->stat.uncorrectable_errors++;
		spin_unlock(&sctx->stat_lock);
		btrfs_err_rl_in_rcu(fs_info,
			"unable to fixup (regular) error at logical %llu on dev %s",
			logical, rcu_str_deref(dev->name));
	}

out:
	for (mirror_index = 0; mirror_index < BTRFS_MAX_MIRRORS; mirror_index++) {
		struct scrub_block *sblock = sblocks_for_recheck[mirror_index];
		struct scrub_recover *recover;
		int sector_index;

		/* Not allocated, continue checking the next mirror */
		if (!sblock)
			continue;

		for (sector_index = 0; sector_index < sblock->sector_count;
		     sector_index++) {
			/*
			 * Here we just cleanup the recover, each sector will be
			 * properly cleaned up by later scrub_block_put()
			 */
			recover = sblock->sectors[sector_index]->recover;
			if (recover) {
				scrub_put_recover(fs_info, recover);
				sblock->sectors[sector_index]->recover = NULL;
			}
		}
		scrub_block_put(sblock);
	}

	ret = unlock_full_stripe(fs_info, logical, full_stripe_locked);
	memalloc_nofs_restore(nofs_flag);
	if (ret < 0)
		return ret;
	return 0;
}

static inline int scrub_nr_raid_mirrors(struct btrfs_io_context *bioc)
{
	if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID5)
		return 2;
	else if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID6)
		return 3;
	else
		return (int)bioc->num_stripes;
}

static inline void scrub_stripe_index_and_offset(u64 logical, u64 map_type,
						 u64 *raid_map,
						 int nstripes, int mirror,
						 int *stripe_index,
						 u64 *stripe_offset)
{
	int i;

	if (map_type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		/* RAID5/6 */
		for (i = 0; i < nstripes; i++) {
			if (raid_map[i] == RAID6_Q_STRIPE ||
			    raid_map[i] == RAID5_P_STRIPE)
				continue;

			if (logical >= raid_map[i] &&
			    logical < raid_map[i] + BTRFS_STRIPE_LEN)
				break;
		}

		*stripe_index = i;
		*stripe_offset = logical - raid_map[i];
	} else {
		/* The other RAID type */
		*stripe_index = mirror;
		*stripe_offset = 0;
	}
}

static int scrub_setup_recheck_block(struct scrub_block *original_sblock,
				     struct scrub_block *sblocks_for_recheck[])
{
	struct scrub_ctx *sctx = original_sblock->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	u64 logical = original_sblock->logical;
	u64 length = original_sblock->sector_count << fs_info->sectorsize_bits;
	u64 generation = original_sblock->sectors[0]->generation;
	u64 flags = original_sblock->sectors[0]->flags;
	u64 have_csum = original_sblock->sectors[0]->have_csum;
	struct scrub_recover *recover;
	struct btrfs_io_context *bioc;
	u64 sublen;
	u64 mapped_length;
	u64 stripe_offset;
	int stripe_index;
	int sector_index = 0;
	int mirror_index;
	int nmirrors;
	int ret;

	while (length > 0) {
		sublen = min_t(u64, length, fs_info->sectorsize);
		mapped_length = sublen;
		bioc = NULL;

		/*
		 * With a length of sectorsize, each returned stripe represents
		 * one mirror
		 */
		btrfs_bio_counter_inc_blocked(fs_info);
		ret = btrfs_map_sblock(fs_info, BTRFS_MAP_GET_READ_MIRRORS,
				       logical, &mapped_length, &bioc);
		if (ret || !bioc || mapped_length < sublen) {
			btrfs_put_bioc(bioc);
			btrfs_bio_counter_dec(fs_info);
			return -EIO;
		}

		recover = kzalloc(sizeof(struct scrub_recover), GFP_NOFS);
		if (!recover) {
			btrfs_put_bioc(bioc);
			btrfs_bio_counter_dec(fs_info);
			return -ENOMEM;
		}

		refcount_set(&recover->refs, 1);
		recover->bioc = bioc;
		recover->map_length = mapped_length;

		ASSERT(sector_index < SCRUB_MAX_SECTORS_PER_BLOCK);

		nmirrors = min(scrub_nr_raid_mirrors(bioc), BTRFS_MAX_MIRRORS);

		for (mirror_index = 0; mirror_index < nmirrors;
		     mirror_index++) {
			struct scrub_block *sblock;
			struct scrub_sector *sector;

			sblock = sblocks_for_recheck[mirror_index];
			sblock->sctx = sctx;

			sector = alloc_scrub_sector(sblock, logical, GFP_NOFS);
			if (!sector) {
				spin_lock(&sctx->stat_lock);
				sctx->stat.malloc_errors++;
				spin_unlock(&sctx->stat_lock);
				scrub_put_recover(fs_info, recover);
				return -ENOMEM;
			}
			sector->flags = flags;
			sector->generation = generation;
			sector->have_csum = have_csum;
			if (have_csum)
				memcpy(sector->csum,
				       original_sblock->sectors[0]->csum,
				       sctx->fs_info->csum_size);

			scrub_stripe_index_and_offset(logical,
						      bioc->map_type,
						      bioc->raid_map,
						      bioc->num_stripes -
						      bioc->num_tgtdevs,
						      mirror_index,
						      &stripe_index,
						      &stripe_offset);
			/*
			 * We're at the first sector, also populate @sblock
			 * physical and dev.
			 */
			if (sector_index == 0) {
				sblock->physical =
					bioc->stripes[stripe_index].physical +
					stripe_offset;
				sblock->dev = bioc->stripes[stripe_index].dev;
				sblock->physical_for_dev_replace =
					original_sblock->physical_for_dev_replace;
			}

			BUG_ON(sector_index >= original_sblock->sector_count);
			scrub_get_recover(recover);
			sector->recover = recover;
		}
		scrub_put_recover(fs_info, recover);
		length -= sublen;
		logical += sublen;
		sector_index++;
	}

	return 0;
}

static void scrub_bio_wait_endio(struct bio *bio)
{
	complete(bio->bi_private);
}

static int scrub_submit_raid56_bio_wait(struct btrfs_fs_info *fs_info,
					struct bio *bio,
					struct scrub_sector *sector)
{
	DECLARE_COMPLETION_ONSTACK(done);

	bio->bi_iter.bi_sector = (sector->offset + sector->sblock->logical) >>
				 SECTOR_SHIFT;
	bio->bi_private = &done;
	bio->bi_end_io = scrub_bio_wait_endio;
	raid56_parity_recover(bio, sector->recover->bioc, sector->sblock->mirror_num);

	wait_for_completion_io(&done);
	return blk_status_to_errno(bio->bi_status);
}

static void scrub_recheck_block_on_raid56(struct btrfs_fs_info *fs_info,
					  struct scrub_block *sblock)
{
	struct scrub_sector *first_sector = sblock->sectors[0];
	struct bio *bio;
	int i;

	/* All sectors in sblock belong to the same stripe on the same device. */
	ASSERT(sblock->dev);
	if (!sblock->dev->bdev)
		goto out;

	bio = bio_alloc(sblock->dev->bdev, BIO_MAX_VECS, REQ_OP_READ, GFP_NOFS);

	for (i = 0; i < sblock->sector_count; i++) {
		struct scrub_sector *sector = sblock->sectors[i];

		bio_add_scrub_sector(bio, sector, fs_info->sectorsize);
	}

	if (scrub_submit_raid56_bio_wait(fs_info, bio, first_sector)) {
		bio_put(bio);
		goto out;
	}

	bio_put(bio);

	scrub_recheck_block_checksum(sblock);

	return;
out:
	for (i = 0; i < sblock->sector_count; i++)
		sblock->sectors[i]->io_error = 1;

	sblock->no_io_error_seen = 0;
}

/*
 * This function will check the on disk data for checksum errors, header errors
 * and read I/O errors. If any I/O errors happen, the exact sectors which are
 * errored are marked as being bad. The goal is to enable scrub to take those
 * sectors that are not errored from all the mirrors so that the sectors that
 * are errored in the just handled mirror can be repaired.
 */
static void scrub_recheck_block(struct btrfs_fs_info *fs_info,
				struct scrub_block *sblock,
				int retry_failed_mirror)
{
	int i;

	sblock->no_io_error_seen = 1;

	/* short cut for raid56 */
	if (!retry_failed_mirror && scrub_is_page_on_raid56(sblock->sectors[0]))
		return scrub_recheck_block_on_raid56(fs_info, sblock);

	for (i = 0; i < sblock->sector_count; i++) {
		struct scrub_sector *sector = sblock->sectors[i];
		struct bio bio;
		struct bio_vec bvec;

		if (sblock->dev->bdev == NULL) {
			sector->io_error = 1;
			sblock->no_io_error_seen = 0;
			continue;
		}

		bio_init(&bio, sblock->dev->bdev, &bvec, 1, REQ_OP_READ);
		bio_add_scrub_sector(&bio, sector, fs_info->sectorsize);
		bio.bi_iter.bi_sector = (sblock->physical + sector->offset) >>
					SECTOR_SHIFT;

		btrfsic_check_bio(&bio);
		if (submit_bio_wait(&bio)) {
			sector->io_error = 1;
			sblock->no_io_error_seen = 0;
		}

		bio_uninit(&bio);
	}

	if (sblock->no_io_error_seen)
		scrub_recheck_block_checksum(sblock);
}

static inline int scrub_check_fsid(u8 fsid[], struct scrub_sector *sector)
{
	struct btrfs_fs_devices *fs_devices = sector->sblock->dev->fs_devices;
	int ret;

	ret = memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE);
	return !ret;
}

static void scrub_recheck_block_checksum(struct scrub_block *sblock)
{
	sblock->header_error = 0;
	sblock->checksum_error = 0;
	sblock->generation_error = 0;

	if (sblock->sectors[0]->flags & BTRFS_EXTENT_FLAG_DATA)
		scrub_checksum_data(sblock);
	else
		scrub_checksum_tree_block(sblock);
}

static int scrub_repair_block_from_good_copy(struct scrub_block *sblock_bad,
					     struct scrub_block *sblock_good)
{
	int i;
	int ret = 0;

	for (i = 0; i < sblock_bad->sector_count; i++) {
		int ret_sub;

		ret_sub = scrub_repair_sector_from_good_copy(sblock_bad,
							     sblock_good, i, 1);
		if (ret_sub)
			ret = ret_sub;
	}

	return ret;
}

static int scrub_repair_sector_from_good_copy(struct scrub_block *sblock_bad,
					      struct scrub_block *sblock_good,
					      int sector_num, int force_write)
{
	struct scrub_sector *sector_bad = sblock_bad->sectors[sector_num];
	struct scrub_sector *sector_good = sblock_good->sectors[sector_num];
	struct btrfs_fs_info *fs_info = sblock_bad->sctx->fs_info;
	const u32 sectorsize = fs_info->sectorsize;

	if (force_write || sblock_bad->header_error ||
	    sblock_bad->checksum_error || sector_bad->io_error) {
		struct bio bio;
		struct bio_vec bvec;
		int ret;

		if (!sblock_bad->dev->bdev) {
			btrfs_warn_rl(fs_info,
				"scrub_repair_page_from_good_copy(bdev == NULL) is unexpected");
			return -EIO;
		}

		bio_init(&bio, sblock_bad->dev->bdev, &bvec, 1, REQ_OP_WRITE);
		bio.bi_iter.bi_sector = (sblock_bad->physical +
					 sector_bad->offset) >> SECTOR_SHIFT;
		ret = bio_add_scrub_sector(&bio, sector_good, sectorsize);

		btrfsic_check_bio(&bio);
		ret = submit_bio_wait(&bio);
		bio_uninit(&bio);

		if (ret) {
			btrfs_dev_stat_inc_and_print(sblock_bad->dev,
				BTRFS_DEV_STAT_WRITE_ERRS);
			atomic64_inc(&fs_info->dev_replace.num_write_errors);
			return -EIO;
		}
	}

	return 0;
}

static void scrub_write_block_to_dev_replace(struct scrub_block *sblock)
{
	struct btrfs_fs_info *fs_info = sblock->sctx->fs_info;
	int i;

	/*
	 * This block is used for the check of the parity on the source device,
	 * so the data needn't be written into the destination device.
	 */
	if (sblock->sparity)
		return;

	for (i = 0; i < sblock->sector_count; i++) {
		int ret;

		ret = scrub_write_sector_to_dev_replace(sblock, i);
		if (ret)
			atomic64_inc(&fs_info->dev_replace.num_write_errors);
	}
}

static int scrub_write_sector_to_dev_replace(struct scrub_block *sblock, int sector_num)
{
	const u32 sectorsize = sblock->sctx->fs_info->sectorsize;
	struct scrub_sector *sector = sblock->sectors[sector_num];

	if (sector->io_error)
		memset(scrub_sector_get_kaddr(sector), 0, sectorsize);

	return scrub_add_sector_to_wr_bio(sblock->sctx, sector);
}

static int fill_writer_pointer_gap(struct scrub_ctx *sctx, u64 physical)
{
	int ret = 0;
	u64 length;

	if (!btrfs_is_zoned(sctx->fs_info))
		return 0;

	if (!btrfs_dev_is_sequential(sctx->wr_tgtdev, physical))
		return 0;

	if (sctx->write_pointer < physical) {
		length = physical - sctx->write_pointer;

		ret = btrfs_zoned_issue_zeroout(sctx->wr_tgtdev,
						sctx->write_pointer, length);
		if (!ret)
			sctx->write_pointer = physical;
	}
	return ret;
}

static void scrub_block_get(struct scrub_block *sblock)
{
	refcount_inc(&sblock->refs);
}

static int scrub_add_sector_to_wr_bio(struct scrub_ctx *sctx,
				      struct scrub_sector *sector)
{
	struct scrub_block *sblock = sector->sblock;
	struct scrub_bio *sbio;
	int ret;
	const u32 sectorsize = sctx->fs_info->sectorsize;

	mutex_lock(&sctx->wr_lock);
again:
	if (!sctx->wr_curr_bio) {
		sctx->wr_curr_bio = kzalloc(sizeof(*sctx->wr_curr_bio),
					      GFP_KERNEL);
		if (!sctx->wr_curr_bio) {
			mutex_unlock(&sctx->wr_lock);
			return -ENOMEM;
		}
		sctx->wr_curr_bio->sctx = sctx;
		sctx->wr_curr_bio->sector_count = 0;
	}
	sbio = sctx->wr_curr_bio;
	if (sbio->sector_count == 0) {
		ret = fill_writer_pointer_gap(sctx, sector->offset +
					      sblock->physical_for_dev_replace);
		if (ret) {
			mutex_unlock(&sctx->wr_lock);
			return ret;
		}

		sbio->physical = sblock->physical_for_dev_replace + sector->offset;
		sbio->logical = sblock->logical + sector->offset;
		sbio->dev = sctx->wr_tgtdev;
		if (!sbio->bio) {
			sbio->bio = bio_alloc(sbio->dev->bdev, sctx->sectors_per_bio,
					      REQ_OP_WRITE, GFP_NOFS);
		}
		sbio->bio->bi_private = sbio;
		sbio->bio->bi_end_io = scrub_wr_bio_end_io;
		sbio->bio->bi_iter.bi_sector = sbio->physical >> 9;
		sbio->status = 0;
	} else if (sbio->physical + sbio->sector_count * sectorsize !=
		   sblock->physical_for_dev_replace + sector->offset ||
		   sbio->logical + sbio->sector_count * sectorsize !=
		   sblock->logical + sector->offset) {
		scrub_wr_submit(sctx);
		goto again;
	}

	ret = bio_add_scrub_sector(sbio->bio, sector, sectorsize);
	if (ret != sectorsize) {
		if (sbio->sector_count < 1) {
			bio_put(sbio->bio);
			sbio->bio = NULL;
			mutex_unlock(&sctx->wr_lock);
			return -EIO;
		}
		scrub_wr_submit(sctx);
		goto again;
	}

	sbio->sectors[sbio->sector_count] = sector;
	scrub_sector_get(sector);
	/*
	 * Since ssector no longer holds a page, but uses sblock::pages, we
	 * have to ensure the sblock had not been freed before our write bio
	 * finished.
	 */
	scrub_block_get(sector->sblock);

	sbio->sector_count++;
	if (sbio->sector_count == sctx->sectors_per_bio)
		scrub_wr_submit(sctx);
	mutex_unlock(&sctx->wr_lock);

	return 0;
}

static void scrub_wr_submit(struct scrub_ctx *sctx)
{
	struct scrub_bio *sbio;

	if (!sctx->wr_curr_bio)
		return;

	sbio = sctx->wr_curr_bio;
	sctx->wr_curr_bio = NULL;
	scrub_pending_bio_inc(sctx);
	/* process all writes in a single worker thread. Then the block layer
	 * orders the requests before sending them to the driver which
	 * doubled the write performance on spinning disks when measured
	 * with Linux 3.5 */
	btrfsic_check_bio(sbio->bio);
	submit_bio(sbio->bio);

	if (btrfs_is_zoned(sctx->fs_info))
		sctx->write_pointer = sbio->physical + sbio->sector_count *
			sctx->fs_info->sectorsize;
}

static void scrub_wr_bio_end_io(struct bio *bio)
{
	struct scrub_bio *sbio = bio->bi_private;
	struct btrfs_fs_info *fs_info = sbio->dev->fs_info;

	sbio->status = bio->bi_status;
	sbio->bio = bio;

	INIT_WORK(&sbio->work, scrub_wr_bio_end_io_worker);
	queue_work(fs_info->scrub_wr_completion_workers, &sbio->work);
}

static void scrub_wr_bio_end_io_worker(struct work_struct *work)
{
	struct scrub_bio *sbio = container_of(work, struct scrub_bio, work);
	struct scrub_ctx *sctx = sbio->sctx;
	int i;

	ASSERT(sbio->sector_count <= SCRUB_SECTORS_PER_BIO);
	if (sbio->status) {
		struct btrfs_dev_replace *dev_replace =
			&sbio->sctx->fs_info->dev_replace;

		for (i = 0; i < sbio->sector_count; i++) {
			struct scrub_sector *sector = sbio->sectors[i];

			sector->io_error = 1;
			atomic64_inc(&dev_replace->num_write_errors);
		}
	}

	/*
	 * In scrub_add_sector_to_wr_bio() we grab extra ref for sblock, now in
	 * endio we should put the sblock.
	 */
	for (i = 0; i < sbio->sector_count; i++) {
		scrub_block_put(sbio->sectors[i]->sblock);
		scrub_sector_put(sbio->sectors[i]);
	}

	bio_put(sbio->bio);
	kfree(sbio);
	scrub_pending_bio_dec(sctx);
}

static int scrub_checksum(struct scrub_block *sblock)
{
	u64 flags;
	int ret;

	/*
	 * No need to initialize these stats currently,
	 * because this function only use return value
	 * instead of these stats value.
	 *
	 * Todo:
	 * always use stats
	 */
	sblock->header_error = 0;
	sblock->generation_error = 0;
	sblock->checksum_error = 0;

	WARN_ON(sblock->sector_count < 1);
	flags = sblock->sectors[0]->flags;
	ret = 0;
	if (flags & BTRFS_EXTENT_FLAG_DATA)
		ret = scrub_checksum_data(sblock);
	else if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK)
		ret = scrub_checksum_tree_block(sblock);
	else if (flags & BTRFS_EXTENT_FLAG_SUPER)
		ret = scrub_checksum_super(sblock);
	else
		WARN_ON(1);
	if (ret)
		scrub_handle_errored_block(sblock);

	return ret;
}

static int scrub_checksum_data(struct scrub_block *sblock)
{
	struct scrub_ctx *sctx = sblock->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	u8 csum[BTRFS_CSUM_SIZE];
	struct scrub_sector *sector;
	char *kaddr;

	BUG_ON(sblock->sector_count < 1);
	sector = sblock->sectors[0];
	if (!sector->have_csum)
		return 0;

	kaddr = scrub_sector_get_kaddr(sector);

	shash->tfm = fs_info->csum_shash;
	crypto_shash_init(shash);

	crypto_shash_digest(shash, kaddr, fs_info->sectorsize, csum);

	if (memcmp(csum, sector->csum, fs_info->csum_size))
		sblock->checksum_error = 1;
	return sblock->checksum_error;
}

static int scrub_checksum_tree_block(struct scrub_block *sblock)
{
	struct scrub_ctx *sctx = sblock->sctx;
	struct btrfs_header *h;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	u8 calculated_csum[BTRFS_CSUM_SIZE];
	u8 on_disk_csum[BTRFS_CSUM_SIZE];
	/*
	 * This is done in sectorsize steps even for metadata as there's a
	 * constraint for nodesize to be aligned to sectorsize. This will need
	 * to change so we don't misuse data and metadata units like that.
	 */
	const u32 sectorsize = sctx->fs_info->sectorsize;
	const int num_sectors = fs_info->nodesize >> fs_info->sectorsize_bits;
	int i;
	struct scrub_sector *sector;
	char *kaddr;

	BUG_ON(sblock->sector_count < 1);

	/* Each member in sectors is just one sector */
	ASSERT(sblock->sector_count == num_sectors);

	sector = sblock->sectors[0];
	kaddr = scrub_sector_get_kaddr(sector);
	h = (struct btrfs_header *)kaddr;
	memcpy(on_disk_csum, h->csum, sctx->fs_info->csum_size);

	/*
	 * we don't use the getter functions here, as we
	 * a) don't have an extent buffer and
	 * b) the page is already kmapped
	 */
	if (sblock->logical != btrfs_stack_header_bytenr(h)) {
		sblock->header_error = 1;
		btrfs_warn_rl(fs_info,
		"tree block %llu mirror %u has bad bytenr, has %llu want %llu",
			      sblock->logical, sblock->mirror_num,
			      btrfs_stack_header_bytenr(h),
			      sblock->logical);
		goto out;
	}

	if (!scrub_check_fsid(h->fsid, sector)) {
		sblock->header_error = 1;
		btrfs_warn_rl(fs_info,
		"tree block %llu mirror %u has bad fsid, has %pU want %pU",
			      sblock->logical, sblock->mirror_num,
			      h->fsid, sblock->dev->fs_devices->fsid);
		goto out;
	}

	if (memcmp(h->chunk_tree_uuid, fs_info->chunk_tree_uuid, BTRFS_UUID_SIZE)) {
		sblock->header_error = 1;
		btrfs_warn_rl(fs_info,
		"tree block %llu mirror %u has bad chunk tree uuid, has %pU want %pU",
			      sblock->logical, sblock->mirror_num,
			      h->chunk_tree_uuid, fs_info->chunk_tree_uuid);
		goto out;
	}

	shash->tfm = fs_info->csum_shash;
	crypto_shash_init(shash);
	crypto_shash_update(shash, kaddr + BTRFS_CSUM_SIZE,
			    sectorsize - BTRFS_CSUM_SIZE);

	for (i = 1; i < num_sectors; i++) {
		kaddr = scrub_sector_get_kaddr(sblock->sectors[i]);
		crypto_shash_update(shash, kaddr, sectorsize);
	}

	crypto_shash_final(shash, calculated_csum);
	if (memcmp(calculated_csum, on_disk_csum, sctx->fs_info->csum_size)) {
		sblock->checksum_error = 1;
		btrfs_warn_rl(fs_info,
		"tree block %llu mirror %u has bad csum, has " CSUM_FMT " want " CSUM_FMT,
			      sblock->logical, sblock->mirror_num,
			      CSUM_FMT_VALUE(fs_info->csum_size, on_disk_csum),
			      CSUM_FMT_VALUE(fs_info->csum_size, calculated_csum));
		goto out;
	}

	if (sector->generation != btrfs_stack_header_generation(h)) {
		sblock->header_error = 1;
		sblock->generation_error = 1;
		btrfs_warn_rl(fs_info,
		"tree block %llu mirror %u has bad generation, has %llu want %llu",
			      sblock->logical, sblock->mirror_num,
			      btrfs_stack_header_generation(h),
			      sector->generation);
	}

out:
	return sblock->header_error || sblock->checksum_error;
}

static int scrub_checksum_super(struct scrub_block *sblock)
{
	struct btrfs_super_block *s;
	struct scrub_ctx *sctx = sblock->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	u8 calculated_csum[BTRFS_CSUM_SIZE];
	struct scrub_sector *sector;
	char *kaddr;
	int fail_gen = 0;
	int fail_cor = 0;

	BUG_ON(sblock->sector_count < 1);
	sector = sblock->sectors[0];
	kaddr = scrub_sector_get_kaddr(sector);
	s = (struct btrfs_super_block *)kaddr;

	if (sblock->logical != btrfs_super_bytenr(s))
		++fail_cor;

	if (sector->generation != btrfs_super_generation(s))
		++fail_gen;

	if (!scrub_check_fsid(s->fsid, sector))
		++fail_cor;

	shash->tfm = fs_info->csum_shash;
	crypto_shash_init(shash);
	crypto_shash_digest(shash, kaddr + BTRFS_CSUM_SIZE,
			BTRFS_SUPER_INFO_SIZE - BTRFS_CSUM_SIZE, calculated_csum);

	if (memcmp(calculated_csum, s->csum, sctx->fs_info->csum_size))
		++fail_cor;

	return fail_cor + fail_gen;
}

static void scrub_block_put(struct scrub_block *sblock)
{
	if (refcount_dec_and_test(&sblock->refs)) {
		int i;

		if (sblock->sparity)
			scrub_parity_put(sblock->sparity);

		for (i = 0; i < sblock->sector_count; i++)
			scrub_sector_put(sblock->sectors[i]);
		for (i = 0; i < DIV_ROUND_UP(sblock->len, PAGE_SIZE); i++) {
			if (sblock->pages[i]) {
				detach_scrub_page_private(sblock->pages[i]);
				__free_page(sblock->pages[i]);
			}
		}
		kfree(sblock);
	}
}

static void scrub_sector_get(struct scrub_sector *sector)
{
	atomic_inc(&sector->refs);
}

static void scrub_sector_put(struct scrub_sector *sector)
{
	if (atomic_dec_and_test(&sector->refs))
		kfree(sector);
}

/*
 * Throttling of IO submission, bandwidth-limit based, the timeslice is 1
 * second.  Limit can be set via /sys/fs/UUID/devinfo/devid/scrub_speed_max.
 */
static void scrub_throttle(struct scrub_ctx *sctx)
{
	const int time_slice = 1000;
	struct scrub_bio *sbio;
	struct btrfs_device *device;
	s64 delta;
	ktime_t now;
	u32 div;
	u64 bwlimit;

	sbio = sctx->bios[sctx->curr];
	device = sbio->dev;
	bwlimit = READ_ONCE(device->scrub_speed_max);
	if (bwlimit == 0)
		return;

	/*
	 * Slice is divided into intervals when the IO is submitted, adjust by
	 * bwlimit and maximum of 64 intervals.
	 */
	div = max_t(u32, 1, (u32)(bwlimit / (16 * 1024 * 1024)));
	div = min_t(u32, 64, div);

	/* Start new epoch, set deadline */
	now = ktime_get();
	if (sctx->throttle_deadline == 0) {
		sctx->throttle_deadline = ktime_add_ms(now, time_slice / div);
		sctx->throttle_sent = 0;
	}

	/* Still in the time to send? */
	if (ktime_before(now, sctx->throttle_deadline)) {
		/* If current bio is within the limit, send it */
		sctx->throttle_sent += sbio->bio->bi_iter.bi_size;
		if (sctx->throttle_sent <= div_u64(bwlimit, div))
			return;

		/* We're over the limit, sleep until the rest of the slice */
		delta = ktime_ms_delta(sctx->throttle_deadline, now);
	} else {
		/* New request after deadline, start new epoch */
		delta = 0;
	}

	if (delta) {
		long timeout;

		timeout = div_u64(delta * HZ, 1000);
		schedule_timeout_interruptible(timeout);
	}

	/* Next call will start the deadline period */
	sctx->throttle_deadline = 0;
}

static void scrub_submit(struct scrub_ctx *sctx)
{
	struct scrub_bio *sbio;

	if (sctx->curr == -1)
		return;

	scrub_throttle(sctx);

	sbio = sctx->bios[sctx->curr];
	sctx->curr = -1;
	scrub_pending_bio_inc(sctx);
	btrfsic_check_bio(sbio->bio);
	submit_bio(sbio->bio);
}

static int scrub_add_sector_to_rd_bio(struct scrub_ctx *sctx,
				      struct scrub_sector *sector)
{
	struct scrub_block *sblock = sector->sblock;
	struct scrub_bio *sbio;
	const u32 sectorsize = sctx->fs_info->sectorsize;
	int ret;

again:
	/*
	 * grab a fresh bio or wait for one to become available
	 */
	while (sctx->curr == -1) {
		spin_lock(&sctx->list_lock);
		sctx->curr = sctx->first_free;
		if (sctx->curr != -1) {
			sctx->first_free = sctx->bios[sctx->curr]->next_free;
			sctx->bios[sctx->curr]->next_free = -1;
			sctx->bios[sctx->curr]->sector_count = 0;
			spin_unlock(&sctx->list_lock);
		} else {
			spin_unlock(&sctx->list_lock);
			wait_event(sctx->list_wait, sctx->first_free != -1);
		}
	}
	sbio = sctx->bios[sctx->curr];
	if (sbio->sector_count == 0) {
		sbio->physical = sblock->physical + sector->offset;
		sbio->logical = sblock->logical + sector->offset;
		sbio->dev = sblock->dev;
		if (!sbio->bio) {
			sbio->bio = bio_alloc(sbio->dev->bdev, sctx->sectors_per_bio,
					      REQ_OP_READ, GFP_NOFS);
		}
		sbio->bio->bi_private = sbio;
		sbio->bio->bi_end_io = scrub_bio_end_io;
		sbio->bio->bi_iter.bi_sector = sbio->physical >> 9;
		sbio->status = 0;
	} else if (sbio->physical + sbio->sector_count * sectorsize !=
		   sblock->physical + sector->offset ||
		   sbio->logical + sbio->sector_count * sectorsize !=
		   sblock->logical + sector->offset ||
		   sbio->dev != sblock->dev) {
		scrub_submit(sctx);
		goto again;
	}

	sbio->sectors[sbio->sector_count] = sector;
	ret = bio_add_scrub_sector(sbio->bio, sector, sectorsize);
	if (ret != sectorsize) {
		if (sbio->sector_count < 1) {
			bio_put(sbio->bio);
			sbio->bio = NULL;
			return -EIO;
		}
		scrub_submit(sctx);
		goto again;
	}

	scrub_block_get(sblock); /* one for the page added to the bio */
	atomic_inc(&sblock->outstanding_sectors);
	sbio->sector_count++;
	if (sbio->sector_count == sctx->sectors_per_bio)
		scrub_submit(sctx);

	return 0;
}

static void scrub_missing_raid56_end_io(struct bio *bio)
{
	struct scrub_block *sblock = bio->bi_private;
	struct btrfs_fs_info *fs_info = sblock->sctx->fs_info;

	btrfs_bio_counter_dec(fs_info);
	if (bio->bi_status)
		sblock->no_io_error_seen = 0;

	bio_put(bio);

	queue_work(fs_info->scrub_workers, &sblock->work);
}

static void scrub_missing_raid56_worker(struct work_struct *work)
{
	struct scrub_block *sblock = container_of(work, struct scrub_block, work);
	struct scrub_ctx *sctx = sblock->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	u64 logical;
	struct btrfs_device *dev;

	logical = sblock->logical;
	dev = sblock->dev;

	if (sblock->no_io_error_seen)
		scrub_recheck_block_checksum(sblock);

	if (!sblock->no_io_error_seen) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.read_errors++;
		spin_unlock(&sctx->stat_lock);
		btrfs_err_rl_in_rcu(fs_info,
			"IO error rebuilding logical %llu for dev %s",
			logical, rcu_str_deref(dev->name));
	} else if (sblock->header_error || sblock->checksum_error) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.uncorrectable_errors++;
		spin_unlock(&sctx->stat_lock);
		btrfs_err_rl_in_rcu(fs_info,
			"failed to rebuild valid logical %llu for dev %s",
			logical, rcu_str_deref(dev->name));
	} else {
		scrub_write_block_to_dev_replace(sblock);
	}

	if (sctx->is_dev_replace && sctx->flush_all_writes) {
		mutex_lock(&sctx->wr_lock);
		scrub_wr_submit(sctx);
		mutex_unlock(&sctx->wr_lock);
	}

	scrub_block_put(sblock);
	scrub_pending_bio_dec(sctx);
}

static void scrub_missing_raid56_pages(struct scrub_block *sblock)
{
	struct scrub_ctx *sctx = sblock->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	u64 length = sblock->sector_count << fs_info->sectorsize_bits;
	u64 logical = sblock->logical;
	struct btrfs_io_context *bioc = NULL;
	struct bio *bio;
	struct btrfs_raid_bio *rbio;
	int ret;
	int i;

	btrfs_bio_counter_inc_blocked(fs_info);
	ret = btrfs_map_sblock(fs_info, BTRFS_MAP_GET_READ_MIRRORS, logical,
			       &length, &bioc);
	if (ret || !bioc || !bioc->raid_map)
		goto bioc_out;

	if (WARN_ON(!sctx->is_dev_replace ||
		    !(bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK))) {
		/*
		 * We shouldn't be scrubbing a missing device. Even for dev
		 * replace, we should only get here for RAID 5/6. We either
		 * managed to mount something with no mirrors remaining or
		 * there's a bug in scrub_find_good_copy()/btrfs_map_block().
		 */
		goto bioc_out;
	}

	bio = bio_alloc(NULL, BIO_MAX_VECS, REQ_OP_READ, GFP_NOFS);
	bio->bi_iter.bi_sector = logical >> 9;
	bio->bi_private = sblock;
	bio->bi_end_io = scrub_missing_raid56_end_io;

	rbio = raid56_alloc_missing_rbio(bio, bioc);
	if (!rbio)
		goto rbio_out;

	for (i = 0; i < sblock->sector_count; i++) {
		struct scrub_sector *sector = sblock->sectors[i];

		raid56_add_scrub_pages(rbio, scrub_sector_get_page(sector),
				       scrub_sector_get_page_offset(sector),
				       sector->offset + sector->sblock->logical);
	}

	INIT_WORK(&sblock->work, scrub_missing_raid56_worker);
	scrub_block_get(sblock);
	scrub_pending_bio_inc(sctx);
	raid56_submit_missing_rbio(rbio);
	btrfs_put_bioc(bioc);
	return;

rbio_out:
	bio_put(bio);
bioc_out:
	btrfs_bio_counter_dec(fs_info);
	btrfs_put_bioc(bioc);
	spin_lock(&sctx->stat_lock);
	sctx->stat.malloc_errors++;
	spin_unlock(&sctx->stat_lock);
}

static int scrub_sectors(struct scrub_ctx *sctx, u64 logical, u32 len,
		       u64 physical, struct btrfs_device *dev, u64 flags,
		       u64 gen, int mirror_num, u8 *csum,
		       u64 physical_for_dev_replace)
{
	struct scrub_block *sblock;
	const u32 sectorsize = sctx->fs_info->sectorsize;
	int index;

	sblock = alloc_scrub_block(sctx, dev, logical, physical,
				   physical_for_dev_replace, mirror_num);
	if (!sblock) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		spin_unlock(&sctx->stat_lock);
		return -ENOMEM;
	}

	for (index = 0; len > 0; index++) {
		struct scrub_sector *sector;
		/*
		 * Here we will allocate one page for one sector to scrub.
		 * This is fine if PAGE_SIZE == sectorsize, but will cost
		 * more memory for PAGE_SIZE > sectorsize case.
		 */
		u32 l = min(sectorsize, len);

		sector = alloc_scrub_sector(sblock, logical, GFP_KERNEL);
		if (!sector) {
			spin_lock(&sctx->stat_lock);
			sctx->stat.malloc_errors++;
			spin_unlock(&sctx->stat_lock);
			scrub_block_put(sblock);
			return -ENOMEM;
		}
		sector->flags = flags;
		sector->generation = gen;
		if (csum) {
			sector->have_csum = 1;
			memcpy(sector->csum, csum, sctx->fs_info->csum_size);
		} else {
			sector->have_csum = 0;
		}
		len -= l;
		logical += l;
		physical += l;
		physical_for_dev_replace += l;
	}

	WARN_ON(sblock->sector_count == 0);
	if (test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state)) {
		/*
		 * This case should only be hit for RAID 5/6 device replace. See
		 * the comment in scrub_missing_raid56_pages() for details.
		 */
		scrub_missing_raid56_pages(sblock);
	} else {
		for (index = 0; index < sblock->sector_count; index++) {
			struct scrub_sector *sector = sblock->sectors[index];
			int ret;

			ret = scrub_add_sector_to_rd_bio(sctx, sector);
			if (ret) {
				scrub_block_put(sblock);
				return ret;
			}
		}

		if (flags & BTRFS_EXTENT_FLAG_SUPER)
			scrub_submit(sctx);
	}

	/* last one frees, either here or in bio completion for last page */
	scrub_block_put(sblock);
	return 0;
}

static void scrub_bio_end_io(struct bio *bio)
{
	struct scrub_bio *sbio = bio->bi_private;
	struct btrfs_fs_info *fs_info = sbio->dev->fs_info;

	sbio->status = bio->bi_status;
	sbio->bio = bio;

	queue_work(fs_info->scrub_workers, &sbio->work);
}

static void scrub_bio_end_io_worker(struct work_struct *work)
{
	struct scrub_bio *sbio = container_of(work, struct scrub_bio, work);
	struct scrub_ctx *sctx = sbio->sctx;
	int i;

	ASSERT(sbio->sector_count <= SCRUB_SECTORS_PER_BIO);
	if (sbio->status) {
		for (i = 0; i < sbio->sector_count; i++) {
			struct scrub_sector *sector = sbio->sectors[i];

			sector->io_error = 1;
			sector->sblock->no_io_error_seen = 0;
		}
	}

	/* Now complete the scrub_block items that have all pages completed */
	for (i = 0; i < sbio->sector_count; i++) {
		struct scrub_sector *sector = sbio->sectors[i];
		struct scrub_block *sblock = sector->sblock;

		if (atomic_dec_and_test(&sblock->outstanding_sectors))
			scrub_block_complete(sblock);
		scrub_block_put(sblock);
	}

	bio_put(sbio->bio);
	sbio->bio = NULL;
	spin_lock(&sctx->list_lock);
	sbio->next_free = sctx->first_free;
	sctx->first_free = sbio->index;
	spin_unlock(&sctx->list_lock);

	if (sctx->is_dev_replace && sctx->flush_all_writes) {
		mutex_lock(&sctx->wr_lock);
		scrub_wr_submit(sctx);
		mutex_unlock(&sctx->wr_lock);
	}

	scrub_pending_bio_dec(sctx);
}

static inline void __scrub_mark_bitmap(struct scrub_parity *sparity,
				       unsigned long *bitmap,
				       u64 start, u32 len)
{
	u64 offset;
	u32 nsectors;
	u32 sectorsize_bits = sparity->sctx->fs_info->sectorsize_bits;

	if (len >= sparity->stripe_len) {
		bitmap_set(bitmap, 0, sparity->nsectors);
		return;
	}

	start -= sparity->logic_start;
	start = div64_u64_rem(start, sparity->stripe_len, &offset);
	offset = offset >> sectorsize_bits;
	nsectors = len >> sectorsize_bits;

	if (offset + nsectors <= sparity->nsectors) {
		bitmap_set(bitmap, offset, nsectors);
		return;
	}

	bitmap_set(bitmap, offset, sparity->nsectors - offset);
	bitmap_set(bitmap, 0, nsectors - (sparity->nsectors - offset));
}

static inline void scrub_parity_mark_sectors_error(struct scrub_parity *sparity,
						   u64 start, u32 len)
{
	__scrub_mark_bitmap(sparity, &sparity->ebitmap, start, len);
}

static inline void scrub_parity_mark_sectors_data(struct scrub_parity *sparity,
						  u64 start, u32 len)
{
	__scrub_mark_bitmap(sparity, &sparity->dbitmap, start, len);
}

static void scrub_block_complete(struct scrub_block *sblock)
{
	int corrupted = 0;

	if (!sblock->no_io_error_seen) {
		corrupted = 1;
		scrub_handle_errored_block(sblock);
	} else {
		/*
		 * if has checksum error, write via repair mechanism in
		 * dev replace case, otherwise write here in dev replace
		 * case.
		 */
		corrupted = scrub_checksum(sblock);
		if (!corrupted && sblock->sctx->is_dev_replace)
			scrub_write_block_to_dev_replace(sblock);
	}

	if (sblock->sparity && corrupted && !sblock->data_corrected) {
		u64 start = sblock->logical;
		u64 end = sblock->logical +
			  sblock->sectors[sblock->sector_count - 1]->offset +
			  sblock->sctx->fs_info->sectorsize;

		ASSERT(end - start <= U32_MAX);
		scrub_parity_mark_sectors_error(sblock->sparity,
						start, end - start);
	}
}

static void drop_csum_range(struct scrub_ctx *sctx, struct btrfs_ordered_sum *sum)
{
	sctx->stat.csum_discards += sum->len >> sctx->fs_info->sectorsize_bits;
	list_del(&sum->list);
	kfree(sum);
}

/*
 * Find the desired csum for range [logical, logical + sectorsize), and store
 * the csum into @csum.
 *
 * The search source is sctx->csum_list, which is a pre-populated list
 * storing bytenr ordered csum ranges.  We're responsible to cleanup any range
 * that is before @logical.
 *
 * Return 0 if there is no csum for the range.
 * Return 1 if there is csum for the range and copied to @csum.
 */
static int scrub_find_csum(struct scrub_ctx *sctx, u64 logical, u8 *csum)
{
	bool found = false;

	while (!list_empty(&sctx->csum_list)) {
		struct btrfs_ordered_sum *sum = NULL;
		unsigned long index;
		unsigned long num_sectors;

		sum = list_first_entry(&sctx->csum_list,
				       struct btrfs_ordered_sum, list);
		/* The current csum range is beyond our range, no csum found */
		if (sum->bytenr > logical)
			break;

		/*
		 * The current sum is before our bytenr, since scrub is always
		 * done in bytenr order, the csum will never be used anymore,
		 * clean it up so that later calls won't bother with the range,
		 * and continue search the next range.
		 */
		if (sum->bytenr + sum->len <= logical) {
			drop_csum_range(sctx, sum);
			continue;
		}

		/* Now the csum range covers our bytenr, copy the csum */
		found = true;
		index = (logical - sum->bytenr) >> sctx->fs_info->sectorsize_bits;
		num_sectors = sum->len >> sctx->fs_info->sectorsize_bits;

		memcpy(csum, sum->sums + index * sctx->fs_info->csum_size,
		       sctx->fs_info->csum_size);

		/* Cleanup the range if we're at the end of the csum range */
		if (index == num_sectors - 1)
			drop_csum_range(sctx, sum);
		break;
	}
	if (!found)
		return 0;
	return 1;
}

/* scrub extent tries to collect up to 64 kB for each bio */
static int scrub_extent(struct scrub_ctx *sctx, struct map_lookup *map,
			u64 logical, u32 len,
			u64 physical, struct btrfs_device *dev, u64 flags,
			u64 gen, int mirror_num)
{
	struct btrfs_device *src_dev = dev;
	u64 src_physical = physical;
	int src_mirror = mirror_num;
	int ret;
	u8 csum[BTRFS_CSUM_SIZE];
	u32 blocksize;

	if (flags & BTRFS_EXTENT_FLAG_DATA) {
		if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK)
			blocksize = map->stripe_len;
		else
			blocksize = sctx->fs_info->sectorsize;
		spin_lock(&sctx->stat_lock);
		sctx->stat.data_extents_scrubbed++;
		sctx->stat.data_bytes_scrubbed += len;
		spin_unlock(&sctx->stat_lock);
	} else if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK)
			blocksize = map->stripe_len;
		else
			blocksize = sctx->fs_info->nodesize;
		spin_lock(&sctx->stat_lock);
		sctx->stat.tree_extents_scrubbed++;
		sctx->stat.tree_bytes_scrubbed += len;
		spin_unlock(&sctx->stat_lock);
	} else {
		blocksize = sctx->fs_info->sectorsize;
		WARN_ON(1);
	}

	/*
	 * For dev-replace case, we can have @dev being a missing device.
	 * Regular scrub will avoid its execution on missing device at all,
	 * as that would trigger tons of read error.
	 *
	 * Reading from missing device will cause read error counts to
	 * increase unnecessarily.
	 * So here we change the read source to a good mirror.
	 */
	if (sctx->is_dev_replace && !dev->bdev)
		scrub_find_good_copy(sctx->fs_info, logical, len, &src_physical,
				     &src_dev, &src_mirror);
	while (len) {
		u32 l = min(len, blocksize);
		int have_csum = 0;

		if (flags & BTRFS_EXTENT_FLAG_DATA) {
			/* push csums to sbio */
			have_csum = scrub_find_csum(sctx, logical, csum);
			if (have_csum == 0)
				++sctx->stat.no_csum;
		}
		ret = scrub_sectors(sctx, logical, l, src_physical, src_dev,
				    flags, gen, src_mirror,
				    have_csum ? csum : NULL, physical);
		if (ret)
			return ret;
		len -= l;
		logical += l;
		physical += l;
		src_physical += l;
	}
	return 0;
}

static int scrub_sectors_for_parity(struct scrub_parity *sparity,
				  u64 logical, u32 len,
				  u64 physical, struct btrfs_device *dev,
				  u64 flags, u64 gen, int mirror_num, u8 *csum)
{
	struct scrub_ctx *sctx = sparity->sctx;
	struct scrub_block *sblock;
	const u32 sectorsize = sctx->fs_info->sectorsize;
	int index;

	ASSERT(IS_ALIGNED(len, sectorsize));

	sblock = alloc_scrub_block(sctx, dev, logical, physical, physical, mirror_num);
	if (!sblock) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		spin_unlock(&sctx->stat_lock);
		return -ENOMEM;
	}

	sblock->sparity = sparity;
	scrub_parity_get(sparity);

	for (index = 0; len > 0; index++) {
		struct scrub_sector *sector;

		sector = alloc_scrub_sector(sblock, logical, GFP_KERNEL);
		if (!sector) {
			spin_lock(&sctx->stat_lock);
			sctx->stat.malloc_errors++;
			spin_unlock(&sctx->stat_lock);
			scrub_block_put(sblock);
			return -ENOMEM;
		}
		sblock->sectors[index] = sector;
		/* For scrub parity */
		scrub_sector_get(sector);
		list_add_tail(&sector->list, &sparity->sectors_list);
		sector->flags = flags;
		sector->generation = gen;
		if (csum) {
			sector->have_csum = 1;
			memcpy(sector->csum, csum, sctx->fs_info->csum_size);
		} else {
			sector->have_csum = 0;
		}

		/* Iterate over the stripe range in sectorsize steps */
		len -= sectorsize;
		logical += sectorsize;
		physical += sectorsize;
	}

	WARN_ON(sblock->sector_count == 0);
	for (index = 0; index < sblock->sector_count; index++) {
		struct scrub_sector *sector = sblock->sectors[index];
		int ret;

		ret = scrub_add_sector_to_rd_bio(sctx, sector);
		if (ret) {
			scrub_block_put(sblock);
			return ret;
		}
	}

	/* Last one frees, either here or in bio completion for last sector */
	scrub_block_put(sblock);
	return 0;
}

static int scrub_extent_for_parity(struct scrub_parity *sparity,
				   u64 logical, u32 len,
				   u64 physical, struct btrfs_device *dev,
				   u64 flags, u64 gen, int mirror_num)
{
	struct scrub_ctx *sctx = sparity->sctx;
	int ret;
	u8 csum[BTRFS_CSUM_SIZE];
	u32 blocksize;

	if (test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state)) {
		scrub_parity_mark_sectors_error(sparity, logical, len);
		return 0;
	}

	if (flags & BTRFS_EXTENT_FLAG_DATA) {
		blocksize = sparity->stripe_len;
	} else if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		blocksize = sparity->stripe_len;
	} else {
		blocksize = sctx->fs_info->sectorsize;
		WARN_ON(1);
	}

	while (len) {
		u32 l = min(len, blocksize);
		int have_csum = 0;

		if (flags & BTRFS_EXTENT_FLAG_DATA) {
			/* push csums to sbio */
			have_csum = scrub_find_csum(sctx, logical, csum);
			if (have_csum == 0)
				goto skip;
		}
		ret = scrub_sectors_for_parity(sparity, logical, l, physical, dev,
					     flags, gen, mirror_num,
					     have_csum ? csum : NULL);
		if (ret)
			return ret;
skip:
		len -= l;
		logical += l;
		physical += l;
	}
	return 0;
}

/*
 * Given a physical address, this will calculate it's
 * logical offset. if this is a parity stripe, it will return
 * the most left data stripe's logical offset.
 *
 * return 0 if it is a data stripe, 1 means parity stripe.
 */
static int get_raid56_logic_offset(u64 physical, int num,
				   struct map_lookup *map, u64 *offset,
				   u64 *stripe_start)
{
	int i;
	int j = 0;
	u64 stripe_nr;
	u64 last_offset;
	u32 stripe_index;
	u32 rot;
	const int data_stripes = nr_data_stripes(map);

	last_offset = (physical - map->stripes[num].physical) * data_stripes;
	if (stripe_start)
		*stripe_start = last_offset;

	*offset = last_offset;
	for (i = 0; i < data_stripes; i++) {
		*offset = last_offset + i * map->stripe_len;

		stripe_nr = div64_u64(*offset, map->stripe_len);
		stripe_nr = div_u64(stripe_nr, data_stripes);

		/* Work out the disk rotation on this stripe-set */
		stripe_nr = div_u64_rem(stripe_nr, map->num_stripes, &rot);
		/* calculate which stripe this data locates */
		rot += i;
		stripe_index = rot % map->num_stripes;
		if (stripe_index == num)
			return 0;
		if (stripe_index < num)
			j++;
	}
	*offset = last_offset + j * map->stripe_len;
	return 1;
}

static void scrub_free_parity(struct scrub_parity *sparity)
{
	struct scrub_ctx *sctx = sparity->sctx;
	struct scrub_sector *curr, *next;
	int nbits;

	nbits = bitmap_weight(&sparity->ebitmap, sparity->nsectors);
	if (nbits) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.read_errors += nbits;
		sctx->stat.uncorrectable_errors += nbits;
		spin_unlock(&sctx->stat_lock);
	}

	list_for_each_entry_safe(curr, next, &sparity->sectors_list, list) {
		list_del_init(&curr->list);
		scrub_sector_put(curr);
	}

	kfree(sparity);
}

static void scrub_parity_bio_endio_worker(struct work_struct *work)
{
	struct scrub_parity *sparity = container_of(work, struct scrub_parity,
						    work);
	struct scrub_ctx *sctx = sparity->sctx;

	btrfs_bio_counter_dec(sctx->fs_info);
	scrub_free_parity(sparity);
	scrub_pending_bio_dec(sctx);
}

static void scrub_parity_bio_endio(struct bio *bio)
{
	struct scrub_parity *sparity = bio->bi_private;
	struct btrfs_fs_info *fs_info = sparity->sctx->fs_info;

	if (bio->bi_status)
		bitmap_or(&sparity->ebitmap, &sparity->ebitmap,
			  &sparity->dbitmap, sparity->nsectors);

	bio_put(bio);

	INIT_WORK(&sparity->work, scrub_parity_bio_endio_worker);
	queue_work(fs_info->scrub_parity_workers, &sparity->work);
}

static void scrub_parity_check_and_repair(struct scrub_parity *sparity)
{
	struct scrub_ctx *sctx = sparity->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct bio *bio;
	struct btrfs_raid_bio *rbio;
	struct btrfs_io_context *bioc = NULL;
	u64 length;
	int ret;

	if (!bitmap_andnot(&sparity->dbitmap, &sparity->dbitmap,
			   &sparity->ebitmap, sparity->nsectors))
		goto out;

	length = sparity->logic_end - sparity->logic_start;

	btrfs_bio_counter_inc_blocked(fs_info);
	ret = btrfs_map_sblock(fs_info, BTRFS_MAP_WRITE, sparity->logic_start,
			       &length, &bioc);
	if (ret || !bioc || !bioc->raid_map)
		goto bioc_out;

	bio = bio_alloc(NULL, BIO_MAX_VECS, REQ_OP_READ, GFP_NOFS);
	bio->bi_iter.bi_sector = sparity->logic_start >> 9;
	bio->bi_private = sparity;
	bio->bi_end_io = scrub_parity_bio_endio;

	rbio = raid56_parity_alloc_scrub_rbio(bio, bioc,
					      sparity->scrub_dev,
					      &sparity->dbitmap,
					      sparity->nsectors);
	btrfs_put_bioc(bioc);
	if (!rbio)
		goto rbio_out;

	scrub_pending_bio_inc(sctx);
	raid56_parity_submit_scrub_rbio(rbio);
	return;

rbio_out:
	bio_put(bio);
bioc_out:
	btrfs_bio_counter_dec(fs_info);
	bitmap_or(&sparity->ebitmap, &sparity->ebitmap, &sparity->dbitmap,
		  sparity->nsectors);
	spin_lock(&sctx->stat_lock);
	sctx->stat.malloc_errors++;
	spin_unlock(&sctx->stat_lock);
out:
	scrub_free_parity(sparity);
}

static void scrub_parity_get(struct scrub_parity *sparity)
{
	refcount_inc(&sparity->refs);
}

static void scrub_parity_put(struct scrub_parity *sparity)
{
	if (!refcount_dec_and_test(&sparity->refs))
		return;

	scrub_parity_check_and_repair(sparity);
}

/*
 * Return 0 if the extent item range covers any byte of the range.
 * Return <0 if the extent item is before @search_start.
 * Return >0 if the extent item is after @start_start + @search_len.
 */
static int compare_extent_item_range(struct btrfs_path *path,
				     u64 search_start, u64 search_len)
{
	struct btrfs_fs_info *fs_info = path->nodes[0]->fs_info;
	u64 len;
	struct btrfs_key key;

	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
	ASSERT(key.type == BTRFS_EXTENT_ITEM_KEY ||
	       key.type == BTRFS_METADATA_ITEM_KEY);
	if (key.type == BTRFS_METADATA_ITEM_KEY)
		len = fs_info->nodesize;
	else
		len = key.offset;

	if (key.objectid + len <= search_start)
		return -1;
	if (key.objectid >= search_start + search_len)
		return 1;
	return 0;
}

/*
 * Locate one extent item which covers any byte in range
 * [@search_start, @search_start + @search_length)
 *
 * If the path is not initialized, we will initialize the search by doing
 * a btrfs_search_slot().
 * If the path is already initialized, we will use the path as the initial
 * slot, to avoid duplicated btrfs_search_slot() calls.
 *
 * NOTE: If an extent item starts before @search_start, we will still
 * return the extent item. This is for data extent crossing stripe boundary.
 *
 * Return 0 if we found such extent item, and @path will point to the extent item.
 * Return >0 if no such extent item can be found, and @path will be released.
 * Return <0 if hit fatal error, and @path will be released.
 */
static int find_first_extent_item(struct btrfs_root *extent_root,
				  struct btrfs_path *path,
				  u64 search_start, u64 search_len)
{
	struct btrfs_fs_info *fs_info = extent_root->fs_info;
	struct btrfs_key key;
	int ret;

	/* Continue using the existing path */
	if (path->nodes[0])
		goto search_forward;

	if (btrfs_fs_incompat(fs_info, SKINNY_METADATA))
		key.type = BTRFS_METADATA_ITEM_KEY;
	else
		key.type = BTRFS_EXTENT_ITEM_KEY;
	key.objectid = search_start;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
	if (ret < 0)
		return ret;

	ASSERT(ret > 0);
	/*
	 * Here we intentionally pass 0 as @min_objectid, as there could be
	 * an extent item starting before @search_start.
	 */
	ret = btrfs_previous_extent_item(extent_root, path, 0);
	if (ret < 0)
		return ret;
	/*
	 * No matter whether we have found an extent item, the next loop will
	 * properly do every check on the key.
	 */
search_forward:
	while (true) {
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid >= search_start + search_len)
			break;
		if (key.type != BTRFS_METADATA_ITEM_KEY &&
		    key.type != BTRFS_EXTENT_ITEM_KEY)
			goto next;

		ret = compare_extent_item_range(path, search_start, search_len);
		if (ret == 0)
			return ret;
		if (ret > 0)
			break;
next:
		path->slots[0]++;
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(extent_root, path);
			if (ret) {
				/* Either no more item or fatal error */
				btrfs_release_path(path);
				return ret;
			}
		}
	}
	btrfs_release_path(path);
	return 1;
}

static void get_extent_info(struct btrfs_path *path, u64 *extent_start_ret,
			    u64 *size_ret, u64 *flags_ret, u64 *generation_ret)
{
	struct btrfs_key key;
	struct btrfs_extent_item *ei;

	btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
	ASSERT(key.type == BTRFS_METADATA_ITEM_KEY ||
	       key.type == BTRFS_EXTENT_ITEM_KEY);
	*extent_start_ret = key.objectid;
	if (key.type == BTRFS_METADATA_ITEM_KEY)
		*size_ret = path->nodes[0]->fs_info->nodesize;
	else
		*size_ret = key.offset;
	ei = btrfs_item_ptr(path->nodes[0], path->slots[0], struct btrfs_extent_item);
	*flags_ret = btrfs_extent_flags(path->nodes[0], ei);
	*generation_ret = btrfs_extent_generation(path->nodes[0], ei);
}

static bool does_range_cross_boundary(u64 extent_start, u64 extent_len,
				      u64 boundary_start, u64 boudary_len)
{
	return (extent_start < boundary_start &&
		extent_start + extent_len > boundary_start) ||
	       (extent_start < boundary_start + boudary_len &&
		extent_start + extent_len > boundary_start + boudary_len);
}

static int scrub_raid56_data_stripe_for_parity(struct scrub_ctx *sctx,
					       struct scrub_parity *sparity,
					       struct map_lookup *map,
					       struct btrfs_device *sdev,
					       struct btrfs_path *path,
					       u64 logical)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_root *extent_root = btrfs_extent_root(fs_info, logical);
	struct btrfs_root *csum_root = btrfs_csum_root(fs_info, logical);
	u64 cur_logical = logical;
	int ret;

	ASSERT(map->type & BTRFS_BLOCK_GROUP_RAID56_MASK);

	/* Path must not be populated */
	ASSERT(!path->nodes[0]);

	while (cur_logical < logical + map->stripe_len) {
		struct btrfs_io_context *bioc = NULL;
		struct btrfs_device *extent_dev;
		u64 extent_start;
		u64 extent_size;
		u64 mapped_length;
		u64 extent_flags;
		u64 extent_gen;
		u64 extent_physical;
		u64 extent_mirror_num;

		ret = find_first_extent_item(extent_root, path, cur_logical,
					     logical + map->stripe_len - cur_logical);
		/* No more extent item in this data stripe */
		if (ret > 0) {
			ret = 0;
			break;
		}
		if (ret < 0)
			break;
		get_extent_info(path, &extent_start, &extent_size, &extent_flags,
				&extent_gen);

		/* Metadata should not cross stripe boundaries */
		if ((extent_flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) &&
		    does_range_cross_boundary(extent_start, extent_size,
					      logical, map->stripe_len)) {
			btrfs_err(fs_info,
	"scrub: tree block %llu spanning stripes, ignored. logical=%llu",
				  extent_start, logical);
			spin_lock(&sctx->stat_lock);
			sctx->stat.uncorrectable_errors++;
			spin_unlock(&sctx->stat_lock);
			cur_logical += extent_size;
			continue;
		}

		/* Skip hole range which doesn't have any extent */
		cur_logical = max(extent_start, cur_logical);

		/* Truncate the range inside this data stripe */
		extent_size = min(extent_start + extent_size,
				  logical + map->stripe_len) - cur_logical;
		extent_start = cur_logical;
		ASSERT(extent_size <= U32_MAX);

		scrub_parity_mark_sectors_data(sparity, extent_start, extent_size);

		mapped_length = extent_size;
		ret = btrfs_map_block(fs_info, BTRFS_MAP_READ, extent_start,
				      &mapped_length, &bioc, 0);
		if (!ret && (!bioc || mapped_length < extent_size))
			ret = -EIO;
		if (ret) {
			btrfs_put_bioc(bioc);
			scrub_parity_mark_sectors_error(sparity, extent_start,
							extent_size);
			break;
		}
		extent_physical = bioc->stripes[0].physical;
		extent_mirror_num = bioc->mirror_num;
		extent_dev = bioc->stripes[0].dev;
		btrfs_put_bioc(bioc);

		ret = btrfs_lookup_csums_range(csum_root, extent_start,
					       extent_start + extent_size - 1,
					       &sctx->csum_list, 1, false);
		if (ret) {
			scrub_parity_mark_sectors_error(sparity, extent_start,
							extent_size);
			break;
		}

		ret = scrub_extent_for_parity(sparity, extent_start,
					      extent_size, extent_physical,
					      extent_dev, extent_flags,
					      extent_gen, extent_mirror_num);
		scrub_free_csums(sctx);

		if (ret) {
			scrub_parity_mark_sectors_error(sparity, extent_start,
							extent_size);
			break;
		}

		cond_resched();
		cur_logical += extent_size;
	}
	btrfs_release_path(path);
	return ret;
}

static noinline_for_stack int scrub_raid56_parity(struct scrub_ctx *sctx,
						  struct map_lookup *map,
						  struct btrfs_device *sdev,
						  u64 logic_start,
						  u64 logic_end)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_path *path;
	u64 cur_logical;
	int ret;
	struct scrub_parity *sparity;
	int nsectors;

	path = btrfs_alloc_path();
	if (!path) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		spin_unlock(&sctx->stat_lock);
		return -ENOMEM;
	}
	path->search_commit_root = 1;
	path->skip_locking = 1;

	ASSERT(map->stripe_len <= U32_MAX);
	nsectors = map->stripe_len >> fs_info->sectorsize_bits;
	ASSERT(nsectors <= BITS_PER_LONG);
	sparity = kzalloc(sizeof(struct scrub_parity), GFP_NOFS);
	if (!sparity) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		spin_unlock(&sctx->stat_lock);
		btrfs_free_path(path);
		return -ENOMEM;
	}

	ASSERT(map->stripe_len <= U32_MAX);
	sparity->stripe_len = map->stripe_len;
	sparity->nsectors = nsectors;
	sparity->sctx = sctx;
	sparity->scrub_dev = sdev;
	sparity->logic_start = logic_start;
	sparity->logic_end = logic_end;
	refcount_set(&sparity->refs, 1);
	INIT_LIST_HEAD(&sparity->sectors_list);

	ret = 0;
	for (cur_logical = logic_start; cur_logical < logic_end;
	     cur_logical += map->stripe_len) {
		ret = scrub_raid56_data_stripe_for_parity(sctx, sparity, map,
							  sdev, path, cur_logical);
		if (ret < 0)
			break;
	}

	scrub_parity_put(sparity);
	scrub_submit(sctx);
	mutex_lock(&sctx->wr_lock);
	scrub_wr_submit(sctx);
	mutex_unlock(&sctx->wr_lock);

	btrfs_free_path(path);
	return ret < 0 ? ret : 0;
}

static void sync_replace_for_zoned(struct scrub_ctx *sctx)
{
	if (!btrfs_is_zoned(sctx->fs_info))
		return;

	sctx->flush_all_writes = true;
	scrub_submit(sctx);
	mutex_lock(&sctx->wr_lock);
	scrub_wr_submit(sctx);
	mutex_unlock(&sctx->wr_lock);

	wait_event(sctx->list_wait, atomic_read(&sctx->bios_in_flight) == 0);
}

static int sync_write_pointer_for_zoned(struct scrub_ctx *sctx, u64 logical,
					u64 physical, u64 physical_end)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	int ret = 0;

	if (!btrfs_is_zoned(fs_info))
		return 0;

	wait_event(sctx->list_wait, atomic_read(&sctx->bios_in_flight) == 0);

	mutex_lock(&sctx->wr_lock);
	if (sctx->write_pointer < physical_end) {
		ret = btrfs_sync_zone_write_pointer(sctx->wr_tgtdev, logical,
						    physical,
						    sctx->write_pointer);
		if (ret)
			btrfs_err(fs_info,
				  "zoned: failed to recover write pointer");
	}
	mutex_unlock(&sctx->wr_lock);
	btrfs_dev_clear_zone_empty(sctx->wr_tgtdev, physical);

	return ret;
}

/*
 * Scrub one range which can only has simple mirror based profile.
 * (Including all range in SINGLE/DUP/RAID1/RAID1C*, and each stripe in
 *  RAID0/RAID10).
 *
 * Since we may need to handle a subset of block group, we need @logical_start
 * and @logical_length parameter.
 */
static int scrub_simple_mirror(struct scrub_ctx *sctx,
			       struct btrfs_root *extent_root,
			       struct btrfs_root *csum_root,
			       struct btrfs_block_group *bg,
			       struct map_lookup *map,
			       u64 logical_start, u64 logical_length,
			       struct btrfs_device *device,
			       u64 physical, int mirror_num)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	const u64 logical_end = logical_start + logical_length;
	/* An artificial limit, inherit from old scrub behavior */
	const u32 max_length = SZ_64K;
	struct btrfs_path path = { 0 };
	u64 cur_logical = logical_start;
	int ret;

	/* The range must be inside the bg */
	ASSERT(logical_start >= bg->start && logical_end <= bg->start + bg->length);

	path.search_commit_root = 1;
	path.skip_locking = 1;
	/* Go through each extent items inside the logical range */
	while (cur_logical < logical_end) {
		u64 extent_start;
		u64 extent_len;
		u64 extent_flags;
		u64 extent_gen;
		u64 scrub_len;

		/* Canceled? */
		if (atomic_read(&fs_info->scrub_cancel_req) ||
		    atomic_read(&sctx->cancel_req)) {
			ret = -ECANCELED;
			break;
		}
		/* Paused? */
		if (atomic_read(&fs_info->scrub_pause_req)) {
			/* Push queued extents */
			sctx->flush_all_writes = true;
			scrub_submit(sctx);
			mutex_lock(&sctx->wr_lock);
			scrub_wr_submit(sctx);
			mutex_unlock(&sctx->wr_lock);
			wait_event(sctx->list_wait,
				   atomic_read(&sctx->bios_in_flight) == 0);
			sctx->flush_all_writes = false;
			scrub_blocked_if_needed(fs_info);
		}
		/* Block group removed? */
		spin_lock(&bg->lock);
		if (test_bit(BLOCK_GROUP_FLAG_REMOVED, &bg->runtime_flags)) {
			spin_unlock(&bg->lock);
			ret = 0;
			break;
		}
		spin_unlock(&bg->lock);

		ret = find_first_extent_item(extent_root, &path, cur_logical,
					     logical_end - cur_logical);
		if (ret > 0) {
			/* No more extent, just update the accounting */
			sctx->stat.last_physical = physical + logical_length;
			ret = 0;
			break;
		}
		if (ret < 0)
			break;
		get_extent_info(&path, &extent_start, &extent_len,
				&extent_flags, &extent_gen);
		/* Skip hole range which doesn't have any extent */
		cur_logical = max(extent_start, cur_logical);

		/*
		 * Scrub len has three limits:
		 * - Extent size limit
		 * - Scrub range limit
		 *   This is especially imporatant for RAID0/RAID10 to reuse
		 *   this function
		 * - Max scrub size limit
		 */
		scrub_len = min(min(extent_start + extent_len,
				    logical_end), cur_logical + max_length) -
			    cur_logical;

		if (extent_flags & BTRFS_EXTENT_FLAG_DATA) {
			ret = btrfs_lookup_csums_range(csum_root, cur_logical,
					cur_logical + scrub_len - 1,
					&sctx->csum_list, 1, false);
			if (ret)
				break;
		}
		if ((extent_flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) &&
		    does_range_cross_boundary(extent_start, extent_len,
					      logical_start, logical_length)) {
			btrfs_err(fs_info,
"scrub: tree block %llu spanning boundaries, ignored. boundary=[%llu, %llu)",
				  extent_start, logical_start, logical_end);
			spin_lock(&sctx->stat_lock);
			sctx->stat.uncorrectable_errors++;
			spin_unlock(&sctx->stat_lock);
			cur_logical += scrub_len;
			continue;
		}
		ret = scrub_extent(sctx, map, cur_logical, scrub_len,
				   cur_logical - logical_start + physical,
				   device, extent_flags, extent_gen,
				   mirror_num);
		scrub_free_csums(sctx);
		if (ret)
			break;
		if (sctx->is_dev_replace)
			sync_replace_for_zoned(sctx);
		cur_logical += scrub_len;
		/* Don't hold CPU for too long time */
		cond_resched();
	}
	btrfs_release_path(&path);
	return ret;
}

/* Calculate the full stripe length for simple stripe based profiles */
static u64 simple_stripe_full_stripe_len(const struct map_lookup *map)
{
	ASSERT(map->type & (BTRFS_BLOCK_GROUP_RAID0 |
			    BTRFS_BLOCK_GROUP_RAID10));

	return map->num_stripes / map->sub_stripes * map->stripe_len;
}

/* Get the logical bytenr for the stripe */
static u64 simple_stripe_get_logical(struct map_lookup *map,
				     struct btrfs_block_group *bg,
				     int stripe_index)
{
	ASSERT(map->type & (BTRFS_BLOCK_GROUP_RAID0 |
			    BTRFS_BLOCK_GROUP_RAID10));
	ASSERT(stripe_index < map->num_stripes);

	/*
	 * (stripe_index / sub_stripes) gives how many data stripes we need to
	 * skip.
	 */
	return (stripe_index / map->sub_stripes) * map->stripe_len + bg->start;
}

/* Get the mirror number for the stripe */
static int simple_stripe_mirror_num(struct map_lookup *map, int stripe_index)
{
	ASSERT(map->type & (BTRFS_BLOCK_GROUP_RAID0 |
			    BTRFS_BLOCK_GROUP_RAID10));
	ASSERT(stripe_index < map->num_stripes);

	/* For RAID0, it's fixed to 1, for RAID10 it's 0,1,0,1... */
	return stripe_index % map->sub_stripes + 1;
}

static int scrub_simple_stripe(struct scrub_ctx *sctx,
			       struct btrfs_root *extent_root,
			       struct btrfs_root *csum_root,
			       struct btrfs_block_group *bg,
			       struct map_lookup *map,
			       struct btrfs_device *device,
			       int stripe_index)
{
	const u64 logical_increment = simple_stripe_full_stripe_len(map);
	const u64 orig_logical = simple_stripe_get_logical(map, bg, stripe_index);
	const u64 orig_physical = map->stripes[stripe_index].physical;
	const int mirror_num = simple_stripe_mirror_num(map, stripe_index);
	u64 cur_logical = orig_logical;
	u64 cur_physical = orig_physical;
	int ret = 0;

	while (cur_logical < bg->start + bg->length) {
		/*
		 * Inside each stripe, RAID0 is just SINGLE, and RAID10 is
		 * just RAID1, so we can reuse scrub_simple_mirror() to scrub
		 * this stripe.
		 */
		ret = scrub_simple_mirror(sctx, extent_root, csum_root, bg, map,
					  cur_logical, map->stripe_len, device,
					  cur_physical, mirror_num);
		if (ret)
			return ret;
		/* Skip to next stripe which belongs to the target device */
		cur_logical += logical_increment;
		/* For physical offset, we just go to next stripe */
		cur_physical += map->stripe_len;
	}
	return ret;
}

static noinline_for_stack int scrub_stripe(struct scrub_ctx *sctx,
					   struct btrfs_block_group *bg,
					   struct extent_map *em,
					   struct btrfs_device *scrub_dev,
					   int stripe_index)
{
	struct btrfs_path *path;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_root *root;
	struct btrfs_root *csum_root;
	struct blk_plug plug;
	struct map_lookup *map = em->map_lookup;
	const u64 profile = map->type & BTRFS_BLOCK_GROUP_PROFILE_MASK;
	const u64 chunk_logical = bg->start;
	int ret;
	u64 physical = map->stripes[stripe_index].physical;
	const u64 dev_stripe_len = btrfs_calc_stripe_length(em);
	const u64 physical_end = physical + dev_stripe_len;
	u64 logical;
	u64 logic_end;
	/* The logical increment after finishing one stripe */
	u64 increment;
	/* Offset inside the chunk */
	u64 offset;
	u64 stripe_logical;
	u64 stripe_end;
	int stop_loop = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * work on commit root. The related disk blocks are static as
	 * long as COW is applied. This means, it is save to rewrite
	 * them to repair disk errors without any race conditions
	 */
	path->search_commit_root = 1;
	path->skip_locking = 1;
	path->reada = READA_FORWARD;

	wait_event(sctx->list_wait,
		   atomic_read(&sctx->bios_in_flight) == 0);
	scrub_blocked_if_needed(fs_info);

	root = btrfs_extent_root(fs_info, bg->start);
	csum_root = btrfs_csum_root(fs_info, bg->start);

	/*
	 * collect all data csums for the stripe to avoid seeking during
	 * the scrub. This might currently (crc32) end up to be about 1MB
	 */
	blk_start_plug(&plug);

	if (sctx->is_dev_replace &&
	    btrfs_dev_is_sequential(sctx->wr_tgtdev, physical)) {
		mutex_lock(&sctx->wr_lock);
		sctx->write_pointer = physical;
		mutex_unlock(&sctx->wr_lock);
		sctx->flush_all_writes = true;
	}

	/*
	 * There used to be a big double loop to handle all profiles using the
	 * same routine, which grows larger and more gross over time.
	 *
	 * So here we handle each profile differently, so simpler profiles
	 * have simpler scrubbing function.
	 */
	if (!(profile & (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID10 |
			 BTRFS_BLOCK_GROUP_RAID56_MASK))) {
		/*
		 * Above check rules out all complex profile, the remaining
		 * profiles are SINGLE|DUP|RAID1|RAID1C*, which is simple
		 * mirrored duplication without stripe.
		 *
		 * Only @physical and @mirror_num needs to calculated using
		 * @stripe_index.
		 */
		ret = scrub_simple_mirror(sctx, root, csum_root, bg, map,
				bg->start, bg->length, scrub_dev,
				map->stripes[stripe_index].physical,
				stripe_index + 1);
		offset = 0;
		goto out;
	}
	if (profile & (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID10)) {
		ret = scrub_simple_stripe(sctx, root, csum_root, bg, map,
					  scrub_dev, stripe_index);
		offset = map->stripe_len * (stripe_index / map->sub_stripes);
		goto out;
	}

	/* Only RAID56 goes through the old code */
	ASSERT(map->type & BTRFS_BLOCK_GROUP_RAID56_MASK);
	ret = 0;

	/* Calculate the logical end of the stripe */
	get_raid56_logic_offset(physical_end, stripe_index,
				map, &logic_end, NULL);
	logic_end += chunk_logical;

	/* Initialize @offset in case we need to go to out: label */
	get_raid56_logic_offset(physical, stripe_index, map, &offset, NULL);
	increment = map->stripe_len * nr_data_stripes(map);

	/*
	 * Due to the rotation, for RAID56 it's better to iterate each stripe
	 * using their physical offset.
	 */
	while (physical < physical_end) {
		ret = get_raid56_logic_offset(physical, stripe_index, map,
					      &logical, &stripe_logical);
		logical += chunk_logical;
		if (ret) {
			/* it is parity strip */
			stripe_logical += chunk_logical;
			stripe_end = stripe_logical + increment;
			ret = scrub_raid56_parity(sctx, map, scrub_dev,
						  stripe_logical,
						  stripe_end);
			if (ret)
				goto out;
			goto next;
		}

		/*
		 * Now we're at a data stripe, scrub each extents in the range.
		 *
		 * At this stage, if we ignore the repair part, inside each data
		 * stripe it is no different than SINGLE profile.
		 * We can reuse scrub_simple_mirror() here, as the repair part
		 * is still based on @mirror_num.
		 */
		ret = scrub_simple_mirror(sctx, root, csum_root, bg, map,
					  logical, map->stripe_len,
					  scrub_dev, physical, 1);
		if (ret < 0)
			goto out;
next:
		logical += increment;
		physical += map->stripe_len;
		spin_lock(&sctx->stat_lock);
		if (stop_loop)
			sctx->stat.last_physical =
				map->stripes[stripe_index].physical + dev_stripe_len;
		else
			sctx->stat.last_physical = physical;
		spin_unlock(&sctx->stat_lock);
		if (stop_loop)
			break;
	}
out:
	/* push queued extents */
	scrub_submit(sctx);
	mutex_lock(&sctx->wr_lock);
	scrub_wr_submit(sctx);
	mutex_unlock(&sctx->wr_lock);

	blk_finish_plug(&plug);
	btrfs_free_path(path);

	if (sctx->is_dev_replace && ret >= 0) {
		int ret2;

		ret2 = sync_write_pointer_for_zoned(sctx,
				chunk_logical + offset,
				map->stripes[stripe_index].physical,
				physical_end);
		if (ret2)
			ret = ret2;
	}

	return ret < 0 ? ret : 0;
}

static noinline_for_stack int scrub_chunk(struct scrub_ctx *sctx,
					  struct btrfs_block_group *bg,
					  struct btrfs_device *scrub_dev,
					  u64 dev_offset,
					  u64 dev_extent_len)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct extent_map_tree *map_tree = &fs_info->mapping_tree;
	struct map_lookup *map;
	struct extent_map *em;
	int i;
	int ret = 0;

	read_lock(&map_tree->lock);
	em = lookup_extent_mapping(map_tree, bg->start, bg->length);
	read_unlock(&map_tree->lock);

	if (!em) {
		/*
		 * Might have been an unused block group deleted by the cleaner
		 * kthread or relocation.
		 */
		spin_lock(&bg->lock);
		if (!test_bit(BLOCK_GROUP_FLAG_REMOVED, &bg->runtime_flags))
			ret = -EINVAL;
		spin_unlock(&bg->lock);

		return ret;
	}
	if (em->start != bg->start)
		goto out;
	if (em->len < dev_extent_len)
		goto out;

	map = em->map_lookup;
	for (i = 0; i < map->num_stripes; ++i) {
		if (map->stripes[i].dev->bdev == scrub_dev->bdev &&
		    map->stripes[i].physical == dev_offset) {
			ret = scrub_stripe(sctx, bg, em, scrub_dev, i);
			if (ret)
				goto out;
		}
	}
out:
	free_extent_map(em);

	return ret;
}

static int finish_extent_writes_for_zoned(struct btrfs_root *root,
					  struct btrfs_block_group *cache)
{
	struct btrfs_fs_info *fs_info = cache->fs_info;
	struct btrfs_trans_handle *trans;

	if (!btrfs_is_zoned(fs_info))
		return 0;

	btrfs_wait_block_group_reservations(cache);
	btrfs_wait_nocow_writers(cache);
	btrfs_wait_ordered_roots(fs_info, U64_MAX, cache->start, cache->length);

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans))
		return PTR_ERR(trans);
	return btrfs_commit_transaction(trans);
}

static noinline_for_stack
int scrub_enumerate_chunks(struct scrub_ctx *sctx,
			   struct btrfs_device *scrub_dev, u64 start, u64 end)
{
	struct btrfs_dev_extent *dev_extent = NULL;
	struct btrfs_path *path;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_root *root = fs_info->dev_root;
	u64 chunk_offset;
	int ret = 0;
	int ro_set;
	int slot;
	struct extent_buffer *l;
	struct btrfs_key key;
	struct btrfs_key found_key;
	struct btrfs_block_group *cache;
	struct btrfs_dev_replace *dev_replace = &fs_info->dev_replace;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	path->reada = READA_FORWARD;
	path->search_commit_root = 1;
	path->skip_locking = 1;

	key.objectid = scrub_dev->devid;
	key.offset = 0ull;
	key.type = BTRFS_DEV_EXTENT_KEY;

	while (1) {
		u64 dev_extent_len;

		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			break;
		if (ret > 0) {
			if (path->slots[0] >=
			    btrfs_header_nritems(path->nodes[0])) {
				ret = btrfs_next_leaf(root, path);
				if (ret < 0)
					break;
				if (ret > 0) {
					ret = 0;
					break;
				}
			} else {
				ret = 0;
			}
		}

		l = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(l, &found_key, slot);

		if (found_key.objectid != scrub_dev->devid)
			break;

		if (found_key.type != BTRFS_DEV_EXTENT_KEY)
			break;

		if (found_key.offset >= end)
			break;

		if (found_key.offset < key.offset)
			break;

		dev_extent = btrfs_item_ptr(l, slot, struct btrfs_dev_extent);
		dev_extent_len = btrfs_dev_extent_length(l, dev_extent);

		if (found_key.offset + dev_extent_len <= start)
			goto skip;

		chunk_offset = btrfs_dev_extent_chunk_offset(l, dev_extent);

		/*
		 * get a reference on the corresponding block group to prevent
		 * the chunk from going away while we scrub it
		 */
		cache = btrfs_lookup_block_group(fs_info, chunk_offset);

		/* some chunks are removed but not committed to disk yet,
		 * continue scrubbing */
		if (!cache)
			goto skip;

		ASSERT(cache->start <= chunk_offset);
		/*
		 * We are using the commit root to search for device extents, so
		 * that means we could have found a device extent item from a
		 * block group that was deleted in the current transaction. The
		 * logical start offset of the deleted block group, stored at
		 * @chunk_offset, might be part of the logical address range of
		 * a new block group (which uses different physical extents).
		 * In this case btrfs_lookup_block_group() has returned the new
		 * block group, and its start address is less than @chunk_offset.
		 *
		 * We skip such new block groups, because it's pointless to
		 * process them, as we won't find their extents because we search
		 * for them using the commit root of the extent tree. For a device
		 * replace it's also fine to skip it, we won't miss copying them
		 * to the target device because we have the write duplication
		 * setup through the regular write path (by btrfs_map_block()),
		 * and we have committed a transaction when we started the device
		 * replace, right after setting up the device replace state.
		 */
		if (cache->start < chunk_offset) {
			btrfs_put_block_group(cache);
			goto skip;
		}

		if (sctx->is_dev_replace && btrfs_is_zoned(fs_info)) {
			if (!test_bit(BLOCK_GROUP_FLAG_TO_COPY, &cache->runtime_flags)) {
				btrfs_put_block_group(cache);
				goto skip;
			}
		}

		/*
		 * Make sure that while we are scrubbing the corresponding block
		 * group doesn't get its logical address and its device extents
		 * reused for another block group, which can possibly be of a
		 * different type and different profile. We do this to prevent
		 * false error detections and crashes due to bogus attempts to
		 * repair extents.
		 */
		spin_lock(&cache->lock);
		if (test_bit(BLOCK_GROUP_FLAG_REMOVED, &cache->runtime_flags)) {
			spin_unlock(&cache->lock);
			btrfs_put_block_group(cache);
			goto skip;
		}
		btrfs_freeze_block_group(cache);
		spin_unlock(&cache->lock);

		/*
		 * we need call btrfs_inc_block_group_ro() with scrubs_paused,
		 * to avoid deadlock caused by:
		 * btrfs_inc_block_group_ro()
		 * -> btrfs_wait_for_commit()
		 * -> btrfs_commit_transaction()
		 * -> btrfs_scrub_pause()
		 */
		scrub_pause_on(fs_info);

		/*
		 * Don't do chunk preallocation for scrub.
		 *
		 * This is especially important for SYSTEM bgs, or we can hit
		 * -EFBIG from btrfs_finish_chunk_alloc() like:
		 * 1. The only SYSTEM bg is marked RO.
		 *    Since SYSTEM bg is small, that's pretty common.
		 * 2. New SYSTEM bg will be allocated
		 *    Due to regular version will allocate new chunk.
		 * 3. New SYSTEM bg is empty and will get cleaned up
		 *    Before cleanup really happens, it's marked RO again.
		 * 4. Empty SYSTEM bg get scrubbed
		 *    We go back to 2.
		 *
		 * This can easily boost the amount of SYSTEM chunks if cleaner
		 * thread can't be triggered fast enough, and use up all space
		 * of btrfs_super_block::sys_chunk_array
		 *
		 * While for dev replace, we need to try our best to mark block
		 * group RO, to prevent race between:
		 * - Write duplication
		 *   Contains latest data
		 * - Scrub copy
		 *   Contains data from commit tree
		 *
		 * If target block group is not marked RO, nocow writes can
		 * be overwritten by scrub copy, causing data corruption.
		 * So for dev-replace, it's not allowed to continue if a block
		 * group is not RO.
		 */
		ret = btrfs_inc_block_group_ro(cache, sctx->is_dev_replace);
		if (!ret && sctx->is_dev_replace) {
			ret = finish_extent_writes_for_zoned(root, cache);
			if (ret) {
				btrfs_dec_block_group_ro(cache);
				scrub_pause_off(fs_info);
				btrfs_put_block_group(cache);
				break;
			}
		}

		if (ret == 0) {
			ro_set = 1;
		} else if (ret == -ENOSPC && !sctx->is_dev_replace) {
			/*
			 * btrfs_inc_block_group_ro return -ENOSPC when it
			 * failed in creating new chunk for metadata.
			 * It is not a problem for scrub, because
			 * metadata are always cowed, and our scrub paused
			 * commit_transactions.
			 */
			ro_set = 0;
		} else if (ret == -ETXTBSY) {
			btrfs_warn(fs_info,
		   "skipping scrub of block group %llu due to active swapfile",
				   cache->start);
			scrub_pause_off(fs_info);
			ret = 0;
			goto skip_unfreeze;
		} else {
			btrfs_warn(fs_info,
				   "failed setting block group ro: %d", ret);
			btrfs_unfreeze_block_group(cache);
			btrfs_put_block_group(cache);
			scrub_pause_off(fs_info);
			break;
		}

		/*
		 * Now the target block is marked RO, wait for nocow writes to
		 * finish before dev-replace.
		 * COW is fine, as COW never overwrites extents in commit tree.
		 */
		if (sctx->is_dev_replace) {
			btrfs_wait_nocow_writers(cache);
			btrfs_wait_ordered_roots(fs_info, U64_MAX, cache->start,
					cache->length);
		}

		scrub_pause_off(fs_info);
		down_write(&dev_replace->rwsem);
		dev_replace->cursor_right = found_key.offset + dev_extent_len;
		dev_replace->cursor_left = found_key.offset;
		dev_replace->item_needs_writeback = 1;
		up_write(&dev_replace->rwsem);

		ret = scrub_chunk(sctx, cache, scrub_dev, found_key.offset,
				  dev_extent_len);

		/*
		 * flush, submit all pending read and write bios, afterwards
		 * wait for them.
		 * Note that in the dev replace case, a read request causes
		 * write requests that are submitted in the read completion
		 * worker. Therefore in the current situation, it is required
		 * that all write requests are flushed, so that all read and
		 * write requests are really completed when bios_in_flight
		 * changes to 0.
		 */
		sctx->flush_all_writes = true;
		scrub_submit(sctx);
		mutex_lock(&sctx->wr_lock);
		scrub_wr_submit(sctx);
		mutex_unlock(&sctx->wr_lock);

		wait_event(sctx->list_wait,
			   atomic_read(&sctx->bios_in_flight) == 0);

		scrub_pause_on(fs_info);

		/*
		 * must be called before we decrease @scrub_paused.
		 * make sure we don't block transaction commit while
		 * we are waiting pending workers finished.
		 */
		wait_event(sctx->list_wait,
			   atomic_read(&sctx->workers_pending) == 0);
		sctx->flush_all_writes = false;

		scrub_pause_off(fs_info);

		if (sctx->is_dev_replace &&
		    !btrfs_finish_block_group_to_copy(dev_replace->srcdev,
						      cache, found_key.offset))
			ro_set = 0;

		down_write(&dev_replace->rwsem);
		dev_replace->cursor_left = dev_replace->cursor_right;
		dev_replace->item_needs_writeback = 1;
		up_write(&dev_replace->rwsem);

		if (ro_set)
			btrfs_dec_block_group_ro(cache);

		/*
		 * We might have prevented the cleaner kthread from deleting
		 * this block group if it was already unused because we raced
		 * and set it to RO mode first. So add it back to the unused
		 * list, otherwise it might not ever be deleted unless a manual
		 * balance is triggered or it becomes used and unused again.
		 */
		spin_lock(&cache->lock);
		if (!test_bit(BLOCK_GROUP_FLAG_REMOVED, &cache->runtime_flags) &&
		    !cache->ro && cache->reserved == 0 && cache->used == 0) {
			spin_unlock(&cache->lock);
			if (btrfs_test_opt(fs_info, DISCARD_ASYNC))
				btrfs_discard_queue_work(&fs_info->discard_ctl,
							 cache);
			else
				btrfs_mark_bg_unused(cache);
		} else {
			spin_unlock(&cache->lock);
		}
skip_unfreeze:
		btrfs_unfreeze_block_group(cache);
		btrfs_put_block_group(cache);
		if (ret)
			break;
		if (sctx->is_dev_replace &&
		    atomic64_read(&dev_replace->num_write_errors) > 0) {
			ret = -EIO;
			break;
		}
		if (sctx->stat.malloc_errors > 0) {
			ret = -ENOMEM;
			break;
		}
skip:
		key.offset = found_key.offset + dev_extent_len;
		btrfs_release_path(path);
	}

	btrfs_free_path(path);

	return ret;
}

static noinline_for_stack int scrub_supers(struct scrub_ctx *sctx,
					   struct btrfs_device *scrub_dev)
{
	int	i;
	u64	bytenr;
	u64	gen;
	int	ret;
	struct btrfs_fs_info *fs_info = sctx->fs_info;

	if (BTRFS_FS_ERROR(fs_info))
		return -EROFS;

	/* Seed devices of a new filesystem has their own generation. */
	if (scrub_dev->fs_devices != fs_info->fs_devices)
		gen = scrub_dev->generation;
	else
		gen = fs_info->last_trans_committed;

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		bytenr = btrfs_sb_offset(i);
		if (bytenr + BTRFS_SUPER_INFO_SIZE >
		    scrub_dev->commit_total_bytes)
			break;
		if (!btrfs_check_super_location(scrub_dev, bytenr))
			continue;

		ret = scrub_sectors(sctx, bytenr, BTRFS_SUPER_INFO_SIZE, bytenr,
				    scrub_dev, BTRFS_EXTENT_FLAG_SUPER, gen, i,
				    NULL, bytenr);
		if (ret)
			return ret;
	}
	wait_event(sctx->list_wait, atomic_read(&sctx->bios_in_flight) == 0);

	return 0;
}

static void scrub_workers_put(struct btrfs_fs_info *fs_info)
{
	if (refcount_dec_and_mutex_lock(&fs_info->scrub_workers_refcnt,
					&fs_info->scrub_lock)) {
		struct workqueue_struct *scrub_workers = fs_info->scrub_workers;
		struct workqueue_struct *scrub_wr_comp =
						fs_info->scrub_wr_completion_workers;
		struct workqueue_struct *scrub_parity =
						fs_info->scrub_parity_workers;

		fs_info->scrub_workers = NULL;
		fs_info->scrub_wr_completion_workers = NULL;
		fs_info->scrub_parity_workers = NULL;
		mutex_unlock(&fs_info->scrub_lock);

		if (scrub_workers)
			destroy_workqueue(scrub_workers);
		if (scrub_wr_comp)
			destroy_workqueue(scrub_wr_comp);
		if (scrub_parity)
			destroy_workqueue(scrub_parity);
	}
}

/*
 * get a reference count on fs_info->scrub_workers. start worker if necessary
 */
static noinline_for_stack int scrub_workers_get(struct btrfs_fs_info *fs_info,
						int is_dev_replace)
{
	struct workqueue_struct *scrub_workers = NULL;
	struct workqueue_struct *scrub_wr_comp = NULL;
	struct workqueue_struct *scrub_parity = NULL;
	unsigned int flags = WQ_FREEZABLE | WQ_UNBOUND;
	int max_active = fs_info->thread_pool_size;
	int ret = -ENOMEM;

	if (refcount_inc_not_zero(&fs_info->scrub_workers_refcnt))
		return 0;

	scrub_workers = alloc_workqueue("btrfs-scrub", flags,
					is_dev_replace ? 1 : max_active);
	if (!scrub_workers)
		goto fail_scrub_workers;

	scrub_wr_comp = alloc_workqueue("btrfs-scrubwrc", flags, max_active);
	if (!scrub_wr_comp)
		goto fail_scrub_wr_completion_workers;

	scrub_parity = alloc_workqueue("btrfs-scrubparity", flags, max_active);
	if (!scrub_parity)
		goto fail_scrub_parity_workers;

	mutex_lock(&fs_info->scrub_lock);
	if (refcount_read(&fs_info->scrub_workers_refcnt) == 0) {
		ASSERT(fs_info->scrub_workers == NULL &&
		       fs_info->scrub_wr_completion_workers == NULL &&
		       fs_info->scrub_parity_workers == NULL);
		fs_info->scrub_workers = scrub_workers;
		fs_info->scrub_wr_completion_workers = scrub_wr_comp;
		fs_info->scrub_parity_workers = scrub_parity;
		refcount_set(&fs_info->scrub_workers_refcnt, 1);
		mutex_unlock(&fs_info->scrub_lock);
		return 0;
	}
	/* Other thread raced in and created the workers for us */
	refcount_inc(&fs_info->scrub_workers_refcnt);
	mutex_unlock(&fs_info->scrub_lock);

	ret = 0;
	destroy_workqueue(scrub_parity);
fail_scrub_parity_workers:
	destroy_workqueue(scrub_wr_comp);
fail_scrub_wr_completion_workers:
	destroy_workqueue(scrub_workers);
fail_scrub_workers:
	return ret;
}

int btrfs_scrub_dev(struct btrfs_fs_info *fs_info, u64 devid, u64 start,
		    u64 end, struct btrfs_scrub_progress *progress,
		    int readonly, int is_dev_replace)
{
	struct btrfs_dev_lookup_args args = { .devid = devid };
	struct scrub_ctx *sctx;
	int ret;
	struct btrfs_device *dev;
	unsigned int nofs_flag;
	bool need_commit = false;

	if (btrfs_fs_closing(fs_info))
		return -EAGAIN;

	/* At mount time we have ensured nodesize is in the range of [4K, 64K]. */
	ASSERT(fs_info->nodesize <= BTRFS_STRIPE_LEN);

	/*
	 * SCRUB_MAX_SECTORS_PER_BLOCK is calculated using the largest possible
	 * value (max nodesize / min sectorsize), thus nodesize should always
	 * be fine.
	 */
	ASSERT(fs_info->nodesize <=
	       SCRUB_MAX_SECTORS_PER_BLOCK << fs_info->sectorsize_bits);

	/* Allocate outside of device_list_mutex */
	sctx = scrub_setup_ctx(fs_info, is_dev_replace);
	if (IS_ERR(sctx))
		return PTR_ERR(sctx);

	ret = scrub_workers_get(fs_info, is_dev_replace);
	if (ret)
		goto out_free_ctx;

	mutex_lock(&fs_info->fs_devices->device_list_mutex);
	dev = btrfs_find_device(fs_info->fs_devices, &args);
	if (!dev || (test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state) &&
		     !is_dev_replace)) {
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);
		ret = -ENODEV;
		goto out;
	}

	if (!is_dev_replace && !readonly &&
	    !test_bit(BTRFS_DEV_STATE_WRITEABLE, &dev->dev_state)) {
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);
		btrfs_err_in_rcu(fs_info,
			"scrub on devid %llu: filesystem on %s is not writable",
				 devid, rcu_str_deref(dev->name));
		ret = -EROFS;
		goto out;
	}

	mutex_lock(&fs_info->scrub_lock);
	if (!test_bit(BTRFS_DEV_STATE_IN_FS_METADATA, &dev->dev_state) ||
	    test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &dev->dev_state)) {
		mutex_unlock(&fs_info->scrub_lock);
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);
		ret = -EIO;
		goto out;
	}

	down_read(&fs_info->dev_replace.rwsem);
	if (dev->scrub_ctx ||
	    (!is_dev_replace &&
	     btrfs_dev_replace_is_ongoing(&fs_info->dev_replace))) {
		up_read(&fs_info->dev_replace.rwsem);
		mutex_unlock(&fs_info->scrub_lock);
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);
		ret = -EINPROGRESS;
		goto out;
	}
	up_read(&fs_info->dev_replace.rwsem);

	sctx->readonly = readonly;
	dev->scrub_ctx = sctx;
	mutex_unlock(&fs_info->fs_devices->device_list_mutex);

	/*
	 * checking @scrub_pause_req here, we can avoid
	 * race between committing transaction and scrubbing.
	 */
	__scrub_blocked_if_needed(fs_info);
	atomic_inc(&fs_info->scrubs_running);
	mutex_unlock(&fs_info->scrub_lock);

	/*
	 * In order to avoid deadlock with reclaim when there is a transaction
	 * trying to pause scrub, make sure we use GFP_NOFS for all the
	 * allocations done at btrfs_scrub_sectors() and scrub_sectors_for_parity()
	 * invoked by our callees. The pausing request is done when the
	 * transaction commit starts, and it blocks the transaction until scrub
	 * is paused (done at specific points at scrub_stripe() or right above
	 * before incrementing fs_info->scrubs_running).
	 */
	nofs_flag = memalloc_nofs_save();
	if (!is_dev_replace) {
		u64 old_super_errors;

		spin_lock(&sctx->stat_lock);
		old_super_errors = sctx->stat.super_errors;
		spin_unlock(&sctx->stat_lock);

		btrfs_info(fs_info, "scrub: started on devid %llu", devid);
		/*
		 * by holding device list mutex, we can
		 * kick off writing super in log tree sync.
		 */
		mutex_lock(&fs_info->fs_devices->device_list_mutex);
		ret = scrub_supers(sctx, dev);
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);

		spin_lock(&sctx->stat_lock);
		/*
		 * Super block errors found, but we can not commit transaction
		 * at current context, since btrfs_commit_transaction() needs
		 * to pause the current running scrub (hold by ourselves).
		 */
		if (sctx->stat.super_errors > old_super_errors && !sctx->readonly)
			need_commit = true;
		spin_unlock(&sctx->stat_lock);
	}

	if (!ret)
		ret = scrub_enumerate_chunks(sctx, dev, start, end);
	memalloc_nofs_restore(nofs_flag);

	wait_event(sctx->list_wait, atomic_read(&sctx->bios_in_flight) == 0);
	atomic_dec(&fs_info->scrubs_running);
	wake_up(&fs_info->scrub_pause_wait);

	wait_event(sctx->list_wait, atomic_read(&sctx->workers_pending) == 0);

	if (progress)
		memcpy(progress, &sctx->stat, sizeof(*progress));

	if (!is_dev_replace)
		btrfs_info(fs_info, "scrub: %s on devid %llu with status: %d",
			ret ? "not finished" : "finished", devid, ret);

	mutex_lock(&fs_info->scrub_lock);
	dev->scrub_ctx = NULL;
	mutex_unlock(&fs_info->scrub_lock);

	scrub_workers_put(fs_info);
	scrub_put_ctx(sctx);

	/*
	 * We found some super block errors before, now try to force a
	 * transaction commit, as scrub has finished.
	 */
	if (need_commit) {
		struct btrfs_trans_handle *trans;

		trans = btrfs_start_transaction(fs_info->tree_root, 0);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			btrfs_err(fs_info,
	"scrub: failed to start transaction to fix super block errors: %d", ret);
			return ret;
		}
		ret = btrfs_commit_transaction(trans);
		if (ret < 0)
			btrfs_err(fs_info,
	"scrub: failed to commit transaction to fix super block errors: %d", ret);
	}
	return ret;
out:
	scrub_workers_put(fs_info);
out_free_ctx:
	scrub_free_ctx(sctx);

	return ret;
}

void btrfs_scrub_pause(struct btrfs_fs_info *fs_info)
{
	mutex_lock(&fs_info->scrub_lock);
	atomic_inc(&fs_info->scrub_pause_req);
	while (atomic_read(&fs_info->scrubs_paused) !=
	       atomic_read(&fs_info->scrubs_running)) {
		mutex_unlock(&fs_info->scrub_lock);
		wait_event(fs_info->scrub_pause_wait,
			   atomic_read(&fs_info->scrubs_paused) ==
			   atomic_read(&fs_info->scrubs_running));
		mutex_lock(&fs_info->scrub_lock);
	}
	mutex_unlock(&fs_info->scrub_lock);
}

void btrfs_scrub_continue(struct btrfs_fs_info *fs_info)
{
	atomic_dec(&fs_info->scrub_pause_req);
	wake_up(&fs_info->scrub_pause_wait);
}

int btrfs_scrub_cancel(struct btrfs_fs_info *fs_info)
{
	mutex_lock(&fs_info->scrub_lock);
	if (!atomic_read(&fs_info->scrubs_running)) {
		mutex_unlock(&fs_info->scrub_lock);
		return -ENOTCONN;
	}

	atomic_inc(&fs_info->scrub_cancel_req);
	while (atomic_read(&fs_info->scrubs_running)) {
		mutex_unlock(&fs_info->scrub_lock);
		wait_event(fs_info->scrub_pause_wait,
			   atomic_read(&fs_info->scrubs_running) == 0);
		mutex_lock(&fs_info->scrub_lock);
	}
	atomic_dec(&fs_info->scrub_cancel_req);
	mutex_unlock(&fs_info->scrub_lock);

	return 0;
}

int btrfs_scrub_cancel_dev(struct btrfs_device *dev)
{
	struct btrfs_fs_info *fs_info = dev->fs_info;
	struct scrub_ctx *sctx;

	mutex_lock(&fs_info->scrub_lock);
	sctx = dev->scrub_ctx;
	if (!sctx) {
		mutex_unlock(&fs_info->scrub_lock);
		return -ENOTCONN;
	}
	atomic_inc(&sctx->cancel_req);
	while (dev->scrub_ctx) {
		mutex_unlock(&fs_info->scrub_lock);
		wait_event(fs_info->scrub_pause_wait,
			   dev->scrub_ctx == NULL);
		mutex_lock(&fs_info->scrub_lock);
	}
	mutex_unlock(&fs_info->scrub_lock);

	return 0;
}

int btrfs_scrub_progress(struct btrfs_fs_info *fs_info, u64 devid,
			 struct btrfs_scrub_progress *progress)
{
	struct btrfs_dev_lookup_args args = { .devid = devid };
	struct btrfs_device *dev;
	struct scrub_ctx *sctx = NULL;

	mutex_lock(&fs_info->fs_devices->device_list_mutex);
	dev = btrfs_find_device(fs_info->fs_devices, &args);
	if (dev)
		sctx = dev->scrub_ctx;
	if (sctx)
		memcpy(progress, &sctx->stat, sizeof(*progress));
	mutex_unlock(&fs_info->fs_devices->device_list_mutex);

	return dev ? (sctx ? 0 : -ENOTCONN) : -ENODEV;
}

static void scrub_find_good_copy(struct btrfs_fs_info *fs_info,
				 u64 extent_logical, u32 extent_len,
				 u64 *extent_physical,
				 struct btrfs_device **extent_dev,
				 int *extent_mirror_num)
{
	u64 mapped_length;
	struct btrfs_io_context *bioc = NULL;
	int ret;

	mapped_length = extent_len;
	ret = btrfs_map_block(fs_info, BTRFS_MAP_READ, extent_logical,
			      &mapped_length, &bioc, 0);
	if (ret || !bioc || mapped_length < extent_len ||
	    !bioc->stripes[0].dev->bdev) {
		btrfs_put_bioc(bioc);
		return;
	}

	*extent_physical = bioc->stripes[0].physical;
	*extent_mirror_num = bioc->mirror_num;
	*extent_dev = bioc->stripes[0].dev;
	btrfs_put_bioc(bioc);
}
