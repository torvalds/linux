/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/lm_interface.h>

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
 * @nd:
 *
 * Check to make sure the lookup necessary to arrive at this inode from its
 * parent is still good.
 *
 * Returns: 1 if the dentry is ok, 0 if it isn't
 */

static int gfs2_drevalidate(struct dentry *dentry, struct nameidata *nd)
{
	struct dentry *parent = dget_parent(dentry);
	struct gfs2_sbd *sdp = GFS2_SB(parent->d_inode);
	struct gfs2_inode *dip = GFS2_I(parent->d_inode);
	struct inode *inode = dentry->d_inode;
	struct gfs2_holder d_gh;
	struct gfs2_inode *ip = NULL;
	int error;
	int had_lock = 0;

	if (inode) {
		if (is_bad_inode(inode))
			goto invalid;
		ip = GFS2_I(inode);
	}

	if (sdp->sd_args.ar_localcaching)
		goto valid;

	had_lock = (gfs2_glock_is_locked_by_me(dip->i_gl) != NULL);
	if (!had_lock) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
		if (error)
			goto fail;
	} 

	error = gfs2_dir_check(parent->d_inode, &dentry->d_name, ip);
	switch (error) {
	case 0:
		if (!inode)
			goto invalid_gunlock;
		break;
	case -ENOENT:
		if (!inode)
			goto valid_gunlock;
		goto invalid_gunlock;
	default:
		goto fail_gunlock;
	}

valid_gunlock:
	if (!had_lock)
		gfs2_glock_dq_uninit(&d_gh);
valid:
	dput(parent);
	return 1;

invalid_gunlock:
	if (!had_lock)
		gfs2_glock_dq_uninit(&d_gh);
invalid:
	if (inode && S_ISDIR(inode->i_mode)) {
		if (have_submounts(dentry))
			goto valid;
		shrink_dcache_parent(dentry);
	}
	d_drop(dentry);
	dput(parent);
	return 0;

fail_gunlock:
	gfs2_glock_dq_uninit(&d_gh);
fail:
	dput(parent);
	return 0;
}

static int gfs2_dhash(struct dentry *dentry, struct qstr *str)
{
	str->hash = gfs2_disk_hash(str->name, str->len);
	return 0;
}

struct dentry_operations gfs2_dops = {
	.d_revalidate = gfs2_drevalidate,
	.d_hash = gfs2_dhash,
};

