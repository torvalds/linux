// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update.h"
#include "btree_iter.h"
#include "btree_journal_iter.h"
#include "btree_locking.h"
#include "buckets.h"
#include "debug.h"
#include "errcode.h"
#include "error.h"
#include "extents.h"
#include "keylist.h"
#include "snapshot.h"
#include "trace.h"

static inline int btree_insert_entry_cmp(const struct btree_insert_entry *l,
					 const struct btree_insert_entry *r)
{
	return   cmp_int(l->btree_id,	r->btree_id) ?:
		 cmp_int(l->cached,	r->cached) ?:
		 -cmp_int(l->level,	r->level) ?:
		 bpos_cmp(l->k->k.p,	r->k->k.p);
}

static int __must_check
bch2_trans_update_by_path(struct btree_trans *, btree_path_idx_t,
			  struct bkey_i *, enum btree_iter_update_trigger_flags,
			  unsigned long ip);

static noinline int extent_front_merge(struct btree_trans *trans,
				       struct btree_iter *iter,
				       struct bkey_s_c k,
				       struct bkey_i **insert,
				       enum btree_iter_update_trigger_flags flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_i *update;
	int ret;

	if (unlikely(trans->journal_replay_not_finished))
		return 0;

	update = bch2_bkey_make_mut_noupdate(trans, k);
	ret = PTR_ERR_OR_ZERO(update);
	if (ret)
		return ret;

	if (!bch2_bkey_merge(c, bkey_i_to_s(update), bkey_i_to_s_c(*insert)))
		return 0;

	ret =   bch2_key_has_snapshot_overwrites(trans, iter->btree_id, k.k->p) ?:
		bch2_key_has_snapshot_overwrites(trans, iter->btree_id, (*insert)->k.p);
	if (ret < 0)
		return ret;
	if (ret)
		return 0;

	ret = bch2_btree_delete_at(trans, iter, flags);
	if (ret)
		return ret;

	*insert = update;
	return 0;
}

static noinline int extent_back_merge(struct btree_trans *trans,
				      struct btree_iter *iter,
				      struct bkey_i *insert,
				      struct bkey_s_c k)
{
	struct bch_fs *c = trans->c;
	int ret;

	if (unlikely(trans->journal_replay_not_finished))
		return 0;

	ret =   bch2_key_has_snapshot_overwrites(trans, iter->btree_id, insert->k.p) ?:
		bch2_key_has_snapshot_overwrites(trans, iter->btree_id, k.k->p);
	if (ret < 0)
		return ret;
	if (ret)
		return 0;

	bch2_bkey_merge(c, bkey_i_to_s(insert), k);
	return 0;
}

/*
 * When deleting, check if we need to emit a whiteout (because we're overwriting
 * something in an ancestor snapshot)
 */
static int need_whiteout_for_snapshot(struct btree_trans *trans,
				      enum btree_id btree_id, struct bpos pos)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	u32 snapshot = pos.snapshot;
	int ret;

	if (!bch2_snapshot_parent(trans->c, pos.snapshot))
		return 0;

	pos.snapshot++;

	for_each_btree_key_norestart(trans, iter, btree_id, pos,
			   BTREE_ITER_all_snapshots|
			   BTREE_ITER_nopreserve, k, ret) {
		if (!bkey_eq(k.k->p, pos))
			break;

		if (bch2_snapshot_is_ancestor(trans->c, snapshot,
					      k.k->p.snapshot)) {
			ret = !bkey_whiteout(k.k);
			break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

int __bch2_insert_snapshot_whiteouts(struct btree_trans *trans,
				   enum btree_id id,
				   struct bpos old_pos,
				   struct bpos new_pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter old_iter, new_iter = { NULL };
	struct bkey_s_c old_k, new_k;
	snapshot_id_list s;
	struct bkey_i *update;
	int ret = 0;

	if (!bch2_snapshot_has_children(c, old_pos.snapshot))
		return 0;

	darray_init(&s);

	bch2_trans_iter_init(trans, &old_iter, id, old_pos,
			     BTREE_ITER_not_extents|
			     BTREE_ITER_all_snapshots);
	while ((old_k = bch2_btree_iter_prev(&old_iter)).k &&
	       !(ret = bkey_err(old_k)) &&
	       bkey_eq(old_pos, old_k.k->p)) {
		struct bpos whiteout_pos =
			SPOS(new_pos.inode, new_pos.offset, old_k.k->p.snapshot);;

		if (!bch2_snapshot_is_ancestor(c, old_k.k->p.snapshot, old_pos.snapshot) ||
		    snapshot_list_has_ancestor(c, &s, old_k.k->p.snapshot))
			continue;

		new_k = bch2_bkey_get_iter(trans, &new_iter, id, whiteout_pos,
					   BTREE_ITER_not_extents|
					   BTREE_ITER_intent);
		ret = bkey_err(new_k);
		if (ret)
			break;

		if (new_k.k->type == KEY_TYPE_deleted) {
			update = bch2_trans_kmalloc(trans, sizeof(struct bkey_i));
			ret = PTR_ERR_OR_ZERO(update);
			if (ret)
				break;

			bkey_init(&update->k);
			update->k.p		= whiteout_pos;
			update->k.type		= KEY_TYPE_whiteout;

			ret = bch2_trans_update(trans, &new_iter, update,
						BTREE_UPDATE_internal_snapshot_node);
		}
		bch2_trans_iter_exit(trans, &new_iter);

		ret = snapshot_list_add(c, &s, old_k.k->p.snapshot);
		if (ret)
			break;
	}
	bch2_trans_iter_exit(trans, &new_iter);
	bch2_trans_iter_exit(trans, &old_iter);
	darray_exit(&s);

	return ret;
}

int bch2_trans_update_extent_overwrite(struct btree_trans *trans,
				       struct btree_iter *iter,
				       enum btree_iter_update_trigger_flags flags,
				       struct bkey_s_c old,
				       struct bkey_s_c new)
{
	enum btree_id btree_id = iter->btree_id;
	struct bkey_i *update;
	struct bpos new_start = bkey_start_pos(new.k);
	unsigned front_split = bkey_lt(bkey_start_pos(old.k), new_start);
	unsigned back_split  = bkey_gt(old.k->p, new.k->p);
	unsigned middle_split = (front_split || back_split) &&
		old.k->p.snapshot != new.k->p.snapshot;
	unsigned nr_splits = front_split + back_split + middle_split;
	int ret = 0, compressed_sectors;

	/*
	 * If we're going to be splitting a compressed extent, note it
	 * so that __bch2_trans_commit() can increase our disk
	 * reservation:
	 */
	if (nr_splits > 1 &&
	    (compressed_sectors = bch2_bkey_sectors_compressed(old)))
		trans->extra_disk_res += compressed_sectors * (nr_splits - 1);

	if (front_split) {
		update = bch2_bkey_make_mut_noupdate(trans, old);
		if ((ret = PTR_ERR_OR_ZERO(update)))
			return ret;

		bch2_cut_back(new_start, update);

		ret =   bch2_insert_snapshot_whiteouts(trans, btree_id,
					old.k->p, update->k.p) ?:
			bch2_btree_insert_nonextent(trans, btree_id, update,
					BTREE_UPDATE_internal_snapshot_node|flags);
		if (ret)
			return ret;
	}

	/* If we're overwriting in a different snapshot - middle split: */
	if (middle_split) {
		update = bch2_bkey_make_mut_noupdate(trans, old);
		if ((ret = PTR_ERR_OR_ZERO(update)))
			return ret;

		bch2_cut_front(new_start, update);
		bch2_cut_back(new.k->p, update);

		ret =   bch2_insert_snapshot_whiteouts(trans, btree_id,
					old.k->p, update->k.p) ?:
			bch2_btree_insert_nonextent(trans, btree_id, update,
					  BTREE_UPDATE_internal_snapshot_node|flags);
		if (ret)
			return ret;
	}

	if (bkey_le(old.k->p, new.k->p)) {
		update = bch2_trans_kmalloc(trans, sizeof(*update));
		if ((ret = PTR_ERR_OR_ZERO(update)))
			return ret;

		bkey_init(&update->k);
		update->k.p = old.k->p;
		update->k.p.snapshot = new.k->p.snapshot;

		if (new.k->p.snapshot != old.k->p.snapshot) {
			update->k.type = KEY_TYPE_whiteout;
		} else if (btree_type_has_snapshots(btree_id)) {
			ret = need_whiteout_for_snapshot(trans, btree_id, update->k.p);
			if (ret < 0)
				return ret;
			if (ret)
				update->k.type = KEY_TYPE_whiteout;
		}

		ret = bch2_btree_insert_nonextent(trans, btree_id, update,
					  BTREE_UPDATE_internal_snapshot_node|flags);
		if (ret)
			return ret;
	}

	if (back_split) {
		update = bch2_bkey_make_mut_noupdate(trans, old);
		if ((ret = PTR_ERR_OR_ZERO(update)))
			return ret;

		bch2_cut_front(new.k->p, update);

		ret = bch2_trans_update_by_path(trans, iter->path, update,
					  BTREE_UPDATE_internal_snapshot_node|
					  flags, _RET_IP_);
		if (ret)
			return ret;
	}

	return 0;
}

static int bch2_trans_update_extent(struct btree_trans *trans,
				    struct btree_iter *orig_iter,
				    struct bkey_i *insert,
				    enum btree_iter_update_trigger_flags flags)
{
	struct btree_iter iter;
	struct bkey_s_c k;
	enum btree_id btree_id = orig_iter->btree_id;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, btree_id, bkey_start_pos(&insert->k),
			     BTREE_ITER_intent|
			     BTREE_ITER_with_updates|
			     BTREE_ITER_not_extents);
	k = bch2_btree_iter_peek_upto(&iter, POS(insert->k.p.inode, U64_MAX));
	if ((ret = bkey_err(k)))
		goto err;
	if (!k.k)
		goto out;

	if (bkey_eq(k.k->p, bkey_start_pos(&insert->k))) {
		if (bch2_bkey_maybe_mergable(k.k, &insert->k)) {
			ret = extent_front_merge(trans, &iter, k, &insert, flags);
			if (ret)
				goto err;
		}

		goto next;
	}

	while (bkey_gt(insert->k.p, bkey_start_pos(k.k))) {
		bool done = bkey_lt(insert->k.p, k.k->p);

		ret = bch2_trans_update_extent_overwrite(trans, &iter, flags, k, bkey_i_to_s_c(insert));
		if (ret)
			goto err;

		if (done)
			goto out;
next:
		bch2_btree_iter_advance(&iter);
		k = bch2_btree_iter_peek_upto(&iter, POS(insert->k.p.inode, U64_MAX));
		if ((ret = bkey_err(k)))
			goto err;
		if (!k.k)
			goto out;
	}

	if (bch2_bkey_maybe_mergable(&insert->k, k.k)) {
		ret = extent_back_merge(trans, &iter, insert, k);
		if (ret)
			goto err;
	}
out:
	if (!bkey_deleted(&insert->k))
		ret = bch2_btree_insert_nonextent(trans, btree_id, insert, flags);
err:
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static noinline int flush_new_cached_update(struct btree_trans *trans,
					    struct btree_insert_entry *i,
					    enum btree_iter_update_trigger_flags flags,
					    unsigned long ip)
{
	struct bkey k;
	int ret;

	btree_path_idx_t path_idx =
		bch2_path_get(trans, i->btree_id, i->old_k.p, 1, 0,
			      BTREE_ITER_intent, _THIS_IP_);
	ret = bch2_btree_path_traverse(trans, path_idx, 0);
	if (ret)
		goto out;

	struct btree_path *btree_path = trans->paths + path_idx;

	/*
	 * The old key in the insert entry might actually refer to an existing
	 * key in the btree that has been deleted from cache and not yet
	 * flushed. Check for this and skip the flush so we don't run triggers
	 * against a stale key.
	 */
	bch2_btree_path_peek_slot_exact(btree_path, &k);
	if (!bkey_deleted(&k))
		goto out;

	i->key_cache_already_flushed = true;
	i->flags |= BTREE_TRIGGER_norun;

	btree_path_set_should_be_locked(trans, btree_path);
	ret = bch2_trans_update_by_path(trans, path_idx, i->k, flags, ip);
out:
	bch2_path_put(trans, path_idx, true);
	return ret;
}

static int __must_check
bch2_trans_update_by_path(struct btree_trans *trans, btree_path_idx_t path_idx,
			  struct bkey_i *k, enum btree_iter_update_trigger_flags flags,
			  unsigned long ip)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i, n;
	int cmp;

	struct btree_path *path = trans->paths + path_idx;
	EBUG_ON(!path->should_be_locked);
	EBUG_ON(trans->nr_updates >= trans->nr_paths);
	EBUG_ON(!bpos_eq(k->k.p, path->pos));

	n = (struct btree_insert_entry) {
		.flags		= flags,
		.bkey_type	= __btree_node_type(path->level, path->btree_id),
		.btree_id	= path->btree_id,
		.level		= path->level,
		.cached		= path->cached,
		.path		= path_idx,
		.k		= k,
		.ip_allocated	= ip,
	};

#ifdef CONFIG_BCACHEFS_DEBUG
	trans_for_each_update(trans, i)
		BUG_ON(i != trans->updates &&
		       btree_insert_entry_cmp(i - 1, i) >= 0);
#endif

	/*
	 * Pending updates are kept sorted: first, find position of new update,
	 * then delete/trim any updates the new update overwrites:
	 */
	for (i = trans->updates; i < trans->updates + trans->nr_updates; i++) {
		cmp = btree_insert_entry_cmp(&n, i);
		if (cmp <= 0)
			break;
	}

	bool overwrite = !cmp && i < trans->updates + trans->nr_updates;

	if (overwrite) {
		EBUG_ON(i->insert_trigger_run || i->overwrite_trigger_run);

		bch2_path_put(trans, i->path, true);
		i->flags	= n.flags;
		i->cached	= n.cached;
		i->k		= n.k;
		i->path		= n.path;
		i->ip_allocated	= n.ip_allocated;
	} else {
		array_insert_item(trans->updates, trans->nr_updates,
				  i - trans->updates, n);

		i->old_v = bch2_btree_path_peek_slot_exact(path, &i->old_k).v;
		i->old_btree_u64s = !bkey_deleted(&i->old_k) ? i->old_k.u64s : 0;

		if (unlikely(trans->journal_replay_not_finished)) {
			struct bkey_i *j_k =
				bch2_journal_keys_peek_slot(c, n.btree_id, n.level, k->k.p);

			if (j_k) {
				i->old_k = j_k->k;
				i->old_v = &j_k->v;
			}
		}
	}

	__btree_path_get(trans, trans->paths + i->path, true);

	trace_update_by_path(trans, path, i, overwrite);

	/*
	 * If a key is present in the key cache, it must also exist in the
	 * btree - this is necessary for cache coherency. When iterating over
	 * a btree that's cached in the key cache, the btree iter code checks
	 * the key cache - but the key has to exist in the btree for that to
	 * work:
	 */
	if (path->cached && !i->old_btree_u64s)
		return flush_new_cached_update(trans, i, flags, ip);

	return 0;
}

static noinline int bch2_trans_update_get_key_cache(struct btree_trans *trans,
						    struct btree_iter *iter,
						    struct btree_path *path)
{
	struct btree_path *key_cache_path = btree_iter_key_cache_path(trans, iter);

	if (!key_cache_path ||
	    !key_cache_path->should_be_locked ||
	    !bpos_eq(key_cache_path->pos, iter->pos)) {
		struct bkey_cached *ck;
		int ret;

		if (!iter->key_cache_path)
			iter->key_cache_path =
				bch2_path_get(trans, path->btree_id, path->pos, 1, 0,
					      BTREE_ITER_intent|
					      BTREE_ITER_cached, _THIS_IP_);

		iter->key_cache_path =
			bch2_btree_path_set_pos(trans, iter->key_cache_path, path->pos,
						iter->flags & BTREE_ITER_intent,
						_THIS_IP_);

		ret = bch2_btree_path_traverse(trans, iter->key_cache_path, BTREE_ITER_cached);
		if (unlikely(ret))
			return ret;

		ck = (void *) trans->paths[iter->key_cache_path].l[0].b;

		if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
			trace_and_count(trans->c, trans_restart_key_cache_raced, trans, _RET_IP_);
			return btree_trans_restart(trans, BCH_ERR_transaction_restart_key_cache_raced);
		}

		btree_path_set_should_be_locked(trans, trans->paths + iter->key_cache_path);
	}

	return 0;
}

int __must_check bch2_trans_update(struct btree_trans *trans, struct btree_iter *iter,
				   struct bkey_i *k, enum btree_iter_update_trigger_flags flags)
{
	btree_path_idx_t path_idx = iter->update_path ?: iter->path;
	int ret;

	if (iter->flags & BTREE_ITER_is_extents)
		return bch2_trans_update_extent(trans, iter, k, flags);

	if (bkey_deleted(&k->k) &&
	    !(flags & BTREE_UPDATE_key_cache_reclaim) &&
	    (iter->flags & BTREE_ITER_filter_snapshots)) {
		ret = need_whiteout_for_snapshot(trans, iter->btree_id, k->k.p);
		if (unlikely(ret < 0))
			return ret;

		if (ret)
			k->k.type = KEY_TYPE_whiteout;
	}

	/*
	 * Ensure that updates to cached btrees go to the key cache:
	 */
	struct btree_path *path = trans->paths + path_idx;
	if (!(flags & BTREE_UPDATE_key_cache_reclaim) &&
	    !path->cached &&
	    !path->level &&
	    btree_id_cached(trans->c, path->btree_id)) {
		ret = bch2_trans_update_get_key_cache(trans, iter, path);
		if (ret)
			return ret;

		path_idx = iter->key_cache_path;
	}

	return bch2_trans_update_by_path(trans, path_idx, k, flags, _RET_IP_);
}

int bch2_btree_insert_clone_trans(struct btree_trans *trans,
				  enum btree_id btree,
				  struct bkey_i *k)
{
	struct bkey_i *n = bch2_trans_kmalloc(trans, bkey_bytes(&k->k));
	int ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		return ret;

	bkey_copy(n, k);
	return bch2_btree_insert_trans(trans, btree, n, 0);
}

struct jset_entry *__bch2_trans_jset_entry_alloc(struct btree_trans *trans, unsigned u64s)
{
	unsigned new_top = trans->journal_entries_u64s + u64s;
	unsigned old_size = trans->journal_entries_size;

	if (new_top > trans->journal_entries_size) {
		trans->journal_entries_size = roundup_pow_of_two(new_top);

		btree_trans_stats(trans)->journal_entries_size = trans->journal_entries_size;
	}

	struct jset_entry *n =
		bch2_trans_kmalloc_nomemzero(trans,
				trans->journal_entries_size * sizeof(u64));
	if (IS_ERR(n))
		return ERR_CAST(n);

	if (trans->journal_entries)
		memcpy(n, trans->journal_entries, old_size * sizeof(u64));
	trans->journal_entries = n;

	struct jset_entry *e = btree_trans_journal_entries_top(trans);
	trans->journal_entries_u64s = new_top;
	return e;
}

int bch2_bkey_get_empty_slot(struct btree_trans *trans, struct btree_iter *iter,
			     enum btree_id btree, struct bpos end)
{
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_iter_init(trans, iter, btree, POS_MAX, BTREE_ITER_intent);
	k = bch2_btree_iter_prev(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	bch2_btree_iter_advance(iter);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	BUG_ON(k.k->type != KEY_TYPE_deleted);

	if (bkey_gt(k.k->p, end)) {
		ret = -BCH_ERR_ENOSPC_btree_slot;
		goto err;
	}

	return 0;
err:
	bch2_trans_iter_exit(trans, iter);
	return ret;
}

void bch2_trans_commit_hook(struct btree_trans *trans,
			    struct btree_trans_commit_hook *h)
{
	h->next = trans->hooks;
	trans->hooks = h;
}

int bch2_btree_insert_nonextent(struct btree_trans *trans,
				enum btree_id btree, struct bkey_i *k,
				enum btree_iter_update_trigger_flags flags)
{
	struct btree_iter iter;
	int ret;

	bch2_trans_iter_init(trans, &iter, btree, k->k.p,
			     BTREE_ITER_cached|
			     BTREE_ITER_not_extents|
			     BTREE_ITER_intent);
	ret   = bch2_btree_iter_traverse(&iter) ?:
		bch2_trans_update(trans, &iter, k, flags);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_btree_insert_trans(struct btree_trans *trans, enum btree_id id,
			    struct bkey_i *k, enum btree_iter_update_trigger_flags flags)
{
	struct btree_iter iter;
	bch2_trans_iter_init(trans, &iter, id, bkey_start_pos(&k->k),
			     BTREE_ITER_intent|flags);
	int ret = bch2_btree_iter_traverse(&iter) ?:
		  bch2_trans_update(trans, &iter, k, flags);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/**
 * bch2_btree_insert - insert keys into the extent btree
 * @c:			pointer to struct bch_fs
 * @id:			btree to insert into
 * @k:			key to insert
 * @disk_res:		must be non-NULL whenever inserting or potentially
 *			splitting data extents
 * @flags:		transaction commit flags
 * @iter_flags:		btree iter update trigger flags
 *
 * Returns:		0 on success, error code on failure
 */
int bch2_btree_insert(struct bch_fs *c, enum btree_id id, struct bkey_i *k,
		      struct disk_reservation *disk_res, int flags,
		      enum btree_iter_update_trigger_flags iter_flags)
{
	return bch2_trans_commit_do(c, disk_res, NULL, flags,
			     bch2_btree_insert_trans(trans, id, k, iter_flags));
}

int bch2_btree_delete_extent_at(struct btree_trans *trans, struct btree_iter *iter,
				unsigned len, unsigned update_flags)
{
	struct bkey_i *k;

	k = bch2_trans_kmalloc(trans, sizeof(*k));
	if (IS_ERR(k))
		return PTR_ERR(k);

	bkey_init(&k->k);
	k->k.p = iter->pos;
	bch2_key_resize(&k->k, len);
	return bch2_trans_update(trans, iter, k, update_flags);
}

int bch2_btree_delete_at(struct btree_trans *trans,
			 struct btree_iter *iter, unsigned update_flags)
{
	return bch2_btree_delete_extent_at(trans, iter, 0, update_flags);
}

int bch2_btree_delete(struct btree_trans *trans,
		      enum btree_id btree, struct bpos pos,
		      unsigned update_flags)
{
	struct btree_iter iter;
	int ret;

	bch2_trans_iter_init(trans, &iter, btree, pos,
			     BTREE_ITER_cached|
			     BTREE_ITER_intent);
	ret   = bch2_btree_iter_traverse(&iter) ?:
		bch2_btree_delete_at(trans, &iter, update_flags);
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

int bch2_btree_delete_range_trans(struct btree_trans *trans, enum btree_id id,
				  struct bpos start, struct bpos end,
				  unsigned update_flags,
				  u64 *journal_seq)
{
	u32 restart_count = trans->restart_count;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, id, start, BTREE_ITER_intent);
	while ((k = bch2_btree_iter_peek_upto(&iter, end)).k) {
		struct disk_reservation disk_res =
			bch2_disk_reservation_init(trans->c, 0);
		struct bkey_i delete;

		ret = bkey_err(k);
		if (ret)
			goto err;

		bkey_init(&delete.k);

		/*
		 * This could probably be more efficient for extents:
		 */

		/*
		 * For extents, iter.pos won't necessarily be the same as
		 * bkey_start_pos(k.k) (for non extents they always will be the
		 * same). It's important that we delete starting from iter.pos
		 * because the range we want to delete could start in the middle
		 * of k.
		 *
		 * (bch2_btree_iter_peek() does guarantee that iter.pos >=
		 * bkey_start_pos(k.k)).
		 */
		delete.k.p = iter.pos;

		if (iter.flags & BTREE_ITER_is_extents)
			bch2_key_resize(&delete.k,
					bpos_min(end, k.k->p).offset -
					iter.pos.offset);

		ret   = bch2_trans_update(trans, &iter, &delete, update_flags) ?:
			bch2_trans_commit(trans, &disk_res, journal_seq,
					  BCH_TRANS_COMMIT_no_enospc);
		bch2_disk_reservation_put(trans->c, &disk_res);
err:
		/*
		 * the bch2_trans_begin() call is in a weird place because we
		 * need to call it after every transaction commit, to avoid path
		 * overflow, but don't want to call it if the delete operation
		 * is a no-op and we have no work to do:
		 */
		bch2_trans_begin(trans);

		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			ret = 0;
		if (ret)
			break;
	}
	bch2_trans_iter_exit(trans, &iter);

	return ret ?: trans_was_restarted(trans, restart_count);
}

/*
 * bch_btree_delete_range - delete everything within a given range
 *
 * Range is a half open interval - [start, end)
 */
int bch2_btree_delete_range(struct bch_fs *c, enum btree_id id,
			    struct bpos start, struct bpos end,
			    unsigned update_flags,
			    u64 *journal_seq)
{
	int ret = bch2_trans_run(c,
			bch2_btree_delete_range_trans(trans, id, start, end,
						      update_flags, journal_seq));
	if (ret == -BCH_ERR_transaction_restart_nested)
		ret = 0;
	return ret;
}

int bch2_btree_bit_mod(struct btree_trans *trans, enum btree_id btree,
		       struct bpos pos, bool set)
{
	struct bkey_i *k = bch2_trans_kmalloc(trans, sizeof(*k));
	int ret = PTR_ERR_OR_ZERO(k);
	if (ret)
		return ret;

	bkey_init(&k->k);
	k->k.type = set ? KEY_TYPE_set : KEY_TYPE_deleted;
	k->k.p = pos;

	struct btree_iter iter;
	bch2_trans_iter_init(trans, &iter, btree, pos, BTREE_ITER_intent);

	ret   = bch2_btree_iter_traverse(&iter) ?:
		bch2_trans_update(trans, &iter, k, 0);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_btree_bit_mod_buffered(struct btree_trans *trans, enum btree_id btree,
				struct bpos pos, bool set)
{
	struct bkey_i k;

	bkey_init(&k.k);
	k.k.type = set ? KEY_TYPE_set : KEY_TYPE_deleted;
	k.k.p = pos;

	return bch2_trans_update_buffered(trans, btree, &k);
}

static int __bch2_trans_log_msg(struct btree_trans *trans, struct printbuf *buf, unsigned u64s)
{
	struct jset_entry *e = bch2_trans_jset_entry_alloc(trans, jset_u64s(u64s));
	int ret = PTR_ERR_OR_ZERO(e);
	if (ret)
		return ret;

	struct jset_entry_log *l = container_of(e, struct jset_entry_log, entry);
	journal_entry_init(e, BCH_JSET_ENTRY_log, 0, 1, u64s);
	memcpy(l->d, buf->buf, buf->pos);
	return 0;
}

__printf(3, 0)
static int
__bch2_fs_log_msg(struct bch_fs *c, unsigned commit_flags, const char *fmt,
		  va_list args)
{
	struct printbuf buf = PRINTBUF;
	prt_vprintf(&buf, fmt, args);

	unsigned u64s = DIV_ROUND_UP(buf.pos, sizeof(u64));
	prt_chars(&buf, '\0', u64s * sizeof(u64) - buf.pos);

	int ret = buf.allocation_failure ? -BCH_ERR_ENOMEM_trans_log_msg : 0;
	if (ret)
		goto err;

	if (!test_bit(JOURNAL_running, &c->journal.flags)) {
		ret = darray_make_room(&c->journal.early_journal_entries, jset_u64s(u64s));
		if (ret)
			goto err;

		struct jset_entry_log *l = (void *) &darray_top(c->journal.early_journal_entries);
		journal_entry_init(&l->entry, BCH_JSET_ENTRY_log, 0, 1, u64s);
		memcpy(l->d, buf.buf, buf.pos);
		c->journal.early_journal_entries.nr += jset_u64s(u64s);
	} else {
		ret = bch2_trans_commit_do(c, NULL, NULL,
			BCH_TRANS_COMMIT_lazy_rw|commit_flags,
			__bch2_trans_log_msg(trans, &buf, u64s));
	}
err:
	printbuf_exit(&buf);
	return ret;
}

__printf(2, 3)
int bch2_fs_log_msg(struct bch_fs *c, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = __bch2_fs_log_msg(c, 0, fmt, args);
	va_end(args);
	return ret;
}

/*
 * Use for logging messages during recovery to enable reserved space and avoid
 * blocking.
 */
__printf(2, 3)
int bch2_journal_log_msg(struct bch_fs *c, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = __bch2_fs_log_msg(c, BCH_WATERMARK_reclaim, fmt, args);
	va_end(args);
	return ret;
}
