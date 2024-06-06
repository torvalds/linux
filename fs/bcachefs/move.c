// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "backpointers.h"
#include "bkey_buf.h"
#include "btree_gc.h"
#include "btree_io.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_write_buffer.h"
#include "compress.h"
#include "disk_groups.h"
#include "ec.h"
#include "errcode.h"
#include "error.h"
#include "inode.h"
#include "io_read.h"
#include "io_write.h"
#include "journal_reclaim.h"
#include "keylist.h"
#include "move.h"
#include "replicas.h"
#include "snapshot.h"
#include "super-io.h"
#include "trace.h"

#include <linux/ioprio.h>
#include <linux/kthread.h>

const char * const bch2_data_ops_strs[] = {
#define x(t, n, ...) [n] = #t,
	BCH_DATA_OPS()
#undef x
	NULL
};

static void bch2_data_update_opts_to_text(struct printbuf *out, struct bch_fs *c,
					  struct bch_io_opts *io_opts,
					  struct data_update_opts *data_opts)
{
	printbuf_tabstop_push(out, 20);
	prt_str(out, "rewrite ptrs:\t");
	bch2_prt_u64_base2(out, data_opts->rewrite_ptrs);
	prt_newline(out);

	prt_str(out, "kill ptrs:\t");
	bch2_prt_u64_base2(out, data_opts->kill_ptrs);
	prt_newline(out);

	prt_str(out, "target:\t");
	bch2_target_to_text(out, c, data_opts->target);
	prt_newline(out);

	prt_str(out, "compression:\t");
	bch2_compression_opt_to_text(out, background_compression(*io_opts));
	prt_newline(out);

	prt_str(out, "extra replicas:\t");
	prt_u64(out, data_opts->extra_replicas);
}

static void trace_move_extent2(struct bch_fs *c, struct bkey_s_c k,
			       struct bch_io_opts *io_opts,
			       struct data_update_opts *data_opts)
{
	if (trace_move_extent_enabled()) {
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, k);
		prt_newline(&buf);
		bch2_data_update_opts_to_text(&buf, c, io_opts, data_opts);
		trace_move_extent(c, buf.buf);
		printbuf_exit(&buf);
	}
}

static void trace_move_extent_read2(struct bch_fs *c, struct bkey_s_c k)
{
	if (trace_move_extent_read_enabled()) {
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, k);
		trace_move_extent_read(c, buf.buf);
		printbuf_exit(&buf);
	}
}

struct moving_io {
	struct list_head		read_list;
	struct list_head		io_list;
	struct move_bucket_in_flight	*b;
	struct closure			cl;
	bool				read_completed;

	unsigned			read_sectors;
	unsigned			write_sectors;

	struct bch_read_bio		rbio;

	struct data_update		write;
	/* Must be last since it is variable size */
	struct bio_vec			bi_inline_vecs[];
};

static void move_free(struct moving_io *io)
{
	struct moving_context *ctxt = io->write.ctxt;

	if (io->b)
		atomic_dec(&io->b->count);

	bch2_data_update_exit(&io->write);

	mutex_lock(&ctxt->lock);
	list_del(&io->io_list);
	wake_up(&ctxt->wait);
	mutex_unlock(&ctxt->lock);

	kfree(io);
}

static void move_write_done(struct bch_write_op *op)
{
	struct moving_io *io = container_of(op, struct moving_io, write.op);
	struct moving_context *ctxt = io->write.ctxt;

	if (io->write.op.error)
		ctxt->write_error = true;

	atomic_sub(io->write_sectors, &io->write.ctxt->write_sectors);
	atomic_dec(&io->write.ctxt->write_ios);
	move_free(io);
	closure_put(&ctxt->cl);
}

static void move_write(struct moving_io *io)
{
	if (unlikely(io->rbio.bio.bi_status || io->rbio.hole)) {
		move_free(io);
		return;
	}

	if (trace_move_extent_write_enabled()) {
		struct bch_fs *c = io->write.op.c;
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(io->write.k.k));
		trace_move_extent_write(c, buf.buf);
		printbuf_exit(&buf);
	}

	closure_get(&io->write.ctxt->cl);
	atomic_add(io->write_sectors, &io->write.ctxt->write_sectors);
	atomic_inc(&io->write.ctxt->write_ios);

	bch2_data_update_read_done(&io->write, io->rbio.pick.crc);
}

struct moving_io *bch2_moving_ctxt_next_pending_write(struct moving_context *ctxt)
{
	struct moving_io *io =
		list_first_entry_or_null(&ctxt->reads, struct moving_io, read_list);

	return io && io->read_completed ? io : NULL;
}

static void move_read_endio(struct bio *bio)
{
	struct moving_io *io = container_of(bio, struct moving_io, rbio.bio);
	struct moving_context *ctxt = io->write.ctxt;

	atomic_sub(io->read_sectors, &ctxt->read_sectors);
	atomic_dec(&ctxt->read_ios);
	io->read_completed = true;

	wake_up(&ctxt->wait);
	closure_put(&ctxt->cl);
}

void bch2_moving_ctxt_do_pending_writes(struct moving_context *ctxt)
{
	struct moving_io *io;

	while ((io = bch2_moving_ctxt_next_pending_write(ctxt))) {
		bch2_trans_unlock_long(ctxt->trans);
		list_del(&io->read_list);
		move_write(io);
	}
}

void bch2_move_ctxt_wait_for_io(struct moving_context *ctxt)
{
	unsigned sectors_pending = atomic_read(&ctxt->write_sectors);

	move_ctxt_wait_event(ctxt,
		!atomic_read(&ctxt->write_sectors) ||
		atomic_read(&ctxt->write_sectors) != sectors_pending);
}

void bch2_moving_ctxt_flush_all(struct moving_context *ctxt)
{
	move_ctxt_wait_event(ctxt, list_empty(&ctxt->reads));
	bch2_trans_unlock_long(ctxt->trans);
	closure_sync(&ctxt->cl);
}

void bch2_moving_ctxt_exit(struct moving_context *ctxt)
{
	struct bch_fs *c = ctxt->trans->c;

	bch2_moving_ctxt_flush_all(ctxt);

	EBUG_ON(atomic_read(&ctxt->write_sectors));
	EBUG_ON(atomic_read(&ctxt->write_ios));
	EBUG_ON(atomic_read(&ctxt->read_sectors));
	EBUG_ON(atomic_read(&ctxt->read_ios));

	mutex_lock(&c->moving_context_lock);
	list_del(&ctxt->list);
	mutex_unlock(&c->moving_context_lock);

	bch2_trans_put(ctxt->trans);
	memset(ctxt, 0, sizeof(*ctxt));
}

void bch2_moving_ctxt_init(struct moving_context *ctxt,
			   struct bch_fs *c,
			   struct bch_ratelimit *rate,
			   struct bch_move_stats *stats,
			   struct write_point_specifier wp,
			   bool wait_on_copygc)
{
	memset(ctxt, 0, sizeof(*ctxt));

	ctxt->trans	= bch2_trans_get(c);
	ctxt->fn	= (void *) _RET_IP_;
	ctxt->rate	= rate;
	ctxt->stats	= stats;
	ctxt->wp	= wp;
	ctxt->wait_on_copygc = wait_on_copygc;

	closure_init_stack(&ctxt->cl);

	mutex_init(&ctxt->lock);
	INIT_LIST_HEAD(&ctxt->reads);
	INIT_LIST_HEAD(&ctxt->ios);
	init_waitqueue_head(&ctxt->wait);

	mutex_lock(&c->moving_context_lock);
	list_add(&ctxt->list, &c->moving_context_list);
	mutex_unlock(&c->moving_context_lock);
}

void bch2_move_stats_exit(struct bch_move_stats *stats, struct bch_fs *c)
{
	trace_move_data(c, stats);
}

void bch2_move_stats_init(struct bch_move_stats *stats, const char *name)
{
	memset(stats, 0, sizeof(*stats));
	stats->data_type = BCH_DATA_user;
	scnprintf(stats->name, sizeof(stats->name), "%s", name);
}

int bch2_move_extent(struct moving_context *ctxt,
		     struct move_bucket_in_flight *bucket_in_flight,
		     struct btree_iter *iter,
		     struct bkey_s_c k,
		     struct bch_io_opts io_opts,
		     struct data_update_opts data_opts)
{
	struct btree_trans *trans = ctxt->trans;
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	struct moving_io *io;
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned sectors = k.k->size, pages;
	int ret = -ENOMEM;

	trace_move_extent2(c, k, &io_opts, &data_opts);

	if (ctxt->stats)
		ctxt->stats->pos = BBPOS(iter->btree_id, iter->pos);

	bch2_data_update_opts_normalize(k, &data_opts);

	if (!data_opts.rewrite_ptrs &&
	    !data_opts.extra_replicas) {
		if (data_opts.kill_ptrs)
			return bch2_extent_drop_ptrs(trans, iter, k, data_opts);
		return 0;
	}

	/*
	 * Before memory allocations & taking nocow locks in
	 * bch2_data_update_init():
	 */
	bch2_trans_unlock(trans);

	/* write path might have to decompress data: */
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
		sectors = max_t(unsigned, sectors, p.crc.uncompressed_size);

	pages = DIV_ROUND_UP(sectors, PAGE_SECTORS);
	io = kzalloc(sizeof(struct moving_io) +
		     sizeof(struct bio_vec) * pages, GFP_KERNEL);
	if (!io)
		goto err;

	INIT_LIST_HEAD(&io->io_list);
	io->write.ctxt		= ctxt;
	io->read_sectors	= k.k->size;
	io->write_sectors	= k.k->size;

	bio_init(&io->write.op.wbio.bio, NULL, io->bi_inline_vecs, pages, 0);
	bio_set_prio(&io->write.op.wbio.bio,
		     IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0));

	if (bch2_bio_alloc_pages(&io->write.op.wbio.bio, sectors << 9,
				 GFP_KERNEL))
		goto err_free;

	io->rbio.c		= c;
	io->rbio.opts		= io_opts;
	bio_init(&io->rbio.bio, NULL, io->bi_inline_vecs, pages, 0);
	io->rbio.bio.bi_vcnt = pages;
	bio_set_prio(&io->rbio.bio, IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0));
	io->rbio.bio.bi_iter.bi_size = sectors << 9;

	io->rbio.bio.bi_opf		= REQ_OP_READ;
	io->rbio.bio.bi_iter.bi_sector	= bkey_start_offset(k.k);
	io->rbio.bio.bi_end_io		= move_read_endio;

	ret = bch2_data_update_init(trans, iter, ctxt, &io->write, ctxt->wp,
				    io_opts, data_opts, iter->btree_id, k);
	if (ret)
		goto err_free_pages;

	io->write.op.end_io = move_write_done;

	if (ctxt->rate)
		bch2_ratelimit_increment(ctxt->rate, k.k->size);

	if (ctxt->stats) {
		atomic64_inc(&ctxt->stats->keys_moved);
		atomic64_add(k.k->size, &ctxt->stats->sectors_moved);
	}

	if (bucket_in_flight) {
		io->b = bucket_in_flight;
		atomic_inc(&io->b->count);
	}

	this_cpu_add(c->counters[BCH_COUNTER_io_move], k.k->size);
	this_cpu_add(c->counters[BCH_COUNTER_move_extent_read], k.k->size);
	trace_move_extent_read2(c, k);

	mutex_lock(&ctxt->lock);
	atomic_add(io->read_sectors, &ctxt->read_sectors);
	atomic_inc(&ctxt->read_ios);

	list_add_tail(&io->read_list, &ctxt->reads);
	list_add_tail(&io->io_list, &ctxt->ios);
	mutex_unlock(&ctxt->lock);

	/*
	 * dropped by move_read_endio() - guards against use after free of
	 * ctxt when doing wakeup
	 */
	closure_get(&ctxt->cl);
	bch2_read_extent(trans, &io->rbio,
			 bkey_start_pos(k.k),
			 iter->btree_id, k, 0,
			 BCH_READ_NODECODE|
			 BCH_READ_LAST_FRAGMENT);
	return 0;
err_free_pages:
	bio_free_pages(&io->write.op.wbio.bio);
err_free:
	kfree(io);
err:
	if (ret == -BCH_ERR_data_update_done)
		return 0;

	if (bch2_err_matches(ret, EROFS) ||
	    bch2_err_matches(ret, BCH_ERR_transaction_restart))
		return ret;

	count_event(c, move_extent_start_fail);

	if (trace_move_extent_start_fail_enabled()) {
		struct printbuf buf = PRINTBUF;

		bch2_bkey_val_to_text(&buf, c, k);
		prt_str(&buf, ": ");
		prt_str(&buf, bch2_err_str(ret));
		trace_move_extent_start_fail(c, buf.buf);
		printbuf_exit(&buf);
	}
	return ret;
}

struct bch_io_opts *bch2_move_get_io_opts(struct btree_trans *trans,
			  struct per_snapshot_io_opts *io_opts,
			  struct bkey_s_c extent_k)
{
	struct bch_fs *c = trans->c;
	u32 restart_count = trans->restart_count;
	int ret = 0;

	if (io_opts->cur_inum != extent_k.k->p.inode) {
		io_opts->d.nr = 0;

		ret = for_each_btree_key(trans, iter, BTREE_ID_inodes, POS(0, extent_k.k->p.inode),
					 BTREE_ITER_all_snapshots, k, ({
			if (k.k->p.offset != extent_k.k->p.inode)
				break;

			if (!bkey_is_inode(k.k))
				continue;

			struct bch_inode_unpacked inode;
			BUG_ON(bch2_inode_unpack(k, &inode));

			struct snapshot_io_opts_entry e = { .snapshot = k.k->p.snapshot };
			bch2_inode_opts_get(&e.io_opts, trans->c, &inode);

			darray_push(&io_opts->d, e);
		}));
		io_opts->cur_inum = extent_k.k->p.inode;
	}

	ret = ret ?: trans_was_restarted(trans, restart_count);
	if (ret)
		return ERR_PTR(ret);

	if (extent_k.k->p.snapshot)
		darray_for_each(io_opts->d, i)
			if (bch2_snapshot_is_ancestor(c, extent_k.k->p.snapshot, i->snapshot))
				return &i->io_opts;

	return &io_opts->fs_io_opts;
}

int bch2_move_get_io_opts_one(struct btree_trans *trans,
			      struct bch_io_opts *io_opts,
			      struct bkey_s_c extent_k)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	/* reflink btree? */
	if (!extent_k.k->p.inode) {
		*io_opts = bch2_opts_to_inode_opts(trans->c->opts);
		return 0;
	}

	k = bch2_bkey_get_iter(trans, &iter, BTREE_ID_inodes,
			       SPOS(0, extent_k.k->p.inode, extent_k.k->p.snapshot),
			       BTREE_ITER_cached);
	ret = bkey_err(k);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		return ret;

	if (!ret && bkey_is_inode(k.k)) {
		struct bch_inode_unpacked inode;
		bch2_inode_unpack(k, &inode);
		bch2_inode_opts_get(io_opts, trans->c, &inode);
	} else {
		*io_opts = bch2_opts_to_inode_opts(trans->c->opts);
	}

	bch2_trans_iter_exit(trans, &iter);
	return 0;
}

int bch2_move_ratelimit(struct moving_context *ctxt)
{
	struct bch_fs *c = ctxt->trans->c;
	bool is_kthread = current->flags & PF_KTHREAD;
	u64 delay;

	if (ctxt->wait_on_copygc && c->copygc_running) {
		bch2_moving_ctxt_flush_all(ctxt);
		wait_event_killable(c->copygc_running_wq,
				    !c->copygc_running ||
				    (is_kthread && kthread_should_stop()));
	}

	do {
		delay = ctxt->rate ? bch2_ratelimit_delay(ctxt->rate) : 0;

		if (is_kthread && kthread_should_stop())
			return 1;

		if (delay)
			move_ctxt_wait_event_timeout(ctxt,
					freezing(current) ||
					(is_kthread && kthread_should_stop()),
					delay);

		if (unlikely(freezing(current))) {
			bch2_moving_ctxt_flush_all(ctxt);
			try_to_freeze();
		}
	} while (delay);

	/*
	 * XXX: these limits really ought to be per device, SSDs and hard drives
	 * will want different limits
	 */
	move_ctxt_wait_event(ctxt,
		atomic_read(&ctxt->write_sectors) < c->opts.move_bytes_in_flight >> 9 &&
		atomic_read(&ctxt->read_sectors) < c->opts.move_bytes_in_flight >> 9 &&
		atomic_read(&ctxt->write_ios) < c->opts.move_ios_in_flight &&
		atomic_read(&ctxt->read_ios) < c->opts.move_ios_in_flight);

	return 0;
}

static int bch2_move_data_btree(struct moving_context *ctxt,
				struct bpos start,
				struct bpos end,
				move_pred_fn pred, void *arg,
				enum btree_id btree_id)
{
	struct btree_trans *trans = ctxt->trans;
	struct bch_fs *c = trans->c;
	struct per_snapshot_io_opts snapshot_io_opts;
	struct bch_io_opts *io_opts;
	struct bkey_buf sk;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct data_update_opts data_opts;
	int ret = 0, ret2;

	per_snapshot_io_opts_init(&snapshot_io_opts, c);
	bch2_bkey_buf_init(&sk);

	if (ctxt->stats) {
		ctxt->stats->data_type	= BCH_DATA_user;
		ctxt->stats->pos	= BBPOS(btree_id, start);
	}

	bch2_trans_begin(trans);
	bch2_trans_iter_init(trans, &iter, btree_id, start,
			     BTREE_ITER_prefetch|
			     BTREE_ITER_all_snapshots);

	if (ctxt->rate)
		bch2_ratelimit_reset(ctxt->rate);

	while (!bch2_move_ratelimit(ctxt)) {
		bch2_trans_begin(trans);

		k = bch2_btree_iter_peek(&iter);
		if (!k.k)
			break;

		ret = bkey_err(k);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			break;

		if (bkey_ge(bkey_start_pos(k.k), end))
			break;

		if (ctxt->stats)
			ctxt->stats->pos = BBPOS(iter.btree_id, iter.pos);

		if (!bkey_extent_is_direct_data(k.k))
			goto next_nondata;

		io_opts = bch2_move_get_io_opts(trans, &snapshot_io_opts, k);
		ret = PTR_ERR_OR_ZERO(io_opts);
		if (ret)
			continue;

		memset(&data_opts, 0, sizeof(data_opts));
		if (!pred(c, arg, k, io_opts, &data_opts))
			goto next;

		/*
		 * The iterator gets unlocked by __bch2_read_extent - need to
		 * save a copy of @k elsewhere:
		 */
		bch2_bkey_buf_reassemble(&sk, c, k);
		k = bkey_i_to_s_c(sk.k);

		ret2 = bch2_move_extent(ctxt, NULL, &iter, k, *io_opts, data_opts);
		if (ret2) {
			if (bch2_err_matches(ret2, BCH_ERR_transaction_restart))
				continue;

			if (ret2 == -ENOMEM) {
				/* memory allocation failure, wait for some IO to finish */
				bch2_move_ctxt_wait_for_io(ctxt);
				continue;
			}

			/* XXX signal failure */
			goto next;
		}
next:
		if (ctxt->stats)
			atomic64_add(k.k->size, &ctxt->stats->sectors_seen);
next_nondata:
		bch2_btree_iter_advance(&iter);
	}

	bch2_trans_iter_exit(trans, &iter);
	bch2_bkey_buf_exit(&sk, c);
	per_snapshot_io_opts_exit(&snapshot_io_opts);

	return ret;
}

int __bch2_move_data(struct moving_context *ctxt,
		     struct bbpos start,
		     struct bbpos end,
		     move_pred_fn pred, void *arg)
{
	struct bch_fs *c = ctxt->trans->c;
	enum btree_id id;
	int ret = 0;

	for (id = start.btree;
	     id <= min_t(unsigned, end.btree, btree_id_nr_alive(c) - 1);
	     id++) {
		ctxt->stats->pos = BBPOS(id, POS_MIN);

		if (!btree_type_has_ptrs(id) ||
		    !bch2_btree_id_root(c, id)->b)
			continue;

		ret = bch2_move_data_btree(ctxt,
				       id == start.btree ? start.pos : POS_MIN,
				       id == end.btree   ? end.pos   : POS_MAX,
				       pred, arg, id);
		if (ret)
			break;
	}

	return ret;
}

int bch2_move_data(struct bch_fs *c,
		   struct bbpos start,
		   struct bbpos end,
		   struct bch_ratelimit *rate,
		   struct bch_move_stats *stats,
		   struct write_point_specifier wp,
		   bool wait_on_copygc,
		   move_pred_fn pred, void *arg)
{

	struct moving_context ctxt;
	int ret;

	bch2_moving_ctxt_init(&ctxt, c, rate, stats, wp, wait_on_copygc);
	ret = __bch2_move_data(&ctxt, start, end, pred, arg);
	bch2_moving_ctxt_exit(&ctxt);

	return ret;
}

int bch2_evacuate_bucket(struct moving_context *ctxt,
			   struct move_bucket_in_flight *bucket_in_flight,
			   struct bpos bucket, int gen,
			   struct data_update_opts _data_opts)
{
	struct btree_trans *trans = ctxt->trans;
	struct bch_fs *c = trans->c;
	bool is_kthread = current->flags & PF_KTHREAD;
	struct bch_io_opts io_opts = bch2_opts_to_inode_opts(c->opts);
	struct btree_iter iter;
	struct bkey_buf sk;
	struct bch_backpointer bp;
	struct bch_alloc_v4 a_convert;
	const struct bch_alloc_v4 *a;
	struct bkey_s_c k;
	struct data_update_opts data_opts;
	unsigned dirty_sectors, bucket_size;
	u64 fragmentation;
	struct bpos bp_pos = POS_MIN;
	int ret = 0;

	struct bch_dev *ca = bch2_dev_tryget(c, bucket.inode);
	if (!ca)
		return 0;

	trace_bucket_evacuate(c, &bucket);

	bch2_bkey_buf_init(&sk);

	/*
	 * We're not run in a context that handles transaction restarts:
	 */
	bch2_trans_begin(trans);

	bch2_trans_iter_init(trans, &iter, BTREE_ID_alloc,
			     bucket, BTREE_ITER_cached);
	ret = lockrestart_do(trans,
			bkey_err(k = bch2_btree_iter_peek_slot(&iter)));
	bch2_trans_iter_exit(trans, &iter);

	bch_err_msg(c, ret, "looking up alloc key");
	if (ret)
		goto err;

	a = bch2_alloc_to_v4(k, &a_convert);
	dirty_sectors = bch2_bucket_sectors_dirty(*a);
	bucket_size = ca->mi.bucket_size;
	fragmentation = a->fragmentation_lru;

	ret = bch2_btree_write_buffer_tryflush(trans);
	bch_err_msg(c, ret, "flushing btree write buffer");
	if (ret)
		goto err;

	while (!(ret = bch2_move_ratelimit(ctxt))) {
		if (is_kthread && kthread_should_stop())
			break;

		bch2_trans_begin(trans);

		ret = bch2_get_next_backpointer(trans, ca, bucket, gen,
						&bp_pos, &bp,
						BTREE_ITER_cached);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			goto err;
		if (bkey_eq(bp_pos, POS_MAX))
			break;

		if (!bp.level) {
			k = bch2_backpointer_get_key(trans, &iter, bp_pos, bp, 0);
			ret = bkey_err(k);
			if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
				continue;
			if (ret)
				goto err;
			if (!k.k)
				goto next;

			bch2_bkey_buf_reassemble(&sk, c, k);
			k = bkey_i_to_s_c(sk.k);

			ret = bch2_move_get_io_opts_one(trans, &io_opts, k);
			if (ret) {
				bch2_trans_iter_exit(trans, &iter);
				continue;
			}

			data_opts = _data_opts;
			data_opts.target	= io_opts.background_target;
			data_opts.rewrite_ptrs = 0;

			unsigned i = 0;
			bkey_for_each_ptr(bch2_bkey_ptrs_c(k), ptr) {
				if (ptr->dev == bucket.inode) {
					data_opts.rewrite_ptrs |= 1U << i;
					if (ptr->cached) {
						bch2_trans_iter_exit(trans, &iter);
						goto next;
					}
				}
				i++;
			}

			ret = bch2_move_extent(ctxt, bucket_in_flight,
					       &iter, k, io_opts, data_opts);
			bch2_trans_iter_exit(trans, &iter);

			if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
				continue;
			if (ret == -ENOMEM) {
				/* memory allocation failure, wait for some IO to finish */
				bch2_move_ctxt_wait_for_io(ctxt);
				continue;
			}
			if (ret)
				goto err;

			if (ctxt->stats)
				atomic64_add(k.k->size, &ctxt->stats->sectors_seen);
		} else {
			struct btree *b;

			b = bch2_backpointer_get_node(trans, &iter, bp_pos, bp);
			ret = PTR_ERR_OR_ZERO(b);
			if (ret == -BCH_ERR_backpointer_to_overwritten_btree_node)
				continue;
			if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
				continue;
			if (ret)
				goto err;
			if (!b)
				goto next;

			unsigned sectors = btree_ptr_sectors_written(&b->key);

			ret = bch2_btree_node_rewrite(trans, &iter, b, 0);
			bch2_trans_iter_exit(trans, &iter);

			if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
				continue;
			if (ret)
				goto err;

			if (ctxt->rate)
				bch2_ratelimit_increment(ctxt->rate, sectors);
			if (ctxt->stats) {
				atomic64_add(sectors, &ctxt->stats->sectors_seen);
				atomic64_add(sectors, &ctxt->stats->sectors_moved);
			}
		}
next:
		bp_pos = bpos_nosnap_successor(bp_pos);
	}

	trace_evacuate_bucket(c, &bucket, dirty_sectors, bucket_size, fragmentation, ret);
err:
	bch2_dev_put(ca);
	bch2_bkey_buf_exit(&sk, c);
	return ret;
}

typedef bool (*move_btree_pred)(struct bch_fs *, void *,
				struct btree *, struct bch_io_opts *,
				struct data_update_opts *);

static int bch2_move_btree(struct bch_fs *c,
			   struct bbpos start,
			   struct bbpos end,
			   move_btree_pred pred, void *arg,
			   struct bch_move_stats *stats)
{
	bool kthread = (current->flags & PF_KTHREAD) != 0;
	struct bch_io_opts io_opts = bch2_opts_to_inode_opts(c->opts);
	struct moving_context ctxt;
	struct btree_trans *trans;
	struct btree_iter iter;
	struct btree *b;
	enum btree_id btree;
	struct data_update_opts data_opts;
	int ret = 0;

	bch2_moving_ctxt_init(&ctxt, c, NULL, stats,
			      writepoint_ptr(&c->btree_write_point),
			      true);
	trans = ctxt.trans;

	stats->data_type = BCH_DATA_btree;

	for (btree = start.btree;
	     btree <= min_t(unsigned, end.btree, btree_id_nr_alive(c) - 1);
	     btree ++) {
		stats->pos = BBPOS(btree, POS_MIN);

		if (!bch2_btree_id_root(c, btree)->b)
			continue;

		bch2_trans_node_iter_init(trans, &iter, btree, POS_MIN, 0, 0,
					  BTREE_ITER_prefetch);
retry:
		ret = 0;
		while (bch2_trans_begin(trans),
		       (b = bch2_btree_iter_peek_node(&iter)) &&
		       !(ret = PTR_ERR_OR_ZERO(b))) {
			if (kthread && kthread_should_stop())
				break;

			if ((cmp_int(btree, end.btree) ?:
			     bpos_cmp(b->key.k.p, end.pos)) > 0)
				break;

			stats->pos = BBPOS(iter.btree_id, iter.pos);

			if (!pred(c, arg, b, &io_opts, &data_opts))
				goto next;

			ret = bch2_btree_node_rewrite(trans, &iter, b, 0) ?: ret;
			if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
				continue;
			if (ret)
				break;
next:
			bch2_btree_iter_next_node(&iter);
		}
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			goto retry;

		bch2_trans_iter_exit(trans, &iter);

		if (kthread && kthread_should_stop())
			break;
	}

	bch_err_fn(c, ret);
	bch2_moving_ctxt_exit(&ctxt);
	bch2_btree_interior_updates_flush(c);

	return ret;
}

static bool rereplicate_pred(struct bch_fs *c, void *arg,
			     struct bkey_s_c k,
			     struct bch_io_opts *io_opts,
			     struct data_update_opts *data_opts)
{
	unsigned nr_good = bch2_bkey_durability(c, k);
	unsigned replicas = bkey_is_btree_ptr(k.k)
		? c->opts.metadata_replicas
		: io_opts->data_replicas;

	rcu_read_lock();
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	unsigned i = 0;
	bkey_for_each_ptr(ptrs, ptr) {
		struct bch_dev *ca = bch2_dev_rcu(c, ptr->dev);
		if (!ptr->cached &&
		    (!ca || !ca->mi.durability))
			data_opts->kill_ptrs |= BIT(i);
		i++;
	}
	rcu_read_unlock();

	if (!data_opts->kill_ptrs &&
	    (!nr_good || nr_good >= replicas))
		return false;

	data_opts->target		= 0;
	data_opts->extra_replicas	= replicas - nr_good;
	data_opts->btree_insert_flags	= 0;
	return true;
}

static bool migrate_pred(struct bch_fs *c, void *arg,
			 struct bkey_s_c k,
			 struct bch_io_opts *io_opts,
			 struct data_update_opts *data_opts)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	struct bch_ioctl_data *op = arg;
	unsigned i = 0;

	data_opts->rewrite_ptrs		= 0;
	data_opts->target		= 0;
	data_opts->extra_replicas	= 0;
	data_opts->btree_insert_flags	= 0;

	bkey_for_each_ptr(ptrs, ptr) {
		if (ptr->dev == op->migrate.dev)
			data_opts->rewrite_ptrs |= 1U << i;
		i++;
	}

	return data_opts->rewrite_ptrs != 0;
}

static bool rereplicate_btree_pred(struct bch_fs *c, void *arg,
				   struct btree *b,
				   struct bch_io_opts *io_opts,
				   struct data_update_opts *data_opts)
{
	return rereplicate_pred(c, arg, bkey_i_to_s_c(&b->key), io_opts, data_opts);
}

static bool migrate_btree_pred(struct bch_fs *c, void *arg,
			       struct btree *b,
			       struct bch_io_opts *io_opts,
			       struct data_update_opts *data_opts)
{
	return migrate_pred(c, arg, bkey_i_to_s_c(&b->key), io_opts, data_opts);
}

/*
 * Ancient versions of bcachefs produced packed formats which could represent
 * keys that the in memory format cannot represent; this checks for those
 * formats so we can get rid of them.
 */
static bool bformat_needs_redo(struct bkey_format *f)
{
	for (unsigned i = 0; i < f->nr_fields; i++)
		if (bch2_bkey_format_field_overflows(f, i))
			return true;

	return false;
}

static bool rewrite_old_nodes_pred(struct bch_fs *c, void *arg,
				   struct btree *b,
				   struct bch_io_opts *io_opts,
				   struct data_update_opts *data_opts)
{
	if (b->version_ondisk != c->sb.version ||
	    btree_node_need_rewrite(b) ||
	    bformat_needs_redo(&b->format)) {
		data_opts->target		= 0;
		data_opts->extra_replicas	= 0;
		data_opts->btree_insert_flags	= 0;
		return true;
	}

	return false;
}

int bch2_scan_old_btree_nodes(struct bch_fs *c, struct bch_move_stats *stats)
{
	int ret;

	ret = bch2_move_btree(c,
			      BBPOS_MIN,
			      BBPOS_MAX,
			      rewrite_old_nodes_pred, c, stats);
	if (!ret) {
		mutex_lock(&c->sb_lock);
		c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_extents_above_btree_updates_done);
		c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_bformat_overflow_done);
		c->disk_sb.sb->version_min = c->disk_sb.sb->version;
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	}

	bch_err_fn(c, ret);
	return ret;
}

static bool drop_extra_replicas_pred(struct bch_fs *c, void *arg,
			     struct bkey_s_c k,
			     struct bch_io_opts *io_opts,
			     struct data_update_opts *data_opts)
{
	unsigned durability = bch2_bkey_durability(c, k);
	unsigned replicas = bkey_is_btree_ptr(k.k)
		? c->opts.metadata_replicas
		: io_opts->data_replicas;
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned i = 0;

	rcu_read_lock();
	bkey_for_each_ptr_decode(k.k, bch2_bkey_ptrs_c(k), p, entry) {
		unsigned d = bch2_extent_ptr_durability(c, &p);

		if (d && durability - d >= replicas) {
			data_opts->kill_ptrs |= BIT(i);
			durability -= d;
		}

		i++;
	}
	rcu_read_unlock();

	return data_opts->kill_ptrs != 0;
}

static bool drop_extra_replicas_btree_pred(struct bch_fs *c, void *arg,
				   struct btree *b,
				   struct bch_io_opts *io_opts,
				   struct data_update_opts *data_opts)
{
	return drop_extra_replicas_pred(c, arg, bkey_i_to_s_c(&b->key), io_opts, data_opts);
}

int bch2_data_job(struct bch_fs *c,
		  struct bch_move_stats *stats,
		  struct bch_ioctl_data op)
{
	struct bbpos start	= BBPOS(op.start_btree, op.start_pos);
	struct bbpos end	= BBPOS(op.end_btree, op.end_pos);
	int ret = 0;

	if (op.op >= BCH_DATA_OP_NR)
		return -EINVAL;

	bch2_move_stats_init(stats, bch2_data_ops_strs[op.op]);

	switch (op.op) {
	case BCH_DATA_OP_rereplicate:
		stats->data_type = BCH_DATA_journal;
		ret = bch2_journal_flush_device_pins(&c->journal, -1);
		ret = bch2_move_btree(c, start, end,
				      rereplicate_btree_pred, c, stats) ?: ret;
		ret = bch2_move_data(c, start, end,
				     NULL,
				     stats,
				     writepoint_hashed((unsigned long) current),
				     true,
				     rereplicate_pred, c) ?: ret;
		ret = bch2_replicas_gc2(c) ?: ret;
		break;
	case BCH_DATA_OP_migrate:
		if (op.migrate.dev >= c->sb.nr_devices)
			return -EINVAL;

		stats->data_type = BCH_DATA_journal;
		ret = bch2_journal_flush_device_pins(&c->journal, op.migrate.dev);
		ret = bch2_move_btree(c, start, end,
				      migrate_btree_pred, &op, stats) ?: ret;
		ret = bch2_move_data(c, start, end,
				     NULL,
				     stats,
				     writepoint_hashed((unsigned long) current),
				     true,
				     migrate_pred, &op) ?: ret;
		ret = bch2_replicas_gc2(c) ?: ret;
		break;
	case BCH_DATA_OP_rewrite_old_nodes:
		ret = bch2_scan_old_btree_nodes(c, stats);
		break;
	case BCH_DATA_OP_drop_extra_replicas:
		ret = bch2_move_btree(c, start, end,
				drop_extra_replicas_btree_pred, c, stats) ?: ret;
		ret = bch2_move_data(c, start, end, NULL, stats,
				writepoint_hashed((unsigned long) current),
				true,
				drop_extra_replicas_pred, c) ?: ret;
		ret = bch2_replicas_gc2(c) ?: ret;
		break;
	default:
		ret = -EINVAL;
	}

	bch2_move_stats_exit(stats, c);
	return ret;
}

void bch2_move_stats_to_text(struct printbuf *out, struct bch_move_stats *stats)
{
	prt_printf(out, "%s: data type==", stats->name);
	bch2_prt_data_type(out, stats->data_type);
	prt_str(out, " pos=");
	bch2_bbpos_to_text(out, stats->pos);
	prt_newline(out);
	printbuf_indent_add(out, 2);

	prt_printf(out, "keys moved:  %llu\n",	atomic64_read(&stats->keys_moved));
	prt_printf(out, "keys raced:  %llu\n",	atomic64_read(&stats->keys_raced));
	prt_printf(out, "bytes seen:  ");
	prt_human_readable_u64(out, atomic64_read(&stats->sectors_seen) << 9);
	prt_newline(out);

	prt_printf(out, "bytes moved: ");
	prt_human_readable_u64(out, atomic64_read(&stats->sectors_moved) << 9);
	prt_newline(out);

	prt_printf(out, "bytes raced: ");
	prt_human_readable_u64(out, atomic64_read(&stats->sectors_raced) << 9);
	prt_newline(out);

	printbuf_indent_sub(out, 2);
}

static void bch2_moving_ctxt_to_text(struct printbuf *out, struct bch_fs *c, struct moving_context *ctxt)
{
	struct moving_io *io;

	bch2_move_stats_to_text(out, ctxt->stats);
	printbuf_indent_add(out, 2);

	prt_printf(out, "reads: ios %u/%u sectors %u/%u\n",
		   atomic_read(&ctxt->read_ios),
		   c->opts.move_ios_in_flight,
		   atomic_read(&ctxt->read_sectors),
		   c->opts.move_bytes_in_flight >> 9);

	prt_printf(out, "writes: ios %u/%u sectors %u/%u\n",
		   atomic_read(&ctxt->write_ios),
		   c->opts.move_ios_in_flight,
		   atomic_read(&ctxt->write_sectors),
		   c->opts.move_bytes_in_flight >> 9);

	printbuf_indent_add(out, 2);

	mutex_lock(&ctxt->lock);
	list_for_each_entry(io, &ctxt->ios, io_list)
		bch2_write_op_to_text(out, &io->write.op);
	mutex_unlock(&ctxt->lock);

	printbuf_indent_sub(out, 4);
}

void bch2_fs_moving_ctxts_to_text(struct printbuf *out, struct bch_fs *c)
{
	struct moving_context *ctxt;

	mutex_lock(&c->moving_context_lock);
	list_for_each_entry(ctxt, &c->moving_context_list, list)
		bch2_moving_ctxt_to_text(out, c, ctxt);
	mutex_unlock(&c->moving_context_lock);
}

void bch2_fs_move_init(struct bch_fs *c)
{
	INIT_LIST_HEAD(&c->moving_context_list);
	mutex_init(&c->moving_context_lock);
}
