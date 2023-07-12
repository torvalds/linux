// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_gc.h"
#include "btree_io.h"
#include "btree_iter.h"
#include "btree_key_cache.h"
#include "btree_locking.h"
#include "btree_write_buffer.h"
#include "buckets.h"
#include "debug.h"
#include "errcode.h"
#include "error.h"
#include "extent_update.h"
#include "journal.h"
#include "journal_reclaim.h"
#include "keylist.h"
#include "recovery.h"
#include "subvolume.h"
#include "replicas.h"
#include "trace.h"

#include <linux/prefetch.h>
#include <linux/sort.h>

/*
 * bch2_btree_path_peek_slot() for a cached iterator might return a key in a
 * different snapshot:
 */
static struct bkey_s_c bch2_btree_path_peek_slot_exact(struct btree_path *path, struct bkey *u)
{
	struct bkey_s_c k = bch2_btree_path_peek_slot(path, u);

	if (k.k && bpos_eq(path->pos, k.k->p))
		return k;

	bkey_init(u);
	u->p = path->pos;
	return (struct bkey_s_c) { u, NULL };
}

static void verify_update_old_key(struct btree_trans *trans, struct btree_insert_entry *i)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct bch_fs *c = trans->c;
	struct bkey u;
	struct bkey_s_c k = bch2_btree_path_peek_slot_exact(i->path, &u);

	if (unlikely(trans->journal_replay_not_finished)) {
		struct bkey_i *j_k =
			bch2_journal_keys_peek_slot(c, i->btree_id, i->level, i->k->k.p);

		if (j_k)
			k = bkey_i_to_s_c(j_k);
	}

	u = *k.k;
	u.needs_whiteout = i->old_k.needs_whiteout;

	BUG_ON(memcmp(&i->old_k, &u, sizeof(struct bkey)));
	BUG_ON(i->old_v != k.v);
#endif
}

static int __must_check
bch2_trans_update_by_path(struct btree_trans *, struct btree_path *,
			  struct bkey_i *, enum btree_update_flags);

static inline int btree_insert_entry_cmp(const struct btree_insert_entry *l,
					 const struct btree_insert_entry *r)
{
	return   cmp_int(l->btree_id,	r->btree_id) ?:
		 cmp_int(l->cached,	r->cached) ?:
		 -cmp_int(l->level,	r->level) ?:
		 bpos_cmp(l->k->k.p,	r->k->k.p);
}

static inline struct btree_path_level *insert_l(struct btree_insert_entry *i)
{
	return i->path->l + i->level;
}

static inline bool same_leaf_as_prev(struct btree_trans *trans,
				     struct btree_insert_entry *i)
{
	return i != trans->updates &&
		insert_l(&i[0])->b == insert_l(&i[-1])->b;
}

static inline bool same_leaf_as_next(struct btree_trans *trans,
				     struct btree_insert_entry *i)
{
	return i + 1 < trans->updates + trans->nr_updates &&
		insert_l(&i[0])->b == insert_l(&i[1])->b;
}

inline void bch2_btree_node_prep_for_write(struct btree_trans *trans,
					   struct btree_path *path,
					   struct btree *b)
{
	struct bch_fs *c = trans->c;

	if (unlikely(btree_node_just_written(b)) &&
	    bch2_btree_post_write_cleanup(c, b))
		bch2_trans_node_reinit_iter(trans, b);

	/*
	 * If the last bset has been written, or if it's gotten too big - start
	 * a new bset to insert into:
	 */
	if (want_new_bset(c, b))
		bch2_btree_init_next(trans, b);
}

/* Inserting into a given leaf node (last stage of insert): */

/* Handle overwrites and do insert, for non extents: */
bool bch2_btree_bset_insert_key(struct btree_trans *trans,
				struct btree_path *path,
				struct btree *b,
				struct btree_node_iter *node_iter,
				struct bkey_i *insert)
{
	struct bkey_packed *k;
	unsigned clobber_u64s = 0, new_u64s = 0;

	EBUG_ON(btree_node_just_written(b));
	EBUG_ON(bset_written(b, btree_bset_last(b)));
	EBUG_ON(bkey_deleted(&insert->k) && bkey_val_u64s(&insert->k));
	EBUG_ON(bpos_lt(insert->k.p, b->data->min_key));
	EBUG_ON(bpos_gt(insert->k.p, b->data->max_key));
	EBUG_ON(insert->k.u64s >
		bch_btree_keys_u64s_remaining(trans->c, b));

	k = bch2_btree_node_iter_peek_all(node_iter, b);
	if (k && bkey_cmp_left_packed(b, k, &insert->k.p))
		k = NULL;

	/* @k is the key being overwritten/deleted, if any: */
	EBUG_ON(k && bkey_deleted(k));

	/* Deleting, but not found? nothing to do: */
	if (bkey_deleted(&insert->k) && !k)
		return false;

	if (bkey_deleted(&insert->k)) {
		/* Deleting: */
		btree_account_key_drop(b, k);
		k->type = KEY_TYPE_deleted;

		if (k->needs_whiteout)
			push_whiteout(trans->c, b, insert->k.p);
		k->needs_whiteout = false;

		if (k >= btree_bset_last(b)->start) {
			clobber_u64s = k->u64s;
			bch2_bset_delete(b, k, clobber_u64s);
			goto fix_iter;
		} else {
			bch2_btree_path_fix_key_modified(trans, b, k);
		}

		return true;
	}

	if (k) {
		/* Overwriting: */
		btree_account_key_drop(b, k);
		k->type = KEY_TYPE_deleted;

		insert->k.needs_whiteout = k->needs_whiteout;
		k->needs_whiteout = false;

		if (k >= btree_bset_last(b)->start) {
			clobber_u64s = k->u64s;
			goto overwrite;
		} else {
			bch2_btree_path_fix_key_modified(trans, b, k);
		}
	}

	k = bch2_btree_node_iter_bset_pos(node_iter, b, bset_tree_last(b));
overwrite:
	bch2_bset_insert(b, node_iter, k, insert, clobber_u64s);
	new_u64s = k->u64s;
fix_iter:
	if (clobber_u64s != new_u64s)
		bch2_btree_node_iter_fix(trans, path, b, node_iter, k,
					 clobber_u64s, new_u64s);
	return true;
}

static int __btree_node_flush(struct journal *j, struct journal_entry_pin *pin,
			       unsigned i, u64 seq)
{
	struct bch_fs *c = container_of(j, struct bch_fs, journal);
	struct btree_write *w = container_of(pin, struct btree_write, journal);
	struct btree *b = container_of(w, struct btree, writes[i]);
	struct btree_trans trans;
	unsigned long old, new, v;
	unsigned idx = w - b->writes;

	bch2_trans_init(&trans, c, 0, 0);

	btree_node_lock_nopath_nofail(&trans, &b->c, SIX_LOCK_read);
	v = READ_ONCE(b->flags);

	do {
		old = new = v;

		if (!(old & (1 << BTREE_NODE_dirty)) ||
		    !!(old & (1 << BTREE_NODE_write_idx)) != idx ||
		    w->journal.seq != seq)
			break;

		new &= ~BTREE_WRITE_TYPE_MASK;
		new |= BTREE_WRITE_journal_reclaim;
		new |= 1 << BTREE_NODE_need_write;
	} while ((v = cmpxchg(&b->flags, old, new)) != old);

	btree_node_write_if_need(c, b, SIX_LOCK_read);
	six_unlock_read(&b->c.lock);

	bch2_trans_exit(&trans);
	return 0;
}

int bch2_btree_node_flush0(struct journal *j, struct journal_entry_pin *pin, u64 seq)
{
	return __btree_node_flush(j, pin, 0, seq);
}

int bch2_btree_node_flush1(struct journal *j, struct journal_entry_pin *pin, u64 seq)
{
	return __btree_node_flush(j, pin, 1, seq);
}

inline void bch2_btree_add_journal_pin(struct bch_fs *c,
				       struct btree *b, u64 seq)
{
	struct btree_write *w = btree_current_write(b);

	bch2_journal_pin_add(&c->journal, seq, &w->journal,
			     btree_node_write_idx(b) == 0
			     ? bch2_btree_node_flush0
			     : bch2_btree_node_flush1);
}

/**
 * btree_insert_key - insert a key one key into a leaf node
 */
inline void bch2_btree_insert_key_leaf(struct btree_trans *trans,
				       struct btree_path *path,
				       struct bkey_i *insert,
				       u64 journal_seq)
{
	struct bch_fs *c = trans->c;
	struct btree *b = path_l(path)->b;
	struct bset_tree *t = bset_tree_last(b);
	struct bset *i = bset(b, t);
	int old_u64s = bset_u64s(t);
	int old_live_u64s = b->nr.live_u64s;
	int live_u64s_added, u64s_added;

	if (unlikely(!bch2_btree_bset_insert_key(trans, path, b,
					&path_l(path)->iter, insert)))
		return;

	i->journal_seq = cpu_to_le64(max(journal_seq, le64_to_cpu(i->journal_seq)));

	bch2_btree_add_journal_pin(c, b, journal_seq);

	if (unlikely(!btree_node_dirty(b))) {
		EBUG_ON(test_bit(BCH_FS_CLEAN_SHUTDOWN, &c->flags));
		set_btree_node_dirty_acct(c, b);
	}

	live_u64s_added = (int) b->nr.live_u64s - old_live_u64s;
	u64s_added = (int) bset_u64s(t) - old_u64s;

	if (b->sib_u64s[0] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[0] = max(0, (int) b->sib_u64s[0] + live_u64s_added);
	if (b->sib_u64s[1] != U16_MAX && live_u64s_added < 0)
		b->sib_u64s[1] = max(0, (int) b->sib_u64s[1] + live_u64s_added);

	if (u64s_added > live_u64s_added &&
	    bch2_maybe_compact_whiteouts(c, b))
		bch2_trans_node_reinit_iter(trans, b);
}

static void btree_insert_key_leaf(struct btree_trans *trans,
				  struct btree_insert_entry *insert)
{
	bch2_btree_insert_key_leaf(trans, insert->path, insert->k, trans->journal_res.seq);
}

/* Cached btree updates: */

/* Normal update interface: */

static inline void btree_insert_entry_checks(struct btree_trans *trans,
					     struct btree_insert_entry *i)
{
	BUG_ON(!bpos_eq(i->k->k.p, i->path->pos));
	BUG_ON(i->cached	!= i->path->cached);
	BUG_ON(i->level		!= i->path->level);
	BUG_ON(i->btree_id	!= i->path->btree_id);
	EBUG_ON(!i->level &&
		!(i->flags & BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE) &&
		test_bit(JOURNAL_REPLAY_DONE, &trans->c->journal.flags) &&
		i->k->k.p.snapshot &&
		bch2_snapshot_is_internal_node(trans->c, i->k->k.p.snapshot));
}

static noinline int
bch2_trans_journal_preres_get_cold(struct btree_trans *trans, unsigned flags,
				   unsigned long trace_ip)
{
	return drop_locks_do(trans,
		bch2_journal_preres_get(&trans->c->journal,
			&trans->journal_preres,
			trans->journal_preres_u64s,
			(flags & BCH_WATERMARK_MASK)));
}

static __always_inline int bch2_trans_journal_res_get(struct btree_trans *trans,
						      unsigned flags)
{
	return bch2_journal_res_get(&trans->c->journal, &trans->journal_res,
				    trans->journal_u64s, flags);
}

#define JSET_ENTRY_LOG_U64s		4

static noinline void journal_transaction_name(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct journal *j = &c->journal;
	struct jset_entry *entry =
		bch2_journal_add_entry(j, &trans->journal_res,
				       BCH_JSET_ENTRY_log, 0, 0,
				       JSET_ENTRY_LOG_U64s);
	struct jset_entry_log *l =
		container_of(entry, struct jset_entry_log, entry);

	strncpy(l->d, trans->fn, JSET_ENTRY_LOG_U64s * sizeof(u64));
}

static inline int btree_key_can_insert(struct btree_trans *trans,
				       struct btree *b, unsigned u64s)
{
	struct bch_fs *c = trans->c;

	if (!bch2_btree_node_insert_fits(c, b, u64s))
		return -BCH_ERR_btree_insert_btree_node_full;

	return 0;
}

static int btree_key_can_insert_cached(struct btree_trans *trans, unsigned flags,
				       struct btree_path *path, unsigned u64s)
{
	struct bch_fs *c = trans->c;
	struct bkey_cached *ck = (void *) path->l[0].b;
	struct btree_insert_entry *i;
	unsigned new_u64s;
	struct bkey_i *new_k;

	EBUG_ON(path->level);

	if (!test_bit(BKEY_CACHED_DIRTY, &ck->flags) &&
	    bch2_btree_key_cache_must_wait(c) &&
	    !(flags & BTREE_INSERT_JOURNAL_RECLAIM))
		return -BCH_ERR_btree_insert_need_journal_reclaim;

	/*
	 * bch2_varint_decode can read past the end of the buffer by at most 7
	 * bytes (it won't be used):
	 */
	u64s += 1;

	if (u64s <= ck->u64s)
		return 0;

	new_u64s	= roundup_pow_of_two(u64s);
	new_k		= krealloc(ck->k, new_u64s * sizeof(u64), GFP_NOFS);
	if (!new_k) {
		bch_err(c, "error allocating memory for key cache key, btree %s u64s %u",
			bch2_btree_ids[path->btree_id], new_u64s);
		return -BCH_ERR_ENOMEM_btree_key_cache_insert;
	}

	trans_for_each_update(trans, i)
		if (i->old_v == &ck->k->v)
			i->old_v = &new_k->v;

	ck->u64s	= new_u64s;
	ck->k		= new_k;
	return 0;
}

/* Triggers: */

static int run_one_mem_trigger(struct btree_trans *trans,
			       struct btree_insert_entry *i,
			       unsigned flags)
{
	struct bkey_s_c old = { &i->old_k, i->old_v };
	struct bkey_i *new = i->k;
	const struct bkey_ops *old_ops = bch2_bkey_type_ops(old.k->type);
	const struct bkey_ops *new_ops = bch2_bkey_type_ops(i->k->k.type);
	int ret;

	verify_update_old_key(trans, i);

	if (unlikely(flags & BTREE_TRIGGER_NORUN))
		return 0;

	if (!btree_node_type_needs_gc(i->btree_id))
		return 0;

	if (old_ops->atomic_trigger == new_ops->atomic_trigger &&
	    ((1U << old.k->type) & BTREE_TRIGGER_WANTS_OLD_AND_NEW)) {
		ret   = bch2_mark_key(trans, i->btree_id, i->level,
				old, bkey_i_to_s_c(new),
				BTREE_TRIGGER_INSERT|BTREE_TRIGGER_OVERWRITE|flags);
	} else {
		struct bkey		_deleted = KEY(0, 0, 0);
		struct bkey_s_c		deleted = (struct bkey_s_c) { &_deleted, NULL };

		_deleted.p = i->path->pos;

		ret   = bch2_mark_key(trans, i->btree_id, i->level,
				deleted, bkey_i_to_s_c(new),
				BTREE_TRIGGER_INSERT|flags) ?:
			bch2_mark_key(trans, i->btree_id, i->level,
				old, deleted,
				BTREE_TRIGGER_OVERWRITE|flags);
	}

	return ret;
}

static int run_one_trans_trigger(struct btree_trans *trans, struct btree_insert_entry *i,
				 bool overwrite)
{
	/*
	 * Transactional triggers create new btree_insert_entries, so we can't
	 * pass them a pointer to a btree_insert_entry, that memory is going to
	 * move:
	 */
	struct bkey old_k = i->old_k;
	struct bkey_s_c old = { &old_k, i->old_v };
	const struct bkey_ops *old_ops = bch2_bkey_type_ops(old.k->type);
	const struct bkey_ops *new_ops = bch2_bkey_type_ops(i->k->k.type);

	verify_update_old_key(trans, i);

	if ((i->flags & BTREE_TRIGGER_NORUN) ||
	    !(BTREE_NODE_TYPE_HAS_TRANS_TRIGGERS & (1U << i->bkey_type)))
		return 0;

	if (!i->insert_trigger_run &&
	    !i->overwrite_trigger_run &&
	    old_ops->trans_trigger == new_ops->trans_trigger &&
	    ((1U << old.k->type) & BTREE_TRIGGER_WANTS_OLD_AND_NEW)) {
		i->overwrite_trigger_run = true;
		i->insert_trigger_run = true;
		return bch2_trans_mark_key(trans, i->btree_id, i->level, old, i->k,
					   BTREE_TRIGGER_INSERT|
					   BTREE_TRIGGER_OVERWRITE|
					   i->flags) ?: 1;
	} else if (overwrite && !i->overwrite_trigger_run) {
		i->overwrite_trigger_run = true;
		return bch2_trans_mark_old(trans, i->btree_id, i->level, old, i->flags) ?: 1;
	} else if (!overwrite && !i->insert_trigger_run) {
		i->insert_trigger_run = true;
		return bch2_trans_mark_new(trans, i->btree_id, i->level, i->k, i->flags) ?: 1;
	} else {
		return 0;
	}
}

static int run_btree_triggers(struct btree_trans *trans, enum btree_id btree_id,
			      struct btree_insert_entry *btree_id_start)
{
	struct btree_insert_entry *i;
	bool trans_trigger_run;
	int ret, overwrite;

	for (overwrite = 1; overwrite >= 0; --overwrite) {

		/*
		 * Running triggers will append more updates to the list of updates as
		 * we're walking it:
		 */
		do {
			trans_trigger_run = false;

			for (i = btree_id_start;
			     i < trans->updates + trans->nr_updates && i->btree_id <= btree_id;
			     i++) {
				if (i->btree_id != btree_id)
					continue;

				ret = run_one_trans_trigger(trans, i, overwrite);
				if (ret < 0)
					return ret;
				if (ret)
					trans_trigger_run = true;
			}
		} while (trans_trigger_run);
	}

	return 0;
}

static int bch2_trans_commit_run_triggers(struct btree_trans *trans)
{
	struct btree_insert_entry *i = NULL, *btree_id_start = trans->updates;
	unsigned btree_id = 0;
	int ret = 0;

	/*
	 *
	 * For a given btree, this algorithm runs insert triggers before
	 * overwrite triggers: this is so that when extents are being moved
	 * (e.g. by FALLOCATE_FL_INSERT_RANGE), we don't drop references before
	 * they are re-added.
	 */
	for (btree_id = 0; btree_id < BTREE_ID_NR; btree_id++) {
		if (btree_id == BTREE_ID_alloc)
			continue;

		while (btree_id_start < trans->updates + trans->nr_updates &&
		       btree_id_start->btree_id < btree_id)
			btree_id_start++;

		ret = run_btree_triggers(trans, btree_id, btree_id_start);
		if (ret)
			return ret;
	}

	trans_for_each_update(trans, i) {
		if (i->btree_id > BTREE_ID_alloc)
			break;
		if (i->btree_id == BTREE_ID_alloc) {
			ret = run_btree_triggers(trans, BTREE_ID_alloc, i);
			if (ret)
				return ret;
			break;
		}
	}

#ifdef CONFIG_BCACHEFS_DEBUG
	trans_for_each_update(trans, i)
		BUG_ON(!(i->flags & BTREE_TRIGGER_NORUN) &&
		       (BTREE_NODE_TYPE_HAS_TRANS_TRIGGERS & (1U << i->bkey_type)) &&
		       (!i->insert_trigger_run || !i->overwrite_trigger_run));
#endif
	return 0;
}

static noinline int bch2_trans_commit_run_gc_triggers(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	int ret = 0;

	trans_for_each_update(trans, i) {
		/*
		 * XXX: synchronization of cached update triggers with gc
		 * XXX: synchronization of interior node updates with gc
		 */
		BUG_ON(i->cached || i->level);

		if (gc_visited(c, gc_pos_btree_node(insert_l(i)->b))) {
			ret = run_one_mem_trigger(trans, i, i->flags|BTREE_TRIGGER_GC);
			if (ret)
				break;
		}
	}

	return ret;
}

static inline int
bch2_trans_commit_write_locked(struct btree_trans *trans, unsigned flags,
			       struct btree_insert_entry **stopped_at,
			       unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	struct btree_write_buffered_key *wb;
	struct btree_trans_commit_hook *h;
	unsigned u64s = 0;
	bool marking = false;
	int ret;

	if (race_fault()) {
		trace_and_count(c, trans_restart_fault_inject, trans, trace_ip);
		return btree_trans_restart_nounlock(trans, BCH_ERR_transaction_restart_fault_inject);
	}

	/*
	 * Check if the insert will fit in the leaf node with the write lock
	 * held, otherwise another thread could write the node changing the
	 * amount of space available:
	 */

	prefetch(&trans->c->journal.flags);

	trans_for_each_update(trans, i) {
		/* Multiple inserts might go to same leaf: */
		if (!same_leaf_as_prev(trans, i))
			u64s = 0;

		u64s += i->k->k.u64s;
		ret = !i->cached
			? btree_key_can_insert(trans, insert_l(i)->b, u64s)
			: btree_key_can_insert_cached(trans, flags, i->path, u64s);
		if (ret) {
			*stopped_at = i;
			return ret;
		}

		if (btree_node_type_needs_gc(i->bkey_type))
			marking = true;
	}

	if (trans->nr_wb_updates &&
	    trans->nr_wb_updates + c->btree_write_buffer.state.nr > c->btree_write_buffer.size)
		return -BCH_ERR_btree_insert_need_flush_buffer;

	/*
	 * Don't get journal reservation until after we know insert will
	 * succeed:
	 */
	if (likely(!(flags & BTREE_INSERT_JOURNAL_REPLAY))) {
		ret = bch2_trans_journal_res_get(trans,
				(flags & BCH_WATERMARK_MASK)|
				JOURNAL_RES_GET_NONBLOCK);
		if (ret)
			return ret;

		if (unlikely(trans->journal_transaction_names))
			journal_transaction_name(trans);
	} else {
		trans->journal_res.seq = c->journal.replay_journal_seq;
	}

	/*
	 * Not allowed to fail after we've gotten our journal reservation - we
	 * have to use it:
	 */

	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG) &&
	    !(flags & BTREE_INSERT_JOURNAL_REPLAY)) {
		if (bch2_journal_seq_verify)
			trans_for_each_update(trans, i)
				i->k->k.version.lo = trans->journal_res.seq;
		else if (bch2_inject_invalid_keys)
			trans_for_each_update(trans, i)
				i->k->k.version = MAX_VERSION;
	}

	if (trans->fs_usage_deltas &&
	    bch2_trans_fs_usage_apply(trans, trans->fs_usage_deltas))
		return -BCH_ERR_btree_insert_need_mark_replicas;

	if (trans->nr_wb_updates) {
		EBUG_ON(flags & BTREE_INSERT_JOURNAL_REPLAY);

		ret = bch2_btree_insert_keys_write_buffer(trans);
		if (ret)
			goto revert_fs_usage;
	}

	h = trans->hooks;
	while (h) {
		ret = h->fn(trans, h);
		if (ret)
			goto revert_fs_usage;
		h = h->next;
	}

	trans_for_each_update(trans, i)
		if (BTREE_NODE_TYPE_HAS_MEM_TRIGGERS & (1U << i->bkey_type)) {
			ret = run_one_mem_trigger(trans, i, i->flags);
			if (ret)
				goto fatal_err;
		}

	if (unlikely(c->gc_pos.phase)) {
		ret = bch2_trans_commit_run_gc_triggers(trans);
		if  (ret)
			goto fatal_err;
	}

	if (unlikely(trans->extra_journal_entries.nr)) {
		memcpy_u64s_small(journal_res_entry(&c->journal, &trans->journal_res),
				  trans->extra_journal_entries.data,
				  trans->extra_journal_entries.nr);

		trans->journal_res.offset	+= trans->extra_journal_entries.nr;
		trans->journal_res.u64s		-= trans->extra_journal_entries.nr;
	}

	if (likely(!(flags & BTREE_INSERT_JOURNAL_REPLAY))) {
		struct journal *j = &c->journal;
		struct jset_entry *entry;

		trans_for_each_update(trans, i) {
			if (i->key_cache_already_flushed)
				continue;

			if (i->flags & BTREE_UPDATE_NOJOURNAL)
				continue;

			verify_update_old_key(trans, i);

			if (trans->journal_transaction_names) {
				entry = bch2_journal_add_entry(j, &trans->journal_res,
						       BCH_JSET_ENTRY_overwrite,
						       i->btree_id, i->level,
						       i->old_k.u64s);
				bkey_reassemble(&entry->start[0],
						(struct bkey_s_c) { &i->old_k, i->old_v });
			}

			entry = bch2_journal_add_entry(j, &trans->journal_res,
					       BCH_JSET_ENTRY_btree_keys,
					       i->btree_id, i->level,
					       i->k->k.u64s);
			bkey_copy(&entry->start[0], i->k);
		}

		trans_for_each_wb_update(trans, wb) {
			entry = bch2_journal_add_entry(j, &trans->journal_res,
					       BCH_JSET_ENTRY_btree_keys,
					       wb->btree, 0,
					       wb->k.k.u64s);
			bkey_copy(&entry->start[0], &wb->k);
		}

		if (trans->journal_seq)
			*trans->journal_seq = trans->journal_res.seq;
	}

	trans_for_each_update(trans, i) {
		i->k->k.needs_whiteout = false;

		if (!i->cached)
			btree_insert_key_leaf(trans, i);
		else if (!i->key_cache_already_flushed)
			bch2_btree_insert_key_cached(trans, flags, i);
		else {
			bch2_btree_key_cache_drop(trans, i->path);
			btree_path_set_dirty(i->path, BTREE_ITER_NEED_TRAVERSE);
		}
	}

	return 0;
fatal_err:
	bch2_fatal_error(c);
revert_fs_usage:
	if (trans->fs_usage_deltas)
		bch2_trans_fs_usage_revert(trans, trans->fs_usage_deltas);
	return ret;
}

static noinline int trans_lock_write_fail(struct btree_trans *trans, struct btree_insert_entry *i)
{
	while (--i >= trans->updates) {
		if (same_leaf_as_prev(trans, i))
			continue;

		bch2_btree_node_unlock_write(trans, i->path, insert_l(i)->b);
	}

	trace_and_count(trans->c, trans_restart_would_deadlock_write, trans);
	return btree_trans_restart(trans, BCH_ERR_transaction_restart_would_deadlock_write);
}

static inline int trans_lock_write(struct btree_trans *trans)
{
	struct btree_insert_entry *i;

	trans_for_each_update(trans, i) {
		if (same_leaf_as_prev(trans, i))
			continue;

		if (bch2_btree_node_lock_write(trans, i->path, &insert_l(i)->b->c))
			return trans_lock_write_fail(trans, i);

		if (!i->cached)
			bch2_btree_node_prep_for_write(trans, i->path, insert_l(i)->b);
	}

	return 0;
}

static noinline void bch2_drop_overwrites_from_journal(struct btree_trans *trans)
{
	struct btree_insert_entry *i;
	struct btree_write_buffered_key *wb;

	trans_for_each_update(trans, i)
		bch2_journal_key_overwritten(trans->c, i->btree_id, i->level, i->k->k.p);

	trans_for_each_wb_update(trans, wb)
		bch2_journal_key_overwritten(trans->c, wb->btree, 0, wb->k.k.p);
}

#ifdef CONFIG_BCACHEFS_DEBUG
static noinline int bch2_trans_commit_bkey_invalid(struct btree_trans *trans, unsigned flags,
						   struct btree_insert_entry *i,
						   struct printbuf *err)
{
	struct bch_fs *c = trans->c;
	int rw = (flags & BTREE_INSERT_JOURNAL_REPLAY) ? READ : WRITE;

	printbuf_reset(err);
	prt_printf(err, "invalid bkey on insert from %s -> %ps",
		   trans->fn, (void *) i->ip_allocated);
	prt_newline(err);
	printbuf_indent_add(err, 2);

	bch2_bkey_val_to_text(err, c, bkey_i_to_s_c(i->k));
	prt_newline(err);

	bch2_bkey_invalid(c, bkey_i_to_s_c(i->k),
			  i->bkey_type, rw, err);
	bch2_print_string_as_lines(KERN_ERR, err->buf);

	bch2_inconsistent_error(c);
	bch2_dump_trans_updates(trans);
	printbuf_exit(err);

	return -EINVAL;
}
#endif

/*
 * Get journal reservation, take write locks, and attempt to do btree update(s):
 */
static inline int do_bch2_trans_commit(struct btree_trans *trans, unsigned flags,
				       struct btree_insert_entry **stopped_at,
				       unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	int ret, u64s_delta = 0;

#ifdef CONFIG_BCACHEFS_DEBUG
	struct printbuf buf = PRINTBUF;

	trans_for_each_update(trans, i) {
		enum bkey_invalid_flags invalid_flags = 0;

		if (!(flags & BTREE_INSERT_JOURNAL_REPLAY))
			invalid_flags |= BKEY_INVALID_WRITE|BKEY_INVALID_COMMIT;

		if (unlikely(bch2_bkey_invalid(c, bkey_i_to_s_c(i->k),
					       i->bkey_type, invalid_flags, &buf)))
			return bch2_trans_commit_bkey_invalid(trans, flags, i, &buf);
		btree_insert_entry_checks(trans, i);
	}
	printbuf_exit(&buf);
#endif

	trans_for_each_update(trans, i) {
		if (i->cached)
			continue;

		u64s_delta += !bkey_deleted(&i->k->k) ? i->k->k.u64s : 0;
		u64s_delta -= i->old_btree_u64s;

		if (!same_leaf_as_next(trans, i)) {
			if (u64s_delta <= 0) {
				ret = bch2_foreground_maybe_merge(trans, i->path,
							i->level, flags);
				if (unlikely(ret))
					return ret;
			}

			u64s_delta = 0;
		}
	}

	ret = bch2_journal_preres_get(&c->journal,
			&trans->journal_preres, trans->journal_preres_u64s,
			(flags & BCH_WATERMARK_MASK)|JOURNAL_RES_GET_NONBLOCK);
	if (unlikely(ret == -BCH_ERR_journal_preres_get_blocked))
		ret = bch2_trans_journal_preres_get_cold(trans, flags, trace_ip);
	if (unlikely(ret))
		return ret;

	ret = trans_lock_write(trans);
	if (unlikely(ret))
		return ret;

	ret = bch2_trans_commit_write_locked(trans, flags, stopped_at, trace_ip);

	if (!ret && unlikely(trans->journal_replay_not_finished))
		bch2_drop_overwrites_from_journal(trans);

	trans_for_each_update(trans, i)
		if (!same_leaf_as_prev(trans, i))
			bch2_btree_node_unlock_write_inlined(trans, i->path,
							insert_l(i)->b);

	if (!ret && trans->journal_pin)
		bch2_journal_pin_add(&c->journal, trans->journal_res.seq,
				     trans->journal_pin, NULL);

	/*
	 * Drop journal reservation after dropping write locks, since dropping
	 * the journal reservation may kick off a journal write:
	 */
	bch2_journal_res_put(&c->journal, &trans->journal_res);

	if (unlikely(ret))
		return ret;

	bch2_trans_downgrade(trans);

	return 0;
}

static int journal_reclaim_wait_done(struct bch_fs *c)
{
	int ret = bch2_journal_error(&c->journal) ?:
		!bch2_btree_key_cache_must_wait(c);

	if (!ret)
		journal_reclaim_kick(&c->journal);
	return ret;
}

static noinline
int bch2_trans_commit_error(struct btree_trans *trans, unsigned flags,
			    struct btree_insert_entry *i,
			    int ret, unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;

	switch (ret) {
	case -BCH_ERR_btree_insert_btree_node_full:
		ret = bch2_btree_split_leaf(trans, i->path, flags);
		if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
			trace_and_count(c, trans_restart_btree_node_split, trans, trace_ip, i->path);
		break;
	case -BCH_ERR_btree_insert_need_mark_replicas:
		ret = drop_locks_do(trans,
			bch2_replicas_delta_list_mark(c, trans->fs_usage_deltas));
		break;
	case -BCH_ERR_journal_res_get_blocked:
		/*
		 * XXX: this should probably be a separate BTREE_INSERT_NONBLOCK
		 * flag
		 */
		if ((flags & BTREE_INSERT_JOURNAL_RECLAIM) &&
		    (flags & BCH_WATERMARK_MASK) != BCH_WATERMARK_reclaim) {
			ret = -BCH_ERR_journal_reclaim_would_deadlock;
			break;
		}

		ret = drop_locks_do(trans,
			bch2_trans_journal_res_get(trans,
					(flags & BCH_WATERMARK_MASK)|
					JOURNAL_RES_GET_CHECK));
		break;
	case -BCH_ERR_btree_insert_need_journal_reclaim:
		bch2_trans_unlock(trans);

		trace_and_count(c, trans_blocked_journal_reclaim, trans, trace_ip);

		wait_event_freezable(c->journal.reclaim_wait,
				     (ret = journal_reclaim_wait_done(c)));
		if (ret < 0)
			break;

		ret = bch2_trans_relock(trans);
		break;
	case -BCH_ERR_btree_insert_need_flush_buffer: {
		struct btree_write_buffer *wb = &c->btree_write_buffer;

		ret = 0;

		if (wb->state.nr > wb->size * 3 / 4) {
			bch2_trans_unlock(trans);
			mutex_lock(&wb->flush_lock);

			if (wb->state.nr > wb->size * 3 / 4) {
				bch2_trans_begin(trans);
				ret = __bch2_btree_write_buffer_flush(trans,
						flags|BTREE_INSERT_NOCHECK_RW, true);
				if (!ret) {
					trace_and_count(c, trans_restart_write_buffer_flush, trans, _THIS_IP_);
					ret = btree_trans_restart(trans, BCH_ERR_transaction_restart_write_buffer_flush);
				}
			} else {
				mutex_unlock(&wb->flush_lock);
				ret = bch2_trans_relock(trans);
			}
		}
		break;
	}
	default:
		BUG_ON(ret >= 0);
		break;
	}

	BUG_ON(bch2_err_matches(ret, BCH_ERR_transaction_restart) != !!trans->restarted);

	bch2_fs_inconsistent_on(bch2_err_matches(ret, ENOSPC) &&
				!(flags & BTREE_INSERT_NOWAIT) &&
				(flags & BTREE_INSERT_NOFAIL), c,
		"%s: incorrectly got %s\n", __func__, bch2_err_str(ret));

	return ret;
}

static noinline int
bch2_trans_commit_get_rw_cold(struct btree_trans *trans, unsigned flags)
{
	struct bch_fs *c = trans->c;
	int ret;

	if (likely(!(flags & BTREE_INSERT_LAZY_RW)) ||
	    test_bit(BCH_FS_STARTED, &c->flags))
		return -BCH_ERR_erofs_trans_commit;

	ret = drop_locks_do(trans, bch2_fs_read_write_early(c));
	if (ret)
		return ret;

	bch2_write_ref_get(c, BCH_WRITE_REF_trans);
	return 0;
}

/*
 * This is for updates done in the early part of fsck - btree_gc - before we've
 * gone RW. we only add the new key to the list of keys for journal replay to
 * do.
 */
static noinline int
do_bch2_trans_commit_to_journal_replay(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	int ret = 0;

	trans_for_each_update(trans, i) {
		ret = bch2_journal_key_insert(c, i->btree_id, i->level, i->k);
		if (ret)
			break;
	}

	return ret;
}

int __bch2_trans_commit(struct btree_trans *trans, unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i = NULL;
	struct btree_write_buffered_key *wb;
	unsigned u64s;
	int ret = 0;

	if (!trans->nr_updates &&
	    !trans->nr_wb_updates &&
	    !trans->extra_journal_entries.nr)
		goto out_reset;

	if (flags & BTREE_INSERT_GC_LOCK_HELD)
		lockdep_assert_held(&c->gc_lock);

	ret = bch2_trans_commit_run_triggers(trans);
	if (ret)
		goto out_reset;

	if (unlikely(!test_bit(BCH_FS_MAY_GO_RW, &c->flags))) {
		ret = do_bch2_trans_commit_to_journal_replay(trans);
		goto out_reset;
	}

	if (!(flags & BTREE_INSERT_NOCHECK_RW) &&
	    unlikely(!bch2_write_ref_tryget(c, BCH_WRITE_REF_trans))) {
		ret = bch2_trans_commit_get_rw_cold(trans, flags);
		if (ret)
			goto out_reset;
	}

	if (c->btree_write_buffer.state.nr > c->btree_write_buffer.size / 2 &&
	    mutex_trylock(&c->btree_write_buffer.flush_lock)) {
		bch2_trans_begin(trans);
		bch2_trans_unlock(trans);

		ret = __bch2_btree_write_buffer_flush(trans,
					flags|BTREE_INSERT_NOCHECK_RW, true);
		if (!ret) {
			trace_and_count(c, trans_restart_write_buffer_flush, trans, _THIS_IP_);
			ret = btree_trans_restart(trans, BCH_ERR_transaction_restart_write_buffer_flush);
		}
		goto out;
	}

	EBUG_ON(test_bit(BCH_FS_CLEAN_SHUTDOWN, &c->flags));

	memset(&trans->journal_preres, 0, sizeof(trans->journal_preres));

	trans->journal_u64s		= trans->extra_journal_entries.nr;
	trans->journal_preres_u64s	= 0;

	trans->journal_transaction_names = READ_ONCE(c->opts.journal_transaction_names);

	if (trans->journal_transaction_names)
		trans->journal_u64s += jset_u64s(JSET_ENTRY_LOG_U64s);

	trans_for_each_update(trans, i) {
		EBUG_ON(!i->path->should_be_locked);

		ret = bch2_btree_path_upgrade(trans, i->path, i->level + 1);
		if (unlikely(ret))
			goto out;

		EBUG_ON(!btree_node_intent_locked(i->path, i->level));

		if (i->key_cache_already_flushed)
			continue;

		/* we're going to journal the key being updated: */
		u64s = jset_u64s(i->k->k.u64s);
		if (i->cached &&
		    likely(!(flags & BTREE_INSERT_JOURNAL_REPLAY)))
			trans->journal_preres_u64s += u64s;

		if (i->flags & BTREE_UPDATE_NOJOURNAL)
			continue;

		trans->journal_u64s += u64s;

		/* and we're also going to log the overwrite: */
		if (trans->journal_transaction_names)
			trans->journal_u64s += jset_u64s(i->old_k.u64s);
	}

	trans_for_each_wb_update(trans, wb)
		trans->journal_u64s += jset_u64s(wb->k.k.u64s);

	if (trans->extra_journal_res) {
		ret = bch2_disk_reservation_add(c, trans->disk_res,
				trans->extra_journal_res,
				(flags & BTREE_INSERT_NOFAIL)
				? BCH_DISK_RESERVATION_NOFAIL : 0);
		if (ret)
			goto err;
	}
retry:
	bch2_trans_verify_not_in_restart(trans);
	memset(&trans->journal_res, 0, sizeof(trans->journal_res));

	ret = do_bch2_trans_commit(trans, flags, &i, _RET_IP_);

	/* make sure we didn't drop or screw up locks: */
	bch2_trans_verify_locks(trans);

	if (ret)
		goto err;

	trace_and_count(c, transaction_commit, trans, _RET_IP_);
out:
	bch2_journal_preres_put(&c->journal, &trans->journal_preres);

	if (likely(!(flags & BTREE_INSERT_NOCHECK_RW)))
		bch2_write_ref_put(c, BCH_WRITE_REF_trans);
out_reset:
	bch2_trans_reset_updates(trans);

	return ret;
err:
	ret = bch2_trans_commit_error(trans, flags, i, ret, _RET_IP_);
	if (ret)
		goto out;

	goto retry;
}

static noinline int __check_pos_snapshot_overwritten(struct btree_trans *trans,
					  enum btree_id id,
					  struct bpos pos)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, &iter, id, pos,
			     BTREE_ITER_NOT_EXTENTS|
			     BTREE_ITER_ALL_SNAPSHOTS);
	while (1) {
		k = bch2_btree_iter_prev(&iter);
		ret = bkey_err(k);
		if (ret)
			break;

		if (!k.k)
			break;

		if (!bkey_eq(pos, k.k->p))
			break;

		if (bch2_snapshot_is_ancestor(c, k.k->p.snapshot, pos.snapshot)) {
			ret = 1;
			break;
		}
	}
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static inline int check_pos_snapshot_overwritten(struct btree_trans *trans,
					  enum btree_id id,
					  struct bpos pos)
{
	if (!btree_type_has_snapshots(id) ||
	    bch2_snapshot_is_leaf(trans->c, pos.snapshot))
		return 0;

	return __check_pos_snapshot_overwritten(trans, id, pos);
}

static noinline int extent_front_merge(struct btree_trans *trans,
				       struct btree_iter *iter,
				       struct bkey_s_c k,
				       struct bkey_i **insert,
				       enum btree_update_flags flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_i *update;
	int ret;

	update = bch2_bkey_make_mut_noupdate(trans, k);
	ret = PTR_ERR_OR_ZERO(update);
	if (ret)
		return ret;

	if (!bch2_bkey_merge(c, bkey_i_to_s(update), bkey_i_to_s_c(*insert)))
		return 0;

	ret =   check_pos_snapshot_overwritten(trans, iter->btree_id, k.k->p) ?:
		check_pos_snapshot_overwritten(trans, iter->btree_id, (*insert)->k.p);
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

	ret =   check_pos_snapshot_overwritten(trans, iter->btree_id, insert->k.p) ?:
		check_pos_snapshot_overwritten(trans, iter->btree_id, k.k->p);
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
			   BTREE_ITER_ALL_SNAPSHOTS|
			   BTREE_ITER_NOPRESERVE, k, ret) {
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
	struct btree_iter old_iter, new_iter;
	struct bkey_s_c old_k, new_k;
	snapshot_id_list s;
	struct bkey_i *update;
	int ret;

	if (!bch2_snapshot_has_children(c, old_pos.snapshot))
		return 0;

	darray_init(&s);

	bch2_trans_iter_init(trans, &old_iter, id, old_pos,
			     BTREE_ITER_NOT_EXTENTS|
			     BTREE_ITER_ALL_SNAPSHOTS);
	while ((old_k = bch2_btree_iter_prev(&old_iter)).k &&
	       !(ret = bkey_err(old_k)) &&
	       bkey_eq(old_pos, old_k.k->p)) {
		struct bpos whiteout_pos =
			SPOS(new_pos.inode, new_pos.offset, old_k.k->p.snapshot);;

		if (!bch2_snapshot_is_ancestor(c, old_k.k->p.snapshot, old_pos.snapshot) ||
		    snapshot_list_has_ancestor(c, &s, old_k.k->p.snapshot))
			continue;

		new_k = bch2_bkey_get_iter(trans, &new_iter, id, whiteout_pos,
					   BTREE_ITER_NOT_EXTENTS|
					   BTREE_ITER_INTENT);
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
						BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE);
		}
		bch2_trans_iter_exit(trans, &new_iter);

		ret = snapshot_list_add(c, &s, old_k.k->p.snapshot);
		if (ret)
			break;
	}
	bch2_trans_iter_exit(trans, &old_iter);
	darray_exit(&s);

	return ret;
}

int bch2_trans_update_extent(struct btree_trans *trans,
			     struct btree_iter *orig_iter,
			     struct bkey_i *insert,
			     enum btree_update_flags flags)
{
	struct btree_iter iter;
	struct bpos start = bkey_start_pos(&insert->k);
	struct bkey_i *update;
	struct bkey_s_c k;
	enum btree_id btree_id = orig_iter->btree_id;
	int ret = 0, compressed_sectors;

	bch2_trans_iter_init(trans, &iter, btree_id, start,
			     BTREE_ITER_INTENT|
			     BTREE_ITER_WITH_UPDATES|
			     BTREE_ITER_NOT_EXTENTS);
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
		bool front_split = bkey_lt(bkey_start_pos(k.k), start);
		bool back_split  = bkey_gt(k.k->p, insert->k.p);

		/*
		 * If we're going to be splitting a compressed extent, note it
		 * so that __bch2_trans_commit() can increase our disk
		 * reservation:
		 */
		if (((front_split && back_split) ||
		     ((front_split || back_split) && k.k->p.snapshot != insert->k.p.snapshot)) &&
		    (compressed_sectors = bch2_bkey_sectors_compressed(k)))
			trans->extra_journal_res += compressed_sectors;

		if (front_split) {
			update = bch2_bkey_make_mut_noupdate(trans, k);
			if ((ret = PTR_ERR_OR_ZERO(update)))
				goto err;

			bch2_cut_back(start, update);

			ret =   bch2_insert_snapshot_whiteouts(trans, btree_id,
						k.k->p, update->k.p) ?:
				bch2_btree_insert_nonextent(trans, btree_id, update,
						BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE|flags);
			if (ret)
				goto err;
		}

		if (k.k->p.snapshot != insert->k.p.snapshot &&
		    (front_split || back_split)) {
			update = bch2_bkey_make_mut_noupdate(trans, k);
			if ((ret = PTR_ERR_OR_ZERO(update)))
				goto err;

			bch2_cut_front(start, update);
			bch2_cut_back(insert->k.p, update);

			ret =   bch2_insert_snapshot_whiteouts(trans, btree_id,
						k.k->p, update->k.p) ?:
				bch2_btree_insert_nonextent(trans, btree_id, update,
						  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE|flags);
			if (ret)
				goto err;
		}

		if (bkey_le(k.k->p, insert->k.p)) {
			update = bch2_trans_kmalloc(trans, sizeof(*update));
			if ((ret = PTR_ERR_OR_ZERO(update)))
				goto err;

			bkey_init(&update->k);
			update->k.p = k.k->p;
			update->k.p.snapshot = insert->k.p.snapshot;

			if (insert->k.p.snapshot != k.k->p.snapshot) {
				update->k.type = KEY_TYPE_whiteout;
			} else if (btree_type_has_snapshots(btree_id)) {
				ret = need_whiteout_for_snapshot(trans, btree_id, update->k.p);
				if (ret < 0)
					goto err;
				if (ret)
					update->k.type = KEY_TYPE_whiteout;
			}

			ret = bch2_btree_insert_nonextent(trans, btree_id, update,
						  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE|flags);
			if (ret)
				goto err;
		}

		if (back_split) {
			update = bch2_bkey_make_mut_noupdate(trans, k);
			if ((ret = PTR_ERR_OR_ZERO(update)))
				goto err;

			bch2_cut_front(insert->k.p, update);

			ret = bch2_trans_update_by_path(trans, iter.path, update,
						  BTREE_UPDATE_INTERNAL_SNAPSHOT_NODE|
						  flags);
			if (ret)
				goto err;
			goto out;
		}
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
	if (!bkey_deleted(&insert->k)) {
		/*
		 * Rewinding iterators is expensive: get a new one and the one
		 * that points to the start of insert will be cloned from:
		 */
		bch2_trans_iter_exit(trans, &iter);
		bch2_trans_iter_init(trans, &iter, btree_id, insert->k.p,
				     BTREE_ITER_NOT_EXTENTS|
				     BTREE_ITER_INTENT);
		ret   = bch2_btree_iter_traverse(&iter) ?:
			bch2_trans_update(trans, &iter, insert, flags);
	}
err:
	bch2_trans_iter_exit(trans, &iter);

	return ret;
}

static int __must_check
bch2_trans_update_by_path_trace(struct btree_trans *trans, struct btree_path *path,
				struct bkey_i *k, enum btree_update_flags flags,
				unsigned long ip);

static noinline int flush_new_cached_update(struct btree_trans *trans,
					    struct btree_path *path,
					    struct btree_insert_entry *i,
					    enum btree_update_flags flags,
					    unsigned long ip)
{
	struct btree_path *btree_path;
	struct bkey k;
	int ret;

	btree_path = bch2_path_get(trans, path->btree_id, path->pos, 1, 0,
				   BTREE_ITER_INTENT, _THIS_IP_);
	ret = bch2_btree_path_traverse(trans, btree_path, 0);
	if (ret)
		goto out;

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
	i->flags |= BTREE_TRIGGER_NORUN;

	btree_path_set_should_be_locked(btree_path);
	ret = bch2_trans_update_by_path_trace(trans, btree_path, i->k, flags, ip);
out:
	bch2_path_put(trans, btree_path, true);
	return ret;
}

static int __must_check
bch2_trans_update_by_path_trace(struct btree_trans *trans, struct btree_path *path,
				struct bkey_i *k, enum btree_update_flags flags,
				unsigned long ip)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i, n;
	int cmp;

	EBUG_ON(!path->should_be_locked);
	EBUG_ON(trans->nr_updates >= BTREE_ITER_MAX);
	EBUG_ON(!bpos_eq(k->k.p, path->pos));

	n = (struct btree_insert_entry) {
		.flags		= flags,
		.bkey_type	= __btree_node_type(path->level, path->btree_id),
		.btree_id	= path->btree_id,
		.level		= path->level,
		.cached		= path->cached,
		.path		= path,
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
	trans_for_each_update(trans, i) {
		cmp = btree_insert_entry_cmp(&n, i);
		if (cmp <= 0)
			break;
	}

	if (!cmp && i < trans->updates + trans->nr_updates) {
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

	__btree_path_get(i->path, true);

	/*
	 * If a key is present in the key cache, it must also exist in the
	 * btree - this is necessary for cache coherency. When iterating over
	 * a btree that's cached in the key cache, the btree iter code checks
	 * the key cache - but the key has to exist in the btree for that to
	 * work:
	 */
	if (path->cached && bkey_deleted(&i->old_k))
		return flush_new_cached_update(trans, path, i, flags, ip);

	return 0;
}

static inline int __must_check
bch2_trans_update_by_path(struct btree_trans *trans, struct btree_path *path,
			  struct bkey_i *k, enum btree_update_flags flags)
{
	return bch2_trans_update_by_path_trace(trans, path, k, flags, _RET_IP_);
}

int __must_check bch2_trans_update(struct btree_trans *trans, struct btree_iter *iter,
				   struct bkey_i *k, enum btree_update_flags flags)
{
	struct btree_path *path = iter->update_path ?: iter->path;
	struct bkey_cached *ck;
	int ret;

	if (iter->flags & BTREE_ITER_IS_EXTENTS)
		return bch2_trans_update_extent(trans, iter, k, flags);

	if (bkey_deleted(&k->k) &&
	    !(flags & BTREE_UPDATE_KEY_CACHE_RECLAIM) &&
	    (iter->flags & BTREE_ITER_FILTER_SNAPSHOTS)) {
		ret = need_whiteout_for_snapshot(trans, iter->btree_id, k->k.p);
		if (unlikely(ret < 0))
			return ret;

		if (ret)
			k->k.type = KEY_TYPE_whiteout;
	}

	/*
	 * Ensure that updates to cached btrees go to the key cache:
	 */
	if (!(flags & BTREE_UPDATE_KEY_CACHE_RECLAIM) &&
	    !path->cached &&
	    !path->level &&
	    btree_id_cached(trans->c, path->btree_id)) {
		if (!iter->key_cache_path ||
		    !iter->key_cache_path->should_be_locked ||
		    !bpos_eq(iter->key_cache_path->pos, k->k.p)) {
			if (!iter->key_cache_path)
				iter->key_cache_path =
					bch2_path_get(trans, path->btree_id, path->pos, 1, 0,
						      BTREE_ITER_INTENT|
						      BTREE_ITER_CACHED, _THIS_IP_);

			iter->key_cache_path =
				bch2_btree_path_set_pos(trans, iter->key_cache_path, path->pos,
							iter->flags & BTREE_ITER_INTENT,
							_THIS_IP_);

			ret = bch2_btree_path_traverse(trans, iter->key_cache_path,
						       BTREE_ITER_CACHED);
			if (unlikely(ret))
				return ret;

			ck = (void *) iter->key_cache_path->l[0].b;

			if (test_bit(BKEY_CACHED_DIRTY, &ck->flags)) {
				trace_and_count(trans->c, trans_restart_key_cache_raced, trans, _RET_IP_);
				return btree_trans_restart(trans, BCH_ERR_transaction_restart_key_cache_raced);
			}

			btree_path_set_should_be_locked(iter->key_cache_path);
		}

		path = iter->key_cache_path;
	}

	return bch2_trans_update_by_path(trans, path, k, flags);
}

int __must_check bch2_trans_update_buffered(struct btree_trans *trans,
					    enum btree_id btree,
					    struct bkey_i *k)
{
	struct btree_write_buffered_key *i;
	int ret;

	EBUG_ON(trans->nr_wb_updates > trans->wb_updates_size);
	EBUG_ON(k->k.u64s > BTREE_WRITE_BUFERED_U64s_MAX);

	trans_for_each_wb_update(trans, i) {
		if (i->btree == btree && bpos_eq(i->k.k.p, k->k.p)) {
			bkey_copy(&i->k, k);
			return 0;
		}
	}

	if (!trans->wb_updates ||
	    trans->nr_wb_updates == trans->wb_updates_size) {
		struct btree_write_buffered_key *u;

		if (trans->nr_wb_updates == trans->wb_updates_size) {
			struct btree_transaction_stats *s = btree_trans_stats(trans);

			BUG_ON(trans->wb_updates_size > U8_MAX / 2);
			trans->wb_updates_size = max(1, trans->wb_updates_size * 2);
			if (s)
				s->wb_updates_size = trans->wb_updates_size;
		}

		u = bch2_trans_kmalloc_nomemzero(trans,
					trans->wb_updates_size *
					sizeof(struct btree_write_buffered_key));
		ret = PTR_ERR_OR_ZERO(u);
		if (ret)
			return ret;

		if (trans->nr_wb_updates)
			memcpy(u, trans->wb_updates, trans->nr_wb_updates *
			       sizeof(struct btree_write_buffered_key));
		trans->wb_updates = u;
	}

	trans->wb_updates[trans->nr_wb_updates] = (struct btree_write_buffered_key) {
		.btree	= btree,
	};

	bkey_copy(&trans->wb_updates[trans->nr_wb_updates].k, k);
	trans->nr_wb_updates++;

	return 0;
}

int bch2_bkey_get_empty_slot(struct btree_trans *trans, struct btree_iter *iter,
			     enum btree_id btree, struct bpos end)
{
	struct bkey_s_c k;
	int ret = 0;

	bch2_trans_iter_init(trans, iter, btree, POS_MAX, BTREE_ITER_INTENT);
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
				enum btree_update_flags flags)
{
	struct btree_iter iter;
	int ret;

	bch2_trans_iter_init(trans, &iter, btree, k->k.p,
			     BTREE_ITER_NOT_EXTENTS|
			     BTREE_ITER_INTENT);
	ret   = bch2_btree_iter_traverse(&iter) ?:
		bch2_trans_update(trans, &iter, k, flags);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int __bch2_btree_insert(struct btree_trans *trans, enum btree_id id,
			struct bkey_i *k, enum btree_update_flags flags)
{
	struct btree_iter iter;
	int ret;

	bch2_trans_iter_init(trans, &iter, id, bkey_start_pos(&k->k),
			     BTREE_ITER_CACHED|
			     BTREE_ITER_INTENT);
	ret   = bch2_btree_iter_traverse(&iter) ?:
		bch2_trans_update(trans, &iter, k, flags);
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

/**
 * bch2_btree_insert - insert keys into the extent btree
 * @c:			pointer to struct bch_fs
 * @id:			btree to insert into
 * @insert_keys:	list of keys to insert
 * @hook:		insert callback
 */
int bch2_btree_insert(struct bch_fs *c, enum btree_id id,
		      struct bkey_i *k,
		      struct disk_reservation *disk_res,
		      u64 *journal_seq, int flags)
{
	return bch2_trans_do(c, disk_res, journal_seq, flags,
			     __bch2_btree_insert(&trans, id, k, 0));
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

int bch2_btree_delete_at_buffered(struct btree_trans *trans,
				  enum btree_id btree, struct bpos pos)
{
	struct bkey_i *k;

	k = bch2_trans_kmalloc(trans, sizeof(*k));
	if (IS_ERR(k))
		return PTR_ERR(k);

	bkey_init(&k->k);
	k->k.p = pos;
	return bch2_trans_update_buffered(trans, btree, k);
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

	bch2_trans_iter_init(trans, &iter, id, start, BTREE_ITER_INTENT);
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

		if (iter.flags & BTREE_ITER_IS_EXTENTS)
			bch2_key_resize(&delete.k,
					bpos_min(end, k.k->p).offset -
					iter.pos.offset);

		ret   = bch2_trans_update(trans, &iter, &delete, update_flags) ?:
			bch2_trans_commit(trans, &disk_res, journal_seq,
					  BTREE_INSERT_NOFAIL);
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

	if (!ret && trans_was_restarted(trans, restart_count))
		ret = -BCH_ERR_transaction_restart_nested;
	return ret;
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
			bch2_btree_delete_range_trans(&trans, id, start, end,
						      update_flags, journal_seq));
	if (ret == -BCH_ERR_transaction_restart_nested)
		ret = 0;
	return ret;
}

static int __bch2_trans_log_msg(darray_u64 *entries, const char *fmt, va_list args)
{
	struct printbuf buf = PRINTBUF;
	struct jset_entry_log *l;
	unsigned u64s;
	int ret;

	prt_vprintf(&buf, fmt, args);
	ret = buf.allocation_failure ? -BCH_ERR_ENOMEM_trans_log_msg : 0;
	if (ret)
		goto err;

	u64s = DIV_ROUND_UP(buf.pos, sizeof(u64));

	ret = darray_make_room(entries, jset_u64s(u64s));
	if (ret)
		goto err;

	l = (void *) &darray_top(*entries);
	l->entry.u64s		= cpu_to_le16(u64s);
	l->entry.btree_id	= 0;
	l->entry.level		= 1;
	l->entry.type		= BCH_JSET_ENTRY_log;
	l->entry.pad[0]		= 0;
	l->entry.pad[1]		= 0;
	l->entry.pad[2]		= 0;
	memcpy(l->d, buf.buf, buf.pos);
	while (buf.pos & 7)
		l->d[buf.pos++] = '\0';

	entries->nr += jset_u64s(u64s);
err:
	printbuf_exit(&buf);
	return ret;
}

static int
__bch2_fs_log_msg(struct bch_fs *c, unsigned commit_flags, const char *fmt,
		  va_list args)
{
	int ret;

	if (!test_bit(JOURNAL_STARTED, &c->journal.flags)) {
		ret = __bch2_trans_log_msg(&c->journal.early_journal_entries, fmt, args);
	} else {
		ret = bch2_trans_do(c, NULL, NULL,
			BTREE_INSERT_LAZY_RW|commit_flags,
			__bch2_trans_log_msg(&trans.extra_journal_entries, fmt, args));
	}

	return ret;
}

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
int bch2_journal_log_msg(struct bch_fs *c, const char *fmt, ...)
{
	va_list args;
	int ret;

	va_start(args, fmt);
	ret = __bch2_fs_log_msg(c, BCH_WATERMARK_reclaim, fmt, args);
	va_end(args);
	return ret;
}
