// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "btree_iter.h"
#include "btree_update.h"
#include "btree_write_buffer.h"
#include "buckets.h"
#include "clock.h"
#include "compress.h"
#include "disk_groups.h"
#include "errcode.h"
#include "error.h"
#include "inode.h"
#include "io_write.h"
#include "move.h"
#include "rebalance.h"
#include "subvolume.h"
#include "super-io.h"
#include "trace.h"

#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/sched/cputime.h>

/* bch_extent_rebalance: */

static const struct bch_extent_rebalance *bch2_bkey_ptrs_rebalance_opts(struct bkey_ptrs_c ptrs)
{
	const union bch_extent_entry *entry;

	bkey_extent_entry_for_each(ptrs, entry)
		if (__extent_entry_type(entry) == BCH_EXTENT_ENTRY_rebalance)
			return &entry->rebalance;

	return NULL;
}

static const struct bch_extent_rebalance *bch2_bkey_rebalance_opts(struct bkey_s_c k)
{
	return bch2_bkey_ptrs_rebalance_opts(bch2_bkey_ptrs_c(k));
}

static inline unsigned bch2_bkey_ptrs_need_compress(struct bch_fs *c,
					   struct bch_io_opts *opts,
					   struct bkey_s_c k,
					   struct bkey_ptrs_c ptrs)
{
	if (!opts->background_compression)
		return 0;

	unsigned compression_type = bch2_compression_opt_to_type(opts->background_compression);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned ptr_bit = 1;
	unsigned rewrite_ptrs = 0;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		if (p.crc.compression_type == BCH_COMPRESSION_TYPE_incompressible ||
		    p.ptr.unwritten)
			return 0;

		if (!p.ptr.cached && p.crc.compression_type != compression_type)
			rewrite_ptrs |= ptr_bit;
		ptr_bit <<= 1;
	}

	return rewrite_ptrs;
}

static inline unsigned bch2_bkey_ptrs_need_move(struct bch_fs *c,
				       struct bch_io_opts *opts,
				       struct bkey_ptrs_c ptrs)
{
	if (!opts->background_target ||
	    !bch2_target_accepts_data(c, BCH_DATA_user, opts->background_target))
		return 0;

	unsigned ptr_bit = 1;
	unsigned rewrite_ptrs = 0;

	bkey_for_each_ptr(ptrs, ptr) {
		if (!ptr->cached && !bch2_dev_in_target(c, ptr->dev, opts->background_target))
			rewrite_ptrs |= ptr_bit;
		ptr_bit <<= 1;
	}

	return rewrite_ptrs;
}

static unsigned bch2_bkey_ptrs_need_rebalance(struct bch_fs *c,
					      struct bch_io_opts *opts,
					      struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);

	return bch2_bkey_ptrs_need_compress(c, opts, k, ptrs) |
		bch2_bkey_ptrs_need_move(c, opts, ptrs);
}

u64 bch2_bkey_sectors_need_rebalance(struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);

	const struct bch_extent_rebalance *opts = bch2_bkey_ptrs_rebalance_opts(ptrs);
	if (!opts)
		return 0;

	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	u64 sectors = 0;

	if (opts->background_compression) {
		unsigned compression_type = bch2_compression_opt_to_type(opts->background_compression);

		bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
			if (p.crc.compression_type == BCH_COMPRESSION_TYPE_incompressible ||
			    p.ptr.unwritten) {
				sectors = 0;
				goto incompressible;
			}

			if (!p.ptr.cached && p.crc.compression_type != compression_type)
				sectors += p.crc.compressed_size;
		}
	}
incompressible:
	if (opts->background_target)
		bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
			if (!p.ptr.cached && !bch2_dev_in_target(c, p.ptr.dev, opts->background_target))
				sectors += p.crc.compressed_size;

	return sectors;
}

static bool bch2_bkey_rebalance_needs_update(struct bch_fs *c, struct bch_io_opts *opts,
					     struct bkey_s_c k)
{
	if (!bkey_extent_is_direct_data(k.k))
		return 0;

	const struct bch_extent_rebalance *old = bch2_bkey_rebalance_opts(k);

	if (k.k->type == KEY_TYPE_reflink_v || bch2_bkey_ptrs_need_rebalance(c, opts, k)) {
		struct bch_extent_rebalance new = io_opts_to_rebalance_opts(c, opts);
		return old == NULL || memcmp(old, &new, sizeof(new));
	} else {
		return old != NULL;
	}
}

int bch2_bkey_set_needs_rebalance(struct bch_fs *c, struct bch_io_opts *opts,
				  struct bkey_i *_k)
{
	if (!bkey_extent_is_direct_data(&_k->k))
		return 0;

	struct bkey_s k = bkey_i_to_s(_k);
	struct bch_extent_rebalance *old =
		(struct bch_extent_rebalance *) bch2_bkey_rebalance_opts(k.s_c);

	if (k.k->type == KEY_TYPE_reflink_v || bch2_bkey_ptrs_need_rebalance(c, opts, k.s_c)) {
		if (!old) {
			old = bkey_val_end(k);
			k.k->u64s += sizeof(*old) / sizeof(u64);
		}

		*old = io_opts_to_rebalance_opts(c, opts);
	} else {
		if (old)
			extent_entry_drop(k, (union bch_extent_entry *) old);
	}

	return 0;
}

int bch2_get_update_rebalance_opts(struct btree_trans *trans,
				   struct bch_io_opts *io_opts,
				   struct btree_iter *iter,
				   struct bkey_s_c k)
{
	BUG_ON(iter->flags & BTREE_ITER_is_extents);
	BUG_ON(iter->flags & BTREE_ITER_filter_snapshots);

	const struct bch_extent_rebalance *r = k.k->type == KEY_TYPE_reflink_v
		? bch2_bkey_rebalance_opts(k) : NULL;
	if (r) {
#define x(_name)							\
		if (r->_name##_from_inode) {				\
			io_opts->_name = r->_name;			\
			io_opts->_name##_from_inode = true;		\
		}
		BCH_REBALANCE_OPTS()
#undef x
	}

	if (!bch2_bkey_rebalance_needs_update(trans->c, io_opts, k))
		return 0;

	struct bkey_i *n = bch2_trans_kmalloc(trans, bkey_bytes(k.k) + 8);
	int ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		return ret;

	bkey_reassemble(n, k);

	/* On successfull transaction commit, @k was invalidated: */

	return bch2_bkey_set_needs_rebalance(trans->c, io_opts, n) ?:
		bch2_trans_update(trans, iter, n, BTREE_UPDATE_internal_snapshot_node) ?:
		bch2_trans_commit(trans, NULL, NULL, 0) ?:
		-BCH_ERR_transaction_restart_nested;
}

#define REBALANCE_WORK_SCAN_OFFSET	(U64_MAX - 1)

static const char * const bch2_rebalance_state_strs[] = {
#define x(t) #t,
	BCH_REBALANCE_STATES()
	NULL
#undef x
};

int bch2_set_rebalance_needs_scan_trans(struct btree_trans *trans, u64 inum)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_i_cookie *cookie;
	u64 v;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_rebalance_work,
			     SPOS(inum, REBALANCE_WORK_SCAN_OFFSET, U32_MAX),
			     BTREE_ITER_intent);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	v = k.k->type == KEY_TYPE_cookie
		? le64_to_cpu(bkey_s_c_to_cookie(k).v->cookie)
		: 0;

	cookie = bch2_trans_kmalloc(trans, sizeof(*cookie));
	ret = PTR_ERR_OR_ZERO(cookie);
	if (ret)
		goto err;

	bkey_cookie_init(&cookie->k_i);
	cookie->k.p = iter.pos;
	cookie->v.cookie = cpu_to_le64(v + 1);

	ret = bch2_trans_update(trans, &iter, &cookie->k_i, 0);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_set_rebalance_needs_scan(struct bch_fs *c, u64 inum)
{
	int ret = bch2_trans_commit_do(c, NULL, NULL,
				       BCH_TRANS_COMMIT_no_enospc,
			    bch2_set_rebalance_needs_scan_trans(trans, inum));
	rebalance_wakeup(c);
	return ret;
}

int bch2_set_fs_needs_rebalance(struct bch_fs *c)
{
	return bch2_set_rebalance_needs_scan(c, 0);
}

static int bch2_clear_rebalance_needs_scan(struct btree_trans *trans, u64 inum, u64 cookie)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u64 v;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_rebalance_work,
			     SPOS(inum, REBALANCE_WORK_SCAN_OFFSET, U32_MAX),
			     BTREE_ITER_intent);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	v = k.k->type == KEY_TYPE_cookie
		? le64_to_cpu(bkey_s_c_to_cookie(k).v->cookie)
		: 0;

	if (v == cookie)
		ret = bch2_btree_delete_at(trans, &iter, 0);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static struct bkey_s_c next_rebalance_entry(struct btree_trans *trans,
					    struct btree_iter *work_iter)
{
	return !kthread_should_stop()
		? bch2_btree_iter_peek(work_iter)
		: bkey_s_c_null;
}

static int bch2_bkey_clear_needs_rebalance(struct btree_trans *trans,
					   struct btree_iter *iter,
					   struct bkey_s_c k)
{
	if (!bch2_bkey_rebalance_opts(k))
		return 0;

	struct bkey_i *n = bch2_bkey_make_mut(trans, iter, &k, 0);
	int ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		return ret;

	extent_entry_drop(bkey_i_to_s(n),
			  (void *) bch2_bkey_rebalance_opts(bkey_i_to_s_c(n)));
	return bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
}

static struct bkey_s_c next_rebalance_extent(struct btree_trans *trans,
			struct bpos work_pos,
			struct btree_iter *extent_iter,
			struct bch_io_opts *io_opts,
			struct data_update_opts *data_opts)
{
	struct bch_fs *c = trans->c;

	bch2_trans_iter_exit(trans, extent_iter);
	bch2_trans_iter_init(trans, extent_iter,
			     work_pos.inode ? BTREE_ID_extents : BTREE_ID_reflink,
			     work_pos,
			     BTREE_ITER_all_snapshots);
	struct bkey_s_c k = bch2_btree_iter_peek_slot(extent_iter);
	if (bkey_err(k))
		return k;

	int ret = bch2_move_get_io_opts_one(trans, io_opts, extent_iter, k);
	if (ret)
		return bkey_s_c_err(ret);

	memset(data_opts, 0, sizeof(*data_opts));
	data_opts->rewrite_ptrs		= bch2_bkey_ptrs_need_rebalance(c, io_opts, k);
	data_opts->target		= io_opts->background_target;
	data_opts->write_flags		|= BCH_WRITE_only_specified_devs;

	if (!data_opts->rewrite_ptrs) {
		/*
		 * device we would want to write to offline? devices in target
		 * changed?
		 *
		 * We'll now need a full scan before this extent is picked up
		 * again:
		 */
		int ret = bch2_bkey_clear_needs_rebalance(trans, extent_iter, k);
		if (ret)
			return bkey_s_c_err(ret);
		return bkey_s_c_null;
	}

	if (trace_rebalance_extent_enabled()) {
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, k);
		prt_newline(&buf);

		struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);

		unsigned p = bch2_bkey_ptrs_need_compress(c, io_opts, k, ptrs);
		if (p) {
			prt_str(&buf, "compression=");
			bch2_compression_opt_to_text(&buf, io_opts->background_compression);
			prt_str(&buf, " ");
			bch2_prt_u64_base2(&buf, p);
			prt_newline(&buf);
		}

		p = bch2_bkey_ptrs_need_move(c, io_opts, ptrs);
		if (p) {
			prt_str(&buf, "move=");
			bch2_target_to_text(&buf, c, io_opts->background_target);
			prt_str(&buf, " ");
			bch2_prt_u64_base2(&buf, p);
			prt_newline(&buf);
		}

		trace_rebalance_extent(c, buf.buf);
		printbuf_exit(&buf);
	}

	return k;
}

noinline_for_stack
static int do_rebalance_extent(struct moving_context *ctxt,
			       struct bpos work_pos,
			       struct btree_iter *extent_iter)
{
	struct btree_trans *trans = ctxt->trans;
	struct bch_fs *c = trans->c;
	struct bch_fs_rebalance *r = &trans->c->rebalance;
	struct data_update_opts data_opts;
	struct bch_io_opts io_opts;
	struct bkey_s_c k;
	struct bkey_buf sk;
	int ret;

	ctxt->stats = &r->work_stats;
	r->state = BCH_REBALANCE_working;

	bch2_bkey_buf_init(&sk);

	ret = bkey_err(k = next_rebalance_extent(trans, work_pos,
				extent_iter, &io_opts, &data_opts));
	if (ret || !k.k)
		goto out;

	atomic64_add(k.k->size, &ctxt->stats->sectors_seen);

	/*
	 * The iterator gets unlocked by __bch2_read_extent - need to
	 * save a copy of @k elsewhere:
	 */
	bch2_bkey_buf_reassemble(&sk, c, k);
	k = bkey_i_to_s_c(sk.k);

	ret = bch2_move_extent(ctxt, NULL, extent_iter, k, io_opts, data_opts);
	if (ret) {
		if (bch2_err_matches(ret, ENOMEM)) {
			/* memory allocation failure, wait for some IO to finish */
			bch2_move_ctxt_wait_for_io(ctxt);
			ret = -BCH_ERR_transaction_restart_nested;
		}

		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			goto out;

		/* skip it and continue, XXX signal failure */
		ret = 0;
	}
out:
	bch2_bkey_buf_exit(&sk, c);
	return ret;
}

static bool rebalance_pred(struct bch_fs *c, void *arg,
			   struct bkey_s_c k,
			   struct bch_io_opts *io_opts,
			   struct data_update_opts *data_opts)
{
	data_opts->rewrite_ptrs		= bch2_bkey_ptrs_need_rebalance(c, io_opts, k);
	data_opts->target		= io_opts->background_target;
	data_opts->write_flags		|= BCH_WRITE_only_specified_devs;
	return data_opts->rewrite_ptrs != 0;
}

static int do_rebalance_scan(struct moving_context *ctxt, u64 inum, u64 cookie)
{
	struct btree_trans *trans = ctxt->trans;
	struct bch_fs_rebalance *r = &trans->c->rebalance;
	int ret;

	bch2_move_stats_init(&r->scan_stats, "rebalance_scan");
	ctxt->stats = &r->scan_stats;

	if (!inum) {
		r->scan_start	= BBPOS_MIN;
		r->scan_end	= BBPOS_MAX;
	} else {
		r->scan_start	= BBPOS(BTREE_ID_extents, POS(inum, 0));
		r->scan_end	= BBPOS(BTREE_ID_extents, POS(inum, U64_MAX));
	}

	r->state = BCH_REBALANCE_scanning;

	ret = __bch2_move_data(ctxt, r->scan_start, r->scan_end, rebalance_pred, NULL) ?:
		commit_do(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc,
			  bch2_clear_rebalance_needs_scan(trans, inum, cookie));

	bch2_move_stats_exit(&r->scan_stats, trans->c);
	return ret;
}

static void rebalance_wait(struct bch_fs *c)
{
	struct bch_fs_rebalance *r = &c->rebalance;
	struct io_clock *clock = &c->io_clock[WRITE];
	u64 now = atomic64_read(&clock->now);
	u64 min_member_capacity = bch2_min_rw_member_capacity(c);

	if (min_member_capacity == U64_MAX)
		min_member_capacity = 128 * 2048;

	r->wait_iotime_end		= now + (min_member_capacity >> 6);

	if (r->state != BCH_REBALANCE_waiting) {
		r->wait_iotime_start	= now;
		r->wait_wallclock_start	= ktime_get_real_ns();
		r->state		= BCH_REBALANCE_waiting;
	}

	bch2_kthread_io_clock_wait(clock, r->wait_iotime_end, MAX_SCHEDULE_TIMEOUT);
}

static int do_rebalance(struct moving_context *ctxt)
{
	struct btree_trans *trans = ctxt->trans;
	struct bch_fs *c = trans->c;
	struct bch_fs_rebalance *r = &c->rebalance;
	struct btree_iter rebalance_work_iter, extent_iter = { NULL };
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_begin(trans);

	bch2_move_stats_init(&r->work_stats, "rebalance_work");
	bch2_move_stats_init(&r->scan_stats, "rebalance_scan");

	bch2_trans_iter_init(trans, &rebalance_work_iter,
			     BTREE_ID_rebalance_work, POS_MIN,
			     BTREE_ITER_all_snapshots);

	while (!bch2_move_ratelimit(ctxt)) {
		if (!c->opts.rebalance_enabled) {
			bch2_moving_ctxt_flush_all(ctxt);
			kthread_wait_freezable(c->opts.rebalance_enabled ||
					       kthread_should_stop());
		}

		if (kthread_should_stop())
			break;

		bch2_trans_begin(trans);

		ret = bkey_err(k = next_rebalance_entry(trans, &rebalance_work_iter));
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret || !k.k)
			break;

		ret = k.k->type == KEY_TYPE_cookie
			? do_rebalance_scan(ctxt, k.k->p.inode,
					    le64_to_cpu(bkey_s_c_to_cookie(k).v->cookie))
			: do_rebalance_extent(ctxt, k.k->p, &extent_iter);

		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;

		bch2_btree_iter_advance(&rebalance_work_iter);
	}

	bch2_trans_iter_exit(trans, &extent_iter);
	bch2_trans_iter_exit(trans, &rebalance_work_iter);
	bch2_move_stats_exit(&r->scan_stats, c);

	if (!ret &&
	    !kthread_should_stop() &&
	    !atomic64_read(&r->work_stats.sectors_seen) &&
	    !atomic64_read(&r->scan_stats.sectors_seen)) {
		bch2_moving_ctxt_flush_all(ctxt);
		bch2_trans_unlock_long(trans);
		rebalance_wait(c);
	}

	if (!bch2_err_matches(ret, EROFS))
		bch_err_fn(c, ret);
	return ret;
}

static int bch2_rebalance_thread(void *arg)
{
	struct bch_fs *c = arg;
	struct bch_fs_rebalance *r = &c->rebalance;
	struct moving_context ctxt;

	set_freezable();

	bch2_moving_ctxt_init(&ctxt, c, NULL, &r->work_stats,
			      writepoint_ptr(&c->rebalance_write_point),
			      true);

	while (!kthread_should_stop() && !do_rebalance(&ctxt))
		;

	bch2_moving_ctxt_exit(&ctxt);

	return 0;
}

void bch2_rebalance_status_to_text(struct printbuf *out, struct bch_fs *c)
{
	printbuf_tabstop_push(out, 32);

	struct bch_fs_rebalance *r = &c->rebalance;

	/* print pending work */
	struct disk_accounting_pos acc = { .type = BCH_DISK_ACCOUNTING_rebalance_work, };
	u64 v;
	bch2_accounting_mem_read(c, disk_accounting_pos_to_bpos(&acc), &v, 1);

	prt_printf(out, "pending work:\t");
	prt_human_readable_u64(out, v);
	prt_printf(out, "\n\n");

	prt_str(out, bch2_rebalance_state_strs[r->state]);
	prt_newline(out);
	printbuf_indent_add(out, 2);

	switch (r->state) {
	case BCH_REBALANCE_waiting: {
		u64 now = atomic64_read(&c->io_clock[WRITE].now);

		prt_printf(out, "io wait duration:\t");
		bch2_prt_human_readable_s64(out, (r->wait_iotime_end - r->wait_iotime_start) << 9);
		prt_newline(out);

		prt_printf(out, "io wait remaining:\t");
		bch2_prt_human_readable_s64(out, (r->wait_iotime_end - now) << 9);
		prt_newline(out);

		prt_printf(out, "duration waited:\t");
		bch2_pr_time_units(out, ktime_get_real_ns() - r->wait_wallclock_start);
		prt_newline(out);
		break;
	}
	case BCH_REBALANCE_working:
		bch2_move_stats_to_text(out, &r->work_stats);
		break;
	case BCH_REBALANCE_scanning:
		bch2_move_stats_to_text(out, &r->scan_stats);
		break;
	}
	prt_newline(out);

	rcu_read_lock();
	struct task_struct *t = rcu_dereference(c->rebalance.thread);
	if (t)
		get_task_struct(t);
	rcu_read_unlock();

	if (t) {
		bch2_prt_task_backtrace(out, t, 0, GFP_KERNEL);
		put_task_struct(t);
	}

	printbuf_indent_sub(out, 2);
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
	bch_err_msg(c, ret, "creating rebalance thread");
	if (ret)
		return ret;

	get_task_struct(p);
	rcu_assign_pointer(c->rebalance.thread, p);
	wake_up_process(p);
	return 0;
}

void bch2_fs_rebalance_init(struct bch_fs *c)
{
	bch2_pd_controller_init(&c->rebalance.pd);
}
