// SPDX-License-Identifier: GPL-2.0

#include "bcachefs.h"
#include "bkey_methods.h"
#include "btree_cache.h"
#include "btree_iter.h"
#include "btree_locking.h"
#include "debug.h"
#include "extents.h"
#include "trace.h"

#include <linux/prefetch.h>

static inline struct bkey_s_c __btree_iter_peek_all(struct btree_iter *,
						    struct btree_iter_level *,
						    struct bkey *);

#define BTREE_ITER_NO_NODE_GET_LOCKS	((struct btree *) 1)
#define BTREE_ITER_NO_NODE_DROP		((struct btree *) 2)
#define BTREE_ITER_NO_NODE_LOCK_ROOT	((struct btree *) 3)
#define BTREE_ITER_NO_NODE_UP		((struct btree *) 4)
#define BTREE_ITER_NO_NODE_DOWN		((struct btree *) 5)
#define BTREE_ITER_NO_NODE_INIT		((struct btree *) 6)
#define BTREE_ITER_NO_NODE_ERROR	((struct btree *) 7)

static inline bool is_btree_node(struct btree_iter *iter, unsigned l)
{
	return l < BTREE_MAX_DEPTH &&
		(unsigned long) iter->l[l].b >= 128;
}

/* Returns < 0 if @k is before iter pos, > 0 if @k is after */
static inline int __btree_iter_pos_cmp(struct btree_iter *iter,
				       const struct btree *b,
				       const struct bkey_packed *k,
				       bool interior_node)
{
	int cmp = bkey_cmp_left_packed(b, k, &iter->pos);

	if (cmp)
		return cmp;
	if (bkey_deleted(k))
		return -1;

	/*
	 * Normally, for extents we want the first key strictly greater than
	 * the iterator position - with the exception that for interior nodes,
	 * we don't want to advance past the last key if the iterator position
	 * is POS_MAX:
	 */
	if (iter->flags & BTREE_ITER_IS_EXTENTS &&
	    (!interior_node ||
	     bkey_cmp_left_packed_byval(b, k, POS_MAX)))
		return -1;
	return 1;
}

static inline int btree_iter_pos_cmp(struct btree_iter *iter,
				     const struct btree *b,
				     const struct bkey_packed *k)
{
	return __btree_iter_pos_cmp(iter, b, k, b->c.level != 0);
}

/* Btree node locking: */

/*
 * Updates the saved lock sequence number, so that bch2_btree_node_relock() will
 * succeed:
 */
void bch2_btree_node_unlock_write(struct btree *b, struct btree_iter *iter)
{
	struct btree_iter *linked;

	EBUG_ON(iter->l[b->c.level].b != b);
	EBUG_ON(iter->l[b->c.level].lock_seq + 1 != b->c.lock.state.seq);

	trans_for_each_iter_with_node(iter->trans, b, linked)
		linked->l[b->c.level].lock_seq += 2;

	six_unlock_write(&b->c.lock);
}

void __bch2_btree_node_lock_write(struct btree *b, struct btree_iter *iter)
{
	struct btree_iter *linked;
	unsigned readers = 0;

	EBUG_ON(!btree_node_intent_locked(iter, b->c.level));

	trans_for_each_iter(iter->trans, linked)
		if (linked->l[b->c.level].b == b &&
		    btree_node_read_locked(linked, b->c.level))
			readers++;

	/*
	 * Must drop our read locks before calling six_lock_write() -
	 * six_unlock() won't do wakeups until the reader count
	 * goes to 0, and it's safe because we have the node intent
	 * locked:
	 */
	atomic64_sub(__SIX_VAL(read_lock, readers),
		     &b->c.lock.state.counter);
	btree_node_lock_type(iter->trans->c, b, SIX_LOCK_write);
	atomic64_add(__SIX_VAL(read_lock, readers),
		     &b->c.lock.state.counter);
}

bool __bch2_btree_node_relock(struct btree_iter *iter, unsigned level)
{
	struct btree *b = btree_iter_node(iter, level);
	int want = __btree_lock_want(iter, level);

	if (!is_btree_node(iter, level))
		return false;

	if (race_fault())
		return false;

	if (six_relock_type(&b->c.lock, want, iter->l[level].lock_seq) ||
	    (btree_node_lock_seq_matches(iter, b, level) &&
	     btree_node_lock_increment(iter, b, level, want))) {
		mark_btree_node_locked(iter, level, want);
		return true;
	} else {
		return false;
	}
}

static bool bch2_btree_node_upgrade(struct btree_iter *iter, unsigned level)
{
	struct btree *b = iter->l[level].b;

	EBUG_ON(btree_lock_want(iter, level) != BTREE_NODE_INTENT_LOCKED);

	if (!is_btree_node(iter, level))
		return false;

	if (btree_node_intent_locked(iter, level))
		return true;

	if (race_fault())
		return false;

	if (btree_node_locked(iter, level)
	    ? six_lock_tryupgrade(&b->c.lock)
	    : six_relock_type(&b->c.lock, SIX_LOCK_intent, iter->l[level].lock_seq))
		goto success;

	if (btree_node_lock_seq_matches(iter, b, level) &&
	    btree_node_lock_increment(iter, b, level, BTREE_NODE_INTENT_LOCKED)) {
		btree_node_unlock(iter, level);
		goto success;
	}

	return false;
success:
	mark_btree_node_intent_locked(iter, level);
	return true;
}

static inline bool btree_iter_get_locks(struct btree_iter *iter,
					bool upgrade, bool trace)
{
	unsigned l = iter->level;
	int fail_idx = -1;

	do {
		if (!btree_iter_node(iter, l))
			break;

		if (!(upgrade
		      ? bch2_btree_node_upgrade(iter, l)
		      : bch2_btree_node_relock(iter, l))) {
			if (trace)
				(upgrade
				 ? trace_node_upgrade_fail
				 : trace_node_relock_fail)(l, iter->l[l].lock_seq,
						is_btree_node(iter, l)
						? 0
						: (unsigned long) iter->l[l].b,
						is_btree_node(iter, l)
						? iter->l[l].b->c.lock.state.seq
						: 0);

			fail_idx = l;
			btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
		}

		l++;
	} while (l < iter->locks_want);

	/*
	 * When we fail to get a lock, we have to ensure that any child nodes
	 * can't be relocked so bch2_btree_iter_traverse has to walk back up to
	 * the node that we failed to relock:
	 */
	while (fail_idx >= 0) {
		btree_node_unlock(iter, fail_idx);
		iter->l[fail_idx].b = BTREE_ITER_NO_NODE_GET_LOCKS;
		--fail_idx;
	}

	if (iter->uptodate == BTREE_ITER_NEED_RELOCK)
		iter->uptodate = BTREE_ITER_NEED_PEEK;

	bch2_btree_trans_verify_locks(iter->trans);

	return iter->uptodate < BTREE_ITER_NEED_RELOCK;
}

/* Slowpath: */
bool __bch2_btree_node_lock(struct btree *b, struct bpos pos,
			   unsigned level,
			   struct btree_iter *iter,
			   enum six_lock_type type)
{
	struct btree_iter *linked;
	bool ret = true;

	/* Check if it's safe to block: */
	trans_for_each_iter(iter->trans, linked) {
		if (!linked->nodes_locked)
			continue;

		/* * Must lock btree nodes in key order: */
		if (__btree_iter_cmp(iter->btree_id, pos, linked) < 0)
			ret = false;

		/*
		 * Can't block taking an intent lock if we have _any_ nodes read
		 * locked:
		 *
		 * - Our read lock blocks another thread with an intent lock on
		 *   the same node from getting a write lock, and thus from
		 *   dropping its intent lock
		 *
		 * - And the other thread may have multiple nodes intent locked:
		 *   both the node we want to intent lock, and the node we
		 *   already have read locked - deadlock:
		 */
		if (type == SIX_LOCK_intent &&
		    linked->nodes_locked != linked->nodes_intent_locked) {
			if (!(iter->trans->nounlock)) {
				linked->locks_want = max_t(unsigned,
						linked->locks_want,
						__fls(linked->nodes_locked) + 1);
				btree_iter_get_locks(linked, true, false);
			}
			ret = false;
		}

		/*
		 * Interior nodes must be locked before their descendants: if
		 * another iterator has possible descendants locked of the node
		 * we're about to lock, it must have the ancestors locked too:
		 */
		if (linked->btree_id == iter->btree_id &&
		    level > __fls(linked->nodes_locked)) {
			if (!(iter->trans->nounlock)) {
				linked->locks_want =
					max(level + 1, max_t(unsigned,
					    linked->locks_want,
					    iter->locks_want));
				btree_iter_get_locks(linked, true, false);
			}
			ret = false;
		}
	}

	if (unlikely(!ret)) {
		trace_trans_restart_would_deadlock(iter->trans->ip);
		return false;
	}

	__btree_node_lock_type(iter->trans->c, b, type);
	return true;
}

/* Btree iterator locking: */

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_btree_iter_verify_locks(struct btree_iter *iter)
{
	unsigned l;

	for (l = 0; btree_iter_node(iter, l); l++) {
		if (iter->uptodate >= BTREE_ITER_NEED_RELOCK &&
		    !btree_node_locked(iter, l))
			continue;

		BUG_ON(btree_lock_want(iter, l) !=
		       btree_node_locked_type(iter, l));
	}
}

void bch2_btree_trans_verify_locks(struct btree_trans *trans)
{
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		bch2_btree_iter_verify_locks(iter);
}
#endif

__flatten
static bool bch2_btree_iter_relock(struct btree_iter *iter, bool trace)
{
	return iter->uptodate >= BTREE_ITER_NEED_RELOCK
		? btree_iter_get_locks(iter, false, trace)
		: true;
}

bool __bch2_btree_iter_upgrade(struct btree_iter *iter,
			       unsigned new_locks_want)
{
	struct btree_iter *linked;

	EBUG_ON(iter->locks_want >= new_locks_want);

	iter->locks_want = new_locks_want;

	if (btree_iter_get_locks(iter, true, true))
		return true;

	/*
	 * Ancestor nodes must be locked before child nodes, so set locks_want
	 * on iterators that might lock ancestors before us to avoid getting
	 * -EINTR later:
	 */
	trans_for_each_iter(iter->trans, linked)
		if (linked != iter &&
		    linked->btree_id == iter->btree_id &&
		    linked->locks_want < new_locks_want) {
			linked->locks_want = new_locks_want;
			btree_iter_get_locks(linked, true, false);
		}

	return false;
}

bool __bch2_btree_iter_upgrade_nounlock(struct btree_iter *iter,
					unsigned new_locks_want)
{
	unsigned l = iter->level;

	EBUG_ON(iter->locks_want >= new_locks_want);

	iter->locks_want = new_locks_want;

	do {
		if (!btree_iter_node(iter, l))
			break;

		if (!bch2_btree_node_upgrade(iter, l)) {
			iter->locks_want = l;
			return false;
		}

		l++;
	} while (l < iter->locks_want);

	return true;
}

void __bch2_btree_iter_downgrade(struct btree_iter *iter,
				 unsigned downgrade_to)
{
	struct btree_iter *linked;
	unsigned l;

	/*
	 * We downgrade linked iterators as well because btree_iter_upgrade
	 * might have had to modify locks_want on linked iterators due to lock
	 * ordering:
	 */
	trans_for_each_iter(iter->trans, linked) {
		unsigned new_locks_want = downgrade_to ?:
			(linked->flags & BTREE_ITER_INTENT ? 1 : 0);

		if (linked->locks_want <= new_locks_want)
			continue;

		linked->locks_want = new_locks_want;

		while (linked->nodes_locked &&
		       (l = __fls(linked->nodes_locked)) >= linked->locks_want) {
			if (l > linked->level) {
				btree_node_unlock(linked, l);
			} else {
				if (btree_node_intent_locked(linked, l)) {
					six_lock_downgrade(&linked->l[l].b->c.lock);
					linked->nodes_intent_locked ^= 1 << l;
				}
				break;
			}
		}
	}

	bch2_btree_trans_verify_locks(iter->trans);
}

/* Btree transaction locking: */

bool bch2_trans_relock(struct btree_trans *trans)
{
	struct btree_iter *iter;
	bool ret = true;

	trans_for_each_iter(trans, iter)
		if (iter->uptodate == BTREE_ITER_NEED_RELOCK)
			ret &= bch2_btree_iter_relock(iter, true);

	return ret;
}

void bch2_trans_unlock(struct btree_trans *trans)
{
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		__bch2_btree_iter_unlock(iter);
}

/* Btree iterator: */

#ifdef CONFIG_BCACHEFS_DEBUG

static void __bch2_btree_iter_verify(struct btree_iter *iter,
				     struct btree *b)
{
	struct btree_iter_level *l = &iter->l[b->c.level];
	struct btree_node_iter tmp = l->iter;
	struct bkey_packed *k;

	if (!debug_check_iterators(iter->trans->c))
		return;

	if (iter->uptodate > BTREE_ITER_NEED_PEEK)
		return;

	bch2_btree_node_iter_verify(&l->iter, b);

	/*
	 * For interior nodes, the iterator will have skipped past
	 * deleted keys:
	 *
	 * For extents, the iterator may have skipped past deleted keys (but not
	 * whiteouts)
	 */
	k = b->c.level || iter->flags & BTREE_ITER_IS_EXTENTS
		? bch2_btree_node_iter_prev_filter(&tmp, b, KEY_TYPE_discard)
		: bch2_btree_node_iter_prev_all(&tmp, b);
	if (k && btree_iter_pos_cmp(iter, b, k) > 0) {
		char buf[100];
		struct bkey uk = bkey_unpack_key(b, k);

		bch2_bkey_to_text(&PBUF(buf), &uk);
		panic("prev key should be before iter pos:\n%s\n%llu:%llu\n",
		      buf, iter->pos.inode, iter->pos.offset);
	}

	k = bch2_btree_node_iter_peek_all(&l->iter, b);
	if (k && btree_iter_pos_cmp(iter, b, k) < 0) {
		char buf[100];
		struct bkey uk = bkey_unpack_key(b, k);

		bch2_bkey_to_text(&PBUF(buf), &uk);
		panic("iter should be after current key:\n"
		      "iter pos %llu:%llu\n"
		      "cur key  %s\n",
		      iter->pos.inode, iter->pos.offset, buf);
	}

	BUG_ON(iter->uptodate == BTREE_ITER_UPTODATE &&
	       btree_iter_type(iter) == BTREE_ITER_KEYS &&
	       !bkey_whiteout(&iter->k) &&
	       bch2_btree_node_iter_end(&l->iter));
}

void bch2_btree_iter_verify(struct btree_iter *iter, struct btree *b)
{
	struct btree_iter *linked;

	if (!debug_check_iterators(iter->trans->c))
		return;

	trans_for_each_iter_with_node(iter->trans, b, linked)
		__bch2_btree_iter_verify(linked, b);
}

#else

static inline void __bch2_btree_iter_verify(struct btree_iter *iter,
					    struct btree *b) {}

#endif

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

static void __bch2_btree_node_iter_fix(struct btree_iter *iter,
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
	    btree_iter_pos_cmp(iter, b, where) > 0) {
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
	    btree_iter_pos_cmp(iter, b, where) > 0) {
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
	    (b->c.level ||
	     (iter->flags & BTREE_ITER_IS_EXTENTS))) {
		struct bset_tree *t;
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

	if (!b->c.level &&
	    node_iter == &iter->l[0].iter &&
	    iter_current_key_modified) {
		struct bkey_packed *k =
			bch2_btree_node_iter_peek_all(node_iter, b);

		if (likely(k)) {
			bkey_disassemble(b, k, &iter->k);
		} else {
			/* XXX: for extents, calculate size of hole? */
			iter->k.type = KEY_TYPE_deleted;
		}

		btree_iter_set_dirty(iter, BTREE_ITER_NEED_PEEK);
	}
}

void bch2_btree_node_iter_fix(struct btree_iter *iter,
			      struct btree *b,
			      struct btree_node_iter *node_iter,
			      struct bkey_packed *where,
			      unsigned clobber_u64s,
			      unsigned new_u64s)
{
	struct bset_tree *t = bch2_bkey_to_bset_inlined(b, where);
	struct btree_iter *linked;

	if (node_iter != &iter->l[b->c.level].iter) {
		__bch2_btree_node_iter_fix(iter, b, node_iter, t,
					   where, clobber_u64s, new_u64s);
		bch2_btree_node_iter_verify(node_iter, b);
	}

	trans_for_each_iter_with_node(iter->trans, b, linked) {
		__bch2_btree_node_iter_fix(linked, b,
					   &linked->l[b->c.level].iter, t,
					   where, clobber_u64s, new_u64s);
		__bch2_btree_iter_verify(linked, b);
	}
}

static inline struct bkey_s_c __btree_iter_unpack(struct btree_iter *iter,
						  struct btree_iter_level *l,
						  struct bkey *u,
						  struct bkey_packed *k)
{
	struct bkey_s_c ret;

	if (unlikely(!k)) {
		/*
		 * signal to bch2_btree_iter_peek_slot() that we're currently at
		 * a hole
		 */
		u->type = KEY_TYPE_deleted;
		return bkey_s_c_null;
	}

	ret = bkey_disassemble(l->b, k, u);

	if (debug_check_bkeys(iter->trans->c))
		bch2_bkey_debugcheck(iter->trans->c, l->b, ret);

	return ret;
}

/* peek_all() doesn't skip deleted keys */
static inline struct bkey_s_c __btree_iter_peek_all(struct btree_iter *iter,
						    struct btree_iter_level *l,
						    struct bkey *u)
{
	return __btree_iter_unpack(iter, l, u,
			bch2_btree_node_iter_peek_all(&l->iter, l->b));
}

static inline struct bkey_s_c __btree_iter_peek(struct btree_iter *iter,
						struct btree_iter_level *l)
{
	return __btree_iter_unpack(iter, l, &iter->k,
			bch2_btree_node_iter_peek(&l->iter, l->b));
}

static inline struct bkey_s_c __btree_iter_prev(struct btree_iter *iter,
						struct btree_iter_level *l)
{
	return __btree_iter_unpack(iter, l, &iter->k,
			bch2_btree_node_iter_prev(&l->iter, l->b));
}

static inline bool btree_iter_advance_to_pos(struct btree_iter *iter,
					     struct btree_iter_level *l,
					     int max_advance)
{
	struct bkey_packed *k;
	int nr_advanced = 0;

	while ((k = bch2_btree_node_iter_peek_all(&l->iter, l->b)) &&
	       btree_iter_pos_cmp(iter, l->b, k) < 0) {
		if (max_advance > 0 && nr_advanced >= max_advance)
			return false;

		bch2_btree_node_iter_advance(&l->iter, l->b);
		nr_advanced++;
	}

	return true;
}

/*
 * Verify that iterator for parent node points to child node:
 */
static void btree_iter_verify_new_node(struct btree_iter *iter, struct btree *b)
{
	struct btree_iter_level *l;
	unsigned plevel;
	bool parent_locked;
	struct bkey_packed *k;

	if (!IS_ENABLED(CONFIG_BCACHEFS_DEBUG))
		return;

	plevel = b->c.level + 1;
	if (!btree_iter_node(iter, plevel))
		return;

	parent_locked = btree_node_locked(iter, plevel);

	if (!bch2_btree_node_relock(iter, plevel))
		return;

	l = &iter->l[plevel];
	k = bch2_btree_node_iter_peek_all(&l->iter, l->b);
	if (!k ||
	    bkey_deleted(k) ||
	    bkey_cmp_left_packed(l->b, k, &b->key.k.p)) {
		char buf[100];
		struct bkey uk = bkey_unpack_key(b, k);

		bch2_bkey_to_text(&PBUF(buf), &uk);
		panic("parent iter doesn't point to new node:\n%s\n%llu:%llu\n",
		      buf, b->key.k.p.inode, b->key.k.p.offset);
	}

	if (!parent_locked)
		btree_node_unlock(iter, b->c.level + 1);
}

static inline bool btree_iter_pos_before_node(struct btree_iter *iter,
					      struct btree *b)
{
	return bkey_cmp(iter->pos, b->data->min_key) < 0;
}

static inline bool btree_iter_pos_after_node(struct btree_iter *iter,
					     struct btree *b)
{
	int cmp = bkey_cmp(b->key.k.p, iter->pos);

	if (!cmp &&
	    (iter->flags & BTREE_ITER_IS_EXTENTS) &&
	    bkey_cmp(b->key.k.p, POS_MAX))
		cmp = -1;
	return cmp < 0;
}

static inline bool btree_iter_pos_in_node(struct btree_iter *iter,
					  struct btree *b)
{
	return iter->btree_id == b->c.btree_id &&
		!btree_iter_pos_before_node(iter, b) &&
		!btree_iter_pos_after_node(iter, b);
}

static inline void __btree_iter_init(struct btree_iter *iter,
				     unsigned level)
{
	struct btree_iter_level *l = &iter->l[level];

	bch2_btree_node_iter_init(&l->iter, l->b, &iter->pos);

	if (iter->flags & BTREE_ITER_IS_EXTENTS)
		btree_iter_advance_to_pos(iter, l, -1);

	/* Skip to first non whiteout: */
	if (level)
		bch2_btree_node_iter_peek(&l->iter, l->b);

	btree_iter_set_dirty(iter, BTREE_ITER_NEED_PEEK);
}

static inline void btree_iter_node_set(struct btree_iter *iter,
				       struct btree *b)
{
	btree_iter_verify_new_node(iter, b);

	EBUG_ON(!btree_iter_pos_in_node(iter, b));
	EBUG_ON(b->c.lock.state.seq & 1);

	iter->l[b->c.level].lock_seq = b->c.lock.state.seq;
	iter->l[b->c.level].b = b;
	__btree_iter_init(iter, b->c.level);
}

/*
 * A btree node is being replaced - update the iterator to point to the new
 * node:
 */
void bch2_btree_iter_node_replace(struct btree_iter *iter, struct btree *b)
{
	enum btree_node_locked_type t;
	struct btree_iter *linked;

	trans_for_each_iter(iter->trans, linked)
		if (btree_iter_pos_in_node(linked, b)) {
			/*
			 * bch2_btree_iter_node_drop() has already been called -
			 * the old node we're replacing has already been
			 * unlocked and the pointer invalidated
			 */
			BUG_ON(btree_node_locked(linked, b->c.level));

			t = btree_lock_want(linked, b->c.level);
			if (t != BTREE_NODE_UNLOCKED) {
				six_lock_increment(&b->c.lock, (enum six_lock_type) t);
				mark_btree_node_locked(linked, b->c.level, (enum six_lock_type) t);
			}

			btree_iter_node_set(linked, b);
		}

	six_unlock_intent(&b->c.lock);
}

void bch2_btree_iter_node_drop(struct btree_iter *iter, struct btree *b)
{
	struct btree_iter *linked;
	unsigned level = b->c.level;

	trans_for_each_iter(iter->trans, linked)
		if (linked->l[level].b == b) {
			__btree_node_unlock(linked, level);
			linked->l[level].b = BTREE_ITER_NO_NODE_DROP;
		}
}

/*
 * A btree node has been modified in such a way as to invalidate iterators - fix
 * them:
 */
void bch2_btree_iter_reinit_node(struct btree_iter *iter, struct btree *b)
{
	struct btree_iter *linked;

	trans_for_each_iter_with_node(iter->trans, b, linked)
		__btree_iter_init(linked, b->c.level);
}

static inline int btree_iter_lock_root(struct btree_iter *iter,
				       unsigned depth_want)
{
	struct bch_fs *c = iter->trans->c;
	struct btree *b;
	enum six_lock_type lock_type;
	unsigned i;

	EBUG_ON(iter->nodes_locked);

	while (1) {
		b = READ_ONCE(c->btree_roots[iter->btree_id].b);
		iter->level = READ_ONCE(b->c.level);

		if (unlikely(iter->level < depth_want)) {
			/*
			 * the root is at a lower depth than the depth we want:
			 * got to the end of the btree, or we're walking nodes
			 * greater than some depth and there are no nodes >=
			 * that depth
			 */
			iter->level = depth_want;
			for (i = iter->level; i < BTREE_MAX_DEPTH; i++)
				iter->l[i].b = NULL;
			return 1;
		}

		lock_type = __btree_lock_want(iter, iter->level);
		if (unlikely(!btree_node_lock(b, POS_MAX, iter->level,
					      iter, lock_type)))
			return -EINTR;

		if (likely(b == c->btree_roots[iter->btree_id].b &&
			   b->c.level == iter->level &&
			   !race_fault())) {
			for (i = 0; i < iter->level; i++)
				iter->l[i].b = BTREE_ITER_NO_NODE_LOCK_ROOT;
			iter->l[iter->level].b = b;
			for (i = iter->level + 1; i < BTREE_MAX_DEPTH; i++)
				iter->l[i].b = NULL;

			mark_btree_node_locked(iter, iter->level, lock_type);
			btree_iter_node_set(iter, b);
			return 0;
		}

		six_unlock_type(&b->c.lock, lock_type);
	}
}

noinline
static void btree_iter_prefetch(struct btree_iter *iter)
{
	struct bch_fs *c = iter->trans->c;
	struct btree_iter_level *l = &iter->l[iter->level];
	struct btree_node_iter node_iter = l->iter;
	struct bkey_packed *k;
	BKEY_PADDED(k) tmp;
	unsigned nr = test_bit(BCH_FS_STARTED, &c->flags)
		? (iter->level > 1 ? 0 :  2)
		: (iter->level > 1 ? 1 : 16);
	bool was_locked = btree_node_locked(iter, iter->level);

	while (nr) {
		if (!bch2_btree_node_relock(iter, iter->level))
			return;

		bch2_btree_node_iter_advance(&node_iter, l->b);
		k = bch2_btree_node_iter_peek(&node_iter, l->b);
		if (!k)
			break;

		bch2_bkey_unpack(l->b, &tmp.k, k);
		bch2_btree_node_prefetch(c, iter, &tmp.k, iter->level - 1);
	}

	if (!was_locked)
		btree_node_unlock(iter, iter->level);
}

static inline int btree_iter_down(struct btree_iter *iter)
{
	struct bch_fs *c = iter->trans->c;
	struct btree_iter_level *l = &iter->l[iter->level];
	struct btree *b;
	unsigned level = iter->level - 1;
	enum six_lock_type lock_type = __btree_lock_want(iter, level);
	BKEY_PADDED(k) tmp;

	BUG_ON(!btree_node_locked(iter, iter->level));

	bch2_bkey_unpack(l->b, &tmp.k,
			 bch2_btree_node_iter_peek(&l->iter, l->b));

	b = bch2_btree_node_get(c, iter, &tmp.k, level, lock_type);
	if (unlikely(IS_ERR(b)))
		return PTR_ERR(b);

	mark_btree_node_locked(iter, level, lock_type);
	btree_iter_node_set(iter, b);

	if (iter->flags & BTREE_ITER_PREFETCH)
		btree_iter_prefetch(iter);

	iter->level = level;

	return 0;
}

static void btree_iter_up(struct btree_iter *iter)
{
	btree_node_unlock(iter, iter->level++);
}

static int btree_iter_traverse_one(struct btree_iter *);

static int __btree_iter_traverse_all(struct btree_trans *trans,
				   struct btree_iter *orig_iter, int ret)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter;
	u8 sorted[BTREE_ITER_MAX];
	unsigned i, nr_sorted = 0;

	trans_for_each_iter(trans, iter)
		sorted[nr_sorted++] = iter - trans->iters;

#define btree_iter_cmp_by_idx(_l, _r)				\
		btree_iter_cmp(&trans->iters[_l], &trans->iters[_r])

	bubble_sort(sorted, nr_sorted, btree_iter_cmp_by_idx);
#undef btree_iter_cmp_by_idx

retry_all:
	bch2_trans_unlock(trans);

	if (unlikely(ret == -ENOMEM)) {
		struct closure cl;

		closure_init_stack(&cl);

		do {
			ret = bch2_btree_cache_cannibalize_lock(c, &cl);
			closure_sync(&cl);
		} while (ret);
	}

	if (unlikely(ret == -EIO)) {
		trans->error = true;
		orig_iter->flags |= BTREE_ITER_ERROR;
		orig_iter->l[orig_iter->level].b = BTREE_ITER_NO_NODE_ERROR;
		goto out;
	}

	BUG_ON(ret && ret != -EINTR);

	/* Now, redo traversals in correct order: */
	for (i = 0; i < nr_sorted; i++) {
		iter = &trans->iters[sorted[i]];

		do {
			ret = btree_iter_traverse_one(iter);
		} while (ret == -EINTR);

		if (ret)
			goto retry_all;
	}

	ret = hweight64(trans->iters_live) > 1 ? -EINTR : 0;
out:
	bch2_btree_cache_cannibalize_unlock(c);
	return ret;
}

int bch2_btree_iter_traverse_all(struct btree_trans *trans)
{
	return __btree_iter_traverse_all(trans, NULL, 0);
}

static inline bool btree_iter_good_node(struct btree_iter *iter,
					unsigned l, int check_pos)
{
	if (!is_btree_node(iter, l) ||
	    !bch2_btree_node_relock(iter, l))
		return false;

	if (check_pos <= 0 && btree_iter_pos_before_node(iter, iter->l[l].b))
		return false;
	if (check_pos >= 0 && btree_iter_pos_after_node(iter, iter->l[l].b))
		return false;
	return true;
}

static inline unsigned btree_iter_up_until_good_node(struct btree_iter *iter,
						     int check_pos)
{
	unsigned l = iter->level;

	while (btree_iter_node(iter, l) &&
	       !btree_iter_good_node(iter, l, check_pos)) {
		btree_node_unlock(iter, l);
		iter->l[l].b = BTREE_ITER_NO_NODE_UP;
		l++;
	}

	return l;
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
static int btree_iter_traverse_one(struct btree_iter *iter)
{
	unsigned depth_want = iter->level;

	if (unlikely(iter->level >= BTREE_MAX_DEPTH))
		return 0;

	if (bch2_btree_iter_relock(iter, false))
		return 0;

	/*
	 * XXX: correctly using BTREE_ITER_UPTODATE should make using check_pos
	 * here unnecessary
	 */
	iter->level = btree_iter_up_until_good_node(iter, 0);

	/*
	 * If we've got a btree node locked (i.e. we aren't about to relock the
	 * root) - advance its node iterator if necessary:
	 *
	 * XXX correctly using BTREE_ITER_UPTODATE should make this unnecessary
	 */
	if (btree_iter_node(iter, iter->level)) {
		BUG_ON(!btree_iter_pos_in_node(iter, iter->l[iter->level].b));

		btree_iter_advance_to_pos(iter, &iter->l[iter->level], -1);
	}

	/*
	 * Note: iter->nodes[iter->level] may be temporarily NULL here - that
	 * would indicate to other code that we got to the end of the btree,
	 * here it indicates that relocking the root failed - it's critical that
	 * btree_iter_lock_root() comes next and that it can't fail
	 */
	while (iter->level > depth_want) {
		int ret = btree_iter_node(iter, iter->level)
			? btree_iter_down(iter)
			: btree_iter_lock_root(iter, depth_want);
		if (unlikely(ret)) {
			if (ret == 1)
				return 0;

			iter->level = depth_want;
			iter->l[iter->level].b = BTREE_ITER_NO_NODE_DOWN;
			return ret;
		}
	}

	iter->uptodate = BTREE_ITER_NEED_PEEK;

	bch2_btree_trans_verify_locks(iter->trans);
	__bch2_btree_iter_verify(iter, iter->l[iter->level].b);
	return 0;
}

int __must_check __bch2_btree_iter_traverse(struct btree_iter *iter)
{
	int ret;

	ret =   bch2_trans_cond_resched(iter->trans) ?:
		btree_iter_traverse_one(iter);
	if (unlikely(ret))
		ret = __btree_iter_traverse_all(iter->trans, iter, ret);

	return ret;
}

static inline void bch2_btree_iter_checks(struct btree_iter *iter,
					  enum btree_iter_type type)
{
	EBUG_ON(iter->btree_id >= BTREE_ID_NR);
	EBUG_ON(!!(iter->flags & BTREE_ITER_IS_EXTENTS) !=
		(btree_node_type_is_extents(iter->btree_id) &&
		 type != BTREE_ITER_NODES));
	EBUG_ON(btree_iter_type(iter) != type);

	bch2_btree_trans_verify_locks(iter->trans);
}

/* Iterate across nodes (leaf and interior nodes) */

struct btree *bch2_btree_iter_peek_node(struct btree_iter *iter)
{
	struct btree *b;
	int ret;

	bch2_btree_iter_checks(iter, BTREE_ITER_NODES);

	if (iter->uptodate == BTREE_ITER_UPTODATE)
		return iter->l[iter->level].b;

	ret = bch2_btree_iter_traverse(iter);
	if (ret)
		return NULL;

	b = btree_iter_node(iter, iter->level);
	if (!b)
		return NULL;

	BUG_ON(bkey_cmp(b->key.k.p, iter->pos) < 0);

	iter->pos = b->key.k.p;
	iter->uptodate = BTREE_ITER_UPTODATE;

	return b;
}

struct btree *bch2_btree_iter_next_node(struct btree_iter *iter, unsigned depth)
{
	struct btree *b;
	int ret;

	bch2_btree_iter_checks(iter, BTREE_ITER_NODES);

	/* already got to end? */
	if (!btree_iter_node(iter, iter->level))
		return NULL;

	bch2_trans_cond_resched(iter->trans);

	btree_iter_up(iter);

	if (!bch2_btree_node_relock(iter, iter->level))
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_RELOCK);

	ret = bch2_btree_iter_traverse(iter);
	if (ret)
		return NULL;

	/* got to end? */
	b = btree_iter_node(iter, iter->level);
	if (!b)
		return NULL;

	if (bkey_cmp(iter->pos, b->key.k.p) < 0) {
		/*
		 * Haven't gotten to the end of the parent node: go back down to
		 * the next child node
		 */

		/*
		 * We don't really want to be unlocking here except we can't
		 * directly tell btree_iter_traverse() "traverse to this level"
		 * except by setting iter->level, so we have to unlock so we
		 * don't screw up our lock invariants:
		 */
		if (btree_node_read_locked(iter, iter->level))
			btree_node_unlock(iter, iter->level);

		/* ick: */
		iter->pos	= iter->btree_id == BTREE_ID_INODES
			? btree_type_successor(iter->btree_id, iter->pos)
			: bkey_successor(iter->pos);
		iter->level	= depth;

		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
		ret = bch2_btree_iter_traverse(iter);
		if (ret)
			return NULL;

		b = iter->l[iter->level].b;
	}

	iter->pos = b->key.k.p;
	iter->uptodate = BTREE_ITER_UPTODATE;

	return b;
}

/* Iterate across keys (in leaf nodes only) */

void bch2_btree_iter_set_pos_same_leaf(struct btree_iter *iter, struct bpos new_pos)
{
	struct btree_iter_level *l = &iter->l[0];

	EBUG_ON(iter->level != 0);
	EBUG_ON(bkey_cmp(new_pos, iter->pos) < 0);
	EBUG_ON(!btree_node_locked(iter, 0));
	EBUG_ON(bkey_cmp(new_pos, l->b->key.k.p) > 0);

	iter->pos = new_pos;
	btree_iter_set_dirty(iter, BTREE_ITER_NEED_PEEK);

	btree_iter_advance_to_pos(iter, l, -1);

	if (bch2_btree_node_iter_end(&l->iter) &&
	    btree_iter_pos_after_node(iter, l->b))
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
}

static unsigned btree_iter_pos_changed(struct btree_iter *iter, int cmp)
{
	unsigned l = btree_iter_up_until_good_node(iter, cmp);

	if (btree_iter_node(iter, l)) {
		/*
		 * We might have to skip over many keys, or just a few: try
		 * advancing the node iterator, and if we have to skip over too
		 * many keys just reinit it (or if we're rewinding, since that
		 * is expensive).
		 */
		if (cmp < 0 ||
		    !btree_iter_advance_to_pos(iter, &iter->l[l], 8))
			__btree_iter_init(iter, l);

		/* Don't leave it locked if we're not supposed to: */
		if (btree_lock_want(iter, l) == BTREE_NODE_UNLOCKED)
			btree_node_unlock(iter, l);
	}

	return l;
}

void bch2_btree_iter_set_pos(struct btree_iter *iter, struct bpos new_pos)
{
	int cmp = bkey_cmp(new_pos, iter->pos);
	unsigned l;

	if (!cmp)
		return;

	iter->pos = new_pos;

	l = btree_iter_pos_changed(iter, cmp);

	if (l != iter->level)
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
	else
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_PEEK);
}

static inline bool btree_iter_set_pos_to_next_leaf(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];

	iter->pos	= l->b->key.k.p;
	iter->uptodate	= BTREE_ITER_NEED_TRAVERSE;

	if (!bkey_cmp(iter->pos, POS_MAX)) {
		bkey_init(&iter->k);
		iter->k.p	= POS_MAX;
		return false;
	}

	iter->pos = btree_type_successor(iter->btree_id, iter->pos);
	btree_iter_pos_changed(iter, 1);
	return true;
}

static inline bool btree_iter_set_pos_to_prev_leaf(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];

	iter->pos	= l->b->data->min_key;
	iter->uptodate	= BTREE_ITER_NEED_TRAVERSE;

	if (!bkey_cmp(iter->pos, POS_MIN)) {
		bkey_init(&iter->k);
		iter->k.p	= POS_MIN;
		return false;
	}

	iter->pos = btree_type_predecessor(iter->btree_id, iter->pos);
	btree_iter_pos_changed(iter, -1);
	return true;
}

static inline struct bkey_s_c btree_iter_peek_uptodate(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_s_c ret = { .k = &iter->k };

	if (!bkey_deleted(&iter->k)) {
		struct bkey_packed *_k =
			__bch2_btree_node_iter_peek_all(&l->iter, l->b);

		ret.v = bkeyp_val(&l->b->format, _k);

		if (debug_check_iterators(iter->trans->c)) {
			struct bkey k = bkey_unpack_key(l->b, _k);
			BUG_ON(memcmp(&k, &iter->k, sizeof(k)));
		}

		if (debug_check_bkeys(iter->trans->c))
			bch2_bkey_debugcheck(iter->trans->c, l->b, ret);
	}

	return ret;
}

/**
 * bch2_btree_iter_peek: returns first key greater than or equal to iterator's
 * current position
 */
struct bkey_s_c bch2_btree_iter_peek(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_s_c k;
	int ret;

	bch2_btree_iter_checks(iter, BTREE_ITER_KEYS);

	if (iter->uptodate == BTREE_ITER_UPTODATE)
		return btree_iter_peek_uptodate(iter);

	while (1) {
		ret = bch2_btree_iter_traverse(iter);
		if (unlikely(ret))
			return bkey_s_c_err(ret);

		k = __btree_iter_peek(iter, l);
		if (likely(k.k))
			break;

		if (!btree_iter_set_pos_to_next_leaf(iter))
			return bkey_s_c_null;
	}

	/*
	 * iter->pos should always be equal to the key we just
	 * returned - except extents can straddle iter->pos:
	 */
	if (!(iter->flags & BTREE_ITER_IS_EXTENTS) ||
	    bkey_cmp(bkey_start_pos(k.k), iter->pos) > 0)
		iter->pos = bkey_start_pos(k.k);

	iter->uptodate = BTREE_ITER_UPTODATE;
	return k;
}

/**
 * bch2_btree_iter_next: returns first key greater than iterator's current
 * position
 */
struct bkey_s_c bch2_btree_iter_next(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_packed *p;
	struct bkey_s_c k;

	bch2_btree_iter_checks(iter, BTREE_ITER_KEYS);

	if (unlikely(iter->uptodate != BTREE_ITER_UPTODATE)) {
		if (unlikely(!bkey_cmp(iter->k.p, POS_MAX)))
			return bkey_s_c_null;

		/*
		 * XXX: when we just need to relock we should be able to avoid
		 * calling traverse, but we need to kill BTREE_ITER_NEED_PEEK
		 * for that to work
		 */
		iter->uptodate	= BTREE_ITER_NEED_TRAVERSE;

		bch2_btree_iter_set_pos(iter,
			btree_type_successor(iter->btree_id, iter->k.p));

		return bch2_btree_iter_peek(iter);
	}

	do {
		bch2_btree_node_iter_advance(&l->iter, l->b);
		p = bch2_btree_node_iter_peek_all(&l->iter, l->b);
	} while (likely(p) && bkey_whiteout(p));

	if (unlikely(!p))
		return btree_iter_set_pos_to_next_leaf(iter)
			? bch2_btree_iter_peek(iter)
			: bkey_s_c_null;

	k = __btree_iter_unpack(iter, l, &iter->k, p);

	EBUG_ON(bkey_cmp(bkey_start_pos(k.k), iter->pos) < 0);
	iter->pos = bkey_start_pos(k.k);
	return k;
}

/**
 * bch2_btree_iter_peek_prev: returns first key less than or equal to
 * iterator's current position
 */
struct bkey_s_c bch2_btree_iter_peek_prev(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_s_c k;
	int ret;

	bch2_btree_iter_checks(iter, BTREE_ITER_KEYS);

	if (iter->uptodate == BTREE_ITER_UPTODATE)
		return btree_iter_peek_uptodate(iter);

	while (1) {
		ret = bch2_btree_iter_traverse(iter);
		if (unlikely(ret))
			return bkey_s_c_err(ret);

		k = __btree_iter_peek(iter, l);
		if (!k.k ||
		    bkey_cmp(bkey_start_pos(k.k), iter->pos) > 0)
			k = __btree_iter_prev(iter, l);

		if (likely(k.k))
			break;

		if (!btree_iter_set_pos_to_prev_leaf(iter))
			return bkey_s_c_null;
	}

	EBUG_ON(bkey_cmp(bkey_start_pos(k.k), iter->pos) > 0);
	iter->pos	= bkey_start_pos(k.k);
	iter->uptodate	= BTREE_ITER_UPTODATE;
	return k;
}

/**
 * bch2_btree_iter_prev: returns first key less than iterator's current
 * position
 */
struct bkey_s_c bch2_btree_iter_prev(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_s_c k;

	bch2_btree_iter_checks(iter, BTREE_ITER_KEYS);

	if (unlikely(iter->uptodate != BTREE_ITER_UPTODATE)) {
		/*
		 * XXX: when we just need to relock we should be able to avoid
		 * calling traverse, but we need to kill BTREE_ITER_NEED_PEEK
		 * for that to work
		 */
		iter->pos	= btree_type_predecessor(iter->btree_id,
							 iter->pos);
		iter->uptodate	= BTREE_ITER_NEED_TRAVERSE;

		return bch2_btree_iter_peek_prev(iter);
	}

	k = __btree_iter_prev(iter, l);
	if (unlikely(!k.k))
		return btree_iter_set_pos_to_prev_leaf(iter)
			? bch2_btree_iter_peek(iter)
			: bkey_s_c_null;

	EBUG_ON(bkey_cmp(bkey_start_pos(k.k), iter->pos) >= 0);
	iter->pos	= bkey_start_pos(k.k);
	return k;
}

static inline struct bkey_s_c
__bch2_btree_iter_peek_slot_extents(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct btree_node_iter node_iter;
	struct bkey_s_c k;
	struct bkey n;
	int ret;

recheck:
	while ((k = __btree_iter_peek_all(iter, l, &iter->k)).k &&
	       bkey_cmp(k.k->p, iter->pos) <= 0)
		bch2_btree_node_iter_advance(&l->iter, l->b);

	/*
	 * iterator is now at the correct position for inserting at iter->pos,
	 * but we need to keep iterating until we find the first non whiteout so
	 * we know how big a hole we have, if any:
	 */

	node_iter = l->iter;
	if (k.k && bkey_whiteout(k.k))
		k = __btree_iter_unpack(iter, l, &iter->k,
			bch2_btree_node_iter_peek(&node_iter, l->b));

	/*
	 * If we got to the end of the node, check if we need to traverse to the
	 * next node:
	 */
	if (unlikely(!k.k && btree_iter_pos_after_node(iter, l->b))) {
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
		ret = bch2_btree_iter_traverse(iter);
		if (unlikely(ret))
			return bkey_s_c_err(ret);

		goto recheck;
	}

	if (k.k &&
	    !bkey_whiteout(k.k) &&
	    bkey_cmp(bkey_start_pos(k.k), iter->pos) <= 0) {
		/*
		 * if we skipped forward to find the first non whiteout and
		 * there _wasn't_ actually a hole, we want the iterator to be
		 * pointed at the key we found:
		 */
		l->iter = node_iter;

		EBUG_ON(bkey_cmp(k.k->p, iter->pos) < 0);
		EBUG_ON(bkey_deleted(k.k));
		iter->uptodate = BTREE_ITER_UPTODATE;

		__bch2_btree_iter_verify(iter, l->b);
		return k;
	}

	/* hole */

	/* holes can't span inode numbers: */
	if (iter->pos.offset == KEY_OFFSET_MAX) {
		if (iter->pos.inode == KEY_INODE_MAX)
			return bkey_s_c_null;

		iter->pos = bkey_successor(iter->pos);
		goto recheck;
	}

	if (!k.k)
		k.k = &l->b->key.k;

	bkey_init(&n);
	n.p = iter->pos;
	bch2_key_resize(&n,
			min_t(u64, KEY_SIZE_MAX,
			      (k.k->p.inode == n.p.inode
			       ? bkey_start_offset(k.k)
			       : KEY_OFFSET_MAX) -
			      n.p.offset));

	EBUG_ON(!n.size);

	iter->k	= n;
	iter->uptodate = BTREE_ITER_UPTODATE;

	__bch2_btree_iter_verify(iter, l->b);
	return (struct bkey_s_c) { &iter->k, NULL };
}

static inline struct bkey_s_c
__bch2_btree_iter_peek_slot(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_s_c k;
	int ret;

	if (iter->flags & BTREE_ITER_IS_EXTENTS)
		return __bch2_btree_iter_peek_slot_extents(iter);

recheck:
	while ((k = __btree_iter_peek_all(iter, l, &iter->k)).k &&
	       bkey_deleted(k.k) &&
	       bkey_cmp(k.k->p, iter->pos) == 0)
		bch2_btree_node_iter_advance(&l->iter, l->b);

	/*
	 * If we got to the end of the node, check if we need to traverse to the
	 * next node:
	 */
	if (unlikely(!k.k && btree_iter_pos_after_node(iter, l->b))) {
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
		ret = bch2_btree_iter_traverse(iter);
		if (unlikely(ret))
			return bkey_s_c_err(ret);

		goto recheck;
	}

	if (!k.k ||
	    bkey_deleted(k.k) ||
	    bkey_cmp(iter->pos, k.k->p)) {
		/* hole */
		bkey_init(&iter->k);
		iter->k.p = iter->pos;
		k = (struct bkey_s_c) { &iter->k, NULL };
	}

	iter->uptodate = BTREE_ITER_UPTODATE;
	__bch2_btree_iter_verify(iter, l->b);
	return k;
}

struct bkey_s_c bch2_btree_iter_peek_slot(struct btree_iter *iter)
{
	int ret;

	bch2_btree_iter_checks(iter, BTREE_ITER_KEYS);

	if (iter->uptodate == BTREE_ITER_UPTODATE)
		return btree_iter_peek_uptodate(iter);

	ret = bch2_btree_iter_traverse(iter);
	if (unlikely(ret))
		return bkey_s_c_err(ret);

	return __bch2_btree_iter_peek_slot(iter);
}

struct bkey_s_c bch2_btree_iter_next_slot(struct btree_iter *iter)
{
	bch2_btree_iter_checks(iter, BTREE_ITER_KEYS);

	iter->pos = btree_type_successor(iter->btree_id, iter->k.p);

	if (unlikely(iter->uptodate != BTREE_ITER_UPTODATE)) {
		/*
		 * XXX: when we just need to relock we should be able to avoid
		 * calling traverse, but we need to kill BTREE_ITER_NEED_PEEK
		 * for that to work
		 */
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);

		return bch2_btree_iter_peek_slot(iter);
	}

	if (!bkey_deleted(&iter->k))
		bch2_btree_node_iter_advance(&iter->l[0].iter, iter->l[0].b);

	btree_iter_set_dirty(iter, BTREE_ITER_NEED_PEEK);

	return __bch2_btree_iter_peek_slot(iter);
}

static inline void bch2_btree_iter_init(struct btree_trans *trans,
			struct btree_iter *iter, enum btree_id btree_id,
			struct bpos pos, unsigned flags)
{
	struct bch_fs *c = trans->c;
	unsigned i;

	if (btree_node_type_is_extents(btree_id) &&
	    !(flags & BTREE_ITER_NODES))
		flags |= BTREE_ITER_IS_EXTENTS;

	iter->trans			= trans;
	iter->pos			= pos;
	bkey_init(&iter->k);
	iter->k.p			= pos;
	iter->flags			= flags;
	iter->uptodate			= BTREE_ITER_NEED_TRAVERSE;
	iter->btree_id			= btree_id;
	iter->level			= 0;
	iter->locks_want		= flags & BTREE_ITER_INTENT ? 1 : 0;
	iter->nodes_locked		= 0;
	iter->nodes_intent_locked	= 0;
	for (i = 0; i < ARRAY_SIZE(iter->l); i++)
		iter->l[i].b		= NULL;
	iter->l[iter->level].b		= BTREE_ITER_NO_NODE_INIT;

	prefetch(c->btree_roots[btree_id].b);
}

/* new transactional stuff: */

int bch2_trans_iter_put(struct btree_trans *trans,
			struct btree_iter *iter)
{
	int ret = btree_iter_err(iter);

	trans->iters_live	&= ~(1ULL << iter->idx);
	return ret;
}

static inline void __bch2_trans_iter_free(struct btree_trans *trans,
					  unsigned idx)
{
	__bch2_btree_iter_unlock(&trans->iters[idx]);
	trans->iters_linked		&= ~(1ULL << idx);
	trans->iters_live		&= ~(1ULL << idx);
	trans->iters_touched		&= ~(1ULL << idx);
	trans->iters_unlink_on_restart	&= ~(1ULL << idx);
	trans->iters_unlink_on_commit	&= ~(1ULL << idx);
}

int bch2_trans_iter_free(struct btree_trans *trans,
			 struct btree_iter *iter)
{
	int ret = btree_iter_err(iter);

	__bch2_trans_iter_free(trans, iter->idx);
	return ret;
}

int bch2_trans_iter_free_on_commit(struct btree_trans *trans,
				   struct btree_iter *iter)
{
	int ret = btree_iter_err(iter);

	trans->iters_unlink_on_commit |= 1ULL << iter->idx;
	return ret;
}

static int bch2_trans_realloc_iters(struct btree_trans *trans,
				    unsigned new_size)
{
	void *new_iters, *new_updates, *new_sorted;
	size_t iters_bytes;
	size_t updates_bytes;
	size_t sorted_bytes;

	new_size = roundup_pow_of_two(new_size);

	BUG_ON(new_size > BTREE_ITER_MAX);

	if (new_size <= trans->size)
		return 0;

	BUG_ON(trans->used_mempool);

	bch2_trans_unlock(trans);

	iters_bytes	= sizeof(struct btree_iter) * new_size;
	updates_bytes	= sizeof(struct btree_insert_entry) * (new_size + 4);
	sorted_bytes	= sizeof(u8) * (new_size + 4);

	new_iters = kmalloc(iters_bytes +
			    updates_bytes +
			    sorted_bytes, GFP_NOFS);
	if (new_iters)
		goto success;

	new_iters = mempool_alloc(&trans->c->btree_iters_pool, GFP_NOFS);
	new_size = BTREE_ITER_MAX;

	trans->used_mempool = true;
success:
	new_updates	= new_iters + iters_bytes;
	new_sorted	= new_updates + updates_bytes;

	memcpy(new_iters, trans->iters,
	       sizeof(struct btree_iter) * trans->nr_iters);
	memcpy(new_updates, trans->updates,
	       sizeof(struct btree_insert_entry) * trans->nr_updates);

	if (IS_ENABLED(CONFIG_BCACHEFS_DEBUG))
		memset(trans->iters, POISON_FREE,
		       sizeof(struct btree_iter) * trans->nr_iters +
		       sizeof(struct btree_insert_entry) * trans->nr_iters);

	if (trans->iters != trans->iters_onstack)
		kfree(trans->iters);

	trans->iters		= new_iters;
	trans->updates		= new_updates;
	trans->updates_sorted	= new_sorted;
	trans->size		= new_size;

	if (trans->iters_live) {
		trace_trans_restart_iters_realloced(trans->ip, trans->size);
		return -EINTR;
	}

	return 0;
}

static struct btree_iter *btree_trans_iter_alloc(struct btree_trans *trans)
{
	unsigned idx = __ffs64(~trans->iters_linked);

	if (idx < trans->nr_iters)
		goto got_slot;

	if (trans->nr_iters == trans->size) {
		int ret = bch2_trans_realloc_iters(trans, trans->size * 2);
		if (ret)
			return ERR_PTR(ret);
	}

	idx = trans->nr_iters++;
	BUG_ON(trans->nr_iters > trans->size);

	trans->iters[idx].idx = idx;
got_slot:
	BUG_ON(trans->iters_linked & (1ULL << idx));
	trans->iters_linked |= 1ULL << idx;
	return &trans->iters[idx];
}

static struct btree_iter *__btree_trans_get_iter(struct btree_trans *trans,
						 unsigned btree_id, struct bpos pos,
						 unsigned flags, u64 iter_id)
{
	struct btree_iter *iter;

	BUG_ON(trans->nr_iters > BTREE_ITER_MAX);

	trans_for_each_iter(trans, iter)
		if (iter_id
		    ? iter->id == iter_id
		    : (iter->btree_id == btree_id &&
		       !bkey_cmp(iter->pos, pos)))
			goto found;

	iter = NULL;
found:
	if (!iter) {
		iter = btree_trans_iter_alloc(trans);
		if (IS_ERR(iter))
			return iter;

		iter->id = iter_id;

		bch2_btree_iter_init(trans, iter, btree_id, pos, flags);
	} else {
		iter->flags &= ~(BTREE_ITER_SLOTS|BTREE_ITER_INTENT|BTREE_ITER_PREFETCH);
		iter->flags |= flags & (BTREE_ITER_SLOTS|BTREE_ITER_INTENT|BTREE_ITER_PREFETCH);

		if ((iter->flags & BTREE_ITER_INTENT) &&
		    !bch2_btree_iter_upgrade(iter, 1)) {
			trace_trans_restart_upgrade(trans->ip);
			return ERR_PTR(-EINTR);
		}
	}

	BUG_ON(iter->btree_id != btree_id);
	BUG_ON(trans->iters_live & (1ULL << iter->idx));
	trans->iters_live	|= 1ULL << iter->idx;
	trans->iters_touched	|= 1ULL << iter->idx;

	BUG_ON(iter->btree_id != btree_id);
	BUG_ON((iter->flags ^ flags) & BTREE_ITER_TYPE);

	return iter;
}

struct btree_iter *__bch2_trans_get_iter(struct btree_trans *trans,
					 enum btree_id btree_id,
					 struct bpos pos, unsigned flags,
					 u64 iter_id)
{
	struct btree_iter *iter =
		__btree_trans_get_iter(trans, btree_id, pos, flags, iter_id);

	if (!IS_ERR(iter))
		bch2_btree_iter_set_pos(iter, pos);
	return iter;
}

struct btree_iter *bch2_trans_get_node_iter(struct btree_trans *trans,
					    enum btree_id btree_id,
					    struct bpos pos,
					    unsigned locks_want,
					    unsigned depth,
					    unsigned flags)
{
	struct btree_iter *iter =
		__btree_trans_get_iter(trans, btree_id, pos,
				       flags|BTREE_ITER_NODES, 0);
	unsigned i;

	BUG_ON(IS_ERR(iter));
	BUG_ON(bkey_cmp(iter->pos, pos));

	iter->locks_want = locks_want;
	iter->level	= depth;

	for (i = 0; i < ARRAY_SIZE(iter->l); i++)
		iter->l[i].b		= NULL;
	iter->l[iter->level].b		= BTREE_ITER_NO_NODE_INIT;

	return iter;
}

struct btree_iter *bch2_trans_copy_iter(struct btree_trans *trans,
					struct btree_iter *src)
{
	struct btree_iter *iter;
	int idx, i;

	iter = btree_trans_iter_alloc(trans);
	if (IS_ERR(iter))
		return iter;

	idx = iter->idx;
	*iter = *src;
	iter->idx = idx;

	trans->iters_live		|= 1ULL << idx;
	trans->iters_touched		|= 1ULL << idx;
	trans->iters_unlink_on_restart	|= 1ULL << idx;

	for (i = 0; i < BTREE_MAX_DEPTH; i++)
		if (btree_node_locked(iter, i))
			six_lock_increment(&iter->l[i].b->c.lock,
					   __btree_lock_want(iter, i));

	return iter;
}

static int bch2_trans_preload_mem(struct btree_trans *trans, size_t size)
{
	if (size > trans->mem_bytes) {
		size_t old_bytes = trans->mem_bytes;
		size_t new_bytes = roundup_pow_of_two(size);
		void *new_mem = krealloc(trans->mem, new_bytes, GFP_NOFS);

		if (!new_mem)
			return -ENOMEM;

		trans->mem = new_mem;
		trans->mem_bytes = new_bytes;

		if (old_bytes) {
			trace_trans_restart_mem_realloced(trans->ip, new_bytes);
			return -EINTR;
		}
	}

	return 0;
}

void *bch2_trans_kmalloc(struct btree_trans *trans, size_t size)
{
	void *p;
	int ret;

	ret = bch2_trans_preload_mem(trans, trans->mem_top + size);
	if (ret)
		return ERR_PTR(ret);

	p = trans->mem + trans->mem_top;
	trans->mem_top += size;
	return p;
}

inline void bch2_trans_unlink_iters(struct btree_trans *trans, u64 iters)
{
	iters &= trans->iters_linked;
	iters &= ~trans->iters_live;

	while (iters) {
		unsigned idx = __ffs64(iters);

		iters &= ~(1ULL << idx);
		__bch2_trans_iter_free(trans, idx);
	}
}

void bch2_trans_begin(struct btree_trans *trans)
{
	u64 iters_to_unlink;

	/*
	 * On transaction restart, the transaction isn't required to allocate
	 * all the same iterators it on the last iteration:
	 *
	 * Unlink any iterators it didn't use this iteration, assuming it got
	 * further (allocated an iter with a higher idx) than where the iter
	 * was originally allocated:
	 */
	iters_to_unlink = ~trans->iters_live &
		((1ULL << fls64(trans->iters_live)) - 1);

	iters_to_unlink |= trans->iters_unlink_on_restart;
	iters_to_unlink |= trans->iters_unlink_on_commit;

	trans->iters_live		= 0;

	bch2_trans_unlink_iters(trans, iters_to_unlink);

	trans->iters_touched		= 0;
	trans->iters_unlink_on_restart	= 0;
	trans->iters_unlink_on_commit	= 0;
	trans->nr_updates		= 0;
	trans->mem_top			= 0;

	bch2_btree_iter_traverse_all(trans);
}

void bch2_trans_init(struct btree_trans *trans, struct bch_fs *c,
		     unsigned expected_nr_iters,
		     size_t expected_mem_bytes)
{
	memset(trans, 0, offsetof(struct btree_trans, iters_onstack));

	trans->c		= c;
	trans->ip		= _RET_IP_;
	trans->size		= ARRAY_SIZE(trans->iters_onstack);
	trans->iters		= trans->iters_onstack;
	trans->updates		= trans->updates_onstack;
	trans->updates_sorted	= trans->updates_sorted_onstack;
	trans->fs_usage_deltas	= NULL;

	if (expected_nr_iters > trans->size)
		bch2_trans_realloc_iters(trans, expected_nr_iters);

	if (expected_mem_bytes)
		bch2_trans_preload_mem(trans, expected_mem_bytes);
}

int bch2_trans_exit(struct btree_trans *trans)
{
	bch2_trans_unlock(trans);

	kfree(trans->fs_usage_deltas);
	kfree(trans->mem);
	if (trans->used_mempool)
		mempool_free(trans->iters, &trans->c->btree_iters_pool);
	else if (trans->iters != trans->iters_onstack)
		kfree(trans->iters);
	trans->mem	= (void *) 0x1;
	trans->iters	= (void *) 0x1;

	return trans->error ? -EIO : 0;
}

void bch2_fs_btree_iter_exit(struct bch_fs *c)
{
	mempool_exit(&c->btree_iters_pool);
}

int bch2_fs_btree_iter_init(struct bch_fs *c)
{
	unsigned nr = BTREE_ITER_MAX;

	return mempool_init_kmalloc_pool(&c->btree_iters_pool, 1,
			sizeof(struct btree_iter) * nr +
			sizeof(struct btree_insert_entry) * (nr + 4) +
			sizeof(u8) * (nr + 4));
}
