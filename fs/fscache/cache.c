// SPDX-License-Identifier: GPL-2.0-or-later
/* FS-Cache cache handling
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define FSCACHE_DEBUG_LEVEL CACHE
#include <linux/module.h>
#include <linux/slab.h>
#include "internal.h"

LIST_HEAD(fscache_cache_list);
DECLARE_RWSEM(fscache_addremove_sem);
DECLARE_WAIT_QUEUE_HEAD(fscache_cache_cleared_wq);
EXPORT_SYMBOL(fscache_cache_cleared_wq);

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
		if (fscache_object_is_dying(object) ||
		    test_bit(FSCACHE_IOERROR, &cache->flags))
			cache = NULL;

		spin_unlock(&cookie->lock);
		_leave(" = %s [parent]", cache ? cache->tag->name : "NULL");
		return cache;
	}

	/* the parent is unbacked */
	if (cookie->type != FSCACHE_COOKIE_TYPE_INDEX) {
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

	_leave(" = %s [specific]", tag->name);
	return tag->cache;

no_preference:
	/* netfs has no preference - just select first cache */
	cache = list_entry(fscache_cache_list.next,
			   struct fscache_cache, link);
	_leave(" = %s [first]", cache->tag->name);
	return cache;
}

/**
 * fscache_init_cache - Initialise a cache record
 * @cache: The cache record to be initialised
 * @ops: The cache operations to be installed in that record
 * @idfmt: Format string to define identifier
 * @...: sprintf-style arguments
 *
 * Initialise a record of a cache and fill in the name.
 *
 * See Documentation/filesystems/caching/backend-api.rst for a complete
 * description.
 */
void fscache_init_cache(struct fscache_cache *cache,
			const struct fscache_cache_ops *ops,
			const char *idfmt,
			...)
{
	va_list va;

	memset(cache, 0, sizeof(*cache));

	cache->ops = ops;

	va_start(va, idfmt);
	vsnprintf(cache->identifier, sizeof(cache->identifier), idfmt, va);
	va_end(va);

	INIT_WORK(&cache->op_gc, fscache_operation_gc);
	INIT_LIST_HEAD(&cache->link);
	INIT_LIST_HEAD(&cache->object_list);
	INIT_LIST_HEAD(&cache->op_gc_list);
	spin_lock_init(&cache->object_list_lock);
	spin_lock_init(&cache->op_gc_list_lock);
}
EXPORT_SYMBOL(fscache_init_cache);

/**
 * fscache_add_cache - Declare a cache as being open for business
 * @cache: The record describing the cache
 * @ifsdef: The record of the cache object describing the top-level index
 * @tagname: The tag describing this cache
 *
 * Add a cache to the system, making it available for netfs's to use.
 *
 * See Documentation/filesystems/caching/backend-api.rst for a complete
 * description.
 */
int fscache_add_cache(struct fscache_cache *cache,
		      struct fscache_object *ifsdef,
		      const char *tagname)
{
	struct fscache_cache_tag *tag;

	ASSERTCMP(ifsdef->cookie, ==, &fscache_fsdef_index);
	BUG_ON(!cache->ops);
	BUG_ON(!ifsdef);

	cache->flags = 0;
	ifsdef->event_mask =
		((1 << NR_FSCACHE_OBJECT_EVENTS) - 1) &
		~(1 << FSCACHE_OBJECT_EV_CLEARED);
	__set_bit(FSCACHE_OBJECT_IS_AVAILABLE, &ifsdef->flags);

	if (!tagname)
		tagname = cache->identifier;

	BUG_ON(!tagname[0]);

	_enter("{%s.%s},,%s", cache->ops->name, cache->identifier, tagname);

	/* we use the cache tag to uniquely identify caches */
	tag = __fscache_lookup_cache_tag(tagname);
	if (IS_ERR(tag))
		goto nomem;

	if (test_and_set_bit(FSCACHE_TAG_RESERVED, &tag->flags))
		goto tag_in_use;

	cache->kobj = kobject_create_and_add(tagname, fscache_root);
	if (!cache->kobj)
		goto error;

	ifsdef->cache = cache;
	cache->fsdef = ifsdef;

	down_write(&fscache_addremove_sem);

	tag->cache = cache;
	cache->tag = tag;

	/* add the cache to the list */
	list_add(&cache->link, &fscache_cache_list);

	/* add the cache's netfs definition index object to the cache's
	 * list */
	spin_lock(&cache->object_list_lock);
	list_add_tail(&ifsdef->cache_link, &cache->object_list);
	spin_unlock(&cache->object_list_lock);

	/* add the cache's netfs definition index object to the top level index
	 * cookie as a known backing object */
	spin_lock(&fscache_fsdef_index.lock);

	hlist_add_head(&ifsdef->cookie_link,
		       &fscache_fsdef_index.backing_objects);

	refcount_inc(&fscache_fsdef_index.ref);

	/* done */
	spin_unlock(&fscache_fsdef_index.lock);
	up_write(&fscache_addremove_sem);

	pr_notice("Cache \"%s\" added (type %s)\n",
		  cache->tag->name, cache->ops->name);
	kobject_uevent(cache->kobj, KOBJ_ADD);

	_leave(" = 0 [%s]", cache->identifier);
	return 0;

tag_in_use:
	pr_err("Cache tag '%s' already in use\n", tagname);
	__fscache_release_cache_tag(tag);
	_leave(" = -EXIST");
	return -EEXIST;

error:
	__fscache_release_cache_tag(tag);
	_leave(" = -EINVAL");
	return -EINVAL;

nomem:
	_leave(" = -ENOMEM");
	return -ENOMEM;
}
EXPORT_SYMBOL(fscache_add_cache);

/**
 * fscache_io_error - Note a cache I/O error
 * @cache: The record describing the cache
 *
 * Note that an I/O error occurred in a cache and that it should no longer be
 * used for anything.  This also reports the error into the kernel log.
 *
 * See Documentation/filesystems/caching/backend-api.rst for a complete
 * description.
 */
void fscache_io_error(struct fscache_cache *cache)
{
	if (!test_and_set_bit(FSCACHE_IOERROR, &cache->flags))
		pr_err("Cache '%s' stopped due to I/O error\n",
		       cache->ops->name);
}
EXPORT_SYMBOL(fscache_io_error);

/*
 * request withdrawal of all the objects in a cache
 * - all the objects being withdrawn are moved onto the supplied list
 */
static void fscache_withdraw_all_objects(struct fscache_cache *cache,
					 struct list_head *dying_objects)
{
	struct fscache_object *object;

	while (!list_empty(&cache->object_list)) {
		spin_lock(&cache->object_list_lock);

		if (!list_empty(&cache->object_list)) {
			object = list_entry(cache->object_list.next,
					    struct fscache_object, cache_link);
			list_move_tail(&object->cache_link, dying_objects);

			_debug("withdraw %x", object->cookie->debug_id);

			/* This must be done under object_list_lock to prevent
			 * a race with fscache_drop_object().
			 */
			fscache_raise_event(object, FSCACHE_OBJECT_EV_KILL);
		}

		spin_unlock(&cache->object_list_lock);
		cond_resched();
	}
}

/**
 * fscache_withdraw_cache - Withdraw a cache from the active service
 * @cache: The record describing the cache
 *
 * Withdraw a cache from service, unbinding all its cache objects from the
 * netfs cookies they're currently representing.
 *
 * See Documentation/filesystems/caching/backend-api.rst for a complete
 * description.
 */
void fscache_withdraw_cache(struct fscache_cache *cache)
{
	LIST_HEAD(dying_objects);

	_enter("");

	pr_notice("Withdrawing cache \"%s\"\n",
		  cache->tag->name);

	/* make the cache unavailable for cookie acquisition */
	if (test_and_set_bit(FSCACHE_CACHE_WITHDRAWN, &cache->flags))
		BUG();

	down_write(&fscache_addremove_sem);
	list_del_init(&cache->link);
	cache->tag->cache = NULL;
	up_write(&fscache_addremove_sem);

	/* make sure all pages pinned by operations on behalf of the netfs are
	 * written to disk */
	fscache_stat(&fscache_n_cop_sync_cache);
	cache->ops->sync_cache(cache);
	fscache_stat_d(&fscache_n_cop_sync_cache);

	/* dissociate all the netfs pages backed by this cache from the block
	 * mappings in the cache */
	fscache_stat(&fscache_n_cop_dissociate_pages);
	cache->ops->dissociate_pages(cache);
	fscache_stat_d(&fscache_n_cop_dissociate_pages);

	/* we now have to destroy all the active objects pertaining to this
	 * cache - which we do by passing them off to thread pool to be
	 * disposed of */
	_debug("destroy");

	fscache_withdraw_all_objects(cache, &dying_objects);

	/* wait for all extant objects to finish their outstanding operations
	 * and go away */
	_debug("wait for finish");
	wait_event(fscache_cache_cleared_wq,
		   atomic_read(&cache->object_count) == 0);
	_debug("wait for clearance");
	wait_event(fscache_cache_cleared_wq,
		   list_empty(&cache->object_list));
	_debug("cleared");
	ASSERT(list_empty(&dying_objects));

	kobject_put(cache->kobj);

	clear_bit(FSCACHE_TAG_RESERVED, &cache->tag->flags);
	fscache_release_cache_tag(cache->tag);
	cache->tag = NULL;

	_leave("");
}
EXPORT_SYMBOL(fscache_withdraw_cache);
