/* CacheFiles security management
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/fs.h>
#include <linux/cred.h>
#include "internal.h"

/*
 * determine the security context within which we access the cache from within
 * the kernel
 */
int cachefiles_get_security_ID(struct cachefiles_cache *cache)
{
	struct cred *new;
	int ret;

	_enter("{%s}", cache->secctx);

	new = prepare_kernel_cred(current);
	if (!new) {
		ret = -ENOMEM;
		goto error;
	}

	if (cache->secctx) {
		ret = set_security_override_from_ctx(new, cache->secctx);
		if (ret < 0) {
			put_cred(new);
			printk(KERN_ERR "CacheFiles:"
			       " Security denies permission to nominate"
			       " security context: error %d\n",
			       ret);
			goto error;
		}
	}

	cache->cache_cred = new;
	ret = 0;
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * see if mkdir and create can be performed in the root directory
 */
static int cachefiles_check_cache_dir(struct cachefiles_cache *cache,
				      struct dentry *root)
{
	int ret;

	ret = security_inode_mkdir(root->d_inode, root, 0);
	if (ret < 0) {
		printk(KERN_ERR "CacheFiles:"
		       " Security denies permission to make dirs: error %d",
		       ret);
		return ret;
	}

	ret = security_inode_create(root->d_inode, root, 0);
	if (ret < 0)
		printk(KERN_ERR "CacheFiles:"
		       " Security denies permission to create files: error %d",
		       ret);

	return ret;
}

/*
 * check the security details of the on-disk cache
 * - must be called with security override in force
 */
int cachefiles_determine_cache_security(struct cachefiles_cache *cache,
					struct dentry *root,
					const struct cred **_saved_cred)
{
	struct cred *new;
	int ret;

	_enter("");

	/* duplicate the cache creds for COW (the override is currently in
	 * force, so we can use prepare_creds() to do this) */
	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	cachefiles_end_secure(cache, *_saved_cred);

	/* use the cache root dir's security context as the basis with
	 * which create files */
	ret = set_create_files_as(new, root->d_inode);
	if (ret < 0) {
		_leave(" = %d [cfa]", ret);
		return ret;
	}

	put_cred(cache->cache_cred);
	cache->cache_cred = new;

	cachefiles_begin_secure(cache, _saved_cred);
	ret = cachefiles_check_cache_dir(cache, root);

	if (ret == -EOPNOTSUPP)
		ret = 0;
	_leave(" = %d", ret);
	return ret;
}
