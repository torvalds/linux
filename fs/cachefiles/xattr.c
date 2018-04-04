/* CacheFiles extended attribute management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/quotaops.h>
#include <linux/xattr.h>
#include <linux/slab.h>
#include "internal.h"

static const char cachefiles_xattr_cache[] =
	XATTR_USER_PREFIX "CacheFiles.cache";

/*
 * check the type label on an object
 * - done using xattrs
 */
int cachefiles_check_object_type(struct cachefiles_object *object)
{
	struct dentry *dentry = object->dentry;
	char type[3], xtype[3];
	int ret;

	ASSERT(dentry);
	ASSERT(d_backing_inode(dentry));

	if (!object->fscache.cookie)
		strcpy(type, "C3");
	else
		snprintf(type, 3, "%02x", object->fscache.cookie->def->type);

	_enter("%p{%s}", object, type);

	/* attempt to install a type label directly */
	ret = vfs_setxattr(dentry, cachefiles_xattr_cache, type, 2,
			   XATTR_CREATE);
	if (ret == 0) {
		_debug("SET"); /* we succeeded */
		goto error;
	}

	if (ret != -EEXIST) {
		pr_err("Can't set xattr on %pd [%lu] (err %d)\n",
		       dentry, d_backing_inode(dentry)->i_ino,
		       -ret);
		goto error;
	}

	/* read the current type label */
	ret = vfs_getxattr(dentry, cachefiles_xattr_cache, xtype, 3);
	if (ret < 0) {
		if (ret == -ERANGE)
			goto bad_type_length;

		pr_err("Can't read xattr on %pd [%lu] (err %d)\n",
		       dentry, d_backing_inode(dentry)->i_ino,
		       -ret);
		goto error;
	}

	/* check the type is what we're expecting */
	if (ret != 2)
		goto bad_type_length;

	if (xtype[0] != type[0] || xtype[1] != type[1])
		goto bad_type;

	ret = 0;

error:
	_leave(" = %d", ret);
	return ret;

bad_type_length:
	pr_err("Cache object %lu type xattr length incorrect\n",
	       d_backing_inode(dentry)->i_ino);
	ret = -EIO;
	goto error;

bad_type:
	xtype[2] = 0;
	pr_err("Cache object %pd [%lu] type %s not %s\n",
	       dentry, d_backing_inode(dentry)->i_ino,
	       xtype, type);
	ret = -EIO;
	goto error;
}

/*
 * set the state xattr on a cache file
 */
int cachefiles_set_object_xattr(struct cachefiles_object *object,
				struct cachefiles_xattr *auxdata)
{
	struct dentry *dentry = object->dentry;
	int ret;

	ASSERT(dentry);

	_enter("%p,#%d", object, auxdata->len);

	/* attempt to install the cache metadata directly */
	_debug("SET #%u", auxdata->len);

	clear_bit(FSCACHE_COOKIE_AUX_UPDATED, &object->fscache.cookie->flags);
	ret = vfs_setxattr(dentry, cachefiles_xattr_cache,
			   &auxdata->type, auxdata->len,
			   XATTR_CREATE);
	if (ret < 0 && ret != -ENOMEM)
		cachefiles_io_error_obj(
			object,
			"Failed to set xattr with error %d", ret);

	_leave(" = %d", ret);
	return ret;
}

/*
 * update the state xattr on a cache file
 */
int cachefiles_update_object_xattr(struct cachefiles_object *object,
				   struct cachefiles_xattr *auxdata)
{
	struct dentry *dentry = object->dentry;
	int ret;

	ASSERT(dentry);

	_enter("%p,#%d", object, auxdata->len);

	/* attempt to install the cache metadata directly */
	_debug("SET #%u", auxdata->len);

	clear_bit(FSCACHE_COOKIE_AUX_UPDATED, &object->fscache.cookie->flags);
	ret = vfs_setxattr(dentry, cachefiles_xattr_cache,
			   &auxdata->type, auxdata->len,
			   XATTR_REPLACE);
	if (ret < 0 && ret != -ENOMEM)
		cachefiles_io_error_obj(
			object,
			"Failed to update xattr with error %d", ret);

	_leave(" = %d", ret);
	return ret;
}

/*
 * check the consistency between the backing cache and the FS-Cache cookie
 */
int cachefiles_check_auxdata(struct cachefiles_object *object)
{
	struct cachefiles_xattr *auxbuf;
	enum fscache_checkaux validity;
	struct dentry *dentry = object->dentry;
	ssize_t xlen;
	int ret;

	ASSERT(dentry);
	ASSERT(d_backing_inode(dentry));
	ASSERT(object->fscache.cookie->def->check_aux);

	auxbuf = kmalloc(sizeof(struct cachefiles_xattr) + 512, GFP_KERNEL);
	if (!auxbuf)
		return -ENOMEM;

	xlen = vfs_getxattr(dentry, cachefiles_xattr_cache,
			    &auxbuf->type, 512 + 1);
	ret = -ESTALE;
	if (xlen < 1 ||
	    auxbuf->type != object->fscache.cookie->def->type)
		goto error;

	xlen--;
	validity = fscache_check_aux(&object->fscache, &auxbuf->data, xlen);
	if (validity != FSCACHE_CHECKAUX_OKAY)
		goto error;

	ret = 0;
error:
	kfree(auxbuf);
	return ret;
}

/*
 * check the state xattr on a cache file
 * - return -ESTALE if the object should be deleted
 */
int cachefiles_check_object_xattr(struct cachefiles_object *object,
				  struct cachefiles_xattr *auxdata)
{
	struct cachefiles_xattr *auxbuf;
	struct dentry *dentry = object->dentry;
	int ret;

	_enter("%p,#%d", object, auxdata->len);

	ASSERT(dentry);
	ASSERT(d_backing_inode(dentry));

	auxbuf = kmalloc(sizeof(struct cachefiles_xattr) + 512, cachefiles_gfp);
	if (!auxbuf) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	/* read the current type label */
	ret = vfs_getxattr(dentry, cachefiles_xattr_cache,
			   &auxbuf->type, 512 + 1);
	if (ret < 0) {
		if (ret == -ENODATA)
			goto stale; /* no attribute - power went off
				     * mid-cull? */

		if (ret == -ERANGE)
			goto bad_type_length;

		cachefiles_io_error_obj(object,
					"Can't read xattr on %lu (err %d)",
					d_backing_inode(dentry)->i_ino, -ret);
		goto error;
	}

	/* check the on-disk object */
	if (ret < 1)
		goto bad_type_length;

	if (auxbuf->type != auxdata->type)
		goto stale;

	auxbuf->len = ret;

	/* consult the netfs */
	if (object->fscache.cookie->def->check_aux) {
		enum fscache_checkaux result;
		unsigned int dlen;

		dlen = auxbuf->len - 1;

		_debug("checkaux %s #%u",
		       object->fscache.cookie->def->name, dlen);

		result = fscache_check_aux(&object->fscache,
					   &auxbuf->data, dlen);

		switch (result) {
			/* entry okay as is */
		case FSCACHE_CHECKAUX_OKAY:
			goto okay;

			/* entry requires update */
		case FSCACHE_CHECKAUX_NEEDS_UPDATE:
			break;

			/* entry requires deletion */
		case FSCACHE_CHECKAUX_OBSOLETE:
			goto stale;

		default:
			BUG();
		}

		/* update the current label */
		ret = vfs_setxattr(dentry, cachefiles_xattr_cache,
				   &auxdata->type, auxdata->len,
				   XATTR_REPLACE);
		if (ret < 0) {
			cachefiles_io_error_obj(object,
						"Can't update xattr on %lu"
						" (error %d)",
						d_backing_inode(dentry)->i_ino, -ret);
			goto error;
		}
	}

okay:
	ret = 0;

error:
	kfree(auxbuf);
	_leave(" = %d", ret);
	return ret;

bad_type_length:
	pr_err("Cache object %lu xattr length incorrect\n",
	       d_backing_inode(dentry)->i_ino);
	ret = -EIO;
	goto error;

stale:
	ret = -ESTALE;
	goto error;
}

/*
 * remove the object's xattr to mark it stale
 */
int cachefiles_remove_object_xattr(struct cachefiles_cache *cache,
				   struct dentry *dentry)
{
	int ret;

	ret = vfs_removexattr(dentry, cachefiles_xattr_cache);
	if (ret < 0) {
		if (ret == -ENOENT || ret == -ENODATA)
			ret = 0;
		else if (ret != -ENOMEM)
			cachefiles_io_error(cache,
					    "Can't remove xattr from %lu"
					    " (error %d)",
					    d_backing_inode(dentry)->i_ino, -ret);
	}

	_leave(" = %d", ret);
	return ret;
}
