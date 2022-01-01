/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_BACKGROUND_H
#define _BCACHEFS_ALLOC_BACKGROUND_H

#include "bcachefs.h"
#include "alloc_types.h"
#include "buckets.h"
#include "debug.h"
#include "super.h"

extern const char * const bch2_allocator_states[];

/* How out of date a pointer gen is allowed to be: */
#define BUCKET_GC_GEN_MAX	96U

struct bkey_i_alloc_v4 *
bch2_trans_start_alloc_update(struct btree_trans *, struct btree_iter *, struct bpos);

void bch2_alloc_to_v4(struct bkey_s_c, struct bch_alloc_v4 *);
struct bkey_i_alloc_v4 *bch2_alloc_to_v4_mut(struct btree_trans *, struct bkey_s_c);

int bch2_bucket_io_time_reset(struct btree_trans *, unsigned, size_t, int);

#define ALLOC_SCAN_BATCH(ca)		max_t(size_t, 1, (ca)->mi.nbuckets >> 9)

const char *bch2_alloc_v1_invalid(const struct bch_fs *, struct bkey_s_c);
const char *bch2_alloc_v2_invalid(const struct bch_fs *, struct bkey_s_c);
const char *bch2_alloc_v3_invalid(const struct bch_fs *, struct bkey_s_c);
const char *bch2_alloc_v4_invalid(const struct bch_fs *, struct bkey_s_c k);
void bch2_alloc_v4_swab(struct bkey_s);
void bch2_alloc_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_alloc (struct bkey_ops) {		\
	.key_invalid	= bch2_alloc_v1_invalid,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.atomic_trigger	= bch2_mark_alloc,		\
}

#define bch2_bkey_ops_alloc_v2 (struct bkey_ops) {	\
	.key_invalid	= bch2_alloc_v2_invalid,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.atomic_trigger	= bch2_mark_alloc,		\
}

#define bch2_bkey_ops_alloc_v3 (struct bkey_ops) {	\
	.key_invalid	= bch2_alloc_v3_invalid,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.atomic_trigger	= bch2_mark_alloc,		\
}

#define bch2_bkey_ops_alloc_v4 (struct bkey_ops) {	\
	.key_invalid	= bch2_alloc_v4_invalid,	\
	.val_to_text	= bch2_alloc_to_text,		\
	.swab		= bch2_alloc_v4_swab,		\
	.atomic_trigger	= bch2_mark_alloc,		\
}

static inline bool bkey_is_alloc(const struct bkey *k)
{
	return  k->type == KEY_TYPE_alloc ||
		k->type == KEY_TYPE_alloc_v2 ||
		k->type == KEY_TYPE_alloc_v3;
}

int bch2_alloc_read(struct bch_fs *, bool, bool);

static inline void bch2_wake_allocator(struct bch_dev *ca)
{
	struct task_struct *p;

	rcu_read_lock();
	p = rcu_dereference(ca->alloc_thread);
	if (p)
		wake_up_process(p);
	rcu_read_unlock();
}

static inline void verify_not_on_freelist(struct bch_fs *c, struct bch_dev *ca,
					  size_t bucket)
{
	if (bch2_expensive_debug_checks) {
		size_t iter;
		long i;
		unsigned j;

		for (j = 0; j < RESERVE_NR; j++)
			fifo_for_each_entry(i, &ca->free[j], iter)
				BUG_ON(i == bucket);
		fifo_for_each_entry(i, &ca->free_inc, iter)
			BUG_ON(i == bucket);
	}
}

void bch2_recalc_capacity(struct bch_fs *);

void bch2_dev_allocator_remove(struct bch_fs *, struct bch_dev *);
void bch2_dev_allocator_add(struct bch_fs *, struct bch_dev *);

void bch2_dev_allocator_quiesce(struct bch_fs *, struct bch_dev *);
void bch2_dev_allocator_stop(struct bch_dev *);
int bch2_dev_allocator_start(struct bch_dev *);

void bch2_fs_allocator_background_init(struct bch_fs *);

#endif /* _BCACHEFS_ALLOC_BACKGROUND_H */
