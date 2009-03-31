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
#include <linux/namei.h>
#include <linux/utsname.h>
#include <linux/mm.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/fiemap.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "bmap.h"
#include "dir.h"
#include "eaops.h"
#include "eattr.h"
#include "glock.h"
#include "inode.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"
#include "super.h"

/**
 * gfs2_create - Create a file
 * @dir: The directory in which to create the file
 * @dentry: The dentry of the new file
 * @mode: The mode of the new file
 *
 * Returns: errno
 */

static int gfs2_create(struct inode *dir, struct dentry *dentry,
		       int mode, struct nameidata *nd)
{
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct gfs2_holder ghs[2];
	struct inode *inode;

	gfs2_holder_init(dip->i_gl, 0, 0, ghs);

	for (;;) {
		inode = gfs2_createi(ghs, &dentry->d_name, S_IFREG | mode, 0);
		if (!IS_ERR(inode)) {
			gfs2_trans_end(sdp);
			if (dip->i_alloc->al_rgd)
				gfs2_inplace_release(dip);
			gfs2_quota_unlock(dip);
			gfs2_alloc_put(dip);
			gfs2_glock_dq_uninit_m(2, ghs);
			mark_inode_dirty(inode);
			break;
		} else if (PTR_ERR(inode) != -EEXIST ||
			   (nd && nd->flags & LOOKUP_EXCL)) {
			gfs2_holder_uninit(ghs);
			return PTR_ERR(inode);
		}

		inode = gfs2_lookupi(dir, &dentry->d_name, 0);
		if (inode) {
			if (!IS_ERR(inode)) {
				gfs2_holder_uninit(ghs);
				break;
			} else {
				gfs2_holder_uninit(ghs);
				return PTR_ERR(inode);
			}
		}
	}

	d_instantiate(dentry, inode);

	return 0;
}

/**
 * gfs2_lookup - Look up a filename in a directory and return its inode
 * @dir: The directory inode
 * @dentry: The dentry of the new inode
 * @nd: passed from Linux VFS, ignored by us
 *
 * Called by the VFS layer. Lock dir and call gfs2_lookupi()
 *
 * Returns: errno
 */

static struct dentry *gfs2_lookup(struct inode *dir, struct dentry *dentry,
				  struct nameidata *nd)
{
	struct inode *inode = NULL;

	dentry->d_op = &gfs2_dops;

	inode = gfs2_lookupi(dir, &dentry->d_name, 0);
	if (inode && IS_ERR(inode))
		return ERR_CAST(inode);

	if (inode) {
		struct gfs2_glock *gl = GFS2_I(inode)->i_gl;
		struct gfs2_holder gh;
		int error;
		error = gfs2_glock_nq_init(gl, LM_ST_SHARED, LM_FLAG_ANY, &gh);
		if (error) {
			iput(inode);
			return ERR_PTR(error);
		}
		gfs2_glock_dq_uninit(&gh);
		return d_splice_alias(inode, dentry);
	}
	d_add(dentry, inode);

	return NULL;
}

/**
 * gfs2_link - Link to a file
 * @old_dentry: The inode to link
 * @dir: Add link to this directory
 * @dentry: The name of the link
 *
 * Link the inode in "old_dentry" into the directory "dir" with the
 * name in "dentry".
 *
 * Returns: errno
 */

static int gfs2_link(struct dentry *old_dentry, struct inode *dir,
		     struct dentry *dentry)
{
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct inode *inode = old_dentry->d_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder ghs[2];
	int alloc_required;
	int error;

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	gfs2_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, ghs);
	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, ghs + 1);

	error = gfs2_glock_nq(ghs); /* parent */
	if (error)
		goto out_parent;

	error = gfs2_glock_nq(ghs + 1); /* child */
	if (error)
		goto out_child;

	error = gfs2_permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		goto out_gunlock;

	error = gfs2_dir_check(dir, &dentry->d_name, NULL);
	switch (error) {
	case -ENOENT:
		break;
	case 0:
		error = -EEXIST;
	default:
		goto out_gunlock;
	}

	error = -EINVAL;
	if (!dip->i_inode.i_nlink)
		goto out_gunlock;
	error = -EFBIG;
	if (dip->i_entries == (u32)-1)
		goto out_gunlock;
	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto out_gunlock;
	error = -EINVAL;
	if (!ip->i_inode.i_nlink)
		goto out_gunlock;
	error = -EMLINK;
	if (ip->i_inode.i_nlink == (u32)-1)
		goto out_gunlock;

	alloc_required = error = gfs2_diradd_alloc_required(dir, &dentry->d_name);
	if (error < 0)
		goto out_gunlock;
	error = 0;

	if (alloc_required) {
		struct gfs2_alloc *al = gfs2_alloc_get(dip);
		if (!al) {
			error = -ENOMEM;
			goto out_gunlock;
		}

		error = gfs2_quota_lock_check(dip);
		if (error)
			goto out_alloc;

		al->al_requested = sdp->sd_max_dirres;

		error = gfs2_inplace_reserve(dip);
		if (error)
			goto out_gunlock_q;

		error = gfs2_trans_begin(sdp, sdp->sd_max_dirres +
					 al->al_rgd->rd_length +
					 2 * RES_DINODE + RES_STATFS +
					 RES_QUOTA, 0);
		if (error)
			goto out_ipres;
	} else {
		error = gfs2_trans_begin(sdp, 2 * RES_DINODE + RES_LEAF, 0);
		if (error)
			goto out_ipres;
	}

	error = gfs2_dir_add(dir, &dentry->d_name, ip, IF2DT(inode->i_mode));
	if (error)
		goto out_end_trans;

	error = gfs2_change_nlink(ip, +1);

out_end_trans:
	gfs2_trans_end(sdp);
out_ipres:
	if (alloc_required)
		gfs2_inplace_release(dip);
out_gunlock_q:
	if (alloc_required)
		gfs2_quota_unlock(dip);
out_alloc:
	if (alloc_required)
		gfs2_alloc_put(dip);
out_gunlock:
	gfs2_glock_dq(ghs + 1);
out_child:
	gfs2_glock_dq(ghs);
out_parent:
	gfs2_holder_uninit(ghs);
	gfs2_holder_uninit(ghs + 1);
	if (!error) {
		atomic_inc(&inode->i_count);
		d_instantiate(dentry, inode);
		mark_inode_dirty(inode);
	}
	return error;
}

/**
 * gfs2_unlink - Unlink a file
 * @dir: The inode of the directory containing the file to unlink
 * @dentry: The file itself
 *
 * Unlink a file.  Call gfs2_unlinki()
 *
 * Returns: errno
 */

static int gfs2_unlink(struct inode *dir, struct dentry *dentry)
{
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct gfs2_inode *ip = GFS2_I(dentry->d_inode);
	struct gfs2_holder ghs[3];
	struct gfs2_rgrpd *rgd;
	struct gfs2_holder ri_gh;
	int error;

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		return error;

	gfs2_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, ghs);
	gfs2_holder_init(ip->i_gl,  LM_ST_EXCLUSIVE, 0, ghs + 1);

	rgd = gfs2_blk2rgrpd(sdp, ip->i_no_addr);
	gfs2_holder_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, ghs + 2);


	error = gfs2_glock_nq(ghs); /* parent */
	if (error)
		goto out_parent;

	error = gfs2_glock_nq(ghs + 1); /* child */
	if (error)
		goto out_child;

	error = gfs2_glock_nq(ghs + 2); /* rgrp */
	if (error)
		goto out_rgrp;

	error = gfs2_unlink_ok(dip, &dentry->d_name, ip);
	if (error)
		goto out_gunlock;

	error = gfs2_trans_begin(sdp, 2*RES_DINODE + RES_LEAF + RES_RG_BIT, 0);
	if (error)
		goto out_rgrp;

	error = gfs2_dir_del(dip, &dentry->d_name);
        if (error)
                goto out_end_trans;

	error = gfs2_change_nlink(ip, -1);

out_end_trans:
	gfs2_trans_end(sdp);
out_gunlock:
	gfs2_glock_dq(ghs + 2);
out_rgrp:
	gfs2_holder_uninit(ghs + 2);
	gfs2_glock_dq(ghs + 1);
out_child:
	gfs2_holder_uninit(ghs + 1);
	gfs2_glock_dq(ghs);
out_parent:
	gfs2_holder_uninit(ghs);
	gfs2_glock_dq_uninit(&ri_gh);
	return error;
}

/**
 * gfs2_symlink - Create a symlink
 * @dir: The directory to create the symlink in
 * @dentry: The dentry to put the symlink in
 * @symname: The thing which the link points to
 *
 * Returns: errno
 */

static int gfs2_symlink(struct inode *dir, struct dentry *dentry,
			const char *symname)
{
	struct gfs2_inode *dip = GFS2_I(dir), *ip;
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct gfs2_holder ghs[2];
	struct inode *inode;
	struct buffer_head *dibh;
	int size;
	int error;

	/* Must be stuffed with a null terminator for gfs2_follow_link() */
	size = strlen(symname);
	if (size > sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode) - 1)
		return -ENAMETOOLONG;

	gfs2_holder_init(dip->i_gl, 0, 0, ghs);

	inode = gfs2_createi(ghs, &dentry->d_name, S_IFLNK | S_IRWXUGO, 0);
	if (IS_ERR(inode)) {
		gfs2_holder_uninit(ghs);
		return PTR_ERR(inode);
	}

	ip = ghs[1].gh_gl->gl_object;

	ip->i_disksize = size;

	error = gfs2_meta_inode_buffer(ip, &dibh);

	if (!gfs2_assert_withdraw(sdp, !error)) {
		gfs2_dinode_out(ip, dibh->b_data);
		memcpy(dibh->b_data + sizeof(struct gfs2_dinode), symname,
		       size);
		brelse(dibh);
	}

	gfs2_trans_end(sdp);
	if (dip->i_alloc->al_rgd)
		gfs2_inplace_release(dip);
	gfs2_quota_unlock(dip);
	gfs2_alloc_put(dip);

	gfs2_glock_dq_uninit_m(2, ghs);

	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);

	return 0;
}

/**
 * gfs2_mkdir - Make a directory
 * @dir: The parent directory of the new one
 * @dentry: The dentry of the new directory
 * @mode: The mode of the new directory
 *
 * Returns: errno
 */

static int gfs2_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct gfs2_inode *dip = GFS2_I(dir), *ip;
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct gfs2_holder ghs[2];
	struct inode *inode;
	struct buffer_head *dibh;
	int error;

	gfs2_holder_init(dip->i_gl, 0, 0, ghs);

	inode = gfs2_createi(ghs, &dentry->d_name, S_IFDIR | mode, 0);
	if (IS_ERR(inode)) {
		gfs2_holder_uninit(ghs);
		return PTR_ERR(inode);
	}

	ip = ghs[1].gh_gl->gl_object;

	ip->i_inode.i_nlink = 2;
	ip->i_disksize = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_dinode);
	ip->i_diskflags |= GFS2_DIF_JDATA;
	ip->i_entries = 2;

	error = gfs2_meta_inode_buffer(ip, &dibh);

	if (!gfs2_assert_withdraw(sdp, !error)) {
		struct gfs2_dinode *di = (struct gfs2_dinode *)dibh->b_data;
		struct gfs2_dirent *dent = (struct gfs2_dirent *)(di+1);
		struct qstr str;

		gfs2_str2qstr(&str, ".");
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_qstr2dirent(&str, GFS2_DIRENT_SIZE(str.len), dent);
		dent->de_inum = di->di_num; /* already GFS2 endian */
		dent->de_type = cpu_to_be16(DT_DIR);
		di->di_entries = cpu_to_be32(1);

		gfs2_str2qstr(&str, "..");
		dent = (struct gfs2_dirent *)((char*)dent + GFS2_DIRENT_SIZE(1));
		gfs2_qstr2dirent(&str, dibh->b_size - GFS2_DIRENT_SIZE(1) - sizeof(struct gfs2_dinode), dent);

		gfs2_inum_out(dip, dent);
		dent->de_type = cpu_to_be16(DT_DIR);

		gfs2_dinode_out(ip, di);

		brelse(dibh);
	}

	error = gfs2_change_nlink(dip, +1);
	gfs2_assert_withdraw(sdp, !error); /* dip already pinned */

	gfs2_trans_end(sdp);
	if (dip->i_alloc->al_rgd)
		gfs2_inplace_release(dip);
	gfs2_quota_unlock(dip);
	gfs2_alloc_put(dip);

	gfs2_glock_dq_uninit_m(2, ghs);

	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);

	return 0;
}

/**
 * gfs2_rmdir - Remove a directory
 * @dir: The parent directory of the directory to be removed
 * @dentry: The dentry of the directory to remove
 *
 * Remove a directory. Call gfs2_rmdiri()
 *
 * Returns: errno
 */

static int gfs2_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct gfs2_inode *ip = GFS2_I(dentry->d_inode);
	struct gfs2_holder ghs[3];
	struct gfs2_rgrpd *rgd;
	struct gfs2_holder ri_gh;
	int error;

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		return error;
	gfs2_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, ghs);
	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, ghs + 1);

	rgd = gfs2_blk2rgrpd(sdp, ip->i_no_addr);
	gfs2_holder_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, ghs + 2);

	error = gfs2_glock_nq(ghs); /* parent */
	if (error)
		goto out_parent;

	error = gfs2_glock_nq(ghs + 1); /* child */
	if (error)
		goto out_child;

	error = gfs2_glock_nq(ghs + 2); /* rgrp */
	if (error)
		goto out_rgrp;

	error = gfs2_unlink_ok(dip, &dentry->d_name, ip);
	if (error)
		goto out_gunlock;

	if (ip->i_entries < 2) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(ip);
		error = -EIO;
		goto out_gunlock;
	}
	if (ip->i_entries > 2) {
		error = -ENOTEMPTY;
		goto out_gunlock;
	}

	error = gfs2_trans_begin(sdp, 2 * RES_DINODE + 3 * RES_LEAF + RES_RG_BIT, 0);
	if (error)
		goto out_gunlock;

	error = gfs2_rmdiri(dip, &dentry->d_name, ip);

	gfs2_trans_end(sdp);

out_gunlock:
	gfs2_glock_dq(ghs + 2);
out_rgrp:
	gfs2_holder_uninit(ghs + 2);
	gfs2_glock_dq(ghs + 1);
out_child:
	gfs2_holder_uninit(ghs + 1);
	gfs2_glock_dq(ghs);
out_parent:
	gfs2_holder_uninit(ghs);
	gfs2_glock_dq_uninit(&ri_gh);
	return error;
}

/**
 * gfs2_mknod - Make a special file
 * @dir: The directory in which the special file will reside
 * @dentry: The dentry of the special file
 * @mode: The mode of the special file
 * @rdev: The device specification of the special file
 *
 */

static int gfs2_mknod(struct inode *dir, struct dentry *dentry, int mode,
		      dev_t dev)
{
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct gfs2_holder ghs[2];
	struct inode *inode;

	gfs2_holder_init(dip->i_gl, 0, 0, ghs);

	inode = gfs2_createi(ghs, &dentry->d_name, mode, dev);
	if (IS_ERR(inode)) {
		gfs2_holder_uninit(ghs);
		return PTR_ERR(inode);
	}

	gfs2_trans_end(sdp);
	if (dip->i_alloc->al_rgd)
		gfs2_inplace_release(dip);
	gfs2_quota_unlock(dip);
	gfs2_alloc_put(dip);

	gfs2_glock_dq_uninit_m(2, ghs);

	d_instantiate(dentry, inode);
	mark_inode_dirty(inode);

	return 0;
}

/*
 * gfs2_ok_to_move - check if it's ok to move a directory to another directory
 * @this: move this
 * @to: to here
 *
 * Follow @to back to the root and make sure we don't encounter @this
 * Assumes we already hold the rename lock.
 *
 * Returns: errno
 */

static int gfs2_ok_to_move(struct gfs2_inode *this, struct gfs2_inode *to)
{
	struct inode *dir = &to->i_inode;
	struct super_block *sb = dir->i_sb;
	struct inode *tmp;
	struct qstr dotdot;
	int error = 0;

	gfs2_str2qstr(&dotdot, "..");

	igrab(dir);

	for (;;) {
		if (dir == &this->i_inode) {
			error = -EINVAL;
			break;
		}
		if (dir == sb->s_root->d_inode) {
			error = 0;
			break;
		}

		tmp = gfs2_lookupi(dir, &dotdot, 1);
		if (IS_ERR(tmp)) {
			error = PTR_ERR(tmp);
			break;
		}

		iput(dir);
		dir = tmp;
	}

	iput(dir);

	return error;
}

/**
 * gfs2_rename - Rename a file
 * @odir: Parent directory of old file name
 * @odentry: The old dentry of the file
 * @ndir: Parent directory of new file name
 * @ndentry: The new dentry of the file
 *
 * Returns: errno
 */

static int gfs2_rename(struct inode *odir, struct dentry *odentry,
		       struct inode *ndir, struct dentry *ndentry)
{
	struct gfs2_inode *odip = GFS2_I(odir);
	struct gfs2_inode *ndip = GFS2_I(ndir);
	struct gfs2_inode *ip = GFS2_I(odentry->d_inode);
	struct gfs2_inode *nip = NULL;
	struct gfs2_sbd *sdp = GFS2_SB(odir);
	struct gfs2_holder ghs[5], r_gh = { .gh_gl = NULL, };
	struct gfs2_rgrpd *nrgd;
	unsigned int num_gh;
	int dir_rename = 0;
	int alloc_required;
	unsigned int x;
	int error;

	if (ndentry->d_inode) {
		nip = GFS2_I(ndentry->d_inode);
		if (ip == nip)
			return 0;
	}


	if (odip != ndip) {
		error = gfs2_glock_nq_init(sdp->sd_rename_gl, LM_ST_EXCLUSIVE,
					   0, &r_gh);
		if (error)
			goto out;

		if (S_ISDIR(ip->i_inode.i_mode)) {
			dir_rename = 1;
			/* don't move a dirctory into it's subdir */
			error = gfs2_ok_to_move(ip, ndip);
			if (error)
				goto out_gunlock_r;
		}
	}

	num_gh = 1;
	gfs2_holder_init(odip->i_gl, LM_ST_EXCLUSIVE, 0, ghs);
	if (odip != ndip) {
		gfs2_holder_init(ndip->i_gl, LM_ST_EXCLUSIVE, 0, ghs + num_gh);
		num_gh++;
	}
	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, ghs + num_gh);
	num_gh++;

	if (nip) {
		gfs2_holder_init(nip->i_gl, LM_ST_EXCLUSIVE, 0, ghs + num_gh);
		num_gh++;
		/* grab the resource lock for unlink flag twiddling 
		 * this is the case of the target file already existing
		 * so we unlink before doing the rename
		 */
		nrgd = gfs2_blk2rgrpd(sdp, nip->i_no_addr);
		if (nrgd)
			gfs2_holder_init(nrgd->rd_gl, LM_ST_EXCLUSIVE, 0, ghs + num_gh++);
	}

	for (x = 0; x < num_gh; x++) {
		error = gfs2_glock_nq(ghs + x);
		if (error)
			goto out_gunlock;
	}

	/* Check out the old directory */

	error = gfs2_unlink_ok(odip, &odentry->d_name, ip);
	if (error)
		goto out_gunlock;

	/* Check out the new directory */

	if (nip) {
		error = gfs2_unlink_ok(ndip, &ndentry->d_name, nip);
		if (error)
			goto out_gunlock;

		if (S_ISDIR(nip->i_inode.i_mode)) {
			if (nip->i_entries < 2) {
				if (gfs2_consist_inode(nip))
					gfs2_dinode_print(nip);
				error = -EIO;
				goto out_gunlock;
			}
			if (nip->i_entries > 2) {
				error = -ENOTEMPTY;
				goto out_gunlock;
			}
		}
	} else {
		error = gfs2_permission(ndir, MAY_WRITE | MAY_EXEC);
		if (error)
			goto out_gunlock;

		error = gfs2_dir_check(ndir, &ndentry->d_name, NULL);
		switch (error) {
		case -ENOENT:
			error = 0;
			break;
		case 0:
			error = -EEXIST;
		default:
			goto out_gunlock;
		};

		if (odip != ndip) {
			if (!ndip->i_inode.i_nlink) {
				error = -EINVAL;
				goto out_gunlock;
			}
			if (ndip->i_entries == (u32)-1) {
				error = -EFBIG;
				goto out_gunlock;
			}
			if (S_ISDIR(ip->i_inode.i_mode) &&
			    ndip->i_inode.i_nlink == (u32)-1) {
				error = -EMLINK;
				goto out_gunlock;
			}
		}
	}

	/* Check out the dir to be renamed */

	if (dir_rename) {
		error = gfs2_permission(odentry->d_inode, MAY_WRITE);
		if (error)
			goto out_gunlock;
	}

	alloc_required = error = gfs2_diradd_alloc_required(ndir, &ndentry->d_name);
	if (error < 0)
		goto out_gunlock;
	error = 0;

	if (alloc_required) {
		struct gfs2_alloc *al = gfs2_alloc_get(ndip);
		if (!al) {
			error = -ENOMEM;
			goto out_gunlock;
		}

		error = gfs2_quota_lock_check(ndip);
		if (error)
			goto out_alloc;

		al->al_requested = sdp->sd_max_dirres;

		error = gfs2_inplace_reserve(ndip);
		if (error)
			goto out_gunlock_q;

		error = gfs2_trans_begin(sdp, sdp->sd_max_dirres +
					 al->al_rgd->rd_length +
					 4 * RES_DINODE + 4 * RES_LEAF +
					 RES_STATFS + RES_QUOTA + 4, 0);
		if (error)
			goto out_ipreserv;
	} else {
		error = gfs2_trans_begin(sdp, 4 * RES_DINODE +
					 5 * RES_LEAF + 4, 0);
		if (error)
			goto out_gunlock;
	}

	/* Remove the target file, if it exists */

	if (nip) {
		if (S_ISDIR(nip->i_inode.i_mode))
			error = gfs2_rmdiri(ndip, &ndentry->d_name, nip);
		else {
			error = gfs2_dir_del(ndip, &ndentry->d_name);
			if (error)
				goto out_end_trans;
			error = gfs2_change_nlink(nip, -1);
		}
		if (error)
			goto out_end_trans;
	}

	if (dir_rename) {
		struct qstr name;
		gfs2_str2qstr(&name, "..");

		error = gfs2_change_nlink(ndip, +1);
		if (error)
			goto out_end_trans;
		error = gfs2_change_nlink(odip, -1);
		if (error)
			goto out_end_trans;

		error = gfs2_dir_mvino(ip, &name, ndip, DT_DIR);
		if (error)
			goto out_end_trans;
	} else {
		struct buffer_head *dibh;
		error = gfs2_meta_inode_buffer(ip, &dibh);
		if (error)
			goto out_end_trans;
		ip->i_inode.i_ctime = CURRENT_TIME;
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		brelse(dibh);
	}

	error = gfs2_dir_del(odip, &odentry->d_name);
	if (error)
		goto out_end_trans;

	error = gfs2_dir_add(ndir, &ndentry->d_name, ip, IF2DT(ip->i_inode.i_mode));
	if (error)
		goto out_end_trans;

out_end_trans:
	gfs2_trans_end(sdp);
out_ipreserv:
	if (alloc_required)
		gfs2_inplace_release(ndip);
out_gunlock_q:
	if (alloc_required)
		gfs2_quota_unlock(ndip);
out_alloc:
	if (alloc_required)
		gfs2_alloc_put(ndip);
out_gunlock:
	while (x--) {
		gfs2_glock_dq(ghs + x);
		gfs2_holder_uninit(ghs + x);
	}
out_gunlock_r:
	if (r_gh.gh_gl)
		gfs2_glock_dq_uninit(&r_gh);
out:
	return error;
}

/**
 * gfs2_readlink - Read the value of a symlink
 * @dentry: the symlink
 * @buf: the buffer to read the symlink data into
 * @size: the size of the buffer
 *
 * Returns: errno
 */

static int gfs2_readlink(struct dentry *dentry, char __user *user_buf,
			 int user_size)
{
	struct gfs2_inode *ip = GFS2_I(dentry->d_inode);
	char array[GFS2_FAST_NAME_SIZE], *buf = array;
	unsigned int len = GFS2_FAST_NAME_SIZE;
	int error;

	error = gfs2_readlinki(ip, &buf, &len);
	if (error)
		return error;

	if (user_size > len - 1)
		user_size = len - 1;

	if (copy_to_user(user_buf, buf, user_size))
		error = -EFAULT;
	else
		error = user_size;

	if (buf != array)
		kfree(buf);

	return error;
}

/**
 * gfs2_follow_link - Follow a symbolic link
 * @dentry: The dentry of the link
 * @nd: Data that we pass to vfs_follow_link()
 *
 * This can handle symlinks of any size. It is optimised for symlinks
 * under GFS2_FAST_NAME_SIZE.
 *
 * Returns: 0 on success or error code
 */

static void *gfs2_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	struct gfs2_inode *ip = GFS2_I(dentry->d_inode);
	char array[GFS2_FAST_NAME_SIZE], *buf = array;
	unsigned int len = GFS2_FAST_NAME_SIZE;
	int error;

	error = gfs2_readlinki(ip, &buf, &len);
	if (!error) {
		error = vfs_follow_link(nd, buf);
		if (buf != array)
			kfree(buf);
	}

	return ERR_PTR(error);
}

/**
 * gfs2_permission -
 * @inode:
 * @mask:
 * @nd: passed from Linux VFS, ignored by us
 *
 * This may be called from the VFS directly, or from within GFS2 with the
 * inode locked, so we look to see if the glock is already locked and only
 * lock the glock if its not already been done.
 *
 * Returns: errno
 */

int gfs2_permission(struct inode *inode, int mask)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder i_gh;
	int error;
	int unlock = 0;

	if (gfs2_glock_is_locked_by_me(ip->i_gl) == NULL) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
		if (error)
			return error;
		unlock = 1;
	}

	if ((mask & MAY_WRITE) && IS_IMMUTABLE(inode))
		error = -EACCES;
	else
		error = generic_permission(inode, mask, gfs2_check_acl);
	if (unlock)
		gfs2_glock_dq_uninit(&i_gh);

	return error;
}

static int setattr_size(struct inode *inode, struct iattr *attr)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	int error;

	if (attr->ia_size != ip->i_disksize) {
		error = gfs2_trans_begin(sdp, 0, sdp->sd_jdesc->jd_blocks);
		if (error)
			return error;
		error = vmtruncate(inode, attr->ia_size);
		gfs2_trans_end(sdp);
		if (error) 
			return error;
	}

	error = gfs2_truncatei(ip, attr->ia_size);
	if (error && (inode->i_size != ip->i_disksize))
		i_size_write(inode, ip->i_disksize);

	return error;
}

static int setattr_chown(struct inode *inode, struct iattr *attr)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *dibh;
	u32 ouid, ogid, nuid, ngid;
	int error;

	ouid = inode->i_uid;
	ogid = inode->i_gid;
	nuid = attr->ia_uid;
	ngid = attr->ia_gid;

	if (!(attr->ia_valid & ATTR_UID) || ouid == nuid)
		ouid = nuid = NO_QUOTA_CHANGE;
	if (!(attr->ia_valid & ATTR_GID) || ogid == ngid)
		ogid = ngid = NO_QUOTA_CHANGE;

	if (!gfs2_alloc_get(ip))
		return -ENOMEM;

	error = gfs2_quota_lock(ip, nuid, ngid);
	if (error)
		goto out_alloc;

	if (ouid != NO_QUOTA_CHANGE || ogid != NO_QUOTA_CHANGE) {
		error = gfs2_quota_check(ip, nuid, ngid);
		if (error)
			goto out_gunlock_q;
	}

	error = gfs2_trans_begin(sdp, RES_DINODE + 2 * RES_QUOTA, 0);
	if (error)
		goto out_gunlock_q;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out_end_trans;

	error = inode_setattr(inode, attr);
	gfs2_assert_warn(sdp, !error);

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);

	if (ouid != NO_QUOTA_CHANGE || ogid != NO_QUOTA_CHANGE) {
		u64 blocks = gfs2_get_inode_blocks(&ip->i_inode);
		gfs2_quota_change(ip, -blocks, ouid, ogid);
		gfs2_quota_change(ip, blocks, nuid, ngid);
	}

out_end_trans:
	gfs2_trans_end(sdp);
out_gunlock_q:
	gfs2_quota_unlock(ip);
out_alloc:
	gfs2_alloc_put(ip);
	return error;
}

/**
 * gfs2_setattr - Change attributes on an inode
 * @dentry: The dentry which is changing
 * @attr: The structure describing the change
 *
 * The VFS layer wants to change one or more of an inodes attributes.  Write
 * that change out to disk.
 *
 * Returns: errno
 */

static int gfs2_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = dentry->d_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder i_gh;
	int error;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		return error;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto out;

	error = inode_change_ok(inode, attr);
	if (error)
		goto out;

	if (attr->ia_valid & ATTR_SIZE)
		error = setattr_size(inode, attr);
	else if (attr->ia_valid & (ATTR_UID | ATTR_GID))
		error = setattr_chown(inode, attr);
	else if ((attr->ia_valid & ATTR_MODE) && IS_POSIXACL(inode))
		error = gfs2_acl_chmod(ip, attr);
	else
		error = gfs2_setattr_simple(ip, attr);

out:
	gfs2_glock_dq_uninit(&i_gh);
	if (!error)
		mark_inode_dirty(inode);
	return error;
}

/**
 * gfs2_getattr - Read out an inode's attributes
 * @mnt: The vfsmount the inode is being accessed from
 * @dentry: The dentry to stat
 * @stat: The inode's stats
 *
 * This may be called from the VFS directly, or from within GFS2 with the
 * inode locked, so we look to see if the glock is already locked and only
 * lock the glock if its not already been done. Note that its the NFS
 * readdirplus operation which causes this to be called (from filldir)
 * with the glock already held.
 *
 * Returns: errno
 */

static int gfs2_getattr(struct vfsmount *mnt, struct dentry *dentry,
			struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	int error;
	int unlock = 0;

	if (gfs2_glock_is_locked_by_me(ip->i_gl) == NULL) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &gh);
		if (error)
			return error;
		unlock = 1;
	}

	generic_fillattr(inode, stat);
	if (unlock)
		gfs2_glock_dq_uninit(&gh);

	return 0;
}

static int gfs2_setxattr(struct dentry *dentry, const char *name,
			 const void *data, size_t size, int flags)
{
	struct inode *inode = dentry->d_inode;
	struct gfs2_ea_request er;

	memset(&er, 0, sizeof(struct gfs2_ea_request));
	er.er_type = gfs2_ea_name2type(name, &er.er_name);
	if (er.er_type == GFS2_EATYPE_UNUSED)
		return -EOPNOTSUPP;
	er.er_data = (char *)data;
	er.er_name_len = strlen(er.er_name);
	er.er_data_len = size;
	er.er_flags = flags;

	gfs2_assert_warn(GFS2_SB(inode), !(er.er_flags & GFS2_ERF_MODE));

	return gfs2_ea_set(GFS2_I(inode), &er);
}

static ssize_t gfs2_getxattr(struct dentry *dentry, const char *name,
			     void *data, size_t size)
{
	struct gfs2_ea_request er;

	memset(&er, 0, sizeof(struct gfs2_ea_request));
	er.er_type = gfs2_ea_name2type(name, &er.er_name);
	if (er.er_type == GFS2_EATYPE_UNUSED)
		return -EOPNOTSUPP;
	er.er_data = data;
	er.er_name_len = strlen(er.er_name);
	er.er_data_len = size;

	return gfs2_ea_get(GFS2_I(dentry->d_inode), &er);
}

static ssize_t gfs2_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	struct gfs2_ea_request er;

	memset(&er, 0, sizeof(struct gfs2_ea_request));
	er.er_data = (size) ? buffer : NULL;
	er.er_data_len = size;

	return gfs2_ea_list(GFS2_I(dentry->d_inode), &er);
}

static int gfs2_removexattr(struct dentry *dentry, const char *name)
{
	struct gfs2_ea_request er;

	memset(&er, 0, sizeof(struct gfs2_ea_request));
	er.er_type = gfs2_ea_name2type(name, &er.er_name);
	if (er.er_type == GFS2_EATYPE_UNUSED)
		return -EOPNOTSUPP;
	er.er_name_len = strlen(er.er_name);

	return gfs2_ea_remove(GFS2_I(dentry->d_inode), &er);
}

static int gfs2_fiemap(struct inode *inode, struct fiemap_extent_info *fieinfo,
		       u64 start, u64 len)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	int ret;

	ret = fiemap_check_flags(fieinfo, FIEMAP_FLAG_SYNC);
	if (ret)
		return ret;

	mutex_lock(&inode->i_mutex);

	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
	if (ret)
		goto out;

	if (gfs2_is_stuffed(ip)) {
		u64 phys = ip->i_no_addr << inode->i_blkbits;
		u64 size = i_size_read(inode);
		u32 flags = FIEMAP_EXTENT_LAST|FIEMAP_EXTENT_NOT_ALIGNED|
			    FIEMAP_EXTENT_DATA_INLINE;
		phys += sizeof(struct gfs2_dinode);
		phys += start;
		if (start + len > size)
			len = size - start;
		if (start < size)
			ret = fiemap_fill_next_extent(fieinfo, start, phys,
						      len, flags);
		if (ret == 1)
			ret = 0;
	} else {
		ret = __generic_block_fiemap(inode, fieinfo, start, len,
					     gfs2_block_map);
	}

	gfs2_glock_dq_uninit(&gh);
out:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

const struct inode_operations gfs2_file_iops = {
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.setxattr = gfs2_setxattr,
	.getxattr = gfs2_getxattr,
	.listxattr = gfs2_listxattr,
	.removexattr = gfs2_removexattr,
	.fiemap = gfs2_fiemap,
};

const struct inode_operations gfs2_dir_iops = {
	.create = gfs2_create,
	.lookup = gfs2_lookup,
	.link = gfs2_link,
	.unlink = gfs2_unlink,
	.symlink = gfs2_symlink,
	.mkdir = gfs2_mkdir,
	.rmdir = gfs2_rmdir,
	.mknod = gfs2_mknod,
	.rename = gfs2_rename,
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.setxattr = gfs2_setxattr,
	.getxattr = gfs2_getxattr,
	.listxattr = gfs2_listxattr,
	.removexattr = gfs2_removexattr,
	.fiemap = gfs2_fiemap,
};

const struct inode_operations gfs2_symlink_iops = {
	.readlink = gfs2_readlink,
	.follow_link = gfs2_follow_link,
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.setxattr = gfs2_setxattr,
	.getxattr = gfs2_getxattr,
	.listxattr = gfs2_listxattr,
	.removexattr = gfs2_removexattr,
	.fiemap = gfs2_fiemap,
};

