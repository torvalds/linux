/* FS-Cache cache handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include <linux/slab.h>
#include "internal.h"

LIST_HEAD(fscache_cache_list);
DECLARE_RWSEM(fscache_addremove_sem);

static LIST_HEAD(fscache_cache_tag_list);

/*
 * look up a cache tag
 */
struct fscache_cache_tag *__fscache_lookup_cache_tag(const char *name)
{
	struct fscache_cache_tag *tag, *xtag;

	/* firstly check for the existence of the tag under read lock */
	down_read(&fscache_addremove_sem);

	list_for_each_entry(tag, &fscache_cache_tag_list, link) {
		if (strcmp(tag->name, name) == 0) {
			atomic_inc(&tag->usage);
			up_read(&fscache_addremove_sem);
			return tag;
		}
	}

	up_read(&fscache_addremove_sem);

	/* the tag does not exist - create a candidate */
	xtag = kzalloc(sizeof(*xtag) + strlen(name) + 1, GFP_KERNEL);
	if (!xtag)
		/* return a dummy tag if out of memory */
		return ERR_PTR(-ENOMEM);

	atomic_set(&xtag->usage, 1);
	strcpy(xtag->name, name);

	/* write lock, search again and add if still not present */
	down_write(&fscache_addremove_sem);

	list_for_each_entry(tag, &fscache_cache_tag_list, link) {
		if (strcmp(tag->name, name) == 0) {
			atomic_inc(&tag->usage);
			up_write(&fscache_addremove_sem);
			kfree(xtag);
			return tag;
		}
	}

	list_add_tail(&xtag->link, &fscache_cache_tag_list);
	up_write(&fscache_addremove_sem);
	return xtag;
}

/*
 * release a reference to a cache tag
 */
void __fscache_release_cache_tag(struct fscache_cache_tag *tag)
{
	if (tag != ERR_PTR(-ENOMEM)) {
		down_write(&fscache_addremove_sem);

		if (atomic_dec_and_test(&tag->usage))
			list_del_init(&tag->link);
		else
			tag = NULL;

		up_write(&fscache_addremove_sem);

		kfree(tag);
	}
}

/*
 * select a cache in which to store an object
 * - the cache addremove semaphore must be at least read-locked by the caller
 * - the object will never be an index
 */
struct fscache_cache *fscache_select_cache_for_object(
	struct fscache_cookie *cookie)
{
	struct fscache_cache_tag *tag;
	struct fscache_object *object;
	struct fscache_cache *cache;

	_enter("");

	if (list_empty(&fscache_cache_list)) {
		_leave(" = NULL [no cache]");
		return NULL;
	}

	/* we check the parent to determine the cache to use */
	spin_lock(&cookie->lock);

	/* the first in the parent's backing list should be the preferred
	 * cache */
	if (!hlist_empty(&cookie->backing_objects)) {
		object = hlist_entry(cookie->backing_objects.first,
				     struct fscache_object, cookie_link);

		cache = object->cache;
		if (object->state >= FSCACHE_OBJECT_DYING ||
		    test_bit(FSCACHE_IOERROR, &cache->flags))
			cache = NULL;

		spin_unlock(&cookie->lock);
		_leave(" = %p [parent]", cache);
		return cache;
	}

	/* the parent is unbacked */
	if (cookie->def->type != FSCACHE_COOKIE_TYPE_INDEX) {
		/* cookie not an index and is unbacked */
		spin_unlock(&cookie->lock);
		_leave(" = NULL [cookie ub,ni]");
		return NULL;
	}

	spin_unlock(&cookie->lock);

	if (!cookie->def->select_cache)
		goto no_preference;

	/* ask the netfs for its preference */
	tag = cookie->def->select_cache(cookie->parent->netfs_data,
					cookie->netfs_data);
	if (!tag)
		goto no_preference;

	if (tag == ERR_PTR(-ENOMEM)) {
		_leave(" = NULL [nomem tag]");
		return NULL;
	}

	if (!tag->cache) {
		_leave(" = NULL [unbacked tag]");
		return NULL;
	}

	if (test_bit(FSCACHE_IOERROR, &tag->cache->flags))
		return NULL;

	_leave(" = %p [specific]", tag->cache);
	return tag->cache;

no_preference:
	/* netfs has no preference - just select first cache */
	cache = list_entry(fscache_cache_list.next,
			   struct fscache_cache, link);
	_leave(" = %p [first]", cache);
	return cache;
}
