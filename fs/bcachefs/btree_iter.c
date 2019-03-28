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

#define BTREE_ITER_NOT_END	((struct btree *) 1)

static inline bool is_btree_node(struct btree_iter *iter, unsigned l)
{
	return l < BTREE_MAX_DEPTH &&
		iter->l[l].b &&
		iter->l[l].b != BTREE_ITER_NOT_END;
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
	return __btree_iter_pos_cmp(iter, b, k, b->level != 0);
}

/* Btree node locking: */

/*
 * Updates the saved lock sequence number, so that bch2_btree_node_relock() will
 * succeed:
 */
void bch2_btree_node_unlock_write(struct btree *b, struct btree_iter *iter)
{
	struct btree_iter *linked;

	EBUG_ON(iter->l[b->level].b != b);
	EBUG_ON(iter->l[b->level].lock_seq + 1 != b->lock.state.seq);

	trans_for_each_iter_with_node(iter->trans, b, linked)
		linked->l[b->level].lock_seq += 2;

	six_unlock_write(&b->lock);
}

void __bch2_btree_node_lock_write(struct btree *b, struct btree_iter *iter)
{
	struct btree_iter *linked;
	unsigned readers = 0;

	EBUG_ON(btree_node_read_locked(iter, b->level));

	trans_for_each_iter(iter->trans, linked)
		if (linked->l[b->level].b == b &&
		    btree_node_read_locked(linked, b->level))
			readers++;

	/*
	 * Must drop our read locks before calling six_lock_write() -
	 * six_unlock() won't do wakeups until the reader count
	 * goes to 0, and it's safe because we have the node intent
	 * locked:
	 */
	atomic64_sub(__SIX_VAL(read_lock, readers),
		     &b->lock.state.counter);
	btree_node_lock_type(iter->trans->c, b, SIX_LOCK_write);
	atomic64_add(__SIX_VAL(read_lock, readers),
		     &b->lock.state.counter);
}

bool __bch2_btree_node_relock(struct btree_iter *iter, unsigned level)
{
	struct btree *b = btree_iter_node(iter, level);
	int want = __btree_lock_want(iter, level);

	if (!b || b == BTREE_ITER_NOT_END)
		return false;

	if (race_fault())
		return false;

	if (!six_relock_type(&b->lock, want, iter->l[level].lock_seq) &&
	    !(iter->l[level].lock_seq >> 1 == b->lock.state.seq >> 1 &&
	      btree_node_lock_increment(iter, b, level, want)))
		return false;

	mark_btree_node_locked(iter, level, want);
	return true;
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
	    ? six_lock_tryupgrade(&b->lock)
	    : six_relock_type(&b->lock, SIX_LOCK_intent, iter->l[level].lock_seq))
		goto success;

	if (iter->l[level].lock_seq >> 1 == b->lock.state.seq >> 1 &&
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
					bool upgrade)
{
	unsigned l = iter->level;
	int fail_idx = -1;

	do {
		if (!btree_iter_node(iter, l))
			break;

		if (!(upgrade
		      ? bch2_btree_node_upgrade(iter, l)
		      : bch2_btree_node_relock(iter, l))) {
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
		iter->l[fail_idx].b = BTREE_ITER_NOT_END;
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
			   enum six_lock_type type,
			   bool may_drop_locks)
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
			if (may_drop_locks) {
				linked->locks_want = max_t(unsigned,
						linked->locks_want,
						__fls(linked->nodes_locked) + 1);
				btree_iter_get_locks(linked, true);
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
			if (may_drop_locks) {
				linked->locks_want =
					max(level + 1, max_t(unsigned,
					    linked->locks_want,
					    iter->locks_want));
				btree_iter_get_locks(linked, true);
			}
			ret = false;
		}
	}

	if (ret)
		__btree_node_lock_type(iter->trans->c, b, type);
	else
		trans_restart();

	return ret;
}

/* Btree iterator locking: */

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_btree_iter_verify_locks(struct btree_iter *iter)
{
	unsigned l;

	BUG_ON((iter->flags & BTREE_ITER_NOUNLOCK) &&
	       !btree_node_locked(iter, 0));

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
static bool bch2_btree_iter_relock(struct btree_iter *iter)
{
	return iter->uptodate >= BTREE_ITER_NEED_RELOCK
		? btree_iter_get_locks(iter, false)
		: true;
}

bool __bch2_btree_iter_upgrade(struct btree_iter *iter,
			       unsigned new_locks_want)
{
	struct btree_iter *linked;

	EBUG_ON(iter->locks_want >= new_locks_want);

	iter->locks_want = new_locks_want;

	if (btree_iter_get_locks(iter, true))
		return true;

	/*
	 * Ancestor nodes must be locked before child nodes, so set locks_want
	 * on iterators that might lock ancestors before us to avoid getting
	 * -EINTR later:
	 */
	trans_for_each_iter(iter->trans, linked)
		if (linked != iter &&
		    linked->btree_id == iter->btree_id &&
		    btree_iter_cmp(linked, iter) <= 0 &&
		    linked->locks_want < new_locks_want) {
			linked->locks_want = new_locks_want;
			btree_iter_get_locks(linked, true);
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
					six_lock_downgrade(&linked->l[l].b->lock);
					linked->nodes_intent_locked ^= 1 << l;
				}
				break;
			}
		}
	}

	bch2_btree_trans_verify_locks(iter->trans);
}

int bch2_btree_iter_unlock(struct btree_iter *iter)
{
	struct btree_iter *linked;

	trans_for_each_iter(iter->trans, linked)
		__bch2_btree_iter_unlock(linked);

	return btree_iter_err(iter);
}

bool bch2_btree_trans_relock(struct btree_trans *trans)
{
	struct btree_iter *iter;
	bool ret = true;

	trans_for_each_iter(trans, iter)
		ret &= bch2_btree_iter_relock(iter);

	return ret;
}

void bch2_btree_trans_unlock(struct btree_trans *trans)
{
	struct btree_iter *iter;

	trans_for_each_iter(trans, iter)
		__bch2_btree_iter_unlock(iter);
}

/* Btree transaction locking: */

/* Btree iterator: */

#ifdef CONFIG_BCACHEFS_DEBUG

static void __bch2_btree_iter_verify(struct btree_iter *iter,
				     struct btree *b)
{
	struct btree_iter_level *l = &iter->l[b->level];
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
	k = b->level || iter->flags & BTREE_ITER_IS_EXTENTS
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
	       (iter->flags & BTREE_ITER_TYPE) == BTREE_ITER_KEYS &&
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

	btree_node_iter_for_each(node_iter, set)
		if (set->end == old_end)
			goto found;

	/* didn't find the bset in the iterator - might have to readd it: */
	if (new_u64s &&
	    btree_iter_pos_cmp(iter, b, where) > 0) {
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_PEEK);

		bch2_btree_node_iter_push(node_iter, b, where, end);

		if (!b->level &&
		    node_iter == &iter->l[0].iter)
			bkey_disassemble(b,
				bch2_btree_node_iter_peek_all(node_iter, b),
				&iter->k);
	}
	return;
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
		set->k = (int) set->k + shift;
		goto iter_current_key_not_modified;
	}

	btree_iter_set_dirty(iter, BTREE_ITER_NEED_PEEK);

	bch2_btree_node_iter_sort(node_iter, b);
	if (!b->level && node_iter == &iter->l[0].iter) {
		/*
		 * not legal to call bkey_debugcheck() here, because we're
		 * called midway through the update path after update has been
		 * marked but before deletes have actually happened:
		 */
#if 0
		__btree_iter_peek_all(iter, &iter->l[0], &iter->k);
#endif
		struct btree_iter_level *l = &iter->l[0];
		struct bkey_packed *k =
			bch2_btree_node_iter_peek_all(&l->iter, l->b);

		if (unlikely(!k))
			iter->k.type = KEY_TYPE_deleted;
		else
			bkey_disassemble(l->b, k, &iter->k);
	}
iter_current_key_not_modified:

	/*
	 * Interior nodes are special because iterators for interior nodes don't
	 * obey the usual invariants regarding the iterator position:
	 *
	 * We may have whiteouts that compare greater than the iterator
	 * position, and logically should be in the iterator, but that we
	 * skipped past to find the first live key greater than the iterator
	 * position. This becomes an issue when we insert a new key that is
	 * greater than the current iterator position, but smaller than the
	 * whiteouts we've already skipped past - this happens in the course of
	 * a btree split.
	 *
	 * We have to rewind the iterator past to before those whiteouts here,
	 * else bkey_node_iter_prev() is not going to work and who knows what
	 * else would happen. And we have to do it manually, because here we've
	 * already done the insert and the iterator is currently inconsistent:
	 *
	 * We've got multiple competing invariants, here - we have to be careful
	 * about rewinding iterators for interior nodes, because they should
	 * always point to the key for the child node the btree iterator points
	 * to.
	 */
	if (b->level && new_u64s &&
	    btree_iter_pos_cmp(iter, b, where) > 0) {
		struct bset_tree *t, *where_set = bch2_bkey_to_bset_inlined(b, where);
		struct bkey_packed *k;

		for_each_bset(b, t) {
			if (where_set == t)
				continue;

			k = bch2_bkey_prev_all(b, t,
				bch2_btree_node_iter_bset_pos(node_iter, b, t));
			if (k &&
			    bkey_iter_cmp(b, k, where) > 0) {
				struct btree_node_iter_set *set;
				unsigned offset =
					__btree_node_key_to_offset(b, bkey_next(k));

				btree_node_iter_for_each(node_iter, set)
					if (set->k == offset) {
						set->k = __btree_node_key_to_offset(b, k);
						bch2_btree_node_iter_sort(node_iter, b);
						goto next_bset;
					}

				bch2_btree_node_iter_push(node_iter, b, k,
						btree_bkey_last(b, t));
			}
next_bset:
			t = t;
		}
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

	if (node_iter != &iter->l[b->level].iter)
		__bch2_btree_node_iter_fix(iter, b, node_iter, t,
					  where, clobber_u64s, new_u64s);

	trans_for_each_iter_with_node(iter->trans, b, linked)
		__bch2_btree_node_iter_fix(linked, b,
					  &linked->l[b->level].iter, t,
					  where, clobber_u64s, new_u64s);
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

	plevel = b->level + 1;
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
		btree_node_unlock(iter, b->level + 1);
}

static inline bool btree_iter_pos_after_node(struct btree_iter *iter,
					     struct btree *b)
{
	return __btree_iter_pos_cmp(iter, NULL,
			bkey_to_packed(&b->key), true) < 0;
}

static inline bool btree_iter_pos_in_node(struct btree_iter *iter,
					  struct btree *b)
{
	return iter->btree_id == b->btree_id &&
		bkey_cmp(iter->pos, b->data->min_key) >= 0 &&
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
	EBUG_ON(b->lock.state.seq & 1);

	iter->l[b->level].lock_seq = b->lock.state.seq;
	iter->l[b->level].b = b;
	__btree_iter_init(iter, b->level);
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
			BUG_ON(btree_node_locked(linked, b->level));

			t = btree_lock_want(linked, b->level);
			if (t != BTREE_NODE_UNLOCKED) {
				six_lock_increment(&b->lock, (enum six_lock_type) t);
				mark_btree_node_locked(linked, b->level, (enum six_lock_type) t);
			}

			btree_iter_node_set(linked, b);
		}

	six_unlock_intent(&b->lock);
}

void bch2_btree_iter_node_drop(struct btree_iter *iter, struct btree *b)
{
	struct btree_iter *linked;
	unsigned level = b->level;

	/* caller now responsible for unlocking @b */

	BUG_ON(iter->l[level].b != b);
	BUG_ON(!btree_node_intent_locked(iter, level));

	iter->l[level].b = BTREE_ITER_NOT_END;
	mark_btree_node_unlocked(iter, level);

	trans_for_each_iter(iter->trans, linked)
		if (linked->l[level].b == b) {
			__btree_node_unlock(linked, level);
			linked->l[level].b = BTREE_ITER_NOT_END;
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
		__btree_iter_init(linked, b->level);
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
		iter->level = READ_ONCE(b->level);

		if (unlikely(iter->level < depth_want)) {
			/*
			 * the root is at a lower depth than the depth we want:
			 * got to the end of the btree, or we're walking nodes
			 * greater than some depth and there are no nodes >=
			 * that depth
			 */
			iter->level = depth_want;
			iter->l[iter->level].b = NULL;
			return 1;
		}

		lock_type = __btree_lock_want(iter, iter->level);
		if (unlikely(!btree_node_lock(b, POS_MAX, iter->level,
					      iter, lock_type, true)))
			return -EINTR;

		if (likely(b == c->btree_roots[iter->btree_id].b &&
			   b->level == iter->level &&
			   !race_fault())) {
			for (i = 0; i < iter->level; i++)
				iter->l[i].b = BTREE_ITER_NOT_END;
			iter->l[iter->level].b = b;

			mark_btree_node_locked(iter, iter->level, lock_type);
			btree_iter_node_set(iter, b);
			return 0;

		}

		six_unlock_type(&b->lock, lock_type);
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

	b = bch2_btree_node_get(c, iter, &tmp.k, level, lock_type, true);
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

int __must_check __bch2_btree_iter_traverse(struct btree_iter *);

static int __btree_iter_traverse_all(struct btree_trans *trans,
				     struct btree_iter *iter, int ret)
{
	struct bch_fs *c = trans->c;
	u8 sorted[BTREE_ITER_MAX];
	unsigned i, nr_sorted = 0;

	trans_for_each_iter(trans, iter)
		sorted[nr_sorted++] = iter - trans->iters;

#define btree_iter_cmp_by_idx(_l, _r)				\
		btree_iter_cmp(&trans->iters[_l], &trans->iters[_r])

	bubble_sort(sorted, nr_sorted, btree_iter_cmp_by_idx);
#undef btree_iter_cmp_by_idx

retry_all:
	bch2_btree_trans_unlock(trans);

	if (unlikely(ret == -ENOMEM)) {
		struct closure cl;

		closure_init_stack(&cl);

		do {
			ret = bch2_btree_cache_cannibalize_lock(c, &cl);
			closure_sync(&cl);
		} while (ret);
	}

	if (unlikely(ret == -EIO)) {
		iter->flags |= BTREE_ITER_ERROR;
		iter->l[iter->level].b = BTREE_ITER_NOT_END;
		goto out;
	}

	BUG_ON(ret && ret != -EINTR);

	/* Now, redo traversals in correct order: */
	for (i = 0; i < nr_sorted; i++) {
		iter = &trans->iters[sorted[i]];

		do {
			ret = __bch2_btree_iter_traverse(iter);
		} while (ret == -EINTR);

		if (ret)
			goto retry_all;
	}

	ret = btree_trans_has_multiple_iters(trans) ? -EINTR : 0;
out:
	bch2_btree_cache_cannibalize_unlock(c);
	return ret;
}

int bch2_btree_iter_traverse_all(struct btree_trans *trans)
{
	return __btree_iter_traverse_all(trans, NULL, 0);
}

static unsigned btree_iter_up_until_locked(struct btree_iter *iter,
					   bool check_pos)
{
	unsigned l = iter->level;

	while (btree_iter_node(iter, l) &&
	       !(is_btree_node(iter, l) &&
		 bch2_btree_node_relock(iter, l) &&
		 (!check_pos ||
		  btree_iter_pos_in_node(iter, iter->l[l].b)))) {
		btree_node_unlock(iter, l);
		iter->l[l].b = BTREE_ITER_NOT_END;
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
 * stashed in the iterator and returned from bch2_btree_iter_unlock().
 */
int __must_check __bch2_btree_iter_traverse(struct btree_iter *iter)
{
	unsigned depth_want = iter->level;

	if (unlikely(iter->level >= BTREE_MAX_DEPTH))
		return 0;

	if (bch2_btree_iter_relock(iter))
		return 0;

	/*
	 * XXX: correctly using BTREE_ITER_UPTODATE should make using check_pos
	 * here unnecessary
	 */
	iter->level = btree_iter_up_until_locked(iter, true);

	/*
	 * If we've got a btree node locked (i.e. we aren't about to relock the
	 * root) - advance its node iterator if necessary:
	 *
	 * XXX correctly using BTREE_ITER_UPTODATE should make this unnecessary
	 */
	if (btree_iter_node(iter, iter->level))
		btree_iter_advance_to_pos(iter, &iter->l[iter->level], -1);

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
			iter->l[iter->level].b = BTREE_ITER_NOT_END;
			return ret;
		}
	}

	iter->uptodate = BTREE_ITER_NEED_PEEK;

	bch2_btree_trans_verify_locks(iter->trans);
	__bch2_btree_iter_verify(iter, iter->l[iter->level].b);
	return 0;
}

int __must_check bch2_btree_iter_traverse(struct btree_iter *iter)
{
	int ret;

	ret = __bch2_btree_iter_traverse(iter);
	if (unlikely(ret))
		ret = __btree_iter_traverse_all(iter->trans, iter, ret);

	BUG_ON(ret == -EINTR && !btree_trans_has_multiple_iters(iter->trans));

	return ret;
}

static inline void bch2_btree_iter_checks(struct btree_iter *iter,
					  enum btree_iter_type type)
{
	EBUG_ON(iter->btree_id >= BTREE_ID_NR);
	EBUG_ON(!!(iter->flags & BTREE_ITER_IS_EXTENTS) !=
		(iter->btree_id == BTREE_ID_EXTENTS &&
		 type != BTREE_ITER_NODES));

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

void bch2_btree_iter_set_pos(struct btree_iter *iter, struct bpos new_pos)
{
	int cmp = bkey_cmp(new_pos, iter->pos);
	unsigned level;

	if (!cmp)
		return;

	iter->pos = new_pos;

	level = btree_iter_up_until_locked(iter, true);

	if (btree_iter_node(iter, level)) {
		/*
		 * We might have to skip over many keys, or just a few: try
		 * advancing the node iterator, and if we have to skip over too
		 * many keys just reinit it (or if we're rewinding, since that
		 * is expensive).
		 */
		if (cmp < 0 ||
		    !btree_iter_advance_to_pos(iter, &iter->l[level], 8))
			__btree_iter_init(iter, level);

		/* Don't leave it locked if we're not supposed to: */
		if (btree_lock_want(iter, level) == BTREE_NODE_UNLOCKED)
			btree_node_unlock(iter, level);
	}

	if (level != iter->level)
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_TRAVERSE);
	else
		btree_iter_set_dirty(iter, BTREE_ITER_NEED_PEEK);
}

static inline struct bkey_s_c btree_iter_peek_uptodate(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_s_c ret = { .k = &iter->k };

	if (!bkey_deleted(&iter->k)) {
		EBUG_ON(bch2_btree_node_iter_end(&l->iter));
		ret.v = bkeyp_val(&l->b->format,
			__bch2_btree_node_iter_peek_all(&l->iter, l->b));
	}

	if (debug_check_bkeys(iter->trans->c) &&
	    !bkey_deleted(ret.k))
		bch2_bkey_debugcheck(iter->trans->c, l->b, ret);
	return ret;
}

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

		/* got to the end of the leaf, iterator needs to be traversed: */
		iter->pos	= l->b->key.k.p;
		iter->uptodate	= BTREE_ITER_NEED_TRAVERSE;

		if (!bkey_cmp(iter->pos, POS_MAX))
			return bkey_s_c_null;

		iter->pos = btree_type_successor(iter->btree_id, iter->pos);
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

static noinline
struct bkey_s_c bch2_btree_iter_peek_next_leaf(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];

	iter->pos	= l->b->key.k.p;
	iter->uptodate	= BTREE_ITER_NEED_TRAVERSE;

	if (!bkey_cmp(iter->pos, POS_MAX))
		return bkey_s_c_null;

	iter->pos = btree_type_successor(iter->btree_id, iter->pos);

	return bch2_btree_iter_peek(iter);
}

struct bkey_s_c bch2_btree_iter_next(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_packed *p;
	struct bkey_s_c k;

	bch2_btree_iter_checks(iter, BTREE_ITER_KEYS);

	if (unlikely(iter->uptodate != BTREE_ITER_UPTODATE)) {
		k = bch2_btree_iter_peek(iter);
		if (IS_ERR_OR_NULL(k.k))
			return k;
	}

	do {
		bch2_btree_node_iter_advance(&l->iter, l->b);
		p = bch2_btree_node_iter_peek_all(&l->iter, l->b);
		if (unlikely(!p))
			return bch2_btree_iter_peek_next_leaf(iter);
	} while (bkey_whiteout(p));

	k = __btree_iter_unpack(iter, l, &iter->k, p);

	EBUG_ON(bkey_cmp(bkey_start_pos(k.k), iter->pos) < 0);
	iter->pos = bkey_start_pos(k.k);
	return k;
}

struct bkey_s_c bch2_btree_iter_prev(struct btree_iter *iter)
{
	struct btree_iter_level *l = &iter->l[0];
	struct bkey_packed *p;
	struct bkey_s_c k;
	int ret;

	bch2_btree_iter_checks(iter, BTREE_ITER_KEYS);

	if (unlikely(iter->uptodate != BTREE_ITER_UPTODATE)) {
		k = bch2_btree_iter_peek(iter);
		if (IS_ERR(k.k))
			return k;
	}

	while (1) {
		p = bch2_btree_node_iter_prev(&l->iter, l->b);
		if (likely(p))
			break;

		iter->pos = l->b->data->min_key;
		if (!bkey_cmp(iter->pos, POS_MIN))
			return bkey_s_c_null;

		bch2_btree_iter_set_pos(iter,
			btree_type_predecessor(iter->btree_id, iter->pos));

		ret = bch2_btree_iter_traverse(iter);
		if (unlikely(ret))
			return bkey_s_c_err(ret);

		p = bch2_btree_node_iter_peek(&l->iter, l->b);
		if (p)
			break;
	}

	k = __btree_iter_unpack(iter, l, &iter->k, p);

	EBUG_ON(bkey_cmp(bkey_start_pos(k.k), iter->pos) > 0);

	iter->pos	= bkey_start_pos(k.k);
	iter->uptodate	= BTREE_ITER_UPTODATE;
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
	       bkey_deleted(k.k) &&
	       bkey_cmp(bkey_start_pos(k.k), iter->pos) == 0)
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

	if (k.k &&
	    !bkey_deleted(k.k) &&
	    !bkey_cmp(iter->pos, k.k->p)) {
		iter->uptodate = BTREE_ITER_UPTODATE;
		return k;
	} else {
		/* hole */
		bkey_init(&iter->k);
		iter->k.p = iter->pos;

		iter->uptodate = BTREE_ITER_UPTODATE;
		return (struct bkey_s_c) { &iter->k, NULL };
	}
}

struct bkey_s_c bch2_btree_iter_peek_slot(struct btree_iter *iter)
{
	int ret;

	bch2_btree_iter_checks(iter, BTREE_ITER_SLOTS);

	if (iter->uptodate == BTREE_ITER_UPTODATE)
		return btree_iter_peek_uptodate(iter);

	ret = bch2_btree_iter_traverse(iter);
	if (unlikely(ret))
		return bkey_s_c_err(ret);

	return __bch2_btree_iter_peek_slot(iter);
}

struct bkey_s_c bch2_btree_iter_next_slot(struct btree_iter *iter)
{
	bch2_btree_iter_checks(iter, BTREE_ITER_SLOTS);

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

	if (btree_id == BTREE_ID_EXTENTS &&
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
	iter->l[iter->level].b		= BTREE_ITER_NOT_END;

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

static int btree_trans_realloc_iters(struct btree_trans *trans,
				     unsigned new_size)
{
	void *new_iters, *new_updates;

	BUG_ON(new_size > BTREE_ITER_MAX);

	if (new_size <= trans->size)
		return 0;

	BUG_ON(trans->used_mempool);

	bch2_trans_unlock(trans);

	new_iters = kmalloc(sizeof(struct btree_iter) * new_size +
			    sizeof(struct btree_insert_entry) * (new_size + 4),
			    GFP_NOFS);
	if (new_iters)
		goto success;

	new_iters = mempool_alloc(&trans->c->btree_iters_pool, GFP_NOFS);
	new_size = BTREE_ITER_MAX;

	trans->used_mempool = true;
success:
	new_updates = new_iters + sizeof(struct btree_iter) * new_size;

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

	trans->iters	= new_iters;
	trans->updates	= new_updates;
	trans->size	= new_size;

	if (trans->iters_live) {
		trans_restart();
		return -EINTR;
	}

	return 0;
}

void bch2_trans_preload_iters(struct btree_trans *trans)
{
	btree_trans_realloc_iters(trans, BTREE_ITER_MAX);
}

static int btree_trans_iter_alloc(struct btree_trans *trans)
{
	unsigned idx = ffz(trans->iters_linked);

	if (idx < trans->nr_iters)
		goto got_slot;

	if (trans->nr_iters == trans->size) {
		int ret = btree_trans_realloc_iters(trans, trans->size * 2);
		if (ret)
			return ret;
	}

	idx = trans->nr_iters++;
	BUG_ON(trans->nr_iters > trans->size);

	trans->iters[idx].idx = idx;
got_slot:
	BUG_ON(trans->iters_linked & (1ULL << idx));
	trans->iters_linked |= 1ULL << idx;
	return idx;
}

static struct btree_iter *__btree_trans_get_iter(struct btree_trans *trans,
						 unsigned btree_id, struct bpos pos,
						 unsigned flags, u64 iter_id)
{
	struct btree_iter *iter;
	int idx;

	BUG_ON(trans->nr_iters > BTREE_ITER_MAX);

	for (idx = 0; idx < trans->nr_iters; idx++) {
		if (!(trans->iters_linked & (1ULL << idx)))
			continue;

		iter = &trans->iters[idx];
		if (iter_id
		    ? iter->id == iter_id
		    : (iter->btree_id == btree_id &&
		       !bkey_cmp(iter->pos, pos)))
			goto found;
	}
	idx = -1;
found:
	if (idx < 0) {
		idx = btree_trans_iter_alloc(trans);
		if (idx < 0)
			return ERR_PTR(idx);

		iter = &trans->iters[idx];
		iter->id = iter_id;

		bch2_btree_iter_init(trans, iter, btree_id, pos, flags);
	} else {
		iter = &trans->iters[idx];

		iter->flags &= ~(BTREE_ITER_INTENT|BTREE_ITER_PREFETCH);
		iter->flags |= flags & (BTREE_ITER_INTENT|BTREE_ITER_PREFETCH);
	}

	BUG_ON(iter->btree_id != btree_id);
	BUG_ON(trans->iters_live & (1ULL << idx));
	trans->iters_live	|= 1ULL << idx;
	trans->iters_touched	|= 1ULL << idx;

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
	iter->l[iter->level].b		= BTREE_ITER_NOT_END;

	return iter;
}

struct btree_iter *bch2_trans_copy_iter(struct btree_trans *trans,
					struct btree_iter *src)
{
	struct btree_iter *iter;
	unsigned offset = offsetof(struct btree_iter, trans);
	int i, idx;

	idx = btree_trans_iter_alloc(trans);
	if (idx < 0)
		return ERR_PTR(idx);

	trans->iters_live		|= 1ULL << idx;
	trans->iters_touched		|= 1ULL << idx;
	trans->iters_unlink_on_restart	|= 1ULL << idx;

	iter = &trans->iters[idx];

	memcpy((void *) iter + offset,
	       (void *)  src + offset,
	       sizeof(*iter) - offset);

	for (i = 0; i < BTREE_MAX_DEPTH; i++)
		if (btree_node_locked(iter, i))
			six_lock_increment(&iter->l[i].b->lock,
					   __btree_lock_want(iter, i));

	return &trans->iters[idx];
}

void *bch2_trans_kmalloc(struct btree_trans *trans,
			 size_t size)
{
	void *ret;

	if (trans->mem_top + size > trans->mem_bytes) {
		size_t old_bytes = trans->mem_bytes;
		size_t new_bytes = roundup_pow_of_two(trans->mem_top + size);
		void *new_mem = krealloc(trans->mem, new_bytes, GFP_NOFS);

		if (!new_mem)
			return ERR_PTR(-ENOMEM);

		trans->mem = new_mem;
		trans->mem_bytes = new_bytes;

		if (old_bytes) {
			trans_restart();
			return ERR_PTR(-EINTR);
		}
	}

	ret = trans->mem + trans->mem_top;
	trans->mem_top += size;
	return ret;
}

int bch2_trans_unlock(struct btree_trans *trans)
{
	unsigned iters = trans->iters_linked;
	int ret = 0;

	while (iters) {
		unsigned idx = __ffs(iters);
		struct btree_iter *iter = &trans->iters[idx];

		ret = ret ?: btree_iter_err(iter);

		__bch2_btree_iter_unlock(iter);
		iters ^= 1 << idx;
	}

	return ret;
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

void __bch2_trans_begin(struct btree_trans *trans)
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

void bch2_trans_init(struct btree_trans *trans, struct bch_fs *c)
{
	memset(trans, 0, offsetof(struct btree_trans, iters_onstack));

	trans->c		= c;
	trans->size		= ARRAY_SIZE(trans->iters_onstack);
	trans->iters		= trans->iters_onstack;
	trans->updates		= trans->updates_onstack;
}

int bch2_trans_exit(struct btree_trans *trans)
{
	int ret = bch2_trans_unlock(trans);

	kfree(trans->mem);
	if (trans->used_mempool)
		mempool_free(trans->iters, &trans->c->btree_iters_pool);
	else if (trans->iters != trans->iters_onstack)
		kfree(trans->iters);
	trans->mem	= (void *) 0x1;
	trans->iters	= (void *) 0x1;
	return ret;
}
