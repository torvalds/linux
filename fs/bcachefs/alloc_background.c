// SPDX-License-Identifier: GPL-2.0
#include "bcachefs.h"
#include "alloc_background.h"
#include "alloc_foreground.h"
#include "btree_cache.h"
#include "btree_io.h"
#include "btree_key_cache.h"
#include "btree_update.h"
#include "btree_update_interior.h"
#include "btree_gc.h"
#include "buckets.h"
#include "clock.h"
#include "debug.h"
#include "ec.h"
#include "error.h"
#include "recovery.h"
#include "trace.h"

#include <linux/kthread.h>
#include <linux/math64.h>
#include <linux/random.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/sort.h>

static const char * const bch2_alloc_field_names[] = {
#define x(name, bytes) #name,
	BCH_ALLOC_FIELDS()
#undef x
	NULL
};

static void bch2_recalc_oldest_io(struct bch_fs *, struct bch_dev *, int);

/* Ratelimiting/PD controllers */

static void pd_controllers_update(struct work_struct *work)
{
	struct bch_fs *c = container_of(to_delayed_work(work),
					   struct bch_fs,
					   pd_controllers_update);
	struct bch_dev *ca;
	s64 free = 0, fragmented = 0;
	unsigned i;

	for_each_member_device(ca, c, i) {
		struct bch_dev_usage stats = bch2_dev_usage_read(ca);

		free += bucket_to_sector(ca,
				__dev_buckets_free(ca, stats)) << 9;
		/*
		 * Bytes of internal fragmentation, which can be
		 * reclaimed by copy GC
		 */
		fragmented += max_t(s64, 0, (bucket_to_sector(ca,
					stats.buckets[BCH_DATA_user] +
					stats.buckets[BCH_DATA_cached]) -
				  (stats.sectors[BCH_DATA_user] +
				   stats.sectors[BCH_DATA_cached])) << 9);
	}

	bch2_pd_controller_update(&c->copygc_pd, free, fragmented, -1);
	schedule_delayed_work(&c->pd_controllers_update,
			      c->pd_controllers_update_seconds * HZ);
}

/* Persistent alloc info: */

static inline u64 get_alloc_field(const struct bch_alloc *a,
				  const void **p, unsigned field)
{
	unsigned bytes = BCH_ALLOC_FIELD_BYTES[field];
	u64 v;

	if (!(a->fields & (1 << field)))
		return 0;

	switch (bytes) {
	case 1:
		v = *((const u8 *) *p);
		break;
	case 2:
		v = le16_to_cpup(*p);
		break;
	case 4:
		v = le32_to_cpup(*p);
		break;
	case 8:
		v = le64_to_cpup(*p);
		break;
	default:
		BUG();
	}

	*p += bytes;
	return v;
}

static inline void put_alloc_field(struct bkey_i_alloc *a, void **p,
				   unsigned field, u64 v)
{
	unsigned bytes = BCH_ALLOC_FIELD_BYTES[field];

	if (!v)
		return;

	a->v.fields |= 1 << field;

	switch (bytes) {
	case 1:
		*((u8 *) *p) = v;
		break;
	case 2:
		*((__le16 *) *p) = cpu_to_le16(v);
		break;
	case 4:
		*((__le32 *) *p) = cpu_to_le32(v);
		break;
	case 8:
		*((__le64 *) *p) = cpu_to_le64(v);
		break;
	default:
		BUG();
	}

	*p += bytes;
}

struct bkey_alloc_unpacked bch2_alloc_unpack(struct bkey_s_c k)
{
	struct bkey_alloc_unpacked ret = { .gen = 0 };

	if (k.k->type == KEY_TYPE_alloc) {
		const struct bch_alloc *a = bkey_s_c_to_alloc(k).v;
		const void *d = a->data;
		unsigned idx = 0;

		ret.gen = a->gen;

#define x(_name, _bits)	ret._name = get_alloc_field(a, &d, idx++);
		BCH_ALLOC_FIELDS()
#undef  x
	}
	return ret;
}

void bch2_alloc_pack(struct bkey_i_alloc *dst,
		     const struct bkey_alloc_unpacked src)
{
	unsigned idx = 0;
	void *d = dst->v.data;
	unsigned bytes;

	dst->v.fields	= 0;
	dst->v.gen	= src.gen;

#define x(_name, _bits)	put_alloc_field(dst, &d, idx++, src._name);
	BCH_ALLOC_FIELDS()
#undef  x

	bytes = (void *) d - (void *) &dst->v;
	set_bkey_val_bytes(&dst->k, bytes);
	memset_u64s_tail(&dst->v, 0, bytes);
}

static unsigned bch_alloc_val_u64s(const struct bch_alloc *a)
{
	unsigned i, bytes = offsetof(struct bch_alloc, data);

	for (i = 0; i < ARRAY_SIZE(BCH_ALLOC_FIELD_BYTES); i++)
		if (a->fields & (1 << i))
			bytes += BCH_ALLOC_FIELD_BYTES[i];

	return DIV_ROUND_UP(bytes, sizeof(u64));
}

const char *bch2_alloc_invalid(const struct bch_fs *c, struct bkey_s_c k)
{
	struct bkey_s_c_alloc a = bkey_s_c_to_alloc(k);

	if (k.k->p.inode >= c->sb.nr_devices ||
	    !c->devs[k.k->p.inode])
		return "invalid device";

	/* allow for unknown fields */
	if (bkey_val_u64s(a.k) < bch_alloc_val_u64s(a.v))
		return "incorrect value size";

	return NULL;
}

void bch2_alloc_to_text(struct printbuf *out, struct bch_fs *c,
			struct bkey_s_c k)
{
	struct bkey_s_c_alloc a = bkey_s_c_to_alloc(k);
	const void *d = a.v->data;
	unsigned i;

	pr_buf(out, "gen %u", a.v->gen);

	for (i = 0; i < BCH_ALLOC_FIELD_NR; i++)
		if (a.v->fields & (1 << i))
			pr_buf(out, " %s %llu",
			       bch2_alloc_field_names[i],
			       get_alloc_field(a.v, &d, i));
}

static int bch2_alloc_read_fn(struct bch_fs *c, enum btree_id id,
			      unsigned level, struct bkey_s_c k)
{
	struct bch_dev *ca;
	struct bucket *g;
	struct bkey_alloc_unpacked u;

	if (level || k.k->type != KEY_TYPE_alloc)
		return 0;

	ca = bch_dev_bkey_exists(c, k.k->p.inode);
	g = __bucket(ca, k.k->p.offset, 0);
	u = bch2_alloc_unpack(k);

	g->_mark.gen		= u.gen;
	g->_mark.data_type	= u.data_type;
	g->_mark.dirty_sectors	= u.dirty_sectors;
	g->_mark.cached_sectors	= u.cached_sectors;
	g->io_time[READ]	= u.read_time;
	g->io_time[WRITE]	= u.write_time;
	g->oldest_gen		= u.oldest_gen;
	g->gen_valid		= 1;

	return 0;
}

int bch2_alloc_read(struct bch_fs *c, struct journal_keys *journal_keys)
{
	struct bch_dev *ca;
	unsigned i;
	int ret = 0;

	down_read(&c->gc_lock);
	ret = bch2_btree_and_journal_walk(c, journal_keys, BTREE_ID_ALLOC,
					  NULL, bch2_alloc_read_fn);
	up_read(&c->gc_lock);

	if (ret) {
		bch_err(c, "error reading alloc info: %i", ret);
		return ret;
	}

	percpu_down_write(&c->mark_lock);
	bch2_dev_usage_from_buckets(c);
	percpu_up_write(&c->mark_lock);

	mutex_lock(&c->bucket_clock[READ].lock);
	for_each_member_device(ca, c, i) {
		down_read(&ca->bucket_lock);
		bch2_recalc_oldest_io(c, ca, READ);
		up_read(&ca->bucket_lock);
	}
	mutex_unlock(&c->bucket_clock[READ].lock);

	mutex_lock(&c->bucket_clock[WRITE].lock);
	for_each_member_device(ca, c, i) {
		down_read(&ca->bucket_lock);
		bch2_recalc_oldest_io(c, ca, WRITE);
		up_read(&ca->bucket_lock);
	}
	mutex_unlock(&c->bucket_clock[WRITE].lock);

	return 0;
}

static int bch2_alloc_write_key(struct btree_trans *trans,
				struct btree_iter *iter,
				unsigned flags)
{
	struct bch_fs *c = trans->c;
	struct bkey_s_c k;
	struct bch_dev *ca;
	struct bucket_array *ba;
	struct bucket *g;
	struct bucket_mark m;
	struct bkey_alloc_unpacked old_u, new_u;
	__BKEY_PADDED(k, 8) alloc_key; /* hack: */
	struct bkey_i_alloc *a;
	int ret;
retry:
	bch2_trans_begin(trans);

	ret = bch2_btree_key_cache_flush(trans,
			BTREE_ID_ALLOC, iter->pos);
	if (ret)
		goto err;

	k = bch2_btree_iter_peek_slot(iter);
	ret = bkey_err(k);
	if (ret)
		goto err;

	old_u = bch2_alloc_unpack(k);

	percpu_down_read(&c->mark_lock);
	ca	= bch_dev_bkey_exists(c, iter->pos.inode);
	ba	= bucket_array(ca);

	g	= &ba->b[iter->pos.offset];
	m	= READ_ONCE(g->mark);
	new_u	= alloc_mem_to_key(g, m);
	percpu_up_read(&c->mark_lock);

	if (!bkey_alloc_unpacked_cmp(old_u, new_u))
		return 0;

	a = bkey_alloc_init(&alloc_key.k);
	a->k.p = iter->pos;
	bch2_alloc_pack(a, new_u);

	bch2_trans_update(trans, iter, &a->k_i,
			  BTREE_TRIGGER_NORUN);
	ret = bch2_trans_commit(trans, NULL, NULL,
				BTREE_INSERT_NOFAIL|flags);
err:
	if (ret == -EINTR)
		goto retry;
	return ret;
}

int bch2_dev_alloc_write(struct bch_fs *c, struct bch_dev *ca, unsigned flags)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	u64 first_bucket, nbuckets;
	int ret = 0;

	percpu_down_read(&c->mark_lock);
	first_bucket	= bucket_array(ca)->first_bucket;
	nbuckets	= bucket_array(ca)->nbuckets;
	percpu_up_read(&c->mark_lock);

	BUG_ON(BKEY_ALLOC_VAL_U64s_MAX > 8);

	bch2_trans_init(&trans, c, BTREE_ITER_MAX, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_ALLOC,
				   POS(ca->dev_idx, first_bucket),
				   BTREE_ITER_SLOTS|BTREE_ITER_INTENT);

	while (iter->pos.offset < nbuckets) {
		bch2_trans_cond_resched(&trans);

		ret = bch2_alloc_write_key(&trans, iter, flags);
		if (ret)
			break;
		bch2_btree_iter_next_slot(iter);
	}

	bch2_trans_exit(&trans);

	return ret;
}

int bch2_alloc_write(struct bch_fs *c, unsigned flags)
{
	struct bch_dev *ca;
	unsigned i;
	int ret = 0;

	for_each_rw_member(ca, c, i) {
		bch2_dev_alloc_write(c, ca, flags);
		if (ret) {
			percpu_ref_put(&ca->io_ref);
			break;
		}
	}

	return ret;
}

/* Bucket IO clocks: */

static void bch2_recalc_oldest_io(struct bch_fs *c, struct bch_dev *ca, int rw)
{
	struct bucket_clock *clock = &c->bucket_clock[rw];
	struct bucket_array *buckets = bucket_array(ca);
	struct bucket *g;
	u16 max_last_io = 0;
	unsigned i;

	lockdep_assert_held(&c->bucket_clock[rw].lock);

	/* Recalculate max_last_io for this device: */
	for_each_bucket(g, buckets)
		max_last_io = max(max_last_io, bucket_last_io(c, g, rw));

	ca->max_last_bucket_io[rw] = max_last_io;

	/* Recalculate global max_last_io: */
	max_last_io = 0;

	for_each_member_device(ca, c, i)
		max_last_io = max(max_last_io, ca->max_last_bucket_io[rw]);

	clock->max_last_io = max_last_io;
}

static void bch2_rescale_bucket_io_times(struct bch_fs *c, int rw)
{
	struct bucket_clock *clock = &c->bucket_clock[rw];
	struct bucket_array *buckets;
	struct bch_dev *ca;
	struct bucket *g;
	unsigned i;

	trace_rescale_prios(c);

	for_each_member_device(ca, c, i) {
		down_read(&ca->bucket_lock);
		buckets = bucket_array(ca);

		for_each_bucket(g, buckets)
			g->io_time[rw] = clock->hand -
			bucket_last_io(c, g, rw) / 2;

		bch2_recalc_oldest_io(c, ca, rw);

		up_read(&ca->bucket_lock);
	}
}

static inline u64 bucket_clock_freq(u64 capacity)
{
	return max(capacity >> 10, 2028ULL);
}

static void bch2_inc_clock_hand(struct io_timer *timer)
{
	struct bucket_clock *clock = container_of(timer,
						struct bucket_clock, rescale);
	struct bch_fs *c = container_of(clock,
					struct bch_fs, bucket_clock[clock->rw]);
	struct bch_dev *ca;
	u64 capacity;
	unsigned i;

	mutex_lock(&clock->lock);

	/* if clock cannot be advanced more, rescale prio */
	if (clock->max_last_io >= U16_MAX - 2)
		bch2_rescale_bucket_io_times(c, clock->rw);

	BUG_ON(clock->max_last_io >= U16_MAX - 2);

	for_each_member_device(ca, c, i)
		ca->max_last_bucket_io[clock->rw]++;
	clock->max_last_io++;
	clock->hand++;

	mutex_unlock(&clock->lock);

	capacity = READ_ONCE(c->capacity);

	if (!capacity)
		return;

	/*
	 * we only increment when 0.1% of the filesystem capacity has been read
	 * or written too, this determines if it's time
	 *
	 * XXX: we shouldn't really be going off of the capacity of devices in
	 * RW mode (that will be 0 when we're RO, yet we can still service
	 * reads)
	 */
	timer->expire += bucket_clock_freq(capacity);

	bch2_io_timer_add(&c->io_clock[clock->rw], timer);
}

static void bch2_bucket_clock_init(struct bch_fs *c, int rw)
{
	struct bucket_clock *clock = &c->bucket_clock[rw];

	clock->hand		= 1;
	clock->rw		= rw;
	clock->rescale.fn	= bch2_inc_clock_hand;
	clock->rescale.expire	= bucket_clock_freq(c->capacity);
	mutex_init(&clock->lock);
}

int bch2_bucket_io_time_reset(struct btree_trans *trans, unsigned dev,
			      size_t bucket_nr, int rw)
{
	struct bch_fs *c = trans->c;
	struct bch_dev *ca = bch_dev_bkey_exists(c, dev);
	struct btree_iter *iter;
	struct bucket *g;
	struct bkey_i_alloc *a;
	struct bkey_alloc_unpacked u;
	u16 *time;
	int ret = 0;

	iter = bch2_trans_get_iter(trans, BTREE_ID_ALLOC, POS(dev, bucket_nr),
				   BTREE_ITER_CACHED|
				   BTREE_ITER_CACHED_NOFILL|
				   BTREE_ITER_INTENT);
	ret = bch2_btree_iter_traverse(iter);
	if (ret)
		goto out;

	a = bch2_trans_kmalloc(trans, BKEY_ALLOC_U64s_MAX * 8);
	ret = PTR_ERR_OR_ZERO(a);
	if (ret)
		goto out;

	percpu_down_read(&c->mark_lock);
	g = bucket(ca, bucket_nr);
	u = alloc_mem_to_key(g, READ_ONCE(g->mark));
	percpu_up_read(&c->mark_lock);

	bkey_alloc_init(&a->k_i);
	a->k.p = iter->pos;

	time = rw == READ ? &u.read_time : &u.write_time;
	if (*time == c->bucket_clock[rw].hand)
		goto out;

	*time = c->bucket_clock[rw].hand;

	bch2_alloc_pack(a, u);

	ret   = bch2_trans_update(trans, iter, &a->k_i, 0) ?:
		bch2_trans_commit(trans, NULL, NULL, 0);
out:
	bch2_trans_iter_put(trans, iter);
	return ret;
}

/* Background allocator thread: */

/*
 * Scans for buckets to be invalidated, invalidates them, rewrites prios/gens
 * (marking them as invalidated on disk), then optionally issues discard
 * commands to the newly free buckets, then puts them on the various freelists.
 */

/**
 * wait_buckets_available - wait on reclaimable buckets
 *
 * If there aren't enough available buckets to fill up free_inc, wait until
 * there are.
 */
static int wait_buckets_available(struct bch_fs *c, struct bch_dev *ca)
{
	unsigned long gc_count = c->gc_count;
	u64 available;
	int ret = 0;

	ca->allocator_state = ALLOCATOR_BLOCKED;
	closure_wake_up(&c->freelist_wait);

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (kthread_should_stop()) {
			ret = 1;
			break;
		}

		if (gc_count != c->gc_count)
			ca->inc_gen_really_needs_gc = 0;

		available = max_t(s64, 0, dev_buckets_available(ca) -
				  ca->inc_gen_really_needs_gc);

		if (available > fifo_free(&ca->free_inc) ||
		    (available &&
		     !fifo_full(&ca->free[RESERVE_MOVINGGC])))
			break;

		up_read(&c->gc_lock);
		schedule();
		try_to_freeze();
		down_read(&c->gc_lock);
	}

	__set_current_state(TASK_RUNNING);
	ca->allocator_state = ALLOCATOR_RUNNING;
	closure_wake_up(&c->freelist_wait);

	return ret;
}

static bool bch2_can_invalidate_bucket(struct bch_dev *ca,
				       size_t bucket,
				       struct bucket_mark mark)
{
	u8 gc_gen;

	if (!is_available_bucket(mark))
		return false;

	if (ca->buckets_nouse &&
	    test_bit(bucket, ca->buckets_nouse))
		return false;

	gc_gen = bucket_gc_gen(ca, bucket);

	if (gc_gen >= BUCKET_GC_GEN_MAX / 2)
		ca->inc_gen_needs_gc++;

	if (gc_gen >= BUCKET_GC_GEN_MAX)
		ca->inc_gen_really_needs_gc++;

	return gc_gen < BUCKET_GC_GEN_MAX;
}

/*
 * Determines what order we're going to reuse buckets, smallest bucket_key()
 * first.
 *
 *
 * - We take into account the read prio of the bucket, which gives us an
 *   indication of how hot the data is -- we scale the prio so that the prio
 *   farthest from the clock is worth 1/8th of the closest.
 *
 * - The number of sectors of cached data in the bucket, which gives us an
 *   indication of the cost in cache misses this eviction will cause.
 *
 * - If hotness * sectors used compares equal, we pick the bucket with the
 *   smallest bucket_gc_gen() - since incrementing the same bucket's generation
 *   number repeatedly forces us to run mark and sweep gc to avoid generation
 *   number wraparound.
 */

static unsigned long bucket_sort_key(struct bch_fs *c, struct bch_dev *ca,
				     size_t b, struct bucket_mark m)
{
	unsigned last_io = bucket_last_io(c, bucket(ca, b), READ);
	unsigned max_last_io = ca->max_last_bucket_io[READ];

	/*
	 * Time since last read, scaled to [0, 8) where larger value indicates
	 * more recently read data:
	 */
	unsigned long hotness = (max_last_io - last_io) * 7 / max_last_io;

	/* How much we want to keep the data in this bucket: */
	unsigned long data_wantness =
		(hotness + 1) * bucket_sectors_used(m);

	unsigned long needs_journal_commit =
		bucket_needs_journal_commit(m, c->journal.last_seq_ondisk);

	return  (data_wantness << 9) |
		(needs_journal_commit << 8) |
		(bucket_gc_gen(ca, b) / 16);
}

static inline int bucket_alloc_cmp(alloc_heap *h,
				   struct alloc_heap_entry l,
				   struct alloc_heap_entry r)
{
	return  cmp_int(l.key, r.key) ?:
		cmp_int(r.nr, l.nr) ?:
		cmp_int(l.bucket, r.bucket);
}

static inline int bucket_idx_cmp(const void *_l, const void *_r)
{
	const struct alloc_heap_entry *l = _l, *r = _r;

	return cmp_int(l->bucket, r->bucket);
}

static void find_reclaimable_buckets_lru(struct bch_fs *c, struct bch_dev *ca)
{
	struct bucket_array *buckets;
	struct alloc_heap_entry e = { 0 };
	size_t b, i, nr = 0;

	ca->alloc_heap.used = 0;

	mutex_lock(&c->bucket_clock[READ].lock);
	down_read(&ca->bucket_lock);

	buckets = bucket_array(ca);

	bch2_recalc_oldest_io(c, ca, READ);

	/*
	 * Find buckets with lowest read priority, by building a maxheap sorted
	 * by read priority and repeatedly replacing the maximum element until
	 * all buckets have been visited.
	 */
	for (b = ca->mi.first_bucket; b < ca->mi.nbuckets; b++) {
		struct bucket_mark m = READ_ONCE(buckets->b[b].mark);
		unsigned long key = bucket_sort_key(c, ca, b, m);

		if (!bch2_can_invalidate_bucket(ca, b, m))
			continue;

		if (e.nr && e.bucket + e.nr == b && e.key == key) {
			e.nr++;
		} else {
			if (e.nr)
				heap_add_or_replace(&ca->alloc_heap, e,
					-bucket_alloc_cmp, NULL);

			e = (struct alloc_heap_entry) {
				.bucket = b,
				.nr	= 1,
				.key	= key,
			};
		}

		cond_resched();
	}

	if (e.nr)
		heap_add_or_replace(&ca->alloc_heap, e,
				-bucket_alloc_cmp, NULL);

	for (i = 0; i < ca->alloc_heap.used; i++)
		nr += ca->alloc_heap.data[i].nr;

	while (nr - ca->alloc_heap.data[0].nr >= ALLOC_SCAN_BATCH(ca)) {
		nr -= ca->alloc_heap.data[0].nr;
		heap_pop(&ca->alloc_heap, e, -bucket_alloc_cmp, NULL);
	}

	up_read(&ca->bucket_lock);
	mutex_unlock(&c->bucket_clock[READ].lock);
}

static void find_reclaimable_buckets_fifo(struct bch_fs *c, struct bch_dev *ca)
{
	struct bucket_array *buckets = bucket_array(ca);
	struct bucket_mark m;
	size_t b, start;

	if (ca->fifo_last_bucket <  ca->mi.first_bucket ||
	    ca->fifo_last_bucket >= ca->mi.nbuckets)
		ca->fifo_last_bucket = ca->mi.first_bucket;

	start = ca->fifo_last_bucket;

	do {
		ca->fifo_last_bucket++;
		if (ca->fifo_last_bucket == ca->mi.nbuckets)
			ca->fifo_last_bucket = ca->mi.first_bucket;

		b = ca->fifo_last_bucket;
		m = READ_ONCE(buckets->b[b].mark);

		if (bch2_can_invalidate_bucket(ca, b, m)) {
			struct alloc_heap_entry e = { .bucket = b, .nr = 1, };

			heap_add(&ca->alloc_heap, e, bucket_alloc_cmp, NULL);
			if (heap_full(&ca->alloc_heap))
				break;
		}

		cond_resched();
	} while (ca->fifo_last_bucket != start);
}

static void find_reclaimable_buckets_random(struct bch_fs *c, struct bch_dev *ca)
{
	struct bucket_array *buckets = bucket_array(ca);
	struct bucket_mark m;
	size_t checked, i;

	for (checked = 0;
	     checked < ca->mi.nbuckets / 2;
	     checked++) {
		size_t b = bch2_rand_range(ca->mi.nbuckets -
					   ca->mi.first_bucket) +
			ca->mi.first_bucket;

		m = READ_ONCE(buckets->b[b].mark);

		if (bch2_can_invalidate_bucket(ca, b, m)) {
			struct alloc_heap_entry e = { .bucket = b, .nr = 1, };

			heap_add(&ca->alloc_heap, e, bucket_alloc_cmp, NULL);
			if (heap_full(&ca->alloc_heap))
				break;
		}

		cond_resched();
	}

	sort(ca->alloc_heap.data,
	     ca->alloc_heap.used,
	     sizeof(ca->alloc_heap.data[0]),
	     bucket_idx_cmp, NULL);

	/* remove duplicates: */
	for (i = 0; i + 1 < ca->alloc_heap.used; i++)
		if (ca->alloc_heap.data[i].bucket ==
		    ca->alloc_heap.data[i + 1].bucket)
			ca->alloc_heap.data[i].nr = 0;
}

static size_t find_reclaimable_buckets(struct bch_fs *c, struct bch_dev *ca)
{
	size_t i, nr = 0;

	ca->inc_gen_needs_gc			= 0;

	switch (ca->mi.replacement) {
	case CACHE_REPLACEMENT_LRU:
		find_reclaimable_buckets_lru(c, ca);
		break;
	case CACHE_REPLACEMENT_FIFO:
		find_reclaimable_buckets_fifo(c, ca);
		break;
	case CACHE_REPLACEMENT_RANDOM:
		find_reclaimable_buckets_random(c, ca);
		break;
	}

	heap_resort(&ca->alloc_heap, bucket_alloc_cmp, NULL);

	for (i = 0; i < ca->alloc_heap.used; i++)
		nr += ca->alloc_heap.data[i].nr;

	return nr;
}

static inline long next_alloc_bucket(struct bch_dev *ca)
{
	struct alloc_heap_entry e, *top = ca->alloc_heap.data;

	while (ca->alloc_heap.used) {
		if (top->nr) {
			size_t b = top->bucket;

			top->bucket++;
			top->nr--;
			return b;
		}

		heap_pop(&ca->alloc_heap, e, bucket_alloc_cmp, NULL);
	}

	return -1;
}

/*
 * returns sequence number of most recent journal entry that updated this
 * bucket:
 */
static u64 bucket_journal_seq(struct bch_fs *c, struct bucket_mark m)
{
	if (m.journal_seq_valid) {
		u64 journal_seq = atomic64_read(&c->journal.seq);
		u64 bucket_seq	= journal_seq;

		bucket_seq &= ~((u64) U16_MAX);
		bucket_seq |= m.journal_seq;

		if (bucket_seq > journal_seq)
			bucket_seq -= 1 << 16;

		return bucket_seq;
	} else {
		return 0;
	}
}

static int bch2_invalidate_one_bucket2(struct btree_trans *trans,
				       struct bch_dev *ca,
				       struct btree_iter *iter,
				       u64 *journal_seq, unsigned flags)
{
#if 0
	__BKEY_PADDED(k, BKEY_ALLOC_VAL_U64s_MAX) alloc_key;
#else
	/* hack: */
	__BKEY_PADDED(k, 8) alloc_key;
#endif
	struct bch_fs *c = trans->c;
	struct bkey_i_alloc *a;
	struct bkey_alloc_unpacked u;
	struct bucket *g;
	struct bucket_mark m;
	bool invalidating_cached_data;
	size_t b;
	int ret = 0;

	BUG_ON(!ca->alloc_heap.used ||
	       !ca->alloc_heap.data[0].nr);
	b = ca->alloc_heap.data[0].bucket;

	/* first, put on free_inc and mark as owned by allocator: */
	percpu_down_read(&c->mark_lock);
	spin_lock(&c->freelist_lock);

	verify_not_on_freelist(c, ca, b);

	BUG_ON(!fifo_push(&ca->free_inc, b));

	g = bucket(ca, b);
	m = READ_ONCE(g->mark);

	invalidating_cached_data = m.cached_sectors != 0;

	/*
	 * If we're not invalidating cached data, we only increment the bucket
	 * gen in memory here, the incremented gen will be updated in the btree
	 * by bch2_trans_mark_pointer():
	 */

	if (!invalidating_cached_data)
		bch2_invalidate_bucket(c, ca, b, &m);
	else
		bch2_mark_alloc_bucket(c, ca, b, true, gc_pos_alloc(c, NULL), 0);

	spin_unlock(&c->freelist_lock);
	percpu_up_read(&c->mark_lock);

	if (!invalidating_cached_data)
		goto out;

	/*
	 * If the read-only path is trying to shut down, we can't be generating
	 * new btree updates:
	 */
	if (test_bit(BCH_FS_ALLOCATOR_STOPPING, &c->flags)) {
		ret = 1;
		goto out;
	}

	BUG_ON(BKEY_ALLOC_VAL_U64s_MAX > 8);

	bch2_btree_iter_set_pos(iter, POS(ca->dev_idx, b));
retry:
	ret = bch2_btree_iter_traverse(iter);
	if (ret)
		return ret;

	percpu_down_read(&c->mark_lock);
	g = bucket(ca, iter->pos.offset);
	m = READ_ONCE(g->mark);
	u = alloc_mem_to_key(g, m);

	percpu_up_read(&c->mark_lock);

	invalidating_cached_data = u.cached_sectors != 0;

	u.gen++;
	u.data_type	= 0;
	u.dirty_sectors	= 0;
	u.cached_sectors = 0;
	u.read_time	= c->bucket_clock[READ].hand;
	u.write_time	= c->bucket_clock[WRITE].hand;

	a = bkey_alloc_init(&alloc_key.k);
	a->k.p = iter->pos;
	bch2_alloc_pack(a, u);

	bch2_trans_update(trans, iter, &a->k_i,
			  BTREE_TRIGGER_BUCKET_INVALIDATE);

	/*
	 * XXX:
	 * when using deferred btree updates, we have journal reclaim doing
	 * btree updates and thus requiring the allocator to make forward
	 * progress, and here the allocator is requiring space in the journal -
	 * so we need a journal pre-reservation:
	 */
	ret = bch2_trans_commit(trans, NULL,
				invalidating_cached_data ? journal_seq : NULL,
				BTREE_INSERT_NOUNLOCK|
				BTREE_INSERT_NOCHECK_RW|
				BTREE_INSERT_NOFAIL|
				BTREE_INSERT_JOURNAL_RESERVED|
				flags);
	if (ret == -EINTR)
		goto retry;
out:
	if (!ret) {
		/* remove from alloc_heap: */
		struct alloc_heap_entry e, *top = ca->alloc_heap.data;

		top->bucket++;
		top->nr--;

		if (!top->nr)
			heap_pop(&ca->alloc_heap, e, bucket_alloc_cmp, NULL);

		/*
		 * Make sure we flush the last journal entry that updated this
		 * bucket (i.e. deleting the last reference) before writing to
		 * this bucket again:
		 */
		*journal_seq = max(*journal_seq, bucket_journal_seq(c, m));
	} else {
		size_t b2;

		/* remove from free_inc: */
		percpu_down_read(&c->mark_lock);
		spin_lock(&c->freelist_lock);

		bch2_mark_alloc_bucket(c, ca, b, false,
				       gc_pos_alloc(c, NULL), 0);

		BUG_ON(!fifo_pop_back(&ca->free_inc, b2));
		BUG_ON(b != b2);

		spin_unlock(&c->freelist_lock);
		percpu_up_read(&c->mark_lock);
	}

	return ret < 0 ? ret : 0;
}

/*
 * Pull buckets off ca->alloc_heap, invalidate them, move them to ca->free_inc:
 */
static int bch2_invalidate_buckets(struct bch_fs *c, struct bch_dev *ca)
{
	struct btree_trans trans;
	struct btree_iter *iter;
	u64 journal_seq = 0;
	int ret = 0;

	bch2_trans_init(&trans, c, 0, 0);

	iter = bch2_trans_get_iter(&trans, BTREE_ID_ALLOC,
				   POS(ca->dev_idx, 0),
				   BTREE_ITER_CACHED|
				   BTREE_ITER_CACHED_NOFILL|
				   BTREE_ITER_INTENT);

	/* Only use nowait if we've already invalidated at least one bucket: */
	while (!ret &&
	       !fifo_full(&ca->free_inc) &&
	       ca->alloc_heap.used)
		ret = bch2_invalidate_one_bucket2(&trans, ca, iter, &journal_seq,
				BTREE_INSERT_GC_LOCK_HELD|
				(!fifo_empty(&ca->free_inc)
				 ? BTREE_INSERT_NOWAIT : 0));

	bch2_trans_exit(&trans);

	/* If we used NOWAIT, don't return the error: */
	if (!fifo_empty(&ca->free_inc))
		ret = 0;
	if (ret) {
		bch_err(ca, "error invalidating buckets: %i", ret);
		return ret;
	}

	if (journal_seq)
		ret = bch2_journal_flush_seq(&c->journal, journal_seq);
	if (ret) {
		bch_err(ca, "journal error: %i", ret);
		return ret;
	}

	return 0;
}

static int push_invalidated_bucket(struct bch_fs *c, struct bch_dev *ca, size_t bucket)
{
	unsigned i;
	int ret = 0;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);

		spin_lock(&c->freelist_lock);
		for (i = 0; i < RESERVE_NR; i++) {

			/*
			 * Don't strand buckets on the copygc freelist until
			 * after recovery is finished:
			 */
			if (!test_bit(BCH_FS_STARTED, &c->flags) &&
			    i == RESERVE_MOVINGGC)
				continue;

			if (fifo_push(&ca->free[i], bucket)) {
				fifo_pop(&ca->free_inc, bucket);

				closure_wake_up(&c->freelist_wait);
				ca->allocator_state = ALLOCATOR_RUNNING;

				spin_unlock(&c->freelist_lock);
				goto out;
			}
		}

		if (ca->allocator_state != ALLOCATOR_BLOCKED_FULL) {
			ca->allocator_state = ALLOCATOR_BLOCKED_FULL;
			closure_wake_up(&c->freelist_wait);
		}

		spin_unlock(&c->freelist_lock);

		if ((current->flags & PF_KTHREAD) &&
		    kthread_should_stop()) {
			ret = 1;
			break;
		}

		schedule();
		try_to_freeze();
	}
out:
	__set_current_state(TASK_RUNNING);
	return ret;
}

/*
 * Pulls buckets off free_inc, discards them (if enabled), then adds them to
 * freelists, waiting until there's room if necessary:
 */
static int discard_invalidated_buckets(struct bch_fs *c, struct bch_dev *ca)
{
	while (!fifo_empty(&ca->free_inc)) {
		size_t bucket = fifo_peek(&ca->free_inc);

		if (ca->mi.discard &&
		    bdev_max_discard_sectors(ca->disk_sb.bdev))
			blkdev_issue_discard(ca->disk_sb.bdev,
					     bucket_to_sector(ca, bucket),
					     ca->mi.bucket_size, GFP_NOIO);

		if (push_invalidated_bucket(c, ca, bucket))
			return 1;
	}

	return 0;
}

/**
 * bch_allocator_thread - move buckets from free_inc to reserves
 *
 * The free_inc FIFO is populated by find_reclaimable_buckets(), and
 * the reserves are depleted by bucket allocation. When we run out
 * of free_inc, try to invalidate some buckets and write out
 * prios and gens.
 */
static int bch2_allocator_thread(void *arg)
{
	struct bch_dev *ca = arg;
	struct bch_fs *c = ca->fs;
	size_t nr;
	int ret;

	set_freezable();
	ca->allocator_state = ALLOCATOR_RUNNING;

	while (1) {
		cond_resched();
		if (kthread_should_stop())
			break;

		pr_debug("discarding %zu invalidated buckets",
			 fifo_used(&ca->free_inc));

		ret = discard_invalidated_buckets(c, ca);
		if (ret)
			goto stop;

		down_read(&c->gc_lock);

		ret = bch2_invalidate_buckets(c, ca);
		if (ret) {
			up_read(&c->gc_lock);
			goto stop;
		}

		if (!fifo_empty(&ca->free_inc)) {
			up_read(&c->gc_lock);
			continue;
		}

		pr_debug("free_inc now empty");

		do {
			/*
			 * Find some buckets that we can invalidate, either
			 * they're completely unused, or only contain clean data
			 * that's been written back to the backing device or
			 * another cache tier
			 */

			pr_debug("scanning for reclaimable buckets");

			nr = find_reclaimable_buckets(c, ca);

			pr_debug("found %zu buckets", nr);

			trace_alloc_batch(ca, nr, ca->alloc_heap.size);

			if ((ca->inc_gen_needs_gc >= ALLOC_SCAN_BATCH(ca) ||
			     ca->inc_gen_really_needs_gc) &&
			    c->gc_thread) {
				atomic_inc(&c->kick_gc);
				wake_up_process(c->gc_thread);
			}

			/*
			 * If we found any buckets, we have to invalidate them
			 * before we scan for more - but if we didn't find very
			 * many we may want to wait on more buckets being
			 * available so we don't spin:
			 */
			if (!nr ||
			    (nr < ALLOC_SCAN_BATCH(ca) &&
			     !fifo_empty(&ca->free[RESERVE_NONE]))) {
				ret = wait_buckets_available(c, ca);
				if (ret) {
					up_read(&c->gc_lock);
					goto stop;
				}
			}
		} while (!nr);

		up_read(&c->gc_lock);

		pr_debug("%zu buckets to invalidate", nr);

		/*
		 * alloc_heap is now full of newly-invalidated buckets: next,
		 * write out the new bucket gens:
		 */
	}

stop:
	pr_debug("alloc thread stopping (ret %i)", ret);
	ca->allocator_state = ALLOCATOR_STOPPED;
	closure_wake_up(&c->freelist_wait);
	return 0;
}

/* Startup/shutdown (ro/rw): */

void bch2_recalc_capacity(struct bch_fs *c)
{
	struct bch_dev *ca;
	u64 capacity = 0, reserved_sectors = 0, gc_reserve, copygc_threshold = 0;
	unsigned bucket_size_max = 0;
	unsigned long ra_pages = 0;
	unsigned i, j;

	lockdep_assert_held(&c->state_lock);

	for_each_online_member(ca, c, i) {
		struct backing_dev_info *bdi = ca->disk_sb.bdev->bd_disk->bdi;

		ra_pages += bdi->ra_pages;
	}

	bch2_set_ra_pages(c, ra_pages);

	for_each_rw_member(ca, c, i) {
		u64 dev_reserve = 0;

		/*
		 * We need to reserve buckets (from the number
		 * of currently available buckets) against
		 * foreground writes so that mainly copygc can
		 * make forward progress.
		 *
		 * We need enough to refill the various reserves
		 * from scratch - copygc will use its entire
		 * reserve all at once, then run against when
		 * its reserve is refilled (from the formerly
		 * available buckets).
		 *
		 * This reserve is just used when considering if
		 * allocations for foreground writes must wait -
		 * not -ENOSPC calculations.
		 */
		for (j = 0; j < RESERVE_NONE; j++)
			dev_reserve += ca->free[j].size;

		dev_reserve += 1;	/* btree write point */
		dev_reserve += 1;	/* copygc write point */
		dev_reserve += 1;	/* rebalance write point */

		dev_reserve *= ca->mi.bucket_size;

		copygc_threshold += dev_reserve;

		capacity += bucket_to_sector(ca, ca->mi.nbuckets -
					     ca->mi.first_bucket);

		reserved_sectors += dev_reserve * 2;

		bucket_size_max = max_t(unsigned, bucket_size_max,
					ca->mi.bucket_size);
	}

	gc_reserve = c->opts.gc_reserve_bytes
		? c->opts.gc_reserve_bytes >> 9
		: div64_u64(capacity * c->opts.gc_reserve_percent, 100);

	reserved_sectors = max(gc_reserve, reserved_sectors);

	reserved_sectors = min(reserved_sectors, capacity);

	c->copygc_threshold = copygc_threshold;
	c->capacity = capacity - reserved_sectors;

	c->bucket_size_max = bucket_size_max;

	/* Wake up case someone was waiting for buckets */
	closure_wake_up(&c->freelist_wait);
}

static bool bch2_dev_has_open_write_point(struct bch_fs *c, struct bch_dev *ca)
{
	struct open_bucket *ob;
	bool ret = false;

	for (ob = c->open_buckets;
	     ob < c->open_buckets + ARRAY_SIZE(c->open_buckets);
	     ob++) {
		spin_lock(&ob->lock);
		if (ob->valid && !ob->on_partial_list &&
		    ob->ptr.dev == ca->dev_idx)
			ret = true;
		spin_unlock(&ob->lock);
	}

	return ret;
}

/* device goes ro: */
void bch2_dev_allocator_remove(struct bch_fs *c, struct bch_dev *ca)
{
	unsigned i;

	BUG_ON(ca->alloc_thread);

	/* First, remove device from allocation groups: */

	for (i = 0; i < ARRAY_SIZE(c->rw_devs); i++)
		clear_bit(ca->dev_idx, c->rw_devs[i].d);

	/*
	 * Capacity is calculated based off of devices in allocation groups:
	 */
	bch2_recalc_capacity(c);

	/* Next, close write points that point to this device... */
	for (i = 0; i < ARRAY_SIZE(c->write_points); i++)
		bch2_writepoint_stop(c, ca, &c->write_points[i]);

	bch2_writepoint_stop(c, ca, &c->copygc_write_point);
	bch2_writepoint_stop(c, ca, &c->rebalance_write_point);
	bch2_writepoint_stop(c, ca, &c->btree_write_point);

	mutex_lock(&c->btree_reserve_cache_lock);
	while (c->btree_reserve_cache_nr) {
		struct btree_alloc *a =
			&c->btree_reserve_cache[--c->btree_reserve_cache_nr];

		bch2_open_buckets_put(c, &a->ob);
	}
	mutex_unlock(&c->btree_reserve_cache_lock);

	while (1) {
		struct open_bucket *ob;

		spin_lock(&c->freelist_lock);
		if (!ca->open_buckets_partial_nr) {
			spin_unlock(&c->freelist_lock);
			break;
		}
		ob = c->open_buckets +
			ca->open_buckets_partial[--ca->open_buckets_partial_nr];
		ob->on_partial_list = false;
		spin_unlock(&c->freelist_lock);

		bch2_open_bucket_put(c, ob);
	}

	bch2_ec_stop_dev(c, ca);

	/*
	 * Wake up threads that were blocked on allocation, so they can notice
	 * the device can no longer be removed and the capacity has changed:
	 */
	closure_wake_up(&c->freelist_wait);

	/*
	 * journal_res_get() can block waiting for free space in the journal -
	 * it needs to notice there may not be devices to allocate from anymore:
	 */
	wake_up(&c->journal.wait);

	/* Now wait for any in flight writes: */

	closure_wait_event(&c->open_buckets_wait,
			   !bch2_dev_has_open_write_point(c, ca));
}

/* device goes rw: */
void bch2_dev_allocator_add(struct bch_fs *c, struct bch_dev *ca)
{
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(c->rw_devs); i++)
		if (ca->mi.data_allowed & (1 << i))
			set_bit(ca->dev_idx, c->rw_devs[i].d);
}

void bch2_dev_allocator_quiesce(struct bch_fs *c, struct bch_dev *ca)
{
	if (ca->alloc_thread)
		closure_wait_event(&c->freelist_wait,
				   ca->allocator_state != ALLOCATOR_RUNNING);
}

/* stop allocator thread: */
void bch2_dev_allocator_stop(struct bch_dev *ca)
{
	struct task_struct *p;

	p = rcu_dereference_protected(ca->alloc_thread, 1);
	ca->alloc_thread = NULL;

	/*
	 * We need an rcu barrier between setting ca->alloc_thread = NULL and
	 * the thread shutting down to avoid bch2_wake_allocator() racing:
	 *
	 * XXX: it would be better to have the rcu barrier be asynchronous
	 * instead of blocking us here
	 */
	synchronize_rcu();

	if (p) {
		kthread_stop(p);
		put_task_struct(p);
	}
}

/* start allocator thread: */
int bch2_dev_allocator_start(struct bch_dev *ca)
{
	struct task_struct *p;

	/*
	 * allocator thread already started?
	 */
	if (ca->alloc_thread)
		return 0;

	p = kthread_create(bch2_allocator_thread, ca,
			   "bch-alloc/%s", ca->name);
	if (IS_ERR(p))
		return PTR_ERR(p);

	get_task_struct(p);
	rcu_assign_pointer(ca->alloc_thread, p);
	wake_up_process(p);
	return 0;
}

void bch2_fs_allocator_background_init(struct bch_fs *c)
{
	spin_lock_init(&c->freelist_lock);
	bch2_bucket_clock_init(c, READ);
	bch2_bucket_clock_init(c, WRITE);

	c->pd_controllers_update_seconds = 5;
	INIT_DELAYED_WORK(&c->pd_controllers_update, pd_controllers_update);
}
