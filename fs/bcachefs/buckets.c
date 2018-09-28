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
#include "btree_gc.h"
#include "buckets.h"
#include "error.h"
#include "movinggc.h"
#include "trace.h"

#include <linux/preempt.h>

static inline u64 __bch2_fs_sectors_used(struct bch_fs *, struct bch_fs_usage);

#ifdef DEBUG_BUCKETS

#define lg_local_lock	lg_global_lock
#define lg_local_unlock	lg_global_unlock

static void bch2_fs_stats_verify(struct bch_fs *c)
{
	struct bch_fs_usage stats =
		__bch2_fs_usage_read(c);
	unsigned i, j;

	for (i = 0; i < ARRAY_SIZE(stats.replicas); i++) {
		for (j = 0; j < ARRAY_SIZE(stats.replicas[i].data); j++)
			if ((s64) stats.replicas[i].data[j] < 0)
				panic("replicas %u %s sectors underflow: %lli\n",
				      i + 1, bch_data_types[j],
				      stats.replicas[i].data[j]);

		if ((s64) stats.replicas[i].persistent_reserved < 0)
			panic("replicas %u reserved underflow: %lli\n",
			      i + 1, stats.replicas[i].persistent_reserved);
	}

	for (j = 0; j < ARRAY_SIZE(stats.buckets); j++)
		if ((s64) stats.replicas[i].data_buckets[j] < 0)
			panic("%s buckets underflow: %lli\n",
			      bch_data_types[j],
			      stats.buckets[j]);

	if ((s64) stats.online_reserved < 0)
		panic("sectors_online_reserved underflow: %lli\n",
		      stats.online_reserved);
}

static void bch2_dev_stats_verify(struct bch_dev *ca)
{
	struct bch_dev_usage stats =
		__bch2_dev_usage_read(ca);
	u64 n = ca->mi.nbuckets - ca->mi.first_bucket;
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(stats.buckets); i++)
		BUG_ON(stats.buckets[i]		> n);
	BUG_ON(stats.buckets_alloc		> n);
	BUG_ON(stats.buckets_unavailable	> n);
}

static void bch2_disk_reservations_verify(struct bch_fs *c, int flags)
{
	if (!(flags & BCH_DISK_RESERVATION_NOFAIL)) {
		u64 used = __bch2_fs_sectors_used(c);
		u64 cached = 0;
		u64 avail = atomic64_read(&c->sectors_available);
		int cpu;

		for_each_possible_cpu(cpu)
			cached += per_cpu_ptr(c->usage_percpu, cpu)->available_cache;

		if (used + avail + cached > c->capacity)
			panic("used %llu avail %llu cached %llu capacity %llu\n",
			      used, avail, cached, c->capacity);
	}
}

#else

static void bch2_fs_stats_verify(struct bch_fs *c) {}
static void bch2_dev_stats_verify(struct bch_dev *ca) {}
static void bch2_disk_reservations_verify(struct bch_fs *c, int flags) {}

#endif

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

#define bch2_usage_add(_acc, _stats)					\
do {									\
	typeof(_acc) _a = (_acc), _s = (_stats);			\
	unsigned i;							\
									\
	for (i = 0; i < sizeof(*_a) / sizeof(u64); i++)			\
		((u64 *) (_a))[i] += ((u64 *) (_s))[i];			\
} while (0)

#define bch2_usage_read_raw(_stats)					\
({									\
	typeof(*this_cpu_ptr(_stats)) _acc;				\
	int cpu;							\
									\
	memset(&_acc, 0, sizeof(_acc));					\
									\
	for_each_possible_cpu(cpu)					\
		bch2_usage_add(&_acc, per_cpu_ptr((_stats), cpu));	\
									\
	_acc;								\
})

#define bch2_usage_read_cached(_c, _cached, _uncached)			\
({									\
	typeof(_cached) _ret;						\
	unsigned _seq;							\
									\
	do {								\
		_seq = read_seqcount_begin(&(_c)->gc_pos_lock);		\
		_ret = (_c)->gc_pos.phase == GC_PHASE_DONE		\
			? bch2_usage_read_raw(_uncached)			\
			: (_cached);					\
	} while (read_seqcount_retry(&(_c)->gc_pos_lock, _seq));	\
									\
	_ret;								\
})

struct bch_dev_usage __bch2_dev_usage_read(struct bch_dev *ca)
{
	return bch2_usage_read_raw(ca->usage_percpu);
}

struct bch_dev_usage bch2_dev_usage_read(struct bch_fs *c, struct bch_dev *ca)
{
	return bch2_usage_read_cached(c, ca->usage_cached, ca->usage_percpu);
}

struct bch_fs_usage
__bch2_fs_usage_read(struct bch_fs *c)
{
	return bch2_usage_read_raw(c->usage_percpu);
}

struct bch_fs_usage
bch2_fs_usage_read(struct bch_fs *c)
{
	return bch2_usage_read_cached(c,
				     c->usage_cached,
				     c->usage_percpu);
}

struct fs_usage_sum {
	u64	hidden;
	u64	data;
	u64	cached;
	u64	reserved;
};

static inline struct fs_usage_sum __fs_usage_sum(struct bch_fs_usage stats)
{
	struct fs_usage_sum sum = { 0 };
	unsigned i;

	/*
	 * For superblock and journal we count bucket usage, not sector usage,
	 * because any internal fragmentation should _not_ be counted as
	 * free space:
	 */
	sum.hidden += stats.buckets[BCH_DATA_SB];
	sum.hidden += stats.buckets[BCH_DATA_JOURNAL];

	for (i = 0; i < ARRAY_SIZE(stats.replicas); i++) {
		sum.data	+= stats.replicas[i].data[BCH_DATA_BTREE];
		sum.data	+= stats.replicas[i].data[BCH_DATA_USER];
		sum.cached	+= stats.replicas[i].data[BCH_DATA_CACHED];
		sum.reserved	+= stats.replicas[i].persistent_reserved;
	}

	sum.reserved += stats.online_reserved;
	return sum;
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

static inline u64 __bch2_fs_sectors_used(struct bch_fs *c, struct bch_fs_usage stats)
{
	struct fs_usage_sum sum = __fs_usage_sum(stats);

	return sum.hidden + sum.data + reserve_factor(sum.reserved);
}

u64 bch2_fs_sectors_used(struct bch_fs *c, struct bch_fs_usage stats)
{
	return min(c->capacity, __bch2_fs_sectors_used(c, stats));
}

static u64 bch2_fs_sectors_free(struct bch_fs *c, struct bch_fs_usage stats)
{
	return c->capacity - bch2_fs_sectors_used(c, stats);
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
		?  BCH_DATA_CACHED
		: m.data_type;
}

static bool bucket_became_unavailable(struct bch_fs *c,
				      struct bucket_mark old,
				      struct bucket_mark new)
{
	return is_available_bucket(old) &&
	       !is_available_bucket(new) &&
	       (!c || c->gc_pos.phase == GC_PHASE_DONE);
}

void bch2_fs_usage_apply(struct bch_fs *c,
			 struct bch_fs_usage *stats,
			 struct disk_reservation *disk_res,
			 struct gc_pos gc_pos)
{
	struct fs_usage_sum sum = __fs_usage_sum(*stats);
	s64 added = sum.data + sum.reserved;

	/*
	 * Not allowed to reduce sectors_available except by getting a
	 * reservation:
	 */
	BUG_ON(added > (s64) (disk_res ? disk_res->sectors : 0));

	if (added > 0) {
		disk_res->sectors	-= added;
		stats->online_reserved	-= added;
	}

	percpu_down_read(&c->usage_lock);
	preempt_disable();
	/* online_reserved not subject to gc: */
	this_cpu_add(c->usage_percpu->online_reserved, stats->online_reserved);
	stats->online_reserved = 0;

	if (!gc_will_visit(c, gc_pos))
		bch2_usage_add(this_cpu_ptr(c->usage_percpu), stats);

	bch2_fs_stats_verify(c);
	preempt_enable();
	percpu_up_read(&c->usage_lock);

	memset(stats, 0, sizeof(*stats));
}

static void bch2_dev_usage_update(struct bch_fs *c, struct bch_dev *ca,
				  struct bch_fs_usage *stats,
				  struct bucket_mark old, struct bucket_mark new)
{
	struct bch_dev_usage *dev_usage;

	percpu_rwsem_assert_held(&c->usage_lock);

	bch2_fs_inconsistent_on(old.data_type && new.data_type &&
				old.data_type != new.data_type, c,
		"different types of data in same bucket: %s, %s",
		bch2_data_types[old.data_type],
		bch2_data_types[new.data_type]);

	stats->buckets[bucket_type(old)] -= ca->mi.bucket_size;
	stats->buckets[bucket_type(new)] += ca->mi.bucket_size;

	preempt_disable();
	dev_usage = this_cpu_ptr(ca->usage_percpu);

	dev_usage->buckets[bucket_type(old)]--;
	dev_usage->buckets[bucket_type(new)]++;

	dev_usage->buckets_alloc +=
		(int) new.owned_by_allocator - (int) old.owned_by_allocator;
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

	bch2_dev_stats_verify(ca);
}

#define bucket_data_cmpxchg(c, ca, stats, g, new, expr)		\
({								\
	struct bucket_mark _old = bucket_cmpxchg(g, new, expr);	\
								\
	bch2_dev_usage_update(c, ca, stats, _old, new);		\
	_old;							\
})

void bch2_invalidate_bucket(struct bch_fs *c, struct bch_dev *ca,
			    size_t b, struct bucket_mark *old)
{
	struct bch_fs_usage *stats = this_cpu_ptr(c->usage_percpu);
	struct bucket *g;
	struct bucket_mark new;

	percpu_rwsem_assert_held(&c->usage_lock);

	g = bucket(ca, b);

	*old = bucket_data_cmpxchg(c, ca, stats, g, new, ({
		BUG_ON(!is_available_bucket(new));

		new.owned_by_allocator	= 1;
		new.data_type		= 0;
		new.cached_sectors	= 0;
		new.dirty_sectors	= 0;
		new.gen++;
	}));

	/*
	 * This isn't actually correct yet, since fs usage is still
	 * uncompressed sectors:
	 */
	stats->replicas[0].data[BCH_DATA_CACHED] -= old->cached_sectors;

	if (!old->owned_by_allocator && old->cached_sectors)
		trace_invalidate(ca, bucket_to_sector(ca, b),
				 old->cached_sectors);
}

void bch2_mark_alloc_bucket(struct bch_fs *c, struct bch_dev *ca,
			    size_t b, bool owned_by_allocator,
			    struct gc_pos pos, unsigned flags)
{
	struct bch_fs_usage *stats = this_cpu_ptr(c->usage_percpu);
	struct bucket *g;
	struct bucket_mark old, new;

	percpu_rwsem_assert_held(&c->usage_lock);
	g = bucket(ca, b);

	if (!(flags & BCH_BUCKET_MARK_GC_LOCK_HELD) &&
	    gc_will_visit(c, pos))
		return;

	old = bucket_data_cmpxchg(c, ca, stats, g, new, ({
		new.owned_by_allocator	= owned_by_allocator;
	}));

	BUG_ON(!owned_by_allocator && !old.owned_by_allocator &&
	       c->gc_pos.phase == GC_PHASE_DONE);
}

#define checked_add(a, b)					\
do {								\
	unsigned _res = (unsigned) (a) + (b);			\
	(a) = _res;						\
	BUG_ON((a) != _res);					\
} while (0)

void bch2_mark_metadata_bucket(struct bch_fs *c, struct bch_dev *ca,
			       size_t b, enum bch_data_type type,
			       unsigned sectors, struct gc_pos pos,
			       unsigned flags)
{
	struct bch_fs_usage *stats;
	struct bucket *g;
	struct bucket_mark old, new;

	BUG_ON(type != BCH_DATA_SB &&
	       type != BCH_DATA_JOURNAL);

	if (likely(c)) {
		percpu_rwsem_assert_held(&c->usage_lock);

		if (!(flags & BCH_BUCKET_MARK_GC_LOCK_HELD) &&
		    gc_will_visit(c, pos))
			return;

		preempt_disable();
		stats = this_cpu_ptr(c->usage_percpu);

		g = bucket(ca, b);
		old = bucket_data_cmpxchg(c, ca, stats, g, new, ({
			new.data_type = type;
			checked_add(new.dirty_sectors, sectors);
		}));

		stats->replicas[0].data[type] += sectors;
		preempt_enable();
	} else {
		rcu_read_lock();

		g = bucket(ca, b);
		old = bucket_cmpxchg(g, new, ({
			new.data_type = type;
			checked_add(new.dirty_sectors, sectors);
		}));

		rcu_read_unlock();
	}

	BUG_ON(!(flags & BCH_BUCKET_MARK_MAY_MAKE_UNAVAILABLE) &&
	       bucket_became_unavailable(c, old, new));
}

static int __disk_sectors(struct bch_extent_crc_unpacked crc, unsigned sectors)
{
	if (!sectors)
		return 0;

	return max(1U, DIV_ROUND_UP(sectors * crc.compressed_size,
				    crc.uncompressed_size));
}

/*
 * Checking against gc's position has to be done here, inside the cmpxchg()
 * loop, to avoid racing with the start of gc clearing all the marks - GC does
 * that with the gc pos seqlock held.
 */
static void bch2_mark_pointer(struct bch_fs *c,
			      struct bkey_s_c_extent e,
			      struct extent_ptr_decoded p,
			      s64 sectors, enum bch_data_type data_type,
			      unsigned replicas,
			      struct bch_fs_usage *fs_usage,
			      u64 journal_seq, unsigned flags)
{
	struct bucket_mark old, new;
	struct bch_dev *ca = bch_dev_bkey_exists(c, p.ptr.dev);
	struct bucket *g = PTR_BUCKET(ca, &p.ptr);
	s64 uncompressed_sectors = sectors;
	u64 v;

	if (p.crc.compression_type) {
		unsigned old_sectors, new_sectors;

		if (sectors > 0) {
			old_sectors = 0;
			new_sectors = sectors;
		} else {
			old_sectors = e.k->size;
			new_sectors = e.k->size + sectors;
		}

		sectors = -__disk_sectors(p.crc, old_sectors)
			  +__disk_sectors(p.crc, new_sectors);
	}

	/*
	 * fs level usage (which determines free space) is in uncompressed
	 * sectors, until copygc + compression is sorted out:
	 *
	 * note also that we always update @fs_usage, even when we otherwise
	 * wouldn't do anything because gc is running - this is because the
	 * caller still needs to account w.r.t. its disk reservation. It is
	 * caller's responsibility to not apply @fs_usage if gc is in progress.
	 */
	fs_usage->replicas
		[!p.ptr.cached && replicas ? replicas - 1 : 0].data
		[!p.ptr.cached ? data_type : BCH_DATA_CACHED] +=
			uncompressed_sectors;

	if (flags & BCH_BUCKET_MARK_GC_WILL_VISIT) {
		if (journal_seq)
			bucket_cmpxchg(g, new, ({
				new.journal_seq_valid	= 1;
				new.journal_seq		= journal_seq;
			}));

		return;
	}

	v = atomic64_read(&g->_mark.v);
	do {
		new.v.counter = old.v.counter = v;

		/*
		 * Check this after reading bucket mark to guard against
		 * the allocator invalidating a bucket after we've already
		 * checked the gen
		 */
		if (gen_after(new.gen, p.ptr.gen)) {
			BUG_ON(!test_bit(BCH_FS_ALLOC_READ_DONE, &c->flags));
			EBUG_ON(!p.ptr.cached &&
				test_bit(JOURNAL_REPLAY_DONE, &c->journal.flags));
			return;
		}

		if (!p.ptr.cached)
			checked_add(new.dirty_sectors, sectors);
		else
			checked_add(new.cached_sectors, sectors);

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

	bch2_dev_usage_update(c, ca, fs_usage, old, new);

	BUG_ON(!(flags & BCH_BUCKET_MARK_MAY_MAKE_UNAVAILABLE) &&
	       bucket_became_unavailable(c, old, new));
}

void bch2_mark_key(struct bch_fs *c, struct bkey_s_c k,
		   s64 sectors, enum bch_data_type data_type,
		   struct gc_pos pos,
		   struct bch_fs_usage *stats,
		   u64 journal_seq, unsigned flags)
{
	unsigned replicas = bch2_extent_nr_dirty_ptrs(k);

	BUG_ON(replicas && replicas - 1 > ARRAY_SIZE(stats->replicas));

	/*
	 * synchronization w.r.t. GC:
	 *
	 * Normally, bucket sector counts/marks are updated on the fly, as
	 * references are added/removed from the btree, the lists of buckets the
	 * allocator owns, other metadata buckets, etc.
	 *
	 * When GC is in progress and going to mark this reference, we do _not_
	 * mark this reference here, to avoid double counting - GC will count it
	 * when it gets to it.
	 *
	 * To know whether we should mark a given reference (GC either isn't
	 * running, or has already marked references at this position) we
	 * construct a total order for everything GC walks. Then, we can simply
	 * compare the position of the reference we're marking - @pos - with
	 * GC's current position. If GC is going to mark this reference, GC's
	 * current position will be less than @pos; if GC's current position is
	 * greater than @pos GC has either already walked this position, or
	 * isn't running.
	 *
	 * To avoid racing with GC's position changing, we have to deal with
	 *  - GC's position being set to GC_POS_MIN when GC starts:
	 *    usage_lock guards against this
	 *  - GC's position overtaking @pos: we guard against this with
	 *    whatever lock protects the data structure the reference lives in
	 *    (e.g. the btree node lock, or the relevant allocator lock).
	 */

	percpu_down_read(&c->usage_lock);
	if (!(flags & BCH_BUCKET_MARK_GC_LOCK_HELD) &&
	    gc_will_visit(c, pos))
		flags |= BCH_BUCKET_MARK_GC_WILL_VISIT;

	if (!stats)
		stats = this_cpu_ptr(c->usage_percpu);

	switch (k.k->type) {
	case BCH_EXTENT:
	case BCH_EXTENT_CACHED: {
		struct bkey_s_c_extent e = bkey_s_c_to_extent(k);
		const union bch_extent_entry *entry;
		struct extent_ptr_decoded p;

		BUG_ON(!sectors);

		extent_for_each_ptr_decode(e, p, entry)
			bch2_mark_pointer(c, e, p, sectors, data_type,
					  replicas, stats, journal_seq, flags);
		break;
	}
	case BCH_RESERVATION:
		if (replicas)
			stats->replicas[replicas - 1].persistent_reserved +=
				sectors * replicas;
		break;
	}
	percpu_up_read(&c->usage_lock);
}

/* Disk reservations: */

static u64 __recalc_sectors_available(struct bch_fs *c)
{
	int cpu;

	for_each_possible_cpu(cpu)
		per_cpu_ptr(c->usage_percpu, cpu)->available_cache = 0;

	return avail_factor(bch2_fs_sectors_free(c, bch2_fs_usage_read(c)));
}

/* Used by gc when it's starting: */
void bch2_recalc_sectors_available(struct bch_fs *c)
{
	percpu_down_write(&c->usage_lock);
	atomic64_set(&c->sectors_available, __recalc_sectors_available(c));
	percpu_up_write(&c->usage_lock);
}

void __bch2_disk_reservation_put(struct bch_fs *c, struct disk_reservation *res)
{
	percpu_down_read(&c->usage_lock);
	this_cpu_sub(c->usage_percpu->online_reserved,
		     res->sectors);

	bch2_fs_stats_verify(c);
	percpu_up_read(&c->usage_lock);

	res->sectors = 0;
}

#define SECTORS_CACHE	1024

int bch2_disk_reservation_add(struct bch_fs *c, struct disk_reservation *res,
			      unsigned sectors, int flags)
{
	struct bch_fs_usage *stats;
	u64 old, v, get;
	s64 sectors_available;
	int ret;

	percpu_down_read(&c->usage_lock);
	preempt_disable();
	stats = this_cpu_ptr(c->usage_percpu);

	if (sectors <= stats->available_cache)
		goto out;

	v = atomic64_read(&c->sectors_available);
	do {
		old = v;
		get = min((u64) sectors + SECTORS_CACHE, old);

		if (get < sectors) {
			preempt_enable();
			percpu_up_read(&c->usage_lock);
			goto recalculate;
		}
	} while ((v = atomic64_cmpxchg(&c->sectors_available,
				       old, old - get)) != old);

	stats->available_cache	+= get;

out:
	stats->available_cache	-= sectors;
	stats->online_reserved	+= sectors;
	res->sectors		+= sectors;

	bch2_disk_reservations_verify(c, flags);
	bch2_fs_stats_verify(c);
	preempt_enable();
	percpu_up_read(&c->usage_lock);
	return 0;

recalculate:
	/*
	 * GC recalculates sectors_available when it starts, so that hopefully
	 * we don't normally end up blocking here:
	 */

	/*
	 * Piss fuck, we can be called from extent_insert_fixup() with btree
	 * locks held:
	 */

	if (!(flags & BCH_DISK_RESERVATION_GC_LOCK_HELD)) {
		if (!(flags & BCH_DISK_RESERVATION_BTREE_LOCKS_HELD))
			down_read(&c->gc_lock);
		else if (!down_read_trylock(&c->gc_lock))
			return -EINTR;
	}

	percpu_down_write(&c->usage_lock);
	sectors_available = __recalc_sectors_available(c);

	if (sectors <= sectors_available ||
	    (flags & BCH_DISK_RESERVATION_NOFAIL)) {
		atomic64_set(&c->sectors_available,
			     max_t(s64, 0, sectors_available - sectors));
		stats->online_reserved	+= sectors;
		res->sectors		+= sectors;
		ret = 0;

		bch2_disk_reservations_verify(c, flags);
	} else {
		atomic64_set(&c->sectors_available, sectors_available);
		ret = -ENOSPC;
	}

	bch2_fs_stats_verify(c);
	percpu_up_write(&c->usage_lock);

	if (!(flags & BCH_DISK_RESERVATION_GC_LOCK_HELD))
		up_read(&c->gc_lock);

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
	unsigned long *buckets_dirty = NULL;
	u8 *oldest_gens = NULL;
	alloc_fifo	free[RESERVE_NR];
	alloc_fifo	free_inc;
	alloc_heap	alloc_heap;
	copygc_heap	copygc_heap;

	size_t btree_reserve	= DIV_ROUND_UP(BTREE_NODE_RESERVE,
			     ca->mi.bucket_size / c->opts.btree_node_size);
	/* XXX: these should be tunable */
	size_t reserve_none	= max_t(size_t, 4, nbuckets >> 9);
	size_t copygc_reserve	= max_t(size_t, 16, nbuckets >> 7);
	size_t free_inc_nr	= max(max_t(size_t, 16, nbuckets >> 12),
				      btree_reserve);
	bool resize = ca->buckets != NULL,
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
	    !(oldest_gens	= kvpmalloc(nbuckets * sizeof(u8),
					    GFP_KERNEL|__GFP_ZERO)) ||
	    !(buckets_dirty	= kvpmalloc(BITS_TO_LONGS(nbuckets) *
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
		percpu_down_write(&c->usage_lock);
	}

	old_buckets = bucket_array(ca);

	if (resize) {
		size_t n = min(buckets->nbuckets, old_buckets->nbuckets);

		memcpy(buckets->b,
		       old_buckets->b,
		       n * sizeof(struct bucket));
		memcpy(oldest_gens,
		       ca->oldest_gens,
		       n * sizeof(u8));
		memcpy(buckets_dirty,
		       ca->buckets_dirty,
		       BITS_TO_LONGS(n) * sizeof(unsigned long));
	}

	rcu_assign_pointer(ca->buckets, buckets);
	buckets = old_buckets;

	swap(ca->oldest_gens, oldest_gens);
	swap(ca->buckets_dirty, buckets_dirty);

	if (resize)
		percpu_up_write(&c->usage_lock);

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
	kvpfree(buckets_dirty,
		BITS_TO_LONGS(nbuckets) * sizeof(unsigned long));
	kvpfree(oldest_gens,
		nbuckets * sizeof(u8));
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
	kvpfree(ca->buckets_dirty,
		BITS_TO_LONGS(ca->mi.nbuckets) * sizeof(unsigned long));
	kvpfree(ca->oldest_gens, ca->mi.nbuckets * sizeof(u8));
	kvpfree(rcu_dereference_protected(ca->buckets, 1),
		sizeof(struct bucket_array) +
		ca->mi.nbuckets * sizeof(struct bucket));

	free_percpu(ca->usage_percpu);
}

int bch2_dev_buckets_alloc(struct bch_fs *c, struct bch_dev *ca)
{
	if (!(ca->usage_percpu = alloc_percpu(struct bch_dev_usage)))
		return -ENOMEM;

	return bch2_dev_buckets_resize(c, ca, ca->mi.nbuckets);;
}
