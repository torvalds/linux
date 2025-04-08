/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BCACHEFS_DISK_ACCOUNTING_H
#define _BCACHEFS_DISK_ACCOUNTING_H

#include "btree_update.h"
#include "eytzinger.h"
#include "sb-members.h"

static inline void bch2_u64s_neg(u64 *v, unsigned nr)
{
	for (unsigned i = 0; i < nr; i++)
		v[i] = -v[i];
}

static inline unsigned bch2_accounting_counters(const struct bkey *k)
{
	return bkey_val_u64s(k) - offsetof(struct bch_accounting, d) / sizeof(u64);
}

static inline void bch2_accounting_neg(struct bkey_s_accounting a)
{
	bch2_u64s_neg(a.v->d, bch2_accounting_counters(a.k));
}

static inline bool bch2_accounting_key_is_zero(struct bkey_s_c_accounting a)
{
	for (unsigned i = 0;  i < bch2_accounting_counters(a.k); i++)
		if (a.v->d[i])
			return false;
	return true;
}

static inline void bch2_accounting_accumulate(struct bkey_i_accounting *dst,
					      struct bkey_s_c_accounting src)
{
	for (unsigned i = 0;
	     i < min(bch2_accounting_counters(&dst->k),
		     bch2_accounting_counters(src.k));
	     i++)
		dst->v.d[i] += src.v->d[i];

	if (bversion_cmp(dst->k.bversion, src.k->bversion) < 0)
		dst->k.bversion = src.k->bversion;
}

static inline void fs_usage_data_type_to_base(struct bch_fs_usage_base *fs_usage,
					      enum bch_data_type data_type,
					      s64 sectors)
{
	switch (data_type) {
	case BCH_DATA_btree:
		fs_usage->btree		+= sectors;
		break;
	case BCH_DATA_user:
	case BCH_DATA_parity:
		fs_usage->data		+= sectors;
		break;
	case BCH_DATA_cached:
		fs_usage->cached	+= sectors;
		break;
	default:
		break;
	}
}

static inline void bpos_to_disk_accounting_pos(struct disk_accounting_pos *acc, struct bpos p)
{
	BUILD_BUG_ON(sizeof(*acc) != sizeof(p));

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	acc->_pad = p;
#else
	memcpy_swab(acc, &p, sizeof(p));
#endif
}

static inline struct bpos disk_accounting_pos_to_bpos(struct disk_accounting_pos *acc)
{
	struct bpos p;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	p = acc->_pad;
#else
	memcpy_swab(&p, acc, sizeof(p));
#endif
	return p;
}

int bch2_disk_accounting_mod(struct btree_trans *, struct disk_accounting_pos *,
			     s64 *, unsigned, bool);

#define disk_accounting_key_init(_k, _type, ...)			\
do {									\
	memset(&(_k), 0, sizeof(_k));					\
	(_k).type	= BCH_DISK_ACCOUNTING_##_type;			\
	(_k)._type	= (struct bch_acct_##_type) { __VA_ARGS__ };	\
} while (0)

#define bch2_disk_accounting_mod2_nr(_trans, _gc, _v, _nr, ...)		\
({									\
	struct disk_accounting_pos pos;					\
	disk_accounting_key_init(pos, __VA_ARGS__);			\
	bch2_disk_accounting_mod(trans, &pos, _v, _nr, _gc);		\
})

#define bch2_disk_accounting_mod2(_trans, _gc, _v, ...)			\
	bch2_disk_accounting_mod2_nr(_trans, _gc, _v, ARRAY_SIZE(_v), __VA_ARGS__)

int bch2_mod_dev_cached_sectors(struct btree_trans *, unsigned, s64, bool);

int bch2_accounting_validate(struct bch_fs *, struct bkey_s_c,
			     struct bkey_validate_context);
void bch2_accounting_key_to_text(struct printbuf *, struct disk_accounting_pos *);
void bch2_accounting_to_text(struct printbuf *, struct bch_fs *, struct bkey_s_c);
void bch2_accounting_swab(struct bkey_s);

#define bch2_bkey_ops_accounting ((struct bkey_ops) {	\
	.key_validate	= bch2_accounting_validate,	\
	.val_to_text	= bch2_accounting_to_text,	\
	.swab		= bch2_accounting_swab,		\
	.min_val_size	= 8,				\
})

int bch2_accounting_update_sb(struct btree_trans *);

static inline int accounting_pos_cmp(const void *_l, const void *_r)
{
	const struct bpos *l = _l, *r = _r;

	return bpos_cmp(*l, *r);
}

enum bch_accounting_mode {
	BCH_ACCOUNTING_normal,
	BCH_ACCOUNTING_gc,
	BCH_ACCOUNTING_read,
};

int bch2_accounting_mem_insert(struct bch_fs *, struct bkey_s_c_accounting, enum bch_accounting_mode);
void bch2_accounting_mem_gc(struct bch_fs *);

static inline bool bch2_accounting_is_mem(struct disk_accounting_pos acc)
{
	return acc.type < BCH_DISK_ACCOUNTING_TYPE_NR &&
		acc.type != BCH_DISK_ACCOUNTING_inum;
}

/*
 * Update in memory counters so they match the btree update we're doing; called
 * from transaction commit path
 */
static inline int bch2_accounting_mem_mod_locked(struct btree_trans *trans,
						 struct bkey_s_c_accounting a,
						 enum bch_accounting_mode mode)
{
	struct bch_fs *c = trans->c;
	struct bch_accounting_mem *acc = &c->accounting;
	struct disk_accounting_pos acc_k;
	bpos_to_disk_accounting_pos(&acc_k, a.k->p);
	bool gc = mode == BCH_ACCOUNTING_gc;

	if (gc && !acc->gc_running)
		return 0;

	if (!bch2_accounting_is_mem(acc_k))
		return 0;

	if (mode == BCH_ACCOUNTING_normal) {
		switch (acc_k.type) {
		case BCH_DISK_ACCOUNTING_persistent_reserved:
			trans->fs_usage_delta.reserved += acc_k.persistent_reserved.nr_replicas * a.v->d[0];
			break;
		case BCH_DISK_ACCOUNTING_replicas:
			fs_usage_data_type_to_base(&trans->fs_usage_delta, acc_k.replicas.data_type, a.v->d[0]);
			break;
		case BCH_DISK_ACCOUNTING_dev_data_type:
			rcu_read_lock();
			struct bch_dev *ca = bch2_dev_rcu_noerror(c, acc_k.dev_data_type.dev);
			if (ca) {
				this_cpu_add(ca->usage->d[acc_k.dev_data_type.data_type].buckets, a.v->d[0]);
				this_cpu_add(ca->usage->d[acc_k.dev_data_type.data_type].sectors, a.v->d[1]);
				this_cpu_add(ca->usage->d[acc_k.dev_data_type.data_type].fragmented, a.v->d[2]);
			}
			rcu_read_unlock();
			break;
		}
	}

	unsigned idx;

	while ((idx = eytzinger0_find(acc->k.data, acc->k.nr, sizeof(acc->k.data[0]),
				      accounting_pos_cmp, &a.k->p)) >= acc->k.nr) {
		int ret = bch2_accounting_mem_insert(c, a, mode);
		if (ret)
			return ret;
	}

	struct accounting_mem_entry *e = &acc->k.data[idx];

	EBUG_ON(bch2_accounting_counters(a.k) != e->nr_counters);

	for (unsigned i = 0; i < bch2_accounting_counters(a.k); i++)
		this_cpu_add(e->v[gc][i], a.v->d[i]);
	return 0;
}

static inline int bch2_accounting_mem_add(struct btree_trans *trans, struct bkey_s_c_accounting a, bool gc)
{
	percpu_down_read(&trans->c->mark_lock);
	int ret = bch2_accounting_mem_mod_locked(trans, a, gc ? BCH_ACCOUNTING_gc : BCH_ACCOUNTING_normal);
	percpu_up_read(&trans->c->mark_lock);
	return ret;
}

static inline void bch2_accounting_mem_read_counters(struct bch_accounting_mem *acc,
						     unsigned idx, u64 *v, unsigned nr, bool gc)
{
	memset(v, 0, sizeof(*v) * nr);

	if (unlikely(idx >= acc->k.nr))
		return;

	struct accounting_mem_entry *e = &acc->k.data[idx];

	nr = min_t(unsigned, nr, e->nr_counters);

	for (unsigned i = 0; i < nr; i++)
		v[i] = percpu_u64_get(e->v[gc] + i);
}

static inline void bch2_accounting_mem_read(struct bch_fs *c, struct bpos p,
					    u64 *v, unsigned nr)
{
	percpu_down_read(&c->mark_lock);
	struct bch_accounting_mem *acc = &c->accounting;
	unsigned idx = eytzinger0_find(acc->k.data, acc->k.nr, sizeof(acc->k.data[0]),
				       accounting_pos_cmp, &p);

	bch2_accounting_mem_read_counters(acc, idx, v, nr, false);
	percpu_up_read(&c->mark_lock);
}

static inline struct bversion journal_pos_to_bversion(struct journal_res *res, unsigned offset)
{
	EBUG_ON(!res->ref);

	return (struct bversion) {
		.hi = res->seq >> 32,
		.lo = (res->seq << 32) | (res->offset + offset),
	};
}

static inline int bch2_accounting_trans_commit_hook(struct btree_trans *trans,
						    struct bkey_i_accounting *a,
						    unsigned commit_flags)
{
	a->k.bversion = journal_pos_to_bversion(&trans->journal_res,
						(u64 *) a - (u64 *) trans->journal_entries);

	EBUG_ON(bversion_zero(a->k.bversion));

	return likely(!(commit_flags & BCH_TRANS_COMMIT_skip_accounting_apply))
		? bch2_accounting_mem_mod_locked(trans, accounting_i_to_s_c(a), BCH_ACCOUNTING_normal)
		: 0;
}

static inline void bch2_accounting_trans_commit_revert(struct btree_trans *trans,
						       struct bkey_i_accounting *a_i,
						       unsigned commit_flags)
{
	if (likely(!(commit_flags & BCH_TRANS_COMMIT_skip_accounting_apply))) {
		struct bkey_s_accounting a = accounting_i_to_s(a_i);

		bch2_accounting_neg(a);
		bch2_accounting_mem_mod_locked(trans, a.c, BCH_ACCOUNTING_normal);
		bch2_accounting_neg(a);
	}
}

int bch2_fs_replicas_usage_read(struct bch_fs *, darray_char *);
int bch2_fs_accounting_read(struct bch_fs *, darray_char *, unsigned);

int bch2_gc_accounting_start(struct bch_fs *);
int bch2_gc_accounting_done(struct bch_fs *);

int bch2_accounting_read(struct bch_fs *);

int bch2_dev_usage_remove(struct bch_fs *, unsigned);
int bch2_dev_usage_init(struct bch_dev *, bool);

void bch2_verify_accounting_clean(struct bch_fs *c);

void bch2_accounting_gc_free(struct bch_fs *);
void bch2_fs_accounting_exit(struct bch_fs *);

#endif /* _BCACHEFS_DISK_ACCOUNTING_H */
