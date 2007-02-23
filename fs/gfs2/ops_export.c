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
#include "glops.h"
#include "inode.h"
#include "ops_dentry.h"
#include "ops_export.h"
#include "rgrp.h"
#include "util.h"

static struct dentry *gfs2_decode_fh(struct super_block *sb,
				     __u32 *p,
				     int fh_len,
				     int fh_type,
				     int (*acceptable)(void *context,
						       struct dentry *dentry),
				     void *context)
{
	__be32 *fh = (__force __be32 *)p;
	struct gfs2_fh_obj fh_obj;
	struct gfs2_inum_host *this, parent;

	this 		= &fh_obj.this;
	fh_obj.imode 	= DT_UNKNOWN;
	memset(&parent, 0, sizeof(struct gfs2_inum));

	switch (fh_len) {
	case GFS2_LARGE_FH_SIZE:
		parent.no_formal_ino = ((u64)be32_to_cpu(fh[4])) << 32;
		parent.no_formal_ino |= be32_to_cpu(fh[5]);
		parent.no_addr = ((u64)be32_to_cpu(fh[6])) << 32;
		parent.no_addr |= be32_to_cpu(fh[7]);
		fh_obj.imode = be32_to_cpu(fh[8]);
	case GFS2_SMALL_FH_SIZE:
		this->no_formal_ino = ((u64)be32_to_cpu(fh[0])) << 32;
		this->no_formal_ino |= be32_to_cpu(fh[1]);
		this->no_addr = ((u64)be32_to_cpu(fh[2])) << 32;
		this->no_addr |= be32_to_cpu(fh[3]);
		break;
	default:
		return NULL;
	}

	return gfs2_export_ops.find_exported_dentry(sb, &fh_obj, &parent,
						    acceptable, context);
}

static int gfs2_encode_fh(struct dentry *dentry, __u32 *p, int *len,
			  int connectable)
{
	__be32 *fh = (__force __be32 *)p;
	struct inode *inode = dentry->d_inode;
	struct super_block *sb = inode->i_sb;
	struct gfs2_inode *ip = GFS2_I(inode);

	if (*len < GFS2_SMALL_FH_SIZE ||
	    (connectable && *len < GFS2_LARGE_FH_SIZE))
		return 255;

	fh[0] = cpu_to_be32(ip->i_num.no_formal_ino >> 32);
	fh[1] = cpu_to_be32(ip->i_num.no_formal_ino & 0xFFFFFFFF);
	fh[2] = cpu_to_be32(ip->i_num.no_addr >> 32);
	fh[3] = cpu_to_be32(ip->i_num.no_addr & 0xFFFFFFFF);
	*len = GFS2_SMALL_FH_SIZE;

	if (!connectable || inode == sb->s_root->d_inode)
		return *len;

	spin_lock(&dentry->d_lock);
	inode = dentry->d_parent->d_inode;
	ip = GFS2_I(inode);
	igrab(inode);
	spin_unlock(&dentry->d_lock);

	fh[4] = cpu_to_be32(ip->i_num.no_formal_ino >> 32);
	fh[5] = cpu_to_be32(ip->i_num.no_formal_ino & 0xFFFFFFFF);
	fh[6] = cpu_to_be32(ip->i_num.no_addr >> 32);
	fh[7] = cpu_to_be32(ip->i_num.no_addr & 0xFFFFFFFF);

	fh[8]  = cpu_to_be32(inode->i_mode);
	fh[9]  = 0;	/* pad to double word */
	*len = GFS2_LARGE_FH_SIZE;

	iput(inode);

	return *len;
}

struct get_name_filldir {
	struct gfs2_inum_host inum;
	char *name;
};

static int get_name_filldir(void *opaque, const char *name, int length,
			    loff_t offset, u64 inum, unsigned int type)
{
	struct get_name_filldir *gnfd = opaque;

	if (inum != gnfd->inum.no_addr)
		return 0;

	memcpy(gnfd->name, name, length);
	gnfd->name[length] = 0;

	return 1;
}

static int gfs2_get_name(struct dentry *parent, char *name,
			 struct dentry *child)
{
	struct inode *dir = parent->d_inode;
	struct inode *inode = child->d_inode;
	struct gfs2_inode *dip, *ip;
	struct get_name_filldir gnfd;
	struct gfs2_holder gh;
	u64 offset = 0;
	int error;

	if (!dir)
		return -EINVAL;

	if (!S_ISDIR(dir->i_mode) || !inode)
		return -EINVAL;

	dip = GFS2_I(dir);
	ip = GFS2_I(inode);

	*name = 0;
	gnfd.inum = ip->i_num;
	gnfd.name = name;

	error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &gh);
	if (error)
		return error;

	error = gfs2_dir_read(dir, &offset, &gnfd, get_name_filldir);

	gfs2_glock_dq_uninit(&gh);

	if (!error && !*name)
		error = -ENOENT;

	return error;
}

static struct dentry *gfs2_get_parent(struct dentry *child)
{
	struct qstr dotdot;
	struct inode *inode;
	struct dentry *dentry;

	gfs2_str2qstr(&dotdot, "..");
	inode = gfs2_lookupi(child->d_inode, &dotdot, 1, NULL);

	if (!inode)
		return ERR_PTR(-ENOENT);
	/*
	 * In case of an error, @inode carries the error value, and we
	 * have to return that as a(n invalid) pointer to dentry.
	 */
	if (IS_ERR(inode))
		return ERR_PTR(PTR_ERR(inode));

	dentry = d_alloc_anon(inode);
	if (!dentry) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}

	dentry->d_op = &gfs2_dops;
	return dentry;
}

static struct dentry *gfs2_get_dentry(struct super_block *sb, void *inum_obj)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct gfs2_fh_obj *fh_obj = (struct gfs2_fh_obj *)inum_obj;
	struct gfs2_inum_host *inum = &fh_obj->this;
	struct gfs2_holder i_gh, ri_gh, rgd_gh;
	struct gfs2_rgrpd *rgd;
	struct inode *inode;
	struct dentry *dentry;
	int error;

	/* System files? */

	inode = gfs2_ilookup(sb, inum);
	if (inode) {
		if (GFS2_I(inode)->i_num.no_formal_ino != inum->no_formal_ino) {
			iput(inode);
			return ERR_PTR(-ESTALE);
		}
		goto out_inode;
	}

	error = gfs2_glock_nq_num(sdp, inum->no_addr, &gfs2_inode_glops,
				  LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return ERR_PTR(error);

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		goto fail;

	error = -EINVAL;
	rgd = gfs2_blk2rgrpd(sdp, inum->no_addr);
	if (!rgd)
		goto fail_rindex;

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_SHARED, 0, &rgd_gh);
	if (error)
		goto fail_rindex;

	error = -ESTALE;
	if (gfs2_get_block_type(rgd, inum->no_addr) != GFS2_BLKST_DINODE)
		goto fail_rgd;

	gfs2_glock_dq_uninit(&rgd_gh);
	gfs2_glock_dq_uninit(&ri_gh);

	inode = gfs2_inode_lookup(sb, inum, fh_obj->imode);
	if (!inode)
		goto fail;
	if (IS_ERR(inode)) {
		error = PTR_ERR(inode);
		goto fail;
	}

	error = gfs2_inode_refresh(GFS2_I(inode));
	if (error) {
		iput(inode);
		goto fail;
	}

	error = -EIO;
	if (GFS2_I(inode)->i_di.di_flags & GFS2_DIF_SYSTEM) {
		iput(inode);
		goto fail;
	}

	gfs2_glock_dq_uninit(&i_gh);

out_inode:
	dentry = d_alloc_anon(inode);
	if (!dentry) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}

	dentry->d_op = &gfs2_dops;
	return dentry;

fail_rgd:
	gfs2_glock_dq_uninit(&rgd_gh);

fail_rindex:
	gfs2_glock_dq_uninit(&ri_gh);

fail:
	gfs2_glock_dq_uninit(&i_gh);
	return ERR_PTR(error);
}

struct export_operations gfs2_export_ops = {
	.decode_fh = gfs2_decode_fh,
	.encode_fh = gfs2_encode_fh,
	.get_name = gfs2_get_name,
	.get_parent = gfs2_get_parent,
	.get_dentry = gfs2_get_dentry,
};

