// SPDX-License-Identifier: GPL-2.0-or-later
/* CacheFiles path walking and related routines
 *
 * Copyright (C) 2021 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs.h>
#include "internal.h"

/*
 * Mark the backing file as being a cache file if it's not already in use.  The
 * mark tells the culling request command that it's not allowed to cull the
 * file or directory.  The caller must hold the inode lock.
 */
static bool __cachefiles_mark_inode_in_use(struct cachefiles_object *object,
					   struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);
	bool can_use = false;

	if (!(inode->i_flags & S_KERNEL_FILE)) {
		inode->i_flags |= S_KERNEL_FILE;
		trace_cachefiles_mark_active(object, inode);
		can_use = true;
	} else {
		pr_notice("cachefiles: Inode already in use: %pd\n", dentry);
	}

	return can_use;
}

/*
 * Unmark a backing inode.  The caller must hold the inode lock.
 */
static void __cachefiles_unmark_inode_in_use(struct cachefiles_object *object,
					     struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);

	inode->i_flags &= ~S_KERNEL_FILE;
	trace_cachefiles_mark_inactive(object, inode);
}
