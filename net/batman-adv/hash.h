/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2006-2019  B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich, Marek Lindner
 */

#ifndef _NET_BATMAN_ADV_HASH_H_
#define _NET_BATMAN_ADV_HASH_H_

#include "main.h"

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/spinlock.h>
#include <linux/stddef.h>
#include <linux/types.h>

struct lock_class_key;

/* callback to a compare function.  should compare 2 element datas for their
 * keys
 *
 * Return: true if same and false if not same
 */
typedef bool (*batadv_hashdata_compare_cb)(const struct hlist_node *,
					   const void *);

/* the hashfunction
 *
 * Return: an index based on the key in the data of the first argument and the
 * size the second
 */
typedef u32 (*batadv_hashdata_choose_cb)(const void *, u32);
typedef void (*batadv_hashdata_free_cb)(struct hlist_node *, void *);

/**
 * struct batadv_hashtable - Wrapper of simple hlist based hashtable
 */
struct batadv_hashtable {
	/** @table: the hashtable itself with the buckets */
	struct hlist_head *table;

	/** @list_locks: spinlock for each hash list entry */
	spinlock_t *list_locks;

	/** @size: size of hashtable */
	u32 size;

	/** @generation: current (generation) sequence number */
	atomic_t generation;
};

/* allocates and clears the hash */
struct batadv_hashtable *batadv_hash_new(u32 size);

/* set class key for all locks */
void batadv_hash_set_lock_class(struct batadv_hashtable *hash,
				struct lock_class_key *key);

/* free only the hashtable and the hash itself. */
void batadv_hash_destroy(struct batadv_hashtable *hash);

/**
 *	batadv_hash_add() - adds data to the hashtable
 *	@hash: storage hash table
 *	@compare: callback to determine if 2 hash elements are identical
 *	@choose: callback calculating the hash index
 *	@data: data passed to the aforementioned callbacks as argument
 *	@data_node: to be added element
 *
 *	Return: 0 on success, 1 if the element already is in the hash
 *	and -1 on error.
 */
static inline int batadv_hash_add(struct batadv_hashtable *hash,
				  batadv_hashdata_compare_cb compare,
				  batadv_hashdata_choose_cb choose,
				  const void *data,
				  struct hlist_node *data_node)
{
	u32 index;
	int ret = -1;
	struct hlist_head *head;
	struct hlist_node *node;
	spinlock_t *list_lock; /* spinlock to protect write access */

	if (!hash)
		goto out;

	index = choose(data, hash->size);
	head = &hash->table[index];
	list_lock = &hash->list_locks[index];

	spin_lock_bh(list_lock);

	hlist_for_each(node, head) {
		if (!compare(node, data))
			continue;

		ret = 1;
		goto unlock;
	}

	/* no duplicate found in list, add new element */
	hlist_add_head_rcu(data_node, head);
	atomic_inc(&hash->generation);

	ret = 0;

unlock:
	spin_unlock_bh(list_lock);
out:
	return ret;
}

/**
 * batadv_hash_remove() - Removes data from hash, if found
 * @hash: hash table
 * @compare: callback to determine if 2 hash elements are identical
 * @choose: callback calculating the hash index
 * @data: data passed to the aforementioned callbacks as argument
 *
 * ata could be the structure you use with  just the key filled, we just need
 * the key for comparing.
 *
 * Return: returns pointer do data on success, so you can remove the used
 * structure yourself, or NULL on error
 */
static inline void *batadv_hash_remove(struct batadv_hashtable *hash,
				       batadv_hashdata_compare_cb compare,
				       batadv_hashdata_choose_cb choose,
				       void *data)
{
	u32 index;
	struct hlist_node *node;
	struct hlist_head *head;
	void *data_save = NULL;

	index = choose(data, hash->size);
	head = &hash->table[index];

	spin_lock_bh(&hash->list_locks[index]);
	hlist_for_each(node, head) {
		if (!compare(node, data))
			continue;

		data_save = node;
		hlist_del_rcu(node);
		atomic_inc(&hash->generation);
		break;
	}
	spin_unlock_bh(&hash->list_locks[index]);

	return data_save;
}

#endif /* _NET_BATMAN_ADV_HASH_H_ */
