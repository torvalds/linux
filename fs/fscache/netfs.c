/* FS-Cache netfs (client) registration
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#define FSCACHE_DEBUG_LEVEL COOKIE
#include <linux/module.h>
#include <linux/slab.h>
#include "internal.h"

/*
 * register a network filesystem for caching
 */
int __fscache_register_netfs(struct fscache_netfs *netfs)
{
	struct fscache_cookie *candidate, *cookie;

	_enter("{%s}", netfs->name);

	/* allocate a cookie for the primary index */
	candidate = fscache_alloc_cookie(&fscache_fsdef_index,
					 &fscache_fsdef_netfs_def,
					 netfs->name, strlen(netfs->name),
					 &netfs->version, sizeof(netfs->version),
					 netfs, 0);
	if (!candidate) {
		_leave(" = -ENOMEM");
		return -ENOMEM;
	}

	candidate->flags = 1 << FSCACHE_COOKIE_ENABLED;

	/* check the netfs type is not already present */
	cookie = fscache_hash_cookie(candidate);
	if (!cookie)
		goto already_registered;
	if (cookie != candidate) {
		trace_fscache_cookie(candidate, fscache_cookie_discard, 1);
		fscache_free_cookie(candidate);
	}

	fscache_cookie_get(cookie->parent, fscache_cookie_get_register_netfs);
	atomic_inc(&cookie->parent->n_children);

	netfs->primary_index = cookie;

	pr_notice("Netfs '%s' registered for caching\n", netfs->name);
	trace_fscache_netfs(netfs);
	_leave(" = 0");
	return 0;

already_registered:
	fscache_cookie_put(candidate, fscache_cookie_put_dup_netfs);
	_leave(" = -EEXIST");
	return -EEXIST;
}
EXPORT_SYMBOL(__fscache_register_netfs);

/*
 * unregister a network filesystem from the cache
 * - all cookies must have been released first
 */
void __fscache_unregister_netfs(struct fscache_netfs *netfs)
{
	_enter("{%s.%u}", netfs->name, netfs->version);

	fscache_relinquish_cookie(netfs->primary_index, NULL, false);
	pr_notice("Netfs '%s' unregistered from caching\n", netfs->name);

	_leave("");
}
EXPORT_SYMBOL(__fscache_unregister_netfs);
