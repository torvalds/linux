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
#include "errcode.h"
#include "error.h"
#include "lru.h"
#include "move.h"
#include "movinggc.h"
#include "trace.h"

#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/sched/task.h>
#include <linux/wait.h>

struct buckets_in_flight {
	struct rhashtable		table;
	struct move_bucket_in_flight	*first;
	struct move_bucket_in_flight	*last;
	size_t				nr;
	size_t				sectors;
};

static const struct rhashtable_params bch_move_bucket_params = {
	.head_offset		= offsetof(struct move_bucket_in_flight, hash),
	.key_offset		= offsetof(struct move_bucket_in_flight, bucket.k),
	.key_len		= sizeof(struct move_bucket_key),
	.automatic_shrinking	= true,
};

static struct move_bucket_in_flight *
move_bucket_in_flight_add(struct buckets_in_flight *list, struct move_bucket b)
{
	struct move_bucket_in_flight *new = kzalloc(sizeof(*new), GFP_KERNEL);
	int ret;

	if (!new)
		return ERR_PTR(-ENOMEM);

	new->bucket = b;

	ret = rhashtable_lookup_insert_fast(&list->table, &new->hash,
					    bch_move_bucket_params);
	if (ret) {
		kfree(new);
		return ERR_PTR(ret);
	}

	if (!list->first)
		list->first = new;
	else
		list->last->next = new;

	list->last = new;
	list->nr++;
	list->sectors += b.sectors;
	return new;
}

static int bch2_bucket_is_movable(struct btree_trans *trans,
				  struct move_bucket *b, u64 time)
{
	struct bch_fs *c = trans->c;

	if (bch2_bucket_is_open(c, b->k.bucket.inode, b->k.bucket.offset))
		return 0;

	struct btree_iter iter;
	struct bkey_s_c k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_alloc,
				       b->k.bucket, BTREE_ITER_cached);
	int ret = bkey_err(k);
	if (ret)
		return ret;

	struct bch_dev *ca = bch2_dev_tryget(c, k.k->p.inode);
	if (!ca)
		goto out;

	if (ca->mi.state != BCH_MEMBER_STATE_rw ||
	    !bch2_dev_is_online(ca))
		goto out_put;

	struct bch_alloc_v4 _a;
	const struct bch_alloc_v4 *a = bch2_alloc_to_v4(k, &_a);
	b->k.gen	= a->gen;
	b->sectors	= bch2_bucket_sectors_dirty(*a);
	u64 lru_idx	= alloc_lru_idx_fragmentation(*a, ca);

	ret = lru_idx && lru_idx <= time;
out_put:
	bch2_dev_put(ca);
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static void move_buckets_wait(struct moving_context *ctxt,
			      struct buckets_in_flight *list,
			      bool flush)
{
	struct move_bucket_in_flight *i;
	int ret;

	while ((i = list->first)) {
		if (flush)
			move_ctxt_wait_event(ctxt, !atomic_read(&i->count));

		if (atomic_read(&i->count))
			break;

		list->first = i->next;
		if (!list->first)
			list->last = NULL;

		list->nr--;
		list->sectors -= i->bucket.sectors;

		ret = rhashtable_remove_fast(&list->table, &i->hash,
					     bch_move_bucket_params);
		BUG_ON(ret);
		kfree(i);
	}

	bch2_trans_unlock_long(ctxt->trans);
}

static bool bucket_in_flight(struct buckets_in_flight *list,
			     struct move_bucket_key k)
{
	return rhashtable_lookup_fast(&list->table, &k, bch_move_bucket_params);
}

typedef DARRAY(struct move_bucket) move_buckets;

static int bch2_copygc_get_buckets(struct moving_context *ctxt,
			struct buckets_in_flight *buckets_in_flight,
			move_buckets *buckets)
{
	struct btree_trans *trans = ctxt->trans;
	struct bch_fs *c = trans->c;
	size_t nr_to_get = max_t(size_t, 16U, buckets_in_flight->nr / 4);
	size_t saw = 0, in_flight = 0, not_movable = 0, sectors = 0;
	int ret;

	move_buckets_wait(ctxt, buckets_in_flight, false);

	ret = bch2_btree_write_buffer_tryflush(trans);
	if (bch2_err_matches(ret, EROFS))
		return ret;

	if (bch2_fs_fatal_err_on(ret, c, "%s: from bch2_btree_write_buffer_tryflush()", bch2_err_str(ret)))
		return ret;

	bch2_trans_begin(trans);

	ret = for_each_btree_key_max(trans, iter, BTREE_ID_lru,
				  lru_pos(BCH_LRU_FRAGMENTATION_START, 0, 0),
				  lru_pos(BCH_LRU_FRAGMENTATION_START, U64_MAX, LRU_TIME_MAX),
				  0, k, ({
		struct move_bucket b = { .k.bucket = u64_to_bucket(k.k->p.offset) };
		int ret2 = 0;

		saw++;

		ret2 = bch2_bucket_is_movable(trans, &b, lru_pos_time(k.k->p));
		if (ret2 < 0)
			goto err;

		if (!ret2)
			not_movable++;
		else if (bucket_in_flight(buckets_in_flight, b.k))
			in_flight++;
		else {
			ret2 = darray_push(buckets, b);
			if (ret2)
				goto err;
			sectors += b.sectors;
		}

		ret2 = buckets->nr >= nr_to_get;
err:
		ret2;
	}));

	pr_debug("have: %zu (%zu) saw %zu in flight %zu not movable %zu got %zu (%zu)/%zu buckets ret %i",
		 buckets_in_flight->nr, buckets_in_flight->sectors,
		 saw, in_flight, not_movable, buckets->nr, sectors, nr_to_get, ret);

	return ret < 0 ? ret : 0;
}

noinline
static int bch2_copygc(struct moving_context *ctxt,
		       struct buckets_in_flight *buckets_in_flight,
		       bool *did_work)
{
	struct btree_trans *trans = ctxt->trans;
	struct bch_fs *c = trans->c;
	struct data_update_opts data_opts = {
		.btree_insert_flags = BCH_WATERMARK_copygc,
	};
	move_buckets buckets = { 0 };
	struct move_bucket_in_flight *f;
	u64 sectors_seen	= atomic64_read(&ctxt->stats->sectors_seen);
	u64 sectors_moved	= atomic64_read(&ctxt->stats->sectors_moved);
	int ret = 0;

	ret = bch2_copygc_get_buckets(ctxt, buckets_in_flight, &buckets);
	if (ret)
		goto err;

	darray_for_each(buckets, i) {
		if (kthread_should_stop() || freezing(current))
			break;

		f = move_bucket_in_flight_add(buckets_in_flight, *i);
		ret = PTR_ERR_OR_ZERO(f);
		if (ret == -EEXIST) { /* rare race: copygc_get_buckets returned same bucket more than once */
			ret = 0;
			continue;
		}
		if (ret == -ENOMEM) { /* flush IO, continue later */
			ret = 0;
			break;
		}

		ret = bch2_evacuate_bucket(ctxt, f, f->bucket.k.bucket,
					     f->bucket.k.gen, data_opts);
		if (ret)
			goto err;

		*did_work = true;
	}
err:

	/* no entries in LRU btree found, or got to end: */
	if (bch2_err_matches(ret, ENOENT))
		ret = 0;

	if (ret < 0 && !bch2_err_matches(ret, EROFS))
		bch_err_msg(c, ret, "from bch2_move_data()");

	sectors_seen	= atomic64_read(&ctxt->stats->sectors_seen) - sectors_seen;
	sectors_moved	= atomic64_read(&ctxt->stats->sectors_moved) - sectors_moved;
	trace_and_count(c, copygc, c, buckets.nr, sectors_seen, sectors_moved);

	darray_exit(&buckets);
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
	s64 wait = S64_MAX, fragmented_allowed, fragmented;

	for_each_rw_member(c, ca) {
		struct bch_dev_usage usage = bch2_dev_usage_read(ca);

		fragmented_allowed = ((__dev_buckets_available(ca, usage, BCH_WATERMARK_stripe) *
				       ca->mi.bucket_size) >> 1);
		fragmented = 0;

		for (unsigned i = 0; i < BCH_DATA_NR; i++)
			if (data_type_movable(i))
				fragmented += usage.d[i].fragmented;

		wait = min(wait, max(0LL, fragmented_allowed - fragmented));
	}

	return wait;
}

void bch2_copygc_wait_to_text(struct printbuf *out, struct bch_fs *c)
{
	printbuf_tabstop_push(out, 32);
	prt_printf(out, "running:\t%u\n",		c->copygc_running);
	prt_printf(out, "copygc_wait:\t%llu\n",		c->copygc_wait);
	prt_printf(out, "copygc_wait_at:\t%llu\n",	c->copygc_wait_at);

	prt_printf(out, "Currently waiting for:\t");
	prt_human_readable_u64(out, max(0LL, c->copygc_wait -
					atomic64_read(&c->io_clock[WRITE].now)) << 9);
	prt_newline(out);

	prt_printf(out, "Currently waiting since:\t");
	prt_human_readable_u64(out, max(0LL,
					atomic64_read(&c->io_clock[WRITE].now) -
					c->copygc_wait_at) << 9);
	prt_newline(out);

	prt_printf(out, "Currently calculated wait:\t");
	prt_human_readable_u64(out, bch2_copygc_wait_amount(c));
	prt_newline(out);
}

static int bch2_copygc_thread(void *arg)
{
	struct bch_fs *c = arg;
	struct moving_context ctxt;
	struct bch_move_stats move_stats;
	struct io_clock *clock = &c->io_clock[WRITE];
	struct buckets_in_flight *buckets;
	u64 last, wait;
	int ret = 0;

	buckets = kzalloc(sizeof(struct buckets_in_flight), GFP_KERNEL);
	if (!buckets)
		return -ENOMEM;
	ret = rhashtable_init(&buckets->table, &bch_move_bucket_params);
	bch_err_msg(c, ret, "allocating copygc buckets in flight");
	if (ret) {
		kfree(buckets);
		return ret;
	}

	set_freezable();

	bch2_move_stats_init(&move_stats, "copygc");
	bch2_moving_ctxt_init(&ctxt, c, NULL, &move_stats,
			      writepoint_ptr(&c->copygc_write_point),
			      false);

	while (!ret && !kthread_should_stop()) {
		bool did_work = false;

		bch2_trans_unlock_long(ctxt.trans);
		cond_resched();

		if (!c->opts.copygc_enabled) {
			move_buckets_wait(&ctxt, buckets, true);
			kthread_wait_freezable(c->opts.copygc_enabled ||
					       kthread_should_stop());
		}

		if (unlikely(freezing(current))) {
			move_buckets_wait(&ctxt, buckets, true);
			__refrigerator(false);
			continue;
		}

		last = atomic64_read(&clock->now);
		wait = bch2_copygc_wait_amount(c);

		if (wait > clock->max_slop) {
			c->copygc_wait_at = last;
			c->copygc_wait = last + wait;
			move_buckets_wait(&ctxt, buckets, true);
			trace_and_count(c, copygc_wait, c, wait, last + wait);
			bch2_kthread_io_clock_wait(clock, last + wait,
					MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		c->copygc_wait = 0;

		c->copygc_running = true;
		ret = bch2_copygc(&ctxt, buckets, &did_work);
		c->copygc_running = false;

		wake_up(&c->copygc_running_wq);

		if (!wait && !did_work) {
			u64 min_member_capacity = bch2_min_rw_member_capacity(c);

			if (min_member_capacity == U64_MAX)
				min_member_capacity = 128 * 2048;

			move_buckets_wait(&ctxt, buckets, true);
			bch2_kthread_io_clock_wait(clock, last + (min_member_capacity >> 6),
					MAX_SCHEDULE_TIMEOUT);
		}
	}

	move_buckets_wait(&ctxt, buckets, true);

	rhashtable_destroy(&buckets->table);
	kfree(buckets);
	bch2_moving_ctxt_exit(&ctxt);
	bch2_move_stats_exit(&move_stats, c);

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
	bch_err_msg(c, ret, "creating copygc thread");
	if (ret)
		return ret;

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
