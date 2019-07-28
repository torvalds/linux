/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * Generic non-thread safe hash map implementation.
 *
 * Copyright (c) 2019 Facebook
 */
#ifndef __LIBBPF_HASHMAP_H
#define __LIBBPF_HASHMAP_H

#include <stdbool.h>
#include <stddef.h>
#include "libbpf_internal.h"

static inline size_t hash_bits(size_t h, int bits)
{
	/* shuffle bits and return requested number of upper bits */
	return (h * 11400714819323198485llu) >> (__WORDSIZE - bits);
}

typedef size_t (*hashmap_hash_fn)(const void *key, void *ctx);
typedef bool (*hashmap_equal_fn)(const void *key1, const void *key2, void *ctx);

struct hashmap_entry {
	const void *key;
	void *value;
	struct hashmap_entry *next;
};

struct hashmap {
	hashmap_hash_fn hash_fn;
	hashmap_equal_fn equal_fn;
	void *ctx;

	struct hashmap_entry **buckets;
	size_t cap;
	size_t cap_bits;
	size_t sz;
};

#define HASHMAP_INIT(hash_fn, equal_fn, ctx) {	\
	.hash_fn = (hash_fn),			\
	.equal_fn = (equal_fn),			\
	.ctx = (ctx),				\
	.buckets = NULL,			\
	.cap = 0,				\
	.cap_bits = 0,				\
	.sz = 0,				\
}

void hashmap__init(struct hashmap *map, hashmap_hash_fn hash_fn,
		   hashmap_equal_fn equal_fn, void *ctx);
struct hashmap *hashmap__new(hashmap_hash_fn hash_fn,
			     hashmap_equal_fn equal_fn,
			     void *ctx);
void hashmap__clear(struct hashmap *map);
void hashmap__free(struct hashmap *map);

size_t hashmap__size(const struct hashmap *map);
size_t hashmap__capacity(const struct hashmap *map);

/*
 * Hashmap insertion strategy:
 * - HASHMAP_ADD - only add key/value if key doesn't exist yet;
 * - HASHMAP_SET - add key/value pair if key doesn't exist yet; otherwise,
 *   update value;
 * - HASHMAP_UPDATE - update value, if key already exists; otherwise, do
 *   nothing and return -ENOENT;
 * - HASHMAP_APPEND - always add key/value pair, even if key already exists.
 *   This turns hashmap into a multimap by allowing multiple values to be
 *   associated with the same key. Most useful read API for such hashmap is
 *   hashmap__for_each_key_entry() iteration. If hashmap__find() is still
 *   used, it will return last inserted key/value entry (first in a bucket
 *   chain).
 */
enum hashmap_insert_strategy {
	HASHMAP_ADD,
	HASHMAP_SET,
	HASHMAP_UPDATE,
	HASHMAP_APPEND,
};

/*
 * hashmap__insert() adds key/value entry w/ various semantics, depending on
 * provided strategy value. If a given key/value pair replaced already
 * existing key/value pair, both old key and old value will be returned
 * through old_key and old_value to allow calling code do proper memory
 * management.
 */
int hashmap__insert(struct hashmap *map, const void *key, void *value,
		    enum hashmap_insert_strategy strategy,
		    const void **old_key, void **old_value);

static inline int hashmap__add(struct hashmap *map,
			       const void *key, void *value)
{
	return hashmap__insert(map, key, value, HASHMAP_ADD, NULL, NULL);
}

static inline int hashmap__set(struct hashmap *map,
			       const void *key, void *value,
			       const void **old_key, void **old_value)
{
	return hashmap__insert(map, key, value, HASHMAP_SET,
			       old_key, old_value);
}

static inline int hashmap__update(struct hashmap *map,
				  const void *key, void *value,
				  const void **old_key, void **old_value)
{
	return hashmap__insert(map, key, value, HASHMAP_UPDATE,
			       old_key, old_value);
}

static inline int hashmap__append(struct hashmap *map,
				  const void *key, void *value)
{
	return hashmap__insert(map, key, value, HASHMAP_APPEND, NULL, NULL);
}

bool hashmap__delete(struct hashmap *map, const void *key,
		     const void **old_key, void **old_value);

bool hashmap__find(const struct hashmap *map, const void *key, void **value);

/*
 * hashmap__for_each_entry - iterate over all entries in hashmap
 * @map: hashmap to iterate
 * @cur: struct hashmap_entry * used as a loop cursor
 * @bkt: integer used as a bucket loop cursor
 */
#define hashmap__for_each_entry(map, cur, bkt)				    \
	for (bkt = 0; bkt < map->cap; bkt++)				    \
		for (cur = map->buckets[bkt]; cur; cur = cur->next)

/*
 * hashmap__for_each_entry_safe - iterate over all entries in hashmap, safe
 * against removals
 * @map: hashmap to iterate
 * @cur: struct hashmap_entry * used as a loop cursor
 * @tmp: struct hashmap_entry * used as a temporary next cursor storage
 * @bkt: integer used as a bucket loop cursor
 */
#define hashmap__for_each_entry_safe(map, cur, tmp, bkt)		    \
	for (bkt = 0; bkt < map->cap; bkt++)				    \
		for (cur = map->buckets[bkt];				    \
		     cur && ({tmp = cur->next; true; });		    \
		     cur = tmp)

/*
 * hashmap__for_each_key_entry - iterate over entries associated with given key
 * @map: hashmap to iterate
 * @cur: struct hashmap_entry * used as a loop cursor
 * @key: key to iterate entries for
 */
#define hashmap__for_each_key_entry(map, cur, _key)			    \
	for (cur = ({ size_t bkt = hash_bits(map->hash_fn((_key), map->ctx),\
					     map->cap_bits);		    \
		     map->buckets ? map->buckets[bkt] : NULL; });	    \
	     cur;							    \
	     cur = cur->next)						    \
		if (map->equal_fn(cur->key, (_key), map->ctx))

#define hashmap__for_each_key_entry_safe(map, cur, tmp, _key)		    \
	for (cur = ({ size_t bkt = hash_bits(map->hash_fn((_key), map->ctx),\
					     map->cap_bits);		    \
		     cur = map->buckets ? map->buckets[bkt] : NULL; });	    \
	     cur && ({ tmp = cur->next; true; });			    \
	     cur = tmp)							    \
		if (map->equal_fn(cur->key, (_key), map->ctx))

#endif /* __LIBBPF_HASHMAP_H */
