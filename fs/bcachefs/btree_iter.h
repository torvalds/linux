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
	u64 l;

	if (idx == BTREE_ITER_MAX)
		return NULL;

	l = trans->iters_linked >> idx;
	if (!l)
		return NULL;

	idx += __ffs64(l);
	EBUG_ON(idx >= BTREE_ITER_MAX);
	EBUG_ON(trans->iters[idx].idx != idx);
	return &trans->iters[idx];
}

#define trans_for_each_iter(_trans, _iter)				\
	for (_iter = __trans_next_iter((_trans), 0);			\
	     (_iter);							\
	     _iter = __trans_next_iter((_trans), (_iter)->idx + 1))

static inline struct btree_iter *next_btree_iter(struct btree_trans *trans, struct btree_iter *iter)
{
	unsigned idx = iter ? iter->sorted_idx + 1 : 0;

	EBUG_ON(idx > trans->nr_sorted);

	return idx < trans->nr_sorted
		? trans->iters + trans->sorted[idx]
		: NULL;
}

static inline struct btree_iter *prev_btree_iter(struct btree_trans *trans, struct btree_iter *iter)
{
	unsigned idx = iter ? iter->sorted_idx : trans->nr_sorted;

	return idx
		? trans->iters + trans->sorted[idx - 1]
		: NULL;
}

#define trans_for_each_iter_inorder(_trans, _iter, _i)			\
	for (_i = 0;							\
	     ((_iter) = (_trans)->iters + trans->sorted[_i]), (_i) < (_trans)->nr_sorted;\
	     _i++)

#define trans_for_each_iter_inorder_reverse(_trans, _iter, _i)		\
	for (_i = trans->nr_sorted - 1;					\
	     ((_iter) = (_trans)->iters + trans->sorted[_i]), (_i) >= 0;\
	     --_i)

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
void bch2_trans_verify_iters(struct btree_trans *, struct btree *);
void bch2_trans_verify_locks(struct btree_trans *);
#else
static inline void bch2_trans_verify_iters(struct btree_trans *trans,
					   struct btree *b) {}
static inline void bch2_trans_verify_locks(struct btree_trans *iter) {}
#endif

void bch2_btree_iter_fix_key_modified(struct btree_trans *trans,
				      struct btree *, struct bkey_packed *);
void bch2_btree_node_iter_fix(struct btree_trans *trans, struct btree_iter *,
			      struct btree *, struct btree_node_iter *,
			      struct bkey_packed *, unsigned, unsigned);

bool bch2_btree_iter_relock_intent(struct btree_trans *, struct btree_iter *);

bool bch2_trans_relock(struct btree_trans *);
void bch2_trans_unlock(struct btree_trans *);

__always_inline
static inline int btree_trans_restart(struct btree_trans *trans)
{
	trans->restarted = true;
	bch2_trans_unlock(trans);
	return -EINTR;
}

bool __bch2_btree_iter_upgrade(struct btree_trans *,
			       struct btree_iter *, unsigned);

static inline bool bch2_btree_iter_upgrade(struct btree_trans *trans,
					   struct btree_iter *iter,
					   unsigned new_locks_want)
{
	new_locks_want = min(new_locks_want, BTREE_MAX_DEPTH);

	return iter->locks_want < new_locks_want
		? __bch2_btree_iter_upgrade(trans, iter, new_locks_want)
		: iter->uptodate == BTREE_ITER_UPTODATE;
}

void __bch2_btree_iter_downgrade(struct btree_iter *, unsigned);

static inline void bch2_btree_iter_downgrade(struct btree_iter *iter)
{
	unsigned new_locks_want = iter->level + !!(iter->flags & BTREE_ITER_INTENT);

	if (iter->locks_want > new_locks_want)
		__bch2_btree_iter_downgrade(iter, new_locks_want);
}

void bch2_trans_downgrade(struct btree_trans *);

void bch2_trans_node_add(struct btree_trans *trans, struct btree *);
void bch2_trans_node_drop(struct btree_trans *, struct btree *);
void bch2_trans_node_reinit_iter(struct btree_trans *, struct btree *);

int __must_check bch2_btree_iter_traverse(struct btree_iter *);

struct btree *bch2_btree_iter_peek_node(struct btree_iter *);
struct btree *bch2_btree_iter_next_node(struct btree_iter *);

struct bkey_s_c bch2_btree_iter_peek(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_next(struct btree_iter *);

struct bkey_s_c bch2_btree_iter_peek_prev(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_prev(struct btree_iter *);

struct bkey_s_c bch2_btree_iter_peek_slot(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_next_slot(struct btree_iter *);
struct bkey_s_c bch2_btree_iter_prev_slot(struct btree_iter *);

bool bch2_btree_iter_advance(struct btree_iter *);
bool bch2_btree_iter_rewind(struct btree_iter *);

static inline void bch2_btree_iter_set_pos(struct btree_iter *iter, struct bpos new_pos)
{
	if (!(iter->flags & BTREE_ITER_ALL_SNAPSHOTS))
		new_pos.snapshot = iter->snapshot;

	iter->k.type = KEY_TYPE_deleted;
	iter->k.p.inode		= iter->pos.inode	= new_pos.inode;
	iter->k.p.offset	= iter->pos.offset	= new_pos.offset;
	iter->k.p.snapshot	= iter->pos.snapshot	= new_pos.snapshot;
	iter->k.size = 0;
	iter->should_be_locked = false;
}

static inline void bch2_btree_iter_set_pos_to_extent_start(struct btree_iter *iter)
{
	BUG_ON(!(iter->flags & BTREE_ITER_IS_EXTENTS));
	iter->pos = bkey_start_pos(&iter->k);
}

static inline struct btree_iter *idx_to_btree_iter(struct btree_trans *trans, unsigned idx)
{
	return idx != U8_MAX ? trans->iters + idx : NULL;
}

static inline struct btree_iter *btree_iter_child(struct btree_trans *trans,
						  struct btree_iter *iter)
{
	return idx_to_btree_iter(trans, iter->child_idx);
}

/*
 * Unlocks before scheduling
 * Note: does not revalidate iterator
 */
static inline int bch2_trans_cond_resched(struct btree_trans *trans)
{
	if (need_resched() || race_fault()) {
		bch2_trans_unlock(trans);
		schedule();
		return bch2_trans_relock(trans) ? 0 : -EINTR;
	} else {
		return 0;
	}
}

#define __for_each_btree_node(_trans, _iter, _btree_id, _start,	\
			      _locks_want, _depth, _flags, _b)		\
	for (iter = bch2_trans_get_node_iter((_trans), (_btree_id),	\
				_start, _locks_want, _depth, _flags),	\
	     _b = bch2_btree_iter_peek_node(_iter);			\
	     (_b);							\
	     (_b) = bch2_btree_iter_next_node(_iter))

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
	return flags & BTREE_ITER_SLOTS
		? bch2_btree_iter_next_slot(iter)
		: bch2_btree_iter_next(iter);
}

static inline int bkey_err(struct bkey_s_c k)
{
	return PTR_ERR_OR_ZERO(k.k);
}

#define for_each_btree_key(_trans, _iter, _btree_id,			\
			   _start, _flags, _k, _ret)			\
	for ((_iter) = bch2_trans_get_iter((_trans), (_btree_id),	\
					   (_start), (_flags)),		\
	     (_k) = __bch2_btree_iter_peek(_iter, _flags);		\
	     !((_ret) = bkey_err(_k)) && (_k).k;			\
	     (_k) = __bch2_btree_iter_next(_iter, _flags))

#define for_each_btree_key_continue(_iter, _flags, _k, _ret)		\
	for ((_k) = __bch2_btree_iter_peek(_iter, _flags);		\
	     !((_ret) = bkey_err(_k)) && (_k).k;			\
	     (_k) = __bch2_btree_iter_next(_iter, _flags))

/* new multiple iterator interface: */

void bch2_dump_trans_iters_updates(struct btree_trans *);

int bch2_trans_iter_put(struct btree_trans *, struct btree_iter *);
int bch2_trans_iter_free(struct btree_trans *, struct btree_iter *);

void bch2_trans_unlink_iters(struct btree_trans *);

struct btree_iter *__bch2_trans_get_iter(struct btree_trans *, enum btree_id,
					 struct bpos, unsigned,
					 unsigned, unsigned);

static inline struct btree_iter *
bch2_trans_get_iter(struct btree_trans *trans, enum btree_id btree_id,
		    struct bpos pos, unsigned flags)
{
	struct btree_iter *iter =
		__bch2_trans_get_iter(trans, btree_id, pos,
				      (flags & BTREE_ITER_INTENT) != 0, 0,
				      flags);
	iter->ip_allocated = _THIS_IP_;
	return iter;
}

struct btree_iter *__bch2_trans_copy_iter(struct btree_trans *,
					  struct btree_iter *);
static inline struct btree_iter *
bch2_trans_copy_iter(struct btree_trans *trans, struct btree_iter *src)
{
	struct btree_iter *iter =
		__bch2_trans_copy_iter(trans, src);

	iter->ip_allocated = _THIS_IP_;
	return iter;
}

struct btree_iter *bch2_trans_get_node_iter(struct btree_trans *,
				enum btree_id, struct bpos,
				unsigned, unsigned, unsigned);

static inline bool btree_iter_live(struct btree_trans *trans, struct btree_iter *iter)
{
	return (trans->iters_live & (1ULL << iter->idx)) != 0;
}

static inline bool btree_iter_keep(struct btree_trans *trans, struct btree_iter *iter)
{
	return btree_iter_live(trans, iter) ||
		(iter->flags & BTREE_ITER_KEEP_UNTIL_COMMIT);
}

static inline void set_btree_iter_dontneed(struct btree_trans *trans, struct btree_iter *iter)
{
	trans->iters_touched &= ~(1ULL << iter->idx);
}

void bch2_trans_begin(struct btree_trans *);

void *bch2_trans_kmalloc(struct btree_trans *, size_t);
void bch2_trans_init(struct btree_trans *, struct bch_fs *, unsigned, size_t);
int bch2_trans_exit(struct btree_trans *);

void bch2_btree_trans_to_text(struct printbuf *, struct bch_fs *);

void bch2_fs_btree_iter_exit(struct bch_fs *);
int bch2_fs_btree_iter_init(struct bch_fs *);

#endif /* _BCACHEFS_BTREE_ITER_H */
