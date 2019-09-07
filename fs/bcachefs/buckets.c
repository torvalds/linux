// SPDX-License-Identifier: GPL-2.0
/*
 * Code for manipulating bucket marks for garbage collection.
 *
 * Copyright 2014 Datera, Inc.
 *
 * Bucket states:
 * - free bucket: mark == 0
 *   The bucket contains no data and will not be read
 *
 * - allocator bucket: owned_by_allocator == 1
 *   The bucket is on a free list, or it is an open bucket
 *
 * - cached bucket: owned_by_allocator == 0 &&
 *                  dirty_sectors == 0 &&
 *                  cached_sectors > 0
 *   The bucket contains data but may be safely discarded as there are
 *   enough replicas of the data on other cache devices, or it has been
 *   written back to the backing device
 *
 * - dirty bucket: owned_by_allocator == 0 &&
 *                 dirty_sectors > 0
 *   The bucket contains data that we must not discard (either only copy,
 *   or one of the 'main copies' for data requiring multiple replicas)
 *
 * - metadata bucket: owned_by_allocator == 0 && is_metadata == 1
 *   This is a btree node, journal or gen/prio bucket
 *
 * Lifecycle:
 *
 * bucket invalidated => bucket on freelist => open bucket =>
 *     [dirty bucket =>] cached bucket => bucket invalidated => ...
 *
 * Note that cache promotion can skip the dirty bucket step, as data
 * is copied from a deeper tier to a shallower tier, onto a cached
 * bucket.
 * Note also that a cached bucket can spontaneously become dirty --
 * see below.
 *
 * Only a traversal of the key space can determine whether a bucket is
 * truly dirty or cached.
 *
 * Transitions:
 *
 * - free => allocator: bucket was invalidated
 * - cached => allocator: bucket was invalidated
 *
 * - allocator => dirty: open bucket was filled up
 * - allocator => cached: open bucket was filled up
 * - allocator => metadata: metadata was allocated
 *
 * - dirty => cached: dirty sectors were copied to a deeper tier
 * - dirty => free: dirty sectors were overwritten or moved (copy gc)
 * - cached => free: cached sectors were overwritten
 *
 * - metadata => free: metadata was freed
 *
 * Oddities:
 * - cached => dirty: a device was removed so formerly replicated data
 *                    is no longer sufficiently replicated
 * - free => cached: cannot happen
 * - free => dirty: cannot happen
 * - free => metadata: cannot happen
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
#include "replicas.h"
#include "trace.h"

#include <linux/preempt.h>

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

		switch (e->data_type) {
		case BCH_DATA_BTREE:
			usage->btree	+= usage->replicas[i];
			break;
		case BCH_DATA_USER:
			usage->data	+= usage->replicas[i];
			break;
		case BCH_DATA_CACHED:
			usage->cached	+= usage->replicas[i];
			break;
		}
	}

	percpu_up_write(&c->mark_lock);
}

void bch2_fs_usage_scratch_put(struct bch_fs *c, struct bch_fs_usage_online *fs_usage)
{
	if (fs_usage == c->usage_scratch)
		mutex_unlock(&c->usage_scratch_lock);
	else
		kfree(fs_usage);
}

struct bch_fs_usage_online *bch2_fs_usage_scratch_get(struct bch_fs *c)
{
	struct bch_fs_usage_online *ret;
	unsigned bytes = sizeof(struct bch_fs_usage_online) + sizeof(u64) *
		READ_ONCE(c->replicas.nr);
	ret = kzalloc(bytes, GFP_NOWAIT|__GFP_NOWARN);
	if (ret)
		return ret;

	if (mutex_trylock(&c->usage_scratch_lock))
		goto out_pool;

	ret = kzalloc(bytes, GFP_NOFS);
	if (ret)
		return ret;

	mutex_lock(&c->usage_scratch_lock);
out_pool:
	ret = c->usage_scratch;
	memset(ret, 0, bytes);
	return ret;
}

struct bch_dev_usage bch2_dev_usage_read(struct bch_fs *c, struct bch_dev *ca)
{
	struct bch_dev_usage ret;

	memset(&ret, 0, sizeof(ret));
	acc_u64s_percpu((u64 *) &ret,
			(u64 __percpu *) ca->usage[0],
			sizeof(ret) / sizeof(u64));

	return ret;
}

static inline struct bch_fs_usage *fs_usage_ptr(struct bch_fs *c,
						unsigned journal_seq,
						bool gc)
{
	return this_cpu_ptr(gc
			    ? c->usage_gc
			    : c->usage[journal_seq & 1]);
}

u64 bch2_fs_usage_read_one(struct bch_fs *c, u64 *v)
{
	ssize_t offset = v - (u64 *) c->usage_base;
	unsigned seq;
	u64 ret;

	BUG_ON(offset < 0 || offset >= fs_usage_u64s(c));
	percpu_rwsem_assert_held(&c->mark_lock);

	do {
		seq = read_seqcount_begin(&c->usage_lock);
		ret = *v +
			percpu_u64_get((u64 __percpu *) c->usage[0] + offset) +
			percpu_u64_get((u64 __percpu *) c->usage[1] + offset);
	} while (read_seqcount_retry(&c->usage_lock, seq));

	return ret;
}

struct bch_fs_usage_online *bch2_fs_usage_read(struct bch_fs *c)
{
	struct bch_fs_usage_online *ret;
	unsigned seq, i, u64s;

	percpu_down_read(&c->mark_lock);

	ret = kmalloc(sizeof(struct bch_fs_usage_online) +
		      sizeof(u64) + c->replicas.nr, GFP_NOFS);
	if (unlikely(!ret)) {
		percpu_up_read(&c->mark_lock);
		return NULL;
	}

	ret->online_reserved = percpu_u64_get(c->online_reserved);

	u64s = fs_usage_u64s(c);
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
	unsigned u64s = fs_usage_u64s(c);

	BUG_ON(idx >= ARRAY_SIZE(c->usage));

	preempt_disable();
	write_seqcount_begin(&c->usage_lock);

	acc_u64s_percpu((u64 *) c->usage_base,
			(u64 __percpu *) c->usage[idx], u64s);
	percpu_memset(c->usage[idx], 0, u64s * sizeof(u64));

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

#define RESERVE_FACTOR	6

static u64 reserve_factor(u64 r)
{
	return r + (round_up(r, (1 << RESERVE_FACTOR)) >> RESERVE_FACTOR);
}

static u64 avail_factor(u64 r)
{
	return (r << RESERVE_FACTOR) / ((1 << RESERVE_FACTOR) + 1);
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

static inline int is_fragmented_bucket(struct bucket_mark m,
				       struct bch_dev *ca)
{
	if (!m.owned_by_allocator &&
	    m.data_type == BCH_DATA_USER &&
	    bucket_sectors_used(m))
		return max_t(int, 0, (int) ca->mi.bucket_size -
			     bucket_sectors_used(m));
	return 0;
}

static inline enum bch_data_type bucket_type(struct bucket_mark m)
{
	return m.cached_sectors && !m.dirty_sectors
		? BCH_DATA_CACHED
		: m.data_type;
}

static bool bucket_became_unavailable(struct bucket_mark old,
				      struct bucket_mark new)
{
	return is_available_bucket(old) &&
	       !is_available_bucket(new);
}

int bch2_fs_usage_apply(struct bch_fs *c,
			struct bch_fs_usage_online *src,
			struct disk_reservation *disk_res,
			unsigned journal_seq)
{
	struct bch_fs_usage *dst = fs_usage_ptr(c, journal_seq, false);
	s64 added = src->u.data + src->u.reserved;
	s64 should_not_have_added;
	int ret = 0;

	percpu_rwsem_assert_held(&c->mark_lock);

	/*
	 * Not allowed to reduce sectors_available except by getting a
	 * reservation:
	 */
	should_not_have_added = added - (s64) (disk_res ? disk_res->sectors : 0);
	if (WARN_ONCE(should_not_have_added > 0,
		      "disk usage increased by %lli more than reservation of %llu",
		      added, disk_res ? disk_res->sectors : 0)) {
		atomic64_sub(should_not_have_added, &c->sectors_available);
		added -= should_not_have_added;
		ret = -1;
	}

	if (added > 0) {
		disk_res->sectors	-= added;
		src->online_reserved	-= added;
	}

	this_cpu_add(*c->online_reserved, src->online_reserved);

	preempt_disable();
	acc_u64s((u64 *) dst, (u64 *) &src->u, fs_usage_u64s(c));
	preempt_enable();

	return ret;
}

static inline void account_bucket(struct bch_fs_usage *fs_usage,
				  struct bch_dev_usage *dev_usage,
				  enum bch_data_type type,
				  int nr, s64 size)
{
	if (type == BCH_DATA_SB || type == BCH_DATA_JOURNAL)
		fs_usage->hidden	+= size;

	dev_usage->buckets[type]	+= nr;
}

static void bch2_dev_usage_update(struct bch_fs *c, struct bch_dev *ca,
				  struct bch_fs_usage *fs_usage,
				  struct bucket_mark old, struct bucket_mark new,
				  bool gc)
{
	struct bch_dev_usage *dev_usage;

	percpu_rwsem_assert_held(&c->mark_lock);

	preempt_disable();
	dev_usage = this_cpu_ptr(ca->usage[gc]);

	if (bucket_type(old))
		account_bucket(fs_usage, dev_usage, bucket_type(old),
			       -1, -ca->mi.bucket_size);

	if (bucket_type(new))
		account_bucket(fs_usage, dev_usage, bucket_type(new),
			       1, ca->mi.bucket_size);

	dev_usage->buckets_ec += (int) new.stripe - (int) old.stripe;
	dev_usage->buckets_unavailable +=
		is_unavailable_bucket(new) - is_unavailable_bucket(old);

	dev_usage->sectors[old.data_type] -= old.dirty_sectors;
	dev_usage->sectors[new.data_type] += new.dirty_sectors;
	dev_usage->sectors[BCH_DATA_CACHED] +=
		(int) new.cached_sectors - (int) old.cached_sectors;
	dev_usage->sectors_fragmented +=
		is_fragmented_bucket(new, ca) - is_fragmented_bucket(old, ca);
	preempt_enable();

	if (!is_available_bucket(old) && is_available_bucket(new))
		bch2_wake_allocator(ca);
}

void bch2_dev_usage_from_buckets(struct bch_fs *c)
{
	struct bch_dev *ca;
	struct bucket_mark old = { .v.counter = 0 };
	struct bucket_array *buckets;
	struct bucket *g;
	unsigned i;
	int cpu;

	c->usage_base->hidden = 0;

	for_each_member_device(ca, c, i) {
		for_each_possible_cpu(cpu)
			memset(per_cpu_ptr(ca->usage[0], cpu), 0,
			       sizeof(*ca->usage[0]));

		buckets = bucket_array(ca);

		for_each_bucket(g, buckets)
			bch2_dev_usage_update(c, ca, c->usage_base,
					      old, g->mark, false);
	}
}

static inline void update_replicas(struct bch_fs *c,
				   struct bch_fs_usage *fs_usage,
				   struct bch_replicas_entry *r,
				   s64 sectors)
{
	int idx = bch2_replicas_entry_idx(c, r);

	BUG_ON(idx < 0);

	switch (r->data_type) {
	case BCH_DATA_BTREE:
		fs_usage->btree		+= sectors;
		break;
	case BCH_DATA_USER:
		fs_usage->data		+= sectors;
		break;
	case BCH_DATA_CACHED:
		fs_usage->cached	+= sectors;
		break;
	}
	fs_usage->replicas[idx]		+= sectors;
}

static inline void update_cached_sectors(struct bch_fs *c,
					 struct bch_fs_usage *fs_usage,
					 unsigned dev, s64 sectors)
{
	struct bch_replicas_padded r;

	bch2_replicas_entry_cached(&r.e, dev);

	update_replicas(c, fs_usage, &r.e, sectors);
}

static struct replicas_delta_list *
replicas_deltas_realloc(struct btree_trans *trans, unsigned more)
{
	struct replicas_delta_list *d = trans->fs_usage_deltas;
	unsigned new_size = d ? (d->size + more) * 2 : 128;

	if (!d || d->used + more > d->size) {
		d = krealloc(d, sizeof(*d) + new_size, GFP_NOIO|__GFP_ZERO);
		BUG_ON(!d);

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
	d->used += b;
}

static inline void update_cached_sectors_list(struct btree_trans *trans,
					      unsigned dev, s64 sectors)
{
	struct bch_replicas_padded r;

	bch2_replicas_entry_cached(&r.e, dev);

	update_replicas_list(trans, &r.e, sectors);
}

void bch2_replicas_delta_list_apply(struct bch_fs *c,
				    struct bch_fs_usage *fs_usage,
				    struct replicas_delta_list *r)
{
	struct replicas_delta *d = r->d;
	struct replicas_delta *top = (void *) r->d + r->used;

	acc_u64s((u64 *) fs_usage,
		 (u64 *) &r->fs_usage, sizeof(*fs_usage) / sizeof(u64));

	while (d != top) {
		BUG_ON((void *) d > (void *) top);

		update_replicas(c, fs_usage, &d->r, d->delta);

		d = (void *) d + replicas_entry_bytes(&d->r) + 8;
	}
}

#define do_mark_fn(fn, c, pos, flags, ...)				\
({									\
	int gc, ret = 0;						\
									\
	percpu_rwsem_assert_held(&c->mark_lock);			\
									\
	for (gc = 0; gc < 2 && !ret; gc++)				\
		if (!gc == !(flags & BCH_BUCKET_MARK_GC) ||		\
		    (gc && gc_visited(c, pos)))				\
			ret = fn(c, __VA_ARGS__, gc);			\
	ret;								\
})

static int __bch2_invalidate_bucket(struct bch_fs *c, struct bch_dev *ca,
				    size_t b, struct bucket_mark *ret,
				    bool gc)
{
	struct bch_fs_usage *fs_usage = fs_usage_ptr(c, 0, gc);
	struct bucket *g = __bucket(ca, b, gc);
	struct bucket_mark old, new;

	old = bucket_cmpxchg(g, new, ({
		BUG_ON(!is_available_bucket(new));

		new.owned_by_allocator	= true;
		new.data_type		= 0;
		new.cached_sectors	= 0;
		new.dirty_sectors	= 0;
		new.gen++;
	}));

	bch2_dev_usage_update(c, ca, fs_usage, old, new, gc);

	if (old.cached_sectors)
		update_cached_sectors(c, fs_usage, ca->dev_idx,
				      -((s64) old.cached_sectors));

	if (!gc)
		*ret = old;
	return 0;
}

void bch2_invalidate_bucket(struct bch_fs *c, struct bch_dev *ca,
			    size_t b, struct bucket_mark *old)
{
	do_mark_fn(__bch2_invalidate_bucket, c, gc_phase(GC_PHASE_START), 0,
		   ca, b, old);

	if (!old->owned_by_allocator && old->cached_sectors)
		trace_invalidate(ca, bucket_to_sector(ca, b),
				 old->cached_sectors);
}

static int __bch2_mark_alloc_bucket(struct bch_fs *c, struct bch_dev *ca,
				    size_t b, bool owned_by_allocator,
				    bool gc)
{
	struct bch_fs_usage *fs_usage = fs_usage_ptr(c, 0, gc);
	struct bucket *g = __bucket(ca, b, gc);
	struct bucket_mark old, new;

	old = bucket_cmpxchg(g, new, ({
		new.owned_by_allocator	= owned_by_allocator;
	}));

	bch2_dev_usage_update(c, ca, fs_usage, old, new, gc);

	BUG_ON(!gc &&
	       !owned_by_allocator && !old.owned_by_allocator);

	return 0;
}

void bch2_mark_alloc_bucket(struct bch_fs *c, struct bch_dev *ca,
			    size_t b, bool owned_by_allocator,
			    struct gc_pos pos, unsigned flags)
{
	preempt_disable();

	do_mark_fn(__bch2_mark_alloc_bucket, c, pos, flags,
		   ca, b, owned_by_allocator);

	preempt_enable();
}

static int bch2_mark_alloc(struct bch_fs *c, struct bkey_s_c k,
			   struct bch_fs_usage *fs_usage,
			   u64 journal_seq, unsigned flags)
{
	bool gc = flags & BCH_BUCKET_MARK_GC;
	struct bkey_alloc_unpacked u;
	struct bch_dev *ca;
	struct bucket *g;
	struct bucket_mark old, m;

	/*
	 * alloc btree is read in by bch2_alloc_read, not gc:
	 */
	if ((flags & BCH_BUCKET_MARK_GC) &&
	    !(flags & BCH_BUCKET_MARK_BUCKET_INVALIDATE))
		return 0;

	ca = bch_dev_bkey_exists(c, k.k->p.inode);

	if (k.k->p.offset >= ca->mi.nbuckets)
		return 0;

	g = __bucket(ca, k.k->p.offset, gc);
	u = bch2_alloc_unpack(k);

	old = bucket_cmpxchg(g, m, ({
		m.gen			= u.gen;
		m.data_type		= u.data_type;
		m.dirty_sectors		= u.dirty_sectors;
		m.cached_sectors	= u.cached_sectors;

		if (journal_seq) {
			m.journal_seq_valid	= 1;
			m.journal_seq		= journal_seq;
		}
	}));

	if (!(flags & BCH_BUCKET_MARK_ALLOC_READ))
		bch2_dev_usage_update(c, ca, fs_usage, old, m, gc);

	g->io_time[READ]	= u.read_time;
	g->io_time[WRITE]	= u.write_time;
	g->oldest_gen		= u.oldest_gen;
	g->gen_valid		= 1;

	/*
	 * need to know if we're getting called from the invalidate path or
	 * not:
	 */

	if ((flags & BCH_BUCKET_MARK_BUCKET_INVALIDATE) &&
	    old.cached_sectors) {
		update_cached_sectors(c, fs_usage, ca->dev_idx,
				      -old.cached_sectors);
		trace_invalidate(ca, bucket_to_sector(ca, k.k->p.offset),
				 old.cached_sectors);
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
				       size_t b, enum bch_data_type type,
				       unsigned sectors, bool gc)
{
	struct bucket *g = __bucket(ca, b, gc);
	struct bucket_mark old, new;
	bool overflow;

	BUG_ON(type != BCH_DATA_SB &&
	       type != BCH_DATA_JOURNAL);

	old = bucket_cmpxchg(g, new, ({
		new.data_type	= type;
		overflow = checked_add(new.dirty_sectors, sectors);
	}));

	bch2_fs_inconsistent_on(old.data_type &&
				old.data_type != type, c,
		"different types of data in same bucket: %s, %s",
		bch2_data_types[old.data_type],
		bch2_data_types[type]);

	bch2_fs_inconsistent_on(overflow, c,
		"bucket sector count overflow: %u + %u > U16_MAX",
		old.dirty_sectors, sectors);

	if (c)
		bch2_dev_usage_update(c, ca, fs_usage_ptr(c, 0, gc),
				      old, new, gc);

	return 0;
}

void bch2_mark_metadata_bucket(struct bch_fs *c, struct bch_dev *ca,
			       size_t b, enum bch_data_type type,
			       unsigned sectors, struct gc_pos pos,
			       unsigned flags)
{
	BUG_ON(type != BCH_DATA_SB &&
	       type != BCH_DATA_JOURNAL);

	preempt_disable();

	if (likely(c)) {
		do_mark_fn(__bch2_mark_metadata_bucket, c, pos, flags,
			   ca, b, type, sectors);
	} else {
		__bch2_mark_metadata_bucket(c, ca, b, type, sectors, 0);
	}

	preempt_enable();
}

static s64 ptr_disk_sectors_delta(struct extent_ptr_decoded p,
				  unsigned offset, s64 delta,
				  unsigned flags)
{
	if (flags & BCH_BUCKET_MARK_OVERWRITE_SPLIT) {
		BUG_ON(offset + -delta > p.crc.live_size);

		return -((s64) ptr_disk_sectors(p)) +
			__ptr_disk_sectors(p, offset) +
			__ptr_disk_sectors(p, p.crc.live_size -
					   offset + delta);
	} else if (flags & BCH_BUCKET_MARK_OVERWRITE) {
		BUG_ON(offset + -delta > p.crc.live_size);

		return -((s64) ptr_disk_sectors(p)) +
			__ptr_disk_sectors(p, p.crc.live_size +
					   delta);
	} else {
		return ptr_disk_sectors(p);
	}
}

static void bucket_set_stripe(struct bch_fs *c,
			      const struct bch_stripe *v,
			      struct bch_fs_usage *fs_usage,
			      u64 journal_seq,
			      unsigned flags)
{
	bool enabled = !(flags & BCH_BUCKET_MARK_OVERWRITE);
	bool gc = flags & BCH_BUCKET_MARK_GC;
	unsigned i;

	for (i = 0; i < v->nr_blocks; i++) {
		const struct bch_extent_ptr *ptr = v->ptrs + i;
		struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
		struct bucket *g = PTR_BUCKET(ca, ptr, gc);
		struct bucket_mark new, old;

		old = bucket_cmpxchg(g, new, ({
			new.stripe			= enabled;
			if (journal_seq) {
				new.journal_seq_valid	= 1;
				new.journal_seq		= journal_seq;
			}
		}));

		bch2_dev_usage_update(c, ca, fs_usage, old, new, gc);

		/*
		 * XXX write repair code for these, flag stripe as possibly bad
		 */
		if (old.gen != ptr->gen)
			bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
				      "stripe with stale pointer");
#if 0
		/*
		 * We'd like to check for these, but these checks don't work
		 * yet:
		 */
		if (old.stripe && enabled)
			bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
				      "multiple stripes using same bucket");

		if (!old.stripe && !enabled)
			bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
				      "deleting stripe but bucket not marked as stripe bucket");
#endif
	}
}

static bool bch2_mark_pointer(struct bch_fs *c,
			      struct extent_ptr_decoded p,
			      s64 sectors, enum bch_data_type data_type,
			      struct bch_fs_usage *fs_usage,
			      u64 journal_seq, unsigned flags)
{
	bool gc = flags & BCH_BUCKET_MARK_GC;
	struct bucket_mark old, new;
	struct bch_dev *ca = bch_dev_bkey_exists(c, p.ptr.dev);
	struct bucket *g = PTR_BUCKET(ca, &p.ptr, gc);
	bool overflow;
	u64 v;

	v = atomic64_read(&g->_mark.v);
	do {
		new.v.counter = old.v.counter = v;

		/*
		 * Check this after reading bucket mark to guard against
		 * the allocator invalidating a bucket after we've already
		 * checked the gen
		 */
		if (gen_after(p.ptr.gen, new.gen)) {
			bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
				      "pointer gen in the future");
			return true;
		}

		if (new.gen != p.ptr.gen) {
			/* XXX write repair code for this */
			if (!p.ptr.cached &&
			    test_bit(JOURNAL_REPLAY_DONE, &c->journal.flags))
				bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
					      "stale dirty pointer");
			return true;
		}

		if (!p.ptr.cached)
			overflow = checked_add(new.dirty_sectors, sectors);
		else
			overflow = checked_add(new.cached_sectors, sectors);

		if (!new.dirty_sectors &&
		    !new.cached_sectors) {
			new.data_type	= 0;

			if (journal_seq) {
				new.journal_seq_valid = 1;
				new.journal_seq = journal_seq;
			}
		} else {
			new.data_type = data_type;
		}

		if (flags & BCH_BUCKET_MARK_NOATOMIC) {
			g->_mark = new;
			break;
		}
	} while ((v = atomic64_cmpxchg(&g->_mark.v,
			      old.v.counter,
			      new.v.counter)) != old.v.counter);

	if (old.data_type && old.data_type != data_type)
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u different types of data in same bucket: %s, %s",
			p.ptr.dev, PTR_BUCKET_NR(ca, &p.ptr),
			new.gen,
			bch2_data_types[old.data_type],
			bch2_data_types[data_type]);

	bch2_fs_inconsistent_on(overflow, c,
		"bucket sector count overflow: %u + %lli > U16_MAX",
		!p.ptr.cached
		? old.dirty_sectors
		: old.cached_sectors, sectors);

	bch2_dev_usage_update(c, ca, fs_usage, old, new, gc);

	BUG_ON(!gc && bucket_became_unavailable(old, new));

	return false;
}

static int bch2_mark_stripe_ptr(struct bch_fs *c,
				struct bch_extent_stripe_ptr p,
				enum bch_data_type data_type,
				struct bch_fs_usage *fs_usage,
				s64 sectors, unsigned flags)
{
	bool gc = flags & BCH_BUCKET_MARK_GC;
	struct stripe *m;
	unsigned old, new, nr_data;
	int blocks_nonempty_delta;
	s64 parity_sectors;

	BUG_ON(!sectors);

	m = genradix_ptr(&c->stripes[gc], p.idx);

	spin_lock(&c->ec_stripes_heap_lock);

	if (!m || !m->alive) {
		spin_unlock(&c->ec_stripes_heap_lock);
		bch_err_ratelimited(c, "pointer to nonexistent stripe %llu",
				    (u64) p.idx);
		return -EIO;
	}

	BUG_ON(m->r.e.data_type != data_type);

	nr_data = m->nr_blocks - m->nr_redundant;

	parity_sectors = DIV_ROUND_UP(abs(sectors) * m->nr_redundant, nr_data);

	if (sectors < 0)
		parity_sectors = -parity_sectors;
	sectors += parity_sectors;

	old = m->block_sectors[p.block];
	m->block_sectors[p.block] += sectors;
	new = m->block_sectors[p.block];

	blocks_nonempty_delta = (int) !!new - (int) !!old;
	if (blocks_nonempty_delta) {
		m->blocks_nonempty += blocks_nonempty_delta;

		if (!gc)
			bch2_stripes_heap_update(c, m, p.idx);
	}

	m->dirty = true;

	spin_unlock(&c->ec_stripes_heap_lock);

	update_replicas(c, fs_usage, &m->r.e, sectors);

	return 0;
}

static int bch2_mark_extent(struct bch_fs *c, struct bkey_s_c k,
			    unsigned offset, s64 sectors,
			    enum bch_data_type data_type,
			    struct bch_fs_usage *fs_usage,
			    unsigned journal_seq, unsigned flags)
{
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct bch_replicas_padded r;
	s64 dirty_sectors = 0;
	unsigned i;
	int ret;

	r.e.data_type	= data_type;
	r.e.nr_devs	= 0;
	r.e.nr_required	= 1;

	BUG_ON(!sectors);

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		s64 disk_sectors = data_type == BCH_DATA_BTREE
			? sectors
			: ptr_disk_sectors_delta(p, offset, sectors, flags);
		bool stale = bch2_mark_pointer(c, p, disk_sectors, data_type,
					fs_usage, journal_seq, flags);

		if (p.ptr.cached) {
			if (!stale)
				update_cached_sectors(c, fs_usage, p.ptr.dev,
						      disk_sectors);
		} else if (!p.ec_nr) {
			dirty_sectors	       += disk_sectors;
			r.e.devs[r.e.nr_devs++]	= p.ptr.dev;
		} else {
			for (i = 0; i < p.ec_nr; i++) {
				ret = bch2_mark_stripe_ptr(c, p.ec[i],
						data_type, fs_usage,
						disk_sectors, flags);
				if (ret)
					return ret;
			}

			r.e.nr_required = 0;
		}
	}

	update_replicas(c, fs_usage, &r.e, dirty_sectors);

	return 0;
}

static int bch2_mark_stripe(struct bch_fs *c, struct bkey_s_c k,
			    struct bch_fs_usage *fs_usage,
			    u64 journal_seq, unsigned flags)
{
	bool gc = flags & BCH_BUCKET_MARK_GC;
	struct bkey_s_c_stripe s = bkey_s_c_to_stripe(k);
	size_t idx = s.k->p.offset;
	struct stripe *m = genradix_ptr(&c->stripes[gc], idx);
	unsigned i;

	spin_lock(&c->ec_stripes_heap_lock);

	if (!m || ((flags & BCH_BUCKET_MARK_OVERWRITE) && !m->alive)) {
		spin_unlock(&c->ec_stripes_heap_lock);
		bch_err_ratelimited(c, "error marking nonexistent stripe %zu",
				    idx);
		return -1;
	}

	if (!(flags & BCH_BUCKET_MARK_OVERWRITE)) {
		m->sectors	= le16_to_cpu(s.v->sectors);
		m->algorithm	= s.v->algorithm;
		m->nr_blocks	= s.v->nr_blocks;
		m->nr_redundant	= s.v->nr_redundant;

		bch2_bkey_to_replicas(&m->r.e, k);

		/*
		 * XXX: account for stripes somehow here
		 */
#if 0
		update_replicas(c, fs_usage, &m->r.e, stripe_sectors);
#endif

		/* gc recalculates these fields: */
		if (!(flags & BCH_BUCKET_MARK_GC)) {
			for (i = 0; i < s.v->nr_blocks; i++) {
				m->block_sectors[i] =
					stripe_blockcount_get(s.v, i);
				m->blocks_nonempty += !!m->block_sectors[i];
			}
		}

		if (!gc)
			bch2_stripes_heap_update(c, m, idx);
		m->alive	= true;
	} else {
		if (!gc)
			bch2_stripes_heap_del(c, m, idx);
		memset(m, 0, sizeof(*m));
	}

	spin_unlock(&c->ec_stripes_heap_lock);

	bucket_set_stripe(c, s.v, fs_usage, 0, flags);
	return 0;
}

int bch2_mark_key_locked(struct bch_fs *c,
		   struct bkey_s_c k,
		   unsigned offset, s64 sectors,
		   struct bch_fs_usage *fs_usage,
		   u64 journal_seq, unsigned flags)
{
	int ret = 0;

	preempt_disable();

	if (!fs_usage || (flags & BCH_BUCKET_MARK_GC))
		fs_usage = fs_usage_ptr(c, journal_seq,
					flags & BCH_BUCKET_MARK_GC);

	switch (k.k->type) {
	case KEY_TYPE_alloc:
		ret = bch2_mark_alloc(c, k, fs_usage, journal_seq, flags);
		break;
	case KEY_TYPE_btree_ptr:
		sectors = !(flags & BCH_BUCKET_MARK_OVERWRITE)
			?  c->opts.btree_node_size
			: -c->opts.btree_node_size;

		ret = bch2_mark_extent(c, k, offset, sectors, BCH_DATA_BTREE,
				fs_usage, journal_seq, flags);
		break;
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		ret = bch2_mark_extent(c, k, offset, sectors, BCH_DATA_USER,
				fs_usage, journal_seq, flags);
		break;
	case KEY_TYPE_stripe:
		ret = bch2_mark_stripe(c, k, fs_usage, journal_seq, flags);
		break;
	case KEY_TYPE_inode:
		if (!(flags & BCH_BUCKET_MARK_OVERWRITE))
			fs_usage->nr_inodes++;
		else
			fs_usage->nr_inodes--;
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
	}

	preempt_enable();

	return ret;
}

int bch2_mark_key(struct bch_fs *c, struct bkey_s_c k,
		  unsigned offset, s64 sectors,
		  struct bch_fs_usage *fs_usage,
		  u64 journal_seq, unsigned flags)
{
	int ret;

	percpu_down_read(&c->mark_lock);
	ret = bch2_mark_key_locked(c, k, offset, sectors,
				   fs_usage, journal_seq, flags);
	percpu_up_read(&c->mark_lock);

	return ret;
}

inline int bch2_mark_overwrite(struct btree_trans *trans,
			       struct btree_iter *iter,
			       struct bkey_s_c old,
			       struct bkey_i *new,
			       struct bch_fs_usage *fs_usage,
			       unsigned flags)
{
	struct bch_fs		*c = trans->c;
	struct btree		*b = iter->l[0].b;
	unsigned		offset = 0;
	s64			sectors = 0;

	flags |= BCH_BUCKET_MARK_OVERWRITE;

	if (btree_node_is_extents(b)
	    ? bkey_cmp(new->k.p, bkey_start_pos(old.k)) <= 0
	    : bkey_cmp(new->k.p, old.k->p))
		return 0;

	if (btree_node_is_extents(b)) {
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
			flags |= BCH_BUCKET_MARK_OVERWRITE_SPLIT;
			break;
		}

		BUG_ON(sectors >= 0);
	}

	return bch2_mark_key_locked(c, old, offset, sectors, fs_usage,
				    trans->journal_res.seq, flags) ?: 1;
}

int bch2_mark_update(struct btree_trans *trans,
		     struct btree_insert_entry *insert,
		     struct bch_fs_usage *fs_usage,
		     unsigned flags)
{
	struct bch_fs		*c = trans->c;
	struct btree_iter	*iter = insert->iter;
	struct btree		*b = iter->l[0].b;
	struct btree_node_iter	node_iter = iter->l[0].iter;
	struct bkey_packed	*_k;
	int ret = 0;

	if (!btree_node_type_needs_gc(iter->btree_id))
		return 0;

	bch2_mark_key_locked(c, bkey_i_to_s_c(insert->k),
		0, insert->k->k.size,
		fs_usage, trans->journal_res.seq,
		BCH_BUCKET_MARK_INSERT|flags);

	if (unlikely(trans->flags & BTREE_INSERT_NOMARK_OVERWRITES))
		return 0;

	/*
	 * For non extents, we only mark the new key, not the key being
	 * overwritten - unless we're actually deleting:
	 */
	if ((iter->btree_id == BTREE_ID_ALLOC ||
	     iter->btree_id == BTREE_ID_EC) &&
	    !bkey_deleted(&insert->k->k))
		return 0;

	while ((_k = bch2_btree_node_iter_peek_filter(&node_iter, b,
						      KEY_TYPE_discard))) {
		struct bkey		unpacked;
		struct bkey_s_c		k = bkey_disassemble(b, _k, &unpacked);

		ret = bch2_mark_overwrite(trans, iter, k, insert->k,
					  fs_usage, flags);
		if (ret <= 0)
			break;

		bch2_btree_node_iter_advance(&node_iter, b);
	}

	return ret;
}

void bch2_trans_fs_usage_apply(struct btree_trans *trans,
			       struct bch_fs_usage_online *fs_usage)
{
	struct bch_fs *c = trans->c;
	struct btree_insert_entry *i;
	static int warned_disk_usage = 0;
	u64 disk_res_sectors = trans->disk_res ? trans->disk_res->sectors : 0;
	char buf[200];

	if (!bch2_fs_usage_apply(c, fs_usage, trans->disk_res,
				 trans->journal_res.seq) ||
	    warned_disk_usage ||
	    xchg(&warned_disk_usage, 1))
		return;

	bch_err(c, "disk usage increased more than %llu sectors reserved",
		disk_res_sectors);

	trans_for_each_update_iter(trans, i) {
		struct btree_iter	*iter = i->iter;
		struct btree		*b = iter->l[0].b;
		struct btree_node_iter	node_iter = iter->l[0].iter;
		struct bkey_packed	*_k;

		pr_err("while inserting");
		bch2_bkey_val_to_text(&PBUF(buf), c, bkey_i_to_s_c(i->k));
		pr_err("%s", buf);
		pr_err("overlapping with");

		node_iter = iter->l[0].iter;
		while ((_k = bch2_btree_node_iter_peek_filter(&node_iter, b,
							KEY_TYPE_discard))) {
			struct bkey		unpacked;
			struct bkey_s_c		k;

			k = bkey_disassemble(b, _k, &unpacked);

			if (btree_node_is_extents(b)
			    ? bkey_cmp(i->k->k.p, bkey_start_pos(k.k)) <= 0
			    : bkey_cmp(i->k->k.p, k.k->p))
				break;

			bch2_bkey_val_to_text(&PBUF(buf), c, k);
			pr_err("%s", buf);

			bch2_btree_node_iter_advance(&node_iter, b);
		}
	}
}

/* trans_mark: */

static int trans_get_key(struct btree_trans *trans,
			 enum btree_id btree_id, struct bpos pos,
			 struct btree_iter **iter,
			 struct bkey_s_c *k)
{
	struct btree_insert_entry *i;
	int ret;

	trans_for_each_update_iter(trans, i)
		if (i->iter->btree_id == btree_id &&
		    (btree_node_type_is_extents(btree_id)
		     ? bkey_cmp(pos, bkey_start_pos(&i->k->k)) >= 0 &&
		       bkey_cmp(pos, i->k->k.p) < 0
		     : !bkey_cmp(pos, i->iter->pos))) {
			*iter	= i->iter;
			*k	= bkey_i_to_s_c(i->k);
			return 1;
		}

	*iter = __bch2_trans_get_iter(trans, btree_id, pos,
				   BTREE_ITER_SLOTS|BTREE_ITER_INTENT, 0);
	if (IS_ERR(*iter))
		return PTR_ERR(*iter);

	bch2_trans_iter_free_on_commit(trans, *iter);

	*k = bch2_btree_iter_peek_slot(*iter);
	ret = bkey_err(*k);
	if (ret)
		bch2_trans_iter_put(trans, *iter);
	return ret;
}

static void *trans_update_key(struct btree_trans *trans,
			      struct btree_iter *iter,
			      unsigned u64s)
{
	struct btree_insert_entry *i;
	struct bkey_i *new_k;

	new_k = bch2_trans_kmalloc(trans, u64s * sizeof(u64));
	if (IS_ERR(new_k))
		return new_k;

	bkey_init(&new_k->k);
	new_k->k.p = iter->pos;

	trans_for_each_update_iter(trans, i)
		if (i->iter == iter) {
			i->k = new_k;
			return new_k;
		}

	bch2_trans_update(trans, BTREE_INSERT_ENTRY(iter, new_k));
	return new_k;
}

static int bch2_trans_mark_pointer(struct btree_trans *trans,
			struct extent_ptr_decoded p,
			s64 sectors, enum bch_data_type data_type)
{
	struct bch_fs *c = trans->c;
	struct bch_dev *ca = bch_dev_bkey_exists(c, p.ptr.dev);
	struct btree_iter *iter;
	struct bkey_s_c k;
	struct bkey_alloc_unpacked u;
	struct bkey_i_alloc *a;
	unsigned old;
	bool overflow;
	int ret;

	ret = trans_get_key(trans, BTREE_ID_ALLOC,
			    POS(p.ptr.dev, PTR_BUCKET_NR(ca, &p.ptr)),
			    &iter, &k);
	if (ret < 0)
		return ret;

	if (!ret) {
		/*
		 * During journal replay, and if gc repairs alloc info at
		 * runtime, the alloc info in the btree might not be up to date
		 * yet - so, trust the in memory mark:
		 */
		struct bucket *g;
		struct bucket_mark m;

		percpu_down_read(&c->mark_lock);
		g	= bucket(ca, iter->pos.offset);
		m	= READ_ONCE(g->mark);
		u	= alloc_mem_to_key(g, m);
		percpu_up_read(&c->mark_lock);
	} else {
		/*
		 * Unless we're already updating that key:
		 */
		if (k.k->type != KEY_TYPE_alloc) {
			bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
				      "pointer to nonexistent bucket %llu:%llu",
				      iter->pos.inode, iter->pos.offset);
			ret = -1;
			goto out;
		}

		u = bch2_alloc_unpack(k);
	}

	if (gen_after(u.gen, p.ptr.gen)) {
		ret = 1;
		goto out;
	}

	if (u.data_type && u.data_type != data_type) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %llu:%llu gen %u different types of data in same bucket: %s, %s",
			iter->pos.inode, iter->pos.offset,
			u.gen,
			bch2_data_types[u.data_type],
			bch2_data_types[data_type]);
		ret = -1;
		goto out;
	}

	if (!p.ptr.cached) {
		old = u.dirty_sectors;
		overflow = checked_add(u.dirty_sectors, sectors);
	} else {
		old = u.cached_sectors;
		overflow = checked_add(u.cached_sectors, sectors);
	}

	u.data_type = u.dirty_sectors || u.cached_sectors
		? data_type : 0;

	bch2_fs_inconsistent_on(overflow, c,
		"bucket sector count overflow: %u + %lli > U16_MAX",
		old, sectors);

	a = trans_update_key(trans, iter, BKEY_ALLOC_U64s_MAX);
	ret = PTR_ERR_OR_ZERO(a);
	if (ret)
		goto out;

	bkey_alloc_init(&a->k_i);
	a->k.p = iter->pos;
	bch2_alloc_pack(a, u);
out:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int bch2_trans_mark_stripe_ptr(struct btree_trans *trans,
			struct bch_extent_stripe_ptr p,
			s64 sectors, enum bch_data_type data_type)
{
	struct bch_fs *c = trans->c;
	struct bch_replicas_padded r;
	struct btree_iter *iter;
	struct bkey_i *new_k;
	struct bkey_s_c k;
	struct bkey_s_stripe s;
	unsigned nr_data;
	s64 parity_sectors;
	int ret = 0;

	ret = trans_get_key(trans, BTREE_ID_EC, POS(0, p.idx), &iter, &k);
	if (ret < 0)
		return ret;

	if (k.k->type != KEY_TYPE_stripe) {
		bch2_fs_inconsistent(c,
			"pointer to nonexistent stripe %llu",
			(u64) p.idx);
		ret = -EIO;
		goto out;
	}

	new_k = trans_update_key(trans, iter, k.k->u64s);
	ret = PTR_ERR_OR_ZERO(new_k);
	if (ret)
		goto out;

	bkey_reassemble(new_k, k);
	s = bkey_i_to_s_stripe(new_k);

	nr_data = s.v->nr_blocks - s.v->nr_redundant;

	parity_sectors = DIV_ROUND_UP(abs(sectors) * s.v->nr_redundant, nr_data);

	if (sectors < 0)
		parity_sectors = -parity_sectors;

	stripe_blockcount_set(s.v, p.block,
		stripe_blockcount_get(s.v, p.block) +
		sectors + parity_sectors);

	bch2_bkey_to_replicas(&r.e, s.s_c);

	update_replicas_list(trans, &r.e, sectors);
out:
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
	unsigned i;
	int ret;

	r.e.data_type	= data_type;
	r.e.nr_devs	= 0;
	r.e.nr_required	= 1;

	BUG_ON(!sectors);

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		s64 disk_sectors = data_type == BCH_DATA_BTREE
			? sectors
			: ptr_disk_sectors_delta(p, offset, sectors, flags);

		ret = bch2_trans_mark_pointer(trans, p, disk_sectors,
					      data_type);
		if (ret < 0)
			return ret;

		stale = ret > 0;

		if (p.ptr.cached) {
			if (!stale)
				update_cached_sectors_list(trans, p.ptr.dev,
							   disk_sectors);
		} else if (!p.ec_nr) {
			dirty_sectors	       += disk_sectors;
			r.e.devs[r.e.nr_devs++]	= p.ptr.dev;
		} else {
			for (i = 0; i < p.ec_nr; i++) {
				ret = bch2_trans_mark_stripe_ptr(trans, p.ec[i],
						disk_sectors, data_type);
				if (ret)
					return ret;
			}

			r.e.nr_required = 0;
		}
	}

	update_replicas_list(trans, &r.e, dirty_sectors);

	return 0;
}

static int __bch2_trans_mark_reflink_p(struct btree_trans *trans,
			struct bkey_s_c_reflink_p p,
			u64 idx, unsigned sectors,
			unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_iter *iter;
	struct bkey_i *new_k;
	struct bkey_s_c k;
	struct bkey_i_reflink_v *r_v;
	s64 ret;

	ret = trans_get_key(trans, BTREE_ID_REFLINK,
			    POS(0, idx), &iter, &k);
	if (ret < 0)
		return ret;

	if (k.k->type != KEY_TYPE_reflink_v) {
		bch2_fs_inconsistent(c,
			"%llu:%llu len %u points to nonexistent indirect extent %llu",
			p.k->p.inode, p.k->p.offset, p.k->size, idx);
		ret = -EIO;
		goto err;
	}

	if ((flags & BCH_BUCKET_MARK_OVERWRITE) &&
	    (bkey_start_offset(k.k) < idx ||
	     k.k->p.offset > idx + sectors))
		goto out;

	bch2_btree_iter_set_pos(iter, bkey_start_pos(k.k));
	BUG_ON(iter->uptodate > BTREE_ITER_NEED_PEEK);

	new_k = trans_update_key(trans, iter, k.k->u64s);
	ret = PTR_ERR_OR_ZERO(new_k);
	if (ret)
		goto err;

	bkey_reassemble(new_k, k);
	r_v = bkey_i_to_reflink_v(new_k);

	le64_add_cpu(&r_v->v.refcount,
		     !(flags & BCH_BUCKET_MARK_OVERWRITE) ? 1 : -1);

	if (!r_v->v.refcount) {
		r_v->k.type = KEY_TYPE_deleted;
		set_bkey_val_u64s(&r_v->k, 0);
	}
out:
	ret = k.k->p.offset - idx;
err:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

static int bch2_trans_mark_reflink_p(struct btree_trans *trans,
			struct bkey_s_c_reflink_p p, unsigned offset,
			s64 sectors, unsigned flags)
{
	u64 idx = le64_to_cpu(p.v->idx) + offset;
	s64 ret = 0;

	sectors = abs(sectors);
	BUG_ON(offset + sectors > p.k->size);

	while (sectors) {
		ret = __bch2_trans_mark_reflink_p(trans, p, idx, sectors, flags);
		if (ret < 0)
			break;

		idx += ret;
		sectors = max_t(s64, 0LL, sectors - ret);
		ret = 0;
	}

	return ret;
}

int bch2_trans_mark_key(struct btree_trans *trans, struct bkey_s_c k,
			unsigned offset, s64 sectors, unsigned flags)
{
	struct replicas_delta_list *d;
	struct bch_fs *c = trans->c;

	switch (k.k->type) {
	case KEY_TYPE_btree_ptr:
		sectors = !(flags & BCH_BUCKET_MARK_OVERWRITE)
			?  c->opts.btree_node_size
			: -c->opts.btree_node_size;

		return bch2_trans_mark_extent(trans, k, offset, sectors,
					      flags, BCH_DATA_BTREE);
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		return bch2_trans_mark_extent(trans, k, offset, sectors,
					      flags, BCH_DATA_USER);
	case KEY_TYPE_inode:
		d = replicas_deltas_realloc(trans, 0);

		if (!(flags & BCH_BUCKET_MARK_OVERWRITE))
			d->fs_usage.nr_inodes++;
		else
			d->fs_usage.nr_inodes--;
		return 0;
	case KEY_TYPE_reservation: {
		unsigned replicas = bkey_s_c_to_reservation(k).v->nr_replicas;

		d = replicas_deltas_realloc(trans, 0);

		sectors *= replicas;
		replicas = clamp_t(unsigned, replicas, 1,
				   ARRAY_SIZE(d->fs_usage.persistent_reserved));

		d->fs_usage.reserved				+= sectors;
		d->fs_usage.persistent_reserved[replicas - 1]	+= sectors;
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
			   struct bkey_i *insert)
{
	struct btree		*b = iter->l[0].b;
	struct btree_node_iter	node_iter = iter->l[0].iter;
	struct bkey_packed	*_k;
	int ret;

	if (!btree_node_type_needs_gc(iter->btree_id))
		return 0;

	ret = bch2_trans_mark_key(trans, bkey_i_to_s_c(insert),
			0, insert->k.size, BCH_BUCKET_MARK_INSERT);
	if (ret)
		return ret;

	if (unlikely(trans->flags & BTREE_INSERT_NOMARK_OVERWRITES))
		return 0;

	while ((_k = bch2_btree_node_iter_peek_filter(&node_iter, b,
						      KEY_TYPE_discard))) {
		struct bkey		unpacked;
		struct bkey_s_c		k;
		unsigned		offset = 0;
		s64			sectors = 0;
		unsigned		flags = BCH_BUCKET_MARK_OVERWRITE;

		k = bkey_disassemble(b, _k, &unpacked);

		if (btree_node_is_extents(b)
		    ? bkey_cmp(insert->k.p, bkey_start_pos(k.k)) <= 0
		    : bkey_cmp(insert->k.p, k.k->p))
			break;

		if (btree_node_is_extents(b)) {
			switch (bch2_extent_overlap(&insert->k, k.k)) {
			case BCH_EXTENT_OVERLAP_ALL:
				offset = 0;
				sectors = -((s64) k.k->size);
				break;
			case BCH_EXTENT_OVERLAP_BACK:
				offset = bkey_start_offset(&insert->k) -
					bkey_start_offset(k.k);
				sectors = bkey_start_offset(&insert->k) -
					k.k->p.offset;
				break;
			case BCH_EXTENT_OVERLAP_FRONT:
				offset = 0;
				sectors = bkey_start_offset(k.k) -
					insert->k.p.offset;
				break;
			case BCH_EXTENT_OVERLAP_MIDDLE:
				offset = bkey_start_offset(&insert->k) -
					bkey_start_offset(k.k);
				sectors = -((s64) insert->k.size);
				flags |= BCH_BUCKET_MARK_OVERWRITE_SPLIT;
				break;
			}

			BUG_ON(sectors >= 0);
		}

		ret = bch2_trans_mark_key(trans, k, offset, sectors, flags);
		if (ret)
			return ret;

		bch2_btree_node_iter_advance(&node_iter, b);
	}

	return 0;
}

/* Disk reservations: */

#define SECTORS_CACHE	1024

int bch2_disk_reservation_add(struct bch_fs *c, struct disk_reservation *res,
			      unsigned sectors, int flags)
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
	copygc_heap	copygc_heap;

	size_t btree_reserve	= DIV_ROUND_UP(BTREE_NODE_RESERVE,
			     ca->mi.bucket_size / c->opts.btree_node_size);
	/* XXX: these should be tunable */
	size_t reserve_none	= max_t(size_t, 1, nbuckets >> 9);
	size_t copygc_reserve	= max_t(size_t, 2, nbuckets >> 7);
	size_t free_inc_nr	= max(max_t(size_t, 1, nbuckets >> 12),
				      btree_reserve * 2);
	bool resize = ca->buckets[0] != NULL,
	     start_copygc = ca->copygc_thread != NULL;
	int ret = -ENOMEM;
	unsigned i;

	memset(&free,		0, sizeof(free));
	memset(&free_inc,	0, sizeof(free_inc));
	memset(&alloc_heap,	0, sizeof(alloc_heap));
	memset(&copygc_heap,	0, sizeof(copygc_heap));

	if (!(buckets		= kvpmalloc(sizeof(struct bucket_array) +
					    nbuckets * sizeof(struct bucket),
					    GFP_KERNEL|__GFP_ZERO)) ||
	    !(buckets_nouse	= kvpmalloc(BITS_TO_LONGS(nbuckets) *
					    sizeof(unsigned long),
					    GFP_KERNEL|__GFP_ZERO)) ||
	    !init_fifo(&free[RESERVE_BTREE], btree_reserve, GFP_KERNEL) ||
	    !init_fifo(&free[RESERVE_MOVINGGC],
		       copygc_reserve, GFP_KERNEL) ||
	    !init_fifo(&free[RESERVE_NONE], reserve_none, GFP_KERNEL) ||
	    !init_fifo(&free_inc,	free_inc_nr, GFP_KERNEL) ||
	    !init_heap(&alloc_heap,	ALLOC_SCAN_BATCH(ca) << 1, GFP_KERNEL) ||
	    !init_heap(&copygc_heap,	copygc_reserve, GFP_KERNEL))
		goto err;

	buckets->first_bucket	= ca->mi.first_bucket;
	buckets->nbuckets	= nbuckets;

	bch2_copygc_stop(ca);

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

	if (resize)
		percpu_up_write(&c->mark_lock);

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

	/* and we shut down copygc: */
	swap(ca->copygc_heap, copygc_heap);

	nbuckets = ca->mi.nbuckets;

	if (resize) {
		up_write(&ca->bucket_lock);
		up_write(&c->gc_lock);
	}

	if (start_copygc &&
	    bch2_copygc_start(c, ca))
		bch_err(ca, "error restarting copygc thread");

	ret = 0;
err:
	free_heap(&copygc_heap);
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

	free_heap(&ca->copygc_heap);
	free_heap(&ca->alloc_heap);
	free_fifo(&ca->free_inc);
	for (i = 0; i < RESERVE_NR; i++)
		free_fifo(&ca->free[i]);
	kvpfree(ca->buckets_nouse,
		BITS_TO_LONGS(ca->mi.nbuckets) * sizeof(unsigned long));
	kvpfree(rcu_dereference_protected(ca->buckets[0], 1),
		sizeof(struct bucket_array) +
		ca->mi.nbuckets * sizeof(struct bucket));

	free_percpu(ca->usage[0]);
}

int bch2_dev_buckets_alloc(struct bch_fs *c, struct bch_dev *ca)
{
	if (!(ca->usage[0] = alloc_percpu(struct bch_dev_usage)))
		return -ENOMEM;

	return bch2_dev_buckets_resize(c, ca, ca->mi.nbuckets);;
}
