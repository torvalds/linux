/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_ITER_H
#define _BCACHEFS_BTREE_ITER_H

#include "bset.h"
#include "btree_types.h"

static inline void btree_iter_set_dirty(struct btree_iter *iter,
					enum btree_iter_uptodate u)
{
	iter->uptodate = max_t(unsigned, iter->uptodate, u);
}

static inline struct btree *btree_iter_node(struct btree_iter *iter,
					    unsigned level)
{
	return level < BTREE_MAX_DEPTH ? iter->l[level].b : NULL;
}

static inline bool btree_node_lock_seq_matches(const struct btree_iter *iter,
					const struct btree *b, unsigned level)
{
	/*
	 * We don't compare the low bits of the lock sequence numbers because
	 * @iter might have taken a write lock on @b, and we don't want to skip
	 * the linked iterator if the sequence numbers were equal before taking
	 * that write lock. The lock sequence number is incremented by taking
	 * and releasing write locks and is even when unlocked:
	 */
	return iter->l[level].lock_seq >> 1 == b->c.lock.state.seq >> 1;
}

static inline struct btree *btree_node_parent(struct btree_iter *iter,
					      struct btree *b)
{
	return btree_iter_node(iter, b->c.level + 1);
}

static inline bool btree_trans_has_multiple_iters(const struct btree_trans *trans)
{
	return hweight64(trans->iters_linked) > 1;
}

static inline int btree_iter_err(const struct btree_iter *iter)
{
	return iter->flags & BTREE_ITER_ERROR ? -EIO : 0;
}

/* Iterate over iters within a transaction: */

static inline struct btree_iter *
__trans_next_iter(struct btree_trans *trans, unsigned idx)
{
	EBUG_ON(idx < trans->nr_iters && trans->iters[idx].idx != idx);

	for (; idx < trans->nr_iters; idx++)
		if (trans->iters_linked & (1ULL << idx))
			return &trans->iters[idx];

	return NULL;
}

#define trans_for_each_iter(_trans, _iter)				\
	for (_iter = __trans_next_iter((_trans), 0);			\
	     (_iter);							\
	     _iter = __trans_next_iter((_trans), (_iter)->idx + 1))

static inline bool __iter_has_node(const struct btree_iter *iter,
				   const struct btree *b)
{
	return iter->l[b->c.level].b == b &&
		btree_node_lock_seq_matches(iter, b, b->c.level);
}

static inline struct btree_iter *
__trans_next_iter_with_node(struct btree_trans *trans, struct btree *b,
			    unsigned idx)
{
	struct btree_iter *iter = __trans_next_iter(trans, idx);

	while (iter && !__iter_has_node(iter, b))
		iter = __trans_next_iter(trans, iter->idx + 1);

	return iter;
}

#define trans_for_each_iter_with_node(_trans, _b, _iter)		\
	for (_iter = __trans_next_iter_with_node((_trans), (_b), 0);	\
	     (_iter);							\
	     _iter = __trans_next_iter_with_node((_trans), (_b),	\
						 (_iter)->idx + 1))

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_btree_iter_verify(struct btree_iter *, struct btree *);
void bch2_btree_trans_verify_locks(struct btree_trans *);
#else
static inline void bch2_btree_iter_verify(struct btree_iter *iter,
					  struct btree *b) {}
static inline void bch2_btree_trans_verify_locks(struct btree_trans *iter) {}
#endif

void bch2_btree_node_iter_fix(struct btree_iter *, struct btree *,
			      struct btree_node_iter *, struct bkey_packed *,
			      unsigned, unsigned);

bool bch2_trans_relock(struct btree_trans *);
void bch2_trans_unlock(struct btree_trans *);

bool __bch2_btree_iter_upgrade(struct btree_iter *, unsigned);
bool __bch2_btree_iter_upgrade_nounlock(struct btree_iter *, unsigned);

static inline bool bch2_btree_iter_upgrade(struct btree_iter *iter,
					   unsigned new_locks_want)
{
	new_locks_want = min(new_locks_want, BTREE_MAX_DEPTH);

	return iter->locks_want < new_locks_want
		? (!iter->trans->nounlock
		   ? __bch2_btree_iter_upgrade(iter, new_locks_want)
		   : __bch2_btree_iter_upgrade_nounlock(iter, new_locks_want))
		: iter->uptodate <= BTREE_ITER_NEED_PEEK;
}

void __bch2_btree_iter_downgrade(struct btree_iter *, unsigned);

static inline void bch2_btree_iter_downgrade(struct btree_iter *iter)
{
	if (iter->locks_want > (iter->flags & BTREE_ITER_INTENT) ? 1 : 0)
		__bch2_btree_iter_downgrade(iter, 0);
}

void bch2_btree_iter_node_replace(struct btree_iter *, struct btree *);
void bch2_btree_iter_node_drop(struct btree_iter *, struct btree *);

void bch2_btree_iter_reinit_node(struct btree_iter *, struct btree *);

int __must_check bch2_btree_iter_traverse(struct btree_iter *);
int bch2_btree_iter_traverse_all(struct btree_trans *);

struct btree *bch2_btree_iter_peek_node(struct btree_iter *);
struct btree *bch2_btree_iter_next_node(struct btree_iter *, unsigned);

struct bkey_s_c bch2_btree_iter_peek(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_next(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_prev(struct btree_iter *);

struct bkey_s_c bch2_btree_iter_peek_slot(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_next_slot(struct btree_iter *);

void bch2_btree_iter_set_pos_same_leaf(struct btree_iter *, struct bpos);
void bch2_btree_iter_set_pos(struct btree_iter *, struct bpos);

static inline struct bpos btree_type_successor(enum btree_id id,
					       struct bpos pos)
{
	if (id == BTREE_ID_INODES) {
		pos.inode++;
		pos.offset = 0;
	} else if (!btree_node_type_is_extents(id)) {
		pos = bkey_successor(pos);
	}

	return pos;
}

static inline struct bpos btree_type_predecessor(enum btree_id id,
					       struct bpos pos)
{
	if (id == BTREE_ID_INODES) {
		--pos.inode;
		pos.offset = 0;
	} else {
		pos = bkey_predecessor(pos);
	}

	return pos;
}

static inline int __btree_iter_cmp(enum btree_id id,
				   struct bpos pos,
				   const struct btree_iter *r)
{
	if (id != r->btree_id)
		return id < r->btree_id ? -1 : 1;
	return bkey_cmp(pos, r->pos);
}

static inline int btree_iter_cmp(const struct btree_iter *l,
				 const struct btree_iter *r)
{
	return __btree_iter_cmp(l->btree_id, l->pos, r);
}

/*
 * Unlocks before scheduling
 * Note: does not revalidate iterator
 */
static inline void bch2_trans_cond_resched(struct btree_trans *trans)
{
	if (need_resched()) {
		bch2_trans_unlock(trans);
		schedule();
	} else if (race_fault()) {
		bch2_trans_unlock(trans);
	}
}

#define __for_each_btree_node(_trans, _iter, _btree_id, _start,	\
			      _locks_want, _depth, _flags, _b)		\
	for (iter = bch2_trans_get_node_iter((_trans), (_btree_id),	\
				_start, _locks_want, _depth, _flags),	\
	     _b = bch2_btree_iter_peek_node(_iter);			\
	     (_b);							\
	     (_b) = bch2_btree_iter_next_node(_iter, _depth))

#define for_each_btree_node(_trans, _iter, _btree_id, _start,		\
			    _flags, _b)					\
	__for_each_btree_node(_trans, _iter, _btree_id, _start,		\
			      0, 0, _flags, _b)

static inline struct bkey_s_c __bch2_btree_iter_peek(struct btree_iter *iter,
						     unsigned flags)
{
	return flags & BTREE_ITER_SLOTS
		? bch2_btree_iter_peek_slot(iter)
		: bch2_btree_iter_peek(iter);
}

static inline struct bkey_s_c __bch2_btree_iter_next(struct btree_iter *iter,
						     unsigned flags)
{
	bch2_trans_cond_resched(iter->trans);

	return flags & BTREE_ITER_SLOTS
		? bch2_btree_iter_next_slot(iter)
		: bch2_btree_iter_next(iter);
}

#define for_each_btree_key(_trans, _iter, _btree_id,			\
			   _start, _flags, _k, _ret)			\
	for ((_ret) = PTR_ERR_OR_ZERO((_iter) =				\
			bch2_trans_get_iter((_trans), (_btree_id),	\
					    (_start), (_flags))) ?:	\
		      PTR_ERR_OR_ZERO(((_k) =				\
			__bch2_btree_iter_peek(_iter, _flags)).k);	\
	     !ret && (_k).k;						\
	     (_ret) = PTR_ERR_OR_ZERO(((_k) =				\
			__bch2_btree_iter_next(_iter, _flags)).k))

#define for_each_btree_key_continue(_iter, _flags, _k)			\
	for ((_k) = __bch2_btree_iter_peek(_iter, _flags);		\
	     !IS_ERR_OR_NULL((_k).k);					\
	     (_k) = __bch2_btree_iter_next(_iter, _flags))

static inline int bkey_err(struct bkey_s_c k)
{
	return PTR_ERR_OR_ZERO(k.k);
}

/* new multiple iterator interface: */

void bch2_trans_preload_iters(struct btree_trans *);
int bch2_trans_iter_put(struct btree_trans *, struct btree_iter *);
int bch2_trans_iter_free(struct btree_trans *, struct btree_iter *);
int bch2_trans_iter_free_on_commit(struct btree_trans *, struct btree_iter *);

void bch2_trans_unlink_iters(struct btree_trans *, u64);

struct btree_iter *__bch2_trans_get_iter(struct btree_trans *, enum btree_id,
					 struct bpos, unsigned, u64);
struct btree_iter *bch2_trans_copy_iter(struct btree_trans *,
					struct btree_iter *);

static __always_inline u64 __btree_iter_id(void)
{
	u64 ret = 0;

	ret <<= 32;
	ret |= _RET_IP_ & U32_MAX;
	ret <<= 32;
	ret |= _THIS_IP_ & U32_MAX;
	return ret;
}

static __always_inline struct btree_iter *
bch2_trans_get_iter(struct btree_trans *trans, enum btree_id btree_id,
		    struct bpos pos, unsigned flags)
{
	return __bch2_trans_get_iter(trans, btree_id, pos, flags,
				     __btree_iter_id());
}

struct btree_iter *bch2_trans_get_node_iter(struct btree_trans *,
				enum btree_id, struct bpos,
				unsigned, unsigned, unsigned);

void __bch2_trans_begin(struct btree_trans *);

static inline void bch2_trans_begin_updates(struct btree_trans *trans)
{
	trans->nr_updates = 0;
}

void *bch2_trans_kmalloc(struct btree_trans *, size_t);
void bch2_trans_init(struct btree_trans *, struct bch_fs *);
int bch2_trans_exit(struct btree_trans *);

#ifdef TRACE_TRANSACTION_RESTARTS
#define bch2_trans_begin(_trans)					\
do {									\
	if (is_power_of_2((_trans)->nr_restarts) &&			\
	    (_trans)->nr_restarts >= 8)					\
		pr_info("nr restarts: %zu", (_trans)->nr_restarts);	\
									\
	(_trans)->nr_restarts++;					\
	__bch2_trans_begin(_trans);					\
} while (0)
#else
#define bch2_trans_begin(_trans)	__bch2_trans_begin(_trans)
#endif

#ifdef TRACE_TRANSACTION_RESTARTS_ALL
#define trans_restart(...) pr_info("transaction restart" __VA_ARGS__)
#else
#define trans_restart(...) no_printk("transaction restart" __VA_ARGS__)
#endif

#endif /* _BCACHEFS_BTREE_ITER_H */
