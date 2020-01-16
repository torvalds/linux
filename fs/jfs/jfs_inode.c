// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_iyesde.h"
#include "jfs_filsys.h"
#include "jfs_imap.h"
#include "jfs_diyesde.h"
#include "jfs_debug.h"


void jfs_set_iyesde_flags(struct iyesde *iyesde)
{
	unsigned int flags = JFS_IP(iyesde)->mode2;
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
	iyesde_set_flags(iyesde, new_fl, S_IMMUTABLE | S_APPEND | S_NOATIME |
			S_DIRSYNC | S_SYNC);
}

/*
 * NAME:	ialloc()
 *
 * FUNCTION:	Allocate a new iyesde
 *
 */
struct iyesde *ialloc(struct iyesde *parent, umode_t mode)
{
	struct super_block *sb = parent->i_sb;
	struct iyesde *iyesde;
	struct jfs_iyesde_info *jfs_iyesde;
	int rc;

	iyesde = new_iyesde(sb);
	if (!iyesde) {
		jfs_warn("ialloc: new_iyesde returned NULL!");
		return ERR_PTR(-ENOMEM);
	}

	jfs_iyesde = JFS_IP(iyesde);

	rc = diAlloc(parent, S_ISDIR(mode), iyesde);
	if (rc) {
		jfs_warn("ialloc: diAlloc returned %d!", rc);
		goto fail_put;
	}

	if (insert_iyesde_locked(iyesde) < 0) {
		rc = -EINVAL;
		goto fail_put;
	}

	iyesde_init_owner(iyesde, parent, mode);
	/*
	 * New iyesdes need to save sane values on disk when
	 * uid & gid mount options are used
	 */
	jfs_iyesde->saved_uid = iyesde->i_uid;
	jfs_iyesde->saved_gid = iyesde->i_gid;

	/*
	 * Allocate iyesde to quota.
	 */
	rc = dquot_initialize(iyesde);
	if (rc)
		goto fail_drop;
	rc = dquot_alloc_iyesde(iyesde);
	if (rc)
		goto fail_drop;

	/* inherit flags from parent */
	jfs_iyesde->mode2 = JFS_IP(parent)->mode2 & JFS_FL_INHERIT;

	if (S_ISDIR(mode)) {
		jfs_iyesde->mode2 |= IDIRECTORY;
		jfs_iyesde->mode2 &= ~JFS_DIRSYNC_FL;
	}
	else {
		jfs_iyesde->mode2 |= INLINEEA | ISPARSE;
		if (S_ISLNK(mode))
			jfs_iyesde->mode2 &= ~(JFS_IMMUTABLE_FL|JFS_APPEND_FL);
	}
	jfs_iyesde->mode2 |= iyesde->i_mode;

	iyesde->i_blocks = 0;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	jfs_iyesde->otime = iyesde->i_ctime.tv_sec;
	iyesde->i_generation = JFS_SBI(sb)->gengen++;

	jfs_iyesde->cflag = 0;

	/* Zero remaining fields */
	memset(&jfs_iyesde->acl, 0, sizeof(dxd_t));
	memset(&jfs_iyesde->ea, 0, sizeof(dxd_t));
	jfs_iyesde->next_index = 0;
	jfs_iyesde->acltype = 0;
	jfs_iyesde->btorder = 0;
	jfs_iyesde->btindex = 0;
	jfs_iyesde->bxflag = 0;
	jfs_iyesde->blid = 0;
	jfs_iyesde->atlhead = 0;
	jfs_iyesde->atltail = 0;
	jfs_iyesde->xtlid = 0;
	jfs_set_iyesde_flags(iyesde);

	jfs_info("ialloc returns iyesde = 0x%p", iyesde);

	return iyesde;

fail_drop:
	dquot_drop(iyesde);
	iyesde->i_flags |= S_NOQUOTA;
	clear_nlink(iyesde);
	discard_new_iyesde(iyesde);
	return ERR_PTR(rc);

fail_put:
	iput(iyesde);
	return ERR_PTR(rc);
}
