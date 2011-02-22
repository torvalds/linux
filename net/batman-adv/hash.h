/*
 * Copyright (C) 2006-2010 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#ifndef _NET_BATMAN_ADV_HASH_H_
#define _NET_BATMAN_ADV_HASH_H_

#include <linux/list.h>

/* callback to a compare function.  should
 * compare 2 element datas for their keys,
 * return 0 if same and not 0 if not
 * same */
typedef int (*hashdata_compare_cb)(void *, void *);

/* the hashfunction, should return an index
 * based on the key in the data of the first
 * argument and the size the second */
typedef int (*hashdata_choose_cb)(void *, int);
typedef void (*hashdata_free_cb)(void *, void *);

struct element_t {
	void *data;		/* pointer to the data */
	struct hlist_node hlist;	/* bucket list pointer */
};

struct hashtable_t {
	struct hlist_head *table;   /* the hashtable itself, with the buckets */
	int size;		    /* size of hashtable */
};

/* allocates and clears the hash */
struct hashtable_t *hash_new(int size);

/* remove element if you already found the element you want to delete and don't
 * need the overhead to find it again with hash_remove().  But usually, you
 * don't want to use this function, as it fiddles with hash-internals. */
void *hash_remove_element(struct hashtable_t *hash, struct element_t *elem);

/* free only the hashtable and the hash itself. */
void hash_destroy(struct hashtable_t *hash);

/* remove the hash structure. if hashdata_free_cb != NULL, this function will be
 * called to remove the elements inside of the hash.  if you don't remove the
 * elements, memory might be leaked. */
static inline void hash_delete(struct hashtable_t *hash,
			       hashdata_free_cb free_cb, void *arg)
{
	struct hlist_head *head;
	struct hlist_node *walk, *safe;
	struct element_t *bucket;
	int i;

	for (i = 0; i < hash->size; i++) {
		head = &hash->table[i];

		hlist_for_each_safe(walk, safe, head) {
			bucket = hlist_entry(walk, struct element_t, hlist);
			if (free_cb)
				free_cb(bucket->data, arg);

			hlist_del(walk);
			kfree(bucket);
		}
	}

	hash_destroy(hash);
}

/* adds data to the hashtable. returns 0 on success, -1 on error */
static inline int hash_add(struct hashtable_t *hash,
			   hashdata_compare_cb compare,
			   hashdata_choose_cb choose, void *data)
{
	int index;
	struct hlist_head *head;
	struct hlist_node *walk, *safe;
	struct element_t *bucket;

	if (!hash)
		return -1;

	index = choose(data, hash->size);
	head = &hash->table[index];

	hlist_for_each_safe(walk, safe, head) {
		bucket = hlist_entry(walk, struct element_t, hlist);
		if (compare(bucket->data, data))
			return -1;
	}

	/* no duplicate found in list, add new element */
	bucket = kmalloc(sizeof(struct element_t), GFP_ATOMIC);

	if (!bucket)
		return -1;

	bucket->data = data;
	hlist_add_head(&bucket->hlist, head);

	return 0;
}

/* removes data from hash, if found. returns pointer do data on success, so you
 * can remove the used structure yourself, or NULL on error .  data could be the
 * structure you use with just the key filled, we just need the key for
 * comparing. */
static inline void *hash_remove(struct hashtable_t *hash,
				hashdata_compare_cb compare,
				hashdata_choose_cb choose, void *data)
{
	size_t index;
	struct hlist_node *walk;
	struct element_t *bucket;
	struct hlist_head *head;
	void *data_save;

	index = choose(data, hash->size);
	head = &hash->table[index];

	hlist_for_each_entry(bucket, walk, head, hlist) {
		if (compare(bucket->data, data)) {
			data_save = bucket->data;
			hlist_del(walk);
			kfree(bucket);
			return data_save;
		}
	}

	return NULL;
}

/* finds data, based on the key in keydata. returns the found data on success,
 * or NULL on error */
static inline void *hash_find(struct hashtable_t *hash,
			      hashdata_compare_cb compare,
			      hashdata_choose_cb choose, void *keydata)
{
	int index;
	struct hlist_head *head;
	struct hlist_node *walk;
	struct element_t *bucket;

	if (!hash)
		return NULL;

	index = choose(keydata , hash->size);
	head = &hash->table[index];

	hlist_for_each(walk, head) {
		bucket = hlist_entry(walk, struct element_t, hlist);
		if (compare(bucket->data, keydata))
			return bucket->data;
	}

	return NULL;
}

#endif /* _NET_BATMAN_ADV_HASH_H_ */
