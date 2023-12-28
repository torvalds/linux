// SPDX-License-Identifier: GPL-2.0
/*
 * Code for manipulating bucket marks for garbage collection.
 *
 * Copyright 2014 Datera, Inc.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "backpointers.h"
#include "bset.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "buckets.h"
#include "buckets_waiting_for_journal.h"
#include "ec.h"
#include "error.h"
#include "inode.h"
#include "movinggc.h"
#include "recovery.h"
#include "reflink.h"
#include "replicas.h"
#include "subvolume.h"
#include "trace.h"

#include <linux/preempt.h>

static inline void fs_usage_data_type_to_base(struct bch_fs_usage *fs_usage,
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

void bch2_fs_usage_initialize(struct bch_fs *c)
{
	percpu_down_write(&c->mark_lock);
	struct bch_fs_usage *usage = c->usage_base;

	for (unsigned i = 0; i < ARRAY_SIZE(c->usage); i++)
		bch2_fs_usage_acc_to_base(c, i);

	for (unsigned i = 0; i < BCH_REPLICAS_MAX; i++)
		usage->reserved += usage->persistent_reserved[i];

	for (unsigned i = 0; i < c->replicas.nr; i++) {
		struct bch_replicas_entry_v1 *e =
			cpu_replicas_entry(&c->replicas, i);

		fs_usage_data_type_to_base(usage, e->data_type, usage->replicas[i]);
	}

	for_each_member_device(c, ca) {
		struct bch_dev_usage dev = bch2_dev_usage_read(ca);

		usage->hidden += (dev.d[BCH_DATA_sb].buckets +
				  dev.d[BCH_DATA_journal].buckets) *
			ca->mi.bucket_size;
	}

	percpu_up_write(&c->mark_lock);
}

static inline struct bch_dev_usage *dev_usage_ptr(struct bch_dev *ca,
						  unsigned journal_seq,
						  bool gc)
{
	BUG_ON(!gc && !journal_seq);

	return this_cpu_ptr(gc
			    ? ca->usage_gc
			    : ca->usage[journal_seq & JOURNAL_BUF_MASK]);
}

void bch2_dev_usage_read_fast(struct bch_dev *ca, struct bch_dev_usage *usage)
{
	struct bch_fs *c = ca->fs;
	unsigned seq, i, u64s = dev_usage_u64s();

	do {
		seq = read_seqcount_begin(&c->usage_lock);
		memcpy(usage, ca->usage_base, u64s * sizeof(u64));
		for (i = 0; i < ARRAY_SIZE(ca->usage); i++)
			acc_u64s_percpu((u64 *) usage, (u64 __percpu *) ca->usage[i], u64s);
	} while (read_seqcount_retry(&c->usage_lock, seq));
}

u64 bch2_fs_usage_read_one(struct bch_fs *c, u64 *v)
{
	ssize_t offset = v - (u64 *) c->usage_base;
	unsigned i, seq;
	u64 ret;

	BUG_ON(offset < 0 || offset >= fs_usage_u64s(c));
	percpu_rwsem_assert_held(&c->mark_lock);

	do {
		seq = read_seqcount_begin(&c->usage_lock);
		ret = *v;

		for (i = 0; i < ARRAY_SIZE(c->usage); i++)
			ret += percpu_u64_get((u64 __percpu *) c->usage[i] + offset);
	} while (read_seqcount_retry(&c->usage_lock, seq));

	return ret;
}

struct bch_fs_usage_online *bch2_fs_usage_read(struct bch_fs *c)
{
	struct bch_fs_usage_online *ret;
	unsigned nr_replicas = READ_ONCE(c->replicas.nr);
	unsigned seq, i;
retry:
	ret = kmalloc(__fs_usage_online_u64s(nr_replicas) * sizeof(u64), GFP_KERNEL);
	if (unlikely(!ret))
		return NULL;

	percpu_down_read(&c->mark_lock);

	if (nr_replicas != c->replicas.nr) {
		nr_replicas = c->replicas.nr;
		percpu_up_read(&c->mark_lock);
		kfree(ret);
		goto retry;
	}

	ret->online_reserved = percpu_u64_get(c->online_reserved);

	do {
		seq = read_seqcount_begin(&c->usage_lock);
		unsafe_memcpy(&ret->u, c->usage_base,
			      __fs_usage_u64s(nr_replicas) * sizeof(u64),
			      "embedded variable length struct");
		for (i = 0; i < ARRAY_SIZE(c->usage); i++)
			acc_u64s_percpu((u64 *) &ret->u, (u64 __percpu *) c->usage[i],
					__fs_usage_u64s(nr_replicas));
	} while (read_seqcount_retry(&c->usage_lock, seq));

	return ret;
}

void bch2_fs_usage_acc_to_base(struct bch_fs *c, unsigned idx)
{
	unsigned u64s = fs_usage_u64s(c);

	BUG_ON(idx >= ARRAY_SIZE(c->usage));

	preempt_disable();
	write_seqcount_begin(&c->usage_lock);

	acc_u64s_percpu((u64 *) c->usage_base,
			(u64 __percpu *) c->usage[idx], u64s);
	percpu_memset(c->usage[idx], 0, u64s * sizeof(u64));

	rcu_read_lock();
	for_each_member_device_rcu(c, ca, NULL) {
		u64s = dev_usage_u64s();

		acc_u64s_percpu((u64 *) ca->usage_base,
				(u64 __percpu *) ca->usage[idx], u64s);
		percpu_memset(ca->usage[idx], 0, u64s * sizeof(u64));
	}
	rcu_read_unlock();

	write_seqcount_end(&c->usage_lock);
	preempt_enable();
}

void bch2_fs_usage_to_text(struct printbuf *out,
			   struct bch_fs *c,
			   struct bch_fs_usage_online *fs_usage)
{
	unsigned i;

	prt_printf(out, "capacity:\t\t\t%llu\n", c->capacity);

	prt_printf(out, "hidden:\t\t\t\t%llu\n",
	       fs_usage->u.hidden);
	prt_printf(out, "data:\t\t\t\t%llu\n",
	       fs_usage->u.data);
	prt_printf(out, "cached:\t\t\t\t%llu\n",
	       fs_usage->u.cached);
	prt_printf(out, "reserved:\t\t\t%llu\n",
	       fs_usage->u.reserved);
	prt_printf(out, "nr_inodes:\t\t\t%llu\n",
	       fs_usage->u.nr_inodes);
	prt_printf(out, "online reserved:\t\t%llu\n",
	       fs_usage->online_reserved);

	for (i = 0;
	     i < ARRAY_SIZE(fs_usage->u.persistent_reserved);
	     i++) {
		prt_printf(out, "%u replicas:\n", i + 1);
		prt_printf(out, "\treserved:\t\t%llu\n",
		       fs_usage->u.persistent_reserved[i]);
	}

	for (i = 0; i < c->replicas.nr; i++) {
		struct bch_replicas_entry_v1 *e =
			cpu_replicas_entry(&c->replicas, i);

		prt_printf(out, "\t");
		bch2_replicas_entry_to_text(out, e);
		prt_printf(out, ":\t%llu\n", fs_usage->u.replicas[i]);
	}
}

static u64 reserve_factor(u64 r)
{
	return r + (round_up(r, (1 << RESERVE_FACTOR)) >> RESERVE_FACTOR);
}

u64 bch2_fs_sectors_used(struct bch_fs *c, struct bch_fs_usage_online *fs_usage)
{
	return min(fs_usage->u.hidden +
		   fs_usage->u.btree +
		   fs_usage->u.data +
		   reserve_factor(fs_usage->u.reserved +
				  fs_usage->online_reserved),
		   c->capacity);
}

static struct bch_fs_usage_short
__bch2_fs_usage_read_short(struct bch_fs *c)
{
	struct bch_fs_usage_short ret;
	u64 data, reserved;

	ret.capacity = c->capacity -
		bch2_fs_usage_read_one(c, &c->usage_base->hidden);

	data		= bch2_fs_usage_read_one(c, &c->usage_base->data) +
		bch2_fs_usage_read_one(c, &c->usage_base->btree);
	reserved	= bch2_fs_usage_read_one(c, &c->usage_base->reserved) +
		percpu_u64_get(c->online_reserved);

	ret.used	= min(ret.capacity, data + reserve_factor(reserved));
	ret.free	= ret.capacity - ret.used;

	ret.nr_inodes	= bch2_fs_usage_read_one(c, &c->usage_base->nr_inodes);

	return ret;
}

struct bch_fs_usage_short
bch2_fs_usage_read_short(struct bch_fs *c)
{
	struct bch_fs_usage_short ret;

	percpu_down_read(&c->mark_lock);
	ret = __bch2_fs_usage_read_short(c);
	percpu_up_read(&c->mark_lock);

	return ret;
}

void bch2_dev_usage_init(struct bch_dev *ca)
{
	ca->usage_base->d[BCH_DATA_free].buckets = ca->mi.nbuckets - ca->mi.first_bucket;
}

void bch2_dev_usage_to_text(struct printbuf *out, struct bch_dev_usage *usage)
{
	prt_tab(out);
	prt_str(out, "buckets");
	prt_tab_rjust(out);
	prt_str(out, "sectors");
	prt_tab_rjust(out);
	prt_str(out, "fragmented");
	prt_tab_rjust(out);
	prt_newline(out);

	for (unsigned i = 0; i < BCH_DATA_NR; i++) {
		prt_str(out, bch2_data_types[i]);
		prt_tab(out);
		prt_u64(out, usage->d[i].buckets);
		prt_tab_rjust(out);
		prt_u64(out, usage->d[i].sectors);
		prt_tab_rjust(out);
		prt_u64(out, usage->d[i].fragmented);
		prt_tab_rjust(out);
		prt_newline(out);
	}
}

void bch2_dev_usage_update(struct bch_fs *c, struct bch_dev *ca,
			   const struct bch_alloc_v4 *old,
			   const struct bch_alloc_v4 *new,
			   u64 journal_seq, bool gc)
{
	struct bch_fs_usage *fs_usage;
	struct bch_dev_usage *u;

	preempt_disable();
	fs_usage = fs_usage_ptr(c, journal_seq, gc);

	if (data_type_is_hidden(old->data_type))
		fs_usage->hidden -= ca->mi.bucket_size;
	if (data_type_is_hidden(new->data_type))
		fs_usage->hidden += ca->mi.bucket_size;

	u = dev_usage_ptr(ca, journal_seq, gc);

	u->d[old->data_type].buckets--;
	u->d[new->data_type].buckets++;

	u->d[old->data_type].sectors -= bch2_bucket_sectors_dirty(*old);
	u->d[new->data_type].sectors += bch2_bucket_sectors_dirty(*new);

	u->d[BCH_DATA_cached].sectors += new->cached_sectors;
	u->d[BCH_DATA_cached].sectors -= old->cached_sectors;

	u->d[old->data_type].fragmented -= bch2_bucket_sectors_fragmented(ca, *old);
	u->d[new->data_type].fragmented += bch2_bucket_sectors_fragmented(ca, *new);

	preempt_enable();
}

static inline struct bch_alloc_v4 bucket_m_to_alloc(struct bucket b)
{
	return (struct bch_alloc_v4) {
		.gen		= b.gen,
		.data_type	= b.data_type,
		.dirty_sectors	= b.dirty_sectors,
		.cached_sectors	= b.cached_sectors,
		.stripe		= b.stripe,
	};
}

void bch2_dev_usage_update_m(struct bch_fs *c, struct bch_dev *ca,
			     struct bucket *old, struct bucket *new)
{
	struct bch_alloc_v4 old_a = bucket_m_to_alloc(*old);
	struct bch_alloc_v4 new_a = bucket_m_to_alloc(*new);

	bch2_dev_usage_update(c, ca, &old_a, &new_a, 0, true);
}

static inline int __update_replicas(struct bch_fs *c,
				    struct bch_fs_usage *fs_usage,
				    struct bch_replicas_entry_v1 *r,
				    s64 sectors)
{
	int idx = bch2_replicas_entry_idx(c, r);

	if (idx < 0)
		return -1;

	fs_usage_data_type_to_base(fs_usage, r->data_type, sectors);
	fs_usage->replicas[idx]		+= sectors;
	return 0;
}

int bch2_update_replicas(struct bch_fs *c, struct bkey_s_c k,
			 struct bch_replicas_entry_v1 *r, s64 sectors,
			 unsigned journal_seq, bool gc)
{
	struct bch_fs_usage *fs_usage;
	int idx, ret = 0;
	struct printbuf buf = PRINTBUF;

	percpu_down_read(&c->mark_lock);

	idx = bch2_replicas_entry_idx(c, r);
	if (idx < 0 &&
	    fsck_err(c, ptr_to_missing_replicas_entry,
		     "no replicas entry\n  while marking %s",
		     (bch2_bkey_val_to_text(&buf, c, k), buf.buf))) {
		percpu_up_read(&c->mark_lock);
		ret = bch2_mark_replicas(c, r);
		percpu_down_read(&c->mark_lock);

		if (ret)
			goto err;
		idx = bch2_replicas_entry_idx(c, r);
	}
	if (idx < 0) {
		ret = -1;
		goto err;
	}

	preempt_disable();
	fs_usage = fs_usage_ptr(c, journal_seq, gc);
	fs_usage_data_type_to_base(fs_usage, r->data_type, sectors);
	fs_usage->replicas[idx]		+= sectors;
	preempt_enable();
err:
fsck_err:
	percpu_up_read(&c->mark_lock);
	printbuf_exit(&buf);
	return ret;
}

static inline int update_cached_sectors(struct bch_fs *c,
			struct bkey_s_c k,
			unsigned dev, s64 sectors,
			unsigned journal_seq, bool gc)
{
	struct bch_replicas_padded r;

	bch2_replicas_entry_cached(&r.e, dev);

	return bch2_update_replicas(c, k, &r.e, sectors, journal_seq, gc);
}

static int __replicas_deltas_realloc(struct btree_trans *trans, unsigned more,
				     gfp_t gfp)
{
	struct replicas_delta_list *d = trans->fs_usage_deltas;
	unsigned new_size = d ? (d->size + more) * 2 : 128;
	unsigned alloc_size = sizeof(*d) + new_size;

	WARN_ON_ONCE(alloc_size > REPLICAS_DELTA_LIST_MAX);

	if (!d || d->used + more > d->size) {
		d = krealloc(d, alloc_size, gfp|__GFP_ZERO);

		if (unlikely(!d)) {
			if (alloc_size > REPLICAS_DELTA_LIST_MAX)
				return -ENOMEM;

			d = mempool_alloc(&trans->c->replicas_delta_pool, gfp);
			if (!d)
				return -ENOMEM;

			memset(d, 0, REPLICAS_DELTA_LIST_MAX);

			if (trans->fs_usage_deltas)
				memcpy(d, trans->fs_usage_deltas,
				       trans->fs_usage_deltas->size + sizeof(*d));

			new_size = REPLICAS_DELTA_LIST_MAX - sizeof(*d);
			kfree(trans->fs_usage_deltas);
		}

		d->size = new_size;
		trans->fs_usage_deltas = d;
	}

	return 0;
}

int bch2_replicas_deltas_realloc(struct btree_trans *trans, unsigned more)
{
	return allocate_dropping_locks_errcode(trans,
				__replicas_deltas_realloc(trans, more, _gfp));
}

int bch2_update_replicas_list(struct btree_trans *trans,
			 struct bch_replicas_entry_v1 *r,
			 s64 sectors)
{
	struct replicas_delta_list *d;
	struct replicas_delta *n;
	unsigned b;
	int ret;

	if (!sectors)
		return 0;

	b = replicas_entry_bytes(r) + 8;
	ret = bch2_replicas_deltas_realloc(trans, b);
	if (ret)
		return ret;

	d = trans->fs_usage_deltas;
	n = (void *) d->d + d->used;
	n->delta = sectors;
	unsafe_memcpy((void *) n + offsetof(struct replicas_delta, r),
		      r, replicas_entry_bytes(r),
		      "flexible array member embedded in strcuct with padding");
	bch2_replicas_entry_sort(&n->r);
	d->used += b;
	return 0;
}

int bch2_update_cached_sectors_list(struct btree_trans *trans, unsigned dev, s64 sectors)
{
	struct bch_replicas_padded r;

	bch2_replicas_entry_cached(&r.e, dev);

	return bch2_update_replicas_list(trans, &r.e, sectors);
}

int bch2_mark_metadata_bucket(struct bch_fs *c, struct bch_dev *ca,
			      size_t b, enum bch_data_type data_type,
			      unsigned sectors, struct gc_pos pos,
			      unsigned flags)
{
	struct bucket old, new, *g;
	int ret = 0;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));
	BUG_ON(data_type != BCH_DATA_sb &&
	       data_type != BCH_DATA_journal);

	/*
	 * Backup superblock might be past the end of our normal usable space:
	 */
	if (b >= ca->mi.nbuckets)
		return 0;

	percpu_down_read(&c->mark_lock);
	g = gc_bucket(ca, b);

	bucket_lock(g);
	old = *g;

	if (bch2_fs_inconsistent_on(g->data_type &&
			g->data_type != data_type, c,
			"different types of data in same bucket: %s, %s",
			bch2_data_types[g->data_type],
			bch2_data_types[data_type])) {
		ret = -EIO;
		goto err;
	}

	if (bch2_fs_inconsistent_on((u64) g->dirty_sectors + sectors > ca->mi.bucket_size, c,
			"bucket %u:%zu gen %u data type %s sector count overflow: %u + %u > bucket size",
			ca->dev_idx, b, g->gen,
			bch2_data_types[g->data_type ?: data_type],
			g->dirty_sectors, sectors)) {
		ret = -EIO;
		goto err;
	}

	g->data_type = data_type;
	g->dirty_sectors += sectors;
	new = *g;
err:
	bucket_unlock(g);
	if (!ret)
		bch2_dev_usage_update_m(c, ca, &old, &new);
	percpu_up_read(&c->mark_lock);
	return ret;
}

int bch2_check_bucket_ref(struct btree_trans *trans,
			  struct bkey_s_c k,
			  const struct bch_extent_ptr *ptr,
			  s64 sectors, enum bch_data_type ptr_data_type,
			  u8 b_gen, u8 bucket_data_type,
			  u32 bucket_sectors)
{
	struct bch_fs *c = trans->c;
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
	size_t bucket_nr = PTR_BUCKET_NR(ca, ptr);
	struct printbuf buf = PRINTBUF;
	int ret = 0;

	if (bucket_data_type == BCH_DATA_cached)
		bucket_data_type = BCH_DATA_user;

	if ((bucket_data_type == BCH_DATA_stripe && ptr_data_type == BCH_DATA_user) ||
	    (bucket_data_type == BCH_DATA_user   && ptr_data_type == BCH_DATA_stripe))
		bucket_data_type = ptr_data_type = BCH_DATA_stripe;

	if (gen_after(ptr->gen, b_gen)) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			      BCH_FSCK_ERR_ptr_gen_newer_than_bucket_gen,
			"bucket %u:%zu gen %u data type %s: ptr gen %u newer than bucket gen\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(bch2_bkey_val_to_text(&buf, c, k), buf.buf));
		ret = -EIO;
		goto err;
	}

	if (gen_cmp(b_gen, ptr->gen) > BUCKET_GC_GEN_MAX) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			      BCH_FSCK_ERR_ptr_too_stale,
			"bucket %u:%zu gen %u data type %s: ptr gen %u too stale\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf));
		ret = -EIO;
		goto err;
	}

	if (b_gen != ptr->gen && !ptr->cached) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			      BCH_FSCK_ERR_stale_dirty_ptr,
			"bucket %u:%zu gen %u (mem gen %u) data type %s: stale dirty ptr (gen %u)\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			*bucket_gen(ca, bucket_nr),
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf));
		ret = -EIO;
		goto err;
	}

	if (b_gen != ptr->gen) {
		ret = 1;
		goto out;
	}

	if (!data_type_is_empty(bucket_data_type) &&
	    ptr_data_type &&
	    bucket_data_type != ptr_data_type) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			      BCH_FSCK_ERR_ptr_bucket_data_type_mismatch,
			"bucket %u:%zu gen %u different types of data in same bucket: %s, %s\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_types[bucket_data_type],
			bch2_data_types[ptr_data_type],
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf));
		ret = -EIO;
		goto err;
	}

	if ((u64) bucket_sectors + sectors > U32_MAX) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			      BCH_FSCK_ERR_bucket_sector_count_overflow,
			"bucket %u:%zu gen %u data type %s sector count overflow: %u + %lli > U32_MAX\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			bucket_sectors, sectors,
			(printbuf_reset(&buf),
			 bch2_bkey_val_to_text(&buf, c, k), buf.buf));
		ret = -EIO;
		goto err;
	}
out:
	printbuf_exit(&buf);
	return ret;
err:
	bch2_dump_trans_updates(trans);
	goto out;
}

void bch2_trans_fs_usage_revert(struct btree_trans *trans,
				struct replicas_delta_list *deltas)
{
	struct bch_fs *c = trans->c;
	struct bch_fs_usage *dst;
	struct replicas_delta *d, *top = (void *) deltas->d + deltas->used;
	s64 added = 0;
	unsigned i;

	percpu_down_read(&c->mark_lock);
	preempt_disable();
	dst = fs_usage_ptr(c, trans->journal_res.seq, false);

	/* revert changes: */
	for (d = deltas->d; d != top; d = replicas_delta_next(d)) {
		switch (d->r.data_type) {
		case BCH_DATA_btree:
		case BCH_DATA_user:
		case BCH_DATA_parity:
			added += d->delta;
		}
		BUG_ON(__update_replicas(c, dst, &d->r, -d->delta));
	}

	dst->nr_inodes -= deltas->nr_inodes;

	for (i = 0; i < BCH_REPLICAS_MAX; i++) {
		added				-= deltas->persistent_reserved[i];
		dst->reserved			-= deltas->persistent_reserved[i];
		dst->persistent_reserved[i]	-= deltas->persistent_reserved[i];
	}

	if (added > 0) {
		trans->disk_res->sectors += added;
		this_cpu_add(*c->online_reserved, added);
	}

	preempt_enable();
	percpu_up_read(&c->mark_lock);
}

int bch2_trans_fs_usage_apply(struct btree_trans *trans,
			      struct replicas_delta_list *deltas)
{
	struct bch_fs *c = trans->c;
	static int warned_disk_usage = 0;
	bool warn = false;
	u64 disk_res_sectors = trans->disk_res ? trans->disk_res->sectors : 0;
	struct replicas_delta *d, *d2;
	struct replicas_delta *top = (void *) deltas->d + deltas->used;
	struct bch_fs_usage *dst;
	s64 added = 0, should_not_have_added;
	unsigned i;

	percpu_down_read(&c->mark_lock);
	preempt_disable();
	dst = fs_usage_ptr(c, trans->journal_res.seq, false);

	for (d = deltas->d; d != top; d = replicas_delta_next(d)) {
		switch (d->r.data_type) {
		case BCH_DATA_btree:
		case BCH_DATA_user:
		case BCH_DATA_parity:
			added += d->delta;
		}

		if (__update_replicas(c, dst, &d->r, d->delta))
			goto need_mark;
	}

	dst->nr_inodes += deltas->nr_inodes;

	for (i = 0; i < BCH_REPLICAS_MAX; i++) {
		added				+= deltas->persistent_reserved[i];
		dst->reserved			+= deltas->persistent_reserved[i];
		dst->persistent_reserved[i]	+= deltas->persistent_reserved[i];
	}

	/*
	 * Not allowed to reduce sectors_available except by getting a
	 * reservation:
	 */
	should_not_have_added = added - (s64) disk_res_sectors;
	if (unlikely(should_not_have_added > 0)) {
		u64 old, new, v = atomic64_read(&c->sectors_available);

		do {
			old = v;
			new = max_t(s64, 0, old - should_not_have_added);
		} while ((v = atomic64_cmpxchg(&c->sectors_available,
					       old, new)) != old);

		added -= should_not_have_added;
		warn = true;
	}

	if (added > 0) {
		trans->disk_res->sectors -= added;
		this_cpu_sub(*c->online_reserved, added);
	}

	preempt_enable();
	percpu_up_read(&c->mark_lock);

	if (unlikely(warn) && !xchg(&warned_disk_usage, 1))
		bch2_trans_inconsistent(trans,
					"disk usage increased %lli more than %llu sectors reserved)",
					should_not_have_added, disk_res_sectors);
	return 0;
need_mark:
	/* revert changes: */
	for (d2 = deltas->d; d2 != d; d2 = replicas_delta_next(d2))
		BUG_ON(__update_replicas(c, dst, &d2->r, -d2->delta));

	preempt_enable();
	percpu_up_read(&c->mark_lock);
	return -1;
}

/* KEY_TYPE_extent: */

static int __mark_pointer(struct btree_trans *trans,
			  struct bkey_s_c k,
			  const struct bch_extent_ptr *ptr,
			  s64 sectors, enum bch_data_type ptr_data_type,
			  u8 bucket_gen, u8 *bucket_data_type,
			  u32 *dirty_sectors, u32 *cached_sectors)
{
	u32 *dst_sectors = !ptr->cached
		? dirty_sectors
		: cached_sectors;
	int ret = bch2_check_bucket_ref(trans, k, ptr, sectors, ptr_data_type,
				   bucket_gen, *bucket_data_type, *dst_sectors);

	if (ret)
		return ret;

	*dst_sectors += sectors;

	if (!*dirty_sectors && !*cached_sectors)
		*bucket_data_type = 0;
	else if (*bucket_data_type != BCH_DATA_stripe)
		*bucket_data_type = ptr_data_type;

	return 0;
}

static int bch2_trigger_pointer(struct btree_trans *trans,
			enum btree_id btree_id, unsigned level,
			struct bkey_s_c k, struct extent_ptr_decoded p,
			s64 *sectors,
			unsigned flags)
{
	bool insert = !(flags & BTREE_TRIGGER_OVERWRITE);
	struct bpos bucket;
	struct bch_backpointer bp;

	bch2_extent_ptr_to_bp(trans->c, btree_id, level, k, p, &bucket, &bp);
	*sectors = insert ? bp.bucket_len : -((s64) bp.bucket_len);

	if (flags & BTREE_TRIGGER_TRANSACTIONAL) {
		struct btree_iter iter;
		struct bkey_i_alloc_v4 *a = bch2_trans_start_alloc_update(trans, &iter, bucket);
		int ret = PTR_ERR_OR_ZERO(a);
		if (ret)
			return ret;

		ret = __mark_pointer(trans, k, &p.ptr, *sectors, bp.data_type,
				     a->v.gen, &a->v.data_type,
				     &a->v.dirty_sectors, &a->v.cached_sectors) ?:
			bch2_trans_update(trans, &iter, &a->k_i, 0);
		bch2_trans_iter_exit(trans, &iter);

		if (ret)
			return ret;

		if (!p.ptr.cached) {
			ret = bch2_bucket_backpointer_mod(trans, bucket, bp, k, insert);
			if (ret)
				return ret;
		}
	}

	if (flags & BTREE_TRIGGER_GC) {
		struct bch_fs *c = trans->c;
		struct bch_dev *ca = bch_dev_bkey_exists(c, p.ptr.dev);
		enum bch_data_type data_type = bkey_ptr_data_type(btree_id, level, k, p);

		percpu_down_read(&c->mark_lock);
		struct bucket *g = PTR_GC_BUCKET(ca, &p.ptr);
		bucket_lock(g);
		struct bucket old = *g;

		u8 bucket_data_type = g->data_type;
		int ret = __mark_pointer(trans, k, &p.ptr, *sectors,
				     data_type, g->gen,
				     &bucket_data_type,
				     &g->dirty_sectors,
				     &g->cached_sectors);
		if (ret) {
			bucket_unlock(g);
			percpu_up_read(&c->mark_lock);
			return ret;
		}

		g->data_type = bucket_data_type;
		struct bucket new = *g;
		bucket_unlock(g);
		bch2_dev_usage_update_m(c, ca, &old, &new);
		percpu_up_read(&c->mark_lock);
	}

	return 0;
}

static int bch2_trigger_stripe_ptr(struct btree_trans *trans,
				struct bkey_s_c k,
				struct extent_ptr_decoded p,
				enum bch_data_type data_type,
				s64 sectors, unsigned flags)
{
	if (flags & BTREE_TRIGGER_TRANSACTIONAL) {
		struct btree_iter iter;
		struct bkey_i_stripe *s = bch2_bkey_get_mut_typed(trans, &iter,
				BTREE_ID_stripes, POS(0, p.ec.idx),
				BTREE_ITER_WITH_UPDATES, stripe);
		int ret = PTR_ERR_OR_ZERO(s);
		if (unlikely(ret)) {
			bch2_trans_inconsistent_on(bch2_err_matches(ret, ENOENT), trans,
				"pointer to nonexistent stripe %llu",
				(u64) p.ec.idx);
			goto err;
		}

		if (!bch2_ptr_matches_stripe(&s->v, p)) {
			bch2_trans_inconsistent(trans,
				"stripe pointer doesn't match stripe %llu",
				(u64) p.ec.idx);
			ret = -EIO;
			goto err;
		}

		stripe_blockcount_set(&s->v, p.ec.block,
			stripe_blockcount_get(&s->v, p.ec.block) +
			sectors);

		struct bch_replicas_padded r;
		bch2_bkey_to_replicas(&r.e, bkey_i_to_s_c(&s->k_i));
		r.e.data_type = data_type;
		ret = bch2_update_replicas_list(trans, &r.e, sectors);
err:
		bch2_trans_iter_exit(trans, &iter);
		return ret;
	}

	if (flags & BTREE_TRIGGER_GC) {
		struct bch_fs *c = trans->c;

		BUG_ON(!(flags & BTREE_TRIGGER_GC));

		struct gc_stripe *m = genradix_ptr_alloc(&c->gc_stripes, p.ec.idx, GFP_KERNEL);
		if (!m) {
			bch_err(c, "error allocating memory for gc_stripes, idx %llu",
				(u64) p.ec.idx);
			return -BCH_ERR_ENOMEM_mark_stripe_ptr;
		}

		mutex_lock(&c->ec_stripes_heap_lock);

		if (!m || !m->alive) {
			mutex_unlock(&c->ec_stripes_heap_lock);
			struct printbuf buf = PRINTBUF;
			bch2_bkey_val_to_text(&buf, c, k);
			bch_err_ratelimited(c, "pointer to nonexistent stripe %llu\n  while marking %s",
					    (u64) p.ec.idx, buf.buf);
			printbuf_exit(&buf);
			bch2_inconsistent_error(c);
			return -EIO;
		}

		m->block_sectors[p.ec.block] += sectors;

		struct bch_replicas_padded r = m->r;
		mutex_unlock(&c->ec_stripes_heap_lock);

		r.e.data_type = data_type;
		bch2_update_replicas(c, k, &r.e, sectors, trans->journal_res.seq, true);
	}

	return 0;
}

static int __trigger_extent(struct btree_trans *trans,
			    enum btree_id btree_id, unsigned level,
			    struct bkey_s_c k, unsigned flags)
{
	bool gc = flags & BTREE_TRIGGER_GC;
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct bch_replicas_padded r;
	enum bch_data_type data_type = bkey_is_btree_ptr(k.k)
		? BCH_DATA_btree
		: BCH_DATA_user;
	s64 dirty_sectors = 0;
	int ret = 0;

	r.e.data_type	= data_type;
	r.e.nr_devs	= 0;
	r.e.nr_required	= 1;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		s64 disk_sectors;
		ret = bch2_trigger_pointer(trans, btree_id, level, k, p, &disk_sectors, flags);
		if (ret < 0)
			return ret;

		bool stale = ret > 0;

		if (p.ptr.cached) {
			if (!stale) {
				ret = !gc
					? bch2_update_cached_sectors_list(trans, p.ptr.dev, disk_sectors)
					: update_cached_sectors(c, k, p.ptr.dev, disk_sectors, 0, true);
				bch2_fs_fatal_err_on(ret && gc, c, "%s(): no replicas entry while updating cached sectors",
						     __func__);
				if (ret)
					return ret;
			}
		} else if (!p.has_ec) {
			dirty_sectors	       += disk_sectors;
			r.e.devs[r.e.nr_devs++]	= p.ptr.dev;
		} else {
			ret = bch2_trigger_stripe_ptr(trans, k, p, data_type, disk_sectors, flags);
			if (ret)
				return ret;

			/*
			 * There may be other dirty pointers in this extent, but
			 * if so they're not required for mounting if we have an
			 * erasure coded pointer in this extent:
			 */
			r.e.nr_required = 0;
		}
	}

	if (r.e.nr_devs) {
		ret = !gc
			? bch2_update_replicas_list(trans, &r.e, dirty_sectors)
			: bch2_update_replicas(c, k, &r.e, dirty_sectors, 0, true);
		if (unlikely(ret && gc)) {
			struct printbuf buf = PRINTBUF;

			bch2_bkey_val_to_text(&buf, c, k);
			bch2_fs_fatal_error(c, "%s(): no replicas entry for %s", __func__, buf.buf);
			printbuf_exit(&buf);
		}
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_trigger_extent(struct btree_trans *trans,
			enum btree_id btree_id, unsigned level,
			struct bkey_s_c old, struct bkey_s new,
			unsigned flags)
{
	if (flags & BTREE_TRIGGER_TRANSACTIONAL) {
		struct bch_fs *c = trans->c;
		int mod = (int) bch2_bkey_needs_rebalance(c, new.s_c) -
			  (int) bch2_bkey_needs_rebalance(c, old);

		if (mod) {
			int ret = bch2_btree_bit_mod(trans, BTREE_ID_rebalance_work, new.k->p, mod > 0);
			if (ret)
				return ret;
		}
	}

	if (flags & (BTREE_TRIGGER_TRANSACTIONAL|BTREE_TRIGGER_GC))
		return trigger_run_overwrite_then_insert(__trigger_extent, trans, btree_id, level, old, new, flags);

	return 0;
}

/* KEY_TYPE_reservation */

static int __trigger_reservation(struct btree_trans *trans,
				 enum btree_id btree_id, unsigned level,
				 struct bkey_s_c k, unsigned flags)
{
	struct bch_fs *c = trans->c;
	unsigned replicas = bkey_s_c_to_reservation(k).v->nr_replicas;
	s64 sectors = (s64) k.k->size * replicas;

	if (flags & BTREE_TRIGGER_OVERWRITE)
		sectors = -sectors;

	if (flags & BTREE_TRIGGER_TRANSACTIONAL) {
		int ret = bch2_replicas_deltas_realloc(trans, 0);
		if (ret)
			return ret;

		struct replicas_delta_list *d = trans->fs_usage_deltas;
		replicas = min(replicas, ARRAY_SIZE(d->persistent_reserved));

		d->persistent_reserved[replicas - 1] += sectors;
	}

	if (flags & BTREE_TRIGGER_GC) {
		percpu_down_read(&c->mark_lock);
		preempt_disable();

		struct bch_fs_usage *fs_usage = this_cpu_ptr(c->usage_gc);

		replicas = min(replicas, ARRAY_SIZE(fs_usage->persistent_reserved));
		fs_usage->reserved				+= sectors;
		fs_usage->persistent_reserved[replicas - 1]	+= sectors;

		preempt_enable();
		percpu_up_read(&c->mark_lock);
	}

	return 0;
}

int bch2_trigger_reservation(struct btree_trans *trans,
			  enum btree_id btree_id, unsigned level,
			  struct bkey_s_c old, struct bkey_s new,
			  unsigned flags)
{
	return trigger_run_overwrite_then_insert(__trigger_reservation, trans, btree_id, level, old, new, flags);
}

/* Mark superblocks: */

static int __bch2_trans_mark_metadata_bucket(struct btree_trans *trans,
				    struct bch_dev *ca, size_t b,
				    enum bch_data_type type,
				    unsigned sectors)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_i_alloc_v4 *a;
	int ret = 0;

	/*
	 * Backup superblock might be past the end of our normal usable space:
	 */
	if (b >= ca->mi.nbuckets)
		return 0;

	a = bch2_trans_start_alloc_update(trans, &iter, POS(ca->dev_idx, b));
	if (IS_ERR(a))
		return PTR_ERR(a);

	if (a->v.data_type && type && a->v.data_type != type) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			      BCH_FSCK_ERR_bucket_metadata_type_mismatch,
			"bucket %llu:%llu gen %u different types of data in same bucket: %s, %s\n"
			"while marking %s",
			iter.pos.inode, iter.pos.offset, a->v.gen,
			bch2_data_types[a->v.data_type],
			bch2_data_types[type],
			bch2_data_types[type]);
		ret = -EIO;
		goto err;
	}

	if (a->v.data_type	!= type ||
	    a->v.dirty_sectors	!= sectors) {
		a->v.data_type		= type;
		a->v.dirty_sectors	= sectors;
		ret = bch2_trans_update(trans, &iter, &a->k_i, 0);
	}
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

int bch2_trans_mark_metadata_bucket(struct btree_trans *trans,
				    struct bch_dev *ca, size_t b,
				    enum bch_data_type type,
				    unsigned sectors)
{
	return commit_do(trans, NULL, NULL, 0,
			__bch2_trans_mark_metadata_bucket(trans, ca, b, type, sectors));
}

static int bch2_trans_mark_metadata_sectors(struct btree_trans *trans,
					    struct bch_dev *ca,
					    u64 start, u64 end,
					    enum bch_data_type type,
					    u64 *bucket, unsigned *bucket_sectors)
{
	do {
		u64 b = sector_to_bucket(ca, start);
		unsigned sectors =
			min_t(u64, bucket_to_sector(ca, b + 1), end) - start;

		if (b != *bucket && *bucket_sectors) {
			int ret = bch2_trans_mark_metadata_bucket(trans, ca, *bucket,
								  type, *bucket_sectors);
			if (ret)
				return ret;

			*bucket_sectors = 0;
		}

		*bucket		= b;
		*bucket_sectors	+= sectors;
		start += sectors;
	} while (start < end);

	return 0;
}

static int __bch2_trans_mark_dev_sb(struct btree_trans *trans,
				    struct bch_dev *ca)
{
	struct bch_sb_layout *layout = &ca->disk_sb.sb->layout;
	u64 bucket = 0;
	unsigned i, bucket_sectors = 0;
	int ret;

	for (i = 0; i < layout->nr_superblocks; i++) {
		u64 offset = le64_to_cpu(layout->sb_offset[i]);

		if (offset == BCH_SB_SECTOR) {
			ret = bch2_trans_mark_metadata_sectors(trans, ca,
						0, BCH_SB_SECTOR,
						BCH_DATA_sb, &bucket, &bucket_sectors);
			if (ret)
				return ret;
		}

		ret = bch2_trans_mark_metadata_sectors(trans, ca, offset,
				      offset + (1 << layout->sb_max_size_bits),
				      BCH_DATA_sb, &bucket, &bucket_sectors);
		if (ret)
			return ret;
	}

	if (bucket_sectors) {
		ret = bch2_trans_mark_metadata_bucket(trans, ca,
				bucket, BCH_DATA_sb, bucket_sectors);
		if (ret)
			return ret;
	}

	for (i = 0; i < ca->journal.nr; i++) {
		ret = bch2_trans_mark_metadata_bucket(trans, ca,
				ca->journal.buckets[i],
				BCH_DATA_journal, ca->mi.bucket_size);
		if (ret)
			return ret;
	}

	return 0;
}

int bch2_trans_mark_dev_sb(struct bch_fs *c, struct bch_dev *ca)
{
	int ret = bch2_trans_run(c, __bch2_trans_mark_dev_sb(trans, ca));

	bch_err_fn(c, ret);
	return ret;
}

int bch2_trans_mark_dev_sbs(struct bch_fs *c)
{
	for_each_online_member(c, ca) {
		int ret = bch2_trans_mark_dev_sb(c, ca);
		if (ret) {
			percpu_ref_put(&ca->ref);
			return ret;
		}
	}

	return 0;
}

/* Disk reservations: */

#define SECTORS_CACHE	1024

int __bch2_disk_reservation_add(struct bch_fs *c, struct disk_reservation *res,
			      u64 sectors, int flags)
{
	struct bch_fs_pcpu *pcpu;
	u64 old, v, get;
	s64 sectors_available;
	int ret;

	percpu_down_read(&c->mark_lock);
	preempt_disable();
	pcpu = this_cpu_ptr(c->pcpu);

	if (sectors <= pcpu->sectors_available)
		goto out;

	v = atomic64_read(&c->sectors_available);
	do {
		old = v;
		get = min((u64) sectors + SECTORS_CACHE, old);

		if (get < sectors) {
			preempt_enable();
			goto recalculate;
		}
	} while ((v = atomic64_cmpxchg(&c->sectors_available,
				       old, old - get)) != old);

	pcpu->sectors_available		+= get;

out:
	pcpu->sectors_available		-= sectors;
	this_cpu_add(*c->online_reserved, sectors);
	res->sectors			+= sectors;

	preempt_enable();
	percpu_up_read(&c->mark_lock);
	return 0;

recalculate:
	mutex_lock(&c->sectors_available_lock);

	percpu_u64_set(&c->pcpu->sectors_available, 0);
	sectors_available = avail_factor(__bch2_fs_usage_read_short(c).free);

	if (sectors <= sectors_available ||
	    (flags & BCH_DISK_RESERVATION_NOFAIL)) {
		atomic64_set(&c->sectors_available,
			     max_t(s64, 0, sectors_available - sectors));
		this_cpu_add(*c->online_reserved, sectors);
		res->sectors			+= sectors;
		ret = 0;
	} else {
		atomic64_set(&c->sectors_available, sectors_available);
		ret = -BCH_ERR_ENOSPC_disk_reservation;
	}

	mutex_unlock(&c->sectors_available_lock);
	percpu_up_read(&c->mark_lock);

	return ret;
}

/* Startup/shutdown: */

static void bucket_gens_free_rcu(struct rcu_head *rcu)
{
	struct bucket_gens *buckets =
		container_of(rcu, struct bucket_gens, rcu);

	kvpfree(buckets, sizeof(*buckets) + buckets->nbuckets);
}

int bch2_dev_buckets_resize(struct bch_fs *c, struct bch_dev *ca, u64 nbuckets)
{
	struct bucket_gens *bucket_gens = NULL, *old_bucket_gens = NULL;
	unsigned long *buckets_nouse = NULL;
	bool resize = ca->bucket_gens != NULL;
	int ret;

	if (!(bucket_gens	= kvpmalloc(sizeof(struct bucket_gens) + nbuckets,
					    GFP_KERNEL|__GFP_ZERO))) {
		ret = -BCH_ERR_ENOMEM_bucket_gens;
		goto err;
	}

	if ((c->opts.buckets_nouse &&
	     !(buckets_nouse	= kvpmalloc(BITS_TO_LONGS(nbuckets) *
					    sizeof(unsigned long),
					    GFP_KERNEL|__GFP_ZERO)))) {
		ret = -BCH_ERR_ENOMEM_buckets_nouse;
		goto err;
	}

	bucket_gens->first_bucket = ca->mi.first_bucket;
	bucket_gens->nbuckets	= nbuckets;

	if (resize) {
		down_write(&c->gc_lock);
		down_write(&ca->bucket_lock);
		percpu_down_write(&c->mark_lock);
	}

	old_bucket_gens = rcu_dereference_protected(ca->bucket_gens, 1);

	if (resize) {
		size_t n = min(bucket_gens->nbuckets, old_bucket_gens->nbuckets);

		memcpy(bucket_gens->b,
		       old_bucket_gens->b,
		       n);
		if (buckets_nouse)
			memcpy(buckets_nouse,
			       ca->buckets_nouse,
			       BITS_TO_LONGS(n) * sizeof(unsigned long));
	}

	rcu_assign_pointer(ca->bucket_gens, bucket_gens);
	bucket_gens	= old_bucket_gens;

	swap(ca->buckets_nouse, buckets_nouse);

	nbuckets = ca->mi.nbuckets;

	if (resize) {
		percpu_up_write(&c->mark_lock);
		up_write(&ca->bucket_lock);
		up_write(&c->gc_lock);
	}

	ret = 0;
err:
	kvpfree(buckets_nouse,
		BITS_TO_LONGS(nbuckets) * sizeof(unsigned long));
	if (bucket_gens)
		call_rcu(&bucket_gens->rcu, bucket_gens_free_rcu);

	return ret;
}

void bch2_dev_buckets_free(struct bch_dev *ca)
{
	unsigned i;

	kvpfree(ca->buckets_nouse,
		BITS_TO_LONGS(ca->mi.nbuckets) * sizeof(unsigned long));
	kvpfree(rcu_dereference_protected(ca->bucket_gens, 1),
		sizeof(struct bucket_gens) + ca->mi.nbuckets);

	for (i = 0; i < ARRAY_SIZE(ca->usage); i++)
		free_percpu(ca->usage[i]);
	kfree(ca->usage_base);
}

int bch2_dev_buckets_alloc(struct bch_fs *c, struct bch_dev *ca)
{
	unsigned i;

	ca->usage_base = kzalloc(sizeof(struct bch_dev_usage), GFP_KERNEL);
	if (!ca->usage_base)
		return -BCH_ERR_ENOMEM_usage_init;

	for (i = 0; i < ARRAY_SIZE(ca->usage); i++) {
		ca->usage[i] = alloc_percpu(struct bch_dev_usage);
		if (!ca->usage[i])
			return -BCH_ERR_ENOMEM_usage_init;
	}

	return bch2_dev_buckets_resize(c, ca, ca->mi.nbuckets);
}
