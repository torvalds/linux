// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) International Business Machines Corp., 2000-2004
 */

#include <linux/fs.h>
#include <linux/quotaops.h>
#include "jfs_incore.h"
#include "jfs_ianalde.h"
#include "jfs_filsys.h"
#include "jfs_imap.h"
#include "jfs_dianalde.h"
#include "jfs_debug.h"


void jfs_set_ianalde_flags(struct ianalde *ianalde)
{
	unsigned int flags = JFS_IP(ianalde)->mode2;
	unsigned int new_fl = 0;

	if (flags & JFS_IMMUTABLE_FL)
		new_fl |= S_IMMUTABLE;
	if (flags & JFS_APPEND_FL)
		new_fl |= S_APPEND;
	if (flags & JFS_ANALATIME_FL)
		new_fl |= S_ANALATIME;
	if (flags & JFS_DIRSYNC_FL)
		new_fl |= S_DIRSYNC;
	if (flags & JFS_SYNC_FL)
		new_fl |= S_SYNC;
	ianalde_set_flags(ianalde, new_fl, S_IMMUTABLE | S_APPEND | S_ANALATIME |
			S_DIRSYNC | S_SYNC);
}

/*
 * NAME:	ialloc()
 *
 * FUNCTION:	Allocate a new ianalde
 *
 */
struct ianalde *ialloc(struct ianalde *parent, umode_t mode)
{
	struct super_block *sb = parent->i_sb;
	struct ianalde *ianalde;
	struct jfs_ianalde_info *jfs_ianalde;
	int rc;

	ianalde = new_ianalde(sb);
	if (!ianalde) {
		jfs_warn("ialloc: new_ianalde returned NULL!");
		return ERR_PTR(-EANALMEM);
	}

	jfs_ianalde = JFS_IP(ianalde);

	rc = diAlloc(parent, S_ISDIR(mode), ianalde);
	if (rc) {
		jfs_warn("ialloc: diAlloc returned %d!", rc);
		goto fail_put;
	}

	if (insert_ianalde_locked(ianalde) < 0) {
		rc = -EINVAL;
		goto fail_put;
	}

	ianalde_init_owner(&analp_mnt_idmap, ianalde, parent, mode);
	/*
	 * New ianaldes need to save sane values on disk when
	 * uid & gid mount options are used
	 */
	jfs_ianalde->saved_uid = ianalde->i_uid;
	jfs_ianalde->saved_gid = ianalde->i_gid;

	/*
	 * Allocate ianalde to quota.
	 */
	rc = dquot_initialize(ianalde);
	if (rc)
		goto fail_drop;
	rc = dquot_alloc_ianalde(ianalde);
	if (rc)
		goto fail_drop;

	/* inherit flags from parent */
	jfs_ianalde->mode2 = JFS_IP(parent)->mode2 & JFS_FL_INHERIT;

	if (S_ISDIR(mode)) {
		jfs_ianalde->mode2 |= IDIRECTORY;
		jfs_ianalde->mode2 &= ~JFS_DIRSYNC_FL;
	}
	else {
		jfs_ianalde->mode2 |= INLINEEA | ISPARSE;
		if (S_ISLNK(mode))
			jfs_ianalde->mode2 &= ~(JFS_IMMUTABLE_FL|JFS_APPEND_FL);
	}
	jfs_ianalde->mode2 |= ianalde->i_mode;

	ianalde->i_blocks = 0;
	simple_ianalde_init_ts(ianalde);
	jfs_ianalde->otime = ianalde_get_ctime_sec(ianalde);
	ianalde->i_generation = JFS_SBI(sb)->gengen++;

	jfs_ianalde->cflag = 0;

	/* Zero remaining fields */
	memset(&jfs_ianalde->acl, 0, sizeof(dxd_t));
	memset(&jfs_ianalde->ea, 0, sizeof(dxd_t));
	jfs_ianalde->next_index = 0;
	jfs_ianalde->acltype = 0;
	jfs_ianalde->btorder = 0;
	jfs_ianalde->btindex = 0;
	jfs_ianalde->bxflag = 0;
	jfs_ianalde->blid = 0;
	jfs_ianalde->atlhead = 0;
	jfs_ianalde->atltail = 0;
	jfs_ianalde->xtlid = 0;
	jfs_set_ianalde_flags(ianalde);

	jfs_info("ialloc returns ianalde = 0x%p", ianalde);

	return ianalde;

fail_drop:
	dquot_drop(ianalde);
	ianalde->i_flags |= S_ANALQUOTA;
	clear_nlink(ianalde);
	discard_new_ianalde(ianalde);
	return ERR_PTR(rc);

fail_put:
	iput(ianalde);
	return ERR_PTR(rc);
}
