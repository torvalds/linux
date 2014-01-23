/* FS-Cache interface to CacheFiles
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/mount.h>
#include "internal.h"

struct cachefiles_lookup_data {
	struct cachefiles_xattr	*auxdata;	/* auxiliary data */
	char			*key;		/* key path */
};

static int cachefiles_attr_changed(struct fscache_object *_object);

/*
 * allocate an object record for a cookie lookup and prepare the lookup data
 */
static struct fscache_object *cachefiles_alloc_object(
	struct fscache_cache *_cache,
	struct fscache_cookie *cookie)
{
	struct cachefiles_lookup_data *lookup_data;
	struct cachefiles_object *object;
	struct cachefiles_cache *cache;
	struct cachefiles_xattr *auxdata;
	unsigned keylen, auxlen;
	void *buffer;
	char *key;

	cache = container_of(_cache, struct cachefiles_cache, cache);

	_enter("{%s},%p,", cache->cache.identifier, cookie);

	lookup_data = kmalloc(sizeof(*lookup_data), cachefiles_gfp);
	if (!lookup_data)
		goto nomem_lookup_data;

	/* create a new object record and a temporary leaf image */
	object = kmem_cache_alloc(cachefiles_object_jar, cachefiles_gfp);
	if (!object)
		goto nomem_object;

	ASSERTCMP(object->backer, ==, NULL);

	BUG_ON(test_bit(CACHEFILES_OBJECT_ACTIVE, &object->flags));
	atomic_set(&object->usage, 1);

	fscache_object_init(&object->fscache, cookie, &cache->cache);

	object->type = cookie->def->type;

	/* get hold of the raw key
	 * - stick the length on the front and leave space on the back for the
	 *   encoder
	 */
	buffer = kmalloc((2 + 512) + 3, cachefiles_gfp);
	if (!buffer)
		goto nomem_buffer;

	keylen = cookie->def->get_key(cookie->netfs_data, buffer + 2, 512);
	ASSERTCMP(keylen, <, 512);

	*(uint16_t *)buffer = keylen;
	((char *)buffer)[keylen + 2] = 0;
	((char *)buffer)[keylen + 3] = 0;
	((char *)buffer)[keylen + 4] = 0;

	/* turn the raw key into something that can work with as a filename */
	key = cachefiles_cook_key(buffer, keylen + 2, object->type);
	if (!key)
		goto nomem_key;

	/* get hold of the auxiliary data and prepend the object type */
	auxdata = buffer;
	auxlen = 0;
	if (cookie->def->get_aux) {
		auxlen = cookie->def->get_aux(cookie->netfs_data,
					      auxdata->data, 511);
		ASSERTCMP(auxlen, <, 511);
	}

	auxdata->len = auxlen + 1;
	auxdata->type = cookie->def->type;

	lookup_data->auxdata = auxdata;
	lookup_data->key = key;
	object->lookup_data = lookup_data;

	_leave(" = %p [%p]", &object->fscache, lookup_data);
	return &object->fscache;

nomem_key:
	kfree(buffer);
nomem_buffer:
	BUG_ON(test_bit(CACHEFILES_OBJECT_ACTIVE, &object->flags));
	kmem_cache_free(cachefiles_object_jar, object);
	fscache_object_destroyed(&cache->cache);
nomem_object:
	kfree(lookup_data);
nomem_lookup_data:
	_leave(" = -ENOMEM");
	return ERR_PTR(-ENOMEM);
}

/*
 * attempt to look up the nominated node in this cache
 * - return -ETIMEDOUT to be scheduled again
 */
static int cachefiles_lookup_object(struct fscache_object *_object)
{
	struct cachefiles_lookup_data *lookup_data;
	struct cachefiles_object *parent, *object;
	struct cachefiles_cache *cache;
	const struct cred *saved_cred;
	int ret;

	_enter("{OBJ%x}", _object->debug_id);

	cache = container_of(_object->cache, struct cachefiles_cache, cache);
	parent = container_of(_object->parent,
			      struct cachefiles_object, fscache);
	object = container_of(_object, struct cachefiles_object, fscache);
	lookup_data = object->lookup_data;

	ASSERTCMP(lookup_data, !=, NULL);

	/* look up the key, creating any missing bits */
	cachefiles_begin_secure(cache, &saved_cred);
	ret = cachefiles_walk_to_object(parent, object,
					lookup_data->key,
					lookup_data->auxdata);
	cachefiles_end_secure(cache, saved_cred);

	/* polish off by setting the attributes of non-index files */
	if (ret == 0 &&
	    object->fscache.cookie->def->type != FSCACHE_COOKIE_TYPE_INDEX)
		cachefiles_attr_changed(&object->fscache);

	if (ret < 0 && ret != -ETIMEDOUT) {
		if (ret != -ENOBUFS)
			printk(KERN_WARNING
			       "CacheFiles: Lookup failed error %d\n", ret);
		fscache_object_lookup_error(&object->fscache);
	}

	_leave(" [%d]", ret);
	return ret;
}

/*
 * indication of lookup completion
 */
static void cachefiles_lookup_complete(struct fscache_object *_object)
{
	struct cachefiles_object *object;

	object = container_of(_object, struct cachefiles_object, fscache);

	_enter("{OBJ%x,%p}", object->fscache.debug_id, object->lookup_data);

	if (object->lookup_data) {
		kfree(object->lookup_data->key);
		kfree(object->lookup_data->auxdata);
		kfree(object->lookup_data);
		object->lookup_data = NULL;
	}
}

/*
 * increment the usage count on an inode object (may fail if unmounting)
 */
static
struct fscache_object *cachefiles_grab_object(struct fscache_object *_object)
{
	struct cachefiles_object *object =
		container_of(_object, struct cachefiles_object, fscache);

	_enter("{OBJ%x,%d}", _object->debug_id, atomic_read(&object->usage));

#ifdef CACHEFILES_DEBUG_SLAB
	ASSERT((atomic_read(&object->usage) & 0xffff0000) != 0x6b6b0000);
#endif

	atomic_inc(&object->usage);
	return &object->fscache;
}

/*
 * update the auxiliary data for an object object on disk
 */
static void cachefiles_update_object(struct fscache_object *_object)
{
	struct cachefiles_object *object;
	struct cachefiles_xattr *auxdata;
	struct cachefiles_cache *cache;
	struct fscache_cookie *cookie;
	const struct cred *saved_cred;
	unsigned auxlen;

	_enter("{OBJ%x}", _object->debug_id);

	object = container_of(_object, struct cachefiles_object, fscache);
	cache = container_of(object->fscache.cache, struct cachefiles_cache,
			     cache);

	if (!fscache_use_cookie(_object)) {
		_leave(" [relinq]");
		return;
	}

	cookie = object->fscache.cookie;

	if (!cookie->def->get_aux) {
		fscache_unuse_cookie(_object);
		_leave(" [no aux]");
		return;
	}

	auxdata = kmalloc(2 + 512 + 3, cachefiles_gfp);
	if (!auxdata) {
		fscache_unuse_cookie(_object);
		_leave(" [nomem]");
		return;
	}

	auxlen = cookie->def->get_aux(cookie->netfs_data, auxdata->data, 511);
	fscache_unuse_cookie(_object);
	ASSERTCMP(auxlen, <, 511);

	auxdata->len = auxlen + 1;
	auxdata->type = cookie->def->type;

	cachefiles_begin_secure(cache, &saved_cred);
	cachefiles_update_object_xattr(object, auxdata);
	cachefiles_end_secure(cache, saved_cred);
	kfree(auxdata);
	_leave("");
}

/*
 * discard the resources pinned by an object and effect retirement if
 * requested
 */
static void cachefiles_drop_object(struct fscache_object *_object)
{
	struct cachefiles_object *object;
	struct cachefiles_cache *cache;
	const struct cred *saved_cred;

	ASSERT(_object);

	object = container_of(_object, struct cachefiles_object, fscache);

	_enter("{OBJ%x,%d}",
	       object->fscache.debug_id, atomic_read(&object->usage));

	cache = container_of(object->fscache.cache,
			     struct cachefiles_cache, cache);

#ifdef CACHEFILES_DEBUG_SLAB
	ASSERT((atomic_read(&object->usage) & 0xffff0000) != 0x6b6b0000);
#endif

	/* delete retired objects */
	if (test_bit(FSCACHE_OBJECT_RETIRED, &object->fscache.flags) &&
	    _object != cache->cache.fsdef
	    ) {
		_debug("- retire object OBJ%x", object->fscache.debug_id);
		cachefiles_begin_secure(cache, &saved_cred);
		cachefiles_delete_object(cache, object);
		cachefiles_end_secure(cache, saved_cred);
	}

	/* close the filesystem stuff attached to the object */
	if (object->backer != object->dentry)
		dput(object->backer);
	object->backer = NULL;

	/* note that the object is now inactive */
	if (test_bit(CACHEFILES_OBJECT_ACTIVE, &object->flags)) {
		write_lock(&cache->active_lock);
		if (!test_and_clear_bit(CACHEFILES_OBJECT_ACTIVE,
					&object->flags))
			BUG();
		rb_erase(&object->active_node, &cache->active_nodes);
		wake_up_bit(&object->flags, CACHEFILES_OBJECT_ACTIVE);
		write_unlock(&cache->active_lock);
	}

	dput(object->dentry);
	object->dentry = NULL;

	_leave("");
}

/*
 * dispose of a reference to an object
 */
static void cachefiles_put_object(struct fscache_object *_object)
{
	struct cachefiles_object *object;
	struct fscache_cache *cache;

	ASSERT(_object);

	object = container_of(_object, struct cachefiles_object, fscache);

	_enter("{OBJ%x,%d}",
	       object->fscache.debug_id, atomic_read(&object->usage));

#ifdef CACHEFILES_DEBUG_SLAB
	ASSERT((atomic_read(&object->usage) & 0xffff0000) != 0x6b6b0000);
#endif

	ASSERTIFCMP(object->fscache.parent,
		    object->fscache.parent->n_children, >, 0);

	if (atomic_dec_and_test(&object->usage)) {
		_debug("- kill object OBJ%x", object->fscache.debug_id);

		ASSERT(!test_bit(CACHEFILES_OBJECT_ACTIVE, &object->flags));
		ASSERTCMP(object->fscache.parent, ==, NULL);
		ASSERTCMP(object->backer, ==, NULL);
		ASSERTCMP(object->dentry, ==, NULL);
		ASSERTCMP(object->fscache.n_ops, ==, 0);
		ASSERTCMP(object->fscache.n_children, ==, 0);

		if (object->lookup_data) {
			kfree(object->lookup_data->key);
			kfree(object->lookup_data->auxdata);
			kfree(object->lookup_data);
			object->lookup_data = NULL;
		}

		cache = object->fscache.cache;
		fscache_object_destroy(&object->fscache);
		kmem_cache_free(cachefiles_object_jar, object);
		fscache_object_destroyed(cache);
	}

	_leave("");
}

/*
 * sync a cache
 */
static void cachefiles_sync_cache(struct fscache_cache *_cache)
{
	struct cachefiles_cache *cache;
	const struct cred *saved_cred;
	int ret;

	_enter("%p", _cache);

	cache = container_of(_cache, struct cachefiles_cache, cache);

	/* make sure all pages pinned by operations on behalf of the netfs are
	 * written to disc */
	cachefiles_begin_secure(cache, &saved_cred);
	down_read(&cache->mnt->mnt_sb->s_umount);
	ret = sync_filesystem(cache->mnt->mnt_sb);
	up_read(&cache->mnt->mnt_sb->s_umount);
	cachefiles_end_secure(cache, saved_cred);

	if (ret == -EIO)
		cachefiles_io_error(cache,
				    "Attempt to sync backing fs superblock"
				    " returned error %d",
				    ret);
}

/*
 * check if the backing cache is updated to FS-Cache
 * - called by FS-Cache when evaluates if need to invalidate the cache
 */
static bool cachefiles_check_consistency(struct fscache_operation *op)
{
	struct cachefiles_object *object;
	struct cachefiles_cache *cache;
	const struct cred *saved_cred;
	int ret;

	_enter("{OBJ%x}", op->object->debug_id);

	object = container_of(op->object, struct cachefiles_object, fscache);
	cache = container_of(object->fscache.cache,
			     struct cachefiles_cache, cache);

	cachefiles_begin_secure(cache, &saved_cred);
	ret = cachefiles_check_auxdata(object);
	cachefiles_end_secure(cache, saved_cred);

	_leave(" = %d", ret);
	return ret;
}

/*
 * notification the attributes on an object have changed
 * - called with reads/writes excluded by FS-Cache
 */
static int cachefiles_attr_changed(struct fscache_object *_object)
{
	struct cachefiles_object *object;
	struct cachefiles_cache *cache;
	const struct cred *saved_cred;
	struct iattr newattrs;
	uint64_t ni_size;
	loff_t oi_size;
	int ret;

	_object->cookie->def->get_attr(_object->cookie->netfs_data, &ni_size);

	_enter("{OBJ%x},[%llu]",
	       _object->debug_id, (unsigned long long) ni_size);

	object = container_of(_object, struct cachefiles_object, fscache);
	cache = container_of(object->fscache.cache,
			     struct cachefiles_cache, cache);

	if (ni_size == object->i_size)
		return 0;

	if (!object->backer)
		return -ENOBUFS;

	ASSERT(S_ISREG(object->backer->d_inode->i_mode));

	fscache_set_store_limit(&object->fscache, ni_size);

	oi_size = i_size_read(object->backer->d_inode);
	if (oi_size == ni_size)
		return 0;

	cachefiles_begin_secure(cache, &saved_cred);
	mutex_lock(&object->backer->d_inode->i_mutex);

	/* if there's an extension to a partial page at the end of the backing
	 * file, we need to discard the partial page so that we pick up new
	 * data after it */
	if (oi_size & ~PAGE_MASK && ni_size > oi_size) {
		_debug("discard tail %llx", oi_size);
		newattrs.ia_valid = ATTR_SIZE;
		newattrs.ia_size = oi_size & PAGE_MASK;
		ret = notify_change(object->backer, &newattrs, NULL);
		if (ret < 0)
			goto truncate_failed;
	}

	newattrs.ia_valid = ATTR_SIZE;
	newattrs.ia_size = ni_size;
	ret = notify_change(object->backer, &newattrs, NULL);

truncate_failed:
	mutex_unlock(&object->backer->d_inode->i_mutex);
	cachefiles_end_secure(cache, saved_cred);

	if (ret == -EIO) {
		fscache_set_store_limit(&object->fscache, 0);
		cachefiles_io_error_obj(object, "Size set failed");
		ret = -ENOBUFS;
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * Invalidate an object
 */
static void cachefiles_invalidate_object(struct fscache_operation *op)
{
	struct cachefiles_object *object;
	struct cachefiles_cache *cache;
	const struct cred *saved_cred;
	struct path path;
	uint64_t ni_size;
	int ret;

	object = container_of(op->object, struct cachefiles_object, fscache);
	cache = container_of(object->fscache.cache,
			     struct cachefiles_cache, cache);

	op->object->cookie->def->get_attr(op->object->cookie->netfs_data,
					  &ni_size);

	_enter("{OBJ%x},[%llu]",
	       op->object->debug_id, (unsigned long long)ni_size);

	if (object->backer) {
		ASSERT(S_ISREG(object->backer->d_inode->i_mode));

		fscache_set_store_limit(&object->fscache, ni_size);

		path.dentry = object->backer;
		path.mnt = cache->mnt;

		cachefiles_begin_secure(cache, &saved_cred);
		ret = vfs_truncate(&path, 0);
		if (ret == 0)
			ret = vfs_truncate(&path, ni_size);
		cachefiles_end_secure(cache, saved_cred);

		if (ret != 0) {
			fscache_set_store_limit(&object->fscache, 0);
			if (ret == -EIO)
				cachefiles_io_error_obj(object,
							"Invalidate failed");
		}
	}

	fscache_op_complete(op, true);
	_leave("");
}

/*
 * dissociate a cache from all the pages it was backing
 */
static void cachefiles_dissociate_pages(struct fscache_cache *cache)
{
	_enter("");
}

const struct fscache_cache_ops cachefiles_cache_ops = {
	.name			= "cachefiles",
	.alloc_object		= cachefiles_alloc_object,
	.lookup_object		= cachefiles_lookup_object,
	.lookup_complete	= cachefiles_lookup_complete,
	.grab_object		= cachefiles_grab_object,
	.update_object		= cachefiles_update_object,
	.invalidate_object	= cachefiles_invalidate_object,
	.drop_object		= cachefiles_drop_object,
	.put_object		= cachefiles_put_object,
	.sync_cache		= cachefiles_sync_cache,
	.attr_changed		= cachefiles_attr_changed,
	.read_or_alloc_page	= cachefiles_read_or_alloc_page,
	.read_or_alloc_pages	= cachefiles_read_or_alloc_pages,
	.allocate_page		= cachefiles_allocate_page,
	.allocate_pages		= cachefiles_allocate_pages,
	.write_page		= cachefiles_write_page,
	.uncache_page		= cachefiles_uncache_page,
	.dissociate_pages	= cachefiles_dissociate_pages,
	.check_consistency	= cachefiles_check_consistency,
};
