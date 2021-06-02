// SPDX-License-Identifier: GPL-2.0
/*
 * Code for manipulating bucket marks for garbage collection.
 *
 * Copyright 2014 Datera, Inc.
 */

#include "bcachefs.h"
#include "alloc_background.h"
#include "bset.h"
#include "btree_gc.h"
#include "btree_update.h"
#include "buckets.h"
#include "ec.h"
#include "error.h"
#include "movinggc.h"
#include "reflink.h"
#include "replicas.h"
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

/*
 * Clear journal_seq_valid for buckets for which it's not needed, to prevent
 * wraparound:
 */
void bch2_bucket_seq_cleanup(struct bch_fs *c)
{
	u64 journal_seq = atomic64_read(&c->journal.seq);
	u16 last_seq_ondisk = c->journal.last_seq_ondisk;
	struct bch_dev *ca;
	struct bucket_array *buckets;
	struct bucket *g;
	struct bucket_mark m;
	unsigned i;

	if (journal_seq - c->last_bucket_seq_cleanup <
	    (1U << (BUCKET_JOURNAL_SEQ_BITS - 2)))
		return;

	c->last_bucket_seq_cleanup = journal_seq;

	for_each_member_device(ca, c, i) {
		down_read(&ca->bucket_lock);
		buckets = bucket_array(ca);

		for_each_bucket(g, buckets) {
			bucket_cmpxchg(g, m, ({
				if (!m.journal_seq_valid ||
				    bucket_needs_journal_commit(m, last_seq_ondisk))
					break;

				m.journal_seq_valid = 0;
			}));
		}
		up_read(&ca->bucket_lock);
	}
}

void bch2_fs_usage_initialize(struct bch_fs *c)
{
	struct bch_fs_usage *usage;
	struct bch_dev *ca;
	unsigned i;

	percpu_down_write(&c->mark_lock);
	usage = c->usage_base;

	for (i = 0; i < ARRAY_SIZE(c->usage); i++)
		bch2_fs_usage_acc_to_base(c, i);

	for (i = 0; i < BCH_REPLICAS_MAX; i++)
		usage->reserved += usage->persistent_reserved[i];

	for (i = 0; i < c->replicas.nr; i++) {
		struct bch_replicas_entry *e =
			cpu_replicas_entry(&c->replicas, i);

		fs_usage_data_type_to_base(usage, e->data_type, usage->replicas[i]);
	}

	for_each_member_device(ca, c, i) {
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
	return this_cpu_ptr(gc
			    ? ca->usage_gc
			    : ca->usage[journal_seq & JOURNAL_BUF_MASK]);
}

struct bch_dev_usage bch2_dev_usage_read(struct bch_dev *ca)
{
	struct bch_fs *c = ca->fs;
	struct bch_dev_usage ret;
	unsigned seq, i, u64s = dev_usage_u64s();

	do {
		seq = read_seqcount_begin(&c->usage_lock);
		memcpy(&ret, ca->usage_base, u64s * sizeof(u64));
		for (i = 0; i < ARRAY_SIZE(ca->usage); i++)
			acc_u64s_percpu((u64 *) &ret, (u64 __percpu *) ca->usage[i], u64s);
	} while (read_seqcount_retry(&c->usage_lock, seq));

	return ret;
}

static inline struct bch_fs_usage *fs_usage_ptr(struct bch_fs *c,
						unsigned journal_seq,
						bool gc)
{
	return this_cpu_ptr(gc
			    ? c->usage_gc
			    : c->usage[journal_seq & JOURNAL_BUF_MASK]);
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
	unsigned seq, i, v, u64s = fs_usage_u64s(c) + 1;
retry:
	ret = kmalloc(u64s * sizeof(u64), GFP_NOFS);
	if (unlikely(!ret))
		return NULL;

	percpu_down_read(&c->mark_lock);

	v = fs_usage_u64s(c) + 1;
	if (unlikely(u64s != v)) {
		u64s = v;
		percpu_up_read(&c->mark_lock);
		kfree(ret);
		goto retry;
	}

	ret->online_reserved = percpu_u64_get(c->online_reserved);

	do {
		seq = read_seqcount_begin(&c->usage_lock);
		memcpy(&ret->u, c->usage_base, u64s * sizeof(u64));
		for (i = 0; i < ARRAY_SIZE(c->usage); i++)
			acc_u64s_percpu((u64 *) &ret->u, (u64 __percpu *) c->usage[i], u64s);
	} while (read_seqcount_retry(&c->usage_lock, seq));

	return ret;
}

void bch2_fs_usage_acc_to_base(struct bch_fs *c, unsigned idx)
{
	struct bch_dev *ca;
	unsigned i, u64s = fs_usage_u64s(c);

	BUG_ON(idx >= ARRAY_SIZE(c->usage));

	preempt_disable();
	write_seqcount_begin(&c->usage_lock);

	acc_u64s_percpu((u64 *) c->usage_base,
			(u64 __percpu *) c->usage[idx], u64s);
	percpu_memset(c->usage[idx], 0, u64s * sizeof(u64));

	rcu_read_lock();
	for_each_member_device_rcu(ca, c, i, NULL) {
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

	pr_buf(out, "capacity:\t\t\t%llu\n", c->capacity);

	pr_buf(out, "hidden:\t\t\t\t%llu\n",
	       fs_usage->u.hidden);
	pr_buf(out, "data:\t\t\t\t%llu\n",
	       fs_usage->u.data);
	pr_buf(out, "cached:\t\t\t\t%llu\n",
	       fs_usage->u.cached);
	pr_buf(out, "reserved:\t\t\t%llu\n",
	       fs_usage->u.reserved);
	pr_buf(out, "nr_inodes:\t\t\t%llu\n",
	       fs_usage->u.nr_inodes);
	pr_buf(out, "online reserved:\t\t%llu\n",
	       fs_usage->online_reserved);

	for (i = 0;
	     i < ARRAY_SIZE(fs_usage->u.persistent_reserved);
	     i++) {
		pr_buf(out, "%u replicas:\n", i + 1);
		pr_buf(out, "\treserved:\t\t%llu\n",
		       fs_usage->u.persistent_reserved[i]);
	}

	for (i = 0; i < c->replicas.nr; i++) {
		struct bch_replicas_entry *e =
			cpu_replicas_entry(&c->replicas, i);

		pr_buf(out, "\t");
		bch2_replicas_entry_to_text(out, e);
		pr_buf(out, ":\t%llu\n", fs_usage->u.replicas[i]);
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

static inline int is_unavailable_bucket(struct bucket_mark m)
{
	return !is_available_bucket(m);
}

static inline int bucket_sectors_fragmented(struct bch_dev *ca,
					    struct bucket_mark m)
{
	return bucket_sectors_used(m)
		? max(0, (int) ca->mi.bucket_size - (int) bucket_sectors_used(m))
		: 0;
}

static inline int is_stripe_data_bucket(struct bucket_mark m)
{
	return m.stripe && m.data_type != BCH_DATA_parity;
}

static inline enum bch_data_type bucket_type(struct bucket_mark m)
{
	return m.cached_sectors && !m.dirty_sectors
		? BCH_DATA_cached
		: m.data_type;
}

static bool bucket_became_unavailable(struct bucket_mark old,
				      struct bucket_mark new)
{
	return is_available_bucket(old) &&
	       !is_available_bucket(new);
}

static inline void account_bucket(struct bch_fs_usage *fs_usage,
				  struct bch_dev_usage *dev_usage,
				  enum bch_data_type type,
				  int nr, s64 size)
{
	if (type == BCH_DATA_sb || type == BCH_DATA_journal)
		fs_usage->hidden	+= size;

	dev_usage->d[type].buckets	+= nr;
}

static void bch2_dev_usage_update(struct bch_fs *c, struct bch_dev *ca,
				  struct bch_fs_usage *fs_usage,
				  struct bucket_mark old, struct bucket_mark new,
				  u64 journal_seq, bool gc)
{
	struct bch_dev_usage *u;

	percpu_rwsem_assert_held(&c->mark_lock);

	preempt_disable();
	if (!fs_usage)
		fs_usage = fs_usage_ptr(c, journal_seq, gc);
	u = dev_usage_ptr(ca, journal_seq, gc);

	if (bucket_type(old))
		account_bucket(fs_usage, u, bucket_type(old),
			       -1, -ca->mi.bucket_size);

	if (bucket_type(new))
		account_bucket(fs_usage, u, bucket_type(new),
			       1, ca->mi.bucket_size);

	u->buckets_unavailable +=
		is_unavailable_bucket(new) - is_unavailable_bucket(old);

	u->d[old.data_type].sectors -= old.dirty_sectors;
	u->d[new.data_type].sectors += new.dirty_sectors;
	u->d[BCH_DATA_cached].sectors +=
		(int) new.cached_sectors - (int) old.cached_sectors;

	u->d[old.data_type].fragmented -= bucket_sectors_fragmented(ca, old);
	u->d[new.data_type].fragmented += bucket_sectors_fragmented(ca, new);

	preempt_enable();

	if (!is_available_bucket(old) && is_available_bucket(new))
		bch2_wake_allocator(ca);
}

static inline int update_replicas(struct bch_fs *c,
				   struct bch_fs_usage *fs_usage,
				   struct bch_replicas_entry *r,
				   s64 sectors)
{
	int idx = bch2_replicas_entry_idx(c, r);

	if (idx < 0)
		return -1;

	fs_usage_data_type_to_base(fs_usage, r->data_type, sectors);
	fs_usage->replicas[idx]		+= sectors;
	return 0;
}

static inline int update_cached_sectors(struct bch_fs *c,
					 struct bch_fs_usage *fs_usage,
					 unsigned dev, s64 sectors)
{
	struct bch_replicas_padded r;

	bch2_replicas_entry_cached(&r.e, dev);

	return update_replicas(c, fs_usage, &r.e, sectors);
}

static struct replicas_delta_list *
replicas_deltas_realloc(struct btree_trans *trans, unsigned more)
{
	struct replicas_delta_list *d = trans->fs_usage_deltas;
	unsigned new_size = d ? (d->size + more) * 2 : 128;
	unsigned alloc_size = sizeof(*d) + new_size;

	WARN_ON_ONCE(alloc_size > REPLICAS_DELTA_LIST_MAX);

	if (!d || d->used + more > d->size) {
		d = krealloc(d, alloc_size, GFP_NOIO|__GFP_ZERO);

		BUG_ON(!d && alloc_size > REPLICAS_DELTA_LIST_MAX);

		if (!d) {
			d = mempool_alloc(&trans->c->replicas_delta_pool, GFP_NOIO);
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
	return d;
}

static inline void update_replicas_list(struct btree_trans *trans,
					struct bch_replicas_entry *r,
					s64 sectors)
{
	struct replicas_delta_list *d;
	struct replicas_delta *n;
	unsigned b;

	if (!sectors)
		return;

	b = replicas_entry_bytes(r) + 8;
	d = replicas_deltas_realloc(trans, b);

	n = (void *) d->d + d->used;
	n->delta = sectors;
	memcpy((void *) n + offsetof(struct replicas_delta, r),
	       r, replicas_entry_bytes(r));
	bch2_replicas_entry_sort(&n->r);
	d->used += b;
}

static inline void update_cached_sectors_list(struct btree_trans *trans,
					      unsigned dev, s64 sectors)
{
	struct bch_replicas_padded r;

	bch2_replicas_entry_cached(&r.e, dev);

	update_replicas_list(trans, &r.e, sectors);
}

#define do_mark_fn(fn, c, pos, flags, ...)				\
({									\
	int gc, ret = 0;						\
									\
	percpu_rwsem_assert_held(&c->mark_lock);			\
									\
	for (gc = 0; gc < 2 && !ret; gc++)				\
		if (!gc == !(flags & BTREE_TRIGGER_GC) ||		\
		    (gc && gc_visited(c, pos)))				\
			ret = fn(c, __VA_ARGS__, gc);			\
	ret;								\
})

void bch2_mark_alloc_bucket(struct bch_fs *c, struct bch_dev *ca,
			    size_t b, bool owned_by_allocator)
{
	struct bucket *g = bucket(ca, b);
	struct bucket_mark old, new;

	old = bucket_cmpxchg(g, new, ({
		new.owned_by_allocator	= owned_by_allocator;
	}));

	BUG_ON(owned_by_allocator == old.owned_by_allocator);
}

static int bch2_mark_alloc(struct bch_fs *c,
			   struct bkey_s_c old, struct bkey_s_c new,
			   struct bch_fs_usage *fs_usage,
			   u64 journal_seq, unsigned flags)
{
	bool gc = flags & BTREE_TRIGGER_GC;
	struct bkey_alloc_unpacked u;
	struct bch_dev *ca;
	struct bucket *g;
	struct bucket_mark old_m, m;

	/* We don't do anything for deletions - do we?: */
	if (new.k->type != KEY_TYPE_alloc &&
	    new.k->type != KEY_TYPE_alloc_v2)
		return 0;

	/*
	 * alloc btree is read in by bch2_alloc_read, not gc:
	 */
	if ((flags & BTREE_TRIGGER_GC) &&
	    !(flags & BTREE_TRIGGER_BUCKET_INVALIDATE))
		return 0;

	ca = bch_dev_bkey_exists(c, new.k->p.inode);

	if (new.k->p.offset >= ca->mi.nbuckets)
		return 0;

	g = __bucket(ca, new.k->p.offset, gc);
	u = bch2_alloc_unpack(new);

	old_m = bucket_cmpxchg(g, m, ({
		m.gen			= u.gen;
		m.data_type		= u.data_type;
		m.dirty_sectors		= u.dirty_sectors;
		m.cached_sectors	= u.cached_sectors;
		m.stripe		= u.stripe != 0;

		if (journal_seq) {
			m.journal_seq_valid	= 1;
			m.journal_seq		= journal_seq;
		}
	}));

	bch2_dev_usage_update(c, ca, fs_usage, old_m, m, journal_seq, gc);

	g->io_time[READ]	= u.read_time;
	g->io_time[WRITE]	= u.write_time;
	g->oldest_gen		= u.oldest_gen;
	g->gen_valid		= 1;
	g->stripe		= u.stripe;
	g->stripe_redundancy	= u.stripe_redundancy;

	/*
	 * need to know if we're getting called from the invalidate path or
	 * not:
	 */

	if ((flags & BTREE_TRIGGER_BUCKET_INVALIDATE) &&
	    old_m.cached_sectors) {
		if (update_cached_sectors(c, fs_usage, ca->dev_idx,
				      -old_m.cached_sectors)) {
			bch2_fs_fatal_error(c, "bch2_mark_alloc(): no replicas entry while updating cached sectors");
			return -1;
		}

		trace_invalidate(ca, bucket_to_sector(ca, new.k->p.offset),
				 old_m.cached_sectors);
	}

	return 0;
}

#define checked_add(a, b)					\
({								\
	unsigned _res = (unsigned) (a) + (b);			\
	bool overflow = _res > U16_MAX;				\
	if (overflow)						\
		_res = U16_MAX;					\
	(a) = _res;						\
	overflow;						\
})

static int __bch2_mark_metadata_bucket(struct bch_fs *c, struct bch_dev *ca,
				       size_t b, enum bch_data_type data_type,
				       unsigned sectors, bool gc)
{
	struct bucket *g = __bucket(ca, b, gc);
	struct bucket_mark old, new;
	bool overflow;

	BUG_ON(data_type != BCH_DATA_sb &&
	       data_type != BCH_DATA_journal);

	old = bucket_cmpxchg(g, new, ({
		new.data_type	= data_type;
		overflow = checked_add(new.dirty_sectors, sectors);
	}));

	bch2_fs_inconsistent_on(old.data_type &&
				old.data_type != data_type, c,
		"different types of data in same bucket: %s, %s",
		bch2_data_types[old.data_type],
		bch2_data_types[data_type]);

	bch2_fs_inconsistent_on(overflow, c,
		"bucket %u:%zu gen %u data type %s sector count overflow: %u + %u > U16_MAX",
		ca->dev_idx, b, new.gen,
		bch2_data_types[old.data_type ?: data_type],
		old.dirty_sectors, sectors);

	if (c)
		bch2_dev_usage_update(c, ca, fs_usage_ptr(c, 0, gc),
				      old, new, 0, gc);

	return 0;
}

void bch2_mark_metadata_bucket(struct bch_fs *c, struct bch_dev *ca,
			       size_t b, enum bch_data_type type,
			       unsigned sectors, struct gc_pos pos,
			       unsigned flags)
{
	BUG_ON(type != BCH_DATA_sb &&
	       type != BCH_DATA_journal);

	/*
	 * Backup superblock might be past the end of our normal usable space:
	 */
	if (b >= ca->mi.nbuckets)
		return;

	preempt_disable();

	if (likely(c)) {
		do_mark_fn(__bch2_mark_metadata_bucket, c, pos, flags,
			   ca, b, type, sectors);
	} else {
		__bch2_mark_metadata_bucket(c, ca, b, type, sectors, 0);
	}

	preempt_enable();
}

static s64 disk_sectors_scaled(unsigned n, unsigned d, unsigned sectors)
{
	return DIV_ROUND_UP(sectors * n, d);
}

static s64 __ptr_disk_sectors_delta(unsigned old_size,
				    unsigned offset, s64 delta,
				    unsigned flags,
				    unsigned n, unsigned d)
{
	BUG_ON(!n || !d);

	if (flags & BTREE_TRIGGER_OVERWRITE_SPLIT) {
		BUG_ON(offset + -delta > old_size);

		return -disk_sectors_scaled(n, d, old_size) +
			disk_sectors_scaled(n, d, offset) +
			disk_sectors_scaled(n, d, old_size - offset + delta);
	} else if (flags & BTREE_TRIGGER_OVERWRITE) {
		BUG_ON(offset + -delta > old_size);

		return -disk_sectors_scaled(n, d, old_size) +
			disk_sectors_scaled(n, d, old_size + delta);
	} else {
		return  disk_sectors_scaled(n, d, delta);
	}
}

static s64 ptr_disk_sectors_delta(struct extent_ptr_decoded p,
				  unsigned offset, s64 delta,
				  unsigned flags)
{
	return __ptr_disk_sectors_delta(p.crc.live_size,
					offset, delta, flags,
					p.crc.compressed_size,
					p.crc.uncompressed_size);
}

static int check_bucket_ref(struct bch_fs *c, struct bkey_s_c k,
			    const struct bch_extent_ptr *ptr,
			    s64 sectors, enum bch_data_type ptr_data_type,
			    u8 bucket_gen, u8 bucket_data_type,
			    u16 dirty_sectors, u16 cached_sectors)
{
	size_t bucket_nr = PTR_BUCKET_NR(bch_dev_bkey_exists(c, ptr->dev), ptr);
	u16 bucket_sectors = !ptr->cached
		? dirty_sectors
		: cached_sectors;
	char buf[200];

	if (gen_after(ptr->gen, bucket_gen)) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u data type %s: ptr gen %u newer than bucket gen\n"
			"while marking %s",
			ptr->dev, bucket_nr, bucket_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	if (gen_cmp(bucket_gen, ptr->gen) > BUCKET_GC_GEN_MAX) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u data type %s: ptr gen %u too stale\n"
			"while marking %s",
			ptr->dev, bucket_nr, bucket_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	if (bucket_gen != ptr->gen && !ptr->cached) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u data type %s: stale dirty ptr (gen %u)\n"
			"while marking %s",
			ptr->dev, bucket_nr, bucket_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	if (bucket_gen != ptr->gen)
		return 1;

	if (bucket_data_type && ptr_data_type &&
	    bucket_data_type != ptr_data_type) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u different types of data in same bucket: %s, %s\n"
			"while marking %s",
			ptr->dev, bucket_nr, bucket_gen,
			bch2_data_types[bucket_data_type],
			bch2_data_types[ptr_data_type],
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	if ((unsigned) (bucket_sectors + sectors) > U16_MAX) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u data type %s sector count overflow: %u + %lli > U16_MAX\n"
			"while marking %s",
			ptr->dev, bucket_nr, bucket_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			bucket_sectors, sectors,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	return 0;
}

static int mark_stripe_bucket(struct bch_fs *c, struct bkey_s_c k,
			     unsigned ptr_idx,
			     struct bch_fs_usage *fs_usage,
			     u64 journal_seq, unsigned flags)
{
	const struct bch_stripe *s = bkey_s_c_to_stripe(k).v;
	unsigned nr_data = s->nr_blocks - s->nr_redundant;
	bool parity = ptr_idx >= nr_data;
	const struct bch_extent_ptr *ptr = s->ptrs + ptr_idx;
	bool gc = flags & BTREE_TRIGGER_GC;
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
	struct bucket *g = PTR_BUCKET(ca, ptr, gc);
	struct bucket_mark new, old;
	char buf[200];
	int ret;

	if (g->stripe && g->stripe != k.k->p.offset) {
		bch2_fs_inconsistent(c,
			      "bucket %u:%zu gen %u: multiple stripes using same bucket\n%s",
			      ptr->dev, PTR_BUCKET_NR(ca, ptr), g->mark.gen,
			      (bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EINVAL;
	}

	old = bucket_cmpxchg(g, new, ({
		ret = check_bucket_ref(c, k, ptr, 0, 0, new.gen, new.data_type,
				       new.dirty_sectors, new.cached_sectors);
		if (ret)
			return ret;

		if (parity) {
			new.data_type		= BCH_DATA_parity;
			new.dirty_sectors	= le16_to_cpu(s->sectors);
		}

		if (journal_seq) {
			new.journal_seq_valid	= 1;
			new.journal_seq		= journal_seq;
		}
	}));

	g->stripe		= k.k->p.offset;
	g->stripe_redundancy	= s->nr_redundant;

	bch2_dev_usage_update(c, ca, fs_usage, old, new, journal_seq, gc);
	return 0;
}

static int __mark_pointer(struct bch_fs *c, struct bkey_s_c k,
			  const struct bch_extent_ptr *ptr,
			  s64 sectors, enum bch_data_type ptr_data_type,
			  u8 bucket_gen, u8 *bucket_data_type,
			  u16 *dirty_sectors, u16 *cached_sectors)
{
	u16 *dst_sectors = !ptr->cached
		? dirty_sectors
		: cached_sectors;
	int ret = check_bucket_ref(c, k, ptr, sectors, ptr_data_type,
				   bucket_gen, *bucket_data_type,
				   *dirty_sectors, *cached_sectors);

	if (ret)
		return ret;

	*dst_sectors += sectors;
	*bucket_data_type = *dirty_sectors || *cached_sectors
		? ptr_data_type : 0;
	return 0;
}

static int bch2_mark_pointer(struct bch_fs *c, struct bkey_s_c k,
			     struct extent_ptr_decoded p,
			     s64 sectors, enum bch_data_type data_type,
			     struct bch_fs_usage *fs_usage,
			     u64 journal_seq, unsigned flags)
{
	bool gc = flags & BTREE_TRIGGER_GC;
	struct bucket_mark old, new;
	struct bch_dev *ca = bch_dev_bkey_exists(c, p.ptr.dev);
	struct bucket *g = PTR_BUCKET(ca, &p.ptr, gc);
	u8 bucket_data_type;
	u64 v;
	int ret;

	v = atomic64_read(&g->_mark.v);
	do {
		new.v.counter = old.v.counter = v;
		bucket_data_type = new.data_type;

		ret = __mark_pointer(c, k, &p.ptr, sectors, data_type, new.gen,
				     &bucket_data_type,
				     &new.dirty_sectors,
				     &new.cached_sectors);
		if (ret)
			return ret;

		new.data_type = bucket_data_type;

		if (journal_seq) {
			new.journal_seq_valid = 1;
			new.journal_seq = journal_seq;
		}

		if (flags & BTREE_TRIGGER_NOATOMIC) {
			g->_mark = new;
			break;
		}
	} while ((v = atomic64_cmpxchg(&g->_mark.v,
			      old.v.counter,
			      new.v.counter)) != old.v.counter);

	bch2_dev_usage_update(c, ca, fs_usage, old, new, journal_seq, gc);

	BUG_ON(!gc && bucket_became_unavailable(old, new));

	return 0;
}

static int bch2_mark_stripe_ptr(struct bch_fs *c,
				struct bch_extent_stripe_ptr p,
				enum bch_data_type data_type,
				struct bch_fs_usage *fs_usage,
				s64 sectors, unsigned flags)
{
	bool gc = flags & BTREE_TRIGGER_GC;
	struct bch_replicas_padded r;
	struct stripe *m;
	unsigned i, blocks_nonempty = 0;

	m = genradix_ptr(&c->stripes[gc], p.idx);

	spin_lock(&c->ec_stripes_heap_lock);

	if (!m || !m->alive) {
		spin_unlock(&c->ec_stripes_heap_lock);
		bch_err_ratelimited(c, "pointer to nonexistent stripe %llu",
				    (u64) p.idx);
		bch2_inconsistent_error(c);
		return -EIO;
	}

	m->block_sectors[p.block] += sectors;

	r = m->r;

	for (i = 0; i < m->nr_blocks; i++)
		blocks_nonempty += m->block_sectors[i] != 0;

	if (m->blocks_nonempty != blocks_nonempty) {
		m->blocks_nonempty = blocks_nonempty;
		if (!gc)
			bch2_stripes_heap_update(c, m, p.idx);
	}

	spin_unlock(&c->ec_stripes_heap_lock);

	r.e.data_type = data_type;
	update_replicas(c, fs_usage, &r.e, sectors);

	return 0;
}

static int bch2_mark_extent(struct bch_fs *c,
			    struct bkey_s_c old, struct bkey_s_c new,
			    unsigned offset, s64 sectors,
			    enum bch_data_type data_type,
			    struct bch_fs_usage *fs_usage,
			    unsigned journal_seq, unsigned flags)
{
	struct bkey_s_c k = flags & BTREE_TRIGGER_INSERT ? new : old;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct bch_replicas_padded r;
	s64 dirty_sectors = 0;
	bool stale;
	int ret;

	r.e.data_type	= data_type;
	r.e.nr_devs	= 0;
	r.e.nr_required	= 1;

	BUG_ON(!sectors);

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		s64 disk_sectors = data_type == BCH_DATA_btree
			? sectors
			: ptr_disk_sectors_delta(p, offset, sectors, flags);

		ret = bch2_mark_pointer(c, k, p, disk_sectors, data_type,
					fs_usage, journal_seq, flags);
		if (ret < 0)
			return ret;

		stale = ret > 0;

		if (p.ptr.cached) {
			if (!stale)
				if (update_cached_sectors(c, fs_usage, p.ptr.dev,
							  disk_sectors)) {
					bch2_fs_fatal_error(c, "bch2_mark_extent(): no replicas entry while updating cached sectors");
					return -1;

				}
		} else if (!p.has_ec) {
			dirty_sectors	       += disk_sectors;
			r.e.devs[r.e.nr_devs++]	= p.ptr.dev;
		} else {
			ret = bch2_mark_stripe_ptr(c, p.ec, data_type,
					fs_usage, disk_sectors, flags);
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
		if (update_replicas(c, fs_usage, &r.e, dirty_sectors)) {
			char buf[200];

			bch2_bkey_val_to_text(&PBUF(buf), c, k);
			bch2_fs_fatal_error(c, "no replicas entry for %s", buf);
			return -1;
		}
	}

	return 0;
}

static int bch2_mark_stripe(struct bch_fs *c,
			    struct bkey_s_c old, struct bkey_s_c new,
			    struct bch_fs_usage *fs_usage,
			    u64 journal_seq, unsigned flags)
{
	bool gc = flags & BTREE_TRIGGER_GC;
	size_t idx = new.k->p.offset;
	const struct bch_stripe *old_s = old.k->type == KEY_TYPE_stripe
		? bkey_s_c_to_stripe(old).v : NULL;
	const struct bch_stripe *new_s = new.k->type == KEY_TYPE_stripe
		? bkey_s_c_to_stripe(new).v : NULL;
	struct stripe *m = genradix_ptr(&c->stripes[gc], idx);
	unsigned i;
	int ret;

	BUG_ON(gc && old_s);

	if (!m || (old_s && !m->alive)) {
		bch_err_ratelimited(c, "error marking nonexistent stripe %zu",
				    idx);
		bch2_inconsistent_error(c);
		return -1;
	}

	if (!new_s) {
		spin_lock(&c->ec_stripes_heap_lock);
		bch2_stripes_heap_del(c, m, idx);
		spin_unlock(&c->ec_stripes_heap_lock);

		memset(m, 0, sizeof(*m));
	} else {
		m->alive	= true;
		m->sectors	= le16_to_cpu(new_s->sectors);
		m->algorithm	= new_s->algorithm;
		m->nr_blocks	= new_s->nr_blocks;
		m->nr_redundant	= new_s->nr_redundant;
		m->blocks_nonempty = 0;

		for (i = 0; i < new_s->nr_blocks; i++) {
			m->block_sectors[i] =
				stripe_blockcount_get(new_s, i);
			m->blocks_nonempty += !!m->block_sectors[i];

			m->ptrs[i] = new_s->ptrs[i];
		}

		bch2_bkey_to_replicas(&m->r.e, new);

		if (!gc) {
			spin_lock(&c->ec_stripes_heap_lock);
			bch2_stripes_heap_update(c, m, idx);
			spin_unlock(&c->ec_stripes_heap_lock);
		}
	}

	if (gc) {
		/*
		 * gc recalculates this field from stripe ptr
		 * references:
		 */
		memset(m->block_sectors, 0, sizeof(m->block_sectors));
		m->blocks_nonempty = 0;

		for (i = 0; i < new_s->nr_blocks; i++) {
			ret = mark_stripe_bucket(c, new, i, fs_usage,
						 journal_seq, flags);
			if (ret)
				return ret;
		}

		if (update_replicas(c, fs_usage, &m->r.e,
				((s64) m->sectors * m->nr_redundant))) {
			char buf[200];

			bch2_bkey_val_to_text(&PBUF(buf), c, new);
			bch2_fs_fatal_error(c, "no replicas entry for %s", buf);
			return -1;
		}
	}

	return 0;
}

static int __reflink_p_frag_references(struct bkey_s_c_reflink_p p,
				       u64 p_start, u64 p_end,
				       u64 v_start, u64 v_end)
{
	if (p_start == p_end)
		return false;

	p_start	+= le64_to_cpu(p.v->idx);
	p_end	+= le64_to_cpu(p.v->idx);

	if (p_end <= v_start)
		return false;
	if (p_start >= v_end)
		return false;
	return true;
}

static int reflink_p_frag_references(struct bkey_s_c_reflink_p p,
				     u64 start, u64 end,
				     struct bkey_s_c k)
{
	return __reflink_p_frag_references(p, start, end,
					   bkey_start_offset(k.k),
					   k.k->p.offset);
}

static int __bch2_mark_reflink_p(struct bch_fs *c,
			struct bkey_s_c_reflink_p p,
			u64 idx, unsigned sectors,
			unsigned front_frag,
			unsigned back_frag,
			unsigned flags,
			size_t *r_idx)
{
	struct reflink_gc *r;
	int add = !(flags & BTREE_TRIGGER_OVERWRITE) ? 1 : -1;
	int frags_referenced;

	while (1) {
		if (*r_idx >= c->reflink_gc_nr)
			goto not_found;
		r = genradix_ptr(&c->reflink_gc_table, *r_idx);
		BUG_ON(!r);

		if (r->offset > idx)
			break;
		(*r_idx)++;
	}

	frags_referenced =
		__reflink_p_frag_references(p, 0, front_frag,
					    r->offset - r->size, r->offset) +
		__reflink_p_frag_references(p, back_frag, p.k->size,
					    r->offset - r->size, r->offset);

	if (frags_referenced == 2) {
		BUG_ON(!(flags & BTREE_TRIGGER_OVERWRITE_SPLIT));
		add = -add;
	} else if (frags_referenced == 1) {
		BUG_ON(!(flags & BTREE_TRIGGER_OVERWRITE));
		add = 0;
	}

	BUG_ON((s64) r->refcount + add < 0);

	r->refcount += add;
	return min_t(u64, sectors, r->offset - idx);
not_found:
	bch2_fs_inconsistent(c,
		"%llu:%llu len %u points to nonexistent indirect extent %llu",
		p.k->p.inode, p.k->p.offset, p.k->size, idx);
	bch2_inconsistent_error(c);
	return -EIO;
}

static int bch2_mark_reflink_p(struct bch_fs *c,
			       struct bkey_s_c_reflink_p p, unsigned offset,
			       s64 sectors, unsigned flags)
{
	u64 idx = le64_to_cpu(p.v->idx) + offset;
	struct reflink_gc *ref;
	size_t l, r, m;
	unsigned front_frag, back_frag;
	s64 ret = 0;

	if (sectors < 0)
		sectors = -sectors;

	BUG_ON(offset + sectors > p.k->size);

	front_frag = offset;
	back_frag = offset + sectors;

	l = 0;
	r = c->reflink_gc_nr;
	while (l < r) {
		m = l + (r - l) / 2;

		ref = genradix_ptr(&c->reflink_gc_table, m);
		if (ref->offset <= idx)
			l = m + 1;
		else
			r = m;
	}

	while (sectors) {
		ret = __bch2_mark_reflink_p(c, p, idx, sectors,
				front_frag, back_frag, flags, &l);
		if (ret < 0)
			return ret;

		idx	+= ret;
		sectors	-= ret;
	}

	return 0;
}

static int bch2_mark_key_locked(struct bch_fs *c,
		   struct bkey_s_c old,
		   struct bkey_s_c new,
		   unsigned offset, s64 sectors,
		   struct bch_fs_usage *fs_usage,
		   u64 journal_seq, unsigned flags)
{
	struct bkey_s_c k = flags & BTREE_TRIGGER_INSERT ? new : old;
	int ret = 0;

	BUG_ON(!(flags & (BTREE_TRIGGER_INSERT|BTREE_TRIGGER_OVERWRITE)));

	preempt_disable();

	if (!fs_usage || (flags & BTREE_TRIGGER_GC))
		fs_usage = fs_usage_ptr(c, journal_seq,
					flags & BTREE_TRIGGER_GC);

	switch (k.k->type) {
	case KEY_TYPE_alloc:
	case KEY_TYPE_alloc_v2:
		ret = bch2_mark_alloc(c, old, new, fs_usage, journal_seq, flags);
		break;
	case KEY_TYPE_btree_ptr:
	case KEY_TYPE_btree_ptr_v2:
		sectors = !(flags & BTREE_TRIGGER_OVERWRITE)
			?  c->opts.btree_node_size
			: -c->opts.btree_node_size;

		ret = bch2_mark_extent(c, old, new, offset, sectors,
				BCH_DATA_btree, fs_usage, journal_seq, flags);
		break;
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		ret = bch2_mark_extent(c, old, new, offset, sectors,
				BCH_DATA_user, fs_usage, journal_seq, flags);
		break;
	case KEY_TYPE_stripe:
		ret = bch2_mark_stripe(c, old, new, fs_usage, journal_seq, flags);
		break;
	case KEY_TYPE_inode:
		fs_usage->nr_inodes += new.k->type == KEY_TYPE_inode;
		fs_usage->nr_inodes -= old.k->type == KEY_TYPE_inode;
		break;
	case KEY_TYPE_reservation: {
		unsigned replicas = bkey_s_c_to_reservation(k).v->nr_replicas;

		sectors *= replicas;
		replicas = clamp_t(unsigned, replicas, 1,
				   ARRAY_SIZE(fs_usage->persistent_reserved));

		fs_usage->reserved				+= sectors;
		fs_usage->persistent_reserved[replicas - 1]	+= sectors;
		break;
	}
	case KEY_TYPE_reflink_p:
		ret = bch2_mark_reflink_p(c, bkey_s_c_to_reflink_p(k),
					  offset, sectors, flags);
		break;
	}

	preempt_enable();

	return ret;
}

int bch2_mark_key(struct bch_fs *c, struct bkey_s_c new,
		  unsigned offset, s64 sectors,
		  struct bch_fs_usage *fs_usage,
		  u64 journal_seq, unsigned flags)
{
	struct bkey deleted;
	struct bkey_s_c old = (struct bkey_s_c) { &deleted, NULL };
	int ret;

	bkey_init(&deleted);

	percpu_down_read(&c->mark_lock);
	ret = bch2_mark_key_locked(c, old, new, offset, sectors,
				   fs_usage, journal_seq,
				   BTREE_TRIGGER_INSERT|flags);
	percpu_up_read(&c->mark_lock);

	return ret;
}

int bch2_mark_update(struct btree_trans *trans,
		     struct btree_iter *iter,
		     struct bkey_i *new,
		     struct bch_fs_usage *fs_usage,
		     unsigned flags)
{
	struct bch_fs		*c = trans->c;
	struct bkey_s_c		old;
	struct bkey		unpacked;
	int ret = 0;

	if (unlikely(flags & BTREE_TRIGGER_NORUN))
		return 0;

	if (!btree_node_type_needs_gc(iter->btree_id))
		return 0;

	bkey_init(&unpacked);
	old = (struct bkey_s_c) { &unpacked, NULL };

	if (!btree_node_type_is_extents(iter->btree_id)) {
		/* iterators should be uptodate, shouldn't get errors here: */
		if (btree_iter_type(iter) != BTREE_ITER_CACHED) {
			old = bch2_btree_iter_peek_slot(iter);
			BUG_ON(bkey_err(old));
		} else {
			struct bkey_cached *ck = (void *) iter->l[0].b;

			if (ck->valid)
				old = bkey_i_to_s_c(ck->k);
		}

		if (old.k->type == new->k.type) {
			bch2_mark_key_locked(c, old, bkey_i_to_s_c(new), 0, 0,
				fs_usage, trans->journal_res.seq,
				BTREE_TRIGGER_INSERT|BTREE_TRIGGER_OVERWRITE|flags);

		} else {
			bch2_mark_key_locked(c, old, bkey_i_to_s_c(new), 0, 0,
				fs_usage, trans->journal_res.seq,
				BTREE_TRIGGER_INSERT|flags);
			bch2_mark_key_locked(c, old, bkey_i_to_s_c(new), 0, 0,
				fs_usage, trans->journal_res.seq,
				BTREE_TRIGGER_OVERWRITE|flags);
		}
	} else {
		struct btree_iter *copy;

		BUG_ON(btree_iter_type(iter) == BTREE_ITER_CACHED);
		bch2_mark_key_locked(c, old, bkey_i_to_s_c(new),
			0, new->k.size,
			fs_usage, trans->journal_res.seq,
			BTREE_TRIGGER_INSERT|flags);

		copy = bch2_trans_copy_iter(trans, iter);

		for_each_btree_key_continue(copy, 0, old, ret) {
			unsigned offset = 0;
			s64 sectors = -((s64) old.k->size);

			flags |= BTREE_TRIGGER_OVERWRITE;

			if (bkey_cmp(new->k.p, bkey_start_pos(old.k)) <= 0)
				break;

			switch (bch2_extent_overlap(&new->k, old.k)) {
			case BCH_EXTENT_OVERLAP_ALL:
				offset = 0;
				sectors = -((s64) old.k->size);
				break;
			case BCH_EXTENT_OVERLAP_BACK:
				offset = bkey_start_offset(&new->k) -
					bkey_start_offset(old.k);
				sectors = bkey_start_offset(&new->k) -
					old.k->p.offset;
				break;
			case BCH_EXTENT_OVERLAP_FRONT:
				offset = 0;
				sectors = bkey_start_offset(old.k) -
					new->k.p.offset;
				break;
			case BCH_EXTENT_OVERLAP_MIDDLE:
				offset = bkey_start_offset(&new->k) -
					bkey_start_offset(old.k);
				sectors = -((s64) new->k.size);
				flags |= BTREE_TRIGGER_OVERWRITE_SPLIT;
				break;
			}

			BUG_ON(sectors >= 0);

			ret = bch2_mark_key_locked(c, old, bkey_i_to_s_c(new),
					offset, sectors, fs_usage,
					trans->journal_res.seq, flags) ?: 1;
			if (ret <= 0)
				break;
		}
		bch2_trans_iter_put(trans, copy);
	}

	return ret;
}

static noinline __cold
void fs_usage_apply_warn(struct btree_trans *trans,
			 unsigned disk_res_sectors,
			 s64 should_not_have_added)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	char buf[200];

	bch_err(c, "disk usage increased %lli more than %u sectors reserved",
		should_not_have_added, disk_res_sectors);

	trans_for_each_update(trans, i) {
		pr_err("while inserting");
		bch2_bkey_val_to_text(&PBUF(buf), c, bkey_i_to_s_c(i->k));
		pr_err("%s", buf);
		pr_err("overlapping with");

		if (btree_iter_type(i->iter) != BTREE_ITER_CACHED) {
			struct btree_iter *copy = bch2_trans_copy_iter(trans, i->iter);
			struct bkey_s_c k;
			int ret;

			for_each_btree_key_continue(copy, 0, k, ret) {
				if (btree_node_type_is_extents(i->iter->btree_id)
				    ? bkey_cmp(i->k->k.p, bkey_start_pos(k.k)) <= 0
				    : bkey_cmp(i->k->k.p, k.k->p))
					break;

				bch2_bkey_val_to_text(&PBUF(buf), c, k);
				pr_err("%s", buf);
			}
			bch2_trans_iter_put(trans, copy);
		} else {
			struct bkey_cached *ck = (void *) i->iter->l[0].b;

			if (ck->valid) {
				bch2_bkey_val_to_text(&PBUF(buf), c, bkey_i_to_s_c(ck->k));
				pr_err("%s", buf);
			}
		}
	}
	__WARN();
}

void bch2_trans_fs_usage_apply(struct btree_trans *trans,
			       struct replicas_delta_list *deltas)
{
	struct bch_fs *c = trans->c;
	static int warned_disk_usage = 0;
	bool warn = false;
	unsigned disk_res_sectors = trans->disk_res ? trans->disk_res->sectors : 0;
	struct replicas_delta *d = deltas->d;
	struct replicas_delta *top = (void *) deltas->d + deltas->used;
	struct bch_fs_usage *dst;
	s64 added = 0, should_not_have_added;
	unsigned i;

	percpu_rwsem_assert_held(&c->mark_lock);

	preempt_disable();
	dst = fs_usage_ptr(c, trans->journal_res.seq, false);

	for (d = deltas->d; d != top; d = replicas_delta_next(d)) {
		switch (d->r.data_type) {
		case BCH_DATA_btree:
		case BCH_DATA_user:
		case BCH_DATA_parity:
			added += d->delta;
		}

		BUG_ON(update_replicas(c, dst, &d->r, d->delta));
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
		atomic64_sub(should_not_have_added, &c->sectors_available);
		added -= should_not_have_added;
		warn = true;
	}

	if (added > 0) {
		trans->disk_res->sectors -= added;
		this_cpu_sub(*c->online_reserved, added);
	}

	preempt_enable();

	if (unlikely(warn) && !xchg(&warned_disk_usage, 1))
		fs_usage_apply_warn(trans, disk_res_sectors, should_not_have_added);
}

/* trans_mark: */

static struct btree_iter *trans_get_update(struct btree_trans *trans,
			    enum btree_id btree_id, struct bpos pos,
			    struct bkey_s_c *k)
{
	struct btree_insert_entry *i;

	trans_for_each_update(trans, i)
		if (i->iter->btree_id == btree_id &&
		    (btree_node_type_is_extents(btree_id)
		     ? bkey_cmp(pos, bkey_start_pos(&i->k->k)) >= 0 &&
		       bkey_cmp(pos, i->k->k.p) < 0
		     : !bkey_cmp(pos, i->iter->pos))) {
			*k = bkey_i_to_s_c(i->k);

			/* ugly hack.. */
			BUG_ON(btree_iter_live(trans, i->iter));
			trans->iters_live |= 1ULL << i->iter->idx;
			return i->iter;
		}

	return NULL;
}

static struct bkey_alloc_buf *
bch2_trans_start_alloc_update(struct btree_trans *trans, struct btree_iter **_iter,
			      const struct bch_extent_ptr *ptr,
			      struct bkey_alloc_unpacked *u)
{
	struct bch_fs *c = trans->c;
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
	struct bpos pos = POS(ptr->dev, PTR_BUCKET_NR(ca, ptr));
	struct bucket *g;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_alloc_buf *a;
	int ret;

	a = bch2_trans_kmalloc(trans, sizeof(struct bkey_alloc_buf));
	if (IS_ERR(a))
		return a;

	iter = trans_get_update(trans, BTREE_ID_alloc, pos, &k);
	if (iter) {
		*u = bch2_alloc_unpack(k);
	} else {
		iter = bch2_trans_get_iter(trans, BTREE_ID_alloc, pos,
					   BTREE_ITER_CACHED|
					   BTREE_ITER_CACHED_NOFILL|
					   BTREE_ITER_INTENT);
		ret = bch2_btree_iter_traverse(iter);
		if (ret) {
			bch2_trans_iter_put(trans, iter);
			return ERR_PTR(ret);
		}

		percpu_down_read(&c->mark_lock);
		g = bucket(ca, pos.offset);
		*u = alloc_mem_to_key(iter, g, READ_ONCE(g->mark));
		percpu_up_read(&c->mark_lock);
	}

	*_iter = iter;
	return a;
}

static int bch2_trans_mark_pointer(struct btree_trans *trans,
			struct bkey_s_c k, struct extent_ptr_decoded p,
			s64 sectors, enum bch_data_type data_type)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter;
	struct bkey_alloc_unpacked u;
	struct bkey_alloc_buf *a;
	int ret;

	a = bch2_trans_start_alloc_update(trans, &iter, &p.ptr, &u);
	if (IS_ERR(a))
		return PTR_ERR(a);

	ret = __mark_pointer(c, k, &p.ptr, sectors, data_type, u.gen, &u.data_type,
			     &u.dirty_sectors, &u.cached_sectors);
	if (ret)
		goto out;

	bch2_alloc_pack(c, a, u);
	bch2_trans_update(trans, iter, &a->k, 0);
out:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int bch2_trans_mark_stripe_ptr(struct btree_trans *trans,
			struct extent_ptr_decoded p,
			s64 sectors, enum bch_data_type data_type)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_i_stripe *s;
	struct bch_replicas_padded r;
	int ret = 0;

	iter = bch2_trans_get_iter(trans, BTREE_ID_stripes, POS(0, p.ec.idx),
				   BTREE_ITER_INTENT|
				   BTREE_ITER_WITH_UPDATES);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	if (k.k->type != KEY_TYPE_stripe) {
		bch2_fs_inconsistent(c,
			"pointer to nonexistent stripe %llu",
			(u64) p.ec.idx);
		bch2_inconsistent_error(c);
		ret = -EIO;
		goto err;
	}

	if (!bch2_ptr_matches_stripe(bkey_s_c_to_stripe(k).v, p)) {
		bch2_fs_inconsistent(c,
			"stripe pointer doesn't match stripe %llu",
			(u64) p.ec.idx);
		ret = -EIO;
		goto err;
	}

	s = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
	ret = PTR_ERR_OR_ZERO(s);
	if (ret)
		goto err;

	bkey_reassemble(&s->k_i, k);
	stripe_blockcount_set(&s->v, p.ec.block,
		stripe_blockcount_get(&s->v, p.ec.block) +
		sectors);
	bch2_trans_update(trans, iter, &s->k_i, 0);

	bch2_bkey_to_replicas(&r.e, bkey_i_to_s_c(&s->k_i));
	r.e.data_type = data_type;
	update_replicas_list(trans, &r.e, sectors);
err:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int bch2_trans_mark_extent(struct btree_trans *trans,
			struct bkey_s_c k, unsigned offset,
			s64 sectors, unsigned flags,
			enum bch_data_type data_type)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct bch_replicas_padded r;
	s64 dirty_sectors = 0;
	bool stale;
	int ret;

	r.e.data_type	= data_type;
	r.e.nr_devs	= 0;
	r.e.nr_required	= 1;

	BUG_ON(!sectors);

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		s64 disk_sectors = data_type == BCH_DATA_btree
			? sectors
			: ptr_disk_sectors_delta(p, offset, sectors, flags);

		ret = bch2_trans_mark_pointer(trans, k, p, disk_sectors,
					      data_type);
		if (ret < 0)
			return ret;

		stale = ret > 0;

		if (p.ptr.cached) {
			if (!stale)
				update_cached_sectors_list(trans, p.ptr.dev,
							   disk_sectors);
		} else if (!p.has_ec) {
			dirty_sectors	       += disk_sectors;
			r.e.devs[r.e.nr_devs++]	= p.ptr.dev;
		} else {
			ret = bch2_trans_mark_stripe_ptr(trans, p,
					disk_sectors, data_type);
			if (ret)
				return ret;

			r.e.nr_required = 0;
		}
	}

	if (r.e.nr_devs)
		update_replicas_list(trans, &r.e, dirty_sectors);

	return 0;
}

static int bch2_trans_mark_stripe_alloc_ref(struct btree_trans *trans,
					    struct bkey_s_c_stripe s,
					    unsigned idx, bool deleting)
{
	struct bch_fs *c = trans->c;
	const struct bch_extent_ptr *ptr = &s.v->ptrs[idx];
	struct bkey_alloc_buf *a;
	struct btree_iter *iter;
	struct bkey_alloc_unpacked u;
	bool parity = idx >= s.v->nr_blocks - s.v->nr_redundant;
	int ret = 0;

	a = bch2_trans_start_alloc_update(trans, &iter, ptr, &u);
	if (IS_ERR(a))
		return PTR_ERR(a);

	if (parity) {
		s64 sectors = le16_to_cpu(s.v->sectors);

		if (deleting)
			sectors = -sectors;

		u.dirty_sectors += sectors;
		u.data_type = u.dirty_sectors
			? BCH_DATA_parity
			: 0;
	}

	if (!deleting) {
		if (bch2_fs_inconsistent_on(u.stripe && u.stripe != s.k->p.offset, c,
				"bucket %llu:%llu gen %u: multiple stripes using same bucket (%u, %llu)",
				iter->pos.inode, iter->pos.offset, u.gen,
				u.stripe, s.k->p.offset)) {
			ret = -EIO;
			goto err;
		}

		u.stripe		= s.k->p.offset;
		u.stripe_redundancy	= s.v->nr_redundant;
	} else {
		u.stripe		= 0;
		u.stripe_redundancy	= 0;
	}

	bch2_alloc_pack(c, a, u);
	bch2_trans_update(trans, iter, &a->k, 0);
err:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int bch2_trans_mark_stripe(struct btree_trans *trans,
				  struct bkey_s_c old, struct bkey_s_c new,
				  unsigned flags)
{
	struct bkey_s_c_stripe old_s = { .k = NULL };
	struct bkey_s_c_stripe new_s = { .k = NULL };
	struct bch_replicas_padded r;
	unsigned i;
	int ret = 0;

	if (old.k->type == KEY_TYPE_stripe)
		old_s = bkey_s_c_to_stripe(old);
	if (new.k->type == KEY_TYPE_stripe)
		new_s = bkey_s_c_to_stripe(new);

	/*
	 * If the pointers aren't changing, we don't need to do anything:
	 */
	if (new_s.k && old_s.k &&
	    new_s.v->nr_blocks		== old_s.v->nr_blocks &&
	    new_s.v->nr_redundant	== old_s.v->nr_redundant &&
	    !memcmp(old_s.v->ptrs, new_s.v->ptrs,
		    new_s.v->nr_blocks * sizeof(struct bch_extent_ptr)))
		return 0;

	if (new_s.k) {
		s64 sectors = le16_to_cpu(new_s.v->sectors);

		bch2_bkey_to_replicas(&r.e, new);
		update_replicas_list(trans, &r.e, sectors * new_s.v->nr_redundant);

		for (i = 0; i < new_s.v->nr_blocks; i++) {
			ret = bch2_trans_mark_stripe_alloc_ref(trans, new_s,
							       i, false);
			if (ret)
				return ret;
		}
	}

	if (old_s.k) {
		s64 sectors = -((s64) le16_to_cpu(old_s.v->sectors));

		bch2_bkey_to_replicas(&r.e, old);
		update_replicas_list(trans, &r.e, sectors * old_s.v->nr_redundant);

		for (i = 0; i < old_s.v->nr_blocks; i++) {
			ret = bch2_trans_mark_stripe_alloc_ref(trans, old_s,
							       i, true);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static int __bch2_trans_mark_reflink_p(struct btree_trans *trans,
			struct bkey_s_c_reflink_p p,
			u64 idx, unsigned sectors,
			unsigned front_frag,
			unsigned back_frag,
			unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_i *n;
	__le64 *refcount;
	int add = !(flags & BTREE_TRIGGER_OVERWRITE) ? 1 : -1;
	int frags_referenced;
	s64 ret;

	iter = bch2_trans_get_iter(trans, BTREE_ID_reflink, POS(0, idx),
				   BTREE_ITER_INTENT|
				   BTREE_ITER_WITH_UPDATES);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	sectors = min_t(u64, sectors, k.k->p.offset - idx);

	frags_referenced =
		reflink_p_frag_references(p, 0, front_frag, k) +
		reflink_p_frag_references(p, back_frag, p.k->size, k);

	if (frags_referenced == 2) {
		BUG_ON(!(flags & BTREE_TRIGGER_OVERWRITE_SPLIT));
		add = -add;
	} else if (frags_referenced == 1) {
		BUG_ON(!(flags & BTREE_TRIGGER_OVERWRITE));
		goto out;
	}

	n = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
	ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		goto err;

	bkey_reassemble(n, k);

	refcount = bkey_refcount(n);
	if (!refcount) {
		bch2_fs_inconsistent(c,
			"%llu:%llu len %u points to nonexistent indirect extent %llu",
			p.k->p.inode, p.k->p.offset, p.k->size, idx);
		bch2_inconsistent_error(c);
		ret = -EIO;
		goto err;
	}

	BUG_ON(!*refcount && (flags & BTREE_TRIGGER_OVERWRITE));
	le64_add_cpu(refcount, add);

	if (!*refcount) {
		n->k.type = KEY_TYPE_deleted;
		set_bkey_val_u64s(&n->k, 0);
	}

	bch2_btree_iter_set_pos(iter, bkey_start_pos(k.k));
	ret = bch2_trans_update(trans, iter, n, 0);
	if (ret)
		goto err;
out:
	ret = sectors;
err:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int bch2_trans_mark_reflink_p(struct btree_trans *trans,
			struct bkey_s_c_reflink_p p, unsigned offset,
			s64 sectors, unsigned flags)
{
	u64 idx = le64_to_cpu(p.v->idx) + offset;
	unsigned front_frag, back_frag;
	s64 ret = 0;

	if (sectors < 0)
		sectors = -sectors;

	BUG_ON(offset + sectors > p.k->size);

	front_frag = offset;
	back_frag = offset + sectors;

	while (sectors) {
		ret = __bch2_trans_mark_reflink_p(trans, p, idx, sectors,
					front_frag, back_frag, flags);
		if (ret < 0)
			return ret;

		idx	+= ret;
		sectors	-= ret;
	}

	return 0;
}

int bch2_trans_mark_key(struct btree_trans *trans,
			struct bkey_s_c old,
			struct bkey_s_c new,
			unsigned offset, s64 sectors, unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k = flags & BTREE_TRIGGER_INSERT ? new : old;
	struct replicas_delta_list *d;

	BUG_ON(!(flags & (BTREE_TRIGGER_INSERT|BTREE_TRIGGER_OVERWRITE)));

	switch (k.k->type) {
	case KEY_TYPE_btree_ptr:
	case KEY_TYPE_btree_ptr_v2:
		sectors = !(flags & BTREE_TRIGGER_OVERWRITE)
			?  c->opts.btree_node_size
			: -c->opts.btree_node_size;

		return bch2_trans_mark_extent(trans, k, offset, sectors,
					      flags, BCH_DATA_btree);
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		return bch2_trans_mark_extent(trans, k, offset, sectors,
					      flags, BCH_DATA_user);
	case KEY_TYPE_stripe:
		return bch2_trans_mark_stripe(trans, old, new, flags);
	case KEY_TYPE_inode: {
		int nr = (new.k->type == KEY_TYPE_inode) -
			 (old.k->type == KEY_TYPE_inode);

		if (nr) {
			d = replicas_deltas_realloc(trans, 0);
			d->nr_inodes += nr;
		}

		return 0;
	}
	case KEY_TYPE_reservation: {
		unsigned replicas = bkey_s_c_to_reservation(k).v->nr_replicas;

		d = replicas_deltas_realloc(trans, 0);

		sectors *= replicas;
		replicas = clamp_t(unsigned, replicas, 1,
				   ARRAY_SIZE(d->persistent_reserved));

		d->persistent_reserved[replicas - 1] += sectors;
		return 0;
	}
	case KEY_TYPE_reflink_p:
		return bch2_trans_mark_reflink_p(trans,
					bkey_s_c_to_reflink_p(k),
					offset, sectors, flags);
	default:
		return 0;
	}
}

int bch2_trans_mark_update(struct btree_trans *trans,
			   struct btree_iter *iter,
			   struct bkey_i *new,
			   unsigned flags)
{
	struct bkey_s_c	old;
	int ret;

	if (unlikely(flags & BTREE_TRIGGER_NORUN))
		return 0;

	if (!btree_node_type_needs_gc(iter->btree_id))
		return 0;

	if (btree_iter_type(iter) != BTREE_ITER_CACHED) {
		old = bch2_btree_iter_peek_slot(iter);
		ret = bkey_err(old);
		if (ret)
			return ret;
	} else {
		struct bkey_cached *ck = (void *) iter->l[0].b;

		BUG_ON(!ck->valid);
		old = bkey_i_to_s_c(ck->k);
	}

	if (old.k->type == new->k.type &&
	    !btree_node_type_is_extents(iter->btree_id)) {
		ret   = bch2_trans_mark_key(trans, old, bkey_i_to_s_c(new), 0, 0,
				BTREE_TRIGGER_INSERT|BTREE_TRIGGER_OVERWRITE|flags);
	} else {
		ret   = bch2_trans_mark_key(trans, old, bkey_i_to_s_c(new), 0, new->k.size,
				BTREE_TRIGGER_INSERT|flags) ?:
			bch2_trans_mark_key(trans, old, bkey_i_to_s_c(new), 0, -((s64) old.k->size),
				BTREE_TRIGGER_OVERWRITE|flags);
	}

	return ret;
}

static int __bch2_trans_mark_metadata_bucket(struct btree_trans *trans,
				    struct bch_dev *ca, size_t b,
				    enum bch_data_type type,
				    unsigned sectors)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter;
	struct bkey_alloc_unpacked u;
	struct bkey_alloc_buf *a;
	struct bch_extent_ptr ptr = {
		.dev = ca->dev_idx,
		.offset = bucket_to_sector(ca, b),
	};
	int ret = 0;

	/*
	 * Backup superblock might be past the end of our normal usable space:
	 */
	if (b >= ca->mi.nbuckets)
		return 0;

	a = bch2_trans_start_alloc_update(trans, &iter, &ptr, &u);
	if (IS_ERR(a))
		return PTR_ERR(a);

	if (u.data_type && u.data_type != type) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %llu:%llu gen %u different types of data in same bucket: %s, %s\n"
			"while marking %s",
			iter->pos.inode, iter->pos.offset, u.gen,
			bch2_data_types[u.data_type],
			bch2_data_types[type],
			bch2_data_types[type]);
		ret = -EIO;
		goto out;
	}

	u.data_type	= type;
	u.dirty_sectors	= sectors;

	bch2_alloc_pack(c, a, u);
	bch2_trans_update(trans, iter, &a->k, 0);
out:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

int bch2_trans_mark_metadata_bucket(struct btree_trans *trans,
				    struct bch_dev *ca, size_t b,
				    enum bch_data_type type,
				    unsigned sectors)
{
	return __bch2_trans_do(trans, NULL, NULL, 0,
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
	return bch2_trans_do(c, NULL, NULL, BTREE_INSERT_LAZY_RW,
			__bch2_trans_mark_dev_sb(&trans, ca));
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
		ret = -ENOSPC;
	}

	mutex_unlock(&c->sectors_available_lock);
	percpu_up_read(&c->mark_lock);

	return ret;
}

/* Startup/shutdown: */

static void buckets_free_rcu(struct rcu_head *rcu)
{
	struct bucket_array *buckets =
		container_of(rcu, struct bucket_array, rcu);

	kvpfree(buckets,
		sizeof(struct bucket_array) +
		buckets->nbuckets * sizeof(struct bucket));
}

int bch2_dev_buckets_resize(struct bch_fs *c, struct bch_dev *ca, u64 nbuckets)
{
	struct bucket_array *buckets = NULL, *old_buckets = NULL;
	unsigned long *buckets_nouse = NULL;
	alloc_fifo	free[RESERVE_NR];
	alloc_fifo	free_inc;
	alloc_heap	alloc_heap;

	size_t btree_reserve	= DIV_ROUND_UP(BTREE_NODE_RESERVE,
			     ca->mi.bucket_size / c->opts.btree_node_size);
	/* XXX: these should be tunable */
	size_t reserve_none	= max_t(size_t, 1, nbuckets >> 9);
	size_t copygc_reserve	= max_t(size_t, 2, nbuckets >> 6);
	size_t free_inc_nr	= max(max_t(size_t, 1, nbuckets >> 12),
				      btree_reserve * 2);
	bool resize = ca->buckets[0] != NULL;
	int ret = -ENOMEM;
	unsigned i;

	memset(&free,		0, sizeof(free));
	memset(&free_inc,	0, sizeof(free_inc));
	memset(&alloc_heap,	0, sizeof(alloc_heap));

	if (!(buckets		= kvpmalloc(sizeof(struct bucket_array) +
					    nbuckets * sizeof(struct bucket),
					    GFP_KERNEL|__GFP_ZERO)) ||
	    !(buckets_nouse	= kvpmalloc(BITS_TO_LONGS(nbuckets) *
					    sizeof(unsigned long),
					    GFP_KERNEL|__GFP_ZERO)) ||
	    !init_fifo(&free[RESERVE_MOVINGGC],
		       copygc_reserve, GFP_KERNEL) ||
	    !init_fifo(&free[RESERVE_NONE], reserve_none, GFP_KERNEL) ||
	    !init_fifo(&free_inc,	free_inc_nr, GFP_KERNEL) ||
	    !init_heap(&alloc_heap,	ALLOC_SCAN_BATCH(ca) << 1, GFP_KERNEL))
		goto err;

	buckets->first_bucket	= ca->mi.first_bucket;
	buckets->nbuckets	= nbuckets;

	bch2_copygc_stop(c);

	if (resize) {
		down_write(&c->gc_lock);
		down_write(&ca->bucket_lock);
		percpu_down_write(&c->mark_lock);
	}

	old_buckets = bucket_array(ca);

	if (resize) {
		size_t n = min(buckets->nbuckets, old_buckets->nbuckets);

		memcpy(buckets->b,
		       old_buckets->b,
		       n * sizeof(struct bucket));
		memcpy(buckets_nouse,
		       ca->buckets_nouse,
		       BITS_TO_LONGS(n) * sizeof(unsigned long));
	}

	rcu_assign_pointer(ca->buckets[0], buckets);
	buckets = old_buckets;

	swap(ca->buckets_nouse, buckets_nouse);

	if (resize) {
		percpu_up_write(&c->mark_lock);
		up_write(&c->gc_lock);
	}

	spin_lock(&c->freelist_lock);
	for (i = 0; i < RESERVE_NR; i++) {
		fifo_move(&free[i], &ca->free[i]);
		swap(ca->free[i], free[i]);
	}
	fifo_move(&free_inc, &ca->free_inc);
	swap(ca->free_inc, free_inc);
	spin_unlock(&c->freelist_lock);

	/* with gc lock held, alloc_heap can't be in use: */
	swap(ca->alloc_heap, alloc_heap);

	nbuckets = ca->mi.nbuckets;

	if (resize)
		up_write(&ca->bucket_lock);

	ret = 0;
err:
	free_heap(&alloc_heap);
	free_fifo(&free_inc);
	for (i = 0; i < RESERVE_NR; i++)
		free_fifo(&free[i]);
	kvpfree(buckets_nouse,
		BITS_TO_LONGS(nbuckets) * sizeof(unsigned long));
	if (buckets)
		call_rcu(&old_buckets->rcu, buckets_free_rcu);

	return ret;
}

void bch2_dev_buckets_free(struct bch_dev *ca)
{
	unsigned i;

	free_heap(&ca->alloc_heap);
	free_fifo(&ca->free_inc);
	for (i = 0; i < RESERVE_NR; i++)
		free_fifo(&ca->free[i]);
	kvpfree(ca->buckets_nouse,
		BITS_TO_LONGS(ca->mi.nbuckets) * sizeof(unsigned long));
	kvpfree(rcu_dereference_protected(ca->buckets[0], 1),
		sizeof(struct bucket_array) +
		ca->mi.nbuckets * sizeof(struct bucket));

	for (i = 0; i < ARRAY_SIZE(ca->usage); i++)
		free_percpu(ca->usage[i]);
	kfree(ca->usage_base);
}

int bch2_dev_buckets_alloc(struct bch_fs *c, struct bch_dev *ca)
{
	unsigned i;

	ca->usage_base = kzalloc(sizeof(struct bch_dev_usage), GFP_KERNEL);
	if (!ca->usage_base)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(ca->usage); i++) {
		ca->usage[i] = alloc_percpu(struct bch_dev_usage);
		if (!ca->usage[i])
			return -ENOMEM;
	}

	return bch2_dev_buckets_resize(c, ca, ca->mi.nbuckets);;
}
