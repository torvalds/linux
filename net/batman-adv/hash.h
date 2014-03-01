/* Copyright (C) 2006-2014 B.A.T.M.A.N. contributors:
 *
 * Simon Wunderlich, Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _NET_BATMAN_ADV_HASH_H_
#define _NET_BATMAN_ADV_HASH_H_

#include <linux/list.h>

/* callback to a compare function.  should compare 2 element datas for their
 * keys, return 0 if same and not 0 if not same
 */
typedef int (*batadv_hashdata_compare_cb)(const struct hlist_node *,
					  const void *);

/* the hashfunction, should return an index
 * based on the key in the data of the first
 * argument and the size the second
 */
typedef uint32_t (*batadv_hashdata_choose_cb)(const void *, uint32_t);
typedef void (*batadv_hashdata_free_cb)(struct hlist_node *, void *);

struct batadv_hashtable {
	struct hlist_head *table;   /* the hashtable itself with the buckets */
	spinlock_t *list_locks;     /* spinlock for each hash list entry */
	uint32_t size;		    /* size of hashtable */
};

/* allocates and clears the hash */
struct batadv_hashtable *batadv_hash_new(uint32_t size);

/* set class key for all locks */
void batadv_hash_set_lock_class(struct batadv_hashtable *hash,
				struct lock_class_key *key);

/* free only the hashtable and the hash itself. */
void batadv_hash_destroy(struct batadv_hashtable *hash);

/* remove the hash structure. if hashdata_free_cb != NULL, this function will be
 * called to remove the elements inside of the hash.  if you don't remove the
 * elements, memory might be leaked.
 */
static inline void batadv_hash_delete(struct batadv_hashtable *hash,
				      batadv_hashdata_free_cb free_cb,
				      void *arg)
{
	struct hlist_head *head;
	struct hlist_node *node, *node_tmp;
	spinlock_t *list_lock; /* spinlock to protect write access */
	uint32_t i;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];
		list_lock = &hash->list_locks[i];

		spin_lock_bh(list_lock);
		hlist_for_each_safe(node, node_tmp, head) {
			hlist_del_rcu(node);

			if (free_cb)
				free_cb(node, arg);
		}
		spin_unlock_bh(list_lock);
	}

	batadv_hash_destroy(hash);
}

/**
 *	batadv_hash_bytes - hash some bytes and add them to the previous hash
 *	@hash: previous hash value
 *	@data: data to be hashed
 *	@size: number of bytes to be hashed
 *
 *	Returns the new hash value.
 */
static inline uint32_t batadv_hash_bytes(uint32_t hash, const void *data,
					 uint32_t size)
{
	const unsigned char *key = data;
	int i;

	for (i = 0; i < size; i++) {
		hash += key[i];
		hash += (hash << 10);
		hash ^= (hash >> 6);
	}
	return hash;
}

/**
 *	batadv_hash_add - adds data to the hashtable
 *	@hash: storage hash table
 *	@compare: callback to determine if 2 hash elements are identical
 *	@choose: callback calculating the hash index
 *	@data: data passed to the aforementioned callbacks as argument
 *	@data_node: to be added element
 *
 *	Returns 0 on success, 1 if the element already is in the hash
 *	and -1 on error.
 */
static inline int batadv_hash_add(struct batadv_hashtable *hash,
				  batadv_hashdata_compare_cb compare,
				  batadv_hashdata_choose_cb choose,
				  const void *data,
				  struct hlist_node *data_node)
{
	uint32_t index;
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

	ret = 0;

unlock:
	spin_unlock_bh(list_lock);
out:
	return ret;
}

/* removes data from hash, if found. returns pointer do data on success, so you
 * can remove the used structure yourself, or NULL on error .  data could be the
 * structure you use with just the key filled, we just need the key for
 * comparing.
 */
static inline void *batadv_hash_remove(struct batadv_hashtable *hash,
				       batadv_hashdata_compare_cb compare,
				       batadv_hashdata_choose_cb choose,
				       void *data)
{
	uint32_t index;
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
		break;
	}
	spin_unlock_bh(&hash->list_locks[index]);

	return data_save;
}

#endif /* _NET_BATMAN_ADV_HASH_H_ */
