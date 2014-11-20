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
#include <linux/hash.h>
#include <linux/random.h>
#include <linux/rhashtable.h>

#define HASH_DEFAULT_SIZE	64UL
#define HASH_MIN_SIZE		4UL

#define ASSERT_RHT_MUTEX(HT) BUG_ON(!lockdep_rht_mutex_is_held(HT))

#ifdef CONFIG_PROVE_LOCKING
int lockdep_rht_mutex_is_held(const struct rhashtable *ht)
{
	return ht->p.mutex_is_held();
}
EXPORT_SYMBOL_GPL(lockdep_rht_mutex_is_held);
#endif

static void *rht_obj(const struct rhashtable *ht, const struct rhash_head *he)
{
	return (void *) he - ht->p.head_offset;
}

static u32 __hashfn(const struct rhashtable *ht, const void *key,
		      u32 len, u32 hsize)
{
	u32 h;

	h = ht->p.hashfn(key, len, ht->p.hash_rnd);

	return h & (hsize - 1);
}

/**
 * rhashtable_hashfn - compute hash for key of given length
 * @ht:		hash table to compute for
 * @key:	pointer to key
 * @len:	length of key
 *
 * Computes the hash value using the hash function provided in the 'hashfn'
 * of struct rhashtable_params. The returned value is guaranteed to be
 * smaller than the number of buckets in the hash table.
 */
u32 rhashtable_hashfn(const struct rhashtable *ht, const void *key, u32 len)
{
	struct bucket_table *tbl = rht_dereference_rcu(ht->tbl, ht);

	return __hashfn(ht, key, len, tbl->size);
}
EXPORT_SYMBOL_GPL(rhashtable_hashfn);

static u32 obj_hashfn(const struct rhashtable *ht, const void *ptr, u32 hsize)
{
	if (unlikely(!ht->p.key_len)) {
		u32 h;

		h = ht->p.obj_hashfn(ptr, ht->p.hash_rnd);

		return h & (hsize - 1);
	}

	return __hashfn(ht, ptr + ht->p.key_offset, ht->p.key_len, hsize);
}

/**
 * rhashtable_obj_hashfn - compute hash for hashed object
 * @ht:		hash table to compute for
 * @ptr:	pointer to hashed object
 *
 * Computes the hash value using the hash function `hashfn` respectively
 * 'obj_hashfn' depending on whether the hash table is set up to work with
 * a fixed length key. The returned value is guaranteed to be smaller than
 * the number of buckets in the hash table.
 */
u32 rhashtable_obj_hashfn(const struct rhashtable *ht, void *ptr)
{
	struct bucket_table *tbl = rht_dereference_rcu(ht->tbl, ht);

	return obj_hashfn(ht, ptr, tbl->size);
}
EXPORT_SYMBOL_GPL(rhashtable_obj_hashfn);

static u32 head_hashfn(const struct rhashtable *ht,
		       const struct rhash_head *he, u32 hsize)
{
	return obj_hashfn(ht, rht_obj(ht, he), hsize);
}

static struct bucket_table *bucket_table_alloc(size_t nbuckets, gfp_t flags)
{
	struct bucket_table *tbl;
	size_t size;

	size = sizeof(*tbl) + nbuckets * sizeof(tbl->buckets[0]);
	tbl = kzalloc(size, flags);
	if (tbl == NULL)
		tbl = vzalloc(size);

	if (tbl == NULL)
		return NULL;

	tbl->size = nbuckets;

	return tbl;
}

static void bucket_table_free(const struct bucket_table *tbl)
{
	kvfree(tbl);
}

/**
 * rht_grow_above_75 - returns true if nelems > 0.75 * table-size
 * @ht:		hash table
 * @new_size:	new table size
 */
bool rht_grow_above_75(const struct rhashtable *ht, size_t new_size)
{
	/* Expand table when exceeding 75% load */
	return ht->nelems > (new_size / 4 * 3);
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
	return ht->nelems < (new_size * 3 / 10);
}
EXPORT_SYMBOL_GPL(rht_shrink_below_30);

static void hashtable_chain_unzip(const struct rhashtable *ht,
				  const struct bucket_table *new_tbl,
				  struct bucket_table *old_tbl, size_t n)
{
	struct rhash_head *he, *p, *next;
	unsigned int h;

	/* Old bucket empty, no work needed. */
	p = rht_dereference(old_tbl->buckets[n], ht);
	if (!p)
		return;

	/* Advance the old bucket pointer one or more times until it
	 * reaches a node that doesn't hash to the same bucket as the
	 * previous node p. Call the previous node p;
	 */
	h = head_hashfn(ht, p, new_tbl->size);
	rht_for_each(he, p->next, ht) {
		if (head_hashfn(ht, he, new_tbl->size) != h)
			break;
		p = he;
	}
	RCU_INIT_POINTER(old_tbl->buckets[n], p->next);

	/* Find the subsequent node which does hash to the same
	 * bucket as node P, or NULL if no such node exists.
	 */
	next = NULL;
	if (he) {
		rht_for_each(he, he->next, ht) {
			if (head_hashfn(ht, he, new_tbl->size) == h) {
				next = he;
				break;
			}
		}
	}

	/* Set p's next pointer to that subsequent node pointer,
	 * bypassing the nodes which do not hash to p's bucket
	 */
	RCU_INIT_POINTER(p->next, next);
}

/**
 * rhashtable_expand - Expand hash table while allowing concurrent lookups
 * @ht:		the hash table to expand
 * @flags:	allocation flags
 *
 * A secondary bucket array is allocated and the hash entries are migrated
 * while keeping them on both lists until the end of the RCU grace period.
 *
 * This function may only be called in a context where it is safe to call
 * synchronize_rcu(), e.g. not within a rcu_read_lock() section.
 *
 * The caller must ensure that no concurrent table mutations take place.
 * It is however valid to have concurrent lookups if they are RCU protected.
 */
int rhashtable_expand(struct rhashtable *ht, gfp_t flags)
{
	struct bucket_table *new_tbl, *old_tbl = rht_dereference(ht->tbl, ht);
	struct rhash_head *he;
	unsigned int i, h;
	bool complete;

	ASSERT_RHT_MUTEX(ht);

	if (ht->p.max_shift && ht->shift >= ht->p.max_shift)
		return 0;

	new_tbl = bucket_table_alloc(old_tbl->size * 2, flags);
	if (new_tbl == NULL)
		return -ENOMEM;

	ht->shift++;

	/* For each new bucket, search the corresponding old bucket
	 * for the first entry that hashes to the new bucket, and
	 * link the new bucket to that entry. Since all the entries
	 * which will end up in the new bucket appear in the same
	 * old bucket, this constructs an entirely valid new hash
	 * table, but with multiple buckets "zipped" together into a
	 * single imprecise chain.
	 */
	for (i = 0; i < new_tbl->size; i++) {
		h = i & (old_tbl->size - 1);
		rht_for_each(he, old_tbl->buckets[h], ht) {
			if (head_hashfn(ht, he, new_tbl->size) == i) {
				RCU_INIT_POINTER(new_tbl->buckets[i], he);
				break;
			}
		}
	}

	/* Publish the new table pointer. Lookups may now traverse
	 * the new table, but they will not benefit from any
	 * additional efficiency until later steps unzip the buckets.
	 */
	rcu_assign_pointer(ht->tbl, new_tbl);

	/* Unzip interleaved hash chains */
	do {
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
		for (i = 0; i < old_tbl->size; i++) {
			hashtable_chain_unzip(ht, new_tbl, old_tbl, i);
			if (old_tbl->buckets[i] != NULL)
				complete = false;
		}
	} while (!complete);

	bucket_table_free(old_tbl);
	return 0;
}
EXPORT_SYMBOL_GPL(rhashtable_expand);

/**
 * rhashtable_shrink - Shrink hash table while allowing concurrent lookups
 * @ht:		the hash table to shrink
 * @flags:	allocation flags
 *
 * This function may only be called in a context where it is safe to call
 * synchronize_rcu(), e.g. not within a rcu_read_lock() section.
 *
 * The caller must ensure that no concurrent table mutations take place.
 * It is however valid to have concurrent lookups if they are RCU protected.
 */
int rhashtable_shrink(struct rhashtable *ht, gfp_t flags)
{
	struct bucket_table *ntbl, *tbl = rht_dereference(ht->tbl, ht);
	struct rhash_head __rcu **pprev;
	unsigned int i;

	ASSERT_RHT_MUTEX(ht);

	if (ht->shift <= ht->p.min_shift)
		return 0;

	ntbl = bucket_table_alloc(tbl->size / 2, flags);
	if (ntbl == NULL)
		return -ENOMEM;

	ht->shift--;

	/* Link each bucket in the new table to the first bucket
	 * in the old table that contains entries which will hash
	 * to the new bucket.
	 */
	for (i = 0; i < ntbl->size; i++) {
		ntbl->buckets[i] = tbl->buckets[i];

		/* Link each bucket in the new table to the first bucket
		 * in the old table that contains entries which will hash
		 * to the new bucket.
		 */
		for (pprev = &ntbl->buckets[i]; *pprev != NULL;
		     pprev = &rht_dereference(*pprev, ht)->next)
			;
		RCU_INIT_POINTER(*pprev, tbl->buckets[i + ntbl->size]);
	}

	/* Publish the new, valid hash table */
	rcu_assign_pointer(ht->tbl, ntbl);

	/* Wait for readers. No new readers will have references to the
	 * old hash table.
	 */
	synchronize_rcu();

	bucket_table_free(tbl);

	return 0;
}
EXPORT_SYMBOL_GPL(rhashtable_shrink);

/**
 * rhashtable_insert - insert object into hash hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 * @flags:	allocation flags (table expansion)
 *
 * Will automatically grow the table via rhashtable_expand() if the the
 * grow_decision function specified at rhashtable_init() returns true.
 *
 * The caller must ensure that no concurrent table mutations occur. It is
 * however valid to have concurrent lookups if they are RCU protected.
 */
void rhashtable_insert(struct rhashtable *ht, struct rhash_head *obj,
		       gfp_t flags)
{
	struct bucket_table *tbl = rht_dereference(ht->tbl, ht);
	u32 hash;

	ASSERT_RHT_MUTEX(ht);

	hash = head_hashfn(ht, obj, tbl->size);
	RCU_INIT_POINTER(obj->next, tbl->buckets[hash]);
	rcu_assign_pointer(tbl->buckets[hash], obj);
	ht->nelems++;

	if (ht->p.grow_decision && ht->p.grow_decision(ht, tbl->size))
		rhashtable_expand(ht, flags);
}
EXPORT_SYMBOL_GPL(rhashtable_insert);

/**
 * rhashtable_remove_pprev - remove object from hash table given previous element
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 * @pprev:	pointer to previous element
 * @flags:	allocation flags (table expansion)
 *
 * Identical to rhashtable_remove() but caller is alreayd aware of the element
 * in front of the element to be deleted. This is in particular useful for
 * deletion when combined with walking or lookup.
 */
void rhashtable_remove_pprev(struct rhashtable *ht, struct rhash_head *obj,
			     struct rhash_head __rcu **pprev, gfp_t flags)
{
	struct bucket_table *tbl = rht_dereference(ht->tbl, ht);

	ASSERT_RHT_MUTEX(ht);

	RCU_INIT_POINTER(*pprev, obj->next);
	ht->nelems--;

	if (ht->p.shrink_decision &&
	    ht->p.shrink_decision(ht, tbl->size))
		rhashtable_shrink(ht, flags);
}
EXPORT_SYMBOL_GPL(rhashtable_remove_pprev);

/**
 * rhashtable_remove - remove object from hash table
 * @ht:		hash table
 * @obj:	pointer to hash head inside object
 * @flags:	allocation flags (table expansion)
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
bool rhashtable_remove(struct rhashtable *ht, struct rhash_head *obj,
		       gfp_t flags)
{
	struct bucket_table *tbl = rht_dereference(ht->tbl, ht);
	struct rhash_head __rcu **pprev;
	struct rhash_head *he;
	u32 h;

	ASSERT_RHT_MUTEX(ht);

	h = head_hashfn(ht, obj, tbl->size);

	pprev = &tbl->buckets[h];
	rht_for_each(he, tbl->buckets[h], ht) {
		if (he != obj) {
			pprev = &he->next;
			continue;
		}

		rhashtable_remove_pprev(ht, he, pprev, flags);
		return true;
	}

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
 * Lookups may occur in parallel with hash mutations as long as the lookup is
 * guarded by rcu_read_lock(). The caller must take care of this.
 */
void *rhashtable_lookup(const struct rhashtable *ht, const void *key)
{
	const struct bucket_table *tbl = rht_dereference_rcu(ht->tbl, ht);
	struct rhash_head *he;
	u32 h;

	BUG_ON(!ht->p.key_len);

	h = __hashfn(ht, key, ht->p.key_len, tbl->size);
	rht_for_each_rcu(he, tbl->buckets[h], ht) {
		if (memcmp(rht_obj(ht, he) + ht->p.key_offset, key,
			   ht->p.key_len))
			continue;
		return (void *) he - ht->p.head_offset;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(rhashtable_lookup);

/**
 * rhashtable_lookup_compare - search hash table with compare function
 * @ht:		hash table
 * @hash:	hash value of desired entry
 * @compare:	compare function, must return true on match
 * @arg:	argument passed on to compare function
 *
 * Traverses the bucket chain behind the provided hash value and calls the
 * specified compare function for each entry.
 *
 * Lookups may occur in parallel with hash mutations as long as the lookup is
 * guarded by rcu_read_lock(). The caller must take care of this.
 *
 * Returns the first entry on which the compare function returned true.
 */
void *rhashtable_lookup_compare(const struct rhashtable *ht, u32 hash,
				bool (*compare)(void *, void *), void *arg)
{
	const struct bucket_table *tbl = rht_dereference_rcu(ht->tbl, ht);
	struct rhash_head *he;

	if (unlikely(hash >= tbl->size))
		return NULL;

	rht_for_each_rcu(he, tbl->buckets[hash], ht) {
		if (!compare(rht_obj(ht, he), arg))
			continue;
		return (void *) he - ht->p.head_offset;
	}

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
 *	.hashfn = arch_fast_hash,
 *	.mutex_is_held = &my_mutex_is_held,
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
 *	.hashfn = arch_fast_hash,
 *	.obj_hashfn = my_hash_fn,
 *	.mutex_is_held = &my_mutex_is_held,
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

	params->min_shift = max_t(size_t, params->min_shift,
				  ilog2(HASH_MIN_SIZE));

	if (params->nelem_hint)
		size = rounded_hashtable_size(params);

	tbl = bucket_table_alloc(size, GFP_KERNEL);
	if (tbl == NULL)
		return -ENOMEM;

	memset(ht, 0, sizeof(*ht));
	ht->shift = ilog2(tbl->size);
	memcpy(&ht->p, params, sizeof(*params));
	RCU_INIT_POINTER(ht->tbl, tbl);

	if (!ht->p.hash_rnd)
		get_random_bytes(&ht->p.hash_rnd, sizeof(ht->p.hash_rnd));

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
void rhashtable_destroy(const struct rhashtable *ht)
{
	bucket_table_free(ht->tbl);
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

static int test_mutex_is_held(void)
{
	return 1;
}

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

static void test_bucket_stats(struct rhashtable *ht,
				     struct bucket_table *tbl,
				     bool quiet)
{
	unsigned int cnt, i, total = 0;
	struct test_obj *obj;

	for (i = 0; i < tbl->size; i++) {
		cnt = 0;

		if (!quiet)
			pr_info(" [%#4x/%zu]", i, tbl->size);

		rht_for_each_entry_rcu(obj, tbl->buckets[i], node) {
			cnt++;
			total++;
			if (!quiet)
				pr_cont(" [%p],", obj);
		}

		if (!quiet)
			pr_cont("\n  [%#x] first element: %p, chain length: %u\n",
				i, tbl->buckets[i], cnt);
	}

	pr_info("  Traversal complete: counted=%u, nelems=%zu, entries=%d\n",
		total, ht->nelems, TEST_ENTRIES);
}

static int __init test_rhashtable(struct rhashtable *ht)
{
	struct bucket_table *tbl;
	struct test_obj *obj, *next;
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

		rhashtable_insert(ht, &obj->node, GFP_KERNEL);
	}

	rcu_read_lock();
	tbl = rht_dereference_rcu(ht->tbl, ht);
	test_bucket_stats(ht, tbl, true);
	test_rht_lookup(ht);
	rcu_read_unlock();

	for (i = 0; i < TEST_NEXPANDS; i++) {
		pr_info("  Table expansion iteration %u...\n", i);
		rhashtable_expand(ht, GFP_KERNEL);

		rcu_read_lock();
		pr_info("  Verifying lookups...\n");
		test_rht_lookup(ht);
		rcu_read_unlock();
	}

	for (i = 0; i < TEST_NEXPANDS; i++) {
		pr_info("  Table shrinkage iteration %u...\n", i);
		rhashtable_shrink(ht, GFP_KERNEL);

		rcu_read_lock();
		pr_info("  Verifying lookups...\n");
		test_rht_lookup(ht);
		rcu_read_unlock();
	}

	pr_info("  Deleting %d keys\n", TEST_ENTRIES);
	for (i = 0; i < TEST_ENTRIES; i++) {
		u32 key = i * 2;

		obj = rhashtable_lookup(ht, &key);
		BUG_ON(!obj);

		rhashtable_remove(ht, &obj->node, GFP_KERNEL);
		kfree(obj);
	}

	return 0;

error:
	tbl = rht_dereference_rcu(ht->tbl, ht);
	for (i = 0; i < tbl->size; i++)
		rht_for_each_entry_safe(obj, next, tbl->buckets[i], ht, node)
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
		.hashfn = arch_fast_hash,
		.mutex_is_held = &test_mutex_is_held,
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
