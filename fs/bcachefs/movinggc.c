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

#include <linux/bsearch.h>
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
	ret = data_type_movable(a->data_type) &&
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

typedef FIFO(struct move_bucket_in_flight) move_buckets_in_flight;

struct move_bucket {
	struct bpos		bucket;
	u8			gen;
};

typedef DARRAY(struct move_bucket) move_buckets;

static int move_bucket_cmp(const void *_l, const void *_r)
{
	const struct move_bucket *l = _l;
	const struct move_bucket *r = _r;

	return bkey_cmp(l->bucket, r->bucket);
}

static bool bucket_in_flight(move_buckets *buckets_sorted, struct move_bucket b)
{
	return bsearch(&b,
		       buckets_sorted->data,
		       buckets_sorted->nr,
		       sizeof(buckets_sorted->data[0]),
		       move_bucket_cmp) != NULL;
}

static void move_buckets_wait(struct btree_trans *trans,
			      struct moving_context *ctxt,
			      move_buckets_in_flight *buckets_in_flight,
			      size_t nr, bool verify_evacuated)
{
	while (!fifo_empty(buckets_in_flight)) {
		struct move_bucket_in_flight *i = &fifo_peek_front(buckets_in_flight);

		if (fifo_used(buckets_in_flight) > nr)
			move_ctxt_wait_event(ctxt, trans, !atomic_read(&i->count));

		if (atomic_read(&i->count))
			break;

		/*
		 * moving_ctxt_exit calls bch2_write as it flushes pending
		 * reads, which inits another btree_trans; this one must be
		 * unlocked:
		 */
		if (verify_evacuated)
			bch2_verify_bucket_evacuated(trans, i->bucket, i->gen);
		buckets_in_flight->front++;
	}

	bch2_trans_unlock(trans);
}

static int bch2_copygc_get_buckets(struct btree_trans *trans,
			struct moving_context *ctxt,
			move_buckets_in_flight *buckets_in_flight,
			move_buckets *buckets)
{
	struct btree_iter iter;
	move_buckets buckets_sorted = { 0 };
	struct move_bucket_in_flight *i;
	struct bkey_s_c k;
	size_t fifo_iter, nr_to_get;
	int ret;

	move_buckets_wait(trans, ctxt, buckets_in_flight, buckets_in_flight->size / 2, true);

	nr_to_get = max(16UL, fifo_used(buckets_in_flight) / 4);

	fifo_for_each_entry_ptr(i, buckets_in_flight, fifo_iter) {
		ret = darray_push(&buckets_sorted, ((struct move_bucket) {i->bucket, i->gen}));
		if (ret) {
			bch_err(trans->c, "error allocating move_buckets_sorted");
			goto err;
		}
	}

	sort(buckets_sorted.data,
	     buckets_sorted.nr,
	     sizeof(buckets_sorted.data[0]),
	     move_bucket_cmp,
	     NULL);

	ret = for_each_btree_key2_upto(trans, iter, BTREE_ID_lru,
				  lru_pos(BCH_LRU_FRAGMENTATION_START, 0, 0),
				  lru_pos(BCH_LRU_FRAGMENTATION_START, U64_MAX, LRU_TIME_MAX),
				  0, k, ({
		struct move_bucket b = { .bucket = u64_to_bucket(k.k->p.offset) };
		int ret = 0;

		if (!bucket_in_flight(&buckets_sorted, b) &&
		    bch2_bucket_is_movable(trans, b.bucket, lru_pos_time(k.k->p), &b.gen))
			ret = darray_push(buckets, b) ?: buckets->nr >= nr_to_get;

		ret;
	}));
err:
	darray_exit(&buckets_sorted);

	return ret < 0 ? ret : 0;
}

static int bch2_copygc(struct btree_trans *trans,
		       struct moving_context *ctxt,
		       move_buckets_in_flight *buckets_in_flight)
{
	struct bch_fs *c = trans->c;
	struct data_update_opts data_opts = {
		.btree_insert_flags = BTREE_INSERT_USE_RESERVE|JOURNAL_WATERMARK_copygc,
	};
	move_buckets buckets = { 0 };
	struct move_bucket_in_flight *f;
	struct move_bucket *i;
	u64 moved = atomic64_read(&ctxt->stats->sectors_moved);
	int ret = 0;

	ret = bch2_btree_write_buffer_flush(trans);
	if (bch2_fs_fatal_err_on(ret, c, "%s: error %s from bch2_btree_write_buffer_flush()",
				 __func__, bch2_err_str(ret)))
		return ret;

	ret = bch2_copygc_get_buckets(trans, ctxt, buckets_in_flight, &buckets);
	if (ret)
		goto err;

	darray_for_each(buckets, i) {
		if (unlikely(freezing(current)))
			break;

		f = fifo_push_ref(buckets_in_flight);
		f->bucket	= i->bucket;
		f->gen		= i->gen;
		atomic_set(&f->count, 0);

		ret = __bch2_evacuate_bucket(trans, ctxt, f, f->bucket, f->gen, data_opts);
		if (ret)
			goto err;
	}
err:
	darray_exit(&buckets);

	/* no entries in LRU btree found, or got to end: */
	if (ret == -ENOENT)
		ret = 0;

	if (ret < 0 && !bch2_err_matches(ret, EROFS))
		bch_err(c, "error from bch2_move_data() in copygc: %s", bch2_err_str(ret));

	moved = atomic64_read(&ctxt->stats->sectors_moved) - moved;
	trace_and_count(c, copygc, c, moved, 0, 0, 0);
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
	unsigned i;

	for_each_rw_member(ca, c, dev_idx) {
		struct bch_dev_usage usage = bch2_dev_usage_read(ca);

		fragmented_allowed = ((__dev_buckets_available(ca, usage, RESERVE_stripe) *
				       ca->mi.bucket_size) >> 1);
		fragmented = 0;

		for (i = 0; i < BCH_DATA_NR; i++)
			if (data_type_movable(i))
				fragmented += usage.d[i].fragmented;

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
	struct btree_trans trans;
	struct moving_context ctxt;
	struct bch_move_stats move_stats;
	struct io_clock *clock = &c->io_clock[WRITE];
	move_buckets_in_flight move_buckets;
	u64 last, wait;
	int ret = 0;

	if (!init_fifo(&move_buckets, 1 << 14, GFP_KERNEL)) {
		bch_err(c, "error allocating copygc buckets in flight");
		return -ENOMEM;
	}

	set_freezable();
	bch2_trans_init(&trans, c, 0, 0);

	bch2_move_stats_init(&move_stats, "copygc");
	bch2_moving_ctxt_init(&ctxt, c, NULL, &move_stats,
			      writepoint_ptr(&c->copygc_write_point),
			      false);

	while (!ret && !kthread_should_stop()) {
		bch2_trans_unlock(&trans);
		cond_resched();

		if (!c->copy_gc_enabled) {
			move_buckets_wait(&trans, &ctxt, &move_buckets, 0, true);
			kthread_wait_freezable(c->copy_gc_enabled);
		}

		if (unlikely(freezing(current))) {
			move_buckets_wait(&trans, &ctxt, &move_buckets, 0, true);
			__refrigerator(false);
			continue;
		}

		last = atomic64_read(&clock->now);
		wait = bch2_copygc_wait_amount(c);

		if (wait > clock->max_slop) {
			move_buckets_wait(&trans, &ctxt, &move_buckets, 0, true);
			trace_and_count(c, copygc_wait, c, wait, last + wait);
			c->copygc_wait = last + wait;
			bch2_kthread_io_clock_wait(clock, last + wait,
					MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		c->copygc_wait = 0;

		c->copygc_running = true;
		ret = bch2_copygc(&trans, &ctxt, &move_buckets);
		c->copygc_running = false;

		wake_up(&c->copygc_running_wq);
	}

	bch2_moving_ctxt_exit(&ctxt);
	bch2_trans_exit(&trans);
	free_fifo(&move_buckets);

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
