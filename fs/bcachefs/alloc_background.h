/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_BACKGROUND_H
#define _BCACHEFS_ALLOC_BACKGROUND_H

#include "bcachefs.h"
#include "alloc_types.h"
#include "debug.h"

struct bkey_alloc_unpacked {
	u8		gen;
#define x(_name, _bits)	u##_bits _name;
	BCH_ALLOC_FIELDS()
#undef  x
};

/* returns true if not equal */
static inline bool bkey_alloc_unpacked_cmp(struct bkey_alloc_unpacked l,
					   struct bkey_alloc_unpacked r)
{
	return l.gen != r.gen
#define x(_name, _bits)	|| l._name != r._name
	BCH_ALLOC_FIELDS()
#undef  x
	;
}

struct bkey_alloc_unpacked bch2_alloc_unpack(struct bkey_s_c);
void bch2_alloc_pack(struct bkey_i_alloc *,
		     const struct bkey_alloc_unpacked);

static inline struct bkey_alloc_unpacked
alloc_mem_to_key(struct bucket *g, struct bucket_mark m)
{
	return (struct bkey_alloc_unpacked) {
		.gen		= m.gen,
		.oldest_gen	= g->oldest_gen,
		.data_type	= m.data_type,
		.dirty_sectors	= m.dirty_sectors,
		.cached_sectors	= m.cached_sectors,
		.read_time	= g->io_time[READ],
		.write_time	= g->io_time[WRITE],
	};
}

#define ALLOC_SCAN_BATCH(ca)		max_t(size_t, 1, (ca)->mi.nbuckets >> 9)

const char *bch2_alloc_invalid(const struct bch_fs *, struct bkey_s_c);
void bch2_alloc_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);

#define bch2_bkey_ops_alloc (struct bkey_ops) {		\
	.key_invalid	= bch2_alloc_invalid,		\
	.val_to_text	= bch2_alloc_to_text,		\
}

struct journal_keys;
int bch2_alloc_read(struct bch_fs *, struct journal_keys *);
int bch2_alloc_replay_key(struct bch_fs *, struct bkey_i *);

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
	if (expensive_debug_checks(c)) {
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

int bch2_alloc_write(struct bch_fs *, unsigned, bool *);
void bch2_fs_allocator_background_init(struct bch_fs *);

#endif /* _BCACHEFS_ALLOC_BACKGROUND_H */
