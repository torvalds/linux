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
#define SCRUB_PAGES_PER_BIO	32	/* 128KiB per bio for x86 */
#define SCRUB_BIOS_PER_SCTX	64	/* 8MiB per device in flight for x86 */

/*
 * The following value times PAGE_SIZE needs to be large enough to match the
 * largest node/leaf/sector size that shall be supported.
 */
#define SCRUB_MAX_PAGES_PER_BLOCK	(BTRFS_MAX_METADATA_BLOCKSIZE / SZ_4K)

struct scrub_recover {
	refcount_t		refs;
	struct btrfs_io_context	*bioc;
	u64			map_length;
};

struct scrub_page {
	struct scrub_block	*sblock;
	struct page		*page;
	struct btrfs_device	*dev;
	struct list_head	list;
	u64			flags;  /* extent flags */
	u64			generation;
	u64			logical;
	u64			physical;
	u64			physical_for_dev_replace;
	atomic_t		refs;
	u8			mirror_num;
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
	struct scrub_page	*pagev[SCRUB_PAGES_PER_BIO];
	int			page_count;
	int			next_free;
	struct btrfs_work	work;
};

struct scrub_block {
	struct scrub_page	*pagev[SCRUB_MAX_PAGES_PER_BLOCK];
	int			page_count;
	atomic_t		outstanding_pages;
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
	struct btrfs_work	work;
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

	struct list_head	spages;

	/* Work of parity check and repair */
	struct btrfs_work	work;

	/* Mark the parity blocks which have data */
	unsigned long		*dbitmap;

	/*
	 * Mark the parity blocks which have data, but errors happen when
	 * read data or check data
	 */
	unsigned long		*ebitmap;

	unsigned long		bitmap[];
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
	int			pages_per_bio;

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

static int scrub_setup_recheck_block(struct scrub_block *original_sblock,
				     struct scrub_block *sblocks_for_recheck);
static void scrub_recheck_block(struct btrfs_fs_info *fs_info,
				struct scrub_block *sblock,
				int retry_failed_mirror);
static void scrub_recheck_block_checksum(struct scrub_block *sblock);
static int scrub_repair_block_from_good_copy(struct scrub_block *sblock_bad,
					     struct scrub_block *sblock_good);
static int scrub_repair_page_from_good_copy(struct scrub_block *sblock_bad,
					    struct scrub_block *sblock_good,
					    int page_num, int force_write);
static void scrub_write_block_to_dev_replace(struct scrub_block *sblock);
static int scrub_write_page_to_dev_replace(struct scrub_block *sblock,
					   int page_num);
static int scrub_checksum_data(struct scrub_block *sblock);
static int scrub_checksum_tree_block(struct scrub_block *sblock);
static int scrub_checksum_super(struct scrub_block *sblock);
static void scrub_block_put(struct scrub_block *sblock);
static void scrub_page_get(struct scrub_page *spage);
static void scrub_page_put(struct scrub_page *spage);
static void scrub_parity_get(struct scrub_parity *sparity);
static void scrub_parity_put(struct scrub_parity *sparity);
static int scrub_pages(struct scrub_ctx *sctx, u64 logical, u32 len,
		       u64 physical, struct btrfs_device *dev, u64 flags,
		       u64 gen, int mirror_num, u8 *csum,
		       u64 physical_for_dev_replace);
static void scrub_bio_end_io(struct bio *bio);
static void scrub_bio_end_io_worker(struct btrfs_work *work);
static void scrub_block_complete(struct scrub_block *sblock);
static void scrub_remap_extent(struct btrfs_fs_info *fs_info,
			       u64 extent_logical, u32 extent_len,
			       u64 *extent_physical,
			       struct btrfs_device **extent_dev,
			       int *extent_mirror_num);
static int scrub_add_page_to_wr_bio(struct scrub_ctx *sctx,
				    struct scrub_page *spage);
static void scrub_wr_submit(struct scrub_ctx *sctx);
static void scrub_wr_bio_end_io(struct bio *bio);
static void scrub_wr_bio_end_io_worker(struct btrfs_work *work);
static void scrub_put_ctx(struct scrub_ctx *sctx);

static inline int scrub_is_page_on_raid56(struct scrub_page *spage)
{
	return spage->recover &&
	       (spage->recover->bioc->map_type & BTRFS_BLOCK_GROUP_RAID56_MASK);
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

		for (i = 0; i < sbio->page_count; i++) {
			WARN_ON(!sbio->pagev[i]->page);
			scrub_block_put(sbio->pagev[i]->sblock);
		}
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
	sctx->pages_per_bio = SCRUB_PAGES_PER_BIO;
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
		sbio->page_count = 0;
		btrfs_init_work(&sbio->work, scrub_bio_end_io_worker, NULL,
				NULL);

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

	WARN_ON(sblock->page_count < 1);
	dev = sblock->pagev[0]->dev;
	fs_info = sblock->sctx->fs_info;

	path = btrfs_alloc_path();
	if (!path)
		return;

	swarn.physical = sblock->pagev[0]->physical;
	swarn.logical = sblock->pagev[0]->logical;
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
 * pages failed or the bio failed to read, e.g. with EIO. In the latter
 * case, this function handles all pages in the bio, even though only one
 * may be bad.
 * The goal of this function is to repair the errored block by using the
 * contents of one of the mirrors.
 */
static int scrub_handle_errored_block(struct scrub_block *sblock_to_check)
{
	struct scrub_ctx *sctx = sblock_to_check->sctx;
	struct btrfs_device *dev;
	struct btrfs_fs_info *fs_info;
	u64 logical;
	unsigned int failed_mirror_index;
	unsigned int is_metadata;
	unsigned int have_csum;
	struct scrub_block *sblocks_for_recheck; /* holds one for each mirror */
	struct scrub_block *sblock_bad;
	int ret;
	int mirror_index;
	int page_num;
	int success;
	bool full_stripe_locked;
	unsigned int nofs_flag;
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);

	BUG_ON(sblock_to_check->page_count < 1);
	fs_info = sctx->fs_info;
	if (sblock_to_check->pagev[0]->flags & BTRFS_EXTENT_FLAG_SUPER) {
		/*
		 * if we find an error in a super block, we just report it.
		 * They will get written with the next transaction commit
		 * anyway
		 */
		spin_lock(&sctx->stat_lock);
		++sctx->stat.super_errors;
		spin_unlock(&sctx->stat_lock);
		return 0;
	}
	logical = sblock_to_check->pagev[0]->logical;
	BUG_ON(sblock_to_check->pagev[0]->mirror_num < 1);
	failed_mirror_index = sblock_to_check->pagev[0]->mirror_num - 1;
	is_metadata = !(sblock_to_check->pagev[0]->flags &
			BTRFS_EXTENT_FLAG_DATA);
	have_csum = sblock_to_check->pagev[0]->have_csum;
	dev = sblock_to_check->pagev[0]->dev;

	if (!sctx->is_dev_replace && btrfs_repair_one_zone(fs_info, logical))
		return 0;

	/*
	 * We must use GFP_NOFS because the scrub task might be waiting for a
	 * worker task executing this function and in turn a transaction commit
	 * might be waiting the scrub task to pause (which needs to wait for all
	 * the worker tasks to complete before pausing).
	 * We do allocations in the workers through insert_full_stripe_lock()
	 * and scrub_add_page_to_wr_bio(), which happens down the call chain of
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

	sblocks_for_recheck = kcalloc(BTRFS_MAX_MIRRORS,
				      sizeof(*sblocks_for_recheck), GFP_KERNEL);
	if (!sblocks_for_recheck) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		sctx->stat.read_errors++;
		sctx->stat.uncorrectable_errors++;
		spin_unlock(&sctx->stat_lock);
		btrfs_dev_stat_inc_and_print(dev, BTRFS_DEV_STAT_READ_ERRS);
		goto out;
	}

	/* setup the context, map the logical blocks and alloc the pages */
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
	sblock_bad = sblocks_for_recheck + failed_mirror_index;

	/* build and submit the bios for the failed mirror, check checksums */
	scrub_recheck_block(fs_info, sblock_bad, 1);

	if (!sblock_bad->header_error && !sblock_bad->checksum_error &&
	    sblock_bad->no_io_error_seen) {
		/*
		 * the error disappeared after reading page by page, or
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
	 * checksum is present, only those pages are rewritten that had
	 * an I/O error in the block to be repaired, since it cannot be
	 * determined, which copy of the other pages is better (and it
	 * could happen otherwise that a correct page would be
	 * overwritten by a bad one).
	 */
	for (mirror_index = 0; ;mirror_index++) {
		struct scrub_block *sblock_other;

		if (mirror_index == failed_mirror_index)
			continue;

		/* raid56's mirror can be more than BTRFS_MAX_MIRRORS */
		if (!scrub_is_page_on_raid56(sblock_bad->pagev[0])) {
			if (mirror_index >= BTRFS_MAX_MIRRORS)
				break;
			if (!sblocks_for_recheck[mirror_index].page_count)
				break;

			sblock_other = sblocks_for_recheck + mirror_index;
		} else {
			struct scrub_recover *r = sblock_bad->pagev[0]->recover;
			int max_allowed = r->bioc->num_stripes - r->bioc->num_tgtdevs;

			if (mirror_index >= max_allowed)
				break;
			if (!sblocks_for_recheck[1].page_count)
				break;

			ASSERT(failed_mirror_index == 0);
			sblock_other = sblocks_for_recheck + 1;
			sblock_other->pagev[0]->mirror_num = 1 + mirror_index;
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
	for (page_num = 0; page_num < sblock_bad->page_count;
	     page_num++) {
		struct scrub_page *spage_bad = sblock_bad->pagev[page_num];
		struct scrub_block *sblock_other = NULL;

		/* skip no-io-error page in scrub */
		if (!spage_bad->io_error && !sctx->is_dev_replace)
			continue;

		if (scrub_is_page_on_raid56(sblock_bad->pagev[0])) {
			/*
			 * In case of dev replace, if raid56 rebuild process
			 * didn't work out correct data, then copy the content
			 * in sblock_bad to make sure target device is identical
			 * to source device, instead of writing garbage data in
			 * sblock_for_recheck array to target device.
			 */
			sblock_other = NULL;
		} else if (spage_bad->io_error) {
			/* try to find no-io-error page in mirrors */
			for (mirror_index = 0;
			     mirror_index < BTRFS_MAX_MIRRORS &&
			     sblocks_for_recheck[mirror_index].page_count > 0;
			     mirror_index++) {
				if (!sblocks_for_recheck[mirror_index].
				    pagev[page_num]->io_error) {
					sblock_other = sblocks_for_recheck +
						       mirror_index;
					break;
				}
			}
			if (!sblock_other)
				success = 0;
		}

		if (sctx->is_dev_replace) {
			/*
			 * did not find a mirror to fetch the page
			 * from. scrub_write_page_to_dev_replace()
			 * handles this case (page->io_error), by
			 * filling the block with zeros before
			 * submitting the write request
			 */
			if (!sblock_other)
				sblock_other = sblock_bad;

			if (scrub_write_page_to_dev_replace(sblock_other,
							    page_num) != 0) {
				atomic64_inc(
					&fs_info->dev_replace.num_write_errors);
				success = 0;
			}
		} else if (sblock_other) {
			ret = scrub_repair_page_from_good_copy(sblock_bad,
							       sblock_other,
							       page_num, 0);
			if (0 == ret)
				spage_bad->io_error = 0;
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
	if (sblocks_for_recheck) {
		for (mirror_index = 0; mirror_index < BTRFS_MAX_MIRRORS;
		     mirror_index++) {
			struct scrub_block *sblock = sblocks_for_recheck +
						     mirror_index;
			struct scrub_recover *recover;
			int page_index;

			for (page_index = 0; page_index < sblock->page_count;
			     page_index++) {
				sblock->pagev[page_index]->sblock = NULL;
				recover = sblock->pagev[page_index]->recover;
				if (recover) {
					scrub_put_recover(fs_info, recover);
					sblock->pagev[page_index]->recover =
									NULL;
				}
				scrub_page_put(sblock->pagev[page_index]);
			}
		}
		kfree(sblocks_for_recheck);
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
						 u64 mapped_length,
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
			    logical < raid_map[i] + mapped_length)
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
				     struct scrub_block *sblocks_for_recheck)
{
	struct scrub_ctx *sctx = original_sblock->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	u64 length = original_sblock->page_count * fs_info->sectorsize;
	u64 logical = original_sblock->pagev[0]->logical;
	u64 generation = original_sblock->pagev[0]->generation;
	u64 flags = original_sblock->pagev[0]->flags;
	u64 have_csum = original_sblock->pagev[0]->have_csum;
	struct scrub_recover *recover;
	struct btrfs_io_context *bioc;
	u64 sublen;
	u64 mapped_length;
	u64 stripe_offset;
	int stripe_index;
	int page_index = 0;
	int mirror_index;
	int nmirrors;
	int ret;

	/*
	 * note: the two members refs and outstanding_pages
	 * are not used (and not set) in the blocks that are used for
	 * the recheck procedure
	 */

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

		ASSERT(page_index < SCRUB_MAX_PAGES_PER_BLOCK);

		nmirrors = min(scrub_nr_raid_mirrors(bioc), BTRFS_MAX_MIRRORS);

		for (mirror_index = 0; mirror_index < nmirrors;
		     mirror_index++) {
			struct scrub_block *sblock;
			struct scrub_page *spage;

			sblock = sblocks_for_recheck + mirror_index;
			sblock->sctx = sctx;

			spage = kzalloc(sizeof(*spage), GFP_NOFS);
			if (!spage) {
leave_nomem:
				spin_lock(&sctx->stat_lock);
				sctx->stat.malloc_errors++;
				spin_unlock(&sctx->stat_lock);
				scrub_put_recover(fs_info, recover);
				return -ENOMEM;
			}
			scrub_page_get(spage);
			sblock->pagev[page_index] = spage;
			spage->sblock = sblock;
			spage->flags = flags;
			spage->generation = generation;
			spage->logical = logical;
			spage->have_csum = have_csum;
			if (have_csum)
				memcpy(spage->csum,
				       original_sblock->pagev[0]->csum,
				       sctx->fs_info->csum_size);

			scrub_stripe_index_and_offset(logical,
						      bioc->map_type,
						      bioc->raid_map,
						      mapped_length,
						      bioc->num_stripes -
						      bioc->num_tgtdevs,
						      mirror_index,
						      &stripe_index,
						      &stripe_offset);
			spage->physical = bioc->stripes[stripe_index].physical +
					 stripe_offset;
			spage->dev = bioc->stripes[stripe_index].dev;

			BUG_ON(page_index >= original_sblock->page_count);
			spage->physical_for_dev_replace =
				original_sblock->pagev[page_index]->
				physical_for_dev_replace;
			/* for missing devices, dev->bdev is NULL */
			spage->mirror_num = mirror_index + 1;
			sblock->page_count++;
			spage->page = alloc_page(GFP_NOFS);
			if (!spage->page)
				goto leave_nomem;

			scrub_get_recover(recover);
			spage->recover = recover;
		}
		scrub_put_recover(fs_info, recover);
		length -= sublen;
		logical += sublen;
		page_index++;
	}

	return 0;
}

static void scrub_bio_wait_endio(struct bio *bio)
{
	complete(bio->bi_private);
}

static int scrub_submit_raid56_bio_wait(struct btrfs_fs_info *fs_info,
					struct bio *bio,
					struct scrub_page *spage)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int ret;
	int mirror_num;

	bio->bi_iter.bi_sector = spage->logical >> 9;
	bio->bi_private = &done;
	bio->bi_end_io = scrub_bio_wait_endio;

	mirror_num = spage->sblock->pagev[0]->mirror_num;
	ret = raid56_parity_recover(bio, spage->recover->bioc,
				    spage->recover->map_length,
				    mirror_num, 0);
	if (ret)
		return ret;

	wait_for_completion_io(&done);
	return blk_status_to_errno(bio->bi_status);
}

static void scrub_recheck_block_on_raid56(struct btrfs_fs_info *fs_info,
					  struct scrub_block *sblock)
{
	struct scrub_page *first_page = sblock->pagev[0];
	struct bio *bio;
	int page_num;

	/* All pages in sblock belong to the same stripe on the same device. */
	ASSERT(first_page->dev);
	if (!first_page->dev->bdev)
		goto out;

	bio = btrfs_bio_alloc(BIO_MAX_VECS);
	bio_set_dev(bio, first_page->dev->bdev);

	for (page_num = 0; page_num < sblock->page_count; page_num++) {
		struct scrub_page *spage = sblock->pagev[page_num];

		WARN_ON(!spage->page);
		bio_add_page(bio, spage->page, PAGE_SIZE, 0);
	}

	if (scrub_submit_raid56_bio_wait(fs_info, bio, first_page)) {
		bio_put(bio);
		goto out;
	}

	bio_put(bio);

	scrub_recheck_block_checksum(sblock);

	return;
out:
	for (page_num = 0; page_num < sblock->page_count; page_num++)
		sblock->pagev[page_num]->io_error = 1;

	sblock->no_io_error_seen = 0;
}

/*
 * this function will check the on disk data for checksum errors, header
 * errors and read I/O errors. If any I/O errors happen, the exact pages
 * which are errored are marked as being bad. The goal is to enable scrub
 * to take those pages that are not errored from all the mirrors so that
 * the pages that are errored in the just handled mirror can be repaired.
 */
static void scrub_recheck_block(struct btrfs_fs_info *fs_info,
				struct scrub_block *sblock,
				int retry_failed_mirror)
{
	int page_num;

	sblock->no_io_error_seen = 1;

	/* short cut for raid56 */
	if (!retry_failed_mirror && scrub_is_page_on_raid56(sblock->pagev[0]))
		return scrub_recheck_block_on_raid56(fs_info, sblock);

	for (page_num = 0; page_num < sblock->page_count; page_num++) {
		struct bio *bio;
		struct scrub_page *spage = sblock->pagev[page_num];

		if (spage->dev->bdev == NULL) {
			spage->io_error = 1;
			sblock->no_io_error_seen = 0;
			continue;
		}

		WARN_ON(!spage->page);
		bio = btrfs_bio_alloc(1);
		bio_set_dev(bio, spage->dev->bdev);

		bio_add_page(bio, spage->page, fs_info->sectorsize, 0);
		bio->bi_iter.bi_sector = spage->physical >> 9;
		bio->bi_opf = REQ_OP_READ;

		if (btrfsic_submit_bio_wait(bio)) {
			spage->io_error = 1;
			sblock->no_io_error_seen = 0;
		}

		bio_put(bio);
	}

	if (sblock->no_io_error_seen)
		scrub_recheck_block_checksum(sblock);
}

static inline int scrub_check_fsid(u8 fsid[],
				   struct scrub_page *spage)
{
	struct btrfs_fs_devices *fs_devices = spage->dev->fs_devices;
	int ret;

	ret = memcmp(fsid, fs_devices->fsid, BTRFS_FSID_SIZE);
	return !ret;
}

static void scrub_recheck_block_checksum(struct scrub_block *sblock)
{
	sblock->header_error = 0;
	sblock->checksum_error = 0;
	sblock->generation_error = 0;

	if (sblock->pagev[0]->flags & BTRFS_EXTENT_FLAG_DATA)
		scrub_checksum_data(sblock);
	else
		scrub_checksum_tree_block(sblock);
}

static int scrub_repair_block_from_good_copy(struct scrub_block *sblock_bad,
					     struct scrub_block *sblock_good)
{
	int page_num;
	int ret = 0;

	for (page_num = 0; page_num < sblock_bad->page_count; page_num++) {
		int ret_sub;

		ret_sub = scrub_repair_page_from_good_copy(sblock_bad,
							   sblock_good,
							   page_num, 1);
		if (ret_sub)
			ret = ret_sub;
	}

	return ret;
}

static int scrub_repair_page_from_good_copy(struct scrub_block *sblock_bad,
					    struct scrub_block *sblock_good,
					    int page_num, int force_write)
{
	struct scrub_page *spage_bad = sblock_bad->pagev[page_num];
	struct scrub_page *spage_good = sblock_good->pagev[page_num];
	struct btrfs_fs_info *fs_info = sblock_bad->sctx->fs_info;
	const u32 sectorsize = fs_info->sectorsize;

	BUG_ON(spage_bad->page == NULL);
	BUG_ON(spage_good->page == NULL);
	if (force_write || sblock_bad->header_error ||
	    sblock_bad->checksum_error || spage_bad->io_error) {
		struct bio *bio;
		int ret;

		if (!spage_bad->dev->bdev) {
			btrfs_warn_rl(fs_info,
				"scrub_repair_page_from_good_copy(bdev == NULL) is unexpected");
			return -EIO;
		}

		bio = btrfs_bio_alloc(1);
		bio_set_dev(bio, spage_bad->dev->bdev);
		bio->bi_iter.bi_sector = spage_bad->physical >> 9;
		bio->bi_opf = REQ_OP_WRITE;

		ret = bio_add_page(bio, spage_good->page, sectorsize, 0);
		if (ret != sectorsize) {
			bio_put(bio);
			return -EIO;
		}

		if (btrfsic_submit_bio_wait(bio)) {
			btrfs_dev_stat_inc_and_print(spage_bad->dev,
				BTRFS_DEV_STAT_WRITE_ERRS);
			atomic64_inc(&fs_info->dev_replace.num_write_errors);
			bio_put(bio);
			return -EIO;
		}
		bio_put(bio);
	}

	return 0;
}

static void scrub_write_block_to_dev_replace(struct scrub_block *sblock)
{
	struct btrfs_fs_info *fs_info = sblock->sctx->fs_info;
	int page_num;

	/*
	 * This block is used for the check of the parity on the source device,
	 * so the data needn't be written into the destination device.
	 */
	if (sblock->sparity)
		return;

	for (page_num = 0; page_num < sblock->page_count; page_num++) {
		int ret;

		ret = scrub_write_page_to_dev_replace(sblock, page_num);
		if (ret)
			atomic64_inc(&fs_info->dev_replace.num_write_errors);
	}
}

static int scrub_write_page_to_dev_replace(struct scrub_block *sblock,
					   int page_num)
{
	struct scrub_page *spage = sblock->pagev[page_num];

	BUG_ON(spage->page == NULL);
	if (spage->io_error)
		clear_page(page_address(spage->page));

	return scrub_add_page_to_wr_bio(sblock->sctx, spage);
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

static int scrub_add_page_to_wr_bio(struct scrub_ctx *sctx,
				    struct scrub_page *spage)
{
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
		sctx->wr_curr_bio->page_count = 0;
	}
	sbio = sctx->wr_curr_bio;
	if (sbio->page_count == 0) {
		struct bio *bio;

		ret = fill_writer_pointer_gap(sctx,
					      spage->physical_for_dev_replace);
		if (ret) {
			mutex_unlock(&sctx->wr_lock);
			return ret;
		}

		sbio->physical = spage->physical_for_dev_replace;
		sbio->logical = spage->logical;
		sbio->dev = sctx->wr_tgtdev;
		bio = sbio->bio;
		if (!bio) {
			bio = btrfs_bio_alloc(sctx->pages_per_bio);
			sbio->bio = bio;
		}

		bio->bi_private = sbio;
		bio->bi_end_io = scrub_wr_bio_end_io;
		bio_set_dev(bio, sbio->dev->bdev);
		bio->bi_iter.bi_sector = sbio->physical >> 9;
		bio->bi_opf = REQ_OP_WRITE;
		sbio->status = 0;
	} else if (sbio->physical + sbio->page_count * sectorsize !=
		   spage->physical_for_dev_replace ||
		   sbio->logical + sbio->page_count * sectorsize !=
		   spage->logical) {
		scrub_wr_submit(sctx);
		goto again;
	}

	ret = bio_add_page(sbio->bio, spage->page, sectorsize, 0);
	if (ret != sectorsize) {
		if (sbio->page_count < 1) {
			bio_put(sbio->bio);
			sbio->bio = NULL;
			mutex_unlock(&sctx->wr_lock);
			return -EIO;
		}
		scrub_wr_submit(sctx);
		goto again;
	}

	sbio->pagev[sbio->page_count] = spage;
	scrub_page_get(spage);
	sbio->page_count++;
	if (sbio->page_count == sctx->pages_per_bio)
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
	WARN_ON(!sbio->bio->bi_bdev);
	scrub_pending_bio_inc(sctx);
	/* process all writes in a single worker thread. Then the block layer
	 * orders the requests before sending them to the driver which
	 * doubled the write performance on spinning disks when measured
	 * with Linux 3.5 */
	btrfsic_submit_bio(sbio->bio);

	if (btrfs_is_zoned(sctx->fs_info))
		sctx->write_pointer = sbio->physical + sbio->page_count *
			sctx->fs_info->sectorsize;
}

static void scrub_wr_bio_end_io(struct bio *bio)
{
	struct scrub_bio *sbio = bio->bi_private;
	struct btrfs_fs_info *fs_info = sbio->dev->fs_info;

	sbio->status = bio->bi_status;
	sbio->bio = bio;

	btrfs_init_work(&sbio->work, scrub_wr_bio_end_io_worker, NULL, NULL);
	btrfs_queue_work(fs_info->scrub_wr_completion_workers, &sbio->work);
}

static void scrub_wr_bio_end_io_worker(struct btrfs_work *work)
{
	struct scrub_bio *sbio = container_of(work, struct scrub_bio, work);
	struct scrub_ctx *sctx = sbio->sctx;
	int i;

	ASSERT(sbio->page_count <= SCRUB_PAGES_PER_BIO);
	if (sbio->status) {
		struct btrfs_dev_replace *dev_replace =
			&sbio->sctx->fs_info->dev_replace;

		for (i = 0; i < sbio->page_count; i++) {
			struct scrub_page *spage = sbio->pagev[i];

			spage->io_error = 1;
			atomic64_inc(&dev_replace->num_write_errors);
		}
	}

	for (i = 0; i < sbio->page_count; i++)
		scrub_page_put(sbio->pagev[i]);

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

	WARN_ON(sblock->page_count < 1);
	flags = sblock->pagev[0]->flags;
	ret = 0;
	if (flags & BTRFS_EXTENT_FLAG_DATA)
		ret = scrub_checksum_data(sblock);
	else if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK)
		ret = scrub_checksum_tree_block(sblock);
	else if (flags & BTRFS_EXTENT_FLAG_SUPER)
		(void)scrub_checksum_super(sblock);
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
	struct scrub_page *spage;
	char *kaddr;

	BUG_ON(sblock->page_count < 1);
	spage = sblock->pagev[0];
	if (!spage->have_csum)
		return 0;

	kaddr = page_address(spage->page);

	shash->tfm = fs_info->csum_shash;
	crypto_shash_init(shash);

	/*
	 * In scrub_pages() and scrub_pages_for_parity() we ensure each spage
	 * only contains one sector of data.
	 */
	crypto_shash_digest(shash, kaddr, fs_info->sectorsize, csum);

	if (memcmp(csum, spage->csum, fs_info->csum_size))
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
	struct scrub_page *spage;
	char *kaddr;

	BUG_ON(sblock->page_count < 1);

	/* Each member in pagev is just one block, not a full page */
	ASSERT(sblock->page_count == num_sectors);

	spage = sblock->pagev[0];
	kaddr = page_address(spage->page);
	h = (struct btrfs_header *)kaddr;
	memcpy(on_disk_csum, h->csum, sctx->fs_info->csum_size);

	/*
	 * we don't use the getter functions here, as we
	 * a) don't have an extent buffer and
	 * b) the page is already kmapped
	 */
	if (spage->logical != btrfs_stack_header_bytenr(h))
		sblock->header_error = 1;

	if (spage->generation != btrfs_stack_header_generation(h)) {
		sblock->header_error = 1;
		sblock->generation_error = 1;
	}

	if (!scrub_check_fsid(h->fsid, spage))
		sblock->header_error = 1;

	if (memcmp(h->chunk_tree_uuid, fs_info->chunk_tree_uuid,
		   BTRFS_UUID_SIZE))
		sblock->header_error = 1;

	shash->tfm = fs_info->csum_shash;
	crypto_shash_init(shash);
	crypto_shash_update(shash, kaddr + BTRFS_CSUM_SIZE,
			    sectorsize - BTRFS_CSUM_SIZE);

	for (i = 1; i < num_sectors; i++) {
		kaddr = page_address(sblock->pagev[i]->page);
		crypto_shash_update(shash, kaddr, sectorsize);
	}

	crypto_shash_final(shash, calculated_csum);
	if (memcmp(calculated_csum, on_disk_csum, sctx->fs_info->csum_size))
		sblock->checksum_error = 1;

	return sblock->header_error || sblock->checksum_error;
}

static int scrub_checksum_super(struct scrub_block *sblock)
{
	struct btrfs_super_block *s;
	struct scrub_ctx *sctx = sblock->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	u8 calculated_csum[BTRFS_CSUM_SIZE];
	struct scrub_page *spage;
	char *kaddr;
	int fail_gen = 0;
	int fail_cor = 0;

	BUG_ON(sblock->page_count < 1);
	spage = sblock->pagev[0];
	kaddr = page_address(spage->page);
	s = (struct btrfs_super_block *)kaddr;

	if (spage->logical != btrfs_super_bytenr(s))
		++fail_cor;

	if (spage->generation != btrfs_super_generation(s))
		++fail_gen;

	if (!scrub_check_fsid(s->fsid, spage))
		++fail_cor;

	shash->tfm = fs_info->csum_shash;
	crypto_shash_init(shash);
	crypto_shash_digest(shash, kaddr + BTRFS_CSUM_SIZE,
			BTRFS_SUPER_INFO_SIZE - BTRFS_CSUM_SIZE, calculated_csum);

	if (memcmp(calculated_csum, s->csum, sctx->fs_info->csum_size))
		++fail_cor;

	if (fail_cor + fail_gen) {
		/*
		 * if we find an error in a super block, we just report it.
		 * They will get written with the next transaction commit
		 * anyway
		 */
		spin_lock(&sctx->stat_lock);
		++sctx->stat.super_errors;
		spin_unlock(&sctx->stat_lock);
		if (fail_cor)
			btrfs_dev_stat_inc_and_print(spage->dev,
				BTRFS_DEV_STAT_CORRUPTION_ERRS);
		else
			btrfs_dev_stat_inc_and_print(spage->dev,
				BTRFS_DEV_STAT_GENERATION_ERRS);
	}

	return fail_cor + fail_gen;
}

static void scrub_block_get(struct scrub_block *sblock)
{
	refcount_inc(&sblock->refs);
}

static void scrub_block_put(struct scrub_block *sblock)
{
	if (refcount_dec_and_test(&sblock->refs)) {
		int i;

		if (sblock->sparity)
			scrub_parity_put(sblock->sparity);

		for (i = 0; i < sblock->page_count; i++)
			scrub_page_put(sblock->pagev[i]);
		kfree(sblock);
	}
}

static void scrub_page_get(struct scrub_page *spage)
{
	atomic_inc(&spage->refs);
}

static void scrub_page_put(struct scrub_page *spage)
{
	if (atomic_dec_and_test(&spage->refs)) {
		if (spage->page)
			__free_page(spage->page);
		kfree(spage);
	}
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
	btrfsic_submit_bio(sbio->bio);
}

static int scrub_add_page_to_rd_bio(struct scrub_ctx *sctx,
				    struct scrub_page *spage)
{
	struct scrub_block *sblock = spage->sblock;
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
			sctx->bios[sctx->curr]->page_count = 0;
			spin_unlock(&sctx->list_lock);
		} else {
			spin_unlock(&sctx->list_lock);
			wait_event(sctx->list_wait, sctx->first_free != -1);
		}
	}
	sbio = sctx->bios[sctx->curr];
	if (sbio->page_count == 0) {
		struct bio *bio;

		sbio->physical = spage->physical;
		sbio->logical = spage->logical;
		sbio->dev = spage->dev;
		bio = sbio->bio;
		if (!bio) {
			bio = btrfs_bio_alloc(sctx->pages_per_bio);
			sbio->bio = bio;
		}

		bio->bi_private = sbio;
		bio->bi_end_io = scrub_bio_end_io;
		bio_set_dev(bio, sbio->dev->bdev);
		bio->bi_iter.bi_sector = sbio->physical >> 9;
		bio->bi_opf = REQ_OP_READ;
		sbio->status = 0;
	} else if (sbio->physical + sbio->page_count * sectorsize !=
		   spage->physical ||
		   sbio->logical + sbio->page_count * sectorsize !=
		   spage->logical ||
		   sbio->dev != spage->dev) {
		scrub_submit(sctx);
		goto again;
	}

	sbio->pagev[sbio->page_count] = spage;
	ret = bio_add_page(sbio->bio, spage->page, sectorsize, 0);
	if (ret != sectorsize) {
		if (sbio->page_count < 1) {
			bio_put(sbio->bio);
			sbio->bio = NULL;
			return -EIO;
		}
		scrub_submit(sctx);
		goto again;
	}

	scrub_block_get(sblock); /* one for the page added to the bio */
	atomic_inc(&sblock->outstanding_pages);
	sbio->page_count++;
	if (sbio->page_count == sctx->pages_per_bio)
		scrub_submit(sctx);

	return 0;
}

static void scrub_missing_raid56_end_io(struct bio *bio)
{
	struct scrub_block *sblock = bio->bi_private;
	struct btrfs_fs_info *fs_info = sblock->sctx->fs_info;

	if (bio->bi_status)
		sblock->no_io_error_seen = 0;

	bio_put(bio);

	btrfs_queue_work(fs_info->scrub_workers, &sblock->work);
}

static void scrub_missing_raid56_worker(struct btrfs_work *work)
{
	struct scrub_block *sblock = container_of(work, struct scrub_block, work);
	struct scrub_ctx *sctx = sblock->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	u64 logical;
	struct btrfs_device *dev;

	logical = sblock->pagev[0]->logical;
	dev = sblock->pagev[0]->dev;

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
	u64 length = sblock->page_count * PAGE_SIZE;
	u64 logical = sblock->pagev[0]->logical;
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
		 * there's a bug in scrub_remap_extent()/btrfs_map_block().
		 */
		goto bioc_out;
	}

	bio = btrfs_bio_alloc(BIO_MAX_VECS);
	bio->bi_iter.bi_sector = logical >> 9;
	bio->bi_private = sblock;
	bio->bi_end_io = scrub_missing_raid56_end_io;

	rbio = raid56_alloc_missing_rbio(bio, bioc, length);
	if (!rbio)
		goto rbio_out;

	for (i = 0; i < sblock->page_count; i++) {
		struct scrub_page *spage = sblock->pagev[i];

		raid56_add_scrub_pages(rbio, spage->page, spage->logical);
	}

	btrfs_init_work(&sblock->work, scrub_missing_raid56_worker, NULL, NULL);
	scrub_block_get(sblock);
	scrub_pending_bio_inc(sctx);
	raid56_submit_missing_rbio(rbio);
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

static int scrub_pages(struct scrub_ctx *sctx, u64 logical, u32 len,
		       u64 physical, struct btrfs_device *dev, u64 flags,
		       u64 gen, int mirror_num, u8 *csum,
		       u64 physical_for_dev_replace)
{
	struct scrub_block *sblock;
	const u32 sectorsize = sctx->fs_info->sectorsize;
	int index;

	sblock = kzalloc(sizeof(*sblock), GFP_KERNEL);
	if (!sblock) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		spin_unlock(&sctx->stat_lock);
		return -ENOMEM;
	}

	/* one ref inside this function, plus one for each page added to
	 * a bio later on */
	refcount_set(&sblock->refs, 1);
	sblock->sctx = sctx;
	sblock->no_io_error_seen = 1;

	for (index = 0; len > 0; index++) {
		struct scrub_page *spage;
		/*
		 * Here we will allocate one page for one sector to scrub.
		 * This is fine if PAGE_SIZE == sectorsize, but will cost
		 * more memory for PAGE_SIZE > sectorsize case.
		 */
		u32 l = min(sectorsize, len);

		spage = kzalloc(sizeof(*spage), GFP_KERNEL);
		if (!spage) {
leave_nomem:
			spin_lock(&sctx->stat_lock);
			sctx->stat.malloc_errors++;
			spin_unlock(&sctx->stat_lock);
			scrub_block_put(sblock);
			return -ENOMEM;
		}
		ASSERT(index < SCRUB_MAX_PAGES_PER_BLOCK);
		scrub_page_get(spage);
		sblock->pagev[index] = spage;
		spage->sblock = sblock;
		spage->dev = dev;
		spage->flags = flags;
		spage->generation = gen;
		spage->logical = logical;
		spage->physical = physical;
		spage->physical_for_dev_replace = physical_for_dev_replace;
		spage->mirror_num = mirror_num;
		if (csum) {
			spage->have_csum = 1;
			memcpy(spage->csum, csum, sctx->fs_info->csum_size);
		} else {
			spage->have_csum = 0;
		}
		sblock->page_count++;
		spage->page = alloc_page(GFP_KERNEL);
		if (!spage->page)
			goto leave_nomem;
		len -= l;
		logical += l;
		physical += l;
		physical_for_dev_replace += l;
	}

	WARN_ON(sblock->page_count == 0);
	if (test_bit(BTRFS_DEV_STATE_MISSING, &dev->dev_state)) {
		/*
		 * This case should only be hit for RAID 5/6 device replace. See
		 * the comment in scrub_missing_raid56_pages() for details.
		 */
		scrub_missing_raid56_pages(sblock);
	} else {
		for (index = 0; index < sblock->page_count; index++) {
			struct scrub_page *spage = sblock->pagev[index];
			int ret;

			ret = scrub_add_page_to_rd_bio(sctx, spage);
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

	btrfs_queue_work(fs_info->scrub_workers, &sbio->work);
}

static void scrub_bio_end_io_worker(struct btrfs_work *work)
{
	struct scrub_bio *sbio = container_of(work, struct scrub_bio, work);
	struct scrub_ctx *sctx = sbio->sctx;
	int i;

	ASSERT(sbio->page_count <= SCRUB_PAGES_PER_BIO);
	if (sbio->status) {
		for (i = 0; i < sbio->page_count; i++) {
			struct scrub_page *spage = sbio->pagev[i];

			spage->io_error = 1;
			spage->sblock->no_io_error_seen = 0;
		}
	}

	/* now complete the scrub_block items that have all pages completed */
	for (i = 0; i < sbio->page_count; i++) {
		struct scrub_page *spage = sbio->pagev[i];
		struct scrub_block *sblock = spage->sblock;

		if (atomic_dec_and_test(&sblock->outstanding_pages))
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
	__scrub_mark_bitmap(sparity, sparity->ebitmap, start, len);
}

static inline void scrub_parity_mark_sectors_data(struct scrub_parity *sparity,
						  u64 start, u32 len)
{
	__scrub_mark_bitmap(sparity, sparity->dbitmap, start, len);
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
		u64 start = sblock->pagev[0]->logical;
		u64 end = sblock->pagev[sblock->page_count - 1]->logical +
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
			u64 gen, int mirror_num, u64 physical_for_dev_replace)
{
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

	while (len) {
		u32 l = min(len, blocksize);
		int have_csum = 0;

		if (flags & BTRFS_EXTENT_FLAG_DATA) {
			/* push csums to sbio */
			have_csum = scrub_find_csum(sctx, logical, csum);
			if (have_csum == 0)
				++sctx->stat.no_csum;
		}
		ret = scrub_pages(sctx, logical, l, physical, dev, flags, gen,
				  mirror_num, have_csum ? csum : NULL,
				  physical_for_dev_replace);
		if (ret)
			return ret;
		len -= l;
		logical += l;
		physical += l;
		physical_for_dev_replace += l;
	}
	return 0;
}

static int scrub_pages_for_parity(struct scrub_parity *sparity,
				  u64 logical, u32 len,
				  u64 physical, struct btrfs_device *dev,
				  u64 flags, u64 gen, int mirror_num, u8 *csum)
{
	struct scrub_ctx *sctx = sparity->sctx;
	struct scrub_block *sblock;
	const u32 sectorsize = sctx->fs_info->sectorsize;
	int index;

	ASSERT(IS_ALIGNED(len, sectorsize));

	sblock = kzalloc(sizeof(*sblock), GFP_KERNEL);
	if (!sblock) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		spin_unlock(&sctx->stat_lock);
		return -ENOMEM;
	}

	/* one ref inside this function, plus one for each page added to
	 * a bio later on */
	refcount_set(&sblock->refs, 1);
	sblock->sctx = sctx;
	sblock->no_io_error_seen = 1;
	sblock->sparity = sparity;
	scrub_parity_get(sparity);

	for (index = 0; len > 0; index++) {
		struct scrub_page *spage;

		spage = kzalloc(sizeof(*spage), GFP_KERNEL);
		if (!spage) {
leave_nomem:
			spin_lock(&sctx->stat_lock);
			sctx->stat.malloc_errors++;
			spin_unlock(&sctx->stat_lock);
			scrub_block_put(sblock);
			return -ENOMEM;
		}
		ASSERT(index < SCRUB_MAX_PAGES_PER_BLOCK);
		/* For scrub block */
		scrub_page_get(spage);
		sblock->pagev[index] = spage;
		/* For scrub parity */
		scrub_page_get(spage);
		list_add_tail(&spage->list, &sparity->spages);
		spage->sblock = sblock;
		spage->dev = dev;
		spage->flags = flags;
		spage->generation = gen;
		spage->logical = logical;
		spage->physical = physical;
		spage->mirror_num = mirror_num;
		if (csum) {
			spage->have_csum = 1;
			memcpy(spage->csum, csum, sctx->fs_info->csum_size);
		} else {
			spage->have_csum = 0;
		}
		sblock->page_count++;
		spage->page = alloc_page(GFP_KERNEL);
		if (!spage->page)
			goto leave_nomem;


		/* Iterate over the stripe range in sectorsize steps */
		len -= sectorsize;
		logical += sectorsize;
		physical += sectorsize;
	}

	WARN_ON(sblock->page_count == 0);
	for (index = 0; index < sblock->page_count; index++) {
		struct scrub_page *spage = sblock->pagev[index];
		int ret;

		ret = scrub_add_page_to_rd_bio(sctx, spage);
		if (ret) {
			scrub_block_put(sblock);
			return ret;
		}
	}

	/* last one frees, either here or in bio completion for last page */
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
		ret = scrub_pages_for_parity(sparity, logical, l, physical, dev,
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
	struct scrub_page *curr, *next;
	int nbits;

	nbits = bitmap_weight(sparity->ebitmap, sparity->nsectors);
	if (nbits) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.read_errors += nbits;
		sctx->stat.uncorrectable_errors += nbits;
		spin_unlock(&sctx->stat_lock);
	}

	list_for_each_entry_safe(curr, next, &sparity->spages, list) {
		list_del_init(&curr->list);
		scrub_page_put(curr);
	}

	kfree(sparity);
}

static void scrub_parity_bio_endio_worker(struct btrfs_work *work)
{
	struct scrub_parity *sparity = container_of(work, struct scrub_parity,
						    work);
	struct scrub_ctx *sctx = sparity->sctx;

	scrub_free_parity(sparity);
	scrub_pending_bio_dec(sctx);
}

static void scrub_parity_bio_endio(struct bio *bio)
{
	struct scrub_parity *sparity = (struct scrub_parity *)bio->bi_private;
	struct btrfs_fs_info *fs_info = sparity->sctx->fs_info;

	if (bio->bi_status)
		bitmap_or(sparity->ebitmap, sparity->ebitmap, sparity->dbitmap,
			  sparity->nsectors);

	bio_put(bio);

	btrfs_init_work(&sparity->work, scrub_parity_bio_endio_worker, NULL,
			NULL);
	btrfs_queue_work(fs_info->scrub_parity_workers, &sparity->work);
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

	if (!bitmap_andnot(sparity->dbitmap, sparity->dbitmap, sparity->ebitmap,
			   sparity->nsectors))
		goto out;

	length = sparity->logic_end - sparity->logic_start;

	btrfs_bio_counter_inc_blocked(fs_info);
	ret = btrfs_map_sblock(fs_info, BTRFS_MAP_WRITE, sparity->logic_start,
			       &length, &bioc);
	if (ret || !bioc || !bioc->raid_map)
		goto bioc_out;

	bio = btrfs_bio_alloc(BIO_MAX_VECS);
	bio->bi_iter.bi_sector = sparity->logic_start >> 9;
	bio->bi_private = sparity;
	bio->bi_end_io = scrub_parity_bio_endio;

	rbio = raid56_parity_alloc_scrub_rbio(bio, bioc, length,
					      sparity->scrub_dev,
					      sparity->dbitmap,
					      sparity->nsectors);
	if (!rbio)
		goto rbio_out;

	scrub_pending_bio_inc(sctx);
	raid56_parity_submit_scrub_rbio(rbio);
	return;

rbio_out:
	bio_put(bio);
bioc_out:
	btrfs_bio_counter_dec(fs_info);
	btrfs_put_bioc(bioc);
	bitmap_or(sparity->ebitmap, sparity->ebitmap, sparity->dbitmap,
		  sparity->nsectors);
	spin_lock(&sctx->stat_lock);
	sctx->stat.malloc_errors++;
	spin_unlock(&sctx->stat_lock);
out:
	scrub_free_parity(sparity);
}

static inline int scrub_calc_parity_bitmap_len(int nsectors)
{
	return DIV_ROUND_UP(nsectors, BITS_PER_LONG) * sizeof(long);
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

static noinline_for_stack int scrub_raid56_parity(struct scrub_ctx *sctx,
						  struct map_lookup *map,
						  struct btrfs_device *sdev,
						  u64 logic_start,
						  u64 logic_end)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_root *root = btrfs_extent_root(fs_info, logic_start);
	struct btrfs_root *csum_root;
	struct btrfs_extent_item *extent;
	struct btrfs_io_context *bioc = NULL;
	struct btrfs_path *path;
	u64 flags;
	int ret;
	int slot;
	struct extent_buffer *l;
	struct btrfs_key key;
	u64 generation;
	u64 extent_logical;
	u64 extent_physical;
	/* Check the comment in scrub_stripe() for why u32 is enough here */
	u32 extent_len;
	u64 mapped_length;
	struct btrfs_device *extent_dev;
	struct scrub_parity *sparity;
	int nsectors;
	int bitmap_len;
	int extent_mirror_num;
	int stop_loop = 0;

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
	bitmap_len = scrub_calc_parity_bitmap_len(nsectors);
	sparity = kzalloc(sizeof(struct scrub_parity) + 2 * bitmap_len,
			  GFP_NOFS);
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
	INIT_LIST_HEAD(&sparity->spages);
	sparity->dbitmap = sparity->bitmap;
	sparity->ebitmap = (void *)sparity->bitmap + bitmap_len;

	ret = 0;
	while (logic_start < logic_end) {
		if (btrfs_fs_incompat(fs_info, SKINNY_METADATA))
			key.type = BTRFS_METADATA_ITEM_KEY;
		else
			key.type = BTRFS_EXTENT_ITEM_KEY;
		key.objectid = logic_start;
		key.offset = (u64)-1;

		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		if (ret > 0) {
			ret = btrfs_previous_extent_item(root, path, 0);
			if (ret < 0)
				goto out;
			if (ret > 0) {
				btrfs_release_path(path);
				ret = btrfs_search_slot(NULL, root, &key,
							path, 0, 0);
				if (ret < 0)
					goto out;
			}
		}

		stop_loop = 0;
		while (1) {
			u64 bytes;

			l = path->nodes[0];
			slot = path->slots[0];
			if (slot >= btrfs_header_nritems(l)) {
				ret = btrfs_next_leaf(root, path);
				if (ret == 0)
					continue;
				if (ret < 0)
					goto out;

				stop_loop = 1;
				break;
			}
			btrfs_item_key_to_cpu(l, &key, slot);

			if (key.type != BTRFS_EXTENT_ITEM_KEY &&
			    key.type != BTRFS_METADATA_ITEM_KEY)
				goto next;

			if (key.type == BTRFS_METADATA_ITEM_KEY)
				bytes = fs_info->nodesize;
			else
				bytes = key.offset;

			if (key.objectid + bytes <= logic_start)
				goto next;

			if (key.objectid >= logic_end) {
				stop_loop = 1;
				break;
			}

			while (key.objectid >= logic_start + map->stripe_len)
				logic_start += map->stripe_len;

			extent = btrfs_item_ptr(l, slot,
						struct btrfs_extent_item);
			flags = btrfs_extent_flags(l, extent);
			generation = btrfs_extent_generation(l, extent);

			if ((flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) &&
			    (key.objectid < logic_start ||
			     key.objectid + bytes >
			     logic_start + map->stripe_len)) {
				btrfs_err(fs_info,
					  "scrub: tree block %llu spanning stripes, ignored. logical=%llu",
					  key.objectid, logic_start);
				spin_lock(&sctx->stat_lock);
				sctx->stat.uncorrectable_errors++;
				spin_unlock(&sctx->stat_lock);
				goto next;
			}
again:
			extent_logical = key.objectid;
			ASSERT(bytes <= U32_MAX);
			extent_len = bytes;

			if (extent_logical < logic_start) {
				extent_len -= logic_start - extent_logical;
				extent_logical = logic_start;
			}

			if (extent_logical + extent_len >
			    logic_start + map->stripe_len)
				extent_len = logic_start + map->stripe_len -
					     extent_logical;

			scrub_parity_mark_sectors_data(sparity, extent_logical,
						       extent_len);

			mapped_length = extent_len;
			bioc = NULL;
			ret = btrfs_map_block(fs_info, BTRFS_MAP_READ,
					extent_logical, &mapped_length, &bioc,
					0);
			if (!ret) {
				if (!bioc || mapped_length < extent_len)
					ret = -EIO;
			}
			if (ret) {
				btrfs_put_bioc(bioc);
				goto out;
			}
			extent_physical = bioc->stripes[0].physical;
			extent_mirror_num = bioc->mirror_num;
			extent_dev = bioc->stripes[0].dev;
			btrfs_put_bioc(bioc);

			csum_root = btrfs_csum_root(fs_info, extent_logical);
			ret = btrfs_lookup_csums_range(csum_root,
						extent_logical,
						extent_logical + extent_len - 1,
						&sctx->csum_list, 1);
			if (ret)
				goto out;

			ret = scrub_extent_for_parity(sparity, extent_logical,
						      extent_len,
						      extent_physical,
						      extent_dev, flags,
						      generation,
						      extent_mirror_num);

			scrub_free_csums(sctx);

			if (ret)
				goto out;

			if (extent_logical + extent_len <
			    key.objectid + bytes) {
				logic_start += map->stripe_len;

				if (logic_start >= logic_end) {
					stop_loop = 1;
					break;
				}

				if (logic_start < key.objectid + bytes) {
					cond_resched();
					goto again;
				}
			}
next:
			path->slots[0]++;
		}

		btrfs_release_path(path);

		if (stop_loop)
			break;

		logic_start += map->stripe_len;
	}
out:
	if (ret < 0) {
		ASSERT(logic_end - logic_start <= U32_MAX);
		scrub_parity_mark_sectors_error(sparity, logic_start,
						logic_end - logic_start);
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

static noinline_for_stack int scrub_stripe(struct scrub_ctx *sctx,
					   struct btrfs_block_group *bg,
					   struct map_lookup *map,
					   struct btrfs_device *scrub_dev,
					   int stripe_index, u64 dev_extent_len)
{
	struct btrfs_path *path;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_root *root;
	struct btrfs_root *csum_root;
	struct btrfs_extent_item *extent;
	struct blk_plug plug;
	const u64 chunk_logical = bg->start;
	u64 flags;
	int ret;
	int slot;
	u64 nstripes;
	struct extent_buffer *l;
	u64 physical;
	u64 logical;
	u64 logic_end;
	u64 physical_end;
	u64 generation;
	int mirror_num;
	struct btrfs_key key;
	u64 increment = map->stripe_len;
	u64 offset;
	u64 extent_logical;
	u64 extent_physical;
	/*
	 * Unlike chunk length, extent length should never go beyond
	 * BTRFS_MAX_EXTENT_SIZE, thus u32 is enough here.
	 */
	u32 extent_len;
	u64 stripe_logical;
	u64 stripe_end;
	struct btrfs_device *extent_dev;
	int extent_mirror_num;
	int stop_loop = 0;

	physical = map->stripes[stripe_index].physical;
	offset = 0;
	nstripes = div64_u64(dev_extent_len, map->stripe_len);
	mirror_num = 1;
	increment = map->stripe_len;
	if (map->type & BTRFS_BLOCK_GROUP_RAID0) {
		offset = map->stripe_len * stripe_index;
		increment = map->stripe_len * map->num_stripes;
	} else if (map->type & BTRFS_BLOCK_GROUP_RAID10) {
		int factor = map->num_stripes / map->sub_stripes;
		offset = map->stripe_len * (stripe_index / map->sub_stripes);
		increment = map->stripe_len * factor;
		mirror_num = stripe_index % map->sub_stripes + 1;
	} else if (map->type & BTRFS_BLOCK_GROUP_RAID1_MASK) {
		mirror_num = stripe_index % map->num_stripes + 1;
	} else if (map->type & BTRFS_BLOCK_GROUP_DUP) {
		mirror_num = stripe_index % map->num_stripes + 1;
	} else if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		get_raid56_logic_offset(physical, stripe_index, map, &offset,
					NULL);
		increment = map->stripe_len * nr_data_stripes(map);
	}

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

	logical = chunk_logical + offset;
	physical_end = physical + nstripes * map->stripe_len;
	if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		get_raid56_logic_offset(physical_end, stripe_index,
					map, &logic_end, NULL);
		logic_end += chunk_logical;
	} else {
		logic_end = logical + increment * nstripes;
	}
	wait_event(sctx->list_wait,
		   atomic_read(&sctx->bios_in_flight) == 0);
	scrub_blocked_if_needed(fs_info);

	root = btrfs_extent_root(fs_info, logical);
	csum_root = btrfs_csum_root(fs_info, logical);

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
	 * now find all extents for each stripe and scrub them
	 */
	ret = 0;
	while (physical < physical_end) {
		/*
		 * canceled?
		 */
		if (atomic_read(&fs_info->scrub_cancel_req) ||
		    atomic_read(&sctx->cancel_req)) {
			ret = -ECANCELED;
			goto out;
		}
		/*
		 * check to see if we have to pause
		 */
		if (atomic_read(&fs_info->scrub_pause_req)) {
			/* push queued extents */
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

		if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
			ret = get_raid56_logic_offset(physical, stripe_index,
						      map, &logical,
						      &stripe_logical);
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
				goto skip;
			}
		}

		if (btrfs_fs_incompat(fs_info, SKINNY_METADATA))
			key.type = BTRFS_METADATA_ITEM_KEY;
		else
			key.type = BTRFS_EXTENT_ITEM_KEY;
		key.objectid = logical;
		key.offset = (u64)-1;

		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		if (ret > 0) {
			ret = btrfs_previous_extent_item(root, path, 0);
			if (ret < 0)
				goto out;
			if (ret > 0) {
				/* there's no smaller item, so stick with the
				 * larger one */
				btrfs_release_path(path);
				ret = btrfs_search_slot(NULL, root, &key,
							path, 0, 0);
				if (ret < 0)
					goto out;
			}
		}

		stop_loop = 0;
		while (1) {
			u64 bytes;

			l = path->nodes[0];
			slot = path->slots[0];
			if (slot >= btrfs_header_nritems(l)) {
				ret = btrfs_next_leaf(root, path);
				if (ret == 0)
					continue;
				if (ret < 0)
					goto out;

				stop_loop = 1;
				break;
			}
			btrfs_item_key_to_cpu(l, &key, slot);

			if (key.type != BTRFS_EXTENT_ITEM_KEY &&
			    key.type != BTRFS_METADATA_ITEM_KEY)
				goto next;

			if (key.type == BTRFS_METADATA_ITEM_KEY)
				bytes = fs_info->nodesize;
			else
				bytes = key.offset;

			if (key.objectid + bytes <= logical)
				goto next;

			if (key.objectid >= logical + map->stripe_len) {
				/* out of this device extent */
				if (key.objectid >= logic_end)
					stop_loop = 1;
				break;
			}

			/*
			 * If our block group was removed in the meanwhile, just
			 * stop scrubbing since there is no point in continuing.
			 * Continuing would prevent reusing its device extents
			 * for new block groups for a long time.
			 */
			spin_lock(&bg->lock);
			if (bg->removed) {
				spin_unlock(&bg->lock);
				ret = 0;
				goto out;
			}
			spin_unlock(&bg->lock);

			extent = btrfs_item_ptr(l, slot,
						struct btrfs_extent_item);
			flags = btrfs_extent_flags(l, extent);
			generation = btrfs_extent_generation(l, extent);

			if ((flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) &&
			    (key.objectid < logical ||
			     key.objectid + bytes >
			     logical + map->stripe_len)) {
				btrfs_err(fs_info,
					   "scrub: tree block %llu spanning stripes, ignored. logical=%llu",
				       key.objectid, logical);
				spin_lock(&sctx->stat_lock);
				sctx->stat.uncorrectable_errors++;
				spin_unlock(&sctx->stat_lock);
				goto next;
			}

again:
			extent_logical = key.objectid;
			ASSERT(bytes <= U32_MAX);
			extent_len = bytes;

			/*
			 * trim extent to this stripe
			 */
			if (extent_logical < logical) {
				extent_len -= logical - extent_logical;
				extent_logical = logical;
			}
			if (extent_logical + extent_len >
			    logical + map->stripe_len) {
				extent_len = logical + map->stripe_len -
					     extent_logical;
			}

			extent_physical = extent_logical - logical + physical;
			extent_dev = scrub_dev;
			extent_mirror_num = mirror_num;
			if (sctx->is_dev_replace)
				scrub_remap_extent(fs_info, extent_logical,
						   extent_len, &extent_physical,
						   &extent_dev,
						   &extent_mirror_num);

			if (flags & BTRFS_EXTENT_FLAG_DATA) {
				ret = btrfs_lookup_csums_range(csum_root,
						extent_logical,
						extent_logical + extent_len - 1,
						&sctx->csum_list, 1);
				if (ret)
					goto out;
			}

			ret = scrub_extent(sctx, map, extent_logical, extent_len,
					   extent_physical, extent_dev, flags,
					   generation, extent_mirror_num,
					   extent_logical - logical + physical);

			scrub_free_csums(sctx);

			if (ret)
				goto out;

			if (sctx->is_dev_replace)
				sync_replace_for_zoned(sctx);

			if (extent_logical + extent_len <
			    key.objectid + bytes) {
				if (map->type & BTRFS_BLOCK_GROUP_RAID56_MASK) {
					/*
					 * loop until we find next data stripe
					 * or we have finished all stripes.
					 */
loop:
					physical += map->stripe_len;
					ret = get_raid56_logic_offset(physical,
							stripe_index, map,
							&logical, &stripe_logical);
					logical += chunk_logical;

					if (ret && physical < physical_end) {
						stripe_logical += chunk_logical;
						stripe_end = stripe_logical +
								increment;
						ret = scrub_raid56_parity(sctx,
							map, scrub_dev,
							stripe_logical,
							stripe_end);
						if (ret)
							goto out;
						goto loop;
					}
				} else {
					physical += map->stripe_len;
					logical += increment;
				}
				if (logical < key.objectid + bytes) {
					cond_resched();
					goto again;
				}

				if (physical >= physical_end) {
					stop_loop = 1;
					break;
				}
			}
next:
			path->slots[0]++;
		}
		btrfs_release_path(path);
skip:
		logical += increment;
		physical += map->stripe_len;
		spin_lock(&sctx->stat_lock);
		if (stop_loop)
			sctx->stat.last_physical = map->stripes[stripe_index].physical +
						   dev_extent_len;
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
		if (!bg->removed)
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
			ret = scrub_stripe(sctx, bg, map, scrub_dev, i,
					   dev_extent_len);
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

		if (sctx->is_dev_replace && btrfs_is_zoned(fs_info)) {
			spin_lock(&cache->lock);
			if (!cache->to_copy) {
				spin_unlock(&cache->lock);
				btrfs_put_block_group(cache);
				goto skip;
			}
			spin_unlock(&cache->lock);
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
		if (cache->removed) {
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

		ASSERT(cache->start == chunk_offset);
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
		if (!cache->removed && !cache->ro && cache->reserved == 0 &&
		    cache->used == 0) {
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

		ret = scrub_pages(sctx, bytenr, BTRFS_SUPER_INFO_SIZE, bytenr,
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
		struct btrfs_workqueue *scrub_workers = NULL;
		struct btrfs_workqueue *scrub_wr_comp = NULL;
		struct btrfs_workqueue *scrub_parity = NULL;

		scrub_workers = fs_info->scrub_workers;
		scrub_wr_comp = fs_info->scrub_wr_completion_workers;
		scrub_parity = fs_info->scrub_parity_workers;

		fs_info->scrub_workers = NULL;
		fs_info->scrub_wr_completion_workers = NULL;
		fs_info->scrub_parity_workers = NULL;
		mutex_unlock(&fs_info->scrub_lock);

		btrfs_destroy_workqueue(scrub_workers);
		btrfs_destroy_workqueue(scrub_wr_comp);
		btrfs_destroy_workqueue(scrub_parity);
	}
}

/*
 * get a reference count on fs_info->scrub_workers. start worker if necessary
 */
static noinline_for_stack int scrub_workers_get(struct btrfs_fs_info *fs_info,
						int is_dev_replace)
{
	struct btrfs_workqueue *scrub_workers = NULL;
	struct btrfs_workqueue *scrub_wr_comp = NULL;
	struct btrfs_workqueue *scrub_parity = NULL;
	unsigned int flags = WQ_FREEZABLE | WQ_UNBOUND;
	int max_active = fs_info->thread_pool_size;
	int ret = -ENOMEM;

	if (refcount_inc_not_zero(&fs_info->scrub_workers_refcnt))
		return 0;

	scrub_workers = btrfs_alloc_workqueue(fs_info, "scrub", flags,
					      is_dev_replace ? 1 : max_active, 4);
	if (!scrub_workers)
		goto fail_scrub_workers;

	scrub_wr_comp = btrfs_alloc_workqueue(fs_info, "scrubwrc", flags,
					      max_active, 2);
	if (!scrub_wr_comp)
		goto fail_scrub_wr_completion_workers;

	scrub_parity = btrfs_alloc_workqueue(fs_info, "scrubparity", flags,
					     max_active, 2);
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
	btrfs_destroy_workqueue(scrub_parity);
fail_scrub_parity_workers:
	btrfs_destroy_workqueue(scrub_wr_comp);
fail_scrub_wr_completion_workers:
	btrfs_destroy_workqueue(scrub_workers);
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

	if (btrfs_fs_closing(fs_info))
		return -EAGAIN;

	if (fs_info->nodesize > BTRFS_STRIPE_LEN) {
		/*
		 * in this case scrub is unable to calculate the checksum
		 * the way scrub is implemented. Do not handle this
		 * situation at all because it won't ever happen.
		 */
		btrfs_err(fs_info,
			   "scrub: size assumption nodesize <= BTRFS_STRIPE_LEN (%d <= %d) fails",
		       fs_info->nodesize,
		       BTRFS_STRIPE_LEN);
		return -EINVAL;
	}

	if (fs_info->nodesize >
	    PAGE_SIZE * SCRUB_MAX_PAGES_PER_BLOCK ||
	    fs_info->sectorsize > PAGE_SIZE * SCRUB_MAX_PAGES_PER_BLOCK) {
		/*
		 * would exhaust the array bounds of pagev member in
		 * struct scrub_block
		 */
		btrfs_err(fs_info,
			  "scrub: size assumption nodesize and sectorsize <= SCRUB_MAX_PAGES_PER_BLOCK (%d <= %d && %d <= %d) fails",
		       fs_info->nodesize,
		       SCRUB_MAX_PAGES_PER_BLOCK,
		       fs_info->sectorsize,
		       SCRUB_MAX_PAGES_PER_BLOCK);
		return -EINVAL;
	}

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
	 * allocations done at btrfs_scrub_pages() and scrub_pages_for_parity()
	 * invoked by our callees. The pausing request is done when the
	 * transaction commit starts, and it blocks the transaction until scrub
	 * is paused (done at specific points at scrub_stripe() or right above
	 * before incrementing fs_info->scrubs_running).
	 */
	nofs_flag = memalloc_nofs_save();
	if (!is_dev_replace) {
		btrfs_info(fs_info, "scrub: started on devid %llu", devid);
		/*
		 * by holding device list mutex, we can
		 * kick off writing super in log tree sync.
		 */
		mutex_lock(&fs_info->fs_devices->device_list_mutex);
		ret = scrub_supers(sctx, dev);
		mutex_unlock(&fs_info->fs_devices->device_list_mutex);
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

static void scrub_remap_extent(struct btrfs_fs_info *fs_info,
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
