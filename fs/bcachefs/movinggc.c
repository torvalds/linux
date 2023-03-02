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
#include "btree_write_buffer.h"
#include "buckets.h"
#include "clock.h"
#include "disk_groups.h"
#include "errcode.h"
#include "error.h"
#include "extents.h"
#include "eytzinger.h"
#include "io.h"
#include "keylist.h"
#include "lru.h"
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

static int bch2_bucket_is_movable(struct btree_trans *trans,
				  struct bpos bucket, u64 time, u8 *gen)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bch_alloc_v4 _a;
	const struct bch_alloc_v4 *a;
	int ret;

	if (bch2_bucket_is_open(trans->c, bucket.inode, bucket.offset))
		return 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_alloc, bucket, 0);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	bch2_trans_iter_exit(trans, &iter);

	if (ret)
		return ret;

	a = bch2_alloc_to_v4(k, &_a);
	*gen = a->gen;
	ret = (a->data_type == BCH_DATA_btree ||
	       a->data_type == BCH_DATA_user) &&
		a->fragmentation_lru &&
		a->fragmentation_lru <= time;

	if (ret) {
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, trans->c, k);
		pr_debug("%s", buf.buf);
		printbuf_exit(&buf);
	}

	return ret;
}

static int bch2_copygc_next_bucket(struct btree_trans *trans,
				   struct bpos *bucket, u8 *gen, struct bpos *pos)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	ret = for_each_btree_key2_upto(trans, iter, BTREE_ID_lru,
				  bpos_max(*pos, lru_pos(BCH_LRU_FRAGMENTATION_START, 0, 0)),
				  lru_pos(BCH_LRU_FRAGMENTATION_START, U64_MAX, LRU_TIME_MAX),
				  0, k, ({
		*bucket = u64_to_bucket(k.k->p.offset);

		bch2_bucket_is_movable(trans, *bucket, lru_pos_time(k.k->p), gen);
	}));

	*pos = iter.pos;
	if (ret < 0)
		return ret;
	return ret ? 0 : -ENOENT;
}

static int bch2_copygc(struct bch_fs *c)
{
	struct bch_move_stats move_stats;
	struct btree_trans trans;
	struct moving_context ctxt;
	struct data_update_opts data_opts = {
		.btree_insert_flags = BTREE_INSERT_USE_RESERVE|JOURNAL_WATERMARK_copygc,
	};
	struct bpos bucket;
	struct bpos pos;
	u8 gen = 0;
	unsigned nr_evacuated;
	int ret = 0;

	bch2_move_stats_init(&move_stats, "copygc");
	bch2_moving_ctxt_init(&ctxt, c, NULL, &move_stats,
			      writepoint_ptr(&c->copygc_write_point),
			      false);
	bch2_trans_init(&trans, c, 0, 0);

	ret = bch2_btree_write_buffer_flush(&trans);
	BUG_ON(ret);

	for (nr_evacuated = 0, pos = POS_MIN;
	     nr_evacuated < 32 && !ret;
	     nr_evacuated++, pos = bpos_nosnap_successor(pos)) {
		ret = bch2_copygc_next_bucket(&trans, &bucket, &gen, &pos) ?:
			__bch2_evacuate_bucket(&trans, &ctxt, bucket, gen, data_opts);
		if (bkey_eq(pos, POS_MAX))
			break;
	}

	bch2_trans_exit(&trans);
	bch2_moving_ctxt_exit(&ctxt);

	/* no entries in LRU btree found, or got to end: */
	if (ret == -ENOENT)
		ret = 0;

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

void bch2_copygc_wait_to_text(struct printbuf *out, struct bch_fs *c)
{
	prt_printf(out, "Currently waiting for:     ");
	prt_human_readable_u64(out, max(0LL, c->copygc_wait -
					atomic64_read(&c->io_clock[WRITE].now)) << 9);
	prt_newline(out);

	prt_printf(out, "Currently calculated wait: ");
	prt_human_readable_u64(out, bch2_copygc_wait_amount(c));
	prt_newline(out);
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
