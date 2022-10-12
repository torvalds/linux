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

static int bucket_offset_cmp(const void *_l, const void *_r, size_t size)
{
	const struct copygc_heap_entry *l = _l;
	const struct copygc_heap_entry *r = _r;

	return  cmp_int(l->dev,    r->dev) ?:
		cmp_int(l->offset, r->offset);
}

static bool copygc_pred(struct bch_fs *c, void *arg,
			struct bkey_s_c k,
			struct bch_io_opts *io_opts,
			struct data_update_opts *data_opts)
{
	copygc_heap *h = &c->copygc_heap;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p = { 0 };
	unsigned i = 0;

	/*
	 * We need to use the journal reserve here, because
	 *  - journal reclaim depends on btree key cache
	 *    flushing to make forward progress,
	 *  - which has to make forward progress when the
	 *    journal is pre-reservation full,
	 *  - and depends on allocation - meaning allocator and
	 *    copygc
	 */

	data_opts->rewrite_ptrs		= 0;
	data_opts->target		= io_opts->background_target;
	data_opts->extra_replicas	= 0;
	data_opts->btree_insert_flags	= BTREE_INSERT_USE_RESERVE|
		JOURNAL_WATERMARK_copygc;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, p.ptr.dev);
		struct copygc_heap_entry search = {
			.dev	= p.ptr.dev,
			.offset	= p.ptr.offset,
		};
		ssize_t eytz;

		if (p.ptr.cached)
			continue;

		eytz = eytzinger0_find_le(h->data, h->used,
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
		if (eytz >= 0 &&
		    p.ptr.dev == h->data[eytz].dev &&
		    p.ptr.offset < h->data[eytz].offset + ca->mi.bucket_size &&
		    p.ptr.gen == h->data[eytz].gen)
			data_opts->rewrite_ptrs |= 1U << i;

		i++;
	}

	return data_opts->rewrite_ptrs != 0;
}

static inline int fragmentation_cmp(copygc_heap *heap,
				   struct copygc_heap_entry l,
				   struct copygc_heap_entry r)
{
	return cmp_int(l.fragmentation, r.fragmentation);
}

static int walk_buckets_to_copygc(struct bch_fs *c)
{
	copygc_heap *h = &c->copygc_heap;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_alloc_v4 a;
	int ret;

	bch2_trans_init(&trans, c, 0, 0);

	for_each_btree_key(&trans, iter, BTREE_ID_alloc, POS_MIN,
			   BTREE_ITER_PREFETCH, k, ret) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, iter.pos.inode);
		struct copygc_heap_entry e;

		bch2_alloc_to_v4(k, &a);

		if (a.data_type != BCH_DATA_user ||
		    a.dirty_sectors >= ca->mi.bucket_size ||
		    bch2_bucket_is_open(c, iter.pos.inode, iter.pos.offset))
			continue;

		e = (struct copygc_heap_entry) {
			.dev		= iter.pos.inode,
			.gen		= a.gen,
			.replicas	= 1 + a.stripe_redundancy,
			.fragmentation	= div_u64((u64) a.dirty_sectors * (1ULL << 31),
						  ca->mi.bucket_size),
			.sectors	= a.dirty_sectors,
			.offset		= bucket_to_sector(ca, iter.pos.offset),
		};
		heap_add_or_replace(h, e, -fragmentation_cmp, NULL);

	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);
	return ret;
}

static int bucket_inorder_cmp(const void *_l, const void *_r)
{
	const struct copygc_heap_entry *l = _l;
	const struct copygc_heap_entry *r = _r;

	return cmp_int(l->dev, r->dev) ?: cmp_int(l->offset, r->offset);
}

static int check_copygc_was_done(struct bch_fs *c,
				 u64 *sectors_not_moved,
				 u64 *buckets_not_moved)
{
	copygc_heap *h = &c->copygc_heap;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_alloc_v4 a;
	struct copygc_heap_entry *i;
	int ret = 0;

	sort(h->data, h->used, sizeof(h->data[0]), bucket_inorder_cmp, NULL);

	bch2_trans_init(&trans, c, 0, 0);
	bch2_trans_iter_init(&trans, &iter, BTREE_ID_alloc, POS_MIN, 0);

	for (i = h->data; i < h->data + h->used; i++) {
		struct bch_dev *ca = bch_dev_bkey_exists(c, i->dev);

		bch2_btree_iter_set_pos(&iter, POS(i->dev, sector_to_bucket(ca, i->offset)));

		ret = lockrestart_do(&trans,
				bkey_err(k = bch2_btree_iter_peek_slot(&iter)));
		if (ret)
			break;

		bch2_alloc_to_v4(k, &a);

		if (a.gen == i->gen && a.dirty_sectors) {
			*sectors_not_moved += a.dirty_sectors;
			*buckets_not_moved += 1;
		}
	}
	bch2_trans_iter_exit(&trans, &iter);

	bch2_trans_exit(&trans);
	return ret;
}

static int bch2_copygc(struct bch_fs *c)
{
	copygc_heap *h = &c->copygc_heap;
	struct copygc_heap_entry e, *i;
	struct bch_move_stats move_stats;
	u64 sectors_to_move = 0, sectors_to_write = 0, sectors_not_moved = 0;
	u64 sectors_reserved = 0;
	u64 buckets_to_move, buckets_not_moved = 0;
	struct bch_dev *ca;
	unsigned dev_idx;
	size_t heap_size = 0;
	int ret;

	bch2_move_stats_init(&move_stats, "copygc");

	/*
	 * Find buckets with lowest sector counts, skipping completely
	 * empty buckets, by building a maxheap sorted by sector count,
	 * and repeatedly replacing the maximum element until all
	 * buckets have been visited.
	 */
	h->used = 0;

	for_each_rw_member(ca, c, dev_idx)
		heap_size += ca->mi.nbuckets >> 7;

	if (h->size < heap_size) {
		free_heap(&c->copygc_heap);
		if (!init_heap(&c->copygc_heap, heap_size, GFP_KERNEL)) {
			bch_err(c, "error allocating copygc heap");
			return 0;
		}
	}

	for_each_rw_member(ca, c, dev_idx) {
		struct bch_dev_usage usage = bch2_dev_usage_read(ca);

		u64 avail = max_t(s64, 0,
				  usage.d[BCH_DATA_free].buckets +
				  usage.d[BCH_DATA_need_discard].buckets -
				  ca->nr_open_buckets -
				  bch2_dev_buckets_reserved(ca, RESERVE_movinggc));

		avail = min(avail, ca->mi.nbuckets >> 6);

		sectors_reserved += avail * ca->mi.bucket_size;
	}

	ret = walk_buckets_to_copygc(c);
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

	/*
	 * Our btree node allocations also come out of RESERVE_movingc:
	 */
	sectors_reserved = (sectors_reserved * 3) / 4;
	if (!sectors_reserved) {
		bch2_fs_fatal_error(c, "stuck, ran out of copygc reserve!");
		return -1;
	}

	for (i = h->data; i < h->data + h->used; i++) {
		sectors_to_move += i->sectors;
		sectors_to_write += i->sectors * i->replicas;
	}

	while (sectors_to_write > sectors_reserved) {
		BUG_ON(!heap_pop(h, e, -fragmentation_cmp, NULL));
		sectors_to_write -= e.sectors * e.replicas;
	}

	buckets_to_move = h->used;

	if (!buckets_to_move) {
		bch_err_ratelimited(c, "copygc cannot run - sectors_reserved %llu!",
				    sectors_reserved);
		return 0;
	}

	eytzinger0_sort(h->data, h->used,
			sizeof(h->data[0]),
			bucket_offset_cmp, NULL);

	ret = bch2_move_data(c,
			     0,			POS_MIN,
			     BTREE_ID_NR,	POS_MAX,
			     NULL,
			     &move_stats,
			     writepoint_ptr(&c->copygc_write_point),
			     false,
			     copygc_pred, NULL);
	if (ret < 0 && ret != -EROFS)
		bch_err(c, "error from bch2_move_data() in copygc: %s", bch2_err_str(ret));
	if (ret)
		return ret;

	ret = check_copygc_was_done(c, &sectors_not_moved, &buckets_not_moved);
	if (ret) {
		bch_err(c, "error %i from check_copygc_was_done()", ret);
		return ret;
	}

	if (sectors_not_moved)
		bch_warn_ratelimited(c,
			"copygc finished but %llu/%llu sectors, %llu/%llu buckets not moved (move stats: moved %llu sectors, raced %llu keys, %llu sectors)",
			 sectors_not_moved, sectors_to_move,
			 buckets_not_moved, buckets_to_move,
			 atomic64_read(&move_stats.sectors_moved),
			 atomic64_read(&move_stats.keys_raced),
			 atomic64_read(&move_stats.sectors_raced));

	trace_and_count(c, copygc, c,
		     atomic64_read(&move_stats.sectors_moved), sectors_not_moved,
		     buckets_to_move, buckets_not_moved);
	return 0;
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
