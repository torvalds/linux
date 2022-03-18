// SPDX-License-Identifier: GPL-2.0
/*
 * Moving/copying garbage collector
 *
 * Copyright 2012 Google, Inc.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "btree_iter.h"
#include "btree_update.h"
#include "buckets.h"
#include "clock.h"
#include "disk_groups.h"
#include "errcode.h"
#include "error.h"
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

static inline int fragmentation_cmp(copygc_heap *heap,
				   struct copygc_heap_entry l,
				   struct copygc_heap_entry r)
{
	return cmp_int(l.fragmentation, r.fragmentation);
}

static int find_buckets_to_copygc(struct bch_fs *c)
{
	copygc_heap *h = &c->copygc_heap;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	/*
	 * Find buckets with lowest sector counts, skipping completely
	 * empty buckets, by building a maxheap sorted by sector count,
	 * and repeatedly replacing the maximum element until all
	 * buckets have been visited.
	 */
	h->used = 0;

	for_each_btree_key(&trans, iter, BTREE_ID_alloc, POS_MIN,
			   BTREE_ITER_PREFETCH, k, ret) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, iter.pos.inode);
		struct copygc_heap_entry e;
		struct bch_alloc_v4 a_convert;
		const struct bch_alloc_v4 *a;

		a = bch2_alloc_to_v4(k, &a_convert);

		if ((a->data_type != BCH_DATA_btree &&
		     a->data_type != BCH_DATA_user) ||
		    a->dirty_sectors >= ca->mi.bucket_size ||
		    bch2_bucket_is_open(c, iter.pos.inode, iter.pos.offset))
			continue;

		e = (struct copygc_heap_entry) {
			.dev		= iter.pos.inode,
			.gen		= a->gen,
			.replicas	= 1 + a->stripe_redundancy,
			.fragmentation	= div_u64((u64) a->dirty_sectors * (1ULL << 31),
						  ca->mi.bucket_size),
			.sectors	= a->dirty_sectors,
			.bucket		= iter.pos.offset,
		};
		heap_add_or_replace(h, e, -fragmentation_cmp, NULL);

	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);
	return ret;
}

static int bch2_copygc(struct bch_fs *c)
{
	copygc_heap *h = &c->copygc_heap;
	struct copygc_heap_entry e;
	struct bch_move_stats move_stats;
	struct bch_dev *ca;
	unsigned dev_idx;
	size_t heap_size = 0;
	struct moving_context ctxt;
	struct data_update_opts data_opts = {
		.btree_insert_flags = BTREE_INSERT_USE_RESERVE|JOURNAL_WATERMARK_copygc,
	};
	int ret = 0;

	bch2_move_stats_init(&move_stats, "copygc");

	for_each_rw_member(ca, c, dev_idx)
		heap_size += ca->mi.nbuckets >> 7;

	if (h->size < heap_size) {
		free_heap(&c->copygc_heap);
		if (!init_heap(&c->copygc_heap, heap_size, GFP_KERNEL)) {
			bch_err(c, "error allocating copygc heap");
			return 0;
		}
	}

	ret = find_buckets_to_copygc(c);
	if (ret) {
		bch2_fs_fatal_error(c, "error walking buckets to copygc!");
		return ret;
	}

	if (!h->used) {
		s64 wait = S64_MAX, dev_wait;
		u64 dev_min_wait_fragmented = 0;
		u64 dev_min_wait_allowed = 0;
		int dev_min_wait = -1;

		for_each_rw_member(ca, c, dev_idx) {
			struct bch_dev_usage usage = bch2_dev_usage_read(ca);
			s64 allowed = ((__dev_buckets_available(ca, usage, RESERVE_none) *
					       ca->mi.bucket_size) >> 1);
			s64 fragmented = usage.d[BCH_DATA_user].fragmented;

			dev_wait = max(0LL, allowed - fragmented);

			if (dev_min_wait < 0 || dev_wait < wait) {
				dev_min_wait = dev_idx;
				dev_min_wait_fragmented = fragmented;
				dev_min_wait_allowed	= allowed;
			}
		}

		bch_err_ratelimited(c, "copygc requested to run but found no buckets to move! dev %u fragmented %llu allowed %llu",
				    dev_min_wait, dev_min_wait_fragmented, dev_min_wait_allowed);
		return 0;
	}

	heap_resort(h, fragmentation_cmp, NULL);

	bch2_moving_ctxt_init(&ctxt, c, NULL, &move_stats,
			      writepoint_ptr(&c->copygc_write_point),
			      false);

	/* not correct w.r.t. device removal */
	while (h->used && !ret) {
		BUG_ON(!heap_pop(h, e, -fragmentation_cmp, NULL));
		ret = __bch2_evacuate_bucket(&ctxt, POS(e.dev, e.bucket), e.gen,
					     data_opts);
	}

	bch2_moving_ctxt_exit(&ctxt);

	if (ret < 0 && !bch2_err_matches(ret, EROFS))
		bch_err(c, "error from bch2_move_data() in copygc: %s", bch2_err_str(ret));

	trace_and_count(c, copygc, c, atomic64_read(&move_stats.sectors_moved), 0, 0, 0);
	return ret;
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
unsigned long bch2_copygc_wait_amount(struct bch_fs *c)
{
	struct bch_dev *ca;
	unsigned dev_idx;
	s64 wait = S64_MAX, fragmented_allowed, fragmented;

	for_each_rw_member(ca, c, dev_idx) {
		struct bch_dev_usage usage = bch2_dev_usage_read(ca);

		fragmented_allowed = ((__dev_buckets_available(ca, usage, RESERVE_none) *
				       ca->mi.bucket_size) >> 1);
		fragmented = usage.d[BCH_DATA_user].fragmented;

		wait = min(wait, max(0LL, fragmented_allowed - fragmented));
	}

	return wait;
}

static int bch2_copygc_thread(void *arg)
{
	struct bch_fs *c = arg;
	struct io_clock *clock = &c->io_clock[WRITE];
	u64 last, wait;
	int ret = 0;

	set_freezable();

	while (!ret && !kthread_should_stop()) {
		cond_resched();

		if (kthread_wait_freezable(c->copy_gc_enabled))
			break;

		last = atomic64_read(&clock->now);
		wait = bch2_copygc_wait_amount(c);

		if (wait > clock->max_slop) {
			trace_and_count(c, copygc_wait, c, wait, last + wait);
			c->copygc_wait = last + wait;
			bch2_kthread_io_clock_wait(clock, last + wait,
					MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		c->copygc_wait = 0;

		c->copygc_running = true;
		ret = bch2_copygc(c);
		c->copygc_running = false;

		wake_up(&c->copygc_running_wq);
	}

	return 0;
}

void bch2_copygc_stop(struct bch_fs *c)
{
	if (c->copygc_thread) {
		kthread_stop(c->copygc_thread);
		put_task_struct(c->copygc_thread);
	}
	c->copygc_thread = NULL;
}

int bch2_copygc_start(struct bch_fs *c)
{
	struct task_struct *t;
	int ret;

	if (c->copygc_thread)
		return 0;

	if (c->opts.nochanges)
		return 0;

	if (bch2_fs_init_fault("copygc_start"))
		return -ENOMEM;

	t = kthread_create(bch2_copygc_thread, c, "bch-copygc/%s", c->name);
	ret = PTR_ERR_OR_ZERO(t);
	if (ret) {
		bch_err(c, "error creating copygc thread: %s", bch2_err_str(ret));
		return ret;
	}

	get_task_struct(t);

	c->copygc_thread = t;
	wake_up_process(c->copygc_thread);

	return 0;
}

void bch2_fs_copygc_init(struct bch_fs *c)
{
	init_waitqueue_head(&c->copygc_running_wq);
	c->copygc_running = false;
}
