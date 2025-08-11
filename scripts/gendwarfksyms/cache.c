// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Google LLC
 */

#include "gendwarfksyms.h"

struct cache_item {
	unsigned long key;
	int value;
	struct hlist_node hash;
};

void cache_set(struct cache *cache, unsigned long key, int value)
{
	struct cache_item *ci;

	ci = xmalloc(sizeof(*ci));
	ci->key = key;
	ci->value = value;
	hash_add(cache->cache, &ci->hash, hash_32(key));
}

int cache_get(struct cache *cache, unsigned long key)
{
	struct cache_item *ci;

	hash_for_each_possible(cache->cache, ci, hash, hash_32(key)) {
		if (ci->key == key)
			return ci->value;
	}

	return -1;
}

void cache_init(struct cache *cache)
{
	hash_init(cache->cache);
}

void cache_free(struct cache *cache)
{
	struct hlist_node *tmp;
	struct cache_item *ci;

	hash_for_each_safe(cache->cache, ci, tmp, hash) {
		free(ci);
	}

	hash_init(cache->cache);
}
