/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2005 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <asm/semaphore.h>

#include "gfs2.h"
#include "bmap.h"
#include "dir.h"
#include "glock.h"
#include "inode.h"
#include "jdata.h"
#include "meta_io.h"
#include "quota.h"
#include "resize.h"
#include "rgrp.h"
#include "super.h"
#include "trans.h"

/* A single transaction needs to add the structs to rindex and make the
   statfs change. */

int gfs2_resize_add_rgrps(struct gfs2_sbd *sdp, char __user *buf,
			  unsigned int size)
{
	unsigned int num = size / sizeof(struct gfs2_rindex);
	struct gfs2_inode *ip = sdp->sd_rindex;
	struct gfs2_alloc *al = NULL;
	struct gfs2_holder i_gh;
	unsigned int data_blocks, ind_blocks;
	int alloc_required;
	unsigned int x;
	int error;

	gfs2_write_calc_reserv(ip, size, &data_blocks, &ind_blocks);

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE,
				   LM_FLAG_PRIORITY | GL_SYNC, &i_gh);
	if (error)
		return error;

	if (!gfs2_is_jdata(ip)) {
		gfs2_consist_inode(ip);
		error = -EIO;
		goto out;
	}

	error = gfs2_write_alloc_required(ip, ip->i_di.di_size, size,
					  &alloc_required);
	if (error)
		goto out;

	if (alloc_required) {
		al = gfs2_alloc_get(ip);

		al->al_requested = data_blocks + ind_blocks;

		error = gfs2_inplace_reserve(ip);
		if (error)
			goto out_alloc;

		error = gfs2_trans_begin(sdp,
					 al->al_rgd->rd_ri.ri_length +
					 data_blocks + ind_blocks +
					 RES_DINODE + RES_STATFS, 0);
		if (error)
			goto out_relse;
	} else {
		error = gfs2_trans_begin(sdp, data_blocks +
					 RES_DINODE + RES_STATFS, 0);
		if (error)
			goto out;
	}

	for (x = 0; x < num; x++) {
		struct gfs2_rindex ri;
		char ri_buf[sizeof(struct gfs2_rindex)];

		error = copy_from_user(&ri, buf, sizeof(struct gfs2_rindex));
		if (error) {
			error = -EFAULT;
			goto out_trans;
		}
		gfs2_rindex_out(&ri, ri_buf);

		error = gfs2_jdata_write_mem(ip, ri_buf, ip->i_di.di_size,
					     sizeof(struct gfs2_rindex));
		if (error < 0)
			goto out_trans;
		gfs2_assert_withdraw(sdp, error == sizeof(struct gfs2_rindex));
		error = 0;

		gfs2_statfs_change(sdp, ri.ri_data, ri.ri_data, 0);

		buf += sizeof(struct gfs2_rindex);
	}

 out_trans:
	gfs2_trans_end(sdp);

 out_relse:
	if (alloc_required)
		gfs2_inplace_release(ip);

 out_alloc:
	if (alloc_required)
		gfs2_alloc_put(ip);

 out:
	ip->i_gl->gl_vn++;
	gfs2_glock_dq_uninit(&i_gh);

	return error;
}

static void drop_dentries(struct gfs2_inode *ip)
{
	struct inode *inode;
	struct dentry *d;

	inode = gfs2_ip2v_lookup(ip);
	if (!inode)
		return;

 restart:
	spin_lock(&dcache_lock);
	list_for_each_entry(d, &inode->i_dentry, d_alias) {
		if (d_unhashed(d))
			continue;
		dget_locked(d);
		__d_drop(d);
		spin_unlock(&dcache_lock);
		dput(d);
		goto restart;
	}
	spin_unlock(&dcache_lock);

	iput(inode);
}

/* This is called by an ioctl to rename an ordinary file that's represented
   in the vfs to a hidden system file that isn't represented in the vfs.  It's
   used to add journals, along with the associated system files, to a fs. */

int gfs2_rename2system(struct gfs2_inode *ip,
		       struct gfs2_inode *old_dip, char *old_name,
		       struct gfs2_inode *new_dip, char *new_name)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_holder ghs[3];
	struct qstr old_qstr, new_qstr;
	struct gfs2_inum inum;
	int alloc_required;
	struct buffer_head *dibh;
	int error;

	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_NOCACHE, ghs);
	gfs2_holder_init(old_dip->i_gl, LM_ST_EXCLUSIVE, 0, ghs + 1);
	gfs2_holder_init(new_dip->i_gl, LM_ST_EXCLUSIVE, GL_SYNC, ghs + 2);

	error = gfs2_glock_nq_m(3, ghs);
	if (error)
		goto out;	

	error = -EMLINK;
	if (ip->i_di.di_nlink != 1)
		goto out_gunlock;
	error = -EINVAL;
	if (!S_ISREG(ip->i_di.di_mode))
		goto out_gunlock;

	old_qstr.name = old_name;
	old_qstr.len = strlen(old_name);
	error = gfs2_dir_search(old_dip, &old_qstr, &inum, NULL);
	switch (error) {
	case 0:
		break;
	default:
		goto out_gunlock;
	}

	error = -EINVAL;
	if (!gfs2_inum_equal(&inum, &ip->i_num))
		goto out_gunlock;

	new_qstr.name = new_name;
	new_qstr.len = strlen(new_name);
	error = gfs2_dir_search(new_dip, &new_qstr, NULL, NULL);
	switch (error) {
	case -ENOENT:
		break;
	case 0:
		error = -EEXIST;
	default:
		goto out_gunlock;
	}

	gfs2_alloc_get(ip);

	error = gfs2_quota_hold(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out_alloc;

	error = gfs2_diradd_alloc_required(new_dip, &new_qstr, &alloc_required);
	if (error)
		goto out_unhold;

	if (alloc_required) {
		struct gfs2_alloc *al = gfs2_alloc_get(new_dip);

		al->al_requested = sdp->sd_max_dirres;

		error = gfs2_inplace_reserve(new_dip);
		if (error)
			goto out_alloc2;

		error = gfs2_trans_begin(sdp,
					 sdp->sd_max_dirres +
					 al->al_rgd->rd_ri.ri_length +
					 3 * RES_DINODE + RES_LEAF +
					 RES_STATFS + RES_QUOTA, 0);
		if (error)
			goto out_ipreserv;
	} else {
		error = gfs2_trans_begin(sdp,
					 3 * RES_DINODE + 2 * RES_LEAF +
					 RES_QUOTA, 0);
		if (error)
			goto out_unhold;
	}
	
	error = gfs2_dir_del(old_dip, &old_qstr);
	if (error)
		goto out_trans;

	error = gfs2_dir_add(new_dip, &new_qstr, &ip->i_num,
			     IF2DT(ip->i_di.di_mode));
	if (error)
		goto out_trans;

	gfs2_quota_change(ip, -ip->i_di.di_blocks, ip->i_di.di_uid,
			  ip->i_di.di_gid);

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out_trans;
	ip->i_di.di_flags |= GFS2_DIF_SYSTEM;
	gfs2_trans_add_bh(ip->i_gl, dibh);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	drop_dentries(ip);

 out_trans:
	gfs2_trans_end(sdp);

 out_ipreserv:
	if (alloc_required)
		gfs2_inplace_release(new_dip);

 out_alloc2:
	if (alloc_required)
		gfs2_alloc_put(new_dip);

 out_unhold:
	gfs2_quota_unhold(ip);

 out_alloc:
	gfs2_alloc_put(ip);

 out_gunlock:
	gfs2_glock_dq_m(3, ghs);

 out:
	gfs2_holder_uninit(ghs);
	gfs2_holder_uninit(ghs + 1);
	gfs2_holder_uninit(ghs + 2);

	return error;
}

