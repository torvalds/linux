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
#include "raid56.h"
#include "block-group.h"
#include "zoned.h"
#include "fs.h"
#include "accessors.h"
#include "file-item.h"
#include "scrub.h"
#include "raid-stripe-tree.h"

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

struct scrub_ctx;

/*
 * The following value only influences the performance.
 *
 * This determines how many stripes would be submitted in one go,
 * which is 512KiB (BTRFS_STRIPE_LEN * SCRUB_STRIPES_PER_GROUP).
 */
#define SCRUB_STRIPES_PER_GROUP		8

/*
 * How many groups we have for each sctx.
 *
 * This would be 8M per device, the same value as the old scrub in-flight bios
 * size limit.
 */
#define SCRUB_GROUPS_PER_SCTX		16

#define SCRUB_TOTAL_STRIPES		(SCRUB_GROUPS_PER_SCTX * SCRUB_STRIPES_PER_GROUP)

/*
 * The following value times PAGE_SIZE needs to be large enough to match the
 * largest node/leaf/sector size that shall be supported.
 */
#define SCRUB_MAX_SECTORS_PER_BLOCK	(BTRFS_MAX_METADATA_BLOCKSIZE / SZ_4K)

/* Represent one sector and its needed info to verify the content. */
struct scrub_sector_verification {
	union {
		/*
		 * Csum pointer for data csum verification.  Should point to a
		 * sector csum inside scrub_stripe::csums.
		 *
		 * NULL if this data sector has no csum.
		 */
		u8 *csum;

		/*
		 * Extra info for metadata verification.  All sectors inside a
		 * tree block share the same generation.
		 */
		u64 generation;
	};
};

enum scrub_stripe_flags {
	/* Set when @mirror_num, @dev, @physical and @logical are set. */
	SCRUB_STRIPE_FLAG_INITIALIZED,

	/* Set when the read-repair is finished. */
	SCRUB_STRIPE_FLAG_REPAIR_DONE,

	/*
	 * Set for data stripes if it's triggered from P/Q stripe.
	 * During such scrub, we should not report errors in data stripes, nor
	 * update the accounting.
	 */
	SCRUB_STRIPE_FLAG_NO_REPORT,
};

/*
 * We have multiple bitmaps for one scrub_stripe.
 * However each bitmap has at most (BTRFS_STRIPE_LEN / blocksize) bits,
 * which is normally 16, and much smaller than BITS_PER_LONG (32 or 64).
 *
 * So to reduce memory usage for each scrub_stripe, we pack those bitmaps
 * into a larger one.
 *
 * These enum records where the sub-bitmap are inside the larger one.
 * Each subbitmap starts at scrub_bitmap_nr_##name * nr_sectors bit.
 */
enum {
	/* Which blocks are covered by extent items. */
	scrub_bitmap_nr_has_extent = 0,

	/* Which blocks are meteadata. */
	scrub_bitmap_nr_is_metadata,

	/*
	 * Which blocks have errors, including IO, csum, and metadata
	 * errors.
	 * This sub-bitmap is the OR results of the next few error related
	 * sub-bitmaps.
	 */
	scrub_bitmap_nr_error,
	scrub_bitmap_nr_io_error,
	scrub_bitmap_nr_csum_error,
	scrub_bitmap_nr_meta_error,
	scrub_bitmap_nr_meta_gen_error,
	scrub_bitmap_nr_last,
};

#define SCRUB_STRIPE_PAGES		(BTRFS_STRIPE_LEN / PAGE_SIZE)

/*
 * Represent one contiguous range with a length of BTRFS_STRIPE_LEN.
 */
struct scrub_stripe {
	struct scrub_ctx *sctx;
	struct btrfs_block_group *bg;

	struct page *pages[SCRUB_STRIPE_PAGES];
	struct scrub_sector_verification *sectors;

	struct btrfs_device *dev;
	u64 logical;
	u64 physical;

	u16 mirror_num;

	/* Should be BTRFS_STRIPE_LEN / sectorsize. */
	u16 nr_sectors;

	/*
	 * How many data/meta extents are in this stripe.  Only for scrub status
	 * reporting purposes.
	 */
	u16 nr_data_extents;
	u16 nr_meta_extents;

	atomic_t pending_io;
	wait_queue_head_t io_wait;
	wait_queue_head_t repair_wait;

	/*
	 * Indicate the states of the stripe.  Bits are defined in
	 * scrub_stripe_flags enum.
	 */
	unsigned long state;

	/* The large bitmap contains all the sub-bitmaps. */
	unsigned long bitmaps[BITS_TO_LONGS(scrub_bitmap_nr_last *
					    (BTRFS_STRIPE_LEN / BTRFS_MIN_BLOCKSIZE))];

	/*
	 * For writeback (repair or replace) error reporting.
	 * This one is protected by a spinlock, thus can not be packed into
	 * the larger bitmap.
	 */
	unsigned long write_error_bitmap;

	/* Writeback can be concurrent, thus we need to protect the bitmap. */
	spinlock_t write_error_lock;

	/*
	 * Checksum for the whole stripe if this stripe is inside a data block
	 * group.
	 */
	u8 *csums;

	struct work_struct work;
};

struct scrub_ctx {
	struct scrub_stripe	stripes[SCRUB_TOTAL_STRIPES];
	struct scrub_stripe	*raid56_data_stripes;
	struct btrfs_fs_info	*fs_info;
	struct btrfs_path	extent_path;
	struct btrfs_path	csum_path;
	int			first_free;
	int			cur_stripe;
	atomic_t		cancel_req;
	int			readonly;

	/* State of IO submission throttling affecting the associated device */
	ktime_t			throttle_deadline;
	u64			throttle_sent;

	int			is_dev_replace;
	u64			write_pointer;

	struct mutex            wr_lock;
	struct btrfs_device     *wr_tgtdev;

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

#define scrub_calc_start_bit(stripe, name, block_nr)			\
({									\
	unsigned int __start_bit;					\
									\
	ASSERT(block_nr < stripe->nr_sectors,				\
		"nr_sectors=%u block_nr=%u", stripe->nr_sectors, block_nr); \
	__start_bit = scrub_bitmap_nr_##name * stripe->nr_sectors + block_nr; \
	__start_bit;							\
})

#define IMPLEMENT_SCRUB_BITMAP_OPS(name)				\
static inline void scrub_bitmap_set_##name(struct scrub_stripe *stripe,	\
				    unsigned int block_nr,		\
				    unsigned int nr_blocks)		\
{									\
	const unsigned int start_bit = scrub_calc_start_bit(stripe,	\
							    name, block_nr); \
									\
	bitmap_set(stripe->bitmaps, start_bit, nr_blocks);		\
}									\
static inline void scrub_bitmap_clear_##name(struct scrub_stripe *stripe, \
				      unsigned int block_nr,		\
				      unsigned int nr_blocks)		\
{									\
	const unsigned int start_bit = scrub_calc_start_bit(stripe, name, \
							    block_nr);	\
									\
	bitmap_clear(stripe->bitmaps, start_bit, nr_blocks);		\
}									\
static inline bool scrub_bitmap_test_bit_##name(struct scrub_stripe *stripe, \
				     unsigned int block_nr)		\
{									\
	const unsigned int start_bit = scrub_calc_start_bit(stripe, name, \
							    block_nr);	\
									\
	return test_bit(start_bit, stripe->bitmaps);			\
}									\
static inline void scrub_bitmap_set_bit_##name(struct scrub_stripe *stripe, \
				     unsigned int block_nr)		\
{									\
	const unsigned int start_bit = scrub_calc_start_bit(stripe, name, \
							    block_nr);	\
									\
	set_bit(start_bit, stripe->bitmaps);				\
}									\
static inline void scrub_bitmap_clear_bit_##name(struct scrub_stripe *stripe, \
				     unsigned int block_nr)		\
{									\
	const unsigned int start_bit = scrub_calc_start_bit(stripe, name, \
							    block_nr);	\
									\
	clear_bit(start_bit, stripe->bitmaps);				\
}									\
static inline unsigned long scrub_bitmap_read_##name(struct scrub_stripe *stripe) \
{									\
	const unsigned int nr_blocks = stripe->nr_sectors;		\
									\
	ASSERT(nr_blocks > 0 && nr_blocks <= BITS_PER_LONG,		\
	       "nr_blocks=%u BITS_PER_LONG=%u",				\
	       nr_blocks, BITS_PER_LONG);				\
									\
	return bitmap_read(stripe->bitmaps, nr_blocks * scrub_bitmap_nr_##name, \
			   stripe->nr_sectors);				\
}									\
static inline bool scrub_bitmap_empty_##name(struct scrub_stripe *stripe) \
{									\
	unsigned long bitmap = scrub_bitmap_read_##name(stripe);	\
									\
	return bitmap_empty(&bitmap, stripe->nr_sectors);		\
}									\
static inline unsigned int scrub_bitmap_weight_##name(struct scrub_stripe *stripe) \
{									\
	unsigned long bitmap = scrub_bitmap_read_##name(stripe);	\
									\
	return bitmap_weight(&bitmap, stripe->nr_sectors);		\
}
IMPLEMENT_SCRUB_BITMAP_OPS(has_extent);
IMPLEMENT_SCRUB_BITMAP_OPS(is_metadata);
IMPLEMENT_SCRUB_BITMAP_OPS(error);
IMPLEMENT_SCRUB_BITMAP_OPS(io_error);
IMPLEMENT_SCRUB_BITMAP_OPS(csum_error);
IMPLEMENT_SCRUB_BITMAP_OPS(meta_error);
IMPLEMENT_SCRUB_BITMAP_OPS(meta_gen_error);

struct scrub_warning {
	struct btrfs_path	*path;
	u64			extent_item_size;
	const char		*errstr;
	u64			physical;
	u64			logical;
	struct btrfs_device	*dev;
};

struct scrub_error_records {
	/*
	 * Bitmap recording which blocks hit errors (IO/csum/...) during the
	 * initial read.
	 */
	unsigned long init_error_bitmap;

	unsigned int nr_io_errors;
	unsigned int nr_csum_errors;
	unsigned int nr_meta_errors;
	unsigned int nr_meta_gen_errors;
};

static void release_scrub_stripe(struct scrub_stripe *stripe)
{
	if (!stripe)
		return;

	for (int i = 0; i < SCRUB_STRIPE_PAGES; i++) {
		if (stripe->pages[i])
			__free_page(stripe->pages[i]);
		stripe->pages[i] = NULL;
	}
	kfree(stripe->sectors);
	kfree(stripe->csums);
	stripe->sectors = NULL;
	stripe->csums = NULL;
	stripe->sctx = NULL;
	stripe->state = 0;
}

static int init_scrub_stripe(struct btrfs_fs_info *fs_info,
			     struct scrub_stripe *stripe)
{
	int ret;

	memset(stripe, 0, sizeof(*stripe));

	stripe->nr_sectors = BTRFS_STRIPE_LEN >> fs_info->sectorsize_bits;
	stripe->state = 0;

	init_waitqueue_head(&stripe->io_wait);
	init_waitqueue_head(&stripe->repair_wait);
	atomic_set(&stripe->pending_io, 0);
	spin_lock_init(&stripe->write_error_lock);

	ret = btrfs_alloc_page_array(SCRUB_STRIPE_PAGES, stripe->pages, false);
	if (ret < 0)
		goto error;

	stripe->sectors = kcalloc(stripe->nr_sectors,
				  sizeof(struct scrub_sector_verification),
				  GFP_KERNEL);
	if (!stripe->sectors)
		goto error;

	stripe->csums = kcalloc(BTRFS_STRIPE_LEN >> fs_info->sectorsize_bits,
				fs_info->csum_size, GFP_KERNEL);
	if (!stripe->csums)
		goto error;
	return 0;
error:
	release_scrub_stripe(stripe);
	return -ENOMEM;
}

static void wait_scrub_stripe_io(struct scrub_stripe *stripe)
{
	wait_event(stripe->io_wait, atomic_read(&stripe->pending_io) == 0);
}

static void scrub_put_ctx(struct scrub_ctx *sctx);

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

static noinline_for_stack void scrub_free_ctx(struct scrub_ctx *sctx)
{
	int i;

	if (!sctx)
		return;

	for (i = 0; i < SCRUB_TOTAL_STRIPES; i++)
		release_scrub_stripe(&sctx->stripes[i]);

	kvfree(sctx);
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

	/* Since sctx has inline 128 stripes, it can go beyond 64K easily.  Use
	 * kvzalloc().
	 */
	sctx = kvzalloc(sizeof(*sctx), GFP_KERNEL);
	if (!sctx)
		goto nomem;
	refcount_set(&sctx->refs, 1);
	sctx->is_dev_replace = is_dev_replace;
	sctx->fs_info = fs_info;
	sctx->extent_path.search_commit_root = 1;
	sctx->extent_path.skip_locking = 1;
	sctx->csum_path.search_commit_root = 1;
	sctx->csum_path.skip_locking = 1;
	for (i = 0; i < SCRUB_TOTAL_STRIPES; i++) {
		int ret;

		ret = init_scrub_stripe(fs_info, &sctx->stripes[i]);
		if (ret < 0)
			goto nomem;
		sctx->stripes[i].sctx = sctx;
	}
	sctx->first_free = 0;
	atomic_set(&sctx->cancel_req, 0);

	spin_lock_init(&sctx->stat_lock);
	sctx->throttle_deadline = 0;

	mutex_init(&sctx->wr_lock);
	if (is_dev_replace) {
		WARN_ON(!fs_info->dev_replace.tgtdev);
		sctx->wr_tgtdev = fs_info->dev_replace.tgtdev;
	}

	return sctx;

nomem:
	scrub_free_ctx(sctx);
	return ERR_PTR(-ENOMEM);
}

static int scrub_print_warning_inode(u64 inum, u64 offset, u64 num_bytes,
				     u64 root, void *warn_ctx)
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
"scrub: %s at logical %llu on dev %s, physical %llu root %llu inode %llu offset %llu length %u links %u (path: %s)",
				  swarn->errstr, swarn->logical,
				  btrfs_dev_name(swarn->dev),
				  swarn->physical,
				  root, inum, offset,
				  fs_info->sectorsize, nlink,
				  (char *)(unsigned long)ipath->fspath->val[i]);

	btrfs_put_root(local_root);
	free_ipath(ipath);
	return 0;

err:
	btrfs_warn_in_rcu(fs_info,
			  "scrub: %s at logical %llu on dev %s, physical %llu root %llu inode %llu offset %llu: path resolving failed with ret=%d",
			  swarn->errstr, swarn->logical,
			  btrfs_dev_name(swarn->dev),
			  swarn->physical,
			  root, inum, offset, ret);

	free_ipath(ipath);
	return 0;
}

static void scrub_print_common_warning(const char *errstr, struct btrfs_device *dev,
				       bool is_super, u64 logical, u64 physical)
{
	struct btrfs_fs_info *fs_info = dev->fs_info;
	struct btrfs_path *path;
	struct btrfs_key found_key;
	struct extent_buffer *eb;
	struct btrfs_extent_item *ei;
	struct scrub_warning swarn;
	u64 flags = 0;
	u32 item_size;
	int ret;

	/* Super block error, no need to search extent tree. */
	if (is_super) {
		btrfs_warn_in_rcu(fs_info, "scrub: %s on device %s, physical %llu",
				  errstr, btrfs_dev_name(dev), physical);
		return;
	}
	path = btrfs_alloc_path();
	if (!path)
		return;

	swarn.physical = physical;
	swarn.logical = logical;
	swarn.errstr = errstr;
	swarn.dev = NULL;

	ret = extent_from_logical(fs_info, swarn.logical, path, &found_key,
				  &flags);
	if (ret < 0)
		goto out;

	swarn.extent_item_size = found_key.offset;

	eb = path->nodes[0];
	ei = btrfs_item_ptr(eb, path->slots[0], struct btrfs_extent_item);
	item_size = btrfs_item_size(eb, path->slots[0]);

	if (flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
		unsigned long ptr = 0;
		u8 ref_level;
		u64 ref_root;

		while (true) {
			ret = tree_backref_for_extent(&ptr, eb, &found_key, ei,
						      item_size, &ref_root,
						      &ref_level);
			if (ret < 0) {
				btrfs_warn(fs_info,
		   "scrub: failed to resolve tree backref for logical %llu: %d",
					   swarn.logical, ret);
				break;
			}
			if (ret > 0)
				break;
			btrfs_warn_in_rcu(fs_info,
"scrub: %s at logical %llu on dev %s, physical %llu: metadata %s (level %d) in tree %llu",
				errstr, swarn.logical, btrfs_dev_name(dev),
				swarn.physical, (ref_level ? "node" : "leaf"),
				ref_level, ref_root);
		}
		btrfs_release_path(path);
	} else {
		struct btrfs_backref_walk_ctx ctx = { 0 };

		btrfs_release_path(path);

		ctx.bytenr = found_key.objectid;
		ctx.extent_item_pos = swarn.logical - found_key.objectid;
		ctx.fs_info = fs_info;

		swarn.path = path;
		swarn.dev = dev;

		iterate_extent_inodes(&ctx, true, scrub_print_warning_inode, &swarn);
	}

out:
	btrfs_free_path(path);
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

static void *scrub_stripe_get_kaddr(struct scrub_stripe *stripe, int sector_nr)
{
	u32 offset = (sector_nr << stripe->bg->fs_info->sectorsize_bits);
	const struct page *page = stripe->pages[offset >> PAGE_SHIFT];

	/* stripe->pages[] is allocated by us and no highmem is allowed. */
	ASSERT(page);
	ASSERT(!PageHighMem(page));
	return page_address(page) + offset_in_page(offset);
}

static void scrub_verify_one_metadata(struct scrub_stripe *stripe, int sector_nr)
{
	struct btrfs_fs_info *fs_info = stripe->bg->fs_info;
	const u32 sectors_per_tree = fs_info->nodesize >> fs_info->sectorsize_bits;
	const u64 logical = stripe->logical + (sector_nr << fs_info->sectorsize_bits);
	void *first_kaddr = scrub_stripe_get_kaddr(stripe, sector_nr);
	struct btrfs_header *header = first_kaddr;
	SHASH_DESC_ON_STACK(shash, fs_info->csum_shash);
	u8 on_disk_csum[BTRFS_CSUM_SIZE];
	u8 calculated_csum[BTRFS_CSUM_SIZE];

	/*
	 * Here we don't have a good way to attach the pages (and subpages)
	 * to a dummy extent buffer, thus we have to directly grab the members
	 * from pages.
	 */
	memcpy(on_disk_csum, header->csum, fs_info->csum_size);

	if (logical != btrfs_stack_header_bytenr(header)) {
		scrub_bitmap_set_meta_error(stripe, sector_nr, sectors_per_tree);
		scrub_bitmap_set_error(stripe, sector_nr, sectors_per_tree);
		btrfs_warn_rl(fs_info,
	  "scrub: tree block %llu mirror %u has bad bytenr, has %llu want %llu",
			      logical, stripe->mirror_num,
			      btrfs_stack_header_bytenr(header), logical);
		return;
	}
	if (memcmp(header->fsid, fs_info->fs_devices->metadata_uuid,
		   BTRFS_FSID_SIZE) != 0) {
		scrub_bitmap_set_meta_error(stripe, sector_nr, sectors_per_tree);
		scrub_bitmap_set_error(stripe, sector_nr, sectors_per_tree);
		btrfs_warn_rl(fs_info,
	      "scrub: tree block %llu mirror %u has bad fsid, has %pU want %pU",
			      logical, stripe->mirror_num,
			      header->fsid, fs_info->fs_devices->fsid);
		return;
	}
	if (memcmp(header->chunk_tree_uuid, fs_info->chunk_tree_uuid,
		   BTRFS_UUID_SIZE) != 0) {
		scrub_bitmap_set_meta_error(stripe, sector_nr, sectors_per_tree);
		scrub_bitmap_set_error(stripe, sector_nr, sectors_per_tree);
		btrfs_warn_rl(fs_info,
   "scrub: tree block %llu mirror %u has bad chunk tree uuid, has %pU want %pU",
			      logical, stripe->mirror_num,
			      header->chunk_tree_uuid, fs_info->chunk_tree_uuid);
		return;
	}

	/* Now check tree block csum. */
	shash->tfm = fs_info->csum_shash;
	crypto_shash_init(shash);
	crypto_shash_update(shash, first_kaddr + BTRFS_CSUM_SIZE,
			    fs_info->sectorsize - BTRFS_CSUM_SIZE);

	for (int i = sector_nr + 1; i < sector_nr + sectors_per_tree; i++) {
		crypto_shash_update(shash, scrub_stripe_get_kaddr(stripe, i),
				    fs_info->sectorsize);
	}

	crypto_shash_final(shash, calculated_csum);
	if (memcmp(calculated_csum, on_disk_csum, fs_info->csum_size) != 0) {
		scrub_bitmap_set_meta_error(stripe, sector_nr, sectors_per_tree);
		scrub_bitmap_set_error(stripe, sector_nr, sectors_per_tree);
		btrfs_warn_rl(fs_info,
"scrub: tree block %llu mirror %u has bad csum, has " CSUM_FMT " want " CSUM_FMT,
			      logical, stripe->mirror_num,
			      CSUM_FMT_VALUE(fs_info->csum_size, on_disk_csum),
			      CSUM_FMT_VALUE(fs_info->csum_size, calculated_csum));
		return;
	}
	if (stripe->sectors[sector_nr].generation !=
	    btrfs_stack_header_generation(header)) {
		scrub_bitmap_set_meta_gen_error(stripe, sector_nr, sectors_per_tree);
		scrub_bitmap_set_error(stripe, sector_nr, sectors_per_tree);
		btrfs_warn_rl(fs_info,
      "scrub: tree block %llu mirror %u has bad generation, has %llu want %llu",
			      logical, stripe->mirror_num,
			      btrfs_stack_header_generation(header),
			      stripe->sectors[sector_nr].generation);
		return;
	}
	scrub_bitmap_clear_error(stripe, sector_nr, sectors_per_tree);
	scrub_bitmap_clear_csum_error(stripe, sector_nr, sectors_per_tree);
	scrub_bitmap_clear_meta_error(stripe, sector_nr, sectors_per_tree);
	scrub_bitmap_clear_meta_gen_error(stripe, sector_nr, sectors_per_tree);
}

static void scrub_verify_one_sector(struct scrub_stripe *stripe, int sector_nr)
{
	struct btrfs_fs_info *fs_info = stripe->bg->fs_info;
	struct scrub_sector_verification *sector = &stripe->sectors[sector_nr];
	const u32 sectors_per_tree = fs_info->nodesize >> fs_info->sectorsize_bits;
	void *kaddr = scrub_stripe_get_kaddr(stripe, sector_nr);
	u8 csum_buf[BTRFS_CSUM_SIZE];
	int ret;

	ASSERT(sector_nr >= 0 && sector_nr < stripe->nr_sectors);

	/* Sector not utilized, skip it. */
	if (!scrub_bitmap_test_bit_has_extent(stripe, sector_nr))
		return;

	/* IO error, no need to check. */
	if (scrub_bitmap_test_bit_io_error(stripe, sector_nr))
		return;

	/* Metadata, verify the full tree block. */
	if (scrub_bitmap_test_bit_is_metadata(stripe, sector_nr)) {
		/*
		 * Check if the tree block crosses the stripe boundary.  If
		 * crossed the boundary, we cannot verify it but only give a
		 * warning.
		 *
		 * This can only happen on a very old filesystem where chunks
		 * are not ensured to be stripe aligned.
		 */
		if (unlikely(sector_nr + sectors_per_tree > stripe->nr_sectors)) {
			btrfs_warn_rl(fs_info,
			"scrub: tree block at %llu crosses stripe boundary %llu",
				      stripe->logical +
				      (sector_nr << fs_info->sectorsize_bits),
				      stripe->logical);
			return;
		}
		scrub_verify_one_metadata(stripe, sector_nr);
		return;
	}

	/*
	 * Data is easier, we just verify the data csum (if we have it).  For
	 * cases without csum, we have no other choice but to trust it.
	 */
	if (!sector->csum) {
		scrub_bitmap_clear_bit_error(stripe, sector_nr);
		return;
	}

	ret = btrfs_check_sector_csum(fs_info, kaddr, csum_buf, sector->csum);
	if (ret < 0) {
		scrub_bitmap_set_bit_csum_error(stripe, sector_nr);
		scrub_bitmap_set_bit_error(stripe, sector_nr);
	} else {
		scrub_bitmap_clear_bit_csum_error(stripe, sector_nr);
		scrub_bitmap_clear_bit_error(stripe, sector_nr);
	}
}

/* Verify specified sectors of a stripe. */
static void scrub_verify_one_stripe(struct scrub_stripe *stripe, unsigned long bitmap)
{
	struct btrfs_fs_info *fs_info = stripe->bg->fs_info;
	const u32 sectors_per_tree = fs_info->nodesize >> fs_info->sectorsize_bits;
	int sector_nr;

	for_each_set_bit(sector_nr, &bitmap, stripe->nr_sectors) {
		scrub_verify_one_sector(stripe, sector_nr);
		if (scrub_bitmap_test_bit_is_metadata(stripe, sector_nr))
			sector_nr += sectors_per_tree - 1;
	}
}

static int calc_sector_number(struct scrub_stripe *stripe, struct bio_vec *first_bvec)
{
	int i;

	for (i = 0; i < stripe->nr_sectors; i++) {
		if (scrub_stripe_get_kaddr(stripe, i) == bvec_virt(first_bvec))
			break;
	}
	ASSERT(i < stripe->nr_sectors);
	return i;
}

/*
 * Repair read is different to the regular read:
 *
 * - Only reads the failed sectors
 * - May have extra blocksize limits
 */
static void scrub_repair_read_endio(struct btrfs_bio *bbio)
{
	struct scrub_stripe *stripe = bbio->private;
	struct btrfs_fs_info *fs_info = stripe->bg->fs_info;
	struct bio_vec *bvec;
	int sector_nr = calc_sector_number(stripe, bio_first_bvec_all(&bbio->bio));
	u32 bio_size = 0;
	int i;

	ASSERT(sector_nr < stripe->nr_sectors);

	bio_for_each_bvec_all(bvec, &bbio->bio, i)
		bio_size += bvec->bv_len;

	if (bbio->bio.bi_status) {
		scrub_bitmap_set_io_error(stripe, sector_nr,
					  bio_size >> fs_info->sectorsize_bits);
		scrub_bitmap_set_error(stripe, sector_nr,
				       bio_size >> fs_info->sectorsize_bits);
	} else {
		scrub_bitmap_clear_io_error(stripe, sector_nr,
					  bio_size >> fs_info->sectorsize_bits);
	}
	bio_put(&bbio->bio);
	if (atomic_dec_and_test(&stripe->pending_io))
		wake_up(&stripe->io_wait);
}

static int calc_next_mirror(int mirror, int num_copies)
{
	ASSERT(mirror <= num_copies);
	return (mirror + 1 > num_copies) ? 1 : mirror + 1;
}

static void scrub_bio_add_sector(struct btrfs_bio *bbio, struct scrub_stripe *stripe,
				 int sector_nr)
{
	void *kaddr = scrub_stripe_get_kaddr(stripe, sector_nr);
	int ret;

	ret = bio_add_page(&bbio->bio, virt_to_page(kaddr), bbio->fs_info->sectorsize,
			   offset_in_page(kaddr));
	/*
	 * Caller should ensure the bbio has enough size.
	 * And we cannot use __bio_add_page(), which doesn't do any merge.
	 *
	 * Meanwhile for scrub_submit_initial_read() we fully rely on the merge
	 * to create the minimal amount of bio vectors, for fs block size < page
	 * size cases.
	 */
	ASSERT(ret == bbio->fs_info->sectorsize);
}

static void scrub_stripe_submit_repair_read(struct scrub_stripe *stripe,
					    int mirror, int blocksize, bool wait)
{
	struct btrfs_fs_info *fs_info = stripe->bg->fs_info;
	struct btrfs_bio *bbio = NULL;
	const unsigned long old_error_bitmap = scrub_bitmap_read_error(stripe);
	int i;

	ASSERT(stripe->mirror_num >= 1);
	ASSERT(atomic_read(&stripe->pending_io) == 0);

	for_each_set_bit(i, &old_error_bitmap, stripe->nr_sectors) {
		/* The current sector cannot be merged, submit the bio. */
		if (bbio && ((i > 0 && !test_bit(i - 1, &old_error_bitmap)) ||
			     bbio->bio.bi_iter.bi_size >= blocksize)) {
			ASSERT(bbio->bio.bi_iter.bi_size);
			atomic_inc(&stripe->pending_io);
			btrfs_submit_bbio(bbio, mirror);
			if (wait)
				wait_scrub_stripe_io(stripe);
			bbio = NULL;
		}

		if (!bbio) {
			bbio = btrfs_bio_alloc(stripe->nr_sectors, REQ_OP_READ,
				fs_info, scrub_repair_read_endio, stripe);
			bbio->bio.bi_iter.bi_sector = (stripe->logical +
				(i << fs_info->sectorsize_bits)) >> SECTOR_SHIFT;
		}

		scrub_bio_add_sector(bbio, stripe, i);
	}
	if (bbio) {
		ASSERT(bbio->bio.bi_iter.bi_size);
		atomic_inc(&stripe->pending_io);
		btrfs_submit_bbio(bbio, mirror);
		if (wait)
			wait_scrub_stripe_io(stripe);
	}
}

static void scrub_stripe_report_errors(struct scrub_ctx *sctx,
				       struct scrub_stripe *stripe,
				       const struct scrub_error_records *errors)
{
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_device *dev = NULL;
	const unsigned long extent_bitmap = scrub_bitmap_read_has_extent(stripe);
	const unsigned long error_bitmap = scrub_bitmap_read_error(stripe);
	u64 physical = 0;
	int nr_data_sectors = 0;
	int nr_meta_sectors = 0;
	int nr_nodatacsum_sectors = 0;
	int nr_repaired_sectors = 0;
	int sector_nr;

	if (test_bit(SCRUB_STRIPE_FLAG_NO_REPORT, &stripe->state))
		return;

	/*
	 * Init needed infos for error reporting.
	 *
	 * Although our scrub_stripe infrastructure is mostly based on btrfs_submit_bio()
	 * thus no need for dev/physical, error reporting still needs dev and physical.
	 */
	if (!bitmap_empty(&errors->init_error_bitmap, stripe->nr_sectors)) {
		u64 mapped_len = fs_info->sectorsize;
		struct btrfs_io_context *bioc = NULL;
		int stripe_index = stripe->mirror_num - 1;
		int ret;

		/* For scrub, our mirror_num should always start at 1. */
		ASSERT(stripe->mirror_num >= 1);
		ret = btrfs_map_block(fs_info, BTRFS_MAP_GET_READ_MIRRORS,
				      stripe->logical, &mapped_len, &bioc,
				      NULL, NULL);
		/*
		 * If we failed, dev will be NULL, and later detailed reports
		 * will just be skipped.
		 */
		if (ret < 0)
			goto skip;
		physical = bioc->stripes[stripe_index].physical;
		dev = bioc->stripes[stripe_index].dev;
		btrfs_put_bioc(bioc);
	}

skip:
	for_each_set_bit(sector_nr, &extent_bitmap, stripe->nr_sectors) {
		bool repaired = false;

		if (scrub_bitmap_test_bit_is_metadata(stripe, sector_nr)) {
			nr_meta_sectors++;
		} else {
			nr_data_sectors++;
			if (!stripe->sectors[sector_nr].csum)
				nr_nodatacsum_sectors++;
		}

		if (test_bit(sector_nr, &errors->init_error_bitmap) &&
		    !test_bit(sector_nr, &error_bitmap)) {
			nr_repaired_sectors++;
			repaired = true;
		}

		/* Good sector from the beginning, nothing need to be done. */
		if (!test_bit(sector_nr, &errors->init_error_bitmap))
			continue;

		/*
		 * Report error for the corrupted sectors.  If repaired, just
		 * output the message of repaired message.
		 */
		if (repaired) {
			if (dev) {
				btrfs_err_rl_in_rcu(fs_info,
		"scrub: fixed up error at logical %llu on dev %s physical %llu",
					    stripe->logical, btrfs_dev_name(dev),
					    physical);
			} else {
				btrfs_err_rl_in_rcu(fs_info,
			   "scrub: fixed up error at logical %llu on mirror %u",
					    stripe->logical, stripe->mirror_num);
			}
			continue;
		}

		/* The remaining are all for unrepaired. */
		if (dev) {
			btrfs_err_rl_in_rcu(fs_info,
"scrub: unable to fixup (regular) error at logical %llu on dev %s physical %llu",
					    stripe->logical, btrfs_dev_name(dev),
					    physical);
		} else {
			btrfs_err_rl_in_rcu(fs_info,
	  "scrub: unable to fixup (regular) error at logical %llu on mirror %u",
					    stripe->logical, stripe->mirror_num);
		}

		if (scrub_bitmap_test_bit_io_error(stripe, sector_nr))
			if (__ratelimit(&rs) && dev)
				scrub_print_common_warning("i/o error", dev, false,
						     stripe->logical, physical);
		if (scrub_bitmap_test_bit_csum_error(stripe, sector_nr))
			if (__ratelimit(&rs) && dev)
				scrub_print_common_warning("checksum error", dev, false,
						     stripe->logical, physical);
		if (scrub_bitmap_test_bit_meta_error(stripe, sector_nr))
			if (__ratelimit(&rs) && dev)
				scrub_print_common_warning("header error", dev, false,
						     stripe->logical, physical);
		if (scrub_bitmap_test_bit_meta_gen_error(stripe, sector_nr))
			if (__ratelimit(&rs) && dev)
				scrub_print_common_warning("generation error", dev, false,
						     stripe->logical, physical);
	}

	/* Update the device stats. */
	for (int i = 0; i < errors->nr_io_errors; i++)
		btrfs_dev_stat_inc_and_print(stripe->dev, BTRFS_DEV_STAT_READ_ERRS);
	for (int i = 0; i < errors->nr_csum_errors; i++)
		btrfs_dev_stat_inc_and_print(stripe->dev, BTRFS_DEV_STAT_CORRUPTION_ERRS);
	/* Generation mismatch error is based on each metadata, not each block. */
	for (int i = 0; i < errors->nr_meta_gen_errors;
	     i += (fs_info->nodesize >> fs_info->sectorsize_bits))
		btrfs_dev_stat_inc_and_print(stripe->dev, BTRFS_DEV_STAT_GENERATION_ERRS);

	spin_lock(&sctx->stat_lock);
	sctx->stat.data_extents_scrubbed += stripe->nr_data_extents;
	sctx->stat.tree_extents_scrubbed += stripe->nr_meta_extents;
	sctx->stat.data_bytes_scrubbed += nr_data_sectors << fs_info->sectorsize_bits;
	sctx->stat.tree_bytes_scrubbed += nr_meta_sectors << fs_info->sectorsize_bits;
	sctx->stat.no_csum += nr_nodatacsum_sectors;
	sctx->stat.read_errors += errors->nr_io_errors;
	sctx->stat.csum_errors += errors->nr_csum_errors;
	sctx->stat.verify_errors += errors->nr_meta_errors +
				    errors->nr_meta_gen_errors;
	sctx->stat.uncorrectable_errors +=
		bitmap_weight(&error_bitmap, stripe->nr_sectors);
	sctx->stat.corrected_errors += nr_repaired_sectors;
	spin_unlock(&sctx->stat_lock);
}

static void scrub_write_sectors(struct scrub_ctx *sctx, struct scrub_stripe *stripe,
				unsigned long write_bitmap, bool dev_replace);

/*
 * The main entrance for all read related scrub work, including:
 *
 * - Wait for the initial read to finish
 * - Verify and locate any bad sectors
 * - Go through the remaining mirrors and try to read as large blocksize as
 *   possible
 * - Go through all mirrors (including the failed mirror) sector-by-sector
 * - Submit writeback for repaired sectors
 *
 * Writeback for dev-replace does not happen here, it needs extra
 * synchronization for zoned devices.
 */
static void scrub_stripe_read_repair_worker(struct work_struct *work)
{
	struct scrub_stripe *stripe = container_of(work, struct scrub_stripe, work);
	struct scrub_ctx *sctx = stripe->sctx;
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct scrub_error_records errors = { 0 };
	int num_copies = btrfs_num_copies(fs_info, stripe->bg->start,
					  stripe->bg->length);
	unsigned long repaired;
	unsigned long error;
	int mirror;
	int i;

	ASSERT(stripe->mirror_num > 0);

	wait_scrub_stripe_io(stripe);
	scrub_verify_one_stripe(stripe, scrub_bitmap_read_has_extent(stripe));
	/* Save the initial failed bitmap for later repair and report usage. */
	errors.init_error_bitmap = scrub_bitmap_read_error(stripe);
	errors.nr_io_errors = scrub_bitmap_weight_io_error(stripe);
	errors.nr_csum_errors = scrub_bitmap_weight_csum_error(stripe);
	errors.nr_meta_errors = scrub_bitmap_weight_meta_error(stripe);
	errors.nr_meta_gen_errors = scrub_bitmap_weight_meta_gen_error(stripe);

	if (bitmap_empty(&errors.init_error_bitmap, stripe->nr_sectors))
		goto out;

	/*
	 * Try all remaining mirrors.
	 *
	 * Here we still try to read as large block as possible, as this is
	 * faster and we have extra safety nets to rely on.
	 */
	for (mirror = calc_next_mirror(stripe->mirror_num, num_copies);
	     mirror != stripe->mirror_num;
	     mirror = calc_next_mirror(mirror, num_copies)) {
		const unsigned long old_error_bitmap = scrub_bitmap_read_error(stripe);

		scrub_stripe_submit_repair_read(stripe, mirror,
						BTRFS_STRIPE_LEN, false);
		wait_scrub_stripe_io(stripe);
		scrub_verify_one_stripe(stripe, old_error_bitmap);
		if (scrub_bitmap_empty_error(stripe))
			goto out;
	}

	/*
	 * Last safety net, try re-checking all mirrors, including the failed
	 * one, sector-by-sector.
	 *
	 * As if one sector failed the drive's internal csum, the whole read
	 * containing the offending sector would be marked as error.
	 * Thus here we do sector-by-sector read.
	 *
	 * This can be slow, thus we only try it as the last resort.
	 */

	for (i = 0, mirror = stripe->mirror_num;
	     i < num_copies;
	     i++, mirror = calc_next_mirror(mirror, num_copies)) {
		const unsigned long old_error_bitmap = scrub_bitmap_read_error(stripe);

		scrub_stripe_submit_repair_read(stripe, mirror,
						fs_info->sectorsize, true);
		wait_scrub_stripe_io(stripe);
		scrub_verify_one_stripe(stripe, old_error_bitmap);
		if (scrub_bitmap_empty_error(stripe))
			goto out;
	}
out:
	error = scrub_bitmap_read_error(stripe);
	/*
	 * Submit the repaired sectors.  For zoned case, we cannot do repair
	 * in-place, but queue the bg to be relocated.
	 */
	bitmap_andnot(&repaired, &errors.init_error_bitmap, &error,
		      stripe->nr_sectors);
	if (!sctx->readonly && !bitmap_empty(&repaired, stripe->nr_sectors)) {
		if (btrfs_is_zoned(fs_info)) {
			btrfs_repair_one_zone(fs_info, sctx->stripes[0].bg->start);
		} else {
			scrub_write_sectors(sctx, stripe, repaired, false);
			wait_scrub_stripe_io(stripe);
		}
	}

	scrub_stripe_report_errors(sctx, stripe, &errors);
	set_bit(SCRUB_STRIPE_FLAG_REPAIR_DONE, &stripe->state);
	wake_up(&stripe->repair_wait);
}

static void scrub_read_endio(struct btrfs_bio *bbio)
{
	struct scrub_stripe *stripe = bbio->private;
	struct bio_vec *bvec;
	int sector_nr = calc_sector_number(stripe, bio_first_bvec_all(&bbio->bio));
	int num_sectors;
	u32 bio_size = 0;
	int i;

	ASSERT(sector_nr < stripe->nr_sectors);
	bio_for_each_bvec_all(bvec, &bbio->bio, i)
		bio_size += bvec->bv_len;
	num_sectors = bio_size >> stripe->bg->fs_info->sectorsize_bits;

	if (bbio->bio.bi_status) {
		scrub_bitmap_set_io_error(stripe, sector_nr, num_sectors);
		scrub_bitmap_set_error(stripe, sector_nr, num_sectors);
	} else {
		scrub_bitmap_clear_io_error(stripe, sector_nr, num_sectors);
	}
	bio_put(&bbio->bio);
	if (atomic_dec_and_test(&stripe->pending_io)) {
		wake_up(&stripe->io_wait);
		INIT_WORK(&stripe->work, scrub_stripe_read_repair_worker);
		queue_work(stripe->bg->fs_info->scrub_workers, &stripe->work);
	}
}

static void scrub_write_endio(struct btrfs_bio *bbio)
{
	struct scrub_stripe *stripe = bbio->private;
	struct btrfs_fs_info *fs_info = stripe->bg->fs_info;
	struct bio_vec *bvec;
	int sector_nr = calc_sector_number(stripe, bio_first_bvec_all(&bbio->bio));
	u32 bio_size = 0;
	int i;

	bio_for_each_bvec_all(bvec, &bbio->bio, i)
		bio_size += bvec->bv_len;

	if (bbio->bio.bi_status) {
		unsigned long flags;

		spin_lock_irqsave(&stripe->write_error_lock, flags);
		bitmap_set(&stripe->write_error_bitmap, sector_nr,
			   bio_size >> fs_info->sectorsize_bits);
		spin_unlock_irqrestore(&stripe->write_error_lock, flags);
		for (int i = 0; i < (bio_size >> fs_info->sectorsize_bits); i++)
			btrfs_dev_stat_inc_and_print(stripe->dev,
						     BTRFS_DEV_STAT_WRITE_ERRS);
	}
	bio_put(&bbio->bio);

	if (atomic_dec_and_test(&stripe->pending_io))
		wake_up(&stripe->io_wait);
}

static void scrub_submit_write_bio(struct scrub_ctx *sctx,
				   struct scrub_stripe *stripe,
				   struct btrfs_bio *bbio, bool dev_replace)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	u32 bio_len = bbio->bio.bi_iter.bi_size;
	u32 bio_off = (bbio->bio.bi_iter.bi_sector << SECTOR_SHIFT) -
		      stripe->logical;

	fill_writer_pointer_gap(sctx, stripe->physical + bio_off);
	atomic_inc(&stripe->pending_io);
	btrfs_submit_repair_write(bbio, stripe->mirror_num, dev_replace);
	if (!btrfs_is_zoned(fs_info))
		return;
	/*
	 * For zoned writeback, queue depth must be 1, thus we must wait for
	 * the write to finish before the next write.
	 */
	wait_scrub_stripe_io(stripe);

	/*
	 * And also need to update the write pointer if write finished
	 * successfully.
	 */
	if (!test_bit(bio_off >> fs_info->sectorsize_bits,
		      &stripe->write_error_bitmap))
		sctx->write_pointer += bio_len;
}

/*
 * Submit the write bio(s) for the sectors specified by @write_bitmap.
 *
 * Here we utilize btrfs_submit_repair_write(), which has some extra benefits:
 *
 * - Only needs logical bytenr and mirror_num
 *   Just like the scrub read path
 *
 * - Would only result in writes to the specified mirror
 *   Unlike the regular writeback path, which would write back to all stripes
 *
 * - Handle dev-replace and read-repair writeback differently
 */
static void scrub_write_sectors(struct scrub_ctx *sctx, struct scrub_stripe *stripe,
				unsigned long write_bitmap, bool dev_replace)
{
	struct btrfs_fs_info *fs_info = stripe->bg->fs_info;
	struct btrfs_bio *bbio = NULL;
	int sector_nr;

	for_each_set_bit(sector_nr, &write_bitmap, stripe->nr_sectors) {
		/* We should only writeback sectors covered by an extent. */
		ASSERT(scrub_bitmap_test_bit_has_extent(stripe, sector_nr));

		/* Cannot merge with previous sector, submit the current one. */
		if (bbio && sector_nr && !test_bit(sector_nr - 1, &write_bitmap)) {
			scrub_submit_write_bio(sctx, stripe, bbio, dev_replace);
			bbio = NULL;
		}
		if (!bbio) {
			bbio = btrfs_bio_alloc(stripe->nr_sectors, REQ_OP_WRITE,
					       fs_info, scrub_write_endio, stripe);
			bbio->bio.bi_iter.bi_sector = (stripe->logical +
				(sector_nr << fs_info->sectorsize_bits)) >>
				SECTOR_SHIFT;
		}
		scrub_bio_add_sector(bbio, stripe, sector_nr);
	}
	if (bbio)
		scrub_submit_write_bio(sctx, stripe, bbio, dev_replace);
}

/*
 * Throttling of IO submission, bandwidth-limit based, the timeslice is 1
 * second.  Limit can be set via /sys/fs/UUID/devinfo/devid/scrub_speed_max.
 */
static void scrub_throttle_dev_io(struct scrub_ctx *sctx, struct btrfs_device *device,
				  unsigned int bio_size)
{
	const int time_slice = 1000;
	s64 delta;
	ktime_t now;
	u32 div;
	u64 bwlimit;

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
		sctx->throttle_sent += bio_size;
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

/*
 * Given a physical address, this will calculate it's
 * logical offset. if this is a parity stripe, it will return
 * the most left data stripe's logical offset.
 *
 * return 0 if it is a data stripe, 1 means parity stripe.
 */
static int get_raid56_logic_offset(u64 physical, int num,
				   struct btrfs_chunk_map *map, u64 *offset,
				   u64 *stripe_start)
{
	int i;
	int j = 0;
	u64 last_offset;
	const int data_stripes = nr_data_stripes(map);

	last_offset = (physical - map->stripes[num].physical) * data_stripes;
	if (stripe_start)
		*stripe_start = last_offset;

	*offset = last_offset;
	for (i = 0; i < data_stripes; i++) {
		u32 stripe_nr;
		u32 stripe_index;
		u32 rot;

		*offset = last_offset + btrfs_stripe_nr_to_offset(i);

		stripe_nr = (u32)(*offset >> BTRFS_STRIPE_LEN_SHIFT) / data_stripes;

		/* Work out the disk rotation on this stripe-set */
		rot = stripe_nr % map->num_stripes;
		/* calculate which stripe this data locates */
		rot += i;
		stripe_index = rot % map->num_stripes;
		if (stripe_index == num)
			return 0;
		if (stripe_index < num)
			j++;
	}
	*offset = last_offset + btrfs_stripe_nr_to_offset(j);
	return 1;
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

	key.objectid = search_start;
	if (btrfs_fs_incompat(fs_info, SKINNY_METADATA))
		key.type = BTRFS_METADATA_ITEM_KEY;
	else
		key.type = BTRFS_EXTENT_ITEM_KEY;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, extent_root, &key, path, 0, 0);
	if (ret < 0)
		return ret;
	if (ret == 0) {
		/*
		 * Key with offset -1 found, there would have to exist an extent
		 * item with such offset, but this is out of the valid range.
		 */
		btrfs_release_path(path);
		return -EUCLEAN;
	}

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
		ret = btrfs_next_item(extent_root, path);
		if (ret) {
			/* Either no more items or a fatal error. */
			btrfs_release_path(path);
			return ret;
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

static int sync_write_pointer_for_zoned(struct scrub_ctx *sctx, u64 logical,
					u64 physical, u64 physical_end)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	int ret = 0;

	if (!btrfs_is_zoned(fs_info))
		return 0;

	mutex_lock(&sctx->wr_lock);
	if (sctx->write_pointer < physical_end) {
		ret = btrfs_sync_zone_write_pointer(sctx->wr_tgtdev, logical,
						    physical,
						    sctx->write_pointer);
		if (ret)
			btrfs_err(fs_info, "scrub: zoned: failed to recover write pointer");
	}
	mutex_unlock(&sctx->wr_lock);
	btrfs_dev_clear_zone_empty(sctx->wr_tgtdev, physical);

	return ret;
}

static void fill_one_extent_info(struct btrfs_fs_info *fs_info,
				 struct scrub_stripe *stripe,
				 u64 extent_start, u64 extent_len,
				 u64 extent_flags, u64 extent_gen)
{
	for (u64 cur_logical = max(stripe->logical, extent_start);
	     cur_logical < min(stripe->logical + BTRFS_STRIPE_LEN,
			       extent_start + extent_len);
	     cur_logical += fs_info->sectorsize) {
		const int nr_sector = (cur_logical - stripe->logical) >>
				      fs_info->sectorsize_bits;
		struct scrub_sector_verification *sector =
						&stripe->sectors[nr_sector];

		scrub_bitmap_set_bit_has_extent(stripe, nr_sector);
		if (extent_flags & BTRFS_EXTENT_FLAG_TREE_BLOCK) {
			scrub_bitmap_set_bit_is_metadata(stripe, nr_sector);
			sector->generation = extent_gen;
		}
	}
}

static void scrub_stripe_reset_bitmaps(struct scrub_stripe *stripe)
{
	ASSERT(stripe->nr_sectors);
	bitmap_zero(stripe->bitmaps, scrub_bitmap_nr_last * stripe->nr_sectors);
}

/*
 * Locate one stripe which has at least one extent in its range.
 *
 * Return 0 if found such stripe, and store its info into @stripe.
 * Return >0 if there is no such stripe in the specified range.
 * Return <0 for error.
 */
static int scrub_find_fill_first_stripe(struct btrfs_block_group *bg,
					struct btrfs_path *extent_path,
					struct btrfs_path *csum_path,
					struct btrfs_device *dev, u64 physical,
					int mirror_num, u64 logical_start,
					u32 logical_len,
					struct scrub_stripe *stripe)
{
	struct btrfs_fs_info *fs_info = bg->fs_info;
	struct btrfs_root *extent_root = btrfs_extent_root(fs_info, bg->start);
	struct btrfs_root *csum_root = btrfs_csum_root(fs_info, bg->start);
	const u64 logical_end = logical_start + logical_len;
	u64 cur_logical = logical_start;
	u64 stripe_end;
	u64 extent_start;
	u64 extent_len;
	u64 extent_flags;
	u64 extent_gen;
	int ret;

	if (unlikely(!extent_root || !csum_root)) {
		btrfs_err(fs_info, "scrub: no valid extent or csum root found");
		return -EUCLEAN;
	}
	memset(stripe->sectors, 0, sizeof(struct scrub_sector_verification) *
				   stripe->nr_sectors);
	scrub_stripe_reset_bitmaps(stripe);

	/* The range must be inside the bg. */
	ASSERT(logical_start >= bg->start && logical_end <= bg->start + bg->length);

	ret = find_first_extent_item(extent_root, extent_path, logical_start,
				     logical_len);
	/* Either error or not found. */
	if (ret)
		goto out;
	get_extent_info(extent_path, &extent_start, &extent_len, &extent_flags,
			&extent_gen);
	if (extent_flags & BTRFS_EXTENT_FLAG_TREE_BLOCK)
		stripe->nr_meta_extents++;
	if (extent_flags & BTRFS_EXTENT_FLAG_DATA)
		stripe->nr_data_extents++;
	cur_logical = max(extent_start, cur_logical);

	/*
	 * Round down to stripe boundary.
	 *
	 * The extra calculation against bg->start is to handle block groups
	 * whose logical bytenr is not BTRFS_STRIPE_LEN aligned.
	 */
	stripe->logical = round_down(cur_logical - bg->start, BTRFS_STRIPE_LEN) +
			  bg->start;
	stripe->physical = physical + stripe->logical - logical_start;
	stripe->dev = dev;
	stripe->bg = bg;
	stripe->mirror_num = mirror_num;
	stripe_end = stripe->logical + BTRFS_STRIPE_LEN - 1;

	/* Fill the first extent info into stripe->sectors[] array. */
	fill_one_extent_info(fs_info, stripe, extent_start, extent_len,
			     extent_flags, extent_gen);
	cur_logical = extent_start + extent_len;

	/* Fill the extent info for the remaining sectors. */
	while (cur_logical <= stripe_end) {
		ret = find_first_extent_item(extent_root, extent_path, cur_logical,
					     stripe_end - cur_logical + 1);
		if (ret < 0)
			goto out;
		if (ret > 0) {
			ret = 0;
			break;
		}
		get_extent_info(extent_path, &extent_start, &extent_len,
				&extent_flags, &extent_gen);
		if (extent_flags & BTRFS_EXTENT_FLAG_TREE_BLOCK)
			stripe->nr_meta_extents++;
		if (extent_flags & BTRFS_EXTENT_FLAG_DATA)
			stripe->nr_data_extents++;
		fill_one_extent_info(fs_info, stripe, extent_start, extent_len,
				     extent_flags, extent_gen);
		cur_logical = extent_start + extent_len;
	}

	/* Now fill the data csum. */
	if (bg->flags & BTRFS_BLOCK_GROUP_DATA) {
		int sector_nr;
		unsigned long csum_bitmap = 0;

		/* Csum space should have already been allocated. */
		ASSERT(stripe->csums);

		/*
		 * Our csum bitmap should be large enough, as BTRFS_STRIPE_LEN
		 * should contain at most 16 sectors.
		 */
		ASSERT(BITS_PER_LONG >= BTRFS_STRIPE_LEN >> fs_info->sectorsize_bits);

		ret = btrfs_lookup_csums_bitmap(csum_root, csum_path,
						stripe->logical, stripe_end,
						stripe->csums, &csum_bitmap);
		if (ret < 0)
			goto out;
		if (ret > 0)
			ret = 0;

		for_each_set_bit(sector_nr, &csum_bitmap, stripe->nr_sectors) {
			stripe->sectors[sector_nr].csum = stripe->csums +
				sector_nr * fs_info->csum_size;
		}
	}
	set_bit(SCRUB_STRIPE_FLAG_INITIALIZED, &stripe->state);
out:
	return ret;
}

static void scrub_reset_stripe(struct scrub_stripe *stripe)
{
	scrub_stripe_reset_bitmaps(stripe);

	stripe->nr_meta_extents = 0;
	stripe->nr_data_extents = 0;
	stripe->state = 0;

	for (int i = 0; i < stripe->nr_sectors; i++) {
		stripe->sectors[i].csum = NULL;
		stripe->sectors[i].generation = 0;
	}
}

static u32 stripe_length(const struct scrub_stripe *stripe)
{
	ASSERT(stripe->bg);

	return min(BTRFS_STRIPE_LEN,
		   stripe->bg->start + stripe->bg->length - stripe->logical);
}

static void scrub_submit_extent_sector_read(struct scrub_stripe *stripe)
{
	struct btrfs_fs_info *fs_info = stripe->bg->fs_info;
	struct btrfs_bio *bbio = NULL;
	unsigned int nr_sectors = stripe_length(stripe) >> fs_info->sectorsize_bits;
	const unsigned long has_extent = scrub_bitmap_read_has_extent(stripe);
	u64 stripe_len = BTRFS_STRIPE_LEN;
	int mirror = stripe->mirror_num;
	int i;

	atomic_inc(&stripe->pending_io);

	for_each_set_bit(i, &has_extent, stripe->nr_sectors) {
		/* We're beyond the chunk boundary, no need to read anymore. */
		if (i >= nr_sectors)
			break;

		/* The current sector cannot be merged, submit the bio. */
		if (bbio &&
		    ((i > 0 && !test_bit(i - 1, &has_extent)) ||
		     bbio->bio.bi_iter.bi_size >= stripe_len)) {
			ASSERT(bbio->bio.bi_iter.bi_size);
			atomic_inc(&stripe->pending_io);
			btrfs_submit_bbio(bbio, mirror);
			bbio = NULL;
		}

		if (!bbio) {
			struct btrfs_io_stripe io_stripe = {};
			struct btrfs_io_context *bioc = NULL;
			const u64 logical = stripe->logical +
					    (i << fs_info->sectorsize_bits);
			int err;

			io_stripe.rst_search_commit_root = true;
			stripe_len = (nr_sectors - i) << fs_info->sectorsize_bits;
			/*
			 * For RST cases, we need to manually split the bbio to
			 * follow the RST boundary.
			 */
			err = btrfs_map_block(fs_info, BTRFS_MAP_READ, logical,
					      &stripe_len, &bioc, &io_stripe, &mirror);
			btrfs_put_bioc(bioc);
			if (err < 0) {
				if (err != -ENODATA) {
					/*
					 * Earlier btrfs_get_raid_extent_offset()
					 * returned -ENODATA, which means there's
					 * no entry for the corresponding range
					 * in the stripe tree.  But if it's in
					 * the extent tree, then it's a preallocated
					 * extent and not an error.
					 */
					scrub_bitmap_set_bit_io_error(stripe, i);
					scrub_bitmap_set_bit_error(stripe, i);
				}
				continue;
			}

			bbio = btrfs_bio_alloc(stripe->nr_sectors, REQ_OP_READ,
					       fs_info, scrub_read_endio, stripe);
			bbio->bio.bi_iter.bi_sector = logical >> SECTOR_SHIFT;
		}

		scrub_bio_add_sector(bbio, stripe, i);
	}

	if (bbio) {
		ASSERT(bbio->bio.bi_iter.bi_size);
		atomic_inc(&stripe->pending_io);
		btrfs_submit_bbio(bbio, mirror);
	}

	if (atomic_dec_and_test(&stripe->pending_io)) {
		wake_up(&stripe->io_wait);
		INIT_WORK(&stripe->work, scrub_stripe_read_repair_worker);
		queue_work(stripe->bg->fs_info->scrub_workers, &stripe->work);
	}
}

static void scrub_submit_initial_read(struct scrub_ctx *sctx,
				      struct scrub_stripe *stripe)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_bio *bbio;
	unsigned int nr_sectors = stripe_length(stripe) >> fs_info->sectorsize_bits;
	int mirror = stripe->mirror_num;

	ASSERT(stripe->bg);
	ASSERT(stripe->mirror_num > 0);
	ASSERT(test_bit(SCRUB_STRIPE_FLAG_INITIALIZED, &stripe->state));

	if (btrfs_need_stripe_tree_update(fs_info, stripe->bg->flags)) {
		scrub_submit_extent_sector_read(stripe);
		return;
	}

	bbio = btrfs_bio_alloc(SCRUB_STRIPE_PAGES, REQ_OP_READ, fs_info,
			       scrub_read_endio, stripe);

	bbio->bio.bi_iter.bi_sector = stripe->logical >> SECTOR_SHIFT;
	/* Read the whole range inside the chunk boundary. */
	for (unsigned int cur = 0; cur < nr_sectors; cur++)
		scrub_bio_add_sector(bbio, stripe, cur);
	atomic_inc(&stripe->pending_io);

	/*
	 * For dev-replace, either user asks to avoid the source dev, or
	 * the device is missing, we try the next mirror instead.
	 */
	if (sctx->is_dev_replace &&
	    (fs_info->dev_replace.cont_reading_from_srcdev_mode ==
	     BTRFS_DEV_REPLACE_ITEM_CONT_READING_FROM_SRCDEV_MODE_AVOID ||
	     !stripe->dev->bdev)) {
		int num_copies = btrfs_num_copies(fs_info, stripe->bg->start,
						  stripe->bg->length);

		mirror = calc_next_mirror(mirror, num_copies);
	}
	btrfs_submit_bbio(bbio, mirror);
}

static bool stripe_has_metadata_error(struct scrub_stripe *stripe)
{
	const unsigned long error = scrub_bitmap_read_error(stripe);
	int i;

	for_each_set_bit(i, &error, stripe->nr_sectors) {
		if (scrub_bitmap_test_bit_is_metadata(stripe, i)) {
			struct btrfs_fs_info *fs_info = stripe->bg->fs_info;

			btrfs_err(fs_info,
		    "scrub: stripe %llu has unrepaired metadata sector at logical %llu",
				  stripe->logical,
				  stripe->logical + (i << fs_info->sectorsize_bits));
			return true;
		}
	}
	return false;
}

static void submit_initial_group_read(struct scrub_ctx *sctx,
				      unsigned int first_slot,
				      unsigned int nr_stripes)
{
	struct blk_plug plug;

	ASSERT(first_slot < SCRUB_TOTAL_STRIPES);
	ASSERT(first_slot + nr_stripes <= SCRUB_TOTAL_STRIPES);

	scrub_throttle_dev_io(sctx, sctx->stripes[0].dev,
			      btrfs_stripe_nr_to_offset(nr_stripes));
	blk_start_plug(&plug);
	for (int i = 0; i < nr_stripes; i++) {
		struct scrub_stripe *stripe = &sctx->stripes[first_slot + i];

		/* Those stripes should be initialized. */
		ASSERT(test_bit(SCRUB_STRIPE_FLAG_INITIALIZED, &stripe->state));
		scrub_submit_initial_read(sctx, stripe);
	}
	blk_finish_plug(&plug);
}

static int flush_scrub_stripes(struct scrub_ctx *sctx)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct scrub_stripe *stripe;
	const int nr_stripes = sctx->cur_stripe;
	int ret = 0;

	if (!nr_stripes)
		return 0;

	ASSERT(test_bit(SCRUB_STRIPE_FLAG_INITIALIZED, &sctx->stripes[0].state));

	/* Submit the stripes which are populated but not submitted. */
	if (nr_stripes % SCRUB_STRIPES_PER_GROUP) {
		const int first_slot = round_down(nr_stripes, SCRUB_STRIPES_PER_GROUP);

		submit_initial_group_read(sctx, first_slot, nr_stripes - first_slot);
	}

	for (int i = 0; i < nr_stripes; i++) {
		stripe = &sctx->stripes[i];

		wait_event(stripe->repair_wait,
			   test_bit(SCRUB_STRIPE_FLAG_REPAIR_DONE, &stripe->state));
	}

	/* Submit for dev-replace. */
	if (sctx->is_dev_replace) {
		/*
		 * For dev-replace, if we know there is something wrong with
		 * metadata, we should immediately abort.
		 */
		for (int i = 0; i < nr_stripes; i++) {
			if (stripe_has_metadata_error(&sctx->stripes[i])) {
				ret = -EIO;
				goto out;
			}
		}
		for (int i = 0; i < nr_stripes; i++) {
			unsigned long good;
			unsigned long has_extent;
			unsigned long error;

			stripe = &sctx->stripes[i];

			ASSERT(stripe->dev == fs_info->dev_replace.srcdev);

			has_extent = scrub_bitmap_read_has_extent(stripe);
			error = scrub_bitmap_read_error(stripe);
			bitmap_andnot(&good, &has_extent, &error, stripe->nr_sectors);
			scrub_write_sectors(sctx, stripe, good, true);
		}
	}

	/* Wait for the above writebacks to finish. */
	for (int i = 0; i < nr_stripes; i++) {
		stripe = &sctx->stripes[i];

		wait_scrub_stripe_io(stripe);
		spin_lock(&sctx->stat_lock);
		sctx->stat.last_physical = stripe->physical + stripe_length(stripe);
		spin_unlock(&sctx->stat_lock);
		scrub_reset_stripe(stripe);
	}
out:
	sctx->cur_stripe = 0;
	return ret;
}

static void raid56_scrub_wait_endio(struct bio *bio)
{
	complete(bio->bi_private);
}

static int queue_scrub_stripe(struct scrub_ctx *sctx, struct btrfs_block_group *bg,
			      struct btrfs_device *dev, int mirror_num,
			      u64 logical, u32 length, u64 physical,
			      u64 *found_logical_ret)
{
	struct scrub_stripe *stripe;
	int ret;

	/*
	 * There should always be one slot left, as caller filling the last
	 * slot should flush them all.
	 */
	ASSERT(sctx->cur_stripe < SCRUB_TOTAL_STRIPES);

	/* @found_logical_ret must be specified. */
	ASSERT(found_logical_ret);

	stripe = &sctx->stripes[sctx->cur_stripe];
	scrub_reset_stripe(stripe);
	ret = scrub_find_fill_first_stripe(bg, &sctx->extent_path,
					   &sctx->csum_path, dev, physical,
					   mirror_num, logical, length, stripe);
	/* Either >0 as no more extents or <0 for error. */
	if (ret)
		return ret;
	*found_logical_ret = stripe->logical;
	sctx->cur_stripe++;

	/* We filled one group, submit it. */
	if (sctx->cur_stripe % SCRUB_STRIPES_PER_GROUP == 0) {
		const int first_slot = sctx->cur_stripe - SCRUB_STRIPES_PER_GROUP;

		submit_initial_group_read(sctx, first_slot, SCRUB_STRIPES_PER_GROUP);
	}

	/* Last slot used, flush them all. */
	if (sctx->cur_stripe == SCRUB_TOTAL_STRIPES)
		return flush_scrub_stripes(sctx);
	return 0;
}

static int scrub_raid56_parity_stripe(struct scrub_ctx *sctx,
				      struct btrfs_device *scrub_dev,
				      struct btrfs_block_group *bg,
				      struct btrfs_chunk_map *map,
				      u64 full_stripe_start)
{
	DECLARE_COMPLETION_ONSTACK(io_done);
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_raid_bio *rbio;
	struct btrfs_io_context *bioc = NULL;
	struct btrfs_path extent_path = { 0 };
	struct btrfs_path csum_path = { 0 };
	struct bio *bio;
	struct scrub_stripe *stripe;
	bool all_empty = true;
	const int data_stripes = nr_data_stripes(map);
	unsigned long extent_bitmap = 0;
	u64 length = btrfs_stripe_nr_to_offset(data_stripes);
	int ret;

	ASSERT(sctx->raid56_data_stripes);

	/*
	 * For data stripe search, we cannot reuse the same extent/csum paths,
	 * as the data stripe bytenr may be smaller than previous extent.  Thus
	 * we have to use our own extent/csum paths.
	 */
	extent_path.search_commit_root = 1;
	extent_path.skip_locking = 1;
	csum_path.search_commit_root = 1;
	csum_path.skip_locking = 1;

	for (int i = 0; i < data_stripes; i++) {
		int stripe_index;
		int rot;
		u64 physical;

		stripe = &sctx->raid56_data_stripes[i];
		rot = div_u64(full_stripe_start - bg->start,
			      data_stripes) >> BTRFS_STRIPE_LEN_SHIFT;
		stripe_index = (i + rot) % map->num_stripes;
		physical = map->stripes[stripe_index].physical +
			   btrfs_stripe_nr_to_offset(rot);

		scrub_reset_stripe(stripe);
		set_bit(SCRUB_STRIPE_FLAG_NO_REPORT, &stripe->state);
		ret = scrub_find_fill_first_stripe(bg, &extent_path, &csum_path,
				map->stripes[stripe_index].dev, physical, 1,
				full_stripe_start + btrfs_stripe_nr_to_offset(i),
				BTRFS_STRIPE_LEN, stripe);
		if (ret < 0)
			goto out;
		/*
		 * No extent in this data stripe, need to manually mark them
		 * initialized to make later read submission happy.
		 */
		if (ret > 0) {
			stripe->logical = full_stripe_start +
					  btrfs_stripe_nr_to_offset(i);
			stripe->dev = map->stripes[stripe_index].dev;
			stripe->mirror_num = 1;
			set_bit(SCRUB_STRIPE_FLAG_INITIALIZED, &stripe->state);
		}
	}

	/* Check if all data stripes are empty. */
	for (int i = 0; i < data_stripes; i++) {
		stripe = &sctx->raid56_data_stripes[i];
		if (!scrub_bitmap_empty_has_extent(stripe)) {
			all_empty = false;
			break;
		}
	}
	if (all_empty) {
		ret = 0;
		goto out;
	}

	for (int i = 0; i < data_stripes; i++) {
		stripe = &sctx->raid56_data_stripes[i];
		scrub_submit_initial_read(sctx, stripe);
	}
	for (int i = 0; i < data_stripes; i++) {
		stripe = &sctx->raid56_data_stripes[i];

		wait_event(stripe->repair_wait,
			   test_bit(SCRUB_STRIPE_FLAG_REPAIR_DONE, &stripe->state));
	}
	/* For now, no zoned support for RAID56. */
	ASSERT(!btrfs_is_zoned(sctx->fs_info));

	/*
	 * Now all data stripes are properly verified. Check if we have any
	 * unrepaired, if so abort immediately or we could further corrupt the
	 * P/Q stripes.
	 *
	 * During the loop, also populate extent_bitmap.
	 */
	for (int i = 0; i < data_stripes; i++) {
		unsigned long error;
		unsigned long has_extent;

		stripe = &sctx->raid56_data_stripes[i];

		error = scrub_bitmap_read_error(stripe);
		has_extent = scrub_bitmap_read_has_extent(stripe);

		/*
		 * We should only check the errors where there is an extent.
		 * As we may hit an empty data stripe while it's missing.
		 */
		bitmap_and(&error, &error, &has_extent, stripe->nr_sectors);
		if (!bitmap_empty(&error, stripe->nr_sectors)) {
			btrfs_err(fs_info,
"scrub: unrepaired sectors detected, full stripe %llu data stripe %u errors %*pbl",
				  full_stripe_start, i, stripe->nr_sectors,
				  &error);
			ret = -EIO;
			goto out;
		}
		bitmap_or(&extent_bitmap, &extent_bitmap, &has_extent,
			  stripe->nr_sectors);
	}

	/* Now we can check and regenerate the P/Q stripe. */
	bio = bio_alloc(NULL, 1, REQ_OP_READ, GFP_NOFS);
	bio->bi_iter.bi_sector = full_stripe_start >> SECTOR_SHIFT;
	bio->bi_private = &io_done;
	bio->bi_end_io = raid56_scrub_wait_endio;

	btrfs_bio_counter_inc_blocked(fs_info);
	ret = btrfs_map_block(fs_info, BTRFS_MAP_WRITE, full_stripe_start,
			      &length, &bioc, NULL, NULL);
	if (ret < 0) {
		btrfs_put_bioc(bioc);
		btrfs_bio_counter_dec(fs_info);
		goto out;
	}
	rbio = raid56_parity_alloc_scrub_rbio(bio, bioc, scrub_dev, &extent_bitmap,
				BTRFS_STRIPE_LEN >> fs_info->sectorsize_bits);
	btrfs_put_bioc(bioc);
	if (!rbio) {
		ret = -ENOMEM;
		btrfs_bio_counter_dec(fs_info);
		goto out;
	}
	/* Use the recovered stripes as cache to avoid read them from disk again. */
	for (int i = 0; i < data_stripes; i++) {
		stripe = &sctx->raid56_data_stripes[i];

		raid56_parity_cache_data_pages(rbio, stripe->pages,
				full_stripe_start + (i << BTRFS_STRIPE_LEN_SHIFT));
	}
	raid56_parity_submit_scrub_rbio(rbio);
	wait_for_completion_io(&io_done);
	ret = blk_status_to_errno(bio->bi_status);
	bio_put(bio);
	btrfs_bio_counter_dec(fs_info);

	btrfs_release_path(&extent_path);
	btrfs_release_path(&csum_path);
out:
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
			       struct btrfs_block_group *bg,
			       u64 logical_start, u64 logical_length,
			       struct btrfs_device *device,
			       u64 physical, int mirror_num)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	const u64 logical_end = logical_start + logical_length;
	u64 cur_logical = logical_start;
	int ret = 0;

	/* The range must be inside the bg */
	ASSERT(logical_start >= bg->start && logical_end <= bg->start + bg->length);

	/* Go through each extent items inside the logical range */
	while (cur_logical < logical_end) {
		u64 found_logical = U64_MAX;
		u64 cur_physical = physical + cur_logical - logical_start;

		/* Canceled? */
		if (atomic_read(&fs_info->scrub_cancel_req) ||
		    atomic_read(&sctx->cancel_req)) {
			ret = -ECANCELED;
			break;
		}
		/* Paused? */
		if (atomic_read(&fs_info->scrub_pause_req)) {
			/* Push queued extents */
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

		ret = queue_scrub_stripe(sctx, bg, device, mirror_num,
					 cur_logical, logical_end - cur_logical,
					 cur_physical, &found_logical);
		if (ret > 0) {
			/* No more extent, just update the accounting */
			spin_lock(&sctx->stat_lock);
			sctx->stat.last_physical = physical + logical_length;
			spin_unlock(&sctx->stat_lock);
			ret = 0;
			break;
		}
		if (ret < 0)
			break;

		/* queue_scrub_stripe() returned 0, @found_logical must be updated. */
		ASSERT(found_logical != U64_MAX);
		cur_logical = found_logical + BTRFS_STRIPE_LEN;

		/* Don't hold CPU for too long time */
		cond_resched();
	}
	return ret;
}

/* Calculate the full stripe length for simple stripe based profiles */
static u64 simple_stripe_full_stripe_len(const struct btrfs_chunk_map *map)
{
	ASSERT(map->type & (BTRFS_BLOCK_GROUP_RAID0 |
			    BTRFS_BLOCK_GROUP_RAID10));

	return btrfs_stripe_nr_to_offset(map->num_stripes / map->sub_stripes);
}

/* Get the logical bytenr for the stripe */
static u64 simple_stripe_get_logical(struct btrfs_chunk_map *map,
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
	return btrfs_stripe_nr_to_offset(stripe_index / map->sub_stripes) +
	       bg->start;
}

/* Get the mirror number for the stripe */
static int simple_stripe_mirror_num(struct btrfs_chunk_map *map, int stripe_index)
{
	ASSERT(map->type & (BTRFS_BLOCK_GROUP_RAID0 |
			    BTRFS_BLOCK_GROUP_RAID10));
	ASSERT(stripe_index < map->num_stripes);

	/* For RAID0, it's fixed to 1, for RAID10 it's 0,1,0,1... */
	return stripe_index % map->sub_stripes + 1;
}

static int scrub_simple_stripe(struct scrub_ctx *sctx,
			       struct btrfs_block_group *bg,
			       struct btrfs_chunk_map *map,
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
		ret = scrub_simple_mirror(sctx, bg, cur_logical,
					  BTRFS_STRIPE_LEN, device, cur_physical,
					  mirror_num);
		if (ret)
			return ret;
		/* Skip to next stripe which belongs to the target device */
		cur_logical += logical_increment;
		/* For physical offset, we just go to next stripe */
		cur_physical += BTRFS_STRIPE_LEN;
	}
	return ret;
}

static noinline_for_stack int scrub_stripe(struct scrub_ctx *sctx,
					   struct btrfs_block_group *bg,
					   struct btrfs_chunk_map *map,
					   struct btrfs_device *scrub_dev,
					   int stripe_index)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	const u64 profile = map->type & BTRFS_BLOCK_GROUP_PROFILE_MASK;
	const u64 chunk_logical = bg->start;
	int ret;
	int ret2;
	u64 physical = map->stripes[stripe_index].physical;
	const u64 dev_stripe_len = btrfs_calc_stripe_length(map);
	const u64 physical_end = physical + dev_stripe_len;
	u64 logical;
	u64 logic_end;
	/* The logical increment after finishing one stripe */
	u64 increment;
	/* Offset inside the chunk */
	u64 offset;
	u64 stripe_logical;

	/* Extent_path should be released by now. */
	ASSERT(sctx->extent_path.nodes[0] == NULL);

	scrub_blocked_if_needed(fs_info);

	if (sctx->is_dev_replace &&
	    btrfs_dev_is_sequential(sctx->wr_tgtdev, physical)) {
		mutex_lock(&sctx->wr_lock);
		sctx->write_pointer = physical;
		mutex_unlock(&sctx->wr_lock);
	}

	/* Prepare the extra data stripes used by RAID56. */
	if (profile & BTRFS_BLOCK_GROUP_RAID56_MASK) {
		ASSERT(sctx->raid56_data_stripes == NULL);

		sctx->raid56_data_stripes = kcalloc(nr_data_stripes(map),
						    sizeof(struct scrub_stripe),
						    GFP_KERNEL);
		if (!sctx->raid56_data_stripes) {
			ret = -ENOMEM;
			goto out;
		}
		for (int i = 0; i < nr_data_stripes(map); i++) {
			ret = init_scrub_stripe(fs_info,
						&sctx->raid56_data_stripes[i]);
			if (ret < 0)
				goto out;
			sctx->raid56_data_stripes[i].bg = bg;
			sctx->raid56_data_stripes[i].sctx = sctx;
		}
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
		ret = scrub_simple_mirror(sctx, bg, bg->start, bg->length,
				scrub_dev, map->stripes[stripe_index].physical,
				stripe_index + 1);
		offset = 0;
		goto out;
	}
	if (profile & (BTRFS_BLOCK_GROUP_RAID0 | BTRFS_BLOCK_GROUP_RAID10)) {
		ret = scrub_simple_stripe(sctx, bg, map, scrub_dev, stripe_index);
		offset = btrfs_stripe_nr_to_offset(stripe_index / map->sub_stripes);
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
	increment = btrfs_stripe_nr_to_offset(nr_data_stripes(map));

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
			ret = scrub_raid56_parity_stripe(sctx, scrub_dev, bg,
							 map, stripe_logical);
			spin_lock(&sctx->stat_lock);
			sctx->stat.last_physical = min(physical + BTRFS_STRIPE_LEN,
						       physical_end);
			spin_unlock(&sctx->stat_lock);
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
		ret = scrub_simple_mirror(sctx, bg, logical, BTRFS_STRIPE_LEN,
					  scrub_dev, physical, 1);
		if (ret < 0)
			goto out;
next:
		logical += increment;
		physical += BTRFS_STRIPE_LEN;
		spin_lock(&sctx->stat_lock);
		sctx->stat.last_physical = physical;
		spin_unlock(&sctx->stat_lock);
	}
out:
	ret2 = flush_scrub_stripes(sctx);
	if (!ret)
		ret = ret2;
	btrfs_release_path(&sctx->extent_path);
	btrfs_release_path(&sctx->csum_path);

	if (sctx->raid56_data_stripes) {
		for (int i = 0; i < nr_data_stripes(map); i++)
			release_scrub_stripe(&sctx->raid56_data_stripes[i]);
		kfree(sctx->raid56_data_stripes);
		sctx->raid56_data_stripes = NULL;
	}

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
	struct btrfs_chunk_map *map;
	int i;
	int ret = 0;

	map = btrfs_find_chunk_map(fs_info, bg->start, bg->length);
	if (!map) {
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
	if (map->start != bg->start)
		goto out;
	if (map->chunk_len < dev_extent_len)
		goto out;

	for (i = 0; i < map->num_stripes; ++i) {
		if (map->stripes[i].dev->bdev == scrub_dev->bdev &&
		    map->stripes[i].physical == dev_offset) {
			ret = scrub_stripe(sctx, bg, map, scrub_dev, i);
			if (ret)
				goto out;
		}
	}
out:
	btrfs_free_chunk_map(map);

	return ret;
}

static int finish_extent_writes_for_zoned(struct btrfs_root *root,
					  struct btrfs_block_group *cache)
{
	struct btrfs_fs_info *fs_info = cache->fs_info;

	if (!btrfs_is_zoned(fs_info))
		return 0;

	btrfs_wait_block_group_reservations(cache);
	btrfs_wait_nocow_writers(cache);
	btrfs_wait_ordered_roots(fs_info, U64_MAX, cache);

	return btrfs_commit_current_transaction(root);
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
	key.type = BTRFS_DEV_EXTENT_KEY;
	key.offset = 0ull;

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
		} else if (ret == -ENOSPC && !sctx->is_dev_replace &&
			   !(cache->flags & BTRFS_BLOCK_GROUP_RAID56_MASK)) {
			/*
			 * btrfs_inc_block_group_ro return -ENOSPC when it
			 * failed in creating new chunk for metadata.
			 * It is not a problem for scrub, because
			 * metadata are always cowed, and our scrub paused
			 * commit_transactions.
			 *
			 * For RAID56 chunks, we have to mark them read-only
			 * for scrub, as later we would use our own cache
			 * out of RAID56 realm.
			 * Thus we want the RAID56 bg to be marked RO to
			 * prevent RMW from screwing up out cache.
			 */
			ro_set = 0;
		} else if (ret == -ETXTBSY) {
			btrfs_warn(fs_info,
	     "scrub: skipping scrub of block group %llu due to active swapfile",
				   cache->start);
			scrub_pause_off(fs_info);
			ret = 0;
			goto skip_unfreeze;
		} else {
			btrfs_warn(fs_info, "scrub: failed setting block group ro: %d",
				   ret);
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
			btrfs_wait_ordered_roots(fs_info, U64_MAX, cache);
		}

		scrub_pause_off(fs_info);
		down_write(&dev_replace->rwsem);
		dev_replace->cursor_right = found_key.offset + dev_extent_len;
		dev_replace->cursor_left = found_key.offset;
		dev_replace->item_needs_writeback = 1;
		up_write(&dev_replace->rwsem);

		ret = scrub_chunk(sctx, cache, scrub_dev, found_key.offset,
				  dev_extent_len);
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

static int scrub_one_super(struct scrub_ctx *sctx, struct btrfs_device *dev,
			   struct page *page, u64 physical, u64 generation)
{
	struct btrfs_fs_info *fs_info = sctx->fs_info;
	struct btrfs_super_block *sb = page_address(page);
	int ret;

	ret = bdev_rw_virt(dev->bdev, physical >> SECTOR_SHIFT, sb,
			BTRFS_SUPER_INFO_SIZE, REQ_OP_READ);
	if (ret < 0)
		return ret;
	ret = btrfs_check_super_csum(fs_info, sb);
	if (ret != 0) {
		btrfs_err_rl(fs_info,
		  "scrub: super block at physical %llu devid %llu has bad csum",
			physical, dev->devid);
		return -EIO;
	}
	if (btrfs_super_generation(sb) != generation) {
		btrfs_err_rl(fs_info,
"scrub: super block at physical %llu devid %llu has bad generation %llu expect %llu",
			     physical, dev->devid,
			     btrfs_super_generation(sb), generation);
		return -EUCLEAN;
	}

	return btrfs_validate_super(fs_info, sb, -1);
}

static noinline_for_stack int scrub_supers(struct scrub_ctx *sctx,
					   struct btrfs_device *scrub_dev)
{
	int	i;
	u64	bytenr;
	u64	gen;
	int ret = 0;
	struct page *page;
	struct btrfs_fs_info *fs_info = sctx->fs_info;

	if (BTRFS_FS_ERROR(fs_info))
		return -EROFS;

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		spin_lock(&sctx->stat_lock);
		sctx->stat.malloc_errors++;
		spin_unlock(&sctx->stat_lock);
		return -ENOMEM;
	}

	/* Seed devices of a new filesystem has their own generation. */
	if (scrub_dev->fs_devices != fs_info->fs_devices)
		gen = scrub_dev->generation;
	else
		gen = btrfs_get_last_trans_committed(fs_info);

	for (i = 0; i < BTRFS_SUPER_MIRROR_MAX; i++) {
		ret = btrfs_sb_log_location(scrub_dev, i, 0, &bytenr);
		if (ret == -ENOENT)
			break;

		if (ret) {
			spin_lock(&sctx->stat_lock);
			sctx->stat.super_errors++;
			spin_unlock(&sctx->stat_lock);
			continue;
		}

		if (bytenr + BTRFS_SUPER_INFO_SIZE >
		    scrub_dev->commit_total_bytes)
			break;
		if (!btrfs_check_super_location(scrub_dev, bytenr))
			continue;

		ret = scrub_one_super(sctx, scrub_dev, page, bytenr, gen);
		if (ret) {
			spin_lock(&sctx->stat_lock);
			sctx->stat.super_errors++;
			spin_unlock(&sctx->stat_lock);
		}
	}
	__free_page(page);
	return 0;
}

static void scrub_workers_put(struct btrfs_fs_info *fs_info)
{
	if (refcount_dec_and_mutex_lock(&fs_info->scrub_workers_refcnt,
					&fs_info->scrub_lock)) {
		struct workqueue_struct *scrub_workers = fs_info->scrub_workers;

		fs_info->scrub_workers = NULL;
		mutex_unlock(&fs_info->scrub_lock);

		if (scrub_workers)
			destroy_workqueue(scrub_workers);
	}
}

/*
 * get a reference count on fs_info->scrub_workers. start worker if necessary
 */
static noinline_for_stack int scrub_workers_get(struct btrfs_fs_info *fs_info)
{
	struct workqueue_struct *scrub_workers = NULL;
	unsigned int flags = WQ_FREEZABLE | WQ_UNBOUND;
	int max_active = fs_info->thread_pool_size;
	int ret = -ENOMEM;

	if (refcount_inc_not_zero(&fs_info->scrub_workers_refcnt))
		return 0;

	scrub_workers = alloc_workqueue("btrfs-scrub", flags, max_active);
	if (!scrub_workers)
		return -ENOMEM;

	mutex_lock(&fs_info->scrub_lock);
	if (refcount_read(&fs_info->scrub_workers_refcnt) == 0) {
		ASSERT(fs_info->scrub_workers == NULL);
		fs_info->scrub_workers = scrub_workers;
		refcount_set(&fs_info->scrub_workers_refcnt, 1);
		mutex_unlock(&fs_info->scrub_lock);
		return 0;
	}
	/* Other thread raced in and created the workers for us */
	refcount_inc(&fs_info->scrub_workers_refcnt);
	mutex_unlock(&fs_info->scrub_lock);

	ret = 0;

	destroy_workqueue(scrub_workers);
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

	ret = scrub_workers_get(fs_info);
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
			"scrub: devid %llu: filesystem on %s is not writable",
				 devid, btrfs_dev_name(dev));
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

	atomic_dec(&fs_info->scrubs_running);
	wake_up(&fs_info->scrub_pause_wait);

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
