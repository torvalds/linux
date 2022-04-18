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
#include "misc.h"
#include "ctree.h"
#include "disk-io.h"
#include "volumes.h"
#include "raid56.h"
#include "async-thread.h"

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

enum btrfs_rbio_ops {
	BTRFS_RBIO_WRITE,
	BTRFS_RBIO_READ_REBUILD,
	BTRFS_RBIO_PARITY_SCRUB,
	BTRFS_RBIO_REBUILD_MISSING,
};

struct btrfs_raid_bio {
	struct btrfs_io_context *bioc;

	/* while we're doing rmw on a stripe
	 * we put it into a hash table so we can
	 * lock the stripe and merge more rbios
	 * into it.
	 */
	struct list_head hash_list;

	/*
	 * LRU list for the stripe cache
	 */
	struct list_head stripe_cache;

	/*
	 * for scheduling work in the helper threads
	 */
	struct work_struct work;

	/*
	 * bio list and bio_list_lock are used
	 * to add more bios into the stripe
	 * in hopes of avoiding the full rmw
	 */
	struct bio_list bio_list;
	spinlock_t bio_list_lock;

	/* also protected by the bio_list_lock, the
	 * plug list is used by the plugging code
	 * to collect partial bios while plugged.  The
	 * stripe locking code also uses it to hand off
	 * the stripe lock to the next pending IO
	 */
	struct list_head plug_list;

	/*
	 * flags that tell us if it is safe to
	 * merge with this bio
	 */
	unsigned long flags;

	/*
	 * set if we're doing a parity rebuild
	 * for a read from higher up, which is handled
	 * differently from a parity rebuild as part of
	 * rmw
	 */
	enum btrfs_rbio_ops operation;

	/* Size of each individual stripe on disk */
	u32 stripe_len;

	/* How many pages there are for the full stripe including P/Q */
	u16 nr_pages;

	/* How many sectors there are for the full stripe including P/Q */
	u16 nr_sectors;

	/* Number of data stripes (no p/q) */
	u8 nr_data;

	/* Numer of all stripes (including P/Q) */
	u8 real_stripes;

	/* How many pages there are for each stripe */
	u8 stripe_npages;

	/* How many sectors there are for each stripe */
	u8 stripe_nsectors;

	/* First bad stripe, -1 means no corruption */
	s8 faila;

	/* Second bad stripe (for RAID6 use) */
	s8 failb;

	/* Stripe number that we're scrubbing  */
	u8 scrubp;

	/*
	 * size of all the bios in the bio_list.  This
	 * helps us decide if the rbio maps to a full
	 * stripe or not
	 */
	int bio_list_bytes;

	int generic_bio_cnt;

	refcount_t refs;

	atomic_t stripes_pending;

	atomic_t error;
	/*
	 * these are two arrays of pointers.  We allocate the
	 * rbio big enough to hold them both and setup their
	 * locations when the rbio is allocated
	 */

	/* pointers to pages that we allocated for
	 * reading/writing stripes directly from the disk (including P/Q)
	 */
	struct page **stripe_pages;

	/* Pointers to the sectors in the bio_list, for faster lookup */
	struct sector_ptr *bio_sectors;

	/*
	 * For subpage support, we need to map each sector to above
	 * stripe_pages.
	 */
	struct sector_ptr *stripe_sectors;

	/* Bitmap to record which horizontal stripe has data */
	unsigned long *dbitmap;

	/* allocated with real_stripes-many pointers for finish_*() calls */
	void **finish_pointers;

	/* Allocated with stripe_nsectors-many bits for finish_*() calls */
	unsigned long *finish_pbitmap;
};

static int __raid56_parity_recover(struct btrfs_raid_bio *rbio);
static noinline void finish_rmw(struct btrfs_raid_bio *rbio);
static void rmw_work(struct work_struct *work);
static void read_rebuild_work(struct work_struct *work);
static int fail_bio_stripe(struct btrfs_raid_bio *rbio, struct bio *bio);
static int fail_rbio_index(struct btrfs_raid_bio *rbio, int failed);
static void __free_raid_bio(struct btrfs_raid_bio *rbio);
static void index_rbio_pages(struct btrfs_raid_bio *rbio);
static int alloc_rbio_pages(struct btrfs_raid_bio *rbio);

static noinline void finish_parity_scrub(struct btrfs_raid_bio *rbio,
					 int need_check);
static void scrub_parity_work(struct work_struct *work);

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
		if (!rbio->bio_sectors[i].page)
			continue;

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
	u64 num = rbio->bioc->raid_map[0];

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
	struct page *s;
	struct page *d;

	if (!test_bit(RBIO_CACHE_READY_BIT, &src->flags))
		return;

	for (i = 0; i < dest->nr_pages; i++) {
		s = src->stripe_pages[i];
		if (!s || !full_page_sectors_uptodate(src, i))
			continue;

		d = dest->stripe_pages[i];
		if (d)
			__free_page(d);

		dest->stripe_pages[i] = s;
		src->stripe_pages[i] = NULL;
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
	dest->generic_bio_cnt += victim->generic_bio_cnt;
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
		__free_raid_bio(rbio);
}

/*
 * prune a given rbio from the cache
 */
static void remove_rbio_from_cache(struct btrfs_raid_bio *rbio)
{
	struct btrfs_stripe_hash_table *table;
	unsigned long flags;

	if (!test_bit(RBIO_CACHE_BIT, &rbio->flags))
		return;

	table = rbio->bioc->fs_info->stripe_hash_table;

	spin_lock_irqsave(&table->cache_lock, flags);
	__remove_rbio_from_cache(rbio);
	spin_unlock_irqrestore(&table->cache_lock, flags);
}

/*
 * remove everything in the cache
 */
static void btrfs_clear_rbio_cache(struct btrfs_fs_info *info)
{
	struct btrfs_stripe_hash_table *table;
	unsigned long flags;
	struct btrfs_raid_bio *rbio;

	table = info->stripe_hash_table;

	spin_lock_irqsave(&table->cache_lock, flags);
	while (!list_empty(&table->stripe_cache)) {
		rbio = list_entry(table->stripe_cache.next,
				  struct btrfs_raid_bio,
				  stripe_cache);
		__remove_rbio_from_cache(rbio);
	}
	spin_unlock_irqrestore(&table->cache_lock, flags);
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
	unsigned long flags;

	if (!test_bit(RBIO_CACHE_READY_BIT, &rbio->flags))
		return;

	table = rbio->bioc->fs_info->stripe_hash_table;

	spin_lock_irqsave(&table->cache_lock, flags);
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

	spin_unlock_irqrestore(&table->cache_lock, flags);
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
	unsigned long flags;
	unsigned long size = rbio->bio_list_bytes;
	int ret = 1;

	spin_lock_irqsave(&rbio->bio_list_lock, flags);
	if (size != rbio->nr_data * rbio->stripe_len)
		ret = 0;
	BUG_ON(size > rbio->nr_data * rbio->stripe_len);
	spin_unlock_irqrestore(&rbio->bio_list_lock, flags);

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

	if (last->bioc->raid_map[0] != cur->bioc->raid_map[0])
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

	if (last->operation == BTRFS_RBIO_REBUILD_MISSING)
		return 0;

	if (last->operation == BTRFS_RBIO_READ_REBUILD) {
		int fa = last->faila;
		int fb = last->failb;
		int cur_fa = cur->faila;
		int cur_fb = cur->failb;

		if (last->faila >= last->failb) {
			fa = last->failb;
			fb = last->faila;
		}

		if (cur->faila >= cur->failb) {
			cur_fa = cur->failb;
			cur_fb = cur->faila;
		}

		if (fa != cur_fa || fb != cur_fb)
			return 0;
	}
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
	unsigned long flags;
	struct btrfs_raid_bio *freeit = NULL;
	struct btrfs_raid_bio *cache_drop = NULL;
	int ret = 0;

	h = rbio->bioc->fs_info->stripe_hash_table->table + rbio_bucket(rbio);

	spin_lock_irqsave(&h->lock, flags);
	list_for_each_entry(cur, &h->hash_list, hash_list) {
		if (cur->bioc->raid_map[0] != rbio->bioc->raid_map[0])
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
	spin_unlock_irqrestore(&h->lock, flags);
	if (cache_drop)
		remove_rbio_from_cache(cache_drop);
	if (freeit)
		__free_raid_bio(freeit);
	return ret;
}

/*
 * called as rmw or parity rebuild is completed.  If the plug list has more
 * rbios waiting for this stripe, the next one on the list will be started
 */
static noinline void unlock_stripe(struct btrfs_raid_bio *rbio)
{
	int bucket;
	struct btrfs_stripe_hash *h;
	unsigned long flags;
	int keep_cache = 0;

	bucket = rbio_bucket(rbio);
	h = rbio->bioc->fs_info->stripe_hash_table->table + bucket;

	if (list_empty(&rbio->plug_list))
		cache_rbio(rbio);

	spin_lock_irqsave(&h->lock, flags);
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
			spin_unlock_irqrestore(&h->lock, flags);

			if (next->operation == BTRFS_RBIO_READ_REBUILD)
				start_async_work(next, read_rebuild_work);
			else if (next->operation == BTRFS_RBIO_REBUILD_MISSING) {
				steal_rbio(rbio, next);
				start_async_work(next, read_rebuild_work);
			} else if (next->operation == BTRFS_RBIO_WRITE) {
				steal_rbio(rbio, next);
				start_async_work(next, rmw_work);
			} else if (next->operation == BTRFS_RBIO_PARITY_SCRUB) {
				steal_rbio(rbio, next);
				start_async_work(next, scrub_parity_work);
			}

			goto done_nolock;
		}
	}
done:
	spin_unlock(&rbio->bio_list_lock);
	spin_unlock_irqrestore(&h->lock, flags);

done_nolock:
	if (!keep_cache)
		remove_rbio_from_cache(rbio);
}

static void __free_raid_bio(struct btrfs_raid_bio *rbio)
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
	kfree(rbio);
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

	if (rbio->generic_bio_cnt)
		btrfs_bio_counter_sub(rbio->bioc->fs_info, rbio->generic_bio_cnt);

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
	__free_raid_bio(rbio);

	rbio_endio_bio_list(cur, err);
	if (extra)
		rbio_endio_bio_list(extra, err);
}

/*
 * end io function used by finish_rmw.  When we finally
 * get here, we've written a full stripe
 */
static void raid_write_end_io(struct bio *bio)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;
	blk_status_t err = bio->bi_status;
	int max_errors;

	if (err)
		fail_bio_stripe(rbio, bio);

	bio_put(bio);

	if (!atomic_dec_and_test(&rbio->stripes_pending))
		return;

	err = BLK_STS_OK;

	/* OK, we have read all the stripes we need to. */
	max_errors = (rbio->operation == BTRFS_RBIO_PARITY_SCRUB) ?
		     0 : rbio->bioc->max_errors;
	if (atomic_read(&rbio->error) > max_errors)
		err = BLK_STS_IOERR;

	rbio_orig_end_io(rbio, err);
}

/**
 * Get a sector pointer specified by its @stripe_nr and @sector_nr
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

	spin_lock_irq(&rbio->bio_list_lock);
	sector = &rbio->bio_sectors[index];
	if (sector->page || bio_list_only) {
		/* Don't return sector without a valid page pointer */
		if (!sector->page)
			sector = NULL;
		spin_unlock_irq(&rbio->bio_list_lock);
		return sector;
	}
	spin_unlock_irq(&rbio->bio_list_lock);

	return &rbio->stripe_sectors[index];
}

/*
 * allocation and initial setup for the btrfs_raid_bio.  Not
 * this does not allocate any pages for rbio->pages.
 */
static struct btrfs_raid_bio *alloc_rbio(struct btrfs_fs_info *fs_info,
					 struct btrfs_io_context *bioc,
					 u32 stripe_len)
{
	const unsigned int real_stripes = bioc->num_stripes - bioc->num_tgtdevs;
	const unsigned int stripe_npages = stripe_len >> PAGE_SHIFT;
	const unsigned int num_pages = stripe_npages * real_stripes;
	const unsigned int stripe_nsectors = stripe_len >> fs_info->sectorsize_bits;
	const unsigned int num_sectors = stripe_nsectors * real_stripes;
	struct btrfs_raid_bio *rbio;
	int nr_data = 0;
	void *p;

	ASSERT(IS_ALIGNED(stripe_len, PAGE_SIZE));
	/* PAGE_SIZE must also be aligned to sectorsize for subpage support */
	ASSERT(IS_ALIGNED(PAGE_SIZE, fs_info->sectorsize));

	rbio = kzalloc(sizeof(*rbio) +
		       sizeof(*rbio->stripe_pages) * num_pages +
		       sizeof(*rbio->bio_sectors) * num_sectors +
		       sizeof(*rbio->stripe_sectors) * num_sectors +
		       sizeof(*rbio->finish_pointers) * real_stripes +
		       sizeof(*rbio->dbitmap) * BITS_TO_LONGS(stripe_nsectors) +
		       sizeof(*rbio->finish_pbitmap) * BITS_TO_LONGS(stripe_nsectors),
		       GFP_NOFS);
	if (!rbio)
		return ERR_PTR(-ENOMEM);

	bio_list_init(&rbio->bio_list);
	INIT_LIST_HEAD(&rbio->plug_list);
	spin_lock_init(&rbio->bio_list_lock);
	INIT_LIST_HEAD(&rbio->stripe_cache);
	INIT_LIST_HEAD(&rbio->hash_list);
	rbio->bioc = bioc;
	rbio->stripe_len = stripe_len;
	rbio->nr_pages = num_pages;
	rbio->nr_sectors = num_sectors;
	rbio->real_stripes = real_stripes;
	rbio->stripe_npages = stripe_npages;
	rbio->stripe_nsectors = stripe_nsectors;
	rbio->faila = -1;
	rbio->failb = -1;
	refcount_set(&rbio->refs, 1);
	atomic_set(&rbio->error, 0);
	atomic_set(&rbio->stripes_pending, 0);

	/*
	 * The stripe_pages, bio_sectors, etc arrays point to the extra memory
	 * we allocated past the end of the rbio.
	 */
	p = rbio + 1;
#define CONSUME_ALLOC(ptr, count)	do {				\
		ptr = p;						\
		p = (unsigned char *)p + sizeof(*(ptr)) * (count);	\
	} while (0)
	CONSUME_ALLOC(rbio->stripe_pages, num_pages);
	CONSUME_ALLOC(rbio->bio_sectors, num_sectors);
	CONSUME_ALLOC(rbio->stripe_sectors, num_sectors);
	CONSUME_ALLOC(rbio->finish_pointers, real_stripes);
	CONSUME_ALLOC(rbio->dbitmap, BITS_TO_LONGS(stripe_nsectors));
	CONSUME_ALLOC(rbio->finish_pbitmap, BITS_TO_LONGS(stripe_nsectors));
#undef  CONSUME_ALLOC

	if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID5)
		nr_data = real_stripes - 1;
	else if (bioc->map_type & BTRFS_BLOCK_GROUP_RAID6)
		nr_data = real_stripes - 2;
	else
		BUG();

	rbio->nr_data = nr_data;
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
			      unsigned long bio_max_len,
			      unsigned int opf)
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
	if (!stripe->dev->bdev)
		return fail_rbio_index(rbio, stripe_nr);

	/* see if we can add this page onto our existing bio */
	if (last) {
		u64 last_end = last->bi_iter.bi_sector << 9;
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
	bio = bio_alloc(stripe->dev->bdev, max(bio_max_len >> PAGE_SHIFT, 1UL),
			opf, GFP_NOFS);
	bio->bi_iter.bi_sector = disk_start >> 9;
	bio->bi_private = rbio;

	bio_add_page(bio, sector->page, sectorsize, sector->pgoff);
	bio_list_add(bio_list, bio);
	return 0;
}

/*
 * while we're doing the read/modify/write cycle, we could
 * have errors in reading pages off the disk.  This checks
 * for errors and if we're not able to read the page it'll
 * trigger parity reconstruction.  The rmw will be finished
 * after we've reconstructed the failed stripes
 */
static void validate_rbio_for_rmw(struct btrfs_raid_bio *rbio)
{
	if (rbio->faila >= 0 || rbio->failb >= 0) {
		BUG_ON(rbio->faila == rbio->real_stripes - 1);
		__raid56_parity_recover(rbio);
	} else {
		finish_rmw(rbio);
	}
}

static void index_one_bio(struct btrfs_raid_bio *rbio, struct bio *bio)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	struct bio_vec bvec;
	struct bvec_iter iter;
	u32 offset = (bio->bi_iter.bi_sector << SECTOR_SHIFT) -
		     rbio->bioc->raid_map[0];

	if (bio_flagged(bio, BIO_CLONED))
		bio->bi_iter = btrfs_bio(bio)->iter;

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

	spin_lock_irq(&rbio->bio_list_lock);
	bio_list_for_each(bio, &rbio->bio_list)
		index_one_bio(rbio, bio);

	spin_unlock_irq(&rbio->bio_list_lock);
}

/*
 * this is called from one of two situations.  We either
 * have a full stripe from the higher layers, or we've read all
 * the missing bits off disk.
 *
 * This will calculate the parity and then send down any
 * changed blocks.
 */
static noinline void finish_rmw(struct btrfs_raid_bio *rbio)
{
	struct btrfs_io_context *bioc = rbio->bioc;
	const u32 sectorsize = bioc->fs_info->sectorsize;
	void **pointers = rbio->finish_pointers;
	int nr_data = rbio->nr_data;
	int stripe;
	int sectornr;
	bool has_qstripe;
	struct bio_list bio_list;
	struct bio *bio;
	int ret;

	bio_list_init(&bio_list);

	if (rbio->real_stripes - rbio->nr_data == 1)
		has_qstripe = false;
	else if (rbio->real_stripes - rbio->nr_data == 2)
		has_qstripe = true;
	else
		BUG();

	/* at this point we either have a full stripe,
	 * or we've read the full stripe from the drive.
	 * recalculate the parity and write the new results.
	 *
	 * We're not allowed to add any new bios to the
	 * bio list here, anyone else that wants to
	 * change this stripe needs to do their own rmw.
	 */
	spin_lock_irq(&rbio->bio_list_lock);
	set_bit(RBIO_RMW_LOCKED_BIT, &rbio->flags);
	spin_unlock_irq(&rbio->bio_list_lock);

	atomic_set(&rbio->error, 0);

	/*
	 * now that we've set rmw_locked, run through the
	 * bio list one last time and map the page pointers
	 *
	 * We don't cache full rbios because we're assuming
	 * the higher layers are unlikely to use this area of
	 * the disk again soon.  If they do use it again,
	 * hopefully they will send another full bio.
	 */
	index_rbio_pages(rbio);
	if (!rbio_is_full(rbio))
		cache_rbio_pages(rbio);
	else
		clear_bit(RBIO_CACHE_READY_BIT, &rbio->flags);

	for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++) {
		struct sector_ptr *sector;

		/* First collect one sector from each data stripe */
		for (stripe = 0; stripe < nr_data; stripe++) {
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
			memcpy(pointers[nr_data], pointers[0], sectorsize);
			run_xor(pointers + 1, nr_data - 1, sectorsize);
		}
		for (stripe = stripe - 1; stripe >= 0; stripe--)
			kunmap_local(pointers[stripe]);
	}

	/*
	 * time to start writing.  Make bios for everything from the
	 * higher layers (the bio_list in our rbio) and our p/q.  Ignore
	 * everything else.
	 */
	for (stripe = 0; stripe < rbio->real_stripes; stripe++) {
		for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++) {
			struct sector_ptr *sector;

			if (stripe < rbio->nr_data) {
				sector = sector_in_rbio(rbio, stripe, sectornr, 1);
				if (!sector)
					continue;
			} else {
				sector = rbio_stripe_sector(rbio, stripe, sectornr);
			}

			ret = rbio_add_io_sector(rbio, &bio_list, sector, stripe,
						 sectornr, rbio->stripe_len,
						 REQ_OP_WRITE);
			if (ret)
				goto cleanup;
		}
	}

	if (likely(!bioc->num_tgtdevs))
		goto write_data;

	for (stripe = 0; stripe < rbio->real_stripes; stripe++) {
		if (!bioc->tgtdev_map[stripe])
			continue;

		for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++) {
			struct sector_ptr *sector;

			if (stripe < rbio->nr_data) {
				sector = sector_in_rbio(rbio, stripe, sectornr, 1);
				if (!sector)
					continue;
			} else {
				sector = rbio_stripe_sector(rbio, stripe, sectornr);
			}

			ret = rbio_add_io_sector(rbio, &bio_list, sector,
					       rbio->bioc->tgtdev_map[stripe],
					       sectornr, rbio->stripe_len,
					       REQ_OP_WRITE);
			if (ret)
				goto cleanup;
		}
	}

write_data:
	atomic_set(&rbio->stripes_pending, bio_list_size(&bio_list));
	BUG_ON(atomic_read(&rbio->stripes_pending) == 0);

	while ((bio = bio_list_pop(&bio_list))) {
		bio->bi_end_io = raid_write_end_io;

		submit_bio(bio);
	}
	return;

cleanup:
	rbio_orig_end_io(rbio, BLK_STS_IOERR);

	while ((bio = bio_list_pop(&bio_list)))
		bio_put(bio);
}

/*
 * helper to find the stripe number for a given bio.  Used to figure out which
 * stripe has failed.  This expects the bio to correspond to a physical disk,
 * so it looks up based on physical sector numbers.
 */
static int find_bio_stripe(struct btrfs_raid_bio *rbio,
			   struct bio *bio)
{
	u64 physical = bio->bi_iter.bi_sector;
	int i;
	struct btrfs_io_stripe *stripe;

	physical <<= 9;

	for (i = 0; i < rbio->bioc->num_stripes; i++) {
		stripe = &rbio->bioc->stripes[i];
		if (in_range(physical, stripe->physical, rbio->stripe_len) &&
		    stripe->dev->bdev && bio->bi_bdev == stripe->dev->bdev) {
			return i;
		}
	}
	return -1;
}

/*
 * helper to find the stripe number for a given
 * bio (before mapping).  Used to figure out which stripe has
 * failed.  This looks up based on logical block numbers.
 */
static int find_logical_bio_stripe(struct btrfs_raid_bio *rbio,
				   struct bio *bio)
{
	u64 logical = bio->bi_iter.bi_sector << 9;
	int i;

	for (i = 0; i < rbio->nr_data; i++) {
		u64 stripe_start = rbio->bioc->raid_map[i];

		if (in_range(logical, stripe_start, rbio->stripe_len))
			return i;
	}
	return -1;
}

/*
 * returns -EIO if we had too many failures
 */
static int fail_rbio_index(struct btrfs_raid_bio *rbio, int failed)
{
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&rbio->bio_list_lock, flags);

	/* we already know this stripe is bad, move on */
	if (rbio->faila == failed || rbio->failb == failed)
		goto out;

	if (rbio->faila == -1) {
		/* first failure on this rbio */
		rbio->faila = failed;
		atomic_inc(&rbio->error);
	} else if (rbio->failb == -1) {
		/* second failure on this rbio */
		rbio->failb = failed;
		atomic_inc(&rbio->error);
	} else {
		ret = -EIO;
	}
out:
	spin_unlock_irqrestore(&rbio->bio_list_lock, flags);

	return ret;
}

/*
 * helper to fail a stripe based on a physical disk
 * bio.
 */
static int fail_bio_stripe(struct btrfs_raid_bio *rbio,
			   struct bio *bio)
{
	int failed = find_bio_stripe(rbio, bio);

	if (failed < 0)
		return -EIO;

	return fail_rbio_index(rbio, failed);
}

/*
 * For subpage case, we can no longer set page Uptodate directly for
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

/*
 * end io for the read phase of the rmw cycle.  All the bios here are physical
 * stripe bios we've read from the disk so we can recalculate the parity of the
 * stripe.
 *
 * This will usually kick off finish_rmw once all the bios are read in, but it
 * may trigger parity reconstruction if we had any errors along the way
 */
static void raid_rmw_end_io(struct bio *bio)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;

	if (bio->bi_status)
		fail_bio_stripe(rbio, bio);
	else
		set_bio_pages_uptodate(rbio, bio);

	bio_put(bio);

	if (!atomic_dec_and_test(&rbio->stripes_pending))
		return;

	if (atomic_read(&rbio->error) > rbio->bioc->max_errors)
		goto cleanup;

	/*
	 * this will normally call finish_rmw to start our write
	 * but if there are any failed stripes we'll reconstruct
	 * from parity first
	 */
	validate_rbio_for_rmw(rbio);
	return;

cleanup:

	rbio_orig_end_io(rbio, BLK_STS_IOERR);
}

/*
 * the stripe must be locked by the caller.  It will
 * unlock after all the writes are done
 */
static int raid56_rmw_stripe(struct btrfs_raid_bio *rbio)
{
	int bios_to_read = 0;
	struct bio_list bio_list;
	int ret;
	int sectornr;
	int stripe;
	struct bio *bio;

	bio_list_init(&bio_list);

	ret = alloc_rbio_pages(rbio);
	if (ret)
		goto cleanup;

	index_rbio_pages(rbio);

	atomic_set(&rbio->error, 0);
	/*
	 * build a list of bios to read all the missing parts of this
	 * stripe
	 */
	for (stripe = 0; stripe < rbio->nr_data; stripe++) {
		for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++) {
			struct sector_ptr *sector;

			/*
			 * We want to find all the sectors missing from the
			 * rbio and read them from the disk.  If * sector_in_rbio()
			 * finds a page in the bio list we don't need to read
			 * it off the stripe.
			 */
			sector = sector_in_rbio(rbio, stripe, sectornr, 1);
			if (sector)
				continue;

			sector = rbio_stripe_sector(rbio, stripe, sectornr);
			/*
			 * The bio cache may have handed us an uptodate page.
			 * If so, be happy and use it.
			 */
			if (sector->uptodate)
				continue;

			ret = rbio_add_io_sector(rbio, &bio_list, sector,
				       stripe, sectornr, rbio->stripe_len,
				       REQ_OP_READ);
			if (ret)
				goto cleanup;
		}
	}

	bios_to_read = bio_list_size(&bio_list);
	if (!bios_to_read) {
		/*
		 * this can happen if others have merged with
		 * us, it means there is nothing left to read.
		 * But if there are missing devices it may not be
		 * safe to do the full stripe write yet.
		 */
		goto finish;
	}

	/*
	 * The bioc may be freed once we submit the last bio. Make sure not to
	 * touch it after that.
	 */
	atomic_set(&rbio->stripes_pending, bios_to_read);
	while ((bio = bio_list_pop(&bio_list))) {
		bio->bi_end_io = raid_rmw_end_io;

		btrfs_bio_wq_end_io(rbio->bioc->fs_info, bio, BTRFS_WQ_ENDIO_RAID56);

		submit_bio(bio);
	}
	/* the actual write will happen once the reads are done */
	return 0;

cleanup:
	rbio_orig_end_io(rbio, BLK_STS_IOERR);

	while ((bio = bio_list_pop(&bio_list)))
		bio_put(bio);

	return -EIO;

finish:
	validate_rbio_for_rmw(rbio);
	return 0;
}

/*
 * if the upper layers pass in a full stripe, we thank them by only allocating
 * enough pages to hold the parity, and sending it all down quickly.
 */
static int full_stripe_write(struct btrfs_raid_bio *rbio)
{
	int ret;

	ret = alloc_rbio_parity_pages(rbio);
	if (ret) {
		__free_raid_bio(rbio);
		return ret;
	}

	ret = lock_stripe_add(rbio);
	if (ret == 0)
		finish_rmw(rbio);
	return 0;
}

/*
 * partial stripe writes get handed over to async helpers.
 * We're really hoping to merge a few more writes into this
 * rbio before calculating new parity
 */
static int partial_stripe_write(struct btrfs_raid_bio *rbio)
{
	int ret;

	ret = lock_stripe_add(rbio);
	if (ret == 0)
		start_async_work(rbio, rmw_work);
	return 0;
}

/*
 * sometimes while we were reading from the drive to
 * recalculate parity, enough new bios come into create
 * a full stripe.  So we do a check here to see if we can
 * go directly to finish_rmw
 */
static int __raid56_parity_write(struct btrfs_raid_bio *rbio)
{
	/* head off into rmw land if we don't have a full stripe */
	if (!rbio_is_full(rbio))
		return partial_stripe_write(rbio);
	return full_stripe_write(rbio);
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

static void run_plug(struct btrfs_plug_cb *plug)
{
	struct btrfs_raid_bio *cur;
	struct btrfs_raid_bio *last = NULL;

	/*
	 * sort our plug list then try to merge
	 * everything we can in hopes of creating full
	 * stripes.
	 */
	list_sort(NULL, &plug->rbio_list, plug_cmp);
	while (!list_empty(&plug->rbio_list)) {
		cur = list_entry(plug->rbio_list.next,
				 struct btrfs_raid_bio, plug_list);
		list_del_init(&cur->plug_list);

		if (rbio_is_full(cur)) {
			int ret;

			/* we have a full stripe, send it down */
			ret = full_stripe_write(cur);
			BUG_ON(ret);
			continue;
		}
		if (last) {
			if (rbio_can_merge(last, cur)) {
				merge_rbio(last, cur);
				__free_raid_bio(cur);
				continue;

			}
			__raid56_parity_write(last);
		}
		last = cur;
	}
	if (last) {
		__raid56_parity_write(last);
	}
	kfree(plug);
}

/*
 * if the unplug comes from schedule, we have to push the
 * work off to a helper thread
 */
static void unplug_work(struct work_struct *work)
{
	struct btrfs_plug_cb *plug;
	plug = container_of(work, struct btrfs_plug_cb, work);
	run_plug(plug);
}

static void btrfs_raid_unplug(struct blk_plug_cb *cb, bool from_schedule)
{
	struct btrfs_plug_cb *plug;
	plug = container_of(cb, struct btrfs_plug_cb, cb);

	if (from_schedule) {
		INIT_WORK(&plug->work, unplug_work);
		queue_work(plug->info->rmw_workers, &plug->work);
		return;
	}
	run_plug(plug);
}

/*
 * our main entry point for writes from the rest of the FS.
 */
int raid56_parity_write(struct bio *bio, struct btrfs_io_context *bioc, u32 stripe_len)
{
	struct btrfs_fs_info *fs_info = bioc->fs_info;
	struct btrfs_raid_bio *rbio;
	struct btrfs_plug_cb *plug = NULL;
	struct blk_plug_cb *cb;
	int ret;

	rbio = alloc_rbio(fs_info, bioc, stripe_len);
	if (IS_ERR(rbio)) {
		btrfs_put_bioc(bioc);
		return PTR_ERR(rbio);
	}
	bio_list_add(&rbio->bio_list, bio);
	rbio->bio_list_bytes = bio->bi_iter.bi_size;
	rbio->operation = BTRFS_RBIO_WRITE;

	btrfs_bio_counter_inc_noblocked(fs_info);
	rbio->generic_bio_cnt = 1;

	/*
	 * don't plug on full rbios, just get them out the door
	 * as quickly as we can
	 */
	if (rbio_is_full(rbio)) {
		ret = full_stripe_write(rbio);
		if (ret)
			btrfs_bio_counter_dec(fs_info);
		return ret;
	}

	cb = blk_check_plugged(btrfs_raid_unplug, fs_info, sizeof(*plug));
	if (cb) {
		plug = container_of(cb, struct btrfs_plug_cb, cb);
		if (!plug->info) {
			plug->info = fs_info;
			INIT_LIST_HEAD(&plug->rbio_list);
		}
		list_add_tail(&rbio->plug_list, &plug->rbio_list);
		ret = 0;
	} else {
		ret = __raid56_parity_write(rbio);
		if (ret)
			btrfs_bio_counter_dec(fs_info);
	}
	return ret;
}

/*
 * all parity reconstruction happens here.  We've read in everything
 * we can find from the drives and this does the heavy lifting of
 * sorting the good from the bad.
 */
static void __raid_recover_end_io(struct btrfs_raid_bio *rbio)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	int sectornr, stripe;
	void **pointers;
	void **unmap_array;
	int faila = -1, failb = -1;
	blk_status_t err;
	int i;

	/*
	 * This array stores the pointer for each sector, thus it has the extra
	 * pgoff value added from each sector
	 */
	pointers = kcalloc(rbio->real_stripes, sizeof(void *), GFP_NOFS);
	if (!pointers) {
		err = BLK_STS_RESOURCE;
		goto cleanup_io;
	}

	/*
	 * Store copy of pointers that does not get reordered during
	 * reconstruction so that kunmap_local works.
	 */
	unmap_array = kcalloc(rbio->real_stripes, sizeof(void *), GFP_NOFS);
	if (!unmap_array) {
		err = BLK_STS_RESOURCE;
		goto cleanup_pointers;
	}

	faila = rbio->faila;
	failb = rbio->failb;

	if (rbio->operation == BTRFS_RBIO_READ_REBUILD ||
	    rbio->operation == BTRFS_RBIO_REBUILD_MISSING) {
		spin_lock_irq(&rbio->bio_list_lock);
		set_bit(RBIO_RMW_LOCKED_BIT, &rbio->flags);
		spin_unlock_irq(&rbio->bio_list_lock);
	}

	index_rbio_pages(rbio);

	for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++) {
		struct sector_ptr *sector;

		/*
		 * Now we just use bitmap to mark the horizontal stripes in
		 * which we have data when doing parity scrub.
		 */
		if (rbio->operation == BTRFS_RBIO_PARITY_SCRUB &&
		    !test_bit(sectornr, rbio->dbitmap))
			continue;

		/*
		 * Setup our array of pointers with sectors from each stripe
		 *
		 * NOTE: store a duplicate array of pointers to preserve the
		 * pointer order
		 */
		for (stripe = 0; stripe < rbio->real_stripes; stripe++) {
			/*
			 * If we're rebuilding a read, we have to use
			 * pages from the bio list
			 */
			if ((rbio->operation == BTRFS_RBIO_READ_REBUILD ||
			     rbio->operation == BTRFS_RBIO_REBUILD_MISSING) &&
			    (stripe == faila || stripe == failb)) {
				sector = sector_in_rbio(rbio, stripe, sectornr, 0);
			} else {
				sector = rbio_stripe_sector(rbio, stripe, sectornr);
			}
			ASSERT(sector->page);
			pointers[stripe] = kmap_local_page(sector->page) +
					   sector->pgoff;
			unmap_array[stripe] = pointers[stripe];
		}

		/* All raid6 handling here */
		if (rbio->bioc->map_type & BTRFS_BLOCK_GROUP_RAID6) {
			/* Single failure, rebuild from parity raid5 style */
			if (failb < 0) {
				if (faila == rbio->nr_data) {
					/*
					 * Just the P stripe has failed, without
					 * a bad data or Q stripe.
					 * TODO, we should redo the xor here.
					 */
					err = BLK_STS_IOERR;
					goto cleanup;
				}
				/*
				 * a single failure in raid6 is rebuilt
				 * in the pstripe code below
				 */
				goto pstripe;
			}

			/* make sure our ps and qs are in order */
			if (faila > failb)
				swap(faila, failb);

			/* if the q stripe is failed, do a pstripe reconstruction
			 * from the xors.
			 * If both the q stripe and the P stripe are failed, we're
			 * here due to a crc mismatch and we can't give them the
			 * data they want
			 */
			if (rbio->bioc->raid_map[failb] == RAID6_Q_STRIPE) {
				if (rbio->bioc->raid_map[faila] ==
				    RAID5_P_STRIPE) {
					err = BLK_STS_IOERR;
					goto cleanup;
				}
				/*
				 * otherwise we have one bad data stripe and
				 * a good P stripe.  raid5!
				 */
				goto pstripe;
			}

			if (rbio->bioc->raid_map[failb] == RAID5_P_STRIPE) {
				raid6_datap_recov(rbio->real_stripes,
						  sectorsize, faila, pointers);
			} else {
				raid6_2data_recov(rbio->real_stripes,
						  sectorsize, faila, failb,
						  pointers);
			}
		} else {
			void *p;

			/* rebuild from P stripe here (raid5 or raid6) */
			BUG_ON(failb != -1);
pstripe:
			/* Copy parity block into failed block to start with */
			memcpy(pointers[faila], pointers[rbio->nr_data], sectorsize);

			/* rearrange the pointer array */
			p = pointers[faila];
			for (stripe = faila; stripe < rbio->nr_data - 1; stripe++)
				pointers[stripe] = pointers[stripe + 1];
			pointers[rbio->nr_data - 1] = p;

			/* xor in the rest */
			run_xor(pointers, rbio->nr_data - 1, sectorsize);
		}
		/* if we're doing this rebuild as part of an rmw, go through
		 * and set all of our private rbio pages in the
		 * failed stripes as uptodate.  This way finish_rmw will
		 * know they can be trusted.  If this was a read reconstruction,
		 * other endio functions will fiddle the uptodate bits
		 */
		if (rbio->operation == BTRFS_RBIO_WRITE) {
			for (i = 0;  i < rbio->stripe_nsectors; i++) {
				if (faila != -1) {
					sector = rbio_stripe_sector(rbio, faila, i);
					sector->uptodate = 1;
				}
				if (failb != -1) {
					sector = rbio_stripe_sector(rbio, failb, i);
					sector->uptodate = 1;
				}
			}
		}
		for (stripe = rbio->real_stripes - 1; stripe >= 0; stripe--)
			kunmap_local(unmap_array[stripe]);
	}

	err = BLK_STS_OK;
cleanup:
	kfree(unmap_array);
cleanup_pointers:
	kfree(pointers);

cleanup_io:
	/*
	 * Similar to READ_REBUILD, REBUILD_MISSING at this point also has a
	 * valid rbio which is consistent with ondisk content, thus such a
	 * valid rbio can be cached to avoid further disk reads.
	 */
	if (rbio->operation == BTRFS_RBIO_READ_REBUILD ||
	    rbio->operation == BTRFS_RBIO_REBUILD_MISSING) {
		/*
		 * - In case of two failures, where rbio->failb != -1:
		 *
		 *   Do not cache this rbio since the above read reconstruction
		 *   (raid6_datap_recov() or raid6_2data_recov()) may have
		 *   changed some content of stripes which are not identical to
		 *   on-disk content any more, otherwise, a later write/recover
		 *   may steal stripe_pages from this rbio and end up with
		 *   corruptions or rebuild failures.
		 *
		 * - In case of single failure, where rbio->failb == -1:
		 *
		 *   Cache this rbio iff the above read reconstruction is
		 *   executed without problems.
		 */
		if (err == BLK_STS_OK && rbio->failb < 0)
			cache_rbio_pages(rbio);
		else
			clear_bit(RBIO_CACHE_READY_BIT, &rbio->flags);

		rbio_orig_end_io(rbio, err);
	} else if (err == BLK_STS_OK) {
		rbio->faila = -1;
		rbio->failb = -1;

		if (rbio->operation == BTRFS_RBIO_WRITE)
			finish_rmw(rbio);
		else if (rbio->operation == BTRFS_RBIO_PARITY_SCRUB)
			finish_parity_scrub(rbio, 0);
		else
			BUG();
	} else {
		rbio_orig_end_io(rbio, err);
	}
}

/*
 * This is called only for stripes we've read from disk to
 * reconstruct the parity.
 */
static void raid_recover_end_io(struct bio *bio)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;

	/*
	 * we only read stripe pages off the disk, set them
	 * up to date if there were no errors
	 */
	if (bio->bi_status)
		fail_bio_stripe(rbio, bio);
	else
		set_bio_pages_uptodate(rbio, bio);
	bio_put(bio);

	if (!atomic_dec_and_test(&rbio->stripes_pending))
		return;

	if (atomic_read(&rbio->error) > rbio->bioc->max_errors)
		rbio_orig_end_io(rbio, BLK_STS_IOERR);
	else
		__raid_recover_end_io(rbio);
}

/*
 * reads everything we need off the disk to reconstruct
 * the parity. endio handlers trigger final reconstruction
 * when the IO is done.
 *
 * This is used both for reads from the higher layers and for
 * parity construction required to finish a rmw cycle.
 */
static int __raid56_parity_recover(struct btrfs_raid_bio *rbio)
{
	int bios_to_read = 0;
	struct bio_list bio_list;
	int ret;
	int sectornr;
	int stripe;
	struct bio *bio;

	bio_list_init(&bio_list);

	ret = alloc_rbio_pages(rbio);
	if (ret)
		goto cleanup;

	atomic_set(&rbio->error, 0);

	/*
	 * read everything that hasn't failed.  Thanks to the
	 * stripe cache, it is possible that some or all of these
	 * pages are going to be uptodate.
	 */
	for (stripe = 0; stripe < rbio->real_stripes; stripe++) {
		if (rbio->faila == stripe || rbio->failb == stripe) {
			atomic_inc(&rbio->error);
			continue;
		}

		for (sectornr = 0; sectornr < rbio->stripe_nsectors; sectornr++) {
			struct sector_ptr *sector;

			/*
			 * the rmw code may have already read this
			 * page in
			 */
			sector = rbio_stripe_sector(rbio, stripe, sectornr);
			if (sector->uptodate)
				continue;

			ret = rbio_add_io_sector(rbio, &bio_list, sector,
						 stripe, sectornr, rbio->stripe_len,
						 REQ_OP_READ);
			if (ret < 0)
				goto cleanup;
		}
	}

	bios_to_read = bio_list_size(&bio_list);
	if (!bios_to_read) {
		/*
		 * we might have no bios to read just because the pages
		 * were up to date, or we might have no bios to read because
		 * the devices were gone.
		 */
		if (atomic_read(&rbio->error) <= rbio->bioc->max_errors) {
			__raid_recover_end_io(rbio);
			return 0;
		} else {
			goto cleanup;
		}
	}

	/*
	 * The bioc may be freed once we submit the last bio. Make sure not to
	 * touch it after that.
	 */
	atomic_set(&rbio->stripes_pending, bios_to_read);
	while ((bio = bio_list_pop(&bio_list))) {
		bio->bi_end_io = raid_recover_end_io;

		btrfs_bio_wq_end_io(rbio->bioc->fs_info, bio, BTRFS_WQ_ENDIO_RAID56);

		submit_bio(bio);
	}

	return 0;

cleanup:
	if (rbio->operation == BTRFS_RBIO_READ_REBUILD ||
	    rbio->operation == BTRFS_RBIO_REBUILD_MISSING)
		rbio_orig_end_io(rbio, BLK_STS_IOERR);

	while ((bio = bio_list_pop(&bio_list)))
		bio_put(bio);

	return -EIO;
}

/*
 * the main entry point for reads from the higher layers.  This
 * is really only called when the normal read path had a failure,
 * so we assume the bio they send down corresponds to a failed part
 * of the drive.
 */
int raid56_parity_recover(struct bio *bio, struct btrfs_io_context *bioc,
			  u32 stripe_len, int mirror_num, int generic_io)
{
	struct btrfs_fs_info *fs_info = bioc->fs_info;
	struct btrfs_raid_bio *rbio;
	int ret;

	if (generic_io) {
		ASSERT(bioc->mirror_num == mirror_num);
		btrfs_bio(bio)->mirror_num = mirror_num;
	}

	rbio = alloc_rbio(fs_info, bioc, stripe_len);
	if (IS_ERR(rbio)) {
		if (generic_io)
			btrfs_put_bioc(bioc);
		return PTR_ERR(rbio);
	}

	rbio->operation = BTRFS_RBIO_READ_REBUILD;
	bio_list_add(&rbio->bio_list, bio);
	rbio->bio_list_bytes = bio->bi_iter.bi_size;

	rbio->faila = find_logical_bio_stripe(rbio, bio);
	if (rbio->faila == -1) {
		btrfs_warn(fs_info,
"%s could not find the bad stripe in raid56 so that we cannot recover any more (bio has logical %llu len %llu, bioc has map_type %llu)",
			   __func__, bio->bi_iter.bi_sector << 9,
			   (u64)bio->bi_iter.bi_size, bioc->map_type);
		if (generic_io)
			btrfs_put_bioc(bioc);
		kfree(rbio);
		return -EIO;
	}

	if (generic_io) {
		btrfs_bio_counter_inc_noblocked(fs_info);
		rbio->generic_bio_cnt = 1;
	} else {
		btrfs_get_bioc(bioc);
	}

	/*
	 * Loop retry:
	 * for 'mirror == 2', reconstruct from all other stripes.
	 * for 'mirror_num > 2', select a stripe to fail on every retry.
	 */
	if (mirror_num > 2) {
		/*
		 * 'mirror == 3' is to fail the p stripe and
		 * reconstruct from the q stripe.  'mirror > 3' is to
		 * fail a data stripe and reconstruct from p+q stripe.
		 */
		rbio->failb = rbio->real_stripes - (mirror_num - 1);
		ASSERT(rbio->failb > 0);
		if (rbio->failb <= rbio->faila)
			rbio->failb--;
	}

	ret = lock_stripe_add(rbio);

	/*
	 * __raid56_parity_recover will end the bio with
	 * any errors it hits.  We don't want to return
	 * its error value up the stack because our caller
	 * will end up calling bio_endio with any nonzero
	 * return
	 */
	if (ret == 0)
		__raid56_parity_recover(rbio);
	/*
	 * our rbio has been added to the list of
	 * rbios that will be handled after the
	 * currently lock owner is done
	 */
	return 0;

}

static void rmw_work(struct work_struct *work)
{
	struct btrfs_raid_bio *rbio;

	rbio = container_of(work, struct btrfs_raid_bio, work);
	raid56_rmw_stripe(rbio);
}

static void read_rebuild_work(struct work_struct *work)
{
	struct btrfs_raid_bio *rbio;

	rbio = container_of(work, struct btrfs_raid_bio, work);
	__raid56_parity_recover(rbio);
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
				u32 stripe_len, struct btrfs_device *scrub_dev,
				unsigned long *dbitmap, int stripe_nsectors)
{
	struct btrfs_fs_info *fs_info = bioc->fs_info;
	struct btrfs_raid_bio *rbio;
	int i;

	rbio = alloc_rbio(fs_info, bioc, stripe_len);
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

	bitmap_copy(rbio->dbitmap, dbitmap, stripe_nsectors);

	/*
	 * We have already increased bio_counter when getting bioc, record it
	 * so we can free it at rbio_orig_end_io().
	 */
	rbio->generic_bio_cnt = 1;

	return rbio;
}

/* Used for both parity scrub and missing. */
void raid56_add_scrub_pages(struct btrfs_raid_bio *rbio, struct page *page,
			    unsigned int pgoff, u64 logical)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	int stripe_offset;
	int index;

	ASSERT(logical >= rbio->bioc->raid_map[0]);
	ASSERT(logical + sectorsize <= rbio->bioc->raid_map[0] +
				rbio->stripe_len * rbio->nr_data);
	stripe_offset = (int)(logical - rbio->bioc->raid_map[0]);
	index = stripe_offset / sectorsize;
	rbio->bio_sectors[index].page = page;
	rbio->bio_sectors[index].pgoff = pgoff;
}

/*
 * We just scrub the parity that we have correct data on the same horizontal,
 * so we needn't allocate all pages for all the stripes.
 */
static int alloc_rbio_essential_pages(struct btrfs_raid_bio *rbio)
{
	const u32 sectorsize = rbio->bioc->fs_info->sectorsize;
	int stripe;
	int sectornr;

	for_each_set_bit(sectornr, rbio->dbitmap, rbio->stripe_nsectors) {
		for (stripe = 0; stripe < rbio->real_stripes; stripe++) {
			struct page *page;
			int index = (stripe * rbio->stripe_nsectors + sectornr) *
				    sectorsize >> PAGE_SHIFT;

			if (rbio->stripe_pages[index])
				continue;

			page = alloc_page(GFP_NOFS);
			if (!page)
				return -ENOMEM;
			rbio->stripe_pages[index] = page;
		}
	}
	index_stripe_sectors(rbio);
	return 0;
}

static noinline void finish_parity_scrub(struct btrfs_raid_bio *rbio,
					 int need_check)
{
	struct btrfs_io_context *bioc = rbio->bioc;
	const u32 sectorsize = bioc->fs_info->sectorsize;
	void **pointers = rbio->finish_pointers;
	unsigned long *pbitmap = rbio->finish_pbitmap;
	int nr_data = rbio->nr_data;
	int stripe;
	int sectornr;
	bool has_qstripe;
	struct sector_ptr p_sector = { 0 };
	struct sector_ptr q_sector = { 0 };
	struct bio_list bio_list;
	struct bio *bio;
	int is_replace = 0;
	int ret;

	bio_list_init(&bio_list);

	if (rbio->real_stripes - rbio->nr_data == 1)
		has_qstripe = false;
	else if (rbio->real_stripes - rbio->nr_data == 2)
		has_qstripe = true;
	else
		BUG();

	if (bioc->num_tgtdevs && bioc->tgtdev_map[rbio->scrubp]) {
		is_replace = 1;
		bitmap_copy(pbitmap, rbio->dbitmap, rbio->stripe_nsectors);
	}

	/*
	 * Because the higher layers(scrubber) are unlikely to
	 * use this area of the disk again soon, so don't cache
	 * it.
	 */
	clear_bit(RBIO_CACHE_READY_BIT, &rbio->flags);

	if (!need_check)
		goto writeback;

	p_sector.page = alloc_page(GFP_NOFS);
	if (!p_sector.page)
		goto cleanup;
	p_sector.pgoff = 0;
	p_sector.uptodate = 1;

	if (has_qstripe) {
		/* RAID6, allocate and map temp space for the Q stripe */
		q_sector.page = alloc_page(GFP_NOFS);
		if (!q_sector.page) {
			__free_page(p_sector.page);
			p_sector.page = NULL;
			goto cleanup;
		}
		q_sector.pgoff = 0;
		q_sector.uptodate = 1;
		pointers[rbio->real_stripes - 1] = kmap_local_page(q_sector.page);
	}

	atomic_set(&rbio->error, 0);

	/* Map the parity stripe just once */
	pointers[nr_data] = kmap_local_page(p_sector.page);

	for_each_set_bit(sectornr, rbio->dbitmap, rbio->stripe_nsectors) {
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
			bitmap_clear(rbio->dbitmap, sectornr, 1);
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

writeback:
	/*
	 * time to start writing.  Make bios for everything from the
	 * higher layers (the bio_list in our rbio) and our p/q.  Ignore
	 * everything else.
	 */
	for_each_set_bit(sectornr, rbio->dbitmap, rbio->stripe_nsectors) {
		struct sector_ptr *sector;

		sector = rbio_stripe_sector(rbio, rbio->scrubp, sectornr);
		ret = rbio_add_io_sector(rbio, &bio_list, sector, rbio->scrubp,
					 sectornr, rbio->stripe_len, REQ_OP_WRITE);
		if (ret)
			goto cleanup;
	}

	if (!is_replace)
		goto submit_write;

	for_each_set_bit(sectornr, pbitmap, rbio->stripe_nsectors) {
		struct sector_ptr *sector;

		sector = rbio_stripe_sector(rbio, rbio->scrubp, sectornr);
		ret = rbio_add_io_sector(rbio, &bio_list, sector,
				       bioc->tgtdev_map[rbio->scrubp],
				       sectornr, rbio->stripe_len, REQ_OP_WRITE);
		if (ret)
			goto cleanup;
	}

submit_write:
	nr_data = bio_list_size(&bio_list);
	if (!nr_data) {
		/* Every parity is right */
		rbio_orig_end_io(rbio, BLK_STS_OK);
		return;
	}

	atomic_set(&rbio->stripes_pending, nr_data);

	while ((bio = bio_list_pop(&bio_list))) {
		bio->bi_end_io = raid_write_end_io;

		submit_bio(bio);
	}
	return;

cleanup:
	rbio_orig_end_io(rbio, BLK_STS_IOERR);

	while ((bio = bio_list_pop(&bio_list)))
		bio_put(bio);
}

static inline int is_data_stripe(struct btrfs_raid_bio *rbio, int stripe)
{
	if (stripe >= 0 && stripe < rbio->nr_data)
		return 1;
	return 0;
}

/*
 * While we're doing the parity check and repair, we could have errors
 * in reading pages off the disk.  This checks for errors and if we're
 * not able to read the page it'll trigger parity reconstruction.  The
 * parity scrub will be finished after we've reconstructed the failed
 * stripes
 */
static void validate_rbio_for_parity_scrub(struct btrfs_raid_bio *rbio)
{
	if (atomic_read(&rbio->error) > rbio->bioc->max_errors)
		goto cleanup;

	if (rbio->faila >= 0 || rbio->failb >= 0) {
		int dfail = 0, failp = -1;

		if (is_data_stripe(rbio, rbio->faila))
			dfail++;
		else if (is_parity_stripe(rbio->faila))
			failp = rbio->faila;

		if (is_data_stripe(rbio, rbio->failb))
			dfail++;
		else if (is_parity_stripe(rbio->failb))
			failp = rbio->failb;

		/*
		 * Because we can not use a scrubbing parity to repair
		 * the data, so the capability of the repair is declined.
		 * (In the case of RAID5, we can not repair anything)
		 */
		if (dfail > rbio->bioc->max_errors - 1)
			goto cleanup;

		/*
		 * If all data is good, only parity is correctly, just
		 * repair the parity.
		 */
		if (dfail == 0) {
			finish_parity_scrub(rbio, 0);
			return;
		}

		/*
		 * Here means we got one corrupted data stripe and one
		 * corrupted parity on RAID6, if the corrupted parity
		 * is scrubbing parity, luckily, use the other one to repair
		 * the data, or we can not repair the data stripe.
		 */
		if (failp != rbio->scrubp)
			goto cleanup;

		__raid_recover_end_io(rbio);
	} else {
		finish_parity_scrub(rbio, 1);
	}
	return;

cleanup:
	rbio_orig_end_io(rbio, BLK_STS_IOERR);
}

/*
 * end io for the read phase of the rmw cycle.  All the bios here are physical
 * stripe bios we've read from the disk so we can recalculate the parity of the
 * stripe.
 *
 * This will usually kick off finish_rmw once all the bios are read in, but it
 * may trigger parity reconstruction if we had any errors along the way
 */
static void raid56_parity_scrub_end_io(struct bio *bio)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;

	if (bio->bi_status)
		fail_bio_stripe(rbio, bio);
	else
		set_bio_pages_uptodate(rbio, bio);

	bio_put(bio);

	if (!atomic_dec_and_test(&rbio->stripes_pending))
		return;

	/*
	 * this will normally call finish_rmw to start our write
	 * but if there are any failed stripes we'll reconstruct
	 * from parity first
	 */
	validate_rbio_for_parity_scrub(rbio);
}

static void raid56_parity_scrub_stripe(struct btrfs_raid_bio *rbio)
{
	int bios_to_read = 0;
	struct bio_list bio_list;
	int ret;
	int sectornr;
	int stripe;
	struct bio *bio;

	bio_list_init(&bio_list);

	ret = alloc_rbio_essential_pages(rbio);
	if (ret)
		goto cleanup;

	atomic_set(&rbio->error, 0);
	/*
	 * build a list of bios to read all the missing parts of this
	 * stripe
	 */
	for (stripe = 0; stripe < rbio->real_stripes; stripe++) {
		for_each_set_bit(sectornr , rbio->dbitmap, rbio->stripe_nsectors) {
			struct sector_ptr *sector;
			/*
			 * We want to find all the sectors missing from the
			 * rbio and read them from the disk.  If * sector_in_rbio()
			 * finds a sector in the bio list we don't need to read
			 * it off the stripe.
			 */
			sector = sector_in_rbio(rbio, stripe, sectornr, 1);
			if (sector)
				continue;

			sector = rbio_stripe_sector(rbio, stripe, sectornr);
			/*
			 * The bio cache may have handed us an uptodate sector.
			 * If so, be happy and use it.
			 */
			if (sector->uptodate)
				continue;

			ret = rbio_add_io_sector(rbio, &bio_list, sector,
						 stripe, sectornr, rbio->stripe_len,
						 REQ_OP_READ);
			if (ret)
				goto cleanup;
		}
	}

	bios_to_read = bio_list_size(&bio_list);
	if (!bios_to_read) {
		/*
		 * this can happen if others have merged with
		 * us, it means there is nothing left to read.
		 * But if there are missing devices it may not be
		 * safe to do the full stripe write yet.
		 */
		goto finish;
	}

	/*
	 * The bioc may be freed once we submit the last bio. Make sure not to
	 * touch it after that.
	 */
	atomic_set(&rbio->stripes_pending, bios_to_read);
	while ((bio = bio_list_pop(&bio_list))) {
		bio->bi_end_io = raid56_parity_scrub_end_io;

		btrfs_bio_wq_end_io(rbio->bioc->fs_info, bio, BTRFS_WQ_ENDIO_RAID56);

		submit_bio(bio);
	}
	/* the actual write will happen once the reads are done */
	return;

cleanup:
	rbio_orig_end_io(rbio, BLK_STS_IOERR);

	while ((bio = bio_list_pop(&bio_list)))
		bio_put(bio);

	return;

finish:
	validate_rbio_for_parity_scrub(rbio);
}

static void scrub_parity_work(struct work_struct *work)
{
	struct btrfs_raid_bio *rbio;

	rbio = container_of(work, struct btrfs_raid_bio, work);
	raid56_parity_scrub_stripe(rbio);
}

void raid56_parity_submit_scrub_rbio(struct btrfs_raid_bio *rbio)
{
	if (!lock_stripe_add(rbio))
		start_async_work(rbio, scrub_parity_work);
}

/* The following code is used for dev replace of a missing RAID 5/6 device. */

struct btrfs_raid_bio *
raid56_alloc_missing_rbio(struct bio *bio, struct btrfs_io_context *bioc,
			  u64 length)
{
	struct btrfs_fs_info *fs_info = bioc->fs_info;
	struct btrfs_raid_bio *rbio;

	rbio = alloc_rbio(fs_info, bioc, length);
	if (IS_ERR(rbio))
		return NULL;

	rbio->operation = BTRFS_RBIO_REBUILD_MISSING;
	bio_list_add(&rbio->bio_list, bio);
	/*
	 * This is a special bio which is used to hold the completion handler
	 * and make the scrub rbio is similar to the other types
	 */
	ASSERT(!bio->bi_iter.bi_size);

	rbio->faila = find_logical_bio_stripe(rbio, bio);
	if (rbio->faila == -1) {
		BUG();
		kfree(rbio);
		return NULL;
	}

	/*
	 * When we get bioc, we have already increased bio_counter, record it
	 * so we can free it at rbio_orig_end_io()
	 */
	rbio->generic_bio_cnt = 1;

	return rbio;
}

void raid56_submit_missing_rbio(struct btrfs_raid_bio *rbio)
{
	if (!lock_stripe_add(rbio))
		start_async_work(rbio, read_rebuild_work);
}
