/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/smp_lock.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "dir.h"
#include "glock.h"
#include "ops_dentry.h"
#include "util.h"

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
	struct gfs2_inode *ip;
	struct gfs2_inum_host inum;
	unsigned int type;
	int error;

	if (inode && is_bad_inode(inode))
		goto invalid;

	if (sdp->sd_args.ar_localcaching)
		goto valid;

	error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
	if (error)
		goto fail;

	error = gfs2_dir_search(parent->d_inode, &dentry->d_name, &inum, &type);
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

	ip = GFS2_I(inode);

	if (!gfs2_inum_equal(&ip->i_num, &inum))
		goto invalid_gunlock;

	if (IF2DT(ip->i_di.di_mode) != type) {
		gfs2_consist_inode(dip);
		goto fail_gunlock;
	}

valid_gunlock:
	gfs2_glock_dq_uninit(&d_gh);
valid:
	dput(parent);
	return 1;

invalid_gunlock:
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

