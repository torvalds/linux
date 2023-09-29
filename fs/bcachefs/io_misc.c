// SPDX-License-Identifier: GPL-2.0
/*
 * io_misc.c - fallocate, fpunch, truncate:
 */

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "buckets.h"
#include "clock.h"
#include "error.h"
#include "extents.h"
#include "extent_update.h"
#include "inode.h"
#include "io_misc.h"
#include "io_write.h"
#include "logged_ops.h"
#include "subvolume.h"

/* Overwrites whatever was present with zeroes: */
int bch2_extent_fallocate(struct btree_trans *trans,
			  subvol_inum inum,
			  struct btree_iter *iter,
			  unsigned sectors,
			  struct bch_io_opts opts,
			  s64 *i_sectors_delta,
			  struct write_point_specifier write_point)
{
	struct bch_fs *c = trans->c;
	struct disk_reservation disk_res = { 0 };
	struct closure cl;
	struct open_buckets open_buckets = { 0 };
	struct bkey_s_c k;
	struct bkey_buf old, new;
	unsigned sectors_allocated = 0;
	bool have_reservation = false;
	bool unwritten = opts.nocow &&
	    c->sb.version >= bcachefs_metadata_version_unwritten_extents;
	int ret;

	bch2_bkey_buf_init(&old);
	bch2_bkey_buf_init(&new);
	closure_init_stack(&cl);

	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		return ret;

	sectors = min_t(u64, sectors, k.k->p.offset - iter->pos.offset);

	if (!have_reservation) {
		unsigned new_replicas =
			max(0, (int) opts.data_replicas -
			    (int) bch2_bkey_nr_ptrs_fully_allocated(k));
		/*
		 * Get a disk reservation before (in the nocow case) calling
		 * into the allocator:
		 */
		ret = bch2_disk_reservation_get(c, &disk_res, sectors, new_replicas, 0);
		if (unlikely(ret))
			goto err;

		bch2_bkey_buf_reassemble(&old, c, k);
	}

	if (have_reservation) {
		if (!bch2_extents_match(k, bkey_i_to_s_c(old.k)))
			goto err;

		bch2_key_resize(&new.k->k, sectors);
	} else if (!unwritten) {
		struct bkey_i_reservation *reservation;

		bch2_bkey_buf_realloc(&new, c, sizeof(*reservation) / sizeof(u64));
		reservation = bkey_reservation_init(new.k);
		reservation->k.p = iter->pos;
		bch2_key_resize(&reservation->k, sectors);
		reservation->v.nr_replicas = opts.data_replicas;
	} else {
		struct bkey_i_extent *e;
		struct bch_devs_list devs_have;
		struct write_point *wp;
		struct bch_extent_ptr *ptr;

		devs_have.nr = 0;

		bch2_bkey_buf_realloc(&new, c, BKEY_EXTENT_U64s_MAX);

		e = bkey_extent_init(new.k);
		e->k.p = iter->pos;

		ret = bch2_alloc_sectors_start_trans(trans,
				opts.foreground_target,
				false,
				write_point,
				&devs_have,
				opts.data_replicas,
				opts.data_replicas,
				BCH_WATERMARK_normal, 0, &cl, &wp);
		if (bch2_err_matches(ret, BCH_ERR_operation_blocked))
			ret = -BCH_ERR_transaction_restart_nested;
		if (ret)
			goto err;

		sectors = min(sectors, wp->sectors_free);
		sectors_allocated = sectors;

		bch2_key_resize(&e->k, sectors);

		bch2_open_bucket_get(c, wp, &open_buckets);
		bch2_alloc_sectors_append_ptrs(c, wp, &e->k_i, sectors, false);
		bch2_alloc_sectors_done(c, wp);

		extent_for_each_ptr(extent_i_to_s(e), ptr)
			ptr->unwritten = true;
	}

	have_reservation = true;

	ret = bch2_extent_update(trans, inum, iter, new.k, &disk_res,
				 0, i_sectors_delta, true);
err:
	if (!ret && sectors_allocated)
		bch2_increment_clock(c, sectors_allocated, WRITE);

	bch2_open_buckets_put(c, &open_buckets);
	bch2_disk_reservation_put(c, &disk_res);
	bch2_bkey_buf_exit(&new, c);
	bch2_bkey_buf_exit(&old, c);

	if (closure_nr_remaining(&cl) != 1) {
		bch2_trans_unlock(trans);
		closure_sync(&cl);
	}

	return ret;
}

/*
 * Returns -BCH_ERR_transacton_restart if we had to drop locks:
 */
int bch2_fpunch_at(struct btree_trans *trans, struct btree_iter *iter,
		   subvol_inum inum, u64 end,
		   s64 *i_sectors_delta)
{
	struct bch_fs *c	= trans->c;
	unsigned max_sectors	= KEY_SIZE_MAX & (~0 << c->block_bits);
	struct bpos end_pos = POS(inum.inum, end);
	struct bkey_s_c k;
	int ret = 0, ret2 = 0;
	u32 snapshot;

	while (!ret ||
	       bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
		struct disk_reservation disk_res =
			bch2_disk_reservation_init(c, 0);
		struct bkey_i delete;

		if (ret)
			ret2 = ret;

		bch2_trans_begin(trans);

		ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
		if (ret)
			continue;

		bch2_btree_iter_set_snapshot(iter, snapshot);

		/*
		 * peek_upto() doesn't have ideal semantics for extents:
		 */
		k = bch2_btree_iter_peek_upto(iter, end_pos);
		if (!k.k)
			break;

		ret = bkey_err(k);
		if (ret)
			continue;

		bkey_init(&delete.k);
		delete.k.p = iter->pos;

		/* create the biggest key we can */
		bch2_key_resize(&delete.k, max_sectors);
		bch2_cut_back(end_pos, &delete);

		ret = bch2_extent_update(trans, inum, iter, &delete,
				&disk_res, 0, i_sectors_delta, false);
		bch2_disk_reservation_put(c, &disk_res);
	}

	return ret ?: ret2;
}

int bch2_fpunch(struct bch_fs *c, subvol_inum inum, u64 start, u64 end,
		s64 *i_sectors_delta)
{
	struct btree_trans *trans = bch2_trans_get(c);
	struct btree_iter iter;
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     POS(inum.inum, start),
			     BTREE_ITER_INTENT);

	ret = bch2_fpunch_at(trans, &iter, inum, end, i_sectors_delta);

	bch2_trans_iter_exit(trans, &iter);
	bch2_trans_put(trans);

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		ret = 0;

	return ret;
}

/* truncate: */

void bch2_logged_op_truncate_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_logged_op_truncate op = bkey_s_c_to_logged_op_truncate(k);

	prt_printf(out, "subvol=%u", le32_to_cpu(op.v->subvol));
	prt_printf(out, " inum=%llu", le64_to_cpu(op.v->inum));
	prt_printf(out, " new_i_size=%llu", le64_to_cpu(op.v->new_i_size));
}

static int truncate_set_isize(struct btree_trans *trans,
			      subvol_inum inum,
			      u64 new_i_size)
{
	struct btree_iter iter = { NULL };
	struct bch_inode_unpacked inode_u;
	int ret;

	ret   = bch2_inode_peek(trans, &iter, &inode_u, inum, BTREE_ITER_INTENT) ?:
		(inode_u.bi_size = new_i_size, 0) ?:
		bch2_inode_write(trans, &iter, &inode_u);

	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int __bch2_resume_logged_op_truncate(struct btree_trans *trans,
					    struct bkey_i *op_k,
					    u64 *i_sectors_delta)
{
	struct bch_fs *c = trans->c;
	struct btree_iter fpunch_iter;
	struct bkey_i_logged_op_truncate *op = bkey_i_to_logged_op_truncate(op_k);
	subvol_inum inum = { le32_to_cpu(op->v.subvol), le64_to_cpu(op->v.inum) };
	u64 new_i_size = le64_to_cpu(op->v.new_i_size);
	int ret;

	ret = commit_do(trans, NULL, NULL, BTREE_INSERT_NOFAIL,
			truncate_set_isize(trans, inum, new_i_size));
	if (ret)
		goto err;

	bch2_trans_iter_init(trans, &fpunch_iter, BTREE_ID_extents,
			     POS(inum.inum, round_up(new_i_size, block_bytes(c)) >> 9),
			     BTREE_ITER_INTENT);
	ret = bch2_fpunch_at(trans, &fpunch_iter, inum, U64_MAX, i_sectors_delta);
	bch2_trans_iter_exit(trans, &fpunch_iter);

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		ret = 0;
err:
	bch2_logged_op_finish(trans, op_k);
	return ret;
}

int bch2_resume_logged_op_truncate(struct btree_trans *trans, struct bkey_i *op_k)
{
	return __bch2_resume_logged_op_truncate(trans, op_k, NULL);
}

int bch2_truncate(struct bch_fs *c, subvol_inum inum, u64 new_i_size, u64 *i_sectors_delta)
{
	struct bkey_i_logged_op_truncate op;

	bkey_logged_op_truncate_init(&op.k_i);
	op.v.subvol	= cpu_to_le32(inum.subvol);
	op.v.inum	= cpu_to_le64(inum.inum);
	op.v.new_i_size	= cpu_to_le64(new_i_size);

	/*
	 * Logged ops aren't atomic w.r.t. snapshot creation: creating a
	 * snapshot while they're in progress, then crashing, will result in the
	 * resume only proceeding in one of the snapshots
	 */
	down_read(&c->snapshot_create_lock);
	int ret = bch2_trans_run(c,
		bch2_logged_op_start(trans, &op.k_i) ?:
		__bch2_resume_logged_op_truncate(trans, &op.k_i, i_sectors_delta));
	up_read(&c->snapshot_create_lock);

	return ret;
}

/* finsert/fcollapse: */

void bch2_logged_op_finsert_to_text(struct printbuf *out, struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_logged_op_finsert op = bkey_s_c_to_logged_op_finsert(k);

	prt_printf(out, "subvol=%u",		le32_to_cpu(op.v->subvol));
	prt_printf(out, " inum=%llu",		le64_to_cpu(op.v->inum));
	prt_printf(out, " dst_offset=%lli",	le64_to_cpu(op.v->dst_offset));
	prt_printf(out, " src_offset=%llu",	le64_to_cpu(op.v->src_offset));
}

static int adjust_i_size(struct btree_trans *trans, subvol_inum inum, u64 offset, s64 len)
{
	struct btree_iter iter;
	struct bch_inode_unpacked inode_u;
	int ret;

	offset	<<= 9;
	len	<<= 9;

	ret = bch2_inode_peek(trans, &iter, &inode_u, inum, BTREE_ITER_INTENT);
	if (ret)
		return ret;

	if (len > 0) {
		if (MAX_LFS_FILESIZE - inode_u.bi_size < len) {
			ret = -EFBIG;
			goto err;
		}

		if (offset >= inode_u.bi_size) {
			ret = -EINVAL;
			goto err;
		}
	}

	inode_u.bi_size += len;
	inode_u.bi_mtime = inode_u.bi_ctime = bch2_current_time(trans->c);

	ret = bch2_inode_write(trans, &iter, &inode_u);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int __bch2_resume_logged_op_finsert(struct btree_trans *trans,
					   struct bkey_i *op_k,
					   u64 *i_sectors_delta)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_i_logged_op_finsert *op = bkey_i_to_logged_op_finsert(op_k);
	subvol_inum inum = { le32_to_cpu(op->v.subvol), le64_to_cpu(op->v.inum) };
	u64 dst_offset = le64_to_cpu(op->v.dst_offset);
	u64 src_offset = le64_to_cpu(op->v.src_offset);
	s64 shift = dst_offset - src_offset;
	u64 len = abs(shift);
	u64 pos = le64_to_cpu(op->v.pos);
	bool insert = shift > 0;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_extents,
			     POS(inum.inum, 0),
			     BTREE_ITER_INTENT);

	switch (op->v.state) {
case LOGGED_OP_FINSERT_start:
	op->v.state = LOGGED_OP_FINSERT_shift_extents;

	if (insert) {
		ret = commit_do(trans, NULL, NULL, BTREE_INSERT_NOFAIL,
				adjust_i_size(trans, inum, src_offset, len) ?:
				bch2_logged_op_update(trans, &op->k_i));
		if (ret)
			goto err;
	} else {
		bch2_btree_iter_set_pos(&iter, POS(inum.inum, src_offset));

		ret = bch2_fpunch_at(trans, &iter, inum, src_offset + len, i_sectors_delta);
		if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
			goto err;

		ret = commit_do(trans, NULL, NULL, BTREE_INSERT_NOFAIL,
				bch2_logged_op_update(trans, &op->k_i));
	}

	fallthrough;
case LOGGED_OP_FINSERT_shift_extents:
	while (1) {
		struct disk_reservation disk_res =
			bch2_disk_reservation_init(c, 0);
		struct bkey_i delete, *copy;
		struct bkey_s_c k;
		struct bpos src_pos = POS(inum.inum, src_offset);
		u32 snapshot;

		bch2_trans_begin(trans);

		ret = bch2_subvolume_get_snapshot(trans, inum.subvol, &snapshot);
		if (ret)
			goto btree_err;

		bch2_btree_iter_set_snapshot(&iter, snapshot);
		bch2_btree_iter_set_pos(&iter, SPOS(inum.inum, pos, snapshot));

		k = insert
			? bch2_btree_iter_peek_prev(&iter)
			: bch2_btree_iter_peek_upto(&iter, POS(inum.inum, U64_MAX));
		if ((ret = bkey_err(k)))
			goto btree_err;

		if (!k.k ||
		    k.k->p.inode != inum.inum ||
		    bkey_le(k.k->p, POS(inum.inum, src_offset)))
			break;

		copy = bch2_bkey_make_mut_noupdate(trans, k);
		if ((ret = PTR_ERR_OR_ZERO(copy)))
			goto btree_err;

		if (insert &&
		    bkey_lt(bkey_start_pos(k.k), src_pos)) {
			bch2_cut_front(src_pos, copy);

			/* Splitting compressed extent? */
			bch2_disk_reservation_add(c, &disk_res,
					copy->k.size *
					bch2_bkey_nr_ptrs_allocated(bkey_i_to_s_c(copy)),
					BCH_DISK_RESERVATION_NOFAIL);
		}

		bkey_init(&delete.k);
		delete.k.p = copy->k.p;
		delete.k.p.snapshot = snapshot;
		delete.k.size = copy->k.size;

		copy->k.p.offset += shift;
		copy->k.p.snapshot = snapshot;

		op->v.pos = cpu_to_le64(insert ? bkey_start_offset(&delete.k) : delete.k.p.offset);

		ret =   bch2_btree_insert_trans(trans, BTREE_ID_extents, &delete, 0) ?:
			bch2_btree_insert_trans(trans, BTREE_ID_extents, copy, 0) ?:
			bch2_logged_op_update(trans, &op->k_i) ?:
			bch2_trans_commit(trans, &disk_res, NULL, BTREE_INSERT_NOFAIL);
btree_err:
		bch2_disk_reservation_put(c, &disk_res);

		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			continue;
		if (ret)
			goto err;

		pos = le64_to_cpu(op->v.pos);
	}

	op->v.state = LOGGED_OP_FINSERT_finish;

	if (!insert) {
		ret = commit_do(trans, NULL, NULL, BTREE_INSERT_NOFAIL,
				adjust_i_size(trans, inum, src_offset, shift) ?:
				bch2_logged_op_update(trans, &op->k_i));
	} else {
		/* We need an inode update to update bi_journal_seq for fsync: */
		ret = commit_do(trans, NULL, NULL, BTREE_INSERT_NOFAIL,
				adjust_i_size(trans, inum, 0, 0) ?:
				bch2_logged_op_update(trans, &op->k_i));
	}

	break;
case LOGGED_OP_FINSERT_finish:
	break;
	}
err:
	bch2_logged_op_finish(trans, op_k);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_resume_logged_op_finsert(struct btree_trans *trans, struct bkey_i *op_k)
{
	return __bch2_resume_logged_op_finsert(trans, op_k, NULL);
}

int bch2_fcollapse_finsert(struct bch_fs *c, subvol_inum inum,
			   u64 offset, u64 len, bool insert,
			   s64 *i_sectors_delta)
{
	struct bkey_i_logged_op_finsert op;
	s64 shift = insert ? len : -len;

	bkey_logged_op_finsert_init(&op.k_i);
	op.v.subvol	= cpu_to_le32(inum.subvol);
	op.v.inum	= cpu_to_le64(inum.inum);
	op.v.dst_offset	= cpu_to_le64(offset + shift);
	op.v.src_offset	= cpu_to_le64(offset);
	op.v.pos	= cpu_to_le64(insert ? U64_MAX : offset);

	/*
	 * Logged ops aren't atomic w.r.t. snapshot creation: creating a
	 * snapshot while they're in progress, then crashing, will result in the
	 * resume only proceeding in one of the snapshots
	 */
	down_read(&c->snapshot_create_lock);
	int ret = bch2_trans_run(c,
		bch2_logged_op_start(trans, &op.k_i) ?:
		__bch2_resume_logged_op_finsert(trans, &op.k_i, i_sectors_delta));
	up_read(&c->snapshot_create_lock);

	return ret;
}
