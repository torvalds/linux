// SPDX-License-Identifier: GPL-2.0
/*
 * Moving/copying garbage collector
 *
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "btree_iter.h"
#include "btree_update.h"
#include "buckets.h"
#include "clock.h"
#include "disk_groups.h"
#include "extents.h"
#include "eytzinger.h"
#include "io.h"
#include "keylist.h"
#include "move.h"
#include "movinggc.h"
#include "super-io.h"
#include "trace.h"

#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/sched/task.h>
#include <linux/sort.h>
#include <linux/wait.h>

/*
 * We can't use the entire copygc reserve in one iteration of copygc: we may
 * need the buckets we're freeing up to go back into the copygc reserve to make
 * forward progress, but if the copygc reserve is full they'll be available for
 * any allocation - and it's possible that in a given iteration, we free up most
 * of the buckets we're going to free before we allocate most of the buckets
 * we're going to allocate.
 *
 * If we only use half of the reserve per iteration, then in steady state we'll
 * always have room in the reserve for the buckets we're going to need in the
 * next iteration:
 */
#define COPYGC_BUCKETS_PER_ITER(ca)					\
	((ca)->free[RESERVE_MOVINGGC].size / 2)

/*
 * Max sectors to move per iteration: Have to take into account internal
 * fragmentation from the multiple write points for each generation:
 */
#define COPYGC_SECTORS_PER_ITER(ca)					\
	((ca)->mi.bucket_size *	COPYGC_BUCKETS_PER_ITER(ca))

static inline int sectors_used_cmp(copygc_heap *heap,
				   struct copygc_heap_entry l,
				   struct copygc_heap_entry r)
{
	return cmp_int(l.sectors, r.sectors);
}

static int bucket_offset_cmp(const void *_l, const void *_r, size_t size)
{
	const struct copygc_heap_entry *l = _l;
	const struct copygc_heap_entry *r = _r;

	return cmp_int(l->offset, r->offset);
}

static bool __copygc_pred(struct bch_dev *ca,
			  struct bkey_s_c k)
{
	copygc_heap *h = &ca->copygc_heap;
	const struct bch_extent_ptr *ptr =
		bch2_bkey_has_device(k, ca->dev_idx);

	if (ptr) {
		struct copygc_heap_entry search = { .offset = ptr->offset };

		ssize_t i = eytzinger0_find_le(h->data, h->used,
					       sizeof(h->data[0]),
					       bucket_offset_cmp, &search);
#if 0
		/* eytzinger search verify code: */
		ssize_t j = -1, k;

		for (k = 0; k < h->used; k++)
			if (h->data[k].offset <= ptr->offset &&
			    (j < 0 || h->data[k].offset > h->data[j].offset))
				j = k;

		BUG_ON(i != j);
#endif
		return (i >= 0 &&
			ptr->offset < h->data[i].offset + ca->mi.bucket_size &&
			ptr->gen == h->data[i].gen);
	}

	return false;
}

static enum data_cmd copygc_pred(struct bch_fs *c, void *arg,
				 struct bkey_s_c k,
				 struct bch_io_opts *io_opts,
				 struct data_opts *data_opts)
{
	struct bch_dev *ca = arg;

	if (!__copygc_pred(ca, k))
		return DATA_SKIP;

	data_opts->target		= dev_to_target(ca->dev_idx);
	data_opts->btree_insert_flags	= BTREE_INSERT_USE_RESERVE;
	data_opts->rewrite_dev		= ca->dev_idx;
	return DATA_REWRITE;
}

static bool have_copygc_reserve(struct bch_dev *ca)
{
	bool ret;

	spin_lock(&ca->fs->freelist_lock);
	ret = fifo_full(&ca->free[RESERVE_MOVINGGC]) ||
		ca->allocator_state != ALLOCATOR_RUNNING;
	spin_unlock(&ca->fs->freelist_lock);

	return ret;
}

static void bch2_copygc(struct bch_fs *c, struct bch_dev *ca)
{
	copygc_heap *h = &ca->copygc_heap;
	struct copygc_heap_entry e, *i;
	struct bucket_array *buckets;
	struct bch_move_stats move_stats;
	u64 sectors_to_move = 0, sectors_not_moved = 0;
	u64 buckets_to_move, buckets_not_moved = 0;
	size_t b;
	int ret;

	memset(&move_stats, 0, sizeof(move_stats));
	closure_wait_event(&c->freelist_wait, have_copygc_reserve(ca));

	/*
	 * Find buckets with lowest sector counts, skipping completely
	 * empty buckets, by building a maxheap sorted by sector count,
	 * and repeatedly replacing the maximum element until all
	 * buckets have been visited.
	 */
	h->used = 0;

	/*
	 * We need bucket marks to be up to date - gc can't be recalculating
	 * them:
	 */
	down_read(&c->gc_lock);
	down_read(&ca->bucket_lock);
	buckets = bucket_array(ca);

	for (b = buckets->first_bucket; b < buckets->nbuckets; b++) {
		struct bucket_mark m = READ_ONCE(buckets->b[b].mark);
		struct copygc_heap_entry e;

		if (m.owned_by_allocator ||
		    m.data_type != BCH_DATA_user ||
		    !bucket_sectors_used(m) ||
		    bucket_sectors_used(m) >= ca->mi.bucket_size)
			continue;

		e = (struct copygc_heap_entry) {
			.gen		= m.gen,
			.sectors	= bucket_sectors_used(m),
			.offset		= bucket_to_sector(ca, b),
		};
		heap_add_or_replace(h, e, -sectors_used_cmp, NULL);
	}
	up_read(&ca->bucket_lock);
	up_read(&c->gc_lock);

	for (i = h->data; i < h->data + h->used; i++)
		sectors_to_move += i->sectors;

	while (sectors_to_move > COPYGC_SECTORS_PER_ITER(ca)) {
		BUG_ON(!heap_pop(h, e, -sectors_used_cmp, NULL));
		sectors_to_move -= e.sectors;
	}

	buckets_to_move = h->used;

	if (!buckets_to_move)
		return;

	eytzinger0_sort(h->data, h->used,
			sizeof(h->data[0]),
			bucket_offset_cmp, NULL);

	ret = bch2_move_data(c, &ca->copygc_pd.rate,
			     writepoint_ptr(&ca->copygc_write_point),
			     POS_MIN, POS_MAX,
			     copygc_pred, ca,
			     &move_stats);

	down_read(&ca->bucket_lock);
	buckets = bucket_array(ca);
	for (i = h->data; i < h->data + h->used; i++) {
		size_t b = sector_to_bucket(ca, i->offset);
		struct bucket_mark m = READ_ONCE(buckets->b[b].mark);

		if (i->gen == m.gen && bucket_sectors_used(m)) {
			sectors_not_moved += bucket_sectors_used(m);
			buckets_not_moved++;
		}
	}
	up_read(&ca->bucket_lock);

	if (sectors_not_moved && !ret)
		bch_warn_ratelimited(c,
			"copygc finished but %llu/%llu sectors, %llu/%llu buckets not moved (move stats: moved %llu sectors, raced %llu keys, %llu sectors)",
			 sectors_not_moved, sectors_to_move,
			 buckets_not_moved, buckets_to_move,
			 atomic64_read(&move_stats.sectors_moved),
			 atomic64_read(&move_stats.keys_raced),
			 atomic64_read(&move_stats.sectors_raced));

	trace_copygc(ca,
		     atomic64_read(&move_stats.sectors_moved), sectors_not_moved,
		     buckets_to_move, buckets_not_moved);
}

/*
 * Copygc runs when the amount of fragmented data is above some arbitrary
 * threshold:
 *
 * The threshold at the limit - when the device is full - is the amount of space
 * we reserved in bch2_recalc_capacity; we can't have more than that amount of
 * disk space stranded due to fragmentation and store everything we have
 * promised to store.
 *
 * But we don't want to be running copygc unnecessarily when the device still
 * has plenty of free space - rather, we want copygc to smoothly run every so
 * often and continually reduce the amount of fragmented space as the device
 * fills up. So, we increase the threshold by half the current free space.
 */
unsigned long bch2_copygc_wait_amount(struct bch_dev *ca)
{
	struct bch_fs *c = ca->fs;
	struct bch_dev_usage usage = bch2_dev_usage_read(c, ca);
	u64 fragmented_allowed = ca->copygc_threshold +
		((__dev_buckets_available(ca, usage) * ca->mi.bucket_size) >> 1);

	return max_t(s64, 0, fragmented_allowed - usage.sectors_fragmented);
}

static int bch2_copygc_thread(void *arg)
{
	struct bch_dev *ca = arg;
	struct bch_fs *c = ca->fs;
	struct io_clock *clock = &c->io_clock[WRITE];
	unsigned long last, wait;

	set_freezable();

	while (!kthread_should_stop()) {
		if (kthread_wait_freezable(c->copy_gc_enabled))
			break;

		last = atomic_long_read(&clock->now);
		wait = bch2_copygc_wait_amount(ca);

		if (wait > clock->max_slop) {
			bch2_kthread_io_clock_wait(clock, last + wait,
					MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		bch2_copygc(c, ca);
	}

	return 0;
}

void bch2_copygc_stop(struct bch_dev *ca)
{
	ca->copygc_pd.rate.rate = UINT_MAX;
	bch2_ratelimit_reset(&ca->copygc_pd.rate);

	if (ca->copygc_thread) {
		kthread_stop(ca->copygc_thread);
		put_task_struct(ca->copygc_thread);
	}
	ca->copygc_thread = NULL;
}

int bch2_copygc_start(struct bch_fs *c, struct bch_dev *ca)
{
	struct task_struct *t;

	if (ca->copygc_thread)
		return 0;

	if (c->opts.nochanges)
		return 0;

	if (bch2_fs_init_fault("copygc_start"))
		return -ENOMEM;

	t = kthread_create(bch2_copygc_thread, ca,
			   "bch_copygc[%s]", ca->name);
	if (IS_ERR(t))
		return PTR_ERR(t);

	get_task_struct(t);

	ca->copygc_thread = t;
	wake_up_process(ca->copygc_thread);

	return 0;
}

void bch2_dev_copygc_init(struct bch_dev *ca)
{
	bch2_pd_controller_init(&ca->copygc_pd);
	ca->copygc_pd.d_term = 0;
}
