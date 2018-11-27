/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_IO_H
#define _BCACHEFS_BTREE_IO_H

#include "bset.h"
#include "extents.h"
#include "io_types.h"

struct bch_fs;
struct btree_write;
struct btree;
struct btree_iter;

struct btree_read_bio {
	struct bch_fs		*c;
	u64			start_time;
	unsigned		have_ioref:1;
	struct extent_ptr_decoded	pick;
	struct work_struct	work;
	struct bio		bio;
};

struct btree_write_bio {
	void			*data;
	struct work_struct	work;
	struct bch_write_bio	wbio;
};

static inline void btree_node_io_unlock(struct btree *b)
{
	EBUG_ON(!btree_node_write_in_flight(b));
	clear_btree_node_write_in_flight(b);
	wake_up_bit(&b->flags, BTREE_NODE_write_in_flight);
}

static inline void btree_node_io_lock(struct btree *b)
{
	wait_on_bit_lock_io(&b->flags, BTREE_NODE_write_in_flight,
			    TASK_UNINTERRUPTIBLE);
}

static inline void btree_node_wait_on_io(struct btree *b)
{
	wait_on_bit_io(&b->flags, BTREE_NODE_write_in_flight,
		       TASK_UNINTERRUPTIBLE);
}

static inline bool btree_node_may_write(struct btree *b)
{
	return list_empty_careful(&b->write_blocked) &&
		!b->will_make_reachable;
}

enum compact_mode {
	COMPACT_LAZY,
	COMPACT_WRITTEN,
	COMPACT_WRITTEN_NO_WRITE_LOCK,
};

bool __bch2_compact_whiteouts(struct bch_fs *, struct btree *, enum compact_mode);

static inline unsigned should_compact_bset_lazy(struct btree *b, struct bset_tree *t)
{
	unsigned bset_u64s = le16_to_cpu(bset(b, t)->u64s);
	unsigned dead_u64s = bset_u64s - b->nr.bset_u64s[t - b->set];

	return dead_u64s > 128 && dead_u64s * 3 > bset_u64s;
}

static inline bool bch2_maybe_compact_whiteouts(struct bch_fs *c, struct btree *b)
{
	struct bset_tree *t;

	for_each_bset(b, t)
		if (should_compact_bset_lazy(b, t))
			return __bch2_compact_whiteouts(c, b, COMPACT_LAZY);

	return false;
}

void bch2_btree_sort_into(struct bch_fs *, struct btree *, struct btree *);

void bch2_btree_build_aux_trees(struct btree *);
void bch2_btree_init_next(struct bch_fs *, struct btree *,
			 struct btree_iter *);

int bch2_btree_node_read_done(struct bch_fs *, struct btree *, bool);
void bch2_btree_node_read(struct bch_fs *, struct btree *, bool);
int bch2_btree_root_read(struct bch_fs *, enum btree_id,
			 const struct bkey_i *, unsigned);

void bch2_btree_complete_write(struct bch_fs *, struct btree *,
			      struct btree_write *);
void bch2_btree_write_error_work(struct work_struct *);

void __bch2_btree_node_write(struct bch_fs *, struct btree *,
			    enum six_lock_type);
bool bch2_btree_post_write_cleanup(struct bch_fs *, struct btree *);

void bch2_btree_node_write(struct bch_fs *, struct btree *,
			  enum six_lock_type);

/*
 * btree_node_dirty() can be cleared with only a read lock,
 * and for bch2_btree_node_write_cond() we want to set need_write iff it's
 * still dirty:
 */
static inline void set_btree_node_need_write_if_dirty(struct btree *b)
{
	unsigned long old, new, v = READ_ONCE(b->flags);

	do {
		old = new = v;

		if (!(old & (1 << BTREE_NODE_dirty)))
			return;

		new |= (1 << BTREE_NODE_need_write);
	} while ((v = cmpxchg(&b->flags, old, new)) != old);
}

#define bch2_btree_node_write_cond(_c, _b, cond)			\
do {									\
	while ((_b)->written && btree_node_dirty(_b) &&	(cond)) {	\
		if (!btree_node_may_write(_b)) {			\
			set_btree_node_need_write_if_dirty(_b);		\
			break;						\
		}							\
									\
		if (!btree_node_write_in_flight(_b)) {			\
			bch2_btree_node_write(_c, _b, SIX_LOCK_read);	\
			break;						\
		}							\
									\
		six_unlock_read(&(_b)->lock);				\
		btree_node_wait_on_io(_b);				\
		btree_node_lock_type(c, b, SIX_LOCK_read);		\
	}								\
} while (0)

void bch2_btree_flush_all_reads(struct bch_fs *);
void bch2_btree_flush_all_writes(struct bch_fs *);
void bch2_btree_verify_flushed(struct bch_fs *);
ssize_t bch2_dirty_btree_nodes_print(struct bch_fs *, char *);

#endif /* _BCACHEFS_BTREE_IO_H */
