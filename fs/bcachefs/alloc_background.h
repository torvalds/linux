/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_ALLOC_BACKGROUND_H
#define _BCACHEFS_ALLOC_BACKGROUND_H

#include "bcachefs.h"
#include "alloc_types.h"
#include "buckets.h"
#include "debug.h"
#include "super.h"

extern const char * const bch2_allocator_states[];

struct bkey_alloc_unpacked {
	u64		journal_seq;
	u64		bucket;
	u8		dev;
	u8		gen;
	u8		oldest_gen;
	u8		data_type;
#define x(_name, _bits)	u##_bits _name;
	BCH_ALLOC_FIELDS_V2()
#undef  x
};

/* How out of date a pointer gen is allowed to be: */
#define BUCKET_GC_GEN_MAX	96U

/* returns true if not equal */
static inline bool bkey_alloc_unpacked_cmp(struct bkey_alloc_unpacked l,
					   struct bkey_alloc_unpacked r)
{
	return  l.gen != r.gen			||
		l.oldest_gen != r.oldest_gen	||
		l.data_type != r.data_type
#define x(_name, ...)	|| l._name != r._name
	BCH_ALLOC_FIELDS_V2()
#undef  x
	;
}

struct bkey_alloc_buf {
	struct bkey_i	k;
	struct bch_alloc_v3 v;

#define x(_name,  _bits)		+ _bits / 8
	u8		_pad[0 + BCH_ALLOC_FIELDS_V2()];
#undef  x
} __attribute__((packed, aligned(8)));

struct bkey_alloc_unpacked bch2_alloc_unpack(struct bkey_s_c);
struct bkey_alloc_buf *bch2_alloc_pack(struct btree_trans *,
				       const struct bkey_alloc_unpacked);
int bch2_alloc_write(struct btree_trans *, struct btree_iter *,
		     struct bkey_alloc_unpacked *, unsigned);

int bch2_bucket_io_time_reset(struct btree_trans *, unsigned, size_t, int);

#define ALLOC_SCAN_BATCH(ca)		max_t(size_t, 1, (ca)->mi.nbuckets >> 9)

const char *bch2_alloc_v1_invalid(const struct bch_fs *, struct bkey_s_c);
const char *bch2_alloc_v2_invalid(const struct bch_fs *, struct bkey_s_c);
const char *bch2_alloc_v3_invalid(const struct bch_fs *, struct bkey_s_c);
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
