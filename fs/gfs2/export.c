/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>

#include "gfs2.h"
#include "incore.h"
#include "dir.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "super.h"
#include "rgrp.h"
#include "util.h"

#define GFS2_SMALL_FH_SIZE 4
#define GFS2_LARGE_FH_SIZE 8
#define GFS2_OLD_FH_SIZE 10

static int gfs2_encode_fh(struct inode *inode, __u32 *p, int *len,
			  struct inode *parent)
{
	__be32 *fh = (__force __be32 *)p;
	struct super_block *sb = inode->i_sb;
	struct gfs2_inode *ip = GFS2_I(inode);

	if (parent && (*len < GFS2_LARGE_FH_SIZE)) {
		*len = GFS2_LARGE_FH_SIZE;
		return FILEID_INVALID;
	} else if (*len < GFS2_SMALL_FH_SIZE) {
		*len = GFS2_SMALL_FH_SIZE;
		return FILEID_INVALID;
	}

	fh[0] = cpu_to_be32(ip->i_no_formal_ino >> 32);
	fh[1] = cpu_to_be32(ip->i_no_formal_ino & 0xFFFFFFFF);
	fh[2] = cpu_to_be32(ip->i_no_addr >> 32);
	fh[3] = cpu_to_be32(ip->i_no_addr & 0xFFFFFFFF);
	*len = GFS2_SMALL_FH_SIZE;

	if (!parent || inode == sb->s_root->d_inode)
		return *len;

	ip = GFS2_I(parent);

	fh[4] = cpu_to_be32(ip->i_no_formal_ino >> 32);
	fh[5] = cpu_to_be32(ip->i_no_formal_ino & 0xFFFFFFFF);
	fh[6] = cpu_to_be32(ip->i_no_addr >> 32);
	fh[7] = cpu_to_be32(ip->i_no_addr & 0xFFFFFFFF);
	*len = GFS2_LARGE_FH_SIZE;

	return *len;
}

struct get_name_filldir {
	struct dir_context ctx;
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
	struct get_name_filldir gnfd = {
		.ctx.actor = get_name_filldir,
		.name = name
	};
	struct gfs2_holder gh;
	int error;
	struct file_ra_state f_ra = { .start = 0 };

	if (!dir)
		return -EINVAL;

	if (!S_ISDIR(dir->i_mode) || !inode)
		return -EINVAL;

	dip = GFS2_I(dir);
	ip = GFS2_I(inode);

	*name = 0;
	gnfd.inum.no_addr = ip->i_no_addr;
	gnfd.inum.no_formal_ino = ip->i_no_formal_ino;

	error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &gh);
	if (error)
		return error;

	error = gfs2_dir_read(dir, &gnfd.ctx, &f_ra);

	gfs2_glock_dq_uninit(&gh);

	if (!error && !*name)
		error = -ENOENT;

	return error;
}

static struct dentry *gfs2_get_parent(struct dentry *child)
{
	return d_obtain_alias(gfs2_lookupi(child->d_inode, &gfs2_qdotdot, 1));
}

static struct dentry *gfs2_get_dentry(struct super_block *sb,
				      struct gfs2_inum_host *inum)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct inode *inode;

	inode = gfs2_ilookup(sb, inum->no_addr, 0);
	if (inode) {
		if (GFS2_I(inode)->i_no_formal_ino != inum->no_formal_ino) {
			iput(inode);
			return ERR_PTR(-ESTALE);
		}
		goto out_inode;
	}

	inode = gfs2_lookup_by_inum(sdp, inum->no_addr, &inum->no_formal_ino,
				    GFS2_BLKST_DINODE);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

out_inode:
	return d_obtain_alias(inode);
}

static struct dentry *gfs2_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	struct gfs2_inum_host this;
	__be32 *fh = (__force __be32 *)fid->raw;

	switch (fh_type) {
	case GFS2_SMALL_FH_SIZE:
	case GFS2_LARGE_FH_SIZE:
	case GFS2_OLD_FH_SIZE:
		if (fh_len < GFS2_SMALL_FH_SIZE)
			return NULL;
		this.no_formal_ino = ((u64)be32_to_cpu(fh[0])) << 32;
		this.no_formal_ino |= be32_to_cpu(fh[1]);
		this.no_addr = ((u64)be32_to_cpu(fh[2])) << 32;
		this.no_addr |= be32_to_cpu(fh[3]);
		return gfs2_get_dentry(sb, &this);
	default:
		return NULL;
	}
}

static struct dentry *gfs2_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	struct gfs2_inum_host parent;
	__be32 *fh = (__force __be32 *)fid->raw;

	switch (fh_type) {
	case GFS2_LARGE_FH_SIZE:
	case GFS2_OLD_FH_SIZE:
		if (fh_len < GFS2_LARGE_FH_SIZE)
			return NULL;
		parent.no_formal_ino = ((u64)be32_to_cpu(fh[4])) << 32;
		parent.no_formal_ino |= be32_to_cpu(fh[5]);
		parent.no_addr = ((u64)be32_to_cpu(fh[6])) << 32;
		parent.no_addr |= be32_to_cpu(fh[7]);
		return gfs2_get_dentry(sb, &parent);
	default:
		return NULL;
	}
}

const struct export_operations gfs2_export_ops = {
	.encode_fh = gfs2_encode_fh,
	.fh_to_dentry = gfs2_fh_to_dentry,
	.fh_to_parent = gfs2_fh_to_parent,
	.get_name = gfs2_get_name,
	.get_parent = gfs2_get_parent,
};

