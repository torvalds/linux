// SPDX-License-Identifier: GPL-2.0-or-later
/* FS-Cache interface to CacheFiles
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <linux/mount.h>
#include <linux/xattr.h>
#include <linux/file.h>
#include <linux/falloc.h>
#include <trace/events/fscache.h>
#include "internal.h"

static atomic_t cachefiles_object_debug_id;

/*
 * Allocate a cache object record.
 */
static
struct cachefiles_object *cachefiles_alloc_object(struct fscache_cookie *cookie)
{
	struct fscache_volume *vcookie = cookie->volume;
	struct cachefiles_volume *volume = vcookie->cache_priv;
	struct cachefiles_object *object;

	_enter("{%s},%x,", vcookie->key, cookie->debug_id);

	object = kmem_cache_zalloc(cachefiles_object_jar, GFP_KERNEL);
	if (!object)
		return NULL;

	refcount_set(&object->ref, 1);

	spin_lock_init(&object->lock);
	INIT_LIST_HEAD(&object->cache_link);
	object->volume = volume;
	object->debug_id = atomic_inc_return(&cachefiles_object_debug_id);
	object->cookie = fscache_get_cookie(cookie, fscache_cookie_get_attach_object);

	fscache_count_object(vcookie->cache);
	trace_cachefiles_ref(object->debug_id, cookie->debug_id, 1,
			     cachefiles_obj_new);
	return object;
}

/*
 * Note that an object has been seen.
 */
void cachefiles_see_object(struct cachefiles_object *object,
			   enum cachefiles_obj_ref_trace why)
{
	trace_cachefiles_ref(object->debug_id, object->cookie->debug_id,
			     refcount_read(&object->ref), why);
}

/*
 * Increment the usage count on an object;
 */
struct cachefiles_object *cachefiles_grab_object(struct cachefiles_object *object,
						 enum cachefiles_obj_ref_trace why)
{
	int r;

	__refcount_inc(&object->ref, &r);
	trace_cachefiles_ref(object->debug_id, object->cookie->debug_id, r, why);
	return object;
}

/*
 * dispose of a reference to an object
 */
void cachefiles_put_object(struct cachefiles_object *object,
			   enum cachefiles_obj_ref_trace why)
{
	unsigned int object_debug_id = object->debug_id;
	unsigned int cookie_debug_id = object->cookie->debug_id;
	struct fscache_cache *cache;
	bool done;
	int r;

	done = __refcount_dec_and_test(&object->ref, &r);
	trace_cachefiles_ref(object_debug_id, cookie_debug_id, r, why);
	if (done) {
		_debug("- kill object OBJ%x", object_debug_id);

		ASSERTCMP(object->file, ==, NULL);

		kfree(object->d_name);

		cache = object->volume->cache->cache;
		fscache_put_cookie(object->cookie, fscache_cookie_put_object);
		object->cookie = NULL;
		kmem_cache_free(cachefiles_object_jar, object);
		fscache_uncount_object(cache);
	}

	_leave("");
}

const struct fscache_cache_ops cachefiles_cache_ops = {
	.name			= "cachefiles",
	.acquire_volume		= cachefiles_acquire_volume,
	.free_volume		= cachefiles_free_volume,
};
