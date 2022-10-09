// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "buckets.h"
#include "data_update.h"
#include "ec.h"
#include "extents.h"
#include "io.h"
#include "keylist.h"
#include "move.h"
#include "subvolume.h"
#include "trace.h"

static int insert_snapshot_whiteouts(struct btree_trans *trans,
				     enum btree_id id,
				     struct bpos old_pos,
				     struct bpos new_pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter, update_iter;
	struct bkey_s_c k;
	snapshot_id_list s;
	int ret;

	if (!btree_type_has_snapshots(id))
		return 0;

	darray_init(&s);

	if (!bkey_cmp(old_pos, new_pos))
		return 0;

	if (!snapshot_t(c, old_pos.snapshot)->children[0])
		return 0;

	bch2_trans_iter_init(trans, &iter, id, old_pos,
			     BTREE_ITER_NOT_EXTENTS|
			     BTREE_ITER_ALL_SNAPSHOTS);
	while (1) {
		k = bch2_btree_iter_prev(&iter);
		ret = bkey_err(k);
		if (ret)
			break;

		if (bkey_cmp(old_pos, k.k->p))
			break;

		if (bch2_snapshot_is_ancestor(c, k.k->p.snapshot, old_pos.snapshot)) {
			struct bkey_i *update;

			if (snapshot_list_has_ancestor(c, &s, k.k->p.snapshot))
				continue;

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

			ret = snapshot_list_add(c, &s, k.k->p.snapshot);
			if (ret)
				break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);
	darray_exit(&s);

	return ret;
}

static void bch2_bkey_mark_dev_cached(struct bkey_s k, unsigned dev)
{
	struct bkey_ptrs ptrs = bch2_bkey_ptrs(k);
	struct bch_extent_ptr *ptr;

	bkey_for_each_ptr(ptrs, ptr)
		if (ptr->dev == dev)
			ptr->cached = true;
}

int bch2_data_update_index_update(struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct btree_trans trans;
	struct btree_iter iter;
	struct data_update *m =
		container_of(op, struct data_update, op);
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
		struct bkey_s_c old = bkey_i_to_s_c(m->k.k);
		struct bkey_i *insert;
		struct bkey_i_extent *new;
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;
		struct bpos next_pos;
		bool did_work = false;
		bool extending = false, should_check_enospc;
		s64 i_sectors_delta = 0, disk_sectors_delta = 0;
		unsigned i;

		bch2_trans_begin(&trans);

		k = bch2_btree_iter_peek_slot(&iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		new = bkey_i_to_extent(bch2_keylist_front(keys));

		if (!bch2_extents_match(k, old))
			goto nomatch;

		bkey_reassemble(_insert.k, k);
		insert = _insert.k;

		bch2_bkey_buf_copy(&_new, c, bch2_keylist_front(keys));
		new = bkey_i_to_extent(_new.k);
		bch2_cut_front(iter.pos, &new->k_i);

		bch2_cut_front(iter.pos,	insert);
		bch2_cut_back(new->k.p,		insert);
		bch2_cut_back(insert->k.p,	&new->k_i);

		/*
		 * @old: extent that we read from
		 * @insert: key that we're going to update, initialized from
		 * extent currently in btree - same as @old unless we raced with
		 * other updates
		 * @new: extent with new pointers that we'll be adding to @insert
		 *
		 * Fist, drop rewrite_ptrs from @new:
		 */
		i = 0;
		bkey_for_each_ptr_decode(old.k, bch2_bkey_ptrs_c(old), p, entry) {
			if (((1U << i) & m->data_opts.rewrite_ptrs) &&
			    bch2_extent_has_ptr(old, p, bkey_i_to_s_c(insert))) {
				/*
				 * If we're going to be adding a pointer to the
				 * same device, we have to drop the old one -
				 * otherwise, we can just mark it cached:
				 */
				if (bch2_bkey_has_device(bkey_i_to_s_c(&new->k_i), p.ptr.dev))
					bch2_bkey_drop_device_noerror(bkey_i_to_s(insert), p.ptr.dev);
				else
					bch2_bkey_mark_dev_cached(bkey_i_to_s(insert), p.ptr.dev);
			}
			i++;
		}


		/* Add new ptrs: */
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

		bch2_bkey_narrow_crcs(insert, (struct bch_extent_crc_unpacked) { 0 });
		bch2_extent_normalize(c, bkey_i_to_s(insert));

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

			if (ec_ob)
				bch2_ob_add_backpointer(c, ec_ob, &insert->k);

			this_cpu_add(c->counters[BCH_COUNTER_move_extent_finish], new->k.size);
			trace_move_extent_finish(&new->k);
		}
err:
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
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

		this_cpu_add(c->counters[BCH_COUNTER_move_extent_fail], new->k.size);
		trace_move_extent_fail(&new->k);

		bch2_btree_iter_advance(&iter);
		goto next;
	}
out:
	bch2_trans_iter_exit(&trans, &iter);
	bch2_trans_exit(&trans);
	bch2_bkey_buf_exit(&_insert, c);
	bch2_bkey_buf_exit(&_new, c);
	BUG_ON(bch2_err_matches(ret, BCH_ERR_transaction_restart));
	return ret;
}

void bch2_data_update_read_done(struct data_update *m,
				struct bch_extent_crc_unpacked crc)
{
	/* write bio must own pages: */
	BUG_ON(!m->op.wbio.bio.bi_vcnt);

	m->op.crc = crc;
	m->op.wbio.bio.bi_iter.bi_size = crc.compressed_size << 9;

	closure_call(&m->op.cl, bch2_write, NULL, NULL);
}

void bch2_data_update_exit(struct data_update *update)
{
	struct bch_fs *c = update->op.c;

	bch2_bkey_buf_exit(&update->k, c);
	bch2_disk_reservation_put(c, &update->op.res);
	bch2_bio_free_pages_pool(c, &update->op.wbio.bio);
}

int bch2_data_update_init(struct bch_fs *c, struct data_update *m,
			  struct write_point_specifier wp,
			  struct bch_io_opts io_opts,
			  struct data_update_opts data_opts,
			  enum btree_id btree_id,
			  struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned i, reserve_sectors = k.k->size * data_opts.extra_replicas;
	int ret;

	bch2_bkey_buf_init(&m->k);
	bch2_bkey_buf_reassemble(&m->k, c, k);
	m->btree_id	= btree_id;
	m->data_opts	= data_opts;

	bch2_write_op_init(&m->op, c, io_opts);
	m->op.pos	= bkey_start_pos(k.k);
	m->op.version	= k.k->version;
	m->op.target	= data_opts.target,
	m->op.write_point = wp;
	m->op.flags	|= BCH_WRITE_PAGES_STABLE|
		BCH_WRITE_PAGES_OWNED|
		BCH_WRITE_DATA_ENCODED|
		BCH_WRITE_FROM_INTERNAL|
		BCH_WRITE_MOVE|
		m->data_opts.write_flags;
	m->op.compression_type =
		bch2_compression_opt_to_type[io_opts.background_compression ?:
					     io_opts.compression];
	if (m->data_opts.btree_insert_flags & BTREE_INSERT_USE_RESERVE)
		m->op.alloc_reserve = RESERVE_movinggc;

	i = 0;
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		if (((1U << i) & m->data_opts.rewrite_ptrs) &&
		    p.ptr.cached)
			BUG();

		if (!((1U << i) & m->data_opts.rewrite_ptrs))
			bch2_dev_list_add_dev(&m->op.devs_have, p.ptr.dev);

		if (((1U << i) & m->data_opts.rewrite_ptrs) &&
		    crc_is_compressed(p.crc))
			reserve_sectors += k.k->size;

		/*
		 * op->csum_type is normally initialized from the fs/file's
		 * current options - but if an extent is encrypted, we require
		 * that it stays encrypted:
		 */
		if (bch2_csum_type_is_encryption(p.crc.csum_type)) {
			m->op.nonce	= p.crc.nonce + p.crc.offset;
			m->op.csum_type = p.crc.csum_type;
		}

		if (p.crc.compression_type == BCH_COMPRESSION_TYPE_incompressible)
			m->op.incompressible = true;

		i++;
	}

	if (reserve_sectors) {
		ret = bch2_disk_reservation_add(c, &m->op.res, reserve_sectors,
				m->data_opts.extra_replicas
				? 0
				: BCH_DISK_RESERVATION_NOFAIL);
		if (ret)
			return ret;
	}

	m->op.nr_replicas = m->op.nr_replicas_required =
		hweight32(m->data_opts.rewrite_ptrs) + m->data_opts.extra_replicas;

	BUG_ON(!m->op.nr_replicas);
	return 0;
}

void bch2_data_update_opts_normalize(struct bkey_s_c k, struct data_update_opts *opts)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const struct bch_extent_ptr *ptr;
	unsigned i = 0;

	bkey_for_each_ptr(ptrs, ptr) {
		if ((opts->rewrite_ptrs & (1U << i)) && ptr->cached) {
			opts->kill_ptrs |= 1U << i;
			opts->rewrite_ptrs ^= 1U << i;
		}

		i++;
	}
}
