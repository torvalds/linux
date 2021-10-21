// SPDX-License-Identifier: GPL-2.0-or-later
/* kiocb-using read/write
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/mount.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/falloc.h>
#include <linux/sched/mm.h>
#include <trace/events/fscache.h>
#include "internal.h"

/*
 * Clean up an operation.
 */
static void cachefiles_end_operation(struct netfs_cache_resources *cres)
{
	struct file *file = cachefiles_cres_file(cres);

	if (file)
		fput(file);
	fscache_end_cookie_access(fscache_cres_cookie(cres), fscache_access_io_end);
}

static const struct netfs_cache_ops cachefiles_netfs_cache_ops = {
	.end_operation		= cachefiles_end_operation,
};

/*
 * Open the cache file when beginning a cache operation.
 */
bool cachefiles_begin_operation(struct netfs_cache_resources *cres,
				enum fscache_want_state want_state)
{
	struct cachefiles_object *object = cachefiles_cres_object(cres);

	if (!cachefiles_cres_file(cres)) {
		cres->ops = &cachefiles_netfs_cache_ops;
		if (object->file) {
			spin_lock(&object->lock);
			if (!cres->cache_priv2 && object->file)
				cres->cache_priv2 = get_file(object->file);
			spin_unlock(&object->lock);
		}
	}

	if (!cachefiles_cres_file(cres) && want_state != FSCACHE_WANT_PARAMS) {
		pr_err("failed to get cres->file\n");
		return false;
	}

	return true;
}
