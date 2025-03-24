// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 */

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/gfs2_ondisk.h>
#include <linux/namei.h>
#include <linux/crc32.h>

#include "gfs2.h"
#include "incore.h"
#include "dir.h"
#include "glock.h"
#include "super.h"
#include "util.h"
#include "inode.h"

/**
 * gfs2_drevalidate - Check directory lookup consistency
 * @dir: expected parent directory inode
 * @name: expexted name
 * @dentry: dentry to check
 * @flags: lookup flags
 *
 * Check to make sure the lookup necessary to arrive at this inode from its
 * parent is still good.
 *
 * Returns: 1 if the dentry is ok, 0 if it isn't
 */

static int gfs2_drevalidate(struct inode *dir, const struct qstr *name,
			    struct dentry *dentry, unsigned int flags)
{
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct gfs2_inode *dip = GFS2_I(dir);
	struct inode *inode;
	struct gfs2_holder d_gh;
	struct gfs2_inode *ip = NULL;
	int error, valid;
	int had_lock = 0;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	inode = d_inode(dentry);

	if (inode) {
		if (is_bad_inode(inode))
			return 0;
		ip = GFS2_I(inode);
	}

	if (sdp->sd_lockstruct.ls_ops->lm_mount == NULL)
		return 1;

	had_lock = (gfs2_glock_is_locked_by_me(dip->i_gl) != NULL);
	if (!had_lock) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
		if (error)
			return 0;
	}

	error = gfs2_dir_check(dir, name, ip);
	valid = inode ? !error : (error == -ENOENT);

	if (!had_lock)
		gfs2_glock_dq_uninit(&d_gh);
	return valid;
}

static int gfs2_dhash(const struct dentry *dentry, struct qstr *str)
{
	str->hash = gfs2_disk_hash(str->name, str->len);
	return 0;
}

static int gfs2_dentry_delete(const struct dentry *dentry)
{
	struct gfs2_inode *ginode;

	if (d_really_is_negative(dentry))
		return 0;

	ginode = GFS2_I(d_inode(dentry));
	if (!gfs2_holder_initialized(&ginode->i_iopen_gh))
		return 0;

	if (test_bit(GLF_DEMOTE, &ginode->i_iopen_gh.gh_gl->gl_flags))
		return 1;

	return 0;
}

const struct dentry_operations gfs2_dops = {
	.d_revalidate = gfs2_drevalidate,
	.d_hash = gfs2_dhash,
	.d_delete = gfs2_dentry_delete,
};

