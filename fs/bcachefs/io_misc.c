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
	struct btree_trans trans;
	struct btree_iter iter;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 1024);
	bch2_trans_iter_init(&trans, &iter, BTREE_ID_extents,
			     POS(inum.inum, start),
			     BTREE_ITER_INTENT);

	ret = bch2_fpunch_at(&trans, &iter, inum, end, i_sectors_delta);

	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);

	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		ret = 0;

	return ret;
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

int bch2_truncate(struct bch_fs *c, subvol_inum inum, u64 new_i_size, u64 *i_sectors_delta)
{
	struct btree_trans trans;
	struct btree_iter fpunch_iter;
	int ret;

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 1024);
	bch2_trans_iter_init(&trans, &fpunch_iter, BTREE_ID_extents,
			     POS(inum.inum, round_up(new_i_size, block_bytes(c)) >> 9),
			     BTREE_ITER_INTENT);

	ret = commit_do(&trans, NULL, NULL, BTREE_INSERT_NOFAIL,
			truncate_set_isize(&trans, inum, new_i_size));
	if (ret)
		goto err;

	ret = bch2_fpunch_at(&trans, &fpunch_iter, inum, U64_MAX, i_sectors_delta);
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
		ret = 0;
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(&trans, &fpunch_iter);
	bch2_trans_exit(&trans);

	bch2_fs_fatal_err_on(ret, c, "%s: error truncating %u:%llu: %s",
			    __func__, inum.subvol, inum.inum, bch2_err_str(ret));
	return ret;
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

int bch2_fcollapse_finsert(struct bch_fs *c, subvol_inum inum,
			   u64 offset, u64 len, bool insert,
			   s64 *i_sectors_delta)
{
	struct bkey_buf copy;
	struct btree_trans trans;
	struct btree_iter src = { NULL }, dst = { NULL }, del = { NULL };
	s64 shift = insert ? len : -len;
	int ret = 0;

	bch2_bkey_buf_init(&copy);
	bch2_trans_init(&trans, c, 0, 1024);

	bch2_trans_iter_init(&trans, &src, BTREE_ID_extents,
			     POS(inum.inum, U64_MAX),
			     BTREE_ITER_INTENT);
	bch2_trans_copy_iter(&dst, &src);
	bch2_trans_copy_iter(&del, &src);

	if (insert) {
		ret = commit_do(&trans, NULL, NULL, BTREE_INSERT_NOFAIL,
				adjust_i_size(&trans, inum, offset, len));
		if (ret)
			goto err;
	} else {
		bch2_btree_iter_set_pos(&src, POS(inum.inum, offset));

		ret = bch2_fpunch_at(&trans, &src, inum, offset + len, i_sectors_delta);
		if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
			goto err;

		bch2_btree_iter_set_pos(&src, POS(inum.inum, offset + len));
	}

	while (ret == 0 || bch2_err_matches(ret, BCH_ERR_transaction_restart)) {
		struct disk_reservation disk_res =
			bch2_disk_reservation_init(c, 0);
		struct bkey_i delete;
		struct bkey_s_c k;
		struct bpos next_pos;
		struct bpos move_pos = POS(inum.inum, offset);
		struct bpos atomic_end;
		unsigned trigger_flags = 0;
		u32 snapshot;

		bch2_trans_begin(&trans);

		ret = bch2_subvolume_get_snapshot(&trans, inum.subvol, &snapshot);
		if (ret)
			continue;

		bch2_btree_iter_set_snapshot(&src, snapshot);
		bch2_btree_iter_set_snapshot(&dst, snapshot);
		bch2_btree_iter_set_snapshot(&del, snapshot);

		bch2_trans_begin(&trans);

		k = insert
			? bch2_btree_iter_peek_prev(&src)
			: bch2_btree_iter_peek_upto(&src, POS(inum.inum, U64_MAX));
		if ((ret = bkey_err(k)))
			continue;

		if (!k.k || k.k->p.inode != inum.inum)
			break;

		if (insert &&
		    bkey_le(k.k->p, POS(inum.inum, offset)))
			break;
reassemble:
		bch2_bkey_buf_reassemble(&copy, c, k);

		if (insert &&
		    bkey_lt(bkey_start_pos(k.k), move_pos))
			bch2_cut_front(move_pos, copy.k);

		copy.k->k.p.offset += shift;
		bch2_btree_iter_set_pos(&dst, bkey_start_pos(&copy.k->k));

		ret = bch2_extent_atomic_end(&trans, &dst, copy.k, &atomic_end);
		if (ret)
			continue;

		if (!bkey_eq(atomic_end, copy.k->k.p)) {
			if (insert) {
				move_pos = atomic_end;
				move_pos.offset -= shift;
				goto reassemble;
			} else {
				bch2_cut_back(atomic_end, copy.k);
			}
		}

		bkey_init(&delete.k);
		delete.k.p = copy.k->k.p;
		delete.k.size = copy.k->k.size;
		delete.k.p.offset -= shift;
		bch2_btree_iter_set_pos(&del, bkey_start_pos(&delete.k));

		next_pos = insert ? bkey_start_pos(&delete.k) : delete.k.p;

		if (copy.k->k.size != k.k->size) {
			/* We might end up splitting compressed extents: */
			unsigned nr_ptrs =
				bch2_bkey_nr_ptrs_allocated(bkey_i_to_s_c(copy.k));

			ret = bch2_disk_reservation_get(c, &disk_res,
					copy.k->k.size, nr_ptrs,
					BCH_DISK_RESERVATION_NOFAIL);
			BUG_ON(ret);
		}

		ret =   bch2_btree_iter_traverse(&del) ?:
			bch2_trans_update(&trans, &del, &delete, trigger_flags) ?:
			bch2_trans_update(&trans, &dst, copy.k, trigger_flags) ?:
			bch2_trans_commit(&trans, &disk_res, NULL,
					  BTREE_INSERT_NOFAIL);
		bch2_disk_reservation_put(c, &disk_res);

		if (!ret)
			bch2_btree_iter_set_pos(&src, next_pos);
	}

	if (ret && !bch2_err_matches(ret, BCH_ERR_transaction_restart))
		goto err;

	if (!insert) {
		ret = commit_do(&trans, NULL, NULL, BTREE_INSERT_NOFAIL,
				adjust_i_size(&trans, inum, offset, -len));
	} else {
		/* We need an inode update to update bi_journal_seq for fsync: */
		ret = commit_do(&trans, NULL, NULL, BTREE_INSERT_NOFAIL,
				adjust_i_size(&trans, inum, 0, 0));
	}
err:
	bch2_trans_iter_exit(&trans, &del);
	bch2_trans_iter_exit(&trans, &dst);
	bch2_trans_iter_exit(&trans, &src);
	bch2_trans_exit(&trans);
	bch2_bkey_buf_exit(&copy, c);
	return ret;
}
