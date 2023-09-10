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
#include "extents.h"
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
