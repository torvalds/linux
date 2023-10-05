// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2012 Fusion-io  All rights reserved.
 * Copyright (C) 2012 Intel Corp. All rights reserved.
 */

#include <linux/sched.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/raid/pq.h>
#include <linux/hash.h>
#include <linux/list_sort.h>
#include <linux/raid/xor.h>
#include <linux/mm.h>
#include "messages.h"
#include "misc.h"
#include "ctree.h"
#include "disk-io.h"
#include "volumes.h"
#include "raid56.h"
#include "async-thread.h"
#include "file-item.h"
#include "btrfs_inode.h"

/* set when additional merges to this rbio are not allowed */
#define RBIO_RMW_LOCKED_BIT	1

/*
 * set when this rbio is sitting in the hash, but it is just a cache
 * of past RMW
 */
#define RBIO_CACHE_BIT		2

/*
 * set when it is safe to trust the stripe_pages for caching
 */
#define RBIO_CACHE_READY_BIT	3

#define RBIO_CACHE_SIZE 1024

#define BTRFS_STRIPE_HASH_TABLE_BITS				11

/* Used by the raid56 code to lock stripes for read/modify/write */
struct btrfs_stripe_hash {
	struct list_head hash_list;
	spinlock_t lock;
};

/* Used by the raid56 code to lock stripes for read/modify/write */
struct btrfs_stripe_hash_table {
	struct list_head stripe_cache;
	spinlock_t cache_lock;
	int cache_size;
	struct btrfs_stripe_hash table[];
};

/*
 * A bvec like structure to present a sector inside a page.
 *
 * Unlike bvec we don't need bvlen, as it's fixed to sectorsize.
 */
struct sector_ptr {
	struct page *page;
	unsigned int pgoff:24;
	unsigned int uptodate:8;
};

static void rmw_rbio_work(struct work_struct *work);
static void rmw_rbio_work_locked(struct work_struct *work);
static void index_rbio_pages(struct btrfs_raid_bio *rbio);
static int alloc_rbio_pages(struct btrfs_raid_bio *rbio);

static int finish_parity_scrub(struct btrfs_raid_bio *rbio);
static void scrub_rbio_work_locked(struct work_struct *work);

static void free_raid_bio_pointers(struct btrfs_raid_bio *rbio)
{
	bitmap_free(rbio->error_bitmap);
	kfree(rbio->stripe_pages);
	kfree(rbio->bio_sectors);
	kfree(rbio->stripe_sectors);
	kfree(rbio->finish_pointers);
}

static void free_raid_bio(struct btrfs_raid_bio *rbio)
{
	int i;

	if (!refcount_dec_and_test(&rbio->refs))
		return;

	WARN_ON(!list_empty(&rbio->stripe_cache));
	WARN_ON(!list_empty(&rbio->hash_list));
	WARN_ON(!bio_list_empty(&rbio->bio_list));

	for (i = 0; i < rbio->nr_pages; i++) {
		if (rbio->stripe_pages[i]) {
			__free_page(rbio->stripe_pages[i]);
			rbio->stripe_pages[i] = NULL;
		}
	}

	btrfs_put_bioc(rbio->bioc);
	free_raid_bio_pointers(rbio);
	kfree(rbio);
}

static void start_async_work(struct btrfs_raid_bio *rbio, work_func_t work_func)
{
	INIT_WORK(&rbio->work, work_func);
	queue_work(rbio->bioc->fs_info->rmw_workers, &rbio->work);
}

/*
 * the stripe hash table is used for locking, and to collect
 * bios in hopes of making a full stripe
 */
int btrfs_alloc_stripe_hash_table(struct btrfs_fs_info *info)
{
	struct btrfs_stripe_hash_table *table;
	struct btrfs_stripe_hash_table *x;
	struct btrfs_stripe_hash *cur;
	struct btrfs_stripe_hash *h;
	int num_entries = 1 << BTRFS_STRIPE_HASH_TABLE_BITS;
	int i;

	if (info->stripe_hash_table)
		return 0;

	/*
	 * The table is large, starting with order 4 and can go as high as
	 * order 7 in case lock debugging is turned on.
	 *
	 * Try harder to allocate and fallback to vmalloc to lower the chance
	 * of a failing mount.
	 */
	table = kvzalloc(struct_size(table, table, num_entries), GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	spin_lock_init(&table->cache_lock);
	INIT_LIST_HEAD(&table->stripe_cache);

	h = table->table;

	for (i = 0; i < num_entries; i++) {
		cur = h + i;
		INIT_LIST_HEAD(&cur->hash_list);
		spin_lock_init(&cur->lock);
	}

	x = cmpxchg(&info->stripe_hash_table, NULL, table);
	kvfree(x);
	return 0;
}

/*
 * caching an rbio means to copy anything from the
 * bio_sectors array into the stripe_pages array.  We
 * use the page uptodate bit in the stripe cache array
 * to indicate if it has valid data
 *
 * once the caching is done, we set the cache ready
 * bit.
 */
static void cache_rbio_pages(struct btrfs_raid_bio *rbio)
{
	int i;
	int ret;

	ret = alloc_rbio_pages(rbio);
	if (ret)
		return;

	for (i = 0; i < rbio->nr_sectors; i++) {
		/* Some range not covered by bio (partial write), skip it */
		if (!rbio->bio_sectors[i].page) {
			/*
			 * Even if the sector is not covered by bio, if it is
			 * a data sector it should still be uptodate as it is
			 * read from disk.
			 */
			if (i < rbio->nr_data * rbio->stripe_nsectors)
				ASSERT(rbio->stripe_sectors[i].uptodate);
			continue;
		}

		ASSERT(rbio->stripe_sectors[i].page);
		memcpy_page(rbio->stripe_sectors[i].page,
			    rbio->stripe_sectors[i].pgoff,
			    rbio->bio_sectors[i].page,
			    rbio->bio_sectors[i].pgoff,
			    rbio->bioc->fs_info->sectorsize);
		rbio->stripe_sectors[i].uptodate = 1;
	}
	set_bit(RBIO_CACHE_READY_BIT, &rbio->flags);
}

/*
 * we hash on the first logical address of the stripe
 */
static int rbio_bucket(struct btrfs_raid_bio *rbio)
{
	u64 num = rbio->bioc->full_stripe_logical;

	/*
	 * we shift down quite a bit.  We're using byte
	 * addressing, and most of the lower bits are zeros.
	 * This tends to upset hash_64, and it consistently
	 * returns just one or two different values.
	 *
	 * shifting off the lower bits fixes things.
	 */
	return hash_64(num >> 16, BTRFS_STRIPE_HASH_TABLE_BITS);
}

static bool full_page_sectors_uptodate(struct btrfs_raid_bio *rbio,
				       unsigned int page_nr)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	const u32 sectors_per_page = PAGE_SIZE / sectorsize;
	int i;

	ASSERT(page_nr < rbio->nr_pages);

	for (i = sectors_per_page * page_nr;
	     i < sectors_per_page * page_nr + sectors_per_page;
	     i++) {
		if (!rbio->stripe_sectors[i].uptodate)
			return false;
	}
	return true;
}

/*
 * Update the stripe_sectors[] array to use correct page and pgoff
 *
 * Should be called every time any page pointer in stripes_pages[] got modified.
 */
static void index_stripe_sectors(struct btrfs_raid_bio *rbio)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	u32 offset;
	int i;

	for (i = 0, offset = 0; i < rbio->nr_sectors; i++, offset += sectorsize) {
		int page_index = offset >> PAGE_SHIFT;

		ASSERT(page_index < rbio->nr_pages);
		rbio->stripe_sectors[i].page = rbio->stripe_pages[page_index];
		rbio->stripe_sectors[i].pgoff = offset_in_page(offset);
	}
}

static void steal_rbio_page(struct btrfs_raid_bio *src,
			    struct btrfs_raid_bio *dest, int page_nr)
{
	const u32 sectorsize = src->bioc->fs_info->sectorsize;
	const u32 sectors_per_page = PAGE_SIZE / sectorsize;
	int i;

	if (dest->stripe_pages[page_nr])
		__free_page(dest->stripe_pages[page_nr]);
	dest->stripe_pages[page_nr] = src->stripe_pages[page_nr];
	src->stripe_pages[page_nr] = NULL;

	/* Also update the sector->uptodate bits. */
	for (i = sectors_per_page * page_nr;
	     i < sectors_per_page * page_nr + sectors_per_page; i++)
		dest->stripe_sectors[i].uptodate = true;
}

static bool is_data_stripe_page(struct btrfs_raid_bio *rbio, int page_nr)
{
	const int sector_nr = (page_nr << PAGE_SHIFT) >>
			      rbio->bioc->fs_info->sectorsize_bits;

	/*
	 * We have ensured PAGE_SIZE is aligned with sectorsize, thus
	 * we won't have a page which is half data half parity.
	 *
	 * Thus if the first sector of the page belongs to data stripes, then
	 * the full page belongs to data stripes.
	 */
	return (sector_nr < rbio->nr_data * rbio->stripe_nsectors);
}

/*
 * Stealing an rbio means taking all the uptodate pages from the stripe array
 * in the source rbio and putting them into the destination rbio.
 *
 * This will also update the involved stripe_sectors[] which are referring to
 * the old pages.
 */
static void steal_rbio(struct btrfs_raid_bio *src, struct btrfs_raid_bio *dest)
{
	int i;

	if (!test_bit(RBIO_CACHE_READY_BIT, &src->flags))
		return;

	for (i = 0; i < dest->nr_pages; i++) {
		struct page *p = src->stripe_pages[i];

		/*
		 * We don't need to steal P/Q pages as they will always be
		 * regenerated for RMW or full write anyway.
		 */
		if (!is_data_stripe_page(src, i))
			continue;

		/*
		 * If @src already has RBIO_CACHE_READY_BIT, it should have
		 * all data stripe pages present and uptodate.
		 */
		ASSERT(p);
		ASSERT(full_page_sectors_uptodate(src, i));
		steal_rbio_page(src, dest, i);
	}
	index_stripe_sectors(dest);
	index_stripe_sectors(src);
}

/*
 * merging means we take the bio_list from the victim and
 * splice it into the destination.  The victim should
 * be discarded afterwards.
 *
 * must be called with dest->rbio_list_lock held
 */
static void merge_rbio(struct btrfs_raid_bio *dest,
		       struct btrfs_raid_bio *victim)
{
	bio_list_merge(&dest->bio_list, &victim->bio_list);
	dest->bio_list_bytes += victim->bio_list_bytes;
	/* Also inherit the bitmaps from @victim. */
	bitmap_or(&dest->dbitmap, &victim->dbitmap, &dest->dbitmap,
		  dest->stripe_nsectors);
	bio_list_init(&victim->bio_list);
}

/*
 * used to prune items that are in the cache.  The caller
 * must hold the hash table lock.
 */
static void __remove_rbio_from_cache(struct btrfs_raid_bio *rbio)
{
	int bucket = rbio_bucket(rbio);
	struct btrfs_stripe_hash_table *table;
	struct btrfs_stripe_hash *h;
	int freeit = 0;

	/*
	 * check the bit again under the hash table lock.
	 */
	if (!test_bit(RBIO_CACHE_BIT, &rbio->flags))
		return;

	table = rbio->bioc->fs_info->stripe_hash_table;
	h = table->table + bucket;

	/* hold the lock for the bucket because we may be
	 * removing it from the hash table
	 */
	spin_lock(&h->lock);

	/*
	 * hold the lock for the bio list because we need
	 * to make sure the bio list is empty
	 */
	spin_lock(&rbio->bio_list_lock);

	if (test_and_clear_bit(RBIO_CACHE_BIT, &rbio->flags)) {
		list_del_init(&rbio->stripe_cache);
		table->cache_size -= 1;
		freeit = 1;

		/* if the bio list isn't empty, this rbio is
		 * still involved in an IO.  We take it out
		 * of the cache list, and drop the ref that
		 * was held for the list.
		 *
		 * If the bio_list was empty, we also remove
		 * the rbio from the hash_table, and drop
		 * the corresponding ref
		 */
		if (bio_list_empty(&rbio->bio_list)) {
			if (!list_empty(&rbio->hash_list)) {
				list_del_init(&rbio->hash_list);
				refcount_dec(&rbio->refs);
				BUG_ON(!list_empty(&rbio->plug_list));
			}
		}
	}

	spin_unlock(&rbio->bio_list_lock);
	spin_unlock(&h->lock);

	if (freeit)
		free_raid_bio(rbio);
}

/*
 * prune a given rbio from the cache
 */
static void remove_rbio_from_cache(struct btrfs_raid_bio *rbio)
{
	struct btrfs_stripe_hash_table *table;

	if (!test_bit(RBIO_CACHE_BIT, &rbio->flags))
		return;

	table = rbio->bioc->fs_info->stripe_hash_table;

	spin_lock(&table->cache_lock);
	__remove_rbio_from_cache(rbio);
	spin_unlock(&table->cache_lock);
}

/*
 * remove everything in the cache
 */
static void btrfs_clear_rbio_cache(struct btrfs_fs_info *info)
{
	struct btrfs_stripe_hash_table *table;
	struct btrfs_raid_bio *rbio;

	table = info->stripe_hash_table;

	spin_lock(&table->cache_lock);
	while (!list_empty(&table->stripe_cache)) {
		rbio = list_entry(table->stripe_cache.next,
				  struct btrfs_raid_bio,
				  stripe_cache);
		__remove_rbio_from_cache(rbio);
	}
	spin_unlock(&table->cache_lock);
}

/*
 * remove all cached entries and free the hash table
 * used by unmount
 */
void btrfs_free_stripe_hash_table(struct btrfs_fs_info *info)
{
	if (!info->stripe_hash_table)
		return;
	btrfs_clear_rbio_cache(info);
	kvfree(info->stripe_hash_table);
	info->stripe_hash_table = NULL;
}

/*
 * insert an rbio into the stripe cache.  It
 * must have already been prepared by calling
 * cache_rbio_pages
 *
 * If this rbio was already cached, it gets
 * moved to the front of the lru.
 *
 * If the size of the rbio cache is too big, we
 * prune an item.
 */
static void cache_rbio(struct btrfs_raid_bio *rbio)
{
	struct btrfs_stripe_hash_table *table;

	if (!test_bit(RBIO_CACHE_READY_BIT, &rbio->flags))
		return;

	table = rbio->bioc->fs_info->stripe_hash_table;

	spin_lock(&table->cache_lock);
	spin_lock(&rbio->bio_list_lock);

	/* bump our ref if we were not in the list before */
	if (!test_and_set_bit(RBIO_CACHE_BIT, &rbio->flags))
		refcount_inc(&rbio->refs);

	if (!list_empty(&rbio->stripe_cache)){
		list_move(&rbio->stripe_cache, &table->stripe_cache);
	} else {
		list_add(&rbio->stripe_cache, &table->stripe_cache);
		table->cache_size += 1;
	}

	spin_unlock(&rbio->bio_list_lock);

	if (table->cache_size > RBIO_CACHE_SIZE) {
		struct btrfs_raid_bio *found;

		found = list_entry(table->stripe_cache.prev,
				  struct btrfs_raid_bio,
				  stripe_cache);

		if (found != rbio)
			__remove_rbio_from_cache(found);
	}

	spin_unlock(&table->cache_lock);
}

/*
 * helper function to run the xor_blocks api.  It is only
 * able to do MAX_XOR_BLOCKS at a time, so we need to
 * loop through.
 */
static void run_xor(void **pages, int src_cnt, ssize_t len)
{
	int src_off = 0;
	int xor_src_cnt = 0;
	void *dest = pages[src_cnt];

	while(src_cnt > 0) {
		xor_src_cnt = min(src_cnt, MAX_XOR_BLOCKS);
		xor_blocks(xor_src_cnt, len, dest, pages + src_off);

		src_cnt -= xor_src_cnt;
		src_off += xor_src_cnt;
	}
}

/*
 * Returns true if the bio list inside this rbio covers an entire stripe (no
 * rmw required).
 */
static int rbio_is_full(struct btrfs_raid_bio *rbio)
{
	unsigned long size = rbio->bio_list_bytes;
	int ret = 1;

	spin_lock(&rbio->bio_list_lock);
	if (size != rbio->nr_data * BTRFS_STRIPE_LEN)
		ret = 0;
	BUG_ON(size > rbio->nr_data * BTRFS_STRIPE_LEN);
	spin_unlock(&rbio->bio_list_lock);

	return ret;
}

/*
 * returns 1 if it is safe to merge two rbios together.
 * The merging is safe if the two rbios correspond to
 * the same stripe and if they are both going in the same
 * direction (read vs write), and if neither one is
 * locked for final IO
 *
 * The caller is responsible for locking such that
 * rmw_locked is safe to test
 */
static int rbio_can_merge(struct btrfs_raid_bio *last,
			  struct btrfs_raid_bio *cur)
{
	if (test_bit(RBIO_RMW_LOCKED_BIT, &last->flags) ||
	    test_bit(RBIO_RMW_LOCKED_BIT, &cur->flags))
		return 0;

	/*
	 * we can't merge with cached rbios, since the
	 * idea is that when we merge the destination
	 * rbio is going to run our IO for us.  We can
	 * steal from cached rbios though, other functions
	 * handle that.
	 */
	if (test_bit(RBIO_CACHE_BIT, &last->flags) ||
	    test_bit(RBIO_CACHE_BIT, &cur->flags))
		return 0;

	if (last->bioc->full_stripe_logical != cur->bioc->full_stripe_logical)
		return 0;

	/* we can't merge with different operations */
	if (last->operation != cur->operation)
		return 0;
	/*
	 * We've need read the full stripe from the drive.
	 * check and repair the parity and write the new results.
	 *
	 * We're not allowed to add any new bios to the
	 * bio list here, anyone else that wants to
	 * change this stripe needs to do their own rmw.
	 */
	if (last->operation == BTRFS_RBIO_PARITY_SCRUB)
		return 0;

	if (last->operation == BTRFS_RBIO_READ_REBUILD)
		return 0;

	return 1;
}

static unsigned int rbio_stripe_sector_index(const struct btrfs_raid_bio *rbio,
					     unsigned int stripe_nr,
					     unsigned int sector_nr)
{
	ASSERT(stripe_nr < rbio->real_stripes);
	ASSERT(sector_nr < rbio->stripe_nsectors);

	return stripe_nr * rbio->stripe_nsectors + sector_nr;
}

/* Return a sector from rbio->stripe_sectors, not from the bio list */
static struct sector_ptr *rbio_stripe_sector(const struct btrfs_raid_bio *rbio,
					     unsigned int stripe_nr,
					     unsigned int sector_nr)
{
	return &rbio->stripe_sectors[rbio_stripe_sector_index(rbio, stripe_nr,
							      sector_nr)];
}

/* Grab a sector inside P stripe */
static struct sector_ptr *rbio_pstripe_sector(const struct btrfs_raid_bio *rbio,
					      unsigned int sector_nr)
{
	return rbio_stripe_sector(rbio, rbio->nr_data, sector_nr);
}

/* Grab a sector inside Q stripe, return NULL if not RAID6 */
static struct sector_ptr *rbio_qstripe_sector(const struct btrfs_raid_bio *rbio,
					      unsigned int sector_nr)
{
	if (rbio->nr_data + 1 == rbio->real_stripes)
		return NULL;
	return rbio_stripe_sector(rbio, rbio->nr_data + 1, sector_nr);
}

/*
 * The first stripe in the table for a logical address
 * has the lock.  rbios are added in one of three ways:
 *
 * 1) Nobody has the stripe locked yet.  The rbio is given
 * the lock and 0 is returned.  The caller must start the IO
 * themselves.
 *
 * 2) Someone has the stripe locked, but we're able to merge
 * with the lock owner.  The rbio is freed and the IO will
 * start automatically along with the existing rbio.  1 is returned.
 *
 * 3) Someone has the stripe locked, but we're not able to merge.
 * The rbio is added to the lock owner's plug list, or merged into
 * an rbio already on the plug list.  When the lock owner unlocks,
 * the next rbio on the list is run and the IO is started automatically.
 * 1 is returned
 *
 * If we return 0, the caller still owns the rbio and must continue with
 * IO submission.  If we return 1, the caller must assume the rbio has
 * already been freed.
 */
static noinline int lock_stripe_add(struct btrfs_raid_bio *rbio)
{
	struct btrfs_stripe_hash *h;
	struct btrfs_raid_bio *cur;
	struct btrfs_raid_bio *pending;
	struct btrfs_raid_bio *freeit = NULL;
	struct btrfs_raid_bio *cache_drop = NULL;
	int ret = 0;

	h = rbio->bioc->fs_info->stripe_hash_table->table + rbio_bucket(rbio);

	spin_lock(&h->lock);
	list_for_each_entry(cur, &h->hash_list, hash_list) {
		if (cur->bioc->full_stripe_logical != rbio->bioc->full_stripe_logical)
			continue;

		spin_lock(&cur->bio_list_lock);

		/* Can we steal this cached rbio's pages? */
		if (bio_list_empty(&cur->bio_list) &&
		    list_empty(&cur->plug_list) &&
		    test_bit(RBIO_CACHE_BIT, &cur->flags) &&
		    !test_bit(RBIO_RMW_LOCKED_BIT, &cur->flags)) {
			list_del_init(&cur->hash_list);
			refcount_dec(&cur->refs);

			steal_rbio(cur, rbio);
			cache_drop = cur;
			spin_unlock(&cur->bio_list_lock);

			goto lockit;
		}

		/* Can we merge into the lock owner? */
		if (rbio_can_merge(cur, rbio)) {
			merge_rbio(cur, rbio);
			spin_unlock(&cur->bio_list_lock);
			freeit = rbio;
			ret = 1;
			goto out;
		}


		/*
		 * We couldn't merge with the running rbio, see if we can merge
		 * with the pending ones.  We don't have to check for rmw_locked
		 * because there is no way they are inside finish_rmw right now
		 */
		list_for_each_entry(pending, &cur->plug_list, plug_list) {
			if (rbio_can_merge(pending, rbio)) {
				merge_rbio(pending, rbio);
				spin_unlock(&cur->bio_list_lock);
				freeit = rbio;
				ret = 1;
				goto out;
			}
		}

		/*
		 * No merging, put us on the tail of the plug list, our rbio
		 * will be started with the currently running rbio unlocks
		 */
		list_add_tail(&rbio->plug_list, &cur->plug_list);
		spin_unlock(&cur->bio_list_lock);
		ret = 1;
		goto out;
	}
lockit:
	refcount_inc(&rbio->refs);
	list_add(&rbio->hash_list, &h->hash_list);
out:
	spin_unlock(&h->lock);
	if (cache_drop)
		remove_rbio_from_cache(cache_drop);
	if (freeit)
		free_raid_bio(freeit);
	return ret;
}

static void recover_rbio_work_locked(struct work_struct *work);

/*
 * called as rmw or parity rebuild is completed.  If the plug list has more
 * rbios waiting for this stripe, the next one on the list will be started
 */
static noinline void unlock_stripe(struct btrfs_raid_bio *rbio)
{
	int bucket;
	struct btrfs_stripe_hash *h;
	int keep_cache = 0;

	bucket = rbio_bucket(rbio);
	h = rbio->bioc->fs_info->stripe_hash_table->table + bucket;

	if (list_empty(&rbio->plug_list))
		cache_rbio(rbio);

	spin_lock(&h->lock);
	spin_lock(&rbio->bio_list_lock);

	if (!list_empty(&rbio->hash_list)) {
		/*
		 * if we're still cached and there is no other IO
		 * to perform, just leave this rbio here for others
		 * to steal from later
		 */
		if (list_empty(&rbio->plug_list) &&
		    test_bit(RBIO_CACHE_BIT, &rbio->flags)) {
			keep_cache = 1;
			clear_bit(RBIO_RMW_LOCKED_BIT, &rbio->flags);
			BUG_ON(!bio_list_empty(&rbio->bio_list));
			goto done;
		}

		list_del_init(&rbio->hash_list);
		refcount_dec(&rbio->refs);

		/*
		 * we use the plug list to hold all the rbios
		 * waiting for the chance to lock this stripe.
		 * hand the lock over to one of them.
		 */
		if (!list_empty(&rbio->plug_list)) {
			struct btrfs_raid_bio *next;
			struct list_head *head = rbio->plug_list.next;

			next = list_entry(head, struct btrfs_raid_bio,
					  plug_list);

			list_del_init(&rbio->plug_list);

			list_add(&next->hash_list, &h->hash_list);
			refcount_inc(&next->refs);
			spin_unlock(&rbio->bio_list_lock);
			spin_unlock(&h->lock);

			if (next->operation == BTRFS_RBIO_READ_REBUILD) {
				start_async_work(next, recover_rbio_work_locked);
			} else if (next->operation == BTRFS_RBIO_WRITE) {
				steal_rbio(rbio, next);
				start_async_work(next, rmw_rbio_work_locked);
			} else if (next->operation == BTRFS_RBIO_PARITY_SCRUB) {
				steal_rbio(rbio, next);
				start_async_work(next, scrub_rbio_work_locked);
			}

			goto done_nolock;
		}
	}
done:
	spin_unlock(&rbio->bio_list_lock);
	spin_unlock(&h->lock);

done_nolock:
	if (!keep_cache)
		remove_rbio_from_cache(rbio);
}

static void rbio_endio_bio_list(struct bio *cur, blk_status_t err)
{
	struct bio *next;

	while (cur) {
		next = cur->bi_next;
		cur->bi_next = NULL;
		cur->bi_status = err;
		bio_endio(cur);
		cur = next;
	}
}

/*
 * this frees the rbio and runs through all the bios in the
 * bio_list and calls end_io on them
 */
static void rbio_orig_end_io(struct btrfs_raid_bio *rbio, blk_status_t err)
{
	struct bio *cur = bio_list_get(&rbio->bio_list);
	struct bio *extra;

	kfree(rbio->csum_buf);
	bitmap_free(rbio->csum_bitmap);
	rbio->csum_buf = NULL;
	rbio->csum_bitmap = NULL;

	/*
	 * Clear the data bitmap, as the rbio may be cached for later usage.
	 * do this before before unlock_stripe() so there will be no new bio
	 * for this bio.
	 */
	bitmap_clear(&rbio->dbitmap, 0, rbio->stripe_nsectors);

	/*
	 * At this moment, rbio->bio_list is empty, however since rbio does not
	 * always have RBIO_RMW_LOCKED_BIT set and rbio is still linked on the
	 * hash list, rbio may be merged with others so that rbio->bio_list
	 * becomes non-empty.
	 * Once unlock_stripe() is done, rbio->bio_list will not be updated any
	 * more and we can call bio_endio() on all queued bios.
	 */
	unlock_stripe(rbio);
	extra = bio_list_get(&rbio->bio_list);
	free_raid_bio(rbio);

	rbio_endio_bio_list(cur, err);
	if (extra)
		rbio_endio_bio_list(extra, err);
}

/*
 * Get a sector pointer specified by its @stripe_nr and @sector_nr.
 *
 * @rbio:               The raid bio
 * @stripe_nr:          Stripe number, valid range [0, real_stripe)
 * @sector_nr:		Sector number inside the stripe,
 *			valid range [0, stripe_nsectors)
 * @bio_list_only:      Whether to use sectors inside the bio list only.
 *
 * The read/modify/write code wants to reuse the original bio page as much
 * as possible, and only use stripe_sectors as fallback.
 */
static struct sector_ptr *sector_in_rbio(struct btrfs_raid_bio *rbio,
					 int stripe_nr, int sector_nr,
					 bool bio_list_only)
{
	struct sector_ptr *sector;
	int index;

	ASSERT(stripe_nr >= 0 && stripe_nr < rbio->real_stripes);
	ASSERT(sector_nr >= 0 && sector_nr < rbio->stripe_nsectors);

	index = stripe_nr * rbio->stripe_nsectors + sector_nr;
	ASSERT(index >= 0 && index < rbio->nr_sectors);

	spin_lock(&rbio->bio_list_lock);
	sector = &rbio->bio_sectors[index];
	if (sector->page || bio_list_only) {
		/* Don't return sector without a valid page pointer */
		if (!sector->page)
			sector = NULL;
		spin_unlock(&rbio->bio_list_lock);
		return sector;
	}
	spin_unlock(&rbio->bio_list_lock);

	return &rbio->stripe_sectors[index];
}

/*
 * allocation and initial setup for the btrfs_raid_bio.  Not
 * this does not allocate any pages for rbio->pages.
 */
static struct btrfs_raid_bio *alloc_rbio(struct btrfs_fs_info *fs_info,
					 struct btrfs_io_context *bioc)
{
	const unsigned int real_stripes = bioc->num_stripes - bioc->replace_nr_stripes;
	const unsigned int stripe_npages = BTRFS_STRIPE_LEN >> PAGE_SHIFT;
	const unsigned int num_pages = stripe_npages * real_stripes;
	const unsigned int stripe_nsectors =
		BTRFS_STRIPE_LEN >> fs_info->sectorsize_bits;
	const unsigned int num_sectors = stripe_nsectors * real_stripes;
	struct btrfs_raid_bio *rbio;

	/* PAGE_SIZE must also be aligned to sectorsize for subpage support */
	ASSERT(IS_ALIGNED(PAGE_SIZE, fs_info->sectorsize));
	/*
	 * Our current stripe len should be fixed to 64k thus stripe_nsectors
	 * (at most 16) should be no larger than BITS_PER_LONG.
	 */
	ASSERT(stripe_nsectors <= BITS_PER_LONG);

	rbio = kzalloc(sizeof(*rbio), GFP_NOFS);
	if (!rbio)
		return ERR_PTR(-ENOMEM);
	rbio->stripe_pages = kcalloc(num_pages, sizeof(struct page *),
				     GFP_NOFS);
	rbio->bio_sectors = kcalloc(num_sectors, sizeof(struct sector_ptr),
				    GFP_NOFS);
	rbio->stripe_sectors = kcalloc(num_sectors, sizeof(struct sector_ptr),
				       GFP_NOFS);
	rbio->finish_pointers = kcalloc(real_stripes, sizeof(void *), GFP_NOFS);
	rbio->error_bitmap = bitmap_zalloc(num_sectors, GFP_NOFS);

	if (!rbio->stripe_pages || !rbio->bio_sectors || !rbio->stripe_sectors ||
	    !rbio->finish_pointers || !rbio->error_bitmap) {
		free_raid_bio_pointers(rbio);
		kfree(rbio);
		return ERR_PTR(-ENOMEM);
	}

	bio_list_init(&rbio->bio_list);
	init_waitqueue_head(&rbio->io_wait);
	INIT_LIST_HEAD(&rbio->plug_list);
	spin_lock_init(&rbio->bio_list_lock);
	INIT_LIST_HEAD(&rbio->stripe_cache);
	INIT_LIST_HEAD(&rbio->hash_list);
	btrfs_get_bioc(bioc);
	rbio->bioc = bioc;
	rbio->nr_pages = num_pages;
	rbio->nr_sectors = num_sectors;
	rbio->real_stripes = real_stripes;
	rbio->stripe_npages = stripe_npages;
	rbio->stripe_nsectors = stripe_nsectors;
	refcount_set(&rbio->refs, 1);
	atomic_set(&rbio->stripes_pending, 0);

	ASSERT(btrfs_nr_parity_stripes(bioc->map_type));
	rbio->nr_data = real_stripes - btrfs_nr_parity_stripes(bioc->map_type);

	return rbio;
}

/* allocate pages for all the stripes in the bio, including parity */
static int alloc_rbio_pages(struct btrfs_raid_bio *rbio)
{
	int ret;

	ret = btrfs_alloc_page_array(rbio->nr_pages, rbio->stripe_pages);
	if (ret < 0)
		return ret;
	/* Mapping all sectors */
	index_stripe_sectors(rbio);
	return 0;
}

/* only allocate pages for p/q stripes */
static int alloc_rbio_parity_pages(struct btrfs_raid_bio *rbio)
{
	const int data_pages = rbio->nr_data * rbio->stripe_npages;
	int ret;

	ret = btrfs_alloc_page_array(rbio->nr_pages - data_pages,
				     rbio->stripe_pages + data_pages);
	if (ret < 0)
		return ret;

	index_stripe_sectors(rbio);
	return 0;
}

/*
 * Return the total number of errors found in the vertical stripe of @sector_nr.
 *
 * @faila and @failb will also be updated to the first and second stripe
 * number of the errors.
 */
static int get_rbio_veritical_errors(struct btrfs_raid_bio *rbio, int sector_nr,
				     int *faila, int *failb)
{
	int stripe_nr;
	int found_errors = 0;

	if (faila || failb) {
		/*
		 * Both @faila and @failb should be valid pointers if any of
		 * them is specified.
		 */
		ASSERT(faila && failb);
		*faila = -1;
		*failb = -1;
	}

	for (stripe_nr = 0; stripe_nr < rbio->real_stripes; stripe_nr++) {
		int total_sector_nr = stripe_nr * rbio->stripe_nsectors + sector_nr;

		if (test_bit(total_sector_nr, rbio->error_bitmap)) {
			found_errors++;
			if (faila) {
				/* Update faila and failb. */
				if (*faila < 0)
					*faila = stripe_nr;
				else if (*failb < 0)
					*failb = stripe_nr;
			}
		}
	}
	return found_errors;
}

/*
 * Add a single sector @sector into our list of bios for IO.
 *
 * Return 0 if everything went well.
 * Return <0 for error.
 */
static int rbio_add_io_sector(struct btrfs_raid_bio *rbio,
			      struct bio_list *bio_list,
			      struct sector_ptr *sector,
			      unsigned int stripe_nr,
			      unsigned int sector_nr,
			      enum req_op op)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	struct bio *last = bio_list->tail;
	int ret;
	struct bio *bio;
	struct btrfs_io_stripe *stripe;
	u64 disk_start;

	/*
	 * Note: here stripe_nr has taken device replace into consideration,
	 * thus it can be larger than rbio->real_stripe.
	 * So here we check against bioc->num_stripes, not rbio->real_stripes.
	 */
	ASSERT(stripe_nr >= 0 && stripe_nr < rbio->bioc->num_stripes);
	ASSERT(sector_nr >= 0 && sector_nr < rbio->stripe_nsectors);
	ASSERT(sector->page);

	stripe = &rbio->bioc->stripes[stripe_nr];
	disk_start = stripe->physical + sector_nr * sectorsize;

	/* if the device is missing, just fail this stripe */
	if (!stripe->dev->bdev) {
		int found_errors;

		set_bit(stripe_nr * rbio->stripe_nsectors + sector_nr,
			rbio->error_bitmap);

		/* Check if we have reached tolerance early. */
		found_errors = get_rbio_veritical_errors(rbio, sector_nr,
							 NULL, NULL);
		if (found_errors > rbio->bioc->max_errors)
			return -EIO;
		return 0;
	}

	/* see if we can add this page onto our existing bio */
	if (last) {
		u64 last_end = last->bi_iter.bi_sector << SECTOR_SHIFT;
		last_end += last->bi_iter.bi_size;

		/*
		 * we can't merge these if they are from different
		 * devices or if they are not contiguous
		 */
		if (last_end == disk_start && !last->bi_status &&
		    last->bi_bdev == stripe->dev->bdev) {
			ret = bio_add_page(last, sector->page, sectorsize,
					   sector->pgoff);
			if (ret == sectorsize)
				return 0;
		}
	}

	/* put a new bio on the list */
	bio = bio_alloc(stripe->dev->bdev,
			max(BTRFS_STRIPE_LEN >> PAGE_SHIFT, 1),
			op, GFP_NOFS);
	bio->bi_iter.bi_sector = disk_start >> SECTOR_SHIFT;
	bio->bi_private = rbio;

	__bio_add_page(bio, sector->page, sectorsize, sector->pgoff);
	bio_list_add(bio_list, bio);
	return 0;
}

static void index_one_bio(struct btrfs_raid_bio *rbio, struct bio *bio)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	struct bio_vec bvec;
	struct bvec_iter iter;
	u32 offset = (bio->bi_iter.bi_sector << SECTOR_SHIFT) -
		     rbio->bioc->full_stripe_logical;

	bio_for_each_segment(bvec, bio, iter) {
		u32 bvec_offset;

		for (bvec_offset = 0; bvec_offset < bvec.bv_len;
		     bvec_offset += sectorsize, offset += sectorsize) {
			int index = offset / sectorsize;
			struct sector_ptr *sector = &rbio->bio_sectors[index];

			sector->page = bvec.bv_page;
			sector->pgoff = bvec.bv_offset + bvec_offset;
			ASSERT(sector->pgoff < PAGE_SIZE);
		}
	}
}

/*
 * helper function to walk our bio list and populate the bio_pages array with
 * the result.  This seems expensive, but it is faster than constantly
 * searching through the bio list as we setup the IO in finish_rmw or stripe
 * reconstruction.
 *
 * This must be called before you trust the answers from page_in_rbio
 */
static void index_rbio_pages(struct btrfs_raid_bio *rbio)
{
	struct bio *bio;

	spin_lock(&rbio->bio_list_lock);
	bio_list_for_each(bio, &rbio->bio_list)
		index_one_bio(rbio, bio);

	spin_unlock(&rbio->bio_list_lock);
}

static void bio_get_trace_info(struct btrfs_raid_bio *rbio, struct bio *bio,
			       struct raid56_bio_trace_info *trace_info)
{
	const struct btrfs_io_context *bioc = rbio->bioc;
	int i;

	ASSERT(bioc);

	/* We rely on bio->bi_bdev to find the stripe number. */
	if (!bio->bi_bdev)
		goto not_found;

	for (i = 0; i < bioc->num_stripes; i++) {
		if (bio->bi_bdev != bioc->stripes[i].dev->bdev)
			continue;
		trace_info->stripe_nr = i;
		trace_info->devid = bioc->stripes[i].dev->devid;
		trace_info->offset = (bio->bi_iter.bi_sector << SECTOR_SHIFT) -
				     bioc->stripes[i].physical;
		return;
	}

not_found:
	trace_info->devid = -1;
	trace_info->offset = -1;
	trace_info->stripe_nr = -1;
}

static inline void bio_list_put(struct bio_list *bio_list)
{
	struct bio *bio;

	while ((bio = bio_list_pop(bio_list)))
		bio_put(bio);
}

/* Generate PQ for one vertical stripe. */
static void generate_pq_vertical(struct btrfs_raid_bio *rbio, int sectornr)
{
	void **pointers = rbio->finish_pointers;
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	struct sector_ptr *sector;
	int stripe;
	const bool has_qstripe = rbio->bioc->map_type & BTRFS_BLOCK_GROUP_RAID6;

	/* First collect one sector from each data stripe */
	for (stripe = 0; stripe < rbio->nr_data; stripe++) {
		sector = sector_in_rbio(rbio, stripe, sectornr, 0);
		pointers[stripe] = kmap_local_page(sector->page) +
				   sector->pgoff;
	}

	/* Then add the parity stripe */
	sector = rbio_pstripe_sector(rbio, sectornr);
	sector->uptodate = 1;
	pointers[stripe++] = kmap_local_page(sector->page) + sector->pgoff;

	if (has_qstripe) {
		/*
		 * RAID6, add the qstripe and call the library function
		 * to fill in our p/q
		 */
		sector = rbio_qstripe_sector(rbio, sectornr);
		sector->uptodate = 1;
		pointers[stripe++] = kmap_local_page(sector->page) +
				     sector->pgoff;

		raid6_call.gen_syndrome(rbio->real_stripes, sectorsize,
					pointers);
	} else {
		/* raid5 */
		memcpy(pointers[rbio->nr_data], pointers[0], sectorsize);
		run_xor(pointers + 1, rbio->nr_data - 1, sectorsize);
	}
	for (stripe = stripe - 1; stripe >= 0; stripe--)
		kunmap_local(pointers[stripe]);
}

static int rmw_assemble_write_bios(struct btrfs_raid_bio *rbio,
				   struct bio_list *bio_list)
{
	/* The total sector number inside the full stripe. */
	int total_sector_nr;
	int sectornr;
	int stripe;
	int ret;

	ASSERT(bio_list_size(bio_list) == 0);

	/* We should have at least one data sector. */
	ASSERT(bitmap_weight(&rbio->dbitmap, rbio->stripe_nsectors));

	/*
	 * Reset errors, as we may have errors inherited from from degraded
	 * write.
	 */
	bitmap_clear(rbio->error_bitmap, 0, rbio->nr_sectors);

	/*
	 * Start assembly.  Make bios for everything from the higher layers (the
	 * bio_list in our rbio) and our P/Q.  Ignore everything else.
	 */
	for (total_sector_nr = 0; total_sector_nr < rbio->nr_sectors;
	     total_sector_nr++) {
		struct sector_ptr *sector;

		stripe = total_sector_nr / rbio->stripe_nsectors;
		sectornr = total_sector_nr % rbio->stripe_nsectors;

		/* This vertical stripe has no data, skip it. */
		if (!test_bit(sectornr, &rbio->dbitmap))
			continue;

		if (stripe < rbio->nr_data) {
			sector = sector_in_rbio(rbio, stripe, sectornr, 1);
			if (!sector)
				continue;
		} else {
			sector = rbio_stripe_sector(rbio, stripe, sectornr);
		}

		ret = rbio_add_io_sector(rbio, bio_list, sector, stripe,
					 sectornr, REQ_OP_WRITE);
		if (ret)
			goto error;
	}

	if (likely(!rbio->bioc->replace_nr_stripes))
		return 0;

	/*
	 * Make a copy for the replace target device.
	 *
	 * Thus the source stripe number (in replace_stripe_src) should be valid.
	 */
	ASSERT(rbio->bioc->replace_stripe_src >= 0);

	for (total_sector_nr = 0; total_sector_nr < rbio->nr_sectors;
	     total_sector_nr++) {
		struct sector_ptr *sector;

		stripe = total_sector_nr / rbio->stripe_nsectors;
		sectornr = total_sector_nr % rbio->stripe_nsectors;

		/*
		 * For RAID56, there is only one device that can be replaced,
		 * and replace_stripe_src[0] indicates the stripe number we
		 * need to copy from.
		 */
		if (stripe != rbio->bioc->replace_stripe_src) {
			/*
			 * We can skip the whole stripe completely, note
			 * total_sector_nr will be increased by one anyway.
			 */
			ASSERT(sectornr == 0);
			total_sector_nr += rbio->stripe_nsectors - 1;
			continue;
		}

		/* This vertical stripe has no data, skip it. */
		if (!test_bit(sectornr, &rbio->dbitmap))
			continue;

		if (stripe < rbio->nr_data) {
			sector = sector_in_rbio(rbio, stripe, sectornr, 1);
			if (!sector)
				continue;
		} else {
			sector = rbio_stripe_sector(rbio, stripe, sectornr);
		}

		ret = rbio_add_io_sector(rbio, bio_list, sector,
					 rbio->real_stripes,
					 sectornr, REQ_OP_WRITE);
		if (ret)
			goto error;
	}

	return 0;
error:
	bio_list_put(bio_list);
	return -EIO;
}

static void set_rbio_range_error(struct btrfs_raid_bio *rbio, struct bio *bio)
{
	struct btrfs_fs_info *fs_info = rbio->bioc->fs_info;
	u32 offset = (bio->bi_iter.bi_sector << SECTOR_SHIFT) -
		     rbio->bioc->full_stripe_logical;
	int total_nr_sector = offset >> fs_info->sectorsize_bits;

	ASSERT(total_nr_sector < rbio->nr_data * rbio->stripe_nsectors);

	bitmap_set(rbio->error_bitmap, total_nr_sector,
		   bio->bi_iter.bi_size >> fs_info->sectorsize_bits);

	/*
	 * Special handling for raid56_alloc_missing_rbio() used by
	 * scrub/replace.  Unlike call path in raid56_parity_recover(), they
	 * pass an empty bio here.  Thus we have to find out the missing device
	 * and mark the stripe error instead.
	 */
	if (bio->bi_iter.bi_size == 0) {
		bool found_missing = false;
		int stripe_nr;

		for (stripe_nr = 0; stripe_nr < rbio->real_stripes; stripe_nr++) {
			if (!rbio->bioc->stripes[stripe_nr].dev->bdev) {
				found_missing = true;
				bitmap_set(rbio->error_bitmap,
					   stripe_nr * rbio->stripe_nsectors,
					   rbio->stripe_nsectors);
			}
		}
		ASSERT(found_missing);
	}
}

/*
 * For subpage case, we can no longer set page Up-to-date directly for
 * stripe_pages[], thus we need to locate the sector.
 */
static struct sector_ptr *find_stripe_sector(struct btrfs_raid_bio *rbio,
					     struct page *page,
					     unsigned int pgoff)
{
	int i;

	for (i = 0; i < rbio->nr_sectors; i++) {
		struct sector_ptr *sector = &rbio->stripe_sectors[i];

		if (sector->page == page && sector->pgoff == pgoff)
			return sector;
	}
	return NULL;
}

/*
 * this sets each page in the bio uptodate.  It should only be used on private
 * rbio pages, nothing that comes in from the higher layers
 */
static void set_bio_pages_uptodate(struct btrfs_raid_bio *rbio, struct bio *bio)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	ASSERT(!bio_flagged(bio, BIO_CLONED));

	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct sector_ptr *sector;
		int pgoff;

		for (pgoff = bvec->bv_offset; pgoff - bvec->bv_offset < bvec->bv_len;
		     pgoff += sectorsize) {
			sector = find_stripe_sector(rbio, bvec->bv_page, pgoff);
			ASSERT(sector);
			if (sector)
				sector->uptodate = 1;
		}
	}
}

static int get_bio_sector_nr(struct btrfs_raid_bio *rbio, struct bio *bio)
{
	struct bio_vec *bv = bio_first_bvec_all(bio);
	int i;

	for (i = 0; i < rbio->nr_sectors; i++) {
		struct sector_ptr *sector;

		sector = &rbio->stripe_sectors[i];
		if (sector->page == bv->bv_page && sector->pgoff == bv->bv_offset)
			break;
		sector = &rbio->bio_sectors[i];
		if (sector->page == bv->bv_page && sector->pgoff == bv->bv_offset)
			break;
	}
	ASSERT(i < rbio->nr_sectors);
	return i;
}

static void rbio_update_error_bitmap(struct btrfs_raid_bio *rbio, struct bio *bio)
{
	int total_sector_nr = get_bio_sector_nr(rbio, bio);
	u32 bio_size = 0;
	struct bio_vec *bvec;
	int i;

	bio_for_each_bvec_all(bvec, bio, i)
		bio_size += bvec->bv_len;

	/*
	 * Since we can have multiple bios touching the error_bitmap, we cannot
	 * call bitmap_set() without protection.
	 *
	 * Instead use set_bit() for each bit, as set_bit() itself is atomic.
	 */
	for (i = total_sector_nr; i < total_sector_nr +
	     (bio_size >> rbio->bioc->fs_info->sectorsize_bits); i++)
		set_bit(i, rbio->error_bitmap);
}

/* Verify the data sectors at read time. */
static void verify_bio_data_sectors(struct btrfs_raid_bio *rbio,
				    struct bio *bio)
{
	struct btrfs_fs_info *fs_info = rbio->bioc->fs_info;
	int total_sector_nr = get_bio_sector_nr(rbio, bio);
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	/* No data csum for the whole stripe, no need to verify. */
	if (!rbio->csum_bitmap || !rbio->csum_buf)
		return;

	/* P/Q stripes, they have no data csum to verify against. */
	if (total_sector_nr >= rbio->nr_data * rbio->stripe_nsectors)
		return;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		int bv_offset;

		for (bv_offset = bvec->bv_offset;
		     bv_offset < bvec->bv_offset + bvec->bv_len;
		     bv_offset += fs_info->sectorsize, total_sector_nr++) {
			u8 csum_buf[BTRFS_CSUM_SIZE];
			u8 *expected_csum = rbio->csum_buf +
					    total_sector_nr * fs_info->csum_size;
			int ret;

			/* No csum for this sector, skip to the next sector. */
			if (!test_bit(total_sector_nr, rbio->csum_bitmap))
				continue;

			ret = btrfs_check_sector_csum(fs_info, bvec->bv_page,
				bv_offset, csum_buf, expected_csum);
			if (ret < 0)
				set_bit(total_sector_nr, rbio->error_bitmap);
		}
	}
}

static void raid_wait_read_end_io(struct bio *bio)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;

	if (bio->bi_status) {
		rbio_update_error_bitmap(rbio, bio);
	} else {
		set_bio_pages_uptodate(rbio, bio);
		verify_bio_data_sectors(rbio, bio);
	}

	bio_put(bio);
	if (atomic_dec_and_test(&rbio->stripes_pending))
		wake_up(&rbio->io_wait);
}

static void submit_read_wait_bio_list(struct btrfs_raid_bio *rbio,
			     struct bio_list *bio_list)
{
	struct bio *bio;

	atomic_set(&rbio->stripes_pending, bio_list_size(bio_list));
	while ((bio = bio_list_pop(bio_list))) {
		bio->bi_end_io = raid_wait_read_end_io;

		if (trace_raid56_read_enabled()) {
			struct raid56_bio_trace_info trace_info = { 0 };

			bio_get_trace_info(rbio, bio, &trace_info);
			trace_raid56_read(rbio, bio, &trace_info);
		}
		submit_bio(bio);
	}

	wait_event(rbio->io_wait, atomic_read(&rbio->stripes_pending) == 0);
}

static int alloc_rbio_data_pages(struct btrfs_raid_bio *rbio)
{
	const int data_pages = rbio->nr_data * rbio->stripe_npages;
	int ret;

	ret = btrfs_alloc_page_array(data_pages, rbio->stripe_pages);
	if (ret < 0)
		return ret;

	index_stripe_sectors(rbio);
	return 0;
}

/*
 * We use plugging call backs to collect full stripes.
 * Any time we get a partial stripe write while plugged
 * we collect it into a list.  When the unplug comes down,
 * we sort the list by logical block number and merge
 * everything we can into the same rbios
 */
struct btrfs_plug_cb {
	struct blk_plug_cb cb;
	struct btrfs_fs_info *info;
	struct list_head rbio_list;
	struct work_struct work;
};

/*
 * rbios on the plug list are sorted for easier merging.
 */
static int plug_cmp(void *priv, const struct list_head *a,
		    const struct list_head *b)
{
	const struct btrfs_raid_bio *ra = container_of(a, struct btrfs_raid_bio,
						       plug_list);
	const struct btrfs_raid_bio *rb = container_of(b, struct btrfs_raid_bio,
						       plug_list);
	u64 a_sector = ra->bio_list.head->bi_iter.bi_sector;
	u64 b_sector = rb->bio_list.head->bi_iter.bi_sector;

	if (a_sector < b_sector)
		return -1;
	if (a_sector > b_sector)
		return 1;
	return 0;
}

static void raid_unplug(struct blk_plug_cb *cb, bool from_schedule)
{
	struct btrfs_plug_cb *plug = container_of(cb, struct btrfs_plug_cb, cb);
	struct btrfs_raid_bio *cur;
	struct btrfs_raid_bio *last = NULL;

	list_sort(NULL, &plug->rbio_list, plug_cmp);

	while (!list_empty(&plug->rbio_list)) {
		cur = list_entry(plug->rbio_list.next,
				 struct btrfs_raid_bio, plug_list);
		list_del_init(&cur->plug_list);

		if (rbio_is_full(cur)) {
			/* We have a full stripe, queue it down. */
			start_async_work(cur, rmw_rbio_work);
			continue;
		}
		if (last) {
			if (rbio_can_merge(last, cur)) {
				merge_rbio(last, cur);
				free_raid_bio(cur);
				continue;
			}
			start_async_work(last, rmw_rbio_work);
		}
		last = cur;
	}
	if (last)
		start_async_work(last, rmw_rbio_work);
	kfree(plug);
}

/* Add the original bio into rbio->bio_list, and update rbio::dbitmap. */
static void rbio_add_bio(struct btrfs_raid_bio *rbio, struct bio *orig_bio)
{
	const struct btrfs_fs_info *fs_info = rbio->bioc->fs_info;
	const u64 orig_logical = orig_bio->bi_iter.bi_sector << SECTOR_SHIFT;
	const u64 full_stripe_start = rbio->bioc->full_stripe_logical;
	const u32 orig_len = orig_bio->bi_iter.bi_size;
	const u32 sectorsize = fs_info->sectorsize;
	u64 cur_logical;

	ASSERT(orig_logical >= full_stripe_start &&
	       orig_logical + orig_len <= full_stripe_start +
	       rbio->nr_data * BTRFS_STRIPE_LEN);

	bio_list_add(&rbio->bio_list, orig_bio);
	rbio->bio_list_bytes += orig_bio->bi_iter.bi_size;

	/* Update the dbitmap. */
	for (cur_logical = orig_logical; cur_logical < orig_logical + orig_len;
	     cur_logical += sectorsize) {
		int bit = ((u32)(cur_logical - full_stripe_start) >>
			   fs_info->sectorsize_bits) % rbio->stripe_nsectors;

		set_bit(bit, &rbio->dbitmap);
	}
}

/*
 * our main entry point for writes from the rest of the FS.
 */
void raid56_parity_write(struct bio *bio, struct btrfs_io_context *bioc)
{
	struct btrfs_fs_info *fs_info = bioc->fs_info;
	struct btrfs_raid_bio *rbio;
	struct btrfs_plug_cb *plug = NULL;
	struct blk_plug_cb *cb;

	rbio = alloc_rbio(fs_info, bioc);
	if (IS_ERR(rbio)) {
		bio->bi_status = errno_to_blk_status(PTR_ERR(rbio));
		bio_endio(bio);
		return;
	}
	rbio->operation = BTRFS_RBIO_WRITE;
	rbio_add_bio(rbio, bio);

	/*
	 * Don't plug on full rbios, just get them out the door
	 * as quickly as we can
	 */
	if (!rbio_is_full(rbio)) {
		cb = blk_check_plugged(raid_unplug, fs_info, sizeof(*plug));
		if (cb) {
			plug = container_of(cb, struct btrfs_plug_cb, cb);
			if (!plug->info) {
				plug->info = fs_info;
				INIT_LIST_HEAD(&plug->rbio_list);
			}
			list_add_tail(&rbio->plug_list, &plug->rbio_list);
			return;
		}
	}

	/*
	 * Either we don't have any existing plug, or we're doing a full stripe,
	 * queue the rmw work now.
	 */
	start_async_work(rbio, rmw_rbio_work);
}

static int verify_one_sector(struct btrfs_raid_bio *rbio,
			     int stripe_nr, int sector_nr)
{
	struct btrfs_fs_info *fs_info = rbio->bioc->fs_info;
	struct sector_ptr *sector;
	u8 csum_buf[BTRFS_CSUM_SIZE];
	u8 *csum_expected;
	int ret;

	if (!rbio->csum_bitmap || !rbio->csum_buf)
		return 0;

	/* No way to verify P/Q as they are not covered by data csum. */
	if (stripe_nr >= rbio->nr_data)
		return 0;
	/*
	 * If we're rebuilding a read, we have to use pages from the
	 * bio list if possible.
	 */
	if (rbio->operation == BTRFS_RBIO_READ_REBUILD) {
		sector = sector_in_rbio(rbio, stripe_nr, sector_nr, 0);
	} else {
		sector = rbio_stripe_sector(rbio, stripe_nr, sector_nr);
	}

	ASSERT(sector->page);

	csum_expected = rbio->csum_buf +
			(stripe_nr * rbio->stripe_nsectors + sector_nr) *
			fs_info->csum_size;
	ret = btrfs_check_sector_csum(fs_info, sector->page, sector->pgoff,
				      csum_buf, csum_expected);
	return ret;
}

/*
 * Recover a vertical stripe specified by @sector_nr.
 * @*pointers are the pre-allocated pointers by the caller, so we don't
 * need to allocate/free the pointers again and again.
 */
static int recover_vertical(struct btrfs_raid_bio *rbio, int sector_nr,
			    void **pointers, void **unmap_array)
{
	struct btrfs_fs_info *fs_info = rbio->bioc->fs_info;
	struct sector_ptr *sector;
	const u32 sectorsize = fs_info->sectorsize;
	int found_errors;
	int faila;
	int failb;
	int stripe_nr;
	int ret = 0;

	/*
	 * Now we just use bitmap to mark the horizontal stripes in
	 * which we have data when doing parity scrub.
	 */
	if (rbio->operation == BTRFS_RBIO_PARITY_SCRUB &&
	    !test_bit(sector_nr, &rbio->dbitmap))
		return 0;

	found_errors = get_rbio_veritical_errors(rbio, sector_nr, &faila,
						 &failb);
	/*
	 * No errors in the vertical stripe, skip it.  Can happen for recovery
	 * which only part of a stripe failed csum check.
	 */
	if (!found_errors)
		return 0;

	if (found_errors > rbio->bioc->max_errors)
		return -EIO;

	/*
	 * Setup our array of pointers with sectors from each stripe
	 *
	 * NOTE: store a duplicate array of pointers to preserve the
	 * pointer order.
	 */
	for (stripe_nr = 0; stripe_nr < rbio->real_stripes; stripe_nr++) {
		/*
		 * If we're rebuilding a read, we have to use pages from the
		 * bio list if possible.
		 */
		if (rbio->operation == BTRFS_RBIO_READ_REBUILD) {
			sector = sector_in_rbio(rbio, stripe_nr, sector_nr, 0);
		} else {
			sector = rbio_stripe_sector(rbio, stripe_nr, sector_nr);
		}
		ASSERT(sector->page);
		pointers[stripe_nr] = kmap_local_page(sector->page) +
				   sector->pgoff;
		unmap_array[stripe_nr] = pointers[stripe_nr];
	}

	/* All raid6 handling here */
	if (rbio->bioc->map_type & BTRFS_BLOCK_GROUP_RAID6) {
		/* Single failure, rebuild from parity raid5 style */
		if (failb < 0) {
			if (faila == rbio->nr_data)
				/*
				 * Just the P stripe has failed, without
				 * a bad data or Q stripe.
				 * We have nothing to do, just skip the
				 * recovery for this stripe.
				 */
				goto cleanup;
			/*
			 * a single failure in raid6 is rebuilt
			 * in the pstripe code below
			 */
			goto pstripe;
		}

		/*
		 * If the q stripe is failed, do a pstripe reconstruction from
		 * the xors.
		 * If both the q stripe and the P stripe are failed, we're
		 * here due to a crc mismatch and we can't give them the
		 * data they want.
		 */
		if (failb == rbio->real_stripes - 1) {
			if (faila == rbio->real_stripes - 2)
				/*
				 * Only P and Q are corrupted.
				 * We only care about data stripes recovery,
				 * can skip this vertical stripe.
				 */
				goto cleanup;
			/*
			 * Otherwise we have one bad data stripe and
			 * a good P stripe.  raid5!
			 */
			goto pstripe;
		}

		if (failb == rbio->real_stripes - 2) {
			raid6_datap_recov(rbio->real_stripes, sectorsize,
					  faila, pointers);
		} else {
			raid6_2data_recov(rbio->real_stripes, sectorsize,
					  faila, failb, pointers);
		}
	} else {
		void *p;

		/* Rebuild from P stripe here (raid5 or raid6). */
		ASSERT(failb == -1);
pstripe:
		/* Copy parity block into failed block to start with */
		memcpy(pointers[faila], pointers[rbio->nr_data], sectorsize);

		/* Rearrange the pointer array */
		p = pointers[faila];
		for (stripe_nr = faila; stripe_nr < rbio->nr_data - 1;
		     stripe_nr++)
			pointers[stripe_nr] = pointers[stripe_nr + 1];
		pointers[rbio->nr_data - 1] = p;

		/* Xor in the rest */
		run_xor(pointers, rbio->nr_data - 1, sectorsize);

	}

	/*
	 * No matter if this is a RMW or recovery, we should have all
	 * failed sectors repaired in the vertical stripe, thus they are now
	 * uptodate.
	 * Especially if we determine to cache the rbio, we need to
	 * have at least all data sectors uptodate.
	 *
	 * If possible, also check if the repaired sector matches its data
	 * checksum.
	 */
	if (faila >= 0) {
		ret = verify_one_sector(rbio, faila, sector_nr);
		if (ret < 0)
			goto cleanup;

		sector = rbio_stripe_sector(rbio, faila, sector_nr);
		sector->uptodate = 1;
	}
	if (failb >= 0) {
		ret = verify_one_sector(rbio, failb, sector_nr);
		if (ret < 0)
			goto cleanup;

		sector = rbio_stripe_sector(rbio, failb, sector_nr);
		sector->uptodate = 1;
	}

cleanup:
	for (stripe_nr = rbio->real_stripes - 1; stripe_nr >= 0; stripe_nr--)
		kunmap_local(unmap_array[stripe_nr]);
	return ret;
}

static int recover_sectors(struct btrfs_raid_bio *rbio)
{
	void **pointers = NULL;
	void **unmap_array = NULL;
	int sectornr;
	int ret = 0;

	/*
	 * @pointers array stores the pointer for each sector.
	 *
	 * @unmap_array stores copy of pointers that does not get reordered
	 * during reconstruction so that kunmap_local works.
	 */
	pointers = kcalloc(rbio->real_stripes, sizeof(void *), GFP_NOFS);
	unmap_array = kcalloc(rbio->real_stripes, sizeof(void *), GFP_NOFS);
	if (!pointers || !unmap_array) {
		ret = -ENOMEM;
		goto out;
	}

	if (rbio->operation == BTRFS_RBIO_READ_REBUILD) {
		spin_lock(&rbio->bio_list_lock);
		set_bit(RBIO_RMW_LOCKED_BIT, &rbio->flags);
		spin_unlock(&rbio->bio_list_lock);
	}

	index_rbio_pages(rbio);

	for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++) {
		ret = recover_vertical(rbio, sectornr, pointers, unmap_array);
		if (ret < 0)
			break;
	}

out:
	kfree(pointers);
	kfree(unmap_array);
	return ret;
}

static void recover_rbio(struct btrfs_raid_bio *rbio)
{
	struct bio_list bio_list = BIO_EMPTY_LIST;
	int total_sector_nr;
	int ret = 0;

	/*
	 * Either we're doing recover for a read failure or degraded write,
	 * caller should have set error bitmap correctly.
	 */
	ASSERT(bitmap_weight(rbio->error_bitmap, rbio->nr_sectors));

	/* For recovery, we need to read all sectors including P/Q. */
	ret = alloc_rbio_pages(rbio);
	if (ret < 0)
		goto out;

	index_rbio_pages(rbio);

	/*
	 * Read everything that hasn't failed. However this time we will
	 * not trust any cached sector.
	 * As we may read out some stale data but higher layer is not reading
	 * that stale part.
	 *
	 * So here we always re-read everything in recovery path.
	 */
	for (total_sector_nr = 0; total_sector_nr < rbio->nr_sectors;
	     total_sector_nr++) {
		int stripe = total_sector_nr / rbio->stripe_nsectors;
		int sectornr = total_sector_nr % rbio->stripe_nsectors;
		struct sector_ptr *sector;

		/*
		 * Skip the range which has error.  It can be a range which is
		 * marked error (for csum mismatch), or it can be a missing
		 * device.
		 */
		if (!rbio->bioc->stripes[stripe].dev->bdev ||
		    test_bit(total_sector_nr, rbio->error_bitmap)) {
			/*
			 * Also set the error bit for missing device, which
			 * may not yet have its error bit set.
			 */
			set_bit(total_sector_nr, rbio->error_bitmap);
			continue;
		}

		sector = rbio_stripe_sector(rbio, stripe, sectornr);
		ret = rbio_add_io_sector(rbio, &bio_list, sector, stripe,
					 sectornr, REQ_OP_READ);
		if (ret < 0) {
			bio_list_put(&bio_list);
			goto out;
		}
	}

	submit_read_wait_bio_list(rbio, &bio_list);
	ret = recover_sectors(rbio);
out:
	rbio_orig_end_io(rbio, errno_to_blk_status(ret));
}

static void recover_rbio_work(struct work_struct *work)
{
	struct btrfs_raid_bio *rbio;

	rbio = container_of(work, struct btrfs_raid_bio, work);
	if (!lock_stripe_add(rbio))
		recover_rbio(rbio);
}

static void recover_rbio_work_locked(struct work_struct *work)
{
	recover_rbio(container_of(work, struct btrfs_raid_bio, work));
}

static void set_rbio_raid6_extra_error(struct btrfs_raid_bio *rbio, int mirror_num)
{
	bool found = false;
	int sector_nr;

	/*
	 * This is for RAID6 extra recovery tries, thus mirror number should
	 * be large than 2.
	 * Mirror 1 means read from data stripes. Mirror 2 means rebuild using
	 * RAID5 methods.
	 */
	ASSERT(mirror_num > 2);
	for (sector_nr = 0; sector_nr < rbio->stripe_nsectors; sector_nr++) {
		int found_errors;
		int faila;
		int failb;

		found_errors = get_rbio_veritical_errors(rbio, sector_nr,
							 &faila, &failb);
		/* This vertical stripe doesn't have errors. */
		if (!found_errors)
			continue;

		/*
		 * If we found errors, there should be only one error marked
		 * by previous set_rbio_range_error().
		 */
		ASSERT(found_errors == 1);
		found = true;

		/* Now select another stripe to mark as error. */
		failb = rbio->real_stripes - (mirror_num - 1);
		if (failb <= faila)
			failb--;

		/* Set the extra bit in error bitmap. */
		if (failb >= 0)
			set_bit(failb * rbio->stripe_nsectors + sector_nr,
				rbio->error_bitmap);
	}

	/* We should found at least one vertical stripe with error.*/
	ASSERT(found);
}

/*
 * the main entry point for reads from the higher layers.  This
 * is really only called when the normal read path had a failure,
 * so we assume the bio they send down corresponds to a failed part
 * of the drive.
 */
void raid56_parity_recover(struct bio *bio, struct btrfs_io_context *bioc,
			   int mirror_num)
{
	struct btrfs_fs_info *fs_info = bioc->fs_info;
	struct btrfs_raid_bio *rbio;

	rbio = alloc_rbio(fs_info, bioc);
	if (IS_ERR(rbio)) {
		bio->bi_status = errno_to_blk_status(PTR_ERR(rbio));
		bio_endio(bio);
		return;
	}

	rbio->operation = BTRFS_RBIO_READ_REBUILD;
	rbio_add_bio(rbio, bio);

	set_rbio_range_error(rbio, bio);

	/*
	 * Loop retry:
	 * for 'mirror == 2', reconstruct from all other stripes.
	 * for 'mirror_num > 2', select a stripe to fail on every retry.
	 */
	if (mirror_num > 2)
		set_rbio_raid6_extra_error(rbio, mirror_num);

	start_async_work(rbio, recover_rbio_work);
}

static void fill_data_csums(struct btrfs_raid_bio *rbio)
{
	struct btrfs_fs_info *fs_info = rbio->bioc->fs_info;
	struct btrfs_root *csum_root = btrfs_csum_root(fs_info,
						       rbio->bioc->full_stripe_logical);
	const u64 start = rbio->bioc->full_stripe_logical;
	const u32 len = (rbio->nr_data * rbio->stripe_nsectors) <<
			fs_info->sectorsize_bits;
	int ret;

	/* The rbio should not have its csum buffer initialized. */
	ASSERT(!rbio->csum_buf && !rbio->csum_bitmap);

	/*
	 * Skip the csum search if:
	 *
	 * - The rbio doesn't belong to data block groups
	 *   Then we are doing IO for tree blocks, no need to search csums.
	 *
	 * - The rbio belongs to mixed block groups
	 *   This is to avoid deadlock, as we're already holding the full
	 *   stripe lock, if we trigger a metadata read, and it needs to do
	 *   raid56 recovery, we will deadlock.
	 */
	if (!(rbio->bioc->map_type & BTRFS_BLOCK_GROUP_DATA) ||
	    rbio->bioc->map_type & BTRFS_BLOCK_GROUP_METADATA)
		return;

	rbio->csum_buf = kzalloc(rbio->nr_data * rbio->stripe_nsectors *
				 fs_info->csum_size, GFP_NOFS);
	rbio->csum_bitmap = bitmap_zalloc(rbio->nr_data * rbio->stripe_nsectors,
					  GFP_NOFS);
	if (!rbio->csum_buf || !rbio->csum_bitmap) {
		ret = -ENOMEM;
		goto error;
	}

	ret = btrfs_lookup_csums_bitmap(csum_root, NULL, start, start + len - 1,
					rbio->csum_buf, rbio->csum_bitmap);
	if (ret < 0)
		goto error;
	if (bitmap_empty(rbio->csum_bitmap, len >> fs_info->sectorsize_bits))
		goto no_csum;
	return;

error:
	/*
	 * We failed to allocate memory or grab the csum, but it's not fatal,
	 * we can still continue.  But better to warn users that RMW is no
	 * longer safe for this particular sub-stripe write.
	 */
	btrfs_warn_rl(fs_info,
"sub-stripe write for full stripe %llu is not safe, failed to get csum: %d",
			rbio->bioc->full_stripe_logical, ret);
no_csum:
	kfree(rbio->csum_buf);
	bitmap_free(rbio->csum_bitmap);
	rbio->csum_buf = NULL;
	rbio->csum_bitmap = NULL;
}

static int rmw_read_wait_recover(struct btrfs_raid_bio *rbio)
{
	struct bio_list bio_list = BIO_EMPTY_LIST;
	int total_sector_nr;
	int ret = 0;

	/*
	 * Fill the data csums we need for data verification.  We need to fill
	 * the csum_bitmap/csum_buf first, as our endio function will try to
	 * verify the data sectors.
	 */
	fill_data_csums(rbio);

	/*
	 * Build a list of bios to read all sectors (including data and P/Q).
	 *
	 * This behavior is to compensate the later csum verification and recovery.
	 */
	for (total_sector_nr = 0; total_sector_nr < rbio->nr_sectors;
	     total_sector_nr++) {
		struct sector_ptr *sector;
		int stripe = total_sector_nr / rbio->stripe_nsectors;
		int sectornr = total_sector_nr % rbio->stripe_nsectors;

		sector = rbio_stripe_sector(rbio, stripe, sectornr);
		ret = rbio_add_io_sector(rbio, &bio_list, sector,
			       stripe, sectornr, REQ_OP_READ);
		if (ret) {
			bio_list_put(&bio_list);
			return ret;
		}
	}

	/*
	 * We may or may not have any corrupted sectors (including missing dev
	 * and csum mismatch), just let recover_sectors() to handle them all.
	 */
	submit_read_wait_bio_list(rbio, &bio_list);
	return recover_sectors(rbio);
}

static void raid_wait_write_end_io(struct bio *bio)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;
	blk_status_t err = bio->bi_status;

	if (err)
		rbio_update_error_bitmap(rbio, bio);
	bio_put(bio);
	if (atomic_dec_and_test(&rbio->stripes_pending))
		wake_up(&rbio->io_wait);
}

static void submit_write_bios(struct btrfs_raid_bio *rbio,
			      struct bio_list *bio_list)
{
	struct bio *bio;

	atomic_set(&rbio->stripes_pending, bio_list_size(bio_list));
	while ((bio = bio_list_pop(bio_list))) {
		bio->bi_end_io = raid_wait_write_end_io;

		if (trace_raid56_write_enabled()) {
			struct raid56_bio_trace_info trace_info = { 0 };

			bio_get_trace_info(rbio, bio, &trace_info);
			trace_raid56_write(rbio, bio, &trace_info);
		}
		submit_bio(bio);
	}
}

/*
 * To determine if we need to read any sector from the disk.
 * Should only be utilized in RMW path, to skip cached rbio.
 */
static bool need_read_stripe_sectors(struct btrfs_raid_bio *rbio)
{
	int i;

	for (i = 0; i < rbio->nr_data * rbio->stripe_nsectors; i++) {
		struct sector_ptr *sector = &rbio->stripe_sectors[i];

		/*
		 * We have a sector which doesn't have page nor uptodate,
		 * thus this rbio can not be cached one, as cached one must
		 * have all its data sectors present and uptodate.
		 */
		if (!sector->page || !sector->uptodate)
			return true;
	}
	return false;
}

static void rmw_rbio(struct btrfs_raid_bio *rbio)
{
	struct bio_list bio_list;
	int sectornr;
	int ret = 0;

	/*
	 * Allocate the pages for parity first, as P/Q pages will always be
	 * needed for both full-stripe and sub-stripe writes.
	 */
	ret = alloc_rbio_parity_pages(rbio);
	if (ret < 0)
		goto out;

	/*
	 * Either full stripe write, or we have every data sector already
	 * cached, can go to write path immediately.
	 */
	if (!rbio_is_full(rbio) && need_read_stripe_sectors(rbio)) {
		/*
		 * Now we're doing sub-stripe write, also need all data stripes
		 * to do the full RMW.
		 */
		ret = alloc_rbio_data_pages(rbio);
		if (ret < 0)
			goto out;

		index_rbio_pages(rbio);

		ret = rmw_read_wait_recover(rbio);
		if (ret < 0)
			goto out;
	}

	/*
	 * At this stage we're not allowed to add any new bios to the
	 * bio list any more, anyone else that wants to change this stripe
	 * needs to do their own rmw.
	 */
	spin_lock(&rbio->bio_list_lock);
	set_bit(RBIO_RMW_LOCKED_BIT, &rbio->flags);
	spin_unlock(&rbio->bio_list_lock);

	bitmap_clear(rbio->error_bitmap, 0, rbio->nr_sectors);

	index_rbio_pages(rbio);

	/*
	 * We don't cache full rbios because we're assuming
	 * the higher layers are unlikely to use this area of
	 * the disk again soon.  If they do use it again,
	 * hopefully they will send another full bio.
	 */
	if (!rbio_is_full(rbio))
		cache_rbio_pages(rbio);
	else
		clear_bit(RBIO_CACHE_READY_BIT, &rbio->flags);

	for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++)
		generate_pq_vertical(rbio, sectornr);

	bio_list_init(&bio_list);
	ret = rmw_assemble_write_bios(rbio, &bio_list);
	if (ret < 0)
		goto out;

	/* We should have at least one bio assembled. */
	ASSERT(bio_list_size(&bio_list));
	submit_write_bios(rbio, &bio_list);
	wait_event(rbio->io_wait, atomic_read(&rbio->stripes_pending) == 0);

	/* We may have more errors than our tolerance during the read. */
	for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++) {
		int found_errors;

		found_errors = get_rbio_veritical_errors(rbio, sectornr, NULL, NULL);
		if (found_errors > rbio->bioc->max_errors) {
			ret = -EIO;
			break;
		}
	}
out:
	rbio_orig_end_io(rbio, errno_to_blk_status(ret));
}

static void rmw_rbio_work(struct work_struct *work)
{
	struct btrfs_raid_bio *rbio;

	rbio = container_of(work, struct btrfs_raid_bio, work);
	if (lock_stripe_add(rbio) == 0)
		rmw_rbio(rbio);
}

static void rmw_rbio_work_locked(struct work_struct *work)
{
	rmw_rbio(container_of(work, struct btrfs_raid_bio, work));
}

/*
 * The following code is used to scrub/replace the parity stripe
 *
 * Caller must have already increased bio_counter for getting @bioc.
 *
 * Note: We need make sure all the pages that add into the scrub/replace
 * raid bio are correct and not be changed during the scrub/replace. That
 * is those pages just hold metadata or file data with checksum.
 */

struct btrfs_raid_bio *raid56_parity_alloc_scrub_rbio(struct bio *bio,
				struct btrfs_io_context *bioc,
				struct btrfs_device *scrub_dev,
				unsigned long *dbitmap, int stripe_nsectors)
{
	struct btrfs_fs_info *fs_info = bioc->fs_info;
	struct btrfs_raid_bio *rbio;
	int i;

	rbio = alloc_rbio(fs_info, bioc);
	if (IS_ERR(rbio))
		return NULL;
	bio_list_add(&rbio->bio_list, bio);
	/*
	 * This is a special bio which is used to hold the completion handler
	 * and make the scrub rbio is similar to the other types
	 */
	ASSERT(!bio->bi_iter.bi_size);
	rbio->operation = BTRFS_RBIO_PARITY_SCRUB;

	/*
	 * After mapping bioc with BTRFS_MAP_WRITE, parities have been sorted
	 * to the end position, so this search can start from the first parity
	 * stripe.
	 */
	for (i = rbio->nr_data; i < rbio->real_stripes; i++) {
		if (bioc->stripes[i].dev == scrub_dev) {
			rbio->scrubp = i;
			break;
		}
	}
	ASSERT(i < rbio->real_stripes);

	bitmap_copy(&rbio->dbitmap, dbitmap, stripe_nsectors);
	return rbio;
}

/*
 * We just scrub the parity that we have correct data on the same horizontal,
 * so we needn't allocate all pages for all the stripes.
 */
static int alloc_rbio_essential_pages(struct btrfs_raid_bio *rbio)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	int total_sector_nr;

	for (total_sector_nr = 0; total_sector_nr < rbio->nr_sectors;
	     total_sector_nr++) {
		struct page *page;
		int sectornr = total_sector_nr % rbio->stripe_nsectors;
		int index = (total_sector_nr * sectorsize) >> PAGE_SHIFT;

		if (!test_bit(sectornr, &rbio->dbitmap))
			continue;
		if (rbio->stripe_pages[index])
			continue;
		page = alloc_page(GFP_NOFS);
		if (!page)
			return -ENOMEM;
		rbio->stripe_pages[index] = page;
	}
	index_stripe_sectors(rbio);
	return 0;
}

static int finish_parity_scrub(struct btrfs_raid_bio *rbio)
{
	struct btrfs_io_context *bioc = rbio->bioc;
	const u32 sectorsize = bioc->fs_info->sectorsize;
	void **pointers = rbio->finish_pointers;
	unsigned long *pbitmap = &rbio->finish_pbitmap;
	int nr_data = rbio->nr_data;
	int stripe;
	int sectornr;
	bool has_qstripe;
	struct sector_ptr p_sector = { 0 };
	struct sector_ptr q_sector = { 0 };
	struct bio_list bio_list;
	int is_replace = 0;
	int ret;

	bio_list_init(&bio_list);

	if (rbio->real_stripes - rbio->nr_data == 1)
		has_qstripe = false;
	else if (rbio->real_stripes - rbio->nr_data == 2)
		has_qstripe = true;
	else
		BUG();

	/*
	 * Replace is running and our P/Q stripe is being replaced, then we
	 * need to duplicate the final write to replace target.
	 */
	if (bioc->replace_nr_stripes && bioc->replace_stripe_src == rbio->scrubp) {
		is_replace = 1;
		bitmap_copy(pbitmap, &rbio->dbitmap, rbio->stripe_nsectors);
	}

	/*
	 * Because the higher layers(scrubber) are unlikely to
	 * use this area of the disk again soon, so don't cache
	 * it.
	 */
	clear_bit(RBIO_CACHE_READY_BIT, &rbio->flags);

	p_sector.page = alloc_page(GFP_NOFS);
	if (!p_sector.page)
		return -ENOMEM;
	p_sector.pgoff = 0;
	p_sector.uptodate = 1;

	if (has_qstripe) {
		/* RAID6, allocate and map temp space for the Q stripe */
		q_sector.page = alloc_page(GFP_NOFS);
		if (!q_sector.page) {
			__free_page(p_sector.page);
			p_sector.page = NULL;
			return -ENOMEM;
		}
		q_sector.pgoff = 0;
		q_sector.uptodate = 1;
		pointers[rbio->real_stripes - 1] = kmap_local_page(q_sector.page);
	}

	bitmap_clear(rbio->error_bitmap, 0, rbio->nr_sectors);

	/* Map the parity stripe just once */
	pointers[nr_data] = kmap_local_page(p_sector.page);

	for_each_set_bit(sectornr, &rbio->dbitmap, rbio->stripe_nsectors) {
		struct sector_ptr *sector;
		void *parity;

		/* first collect one page from each data stripe */
		for (stripe = 0; stripe < nr_data; stripe++) {
			sector = sector_in_rbio(rbio, stripe, sectornr, 0);
			pointers[stripe] = kmap_local_page(sector->page) +
					   sector->pgoff;
		}

		if (has_qstripe) {
			/* RAID6, call the library function to fill in our P/Q */
			raid6_call.gen_syndrome(rbio->real_stripes, sectorsize,
						pointers);
		} else {
			/* raid5 */
			memcpy(pointers[nr_data], pointers[0], sectorsize);
			run_xor(pointers + 1, nr_data - 1, sectorsize);
		}

		/* Check scrubbing parity and repair it */
		sector = rbio_stripe_sector(rbio, rbio->scrubp, sectornr);
		parity = kmap_local_page(sector->page) + sector->pgoff;
		if (memcmp(parity, pointers[rbio->scrubp], sectorsize) != 0)
			memcpy(parity, pointers[rbio->scrubp], sectorsize);
		else
			/* Parity is right, needn't writeback */
			bitmap_clear(&rbio->dbitmap, sectornr, 1);
		kunmap_local(parity);

		for (stripe = nr_data - 1; stripe >= 0; stripe--)
			kunmap_local(pointers[stripe]);
	}

	kunmap_local(pointers[nr_data]);
	__free_page(p_sector.page);
	p_sector.page = NULL;
	if (q_sector.page) {
		kunmap_local(pointers[rbio->real_stripes - 1]);
		__free_page(q_sector.page);
		q_sector.page = NULL;
	}

	/*
	 * time to start writing.  Make bios for everything from the
	 * higher layers (the bio_list in our rbio) and our p/q.  Ignore
	 * everything else.
	 */
	for_each_set_bit(sectornr, &rbio->dbitmap, rbio->stripe_nsectors) {
		struct sector_ptr *sector;

		sector = rbio_stripe_sector(rbio, rbio->scrubp, sectornr);
		ret = rbio_add_io_sector(rbio, &bio_list, sector, rbio->scrubp,
					 sectornr, REQ_OP_WRITE);
		if (ret)
			goto cleanup;
	}

	if (!is_replace)
		goto submit_write;

	/*
	 * Replace is running and our parity stripe needs to be duplicated to
	 * the target device.  Check we have a valid source stripe number.
	 */
	ASSERT(rbio->bioc->replace_stripe_src >= 0);
	for_each_set_bit(sectornr, pbitmap, rbio->stripe_nsectors) {
		struct sector_ptr *sector;

		sector = rbio_stripe_sector(rbio, rbio->scrubp, sectornr);
		ret = rbio_add_io_sector(rbio, &bio_list, sector,
					 rbio->real_stripes,
					 sectornr, REQ_OP_WRITE);
		if (ret)
			goto cleanup;
	}

submit_write:
	submit_write_bios(rbio, &bio_list);
	return 0;

cleanup:
	bio_list_put(&bio_list);
	return ret;
}

static inline int is_data_stripe(struct btrfs_raid_bio *rbio, int stripe)
{
	if (stripe >= 0 && stripe < rbio->nr_data)
		return 1;
	return 0;
}

static int recover_scrub_rbio(struct btrfs_raid_bio *rbio)
{
	void **pointers = NULL;
	void **unmap_array = NULL;
	int sector_nr;
	int ret = 0;

	/*
	 * @pointers array stores the pointer for each sector.
	 *
	 * @unmap_array stores copy of pointers that does not get reordered
	 * during reconstruction so that kunmap_local works.
	 */
	pointers = kcalloc(rbio->real_stripes, sizeof(void *), GFP_NOFS);
	unmap_array = kcalloc(rbio->real_stripes, sizeof(void *), GFP_NOFS);
	if (!pointers || !unmap_array) {
		ret = -ENOMEM;
		goto out;
	}

	for (sector_nr = 0; sector_nr < rbio->stripe_nsectors; sector_nr++) {
		int dfail = 0, failp = -1;
		int faila;
		int failb;
		int found_errors;

		found_errors = get_rbio_veritical_errors(rbio, sector_nr,
							 &faila, &failb);
		if (found_errors > rbio->bioc->max_errors) {
			ret = -EIO;
			goto out;
		}
		if (found_errors == 0)
			continue;

		/* We should have at least one error here. */
		ASSERT(faila >= 0 || failb >= 0);

		if (is_data_stripe(rbio, faila))
			dfail++;
		else if (is_parity_stripe(faila))
			failp = faila;

		if (is_data_stripe(rbio, failb))
			dfail++;
		else if (is_parity_stripe(failb))
			failp = failb;
		/*
		 * Because we can not use a scrubbing parity to repair the
		 * data, so the capability of the repair is declined.  (In the
		 * case of RAID5, we can not repair anything.)
		 */
		if (dfail > rbio->bioc->max_errors - 1) {
			ret = -EIO;
			goto out;
		}
		/*
		 * If all data is good, only parity is correctly, just repair
		 * the parity, no need to recover data stripes.
		 */
		if (dfail == 0)
			continue;

		/*
		 * Here means we got one corrupted data stripe and one
		 * corrupted parity on RAID6, if the corrupted parity is
		 * scrubbing parity, luckily, use the other one to repair the
		 * data, or we can not repair the data stripe.
		 */
		if (failp != rbio->scrubp) {
			ret = -EIO;
			goto out;
		}

		ret = recover_vertical(rbio, sector_nr, pointers, unmap_array);
		if (ret < 0)
			goto out;
	}
out:
	kfree(pointers);
	kfree(unmap_array);
	return ret;
}

static int scrub_assemble_read_bios(struct btrfs_raid_bio *rbio)
{
	struct bio_list bio_list = BIO_EMPTY_LIST;
	int total_sector_nr;
	int ret = 0;

	/* Build a list of bios to read all the missing parts. */
	for (total_sector_nr = 0; total_sector_nr < rbio->nr_sectors;
	     total_sector_nr++) {
		int sectornr = total_sector_nr % rbio->stripe_nsectors;
		int stripe = total_sector_nr / rbio->stripe_nsectors;
		struct sector_ptr *sector;

		/* No data in the vertical stripe, no need to read. */
		if (!test_bit(sectornr, &rbio->dbitmap))
			continue;

		/*
		 * We want to find all the sectors missing from the rbio and
		 * read them from the disk. If sector_in_rbio() finds a sector
		 * in the bio list we don't need to read it off the stripe.
		 */
		sector = sector_in_rbio(rbio, stripe, sectornr, 1);
		if (sector)
			continue;

		sector = rbio_stripe_sector(rbio, stripe, sectornr);
		/*
		 * The bio cache may have handed us an uptodate sector.  If so,
		 * use it.
		 */
		if (sector->uptodate)
			continue;

		ret = rbio_add_io_sector(rbio, &bio_list, sector, stripe,
					 sectornr, REQ_OP_READ);
		if (ret) {
			bio_list_put(&bio_list);
			return ret;
		}
	}

	submit_read_wait_bio_list(rbio, &bio_list);
	return 0;
}

static void scrub_rbio(struct btrfs_raid_bio *rbio)
{
	int sector_nr;
	int ret;

	ret = alloc_rbio_essential_pages(rbio);
	if (ret)
		goto out;

	bitmap_clear(rbio->error_bitmap, 0, rbio->nr_sectors);

	ret = scrub_assemble_read_bios(rbio);
	if (ret < 0)
		goto out;

	/* We may have some failures, recover the failed sectors first. */
	ret = recover_scrub_rbio(rbio);
	if (ret < 0)
		goto out;

	/*
	 * We have every sector properly prepared. Can finish the scrub
	 * and writeback the good content.
	 */
	ret = finish_parity_scrub(rbio);
	wait_event(rbio->io_wait, atomic_read(&rbio->stripes_pending) == 0);
	for (sector_nr = 0; sector_nr < rbio->stripe_nsectors; sector_nr++) {
		int found_errors;

		found_errors = get_rbio_veritical_errors(rbio, sector_nr, NULL, NULL);
		if (found_errors > rbio->bioc->max_errors) {
			ret = -EIO;
			break;
		}
	}
out:
	rbio_orig_end_io(rbio, errno_to_blk_status(ret));
}

static void scrub_rbio_work_locked(struct work_struct *work)
{
	scrub_rbio(container_of(work, struct btrfs_raid_bio, work));
}

void raid56_parity_submit_scrub_rbio(struct btrfs_raid_bio *rbio)
{
	if (!lock_stripe_add(rbio))
		start_async_work(rbio, scrub_rbio_work_locked);
}

/*
 * This is for scrub call sites where we already have correct data contents.
 * This allows us to avoid reading data stripes again.
 *
 * Unfortunately here we have to do page copy, other than reusing the pages.
 * This is due to the fact rbio has its own page management for its cache.
 */
void raid56_parity_cache_data_pages(struct btrfs_raid_bio *rbio,
				    struct page **data_pages, u64 data_logical)
{
	const u64 offset_in_full_stripe = data_logical -
					  rbio->bioc->full_stripe_logical;
	const int page_index = offset_in_full_stripe >> PAGE_SHIFT;
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	const u32 sectors_per_page = PAGE_SIZE / sectorsize;
	int ret;

	/*
	 * If we hit ENOMEM temporarily, but later at
	 * raid56_parity_submit_scrub_rbio() time it succeeded, we just do
	 * the extra read, not a big deal.
	 *
	 * If we hit ENOMEM later at raid56_parity_submit_scrub_rbio() time,
	 * the bio would got proper error number set.
	 */
	ret = alloc_rbio_data_pages(rbio);
	if (ret < 0)
		return;

	/* data_logical must be at stripe boundary and inside the full stripe. */
	ASSERT(IS_ALIGNED(offset_in_full_stripe, BTRFS_STRIPE_LEN));
	ASSERT(offset_in_full_stripe < (rbio->nr_data << BTRFS_STRIPE_LEN_SHIFT));

	for (int page_nr = 0; page_nr < (BTRFS_STRIPE_LEN >> PAGE_SHIFT); page_nr++) {
		struct page *dst = rbio->stripe_pages[page_nr + page_index];
		struct page *src = data_pages[page_nr];

		memcpy_page(dst, 0, src, 0, PAGE_SIZE);
		for (int sector_nr = sectors_per_page * page_index;
		     sector_nr < sectors_per_page * (page_index + 1);
		     sector_nr++)
			rbio->stripe_sectors[sector_nr].uptodate = true;
	}
}
