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
#include "nocow_locking.h"
#include "subvolume.h"
#include "trace.h"

static int insert_snapshot_whiteouts(struct btree_trans *trans,
				     enum btree_id id,
				     struct bpos old_pos,
				     struct bpos new_pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter, iter2;
	struct bkey_s_c k, k2;
	snapshot_id_list s;
	struct bkey_i *update;
	int ret;

	if (!btree_type_has_snapshots(id))
		return 0;

	darray_init(&s);

	if (!bch2_snapshot_has_children(c, old_pos.snapshot))
		return 0;

	bch2_trans_iter_init(trans, &iter, id, old_pos,
			     BTREE_ITER_NOT_EXTENTS|
			     BTREE_ITER_ALL_SNAPSHOTS);
	while (1) {
		k = bch2_btree_iter_prev(&iter);
		ret = bkey_err(k);
		if (ret)
			break;

		if (!k.k)
			break;

		if (!bkey_eq(old_pos, k.k->p))
			break;

		if (bch2_snapshot_is_ancestor(c, k.k->p.snapshot, old_pos.snapshot) &&
		    !snapshot_list_has_ancestor(c, &s, k.k->p.snapshot)) {
			struct bpos whiteout_pos = new_pos;

			whiteout_pos.snapshot = k.k->p.snapshot;

			bch2_trans_iter_init(trans, &iter2, id, whiteout_pos,
					     BTREE_ITER_NOT_EXTENTS|
					     BTREE_ITER_INTENT);
			k2 = bch2_btree_iter_peek_slot(&iter2);
			ret = bkey_err(k2);

			if (!ret && k2.k->type == KEY_TYPE_deleted) {
				update = bch2_trans_kmalloc(trans, sizeof(struct bkey_i));
				ret = PTR_ERR_OR_ZERO(update);
				if (ret)
					break;

				bkey_init(&update->k);
				update->k.p		= whiteout_pos;
				update->k.type		= KEY_TYPE_whiteout;

				ret = bch2_trans_update(trans, &iter2, update,
							BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
			}
			bch2_trans_iter_exit(trans, &iter2);

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

static int __bch2_data_update_index_update(struct btree_trans *trans,
					   struct bch_write_op *op)
{
	struct bch_fs *c = op->c;
	struct btree_iter iter;
	struct data_update *m =
		container_of(op, struct data_update, op);
	struct keylist *keys = &op->insert_keys;
	struct bkey_buf _new, _insert;
	int ret = 0;

	bch2_bkey_buf_init(&_new);
	bch2_bkey_buf_init(&_insert);
	bch2_bkey_buf_realloc(&_insert, c, U8_MAX);

	bch2_trans_iter_init(trans, &iter, m->btree_id,
			     bkey_start_pos(&bch2_keylist_front(keys)->k),
			     BTREE_ITER_SLOTS|BTREE_ITER_INTENT);

	while (1) {
		struct bkey_s_c k;
		struct bkey_s_c old = bkey_i_to_s_c(m->k.k);
		struct bkey_i *insert = NULL;
		struct bkey_i_extent *new;
		const union bch_extent_entry *entry_c;
		union bch_extent_entry *entry;
		struct extent_ptr_decoded p;
		struct bch_extent_ptr *ptr;
		const struct bch_extent_ptr *ptr_c;
		struct bpos next_pos;
		bool should_check_enospc;
		s64 i_sectors_delta = 0, disk_sectors_delta = 0;
		unsigned rewrites_found = 0, durability, i;

		bch2_trans_begin(trans);

		k = bch2_btree_iter_peek_slot(&iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		new = bkey_i_to_extent(bch2_keylist_front(keys));

		if (!bch2_extents_match(k, old))
			goto nowork;

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
		bkey_for_each_ptr_decode(old.k, bch2_bkey_ptrs_c(old), p, entry_c) {
			if (((1U << i) & m->data_opts.rewrite_ptrs) &&
			    (ptr = bch2_extent_has_ptr(old, p, bkey_i_to_s(insert))) &&
			    !ptr->cached) {
				bch2_extent_ptr_set_cached(bkey_i_to_s(insert), ptr);
				rewrites_found |= 1U << i;
			}
			i++;
		}

		if (m->data_opts.rewrite_ptrs &&
		    !rewrites_found &&
		    bch2_bkey_durability(c, k) >= m->op.opts.data_replicas)
			goto nowork;

		/*
		 * A replica that we just wrote might conflict with a replica
		 * that we want to keep, due to racing with another move:
		 */
restart_drop_conflicting_replicas:
		extent_for_each_ptr(extent_i_to_s(new), ptr)
			if ((ptr_c = bch2_bkey_has_device_c(bkey_i_to_s_c(insert), ptr->dev)) &&
			    !ptr_c->cached) {
				bch2_bkey_drop_ptr_noerror(bkey_i_to_s(&new->k_i), ptr);
				goto restart_drop_conflicting_replicas;
			}

		if (!bkey_val_u64s(&new->k))
			goto nowork;

		/* Now, drop pointers that conflict with what we just wrote: */
		extent_for_each_ptr_decode(extent_i_to_s(new), p, entry)
			if ((ptr = bch2_bkey_has_device(bkey_i_to_s(insert), p.ptr.dev)))
				bch2_bkey_drop_ptr_noerror(bkey_i_to_s(insert), ptr);

		durability = bch2_bkey_durability(c, bkey_i_to_s_c(insert)) +
			bch2_bkey_durability(c, bkey_i_to_s_c(&new->k_i));

		/* Now, drop excess replicas: */
restart_drop_extra_replicas:
		bkey_for_each_ptr_decode(old.k, bch2_bkey_ptrs(bkey_i_to_s(insert)), p, entry) {
			unsigned ptr_durability = bch2_extent_ptr_durability(c, &p);

			if (!p.ptr.cached &&
			    durability - ptr_durability >= m->op.opts.data_replicas) {
				durability -= ptr_durability;
				bch2_extent_ptr_set_cached(bkey_i_to_s(insert), &entry->ptr);
				goto restart_drop_extra_replicas;
			}
		}

		/* Finally, add the pointers we just wrote: */
		extent_for_each_ptr_decode(extent_i_to_s(new), p, entry)
			bch2_extent_ptr_decoded_append(insert, &p);

		bch2_bkey_narrow_crcs(insert, (struct bch_extent_crc_unpacked) { 0 });
		bch2_extent_normalize(c, bkey_i_to_s(insert));

		ret = bch2_sum_sector_overwrites(trans, &iter, insert,
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

		if (!bkey_eq(bkey_start_pos(&insert->k), bkey_start_pos(k.k))) {
			ret = insert_snapshot_whiteouts(trans, m->btree_id, k.k->p,
							bkey_start_pos(&insert->k));
			if (ret)
				goto err;
		}

		if (!bkey_eq(insert->k.p, k.k->p)) {
			ret = insert_snapshot_whiteouts(trans, m->btree_id,
							k.k->p, insert->k.p);
			if (ret)
				goto err;
		}

		ret   = bch2_trans_update(trans, &iter, insert,
				BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE) ?:
			bch2_trans_commit(trans, &op->res,
				NULL,
				BTREE_INSERT_NOCHECK_RW|
				BTREE_INSERT_NOFAIL|
				m->data_opts.btree_insert_flags);
		if (!ret) {
			bch2_btree_iter_set_pos(&iter, next_pos);

			this_cpu_add(c->counters[BCH_COUNTER_move_extent_finish], new->k.size);
			trace_move_extent_finish(&new->k);
		}
err:
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			ret = 0;
		if (ret)
			break;
next:
		while (bkey_ge(iter.pos, bch2_keylist_front(keys)->k.p)) {
			bch2_keylist_pop_front(keys);
			if (bch2_keylist_empty(keys))
				goto out;
		}
		continue;
nowork:
		if (m->ctxt && m->ctxt->stats) {
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
	bch2_trans_iter_exit(trans, &iter);
	bch2_bkey_buf_exit(&_insert, c);
	bch2_bkey_buf_exit(&_new, c);
	BUG_ON(bch2_err_matches(ret, BCH_ERR_transaction_restart));
	return ret;
}

int bch2_data_update_index_update(struct bch_write_op *op)
{
	return bch2_trans_run(op->c, __bch2_data_update_index_update(&trans, op));
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
	struct bkey_ptrs_c ptrs =
		bch2_bkey_ptrs_c(bkey_i_to_s_c(update->k.k));
	const struct bch_extent_ptr *ptr;

	bkey_for_each_ptr(ptrs, ptr) {
		if (c->opts.nocow_enabled)
			bch2_bucket_nocow_unlock(&c->nocow_locks,
						 PTR_BUCKET_POS(c, ptr), 0);
		percpu_ref_put(&bch_dev_bkey_exists(c, ptr->dev)->ref);
	}

	bch2_bkey_buf_exit(&update->k, c);
	bch2_disk_reservation_put(c, &update->op.res);
	bch2_bio_free_pages_pool(c, &update->op.wbio.bio);
}

void bch2_update_unwritten_extent(struct btree_trans *trans,
				  struct data_update *update)
{
	struct bch_fs *c = update->op.c;
	struct bio *bio = &update->op.wbio.bio;
	struct bkey_i_extent *e;
	struct write_point *wp;
	struct bch_extent_ptr *ptr;
	struct closure cl;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	closure_init_stack(&cl);
	bch2_keylist_init(&update->op.insert_keys, update->op.inline_keys);

	while (bio_sectors(bio)) {
		unsigned sectors = bio_sectors(bio);

		bch2_trans_iter_init(trans, &iter, update->btree_id, update->op.pos,
				     BTREE_ITER_SLOTS);
		ret = lockrestart_do(trans, ({
			k = bch2_btree_iter_peek_slot(&iter);
			bkey_err(k);
		}));
		bch2_trans_iter_exit(trans, &iter);

		if (ret || !bch2_extents_match(k, bkey_i_to_s_c(update->k.k)))
			break;

		e = bkey_extent_init(update->op.insert_keys.top);
		e->k.p = update->op.pos;

		ret = bch2_alloc_sectors_start_trans(trans,
				update->op.target,
				false,
				update->op.write_point,
				&update->op.devs_have,
				update->op.nr_replicas,
				update->op.nr_replicas,
				update->op.alloc_reserve,
				0, &cl, &wp);
		if (bch2_err_matches(ret, BCH_ERR_operation_blocked)) {
			bch2_trans_unlock(trans);
			closure_sync(&cl);
			continue;
		}

		if (ret)
			return;

		sectors = min(sectors, wp->sectors_free);

		bch2_key_resize(&e->k, sectors);

		bch2_open_bucket_get(c, wp, &update->op.open_buckets);
		bch2_alloc_sectors_append_ptrs(c, wp, &e->k_i, sectors, false);
		bch2_alloc_sectors_done(c, wp);

		bio_advance(bio, sectors << 9);
		update->op.pos.offset += sectors;

		extent_for_each_ptr(extent_i_to_s(e), ptr)
			ptr->unwritten = true;
		bch2_keylist_push(&update->op.insert_keys);

		ret = __bch2_data_update_index_update(trans, &update->op);

		bch2_open_buckets_put(c, &update->op.open_buckets);

		if (ret)
			break;
	}

	if ((atomic_read(&cl.remaining) & CLOSURE_REMAINING_MASK) != 1) {
		bch2_trans_unlock(trans);
		closure_sync(&cl);
	}
}

int bch2_data_update_init(struct btree_trans *trans,
			  struct moving_context *ctxt,
			  struct data_update *m,
			  struct write_point_specifier wp,
			  struct bch_io_opts io_opts,
			  struct data_update_opts data_opts,
			  enum btree_id btree_id,
			  struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	const struct bch_extent_ptr *ptr;
	unsigned i, reserve_sectors = k.k->size * data_opts.extra_replicas;
	unsigned ptrs_locked = 0;
	int ret;

	bch2_bkey_buf_init(&m->k);
	bch2_bkey_buf_reassemble(&m->k, c, k);
	m->btree_id	= btree_id;
	m->data_opts	= data_opts;

	bch2_write_op_init(&m->op, c, io_opts);
	m->op.pos	= bkey_start_pos(k.k);
	m->op.version	= k.k->version;
	m->op.target	= data_opts.target;
	m->op.write_point = wp;
	m->op.nr_replicas = 0;
	m->op.flags	|= BCH_WRITE_PAGES_STABLE|
		BCH_WRITE_PAGES_OWNED|
		BCH_WRITE_DATA_ENCODED|
		BCH_WRITE_MOVE|
		m->data_opts.write_flags;
	m->op.compression_type =
		bch2_compression_opt_to_type[io_opts.background_compression ?:
					     io_opts.compression];
	if (m->data_opts.btree_insert_flags & BTREE_INSERT_USE_RESERVE)
		m->op.alloc_reserve = RESERVE_movinggc;

	bkey_for_each_ptr(ptrs, ptr)
		percpu_ref_get(&bch_dev_bkey_exists(c, ptr->dev)->ref);

	i = 0;
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		bool locked;

		if (((1U << i) & m->data_opts.rewrite_ptrs)) {
			BUG_ON(p.ptr.cached);

			if (crc_is_compressed(p.crc))
				reserve_sectors += k.k->size;

			m->op.nr_replicas += bch2_extent_ptr_durability(c, &p);
		} else if (!p.ptr.cached) {
			bch2_dev_list_add_dev(&m->op.devs_have, p.ptr.dev);
		}

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

		if (c->opts.nocow_enabled) {
			if (ctxt) {
				move_ctxt_wait_event(ctxt, trans,
						(locked = bch2_bucket_nocow_trylock(&c->nocow_locks,
									  PTR_BUCKET_POS(c, &p.ptr), 0)) ||
						!atomic_read(&ctxt->read_sectors));

				if (!locked)
					bch2_bucket_nocow_lock(&c->nocow_locks,
							       PTR_BUCKET_POS(c, &p.ptr), 0);
			} else {
				if (!bch2_bucket_nocow_trylock(&c->nocow_locks,
							       PTR_BUCKET_POS(c, &p.ptr), 0)) {
					ret = -BCH_ERR_nocow_lock_blocked;
					goto err;
				}
			}
			ptrs_locked |= (1U << i);
		}

		i++;
	}

	if (reserve_sectors) {
		ret = bch2_disk_reservation_add(c, &m->op.res, reserve_sectors,
				m->data_opts.extra_replicas
				? 0
				: BCH_DISK_RESERVATION_NOFAIL);
		if (ret)
			goto err;
	}

	m->op.nr_replicas += m->data_opts.extra_replicas;
	m->op.nr_replicas_required = m->op.nr_replicas;

	BUG_ON(!m->op.nr_replicas);

	/* Special handling required: */
	if (bkey_extent_is_unwritten(k))
		return -BCH_ERR_unwritten_extent_update;
	return 0;
err:
	i = 0;
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		if ((1U << i) & ptrs_locked)
			bch2_bucket_nocow_unlock(&c->nocow_locks,
						 PTR_BUCKET_POS(c, &p.ptr), 0);
		percpu_ref_put(&bch_dev_bkey_exists(c, p.ptr.dev)->ref);
		i++;
	}

	bch2_bkey_buf_exit(&m->k, c);
	bch2_bio_free_pages_pool(c, &m->op.wbio.bio);
	return ret;
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
