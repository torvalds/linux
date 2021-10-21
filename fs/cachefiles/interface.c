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

/*
 * Adjust the size of a cache file if necessary to match the DIO size.  We keep
 * the EOF marker a multiple of DIO blocks so that we don't fall back to doing
 * non-DIO for a partial block straddling the EOF, but we also have to be
 * careful of someone expanding the file and accidentally accreting the
 * padding.
 */
static int cachefiles_adjust_size(struct cachefiles_object *object)
{
	struct iattr newattrs;
	struct file *file = object->file;
	uint64_t ni_size;
	loff_t oi_size;
	int ret;

	ni_size = object->cookie->object_size;
	ni_size = round_up(ni_size, CACHEFILES_DIO_BLOCK_SIZE);

	_enter("{OBJ%x},[%llu]",
	       object->debug_id, (unsigned long long) ni_size);

	if (!file)
		return -ENOBUFS;

	oi_size = i_size_read(file_inode(file));
	if (oi_size == ni_size)
		return 0;

	inode_lock(file_inode(file));

	/* if there's an extension to a partial page at the end of the backing
	 * file, we need to discard the partial page so that we pick up new
	 * data after it */
	if (oi_size & ~PAGE_MASK && ni_size > oi_size) {
		_debug("discard tail %llx", oi_size);
		newattrs.ia_valid = ATTR_SIZE;
		newattrs.ia_size = oi_size & PAGE_MASK;
		ret = cachefiles_inject_remove_error();
		if (ret == 0)
			ret = notify_change(&init_user_ns, file->f_path.dentry,
					    &newattrs, NULL);
		if (ret < 0)
			goto truncate_failed;
	}

	newattrs.ia_valid = ATTR_SIZE;
	newattrs.ia_size = ni_size;
	ret = cachefiles_inject_write_error();
	if (ret == 0)
		ret = notify_change(&init_user_ns, file->f_path.dentry,
				    &newattrs, NULL);

truncate_failed:
	inode_unlock(file_inode(file));

	if (ret < 0)
		trace_cachefiles_io_error(NULL, file_inode(file), ret,
					  cachefiles_trace_notify_change_error);
	if (ret == -EIO) {
		cachefiles_io_error_obj(object, "Size set failed");
		ret = -ENOBUFS;
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * Attempt to look up the nominated node in this cache
 */
static bool cachefiles_lookup_cookie(struct fscache_cookie *cookie)
{
	struct cachefiles_object *object;
	struct cachefiles_cache *cache = cookie->volume->cache->cache_priv;
	const struct cred *saved_cred;
	bool success;

	object = cachefiles_alloc_object(cookie);
	if (!object)
		goto fail;

	_enter("{OBJ%x}", object->debug_id);

	if (!cachefiles_cook_key(object))
		goto fail_put;

	cookie->cache_priv = object;

	cachefiles_begin_secure(cache, &saved_cred);

	success = cachefiles_look_up_object(object);
	if (!success)
		goto fail_withdraw;

	cachefiles_see_object(object, cachefiles_obj_see_lookup_cookie);

	spin_lock(&cache->object_list_lock);
	list_add(&object->cache_link, &cache->object_list);
	spin_unlock(&cache->object_list_lock);
	cachefiles_adjust_size(object);

	cachefiles_end_secure(cache, saved_cred);
	_leave(" = t");
	return true;

fail_withdraw:
	cachefiles_end_secure(cache, saved_cred);
	cachefiles_see_object(object, cachefiles_obj_see_lookup_failed);
	fscache_caching_failed(cookie);
	_debug("failed c=%08x o=%08x", cookie->debug_id, object->debug_id);
	/* The caller holds an access count on the cookie, so we need them to
	 * drop it before we can withdraw the object.
	 */
	return false;

fail_put:
	cachefiles_put_object(object, cachefiles_obj_put_alloc_fail);
fail:
	return false;
}

/*
 * Commit changes to the object as we drop it.
 */
static void cachefiles_commit_object(struct cachefiles_object *object,
				     struct cachefiles_cache *cache)
{
	bool update = false;

	if (test_and_clear_bit(FSCACHE_COOKIE_LOCAL_WRITE, &object->cookie->flags))
		update = true;
	if (test_and_clear_bit(FSCACHE_COOKIE_NEEDS_UPDATE, &object->cookie->flags))
		update = true;
	if (update)
		cachefiles_set_object_xattr(object);

	if (test_bit(CACHEFILES_OBJECT_USING_TMPFILE, &object->flags))
		cachefiles_commit_tmpfile(cache, object);
}

/*
 * Finalise and object and close the VFS structs that we have.
 */
static void cachefiles_clean_up_object(struct cachefiles_object *object,
				       struct cachefiles_cache *cache)
{
	if (test_bit(FSCACHE_COOKIE_RETIRED, &object->cookie->flags)) {
		if (!test_bit(CACHEFILES_OBJECT_USING_TMPFILE, &object->flags)) {
			cachefiles_see_object(object, cachefiles_obj_see_clean_delete);
			_debug("- inval object OBJ%x", object->debug_id);
			cachefiles_delete_object(object, FSCACHE_OBJECT_WAS_RETIRED);
		} else {
			cachefiles_see_object(object, cachefiles_obj_see_clean_drop_tmp);
			_debug("- inval object OBJ%x tmpfile", object->debug_id);
		}
	} else {
		cachefiles_see_object(object, cachefiles_obj_see_clean_commit);
		cachefiles_commit_object(object, cache);
	}

	cachefiles_unmark_inode_in_use(object, object->file);
	if (object->file) {
		fput(object->file);
		object->file = NULL;
	}
}

/*
 * Withdraw caching for a cookie.
 */
static void cachefiles_withdraw_cookie(struct fscache_cookie *cookie)
{
	struct cachefiles_object *object = cookie->cache_priv;
	struct cachefiles_cache *cache = object->volume->cache;
	const struct cred *saved_cred;

	_enter("o=%x", object->debug_id);
	cachefiles_see_object(object, cachefiles_obj_see_withdraw_cookie);

	if (!list_empty(&object->cache_link)) {
		spin_lock(&cache->object_list_lock);
		cachefiles_see_object(object, cachefiles_obj_see_withdrawal);
		list_del_init(&object->cache_link);
		spin_unlock(&cache->object_list_lock);
	}

	if (object->file) {
		cachefiles_begin_secure(cache, &saved_cred);
		cachefiles_clean_up_object(object, cache);
		cachefiles_end_secure(cache, saved_cred);
	}

	cookie->cache_priv = NULL;
	cachefiles_put_object(object, cachefiles_obj_put_detach);
}

/*
 * Invalidate the storage associated with a cookie.
 */
static bool cachefiles_invalidate_cookie(struct fscache_cookie *cookie)
{
	struct cachefiles_object *object = cookie->cache_priv;
	struct file *new_file, *old_file;
	bool old_tmpfile;

	_enter("o=%x,[%llu]", object->debug_id, object->cookie->object_size);

	old_tmpfile = test_bit(CACHEFILES_OBJECT_USING_TMPFILE, &object->flags);

	if (!object->file) {
		fscache_resume_after_invalidation(cookie);
		_leave(" = t [light]");
		return true;
	}

	new_file = cachefiles_create_tmpfile(object);
	if (IS_ERR(new_file))
		goto failed;

	/* Substitute the VFS target */
	_debug("sub");
	spin_lock(&object->lock);

	old_file = object->file;
	object->file = new_file;
	object->content_info = CACHEFILES_CONTENT_NO_DATA;
	set_bit(CACHEFILES_OBJECT_USING_TMPFILE, &object->flags);
	set_bit(FSCACHE_COOKIE_NEEDS_UPDATE, &object->cookie->flags);

	spin_unlock(&object->lock);
	_debug("subbed");

	/* Allow I/O to take place again */
	fscache_resume_after_invalidation(cookie);

	if (old_file) {
		if (!old_tmpfile) {
			struct cachefiles_volume *volume = object->volume;
			struct dentry *fan = volume->fanout[(u8)cookie->key_hash];

			inode_lock_nested(d_inode(fan), I_MUTEX_PARENT);
			cachefiles_bury_object(volume->cache, object, fan,
					       old_file->f_path.dentry,
					       FSCACHE_OBJECT_INVALIDATED);
		}
		fput(old_file);
	}

	_leave(" = t");
	return true;

failed:
	_leave(" = f");
	return false;
}

const struct fscache_cache_ops cachefiles_cache_ops = {
	.name			= "cachefiles",
	.acquire_volume		= cachefiles_acquire_volume,
	.free_volume		= cachefiles_free_volume,
	.lookup_cookie		= cachefiles_lookup_cookie,
	.withdraw_cookie	= cachefiles_withdraw_cookie,
	.invalidate_cookie	= cachefiles_invalidate_cookie,
	.prepare_to_write	= cachefiles_prepare_to_write,
};
