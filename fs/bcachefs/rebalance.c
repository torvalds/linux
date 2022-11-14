// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "btree_iter.h"
#include "buckets.h"
#include "clock.h"
#include "disk_groups.h"
#include "errcode.h"
#include "extents.h"
#include "io.h"
#include "move.h"
#include "rebalance.h"
#include "super-io.h"
#include "trace.h"

#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/sched/cputime.h>

/*
 * Check if an extent should be moved:
 * returns -1 if it should not be moved, or
 * device of pointer that should be moved, if known, or INT_MAX if unknown
 */
static bool rebalance_pred(struct bch_fs *c, void *arg,
			   struct bkey_s_c k,
			   struct bch_io_opts *io_opts,
			   struct data_update_opts *data_opts)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	unsigned i;

	data_opts->rewrite_ptrs		= 0;
	data_opts->target		= io_opts->background_target;
	data_opts->extra_replicas	= 0;
	data_opts->btree_insert_flags	= 0;

	if (io_opts->background_compression &&
	    !bch2_bkey_is_incompressible(k)) {
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;

		i = 0;
		bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
			if (!p.ptr.cached &&
			    p.crc.compression_type !=
			    bch2_compression_opt_to_type[io_opts->background_compression])
				data_opts->rewrite_ptrs |= 1U << i;
			i++;
		}
	}

	if (io_opts->background_target) {
		const struct bch_extent_ptr *ptr;

		i = 0;
		bkey_for_each_ptr(ptrs, ptr) {
			if (!ptr->cached &&
			    !bch2_dev_in_target(c, ptr->dev, io_opts->background_target))
				data_opts->rewrite_ptrs |= 1U << i;
			i++;
		}
	}

	return data_opts->rewrite_ptrs != 0;
}

void bch2_rebalance_add_key(struct bch_fs *c,
			    struct bkey_s_c k,
			    struct bch_io_opts *io_opts)
{
	struct data_update_opts update_opts = { 0 };
	struct bkey_ptrs_c ptrs;
	const struct bch_extent_ptr *ptr;
	unsigned i;

	if (!rebalance_pred(c, NULL, k, io_opts, &update_opts))
		return;

	i = 0;
	ptrs = bch2_bkey_ptrs_c(k);
	bkey_for_each_ptr(ptrs, ptr) {
		if ((1U << i) && update_opts.rewrite_ptrs)
			if (atomic64_add_return(k.k->size,
					&bch_dev_bkey_exists(c, ptr->dev)->rebalance_work) ==
			    k.k->size)
				rebalance_wakeup(c);
		i++;
	}
}

void bch2_rebalance_add_work(struct bch_fs *c, u64 sectors)
{
	if (atomic64_add_return(sectors, &c->rebalance.work_unknown_dev) ==
	    sectors)
		rebalance_wakeup(c);
}

struct rebalance_work {
	int		dev_most_full_idx;
	unsigned	dev_most_full_percent;
	u64		dev_most_full_work;
	u64		dev_most_full_capacity;
	u64		total_work;
};

static void rebalance_work_accumulate(struct rebalance_work *w,
		u64 dev_work, u64 unknown_dev, u64 capacity, int idx)
{
	unsigned percent_full;
	u64 work = dev_work + unknown_dev;

	if (work < dev_work || work < unknown_dev)
		work = U64_MAX;
	work = min(work, capacity);

	percent_full = div64_u64(work * 100, capacity);

	if (percent_full >= w->dev_most_full_percent) {
		w->dev_most_full_idx		= idx;
		w->dev_most_full_percent	= percent_full;
		w->dev_most_full_work		= work;
		w->dev_most_full_capacity	= capacity;
	}

	if (w->total_work + dev_work >= w->total_work &&
	    w->total_work + dev_work >= dev_work)
		w->total_work += dev_work;
}

static struct rebalance_work rebalance_work(struct bch_fs *c)
{
	struct bch_dev *ca;
	struct rebalance_work ret = { .dev_most_full_idx = -1 };
	u64 unknown_dev = atomic64_read(&c->rebalance.work_unknown_dev);
	unsigned i;

	for_each_online_member(ca, c, i)
		rebalance_work_accumulate(&ret,
			atomic64_read(&ca->rebalance_work),
			unknown_dev,
			bucket_to_sector(ca, ca->mi.nbuckets -
					 ca->mi.first_bucket),
			i);

	rebalance_work_accumulate(&ret,
		unknown_dev, 0, c->capacity, -1);

	return ret;
}

static void rebalance_work_reset(struct bch_fs *c)
{
	struct bch_dev *ca;
	unsigned i;

	for_each_online_member(ca, c, i)
		atomic64_set(&ca->rebalance_work, 0);

	atomic64_set(&c->rebalance.work_unknown_dev, 0);
}

static unsigned long curr_cputime(void)
{
	u64 utime, stime;

	task_cputime_adjusted(current, &utime, &stime);
	return nsecs_to_jiffies(utime + stime);
}

static int bch2_rebalance_thread(void *arg)
{
	struct bch_fs *c = arg;
	struct bch_fs_rebalance *r = &c->rebalance;
	struct io_clock *clock = &c->io_clock[WRITE];
	struct rebalance_work w, p;
	struct bch_move_stats move_stats;
	unsigned long start, prev_start;
	unsigned long prev_run_time, prev_run_cputime;
	unsigned long cputime, prev_cputime;
	u64 io_start;
	long throttle;

	set_freezable();

	io_start	= atomic64_read(&clock->now);
	p		= rebalance_work(c);
	prev_start	= jiffies;
	prev_cputime	= curr_cputime();

	bch2_move_stats_init(&move_stats, "rebalance");
	while (!kthread_wait_freezable(r->enabled)) {
		cond_resched();

		start			= jiffies;
		cputime			= curr_cputime();

		prev_run_time		= start - prev_start;
		prev_run_cputime	= cputime - prev_cputime;

		w			= rebalance_work(c);
		BUG_ON(!w.dev_most_full_capacity);

		if (!w.total_work) {
			r->state = REBALANCE_WAITING;
			kthread_wait_freezable(rebalance_work(c).total_work);
			continue;
		}

		/*
		 * If there isn't much work to do, throttle cpu usage:
		 */
		throttle = prev_run_cputime * 100 /
			max(1U, w.dev_most_full_percent) -
			prev_run_time;

		if (w.dev_most_full_percent < 20 && throttle > 0) {
			r->throttled_until_iotime = io_start +
				div_u64(w.dev_most_full_capacity *
					(20 - w.dev_most_full_percent),
					50);

			if (atomic64_read(&clock->now) + clock->max_slop <
			    r->throttled_until_iotime) {
				r->throttled_until_cputime = start + throttle;
				r->state = REBALANCE_THROTTLED;

				bch2_kthread_io_clock_wait(clock,
					r->throttled_until_iotime,
					throttle);
				continue;
			}
		}

		/* minimum 1 mb/sec: */
		r->pd.rate.rate =
			max_t(u64, 1 << 11,
			      r->pd.rate.rate *
			      max(p.dev_most_full_percent, 1U) /
			      max(w.dev_most_full_percent, 1U));

		io_start	= atomic64_read(&clock->now);
		p		= w;
		prev_start	= start;
		prev_cputime	= cputime;

		r->state = REBALANCE_RUNNING;
		memset(&move_stats, 0, sizeof(move_stats));
		rebalance_work_reset(c);

		bch2_move_data(c,
			       0,		POS_MIN,
			       BTREE_ID_NR,	POS_MAX,
			       /* ratelimiting disabled for now */
			       NULL, /*  &r->pd.rate, */
			       &move_stats,
			       writepoint_ptr(&c->rebalance_write_point),
			       true,
			       rebalance_pred, NULL);
	}

	return 0;
}

void bch2_rebalance_work_to_text(struct printbuf *out, struct bch_fs *c)
{
	struct bch_fs_rebalance *r = &c->rebalance;
	struct rebalance_work w = rebalance_work(c);

	if (!out->nr_tabstops)
		printbuf_tabstop_push(out, 20);

	prt_printf(out, "fullest_dev (%i):", w.dev_most_full_idx);
	prt_tab(out);

	prt_human_readable_u64(out, w.dev_most_full_work << 9);
	prt_printf(out, "/");
	prt_human_readable_u64(out, w.dev_most_full_capacity << 9);
	prt_newline(out);

	prt_printf(out, "total work:");
	prt_tab(out);

	prt_human_readable_u64(out, w.total_work << 9);
	prt_printf(out, "/");
	prt_human_readable_u64(out, c->capacity << 9);
	prt_newline(out);

	prt_printf(out, "rate:");
	prt_tab(out);
	prt_printf(out, "%u", r->pd.rate.rate);
	prt_newline(out);

	switch (r->state) {
	case REBALANCE_WAITING:
		prt_printf(out, "waiting");
		break;
	case REBALANCE_THROTTLED:
		prt_printf(out, "throttled for %lu sec or ",
		       (r->throttled_until_cputime - jiffies) / HZ);
		prt_human_readable_u64(out,
			    (r->throttled_until_iotime -
			     atomic64_read(&c->io_clock[WRITE].now)) << 9);
		prt_printf(out, " io");
		break;
	case REBALANCE_RUNNING:
		prt_printf(out, "running");
		break;
	}
	prt_newline(out);
}

void bch2_rebalance_stop(struct bch_fs *c)
{
	struct task_struct *p;

	c->rebalance.pd.rate.rate = UINT_MAX;
	bch2_ratelimit_reset(&c->rebalance.pd.rate);

	p = rcu_dereference_protected(c->rebalance.thread, 1);
	c->rebalance.thread = NULL;

	if (p) {
		/* for sychronizing with rebalance_wakeup() */
		synchronize_rcu();

		kthread_stop(p);
		put_task_struct(p);
	}
}

int bch2_rebalance_start(struct bch_fs *c)
{
	struct task_struct *p;
	int ret;

	if (c->rebalance.thread)
		return 0;

	if (c->opts.nochanges)
		return 0;

	p = kthread_create(bch2_rebalance_thread, c, "bch-rebalance/%s", c->name);
	ret = PTR_ERR_OR_ZERO(p);
	if (ret) {
		bch_err(c, "error creating rebalance thread: %s", bch2_err_str(ret));
		return ret;
	}

	get_task_struct(p);
	rcu_assign_pointer(c->rebalance.thread, p);
	wake_up_process(p);
	return 0;
}

void bch2_fs_rebalance_init(struct bch_fs *c)
{
	bch2_pd_controller_init(&c->rebalance.pd);

	atomic64_set(&c->rebalance.work_unknown_dev, S64_MAX);
}
