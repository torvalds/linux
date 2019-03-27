/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Bismarck, ND. USA.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 **************************************************************************/

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Simple open hash tab implementation.
 *
 * Authors:
 * Thomas Hellström <thomas-at-tungstengraphics-dot-com>
 */

#include <dev/drm2/drmP.h>
#include <dev/drm2/drm_hashtab.h>

#include <sys/hash.h>

int drm_ht_create(struct drm_open_hash *ht, unsigned int order)
{
	ht->size = 1 << order;
	ht->order = order;
	ht->table = NULL;
	ht->table = hashinit_flags(ht->size, DRM_MEM_HASHTAB, &ht->mask,
	    HASH_NOWAIT);
	if (!ht->table) {
		DRM_ERROR("Out of memory for hash table\n");
		return -ENOMEM;
	}
	return 0;
}
EXPORT_SYMBOL(drm_ht_create);

void drm_ht_verbose_list(struct drm_open_hash *ht, unsigned long key)
{
	struct drm_hash_item *entry;
	struct drm_hash_item_list *h_list;
	unsigned int hashed_key;
	int count = 0;

	hashed_key = hash32_buf(&key, sizeof(key), ht->order);
	DRM_DEBUG("Key is 0x%08lx, Hashed key is 0x%08x\n", key, hashed_key);
	h_list = &ht->table[hashed_key & ht->mask];
	LIST_FOREACH(entry, h_list, head)
		DRM_DEBUG("count %d, key: 0x%08lx\n", count++, entry->key);
}

static struct drm_hash_item *drm_ht_find_key(struct drm_open_hash *ht,
					  unsigned long key)
{
	struct drm_hash_item *entry;
	struct drm_hash_item_list *h_list;
	unsigned int hashed_key;

	hashed_key = hash32_buf(&key, sizeof(key), ht->order);
	h_list = &ht->table[hashed_key & ht->mask];
	LIST_FOREACH(entry, h_list, head) {
		if (entry->key == key)
			return entry;
		if (entry->key > key)
			break;
	}
	return NULL;
}


int drm_ht_insert_item(struct drm_open_hash *ht, struct drm_hash_item *item)
{
	struct drm_hash_item *entry, *parent;
	struct drm_hash_item_list *h_list;
	unsigned int hashed_key;
	unsigned long key = item->key;

	hashed_key = hash32_buf(&key, sizeof(key), ht->order);
	h_list = &ht->table[hashed_key & ht->mask];
	parent = NULL;
	LIST_FOREACH(entry, h_list, head) {
		if (entry->key == key)
			return -EINVAL;
		if (entry->key > key)
			break;
		parent = entry;
	}
	if (parent) {
		LIST_INSERT_AFTER(parent, item, head);
	} else {
		LIST_INSERT_HEAD(h_list, item, head);
	}
	return 0;
}
EXPORT_SYMBOL(drm_ht_insert_item);

/*
 * Just insert an item and return any "bits" bit key that hasn't been
 * used before.
 */
int drm_ht_just_insert_please(struct drm_open_hash *ht, struct drm_hash_item *item,
			      unsigned long seed, int bits, int shift,
			      unsigned long add)
{
	int ret;
	unsigned long mask = (1 << bits) - 1;
	unsigned long first, unshifted_key = 0;

	unshifted_key = hash32_buf(&seed, sizeof(seed), unshifted_key);
	first = unshifted_key;
	do {
		item->key = (unshifted_key << shift) + add;
		ret = drm_ht_insert_item(ht, item);
		if (ret)
			unshifted_key = (unshifted_key + 1) & mask;
	} while(ret && (unshifted_key != first));

	if (ret) {
		DRM_ERROR("Available key bit space exhausted\n");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(drm_ht_just_insert_please);

int drm_ht_find_item(struct drm_open_hash *ht, unsigned long key,
		     struct drm_hash_item **item)
{
	struct drm_hash_item *entry;

	entry = drm_ht_find_key(ht, key);
	if (!entry)
		return -EINVAL;

	*item = entry;
	return 0;
}
EXPORT_SYMBOL(drm_ht_find_item);

int drm_ht_remove_key(struct drm_open_hash *ht, unsigned long key)
{
	struct drm_hash_item *entry;

	entry = drm_ht_find_key(ht, key);
	if (entry) {
		LIST_REMOVE(entry, head);
		return 0;
	}
	return -EINVAL;
}

int drm_ht_remove_item(struct drm_open_hash *ht, struct drm_hash_item *item)
{
	LIST_REMOVE(item, head);
	return 0;
}
EXPORT_SYMBOL(drm_ht_remove_item);

void drm_ht_remove(struct drm_open_hash *ht)
{
	if (ht->table) {
		hashdestroy(ht->table, DRM_MEM_HASHTAB, ht->mask);
		ht->table = NULL;
	}
}
EXPORT_SYMBOL(drm_ht_remove);
