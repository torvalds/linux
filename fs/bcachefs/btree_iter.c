// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_methods.h"
#include "bkey_buf.h"
#include "btree_cache.h"
#include "btree_iter.h"
#include "btree_journal_iter.h"
#include "btree_key_cache.h"
#include "btree_locking.h"
#include "btree_update.h"
#include "debug.h"
#include "error.h"
#include "extents.h"
#include "journal.h"
#include "journal_io.h"
#include "replicas.h"
#include "snapshot.h"
#include "trace.h"

#include <linux/random.h>
#include <linux/prefetch.h>

static inline void btree_path_list_remove(struct btree_trans *, struct btree_path *);
static inline void btree_path_list_add(struct btree_trans *,
			btree_path_idx_t, btree_path_idx_t);

static inline unsigned long btree_iter_ip_allocated(struct btree_iter *iter)
{
#ifdef TRACK_PATH_ALLOCATED
	return iter->ip_allocated;
#else
	return 0;
#endif
}

static btree_path_idx_t btree_path_alloc(struct btree_trans *, btree_path_idx_t);
static void bch2_trans_srcu_lock(struct btree_trans *);

static inline int __btree_path_cmp(const struct btree_path *l,
				   enum btree_id	r_btree_id,
				   bool			r_cached,
				   struct bpos		r_pos,
				   unsigned		r_level)
{
	/*
	 * Must match lock ordering as defined by __bch2_btree_node_lock:
	 */
	return   cmp_int(l->btree_id,	r_btree_id) ?:
		 cmp_int((int) l->cached,	(int) r_cached) ?:
		 bpos_cmp(l->pos,	r_pos) ?:
		-cmp_int(l->level,	r_level);
}

static inline int btree_path_cmp(const struct btree_path *l,
				 const struct btree_path *r)
{
	return __btree_path_cmp(l, r->btree_id, r->cached, r->pos, r->level);
}

static inline struct bpos bkey_successor(struct btree_iter *iter, struct bpos p)
{
	/* Are we iterating over keys in all snapshots? */
	if (iter->flags & BTREE_ITER_all_snapshots) {
		p = bpos_successor(p);
	} else {
		p = bpos_nosnap_successor(p);
		p.snapshot = iter->snapshot;
	}

	return p;
}

static inline struct bpos bkey_predecessor(struct btree_iter *iter, struct bpos p)
{
	/* Are we iterating over keys in all snapshots? */
	if (iter->flags & BTREE_ITER_all_snapshots) {
		p = bpos_predecessor(p);
	} else {
		p = bpos_nosnap_predecessor(p);
		p.snapshot = iter->snapshot;
	}

	return p;
}

static inline struct bpos btree_iter_search_key(struct btree_iter *iter)
{
	struct bpos pos = iter->pos;

	if ((iter->flags & BTREE_ITER_is_extents) &&
	    !bkey_eq(pos, POS_MAX))
		pos = bkey_successor(iter, pos);
	return pos;
}

static inline bool btree_path_pos_before_node(struct btree_path *path,
					      struct btree *b)
{
	return bpos_lt(path->pos, b->data->min_key);
}

static inline bool btree_path_pos_after_node(struct btree_path *path,
					     struct btree *b)
{
	return bpos_gt(path->pos, b->key.k.p);
}

static inline bool btree_path_pos_in_node(struct btree_path *path,
					  struct btree *b)
{
	return path->btree_id == b->c.btree_id &&
		!btree_path_pos_before_node(path, b) &&
		!btree_path_pos_after_node(path, b);
}

/* Btree iterator: */

#ifdef CONFIG_BCACHEFS_DEBUG

static void bch2_btree_path_verify_cached(struct btree_trans *trans,
					  struct btree_path *path)
{
	struct bkey_cached *ck;
	bool locked = btree_node_locked(path, 0);

	if (!bch2_btree_node_relock(trans, path, 0))
		return;

	ck = (void *) path->l[0].b;
	BUG_ON(ck->key.btree_id != path->btree_id ||
	       !bkey_eq(ck->key.pos, path->pos));

	if (!locked)
		btree_node_unlock(trans, path, 0);
}

static void bch2_btree_path_verify_level(struct btree_trans *trans,
				struct btree_path *path, unsigned level)
{
	struct btree_path_level *l;
	struct btree_node_iter tmp;
	bool locked;
	struct bkey_packed *p, *k;
	struct printbuf buf1 = PRINTBUF;
	struct printbuf buf2 = PRINTBUF;
	struct printbuf buf3 = PRINTBUF;
	const char *msg;

	if (!bch2_debug_check_iterators)
		return;

	l	= &path->l[level];
	tmp	= l->iter;
	locked	= btree_node_locked(path, level);

	if (path->cached) {
		if (!level)
			bch2_btree_path_verify_cached(trans, path);
		return;
	}

	if (!btree_path_node(path, level))
		return;

	if (!bch2_btree_node_relock_notrace(trans, path, level))
		return;

	BUG_ON(!btree_path_pos_in_node(path, l->b));

	bch2_btree_node_iter_verify(&l->iter, l->b);

	/*
	 * For interior nodes, the iterator will have skipped past deleted keys:
	 */
	p = level
		? bch2_btree_node_iter_prev(&tmp, l->b)
		: bch2_btree_node_iter_prev_all(&tmp, l->b);
	k = bch2_btree_node_iter_peek_all(&l->iter, l->b);

	if (p && bkey_iter_pos_cmp(l->b, p, &path->pos) >= 0) {
		msg = "before";
		goto err;
	}

	if (k && bkey_iter_pos_cmp(l->b, k, &path->pos) < 0) {
		msg = "after";
		goto err;
	}

	if (!locked)
		btree_node_unlock(trans, path, level);
	return;
err:
	bch2_bpos_to_text(&buf1, path->pos);

	if (p) {
		struct bkey uk = bkey_unpack_key(l->b, p);

		bch2_bkey_to_text(&buf2, &uk);
	} else {
		prt_printf(&buf2, "(none)");
	}

	if (k) {
		struct bkey uk = bkey_unpack_key(l->b, k);

		bch2_bkey_to_text(&buf3, &uk);
	} else {
		prt_printf(&buf3, "(none)");
	}

	panic("path should be %s key at level %u:\n"
	      "path pos %s\n"
	      "prev key %s\n"
	      "cur  key %s\n",
	      msg, level, buf1.buf, buf2.buf, buf3.buf);
}

static void bch2_btree_path_verify(struct btree_trans *trans,
				   struct btree_path *path)
{
	struct bch_fs *c = trans->c;

	for (unsigned i = 0; i < (!path->cached ? BTREE_MAX_DEPTH : 1); i++) {
		if (!path->l[i].b) {
			BUG_ON(!path->cached &&
			       bch2_btree_id_root(c, path->btree_id)->b->c.level > i);
			break;
		}

		bch2_btree_path_verify_level(trans, path, i);
	}

	bch2_btree_path_verify_locks(path);
}

void bch2_trans_verify_paths(struct btree_trans *trans)
{
	struct btree_path *path;
	unsigned iter;

	trans_for_each_path(trans, path, iter)
		bch2_btree_path_verify(trans, path);
}

static void bch2_btree_iter_verify(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;

	BUG_ON(!!(iter->flags & BTREE_ITER_cached) != btree_iter_path(trans, iter)->cached);

	BUG_ON((iter->flags & BTREE_ITER_is_extents) &&
	       (iter->flags & BTREE_ITER_all_snapshots));

	BUG_ON(!(iter->flags & BTREE_ITER_snapshot_field) &&
	       (iter->flags & BTREE_ITER_all_snapshots) &&
	       !btree_type_has_snapshot_field(iter->btree_id));

	if (iter->update_path)
		bch2_btree_path_verify(trans, &trans->paths[iter->update_path]);
	bch2_btree_path_verify(trans, btree_iter_path(trans, iter));
}

static void bch2_btree_iter_verify_entry_exit(struct btree_iter *iter)
{
	BUG_ON((iter->flags & BTREE_ITER_filter_snapshots) &&
	       !iter->pos.snapshot);

	BUG_ON(!(iter->flags & BTREE_ITER_all_snapshots) &&
	       iter->pos.snapshot != iter->snapshot);

	BUG_ON(bkey_lt(iter->pos, bkey_start_pos(&iter->k)) ||
	       bkey_gt(iter->pos, iter->k.p));
}

static int bch2_btree_iter_verify_ret(struct btree_iter *iter, struct bkey_s_c k)
{
	struct btree_trans *trans = iter->trans;
	struct btree_iter copy;
	struct bkey_s_c prev;
	int ret = 0;

	if (!bch2_debug_check_iterators)
		return 0;

	if (!(iter->flags & BTREE_ITER_filter_snapshots))
		return 0;

	if (bkey_err(k) || !k.k)
		return 0;

	BUG_ON(!bch2_snapshot_is_ancestor(trans->c,
					  iter->snapshot,
					  k.k->p.snapshot));

	bch2_trans_iter_init(trans, &copy, iter->btree_id, iter->pos,
			     BTREE_ITER_nopreserve|
			     BTREE_ITER_all_snapshots);
	prev = bch2_btree_iter_prev(&copy);
	if (!prev.k)
		goto out;

	ret = bkey_err(prev);
	if (ret)
		goto out;

	if (bkey_eq(prev.k->p, k.k->p) &&
	    bch2_snapshot_is_ancestor(trans->c, iter->snapshot,
				      prev.k->p.snapshot) > 0) {
		struct printbuf buf1 = PRINTBUF, buf2 = PRINTBUF;

		bch2_bkey_to_text(&buf1, k.k);
		bch2_bkey_to_text(&buf2, prev.k);

		panic("iter snap %u\n"
		      "k    %s\n"
		      "prev %s\n",
		      iter->snapshot,
		      buf1.buf, buf2.buf);
	}
out:
	bch2_trans_iter_exit(trans, &copy);
	return ret;
}

void bch2_assert_pos_locked(struct btree_trans *trans, enum btree_id id,
			    struct bpos pos)
{
	bch2_trans_verify_not_unlocked(trans);

	struct btree_path *path;
	struct trans_for_each_path_inorder_iter iter;
	struct printbuf buf = PRINTBUF;

	btree_trans_sort_paths(trans);

	trans_for_each_path_inorder(trans, path, iter) {
		if (path->btree_id != id ||
		    !btree_node_locked(path, 0) ||
		    !path->should_be_locked)
			continue;

		if (!path->cached) {
			if (bkey_ge(pos, path->l[0].b->data->min_key) &&
			    bkey_le(pos, path->l[0].b->key.k.p))
				return;
		} else {
			if (bkey_eq(pos, path->pos))
				return;
		}
	}

	bch2_dump_trans_paths_updates(trans);
	bch2_bpos_to_text(&buf, pos);

	panic("not locked: %s %s\n", bch2_btree_id_str(id), buf.buf);
}

#else

static inline void bch2_btree_path_verify_level(struct btree_trans *trans,
						struct btree_path *path, unsigned l) {}
static inline void bch2_btree_path_verify(struct btree_trans *trans,
					  struct btree_path *path) {}
static inline void bch2_btree_iter_verify(struct btree_iter *iter) {}
static inline void bch2_btree_iter_verify_entry_exit(struct btree_iter *iter) {}
static inline int bch2_btree_iter_verify_ret(struct btree_iter *iter, struct bkey_s_c k) { return 0; }

#endif

/* Btree path: fixups after btree updates */

static void btree_node_iter_set_set_pos(struct btree_node_iter *iter,
					struct btree *b,
					struct bset_tree *t,
					struct bkey_packed *k)
{
	struct btree_node_iter_set *set;

	btree_node_iter_for_each(iter, set)
		if (set->end == t->end_offset) {
			set->k = __btree_node_key_to_offset(b, k);
			bch2_btree_node_iter_sort(iter, b);
			return;
		}

	bch2_btree_node_iter_push(iter, b, k, btree_bkey_last(b, t));
}

static void __bch2_btree_path_fix_key_modified(struct btree_path *path,
					       struct btree *b,
					       struct bkey_packed *where)
{
	struct btree_path_level *l = &path->l[b->c.level];

	if (where != bch2_btree_node_iter_peek_all(&l->iter, l->b))
		return;

	if (bkey_iter_pos_cmp(l->b, where, &path->pos) < 0)
		bch2_btree_node_iter_advance(&l->iter, l->b);
}

void bch2_btree_path_fix_key_modified(struct btree_trans *trans,
				      struct btree *b,
				      struct bkey_packed *where)
{
	struct btree_path *path;
	unsigned i;

	trans_for_each_path_with_node(trans, b, path, i) {
		__bch2_btree_path_fix_key_modified(path, b, where);
		bch2_btree_path_verify_level(trans, path, b->c.level);
	}
}

static void __bch2_btree_node_iter_fix(struct btree_path *path,
				       struct btree *b,
				       struct btree_node_iter *node_iter,
				       struct bset_tree *t,
				       struct bkey_packed *where,
				       unsigned clobber_u64s,
				       unsigned new_u64s)
{
	const struct bkey_packed *end = btree_bkey_last(b, t);
	struct btree_node_iter_set *set;
	unsigned offset = __btree_node_key_to_offset(b, where);
	int shift = new_u64s - clobber_u64s;
	unsigned old_end = t->end_offset - shift;
	unsigned orig_iter_pos = node_iter->data[0].k;
	bool iter_current_key_modified =
		orig_iter_pos >= offset &&
		orig_iter_pos <= offset + clobber_u64s;

	btree_node_iter_for_each(node_iter, set)
		if (set->end == old_end)
			goto found;

	/* didn't find the bset in the iterator - might have to readd it: */
	if (new_u64s &&
	    bkey_iter_pos_cmp(b, where, &path->pos) >= 0) {
		bch2_btree_node_iter_push(node_iter, b, where, end);
		goto fixup_done;
	} else {
		/* Iterator is after key that changed */
		return;
	}
found:
	set->end = t->end_offset;

	/* Iterator hasn't gotten to the key that changed yet: */
	if (set->k < offset)
		return;

	if (new_u64s &&
	    bkey_iter_pos_cmp(b, where, &path->pos) >= 0) {
		set->k = offset;
	} else if (set->k < offset + clobber_u64s) {
		set->k = offset + new_u64s;
		if (set->k == set->end)
			bch2_btree_node_iter_set_drop(node_iter, set);
	} else {
		/* Iterator is after key that changed */
		set->k = (int) set->k + shift;
		return;
	}

	bch2_btree_node_iter_sort(node_iter, b);
fixup_done:
	if (node_iter->data[0].k != orig_iter_pos)
		iter_current_key_modified = true;

	/*
	 * When a new key is added, and the node iterator now points to that
	 * key, the iterator might have skipped past deleted keys that should
	 * come after the key the iterator now points to. We have to rewind to
	 * before those deleted keys - otherwise
	 * bch2_btree_node_iter_prev_all() breaks:
	 */
	if (!bch2_btree_node_iter_end(node_iter) &&
	    iter_current_key_modified &&
	    b->c.level) {
		struct bkey_packed *k, *k2, *p;

		k = bch2_btree_node_iter_peek_all(node_iter, b);

		for_each_bset(b, t) {
			bool set_pos = false;

			if (node_iter->data[0].end == t->end_offset)
				continue;

			k2 = bch2_btree_node_iter_bset_pos(node_iter, b, t);

			while ((p = bch2_bkey_prev_all(b, t, k2)) &&
			       bkey_iter_cmp(b, k, p) < 0) {
				k2 = p;
				set_pos = true;
			}

			if (set_pos)
				btree_node_iter_set_set_pos(node_iter,
							    b, t, k2);
		}
	}
}

void bch2_btree_node_iter_fix(struct btree_trans *trans,
			      struct btree_path *path,
			      struct btree *b,
			      struct btree_node_iter *node_iter,
			      struct bkey_packed *where,
			      unsigned clobber_u64s,
			      unsigned new_u64s)
{
	struct bset_tree *t = bch2_bkey_to_bset_inlined(b, where);
	struct btree_path *linked;
	unsigned i;

	if (node_iter != &path->l[b->c.level].iter) {
		__bch2_btree_node_iter_fix(path, b, node_iter, t,
					   where, clobber_u64s, new_u64s);

		if (bch2_debug_check_iterators)
			bch2_btree_node_iter_verify(node_iter, b);
	}

	trans_for_each_path_with_node(trans, b, linked, i) {
		__bch2_btree_node_iter_fix(linked, b,
					   &linked->l[b->c.level].iter, t,
					   where, clobber_u64s, new_u64s);
		bch2_btree_path_verify_level(trans, linked, b->c.level);
	}
}

/* Btree path level: pointer to a particular btree node and node iter */

static inline struct bkey_s_c __btree_iter_unpack(struct bch_fs *c,
						  struct btree_path_level *l,
						  struct bkey *u,
						  struct bkey_packed *k)
{
	if (unlikely(!k)) {
		/*
		 * signal to bch2_btree_iter_peek_slot() that we're currently at
		 * a hole
		 */
		u->type = KEY_TYPE_deleted;
		return bkey_s_c_null;
	}

	return bkey_disassemble(l->b, k, u);
}

static inline struct bkey_s_c btree_path_level_peek_all(struct bch_fs *c,
							struct btree_path_level *l,
							struct bkey *u)
{
	return __btree_iter_unpack(c, l, u,
			bch2_btree_node_iter_peek_all(&l->iter, l->b));
}

static inline struct bkey_s_c btree_path_level_peek(struct btree_trans *trans,
						    struct btree_path *path,
						    struct btree_path_level *l,
						    struct bkey *u)
{
	struct bkey_s_c k = __btree_iter_unpack(trans->c, l, u,
			bch2_btree_node_iter_peek(&l->iter, l->b));

	path->pos = k.k ? k.k->p : l->b->key.k.p;
	trans->paths_sorted = false;
	bch2_btree_path_verify_level(trans, path, l - path->l);
	return k;
}

static inline struct bkey_s_c btree_path_level_prev(struct btree_trans *trans,
						    struct btree_path *path,
						    struct btree_path_level *l,
						    struct bkey *u)
{
	struct bkey_s_c k = __btree_iter_unpack(trans->c, l, u,
			bch2_btree_node_iter_prev(&l->iter, l->b));

	path->pos = k.k ? k.k->p : l->b->data->min_key;
	trans->paths_sorted = false;
	bch2_btree_path_verify_level(trans, path, l - path->l);
	return k;
}

static inline bool btree_path_advance_to_pos(struct btree_path *path,
					     struct btree_path_level *l,
					     int max_advance)
{
	struct bkey_packed *k;
	int nr_advanced = 0;

	while ((k = bch2_btree_node_iter_peek_all(&l->iter, l->b)) &&
	       bkey_iter_pos_cmp(l->b, k, &path->pos) < 0) {
		if (max_advance > 0 && nr_advanced >= max_advance)
			return false;

		bch2_btree_node_iter_advance(&l->iter, l->b);
		nr_advanced++;
	}

	return true;
}

static inline void __btree_path_level_init(struct btree_path *path,
					   unsigned level)
{
	struct btree_path_level *l = &path->l[level];

	bch2_btree_node_iter_init(&l->iter, l->b, &path->pos);

	/*
	 * Iterators to interior nodes should always be pointed at the first non
	 * whiteout:
	 */
	if (level)
		bch2_btree_node_iter_peek(&l->iter, l->b);
}

void bch2_btree_path_level_init(struct btree_trans *trans,
				struct btree_path *path,
				struct btree *b)
{
	BUG_ON(path->cached);

	EBUG_ON(!btree_path_pos_in_node(path, b));

	path->l[b->c.level].lock_seq = six_lock_seq(&b->c.lock);
	path->l[b->c.level].b = b;
	__btree_path_level_init(path, b->c.level);
}

/* Btree path: fixups after btree node updates: */

static void bch2_trans_revalidate_updates_in_node(struct btree_trans *trans, struct btree *b)
{
	struct bch_fs *c = trans->c;

	trans_for_each_update(trans, i)
		if (!i->cached &&
		    i->level	== b->c.level &&
		    i->btree_id	== b->c.btree_id &&
		    bpos_cmp(i->k->k.p, b->data->min_key) >= 0 &&
		    bpos_cmp(i->k->k.p, b->data->max_key) <= 0) {
			i->old_v = bch2_btree_path_peek_slot(trans->paths + i->path, &i->old_k).v;

			if (unlikely(trans->journal_replay_not_finished)) {
				struct bkey_i *j_k =
					bch2_journal_keys_peek_slot(c, i->btree_id, i->level,
								    i->k->k.p);

				if (j_k) {
					i->old_k = j_k->k;
					i->old_v = &j_k->v;
				}
			}
		}
}

/*
 * A btree node is being replaced - update the iterator to point to the new
 * node:
 */
void bch2_trans_node_add(struct btree_trans *trans,
			 struct btree_path *path,
			 struct btree *b)
{
	struct btree_path *prev;

	BUG_ON(!btree_path_pos_in_node(path, b));

	while ((prev = prev_btree_path(trans, path)) &&
	       btree_path_pos_in_node(prev, b))
		path = prev;

	for (;
	     path && btree_path_pos_in_node(path, b);
	     path = next_btree_path(trans, path))
		if (path->uptodate == BTREE_ITER_UPTODATE && !path->cached) {
			enum btree_node_locked_type t =
				btree_lock_want(path, b->c.level);

			if (t != BTREE_NODE_UNLOCKED) {
				btree_node_unlock(trans, path, b->c.level);
				six_lock_increment(&b->c.lock, (enum six_lock_type) t);
				mark_btree_node_locked(trans, path, b->c.level, t);
			}

			bch2_btree_path_level_init(trans, path, b);
		}

	bch2_trans_revalidate_updates_in_node(trans, b);
}

/*
 * A btree node has been modified in such a way as to invalidate iterators - fix
 * them:
 */
void bch2_trans_node_reinit_iter(struct btree_trans *trans, struct btree *b)
{
	struct btree_path *path;
	unsigned i;

	trans_for_each_path_with_node(trans, b, path, i)
		__btree_path_level_init(path, b->c.level);

	bch2_trans_revalidate_updates_in_node(trans, b);
}

/* Btree path: traverse, set_pos: */

static inline int btree_path_lock_root(struct btree_trans *trans,
				       struct btree_path *path,
				       unsigned depth_want,
				       unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree *b, **rootp = &bch2_btree_id_root(c, path->btree_id)->b;
	enum six_lock_type lock_type;
	unsigned i;
	int ret;

	EBUG_ON(path->nodes_locked);

	while (1) {
		b = READ_ONCE(*rootp);
		path->level = READ_ONCE(b->c.level);

		if (unlikely(path->level < depth_want)) {
			/*
			 * the root is at a lower depth than the depth we want:
			 * got to the end of the btree, or we're walking nodes
			 * greater than some depth and there are no nodes >=
			 * that depth
			 */
			path->level = depth_want;
			for (i = path->level; i < BTREE_MAX_DEPTH; i++)
				path->l[i].b = NULL;
			return 1;
		}

		lock_type = __btree_lock_want(path, path->level);
		ret = btree_node_lock(trans, path, &b->c,
				      path->level, lock_type, trace_ip);
		if (unlikely(ret)) {
			if (bch2_err_matches(ret, BCH_ERR_lock_fail_root_changed))
				continue;
			if (bch2_err_matches(ret, BCH_ERR_transaction_restart))
				return ret;
			BUG();
		}

		if (likely(b == READ_ONCE(*rootp) &&
			   b->c.level == path->level &&
			   !race_fault())) {
			for (i = 0; i < path->level; i++)
				path->l[i].b = ERR_PTR(-BCH_ERR_no_btree_node_lock_root);
			path->l[path->level].b = b;
			for (i = path->level + 1; i < BTREE_MAX_DEPTH; i++)
				path->l[i].b = NULL;

			mark_btree_node_locked(trans, path, path->level,
					       (enum btree_node_locked_type) lock_type);
			bch2_btree_path_level_init(trans, path, b);
			return 0;
		}

		six_unlock_type(&b->c.lock, lock_type);
	}
}

noinline
static int btree_path_prefetch(struct btree_trans *trans, struct btree_path *path)
{
	struct bch_fs *c = trans->c;
	struct btree_path_level *l = path_l(path);
	struct btree_node_iter node_iter = l->iter;
	struct bkey_packed *k;
	struct bkey_buf tmp;
	unsigned nr = test_bit(BCH_FS_started, &c->flags)
		? (path->level > 1 ? 0 :  2)
		: (path->level > 1 ? 1 : 16);
	bool was_locked = btree_node_locked(path, path->level);
	int ret = 0;

	bch2_bkey_buf_init(&tmp);

	while (nr-- && !ret) {
		if (!bch2_btree_node_relock(trans, path, path->level))
			break;

		bch2_btree_node_iter_advance(&node_iter, l->b);
		k = bch2_btree_node_iter_peek(&node_iter, l->b);
		if (!k)
			break;

		bch2_bkey_buf_unpack(&tmp, c, l->b, k);
		ret = bch2_btree_node_prefetch(trans, path, tmp.k, path->btree_id,
					       path->level - 1);
	}

	if (!was_locked)
		btree_node_unlock(trans, path, path->level);

	bch2_bkey_buf_exit(&tmp, c);
	return ret;
}

static int btree_path_prefetch_j(struct btree_trans *trans, struct btree_path *path,
				 struct btree_and_journal_iter *jiter)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k;
	struct bkey_buf tmp;
	unsigned nr = test_bit(BCH_FS_started, &c->flags)
		? (path->level > 1 ? 0 :  2)
		: (path->level > 1 ? 1 : 16);
	bool was_locked = btree_node_locked(path, path->level);
	int ret = 0;

	bch2_bkey_buf_init(&tmp);

	while (nr-- && !ret) {
		if (!bch2_btree_node_relock(trans, path, path->level))
			break;

		bch2_btree_and_journal_iter_advance(jiter);
		k = bch2_btree_and_journal_iter_peek(jiter);
		if (!k.k)
			break;

		bch2_bkey_buf_reassemble(&tmp, c, k);
		ret = bch2_btree_node_prefetch(trans, path, tmp.k, path->btree_id,
					       path->level - 1);
	}

	if (!was_locked)
		btree_node_unlock(trans, path, path->level);

	bch2_bkey_buf_exit(&tmp, c);
	return ret;
}

static noinline void btree_node_mem_ptr_set(struct btree_trans *trans,
					    struct btree_path *path,
					    unsigned plevel, struct btree *b)
{
	struct btree_path_level *l = &path->l[plevel];
	bool locked = btree_node_locked(path, plevel);
	struct bkey_packed *k;
	struct bch_btree_ptr_v2 *bp;

	if (!bch2_btree_node_relock(trans, path, plevel))
		return;

	k = bch2_btree_node_iter_peek_all(&l->iter, l->b);
	BUG_ON(k->type != KEY_TYPE_btree_ptr_v2);

	bp = (void *) bkeyp_val(&l->b->format, k);
	bp->mem_ptr = (unsigned long)b;

	if (!locked)
		btree_node_unlock(trans, path, plevel);
}

static noinline int btree_node_iter_and_journal_peek(struct btree_trans *trans,
						     struct btree_path *path,
						     unsigned flags,
						     struct bkey_buf *out)
{
	struct bch_fs *c = trans->c;
	struct btree_path_level *l = path_l(path);
	struct btree_and_journal_iter jiter;
	struct bkey_s_c k;
	int ret = 0;

	__bch2_btree_and_journal_iter_init_node_iter(trans, &jiter, l->b, l->iter, path->pos);

	k = bch2_btree_and_journal_iter_peek(&jiter);

	bch2_bkey_buf_reassemble(out, c, k);

	if ((flags & BTREE_ITER_prefetch) &&
	    c->opts.btree_node_prefetch)
		ret = btree_path_prefetch_j(trans, path, &jiter);

	bch2_btree_and_journal_iter_exit(&jiter);
	return ret;
}

static __always_inline int btree_path_down(struct btree_trans *trans,
					   struct btree_path *path,
					   unsigned flags,
					   unsigned long trace_ip)
{
	struct bch_fs *c = trans->c;
	struct btree_path_level *l = path_l(path);
	struct btree *b;
	unsigned level = path->level - 1;
	enum six_lock_type lock_type = __btree_lock_want(path, level);
	struct bkey_buf tmp;
	int ret;

	EBUG_ON(!btree_node_locked(path, path->level));

	bch2_bkey_buf_init(&tmp);

	if (unlikely(trans->journal_replay_not_finished)) {
		ret = btree_node_iter_and_journal_peek(trans, path, flags, &tmp);
		if (ret)
			goto err;
	} else {
		struct bkey_packed *k = bch2_btree_node_iter_peek(&l->iter, l->b);
		if (!k) {
			struct printbuf buf = PRINTBUF;

			prt_str(&buf, "node not found at pos ");
			bch2_bpos_to_text(&buf, path->pos);
			prt_str(&buf, " within parent node ");
			bch2_bkey_val_to_text(&buf, c, bkey_i_to_s_c(&l->b->key));

			bch2_fs_fatal_error(c, "%s", buf.buf);
			printbuf_exit(&buf);
			ret = -BCH_ERR_btree_need_topology_repair;
			goto err;
		}

		bch2_bkey_buf_unpack(&tmp, c, l->b, k);

		if ((flags & BTREE_ITER_prefetch) &&
		    c->opts.btree_node_prefetch) {
			ret = btree_path_prefetch(trans, path);
			if (ret)
				goto err;
		}
	}

	b = bch2_btree_node_get(trans, path, tmp.k, level, lock_type, trace_ip);
	ret = PTR_ERR_OR_ZERO(b);
	if (unlikely(ret))
		goto err;

	if (likely(!trans->journal_replay_not_finished &&
		   tmp.k->k.type == KEY_TYPE_btree_ptr_v2) &&
	    unlikely(b != btree_node_mem_ptr(tmp.k)))
		btree_node_mem_ptr_set(trans, path, level + 1, b);

	if (btree_node_read_locked(path, level + 1))
		btree_node_unlock(trans, path, level + 1);

	mark_btree_node_locked(trans, path, level,
			       (enum btree_node_locked_type) lock_type);
	path->level = level;
	bch2_btree_path_level_init(trans, path, b);

	bch2_btree_path_verify_locks(path);
err:
	bch2_bkey_buf_exit(&tmp, c);
	return ret;
}

static int bch2_btree_path_traverse_all(struct btree_trans *trans)
{
	struct bch_fs *c = trans->c;
	struct btree_path *path;
	unsigned long trace_ip = _RET_IP_;
	unsigned i;
	int ret = 0;

	if (trans->in_traverse_all)
		return -BCH_ERR_transaction_restart_in_traverse_all;

	trans->in_traverse_all = true;
retry_all:
	trans->restarted = 0;
	trans->last_restarted_ip = 0;

	trans_for_each_path(trans, path, i)
		path->should_be_locked = false;

	btree_trans_sort_paths(trans);

	bch2_trans_unlock(trans);
	cond_resched();
	trans_set_locked(trans);

	if (unlikely(trans->memory_allocation_failure)) {
		struct closure cl;

		closure_init_stack(&cl);

		do {
			ret = bch2_btree_cache_cannibalize_lock(trans, &cl);
			closure_sync(&cl);
		} while (ret);
	}

	/* Now, redo traversals in correct order: */
	i = 0;
	while (i < trans->nr_sorted) {
		btree_path_idx_t idx = trans->sorted[i];

		/*
		 * Traversing a path can cause another path to be added at about
		 * the same position:
		 */
		if (trans->paths[idx].uptodate) {
			__btree_path_get(&trans->paths[idx], false);
			ret = bch2_btree_path_traverse_one(trans, idx, 0, _THIS_IP_);
			__btree_path_put(&trans->paths[idx], false);

			if (bch2_err_matches(ret, BCH_ERR_transaction_restart) ||
			    bch2_err_matches(ret, ENOMEM))
				goto retry_all;
			if (ret)
				goto err;
		} else {
			i++;
		}
	}

	/*
	 * We used to assert that all paths had been traversed here
	 * (path->uptodate < BTREE_ITER_NEED_TRAVERSE); however, since
	 * path->should_be_locked is not set yet, we might have unlocked and
	 * then failed to relock a path - that's fine.
	 */
err:
	bch2_btree_cache_cannibalize_unlock(trans);

	trans->in_traverse_all = false;

	trace_and_count(c, trans_traverse_all, trans, trace_ip);
	return ret;
}

static inline bool btree_path_check_pos_in_node(struct btree_path *path,
						unsigned l, int check_pos)
{
	if (check_pos < 0 && btree_path_pos_before_node(path, path->l[l].b))
		return false;
	if (check_pos > 0 && btree_path_pos_after_node(path, path->l[l].b))
		return false;
	return true;
}

static inline bool btree_path_good_node(struct btree_trans *trans,
					struct btree_path *path,
					unsigned l, int check_pos)
{
	return is_btree_node(path, l) &&
		bch2_btree_node_relock(trans, path, l) &&
		btree_path_check_pos_in_node(path, l, check_pos);
}

static void btree_path_set_level_down(struct btree_trans *trans,
				      struct btree_path *path,
				      unsigned new_level)
{
	unsigned l;

	path->level = new_level;

	for (l = path->level + 1; l < BTREE_MAX_DEPTH; l++)
		if (btree_lock_want(path, l) == BTREE_NODE_UNLOCKED)
			btree_node_unlock(trans, path, l);

	btree_path_set_dirty(path, BTREE_ITER_NEED_TRAVERSE);
	bch2_btree_path_verify(trans, path);
}

static noinline unsigned __btree_path_up_until_good_node(struct btree_trans *trans,
							 struct btree_path *path,
							 int check_pos)
{
	unsigned i, l = path->level;
again:
	while (btree_path_node(path, l) &&
	       !btree_path_good_node(trans, path, l, check_pos))
		__btree_path_set_level_up(trans, path, l++);

	/* If we need intent locks, take them too: */
	for (i = l + 1;
	     i < path->locks_want && btree_path_node(path, i);
	     i++)
		if (!bch2_btree_node_relock(trans, path, i)) {
			while (l <= i)
				__btree_path_set_level_up(trans, path, l++);
			goto again;
		}

	return l;
}

static inline unsigned btree_path_up_until_good_node(struct btree_trans *trans,
						     struct btree_path *path,
						     int check_pos)
{
	return likely(btree_node_locked(path, path->level) &&
		      btree_path_check_pos_in_node(path, path->level, check_pos))
		? path->level
		: __btree_path_up_until_good_node(trans, path, check_pos);
}

/*
 * This is the main state machine for walking down the btree - walks down to a
 * specified depth
 *
 * Returns 0 on success, -EIO on error (error reading in a btree node).
 *
 * On error, caller (peek_node()/peek_key()) must return NULL; the error is
 * stashed in the iterator and returned from bch2_trans_exit().
 */
int bch2_btree_path_traverse_one(struct btree_trans *trans,
				 btree_path_idx_t path_idx,
				 unsigned flags,
				 unsigned long trace_ip)
{
	struct btree_path *path = &trans->paths[path_idx];
	unsigned depth_want = path->level;
	int ret = -((int) trans->restarted);

	if (unlikely(ret))
		goto out;

	if (unlikely(!trans->srcu_held))
		bch2_trans_srcu_lock(trans);

	/*
	 * Ensure we obey path->should_be_locked: if it's set, we can't unlock
	 * and re-traverse the path without a transaction restart:
	 */
	if (path->should_be_locked) {
		ret = bch2_btree_path_relock(trans, path, trace_ip);
		goto out;
	}

	if (path->cached) {
		ret = bch2_btree_path_traverse_cached(trans, path, flags);
		goto out;
	}

	path = &trans->paths[path_idx];

	if (unlikely(path->level >= BTREE_MAX_DEPTH))
		goto out_uptodate;

	path->level = btree_path_up_until_good_node(trans, path, 0);
	unsigned max_level = path->level;

	EBUG_ON(btree_path_node(path, path->level) &&
		!btree_node_locked(path, path->level));

	/*
	 * Note: path->nodes[path->level] may be temporarily NULL here - that
	 * would indicate to other code that we got to the end of the btree,
	 * here it indicates that relocking the root failed - it's critical that
	 * btree_path_lock_root() comes next and that it can't fail
	 */
	while (path->level > depth_want) {
		ret = btree_path_node(path, path->level)
			? btree_path_down(trans, path, flags, trace_ip)
			: btree_path_lock_root(trans, path, depth_want, trace_ip);
		if (unlikely(ret)) {
			if (ret == 1) {
				/*
				 * No nodes at this level - got to the end of
				 * the btree:
				 */
				ret = 0;
				goto out;
			}

			__bch2_btree_path_unlock(trans, path);
			path->level = depth_want;
			path->l[path->level].b = ERR_PTR(ret);
			goto out;
		}
	}

	if (unlikely(max_level > path->level)) {
		struct btree_path *linked;
		unsigned iter;

		trans_for_each_path_with_node(trans, path_l(path)->b, linked, iter)
			for (unsigned j = path->level + 1; j < max_level; j++)
				linked->l[j] = path->l[j];
	}

out_uptodate:
	path->uptodate = BTREE_ITER_UPTODATE;
out:
	if (bch2_err_matches(ret, BCH_ERR_transaction_restart) != !!trans->restarted)
		panic("ret %s (%i) trans->restarted %s (%i)\n",
		      bch2_err_str(ret), ret,
		      bch2_err_str(trans->restarted), trans->restarted);
	bch2_btree_path_verify(trans, path);
	return ret;
}

static inline void btree_path_copy(struct btree_trans *trans, struct btree_path *dst,
			    struct btree_path *src)
{
	unsigned i, offset = offsetof(struct btree_path, pos);

	memcpy((void *) dst + offset,
	       (void *) src + offset,
	       sizeof(struct btree_path) - offset);

	for (i = 0; i < BTREE_MAX_DEPTH; i++) {
		unsigned t = btree_node_locked_type(dst, i);

		if (t != BTREE_NODE_UNLOCKED)
			six_lock_increment(&dst->l[i].b->c.lock, t);
	}
}

static btree_path_idx_t btree_path_clone(struct btree_trans *trans, btree_path_idx_t src,
					 bool intent, unsigned long ip)
{
	btree_path_idx_t new = btree_path_alloc(trans, src);
	btree_path_copy(trans, trans->paths + new, trans->paths + src);
	__btree_path_get(trans->paths + new, intent);
#ifdef TRACK_PATH_ALLOCATED
	trans->paths[new].ip_allocated = ip;
#endif
	return new;
}

__flatten
btree_path_idx_t __bch2_btree_path_make_mut(struct btree_trans *trans,
			btree_path_idx_t path, bool intent, unsigned long ip)
{
	__btree_path_put(trans->paths + path, intent);
	path = btree_path_clone(trans, path, intent, ip);
	trans->paths[path].preserve = false;
	return path;
}

btree_path_idx_t __must_check
__bch2_btree_path_set_pos(struct btree_trans *trans,
			  btree_path_idx_t path_idx, struct bpos new_pos,
			  bool intent, unsigned long ip)
{
	int cmp = bpos_cmp(new_pos, trans->paths[path_idx].pos);

	bch2_trans_verify_not_in_restart(trans);
	EBUG_ON(!trans->paths[path_idx].ref);

	path_idx = bch2_btree_path_make_mut(trans, path_idx, intent, ip);

	struct btree_path *path = trans->paths + path_idx;
	path->pos		= new_pos;
	trans->paths_sorted	= false;

	if (unlikely(path->cached)) {
		btree_node_unlock(trans, path, 0);
		path->l[0].b = ERR_PTR(-BCH_ERR_no_btree_node_up);
		btree_path_set_dirty(path, BTREE_ITER_NEED_TRAVERSE);
		goto out;
	}

	unsigned level = btree_path_up_until_good_node(trans, path, cmp);

	if (btree_path_node(path, level)) {
		struct btree_path_level *l = &path->l[level];

		BUG_ON(!btree_node_locked(path, level));
		/*
		 * We might have to skip over many keys, or just a few: try
		 * advancing the node iterator, and if we have to skip over too
		 * many keys just reinit it (or if we're rewinding, since that
		 * is expensive).
		 */
		if (cmp < 0 ||
		    !btree_path_advance_to_pos(path, l, 8))
			bch2_btree_node_iter_init(&l->iter, l->b, &path->pos);

		/*
		 * Iterators to interior nodes should always be pointed at the first non
		 * whiteout:
		 */
		if (unlikely(level))
			bch2_btree_node_iter_peek(&l->iter, l->b);
	}

	if (unlikely(level != path->level)) {
		btree_path_set_dirty(path, BTREE_ITER_NEED_TRAVERSE);
		__bch2_btree_path_unlock(trans, path);
	}
out:
	bch2_btree_path_verify(trans, path);
	return path_idx;
}

/* Btree path: main interface: */

static struct btree_path *have_path_at_pos(struct btree_trans *trans, struct btree_path *path)
{
	struct btree_path *sib;

	sib = prev_btree_path(trans, path);
	if (sib && !btree_path_cmp(sib, path))
		return sib;

	sib = next_btree_path(trans, path);
	if (sib && !btree_path_cmp(sib, path))
		return sib;

	return NULL;
}

static struct btree_path *have_node_at_pos(struct btree_trans *trans, struct btree_path *path)
{
	struct btree_path *sib;

	sib = prev_btree_path(trans, path);
	if (sib && sib->level == path->level && path_l(sib)->b == path_l(path)->b)
		return sib;

	sib = next_btree_path(trans, path);
	if (sib && sib->level == path->level && path_l(sib)->b == path_l(path)->b)
		return sib;

	return NULL;
}

static inline void __bch2_path_free(struct btree_trans *trans, btree_path_idx_t path)
{
	__bch2_btree_path_unlock(trans, trans->paths + path);
	btree_path_list_remove(trans, trans->paths + path);
	__clear_bit(path, trans->paths_allocated);
}

static bool bch2_btree_path_can_relock(struct btree_trans *trans, struct btree_path *path)
{
	unsigned l = path->level;

	do {
		if (!btree_path_node(path, l))
			break;

		if (!is_btree_node(path, l))
			return false;

		if (path->l[l].lock_seq != path->l[l].b->c.lock.seq)
			return false;

		l++;
	} while (l < path->locks_want);

	return true;
}

void bch2_path_put(struct btree_trans *trans, btree_path_idx_t path_idx, bool intent)
{
	struct btree_path *path = trans->paths + path_idx, *dup;

	if (!__btree_path_put(path, intent))
		return;

	dup = path->preserve
		? have_path_at_pos(trans, path)
		: have_node_at_pos(trans, path);

	if (!dup && !(!path->preserve && !is_btree_node(path, path->level)))
		return;

	if (path->should_be_locked && !trans->restarted) {
		if (!dup)
			return;

		if (!(trans->locked
		      ? bch2_btree_path_relock_norestart(trans, dup)
		      : bch2_btree_path_can_relock(trans, dup)))
			return;
	}

	if (dup) {
		dup->preserve		|= path->preserve;
		dup->should_be_locked	|= path->should_be_locked;
	}

	__bch2_path_free(trans, path_idx);
}

static void bch2_path_put_nokeep(struct btree_trans *trans, btree_path_idx_t path,
				 bool intent)
{
	if (!__btree_path_put(trans->paths + path, intent))
		return;

	__bch2_path_free(trans, path);
}

void __noreturn bch2_trans_restart_error(struct btree_trans *trans, u32 restart_count)
{
	panic("trans->restart_count %u, should be %u, last restarted by %pS\n",
	      trans->restart_count, restart_count,
	      (void *) trans->last_begin_ip);
}

void __noreturn bch2_trans_in_restart_error(struct btree_trans *trans)
{
	panic("in transaction restart: %s, last restarted by %pS\n",
	      bch2_err_str(trans->restarted),
	      (void *) trans->last_restarted_ip);
}

void __noreturn bch2_trans_unlocked_error(struct btree_trans *trans)
{
	panic("trans should be locked, unlocked by %pS\n",
	      (void *) trans->last_unlock_ip);
}

noinline __cold
void bch2_trans_updates_to_text(struct printbuf *buf, struct btree_trans *trans)
{
	prt_printf(buf, "transaction updates for %s journal seq %llu\n",
	       trans->fn, trans->journal_res.seq);
	printbuf_indent_add(buf, 2);

	trans_for_each_update(trans, i) {
		struct bkey_s_c old = { &i->old_k, i->old_v };

		prt_printf(buf, "update: btree=%s cached=%u %pS\n",
		       bch2_btree_id_str(i->btree_id),
		       i->cached,
		       (void *) i->ip_allocated);

		prt_printf(buf, "  old ");
		bch2_bkey_val_to_text(buf, trans->c, old);
		prt_newline(buf);

		prt_printf(buf, "  new ");
		bch2_bkey_val_to_text(buf, trans->c, bkey_i_to_s_c(i->k));
		prt_newline(buf);
	}

	for (struct jset_entry *e = trans->journal_entries;
	     e != btree_trans_journal_entries_top(trans);
	     e = vstruct_next(e))
		bch2_journal_entry_to_text(buf, trans->c, e);

	printbuf_indent_sub(buf, 2);
}

noinline __cold
void bch2_dump_trans_updates(struct btree_trans *trans)
{
	struct printbuf buf = PRINTBUF;

	bch2_trans_updates_to_text(&buf, trans);
	bch2_print_str(trans->c, buf.buf);
	printbuf_exit(&buf);
}

static void bch2_btree_path_to_text_short(struct printbuf *out, struct btree_trans *trans, btree_path_idx_t path_idx)
{
	struct btree_path *path = trans->paths + path_idx;

	prt_printf(out, "path: idx %2u ref %u:%u %c %c %c btree=%s l=%u pos ",
		   path_idx, path->ref, path->intent_ref,
		   path->preserve ? 'P' : ' ',
		   path->should_be_locked ? 'S' : ' ',
		   path->cached ? 'C' : 'B',
		   bch2_btree_id_str(path->btree_id),
		   path->level);
	bch2_bpos_to_text(out, path->pos);

	if (!path->cached && btree_node_locked(path, path->level)) {
		prt_char(out, ' ');
		struct btree *b = path_l(path)->b;
		bch2_bpos_to_text(out, b->data->min_key);
		prt_char(out, '-');
		bch2_bpos_to_text(out, b->key.k.p);
	}

#ifdef TRACK_PATH_ALLOCATED
	prt_printf(out, " %pS", (void *) path->ip_allocated);
#endif
}

static const char *btree_node_locked_str(enum btree_node_locked_type t)
{
	switch (t) {
	case BTREE_NODE_UNLOCKED:
		return "unlocked";
	case BTREE_NODE_READ_LOCKED:
		return "read";
	case BTREE_NODE_INTENT_LOCKED:
		return "intent";
	case BTREE_NODE_WRITE_LOCKED:
		return "write";
	default:
		return NULL;
	}
}

void bch2_btree_path_to_text(struct printbuf *out, struct btree_trans *trans, btree_path_idx_t path_idx)
{
	bch2_btree_path_to_text_short(out, trans, path_idx);

	struct btree_path *path = trans->paths + path_idx;

	prt_printf(out, " uptodate %u locks_want %u", path->uptodate, path->locks_want);
	prt_newline(out);

	printbuf_indent_add(out, 2);
	for (unsigned l = 0; l < BTREE_MAX_DEPTH; l++) {
		prt_printf(out, "l=%u locks %s seq %u node ", l,
			   btree_node_locked_str(btree_node_locked_type(path, l)),
			   path->l[l].lock_seq);

		int ret = PTR_ERR_OR_ZERO(path->l[l].b);
		if (ret)
			prt_str(out, bch2_err_str(ret));
		else
			prt_printf(out, "%px", path->l[l].b);
		prt_newline(out);
	}
	printbuf_indent_sub(out, 2);
}

static noinline __cold
void __bch2_trans_paths_to_text(struct printbuf *out, struct btree_trans *trans,
				bool nosort)
{
	struct trans_for_each_path_inorder_iter iter;

	if (!nosort)
		btree_trans_sort_paths(trans);

	trans_for_each_path_idx_inorder(trans, iter) {
		bch2_btree_path_to_text_short(out, trans, iter.path_idx);
		prt_newline(out);
	}
}

noinline __cold
void bch2_trans_paths_to_text(struct printbuf *out, struct btree_trans *trans)
{
	__bch2_trans_paths_to_text(out, trans, false);
}

static noinline __cold
void __bch2_dump_trans_paths_updates(struct btree_trans *trans, bool nosort)
{
	struct printbuf buf = PRINTBUF;

	__bch2_trans_paths_to_text(&buf, trans, nosort);
	bch2_trans_updates_to_text(&buf, trans);

	bch2_print_str(trans->c, buf.buf);
	printbuf_exit(&buf);
}

noinline __cold
void bch2_dump_trans_paths_updates(struct btree_trans *trans)
{
	__bch2_dump_trans_paths_updates(trans, false);
}

noinline __cold
static void bch2_trans_update_max_paths(struct btree_trans *trans)
{
	struct btree_transaction_stats *s = btree_trans_stats(trans);
	struct printbuf buf = PRINTBUF;
	size_t nr = bitmap_weight(trans->paths_allocated, trans->nr_paths);

	bch2_trans_paths_to_text(&buf, trans);

	if (!buf.allocation_failure) {
		mutex_lock(&s->lock);
		if (nr > s->nr_max_paths) {
			s->nr_max_paths = nr;
			swap(s->max_paths_text, buf.buf);
		}
		mutex_unlock(&s->lock);
	}

	printbuf_exit(&buf);

	trans->nr_paths_max = nr;
}

noinline __cold
int __bch2_btree_trans_too_many_iters(struct btree_trans *trans)
{
	if (trace_trans_restart_too_many_iters_enabled()) {
		struct printbuf buf = PRINTBUF;

		bch2_trans_paths_to_text(&buf, trans);
		trace_trans_restart_too_many_iters(trans, _THIS_IP_, buf.buf);
		printbuf_exit(&buf);
	}

	count_event(trans->c, trans_restart_too_many_iters);

	return btree_trans_restart(trans, BCH_ERR_transaction_restart_too_many_iters);
}

static noinline void btree_path_overflow(struct btree_trans *trans)
{
	bch2_dump_trans_paths_updates(trans);
	bch_err(trans->c, "trans path overflow");
}

static noinline void btree_paths_realloc(struct btree_trans *trans)
{
	unsigned nr = trans->nr_paths * 2;

	void *p = kvzalloc(BITS_TO_LONGS(nr) * sizeof(unsigned long) +
			  sizeof(struct btree_trans_paths) +
			  nr * sizeof(struct btree_path) +
			  nr * sizeof(btree_path_idx_t) + 8 +
			  nr * sizeof(struct btree_insert_entry), GFP_KERNEL|__GFP_NOFAIL);

	unsigned long *paths_allocated = p;
	memcpy(paths_allocated, trans->paths_allocated, BITS_TO_LONGS(trans->nr_paths) * sizeof(unsigned long));
	p += BITS_TO_LONGS(nr) * sizeof(unsigned long);

	p += sizeof(struct btree_trans_paths);
	struct btree_path *paths = p;
	*trans_paths_nr(paths) = nr;
	memcpy(paths, trans->paths, trans->nr_paths * sizeof(struct btree_path));
	p += nr * sizeof(struct btree_path);

	btree_path_idx_t *sorted = p;
	memcpy(sorted, trans->sorted, trans->nr_sorted * sizeof(btree_path_idx_t));
	p += nr * sizeof(btree_path_idx_t) + 8;

	struct btree_insert_entry *updates = p;
	memcpy(updates, trans->updates, trans->nr_paths * sizeof(struct btree_insert_entry));

	unsigned long *old = trans->paths_allocated;

	rcu_assign_pointer(trans->paths_allocated,	paths_allocated);
	rcu_assign_pointer(trans->paths,		paths);
	rcu_assign_pointer(trans->sorted,		sorted);
	rcu_assign_pointer(trans->updates,		updates);

	trans->nr_paths		= nr;

	if (old != trans->_paths_allocated)
		kfree_rcu_mightsleep(old);
}

static inline btree_path_idx_t btree_path_alloc(struct btree_trans *trans,
						btree_path_idx_t pos)
{
	btree_path_idx_t idx = find_first_zero_bit(trans->paths_allocated, trans->nr_paths);

	if (unlikely(idx == trans->nr_paths)) {
		if (trans->nr_paths == BTREE_ITER_MAX) {
			btree_path_overflow(trans);
			return 0;
		}

		btree_paths_realloc(trans);
	}

	/*
	 * Do this before marking the new path as allocated, since it won't be
	 * initialized yet:
	 */
	if (unlikely(idx > trans->nr_paths_max))
		bch2_trans_update_max_paths(trans);

	__set_bit(idx, trans->paths_allocated);

	struct btree_path *path = &trans->paths[idx];
	path->ref		= 0;
	path->intent_ref	= 0;
	path->nodes_locked	= 0;

	btree_path_list_add(trans, pos, idx);
	trans->paths_sorted = false;
	return idx;
}

btree_path_idx_t bch2_path_get(struct btree_trans *trans,
			     enum btree_id btree_id, struct bpos pos,
			     unsigned locks_want, unsigned level,
			     unsigned flags, unsigned long ip)
{
	struct btree_path *path;
	bool cached = flags & BTREE_ITER_cached;
	bool intent = flags & BTREE_ITER_intent;
	struct trans_for_each_path_inorder_iter iter;
	btree_path_idx_t path_pos = 0, path_idx;

	bch2_trans_verify_not_unlocked(trans);
	bch2_trans_verify_not_in_restart(trans);
	bch2_trans_verify_locks(trans);

	btree_trans_sort_paths(trans);

	trans_for_each_path_inorder(trans, path, iter) {
		if (__btree_path_cmp(path,
				     btree_id,
				     cached,
				     pos,
				     level) > 0)
			break;

		path_pos = iter.path_idx;
	}

	if (path_pos &&
	    trans->paths[path_pos].cached	== cached &&
	    trans->paths[path_pos].btree_id	== btree_id &&
	    trans->paths[path_pos].level	== level) {
		__btree_path_get(trans->paths + path_pos, intent);
		path_idx = bch2_btree_path_set_pos(trans, path_pos, pos, intent, ip);
		path = trans->paths + path_idx;
	} else {
		path_idx = btree_path_alloc(trans, path_pos);
		path = trans->paths + path_idx;

		__btree_path_get(path, intent);
		path->pos			= pos;
		path->btree_id			= btree_id;
		path->cached			= cached;
		path->uptodate			= BTREE_ITER_NEED_TRAVERSE;
		path->should_be_locked		= false;
		path->level			= level;
		path->locks_want		= locks_want;
		path->nodes_locked		= 0;
		for (unsigned i = 0; i < ARRAY_SIZE(path->l); i++)
			path->l[i].b		= ERR_PTR(-BCH_ERR_no_btree_node_init);
#ifdef TRACK_PATH_ALLOCATED
		path->ip_allocated		= ip;
#endif
		trans->paths_sorted		= false;
	}

	if (!(flags & BTREE_ITER_nopreserve))
		path->preserve = true;

	if (path->intent_ref)
		locks_want = max(locks_want, level + 1);

	/*
	 * If the path has locks_want greater than requested, we don't downgrade
	 * it here - on transaction restart because btree node split needs to
	 * upgrade locks, we might be putting/getting the iterator again.
	 * Downgrading iterators only happens via bch2_trans_downgrade(), after
	 * a successful transaction commit.
	 */

	locks_want = min(locks_want, BTREE_MAX_DEPTH);
	if (locks_want > path->locks_want)
		bch2_btree_path_upgrade_noupgrade_sibs(trans, path, locks_want, NULL);

	return path_idx;
}

btree_path_idx_t bch2_path_get_unlocked_mut(struct btree_trans *trans,
					    enum btree_id btree_id,
					    unsigned level,
					    struct bpos pos)
{
	btree_path_idx_t path_idx = bch2_path_get(trans, btree_id, pos, level + 1, level,
			     BTREE_ITER_nopreserve|
			     BTREE_ITER_intent, _RET_IP_);
	path_idx = bch2_btree_path_make_mut(trans, path_idx, true, _RET_IP_);

	struct btree_path *path = trans->paths + path_idx;
	bch2_btree_path_downgrade(trans, path);
	__bch2_btree_path_unlock(trans, path);
	return path_idx;
}

struct bkey_s_c bch2_btree_path_peek_slot(struct btree_path *path, struct bkey *u)
{

	struct btree_path_level *l = path_l(path);
	struct bkey_packed *_k;
	struct bkey_s_c k;

	if (unlikely(!l->b))
		return bkey_s_c_null;

	EBUG_ON(path->uptodate != BTREE_ITER_UPTODATE);
	EBUG_ON(!btree_node_locked(path, path->level));

	if (!path->cached) {
		_k = bch2_btree_node_iter_peek_all(&l->iter, l->b);
		k = _k ? bkey_disassemble(l->b, _k, u) : bkey_s_c_null;

		EBUG_ON(k.k && bkey_deleted(k.k) && bpos_eq(k.k->p, path->pos));

		if (!k.k || !bpos_eq(path->pos, k.k->p))
			goto hole;
	} else {
		struct bkey_cached *ck = (void *) path->l[0].b;
		if (!ck)
			return bkey_s_c_null;

		EBUG_ON(path->btree_id != ck->key.btree_id ||
			!bkey_eq(path->pos, ck->key.pos));

		*u = ck->k->k;
		k = bkey_i_to_s_c(ck->k);
	}

	return k;
hole:
	bkey_init(u);
	u->p = path->pos;
	return (struct bkey_s_c) { u, NULL };
}


void bch2_set_btree_iter_dontneed(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;

	if (!iter->path || trans->restarted)
		return;

	struct btree_path *path = btree_iter_path(trans, iter);
	path->preserve		= false;
	if (path->ref == 1)
		path->should_be_locked	= false;
}
/* Btree iterators: */

int __must_check
__bch2_btree_iter_traverse(struct btree_iter *iter)
{
	return bch2_btree_path_traverse(iter->trans, iter->path, iter->flags);
}

int __must_check
bch2_btree_iter_traverse(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	int ret;

	bch2_trans_verify_not_unlocked(trans);

	iter->path = bch2_btree_path_set_pos(trans, iter->path,
					btree_iter_search_key(iter),
					iter->flags & BTREE_ITER_intent,
					btree_iter_ip_allocated(iter));

	ret = bch2_btree_path_traverse(iter->trans, iter->path, iter->flags);
	if (ret)
		return ret;

	struct btree_path *path = btree_iter_path(trans, iter);
	if (btree_path_node(path, path->level))
		btree_path_set_should_be_locked(path);
	return 0;
}

/* Iterate across nodes (leaf and interior nodes) */

struct btree *bch2_btree_iter_peek_node(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	struct btree *b = NULL;
	int ret;

	EBUG_ON(trans->paths[iter->path].cached);
	bch2_btree_iter_verify(iter);

	ret = bch2_btree_path_traverse(trans, iter->path, iter->flags);
	if (ret)
		goto err;

	struct btree_path *path = btree_iter_path(trans, iter);
	b = btree_path_node(path, path->level);
	if (!b)
		goto out;

	BUG_ON(bpos_lt(b->key.k.p, iter->pos));

	bkey_init(&iter->k);
	iter->k.p = iter->pos = b->key.k.p;

	iter->path = bch2_btree_path_set_pos(trans, iter->path, b->key.k.p,
					iter->flags & BTREE_ITER_intent,
					btree_iter_ip_allocated(iter));
	btree_path_set_should_be_locked(btree_iter_path(trans, iter));
out:
	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);

	return b;
err:
	b = ERR_PTR(ret);
	goto out;
}

struct btree *bch2_btree_iter_peek_node_and_restart(struct btree_iter *iter)
{
	struct btree *b;

	while (b = bch2_btree_iter_peek_node(iter),
	       bch2_err_matches(PTR_ERR_OR_ZERO(b), BCH_ERR_transaction_restart))
		bch2_trans_begin(iter->trans);

	return b;
}

struct btree *bch2_btree_iter_next_node(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	struct btree *b = NULL;
	int ret;

	EBUG_ON(trans->paths[iter->path].cached);
	bch2_trans_verify_not_in_restart(trans);
	bch2_btree_iter_verify(iter);

	ret = bch2_btree_path_traverse(trans, iter->path, iter->flags);
	if (ret)
		goto err;


	struct btree_path *path = btree_iter_path(trans, iter);

	/* already at end? */
	if (!btree_path_node(path, path->level))
		return NULL;

	/* got to end? */
	if (!btree_path_node(path, path->level + 1)) {
		btree_path_set_level_up(trans, path);
		return NULL;
	}

	if (!bch2_btree_node_relock(trans, path, path->level + 1)) {
		__bch2_btree_path_unlock(trans, path);
		path->l[path->level].b		= ERR_PTR(-BCH_ERR_no_btree_node_relock);
		path->l[path->level + 1].b	= ERR_PTR(-BCH_ERR_no_btree_node_relock);
		btree_path_set_dirty(path, BTREE_ITER_NEED_TRAVERSE);
		trace_and_count(trans->c, trans_restart_relock_next_node, trans, _THIS_IP_, path);
		ret = btree_trans_restart(trans, BCH_ERR_transaction_restart_relock);
		goto err;
	}

	b = btree_path_node(path, path->level + 1);

	if (bpos_eq(iter->pos, b->key.k.p)) {
		__btree_path_set_level_up(trans, path, path->level++);
	} else {
		if (btree_lock_want(path, path->level + 1) == BTREE_NODE_UNLOCKED)
			btree_node_unlock(trans, path, path->level + 1);

		/*
		 * Haven't gotten to the end of the parent node: go back down to
		 * the next child node
		 */
		iter->path = bch2_btree_path_set_pos(trans, iter->path,
					bpos_successor(iter->pos),
					iter->flags & BTREE_ITER_intent,
					btree_iter_ip_allocated(iter));

		path = btree_iter_path(trans, iter);
		btree_path_set_level_down(trans, path, iter->min_depth);

		ret = bch2_btree_path_traverse(trans, iter->path, iter->flags);
		if (ret)
			goto err;

		path = btree_iter_path(trans, iter);
		b = path->l[path->level].b;
	}

	bkey_init(&iter->k);
	iter->k.p = iter->pos = b->key.k.p;

	iter->path = bch2_btree_path_set_pos(trans, iter->path, b->key.k.p,
					iter->flags & BTREE_ITER_intent,
					btree_iter_ip_allocated(iter));
	btree_path_set_should_be_locked(btree_iter_path(trans, iter));
	EBUG_ON(btree_iter_path(trans, iter)->uptodate);
out:
	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);

	return b;
err:
	b = ERR_PTR(ret);
	goto out;
}

/* Iterate across keys (in leaf nodes only) */

inline bool bch2_btree_iter_advance(struct btree_iter *iter)
{
	struct bpos pos = iter->k.p;
	bool ret = !(iter->flags & BTREE_ITER_all_snapshots
		     ? bpos_eq(pos, SPOS_MAX)
		     : bkey_eq(pos, SPOS_MAX));

	if (ret && !(iter->flags & BTREE_ITER_is_extents))
		pos = bkey_successor(iter, pos);
	bch2_btree_iter_set_pos(iter, pos);
	return ret;
}

inline bool bch2_btree_iter_rewind(struct btree_iter *iter)
{
	struct bpos pos = bkey_start_pos(&iter->k);
	bool ret = !(iter->flags & BTREE_ITER_all_snapshots
		     ? bpos_eq(pos, POS_MIN)
		     : bkey_eq(pos, POS_MIN));

	if (ret && !(iter->flags & BTREE_ITER_is_extents))
		pos = bkey_predecessor(iter, pos);
	bch2_btree_iter_set_pos(iter, pos);
	return ret;
}

static noinline
void bch2_btree_trans_peek_prev_updates(struct btree_trans *trans, struct btree_iter *iter,
					struct bkey_s_c *k)
{
	struct bpos end = path_l(btree_iter_path(trans, iter))->b->data->min_key;

	trans_for_each_update(trans, i)
		if (!i->key_cache_already_flushed &&
		    i->btree_id == iter->btree_id &&
		    bpos_le(i->k->k.p, iter->pos) &&
		    bpos_ge(i->k->k.p, k->k ? k->k->p : end)) {
			iter->k = i->k->k;
			*k = bkey_i_to_s_c(i->k);
		}
}

static noinline
void bch2_btree_trans_peek_updates(struct btree_trans *trans, struct btree_iter *iter,
				   struct bkey_s_c *k)
{
	struct btree_path *path = btree_iter_path(trans, iter);
	struct bpos end = path_l(path)->b->key.k.p;

	trans_for_each_update(trans, i)
		if (!i->key_cache_already_flushed &&
		    i->btree_id == iter->btree_id &&
		    bpos_ge(i->k->k.p, path->pos) &&
		    bpos_le(i->k->k.p, k->k ? k->k->p : end)) {
			iter->k = i->k->k;
			*k = bkey_i_to_s_c(i->k);
		}
}

static noinline
void bch2_btree_trans_peek_slot_updates(struct btree_trans *trans, struct btree_iter *iter,
					struct bkey_s_c *k)
{
	trans_for_each_update(trans, i)
		if (!i->key_cache_already_flushed &&
		    i->btree_id == iter->btree_id &&
		    bpos_eq(i->k->k.p, iter->pos)) {
			iter->k = i->k->k;
			*k = bkey_i_to_s_c(i->k);
		}
}

static struct bkey_i *bch2_btree_journal_peek(struct btree_trans *trans,
					      struct btree_iter *iter,
					      struct bpos end_pos)
{
	struct btree_path *path = btree_iter_path(trans, iter);

	return bch2_journal_keys_peek_upto(trans->c, iter->btree_id,
					   path->level,
					   path->pos,
					   end_pos,
					   &iter->journal_idx);
}

static noinline
struct bkey_s_c btree_trans_peek_slot_journal(struct btree_trans *trans,
					      struct btree_iter *iter)
{
	struct btree_path *path = btree_iter_path(trans, iter);
	struct bkey_i *k = bch2_btree_journal_peek(trans, iter, path->pos);

	if (k) {
		iter->k = k->k;
		return bkey_i_to_s_c(k);
	} else {
		return bkey_s_c_null;
	}
}

static noinline
struct bkey_s_c btree_trans_peek_journal(struct btree_trans *trans,
					 struct btree_iter *iter,
					 struct bkey_s_c k)
{
	struct btree_path *path = btree_iter_path(trans, iter);
	struct bkey_i *next_journal =
		bch2_btree_journal_peek(trans, iter,
				k.k ? k.k->p : path_l(path)->b->key.k.p);

	if (next_journal) {
		iter->k = next_journal->k;
		k = bkey_i_to_s_c(next_journal);
	}

	return k;
}

/*
 * Checks btree key cache for key at iter->pos and returns it if present, or
 * bkey_s_c_null:
 */
static noinline
struct bkey_s_c btree_trans_peek_key_cache(struct btree_iter *iter, struct bpos pos)
{
	struct btree_trans *trans = iter->trans;
	struct bch_fs *c = trans->c;
	struct bkey u;
	struct bkey_s_c k;
	int ret;

	bch2_trans_verify_not_in_restart(trans);
	bch2_trans_verify_not_unlocked(trans);

	if ((iter->flags & BTREE_ITER_key_cache_fill) &&
	    bpos_eq(iter->pos, pos))
		return bkey_s_c_null;

	if (!bch2_btree_key_cache_find(c, iter->btree_id, pos))
		return bkey_s_c_null;

	if (!iter->key_cache_path)
		iter->key_cache_path = bch2_path_get(trans, iter->btree_id, pos,
						     iter->flags & BTREE_ITER_intent, 0,
						     iter->flags|BTREE_ITER_cached|
						     BTREE_ITER_cached_nofill,
						     _THIS_IP_);

	iter->key_cache_path = bch2_btree_path_set_pos(trans, iter->key_cache_path, pos,
					iter->flags & BTREE_ITER_intent,
					btree_iter_ip_allocated(iter));

	ret =   bch2_btree_path_traverse(trans, iter->key_cache_path,
					 iter->flags|BTREE_ITER_cached) ?:
		bch2_btree_path_relock(trans, btree_iter_path(trans, iter), _THIS_IP_);
	if (unlikely(ret))
		return bkey_s_c_err(ret);

	btree_path_set_should_be_locked(trans->paths + iter->key_cache_path);

	k = bch2_btree_path_peek_slot(trans->paths + iter->key_cache_path, &u);
	if (k.k && !bkey_err(k)) {
		iter->k = u;
		k.k = &iter->k;
	}
	return k;
}

static struct bkey_s_c __bch2_btree_iter_peek(struct btree_iter *iter, struct bpos search_key)
{
	struct btree_trans *trans = iter->trans;
	struct bkey_s_c k, k2;
	int ret;

	EBUG_ON(btree_iter_path(trans, iter)->cached);
	bch2_btree_iter_verify(iter);

	while (1) {
		struct btree_path_level *l;

		iter->path = bch2_btree_path_set_pos(trans, iter->path, search_key,
					iter->flags & BTREE_ITER_intent,
					btree_iter_ip_allocated(iter));

		ret = bch2_btree_path_traverse(trans, iter->path, iter->flags);
		if (unlikely(ret)) {
			/* ensure that iter->k is consistent with iter->pos: */
			bch2_btree_iter_set_pos(iter, iter->pos);
			k = bkey_s_c_err(ret);
			goto out;
		}

		struct btree_path *path = btree_iter_path(trans, iter);
		l = path_l(path);

		if (unlikely(!l->b)) {
			/* No btree nodes at requested level: */
			bch2_btree_iter_set_pos(iter, SPOS_MAX);
			k = bkey_s_c_null;
			goto out;
		}

		btree_path_set_should_be_locked(path);

		k = btree_path_level_peek_all(trans->c, l, &iter->k);

		if (unlikely(iter->flags & BTREE_ITER_with_key_cache) &&
		    k.k &&
		    (k2 = btree_trans_peek_key_cache(iter, k.k->p)).k) {
			k = k2;
			ret = bkey_err(k);
			if (ret) {
				bch2_btree_iter_set_pos(iter, iter->pos);
				goto out;
			}
		}

		if (unlikely(iter->flags & BTREE_ITER_with_journal))
			k = btree_trans_peek_journal(trans, iter, k);

		if (unlikely((iter->flags & BTREE_ITER_with_updates) &&
			     trans->nr_updates))
			bch2_btree_trans_peek_updates(trans, iter, &k);

		if (k.k && bkey_deleted(k.k)) {
			/*
			 * If we've got a whiteout, and it's after the search
			 * key, advance the search key to the whiteout instead
			 * of just after the whiteout - it might be a btree
			 * whiteout, with a real key at the same position, since
			 * in the btree deleted keys sort before non deleted.
			 */
			search_key = !bpos_eq(search_key, k.k->p)
				? k.k->p
				: bpos_successor(k.k->p);
			continue;
		}

		if (likely(k.k)) {
			break;
		} else if (likely(!bpos_eq(l->b->key.k.p, SPOS_MAX))) {
			/* Advance to next leaf node: */
			search_key = bpos_successor(l->b->key.k.p);
		} else {
			/* End of btree: */
			bch2_btree_iter_set_pos(iter, SPOS_MAX);
			k = bkey_s_c_null;
			goto out;
		}
	}
out:
	bch2_btree_iter_verify(iter);

	return k;
}

/**
 * bch2_btree_iter_peek_upto() - returns first key greater than or equal to
 * iterator's current position
 * @iter:	iterator to peek from
 * @end:	search limit: returns keys less than or equal to @end
 *
 * Returns:	key if found, or an error extractable with bkey_err().
 */
struct bkey_s_c bch2_btree_iter_peek_upto(struct btree_iter *iter, struct bpos end)
{
	struct btree_trans *trans = iter->trans;
	struct bpos search_key = btree_iter_search_key(iter);
	struct bkey_s_c k;
	struct bpos iter_pos;
	int ret;

	bch2_trans_verify_not_unlocked(trans);
	EBUG_ON((iter->flags & BTREE_ITER_filter_snapshots) && bkey_eq(end, POS_MAX));

	if (iter->update_path) {
		bch2_path_put_nokeep(trans, iter->update_path,
				     iter->flags & BTREE_ITER_intent);
		iter->update_path = 0;
	}

	bch2_btree_iter_verify_entry_exit(iter);

	while (1) {
		k = __bch2_btree_iter_peek(iter, search_key);
		if (unlikely(!k.k))
			goto end;
		if (unlikely(bkey_err(k)))
			goto out_no_locked;

		/*
		 * We need to check against @end before FILTER_SNAPSHOTS because
		 * if we get to a different inode that requested we might be
		 * seeing keys for a different snapshot tree that will all be
		 * filtered out.
		 *
		 * But we can't do the full check here, because bkey_start_pos()
		 * isn't monotonically increasing before FILTER_SNAPSHOTS, and
		 * that's what we check against in extents mode:
		 */
		if (unlikely(!(iter->flags & BTREE_ITER_is_extents)
			     ? bkey_gt(k.k->p, end)
			     : k.k->p.inode > end.inode))
			goto end;

		if (iter->update_path &&
		    !bkey_eq(trans->paths[iter->update_path].pos, k.k->p)) {
			bch2_path_put_nokeep(trans, iter->update_path,
					     iter->flags & BTREE_ITER_intent);
			iter->update_path = 0;
		}

		if ((iter->flags & BTREE_ITER_filter_snapshots) &&
		    (iter->flags & BTREE_ITER_intent) &&
		    !(iter->flags & BTREE_ITER_is_extents) &&
		    !iter->update_path) {
			struct bpos pos = k.k->p;

			if (pos.snapshot < iter->snapshot) {
				search_key = bpos_successor(k.k->p);
				continue;
			}

			pos.snapshot = iter->snapshot;

			/*
			 * advance, same as on exit for iter->path, but only up
			 * to snapshot
			 */
			__btree_path_get(trans->paths + iter->path, iter->flags & BTREE_ITER_intent);
			iter->update_path = iter->path;

			iter->update_path = bch2_btree_path_set_pos(trans,
						iter->update_path, pos,
						iter->flags & BTREE_ITER_intent,
						_THIS_IP_);
			ret = bch2_btree_path_traverse(trans, iter->update_path, iter->flags);
			if (unlikely(ret)) {
				k = bkey_s_c_err(ret);
				goto out_no_locked;
			}
		}

		/*
		 * We can never have a key in a leaf node at POS_MAX, so
		 * we don't have to check these successor() calls:
		 */
		if ((iter->flags & BTREE_ITER_filter_snapshots) &&
		    !bch2_snapshot_is_ancestor(trans->c,
					       iter->snapshot,
					       k.k->p.snapshot)) {
			search_key = bpos_successor(k.k->p);
			continue;
		}

		if (bkey_whiteout(k.k) &&
		    !(iter->flags & BTREE_ITER_all_snapshots)) {
			search_key = bkey_successor(iter, k.k->p);
			continue;
		}

		/*
		 * iter->pos should be mononotically increasing, and always be
		 * equal to the key we just returned - except extents can
		 * straddle iter->pos:
		 */
		if (!(iter->flags & BTREE_ITER_is_extents))
			iter_pos = k.k->p;
		else
			iter_pos = bkey_max(iter->pos, bkey_start_pos(k.k));

		if (unlikely(!(iter->flags & BTREE_ITER_is_extents)
			     ? bkey_gt(iter_pos, end)
			     : bkey_ge(iter_pos, end)))
			goto end;

		break;
	}

	iter->pos = iter_pos;

	iter->path = bch2_btree_path_set_pos(trans, iter->path, k.k->p,
				iter->flags & BTREE_ITER_intent,
				btree_iter_ip_allocated(iter));

	btree_path_set_should_be_locked(btree_iter_path(trans, iter));
out_no_locked:
	if (iter->update_path) {
		ret = bch2_btree_path_relock(trans, trans->paths + iter->update_path, _THIS_IP_);
		if (unlikely(ret))
			k = bkey_s_c_err(ret);
		else
			btree_path_set_should_be_locked(trans->paths + iter->update_path);
	}

	if (!(iter->flags & BTREE_ITER_all_snapshots))
		iter->pos.snapshot = iter->snapshot;

	ret = bch2_btree_iter_verify_ret(iter, k);
	if (unlikely(ret)) {
		bch2_btree_iter_set_pos(iter, iter->pos);
		k = bkey_s_c_err(ret);
	}

	bch2_btree_iter_verify_entry_exit(iter);

	return k;
end:
	bch2_btree_iter_set_pos(iter, end);
	k = bkey_s_c_null;
	goto out_no_locked;
}

/**
 * bch2_btree_iter_next() - returns first key greater than iterator's current
 * position
 * @iter:	iterator to peek from
 *
 * Returns:	key if found, or an error extractable with bkey_err().
 */
struct bkey_s_c bch2_btree_iter_next(struct btree_iter *iter)
{
	if (!bch2_btree_iter_advance(iter))
		return bkey_s_c_null;

	return bch2_btree_iter_peek(iter);
}

/**
 * bch2_btree_iter_peek_prev() - returns first key less than or equal to
 * iterator's current position
 * @iter:	iterator to peek from
 *
 * Returns:	key if found, or an error extractable with bkey_err().
 */
struct bkey_s_c bch2_btree_iter_peek_prev(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	struct bpos search_key = iter->pos;
	struct bkey_s_c k;
	struct bkey saved_k;
	const struct bch_val *saved_v;
	btree_path_idx_t saved_path = 0;
	int ret;

	bch2_trans_verify_not_unlocked(trans);
	EBUG_ON(btree_iter_path(trans, iter)->cached ||
		btree_iter_path(trans, iter)->level);

	if (iter->flags & BTREE_ITER_with_journal)
		return bkey_s_c_err(-BCH_ERR_btree_iter_with_journal_not_supported);

	bch2_btree_iter_verify(iter);
	bch2_btree_iter_verify_entry_exit(iter);

	if (iter->flags & BTREE_ITER_filter_snapshots)
		search_key.snapshot = U32_MAX;

	while (1) {
		iter->path = bch2_btree_path_set_pos(trans, iter->path, search_key,
						iter->flags & BTREE_ITER_intent,
						btree_iter_ip_allocated(iter));

		ret = bch2_btree_path_traverse(trans, iter->path, iter->flags);
		if (unlikely(ret)) {
			/* ensure that iter->k is consistent with iter->pos: */
			bch2_btree_iter_set_pos(iter, iter->pos);
			k = bkey_s_c_err(ret);
			goto out_no_locked;
		}

		struct btree_path *path = btree_iter_path(trans, iter);

		k = btree_path_level_peek(trans, path, &path->l[0], &iter->k);
		if (!k.k ||
		    ((iter->flags & BTREE_ITER_is_extents)
		     ? bpos_ge(bkey_start_pos(k.k), search_key)
		     : bpos_gt(k.k->p, search_key)))
			k = btree_path_level_prev(trans, path, &path->l[0], &iter->k);

		if (unlikely((iter->flags & BTREE_ITER_with_updates) &&
			     trans->nr_updates))
			bch2_btree_trans_peek_prev_updates(trans, iter, &k);

		if (likely(k.k)) {
			if (iter->flags & BTREE_ITER_filter_snapshots) {
				if (k.k->p.snapshot == iter->snapshot)
					goto got_key;

				/*
				 * If we have a saved candidate, and we're no
				 * longer at the same _key_ (not pos), return
				 * that candidate
				 */
				if (saved_path && !bkey_eq(k.k->p, saved_k.p)) {
					bch2_path_put_nokeep(trans, iter->path,
						      iter->flags & BTREE_ITER_intent);
					iter->path = saved_path;
					saved_path = 0;
					iter->k	= saved_k;
					k.v	= saved_v;
					goto got_key;
				}

				if (bch2_snapshot_is_ancestor(trans->c,
							      iter->snapshot,
							      k.k->p.snapshot)) {
					if (saved_path)
						bch2_path_put_nokeep(trans, saved_path,
						      iter->flags & BTREE_ITER_intent);
					saved_path = btree_path_clone(trans, iter->path,
								iter->flags & BTREE_ITER_intent,
								_THIS_IP_);
					path = btree_iter_path(trans, iter);
					saved_k = *k.k;
					saved_v = k.v;
				}

				search_key = bpos_predecessor(k.k->p);
				continue;
			}
got_key:
			if (bkey_whiteout(k.k) &&
			    !(iter->flags & BTREE_ITER_all_snapshots)) {
				search_key = bkey_predecessor(iter, k.k->p);
				if (iter->flags & BTREE_ITER_filter_snapshots)
					search_key.snapshot = U32_MAX;
				continue;
			}

			btree_path_set_should_be_locked(path);
			break;
		} else if (likely(!bpos_eq(path->l[0].b->data->min_key, POS_MIN))) {
			/* Advance to previous leaf node: */
			search_key = bpos_predecessor(path->l[0].b->data->min_key);
		} else {
			/* Start of btree: */
			bch2_btree_iter_set_pos(iter, POS_MIN);
			k = bkey_s_c_null;
			goto out_no_locked;
		}
	}

	EBUG_ON(bkey_gt(bkey_start_pos(k.k), iter->pos));

	/* Extents can straddle iter->pos: */
	if (bkey_lt(k.k->p, iter->pos))
		iter->pos = k.k->p;

	if (iter->flags & BTREE_ITER_filter_snapshots)
		iter->pos.snapshot = iter->snapshot;
out_no_locked:
	if (saved_path)
		bch2_path_put_nokeep(trans, saved_path, iter->flags & BTREE_ITER_intent);

	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);

	return k;
}

/**
 * bch2_btree_iter_prev() - returns first key less than iterator's current
 * position
 * @iter:	iterator to peek from
 *
 * Returns:	key if found, or an error extractable with bkey_err().
 */
struct bkey_s_c bch2_btree_iter_prev(struct btree_iter *iter)
{
	if (!bch2_btree_iter_rewind(iter))
		return bkey_s_c_null;

	return bch2_btree_iter_peek_prev(iter);
}

struct bkey_s_c bch2_btree_iter_peek_slot(struct btree_iter *iter)
{
	struct btree_trans *trans = iter->trans;
	struct bpos search_key;
	struct bkey_s_c k;
	int ret;

	bch2_trans_verify_not_unlocked(trans);
	bch2_btree_iter_verify(iter);
	bch2_btree_iter_verify_entry_exit(iter);
	EBUG_ON(btree_iter_path(trans, iter)->level && (iter->flags & BTREE_ITER_with_key_cache));

	/* extents can't span inode numbers: */
	if ((iter->flags & BTREE_ITER_is_extents) &&
	    unlikely(iter->pos.offset == KEY_OFFSET_MAX)) {
		if (iter->pos.inode == KEY_INODE_MAX)
			return bkey_s_c_null;

		bch2_btree_iter_set_pos(iter, bpos_nosnap_successor(iter->pos));
	}

	search_key = btree_iter_search_key(iter);
	iter->path = bch2_btree_path_set_pos(trans, iter->path, search_key,
					iter->flags & BTREE_ITER_intent,
					btree_iter_ip_allocated(iter));

	ret = bch2_btree_path_traverse(trans, iter->path, iter->flags);
	if (unlikely(ret)) {
		k = bkey_s_c_err(ret);
		goto out_no_locked;
	}

	if ((iter->flags & BTREE_ITER_cached) ||
	    !(iter->flags & (BTREE_ITER_is_extents|BTREE_ITER_filter_snapshots))) {
		k = bkey_s_c_null;

		if (unlikely((iter->flags & BTREE_ITER_with_updates) &&
			     trans->nr_updates)) {
			bch2_btree_trans_peek_slot_updates(trans, iter, &k);
			if (k.k)
				goto out;
		}

		if (unlikely(iter->flags & BTREE_ITER_with_journal) &&
		    (k = btree_trans_peek_slot_journal(trans, iter)).k)
			goto out;

		if (unlikely(iter->flags & BTREE_ITER_with_key_cache) &&
		    (k = btree_trans_peek_key_cache(iter, iter->pos)).k) {
			if (!bkey_err(k))
				iter->k = *k.k;
			/* We're not returning a key from iter->path: */
			goto out_no_locked;
		}

		k = bch2_btree_path_peek_slot(trans->paths + iter->path, &iter->k);
		if (unlikely(!k.k))
			goto out_no_locked;
	} else {
		struct bpos next;
		struct bpos end = iter->pos;

		if (iter->flags & BTREE_ITER_is_extents)
			end.offset = U64_MAX;

		EBUG_ON(btree_iter_path(trans, iter)->level);

		if (iter->flags & BTREE_ITER_intent) {
			struct btree_iter iter2;

			bch2_trans_copy_iter(&iter2, iter);
			k = bch2_btree_iter_peek_upto(&iter2, end);

			if (k.k && !bkey_err(k)) {
				swap(iter->key_cache_path, iter2.key_cache_path);
				iter->k = iter2.k;
				k.k = &iter->k;
			}
			bch2_trans_iter_exit(trans, &iter2);
		} else {
			struct bpos pos = iter->pos;

			k = bch2_btree_iter_peek_upto(iter, end);
			if (unlikely(bkey_err(k)))
				bch2_btree_iter_set_pos(iter, pos);
			else
				iter->pos = pos;
		}

		if (unlikely(bkey_err(k)))
			goto out_no_locked;

		next = k.k ? bkey_start_pos(k.k) : POS_MAX;

		if (bkey_lt(iter->pos, next)) {
			bkey_init(&iter->k);
			iter->k.p = iter->pos;

			if (iter->flags & BTREE_ITER_is_extents) {
				bch2_key_resize(&iter->k,
						min_t(u64, KEY_SIZE_MAX,
						      (next.inode == iter->pos.inode
						       ? next.offset
						       : KEY_OFFSET_MAX) -
						      iter->pos.offset));
				EBUG_ON(!iter->k.size);
			}

			k = (struct bkey_s_c) { &iter->k, NULL };
		}
	}
out:
	btree_path_set_should_be_locked(btree_iter_path(trans, iter));
out_no_locked:
	bch2_btree_iter_verify_entry_exit(iter);
	bch2_btree_iter_verify(iter);
	ret = bch2_btree_iter_verify_ret(iter, k);
	if (unlikely(ret))
		return bkey_s_c_err(ret);

	return k;
}

struct bkey_s_c bch2_btree_iter_next_slot(struct btree_iter *iter)
{
	if (!bch2_btree_iter_advance(iter))
		return bkey_s_c_null;

	return bch2_btree_iter_peek_slot(iter);
}

struct bkey_s_c bch2_btree_iter_prev_slot(struct btree_iter *iter)
{
	if (!bch2_btree_iter_rewind(iter))
		return bkey_s_c_null;

	return bch2_btree_iter_peek_slot(iter);
}

struct bkey_s_c bch2_btree_iter_peek_and_restart_outlined(struct btree_iter *iter)
{
	struct bkey_s_c k;

	while (btree_trans_too_many_iters(iter->trans) ||
	       (k = bch2_btree_iter_peek_type(iter, iter->flags),
		bch2_err_matches(bkey_err(k), BCH_ERR_transaction_restart)))
		bch2_trans_begin(iter->trans);

	return k;
}

/* new transactional stuff: */

#ifdef CONFIG_BCACHEFS_DEBUG
static void btree_trans_verify_sorted_refs(struct btree_trans *trans)
{
	struct btree_path *path;
	unsigned i;

	BUG_ON(trans->nr_sorted != bitmap_weight(trans->paths_allocated, trans->nr_paths) - 1);

	trans_for_each_path(trans, path, i) {
		BUG_ON(path->sorted_idx >= trans->nr_sorted);
		BUG_ON(trans->sorted[path->sorted_idx] != i);
	}

	for (i = 0; i < trans->nr_sorted; i++) {
		unsigned idx = trans->sorted[i];

		BUG_ON(!test_bit(idx, trans->paths_allocated));
		BUG_ON(trans->paths[idx].sorted_idx != i);
	}
}

static void btree_trans_verify_sorted(struct btree_trans *trans)
{
	struct btree_path *path, *prev = NULL;
	struct trans_for_each_path_inorder_iter iter;

	if (!bch2_debug_check_iterators)
		return;

	trans_for_each_path_inorder(trans, path, iter) {
		if (prev && btree_path_cmp(prev, path) > 0) {
			__bch2_dump_trans_paths_updates(trans, true);
			panic("trans paths out of order!\n");
		}
		prev = path;
	}
}
#else
static inline void btree_trans_verify_sorted_refs(struct btree_trans *trans) {}
static inline void btree_trans_verify_sorted(struct btree_trans *trans) {}
#endif

void __bch2_btree_trans_sort_paths(struct btree_trans *trans)
{
	int i, l = 0, r = trans->nr_sorted, inc = 1;
	bool swapped;

	btree_trans_verify_sorted_refs(trans);

	if (trans->paths_sorted)
		goto out;

	/*
	 * Cocktail shaker sort: this is efficient because iterators will be
	 * mostly sorted.
	 */
	do {
		swapped = false;

		for (i = inc > 0 ? l : r - 2;
		     i + 1 < r && i >= l;
		     i += inc) {
			if (btree_path_cmp(trans->paths + trans->sorted[i],
					   trans->paths + trans->sorted[i + 1]) > 0) {
				swap(trans->sorted[i], trans->sorted[i + 1]);
				trans->paths[trans->sorted[i]].sorted_idx = i;
				trans->paths[trans->sorted[i + 1]].sorted_idx = i + 1;
				swapped = true;
			}
		}

		if (inc > 0)
			--r;
		else
			l++;
		inc = -inc;
	} while (swapped);

	trans->paths_sorted = true;
out:
	btree_trans_verify_sorted(trans);
}

static inline void btree_path_list_remove(struct btree_trans *trans,
					  struct btree_path *path)
{
	EBUG_ON(path->sorted_idx >= trans->nr_sorted);
#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	trans->nr_sorted--;
	memmove_u64s_down_small(trans->sorted + path->sorted_idx,
				trans->sorted + path->sorted_idx + 1,
				DIV_ROUND_UP(trans->nr_sorted - path->sorted_idx,
					     sizeof(u64) / sizeof(btree_path_idx_t)));
#else
	array_remove_item(trans->sorted, trans->nr_sorted, path->sorted_idx);
#endif
	for (unsigned i = path->sorted_idx; i < trans->nr_sorted; i++)
		trans->paths[trans->sorted[i]].sorted_idx = i;
}

static inline void btree_path_list_add(struct btree_trans *trans,
				       btree_path_idx_t pos,
				       btree_path_idx_t path_idx)
{
	struct btree_path *path = trans->paths + path_idx;

	path->sorted_idx = pos ? trans->paths[pos].sorted_idx + 1 : trans->nr_sorted;

#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	memmove_u64s_up_small(trans->sorted + path->sorted_idx + 1,
			      trans->sorted + path->sorted_idx,
			      DIV_ROUND_UP(trans->nr_sorted - path->sorted_idx,
					   sizeof(u64) / sizeof(btree_path_idx_t)));
	trans->nr_sorted++;
	trans->sorted[path->sorted_idx] = path_idx;
#else
	array_insert_item(trans->sorted, trans->nr_sorted, path->sorted_idx, path_idx);
#endif

	for (unsigned i = path->sorted_idx; i < trans->nr_sorted; i++)
		trans->paths[trans->sorted[i]].sorted_idx = i;

	btree_trans_verify_sorted_refs(trans);
}

void bch2_trans_iter_exit(struct btree_trans *trans, struct btree_iter *iter)
{
	if (iter->update_path)
		bch2_path_put_nokeep(trans, iter->update_path,
			      iter->flags & BTREE_ITER_intent);
	if (iter->path)
		bch2_path_put(trans, iter->path,
			      iter->flags & BTREE_ITER_intent);
	if (iter->key_cache_path)
		bch2_path_put(trans, iter->key_cache_path,
			      iter->flags & BTREE_ITER_intent);
	iter->path		= 0;
	iter->update_path	= 0;
	iter->key_cache_path	= 0;
	iter->trans		= NULL;
}

void bch2_trans_iter_init_outlined(struct btree_trans *trans,
			  struct btree_iter *iter,
			  enum btree_id btree_id, struct bpos pos,
			  unsigned flags)
{
	bch2_trans_iter_init_common(trans, iter, btree_id, pos, 0, 0,
			       bch2_btree_iter_flags(trans, btree_id, flags),
			       _RET_IP_);
}

void bch2_trans_node_iter_init(struct btree_trans *trans,
			       struct btree_iter *iter,
			       enum btree_id btree_id,
			       struct bpos pos,
			       unsigned locks_want,
			       unsigned depth,
			       unsigned flags)
{
	flags |= BTREE_ITER_not_extents;
	flags |= BTREE_ITER_snapshot_field;
	flags |= BTREE_ITER_all_snapshots;

	bch2_trans_iter_init_common(trans, iter, btree_id, pos, locks_want, depth,
			       __bch2_btree_iter_flags(trans, btree_id, flags),
			       _RET_IP_);

	iter->min_depth	= depth;

	struct btree_path *path = btree_iter_path(trans, iter);
	BUG_ON(path->locks_want	 < min(locks_want, BTREE_MAX_DEPTH));
	BUG_ON(path->level	!= depth);
	BUG_ON(iter->min_depth	!= depth);
}

void bch2_trans_copy_iter(struct btree_iter *dst, struct btree_iter *src)
{
	struct btree_trans *trans = src->trans;

	*dst = *src;
#ifdef TRACK_PATH_ALLOCATED
	dst->ip_allocated = _RET_IP_;
#endif
	if (src->path)
		__btree_path_get(trans->paths + src->path, src->flags & BTREE_ITER_intent);
	if (src->update_path)
		__btree_path_get(trans->paths + src->update_path, src->flags & BTREE_ITER_intent);
	dst->key_cache_path = 0;
}

void *__bch2_trans_kmalloc(struct btree_trans *trans, size_t size)
{
	struct bch_fs *c = trans->c;
	unsigned new_top = trans->mem_top + size;
	unsigned old_bytes = trans->mem_bytes;
	unsigned new_bytes = roundup_pow_of_two(new_top);
	int ret;
	void *new_mem;
	void *p;

	WARN_ON_ONCE(new_bytes > BTREE_TRANS_MEM_MAX);

	struct btree_transaction_stats *s = btree_trans_stats(trans);
	s->max_mem = max(s->max_mem, new_bytes);

	if (trans->used_mempool) {
		if (trans->mem_bytes >= new_bytes)
			goto out_change_top;

		/* No more space from mempool item, need malloc new one */
		new_mem = kmalloc(new_bytes, GFP_NOWAIT|__GFP_NOWARN);
		if (unlikely(!new_mem)) {
			bch2_trans_unlock(trans);

			new_mem = kmalloc(new_bytes, GFP_KERNEL);
			if (!new_mem)
				return ERR_PTR(-BCH_ERR_ENOMEM_trans_kmalloc);

			ret = bch2_trans_relock(trans);
			if (ret) {
				kfree(new_mem);
				return ERR_PTR(ret);
			}
		}
		memcpy(new_mem, trans->mem, trans->mem_top);
		trans->used_mempool = false;
		mempool_free(trans->mem, &c->btree_trans_mem_pool);
		goto out_new_mem;
	}

	new_mem = krealloc(trans->mem, new_bytes, GFP_NOWAIT|__GFP_NOWARN);
	if (unlikely(!new_mem)) {
		bch2_trans_unlock(trans);

		new_mem = krealloc(trans->mem, new_bytes, GFP_KERNEL);
		if (!new_mem && new_bytes <= BTREE_TRANS_MEM_MAX) {
			new_mem = mempool_alloc(&c->btree_trans_mem_pool, GFP_KERNEL);
			new_bytes = BTREE_TRANS_MEM_MAX;
			memcpy(new_mem, trans->mem, trans->mem_top);
			trans->used_mempool = true;
			kfree(trans->mem);
		}

		if (!new_mem)
			return ERR_PTR(-BCH_ERR_ENOMEM_trans_kmalloc);

		trans->mem = new_mem;
		trans->mem_bytes = new_bytes;

		ret = bch2_trans_relock(trans);
		if (ret)
			return ERR_PTR(ret);
	}
out_new_mem:
	trans->mem = new_mem;
	trans->mem_bytes = new_bytes;

	if (old_bytes) {
		trace_and_count(c, trans_restart_mem_realloced, trans, _RET_IP_, new_bytes);
		return ERR_PTR(btree_trans_restart(trans, BCH_ERR_transaction_restart_mem_realloced));
	}
out_change_top:
	p = trans->mem + trans->mem_top;
	trans->mem_top += size;
	memset(p, 0, size);
	return p;
}

static inline void check_srcu_held_too_long(struct btree_trans *trans)
{
	WARN(trans->srcu_held && time_after(jiffies, trans->srcu_lock_time + HZ * 10),
	     "btree trans held srcu lock (delaying memory reclaim) for %lu seconds",
	     (jiffies - trans->srcu_lock_time) / HZ);
}

void bch2_trans_srcu_unlock(struct btree_trans *trans)
{
	if (trans->srcu_held) {
		struct bch_fs *c = trans->c;
		struct btree_path *path;
		unsigned i;

		trans_for_each_path(trans, path, i)
			if (path->cached && !btree_node_locked(path, 0))
				path->l[0].b = ERR_PTR(-BCH_ERR_no_btree_node_srcu_reset);

		check_srcu_held_too_long(trans);
		srcu_read_unlock(&c->btree_trans_barrier, trans->srcu_idx);
		trans->srcu_held = false;
	}
}

static void bch2_trans_srcu_lock(struct btree_trans *trans)
{
	if (!trans->srcu_held) {
		trans->srcu_idx = srcu_read_lock(&trans->c->btree_trans_barrier);
		trans->srcu_lock_time	= jiffies;
		trans->srcu_held = true;
	}
}

/**
 * bch2_trans_begin() - reset a transaction after a interrupted attempt
 * @trans: transaction to reset
 *
 * Returns:	current restart counter, to be used with trans_was_restarted()
 *
 * While iterating over nodes or updating nodes a attempt to lock a btree node
 * may return BCH_ERR_transaction_restart when the trylock fails. When this
 * occurs bch2_trans_begin() should be called and the transaction retried.
 */
u32 bch2_trans_begin(struct btree_trans *trans)
{
	struct btree_path *path;
	unsigned i;
	u64 now;

	bch2_trans_reset_updates(trans);

	trans->restart_count++;
	trans->mem_top			= 0;
	trans->journal_entries		= NULL;

	trans_for_each_path(trans, path, i) {
		path->should_be_locked = false;

		/*
		 * If the transaction wasn't restarted, we're presuming to be
		 * doing something new: dont keep iterators excpt the ones that
		 * are in use - except for the subvolumes btree:
		 */
		if (!trans->restarted && path->btree_id != BTREE_ID_subvolumes)
			path->preserve = false;

		/*
		 * XXX: we probably shouldn't be doing this if the transaction
		 * was restarted, but currently we still overflow transaction
		 * iterators if we do that
		 */
		if (!path->ref && !path->preserve)
			__bch2_path_free(trans, i);
		else
			path->preserve = false;
	}

	now = local_clock();

	if (!IS_ENABLED(CONFIG_BCACHEFS_NO_LATENCY_ACCT) &&
	    time_after64(now, trans->last_begin_time + 10))
		__bch2_time_stats_update(&btree_trans_stats(trans)->duration,
					 trans->last_begin_time, now);

	if (!trans->restarted &&
	    (need_resched() ||
	     time_after64(now, trans->last_begin_time + BTREE_TRANS_MAX_LOCK_HOLD_TIME_NS))) {
		bch2_trans_unlock(trans);
		cond_resched();
		now = local_clock();
	}
	trans->last_begin_time = now;

	if (unlikely(trans->srcu_held &&
		     time_after(jiffies, trans->srcu_lock_time + msecs_to_jiffies(10))))
		bch2_trans_srcu_unlock(trans);

	trans->last_begin_ip = _RET_IP_;

	trans_set_locked(trans);

	if (trans->restarted) {
		bch2_btree_path_traverse_all(trans);
		trans->notrace_relock_fail = false;
	}

	bch2_trans_verify_not_unlocked(trans);
	return trans->restart_count;
}

const char *bch2_btree_transaction_fns[BCH_TRANSACTIONS_NR] = { "(unknown)" };

unsigned bch2_trans_get_fn_idx(const char *fn)
{
	for (unsigned i = 0; i < ARRAY_SIZE(bch2_btree_transaction_fns); i++)
		if (!bch2_btree_transaction_fns[i] ||
		    bch2_btree_transaction_fns[i] == fn) {
			bch2_btree_transaction_fns[i] = fn;
			return i;
		}

	pr_warn_once("BCH_TRANSACTIONS_NR not big enough!");
	return 0;
}

struct btree_trans *__bch2_trans_get(struct bch_fs *c, unsigned fn_idx)
	__acquires(&c->btree_trans_barrier)
{
	struct btree_trans *trans;

	if (IS_ENABLED(__KERNEL__)) {
		trans = this_cpu_xchg(c->btree_trans_bufs->trans, NULL);
		if (trans) {
			memset(trans, 0, offsetof(struct btree_trans, list));
			goto got_trans;
		}
	}

	trans = mempool_alloc(&c->btree_trans_pool, GFP_NOFS);
	memset(trans, 0, sizeof(*trans));

	seqmutex_lock(&c->btree_trans_lock);
	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG)) {
		struct btree_trans *pos;
		pid_t pid = current->pid;

		trans->locking_wait.task = current;

		list_for_each_entry(pos, &c->btree_trans_list, list) {
			struct task_struct *pos_task = READ_ONCE(pos->locking_wait.task);
			/*
			 * We'd much prefer to be stricter here and completely
			 * disallow multiple btree_trans in the same thread -
			 * but the data move path calls bch2_write when we
			 * already have a btree_trans initialized.
			 */
			BUG_ON(pos_task &&
			       pid == pos_task->pid &&
			       pos->locked);
		}
	}

	list_add(&trans->list, &c->btree_trans_list);
	seqmutex_unlock(&c->btree_trans_lock);
got_trans:
	trans->c		= c;
	trans->last_begin_time	= local_clock();
	trans->fn_idx		= fn_idx;
	trans->locking_wait.task = current;
	trans->journal_replay_not_finished =
		unlikely(!test_bit(JOURNAL_replay_done, &c->journal.flags)) &&
		atomic_inc_not_zero(&c->journal_keys.ref);
	trans->nr_paths		= ARRAY_SIZE(trans->_paths);
	trans->paths_allocated	= trans->_paths_allocated;
	trans->sorted		= trans->_sorted;
	trans->paths		= trans->_paths;
	trans->updates		= trans->_updates;

	*trans_paths_nr(trans->paths) = BTREE_ITER_INITIAL;

	trans->paths_allocated[0] = 1;

	static struct lock_class_key lockdep_key;
	lockdep_init_map(&trans->dep_map, "bcachefs_btree", &lockdep_key, 0);

	if (fn_idx < BCH_TRANSACTIONS_NR) {
		trans->fn = bch2_btree_transaction_fns[fn_idx];

		struct btree_transaction_stats *s = &c->btree_transaction_stats[fn_idx];

		if (s->max_mem) {
			unsigned expected_mem_bytes = roundup_pow_of_two(s->max_mem);

			trans->mem = kmalloc(expected_mem_bytes, GFP_KERNEL);
			if (likely(trans->mem))
				trans->mem_bytes = expected_mem_bytes;
		}

		trans->nr_paths_max = s->nr_max_paths;
		trans->journal_entries_size = s->journal_entries_size;
	}

	trans->srcu_idx		= srcu_read_lock(&c->btree_trans_barrier);
	trans->srcu_lock_time	= jiffies;
	trans->srcu_held	= true;
	trans_set_locked(trans);

	closure_init_stack_release(&trans->ref);
	return trans;
}

static void check_btree_paths_leaked(struct btree_trans *trans)
{
#ifdef CONFIG_BCACHEFS_DEBUG
	struct bch_fs *c = trans->c;
	struct btree_path *path;
	unsigned i;

	trans_for_each_path(trans, path, i)
		if (path->ref)
			goto leaked;
	return;
leaked:
	bch_err(c, "btree paths leaked from %s!", trans->fn);
	trans_for_each_path(trans, path, i)
		if (path->ref)
			printk(KERN_ERR "  btree %s %pS\n",
			       bch2_btree_id_str(path->btree_id),
			       (void *) path->ip_allocated);
	/* Be noisy about this: */
	bch2_fatal_error(c);
#endif
}

void bch2_trans_put(struct btree_trans *trans)
	__releases(&c->btree_trans_barrier)
{
	struct bch_fs *c = trans->c;

	bch2_trans_unlock(trans);

	trans_for_each_update(trans, i)
		__btree_path_put(trans->paths + i->path, true);
	trans->nr_updates	= 0;

	check_btree_paths_leaked(trans);

	if (trans->srcu_held) {
		check_srcu_held_too_long(trans);
		srcu_read_unlock(&c->btree_trans_barrier, trans->srcu_idx);
	}

	if (unlikely(trans->journal_replay_not_finished))
		bch2_journal_keys_put(c);

	/*
	 * trans->ref protects trans->locking_wait.task, btree_paths array; used
	 * by cycle detector
	 */
	closure_return_sync(&trans->ref);
	trans->locking_wait.task = NULL;

	unsigned long *paths_allocated = trans->paths_allocated;
	trans->paths_allocated	= NULL;
	trans->paths		= NULL;

	if (paths_allocated != trans->_paths_allocated)
		kvfree_rcu_mightsleep(paths_allocated);

	if (trans->used_mempool)
		mempool_free(trans->mem, &c->btree_trans_mem_pool);
	else
		kfree(trans->mem);

	/* Userspace doesn't have a real percpu implementation: */
	if (IS_ENABLED(__KERNEL__))
		trans = this_cpu_xchg(c->btree_trans_bufs->trans, trans);

	if (trans) {
		seqmutex_lock(&c->btree_trans_lock);
		list_del(&trans->list);
		seqmutex_unlock(&c->btree_trans_lock);

		mempool_free(trans, &c->btree_trans_pool);
	}
}

bool bch2_current_has_btree_trans(struct bch_fs *c)
{
	seqmutex_lock(&c->btree_trans_lock);
	struct btree_trans *trans;
	bool ret = false;
	list_for_each_entry(trans, &c->btree_trans_list, list)
		if (trans->locking_wait.task == current &&
		    trans->locked) {
			ret = true;
			break;
		}
	seqmutex_unlock(&c->btree_trans_lock);
	return ret;
}

static void __maybe_unused
bch2_btree_bkey_cached_common_to_text(struct printbuf *out,
				      struct btree_bkey_cached_common *b)
{
	struct six_lock_count c = six_lock_counts(&b->lock);
	struct task_struct *owner;
	pid_t pid;

	rcu_read_lock();
	owner = READ_ONCE(b->lock.owner);
	pid = owner ? owner->pid : 0;
	rcu_read_unlock();

	prt_printf(out, "\t%px %c l=%u %s:", b, b->cached ? 'c' : 'b',
		   b->level, bch2_btree_id_str(b->btree_id));
	bch2_bpos_to_text(out, btree_node_pos(b));

	prt_printf(out, "\t locks %u:%u:%u held by pid %u",
		   c.n[0], c.n[1], c.n[2], pid);
}

void bch2_btree_trans_to_text(struct printbuf *out, struct btree_trans *trans)
{
	struct btree_bkey_cached_common *b;
	static char lock_types[] = { 'r', 'i', 'w' };
	struct task_struct *task = READ_ONCE(trans->locking_wait.task);
	unsigned l, idx;

	/* before rcu_read_lock(): */
	bch2_printbuf_make_room(out, 4096);

	if (!out->nr_tabstops) {
		printbuf_tabstop_push(out, 16);
		printbuf_tabstop_push(out, 32);
	}

	prt_printf(out, "%i %s\n", task ? task->pid : 0, trans->fn);

	/* trans->paths is rcu protected vs. freeing */
	rcu_read_lock();
	out->atomic++;

	struct btree_path *paths = rcu_dereference(trans->paths);
	if (!paths)
		goto out;

	unsigned long *paths_allocated = trans_paths_allocated(paths);

	trans_for_each_path_idx_from(paths_allocated, *trans_paths_nr(paths), idx, 1) {
		struct btree_path *path = paths + idx;
		if (!path->nodes_locked)
			continue;

		prt_printf(out, "  path %u %c l=%u %s:",
		       idx,
		       path->cached ? 'c' : 'b',
		       path->level,
		       bch2_btree_id_str(path->btree_id));
		bch2_bpos_to_text(out, path->pos);
		prt_newline(out);

		for (l = 0; l < BTREE_MAX_DEPTH; l++) {
			if (btree_node_locked(path, l) &&
			    !IS_ERR_OR_NULL(b = (void *) READ_ONCE(path->l[l].b))) {
				prt_printf(out, "    %c l=%u ",
					   lock_types[btree_node_locked_type(path, l)], l);
				bch2_btree_bkey_cached_common_to_text(out, b);
				prt_newline(out);
			}
		}
	}

	b = READ_ONCE(trans->locking);
	if (b) {
		prt_printf(out, "  blocked for %lluus on\n",
			   div_u64(local_clock() - trans->locking_wait.start_time, 1000));
		prt_printf(out, "    %c", lock_types[trans->locking_wait.lock_want]);
		bch2_btree_bkey_cached_common_to_text(out, b);
		prt_newline(out);
	}
out:
	--out->atomic;
	rcu_read_unlock();
}

void bch2_fs_btree_iter_exit(struct bch_fs *c)
{
	struct btree_transaction_stats *s;
	struct btree_trans *trans;
	int cpu;

	if (c->btree_trans_bufs)
		for_each_possible_cpu(cpu) {
			struct btree_trans *trans =
				per_cpu_ptr(c->btree_trans_bufs, cpu)->trans;

			if (trans) {
				seqmutex_lock(&c->btree_trans_lock);
				list_del(&trans->list);
				seqmutex_unlock(&c->btree_trans_lock);
			}
			kfree(trans);
		}
	free_percpu(c->btree_trans_bufs);

	trans = list_first_entry_or_null(&c->btree_trans_list, struct btree_trans, list);
	if (trans)
		panic("%s leaked btree_trans\n", trans->fn);

	for (s = c->btree_transaction_stats;
	     s < c->btree_transaction_stats + ARRAY_SIZE(c->btree_transaction_stats);
	     s++) {
		kfree(s->max_paths_text);
		bch2_time_stats_exit(&s->lock_hold_times);
	}

	if (c->btree_trans_barrier_initialized) {
		synchronize_srcu_expedited(&c->btree_trans_barrier);
		cleanup_srcu_struct(&c->btree_trans_barrier);
	}
	mempool_exit(&c->btree_trans_mem_pool);
	mempool_exit(&c->btree_trans_pool);
}

void bch2_fs_btree_iter_init_early(struct bch_fs *c)
{
	struct btree_transaction_stats *s;

	for (s = c->btree_transaction_stats;
	     s < c->btree_transaction_stats + ARRAY_SIZE(c->btree_transaction_stats);
	     s++) {
		bch2_time_stats_init(&s->duration);
		bch2_time_stats_init(&s->lock_hold_times);
		mutex_init(&s->lock);
	}

	INIT_LIST_HEAD(&c->btree_trans_list);
	seqmutex_init(&c->btree_trans_lock);
}

int bch2_fs_btree_iter_init(struct bch_fs *c)
{
	int ret;

	c->btree_trans_bufs = alloc_percpu(struct btree_trans_buf);
	if (!c->btree_trans_bufs)
		return -ENOMEM;

	ret   = mempool_init_kmalloc_pool(&c->btree_trans_pool, 1,
					  sizeof(struct btree_trans)) ?:
		mempool_init_kmalloc_pool(&c->btree_trans_mem_pool, 1,
					  BTREE_TRANS_MEM_MAX) ?:
		init_srcu_struct(&c->btree_trans_barrier);
	if (ret)
		return ret;

	/*
	 * static annotation (hackily done) for lock ordering of reclaim vs.
	 * btree node locks:
	 */
#ifdef CONFIG_LOCKDEP
	fs_reclaim_acquire(GFP_KERNEL);
	struct btree_trans *trans = bch2_trans_get(c);
	trans_set_locked(trans);
	bch2_trans_put(trans);
	fs_reclaim_release(GFP_KERNEL);
#endif

	c->btree_trans_barrier_initialized = true;
	return 0;

}
