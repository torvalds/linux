// SPDX-License-Identifier: GPL-2.0
/*
 * Primary bucket allocation code
 *
 * Copyright 2012 Google, Inc.
 *
 * Allocation in bcache is done in terms of buckets:
 *
 * Each bucket has associated an 8 bit gen; this gen corresponds to the gen in
 * btree pointers - they must match for the pointer to be considered valid.
 *
 * Thus (assuming a bucket has no dirty data or metadata in it) we can reuse a
 * bucket simply by incrementing its gen.
 *
 * The gens (along with the priorities; it's really the gens are important but
 * the code is named as if it's the priorities) are written in an arbitrary list
 * of buckets on disk, with a pointer to them in the journal header.
 *
 * When we invalidate a bucket, we have to write its new gen to disk and wait
 * for that write to complete before we use it - otherwise after a crash we
 * could have pointers that appeared to be good but pointed to data that had
 * been overwritten.
 *
 * Since the gens and priorities are all stored contiguously on disk, we can
 * batch this up: We fill up the free_inc list with freshly invalidated buckets,
 * call prio_write(), and when prio_write() finishes we pull buckets off the
 * free_inc list and optionally discard them.
 *
 * free_inc isn't the only freelist - if it was, we'd often have to sleep while
 * priorities and gens were being written before we could allocate. c->free is a
 * smaller freelist, and buckets on that list are always ready to be used.
 *
 * If we've got discards enabled, that happens when a bucket moves from the
 * free_inc list to the free list.
 *
 * It's important to ensure that gens don't wrap around - with respect to
 * either the oldest gen in the btree or the gen on disk. This is quite
 * difficult to do in practice, but we explicitly guard against it anyways - if
 * a bucket is in danger of wrapping around we simply skip invalidating it that
 * time around, and we garbage collect or rewrite the priorities sooner than we
 * would have otherwise.
 *
 * bch2_bucket_alloc() allocates a single bucket from a specific device.
 *
 * bch2_bucket_alloc_set() allocates one or more buckets from different devices
 * in a given filesystem.
 *
 * invalidate_buckets() drives all the processes described above. It's called
 * from bch2_bucket_alloc() and a few other places that need to make sure free
 * buckets are ready.
 *
 * invalidate_buckets_(lru|fifo)() find buckets that are available to be
 * invalidated, and then invalidate them and stick them on the free_inc list -
 * in either lru or fifo order.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "btree_gc.h"
#include "buckets.h"
#include "clock.h"
#include "debug.h"
#include "disk_groups.h"
#include "io.h"
#include "trace.h"

#include <linux/math64.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>

enum bucket_alloc_ret {
	ALLOC_SUCCESS,
	OPEN_BUCKETS_EMPTY,
	FREELIST_EMPTY,		/* Allocator thread not keeping up */
};

/*
 * Open buckets represent a bucket that's currently being allocated from.  They
 * serve two purposes:
 *
 *  - They track buckets that have been partially allocated, allowing for
 *    sub-bucket sized allocations - they're used by the sector allocator below
 *
 *  - They provide a reference to the buckets they own that mark and sweep GC
 *    can find, until the new allocation has a pointer to it inserted into the
 *    btree
 *
 * When allocating some space with the sector allocator, the allocation comes
 * with a reference to an open bucket - the caller is required to put that
 * reference _after_ doing the index update that makes its allocation reachable.
 */

void __bch2_open_bucket_put(struct bch_fs *c, struct open_bucket *ob)
{
	struct bch_dev *ca = bch_dev_bkey_exists(c, ob->ptr.dev);

	percpu_down_read(&c->usage_lock);
	spin_lock(&ob->lock);

	bch2_mark_alloc_bucket(c, ca, PTR_BUCKET_NR(ca, &ob->ptr),
			       false, gc_pos_alloc(c, ob), 0);
	ob->valid = false;

	spin_unlock(&ob->lock);
	percpu_up_read(&c->usage_lock);

	spin_lock(&c->freelist_lock);
	ob->freelist = c->open_buckets_freelist;
	c->open_buckets_freelist = ob - c->open_buckets;
	c->open_buckets_nr_free++;
	spin_unlock(&c->freelist_lock);

	closure_wake_up(&c->open_buckets_wait);
}

static struct open_bucket *bch2_open_bucket_alloc(struct bch_fs *c)
{
	struct open_bucket *ob;

	BUG_ON(!c->open_buckets_freelist || !c->open_buckets_nr_free);

	ob = c->open_buckets + c->open_buckets_freelist;
	c->open_buckets_freelist = ob->freelist;
	atomic_set(&ob->pin, 1);

	c->open_buckets_nr_free--;
	return ob;
}

static void open_bucket_free_unused(struct bch_fs *c,
				    struct write_point *wp,
				    struct open_bucket *ob)
{
	struct bch_dev *ca = bch_dev_bkey_exists(c, ob->ptr.dev);

	BUG_ON(ca->open_buckets_partial_nr >=
	       ARRAY_SIZE(ca->open_buckets_partial));

	if (wp->type == BCH_DATA_USER) {
		spin_lock(&c->freelist_lock);
		ob->on_partial_list = true;
		ca->open_buckets_partial[ca->open_buckets_partial_nr++] =
			ob - c->open_buckets;
		spin_unlock(&c->freelist_lock);

		closure_wake_up(&c->open_buckets_wait);
		closure_wake_up(&c->freelist_wait);
	} else {
		bch2_open_bucket_put(c, ob);
	}
}

static void verify_not_stale(struct bch_fs *c, const struct open_buckets *obs)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct open_bucket *ob;
	unsigned i;

	open_bucket_for_each(c, obs, ob, i) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, ob->ptr.dev);

		BUG_ON(ptr_stale(ca, &ob->ptr));
	}
#endif
}

/* _only_ for allocating the journal on a new device: */
long bch2_bucket_alloc_new_fs(struct bch_dev *ca)
{
	struct bucket_array *buckets;
	ssize_t b;

	rcu_read_lock();
	buckets = bucket_array(ca);

	for (b = ca->mi.first_bucket; b < ca->mi.nbuckets; b++)
		if (is_available_bucket(buckets->b[b].mark))
			goto success;
	b = -1;
success:
	rcu_read_unlock();
	return b;
}

static inline unsigned open_buckets_reserved(enum alloc_reserve reserve)
{
	switch (reserve) {
	case RESERVE_ALLOC:
		return 0;
	case RESERVE_BTREE:
		return BTREE_NODE_RESERVE / 2;
	default:
		return BTREE_NODE_RESERVE;
	}
}

/**
 * bch_bucket_alloc - allocate a single bucket from a specific device
 *
 * Returns index of bucket on success, 0 on failure
 * */
struct open_bucket *bch2_bucket_alloc(struct bch_fs *c, struct bch_dev *ca,
				      enum alloc_reserve reserve,
				      bool may_alloc_partial,
				      struct closure *cl)
{
	struct bucket_array *buckets;
	struct open_bucket *ob;
	long bucket;

	spin_lock(&c->freelist_lock);

	if (may_alloc_partial &&
	    ca->open_buckets_partial_nr) {
		ob = c->open_buckets +
			ca->open_buckets_partial[--ca->open_buckets_partial_nr];
		ob->on_partial_list = false;
		spin_unlock(&c->freelist_lock);
		return ob;
	}

	if (unlikely(c->open_buckets_nr_free <= open_buckets_reserved(reserve))) {
		if (cl)
			closure_wait(&c->open_buckets_wait, cl);
		spin_unlock(&c->freelist_lock);
		trace_open_bucket_alloc_fail(ca, reserve);
		return ERR_PTR(-OPEN_BUCKETS_EMPTY);
	}

	if (likely(fifo_pop(&ca->free[RESERVE_NONE], bucket)))
		goto out;

	switch (reserve) {
	case RESERVE_ALLOC:
		if (fifo_pop(&ca->free[RESERVE_BTREE], bucket))
			goto out;
		break;
	case RESERVE_BTREE:
		if (fifo_used(&ca->free[RESERVE_BTREE]) * 2 >=
		    ca->free[RESERVE_BTREE].size &&
		    fifo_pop(&ca->free[RESERVE_BTREE], bucket))
			goto out;
		break;
	case RESERVE_MOVINGGC:
		if (fifo_pop(&ca->free[RESERVE_MOVINGGC], bucket))
			goto out;
		break;
	default:
		break;
	}

	if (cl)
		closure_wait(&c->freelist_wait, cl);

	spin_unlock(&c->freelist_lock);

	trace_bucket_alloc_fail(ca, reserve);
	return ERR_PTR(-FREELIST_EMPTY);
out:
	verify_not_on_freelist(c, ca, bucket);

	ob = bch2_open_bucket_alloc(c);

	spin_lock(&ob->lock);
	buckets = bucket_array(ca);

	ob->valid	= true;
	ob->sectors_free = ca->mi.bucket_size;
	ob->ptr		= (struct bch_extent_ptr) {
		.gen	= buckets->b[bucket].mark.gen,
		.offset	= bucket_to_sector(ca, bucket),
		.dev	= ca->dev_idx,
	};

	bucket_io_clock_reset(c, ca, bucket, READ);
	bucket_io_clock_reset(c, ca, bucket, WRITE);
	spin_unlock(&ob->lock);

	spin_unlock(&c->freelist_lock);

	bch2_wake_allocator(ca);

	trace_bucket_alloc(ca, reserve);
	return ob;
}

static int __dev_alloc_cmp(struct write_point *wp,
			   unsigned l, unsigned r)
{
	return ((wp->next_alloc[l] > wp->next_alloc[r]) -
		(wp->next_alloc[l] < wp->next_alloc[r]));
}

#define dev_alloc_cmp(l, r) __dev_alloc_cmp(wp, l, r)

struct dev_alloc_list bch2_wp_alloc_list(struct bch_fs *c,
					 struct write_point *wp,
					 struct bch_devs_mask *devs)
{
	struct dev_alloc_list ret = { .nr = 0 };
	struct bch_dev *ca;
	unsigned i;

	for_each_member_device_rcu(ca, c, i, devs)
		ret.devs[ret.nr++] = i;

	bubble_sort(ret.devs, ret.nr, dev_alloc_cmp);
	return ret;
}

void bch2_wp_rescale(struct bch_fs *c, struct bch_dev *ca,
		     struct write_point *wp)
{
	u64 *v = wp->next_alloc + ca->dev_idx;
	u64 free_space = dev_buckets_free(c, ca);
	u64 free_space_inv = free_space
		? div64_u64(1ULL << 48, free_space)
		: 1ULL << 48;
	u64 scale = *v / 4;

	if (*v + free_space_inv >= *v)
		*v += free_space_inv;
	else
		*v = U64_MAX;

	for (v = wp->next_alloc;
	     v < wp->next_alloc + ARRAY_SIZE(wp->next_alloc); v++)
		*v = *v < scale ? 0 : *v - scale;
}

static int bch2_bucket_alloc_set(struct bch_fs *c,
				 struct open_buckets *ptrs,
				 struct write_point *wp,
				 struct bch_devs_mask *devs_may_alloc,
				 unsigned nr_replicas,
				 unsigned *nr_effective,
				 bool *have_cache,
				 enum alloc_reserve reserve,
				 struct closure *cl)
{
	struct dev_alloc_list devs_sorted =
		bch2_wp_alloc_list(c, wp, devs_may_alloc);
	struct bch_dev *ca;
	bool alloc_failure = false;
	unsigned i;

	BUG_ON(*nr_effective >= nr_replicas);

	for (i = 0; i < devs_sorted.nr; i++) {
		struct open_bucket *ob;

		ca = rcu_dereference(c->devs[devs_sorted.devs[i]]);
		if (!ca)
			continue;

		if (!ca->mi.durability &&
		    (*have_cache ||
		     wp->type != BCH_DATA_USER))
			continue;

		ob = bch2_bucket_alloc(c, ca, reserve,
				       wp->type == BCH_DATA_USER, cl);
		if (IS_ERR(ob)) {
			enum bucket_alloc_ret ret = -PTR_ERR(ob);

			WARN_ON(reserve == RESERVE_MOVINGGC &&
				ret != OPEN_BUCKETS_EMPTY);

			if (cl)
				return -EAGAIN;
			if (ret == OPEN_BUCKETS_EMPTY)
				return -ENOSPC;
			alloc_failure = true;
			continue;
		}

		__clear_bit(ca->dev_idx, devs_may_alloc->d);
		*nr_effective	+= ca->mi.durability;
		*have_cache	|= !ca->mi.durability;

		ob_push(c, ptrs, ob);

		bch2_wp_rescale(c, ca, wp);

		if (*nr_effective >= nr_replicas)
			return 0;
	}

	return alloc_failure ? -ENOSPC : -EROFS;
}

/* Sector allocator */

static int get_buckets_from_writepoint(struct bch_fs *c,
				       struct open_buckets *ptrs,
				       struct write_point *wp,
				       struct bch_devs_mask *devs_may_alloc,
				       unsigned nr_replicas,
				       unsigned *nr_effective,
				       bool *have_cache)
{
	struct open_buckets ptrs_skip = { .nr = 0 };
	struct open_bucket *ob;
	unsigned i;

	open_bucket_for_each(c, &wp->ptrs, ob, i) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, ob->ptr.dev);

		if (*nr_effective < nr_replicas &&
		    test_bit(ob->ptr.dev, devs_may_alloc->d) &&
		    (ca->mi.durability ||
		     (wp->type == BCH_DATA_USER && !*have_cache))) {
			__clear_bit(ob->ptr.dev, devs_may_alloc->d);
			*nr_effective	+= ca->mi.durability;
			*have_cache	|= !ca->mi.durability;

			ob_push(c, ptrs, ob);
		} else {
			ob_push(c, &ptrs_skip, ob);
		}
	}
	wp->ptrs = ptrs_skip;

	return *nr_effective < nr_replicas ? -ENOSPC : 0;
}

static int open_bucket_add_buckets(struct bch_fs *c,
				   struct open_buckets *ptrs,
				   struct write_point *wp,
				   struct bch_devs_list *devs_have,
				   u16 target,
				   unsigned nr_replicas,
				   unsigned *nr_effective,
				   bool *have_cache,
				   enum alloc_reserve reserve,
				   struct closure *cl)
{
	struct bch_devs_mask devs;
	const struct bch_devs_mask *t;
	struct open_bucket *ob;
	unsigned i;
	int ret;

	percpu_down_read(&c->usage_lock);
	rcu_read_lock();

	devs = c->rw_devs[wp->type];

	/* Don't allocate from devices we already have pointers to: */
	for (i = 0; i < devs_have->nr; i++)
		__clear_bit(devs_have->devs[i], devs.d);

	open_bucket_for_each(c, ptrs, ob, i)
		__clear_bit(ob->ptr.dev, devs.d);

	t = bch2_target_to_mask(c, target);
	if (t)
		bitmap_and(devs.d, devs.d, t->d, BCH_SB_MEMBERS_MAX);

	ret = get_buckets_from_writepoint(c, ptrs, wp, &devs,
				nr_replicas, nr_effective, have_cache);
	if (!ret)
		goto out;

	/*
	 * Try nonblocking first, so that if one device is full we'll try from
	 * other devices:
	 */
	ret = bch2_bucket_alloc_set(c, ptrs, wp, &devs,
				nr_replicas, nr_effective, have_cache,
				reserve, NULL);
	if (!ret || ret == -EROFS || !cl)
		goto out;

	ret = bch2_bucket_alloc_set(c, ptrs, wp, &devs,
				nr_replicas, nr_effective, have_cache,
				reserve, cl);
out:
	rcu_read_unlock();
	percpu_up_read(&c->usage_lock);

	return ret;
}

void bch2_writepoint_stop(struct bch_fs *c, struct bch_dev *ca,
			  struct write_point *wp)
{
	struct open_buckets ptrs = { .nr = 0 };
	struct open_bucket *ob;
	unsigned i;

	mutex_lock(&wp->lock);
	open_bucket_for_each(c, &wp->ptrs, ob, i)
		if (ob->ptr.dev == ca->dev_idx)
			open_bucket_free_unused(c, wp, ob);
		else
			ob_push(c, &ptrs, ob);

	wp->ptrs = ptrs;
	mutex_unlock(&wp->lock);
}

static struct write_point *__writepoint_find(struct hlist_head *head,
					     unsigned long write_point)
{
	struct write_point *wp;

	hlist_for_each_entry_rcu(wp, head, node)
		if (wp->write_point == write_point)
			return wp;

	return NULL;
}

static struct write_point *writepoint_find(struct bch_fs *c,
					   unsigned long write_point)
{
	struct write_point *wp, *oldest;
	struct hlist_head *head;

	if (!(write_point & 1UL)) {
		wp = (struct write_point *) write_point;
		mutex_lock(&wp->lock);
		return wp;
	}

	head = writepoint_hash(c, write_point);
restart_find:
	wp = __writepoint_find(head, write_point);
	if (wp) {
lock_wp:
		mutex_lock(&wp->lock);
		if (wp->write_point == write_point)
			goto out;
		mutex_unlock(&wp->lock);
		goto restart_find;
	}

	oldest = NULL;
	for (wp = c->write_points;
	     wp < c->write_points + ARRAY_SIZE(c->write_points);
	     wp++)
		if (!oldest || time_before64(wp->last_used, oldest->last_used))
			oldest = wp;

	mutex_lock(&oldest->lock);
	mutex_lock(&c->write_points_hash_lock);
	wp = __writepoint_find(head, write_point);
	if (wp && wp != oldest) {
		mutex_unlock(&c->write_points_hash_lock);
		mutex_unlock(&oldest->lock);
		goto lock_wp;
	}

	wp = oldest;
	hlist_del_rcu(&wp->node);
	wp->write_point = write_point;
	hlist_add_head_rcu(&wp->node, head);
	mutex_unlock(&c->write_points_hash_lock);
out:
	wp->last_used = sched_clock();
	return wp;
}

/*
 * Get us an open_bucket we can allocate from, return with it locked:
 */
struct write_point *bch2_alloc_sectors_start(struct bch_fs *c,
				unsigned target,
				struct write_point_specifier write_point,
				struct bch_devs_list *devs_have,
				unsigned nr_replicas,
				unsigned nr_replicas_required,
				enum alloc_reserve reserve,
				unsigned flags,
				struct closure *cl)
{
	struct write_point *wp;
	struct open_bucket *ob;
	unsigned nr_effective = 0;
	struct open_buckets ptrs = { .nr = 0 };
	bool have_cache = false;
	int ret = 0, i;

	BUG_ON(!nr_replicas || !nr_replicas_required);

	wp = writepoint_find(c, write_point.v);

	if (!target || (flags & BCH_WRITE_ONLY_SPECIFIED_DEVS)) {
		ret = open_bucket_add_buckets(c, &ptrs, wp, devs_have, target,
					      nr_replicas, &nr_effective,
					      &have_cache, reserve, cl);
	} else {
		ret = open_bucket_add_buckets(c, &ptrs, wp, devs_have, target,
					      nr_replicas, &nr_effective,
					      &have_cache, reserve, NULL);
		if (!ret)
			goto alloc_done;

		ret = open_bucket_add_buckets(c, &ptrs, wp, devs_have, 0,
					      nr_replicas, &nr_effective,
					      &have_cache, reserve, cl);
	}
alloc_done:
	BUG_ON(!ret && nr_effective < nr_replicas);

	if (ret == -EROFS &&
	    nr_effective >= nr_replicas_required)
		ret = 0;

	if (ret)
		goto err;

	/* Free buckets we didn't use: */
	open_bucket_for_each(c, &wp->ptrs, ob, i)
		open_bucket_free_unused(c, wp, ob);

	wp->ptrs = ptrs;

	wp->sectors_free = UINT_MAX;

	open_bucket_for_each(c, &wp->ptrs, ob, i)
		wp->sectors_free = min(wp->sectors_free, ob->sectors_free);

	BUG_ON(!wp->sectors_free || wp->sectors_free == UINT_MAX);

	verify_not_stale(c, &wp->ptrs);

	return wp;
err:
	open_bucket_for_each(c, &wp->ptrs, ob, i)
		if (ptrs.nr < ARRAY_SIZE(ptrs.v))
			ob_push(c, &ptrs, ob);
		else
			open_bucket_free_unused(c, wp, ob);
	wp->ptrs = ptrs;

	mutex_unlock(&wp->lock);
	return ERR_PTR(ret);
}

/*
 * Append pointers to the space we just allocated to @k, and mark @sectors space
 * as allocated out of @ob
 */
void bch2_alloc_sectors_append_ptrs(struct bch_fs *c, struct write_point *wp,
				    struct bkey_i_extent *e, unsigned sectors)
{
	struct open_bucket *ob;
	unsigned i;

	BUG_ON(sectors > wp->sectors_free);
	wp->sectors_free -= sectors;

	open_bucket_for_each(c, &wp->ptrs, ob, i) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, ob->ptr.dev);
		struct bch_extent_ptr tmp = ob->ptr;

		EBUG_ON(bch2_extent_has_device(extent_i_to_s_c(e), ob->ptr.dev));

		tmp.cached = bkey_extent_is_cached(&e->k) ||
			(!ca->mi.durability && wp->type == BCH_DATA_USER);

		tmp.offset += ca->mi.bucket_size - ob->sectors_free;
		extent_ptr_append(e, tmp);

		BUG_ON(sectors > ob->sectors_free);
		ob->sectors_free -= sectors;
	}
}

/*
 * Append pointers to the space we just allocated to @k, and mark @sectors space
 * as allocated out of @ob
 */
void bch2_alloc_sectors_done(struct bch_fs *c, struct write_point *wp)
{
	struct open_buckets ptrs = { .nr = 0 }, keep = { .nr = 0 };
	struct open_bucket *ob;
	unsigned i;

	open_bucket_for_each(c, &wp->ptrs, ob, i)
		ob_push(c, !ob->sectors_free ? &ptrs : &keep, ob);
	wp->ptrs = keep;

	mutex_unlock(&wp->lock);

	bch2_open_buckets_put(c, &ptrs);
}
