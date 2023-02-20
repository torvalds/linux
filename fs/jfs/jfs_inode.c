// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_inode.h"
#include "jfs_filsys.h"
#include "jfs_imap.h"
#include "jfs_dinode.h"
#include "jfs_debug.h"


void jfs_set_inode_flags(struct inode *inode)
{
	unsigned int flags = JFS_IP(inode)->mode2;
	unsigned int new_fl = 0;

	if (flags & JFS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;
	if (flags & JFS_APPEND_FL)
		new_fl |= S_APPEND;
	if (flags & JFS_NOATIME_FL)
		new_fl |= S_NOATIME;
	if (flags & JFS_DIRSYNC_FL)
		new_fl |= S_DIRSYNC;
	if (flags & JFS_SYNC_FL)
		new_fl |= S_SYNC;
	inode_set_flags(inode, new_fl, S_IMMUTABLE | S_APPEND | S_NOATIME |
			S_DIRSYNC | S_SYNC);
}

/*
 * NAME:	ialloc()
 *
 * FUNCTION:	Allocate a new inode
 *
 */
struct inode *ialloc(struct inode *parent, umode_t mode)
{
	struct super_block *sb = parent->i_sb;
	struct inode *inode;
	struct jfs_inode_info *jfs_inode;
	int rc;

	inode = new_inode(sb);
	if (!inode) {
		jfs_warn("ialloc: new_inode returned NULL!");
		return ERR_PTR(-ENOMEM);
	}

	jfs_inode = JFS_IP(inode);

	rc = diAlloc(parent, S_ISDIR(mode), inode);
	if (rc) {
		jfs_warn("ialloc: diAlloc returned %d!", rc);
		goto fail_put;
	}

	if (insert_inode_locked(inode) < 0) {
		rc = -EINVAL;
		goto fail_put;
	}

	inode_init_owner(&nop_mnt_idmap, inode, parent, mode);
	/*
	 * New inodes need to save sane values on disk when
	 * uid & gid mount options are used
	 */
	jfs_inode->saved_uid = inode->i_uid;
	jfs_inode->saved_gid = inode->i_gid;

	/*
	 * Allocate inode to quota.
	 */
	rc = dquot_initialize(inode);
	if (rc)
		goto fail_drop;
	rc = dquot_alloc_inode(inode);
	if (rc)
		goto fail_drop;

	/* inherit flags from parent */
	jfs_inode->mode2 = JFS_IP(parent)->mode2 & JFS_FL_INHERIT;

	if (S_ISDIR(mode)) {
		jfs_inode->mode2 |= IDIRECTORY;
		jfs_inode->mode2 &= ~JFS_DIRSYNC_FL;
	}
	else {
		jfs_inode->mode2 |= INLINEEA | ISPARSE;
		if (S_ISLNK(mode))
			jfs_inode->mode2 &= ~(JFS_IMMUTABLE_FL|JFS_APPEND_FL);
	}
	jfs_inode->mode2 |= inode->i_mode;

	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = current_time(inode);
	jfs_inode->otime = inode->i_ctime.tv_sec;
	inode->i_generation = JFS_SBI(sb)->gengen++;

	jfs_inode->cflag = 0;

	/* Zero remaining fields */
	memset(&jfs_inode->acl, 0, sizeof(dxd_t));
	memset(&jfs_inode->ea, 0, sizeof(dxd_t));
	jfs_inode->next_index = 0;
	jfs_inode->acltype = 0;
	jfs_inode->btorder = 0;
	jfs_inode->btindex = 0;
	jfs_inode->bxflag = 0;
	jfs_inode->blid = 0;
	jfs_inode->atlhead = 0;
	jfs_inode->atltail = 0;
	jfs_inode->xtlid = 0;
	jfs_set_inode_flags(inode);

	jfs_info("ialloc returns inode = 0x%p", inode);

	return inode;

fail_drop:
	dquot_drop(inode);
	inode->i_flags |= S_NOQUOTA;
	clear_nlink(inode);
	discard_new_inode(inode);
	return ERR_PTR(rc);

fail_put:
	iput(inode);
	return ERR_PTR(rc);
}
