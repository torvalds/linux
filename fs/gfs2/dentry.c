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
 * @dentry: the mapping to check
 * @flags: lookup flags
 *
 * Check to make sure the lookup necessary to arrive at this inode from its
 * parent is still good.
 *
 * Returns: 1 if the dentry is ok, 0 if it isn't
 */

static int gfs2_drevalidate(struct dentry *dentry, unsigned int flags)
{
	struct dentry *parent = NULL;
	struct gfs2_sbd *sdp;
	struct gfs2_inode *dip;
	struct inode *dinode, *inode;
	struct gfs2_holder d_gh;
	struct gfs2_inode *ip = NULL;
	int error, valid = 0;
	int had_lock = 0;

	if (flags & LOOKUP_RCU) {
		dinode = d_inode_rcu(READ_ONCE(dentry->d_parent));
		if (!dinode)
			return -ECHILD;
	} else {
		parent = dget_parent(dentry);
		dinode = d_inode(parent);
	}
	sdp = GFS2_SB(dinode);
	dip = GFS2_I(dinode);
	inode = d_inode(dentry);

	if (inode) {
		if (is_bad_inode(inode))
			goto out;
		ip = GFS2_I(inode);
	}

	if (sdp->sd_lockstruct.ls_ops->lm_mount == NULL) {
		valid = 1;
		goto out;
	}

	had_lock = (gfs2_glock_is_locked_by_me(dip->i_gl) != NULL);
	if (!had_lock) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED,
					   flags & LOOKUP_RCU ? GL_NOBLOCK : 0, &d_gh);
		if (error)
			goto out;
	}

	error = gfs2_dir_check(d_inode(parent), &dentry->d_name, ip);
	valid = inode ? !error : (error == -ENOENT);

	if (!had_lock)
		gfs2_glock_dq_uninit(&d_gh);
out:
	dput(parent);
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

