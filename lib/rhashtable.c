/*
 * Resizable, Scalable, Concurrent Hash Table
 *
 * Copyright (c) 2014 Thomas Graf <tgraf@suug.ch>
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

#define HASH_DEFAULT_SIZE	64UL
#define HASH_MIN_SIZE		4UL
#define BUCKET_LOCKS_PER_CPU   128UL

/* Base bits plus 1 bit for nulls marker */
#define HASH_RESERVED_SPACE	(RHT_BASE_BITS + 1)

enum {
	RHT_LOCK_NORMAL,
	RHT_LOCK_NESTED,
	RHT_LOCK_NESTED2,
};

/* The bucket lock is selected based on the hash and protects mutations
 * on a group of hash buckets.
 *
 * IMPORTANT: When holding the bucket lock of both the old and new table
 * during expansions and shrinking, the old bucket lock must always be
 * acquired first.
 */
static spinlock_t *bucket_lock(const struct bucket_table *tbl, u32 hash)
{
	return &tbl->locks[hash & tbl->locks_mask];
}

#define ASSERT_RHT_MUTEX(HT) BUG_ON(!lockdep_rht_mutex_is_held(HT))
#define ASSERT_BUCKET_LOCK(TBL, HASH) \
	BUG_ON(!lockdep_rht_bucket_is_held(TBL, HASH))

#ifdef CONFIG_PROVE_LOCKING
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
#endif

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
	struct bucket_table *tbl = rht_dereference_rcu(ht->tbl, ht);
	u32 hash;

	hash = ht->p.hashfn(key, len, ht->p.hash_rnd);
	hash >>= HASH_RESERVED_SPACE;

	return rht_bucket_index(tbl, hash);
}

static u32 head_hashfn(const struct rhashtable *ht,
		       const struct bucket_table *tbl,
		       const struct rhash_head *he)
{
	return rht_bucket_index(tbl, obj_raw_hashfn(ht, rht_obj(ht, he)));
}

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

	/* Never allocate more than one lock per bucket */
	size = min_t(unsigned int, size, tbl->size);

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
	return atomic_read(&ht->nelems) > (new_size / 4 * 3);
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
	return atomic_read(&ht->nelems) < (new_size * 3 / 10);
}
EXPORT_SYMBOL_GPL(rht_shrink_below_30);

static void hashtable_chain_unzip(const struct rhashtable *ht,
				  const struct bucket_table *new_tbl,
				  struct bucket_table *old_tbl,
				  size_t old_hash)
{
	struct rhash_head *he, *p, *next;
	spinlock_t *new_bucket_lock, *new_bucket_lock2 = NULL;
	unsigned int new_hash, new_hash2;

	ASSERT_BUCKET_LOCK(old_tbl, old_hash);

	/* Old bucket empty, no work needed. */
	p = rht_dereference_bucket(old_tbl->buckets[old_hash], old_tbl,
				   old_hash);
	if (rht_is_a_nulls(p))
		return;

	new_hash = new_hash2 = head_hashfn(ht, new_tbl, p);
	new_bucket_lock = bucket_lock(new_tbl, new_hash);

	/* Advance the old bucket pointer one or more times until it
	 * reaches a node that doesn't hash to the same bucket as the
	 * previous node p. Call the previous node p;
	 */
	rht_for_each_continue(he, p->next, old_tbl, old_hash) {
		new_hash2 = head_hashfn(ht, new_tbl, he);
		if (new_hash != new_hash2)
			break;
		p = he;
	}
	rcu_assign_pointer(old_tbl->buckets[old_hash], p->next);

	spin_lock_bh_nested(new_bucket_lock, RHT_LOCK_NESTED);

	/* If we have encountered an entry that maps to a different bucket in
	 * the new table, lock down that bucket as well as we might cut off
	 * the end of the chain.
	 */
	new_bucket_lock2 = bucket_lock(new_tbl, new_hash);
	if (new_bucket_lock != new_bucket_lock2)
		spin_lock_bh_nested(new_bucket_lock2, RHT_LOCK_NESTED2);

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

	if (new_bucket_lock != new_bucket_lock2)
		spin_unlock_bh(new_bucket_lock2);
	spin_unlock_bh(new_bucket_lock);
}

static void link_old_to_new(struct bucket_table *new_tbl,
			    unsigned int new_hash, struct rhash_head *entry)
{
	spinlock_t *new_bucket_lock;

	new_bucket_lock = bucket_lock(new_tbl, new_hash);

	spin_lock_bh_nested(new_bucket_lock, RHT_LOCK_NESTED);
	rcu_assign_pointer(*bucket_tail(new_tbl, new_hash), entry);
	spin_unlock_bh(new_bucket_lock);
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
	spinlock_t *old_bucket_lock;
	unsigned int new_hash, old_hash;
	bool complete = false;

	ASSERT_RHT_MUTEX(ht);

	if (ht->p.max_shift && ht->shift >= ht->p.max_shift)
		return 0;

	new_tbl = bucket_table_alloc(ht, old_tbl->size * 2);
	if (new_tbl == NULL)
		return -ENOMEM;

	ht->shift++;

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
		old_bucket_lock = bucket_lock(old_tbl, old_hash);

		spin_lock_bh(old_bucket_lock);
		rht_for_each(he, old_tbl, old_hash) {
			if (head_hashfn(ht, new_tbl, he) == new_hash) {
				link_old_to_new(new_tbl, new_hash, he);
				break;
			}
		}
		spin_unlock_bh(old_bucket_lock);
	}

	/* Publish the new table pointer. Lookups may now traverse
	 * the new table, but they will not benefit from any
	 * additional efficiency until later steps unzip the buckets.
	 */
	rcu_assign_pointer(ht->tbl, new_tbl);

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
			struct rhash_head *head;

			old_bucket_lock = bucket_lock(old_tbl, old_hash);
			spin_lock_bh(old_bucket_lock);

			hashtable_chain_unzip(ht, new_tbl, old_tbl, old_hash);
			head = rht_dereference_bucket(old_tbl->buckets[old_hash],
						      old_tbl, old_hash);
			if (!rht_is_a_nulls(head))
				complete = false;

			spin_unlock_bh(old_bucket_lock);
		}
	}

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
	spinlock_t *new_bucket_lock, *old_bucket_lock1, *old_bucket_lock2;
	unsigned int new_hash;

	ASSERT_RHT_MUTEX(ht);

	if (ht->shift <= ht->p.min_shift)
		return 0;

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
	 *
	 * As removals can occur concurrently on the old table, we need
	 * to lock down both matching buckets in the old table.
	 */
	for (new_hash = 0; new_hash < new_tbl->size; new_hash++) {
		old_bucket_lock1 = bucket_lock(tbl, new_hash);
		old_bucket_lock2 = bucket_lock(tbl, new_hash + new_tbl->size);
		new_bucket_lock = bucket_lock(new_tbl, new_hash);

		spin_lock_bh(old_bucket_lock1);
		spin_lock_bh_nested(old_bucket_lock2, RHT_LOCK_NESTED);
		spin_lock_bh_nested(new_bucket_lock, RHT_LOCK_NESTED2);

		rcu_assign_pointer(*bucket_tail(new_tbl, new_hash),
				   tbl->buckets[new_hash]);
		rcu_assign_pointer(*bucket_tail(new_tbl, new_hash),
				   tbl->buckets[new_hash + new_tbl->size]);

		spin_unlock_bh(new_bucket_lock);
		spin_unlock_bh(old_bucket_lock2);
		spin_unlock_bh(old_bucket_lock1);
	}

	/* Publish the new, valid hash table */
	rcu_assign_pointer(ht->tbl, new_tbl);
	ht->shift--;

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

	ht = container_of(work, struct rhashtable, run_work.work);
	mutex_lock(&ht->mutex);
	tbl = rht_dereference(ht->tbl, ht);

	if (ht->p.grow_decision && ht->p.grow_decision(ht, tbl->size))
		rhashtable_expand(ht);
	else if (ht->p.shrink_decision && ht->p.shrink_decision(ht, tbl->size))
		rhashtable_shrink(ht);

	mutex_unlock(&ht->mutex);
}

/**
 * rhashtable_insert - insert object into hash hash table
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
	struct bucket_table *tbl;
	struct rhash_head *head;
	spinlock_t *lock;
	unsigned hash;

	rcu_read_lock();

	tbl = rht_dereference_rcu(ht->future_tbl, ht);
	hash = head_hashfn(ht, tbl, obj);
	lock = bucket_lock(tbl, hash);

	spin_lock_bh(lock);
	head = rht_dereference_bucket(tbl->buckets[hash], tbl, hash);
	if (rht_is_a_nulls(head))
		INIT_RHT_NULLS_HEAD(obj->next, ht, hash);
	else
		RCU_INIT_POINTER(obj->next, head);

	rcu_assign_pointer(tbl->buckets[hash], obj);
	spin_unlock_bh(lock);

	atomic_inc(&ht->nelems);

	/* Only grow the table if no resizing is currently in progress. */
	if (ht->tbl != ht->future_tbl &&
	    ht->p.grow_decision && ht->p.grow_decision(ht, tbl->size))
		schedule_delayed_work(&ht->run_work, 0);

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
 * Will automatically shrink the table via rhashtable_expand() if the the
 * shrink_decision function specified at rhashtable_init() returns true.
 *
 * The caller must ensure that no concurrent table mutations occur. It is
 * however valid to have concurrent lookups if they are RCU protected.
 */
bool rhashtable_remove(struct rhashtable *ht, struct rhash_head *obj)
{
	struct bucket_table *tbl;
	struct rhash_head __rcu **pprev;
	struct rhash_head *he;
	spinlock_t *lock;
	unsigned int hash;

	rcu_read_lock();
	tbl = rht_dereference_rcu(ht->tbl, ht);
	hash = head_hashfn(ht, tbl, obj);

	lock = bucket_lock(tbl, hash);
	spin_lock_bh(lock);

restart:
	pprev = &tbl->buckets[hash];
	rht_for_each(he, tbl, hash) {
		if (he != obj) {
			pprev = &he->next;
			continue;
		}

		rcu_assign_pointer(*pprev, obj->next);
		atomic_dec(&ht->nelems);

		spin_unlock_bh(lock);

		if (ht->tbl != ht->future_tbl &&
		    ht->p.shrink_decision &&
		    ht->p.shrink_decision(ht, tbl->size))
			schedule_delayed_work(&ht->run_work, 0);

		rcu_read_unlock();

		return true;
	}

	if (tbl != rht_dereference_rcu(ht->tbl, ht)) {
		spin_unlock_bh(lock);

		tbl = rht_dereference_rcu(ht->tbl, ht);
		hash = head_hashfn(ht, tbl, obj);

		lock = bucket_lock(tbl, hash);
		spin_lock_bh(lock);
		goto restart;
	}

	spin_unlock_bh(lock);
	rcu_read_unlock();

	return false;
}
EXPORT_SYMBOL_GPL(rhashtable_remove);

/**
 * rhashtable_lookup - lookup key in hash table
 * @ht:		hash table
 * @key:	pointer to key
 *
 * Computes the hash value for the key and traverses the bucket chain looking
 * for a entry with an identical key. The first matching entry is returned.
 *
 * This lookup function may only be used for fixed key hash table (key_len
 * paramter set). It will BUG() if used inappropriately.
 *
 * Lookups may occur in parallel with hashtable mutations and resizing.
 */
void *rhashtable_lookup(struct rhashtable *ht, const void *key)
{
	const struct bucket_table *tbl, *old_tbl;
	struct rhash_head *he;
	u32 hash;

	BUG_ON(!ht->p.key_len);

	rcu_read_lock();
	old_tbl = rht_dereference_rcu(ht->tbl, ht);
	tbl = rht_dereference_rcu(ht->future_tbl, ht);
	hash = key_hashfn(ht, key, ht->p.key_len);
restart:
	rht_for_each_rcu(he, tbl, rht_bucket_index(tbl, hash)) {
		if (memcmp(rht_obj(ht, he) + ht->p.key_offset, key,
			   ht->p.key_len))
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

	if (params->locks_mul)
		ht->p.locks_mul = roundup_pow_of_two(params->locks_mul);
	else
		ht->p.locks_mul = BUCKET_LOCKS_PER_CPU;

	tbl = bucket_table_alloc(ht, size);
	if (tbl == NULL)
		return -ENOMEM;

	ht->shift = ilog2(tbl->size);
	RCU_INIT_POINTER(ht->tbl, tbl);
	RCU_INIT_POINTER(ht->future_tbl, tbl);

	if (!ht->p.hash_rnd)
		get_random_bytes(&ht->p.hash_rnd, sizeof(ht->p.hash_rnd));

	if (ht->p.grow_decision || ht->p.shrink_decision)
		INIT_DEFERRABLE_WORK(&ht->run_work, rht_deferred_worker);

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

	mutex_lock(&ht->mutex);

	cancel_delayed_work(&ht->run_work);
	bucket_table_free(rht_dereference(ht->tbl, ht));

	mutex_unlock(&ht->mutex);
}
EXPORT_SYMBOL_GPL(rhashtable_destroy);

/**************************************************************************
 * Self Test
 **************************************************************************/

#ifdef CONFIG_TEST_RHASHTABLE

#define TEST_HT_SIZE	8
#define TEST_ENTRIES	2048
#define TEST_PTR	((void *) 0xdeadbeef)
#define TEST_NEXPANDS	4

struct test_obj {
	void			*ptr;
	int			value;
	struct rhash_head	node;
};

static int __init test_rht_lookup(struct rhashtable *ht)
{
	unsigned int i;

	for (i = 0; i < TEST_ENTRIES * 2; i++) {
		struct test_obj *obj;
		bool expected = !(i % 2);
		u32 key = i;

		obj = rhashtable_lookup(ht, &key);

		if (expected && !obj) {
			pr_warn("Test failed: Could not find key %u\n", key);
			return -ENOENT;
		} else if (!expected && obj) {
			pr_warn("Test failed: Unexpected entry found for key %u\n",
				key);
			return -EEXIST;
		} else if (expected && obj) {
			if (obj->ptr != TEST_PTR || obj->value != i) {
				pr_warn("Test failed: Lookup value mismatch %p!=%p, %u!=%u\n",
					obj->ptr, TEST_PTR, obj->value, i);
				return -EINVAL;
			}
		}
	}

	return 0;
}

static void test_bucket_stats(struct rhashtable *ht, bool quiet)
{
	unsigned int cnt, rcu_cnt, i, total = 0;
	struct rhash_head *pos;
	struct test_obj *obj;
	struct bucket_table *tbl;

	tbl = rht_dereference_rcu(ht->tbl, ht);
	for (i = 0; i < tbl->size; i++) {
		rcu_cnt = cnt = 0;

		if (!quiet)
			pr_info(" [%#4x/%zu]", i, tbl->size);

		rht_for_each_entry_rcu(obj, pos, tbl, i, node) {
			cnt++;
			total++;
			if (!quiet)
				pr_cont(" [%p],", obj);
		}

		rht_for_each_entry_rcu(obj, pos, tbl, i, node)
			rcu_cnt++;

		if (rcu_cnt != cnt)
			pr_warn("Test failed: Chain count mismach %d != %d",
				cnt, rcu_cnt);

		if (!quiet)
			pr_cont("\n  [%#x] first element: %p, chain length: %u\n",
				i, tbl->buckets[i], cnt);
	}

	pr_info("  Traversal complete: counted=%u, nelems=%u, entries=%d\n",
		total, atomic_read(&ht->nelems), TEST_ENTRIES);

	if (total != atomic_read(&ht->nelems) || total != TEST_ENTRIES)
		pr_warn("Test failed: Total count mismatch ^^^");
}

static int __init test_rhashtable(struct rhashtable *ht)
{
	struct bucket_table *tbl;
	struct test_obj *obj;
	struct rhash_head *pos, *next;
	int err;
	unsigned int i;

	/*
	 * Insertion Test:
	 * Insert TEST_ENTRIES into table with all keys even numbers
	 */
	pr_info("  Adding %d keys\n", TEST_ENTRIES);
	for (i = 0; i < TEST_ENTRIES; i++) {
		struct test_obj *obj;

		obj = kzalloc(sizeof(*obj), GFP_KERNEL);
		if (!obj) {
			err = -ENOMEM;
			goto error;
		}

		obj->ptr = TEST_PTR;
		obj->value = i * 2;

		rhashtable_insert(ht, &obj->node);
	}

	rcu_read_lock();
	test_bucket_stats(ht, true);
	test_rht_lookup(ht);
	rcu_read_unlock();

	for (i = 0; i < TEST_NEXPANDS; i++) {
		pr_info("  Table expansion iteration %u...\n", i);
		mutex_lock(&ht->mutex);
		rhashtable_expand(ht);
		mutex_unlock(&ht->mutex);

		rcu_read_lock();
		pr_info("  Verifying lookups...\n");
		test_rht_lookup(ht);
		rcu_read_unlock();
	}

	for (i = 0; i < TEST_NEXPANDS; i++) {
		pr_info("  Table shrinkage iteration %u...\n", i);
		mutex_lock(&ht->mutex);
		rhashtable_shrink(ht);
		mutex_unlock(&ht->mutex);

		rcu_read_lock();
		pr_info("  Verifying lookups...\n");
		test_rht_lookup(ht);
		rcu_read_unlock();
	}

	rcu_read_lock();
	test_bucket_stats(ht, true);
	rcu_read_unlock();

	pr_info("  Deleting %d keys\n", TEST_ENTRIES);
	for (i = 0; i < TEST_ENTRIES; i++) {
		u32 key = i * 2;

		obj = rhashtable_lookup(ht, &key);
		BUG_ON(!obj);

		rhashtable_remove(ht, &obj->node);
		kfree(obj);
	}

	return 0;

error:
	tbl = rht_dereference_rcu(ht->tbl, ht);
	for (i = 0; i < tbl->size; i++)
		rht_for_each_entry_safe(obj, pos, next, tbl, i, node)
			kfree(obj);

	return err;
}

static int __init test_rht_init(void)
{
	struct rhashtable ht;
	struct rhashtable_params params = {
		.nelem_hint = TEST_HT_SIZE,
		.head_offset = offsetof(struct test_obj, node),
		.key_offset = offsetof(struct test_obj, value),
		.key_len = sizeof(int),
		.hashfn = jhash,
		.nulls_base = (3U << RHT_BASE_SHIFT),
		.grow_decision = rht_grow_above_75,
		.shrink_decision = rht_shrink_below_30,
	};
	int err;

	pr_info("Running resizable hashtable tests...\n");

	err = rhashtable_init(&ht, &params);
	if (err < 0) {
		pr_warn("Test failed: Unable to initialize hashtable: %d\n",
			err);
		return err;
	}

	err = test_rhashtable(&ht);

	rhashtable_destroy(&ht);

	return err;
}

subsys_initcall(test_rht_init);

#endif /* CONFIG_TEST_RHASHTABLE */
