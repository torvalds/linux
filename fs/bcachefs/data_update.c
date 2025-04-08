// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "alloc_foreground.h"
#include "bkey_buf.h"
#include "btree_update.h"
#include "buckets.h"
#include "compress.h"
#include "data_update.h"
#include "disk_groups.h"
#include "ec.h"
#include "error.h"
#include "extents.h"
#include "io_write.h"
#include "keylist.h"
#include "move.h"
#include "nocow_locking.h"
#include "rebalance.h"
#include "snapshot.h"
#include "subvolume.h"
#include "trace.h"

#include <linux/ioprio.h>

static const char * const bch2_data_update_type_strs[] = {
#define x(t, n, ...) [n] = #t,
	BCH_DATA_UPDATE_TYPES()
#undef x
	NULL
};

static void bkey_put_dev_refs(struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);

	bkey_for_each_ptr(ptrs, ptr)
		bch2_dev_put(bch2_dev_have_ref(c, ptr->dev));
}

static bool bkey_get_dev_refs(struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);

	bkey_for_each_ptr(ptrs, ptr) {
		if (unlikely(!bch2_dev_tryget(c, ptr->dev))) {
			bkey_for_each_ptr(ptrs, ptr2) {
				if (ptr2 == ptr)
					break;
				bch2_dev_put(bch2_dev_have_ref(c, ptr2->dev));
			}
			return false;
		}
	}
	return true;
}

static void bkey_nocow_unlock(struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);

	bkey_for_each_ptr(ptrs, ptr) {
		struct bch_dev *ca = bch2_dev_have_ref(c, ptr->dev);
		struct bpos bucket = PTR_BUCKET_POS(ca, ptr);

		bch2_bucket_nocow_unlock(&c->nocow_locks, bucket, 0);
	}
}

static bool bkey_nocow_lock(struct bch_fs *c, struct moving_context *ctxt, struct bkey_s_c k)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);

	bkey_for_each_ptr(ptrs, ptr) {
		struct bch_dev *ca = bch2_dev_have_ref(c, ptr->dev);
		struct bpos bucket = PTR_BUCKET_POS(ca, ptr);

		if (ctxt) {
			bool locked;

			move_ctxt_wait_event(ctxt,
				(locked = bch2_bucket_nocow_trylock(&c->nocow_locks, bucket, 0)) ||
				list_empty(&ctxt->ios));

			if (!locked)
				bch2_bucket_nocow_lock(&c->nocow_locks, bucket, 0);
		} else {
			if (!bch2_bucket_nocow_trylock(&c->nocow_locks, bucket, 0)) {
				bkey_for_each_ptr(ptrs, ptr2) {
					if (ptr2 == ptr)
						break;

					ca = bch2_dev_have_ref(c, ptr2->dev);
					bucket = PTR_BUCKET_POS(ca, ptr2);
					bch2_bucket_nocow_unlock(&c->nocow_locks, bucket, 0);
				}
				return false;
			}
		}
	}
	return true;
}

static noinline void trace_io_move_finish2(struct data_update *u,
					       struct bkey_i *new,
					       struct bkey_i *insert)
{
	struct bch_fs *c = u->op.c;
	struct printbuf buf = PRINTBUF;

	prt_newline(&buf);

	bch2_data_update_to_text(&buf, u);
	prt_newline(&buf);

	prt_str_indented(&buf, "new replicas:\t");
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(new));
	prt_newline(&buf);

	prt_str_indented(&buf, "insert:\t");
	bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(insert));
	prt_newline(&buf);

	trace_io_move_finish(c, buf.buf);
	printbuf_exit(&buf);
}

static void trace_io_move_fail2(struct data_update *m,
			 struct bkey_s_c new,
			 struct bkey_s_c wrote,
			 struct bkey_i *insert,
			 const char *msg)
{
	struct bch_fs *c = m->op.c;
	struct bkey_s_c old = bkey_i_to_s_c(m->k.k);
	struct printbuf buf = PRINTBUF;
	unsigned rewrites_found = 0;

	if (!trace_io_move_fail_enabled())
		return;

	prt_str(&buf, msg);

	if (insert) {
		const union bch_extent_entry *entry;
		struct bch_extent_ptr *ptr;
		struct extent_ptr_decoded p;

		unsigned ptr_bit = 1;
		bkey_for_each_ptr_decode(old.k, bch2_bkey_ptrs_c(old), p, entry) {
			if ((ptr_bit & m->data_opts.rewrite_ptrs) &&
			    (ptr = bch2_extent_has_ptr(old, p, bkey_i_to_s(insert))) &&
			    !ptr->cached)
				rewrites_found |= ptr_bit;
			ptr_bit <<= 1;
		}
	}

	prt_str(&buf, "rewrites found:\t");
	bch2_prt_u64_base2(&buf, rewrites_found);
	prt_newline(&buf);

	bch2_data_update_opts_to_text(&buf, c, &m->op.opts, &m->data_opts);

	prt_str(&buf, "\nold:    ");
	bch2_bkey_val_to_text(&buf, c, old);

	prt_str(&buf, "\nnew:    ");
	bch2_bkey_val_to_text(&buf, c, new);

	prt_str(&buf, "\nwrote:  ");
	bch2_bkey_val_to_text(&buf, c, wrote);

	if (insert) {
		prt_str(&buf, "\ninsert: ");
		bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(insert));
	}

	trace_io_move_fail(c, buf.buf);
	printbuf_exit(&buf);
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
	struct printbuf journal_msg = PRINTBUF;
	int ret = 0;

	bch2_bkey_buf_init(&_new);
	bch2_bkey_buf_init(&_insert);
	bch2_bkey_buf_realloc(&_insert, c, U8_MAX);

	bch2_trans_iter_init(trans, &iter, m->btree_id,
			     bkey_start_pos(&bch2_keylist_front(keys)->k),
			     BTREE_ITER_slots|BTREE_ITER_intent);

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
		unsigned rewrites_found = 0, durability, ptr_bit;

		bch2_trans_begin(trans);

		k = bch2_btree_iter_peek_slot(trans, &iter);
		ret = bkey_err(k);
		if (ret)
			goto err;

		new = bkey_i_to_extent(bch2_keylist_front(keys));

		if (!bch2_extents_match(k, old)) {
			trace_io_move_fail2(m, k, bkey_i_to_s_c(&new->k_i),
						NULL, "no match:");
			goto nowork;
		}

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
		ptr_bit = 1;
		bkey_for_each_ptr_decode(old.k, bch2_bkey_ptrs_c(old), p, entry_c) {
			if ((ptr_bit & m->data_opts.rewrite_ptrs) &&
			    (ptr = bch2_extent_has_ptr(old, p, bkey_i_to_s(insert))) &&
			    !ptr->cached) {
				bch2_extent_ptr_set_cached(c, &m->op.opts,
							   bkey_i_to_s(insert), ptr);
				rewrites_found |= ptr_bit;
			}
			ptr_bit <<= 1;
		}

		if (m->data_opts.rewrite_ptrs &&
		    !rewrites_found &&
		    bch2_bkey_durability(c, k) >= m->op.opts.data_replicas) {
			trace_io_move_fail2(m, k, bkey_i_to_s_c(&new->k_i), insert, "no rewrites found:");
			goto nowork;
		}

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

		if (!bkey_val_u64s(&new->k)) {
			trace_io_move_fail2(m, k, bkey_i_to_s_c(&new->k_i), insert, "new replicas conflicted:");
			goto nowork;
		}

		/* Now, drop pointers that conflict with what we just wrote: */
		extent_for_each_ptr_decode(extent_i_to_s(new), p, entry)
			if ((ptr = bch2_bkey_has_device(bkey_i_to_s(insert), p.ptr.dev)))
				bch2_bkey_drop_ptr_noerror(bkey_i_to_s(insert), ptr);

		durability = bch2_bkey_durability(c, bkey_i_to_s_c(insert)) +
			bch2_bkey_durability(c, bkey_i_to_s_c(&new->k_i));

		/* Now, drop excess replicas: */
		rcu_read_lock();
restart_drop_extra_replicas:
		bkey_for_each_ptr_decode(old.k, bch2_bkey_ptrs(bkey_i_to_s(insert)), p, entry) {
			unsigned ptr_durability = bch2_extent_ptr_durability(c, &p);

			if (!p.ptr.cached &&
			    durability - ptr_durability >= m->op.opts.data_replicas) {
				durability -= ptr_durability;

				bch2_extent_ptr_set_cached(c, &m->op.opts,
							   bkey_i_to_s(insert), &entry->ptr);
				goto restart_drop_extra_replicas;
			}
		}
		rcu_read_unlock();

		/* Finally, add the pointers we just wrote: */
		extent_for_each_ptr_decode(extent_i_to_s(new), p, entry)
			bch2_extent_ptr_decoded_append(insert, &p);

		bch2_bkey_narrow_crcs(insert, (struct bch_extent_crc_unpacked) { 0 });
		bch2_extent_normalize_by_opts(c, &m->op.opts, bkey_i_to_s(insert));

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

		/*
		 * Check for nonce offset inconsistency:
		 * This is debug code - we've been seeing this bug rarely, and
		 * it's been hard to reproduce, so this should give us some more
		 * information when it does occur:
		 */
		int invalid = bch2_bkey_validate(c, bkey_i_to_s_c(insert),
						 (struct bkey_validate_context) {
							.btree	= m->btree_id,
							.flags	= BCH_VALIDATE_commit,
						 });
		if (invalid) {
			struct printbuf buf = PRINTBUF;

			prt_str(&buf, "about to insert invalid key in data update path");
			prt_printf(&buf, "\nop.nonce: %u", m->op.nonce);
			prt_str(&buf, "\nold: ");
			bch2_bkey_val_to_text(&buf, c, old);
			prt_str(&buf, "\nk:   ");
			bch2_bkey_val_to_text(&buf, c, k);
			prt_str(&buf, "\nnew: ");
			bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(insert));

			bch2_print_string_as_lines(KERN_ERR, buf.buf);
			printbuf_exit(&buf);

			bch2_fatal_error(c);
			ret = -BCH_ERR_invalid_bkey;
			goto out;
		}

		if (trace_data_update_enabled()) {
			struct printbuf buf = PRINTBUF;

			prt_str(&buf, "\nold: ");
			bch2_bkey_val_to_text(&buf, c, old);
			prt_str(&buf, "\nk:   ");
			bch2_bkey_val_to_text(&buf, c, k);
			prt_str(&buf, "\nnew: ");
			bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(insert));

			trace_data_update(c, buf.buf);
			printbuf_exit(&buf);
		}

		printbuf_reset(&journal_msg);
		prt_str(&journal_msg, bch2_data_update_type_strs[m->type]);

		ret =   bch2_trans_log_msg(trans, &journal_msg) ?:
			bch2_trans_log_bkey(trans, m->btree_id, 0, m->k.k) ?:
			bch2_insert_snapshot_whiteouts(trans, m->btree_id,
						k.k->p, bkey_start_pos(&insert->k)) ?:
			bch2_insert_snapshot_whiteouts(trans, m->btree_id,
						k.k->p, insert->k.p) ?:
			bch2_bkey_set_needs_rebalance(c, &op->opts, insert) ?:
			bch2_trans_update(trans, &iter, insert,
				BTREE_UPDATE_internal_snapshot_node) ?:
			bch2_trans_commit(trans, &op->res,
				NULL,
				BCH_TRANS_COMMIT_no_check_rw|
				BCH_TRANS_COMMIT_no_enospc|
				m->data_opts.btree_insert_flags);
		if (!ret) {
			bch2_btree_iter_set_pos(trans, &iter, next_pos);

			this_cpu_add(c->counters[BCH_COUNTER_io_move_finish], new->k.size);
			if (trace_io_move_finish_enabled())
				trace_io_move_finish2(m, &new->k_i, insert);
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
		if (m->stats) {
			BUG_ON(k.k->p.offset <= iter.pos.offset);
			atomic64_inc(&m->stats->keys_raced);
			atomic64_add(k.k->p.offset - iter.pos.offset,
				     &m->stats->sectors_raced);
		}

		count_event(c, io_move_fail);

		bch2_btree_iter_advance(trans, &iter);
		goto next;
	}
out:
	printbuf_exit(&journal_msg);
	bch2_trans_iter_exit(trans, &iter);
	bch2_bkey_buf_exit(&_insert, c);
	bch2_bkey_buf_exit(&_new, c);
	BUG_ON(bch2_err_matches(ret, BCH_ERR_transaction_restart));
	return ret;
}

int bch2_data_update_index_update(struct bch_write_op *op)
{
	return bch2_trans_run(op->c, __bch2_data_update_index_update(trans, op));
}

void bch2_data_update_read_done(struct data_update *m)
{
	m->read_done = true;

	/* write bio must own pages: */
	BUG_ON(!m->op.wbio.bio.bi_vcnt);

	m->op.crc = m->rbio.pick.crc;
	m->op.wbio.bio.bi_iter.bi_size = m->op.crc.compressed_size << 9;

	this_cpu_add(m->op.c->counters[BCH_COUNTER_io_move_write], m->k.k->k.size);

	closure_call(&m->op.cl, bch2_write, NULL, NULL);
}

void bch2_data_update_exit(struct data_update *update)
{
	struct bch_fs *c = update->op.c;
	struct bkey_s_c k = bkey_i_to_s_c(update->k.k);

	bch2_bio_free_pages_pool(c, &update->op.wbio.bio);
	kfree(update->bvecs);
	update->bvecs = NULL;

	if (c->opts.nocow_enabled)
		bkey_nocow_unlock(c, k);
	bkey_put_dev_refs(c, k);
	bch2_disk_reservation_put(c, &update->op.res);
	bch2_bkey_buf_exit(&update->k, c);
}

static int bch2_update_unwritten_extent(struct btree_trans *trans,
					struct data_update *update)
{
	struct bch_fs *c = update->op.c;
	struct bkey_i_extent *e;
	struct write_point *wp;
	struct closure cl;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	closure_init_stack(&cl);
	bch2_keylist_init(&update->op.insert_keys, update->op.inline_keys);

	while (bpos_lt(update->op.pos, update->k.k->k.p)) {
		unsigned sectors = update->k.k->k.p.offset -
			update->op.pos.offset;

		bch2_trans_begin(trans);

		bch2_trans_iter_init(trans, &iter, update->btree_id, update->op.pos,
				     BTREE_ITER_slots);
		ret = lockrestart_do(trans, ({
			k = bch2_btree_iter_peek_slot(trans, &iter);
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
				update->op.watermark,
				0, &cl, &wp);
		if (bch2_err_matches(ret, BCH_ERR_operation_blocked)) {
			bch2_trans_unlock(trans);
			closure_sync(&cl);
			continue;
		}

		bch_err_fn_ratelimited(c, ret);

		if (ret)
			break;

		sectors = min(sectors, wp->sectors_free);

		bch2_key_resize(&e->k, sectors);

		bch2_open_bucket_get(c, wp, &update->op.open_buckets);
		bch2_alloc_sectors_append_ptrs(c, wp, &e->k_i, sectors, false);
		bch2_alloc_sectors_done(c, wp);

		update->op.pos.offset += sectors;

		extent_for_each_ptr(extent_i_to_s(e), ptr)
			ptr->unwritten = true;
		bch2_keylist_push(&update->op.insert_keys);

		ret = __bch2_data_update_index_update(trans, &update->op);

		bch2_open_buckets_put(c, &update->op.open_buckets);

		if (ret)
			break;
	}

	if (closure_nr_remaining(&cl) != 1) {
		bch2_trans_unlock(trans);
		closure_sync(&cl);
	}

	return ret;
}

void bch2_data_update_opts_to_text(struct printbuf *out, struct bch_fs *c,
				   struct bch_io_opts *io_opts,
				   struct data_update_opts *data_opts)
{
	if (!out->nr_tabstops)
		printbuf_tabstop_push(out, 20);

	prt_str_indented(out, "rewrite ptrs:\t");
	bch2_prt_u64_base2(out, data_opts->rewrite_ptrs);
	prt_newline(out);

	prt_str_indented(out, "kill ptrs:\t");
	bch2_prt_u64_base2(out, data_opts->kill_ptrs);
	prt_newline(out);

	prt_str_indented(out, "target:\t");
	bch2_target_to_text(out, c, data_opts->target);
	prt_newline(out);

	prt_str_indented(out, "compression:\t");
	bch2_compression_opt_to_text(out, io_opts->background_compression);
	prt_newline(out);

	prt_str_indented(out, "opts.replicas:\t");
	prt_u64(out, io_opts->data_replicas);
	prt_newline(out);

	prt_str_indented(out, "extra replicas:\t");
	prt_u64(out, data_opts->extra_replicas);
}

void bch2_data_update_to_text(struct printbuf *out, struct data_update *m)
{
	prt_str(out, bch2_data_update_type_strs[m->type]);
	prt_newline(out);

	bch2_data_update_opts_to_text(out, m->op.c, &m->op.opts, &m->data_opts);
	prt_newline(out);

	prt_str_indented(out, "old key:\t");
	bch2_bkey_val_to_text(out, m->op.c, bkey_i_to_s_c(m->k.k));
}

void bch2_data_update_inflight_to_text(struct printbuf *out, struct data_update *m)
{
	bch2_bkey_val_to_text(out, m->op.c, bkey_i_to_s_c(m->k.k));
	prt_newline(out);
	printbuf_indent_add(out, 2);
	bch2_data_update_opts_to_text(out, m->op.c, &m->op.opts, &m->data_opts);
	prt_printf(out, "read_done:\t\%u\n", m->read_done);
	bch2_write_op_to_text(out, &m->op);
	printbuf_indent_sub(out, 2);
}

int bch2_extent_drop_ptrs(struct btree_trans *trans,
			  struct btree_iter *iter,
			  struct bkey_s_c k,
			  struct bch_io_opts *io_opts,
			  struct data_update_opts *data_opts)
{
	struct bch_fs *c = trans->c;
	struct bkey_i *n;
	int ret;

	n = bch2_bkey_make_mut_noupdate(trans, k);
	ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		return ret;

	while (data_opts->kill_ptrs) {
		unsigned i = 0, drop = __fls(data_opts->kill_ptrs);

		bch2_bkey_drop_ptrs_noerror(bkey_i_to_s(n), ptr, i++ == drop);
		data_opts->kill_ptrs ^= 1U << drop;
	}

	/*
	 * If the new extent no longer has any pointers, bch2_extent_normalize()
	 * will do the appropriate thing with it (turning it into a
	 * KEY_TYPE_error key, or just a discard if it was a cached extent)
	 */
	bch2_extent_normalize_by_opts(c, io_opts, bkey_i_to_s(n));

	/*
	 * Since we're not inserting through an extent iterator
	 * (BTREE_ITER_all_snapshots iterators aren't extent iterators),
	 * we aren't using the extent overwrite path to delete, we're
	 * just using the normal key deletion path:
	 */
	if (bkey_deleted(&n->k) && !(iter->flags & BTREE_ITER_is_extents))
		n->k.size = 0;

	return bch2_trans_relock(trans) ?:
		bch2_trans_update(trans, iter, n, BTREE_UPDATE_internal_snapshot_node) ?:
		bch2_trans_commit(trans, NULL, NULL, BCH_TRANS_COMMIT_no_enospc);
}

int bch2_data_update_bios_init(struct data_update *m, struct bch_fs *c,
			       struct bch_io_opts *io_opts)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(bkey_i_to_s_c(m->k.k));
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;

	/* write path might have to decompress data: */
	unsigned buf_bytes = 0;
	bkey_for_each_ptr_decode(&m->k.k->k, ptrs, p, entry)
		buf_bytes = max_t(unsigned, buf_bytes, p.crc.uncompressed_size << 9);

	unsigned nr_vecs = DIV_ROUND_UP(buf_bytes, PAGE_SIZE);

	m->bvecs = kmalloc_array(nr_vecs, sizeof*(m->bvecs), GFP_KERNEL);
	if (!m->bvecs)
		return -ENOMEM;

	bio_init(&m->rbio.bio,		NULL, m->bvecs, nr_vecs, REQ_OP_READ);
	bio_init(&m->op.wbio.bio,	NULL, m->bvecs, nr_vecs, 0);

	if (bch2_bio_alloc_pages(&m->op.wbio.bio, buf_bytes, GFP_KERNEL)) {
		kfree(m->bvecs);
		m->bvecs = NULL;
		return -ENOMEM;
	}

	rbio_init(&m->rbio.bio, c, *io_opts, NULL);
	m->rbio.data_update		= true;
	m->rbio.bio.bi_iter.bi_size	= buf_bytes;
	m->rbio.bio.bi_iter.bi_sector	= bkey_start_offset(&m->k.k->k);
	m->op.wbio.bio.bi_ioprio	= IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0);
	return 0;
}

static int can_write_extent(struct bch_fs *c, struct data_update *m)
{
	if ((m->op.flags & BCH_WRITE_alloc_nowait) &&
	    unlikely(c->open_buckets_nr_free <= bch2_open_buckets_reserved(m->op.watermark)))
		return -BCH_ERR_data_update_done_would_block;

	unsigned target = m->op.flags & BCH_WRITE_only_specified_devs
		? m->op.target
		: 0;
	struct bch_devs_mask devs = target_rw_devs(c, BCH_DATA_user, target);

	darray_for_each(m->op.devs_have, i)
		__clear_bit(*i, devs.d);

	rcu_read_lock();
	unsigned nr_replicas = 0, i;
	for_each_set_bit(i, devs.d, BCH_SB_MEMBERS_MAX) {
		struct bch_dev *ca = bch2_dev_rcu(c, i);

		struct bch_dev_usage usage;
		bch2_dev_usage_read_fast(ca, &usage);

		if (!dev_buckets_free(ca, usage, m->op.watermark))
			continue;

		nr_replicas += ca->mi.durability;
		if (nr_replicas >= m->op.nr_replicas)
			break;
	}
	rcu_read_unlock();

	if (!nr_replicas)
		return -BCH_ERR_data_update_done_no_rw_devs;
	if (nr_replicas < m->op.nr_replicas)
		return -BCH_ERR_insufficient_devices;
	return 0;
}

int bch2_data_update_init(struct btree_trans *trans,
			  struct btree_iter *iter,
			  struct moving_context *ctxt,
			  struct data_update *m,
			  struct write_point_specifier wp,
			  struct bch_io_opts *io_opts,
			  struct data_update_opts data_opts,
			  enum btree_id btree_id,
			  struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	unsigned reserve_sectors = k.k->size * data_opts.extra_replicas;
	int ret = 0;

	/*
	 * fs is corrupt  we have a key for a snapshot node that doesn't exist,
	 * and we have to check for this because we go rw before repairing the
	 * snapshots table - just skip it, we can move it later.
	 */
	if (unlikely(k.k->p.snapshot && !bch2_snapshot_exists(c, k.k->p.snapshot)))
		return -BCH_ERR_data_update_done_no_snapshot;

	bch2_bkey_buf_init(&m->k);
	bch2_bkey_buf_reassemble(&m->k, c, k);
	m->type		= data_opts.btree_insert_flags & BCH_WATERMARK_copygc
		? BCH_DATA_UPDATE_copygc
		: BCH_DATA_UPDATE_rebalance;
	m->btree_id	= btree_id;
	m->data_opts	= data_opts;
	m->ctxt		= ctxt;
	m->stats	= ctxt ? ctxt->stats : NULL;

	bch2_write_op_init(&m->op, c, *io_opts);
	m->op.pos	= bkey_start_pos(k.k);
	m->op.version	= k.k->bversion;
	m->op.target	= data_opts.target;
	m->op.write_point = wp;
	m->op.nr_replicas = 0;
	m->op.flags	|= BCH_WRITE_pages_stable|
		BCH_WRITE_pages_owned|
		BCH_WRITE_data_encoded|
		BCH_WRITE_move|
		m->data_opts.write_flags;
	m->op.compression_opt	= io_opts->background_compression;
	m->op.watermark		= m->data_opts.btree_insert_flags & BCH_WATERMARK_MASK;

	unsigned durability_have = 0, durability_removing = 0;

	unsigned ptr_bit = 1;
	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		if (!p.ptr.cached) {
			rcu_read_lock();
			if (ptr_bit & m->data_opts.rewrite_ptrs) {
				if (crc_is_compressed(p.crc))
					reserve_sectors += k.k->size;

				m->op.nr_replicas += bch2_extent_ptr_desired_durability(c, &p);
				durability_removing += bch2_extent_ptr_desired_durability(c, &p);
			} else if (!(ptr_bit & m->data_opts.kill_ptrs)) {
				bch2_dev_list_add_dev(&m->op.devs_have, p.ptr.dev);
				durability_have += bch2_extent_ptr_durability(c, &p);
			}
			rcu_read_unlock();
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

		ptr_bit <<= 1;
	}

	unsigned durability_required = max(0, (int) (io_opts->data_replicas - durability_have));

	/*
	 * If current extent durability is less than io_opts.data_replicas,
	 * we're not trying to rereplicate the extent up to data_replicas here -
	 * unless extra_replicas was specified
	 *
	 * Increasing replication is an explicit operation triggered by
	 * rereplicate, currently, so that users don't get an unexpected -ENOSPC
	 */
	m->op.nr_replicas = min(durability_removing, durability_required) +
		m->data_opts.extra_replicas;

	/*
	 * If device(s) were set to durability=0 after data was written to them
	 * we can end up with a duribilty=0 extent, and the normal algorithm
	 * that tries not to increase durability doesn't work:
	 */
	if (!(durability_have + durability_removing))
		m->op.nr_replicas = max((unsigned) m->op.nr_replicas, 1);

	m->op.nr_replicas_required = m->op.nr_replicas;

	/*
	 * It might turn out that we don't need any new replicas, if the
	 * replicas or durability settings have been changed since the extent
	 * was written:
	 */
	if (!m->op.nr_replicas) {
		m->data_opts.kill_ptrs |= m->data_opts.rewrite_ptrs;
		m->data_opts.rewrite_ptrs = 0;
		/* if iter == NULL, it's just a promote */
		if (iter)
			ret = bch2_extent_drop_ptrs(trans, iter, k, io_opts, &m->data_opts);
		if (!ret)
			ret = -BCH_ERR_data_update_done_no_writes_needed;
		goto out_bkey_buf_exit;
	}

	/*
	 * Check if the allocation will succeed, to avoid getting an error later
	 * in bch2_write() -> bch2_alloc_sectors_start() and doing a useless
	 * read:
	 *
	 * This guards against
	 * - BCH_WRITE_alloc_nowait allocations failing (promotes)
	 * - Destination target full
	 * - Device(s) in destination target offline
	 * - Insufficient durability available in destination target
	 *   (i.e. trying to move a durability=2 replica to a target with a
	 *   single durability=2 device)
	 */
	ret = can_write_extent(c, m);
	if (ret)
		goto out_bkey_buf_exit;

	if (reserve_sectors) {
		ret = bch2_disk_reservation_add(c, &m->op.res, reserve_sectors,
				m->data_opts.extra_replicas
				? 0
				: BCH_DISK_RESERVATION_NOFAIL);
		if (ret)
			goto out_bkey_buf_exit;
	}

	if (!bkey_get_dev_refs(c, k)) {
		ret = -BCH_ERR_data_update_done_no_dev_refs;
		goto out_put_disk_res;
	}

	if (c->opts.nocow_enabled &&
	    !bkey_nocow_lock(c, ctxt, k)) {
		ret = -BCH_ERR_nocow_lock_blocked;
		goto out_put_dev_refs;
	}

	if (bkey_extent_is_unwritten(k)) {
		ret = bch2_update_unwritten_extent(trans, m) ?:
			-BCH_ERR_data_update_done_unwritten;
		goto out_nocow_unlock;
	}

	ret = bch2_data_update_bios_init(m, c, io_opts);
	if (ret)
		goto out_nocow_unlock;

	return 0;
out_nocow_unlock:
	if (c->opts.nocow_enabled)
		bkey_nocow_unlock(c, k);
out_put_dev_refs:
	bkey_put_dev_refs(c, k);
out_put_disk_res:
	bch2_disk_reservation_put(c, &m->op.res);
out_bkey_buf_exit:
	bch2_bkey_buf_exit(&m->k, c);
	return ret;
}

void bch2_data_update_opts_normalize(struct bkey_s_c k, struct data_update_opts *opts)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	unsigned ptr_bit = 1;

	bkey_for_each_ptr(ptrs, ptr) {
		if ((opts->rewrite_ptrs & ptr_bit) && ptr->cached) {
			opts->kill_ptrs |= ptr_bit;
			opts->rewrite_ptrs ^= ptr_bit;
		}

		ptr_bit <<= 1;
	}
}
