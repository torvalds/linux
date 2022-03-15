// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_buf.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "buckets.h"
#include "disk_groups.h"
#include "ec.h"
#include "inode.h"
#include "io.h"
#include "journal_reclaim.h"
#include "keylist.h"
#include "move.h"
#include "replicas.h"
#include "subvolume.h"
#include "super-io.h"
#include "trace.h"

#include <linux/ioprio.h>
#include <linux/kthread.h>

#define SECTORS_IN_FLIGHT_PER_DEVICE	2048

struct moving_io {
	struct list_head	list;
	struct closure		cl;
	bool			read_completed;

	unsigned		read_sectors;
	unsigned		write_sectors;

	struct bch_read_bio	rbio;

	struct migrate_write	write;
	/* Must be last since it is variable size */
	struct bio_vec		bi_inline_vecs[0];
};

struct moving_context {
	/* Closure for waiting on all reads and writes to complete */
	struct closure		cl;

	struct bch_move_stats	*stats;

	struct list_head	reads;

	/* in flight sectors: */
	atomic_t		read_sectors;
	atomic_t		write_sectors;

	wait_queue_head_t	wait;
};

static int insert_snapshot_whiteouts(struct btree_trans *trans,
				     enum btree_id id,
				     struct bpos old_pos,
				     struct bpos new_pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter, update_iter;
	struct bkey_s_c k;
	struct snapshots_seen s;
	int ret;

	if (!btree_type_has_snapshots(id))
		return 0;

	snapshots_seen_init(&s);

	if (!bkey_cmp(old_pos, new_pos))
		return 0;

	if (!snapshot_t(c, old_pos.snapshot)->children[0])
		return 0;

	bch2_trans_iter_init(trans, &iter, id, old_pos,
			     BTREE_ITER_NOT_EXTENTS|
			     BTREE_ITER_ALL_SNAPSHOTS);
	while (1) {
next:
		k = bch2_btree_iter_prev(&iter);
		ret = bkey_err(k);
		if (ret)
			break;

		if (bkey_cmp(old_pos, k.k->p))
			break;

		if (bch2_snapshot_is_ancestor(c, k.k->p.snapshot, old_pos.snapshot)) {
			struct bkey_i *update;
			u32 *i;

			darray_for_each(s.ids, i)
				if (bch2_snapshot_is_ancestor(c, k.k->p.snapshot, *i))
					goto next;

			update = bch2_trans_kmalloc(trans, sizeof(struct bkey_i));

			ret = PTR_ERR_OR_ZERO(update);
			if (ret)
				break;

			bkey_init(&update->k);
			update->k.p = new_pos;
			update->k.p.snapshot = k.k->p.snapshot;

			bch2_trans_iter_init(trans, &update_iter, id, update->k.p,
					     BTREE_ITER_NOT_EXTENTS|
					     BTREE_ITER_ALL_SNAPSHOTS|
					     BTREE_ITER_INTENT);
			ret   = bch2_btree_iter_traverse(&update_iter) ?:
				bch2_trans_update(trans, &update_iter, update,
					  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
			bch2_trans_iter_exit(trans, &update_iter);
			if (ret)
				break;

			ret = snapshots_seen_add(c, &s, k.k->p.snapshot);
			if (ret)
				break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);
	darray_exit(&s.ids);

	return ret;
}

int bch2_migrate_index_update(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct btree_trans trans;
	struct btree_iter iter;
	struct migrate_write *m =
		container_of(op, struct migrate_write, op);
	struct open_bucket *ec_ob = ec_open_bucket(c, &op->open_buckets);
	struct keylist *keys = &op->insert_keys;
	struct bkey_buf _new, _insert;
	int ret = 0;

	bch2_bkey_buf_init(&_new);
	bch2_bkey_buf_init(&_insert);
	bch2_bkey_buf_realloc(&_insert, c, U8_MAX);

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 1024);

	bch2_trans_iter_init(&trans, &iter, m->btree_id,
			     bkey_start_pos(&bch2_keylist_front(keys)->k),
			     BTREE_ITER_SLOTS|BTREE_ITER_INTENT);

	while (1) {
		struct bkey_s_c k;
		struct bkey_i *insert;
		struct bkey_i_extent *new;
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;
		struct bpos next_pos;
		bool did_work = false;
		bool extending = false, should_check_enospc;
		s64 i_sectors_delta = 0, disk_sectors_delta = 0;

		bch2_trans_begin(&trans);

		k = bch2_btree_iter_peek_slot(&iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		new = bkey_i_to_extent(bch2_keylist_front(keys));

		if (bversion_cmp(k.k->version, new->k.version) ||
		    !bch2_bkey_matches_ptr(c, k, m->ptr, m->offset))
			goto nomatch;

		bkey_reassemble(_insert.k, k);
		insert = _insert.k;

		bch2_bkey_buf_copy(&_new, c, bch2_keylist_front(keys));
		new = bkey_i_to_extent(_new.k);
		bch2_cut_front(iter.pos, &new->k_i);

		bch2_cut_front(iter.pos,	insert);
		bch2_cut_back(new->k.p,		insert);
		bch2_cut_back(insert->k.p,	&new->k_i);

		if (m->data_cmd == DATA_REWRITE) {
			struct bch_extent_ptr *new_ptr, *old_ptr = (void *)
				bch2_bkey_has_device(bkey_i_to_s_c(insert),
						     m->data_opts.rewrite_dev);
			if (!old_ptr)
				goto nomatch;

			if (old_ptr->cached)
				extent_for_each_ptr(extent_i_to_s(new), new_ptr)
					new_ptr->cached = true;

			__bch2_bkey_drop_ptr(bkey_i_to_s(insert), old_ptr);
		}

		extent_for_each_ptr_decode(extent_i_to_s(new), p, entry) {
			if (bch2_bkey_has_device(bkey_i_to_s_c(insert), p.ptr.dev)) {
				/*
				 * raced with another move op? extent already
				 * has a pointer to the device we just wrote
				 * data to
				 */
				continue;
			}

			bch2_extent_ptr_decoded_append(insert, &p);
			did_work = true;
		}

		if (!did_work)
			goto nomatch;

		bch2_bkey_narrow_crcs(insert,
				(struct bch_extent_crc_unpacked) { 0 });
		bch2_extent_normalize(c, bkey_i_to_s(insert));
		bch2_bkey_mark_replicas_cached(c, bkey_i_to_s(insert),
					       op->opts.background_target,
					       op->opts.data_replicas);

		ret = bch2_sum_sector_overwrites(&trans, &iter, insert,
						 &extending,
						 &should_check_enospc,
						 &i_sectors_delta,
						 &disk_sectors_delta);
		if (ret)
			goto err;

		if (disk_sectors_delta > (s64) op->res.sectors) {
			ret = bch2_disk_reservation_add(c, &op->res,
						disk_sectors_delta - op->res.sectors,
						!should_check_enospc
						? BCH_DISK_RESERVATION_NOFAIL : 0);
			if (ret)
				goto out;
		}

		next_pos = insert->k.p;

		ret   = insert_snapshot_whiteouts(&trans, m->btree_id,
						  k.k->p, insert->k.p) ?:
			bch2_trans_update(&trans, &iter, insert,
				BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE) ?:
			bch2_trans_commit(&trans, &op->res,
				op_journal_seq(op),
				BTREE_INSERT_NOFAIL|
				m->data_opts.btree_insert_flags);
		if (!ret) {
			bch2_btree_iter_set_pos(&iter, next_pos);
			atomic_long_inc(&c->extent_migrate_done);
			if (ec_ob)
				bch2_ob_add_backpointer(c, ec_ob, &insert->k);
		}
err:
		if (ret == -EINTR)
			ret = 0;
		if (ret)
			break;
next:
		while (bkey_cmp(iter.pos, bch2_keylist_front(keys)->k.p) >= 0) {
			bch2_keylist_pop_front(keys);
			if (bch2_keylist_empty(keys))
				goto out;
		}
		continue;
nomatch:
		if (m->ctxt) {
			BUG_ON(k.k->p.offset <= iter.pos.offset);
			atomic64_inc(&m->ctxt->stats->keys_raced);
			atomic64_add(k.k->p.offset - iter.pos.offset,
				     &m->ctxt->stats->sectors_raced);
		}
		atomic_long_inc(&c->extent_migrate_raced);
		trace_move_race(&new->k);
		bch2_btree_iter_advance(&iter);
		goto next;
	}
out:
	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);
	bch2_bkey_buf_exit(&_insert, c);
	bch2_bkey_buf_exit(&_new, c);
	BUG_ON(ret == -EINTR);
	return ret;
}

void bch2_migrate_read_done(struct migrate_write *m, struct bch_read_bio *rbio)
{
	/* write bio must own pages: */
	BUG_ON(!m->op.wbio.bio.bi_vcnt);

	m->ptr		= rbio->pick.ptr;
	m->offset	= rbio->data_pos.offset - rbio->pick.crc.offset;
	m->op.devs_have	= rbio->devs_have;
	m->op.pos	= rbio->data_pos;
	m->op.version	= rbio->version;
	m->op.crc	= rbio->pick.crc;
	m->op.wbio.bio.bi_iter.bi_size = m->op.crc.compressed_size << 9;

	if (m->data_cmd == DATA_REWRITE)
		bch2_dev_list_drop_dev(&m->op.devs_have, m->data_opts.rewrite_dev);
}

int bch2_migrate_write_init(struct bch_fs *c, struct migrate_write *m,
			    struct write_point_specifier wp,
			    struct bch_io_opts io_opts,
			    enum data_cmd data_cmd,
			    struct data_opts data_opts,
			    enum btree_id btree_id,
			    struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct bch_extent_crc_unpacked crc;
	struct extent_ptr_decoded p;
	int ret;

	m->btree_id	= btree_id;
	m->data_cmd	= data_cmd;
	m->data_opts	= data_opts;
	m->nr_ptrs_reserved = 0;

	bch2_write_op_init(&m->op, c, io_opts);

	if (!bch2_bkey_is_incompressible(k))
		m->op.compression_type =
			bch2_compression_opt_to_type[io_opts.background_compression ?:
						     io_opts.compression];
	else
		m->op.incompressible = true;

	m->op.target	= data_opts.target,
	m->op.write_point = wp;

	/*
	 * op->csum_type is normally initialized from the fs/file's current
	 * options - but if an extent is encrypted, we require that it stays
	 * encrypted:
	 */
	bkey_for_each_crc(k.k, ptrs, crc, entry)
		if (bch2_csum_type_is_encryption(crc.csum_type)) {
			m->op.nonce	= crc.nonce + crc.offset;
			m->op.csum_type = crc.csum_type;
			break;
		}

	if (m->data_opts.btree_insert_flags & BTREE_INSERT_USE_RESERVE) {
		m->op.alloc_reserve = RESERVE_movinggc;
	} else {
		/* XXX: this should probably be passed in */
		m->op.flags |= BCH_WRITE_ONLY_SPECIFIED_DEVS;
	}

	m->op.flags |= BCH_WRITE_PAGES_STABLE|
		BCH_WRITE_PAGES_OWNED|
		BCH_WRITE_DATA_ENCODED|
		BCH_WRITE_FROM_INTERNAL|
		BCH_WRITE_MOVE;

	m->op.nr_replicas	= data_opts.nr_replicas;
	m->op.nr_replicas_required = data_opts.nr_replicas;

	switch (data_cmd) {
	case DATA_ADD_REPLICAS: {
		/*
		 * DATA_ADD_REPLICAS is used for moving data to a different
		 * device in the background, and due to compression the new copy
		 * might take up more space than the old copy:
		 */
#if 0
		int nr = (int) io_opts.data_replicas -
			bch2_bkey_nr_ptrs_allocated(k);
#endif
		int nr = (int) io_opts.data_replicas;

		if (nr > 0) {
			m->op.nr_replicas = m->nr_ptrs_reserved = nr;

			ret = bch2_disk_reservation_get(c, &m->op.res,
					k.k->size, m->op.nr_replicas, 0);
			if (ret)
				return ret;
		}
		break;
	}
	case DATA_REWRITE: {
		unsigned compressed_sectors = 0;

		bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
			if (p.ptr.dev == data_opts.rewrite_dev) {
				if (p.ptr.cached)
					m->op.flags |= BCH_WRITE_CACHED;

				if (!p.ptr.cached &&
				    crc_is_compressed(p.crc))
					compressed_sectors += p.crc.compressed_size;
			}

		if (compressed_sectors) {
			ret = bch2_disk_reservation_add(c, &m->op.res,
					k.k->size * m->op.nr_replicas,
					BCH_DISK_RESERVATION_NOFAIL);
			if (ret)
				return ret;
		}
		break;
	}
	case DATA_PROMOTE:
		m->op.flags	|= BCH_WRITE_ALLOC_NOWAIT;
		m->op.flags	|= BCH_WRITE_CACHED;
		break;
	default:
		BUG();
	}

	return 0;
}

static void move_free(struct moving_io *io)
{
	struct moving_context *ctxt = io->write.ctxt;
	struct bvec_iter_all iter;
	struct bio_vec *bv;

	bch2_disk_reservation_put(io->write.op.c, &io->write.op.res);

	bio_for_each_segment_all(bv, &io->write.op.wbio.bio, iter)
		if (bv->bv_page)
			__free_page(bv->bv_page);

	wake_up(&ctxt->wait);

	kfree(io);
}

static void move_write_done(struct bch_write_op *op)
{
	struct moving_io *io = container_of(op, struct moving_io, write.op);
	struct moving_context *ctxt = io->write.ctxt;

	atomic_sub(io->write_sectors, &io->write.ctxt->write_sectors);
	move_free(io);
	closure_put(&ctxt->cl);
}

static void move_write(struct moving_io *io)
{
	if (unlikely(io->rbio.bio.bi_status || io->rbio.hole)) {
		move_free(io);
		return;
	}

	closure_get(&io->write.ctxt->cl);
	atomic_add(io->write_sectors, &io->write.ctxt->write_sectors);

	bch2_migrate_read_done(&io->write, &io->rbio);
	closure_call(&io->write.op.cl, bch2_write, NULL, NULL);
}

static inline struct moving_io *next_pending_write(struct moving_context *ctxt)
{
	struct moving_io *io =
		list_first_entry_or_null(&ctxt->reads, struct moving_io, list);

	return io && io->read_completed ? io : NULL;
}

static void move_read_endio(struct bio *bio)
{
	struct moving_io *io = container_of(bio, struct moving_io, rbio.bio);
	struct moving_context *ctxt = io->write.ctxt;

	atomic_sub(io->read_sectors, &ctxt->read_sectors);
	io->read_completed = true;

	wake_up(&ctxt->wait);
	closure_put(&ctxt->cl);
}

static void do_pending_writes(struct moving_context *ctxt, struct btree_trans *trans)
{
	struct moving_io *io;

	if (trans)
		bch2_trans_unlock(trans);

	while ((io = next_pending_write(ctxt))) {
		list_del(&io->list);
		move_write(io);
	}
}

#define move_ctxt_wait_event(_ctxt, _trans, _cond)		\
do {								\
	do_pending_writes(_ctxt, _trans);			\
								\
	if (_cond)						\
		break;						\
	__wait_event((_ctxt)->wait,				\
		     next_pending_write(_ctxt) || (_cond));	\
} while (1)

static void bch2_move_ctxt_wait_for_io(struct moving_context *ctxt,
				       struct btree_trans *trans)
{
	unsigned sectors_pending = atomic_read(&ctxt->write_sectors);

	move_ctxt_wait_event(ctxt, trans,
		!atomic_read(&ctxt->write_sectors) ||
		atomic_read(&ctxt->write_sectors) != sectors_pending);
}

static int bch2_move_extent(struct btree_trans *trans,
			    struct moving_context *ctxt,
			    struct write_point_specifier wp,
			    struct bch_io_opts io_opts,
			    enum btree_id btree_id,
			    struct bkey_s_c k,
			    enum data_cmd data_cmd,
			    struct data_opts data_opts)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	struct moving_io *io;
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned sectors = k.k->size, pages;
	int ret = -ENOMEM;

	/* write path might have to decompress data: */
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry)
		sectors = max_t(unsigned, sectors, p.crc.uncompressed_size);

	pages = DIV_ROUND_UP(sectors, PAGE_SECTORS);
	io = kzalloc(sizeof(struct moving_io) +
		     sizeof(struct bio_vec) * pages, GFP_KERNEL);
	if (!io)
		goto err;

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

	ret = bch2_migrate_write_init(c, &io->write, wp, io_opts,
				      data_cmd, data_opts, btree_id, k);
	if (ret)
		goto err_free_pages;

	io->write.op.end_io = move_write_done;

	atomic64_inc(&ctxt->stats->keys_moved);
	atomic64_add(k.k->size, &ctxt->stats->sectors_moved);

	trace_move_extent(k.k);

	atomic_add(io->read_sectors, &ctxt->read_sectors);
	list_add_tail(&io->list, &ctxt->reads);

	/*
	 * dropped by move_read_endio() - guards against use after free of
	 * ctxt when doing wakeup
	 */
	closure_get(&ctxt->cl);
	bch2_read_extent(trans, &io->rbio,
			 bkey_start_pos(k.k),
			 btree_id, k, 0,
			 BCH_READ_NODECODE|
			 BCH_READ_LAST_FRAGMENT);
	return 0;
err_free_pages:
	bio_free_pages(&io->write.op.wbio.bio);
err_free:
	kfree(io);
err:
	trace_move_alloc_fail(k.k);
	return ret;
}

static int lookup_inode(struct btree_trans *trans, struct bpos pos,
			struct bch_inode_unpacked *inode)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_inodes, pos,
			     BTREE_ITER_ALL_SNAPSHOTS);
	k = bch2_btree_iter_peek(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (!k.k || bkey_cmp(k.k->p, pos)) {
		ret = -ENOENT;
		goto err;
	}

	ret = bkey_is_inode(k.k) ? 0 : -EIO;
	if (ret)
		goto err;

	ret = bch2_inode_unpack(k, inode);
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int __bch2_move_data(struct bch_fs *c,
		struct moving_context *ctxt,
		struct bch_ratelimit *rate,
		struct write_point_specifier wp,
		struct bpos start,
		struct bpos end,
		move_pred_fn pred, void *arg,
		struct bch_move_stats *stats,
		enum btree_id btree_id)
{
	bool kthread = (current->flags & PF_KTHREAD) != 0;
	struct bch_io_opts io_opts = bch2_opts_to_inode_opts(c->opts);
	struct bkey_buf sk;
	struct btree_trans trans;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct data_opts data_opts;
	enum data_cmd data_cmd;
	u64 delay, cur_inum = U64_MAX;
	int ret = 0, ret2;

	bch2_bkey_buf_init(&sk);
	bch2_trans_init(&trans, c, 0, 0);

	stats->data_type = BCH_DATA_user;
	stats->btree_id	= btree_id;
	stats->pos	= start;

	bch2_trans_iter_init(&trans, &iter, btree_id, start,
			     BTREE_ITER_PREFETCH|
			     BTREE_ITER_ALL_SNAPSHOTS);

	if (rate)
		bch2_ratelimit_reset(rate);

	while (1) {
		do {
			delay = rate ? bch2_ratelimit_delay(rate) : 0;

			if (delay) {
				bch2_trans_unlock(&trans);
				set_current_state(TASK_INTERRUPTIBLE);
			}

			if (kthread && (ret = kthread_should_stop())) {
				__set_current_state(TASK_RUNNING);
				goto out;
			}

			if (delay)
				schedule_timeout(delay);

			if (unlikely(freezing(current))) {
				move_ctxt_wait_event(ctxt, &trans, list_empty(&ctxt->reads));
				try_to_freeze();
			}
		} while (delay);

		move_ctxt_wait_event(ctxt, &trans,
			atomic_read(&ctxt->write_sectors) <
			SECTORS_IN_FLIGHT_PER_DEVICE);

		move_ctxt_wait_event(ctxt, &trans,
			atomic_read(&ctxt->read_sectors) <
			SECTORS_IN_FLIGHT_PER_DEVICE);

		bch2_trans_begin(&trans);

		k = bch2_btree_iter_peek(&iter);
		if (!k.k)
			break;

		ret = bkey_err(k);
		if (ret == -EINTR)
			continue;
		if (ret)
			break;

		if (bkey_cmp(bkey_start_pos(k.k), end) >= 0)
			break;

		stats->pos = iter.pos;

		if (!bkey_extent_is_direct_data(k.k))
			goto next_nondata;

		if (btree_id == BTREE_ID_extents &&
		    cur_inum != k.k->p.inode) {
			struct bch_inode_unpacked inode;

			io_opts = bch2_opts_to_inode_opts(c->opts);

			ret = lookup_inode(&trans,
					SPOS(0, k.k->p.inode, k.k->p.snapshot),
					&inode);
			if (ret == -EINTR)
				continue;

			if (!ret)
				bch2_io_opts_apply(&io_opts, bch2_inode_opts_get(&inode));

			cur_inum = k.k->p.inode;
		}

		switch ((data_cmd = pred(c, arg, k, &io_opts, &data_opts))) {
		case DATA_SKIP:
			goto next;
		case DATA_SCRUB:
			BUG();
		case DATA_ADD_REPLICAS:
		case DATA_REWRITE:
		case DATA_PROMOTE:
			break;
		default:
			BUG();
		}

		/*
		 * The iterator gets unlocked by __bch2_read_extent - need to
		 * save a copy of @k elsewhere:
		  */
		bch2_bkey_buf_reassemble(&sk, c, k);
		k = bkey_i_to_s_c(sk.k);

		ret2 = bch2_move_extent(&trans, ctxt, wp, io_opts, btree_id, k,
					data_cmd, data_opts);
		if (ret2) {
			if (ret2 == -EINTR)
				continue;

			if (ret2 == -ENOMEM) {
				/* memory allocation failure, wait for some IO to finish */
				bch2_move_ctxt_wait_for_io(ctxt, &trans);
				continue;
			}

			/* XXX signal failure */
			goto next;
		}

		if (rate)
			bch2_ratelimit_increment(rate, k.k->size);
next:
		atomic64_add(k.k->size, &stats->sectors_seen);
next_nondata:
		bch2_btree_iter_advance(&iter);
	}
out:

	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);
	bch2_bkey_buf_exit(&sk, c);

	return ret;
}

inline void bch_move_stats_init(struct bch_move_stats *stats, char *name)
{
	memset(stats, 0, sizeof(*stats));

	scnprintf(stats->name, sizeof(stats->name),
			"%s", name);
}

static inline void progress_list_add(struct bch_fs *c,
				     struct bch_move_stats *stats)
{
	mutex_lock(&c->data_progress_lock);
	list_add(&stats->list, &c->data_progress_list);
	mutex_unlock(&c->data_progress_lock);
}

static inline void progress_list_del(struct bch_fs *c,
				     struct bch_move_stats *stats)
{
	mutex_lock(&c->data_progress_lock);
	list_del(&stats->list);
	mutex_unlock(&c->data_progress_lock);
}

int bch2_move_data(struct bch_fs *c,
		   enum btree_id start_btree_id, struct bpos start_pos,
		   enum btree_id end_btree_id,   struct bpos end_pos,
		   struct bch_ratelimit *rate,
		   struct write_point_specifier wp,
		   move_pred_fn pred, void *arg,
		   struct bch_move_stats *stats)
{
	struct moving_context ctxt = { .stats = stats };
	enum btree_id id;
	int ret;

	progress_list_add(c, stats);
	closure_init_stack(&ctxt.cl);
	INIT_LIST_HEAD(&ctxt.reads);
	init_waitqueue_head(&ctxt.wait);

	stats->data_type = BCH_DATA_user;

	for (id = start_btree_id;
	     id <= min_t(unsigned, end_btree_id, BTREE_ID_NR - 1);
	     id++) {
		stats->btree_id = id;

		if (id != BTREE_ID_extents &&
		    id != BTREE_ID_reflink)
			continue;

		ret = __bch2_move_data(c, &ctxt, rate, wp,
				       id == start_btree_id ? start_pos : POS_MIN,
				       id == end_btree_id   ? end_pos   : POS_MAX,
				       pred, arg, stats, id);
		if (ret)
			break;
	}


	move_ctxt_wait_event(&ctxt, NULL, list_empty(&ctxt.reads));
	closure_sync(&ctxt.cl);

	EBUG_ON(atomic_read(&ctxt.write_sectors));

	trace_move_data(c,
			atomic64_read(&stats->sectors_moved),
			atomic64_read(&stats->keys_moved));

	progress_list_del(c, stats);
	return ret;
}

typedef enum data_cmd (*move_btree_pred)(struct bch_fs *, void *,
					 struct btree *, struct bch_io_opts *,
					 struct data_opts *);

static int bch2_move_btree(struct bch_fs *c,
			   enum btree_id start_btree_id, struct bpos start_pos,
			   enum btree_id end_btree_id,   struct bpos end_pos,
			   move_btree_pred pred, void *arg,
			   struct bch_move_stats *stats)
{
	bool kthread = (current->flags & PF_KTHREAD) != 0;
	struct bch_io_opts io_opts = bch2_opts_to_inode_opts(c->opts);
	struct btree_trans trans;
	struct btree_iter iter;
	struct btree *b;
	enum btree_id id;
	struct data_opts data_opts;
	enum data_cmd cmd;
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);
	progress_list_add(c, stats);

	stats->data_type = BCH_DATA_btree;

	for (id = start_btree_id;
	     id <= min_t(unsigned, end_btree_id, BTREE_ID_NR - 1);
	     id++) {
		stats->btree_id = id;

		bch2_trans_node_iter_init(&trans, &iter, id, POS_MIN, 0, 0,
					  BTREE_ITER_PREFETCH);
retry:
		ret = 0;
		while (bch2_trans_begin(&trans),
		       (b = bch2_btree_iter_peek_node(&iter)) &&
		       !(ret = PTR_ERR_OR_ZERO(b))) {
			if (kthread && kthread_should_stop())
				break;

			if ((cmp_int(id, end_btree_id) ?:
			     bpos_cmp(b->key.k.p, end_pos)) > 0)
				break;

			stats->pos = iter.pos;

			switch ((cmd = pred(c, arg, b, &io_opts, &data_opts))) {
			case DATA_SKIP:
				goto next;
			case DATA_SCRUB:
				BUG();
			case DATA_ADD_REPLICAS:
			case DATA_REWRITE:
				break;
			default:
				BUG();
			}

			ret = bch2_btree_node_rewrite(&trans, &iter, b, 0) ?: ret;
			if (ret == -EINTR)
				continue;
			if (ret)
				break;
next:
			bch2_btree_iter_next_node(&iter);
		}
		if (ret == -EINTR)
			goto retry;

		bch2_trans_iter_exit(&trans, &iter);

		if (kthread && kthread_should_stop())
			break;
	}

	bch2_trans_exit(&trans);

	if (ret)
		bch_err(c, "error %i in bch2_move_btree", ret);

	/* flush relevant btree updates */
	closure_wait_event(&c->btree_interior_update_wait,
			   !bch2_btree_interior_updates_nr_pending(c));

	progress_list_del(c, stats);
	return ret;
}

#if 0
static enum data_cmd scrub_pred(struct bch_fs *c, void *arg,
				struct bkey_s_c k,
				struct bch_io_opts *io_opts,
				struct data_opts *data_opts)
{
	return DATA_SCRUB;
}
#endif

static enum data_cmd rereplicate_pred(struct bch_fs *c, void *arg,
				      struct bkey_s_c k,
				      struct bch_io_opts *io_opts,
				      struct data_opts *data_opts)
{
	unsigned nr_good = bch2_bkey_durability(c, k);
	unsigned replicas = bkey_is_btree_ptr(k.k)
		? c->opts.metadata_replicas
		: io_opts->data_replicas;

	if (!nr_good || nr_good >= replicas)
		return DATA_SKIP;

	data_opts->target		= 0;
	data_opts->nr_replicas		= 1;
	data_opts->btree_insert_flags	= 0;
	return DATA_ADD_REPLICAS;
}

static enum data_cmd migrate_pred(struct bch_fs *c, void *arg,
				  struct bkey_s_c k,
				  struct bch_io_opts *io_opts,
				  struct data_opts *data_opts)
{
	struct bch_ioctl_data *op = arg;

	if (!bch2_bkey_has_device(k, op->migrate.dev))
		return DATA_SKIP;

	data_opts->target		= 0;
	data_opts->nr_replicas		= 1;
	data_opts->btree_insert_flags	= 0;
	data_opts->rewrite_dev		= op->migrate.dev;
	return DATA_REWRITE;
}

static enum data_cmd rereplicate_btree_pred(struct bch_fs *c, void *arg,
					    struct btree *b,
					    struct bch_io_opts *io_opts,
					    struct data_opts *data_opts)
{
	return rereplicate_pred(c, arg, bkey_i_to_s_c(&b->key), io_opts, data_opts);
}

static enum data_cmd migrate_btree_pred(struct bch_fs *c, void *arg,
					struct btree *b,
					struct bch_io_opts *io_opts,
					struct data_opts *data_opts)
{
	return migrate_pred(c, arg, bkey_i_to_s_c(&b->key), io_opts, data_opts);
}

static bool bformat_needs_redo(struct bkey_format *f)
{
	unsigned i;

	for (i = 0; i < f->nr_fields; i++) {
		unsigned unpacked_bits = bch2_bkey_format_current.bits_per_field[i];
		u64 unpacked_mask = ~((~0ULL << 1) << (unpacked_bits - 1));
		u64 field_offset = le64_to_cpu(f->field_offset[i]);

		if (f->bits_per_field[i] > unpacked_bits)
			return true;

		if ((f->bits_per_field[i] == unpacked_bits) && field_offset)
			return true;

		if (((field_offset + ((1ULL << f->bits_per_field[i]) - 1)) &
		     unpacked_mask) <
		    field_offset)
			return true;
	}

	return false;
}

static enum data_cmd rewrite_old_nodes_pred(struct bch_fs *c, void *arg,
					    struct btree *b,
					    struct bch_io_opts *io_opts,
					    struct data_opts *data_opts)
{
	if (b->version_ondisk != c->sb.version ||
	    btree_node_need_rewrite(b) ||
	    bformat_needs_redo(&b->format)) {
		data_opts->target		= 0;
		data_opts->nr_replicas		= 1;
		data_opts->btree_insert_flags	= 0;
		return DATA_REWRITE;
	}

	return DATA_SKIP;
}

int bch2_scan_old_btree_nodes(struct bch_fs *c, struct bch_move_stats *stats)
{
	int ret;

	ret = bch2_move_btree(c,
			      0,		POS_MIN,
			      BTREE_ID_NR,	SPOS_MAX,
			      rewrite_old_nodes_pred, c, stats);
	if (!ret) {
		mutex_lock(&c->sb_lock);
		c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_extents_above_btree_updates_done);
		c->disk_sb.sb->compat[0] |= cpu_to_le64(1ULL << BCH_COMPAT_bformat_overflow_done);
		c->disk_sb.sb->version_min = c->disk_sb.sb->version;
		bch2_write_super(c);
		mutex_unlock(&c->sb_lock);
	}

	return ret;
}

int bch2_data_job(struct bch_fs *c,
		  struct bch_move_stats *stats,
		  struct bch_ioctl_data op)
{
	int ret = 0;

	switch (op.op) {
	case BCH_DATA_OP_REREPLICATE:
		bch_move_stats_init(stats, "rereplicate");
		stats->data_type = BCH_DATA_journal;
		ret = bch2_journal_flush_device_pins(&c->journal, -1);

		ret = bch2_move_btree(c,
				      op.start_btree,	op.start_pos,
				      op.end_btree,	op.end_pos,
				      rereplicate_btree_pred, c, stats) ?: ret;
		ret = bch2_replicas_gc2(c) ?: ret;

		ret = bch2_move_data(c,
				     op.start_btree,	op.start_pos,
				     op.end_btree,	op.end_pos,
				     NULL, writepoint_hashed((unsigned long) current),
				     rereplicate_pred, c, stats) ?: ret;
		ret = bch2_replicas_gc2(c) ?: ret;
		break;
	case BCH_DATA_OP_MIGRATE:
		if (op.migrate.dev >= c->sb.nr_devices)
			return -EINVAL;

		bch_move_stats_init(stats, "migrate");
		stats->data_type = BCH_DATA_journal;
		ret = bch2_journal_flush_device_pins(&c->journal, op.migrate.dev);

		ret = bch2_move_btree(c,
				      op.start_btree,	op.start_pos,
				      op.end_btree,	op.end_pos,
				      migrate_btree_pred, &op, stats) ?: ret;
		ret = bch2_replicas_gc2(c) ?: ret;

		ret = bch2_move_data(c,
				     op.start_btree,	op.start_pos,
				     op.end_btree,	op.end_pos,
				     NULL, writepoint_hashed((unsigned long) current),
				     migrate_pred, &op, stats) ?: ret;
		ret = bch2_replicas_gc2(c) ?: ret;
		break;
	case BCH_DATA_OP_REWRITE_OLD_NODES:
		bch_move_stats_init(stats, "rewrite_old_nodes");
		ret = bch2_scan_old_btree_nodes(c, stats);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}
