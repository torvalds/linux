/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_BTREE_GC_H
#define _BCACHEFS_BTREE_GC_H

#include "bkey.h"
#include "btree_gc_types.h"
#include "btree_types.h"

int bch2_check_topology(struct bch_fs *);
int bch2_check_allocations(struct bch_fs *);

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
	return (struct gc_pos) { .phase	= phase, };
}

static inline struct gc_pos gc_pos_btree(enum btree_id btree, unsigned level,
					 struct bpos pos)
{
	return (struct gc_pos) {
		.phase	= GC_PHASE_btree,
		.btree	= btree,
		.level	= level,
		.pos	= pos,
	};
}

static inline int gc_btree_order(enum btree_id btree)
{
	if (btree == BTREE_ID_alloc)
		return -2;
	if (btree == BTREE_ID_stripes)
		return -1;
	return btree;
}

static inline int gc_pos_cmp(struct gc_pos l, struct gc_pos r)
{
	return  cmp_int(l.phase, r.phase) ?:
		cmp_int(gc_btree_order(l.btree),
			gc_btree_order(r.btree)) ?:
		cmp_int(l.level, r.level) ?:
		bpos_cmp(l.pos, r.pos);
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

void bch2_gc_pos_to_text(struct printbuf *, struct gc_pos *);

int bch2_gc_gens(struct bch_fs *);
void bch2_gc_gens_async(struct bch_fs *);
void bch2_fs_gc_init(struct bch_fs *);

#endif /* _BCACHEFS_BTREE_GC_H */
