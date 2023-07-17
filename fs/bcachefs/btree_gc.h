/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_GC_H
#define _BCACHEFS_BTREE_GC_H

#include "btree_types.h"

int bch2_check_topology(struct bch_fs *);
int bch2_gc(struct bch_fs *, bool, bool);
int bch2_gc_gens(struct bch_fs *);
void bch2_gc_thread_stop(struct bch_fs *);
int bch2_gc_thread_start(struct bch_fs *);

/*
 * For concurrent mark and sweep (with other index updates), we define a total
 * ordering of _all_ references GC walks:
 *
 * Note that some references will have the same GC position as others - e.g.
 * everything within the same btree node; in those cases we're relying on
 * whatever locking exists for where those references live, i.e. the write lock
 * on a btree node.
 *
 * That locking is also required to ensure GC doesn't pass the updater in
 * between the updater adding/removing the reference and updating the GC marks;
 * without that, we would at best double count sometimes.
 *
 * That part is important - whenever calling bch2_mark_pointers(), a lock _must_
 * be held that prevents GC from passing the position the updater is at.
 *
 * (What about the start of gc, when we're clearing all the marks? GC clears the
 * mark with the gc pos seqlock held, and bch_mark_bucket checks against the gc
 * position inside its cmpxchg loop, so crap magically works).
 */

/* Position of (the start of) a gc phase: */
static inline struct gc_pos gc_phase(enum gc_phase phase)
{
	return (struct gc_pos) {
		.phase	= phase,
		.pos	= POS_MIN,
		.level	= 0,
	};
}

static inline int gc_pos_cmp(struct gc_pos l, struct gc_pos r)
{
	return  cmp_int(l.phase, r.phase) ?:
		bpos_cmp(l.pos, r.pos) ?:
		cmp_int(l.level, r.level);
}

static inline enum gc_phase btree_id_to_gc_phase(enum btree_id id)
{
	switch (id) {
#define x(name, v) case BTREE_ID_##name: return GC_PHASE_BTREE_##name;
	BCH_BTREE_IDS()
#undef x
	default:
		BUG();
	}
}

static inline struct gc_pos gc_pos_btree(enum btree_id id,
					 struct bpos pos, unsigned level)
{
	return (struct gc_pos) {
		.phase	= btree_id_to_gc_phase(id),
		.pos	= pos,
		.level	= level,
	};
}

/*
 * GC position of the pointers within a btree node: note, _not_ for &b->key
 * itself, that lives in the parent node:
 */
static inline struct gc_pos gc_pos_btree_node(struct btree *b)
{
	return gc_pos_btree(b->c.btree_id, b->key.k.p, b->c.level);
}

/*
 * GC position of the pointer to a btree root: we don't use
 * gc_pos_pointer_to_btree_node() here to avoid a potential race with
 * btree_split() increasing the tree depth - the new root will have level > the
 * old root and thus have a greater gc position than the old root, but that
 * would be incorrect since once gc has marked the root it's not coming back.
 */
static inline struct gc_pos gc_pos_btree_root(enum btree_id id)
{
	return gc_pos_btree(id, SPOS_MAX, BTREE_MAX_DEPTH);
}

static inline bool gc_visited(struct bch_fs *c, struct gc_pos pos)
{
	unsigned seq;
	bool ret;

	do {
		seq = read_seqcount_begin(&c->gc_pos_lock);
		ret = gc_pos_cmp(pos, c->gc_pos) <= 0;
	} while (read_seqcount_retry(&c->gc_pos_lock, seq));

	return ret;
}

static inline void bch2_do_gc_gens(struct bch_fs *c)
{
	atomic_inc(&c->kick_gc);
	if (c->gc_thread)
		wake_up_process(c->gc_thread);
}

#endif /* _BCACHEFS_BTREE_GC_H */
