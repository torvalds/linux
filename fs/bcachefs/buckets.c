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
	BUG_ON(!gc && !journal_seq);

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
	percpu_rwsem_assert_held(&c->mark_lock);
	BUG_ON(!gc && !journal_seq);

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
	return m.dirty_sectors
		? max(0, (int) ca->mi.bucket_size - (int) m.dirty_sectors)
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
				  struct bucket_mark old, struct bucket_mark new,
				  u64 journal_seq, bool gc)
{
	struct bch_fs_usage *fs_usage;
	struct bch_dev_usage *u;

	preempt_disable();
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

static inline int __update_replicas(struct bch_fs *c,
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

static inline int update_replicas(struct bch_fs *c, struct bkey_s_c k,
			struct bch_replicas_entry *r, s64 sectors,
			unsigned journal_seq, bool gc)
{
	struct bch_fs_usage __percpu *fs_usage;
	int idx, ret = 0;
	char buf[200];

	percpu_down_read(&c->mark_lock);

	idx = bch2_replicas_entry_idx(c, r);
	if (idx < 0 &&
	    (test_bit(BCH_FS_REBUILD_REPLICAS, &c->flags) ||
	     fsck_err(c, "no replicas entry\n"
		      "  while marking %s",
		      (bch2_bkey_val_to_text(&PBUF(buf), c, k), buf)))) {
		percpu_up_read(&c->mark_lock);
		ret = bch2_mark_replicas(c, r);
		if (ret)
			return ret;

		percpu_down_read(&c->mark_lock);
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
	return ret;
}

static inline int update_cached_sectors(struct bch_fs *c,
			struct bkey_s_c k,
			unsigned dev, s64 sectors,
			unsigned journal_seq, bool gc)
{
	struct bch_replicas_padded r;

	bch2_replicas_entry_cached(&r.e, dev);

	return update_replicas(c, k, &r.e, sectors, journal_seq, gc);
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

static int bch2_mark_alloc(struct btree_trans *trans,
			   struct bkey_s_c old, struct bkey_s_c new,
			   unsigned flags)
{
	bool gc = flags & BTREE_TRIGGER_GC;
	u64 journal_seq = trans->journal_res.seq;
	struct bch_fs *c = trans->c;
	struct bkey_alloc_unpacked old_u = bch2_alloc_unpack(old);
	struct bkey_alloc_unpacked new_u = bch2_alloc_unpack(new);
	struct bch_dev *ca;
	struct bucket *g;
	struct bucket_mark old_m, m;
	int ret = 0;

	/*
	 * alloc btree is read in by bch2_alloc_read, not gc:
	 */
	if ((flags & BTREE_TRIGGER_GC) &&
	    !(flags & BTREE_TRIGGER_BUCKET_INVALIDATE))
		return 0;

	if ((flags & BTREE_TRIGGER_INSERT) &&
	    !old_u.data_type != !new_u.data_type &&
	    new.k->type == KEY_TYPE_alloc_v3) {
		struct bch_alloc_v3 *v = (struct bch_alloc_v3 *) new.v;
		u64 old_journal_seq = le64_to_cpu(v->journal_seq);

		BUG_ON(!journal_seq);

		/*
		 * If the btree updates referring to a bucket weren't flushed
		 * before the bucket became empty again, then the we don't have
		 * to wait on a journal flush before we can reuse the bucket:
		 */
		new_u.journal_seq = !new_u.data_type &&
			(journal_seq == old_journal_seq ||
			 bch2_journal_noflush_seq(&c->journal, old_journal_seq))
			? 0 : journal_seq;
		v->journal_seq = cpu_to_le64(new_u.journal_seq);
	}

	if (old_u.data_type && !new_u.data_type && new_u.journal_seq) {
		ret = bch2_set_bucket_needs_journal_commit(&c->buckets_waiting_for_journal,
				c->journal.flushed_seq_ondisk,
				new_u.dev, new_u.bucket,
				new_u.journal_seq);
		if (ret) {
			bch2_fs_fatal_error(c,
				"error setting bucket_needs_journal_commit: %i", ret);
			return ret;
		}
	}

	ca = bch_dev_bkey_exists(c, new_u.dev);

	if (new_u.bucket >= ca->mi.nbuckets)
		return 0;

	percpu_down_read(&c->mark_lock);
	if (!gc && new_u.gen != old_u.gen)
		*bucket_gen(ca, new_u.bucket) = new_u.gen;

	g = __bucket(ca, new_u.bucket, gc);

	old_m = bucket_cmpxchg(g, m, ({
		m.gen			= new_u.gen;
		m.data_type		= new_u.data_type;
		m.dirty_sectors		= new_u.dirty_sectors;
		m.cached_sectors	= new_u.cached_sectors;
		m.stripe		= new_u.stripe != 0;
	}));

	bch2_dev_usage_update(c, ca, old_m, m, journal_seq, gc);

	g->io_time[READ]	= new_u.read_time;
	g->io_time[WRITE]	= new_u.write_time;
	g->oldest_gen		= new_u.oldest_gen;
	g->gen_valid		= 1;
	g->stripe		= new_u.stripe;
	g->stripe_redundancy	= new_u.stripe_redundancy;
	percpu_up_read(&c->mark_lock);

	/*
	 * need to know if we're getting called from the invalidate path or
	 * not:
	 */

	if ((flags & BTREE_TRIGGER_BUCKET_INVALIDATE) &&
	    old_m.cached_sectors) {
		ret = update_cached_sectors(c, new, ca->dev_idx,
					    -old_m.cached_sectors,
					    journal_seq, gc);
		if (ret) {
			bch2_fs_fatal_error(c, "bch2_mark_alloc(): no replicas entry while updating cached sectors");
			return ret;
		}

		trace_invalidate(ca, bucket_to_sector(ca, new_u.bucket),
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

void bch2_mark_metadata_bucket(struct bch_fs *c, struct bch_dev *ca,
			       size_t b, enum bch_data_type data_type,
			       unsigned sectors, struct gc_pos pos,
			       unsigned flags)
{
	struct bucket *g;
	struct bucket_mark old, new;
	bool overflow;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));
	BUG_ON(data_type != BCH_DATA_sb &&
	       data_type != BCH_DATA_journal);

	/*
	 * Backup superblock might be past the end of our normal usable space:
	 */
	if (b >= ca->mi.nbuckets)
		return;

	percpu_down_read(&c->mark_lock);
	g = gc_bucket(ca, b);
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

	bch2_dev_usage_update(c, ca, old, new, 0, true);
	percpu_up_read(&c->mark_lock);
}

static s64 ptr_disk_sectors(s64 sectors, struct extent_ptr_decoded p)
{
	EBUG_ON(sectors < 0);

	return p.crc.compression_type &&
		p.crc.compression_type != BCH_COMPRESSION_TYPE_incompressible
		? DIV_ROUND_UP_ULL(sectors * p.crc.compressed_size,
			       p.crc.uncompressed_size)
		: sectors;
}

static int check_bucket_ref(struct bch_fs *c,
			    struct bkey_s_c k,
			    const struct bch_extent_ptr *ptr,
			    s64 sectors, enum bch_data_type ptr_data_type,
			    u8 b_gen, u8 bucket_data_type,
			    u16 dirty_sectors, u16 cached_sectors)
{
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
	size_t bucket_nr = PTR_BUCKET_NR(ca, ptr);
	u16 bucket_sectors = !ptr->cached
		? dirty_sectors
		: cached_sectors;
	char buf[200];

	if (gen_after(ptr->gen, b_gen)) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u data type %s: ptr gen %u newer than bucket gen\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	if (gen_cmp(b_gen, ptr->gen) > BUCKET_GC_GEN_MAX) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u data type %s: ptr gen %u too stale\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	if (b_gen != ptr->gen && !ptr->cached) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u (mem gen %u) data type %s: stale dirty ptr (gen %u)\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			*bucket_gen(ca, bucket_nr),
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			ptr->gen,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	if (b_gen != ptr->gen)
		return 1;

	if (bucket_data_type && ptr_data_type &&
	    bucket_data_type != ptr_data_type) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u different types of data in same bucket: %s, %s\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_types[bucket_data_type],
			bch2_data_types[ptr_data_type],
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	if ((unsigned) (bucket_sectors + sectors) > U16_MAX) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %u:%zu gen %u data type %s sector count overflow: %u + %lli > U16_MAX\n"
			"while marking %s",
			ptr->dev, bucket_nr, b_gen,
			bch2_data_types[bucket_data_type ?: ptr_data_type],
			bucket_sectors, sectors,
			(bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		return -EIO;
	}

	return 0;
}

static int mark_stripe_bucket(struct btree_trans *trans,
			      struct bkey_s_c k,
			      unsigned ptr_idx,
			      unsigned flags)
{
	struct bch_fs *c = trans->c;
	u64 journal_seq = trans->journal_res.seq;
	const struct bch_stripe *s = bkey_s_c_to_stripe(k).v;
	unsigned nr_data = s->nr_blocks - s->nr_redundant;
	bool parity = ptr_idx >= nr_data;
	enum bch_data_type data_type = parity ? BCH_DATA_parity : 0;
	s64 sectors = parity ? le16_to_cpu(s->sectors) : 0;
	const struct bch_extent_ptr *ptr = s->ptrs + ptr_idx;
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
	struct bucket *g;
	struct bucket_mark new, old;
	char buf[200];
	int ret = 0;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));

	/* * XXX doesn't handle deletion */

	percpu_down_read(&c->mark_lock);
	g = PTR_GC_BUCKET(ca, ptr);

	if (g->mark.dirty_sectors ||
	    (g->stripe && g->stripe != k.k->p.offset)) {
		bch2_fs_inconsistent(c,
			      "bucket %u:%zu gen %u: multiple stripes using same bucket\n%s",
			      ptr->dev, PTR_BUCKET_NR(ca, ptr), g->mark.gen,
			      (bch2_bkey_val_to_text(&PBUF(buf), c, k), buf));
		ret = -EINVAL;
		goto err;
	}

	old = bucket_cmpxchg(g, new, ({
		ret = check_bucket_ref(c, k, ptr, sectors, data_type,
				       new.gen, new.data_type,
				       new.dirty_sectors, new.cached_sectors);
		if (ret)
			goto err;

		new.dirty_sectors += sectors;
		if (data_type)
			new.data_type		= data_type;

		new.stripe = true;
	}));

	g->stripe		= k.k->p.offset;
	g->stripe_redundancy	= s->nr_redundant;

	bch2_dev_usage_update(c, ca, old, new, journal_seq, true);
err:
	percpu_up_read(&c->mark_lock);

	return 0;
}

static int __mark_pointer(struct btree_trans *trans,
			  struct bkey_s_c k,
			  const struct bch_extent_ptr *ptr,
			  s64 sectors, enum bch_data_type ptr_data_type,
			  u8 bucket_gen, u8 *bucket_data_type,
			  u16 *dirty_sectors, u16 *cached_sectors)
{
	u16 *dst_sectors = !ptr->cached
		? dirty_sectors
		: cached_sectors;
	int ret = check_bucket_ref(trans->c, k, ptr, sectors, ptr_data_type,
				   bucket_gen, *bucket_data_type,
				   *dirty_sectors, *cached_sectors);

	if (ret)
		return ret;

	*dst_sectors += sectors;
	*bucket_data_type = *dirty_sectors || *cached_sectors
		? ptr_data_type : 0;
	return 0;
}

static int bch2_mark_pointer(struct btree_trans *trans,
			     struct bkey_s_c k,
			     struct extent_ptr_decoded p,
			     s64 sectors, enum bch_data_type data_type,
			     unsigned flags)
{
	u64 journal_seq = trans->journal_res.seq;
	struct bch_fs *c = trans->c;
	struct bucket_mark old, new;
	struct bch_dev *ca = bch_dev_bkey_exists(c, p.ptr.dev);
	struct bucket *g;
	u8 bucket_data_type;
	u64 v;
	int ret = 0;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));

	percpu_down_read(&c->mark_lock);
	g = PTR_GC_BUCKET(ca, &p.ptr);

	v = atomic64_read(&g->_mark.v);
	do {
		new.v.counter = old.v.counter = v;
		bucket_data_type = new.data_type;

		ret = __mark_pointer(trans, k, &p.ptr, sectors,
				     data_type, new.gen,
				     &bucket_data_type,
				     &new.dirty_sectors,
				     &new.cached_sectors);
		if (ret)
			goto err;

		new.data_type = bucket_data_type;

		if (flags & BTREE_TRIGGER_NOATOMIC) {
			g->_mark = new;
			break;
		}
	} while ((v = atomic64_cmpxchg(&g->_mark.v,
			      old.v.counter,
			      new.v.counter)) != old.v.counter);

	bch2_dev_usage_update(c, ca, old, new, journal_seq, true);
err:
	percpu_up_read(&c->mark_lock);

	return ret;
}

static int bch2_mark_stripe_ptr(struct btree_trans *trans,
				struct bkey_s_c k,
				struct bch_extent_stripe_ptr p,
				enum bch_data_type data_type,
				s64 sectors,
				unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bch_replicas_padded r;
	struct gc_stripe *m;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));

	m = genradix_ptr_alloc(&c->gc_stripes, p.idx, GFP_KERNEL);
	if (!m) {
		bch_err(c, "error allocating memory for gc_stripes, idx %llu",
			(u64) p.idx);
		return -ENOMEM;
	}

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
	spin_unlock(&c->ec_stripes_heap_lock);

	r.e.data_type = data_type;
	update_replicas(c, k, &r.e, sectors, trans->journal_res.seq, true);

	return 0;
}

static int bch2_mark_extent(struct btree_trans *trans,
			    struct bkey_s_c old, struct bkey_s_c new,
			    unsigned flags)
{
	u64 journal_seq = trans->journal_res.seq;
	struct bch_fs *c = trans->c;
	struct bkey_s_c k = flags & BTREE_TRIGGER_OVERWRITE ? old: new;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct bch_replicas_padded r;
	enum bch_data_type data_type = bkey_is_btree_ptr(k.k)
		? BCH_DATA_btree
		: BCH_DATA_user;
	s64 sectors = bkey_is_btree_ptr(k.k)
		? btree_sectors(c)
		: k.k->size;
	s64 dirty_sectors = 0;
	bool stale;
	int ret;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));

	r.e.data_type	= data_type;
	r.e.nr_devs	= 0;
	r.e.nr_required	= 1;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		s64 disk_sectors = ptr_disk_sectors(sectors, p);

		if (flags & BTREE_TRIGGER_OVERWRITE)
			disk_sectors = -disk_sectors;

		ret = bch2_mark_pointer(trans, k, p, disk_sectors,
					data_type, flags);
		if (ret < 0)
			return ret;

		stale = ret > 0;

		if (p.ptr.cached) {
			if (!stale) {
				ret = update_cached_sectors(c, k, p.ptr.dev,
						disk_sectors, journal_seq, true);
				if (ret) {
					bch2_fs_fatal_error(c, "bch2_mark_extent(): no replicas entry while updating cached sectors");
					return ret;
				}
			}
		} else if (!p.has_ec) {
			dirty_sectors	       += disk_sectors;
			r.e.devs[r.e.nr_devs++]	= p.ptr.dev;
		} else {
			ret = bch2_mark_stripe_ptr(trans, k, p.ec, data_type,
					disk_sectors, flags);
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
		ret = update_replicas(c, k, &r.e, dirty_sectors, journal_seq, true);
		if (ret) {
			char buf[200];

			bch2_bkey_val_to_text(&PBUF(buf), c, k);
			bch2_fs_fatal_error(c, "no replicas entry for %s", buf);
			return ret;
		}
	}

	return 0;
}

static int bch2_mark_stripe(struct btree_trans *trans,
			    struct bkey_s_c old, struct bkey_s_c new,
			    unsigned flags)
{
	bool gc = flags & BTREE_TRIGGER_GC;
	u64 journal_seq = trans->journal_res.seq;
	struct bch_fs *c = trans->c;
	u64 idx = new.k->p.offset;
	const struct bch_stripe *old_s = old.k->type == KEY_TYPE_stripe
		? bkey_s_c_to_stripe(old).v : NULL;
	const struct bch_stripe *new_s = new.k->type == KEY_TYPE_stripe
		? bkey_s_c_to_stripe(new).v : NULL;
	unsigned i;
	int ret;

	BUG_ON(gc && old_s);

	if (!gc) {
		struct stripe *m = genradix_ptr(&c->stripes, idx);

		if (!m || (old_s && !m->alive)) {
			char buf1[200], buf2[200];

			bch2_bkey_val_to_text(&PBUF(buf1), c, old);
			bch2_bkey_val_to_text(&PBUF(buf2), c, new);
			bch_err_ratelimited(c, "error marking nonexistent stripe %llu while marking\n"
					    "old %s\n"
					    "new %s", idx, buf1, buf2);
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

			for (i = 0; i < new_s->nr_blocks; i++)
				m->blocks_nonempty += !!stripe_blockcount_get(new_s, i);

			spin_lock(&c->ec_stripes_heap_lock);
			bch2_stripes_heap_update(c, m, idx);
			spin_unlock(&c->ec_stripes_heap_lock);
		}
	} else {
		struct gc_stripe *m =
			genradix_ptr_alloc(&c->gc_stripes, idx, GFP_KERNEL);

		if (!m) {
			bch_err(c, "error allocating memory for gc_stripes, idx %llu",
				idx);
			return -ENOMEM;
		}
		/*
		 * This will be wrong when we bring back runtime gc: we should
		 * be unmarking the old key and then marking the new key
		 */
		m->alive	= true;
		m->sectors	= le16_to_cpu(new_s->sectors);
		m->nr_blocks	= new_s->nr_blocks;
		m->nr_redundant	= new_s->nr_redundant;

		for (i = 0; i < new_s->nr_blocks; i++)
			m->ptrs[i] = new_s->ptrs[i];

		bch2_bkey_to_replicas(&m->r.e, new);

		/*
		 * gc recalculates this field from stripe ptr
		 * references:
		 */
		memset(m->block_sectors, 0, sizeof(m->block_sectors));

		for (i = 0; i < new_s->nr_blocks; i++) {
			ret = mark_stripe_bucket(trans, new, i, flags);
			if (ret)
				return ret;
		}

		ret = update_replicas(c, new, &m->r.e,
				      ((s64) m->sectors * m->nr_redundant),
				      journal_seq, gc);
		if (ret) {
			char buf[200];

			bch2_bkey_val_to_text(&PBUF(buf), c, new);
			bch2_fs_fatal_error(c, "no replicas entry for %s", buf);
			return ret;
		}
	}

	return 0;
}

static int bch2_mark_inode(struct btree_trans *trans,
			   struct bkey_s_c old, struct bkey_s_c new,
			   unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bch_fs_usage __percpu *fs_usage;
	u64 journal_seq = trans->journal_res.seq;

	if (flags & BTREE_TRIGGER_INSERT) {
		struct bch_inode_v2 *v = (struct bch_inode_v2 *) new.v;

		BUG_ON(!journal_seq);
		BUG_ON(new.k->type != KEY_TYPE_inode_v2);

		v->bi_journal_seq = cpu_to_le64(journal_seq);
	}

	if (flags & BTREE_TRIGGER_GC) {
		percpu_down_read(&c->mark_lock);
		preempt_disable();

		fs_usage = fs_usage_ptr(c, journal_seq, flags & BTREE_TRIGGER_GC);
		fs_usage->nr_inodes += bkey_is_inode(new.k);
		fs_usage->nr_inodes -= bkey_is_inode(old.k);

		preempt_enable();
		percpu_up_read(&c->mark_lock);
	}
	return 0;
}

static int bch2_mark_reservation(struct btree_trans *trans,
				 struct bkey_s_c old, struct bkey_s_c new,
				 unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k = flags & BTREE_TRIGGER_OVERWRITE ? old: new;
	struct bch_fs_usage __percpu *fs_usage;
	unsigned replicas = bkey_s_c_to_reservation(k).v->nr_replicas;
	s64 sectors = (s64) k.k->size;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));

	if (flags & BTREE_TRIGGER_OVERWRITE)
		sectors = -sectors;
	sectors *= replicas;

	percpu_down_read(&c->mark_lock);
	preempt_disable();

	fs_usage = fs_usage_ptr(c, trans->journal_res.seq, flags & BTREE_TRIGGER_GC);
	replicas = clamp_t(unsigned, replicas, 1,
			   ARRAY_SIZE(fs_usage->persistent_reserved));

	fs_usage->reserved				+= sectors;
	fs_usage->persistent_reserved[replicas - 1]	+= sectors;

	preempt_enable();
	percpu_up_read(&c->mark_lock);

	return 0;
}

static s64 __bch2_mark_reflink_p(struct bch_fs *c, struct bkey_s_c_reflink_p p,
				 u64 *idx, unsigned flags, size_t r_idx)
{
	struct reflink_gc *r;
	int add = !(flags & BTREE_TRIGGER_OVERWRITE) ? 1 : -1;
	s64 ret = 0;

	if (r_idx >= c->reflink_gc_nr)
		goto not_found;

	r = genradix_ptr(&c->reflink_gc_table, r_idx);
	if (*idx < r->offset - r->size)
		goto not_found;

	BUG_ON((s64) r->refcount + add < 0);

	r->refcount += add;
	*idx = r->offset;
	return 0;
not_found:
	*idx = U64_MAX;
	ret = -EIO;

	/*
	 * XXX: we're replacing the entire reflink pointer with an error
	 * key, we should just be replacing the part that was missing:
	 */
	if (fsck_err(c, "%llu:%llu len %u points to nonexistent indirect extent %llu",
		     p.k->p.inode, p.k->p.offset, p.k->size, *idx)) {
		struct bkey_i_error new;

		bkey_init(&new.k);
		new.k.type	= KEY_TYPE_error;
		new.k.p		= p.k->p;
		new.k.size	= p.k->size;
		ret = bch2_journal_key_insert(c, BTREE_ID_extents, 0, &new.k_i);
	}
fsck_err:
	return ret;
}

static int bch2_mark_reflink_p(struct btree_trans *trans,
			       struct bkey_s_c old, struct bkey_s_c new,
			       unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k = flags & BTREE_TRIGGER_OVERWRITE ? old: new;
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);
	struct reflink_gc *ref;
	size_t l, r, m;
	u64 idx = le64_to_cpu(p.v->idx);
	u64 end = le64_to_cpu(p.v->idx) + p.k->size;
	int ret = 0;

	BUG_ON(!(flags & BTREE_TRIGGER_GC));

	if (c->sb.version >= bcachefs_metadata_version_reflink_p_fix) {
		idx -= le32_to_cpu(p.v->front_pad);
		end += le32_to_cpu(p.v->back_pad);
	}

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

	while (idx < end && !ret)
		ret = __bch2_mark_reflink_p(c, p, &idx, flags, l++);

	return ret;
}

int bch2_mark_key(struct btree_trans *trans,
		  struct bkey_s_c old,
		  struct bkey_s_c new,
		  unsigned flags)
{
	struct bkey_s_c k = flags & BTREE_TRIGGER_OVERWRITE ? old: new;

	switch (k.k->type) {
	case KEY_TYPE_alloc:
	case KEY_TYPE_alloc_v2:
	case KEY_TYPE_alloc_v3:
		return bch2_mark_alloc(trans, old, new, flags);
	case KEY_TYPE_btree_ptr:
	case KEY_TYPE_btree_ptr_v2:
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		return bch2_mark_extent(trans, old, new, flags);
	case KEY_TYPE_stripe:
		return bch2_mark_stripe(trans, old, new, flags);
	case KEY_TYPE_inode:
	case KEY_TYPE_inode_v2:
		return bch2_mark_inode(trans, old, new, flags);
	case KEY_TYPE_reservation:
		return bch2_mark_reservation(trans, old, new, flags);
	case KEY_TYPE_reflink_p:
		return bch2_mark_reflink_p(trans, old, new, flags);
	case KEY_TYPE_snapshot:
		return bch2_mark_snapshot(trans, old, new, flags);
	default:
		return 0;
	}
}

int bch2_mark_update(struct btree_trans *trans, struct btree_path *path,
		     struct bkey_i *new, unsigned flags)
{
	struct bkey		_deleted = KEY(0, 0, 0);
	struct bkey_s_c		deleted = (struct bkey_s_c) { &_deleted, NULL };
	struct bkey_s_c		old;
	struct bkey		unpacked;
	int ret;

	_deleted.p = path->pos;

	if (unlikely(flags & BTREE_TRIGGER_NORUN))
		return 0;

	if (!btree_node_type_needs_gc(path->btree_id))
		return 0;

	old = bch2_btree_path_peek_slot(path, &unpacked);

	if (old.k->type == new->k.type &&
	    ((1U << old.k->type) & BTREE_TRIGGER_WANTS_OLD_AND_NEW)) {
		ret   = bch2_mark_key(trans, old, bkey_i_to_s_c(new),
				BTREE_TRIGGER_INSERT|BTREE_TRIGGER_OVERWRITE|flags);
	} else {
		ret   = bch2_mark_key(trans, deleted, bkey_i_to_s_c(new),
				BTREE_TRIGGER_INSERT|flags) ?:
			bch2_mark_key(trans, old, deleted,
				BTREE_TRIGGER_OVERWRITE|flags);
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
		struct bkey_s_c old = { &i->old_k, i->old_v };

		pr_err("while inserting");
		bch2_bkey_val_to_text(&PBUF(buf), c, bkey_i_to_s_c(i->k));
		pr_err("  %s", buf);
		pr_err("overlapping with");
		bch2_bkey_val_to_text(&PBUF(buf), c, old);
		pr_err("  %s", buf);
	}
	__WARN();
}

int bch2_trans_fs_usage_apply(struct btree_trans *trans,
			      struct replicas_delta_list *deltas)
{
	struct bch_fs *c = trans->c;
	static int warned_disk_usage = 0;
	bool warn = false;
	unsigned disk_res_sectors = trans->disk_res ? trans->disk_res->sectors : 0;
	struct replicas_delta *d = deltas->d, *d2;
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
		fs_usage_apply_warn(trans, disk_res_sectors, should_not_have_added);
	return 0;
need_mark:
	/* revert changes: */
	for (d2 = deltas->d; d2 != d; d2 = replicas_delta_next(d2))
		BUG_ON(__update_replicas(c, dst, &d2->r, -d2->delta));

	preempt_enable();
	percpu_up_read(&c->mark_lock);
	return -1;
}

/* trans_mark: */

static int bch2_trans_start_alloc_update(struct btree_trans *trans, struct btree_iter *iter,
			      const struct bch_extent_ptr *ptr,
			      struct bkey_alloc_unpacked *u)
{
	struct bch_fs *c = trans->c;
	struct bch_dev *ca = bch_dev_bkey_exists(c, ptr->dev);
	struct bkey_s_c k;
	int ret;

	bch2_trans_iter_init(trans, iter, BTREE_ID_alloc,
			     POS(ptr->dev, PTR_BUCKET_NR(ca, ptr)),
			     BTREE_ITER_WITH_UPDATES|
			     BTREE_ITER_CACHED|
			     BTREE_ITER_INTENT);
	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret) {
		bch2_trans_iter_exit(trans, iter);
		return ret;
	}

	*u = bch2_alloc_unpack(k);
	return 0;
}

static int bch2_trans_mark_pointer(struct btree_trans *trans,
			struct bkey_s_c k, struct extent_ptr_decoded p,
			s64 sectors, enum bch_data_type data_type)
{
	struct btree_iter iter;
	struct bkey_alloc_unpacked u;
	int ret;

	ret = bch2_trans_start_alloc_update(trans, &iter, &p.ptr, &u);
	if (ret)
		return ret;

	ret = __mark_pointer(trans, k, &p.ptr, sectors, data_type,
			     u.gen, &u.data_type,
			     &u.dirty_sectors, &u.cached_sectors);
	if (ret)
		goto out;

	ret = bch2_alloc_write(trans, &iter, &u, 0);
	if (ret)
		goto out;
out:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_trans_mark_stripe_ptr(struct btree_trans *trans,
			struct extent_ptr_decoded p,
			s64 sectors, enum bch_data_type data_type)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_i_stripe *s;
	struct bch_replicas_padded r;
	int ret = 0;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_stripes, POS(0, p.ec.idx),
			     BTREE_ITER_INTENT|
			     BTREE_ITER_WITH_UPDATES);
	k = bch2_btree_iter_peek_slot(&iter);
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

	ret = bch2_trans_update(trans, &iter, &s->k_i, 0);
	if (ret)
		goto err;

	bch2_bkey_to_replicas(&r.e, bkey_i_to_s_c(&s->k_i));
	r.e.data_type = data_type;
	update_replicas_list(trans, &r.e, sectors);
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_trans_mark_extent(struct btree_trans *trans,
			struct bkey_s_c k, unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_ptrs_c ptrs = bch2_bkey_ptrs_c(k);
	const union bch_extent_entry *entry;
	struct extent_ptr_decoded p;
	struct bch_replicas_padded r;
	enum bch_data_type data_type = bkey_is_btree_ptr(k.k)
		? BCH_DATA_btree
		: BCH_DATA_user;
	s64 sectors = bkey_is_btree_ptr(k.k)
		? btree_sectors(c)
		: k.k->size;
	s64 dirty_sectors = 0;
	bool stale;
	int ret;

	r.e.data_type	= data_type;
	r.e.nr_devs	= 0;
	r.e.nr_required	= 1;

	bkey_for_each_ptr_decode(k.k, ptrs, p, entry) {
		s64 disk_sectors = ptr_disk_sectors(sectors, p);

		if (flags & BTREE_TRIGGER_OVERWRITE)
			disk_sectors = -disk_sectors;

		ret = bch2_trans_mark_pointer(trans, k, p,
					disk_sectors, data_type);
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

static int bch2_trans_mark_stripe_bucket(struct btree_trans *trans,
					 struct bkey_s_c_stripe s,
					 unsigned idx, bool deleting)
{
	struct bch_fs *c = trans->c;
	const struct bch_extent_ptr *ptr = &s.v->ptrs[idx];
	struct btree_iter iter;
	struct bkey_alloc_unpacked u;
	enum bch_data_type data_type = idx >= s.v->nr_blocks - s.v->nr_redundant
		? BCH_DATA_parity : 0;
	s64 sectors = data_type ? le16_to_cpu(s.v->sectors) : 0;
	int ret = 0;

	if (deleting)
		sectors = -sectors;

	ret = bch2_trans_start_alloc_update(trans, &iter, ptr, &u);
	if (ret)
		return ret;

	ret = check_bucket_ref(c, s.s_c, ptr, sectors, data_type,
			       u.gen, u.data_type,
			       u.dirty_sectors, u.cached_sectors);
	if (ret)
		goto err;

	if (!deleting) {
		if (bch2_fs_inconsistent_on(u.stripe ||
					    u.stripe_redundancy, c,
				"bucket %llu:%llu gen %u data type %s dirty_sectors %u: multiple stripes using same bucket (%u, %llu)",
				iter.pos.inode, iter.pos.offset, u.gen,
				bch2_data_types[u.data_type],
				u.dirty_sectors,
				u.stripe, s.k->p.offset)) {
			ret = -EIO;
			goto err;
		}

		if (bch2_fs_inconsistent_on(data_type && u.dirty_sectors, c,
				"bucket %llu:%llu gen %u data type %s dirty_sectors %u: data already in stripe bucket %llu",
				iter.pos.inode, iter.pos.offset, u.gen,
				bch2_data_types[u.data_type],
				u.dirty_sectors,
				s.k->p.offset)) {
			ret = -EIO;
			goto err;
		}

		u.stripe		= s.k->p.offset;
		u.stripe_redundancy	= s.v->nr_redundant;
	} else {
		if (bch2_fs_inconsistent_on(u.stripe != s.k->p.offset ||
					    u.stripe_redundancy != s.v->nr_redundant, c,
				"bucket %llu:%llu gen %u: not marked as stripe when deleting stripe %llu (got %u)",
				iter.pos.inode, iter.pos.offset, u.gen,
				s.k->p.offset, u.stripe)) {
			ret = -EIO;
			goto err;
		}

		u.stripe		= 0;
		u.stripe_redundancy	= 0;
	}

	u.dirty_sectors += sectors;
	if (data_type)
		u.data_type = !deleting ? data_type : 0;

	ret = bch2_alloc_write(trans, &iter, &u, 0);
	if (ret)
		goto err;
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_trans_mark_stripe(struct btree_trans *trans,
				  struct bkey_s_c old, struct bkey_s_c new,
				  unsigned flags)
{
	struct bkey_s_c_stripe old_s = { .k = NULL };
	struct bkey_s_c_stripe new_s = { .k = NULL };
	struct bch_replicas_padded r;
	unsigned i, nr_blocks;
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

	BUG_ON(new_s.k && old_s.k &&
	       (new_s.v->nr_blocks	!= old_s.v->nr_blocks ||
		new_s.v->nr_redundant	!= old_s.v->nr_redundant));

	nr_blocks = new_s.k ? new_s.v->nr_blocks : old_s.v->nr_blocks;

	if (new_s.k) {
		s64 sectors = le16_to_cpu(new_s.v->sectors);

		bch2_bkey_to_replicas(&r.e, new);
		update_replicas_list(trans, &r.e, sectors * new_s.v->nr_redundant);
	}

	if (old_s.k) {
		s64 sectors = -((s64) le16_to_cpu(old_s.v->sectors));

		bch2_bkey_to_replicas(&r.e, old);
		update_replicas_list(trans, &r.e, sectors * old_s.v->nr_redundant);
	}

	for (i = 0; i < nr_blocks; i++) {
		if (new_s.k && old_s.k &&
		    !memcmp(&new_s.v->ptrs[i],
			    &old_s.v->ptrs[i],
			    sizeof(new_s.v->ptrs[i])))
			continue;

		if (new_s.k) {
			ret = bch2_trans_mark_stripe_bucket(trans, new_s, i, false);
			if (ret)
				break;
		}

		if (old_s.k) {
			ret = bch2_trans_mark_stripe_bucket(trans, old_s, i, true);
			if (ret)
				break;
		}
	}

	return ret;
}

static int bch2_trans_mark_inode(struct btree_trans *trans,
				 struct bkey_s_c old,
				 struct bkey_s_c new,
				 unsigned flags)
{
	int nr = bkey_is_inode(new.k) - bkey_is_inode(old.k);

	if (nr) {
		struct replicas_delta_list *d =
			replicas_deltas_realloc(trans, 0);
		d->nr_inodes += nr;
	}

	return 0;
}

static int bch2_trans_mark_reservation(struct btree_trans *trans,
				       struct bkey_s_c k, unsigned flags)
{
	unsigned replicas = bkey_s_c_to_reservation(k).v->nr_replicas;
	s64 sectors = (s64) k.k->size;
	struct replicas_delta_list *d;

	if (flags & BTREE_TRIGGER_OVERWRITE)
		sectors = -sectors;
	sectors *= replicas;

	d = replicas_deltas_realloc(trans, 0);

	replicas = clamp_t(unsigned, replicas, 1,
			   ARRAY_SIZE(d->persistent_reserved));

	d->persistent_reserved[replicas - 1] += sectors;
	return 0;
}

static int __bch2_trans_mark_reflink_p(struct btree_trans *trans,
			struct bkey_s_c_reflink_p p,
			u64 *idx, unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_s_c k;
	struct bkey_i *n;
	__le64 *refcount;
	int add = !(flags & BTREE_TRIGGER_OVERWRITE) ? 1 : -1;
	char buf[200];
	int ret;

	bch2_trans_iter_init(trans, &iter, BTREE_ID_reflink, POS(0, *idx),
			     BTREE_ITER_INTENT|
			     BTREE_ITER_WITH_UPDATES);
	k = bch2_btree_iter_peek_slot(&iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	n = bch2_trans_kmalloc(trans, bkey_bytes(k.k));
	ret = PTR_ERR_OR_ZERO(n);
	if (ret)
		goto err;

	bkey_reassemble(n, k);

	refcount = bkey_refcount(n);
	if (!refcount) {
		bch2_bkey_val_to_text(&PBUF(buf), c, p.s_c);
		bch2_fs_inconsistent(c,
			"nonexistent indirect extent at %llu while marking\n  %s",
			*idx, buf);
		ret = -EIO;
		goto err;
	}

	if (!*refcount && (flags & BTREE_TRIGGER_OVERWRITE)) {
		bch2_bkey_val_to_text(&PBUF(buf), c, p.s_c);
		bch2_fs_inconsistent(c,
			"indirect extent refcount underflow at %llu while marking\n  %s",
			*idx, buf);
		ret = -EIO;
		goto err;
	}

	if (flags & BTREE_TRIGGER_INSERT) {
		struct bch_reflink_p *v = (struct bch_reflink_p *) p.v;
		u64 pad;

		pad = max_t(s64, le32_to_cpu(v->front_pad),
			    le64_to_cpu(v->idx) - bkey_start_offset(k.k));
		BUG_ON(pad > U32_MAX);
		v->front_pad = cpu_to_le32(pad);

		pad = max_t(s64, le32_to_cpu(v->back_pad),
			    k.k->p.offset - p.k->size - le64_to_cpu(v->idx));
		BUG_ON(pad > U32_MAX);
		v->back_pad = cpu_to_le32(pad);
	}

	le64_add_cpu(refcount, add);

	if (!*refcount) {
		n->k.type = KEY_TYPE_deleted;
		set_bkey_val_u64s(&n->k, 0);
	}

	bch2_btree_iter_set_pos_to_extent_start(&iter);
	ret = bch2_trans_update(trans, &iter, n, 0);
	if (ret)
		goto err;

	*idx = k.k->p.offset;
err:
	bch2_trans_iter_exit(trans, &iter);
	return ret;
}

static int bch2_trans_mark_reflink_p(struct btree_trans *trans,
				     struct bkey_s_c k, unsigned flags)
{
	struct bkey_s_c_reflink_p p = bkey_s_c_to_reflink_p(k);
	u64 idx, end_idx;
	int ret = 0;

	if (flags & BTREE_TRIGGER_INSERT) {
		struct bch_reflink_p *v = (struct bch_reflink_p *) p.v;

		v->front_pad = v->back_pad = 0;
	}

	idx	= le64_to_cpu(p.v->idx) - le32_to_cpu(p.v->front_pad);
	end_idx = le64_to_cpu(p.v->idx) + p.k->size +
		le32_to_cpu(p.v->back_pad);

	while (idx < end_idx && !ret)
		ret = __bch2_trans_mark_reflink_p(trans, p, &idx, flags);

	return ret;
}

int bch2_trans_mark_key(struct btree_trans *trans, struct bkey_s_c old,
			struct bkey_s_c new, unsigned flags)
{
	struct bkey_s_c k = flags & BTREE_TRIGGER_OVERWRITE ? old: new;

	switch (k.k->type) {
	case KEY_TYPE_btree_ptr:
	case KEY_TYPE_btree_ptr_v2:
	case KEY_TYPE_extent:
	case KEY_TYPE_reflink_v:
		return bch2_trans_mark_extent(trans, k, flags);
	case KEY_TYPE_stripe:
		return bch2_trans_mark_stripe(trans, old, new, flags);
	case KEY_TYPE_inode:
	case KEY_TYPE_inode_v2:
		return bch2_trans_mark_inode(trans, old, new, flags);
	case KEY_TYPE_reservation:
		return bch2_trans_mark_reservation(trans, k, flags);
	case KEY_TYPE_reflink_p:
		return bch2_trans_mark_reflink_p(trans, k, flags);
	default:
		return 0;
	}
}

static int __bch2_trans_mark_metadata_bucket(struct btree_trans *trans,
				    struct bch_dev *ca, size_t b,
				    enum bch_data_type type,
				    unsigned sectors)
{
	struct bch_fs *c = trans->c;
	struct btree_iter iter;
	struct bkey_alloc_unpacked u;
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

	ret = bch2_trans_start_alloc_update(trans, &iter, &ptr, &u);
	if (ret)
		return ret;

	if (u.data_type && u.data_type != type) {
		bch2_fsck_err(c, FSCK_CAN_IGNORE|FSCK_NEED_FSCK,
			"bucket %llu:%llu gen %u different types of data in same bucket: %s, %s\n"
			"while marking %s",
			iter.pos.inode, iter.pos.offset, u.gen,
			bch2_data_types[u.data_type],
			bch2_data_types[type],
			bch2_data_types[type]);
		ret = -EIO;
		goto out;
	}

	u.data_type	= type;
	u.dirty_sectors	= sectors;

	ret = bch2_alloc_write(trans, &iter, &u, 0);
	if (ret)
		goto out;
out:
	bch2_trans_iter_exit(trans, &iter);
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
		sizeof(*buckets) +
		buckets->nbuckets * sizeof(struct bucket));
}

static void bucket_gens_free_rcu(struct rcu_head *rcu)
{
	struct bucket_gens *buckets =
		container_of(rcu, struct bucket_gens, rcu);

	kvpfree(buckets, sizeof(*buckets) + buckets->nbuckets);
}

int bch2_dev_buckets_resize(struct bch_fs *c, struct bch_dev *ca, u64 nbuckets)
{
	struct bucket_array *buckets = NULL, *old_buckets = NULL;
	struct bucket_gens *bucket_gens = NULL, *old_bucket_gens = NULL;
	unsigned long *buckets_nouse = NULL;
	alloc_fifo	free[RESERVE_NR];
	alloc_fifo	free_inc;
	alloc_heap	alloc_heap;

	size_t btree_reserve	= DIV_ROUND_UP(BTREE_NODE_RESERVE,
			     ca->mi.bucket_size / btree_sectors(c));
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
	    !(bucket_gens	= kvpmalloc(sizeof(struct bucket_gens) + nbuckets,
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
	bucket_gens->first_bucket = ca->mi.first_bucket;
	bucket_gens->nbuckets	= nbuckets;

	bch2_copygc_stop(c);

	if (resize) {
		down_write(&c->gc_lock);
		down_write(&ca->bucket_lock);
		percpu_down_write(&c->mark_lock);
	}

	old_buckets = bucket_array(ca);
	old_bucket_gens = rcu_dereference_protected(ca->bucket_gens, 1);

	if (resize) {
		size_t n = min(buckets->nbuckets, old_buckets->nbuckets);

		memcpy(buckets->b,
		       old_buckets->b,
		       n * sizeof(struct bucket));
		memcpy(bucket_gens->b,
		       old_bucket_gens->b,
		       n);
		memcpy(buckets_nouse,
		       ca->buckets_nouse,
		       BITS_TO_LONGS(n) * sizeof(unsigned long));
	}

	rcu_assign_pointer(ca->buckets[0], buckets);
	rcu_assign_pointer(ca->bucket_gens, bucket_gens);
	buckets		= old_buckets;
	bucket_gens	= old_bucket_gens;

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
	if (bucket_gens)
		call_rcu(&bucket_gens->rcu, bucket_gens_free_rcu);
	if (buckets)
		call_rcu(&buckets->rcu, buckets_free_rcu);

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
	kvpfree(rcu_dereference_protected(ca->bucket_gens, 1),
		sizeof(struct bucket_gens) + ca->mi.nbuckets);
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
