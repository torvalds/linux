/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dcache.c
 *
 * dentry cache handling code
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/namei.h>

#define MLOG_MASK_PREFIX ML_DCACHE
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dcache.h"
#include "file.h"
#include "inode.h"

static int ocfs2_dentry_revalidate(struct dentry *dentry,
				   struct nameidata *nd)
{
	struct inode *inode = dentry->d_inode;
	int ret = 0;    /* if all else fails, just return false */
	struct ocfs2_super *osb;

	mlog_entry("(0x%p, '%.*s')\n", dentry,
		   dentry->d_name.len, dentry->d_name.name);

	/* Never trust a negative dentry - force a new lookup. */
	if (inode == NULL) {
		mlog(0, "negative dentry: %.*s\n", dentry->d_name.len,
		     dentry->d_name.name);
		goto bail;
	}

	osb = OCFS2_SB(inode->i_sb);

	BUG_ON(!osb);

	if (inode != osb->root_inode) {
		spin_lock(&OCFS2_I(inode)->ip_lock);
		/* did we or someone else delete this inode? */
		if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_DELETED) {
			spin_unlock(&OCFS2_I(inode)->ip_lock);
			mlog(0, "inode (%llu) deleted, returning false\n",
			     (unsigned long long)OCFS2_I(inode)->ip_blkno);
			goto bail;
		}
		spin_unlock(&OCFS2_I(inode)->ip_lock);

		if (!inode->i_nlink) {
			mlog(0, "Inode %llu orphaned, returning false "
			     "dir = %d\n",
			     (unsigned long long)OCFS2_I(inode)->ip_blkno,
			     S_ISDIR(inode->i_mode));
			goto bail;
		}
	}

	ret = 1;

bail:
	mlog_exit(ret);

	return ret;
}

struct dentry_operations ocfs2_dentry_ops = {
	.d_revalidate		= ocfs2_dentry_revalidate,
};
