/* SPDX-License-Identifier: GPL-2.0 */
/*
 * A hash table (hashtab) maintains associations between
 * key values and datum values.  The type of the key values
 * and the type of the datum values is arbitrary.  The
 * functions for hash computation and key comparison are
 * provided by the creator of the table.
 *
 * Author : Stephen Smalley, <stephen.smalley.work@gmail.com>
 */
#ifndef _SS_HASHTAB_H_
#define _SS_HASHTAB_H_

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>

#define HASHTAB_MAX_NODES	U32_MAX

struct hashtab_key_params {
	u32 (*hash)(const void *key);	/* hash function */
	int (*cmp)(const void *key1, const void *key2);
					/* key comparison function */
};

struct hashtab_node {
	void *key;
	void *datum;
	struct hashtab_node *next;
};

struct hashtab {
	struct hashtab_node **htable;	/* hash table */
	u32 size;			/* number of slots in hash table */
	u32 nel;			/* number of elements in hash table */
};

struct hashtab_info {
	u32 slots_used;
	u32 max_chain_len;
	u64 chain2_len_sum;
};

/*
 * Initializes a new hash table with the specified characteristics.
 *
 * Returns -ENOMEM if insufficient space is available or 0 otherwise.
 */
int hashtab_init(struct hashtab *h, u32 nel_hint);

int __hashtab_insert(struct hashtab *h, struct hashtab_node **dst,
		     void *key, void *datum);

/*
 * Inserts the specified (key, datum) pair into the specified hash table.
 *
 * Returns -ENOMEM on memory allocation error,
 * -EEXIST if there is already an entry with the same key,
 * -EINVAL for general errors or
  0 otherwise.
 */
static inline int hashtab_insert(struct hashtab *h, void *key, void *datum,
				 struct hashtab_key_params key_params)
{
	u32 hvalue;
	struct hashtab_node *prev, *cur;

	cond_resched();

	if (!h->size || h->nel == HASHTAB_MAX_NODES)
		return -EINVAL;

	hvalue = key_params.hash(key) & (h->size - 1);
	prev = NULL;
	cur = h->htable[hvalue];
	while (cur) {
		int cmp = key_params.cmp(key, cur->key);

		if (cmp == 0)
			return -EEXIST;
		if (cmp < 0)
			break;
		prev = cur;
		cur = cur->next;
	}

	return __hashtab_insert(h, prev ? &prev->next : &h->htable[hvalue],
				key, datum);
}

/*
 * Searches for the entry with the specified key in the hash table.
 *
 * Returns NULL if no entry has the specified key or
 * the datum of the entry otherwise.
 */
static inline void *hashtab_search(struct hashtab *h, const void *key,
				   struct hashtab_key_params key_params)
{
	u32 hvalue;
	struct hashtab_node *cur;

	if (!h->size)
		return NULL;

	hvalue = key_params.hash(key) & (h->size - 1);
	cur = h->htable[hvalue];
	while (cur) {
		int cmp = key_params.cmp(key, cur->key);

		if (cmp == 0)
			return cur->datum;
		if (cmp < 0)
			break;
		cur = cur->next;
	}
	return NULL;
}

/*
 * Destroys the specified hash table.
 */
void hashtab_destroy(struct hashtab *h);

/*
 * Applies the specified apply function to (key,datum,args)
 * for each entry in the specified hash table.
 *
 * The order in which the function is applied to the entries
 * is dependent upon the internal structure of the hash table.
 *
 * If apply returns a non-zero status, then hashtab_map will cease
 * iterating through the hash table and will propagate the error
 * return to its caller.
 */
int hashtab_map(struct hashtab *h,
		int (*apply)(void *k, void *d, void *args),
		void *args);

int hashtab_duplicate(struct hashtab *new, struct hashtab *orig,
		int (*copy)(struct hashtab_node *new,
			struct hashtab_node *orig, void *args),
		int (*destroy)(void *k, void *d, void *args),
		void *args);

#ifdef CONFIG_SECURITY_SELINUX_DEBUG
/* Fill info with some hash table statistics */
void hashtab_stat(struct hashtab *h, struct hashtab_info *info);
#else
static inline void hashtab_stat(struct hashtab *h, struct hashtab_info *info)
{
}
#endif

#endif	/* _SS_HASHTAB_H */
