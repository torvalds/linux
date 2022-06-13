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

void bch2_data_update_read_done(struct data_update *m, struct bch_read_bio *rbio)
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

int bch2_data_update_init(struct bch_fs *c, struct data_update *m,
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
