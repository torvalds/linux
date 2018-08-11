/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_ITER_H
#define _BCACHEFS_BTREE_ITER_H

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

static inline struct btree *btree_node_parent(struct btree_iter *iter,
					      struct btree *b)
{
	return btree_iter_node(iter, b->level + 1);
}

static inline bool btree_iter_linked(const struct btree_iter *iter)
{
	return iter->next != iter;
}

static inline bool __iter_has_node(const struct btree_iter *iter,
				   const struct btree *b)
{
	/*
	 * We don't compare the low bits of the lock sequence numbers because
	 * @iter might have taken a write lock on @b, and we don't want to skip
	 * the linked iterator if the sequence numbers were equal before taking
	 * that write lock. The lock sequence number is incremented by taking
	 * and releasing write locks and is even when unlocked:
	 */

	return iter->l[b->level].b == b &&
		iter->l[b->level].lock_seq >> 1 == b->lock.state.seq >> 1;
}

static inline struct btree_iter *
__next_linked_iter(struct btree_iter *iter, struct btree_iter *linked)
{
	return linked->next != iter ? linked->next : NULL;
}

static inline struct btree_iter *
__next_iter_with_node(struct btree_iter *iter, struct btree *b,
		      struct btree_iter *linked)
{
	while (linked && !__iter_has_node(linked, b))
		linked = __next_linked_iter(iter, linked);

	return linked;
}

/**
 * for_each_btree_iter - iterate over all iterators linked with @_iter,
 * including @_iter
 */
#define for_each_btree_iter(_iter, _linked)				\
	for ((_linked) = (_iter); (_linked);				\
	     (_linked) = __next_linked_iter(_iter, _linked))

/**
 * for_each_btree_iter_with_node - iterate over all iterators linked with @_iter
 * that also point to @_b
 *
 * @_b is assumed to be locked by @_iter
 *
 * Filters out iterators that don't have a valid btree_node iterator for @_b -
 * i.e. iterators for which bch2_btree_node_relock() would not succeed.
 */
#define for_each_btree_iter_with_node(_iter, _b, _linked)		\
	for ((_linked) = (_iter);					\
	     ((_linked) = __next_iter_with_node(_iter, _b, _linked));	\
	     (_linked) = __next_linked_iter(_iter, _linked))

/**
 * for_each_linked_btree_iter - iterate over all iterators linked with @_iter,
 * _not_ including @_iter
 */
#define for_each_linked_btree_iter(_iter, _linked)			\
	for ((_linked) = (_iter)->next;					\
	     (_linked) != (_iter);					\
	     (_linked) = (_linked)->next)

#ifdef CONFIG_BCACHEFS_DEBUG
void bch2_btree_iter_verify(struct btree_iter *, struct btree *);
void bch2_btree_iter_verify_locks(struct btree_iter *);
#else
static inline void bch2_btree_iter_verify(struct btree_iter *iter,
					  struct btree *b) {}
static inline void bch2_btree_iter_verify_locks(struct btree_iter *iter) {}
#endif

void bch2_btree_node_iter_fix(struct btree_iter *, struct btree *,
			      struct btree_node_iter *, struct bkey_packed *,
			      unsigned, unsigned);

int bch2_btree_iter_unlock(struct btree_iter *);

bool __bch2_btree_iter_upgrade(struct btree_iter *, unsigned);
bool __bch2_btree_iter_upgrade_nounlock(struct btree_iter *, unsigned);

static inline bool bch2_btree_iter_upgrade(struct btree_iter *iter,
					   unsigned new_locks_want,
					   bool may_drop_locks)
{
	new_locks_want = min(new_locks_want, BTREE_MAX_DEPTH);

	return iter->locks_want < new_locks_want
		? (may_drop_locks
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

struct btree *bch2_btree_iter_peek_node(struct btree_iter *);
struct btree *bch2_btree_iter_next_node(struct btree_iter *, unsigned);

struct bkey_s_c bch2_btree_iter_peek(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_next(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_prev(struct btree_iter *);

struct bkey_s_c bch2_btree_iter_peek_slot(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_next_slot(struct btree_iter *);

void bch2_btree_iter_set_pos_same_leaf(struct btree_iter *, struct bpos);
void bch2_btree_iter_set_pos(struct btree_iter *, struct bpos);

void __bch2_btree_iter_init(struct btree_iter *, struct bch_fs *,
			   enum btree_id, struct bpos,
			   unsigned , unsigned, unsigned);

static inline void bch2_btree_iter_init(struct btree_iter *iter,
			struct bch_fs *c, enum btree_id btree_id,
			struct bpos pos, unsigned flags)
{
	__bch2_btree_iter_init(iter, c, btree_id, pos,
			       flags & BTREE_ITER_INTENT ? 1 : 0, 0,
			       (btree_id == BTREE_ID_EXTENTS
				?  BTREE_ITER_IS_EXTENTS : 0)|flags);
}

void bch2_btree_iter_link(struct btree_iter *, struct btree_iter *);
void bch2_btree_iter_unlink(struct btree_iter *);
void bch2_btree_iter_copy(struct btree_iter *, struct btree_iter *);

static inline struct bpos btree_type_successor(enum btree_id id,
					       struct bpos pos)
{
	if (id == BTREE_ID_INODES) {
		pos.inode++;
		pos.offset = 0;
	} else if (id != BTREE_ID_EXTENTS) {
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
	} else /* if (id != BTREE_ID_EXTENTS) */ {
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
static inline void bch2_btree_iter_cond_resched(struct btree_iter *iter)
{
	if (need_resched()) {
		bch2_btree_iter_unlock(iter);
		schedule();
	} else if (race_fault()) {
		bch2_btree_iter_unlock(iter);
	}
}

#define __for_each_btree_node(_iter, _c, _btree_id, _start,		\
			      _locks_want, _depth, _flags, _b)		\
	for (__bch2_btree_iter_init((_iter), (_c), (_btree_id), _start,	\
				    _locks_want, _depth,		\
				    _flags|BTREE_ITER_NODES),		\
	     _b = bch2_btree_iter_peek_node(_iter);			\
	     (_b);							\
	     (_b) = bch2_btree_iter_next_node(_iter, _depth))

#define for_each_btree_node(_iter, _c, _btree_id, _start, _flags, _b)	\
	__for_each_btree_node(_iter, _c, _btree_id, _start, 0, 0, _flags, _b)

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
	bch2_btree_iter_cond_resched(iter);

	return flags & BTREE_ITER_SLOTS
		? bch2_btree_iter_next_slot(iter)
		: bch2_btree_iter_next(iter);
}

#define for_each_btree_key(_iter, _c, _btree_id,  _start, _flags, _k)	\
	for (bch2_btree_iter_init((_iter), (_c), (_btree_id),		\
				  (_start), (_flags)),			\
	     (_k) = __bch2_btree_iter_peek(_iter, _flags);		\
	     !IS_ERR_OR_NULL((_k).k);					\
	     (_k) = __bch2_btree_iter_next(_iter, _flags))

#define for_each_btree_key_continue(_iter, _flags, _k)			\
	for ((_k) = __bch2_btree_iter_peek(_iter, _flags);		\
	     !IS_ERR_OR_NULL((_k).k);					\
	     (_k) = __bch2_btree_iter_next(_iter, _flags))

static inline int btree_iter_err(struct bkey_s_c k)
{
	return PTR_ERR_OR_ZERO(k.k);
}

/* new multiple iterator interface: */

void bch2_trans_preload_iters(struct btree_trans *);
void bch2_trans_iter_put(struct btree_trans *, struct btree_iter *);
void bch2_trans_iter_free(struct btree_trans *, struct btree_iter *);

struct btree_iter *__bch2_trans_get_iter(struct btree_trans *, enum btree_id,
					 struct bpos, unsigned, u64);
struct btree_iter *__bch2_trans_copy_iter(struct btree_trans *,
					  struct btree_iter *, u64);

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

static __always_inline struct btree_iter *
bch2_trans_copy_iter(struct btree_trans *trans, struct btree_iter *src)
{

	return __bch2_trans_copy_iter(trans, src, __btree_iter_id());
}

void __bch2_trans_begin(struct btree_trans *);

static inline void bch2_trans_begin_updates(struct btree_trans *trans)
{
	trans->nr_updates = 0;
}

void *bch2_trans_kmalloc(struct btree_trans *, size_t);
int bch2_trans_unlock(struct btree_trans *);
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
