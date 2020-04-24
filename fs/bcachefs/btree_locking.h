/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_LOCKING_H
#define _BCACHEFS_BTREE_LOCKING_H

/*
 * Only for internal btree use:
 *
 * The btree iterator tracks what locks it wants to take, and what locks it
 * currently has - here we have wrappers for locking/unlocking btree nodes and
 * updating the iterator state
 */

#include "btree_iter.h"
#include "six.h"

/* matches six lock types */
enum btree_node_locked_type {
	BTREE_NODE_UNLOCKED		= -1,
	BTREE_NODE_READ_LOCKED		= SIX_LOCK_read,
	BTREE_NODE_INTENT_LOCKED	= SIX_LOCK_intent,
};

static inline int btree_node_locked_type(struct btree_iter *iter,
					 unsigned level)
{
	/*
	 * We're relying on the fact that if nodes_intent_locked is set
	 * nodes_locked must be set as well, so that we can compute without
	 * branches:
	 */
	return BTREE_NODE_UNLOCKED +
		((iter->nodes_locked >> level) & 1) +
		((iter->nodes_intent_locked >> level) & 1);
}

static inline bool btree_node_intent_locked(struct btree_iter *iter,
					    unsigned level)
{
	return btree_node_locked_type(iter, level) == BTREE_NODE_INTENT_LOCKED;
}

static inline bool btree_node_read_locked(struct btree_iter *iter,
					  unsigned level)
{
	return btree_node_locked_type(iter, level) == BTREE_NODE_READ_LOCKED;
}

static inline bool btree_node_locked(struct btree_iter *iter, unsigned level)
{
	return iter->nodes_locked & (1 << level);
}

static inline void mark_btree_node_unlocked(struct btree_iter *iter,
					    unsigned level)
{
	iter->nodes_locked &= ~(1 << level);
	iter->nodes_intent_locked &= ~(1 << level);
}

static inline void mark_btree_node_locked(struct btree_iter *iter,
					  unsigned level,
					  enum six_lock_type type)
{
	/* relying on this to avoid a branch */
	BUILD_BUG_ON(SIX_LOCK_read   != 0);
	BUILD_BUG_ON(SIX_LOCK_intent != 1);

	iter->nodes_locked |= 1 << level;
	iter->nodes_intent_locked |= type << level;
}

static inline void mark_btree_node_intent_locked(struct btree_iter *iter,
						 unsigned level)
{
	mark_btree_node_locked(iter, level, SIX_LOCK_intent);
}

static inline enum six_lock_type __btree_lock_want(struct btree_iter *iter, int level)
{
	return level < iter->locks_want
		? SIX_LOCK_intent
		: SIX_LOCK_read;
}

static inline enum btree_node_locked_type
btree_lock_want(struct btree_iter *iter, int level)
{
	if (level < iter->level)
		return BTREE_NODE_UNLOCKED;
	if (level < iter->locks_want)
		return BTREE_NODE_INTENT_LOCKED;
	if (level == iter->level)
		return BTREE_NODE_READ_LOCKED;
	return BTREE_NODE_UNLOCKED;
}

static inline void __btree_node_unlock(struct btree_iter *iter, unsigned level)
{
	int lock_type = btree_node_locked_type(iter, level);

	EBUG_ON(level >= BTREE_MAX_DEPTH);

	if (lock_type != BTREE_NODE_UNLOCKED)
		six_unlock_type(&iter->l[level].b->c.lock, lock_type);
	mark_btree_node_unlocked(iter, level);
}

static inline void btree_node_unlock(struct btree_iter *iter, unsigned level)
{
	EBUG_ON(!level && iter->trans->nounlock);

	__btree_node_unlock(iter, level);
}

static inline void __bch2_btree_iter_unlock(struct btree_iter *iter)
{
	btree_iter_set_dirty(iter, BTREE_ITER_NEED_RELOCK);

	while (iter->nodes_locked)
		btree_node_unlock(iter, __ffs(iter->nodes_locked));
}

static inline enum bch_time_stats lock_to_time_stat(enum six_lock_type type)
{
	switch (type) {
	case SIX_LOCK_read:
		return BCH_TIME_btree_lock_contended_read;
	case SIX_LOCK_intent:
		return BCH_TIME_btree_lock_contended_intent;
	case SIX_LOCK_write:
		return BCH_TIME_btree_lock_contended_write;
	default:
		BUG();
	}
}

/*
 * wrapper around six locks that just traces lock contended time
 */
static inline void __btree_node_lock_type(struct bch_fs *c, struct btree *b,
					  enum six_lock_type type)
{
	u64 start_time = local_clock();

	six_lock_type(&b->c.lock, type, NULL, NULL);
	bch2_time_stats_update(&c->times[lock_to_time_stat(type)], start_time);
}

static inline void btree_node_lock_type(struct bch_fs *c, struct btree *b,
					enum six_lock_type type)
{
	if (!six_trylock_type(&b->c.lock, type))
		__btree_node_lock_type(c, b, type);
}

/*
 * Lock a btree node if we already have it locked on one of our linked
 * iterators:
 */
static inline bool btree_node_lock_increment(struct btree_iter *iter,
					     struct btree *b, unsigned level,
					     enum btree_node_locked_type want)
{
	struct btree_iter *linked;

	trans_for_each_iter(iter->trans, linked)
		if (linked->l[level].b == b &&
		    btree_node_locked_type(linked, level) >= want) {
			six_lock_increment(&b->c.lock, want);
			return true;
		}

	return false;
}

bool __bch2_btree_node_lock(struct btree *, struct bpos, unsigned,
			    struct btree_iter *, enum six_lock_type);

static inline bool btree_node_lock(struct btree *b, struct bpos pos,
				   unsigned level,
				   struct btree_iter *iter,
				   enum six_lock_type type)
{
	EBUG_ON(level >= BTREE_MAX_DEPTH);

	return likely(six_trylock_type(&b->c.lock, type)) ||
		btree_node_lock_increment(iter, b, level, type) ||
		__bch2_btree_node_lock(b, pos, level, iter, type);
}

bool __bch2_btree_node_relock(struct btree_iter *, unsigned);

static inline bool bch2_btree_node_relock(struct btree_iter *iter,
					  unsigned level)
{
	EBUG_ON(btree_node_locked(iter, level) &&
		btree_node_locked_type(iter, level) !=
		__btree_lock_want(iter, level));

	return likely(btree_node_locked(iter, level)) ||
		__bch2_btree_node_relock(iter, level);
}

/*
 * Updates the saved lock sequence number, so that bch2_btree_node_relock() will
 * succeed:
 */
static inline void
bch2_btree_node_unlock_write_inlined(struct btree *b, struct btree_iter *iter)
{
	struct btree_iter *linked;

	EBUG_ON(iter->l[b->c.level].b != b);
	EBUG_ON(iter->l[b->c.level].lock_seq + 1 != b->c.lock.state.seq);

	trans_for_each_iter_with_node(iter->trans, b, linked)
		linked->l[b->c.level].lock_seq += 2;

	six_unlock_write(&b->c.lock);
}

void bch2_btree_node_unlock_write(struct btree *, struct btree_iter *);

void __bch2_btree_node_lock_write(struct btree *, struct btree_iter *);

static inline void bch2_btree_node_lock_write(struct btree *b, struct btree_iter *iter)
{
	EBUG_ON(iter->l[b->c.level].b != b);
	EBUG_ON(iter->l[b->c.level].lock_seq != b->c.lock.state.seq);

	if (unlikely(!six_trylock_write(&b->c.lock)))
		__bch2_btree_node_lock_write(b, iter);
}

#endif /* _BCACHEFS_BTREE_LOCKING_H */


