/*
 * Copyright (C) 2012 Fusion-io  All rights reserved.
 * Copyright (C) 2012 Intel Corp. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/random.h>
#include <linux/iocontext.h>
#include <linux/capability.h>
#include <linux/ratelimit.h>
#include <linux/kthread.h>
#include <linux/raid/pq.h>
#include <linux/hash.h>
#include <linux/list_sort.h>
#include <linux/raid/xor.h>
#include <linux/vmalloc.h>
#include <asm/div64.h>
#include "ctree.h"
#include "extent_map.h"
#include "disk-io.h"
#include "transaction.h"
#include "print-tree.h"
#include "volumes.h"
#include "raid56.h"
#include "async-thread.h"
#include "check-integrity.h"
#include "rcu-string.h"

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

struct btrfs_raid_bio {
	struct btrfs_fs_info *fs_info;
	struct btrfs_bio *bbio;

	/*
	 * logical block numbers for the start of each stripe
	 * The last one or two are p/q.  These are sorted,
	 * so raid_map[0] is the start of our full stripe
	 */
	u64 *raid_map;

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
	struct btrfs_work work;

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

	/* size of each individual stripe on disk */
	int stripe_len;

	/* number of data stripes (no p/q) */
	int nr_data;

	/*
	 * set if we're doing a parity rebuild
	 * for a read from higher up, which is handled
	 * differently from a parity rebuild as part of
	 * rmw
	 */
	int read_rebuild;

	/* first bad stripe */
	int faila;

	/* second bad stripe (for raid6 use) */
	int failb;

	/*
	 * number of pages needed to represent the full
	 * stripe
	 */
	int nr_pages;

	/*
	 * size of all the bios in the bio_list.  This
	 * helps us decide if the rbio maps to a full
	 * stripe or not
	 */
	int bio_list_bytes;

	atomic_t refs;

	/*
	 * these are two arrays of pointers.  We allocate the
	 * rbio big enough to hold them both and setup their
	 * locations when the rbio is allocated
	 */

	/* pointers to pages that we allocated for
	 * reading/writing stripes directly from the disk (including P/Q)
	 */
	struct page **stripe_pages;

	/*
	 * pointers to the pages in the bio_list.  Stored
	 * here for faster lookup
	 */
	struct page **bio_pages;
};

static int __raid56_parity_recover(struct btrfs_raid_bio *rbio);
static noinline void finish_rmw(struct btrfs_raid_bio *rbio);
static void rmw_work(struct btrfs_work *work);
static void read_rebuild_work(struct btrfs_work *work);
static void async_rmw_stripe(struct btrfs_raid_bio *rbio);
static void async_read_rebuild(struct btrfs_raid_bio *rbio);
static int fail_bio_stripe(struct btrfs_raid_bio *rbio, struct bio *bio);
static int fail_rbio_index(struct btrfs_raid_bio *rbio, int failed);
static void __free_raid_bio(struct btrfs_raid_bio *rbio);
static void index_rbio_pages(struct btrfs_raid_bio *rbio);
static int alloc_rbio_pages(struct btrfs_raid_bio *rbio);

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
	int table_size;

	if (info->stripe_hash_table)
		return 0;

	/*
	 * The table is large, starting with order 4 and can go as high as
	 * order 7 in case lock debugging is turned on.
	 *
	 * Try harder to allocate and fallback to vmalloc to lower the chance
	 * of a failing mount.
	 */
	table_size = sizeof(*table) + sizeof(*h) * num_entries;
	table = kzalloc(table_size, GFP_KERNEL | __GFP_NOWARN | __GFP_REPEAT);
	if (!table) {
		table = vzalloc(table_size);
		if (!table)
			return -ENOMEM;
	}

	spin_lock_init(&table->cache_lock);
	INIT_LIST_HEAD(&table->stripe_cache);

	h = table->table;

	for (i = 0; i < num_entries; i++) {
		cur = h + i;
		INIT_LIST_HEAD(&cur->hash_list);
		spin_lock_init(&cur->lock);
		init_waitqueue_head(&cur->wait);
	}

	x = cmpxchg(&info->stripe_hash_table, NULL, table);
	if (x) {
		if (is_vmalloc_addr(x))
			vfree(x);
		else
			kfree(x);
	}
	return 0;
}

/*
 * caching an rbio means to copy anything from the
 * bio_pages array into the stripe_pages array.  We
 * use the page uptodate bit in the stripe cache array
 * to indicate if it has valid data
 *
 * once the caching is done, we set the cache ready
 * bit.
 */
static void cache_rbio_pages(struct btrfs_raid_bio *rbio)
{
	int i;
	char *s;
	char *d;
	int ret;

	ret = alloc_rbio_pages(rbio);
	if (ret)
		return;

	for (i = 0; i < rbio->nr_pages; i++) {
		if (!rbio->bio_pages[i])
			continue;

		s = kmap(rbio->bio_pages[i]);
		d = kmap(rbio->stripe_pages[i]);

		memcpy(d, s, PAGE_CACHE_SIZE);

		kunmap(rbio->bio_pages[i]);
		kunmap(rbio->stripe_pages[i]);
		SetPageUptodate(rbio->stripe_pages[i]);
	}
	set_bit(RBIO_CACHE_READY_BIT, &rbio->flags);
}

/*
 * we hash on the first logical address of the stripe
 */
static int rbio_bucket(struct btrfs_raid_bio *rbio)
{
	u64 num = rbio->raid_map[0];

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

/*
 * stealing an rbio means taking all the uptodate pages from the stripe
 * array in the source rbio and putting them into the destination rbio
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
		if (!s || !PageUptodate(s)) {
			continue;
		}

		d = dest->stripe_pages[i];
		if (d)
			__free_page(d);

		dest->stripe_pages[i] = s;
		src->stripe_pages[i] = NULL;
	}
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

	table = rbio->fs_info->stripe_hash_table;
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
				atomic_dec(&rbio->refs);
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

	table = rbio->fs_info->stripe_hash_table;

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
	if (is_vmalloc_addr(info->stripe_hash_table))
		vfree(info->stripe_hash_table);
	else
		kfree(info->stripe_hash_table);
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

	table = rbio->fs_info->stripe_hash_table;

	spin_lock_irqsave(&table->cache_lock, flags);
	spin_lock(&rbio->bio_list_lock);

	/* bump our ref if we were not in the list before */
	if (!test_and_set_bit(RBIO_CACHE_BIT, &rbio->flags))
		atomic_inc(&rbio->refs);

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
	return;
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
 * returns true if the bio list inside this rbio
 * covers an entire stripe (no rmw required).
 * Must be called with the bio list lock held, or
 * at a time when you know it is impossible to add
 * new bios into the list
 */
static int __rbio_is_full(struct btrfs_raid_bio *rbio)
{
	unsigned long size = rbio->bio_list_bytes;
	int ret = 1;

	if (size != rbio->nr_data * rbio->stripe_len)
		ret = 0;

	BUG_ON(size > rbio->nr_data * rbio->stripe_len);
	return ret;
}

static int rbio_is_full(struct btrfs_raid_bio *rbio)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&rbio->bio_list_lock, flags);
	ret = __rbio_is_full(rbio);
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
	 * steal from cached rbio's though, other functions
	 * handle that.
	 */
	if (test_bit(RBIO_CACHE_BIT, &last->flags) ||
	    test_bit(RBIO_CACHE_BIT, &cur->flags))
		return 0;

	if (last->raid_map[0] !=
	    cur->raid_map[0])
		return 0;

	/* reads can't merge with writes */
	if (last->read_rebuild !=
	    cur->read_rebuild) {
		return 0;
	}

	return 1;
}

/*
 * helper to index into the pstripe
 */
static struct page *rbio_pstripe_page(struct btrfs_raid_bio *rbio, int index)
{
	index += (rbio->nr_data * rbio->stripe_len) >> PAGE_CACHE_SHIFT;
	return rbio->stripe_pages[index];
}

/*
 * helper to index into the qstripe, returns null
 * if there is no qstripe
 */
static struct page *rbio_qstripe_page(struct btrfs_raid_bio *rbio, int index)
{
	if (rbio->nr_data + 1 == rbio->bbio->num_stripes)
		return NULL;

	index += ((rbio->nr_data + 1) * rbio->stripe_len) >>
		PAGE_CACHE_SHIFT;
	return rbio->stripe_pages[index];
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
	int bucket = rbio_bucket(rbio);
	struct btrfs_stripe_hash *h = rbio->fs_info->stripe_hash_table->table + bucket;
	struct btrfs_raid_bio *cur;
	struct btrfs_raid_bio *pending;
	unsigned long flags;
	DEFINE_WAIT(wait);
	struct btrfs_raid_bio *freeit = NULL;
	struct btrfs_raid_bio *cache_drop = NULL;
	int ret = 0;
	int walk = 0;

	spin_lock_irqsave(&h->lock, flags);
	list_for_each_entry(cur, &h->hash_list, hash_list) {
		walk++;
		if (cur->raid_map[0] == rbio->raid_map[0]) {
			spin_lock(&cur->bio_list_lock);

			/* can we steal this cached rbio's pages? */
			if (bio_list_empty(&cur->bio_list) &&
			    list_empty(&cur->plug_list) &&
			    test_bit(RBIO_CACHE_BIT, &cur->flags) &&
			    !test_bit(RBIO_RMW_LOCKED_BIT, &cur->flags)) {
				list_del_init(&cur->hash_list);
				atomic_dec(&cur->refs);

				steal_rbio(cur, rbio);
				cache_drop = cur;
				spin_unlock(&cur->bio_list_lock);

				goto lockit;
			}

			/* can we merge into the lock owner? */
			if (rbio_can_merge(cur, rbio)) {
				merge_rbio(cur, rbio);
				spin_unlock(&cur->bio_list_lock);
				freeit = rbio;
				ret = 1;
				goto out;
			}


			/*
			 * we couldn't merge with the running
			 * rbio, see if we can merge with the
			 * pending ones.  We don't have to
			 * check for rmw_locked because there
			 * is no way they are inside finish_rmw
			 * right now
			 */
			list_for_each_entry(pending, &cur->plug_list,
					    plug_list) {
				if (rbio_can_merge(pending, rbio)) {
					merge_rbio(pending, rbio);
					spin_unlock(&cur->bio_list_lock);
					freeit = rbio;
					ret = 1;
					goto out;
				}
			}

			/* no merging, put us on the tail of the plug list,
			 * our rbio will be started with the currently
			 * running rbio unlocks
			 */
			list_add_tail(&rbio->plug_list, &cur->plug_list);
			spin_unlock(&cur->bio_list_lock);
			ret = 1;
			goto out;
		}
	}
lockit:
	atomic_inc(&rbio->refs);
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
	h = rbio->fs_info->stripe_hash_table->table + bucket;

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
		atomic_dec(&rbio->refs);

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
			atomic_inc(&next->refs);
			spin_unlock(&rbio->bio_list_lock);
			spin_unlock_irqrestore(&h->lock, flags);

			if (next->read_rebuild)
				async_read_rebuild(next);
			else {
				steal_rbio(rbio, next);
				async_rmw_stripe(next);
			}

			goto done_nolock;
		} else  if (waitqueue_active(&h->wait)) {
			spin_unlock(&rbio->bio_list_lock);
			spin_unlock_irqrestore(&h->lock, flags);
			wake_up(&h->wait);
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

	WARN_ON(atomic_read(&rbio->refs) < 0);
	if (!atomic_dec_and_test(&rbio->refs))
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
	kfree(rbio->raid_map);
	kfree(rbio->bbio);
	kfree(rbio);
}

static void free_raid_bio(struct btrfs_raid_bio *rbio)
{
	unlock_stripe(rbio);
	__free_raid_bio(rbio);
}

/*
 * this frees the rbio and runs through all the bios in the
 * bio_list and calls end_io on them
 */
static void rbio_orig_end_io(struct btrfs_raid_bio *rbio, int err, int uptodate)
{
	struct bio *cur = bio_list_get(&rbio->bio_list);
	struct bio *next;
	free_raid_bio(rbio);

	while (cur) {
		next = cur->bi_next;
		cur->bi_next = NULL;
		if (uptodate)
			set_bit(BIO_UPTODATE, &cur->bi_flags);
		bio_endio(cur, err);
		cur = next;
	}
}

/*
 * end io function used by finish_rmw.  When we finally
 * get here, we've written a full stripe
 */
static void raid_write_end_io(struct bio *bio, int err)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;

	if (err)
		fail_bio_stripe(rbio, bio);

	bio_put(bio);

	if (!atomic_dec_and_test(&rbio->bbio->stripes_pending))
		return;

	err = 0;

	/* OK, we have read all the stripes we need to. */
	if (atomic_read(&rbio->bbio->error) > rbio->bbio->max_errors)
		err = -EIO;

	rbio_orig_end_io(rbio, err, 0);
	return;
}

/*
 * the read/modify/write code wants to use the original bio for
 * any pages it included, and then use the rbio for everything
 * else.  This function decides if a given index (stripe number)
 * and page number in that stripe fall inside the original bio
 * or the rbio.
 *
 * if you set bio_list_only, you'll get a NULL back for any ranges
 * that are outside the bio_list
 *
 * This doesn't take any refs on anything, you get a bare page pointer
 * and the caller must bump refs as required.
 *
 * You must call index_rbio_pages once before you can trust
 * the answers from this function.
 */
static struct page *page_in_rbio(struct btrfs_raid_bio *rbio,
				 int index, int pagenr, int bio_list_only)
{
	int chunk_page;
	struct page *p = NULL;

	chunk_page = index * (rbio->stripe_len >> PAGE_SHIFT) + pagenr;

	spin_lock_irq(&rbio->bio_list_lock);
	p = rbio->bio_pages[chunk_page];
	spin_unlock_irq(&rbio->bio_list_lock);

	if (p || bio_list_only)
		return p;

	return rbio->stripe_pages[chunk_page];
}

/*
 * number of pages we need for the entire stripe across all the
 * drives
 */
static unsigned long rbio_nr_pages(unsigned long stripe_len, int nr_stripes)
{
	unsigned long nr = stripe_len * nr_stripes;
	return (nr + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
}

/*
 * allocation and initial setup for the btrfs_raid_bio.  Not
 * this does not allocate any pages for rbio->pages.
 */
static struct btrfs_raid_bio *alloc_rbio(struct btrfs_root *root,
			  struct btrfs_bio *bbio, u64 *raid_map,
			  u64 stripe_len)
{
	struct btrfs_raid_bio *rbio;
	int nr_data = 0;
	int num_pages = rbio_nr_pages(stripe_len, bbio->num_stripes);
	void *p;

	rbio = kzalloc(sizeof(*rbio) + num_pages * sizeof(struct page *) * 2,
			GFP_NOFS);
	if (!rbio) {
		kfree(raid_map);
		kfree(bbio);
		return ERR_PTR(-ENOMEM);
	}

	bio_list_init(&rbio->bio_list);
	INIT_LIST_HEAD(&rbio->plug_list);
	spin_lock_init(&rbio->bio_list_lock);
	INIT_LIST_HEAD(&rbio->stripe_cache);
	INIT_LIST_HEAD(&rbio->hash_list);
	rbio->bbio = bbio;
	rbio->raid_map = raid_map;
	rbio->fs_info = root->fs_info;
	rbio->stripe_len = stripe_len;
	rbio->nr_pages = num_pages;
	rbio->faila = -1;
	rbio->failb = -1;
	atomic_set(&rbio->refs, 1);

	/*
	 * the stripe_pages and bio_pages array point to the extra
	 * memory we allocated past the end of the rbio
	 */
	p = rbio + 1;
	rbio->stripe_pages = p;
	rbio->bio_pages = p + sizeof(struct page *) * num_pages;

	if (raid_map[bbio->num_stripes - 1] == RAID6_Q_STRIPE)
		nr_data = bbio->num_stripes - 2;
	else
		nr_data = bbio->num_stripes - 1;

	rbio->nr_data = nr_data;
	return rbio;
}

/* allocate pages for all the stripes in the bio, including parity */
static int alloc_rbio_pages(struct btrfs_raid_bio *rbio)
{
	int i;
	struct page *page;

	for (i = 0; i < rbio->nr_pages; i++) {
		if (rbio->stripe_pages[i])
			continue;
		page = alloc_page(GFP_NOFS | __GFP_HIGHMEM);
		if (!page)
			return -ENOMEM;
		rbio->stripe_pages[i] = page;
		ClearPageUptodate(page);
	}
	return 0;
}

/* allocate pages for just the p/q stripes */
static int alloc_rbio_parity_pages(struct btrfs_raid_bio *rbio)
{
	int i;
	struct page *page;

	i = (rbio->nr_data * rbio->stripe_len) >> PAGE_CACHE_SHIFT;

	for (; i < rbio->nr_pages; i++) {
		if (rbio->stripe_pages[i])
			continue;
		page = alloc_page(GFP_NOFS | __GFP_HIGHMEM);
		if (!page)
			return -ENOMEM;
		rbio->stripe_pages[i] = page;
	}
	return 0;
}

/*
 * add a single page from a specific stripe into our list of bios for IO
 * this will try to merge into existing bios if possible, and returns
 * zero if all went well.
 */
static int rbio_add_io_page(struct btrfs_raid_bio *rbio,
			    struct bio_list *bio_list,
			    struct page *page,
			    int stripe_nr,
			    unsigned long page_index,
			    unsigned long bio_max_len)
{
	struct bio *last = bio_list->tail;
	u64 last_end = 0;
	int ret;
	struct bio *bio;
	struct btrfs_bio_stripe *stripe;
	u64 disk_start;

	stripe = &rbio->bbio->stripes[stripe_nr];
	disk_start = stripe->physical + (page_index << PAGE_CACHE_SHIFT);

	/* if the device is missing, just fail this stripe */
	if (!stripe->dev->bdev)
		return fail_rbio_index(rbio, stripe_nr);

	/* see if we can add this page onto our existing bio */
	if (last) {
		last_end = (u64)last->bi_sector << 9;
		last_end += last->bi_size;

		/*
		 * we can't merge these if they are from different
		 * devices or if they are not contiguous
		 */
		if (last_end == disk_start && stripe->dev->bdev &&
		    test_bit(BIO_UPTODATE, &last->bi_flags) &&
		    last->bi_bdev == stripe->dev->bdev) {
			ret = bio_add_page(last, page, PAGE_CACHE_SIZE, 0);
			if (ret == PAGE_CACHE_SIZE)
				return 0;
		}
	}

	/* put a new bio on the list */
	bio = btrfs_io_bio_alloc(GFP_NOFS, bio_max_len >> PAGE_SHIFT?:1);
	if (!bio)
		return -ENOMEM;

	bio->bi_size = 0;
	bio->bi_bdev = stripe->dev->bdev;
	bio->bi_sector = disk_start >> 9;
	set_bit(BIO_UPTODATE, &bio->bi_flags);

	bio_add_page(bio, page, PAGE_CACHE_SIZE, 0);
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
		BUG_ON(rbio->faila == rbio->bbio->num_stripes - 1);
		__raid56_parity_recover(rbio);
	} else {
		finish_rmw(rbio);
	}
}

/*
 * these are just the pages from the rbio array, not from anything
 * the FS sent down to us
 */
static struct page *rbio_stripe_page(struct btrfs_raid_bio *rbio, int stripe, int page)
{
	int index;
	index = stripe * (rbio->stripe_len >> PAGE_CACHE_SHIFT);
	index += page;
	return rbio->stripe_pages[index];
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
	u64 start;
	unsigned long stripe_offset;
	unsigned long page_index;
	struct page *p;
	int i;

	spin_lock_irq(&rbio->bio_list_lock);
	bio_list_for_each(bio, &rbio->bio_list) {
		start = (u64)bio->bi_sector << 9;
		stripe_offset = start - rbio->raid_map[0];
		page_index = stripe_offset >> PAGE_CACHE_SHIFT;

		for (i = 0; i < bio->bi_vcnt; i++) {
			p = bio->bi_io_vec[i].bv_page;
			rbio->bio_pages[page_index + i] = p;
		}
	}
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
	struct btrfs_bio *bbio = rbio->bbio;
	void *pointers[bbio->num_stripes];
	int stripe_len = rbio->stripe_len;
	int nr_data = rbio->nr_data;
	int stripe;
	int pagenr;
	int p_stripe = -1;
	int q_stripe = -1;
	struct bio_list bio_list;
	struct bio *bio;
	int pages_per_stripe = stripe_len >> PAGE_CACHE_SHIFT;
	int ret;

	bio_list_init(&bio_list);

	if (bbio->num_stripes - rbio->nr_data == 1) {
		p_stripe = bbio->num_stripes - 1;
	} else if (bbio->num_stripes - rbio->nr_data == 2) {
		p_stripe = bbio->num_stripes - 2;
		q_stripe = bbio->num_stripes - 1;
	} else {
		BUG();
	}

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

	atomic_set(&rbio->bbio->error, 0);

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

	for (pagenr = 0; pagenr < pages_per_stripe; pagenr++) {
		struct page *p;
		/* first collect one page from each data stripe */
		for (stripe = 0; stripe < nr_data; stripe++) {
			p = page_in_rbio(rbio, stripe, pagenr, 0);
			pointers[stripe] = kmap(p);
		}

		/* then add the parity stripe */
		p = rbio_pstripe_page(rbio, pagenr);
		SetPageUptodate(p);
		pointers[stripe++] = kmap(p);

		if (q_stripe != -1) {

			/*
			 * raid6, add the qstripe and call the
			 * library function to fill in our p/q
			 */
			p = rbio_qstripe_page(rbio, pagenr);
			SetPageUptodate(p);
			pointers[stripe++] = kmap(p);

			raid6_call.gen_syndrome(bbio->num_stripes, PAGE_SIZE,
						pointers);
		} else {
			/* raid5 */
			memcpy(pointers[nr_data], pointers[0], PAGE_SIZE);
			run_xor(pointers + 1, nr_data - 1, PAGE_CACHE_SIZE);
		}


		for (stripe = 0; stripe < bbio->num_stripes; stripe++)
			kunmap(page_in_rbio(rbio, stripe, pagenr, 0));
	}

	/*
	 * time to start writing.  Make bios for everything from the
	 * higher layers (the bio_list in our rbio) and our p/q.  Ignore
	 * everything else.
	 */
	for (stripe = 0; stripe < bbio->num_stripes; stripe++) {
		for (pagenr = 0; pagenr < pages_per_stripe; pagenr++) {
			struct page *page;
			if (stripe < rbio->nr_data) {
				page = page_in_rbio(rbio, stripe, pagenr, 1);
				if (!page)
					continue;
			} else {
			       page = rbio_stripe_page(rbio, stripe, pagenr);
			}

			ret = rbio_add_io_page(rbio, &bio_list,
				       page, stripe, pagenr, rbio->stripe_len);
			if (ret)
				goto cleanup;
		}
	}

	atomic_set(&bbio->stripes_pending, bio_list_size(&bio_list));
	BUG_ON(atomic_read(&bbio->stripes_pending) == 0);

	while (1) {
		bio = bio_list_pop(&bio_list);
		if (!bio)
			break;

		bio->bi_private = rbio;
		bio->bi_end_io = raid_write_end_io;
		BUG_ON(!test_bit(BIO_UPTODATE, &bio->bi_flags));
		submit_bio(WRITE, bio);
	}
	return;

cleanup:
	rbio_orig_end_io(rbio, -EIO, 0);
}

/*
 * helper to find the stripe number for a given bio.  Used to figure out which
 * stripe has failed.  This expects the bio to correspond to a physical disk,
 * so it looks up based on physical sector numbers.
 */
static int find_bio_stripe(struct btrfs_raid_bio *rbio,
			   struct bio *bio)
{
	u64 physical = bio->bi_sector;
	u64 stripe_start;
	int i;
	struct btrfs_bio_stripe *stripe;

	physical <<= 9;

	for (i = 0; i < rbio->bbio->num_stripes; i++) {
		stripe = &rbio->bbio->stripes[i];
		stripe_start = stripe->physical;
		if (physical >= stripe_start &&
		    physical < stripe_start + rbio->stripe_len) {
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
	u64 logical = bio->bi_sector;
	u64 stripe_start;
	int i;

	logical <<= 9;

	for (i = 0; i < rbio->nr_data; i++) {
		stripe_start = rbio->raid_map[i];
		if (logical >= stripe_start &&
		    logical < stripe_start + rbio->stripe_len) {
			return i;
		}
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
		atomic_inc(&rbio->bbio->error);
	} else if (rbio->failb == -1) {
		/* second failure on this rbio */
		rbio->failb = failed;
		atomic_inc(&rbio->bbio->error);
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
 * this sets each page in the bio uptodate.  It should only be used on private
 * rbio pages, nothing that comes in from the higher layers
 */
static void set_bio_pages_uptodate(struct bio *bio)
{
	int i;
	struct page *p;

	for (i = 0; i < bio->bi_vcnt; i++) {
		p = bio->bi_io_vec[i].bv_page;
		SetPageUptodate(p);
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
static void raid_rmw_end_io(struct bio *bio, int err)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;

	if (err)
		fail_bio_stripe(rbio, bio);
	else
		set_bio_pages_uptodate(bio);

	bio_put(bio);

	if (!atomic_dec_and_test(&rbio->bbio->stripes_pending))
		return;

	err = 0;
	if (atomic_read(&rbio->bbio->error) > rbio->bbio->max_errors)
		goto cleanup;

	/*
	 * this will normally call finish_rmw to start our write
	 * but if there are any failed stripes we'll reconstruct
	 * from parity first
	 */
	validate_rbio_for_rmw(rbio);
	return;

cleanup:

	rbio_orig_end_io(rbio, -EIO, 0);
}

static void async_rmw_stripe(struct btrfs_raid_bio *rbio)
{
	rbio->work.flags = 0;
	rbio->work.func = rmw_work;

	btrfs_queue_worker(&rbio->fs_info->rmw_workers,
			   &rbio->work);
}

static void async_read_rebuild(struct btrfs_raid_bio *rbio)
{
	rbio->work.flags = 0;
	rbio->work.func = read_rebuild_work;

	btrfs_queue_worker(&rbio->fs_info->rmw_workers,
			   &rbio->work);
}

/*
 * the stripe must be locked by the caller.  It will
 * unlock after all the writes are done
 */
static int raid56_rmw_stripe(struct btrfs_raid_bio *rbio)
{
	int bios_to_read = 0;
	struct btrfs_bio *bbio = rbio->bbio;
	struct bio_list bio_list;
	int ret;
	int nr_pages = (rbio->stripe_len + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	int pagenr;
	int stripe;
	struct bio *bio;

	bio_list_init(&bio_list);

	ret = alloc_rbio_pages(rbio);
	if (ret)
		goto cleanup;

	index_rbio_pages(rbio);

	atomic_set(&rbio->bbio->error, 0);
	/*
	 * build a list of bios to read all the missing parts of this
	 * stripe
	 */
	for (stripe = 0; stripe < rbio->nr_data; stripe++) {
		for (pagenr = 0; pagenr < nr_pages; pagenr++) {
			struct page *page;
			/*
			 * we want to find all the pages missing from
			 * the rbio and read them from the disk.  If
			 * page_in_rbio finds a page in the bio list
			 * we don't need to read it off the stripe.
			 */
			page = page_in_rbio(rbio, stripe, pagenr, 1);
			if (page)
				continue;

			page = rbio_stripe_page(rbio, stripe, pagenr);
			/*
			 * the bio cache may have handed us an uptodate
			 * page.  If so, be happy and use it
			 */
			if (PageUptodate(page))
				continue;

			ret = rbio_add_io_page(rbio, &bio_list, page,
				       stripe, pagenr, rbio->stripe_len);
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
	 * the bbio may be freed once we submit the last bio.  Make sure
	 * not to touch it after that
	 */
	atomic_set(&bbio->stripes_pending, bios_to_read);
	while (1) {
		bio = bio_list_pop(&bio_list);
		if (!bio)
			break;

		bio->bi_private = rbio;
		bio->bi_end_io = raid_rmw_end_io;

		btrfs_bio_wq_end_io(rbio->fs_info, bio,
				    BTRFS_WQ_ENDIO_RAID56);

		BUG_ON(!test_bit(BIO_UPTODATE, &bio->bi_flags));
		submit_bio(READ, bio);
	}
	/* the actual write will happen once the reads are done */
	return 0;

cleanup:
	rbio_orig_end_io(rbio, -EIO, 0);
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
		async_rmw_stripe(rbio);
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
	struct btrfs_work work;
};

/*
 * rbios on the plug list are sorted for easier merging.
 */
static int plug_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct btrfs_raid_bio *ra = container_of(a, struct btrfs_raid_bio,
						 plug_list);
	struct btrfs_raid_bio *rb = container_of(b, struct btrfs_raid_bio,
						 plug_list);
	u64 a_sector = ra->bio_list.head->bi_sector;
	u64 b_sector = rb->bio_list.head->bi_sector;

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
			/* we have a full stripe, send it down */
			full_stripe_write(cur);
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
static void unplug_work(struct btrfs_work *work)
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
		plug->work.flags = 0;
		plug->work.func = unplug_work;
		btrfs_queue_worker(&plug->info->rmw_workers,
				   &plug->work);
		return;
	}
	run_plug(plug);
}

/*
 * our main entry point for writes from the rest of the FS.
 */
int raid56_parity_write(struct btrfs_root *root, struct bio *bio,
			struct btrfs_bio *bbio, u64 *raid_map,
			u64 stripe_len)
{
	struct btrfs_raid_bio *rbio;
	struct btrfs_plug_cb *plug = NULL;
	struct blk_plug_cb *cb;

	rbio = alloc_rbio(root, bbio, raid_map, stripe_len);
	if (IS_ERR(rbio))
		return PTR_ERR(rbio);
	bio_list_add(&rbio->bio_list, bio);
	rbio->bio_list_bytes = bio->bi_size;

	/*
	 * don't plug on full rbios, just get them out the door
	 * as quickly as we can
	 */
	if (rbio_is_full(rbio))
		return full_stripe_write(rbio);

	cb = blk_check_plugged(btrfs_raid_unplug, root->fs_info,
			       sizeof(*plug));
	if (cb) {
		plug = container_of(cb, struct btrfs_plug_cb, cb);
		if (!plug->info) {
			plug->info = root->fs_info;
			INIT_LIST_HEAD(&plug->rbio_list);
		}
		list_add_tail(&rbio->plug_list, &plug->rbio_list);
	} else {
		return __raid56_parity_write(rbio);
	}
	return 0;
}

/*
 * all parity reconstruction happens here.  We've read in everything
 * we can find from the drives and this does the heavy lifting of
 * sorting the good from the bad.
 */
static void __raid_recover_end_io(struct btrfs_raid_bio *rbio)
{
	int pagenr, stripe;
	void **pointers;
	int faila = -1, failb = -1;
	int nr_pages = (rbio->stripe_len + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	struct page *page;
	int err;
	int i;

	pointers = kzalloc(rbio->bbio->num_stripes * sizeof(void *),
			   GFP_NOFS);
	if (!pointers) {
		err = -ENOMEM;
		goto cleanup_io;
	}

	faila = rbio->faila;
	failb = rbio->failb;

	if (rbio->read_rebuild) {
		spin_lock_irq(&rbio->bio_list_lock);
		set_bit(RBIO_RMW_LOCKED_BIT, &rbio->flags);
		spin_unlock_irq(&rbio->bio_list_lock);
	}

	index_rbio_pages(rbio);

	for (pagenr = 0; pagenr < nr_pages; pagenr++) {
		/* setup our array of pointers with pages
		 * from each stripe
		 */
		for (stripe = 0; stripe < rbio->bbio->num_stripes; stripe++) {
			/*
			 * if we're rebuilding a read, we have to use
			 * pages from the bio list
			 */
			if (rbio->read_rebuild &&
			    (stripe == faila || stripe == failb)) {
				page = page_in_rbio(rbio, stripe, pagenr, 0);
			} else {
				page = rbio_stripe_page(rbio, stripe, pagenr);
			}
			pointers[stripe] = kmap(page);
		}

		/* all raid6 handling here */
		if (rbio->raid_map[rbio->bbio->num_stripes - 1] ==
		    RAID6_Q_STRIPE) {

			/*
			 * single failure, rebuild from parity raid5
			 * style
			 */
			if (failb < 0) {
				if (faila == rbio->nr_data) {
					/*
					 * Just the P stripe has failed, without
					 * a bad data or Q stripe.
					 * TODO, we should redo the xor here.
					 */
					err = -EIO;
					goto cleanup;
				}
				/*
				 * a single failure in raid6 is rebuilt
				 * in the pstripe code below
				 */
				goto pstripe;
			}

			/* make sure our ps and qs are in order */
			if (faila > failb) {
				int tmp = failb;
				failb = faila;
				faila = tmp;
			}

			/* if the q stripe is failed, do a pstripe reconstruction
			 * from the xors.
			 * If both the q stripe and the P stripe are failed, we're
			 * here due to a crc mismatch and we can't give them the
			 * data they want
			 */
			if (rbio->raid_map[failb] == RAID6_Q_STRIPE) {
				if (rbio->raid_map[faila] == RAID5_P_STRIPE) {
					err = -EIO;
					goto cleanup;
				}
				/*
				 * otherwise we have one bad data stripe and
				 * a good P stripe.  raid5!
				 */
				goto pstripe;
			}

			if (rbio->raid_map[failb] == RAID5_P_STRIPE) {
				raid6_datap_recov(rbio->bbio->num_stripes,
						  PAGE_SIZE, faila, pointers);
			} else {
				raid6_2data_recov(rbio->bbio->num_stripes,
						  PAGE_SIZE, faila, failb,
						  pointers);
			}
		} else {
			void *p;

			/* rebuild from P stripe here (raid5 or raid6) */
			BUG_ON(failb != -1);
pstripe:
			/* Copy parity block into failed block to start with */
			memcpy(pointers[faila],
			       pointers[rbio->nr_data],
			       PAGE_CACHE_SIZE);

			/* rearrange the pointer array */
			p = pointers[faila];
			for (stripe = faila; stripe < rbio->nr_data - 1; stripe++)
				pointers[stripe] = pointers[stripe + 1];
			pointers[rbio->nr_data - 1] = p;

			/* xor in the rest */
			run_xor(pointers, rbio->nr_data - 1, PAGE_CACHE_SIZE);
		}
		/* if we're doing this rebuild as part of an rmw, go through
		 * and set all of our private rbio pages in the
		 * failed stripes as uptodate.  This way finish_rmw will
		 * know they can be trusted.  If this was a read reconstruction,
		 * other endio functions will fiddle the uptodate bits
		 */
		if (!rbio->read_rebuild) {
			for (i = 0;  i < nr_pages; i++) {
				if (faila != -1) {
					page = rbio_stripe_page(rbio, faila, i);
					SetPageUptodate(page);
				}
				if (failb != -1) {
					page = rbio_stripe_page(rbio, failb, i);
					SetPageUptodate(page);
				}
			}
		}
		for (stripe = 0; stripe < rbio->bbio->num_stripes; stripe++) {
			/*
			 * if we're rebuilding a read, we have to use
			 * pages from the bio list
			 */
			if (rbio->read_rebuild &&
			    (stripe == faila || stripe == failb)) {
				page = page_in_rbio(rbio, stripe, pagenr, 0);
			} else {
				page = rbio_stripe_page(rbio, stripe, pagenr);
			}
			kunmap(page);
		}
	}

	err = 0;
cleanup:
	kfree(pointers);

cleanup_io:

	if (rbio->read_rebuild) {
		if (err == 0)
			cache_rbio_pages(rbio);
		else
			clear_bit(RBIO_CACHE_READY_BIT, &rbio->flags);

		rbio_orig_end_io(rbio, err, err == 0);
	} else if (err == 0) {
		rbio->faila = -1;
		rbio->failb = -1;
		finish_rmw(rbio);
	} else {
		rbio_orig_end_io(rbio, err, 0);
	}
}

/*
 * This is called only for stripes we've read from disk to
 * reconstruct the parity.
 */
static void raid_recover_end_io(struct bio *bio, int err)
{
	struct btrfs_raid_bio *rbio = bio->bi_private;

	/*
	 * we only read stripe pages off the disk, set them
	 * up to date if there were no errors
	 */
	if (err)
		fail_bio_stripe(rbio, bio);
	else
		set_bio_pages_uptodate(bio);
	bio_put(bio);

	if (!atomic_dec_and_test(&rbio->bbio->stripes_pending))
		return;

	if (atomic_read(&rbio->bbio->error) > rbio->bbio->max_errors)
		rbio_orig_end_io(rbio, -EIO, 0);
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
	struct btrfs_bio *bbio = rbio->bbio;
	struct bio_list bio_list;
	int ret;
	int nr_pages = (rbio->stripe_len + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
	int pagenr;
	int stripe;
	struct bio *bio;

	bio_list_init(&bio_list);

	ret = alloc_rbio_pages(rbio);
	if (ret)
		goto cleanup;

	atomic_set(&rbio->bbio->error, 0);

	/*
	 * read everything that hasn't failed.  Thanks to the
	 * stripe cache, it is possible that some or all of these
	 * pages are going to be uptodate.
	 */
	for (stripe = 0; stripe < bbio->num_stripes; stripe++) {
		if (rbio->faila == stripe ||
		    rbio->failb == stripe)
			continue;

		for (pagenr = 0; pagenr < nr_pages; pagenr++) {
			struct page *p;

			/*
			 * the rmw code may have already read this
			 * page in
			 */
			p = rbio_stripe_page(rbio, stripe, pagenr);
			if (PageUptodate(p))
				continue;

			ret = rbio_add_io_page(rbio, &bio_list,
				       rbio_stripe_page(rbio, stripe, pagenr),
				       stripe, pagenr, rbio->stripe_len);
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
		if (atomic_read(&rbio->bbio->error) <= rbio->bbio->max_errors) {
			__raid_recover_end_io(rbio);
			goto out;
		} else {
			goto cleanup;
		}
	}

	/*
	 * the bbio may be freed once we submit the last bio.  Make sure
	 * not to touch it after that
	 */
	atomic_set(&bbio->stripes_pending, bios_to_read);
	while (1) {
		bio = bio_list_pop(&bio_list);
		if (!bio)
			break;

		bio->bi_private = rbio;
		bio->bi_end_io = raid_recover_end_io;

		btrfs_bio_wq_end_io(rbio->fs_info, bio,
				    BTRFS_WQ_ENDIO_RAID56);

		BUG_ON(!test_bit(BIO_UPTODATE, &bio->bi_flags));
		submit_bio(READ, bio);
	}
out:
	return 0;

cleanup:
	if (rbio->read_rebuild)
		rbio_orig_end_io(rbio, -EIO, 0);
	return -EIO;
}

/*
 * the main entry point for reads from the higher layers.  This
 * is really only called when the normal read path had a failure,
 * so we assume the bio they send down corresponds to a failed part
 * of the drive.
 */
int raid56_parity_recover(struct btrfs_root *root, struct bio *bio,
			  struct btrfs_bio *bbio, u64 *raid_map,
			  u64 stripe_len, int mirror_num)
{
	struct btrfs_raid_bio *rbio;
	int ret;

	rbio = alloc_rbio(root, bbio, raid_map, stripe_len);
	if (IS_ERR(rbio))
		return PTR_ERR(rbio);

	rbio->read_rebuild = 1;
	bio_list_add(&rbio->bio_list, bio);
	rbio->bio_list_bytes = bio->bi_size;

	rbio->faila = find_logical_bio_stripe(rbio, bio);
	if (rbio->faila == -1) {
		BUG();
		kfree(raid_map);
		kfree(bbio);
		kfree(rbio);
		return -EIO;
	}

	/*
	 * reconstruct from the q stripe if they are
	 * asking for mirror 3
	 */
	if (mirror_num == 3)
		rbio->failb = bbio->num_stripes - 2;

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

static void rmw_work(struct btrfs_work *work)
{
	struct btrfs_raid_bio *rbio;

	rbio = container_of(work, struct btrfs_raid_bio, work);
	raid56_rmw_stripe(rbio);
}

static void read_rebuild_work(struct btrfs_work *work)
{
	struct btrfs_raid_bio *rbio;

	rbio = container_of(work, struct btrfs_raid_bio, work);
	__raid56_parity_recover(rbio);
}
