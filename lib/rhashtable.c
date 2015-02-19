/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2014-2015 Thomas Graf <tgraf@suug.ch>
 * Copyright (c) 2008-2014 Patrick McHardy <kaber@trash.net>
 *
 * Based on the following paper:
 * https://www.usenix.org/legacy/event/atc11/tech/final_files/Triplett.pdf
 *
 * Code partially derived from nft_hash
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/jhash.h>
#include <linux/random.h>
#include <linux/rhashtable.h>
#include <linux/err.h>

#define HASH_DEFAULT_SIZE	64UL
#define HASH_MIN_SIZE		4UL
#define BUCKET_LOCKS_PER_CPU   128UL

/* Base bits plus 1 bit for nulls marker */
#define HASH_RESERVED_SPACE	(RHT_BASE_BITS + 1)

enum {
	RHT_LOCK_NORMAL,
	RHT_LOCK_NESTED,
};

/* The bucket lock is selected based on the hash and protects mutations
 * on a group of hash buckets.
 *
 * A maximum of tbl->size/2 bucket locks is allocated. This ensures that
 * a single lock always covers both buckets which may both contains
 * entries which link to the same bucket of the old table during resizing.
 * This allows to simplify the locking as locking the bucket in both
 * tables during resize always guarantee protection.
 *
 * IMPORTANT: When holding the bucket lock of both the old and new table
 * during expansions and shrinking, the old bucket lock must always be
 * acquired first.
 */
static spinlock_t *bucket_lock(const struct bucket_table *tbl, u32 hash)
{
	return &tbl->locks[hash & tbl->locks_mask];
}

static void *rht_obj(const struct rhashtable *ht, const struct rhash_head *he)
{
	return (void *) he - ht->p.head_offset;
}

static u32 rht_bucket_index(const struct bucket_table *tbl, u32 hash)
{
	return hash & (tbl->size - 1);
}

static u32 obj_raw_hashfn(const struct rhashtable *ht, const void *ptr)
{
	u32 hash;

	if (unlikely(!ht->p.key_len))
		hash = ht->p.obj_hashfn(ptr, ht->p.hash_rnd);
	else
		hash = ht->p.hashfn(ptr + ht->p.key_offset, ht->p.key_len,
				    ht->p.hash_rnd);

	return hash >> HASH_RESERVED_SPACE;
}

static u32 key_hashfn(struct rhashtable *ht, const void *key, u32 len)
{
	return ht->p.hashfn(key, len, ht->p.hash_rnd) >> HASH_RESERVED_SPACE;
}

static u32 head_hashfn(const struct rhashtable *ht,
		       const struct bucket_table *tbl,
		       const struct rhash_head *he)
{
	return rht_bucket_index(tbl, obj_raw_hashfn(ht, rht_obj(ht, he)));
}

#ifdef CONFIG_PROVE_LOCKING
static void debug_dump_buckets(const struct rhashtable *ht,
			       const struct bucket_table *tbl)
{
	struct rhash_head *he;
	unsigned int i, hash;

	for (i = 0; i < tbl->size; i++) {
		pr_warn(" [Bucket %d] ", i);
		rht_for_each_rcu(he, tbl, i) {
			hash = head_hashfn(ht, tbl, he);
			pr_cont("[hash = %#x, lock = %p] ",
				hash, bucket_lock(tbl, hash));
		}
		pr_cont("\n");
	}

}

static void debug_dump_table(struct rhashtable *ht,
			     const struct bucket_table *tbl,
			     unsigned int hash)
{
	struct bucket_table *old_tbl, *future_tbl;

	pr_emerg("BUG: lock for hash %#x in table %p not held\n",
		 hash, tbl);

	rcu_read_lock();
	future_tbl = rht_dereference_rcu(ht->future_tbl, ht);
	old_tbl = rht_dereference_rcu(ht->tbl, ht);
	if (future_tbl != old_tbl) {
		pr_warn("Future table %p (size: %zd)\n",
			future_tbl, future_tbl->size);
		debug_dump_buckets(ht, future_tbl);
	}

	pr_warn("Table %p (size: %zd)\n", old_tbl, old_tbl->size);
	debug_dump_buckets(ht, old_tbl);

	rcu_read_unlock();
}

#define ASSERT_RHT_MUTEX(HT) BUG_ON(!lockdep_rht_mutex_is_held(HT))
#define ASSERT_BUCKET_LOCK(HT, TBL, HASH)				\
	do {								\
		if (unlikely(!lockdep_rht_bucket_is_held(TBL, HASH))) {	\
			debug_dump_table(HT, TBL, HASH);		\
			BUG();						\
		}							\
	} while (0)

int lockdep_rht_mutex_is_held(struct rhashtable *ht)
{
	return (debug_locks) ? lockdep_is_held(&ht->mutex) : 1;
}
EXPORT_SYMBOL_GPL(lockdep_rht_mutex_is_held);

int lockdep_rht_bucket_is_held(const struct bucket_table *tbl, u32 hash)
{
	spinlock_t *lock = bucket_lock(tbl, hash);

	return (debug_locks) ? lockdep_is_held(lock) : 1;
}
EXPORT_SYMBOL_GPL(lockdep_rht_bucket_is_held);
#else
#define ASSERT_RHT_MUTEX(HT)
#define ASSERT_BUCKET_LOCK(HT, TBL, HASH)
#endif


static struct rhash_head __rcu **bucket_tail(struct bucket_table *tbl, u32 n)
{
	struct rhash_head __rcu **pprev;

	for (pprev = &tbl->buckets[n];
	     !rht_is_a_nulls(rht_dereference_bucket(*pprev, tbl, n));
	     pprev = &rht_dereference_bucket(*pprev, tbl, n)->next)
		;

	return pprev;
}

static int alloc_bucket_locks(struct rhashtable *ht, struct bucket_table *tbl)
{
	unsigned int i, size;
#if defined(CONFIG_PROVE_LOCKING)
	unsigned int nr_pcpus = 2;
#else
	unsigned int nr_pcpus = num_possible_cpus();
#endif

	nr_pcpus = min_t(unsigned int, nr_pcpus, 32UL);
	size = roundup_pow_of_two(nr_pcpus * ht->p.locks_mul);

	/* Never allocate more than 0.5 locks per bucket */
	size = min_t(unsigned int, size, tbl->size >> 1);

	if (sizeof(spinlock_t) != 0) {
#ifdef CONFIG_NUMA
		if (size * sizeof(spinlock_t) > PAGE_SIZE)
			tbl->locks = vmalloc(size * sizeof(spinlock_t));
		else
#endif
		tbl->locks = kmalloc_array(size, sizeof(spinlock_t),
					   GFP_KERNEL);
		if (!tbl->locks)
			return -ENOMEM;
		for (i = 0; i < size; i++)
			spin_lock_init(&tbl->locks[i]);
	}
	tbl->locks_mask = size - 1;

	return 0;
}

static void bucket_table_free(const struct bucket_table *tbl)
{
	if (tbl)
		kvfree(tbl->locks);

	kvfree(tbl);
}

static struct bucket_table *bucket_table_alloc(struct rhashtable *ht,
					       size_t nbuckets)
{
	struct bucket_table *tbl;
	size_t size;
	int i;

	size = sizeof(*tbl) + nbuckets * sizeof(tbl->buckets[0]);
	tbl = kzalloc(size, GFP_KERNEL | __GFP_NOWARN);
	if (tbl == NULL)
		tbl = vzalloc(size);

	if (tbl == NULL)
		return NULL;

	tbl->size = nbuckets;

	if (alloc_bucket_locks(ht, tbl) < 0) {
		bucket_table_free(tbl);
		return NULL;
	}

	for (i = 0; i < nbuckets; i++)
		INIT_RHT_NULLS_HEAD(tbl->buckets[i], ht, i);

	return tbl;
}

/**
 * rht_grow_above_75 - returns true if nelems > 0.75 * table-size
 * @ht:		hash table
 * @new_size:	new table size
 */
bool rht_grow_above_75(const struct rhashtable *ht, size_t new_size)
{
	/* Expand table when exceeding 75% load */
	return atomic_read(&ht->nelems) > (new_size / 4 * 3) &&
	       (ht->p.max_shift && atomic_read(&ht->shift) < ht->p.max_shift);
}
EXPORT_SYMBOL_GPL(rht_grow_above_75);

/**
 * rht_shrink_below_30 - returns true if nelems < 0.3 * table-size
 * @ht:		hash table
 * @new_size:	new table size
 */
bool rht_shrink_below_30(const struct rhashtable *ht, size_t new_size)
{
	/* Shrink table beneath 30% load */
	return atomic_read(&ht->nelems) < (new_size * 3 / 10) &&
	       (atomic_read(&ht->shift) > ht->p.min_shift);
}
EXPORT_SYMBOL_GPL(rht_shrink_below_30);

static void lock_buckets(struct bucket_table *new_tbl,
			 struct bucket_table *old_tbl, unsigned int hash)
	__acquires(old_bucket_lock)
{
	spin_lock_bh(bucket_lock(old_tbl, hash));
	if (new_tbl != old_tbl)
		spin_lock_bh_nested(bucket_lock(new_tbl, hash),
				    RHT_LOCK_NESTED);
}

static void unlock_buckets(struct bucket_table *new_tbl,
			   struct bucket_table *old_tbl, unsigned int hash)
	__releases(old_bucket_lock)
{
	if (new_tbl != old_tbl)
		spin_unlock_bh(bucket_lock(new_tbl, hash));
	spin_unlock_bh(bucket_lock(old_tbl, hash));
}

/**
 * Unlink entries on bucket which hash to different bucket.
 *
 * Returns true if no more work needs to be performed on the bucket.
 */
static bool hashtable_chain_unzip(struct rhashtable *ht,
				  const struct bucket_table *new_tbl,
				  struct bucket_table *old_tbl,
				  size_t old_hash)
{
	struct rhash_head *he, *p, *next;
	unsigned int new_hash, new_hash2;

	ASSERT_BUCKET_LOCK(ht, old_tbl, old_hash);

	/* Old bucket empty, no work needed. */
	p = rht_dereference_bucket(old_tbl->buckets[old_hash], old_tbl,
				   old_hash);
	if (rht_is_a_nulls(p))
		return false;

	new_hash = head_hashfn(ht, new_tbl, p);
	ASSERT_BUCKET_LOCK(ht, new_tbl, new_hash);

	/* Advance the old bucket pointer one or more times until it
	 * reaches a node that doesn't hash to the same bucket as the
	 * previous node p. Call the previous node p;
	 */
	rht_for_each_continue(he, p->next, old_tbl, old_hash) {
		new_hash2 = head_hashfn(ht, new_tbl, he);
		ASSERT_BUCKET_LOCK(ht, new_tbl, new_hash2);

		if (new_hash != new_hash2)
			break;
		p = he;
	}
	rcu_assign_pointer(old_tbl->buckets[old_hash], p->next);

	/* Find the subsequent node which does hash to the same
	 * bucket as node P, or NULL if no such node exists.
	 */
	INIT_RHT_NULLS_HEAD(next, ht, old_hash);
	if (!rht_is_a_nulls(he)) {
		rht_for_each_continue(he, he->next, old_tbl, old_hash) {
			if (head_hashfn(ht, new_tbl, he) == new_hash) {
				next = he;
				break;
			}
		}
	}

	/* Set p's next pointer to that subsequent node pointer,
	 * bypassing the nodes which do not hash to p's bucket
	 */
	rcu_assign_pointer(p->next, next);

	p = rht_dereference_bucket(old_tbl->buckets[old_hash], old_tbl,
				   old_hash);

	return !rht_is_a_nulls(p);
}

static void link_old_to_new(struct rhashtable *ht, struct bucket_table *new_tbl,
			    unsigned int new_hash, struct rhash_head *entry)
{
	ASSERT_BUCKET_LOCK(ht, new_tbl, new_hash);

	rcu_assign_pointer(*bucket_tail(new_tbl, new_hash), entry);
}

/**
 * rhashtable_expand - Expand hash table while allowing concurrent lookups
 * @ht:		the hash table to expand
 *
 * A secondary bucket array is allocated and the hash entries are migrated
 * while keeping them on both lists until the end of the RCU grace period.
 *
 * This function may only be called in a context where it is safe to call
 * synchronize_rcu(), e.g. not within a rcu_read_lock() section.
 *
 * The caller must ensure that no concurrent resizing occurs by holding
 * ht->mutex.
 *
 * It is valid to have concurrent insertions and deletions protected by per
 * bucket locks or concurrent RCU protected lookups and traversals.
 */
int rhashtable_expand(struct rhashtable *ht)
{
	struct bucket_table *new_tbl, *old_tbl = rht_dereference(ht->tbl, ht);
	struct rhash_head *he;
	unsigned int new_hash, old_hash;
	bool complete = false;

	ASSERT_RHT_MUTEX(ht);

	new_tbl = bucket_table_alloc(ht, old_tbl->size * 2);
	if (new_tbl == NULL)
		return -ENOMEM;

	atomic_inc(&ht->shift);

	/* Make insertions go into the new, empty table right away. Deletions
	 * and lookups will be attempted in both tables until we synchronize.
	 * The synchronize_rcu() guarantees for the new table to be picked up
	 * so no new additions go into the old table while we relink.
	 */
	rcu_assign_pointer(ht->future_tbl, new_tbl);
	synchronize_rcu();

	/* For each new bucket, search the corresponding old bucket for the
	 * first entry that hashes to the new bucket, and link the end of
	 * newly formed bucket chain (containing entries added to future
	 * table) to that entry. Since all the entries which will end up in
	 * the new bucket appear in the same old bucket, this constructs an
	 * entirely valid new hash table, but with multiple buckets
	 * "zipped" together into a single imprecise chain.
	 */
	for (new_hash = 0; new_hash < new_tbl->size; new_hash++) {
		old_hash = rht_bucket_index(old_tbl, new_hash);
		lock_buckets(new_tbl, old_tbl, new_hash);
		rht_for_each(he, old_tbl, old_hash) {
			if (head_hashfn(ht, new_tbl, he) == new_hash) {
				link_old_to_new(ht, new_tbl, new_hash, he);
				break;
			}
		}
		unlock_buckets(new_tbl, old_tbl, new_hash);
	}

	/* Unzip interleaved hash chains */
	while (!complete && !ht->being_destroyed) {
		/* Wait for readers. All new readers will see the new
		 * table, and thus no references to the old table will
		 * remain.
		 */
		synchronize_rcu();

		/* For each bucket in the old table (each of which
		 * contains items from multiple buckets of the new
		 * table): ...
		 */
		complete = true;
		for (old_hash = 0; old_hash < old_tbl->size; old_hash++) {
			lock_buckets(new_tbl, old_tbl, old_hash);

			if (hashtable_chain_unzip(ht, new_tbl, old_tbl,
						  old_hash))
				complete = false;

			unlock_buckets(new_tbl, old_tbl, old_hash);
		}
	}

	rcu_assign_pointer(ht->tbl, new_tbl);
	synchronize_rcu();

	bucket_table_free(old_tbl);
	return 0;
}
EXPORT_SYMBOL_GPL(rhashtable_expand);

/**
 * rhashtable_shrink - Shrink hash table while allowing concurrent lookups
 * @ht:		the hash table to shrink
 *
 * This function may only be called in a context where it is safe to call
 * synchronize_rcu(), e.g. not within a rcu_read_lock() section.
 *
 * The caller must ensure that no concurrent resizing occurs by holding
 * ht->mutex.
 *
 * The caller must ensure that no concurrent table mutations take place.
 * It is however valid to have concurrent lookups if they are RCU protected.
 *
 * It is valid to have concurrent insertions and deletions protected by per
 * bucket locks or concurrent RCU protected lookups and traversals.
 */
int rhashtable_shrink(struct rhashtable *ht)
{
	struct bucket_table *new_tbl, *tbl = rht_dereference(ht->tbl, ht);
	unsigned int new_hash;

	ASSERT_RHT_MUTEX(ht);

	new_tbl = bucket_table_alloc(ht, tbl->size / 2);
	if (new_tbl == NULL)
		return -ENOMEM;

	rcu_assign_pointer(ht->future_tbl, new_tbl);
	synchronize_rcu();

	/* Link the first entry in the old bucket to the end of the
	 * bucket in the new table. As entries are concurrently being
	 * added to the new table, lock down the new bucket. As we
	 * always divide the size in half when shrinking, each bucket
	 * in the new table maps to exactly two buckets in the old
	 * table.
	 */
	for (new_hash = 0; new_hash < new_tbl->size; new_hash++) {
		lock_buckets(new_tbl, tbl, new_hash);

		rcu_assign_pointer(*bucket_tail(new_tbl, new_hash),
				   tbl->buckets[new_hash]);
		ASSERT_BUCKET_LOCK(ht, tbl, new_hash + new_tbl->size);
		rcu_assign_pointer(*bucket_tail(new_tbl, new_hash),
				   tbl->buckets[new_hash + new_tbl->size]);

		unlock_buckets(new_tbl, tbl, new_hash);
	}

	/* Publish the new, valid hash table */
	rcu_assign_pointer(ht->tbl, new_tbl);
	atomic_dec(&ht->shift);

	/* Wait for readers. No new readers will have references to the
	 * old hash table.
	 */
	synchronize_rcu();

	bucket_table_free(tbl);

	return 0;
}
EXPORT_SYMBOL_GPL(rhashtable_shrink);

static void rht_deferred_worker(struct work_struct *work)
{
	struct rhashtable *ht;
	struct bucket_table *tbl;
	struct rhashtable_walker *walker;

	ht = container_of(work, struct rhashtable, run_work);
	mutex_lock(&ht->mutex);
	if (ht->being_destroyed)
		goto unlock;

	tbl = rht_dereference(ht->tbl, ht);

	list_for_each_entry(walker, &ht->walkers, list)
		walker->resize = true;

	if (ht->p.grow_decision && ht->p.grow_decision(ht, tbl->size))
		rhashtable_expand(ht);
	else if (ht->p.shrink_decision && ht->p.shrink_decision(ht, tbl->size))
		rhashtable_shrink(ht);

unlock:
	mutex_unlock(&ht->mutex);
}

static void rhashtable_wakeup_worker(struct rhashtable *ht)
{
	struct bucket_table *tbl = rht_dereference_rcu(ht->tbl, ht);
	struct bucket_table *new_tbl = rht_dereference_rcu(ht->future_tbl, ht);
	size_t size = tbl->size;

	/* Only adjust the table if no resizing is currently in progress. */
	if (tbl == new_tbl &&
	    ((ht->p.grow_decision && ht->p.grow_decision(ht, size)) ||
	     (ht->p.shrink_decision && ht->p.shrink_decision(ht, size))))
		schedule_work(&ht->run_work);
}

static void __rhashtable_insert(struct rhashtable *ht, struct rhash_head *obj,
				struct bucket_table *tbl, u32 hash)
{
	struct rhash_head *head;

	hash = rht_bucket_index(tbl, hash);
	head = rht_dereference_bucket(tbl->buckets[hash], tbl, hash);

	ASSERT_BUCKET_LOCK(ht, tbl, hash);

	if (rht_is_a_nulls(head))
		INIT_RHT_NULLS_HEAD(obj->next, ht, hash);
	else
		RCU_INIT_POINTER(obj->next, head);

	rcu_assign_pointer(tbl->buckets[hash], obj);

	atomic_inc(&ht->nelems);

	rhashtable_wakeup_worker(ht);
}

/**
 * rhashtable_insert - insert object into hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 *
 * Will take a per bucket spinlock to protect against mutual mutations
 * on the same bucket. Multiple insertions may occur in parallel unless
 * they map to the same bucket lock.
 *
 * It is safe to call this function from atomic context.
 *
 * Will trigger an automatic deferred table resizing if the size grows
 * beyond the watermark indicated by grow_decision() which can be passed
 * to rhashtable_init().
 */
void rhashtable_insert(struct rhashtable *ht, struct rhash_head *obj)
{
	struct bucket_table *tbl, *old_tbl;
	unsigned hash;

	rcu_read_lock();

	tbl = rht_dereference_rcu(ht->future_tbl, ht);
	old_tbl = rht_dereference_rcu(ht->tbl, ht);
	hash = obj_raw_hashfn(ht, rht_obj(ht, obj));

	lock_buckets(tbl, old_tbl, hash);
	__rhashtable_insert(ht, obj, tbl, hash);
	unlock_buckets(tbl, old_tbl, hash);

	rcu_read_unlock();
}
EXPORT_SYMBOL_GPL(rhashtable_insert);

/**
 * rhashtable_remove - remove object from hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 *
 * Since the hash chain is single linked, the removal operation needs to
 * walk the bucket chain upon removal. The removal operation is thus
 * considerable slow if the hash table is not correctly sized.
 *
 * Will automatically shrink the table via rhashtable_expand() if the
 * shrink_decision function specified at rhashtable_init() returns true.
 *
 * The caller must ensure that no concurrent table mutations occur. It is
 * however valid to have concurrent lookups if they are RCU protected.
 */
bool rhashtable_remove(struct rhashtable *ht, struct rhash_head *obj)
{
	struct bucket_table *tbl, *new_tbl, *old_tbl;
	struct rhash_head __rcu **pprev;
	struct rhash_head *he, *he2;
	unsigned int hash, new_hash;
	bool ret = false;

	rcu_read_lock();
	old_tbl = rht_dereference_rcu(ht->tbl, ht);
	tbl = new_tbl = rht_dereference_rcu(ht->future_tbl, ht);
	new_hash = obj_raw_hashfn(ht, rht_obj(ht, obj));

	lock_buckets(new_tbl, old_tbl, new_hash);
restart:
	hash = rht_bucket_index(tbl, new_hash);
	pprev = &tbl->buckets[hash];
	rht_for_each(he, tbl, hash) {
		if (he != obj) {
			pprev = &he->next;
			continue;
		}

		ASSERT_BUCKET_LOCK(ht, tbl, hash);

		if (old_tbl->size > new_tbl->size && tbl == old_tbl &&
		    !rht_is_a_nulls(obj->next) &&
		    head_hashfn(ht, tbl, obj->next) != hash) {
			rcu_assign_pointer(*pprev, (struct rhash_head *) rht_marker(ht, hash));
		} else if (unlikely(old_tbl->size < new_tbl->size && tbl == new_tbl)) {
			rht_for_each_continue(he2, obj->next, tbl, hash) {
				if (head_hashfn(ht, tbl, he2) == hash) {
					rcu_assign_pointer(*pprev, he2);
					goto found;
				}
			}

			rcu_assign_pointer(*pprev, (struct rhash_head *) rht_marker(ht, hash));
		} else {
			rcu_assign_pointer(*pprev, obj->next);
		}

found:
		ret = true;
		break;
	}

	/* The entry may be linked in either 'tbl', 'future_tbl', or both.
	 * 'future_tbl' only exists for a short period of time during
	 * resizing. Thus traversing both is fine and the added cost is
	 * very rare.
	 */
	if (tbl != old_tbl) {
		tbl = old_tbl;
		goto restart;
	}

	unlock_buckets(new_tbl, old_tbl, new_hash);

	if (ret) {
		atomic_dec(&ht->nelems);
		rhashtable_wakeup_worker(ht);
	}

	rcu_read_unlock();

	return ret;
}
EXPORT_SYMBOL_GPL(rhashtable_remove);

struct rhashtable_compare_arg {
	struct rhashtable *ht;
	const void *key;
};

static bool rhashtable_compare(void *ptr, void *arg)
{
	struct rhashtable_compare_arg *x = arg;
	struct rhashtable *ht = x->ht;

	return !memcmp(ptr + ht->p.key_offset, x->key, ht->p.key_len);
}

/**
 * rhashtable_lookup - lookup key in hash table
 * @ht:		hash table
 * @key:	pointer to key
 *
 * Computes the hash value for the key and traverses the bucket chain looking
 * for a entry with an identical key. The first matching entry is returned.
 *
 * This lookup function may only be used for fixed key hash table (key_len
 * parameter set). It will BUG() if used inappropriately.
 *
 * Lookups may occur in parallel with hashtable mutations and resizing.
 */
void *rhashtable_lookup(struct rhashtable *ht, const void *key)
{
	struct rhashtable_compare_arg arg = {
		.ht = ht,
		.key = key,
	};

	BUG_ON(!ht->p.key_len);

	return rhashtable_lookup_compare(ht, key, &rhashtable_compare, &arg);
}
EXPORT_SYMBOL_GPL(rhashtable_lookup);

/**
 * rhashtable_lookup_compare - search hash table with compare function
 * @ht:		hash table
 * @key:	the pointer to the key
 * @compare:	compare function, must return true on match
 * @arg:	argument passed on to compare function
 *
 * Traverses the bucket chain behind the provided hash value and calls the
 * specified compare function for each entry.
 *
 * Lookups may occur in parallel with hashtable mutations and resizing.
 *
 * Returns the first entry on which the compare function returned true.
 */
void *rhashtable_lookup_compare(struct rhashtable *ht, const void *key,
				bool (*compare)(void *, void *), void *arg)
{
	const struct bucket_table *tbl, *old_tbl;
	struct rhash_head *he;
	u32 hash;

	rcu_read_lock();

	old_tbl = rht_dereference_rcu(ht->tbl, ht);
	tbl = rht_dereference_rcu(ht->future_tbl, ht);
	hash = key_hashfn(ht, key, ht->p.key_len);
restart:
	rht_for_each_rcu(he, tbl, rht_bucket_index(tbl, hash)) {
		if (!compare(rht_obj(ht, he), arg))
			continue;
		rcu_read_unlock();
		return rht_obj(ht, he);
	}

	if (unlikely(tbl != old_tbl)) {
		tbl = old_tbl;
		goto restart;
	}
	rcu_read_unlock();

	return NULL;
}
EXPORT_SYMBOL_GPL(rhashtable_lookup_compare);

/**
 * rhashtable_lookup_insert - lookup and insert object into hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 *
 * Locks down the bucket chain in both the old and new table if a resize
 * is in progress to ensure that writers can't remove from the old table
 * and can't insert to the new table during the atomic operation of search
 * and insertion. Searches for duplicates in both the old and new table if
 * a resize is in progress.
 *
 * This lookup function may only be used for fixed key hash table (key_len
 * parameter set). It will BUG() if used inappropriately.
 *
 * It is safe to call this function from atomic context.
 *
 * Will trigger an automatic deferred table resizing if the size grows
 * beyond the watermark indicated by grow_decision() which can be passed
 * to rhashtable_init().
 */
bool rhashtable_lookup_insert(struct rhashtable *ht, struct rhash_head *obj)
{
	struct rhashtable_compare_arg arg = {
		.ht = ht,
		.key = rht_obj(ht, obj) + ht->p.key_offset,
	};

	BUG_ON(!ht->p.key_len);

	return rhashtable_lookup_compare_insert(ht, obj, &rhashtable_compare,
						&arg);
}
EXPORT_SYMBOL_GPL(rhashtable_lookup_insert);

/**
 * rhashtable_lookup_compare_insert - search and insert object to hash table
 *                                    with compare function
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 * @compare:	compare function, must return true on match
 * @arg:	argument passed on to compare function
 *
 * Locks down the bucket chain in both the old and new table if a resize
 * is in progress to ensure that writers can't remove from the old table
 * and can't insert to the new table during the atomic operation of search
 * and insertion. Searches for duplicates in both the old and new table if
 * a resize is in progress.
 *
 * Lookups may occur in parallel with hashtable mutations and resizing.
 *
 * Will trigger an automatic deferred table resizing if the size grows
 * beyond the watermark indicated by grow_decision() which can be passed
 * to rhashtable_init().
 */
bool rhashtable_lookup_compare_insert(struct rhashtable *ht,
				      struct rhash_head *obj,
				      bool (*compare)(void *, void *),
				      void *arg)
{
	struct bucket_table *new_tbl, *old_tbl;
	u32 new_hash;
	bool success = true;

	BUG_ON(!ht->p.key_len);

	rcu_read_lock();
	old_tbl = rht_dereference_rcu(ht->tbl, ht);
	new_tbl = rht_dereference_rcu(ht->future_tbl, ht);
	new_hash = obj_raw_hashfn(ht, rht_obj(ht, obj));

	lock_buckets(new_tbl, old_tbl, new_hash);

	if (rhashtable_lookup_compare(ht, rht_obj(ht, obj) + ht->p.key_offset,
				      compare, arg)) {
		success = false;
		goto exit;
	}

	__rhashtable_insert(ht, obj, new_tbl, new_hash);

exit:
	unlock_buckets(new_tbl, old_tbl, new_hash);
	rcu_read_unlock();

	return success;
}
EXPORT_SYMBOL_GPL(rhashtable_lookup_compare_insert);

/**
 * rhashtable_walk_init - Initialise an iterator
 * @ht:		Table to walk over
 * @iter:	Hash table Iterator
 *
 * This function prepares a hash table walk.
 *
 * Note that if you restart a walk after rhashtable_walk_stop you
 * may see the same object twice.  Also, you may miss objects if
 * there are removals in between rhashtable_walk_stop and the next
 * call to rhashtable_walk_start.
 *
 * For a completely stable walk you should construct your own data
 * structure outside the hash table.
 *
 * This function may sleep so you must not call it from interrupt
 * context or with spin locks held.
 *
 * You must call rhashtable_walk_exit if this function returns
 * successfully.
 */
int rhashtable_walk_init(struct rhashtable *ht, struct rhashtable_iter *iter)
{
	iter->ht = ht;
	iter->p = NULL;
	iter->slot = 0;
	iter->skip = 0;

	iter->walker = kmalloc(sizeof(*iter->walker), GFP_KERNEL);
	if (!iter->walker)
		return -ENOMEM;

	mutex_lock(&ht->mutex);
	list_add(&iter->walker->list, &ht->walkers);
	mutex_unlock(&ht->mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(rhashtable_walk_init);

/**
 * rhashtable_walk_exit - Free an iterator
 * @iter:	Hash table Iterator
 *
 * This function frees resources allocated by rhashtable_walk_init.
 */
void rhashtable_walk_exit(struct rhashtable_iter *iter)
{
	mutex_lock(&iter->ht->mutex);
	list_del(&iter->walker->list);
	mutex_unlock(&iter->ht->mutex);
	kfree(iter->walker);
}
EXPORT_SYMBOL_GPL(rhashtable_walk_exit);

/**
 * rhashtable_walk_start - Start a hash table walk
 * @iter:	Hash table iterator
 *
 * Start a hash table walk.  Note that we take the RCU lock in all
 * cases including when we return an error.  So you must always call
 * rhashtable_walk_stop to clean up.
 *
 * Returns zero if successful.
 *
 * Returns -EAGAIN if resize event occured.  Note that the iterator
 * will rewind back to the beginning and you may use it immediately
 * by calling rhashtable_walk_next.
 */
int rhashtable_walk_start(struct rhashtable_iter *iter)
{
	rcu_read_lock();

	if (iter->walker->resize) {
		iter->slot = 0;
		iter->skip = 0;
		iter->walker->resize = false;
		return -EAGAIN;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rhashtable_walk_start);

/**
 * rhashtable_walk_next - Return the next object and advance the iterator
 * @iter:	Hash table iterator
 *
 * Note that you must call rhashtable_walk_stop when you are finished
 * with the walk.
 *
 * Returns the next object or NULL when the end of the table is reached.
 *
 * Returns -EAGAIN if resize event occured.  Note that the iterator
 * will rewind back to the beginning and you may continue to use it.
 */
void *rhashtable_walk_next(struct rhashtable_iter *iter)
{
	const struct bucket_table *tbl;
	struct rhashtable *ht = iter->ht;
	struct rhash_head *p = iter->p;
	void *obj = NULL;

	tbl = rht_dereference_rcu(ht->tbl, ht);

	if (p) {
		p = rht_dereference_bucket_rcu(p->next, tbl, iter->slot);
		goto next;
	}

	for (; iter->slot < tbl->size; iter->slot++) {
		int skip = iter->skip;

		rht_for_each_rcu(p, tbl, iter->slot) {
			if (!skip)
				break;
			skip--;
		}

next:
		if (!rht_is_a_nulls(p)) {
			iter->skip++;
			iter->p = p;
			obj = rht_obj(ht, p);
			goto out;
		}

		iter->skip = 0;
	}

	iter->p = NULL;

out:
	if (iter->walker->resize) {
		iter->p = NULL;
		iter->slot = 0;
		iter->skip = 0;
		iter->walker->resize = false;
		return ERR_PTR(-EAGAIN);
	}

	return obj;
}
EXPORT_SYMBOL_GPL(rhashtable_walk_next);

/**
 * rhashtable_walk_stop - Finish a hash table walk
 * @iter:	Hash table iterator
 *
 * Finish a hash table walk.
 */
void rhashtable_walk_stop(struct rhashtable_iter *iter)
{
	rcu_read_unlock();
	iter->p = NULL;
}
EXPORT_SYMBOL_GPL(rhashtable_walk_stop);

static size_t rounded_hashtable_size(struct rhashtable_params *params)
{
	return max(roundup_pow_of_two(params->nelem_hint * 4 / 3),
		   1UL << params->min_shift);
}

/**
 * rhashtable_init - initialize a new hash table
 * @ht:		hash table to be initialized
 * @params:	configuration parameters
 *
 * Initializes a new hash table based on the provided configuration
 * parameters. A table can be configured either with a variable or
 * fixed length key:
 *
 * Configuration Example 1: Fixed length keys
 * struct test_obj {
 *	int			key;
 *	void *			my_member;
 *	struct rhash_head	node;
 * };
 *
 * struct rhashtable_params params = {
 *	.head_offset = offsetof(struct test_obj, node),
 *	.key_offset = offsetof(struct test_obj, key),
 *	.key_len = sizeof(int),
 *	.hashfn = jhash,
 *	.nulls_base = (1U << RHT_BASE_SHIFT),
 * };
 *
 * Configuration Example 2: Variable length keys
 * struct test_obj {
 *	[...]
 *	struct rhash_head	node;
 * };
 *
 * u32 my_hash_fn(const void *data, u32 seed)
 * {
 *	struct test_obj *obj = data;
 *
 *	return [... hash ...];
 * }
 *
 * struct rhashtable_params params = {
 *	.head_offset = offsetof(struct test_obj, node),
 *	.hashfn = jhash,
 *	.obj_hashfn = my_hash_fn,
 * };
 */
int rhashtable_init(struct rhashtable *ht, struct rhashtable_params *params)
{
	struct bucket_table *tbl;
	size_t size;

	size = HASH_DEFAULT_SIZE;

	if ((params->key_len && !params->hashfn) ||
	    (!params->key_len && !params->obj_hashfn))
		return -EINVAL;

	if (params->nulls_base && params->nulls_base < (1U << RHT_BASE_SHIFT))
		return -EINVAL;

	params->min_shift = max_t(size_t, params->min_shift,
				  ilog2(HASH_MIN_SIZE));

	if (params->nelem_hint)
		size = rounded_hashtable_size(params);

	memset(ht, 0, sizeof(*ht));
	mutex_init(&ht->mutex);
	memcpy(&ht->p, params, sizeof(*params));
	INIT_LIST_HEAD(&ht->walkers);

	if (params->locks_mul)
		ht->p.locks_mul = roundup_pow_of_two(params->locks_mul);
	else
		ht->p.locks_mul = BUCKET_LOCKS_PER_CPU;

	tbl = bucket_table_alloc(ht, size);
	if (tbl == NULL)
		return -ENOMEM;

	atomic_set(&ht->nelems, 0);
	atomic_set(&ht->shift, ilog2(tbl->size));
	RCU_INIT_POINTER(ht->tbl, tbl);
	RCU_INIT_POINTER(ht->future_tbl, tbl);

	if (!ht->p.hash_rnd)
		get_random_bytes(&ht->p.hash_rnd, sizeof(ht->p.hash_rnd));

	if (ht->p.grow_decision || ht->p.shrink_decision)
		INIT_WORK(&ht->run_work, rht_deferred_worker);

	return 0;
}
EXPORT_SYMBOL_GPL(rhashtable_init);

/**
 * rhashtable_destroy - destroy hash table
 * @ht:		the hash table to destroy
 *
 * Frees the bucket array. This function is not rcu safe, therefore the caller
 * has to make sure that no resizing may happen by unpublishing the hashtable
 * and waiting for the quiescent cycle before releasing the bucket array.
 */
void rhashtable_destroy(struct rhashtable *ht)
{
	ht->being_destroyed = true;

	if (ht->p.grow_decision || ht->p.shrink_decision)
		cancel_work_sync(&ht->run_work);

	mutex_lock(&ht->mutex);
	bucket_table_free(rht_dereference(ht->tbl, ht));
	mutex_unlock(&ht->mutex);
}
EXPORT_SYMBOL_GPL(rhashtable_destroy);
