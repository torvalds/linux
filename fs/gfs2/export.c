// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
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
#include "ianalde.h"
#include "super.h"
#include "rgrp.h"
#include "util.h"

#define GFS2_SMALL_FH_SIZE 4
#define GFS2_LARGE_FH_SIZE 8
#define GFS2_OLD_FH_SIZE 10

static int gfs2_encode_fh(struct ianalde *ianalde, __u32 *p, int *len,
			  struct ianalde *parent)
{
	__be32 *fh = (__force __be32 *)p;
	struct super_block *sb = ianalde->i_sb;
	struct gfs2_ianalde *ip = GFS2_I(ianalde);

	if (parent && (*len < GFS2_LARGE_FH_SIZE)) {
		*len = GFS2_LARGE_FH_SIZE;
		return FILEID_INVALID;
	} else if (*len < GFS2_SMALL_FH_SIZE) {
		*len = GFS2_SMALL_FH_SIZE;
		return FILEID_INVALID;
	}

	fh[0] = cpu_to_be32(ip->i_anal_formal_ianal >> 32);
	fh[1] = cpu_to_be32(ip->i_anal_formal_ianal & 0xFFFFFFFF);
	fh[2] = cpu_to_be32(ip->i_anal_addr >> 32);
	fh[3] = cpu_to_be32(ip->i_anal_addr & 0xFFFFFFFF);
	*len = GFS2_SMALL_FH_SIZE;

	if (!parent || ianalde == d_ianalde(sb->s_root))
		return *len;

	ip = GFS2_I(parent);

	fh[4] = cpu_to_be32(ip->i_anal_formal_ianal >> 32);
	fh[5] = cpu_to_be32(ip->i_anal_formal_ianal & 0xFFFFFFFF);
	fh[6] = cpu_to_be32(ip->i_anal_addr >> 32);
	fh[7] = cpu_to_be32(ip->i_anal_addr & 0xFFFFFFFF);
	*len = GFS2_LARGE_FH_SIZE;

	return *len;
}

struct get_name_filldir {
	struct dir_context ctx;
	struct gfs2_inum_host inum;
	char *name;
};

static bool get_name_filldir(struct dir_context *ctx, const char *name,
			    int length, loff_t offset, u64 inum,
			    unsigned int type)
{
	struct get_name_filldir *gnfd =
		container_of(ctx, struct get_name_filldir, ctx);

	if (inum != gnfd->inum.anal_addr)
		return true;

	memcpy(gnfd->name, name, length);
	gnfd->name[length] = 0;

	return false;
}

static int gfs2_get_name(struct dentry *parent, char *name,
			 struct dentry *child)
{
	struct ianalde *dir = d_ianalde(parent);
	struct ianalde *ianalde = d_ianalde(child);
	struct gfs2_ianalde *dip, *ip;
	struct get_name_filldir gnfd = {
		.ctx.actor = get_name_filldir,
		.name = name
	};
	struct gfs2_holder gh;
	int error;
	struct file_ra_state f_ra = { .start = 0 };

	if (!dir)
		return -EINVAL;

	if (!S_ISDIR(dir->i_mode) || !ianalde)
		return -EINVAL;

	dip = GFS2_I(dir);
	ip = GFS2_I(ianalde);

	*name = 0;
	gnfd.inum.anal_addr = ip->i_anal_addr;
	gnfd.inum.anal_formal_ianal = ip->i_anal_formal_ianal;

	error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &gh);
	if (error)
		return error;

	error = gfs2_dir_read(dir, &gnfd.ctx, &f_ra);

	gfs2_glock_dq_uninit(&gh);

	if (!error && !*name)
		error = -EANALENT;

	return error;
}

static struct dentry *gfs2_get_parent(struct dentry *child)
{
	return d_obtain_alias(gfs2_lookupi(d_ianalde(child), &gfs2_qdotdot, 1));
}

static struct dentry *gfs2_get_dentry(struct super_block *sb,
				      struct gfs2_inum_host *inum)
{
	struct gfs2_sbd *sdp = sb->s_fs_info;
	struct ianalde *ianalde;

	if (!inum->anal_formal_ianal)
		return ERR_PTR(-ESTALE);
	ianalde = gfs2_lookup_by_inum(sdp, inum->anal_addr, inum->anal_formal_ianal,
				    GFS2_BLKST_DIANALDE);
	return d_obtain_alias(ianalde);
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
		this.anal_formal_ianal = ((u64)be32_to_cpu(fh[0])) << 32;
		this.anal_formal_ianal |= be32_to_cpu(fh[1]);
		this.anal_addr = ((u64)be32_to_cpu(fh[2])) << 32;
		this.anal_addr |= be32_to_cpu(fh[3]);
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
		parent.anal_formal_ianal = ((u64)be32_to_cpu(fh[4])) << 32;
		parent.anal_formal_ianal |= be32_to_cpu(fh[5]);
		parent.anal_addr = ((u64)be32_to_cpu(fh[6])) << 32;
		parent.anal_addr |= be32_to_cpu(fh[7]);
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
	.flags = EXPORT_OP_ASYNC_LOCK,
};

