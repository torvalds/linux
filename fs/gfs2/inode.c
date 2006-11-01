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
#include <linux/posix_acl.h>
#include <linux/sort.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/lm_interface.h>
#include <linux/security.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "bmap.h"
#include "dir.h"
#include "eattr.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "ops_address.h"
#include "ops_file.h"
#include "ops_inode.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"

/**
 * gfs2_inode_attr_in - Copy attributes from the dinode into the VFS inode
 * @ip: The GFS2 inode (with embedded disk inode data)
 * @inode:  The Linux VFS inode
 *
 */

void gfs2_inode_attr_in(struct gfs2_inode *ip)
{
	struct inode *inode = &ip->i_inode;
	struct gfs2_dinode_host *di = &ip->i_di;

	inode->i_ino = ip->i_num.no_addr;
	i_size_write(inode, di->di_size);
	inode->i_blocks = di->di_blocks <<
		(GFS2_SB(inode)->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);

	if (di->di_flags & GFS2_DIF_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;

	if (di->di_flags & GFS2_DIF_APPENDONLY)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
}

static int iget_test(struct inode *inode, void *opaque)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_inum_host *inum = opaque;

	if (ip && ip->i_num.no_addr == inum->no_addr)
		return 1;

	return 0;
}

static int iget_set(struct inode *inode, void *opaque)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_inum_host *inum = opaque;

	ip->i_num = *inum;
	return 0;
}

struct inode *gfs2_ilookup(struct super_block *sb, struct gfs2_inum_host *inum)
{
	return ilookup5(sb, (unsigned long)inum->no_formal_ino,
			iget_test, inum);
}

static struct inode *gfs2_iget(struct super_block *sb, struct gfs2_inum_host *inum)
{
	return iget5_locked(sb, (unsigned long)inum->no_formal_ino,
		     iget_test, iget_set, inum);
}

/**
 * gfs2_inode_lookup - Lookup an inode
 * @sb: The super block
 * @inum: The inode number
 * @type: The type of the inode
 *
 * Returns: A VFS inode, or an error
 */

struct inode *gfs2_inode_lookup(struct super_block *sb, struct gfs2_inum_host *inum, unsigned int type)
{
	struct inode *inode = gfs2_iget(sb, inum);
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_glock *io_gl;
	int error;

	if (!inode)
		return ERR_PTR(-ENOBUFS);

	if (inode->i_state & I_NEW) {
		struct gfs2_sbd *sdp = GFS2_SB(inode);
		umode_t mode = DT2IF(type);
		inode->i_private = ip;
		inode->i_mode = mode;

		if (S_ISREG(mode)) {
			inode->i_op = &gfs2_file_iops;
			inode->i_fop = &gfs2_file_fops;
			inode->i_mapping->a_ops = &gfs2_file_aops;
		} else if (S_ISDIR(mode)) {
			inode->i_op = &gfs2_dir_iops;
			inode->i_fop = &gfs2_dir_fops;
		} else if (S_ISLNK(mode)) {
			inode->i_op = &gfs2_symlink_iops;
		} else {
			inode->i_op = &gfs2_dev_iops;
		}

		error = gfs2_glock_get(sdp, inum->no_addr, &gfs2_inode_glops, CREATE, &ip->i_gl);
		if (unlikely(error))
			goto fail;
		ip->i_gl->gl_object = ip;

		error = gfs2_glock_get(sdp, inum->no_addr, &gfs2_iopen_glops, CREATE, &io_gl);
		if (unlikely(error))
			goto fail_put;

		ip->i_vn = ip->i_gl->gl_vn - 1;
		error = gfs2_glock_nq_init(io_gl, LM_ST_SHARED, GL_EXACT, &ip->i_iopen_gh);
		if (unlikely(error))
			goto fail_iopen;

		gfs2_glock_put(io_gl);
		unlock_new_inode(inode);
	}

	return inode;
fail_iopen:
	gfs2_glock_put(io_gl);
fail_put:
	ip->i_gl->gl_object = NULL;
	gfs2_glock_put(ip->i_gl);
fail:
	iput(inode);
	return ERR_PTR(error);
}

static int gfs2_dinode_in(struct gfs2_inode *ip, const void *buf)
{
	struct gfs2_dinode_host *di = &ip->i_di;
	const struct gfs2_dinode *str = buf;

	if (ip->i_num.no_addr != be64_to_cpu(str->di_num.no_addr)) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(ip);
		return -EIO;
	}
	if (ip->i_num.no_formal_ino != be64_to_cpu(str->di_num.no_formal_ino))
		return -ESTALE;

	ip->i_inode.i_mode = be32_to_cpu(str->di_mode);
	ip->i_inode.i_rdev = 0;
	switch (ip->i_inode.i_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		ip->i_inode.i_rdev = MKDEV(be32_to_cpu(str->di_major),
					   be32_to_cpu(str->di_minor));
		break;
	};

	ip->i_inode.i_uid = be32_to_cpu(str->di_uid);
	ip->i_inode.i_gid = be32_to_cpu(str->di_gid);
	/*
	 * We will need to review setting the nlink count here in the
	 * light of the forthcoming ro bind mount work. This is a reminder
	 * to do that.
	 */
	ip->i_inode.i_nlink = be32_to_cpu(str->di_nlink);
	di->di_size = be64_to_cpu(str->di_size);
	di->di_blocks = be64_to_cpu(str->di_blocks);
	ip->i_inode.i_atime.tv_sec = be64_to_cpu(str->di_atime);
	ip->i_inode.i_atime.tv_nsec = 0;
	ip->i_inode.i_mtime.tv_sec = be64_to_cpu(str->di_mtime);
	ip->i_inode.i_mtime.tv_nsec = 0;
	ip->i_inode.i_ctime.tv_sec = be64_to_cpu(str->di_ctime);
	ip->i_inode.i_ctime.tv_nsec = 0;

	di->di_goal_meta = be64_to_cpu(str->di_goal_meta);
	di->di_goal_data = be64_to_cpu(str->di_goal_data);
	di->di_generation = be64_to_cpu(str->di_generation);

	di->di_flags = be32_to_cpu(str->di_flags);
	di->di_payload_format = be32_to_cpu(str->di_payload_format);
	di->di_height = be16_to_cpu(str->di_height);

	di->di_depth = be16_to_cpu(str->di_depth);
	di->di_entries = be32_to_cpu(str->di_entries);

	di->di_eattr = be64_to_cpu(str->di_eattr);
	return 0;
}

/**
 * gfs2_inode_refresh - Refresh the incore copy of the dinode
 * @ip: The GFS2 inode
 *
 * Returns: errno
 */

int gfs2_inode_refresh(struct gfs2_inode *ip)
{
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	if (gfs2_metatype_check(GFS2_SB(&ip->i_inode), dibh, GFS2_METATYPE_DI)) {
		brelse(dibh);
		return -EIO;
	}

	error = gfs2_dinode_in(ip, dibh->b_data);
	brelse(dibh);
	ip->i_vn = ip->i_gl->gl_vn;

	return error;
}

int gfs2_dinode_dealloc(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_alloc *al;
	struct gfs2_rgrpd *rgd;
	int error;

	if (ip->i_di.di_blocks != 1) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(ip);
		return -EIO;
	}

	al = gfs2_alloc_get(ip);

	error = gfs2_quota_hold(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs2_rindex_hold(sdp, &al->al_ri_gh);
	if (error)
		goto out_qs;

	rgd = gfs2_blk2rgrpd(sdp, ip->i_num.no_addr);
	if (!rgd) {
		gfs2_consist_inode(ip);
		error = -EIO;
		goto out_rindex_relse;
	}

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0,
				   &al->al_rgd_gh);
	if (error)
		goto out_rindex_relse;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_STATFS + RES_QUOTA, 1);
	if (error)
		goto out_rg_gunlock;

	gfs2_trans_add_gl(ip->i_gl);

	gfs2_free_di(rgd, ip);

	gfs2_trans_end(sdp);
	clear_bit(GLF_STICKY, &ip->i_gl->gl_flags);

out_rg_gunlock:
	gfs2_glock_dq_uninit(&al->al_rgd_gh);
out_rindex_relse:
	gfs2_glock_dq_uninit(&al->al_ri_gh);
out_qs:
	gfs2_quota_unhold(ip);
out:
	gfs2_alloc_put(ip);
	return error;
}

/**
 * gfs2_change_nlink - Change nlink count on inode
 * @ip: The GFS2 inode
 * @diff: The change in the nlink count required
 *
 * Returns: errno
 */

int gfs2_change_nlink(struct gfs2_inode *ip, int diff)
{
	struct gfs2_sbd *sdp = ip->i_inode.i_sb->s_fs_info;
	struct buffer_head *dibh;
	u32 nlink;
	int error;

	BUG_ON(diff != 1 && diff != -1);
	nlink = ip->i_inode.i_nlink + diff;

	/* If we are reducing the nlink count, but the new value ends up being
	   bigger than the old one, we must have underflowed. */
	if (diff < 0 && nlink > ip->i_inode.i_nlink) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(ip);
		return -EIO;
	}

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	if (diff > 0)
		inc_nlink(&ip->i_inode);
	else
		drop_nlink(&ip->i_inode);

	ip->i_inode.i_ctime.tv_sec = get_seconds();

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);
	mark_inode_dirty(&ip->i_inode);

	if (ip->i_inode.i_nlink == 0) {
		struct gfs2_rgrpd *rgd;
		struct gfs2_holder ri_gh, rg_gh;

		error = gfs2_rindex_hold(sdp, &ri_gh);
		if (error)
			goto out;
		error = -EIO;
		rgd = gfs2_blk2rgrpd(sdp, ip->i_num.no_addr);
		if (!rgd)
			goto out_norgrp;
		error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &rg_gh);
		if (error)
			goto out_norgrp;

		gfs2_unlink_di(&ip->i_inode); /* mark inode unlinked */
		gfs2_glock_dq_uninit(&rg_gh);
out_norgrp:
		gfs2_glock_dq_uninit(&ri_gh);
	}
out:
	return error;
}

struct inode *gfs2_lookup_simple(struct inode *dip, const char *name)
{
	struct qstr qstr;
	gfs2_str2qstr(&qstr, name);
	return gfs2_lookupi(dip, &qstr, 1, NULL);
}


/**
 * gfs2_lookupi - Look up a filename in a directory and return its inode
 * @d_gh: An initialized holder for the directory glock
 * @name: The name of the inode to look for
 * @is_root: If 1, ignore the caller's permissions
 * @i_gh: An uninitialized holder for the new inode glock
 *
 * There will always be a vnode (Linux VFS inode) for the d_gh inode unless
 * @is_root is true.
 *
 * Returns: errno
 */

struct inode *gfs2_lookupi(struct inode *dir, const struct qstr *name,
			   int is_root, struct nameidata *nd)
{
	struct super_block *sb = dir->i_sb;
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_holder d_gh;
	struct gfs2_inum_host inum;
	unsigned int type;
	int error = 0;
	struct inode *inode = NULL;

	if (!name->len || name->len > GFS2_FNAMESIZE)
		return ERR_PTR(-ENAMETOOLONG);

	if ((name->len == 1 && memcmp(name->name, ".", 1) == 0) ||
	    (name->len == 2 && memcmp(name->name, "..", 2) == 0 &&
	     dir == sb->s_root->d_inode)) {
		igrab(dir);
		return dir;
	}

	error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
	if (error)
		return ERR_PTR(error);

	if (!is_root) {
		error = permission(dir, MAY_EXEC, NULL);
		if (error)
			goto out;
	}

	error = gfs2_dir_search(dir, name, &inum, &type);
	if (error)
		goto out;

	inode = gfs2_inode_lookup(sb, &inum, type);

out:
	gfs2_glock_dq_uninit(&d_gh);
	if (error == -ENOENT)
		return NULL;
	return inode;
}

static int pick_formal_ino_1(struct gfs2_sbd *sdp, u64 *formal_ino)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_ir_inode);
	struct buffer_head *bh;
	struct gfs2_inum_range_host ir;
	int error;

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		return error;
	mutex_lock(&sdp->sd_inum_mutex);

	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error) {
		mutex_unlock(&sdp->sd_inum_mutex);
		gfs2_trans_end(sdp);
		return error;
	}

	gfs2_inum_range_in(&ir, bh->b_data + sizeof(struct gfs2_dinode));

	if (ir.ir_length) {
		*formal_ino = ir.ir_start++;
		ir.ir_length--;
		gfs2_trans_add_bh(ip->i_gl, bh, 1);
		gfs2_inum_range_out(&ir,
				    bh->b_data + sizeof(struct gfs2_dinode));
		brelse(bh);
		mutex_unlock(&sdp->sd_inum_mutex);
		gfs2_trans_end(sdp);
		return 0;
	}

	brelse(bh);

	mutex_unlock(&sdp->sd_inum_mutex);
	gfs2_trans_end(sdp);

	return 1;
}

static int pick_formal_ino_2(struct gfs2_sbd *sdp, u64 *formal_ino)
{
	struct gfs2_inode *ip = GFS2_I(sdp->sd_ir_inode);
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_inum_inode);
	struct gfs2_holder gh;
	struct buffer_head *bh;
	struct gfs2_inum_range_host ir;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (error)
		return error;

	error = gfs2_trans_begin(sdp, 2 * RES_DINODE, 0);
	if (error)
		goto out;
	mutex_lock(&sdp->sd_inum_mutex);

	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		goto out_end_trans;

	gfs2_inum_range_in(&ir, bh->b_data + sizeof(struct gfs2_dinode));

	if (!ir.ir_length) {
		struct buffer_head *m_bh;
		u64 x, y;
		__be64 z;

		error = gfs2_meta_inode_buffer(m_ip, &m_bh);
		if (error)
			goto out_brelse;

		z = *(__be64 *)(m_bh->b_data + sizeof(struct gfs2_dinode));
		x = y = be64_to_cpu(z);
		ir.ir_start = x;
		ir.ir_length = GFS2_INUM_QUANTUM;
		x += GFS2_INUM_QUANTUM;
		if (x < y)
			gfs2_consist_inode(m_ip);
		z = cpu_to_be64(x);
		gfs2_trans_add_bh(m_ip->i_gl, m_bh, 1);
		*(__be64 *)(m_bh->b_data + sizeof(struct gfs2_dinode)) = z;

		brelse(m_bh);
	}

	*formal_ino = ir.ir_start++;
	ir.ir_length--;

	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	gfs2_inum_range_out(&ir, bh->b_data + sizeof(struct gfs2_dinode));

out_brelse:
	brelse(bh);
out_end_trans:
	mutex_unlock(&sdp->sd_inum_mutex);
	gfs2_trans_end(sdp);
out:
	gfs2_glock_dq_uninit(&gh);
	return error;
}

static int pick_formal_ino(struct gfs2_sbd *sdp, u64 *inum)
{
	int error;

	error = pick_formal_ino_1(sdp, inum);
	if (error <= 0)
		return error;

	error = pick_formal_ino_2(sdp, inum);

	return error;
}

/**
 * create_ok - OK to create a new on-disk inode here?
 * @dip:  Directory in which dinode is to be created
 * @name:  Name of new dinode
 * @mode:
 *
 * Returns: errno
 */

static int create_ok(struct gfs2_inode *dip, const struct qstr *name,
		     unsigned int mode)
{
	int error;

	error = permission(&dip->i_inode, MAY_WRITE | MAY_EXEC, NULL);
	if (error)
		return error;

	/*  Don't create entries in an unlinked directory  */
	if (!dip->i_inode.i_nlink)
		return -EPERM;

	error = gfs2_dir_search(&dip->i_inode, name, NULL, NULL);
	switch (error) {
	case -ENOENT:
		error = 0;
		break;
	case 0:
		return -EEXIST;
	default:
		return error;
	}

	if (dip->i_di.di_entries == (u32)-1)
		return -EFBIG;
	if (S_ISDIR(mode) && dip->i_inode.i_nlink == (u32)-1)
		return -EMLINK;

	return 0;
}

static void munge_mode_uid_gid(struct gfs2_inode *dip, unsigned int *mode,
			       unsigned int *uid, unsigned int *gid)
{
	if (GFS2_SB(&dip->i_inode)->sd_args.ar_suiddir &&
	    (dip->i_inode.i_mode & S_ISUID) && dip->i_inode.i_uid) {
		if (S_ISDIR(*mode))
			*mode |= S_ISUID;
		else if (dip->i_inode.i_uid != current->fsuid)
			*mode &= ~07111;
		*uid = dip->i_inode.i_uid;
	} else
		*uid = current->fsuid;

	if (dip->i_inode.i_mode & S_ISGID) {
		if (S_ISDIR(*mode))
			*mode |= S_ISGID;
		*gid = dip->i_inode.i_gid;
	} else
		*gid = current->fsgid;
}

static int alloc_dinode(struct gfs2_inode *dip, struct gfs2_inum_host *inum,
			u64 *generation)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	int error;

	gfs2_alloc_get(dip);

	dip->i_alloc.al_requested = RES_DINODE;
	error = gfs2_inplace_reserve(dip);
	if (error)
		goto out;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_STATFS, 0);
	if (error)
		goto out_ipreserv;

	inum->no_addr = gfs2_alloc_di(dip, generation);

	gfs2_trans_end(sdp);

out_ipreserv:
	gfs2_inplace_release(dip);
out:
	gfs2_alloc_put(dip);
	return error;
}

/**
 * init_dinode - Fill in a new dinode structure
 * @dip: the directory this inode is being created in
 * @gl: The glock covering the new inode
 * @inum: the inode number
 * @mode: the file permissions
 * @uid:
 * @gid:
 *
 */

static void init_dinode(struct gfs2_inode *dip, struct gfs2_glock *gl,
			const struct gfs2_inum_host *inum, unsigned int mode,
			unsigned int uid, unsigned int gid,
			const u64 *generation, dev_t dev)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct gfs2_dinode *di;
	struct buffer_head *dibh;

	dibh = gfs2_meta_new(gl, inum->no_addr);
	gfs2_trans_add_bh(gl, dibh, 1);
	gfs2_metatype_set(dibh, GFS2_METATYPE_DI, GFS2_FORMAT_DI);
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));
	di = (struct gfs2_dinode *)dibh->b_data;

	di->di_num.no_formal_ino = cpu_to_be64(inum->no_formal_ino);
	di->di_num.no_addr = cpu_to_be64(inum->no_addr);
	di->di_mode = cpu_to_be32(mode);
	di->di_uid = cpu_to_be32(uid);
	di->di_gid = cpu_to_be32(gid);
	di->di_nlink = cpu_to_be32(0);
	di->di_size = cpu_to_be64(0);
	di->di_blocks = cpu_to_be64(1);
	di->di_atime = di->di_mtime = di->di_ctime = cpu_to_be64(get_seconds());
	di->di_major = cpu_to_be32(MAJOR(dev));
	di->di_minor = cpu_to_be32(MINOR(dev));
	di->di_goal_meta = di->di_goal_data = cpu_to_be64(inum->no_addr);
	di->di_generation = cpu_to_be64(*generation);
	di->di_flags = cpu_to_be32(0);

	if (S_ISREG(mode)) {
		if ((dip->i_di.di_flags & GFS2_DIF_INHERIT_JDATA) ||
		    gfs2_tune_get(sdp, gt_new_files_jdata))
			di->di_flags |= cpu_to_be32(GFS2_DIF_JDATA);
		if ((dip->i_di.di_flags & GFS2_DIF_INHERIT_DIRECTIO) ||
		    gfs2_tune_get(sdp, gt_new_files_directio))
			di->di_flags |= cpu_to_be32(GFS2_DIF_DIRECTIO);
	} else if (S_ISDIR(mode)) {
		di->di_flags |= cpu_to_be32(dip->i_di.di_flags &
					    GFS2_DIF_INHERIT_DIRECTIO);
		di->di_flags |= cpu_to_be32(dip->i_di.di_flags &
					    GFS2_DIF_INHERIT_JDATA);
	}

	di->__pad1 = 0;
	di->di_payload_format = cpu_to_be32(0);
	di->di_height = cpu_to_be32(0);
	di->__pad2 = 0;
	di->__pad3 = 0;
	di->di_depth = cpu_to_be16(0);
	di->di_entries = cpu_to_be32(0);
	memset(&di->__pad4, 0, sizeof(di->__pad4));
	di->di_eattr = cpu_to_be64(0);
	memset(&di->di_reserved, 0, sizeof(di->di_reserved));

	brelse(dibh);
}

static int make_dinode(struct gfs2_inode *dip, struct gfs2_glock *gl,
		       unsigned int mode, const struct gfs2_inum_host *inum,
		       const u64 *generation, dev_t dev)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	unsigned int uid, gid;
	int error;

	munge_mode_uid_gid(dip, &mode, &uid, &gid);
	gfs2_alloc_get(dip);

	error = gfs2_quota_lock(dip, uid, gid);
	if (error)
		goto out;

	error = gfs2_quota_check(dip, uid, gid);
	if (error)
		goto out_quota;

	error = gfs2_trans_begin(sdp, RES_DINODE + RES_QUOTA, 0);
	if (error)
		goto out_quota;

	init_dinode(dip, gl, inum, mode, uid, gid, generation, dev);
	gfs2_quota_change(dip, +1, uid, gid);
	gfs2_trans_end(sdp);

out_quota:
	gfs2_quota_unlock(dip);
out:
	gfs2_alloc_put(dip);
	return error;
}

static int link_dinode(struct gfs2_inode *dip, const struct qstr *name,
		       struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct gfs2_alloc *al;
	int alloc_required;
	struct buffer_head *dibh;
	int error;

	al = gfs2_alloc_get(dip);

	error = gfs2_quota_lock(dip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto fail;

	error = alloc_required = gfs2_diradd_alloc_required(&dip->i_inode, name);
	if (alloc_required < 0)
		goto fail;
	if (alloc_required) {
		error = gfs2_quota_check(dip, dip->i_inode.i_uid, dip->i_inode.i_gid);
		if (error)
			goto fail_quota_locks;

		al->al_requested = sdp->sd_max_dirres;

		error = gfs2_inplace_reserve(dip);
		if (error)
			goto fail_quota_locks;

		error = gfs2_trans_begin(sdp, sdp->sd_max_dirres +
					 al->al_rgd->rd_ri.ri_length +
					 2 * RES_DINODE +
					 RES_STATFS + RES_QUOTA, 0);
		if (error)
			goto fail_ipreserv;
	} else {
		error = gfs2_trans_begin(sdp, RES_LEAF + 2 * RES_DINODE, 0);
		if (error)
			goto fail_quota_locks;
	}

	error = gfs2_dir_add(&dip->i_inode, name, &ip->i_num, IF2DT(ip->i_inode.i_mode));
	if (error)
		goto fail_end_trans;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto fail_end_trans;
	ip->i_inode.i_nlink = 1;
	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);
	return 0;

fail_end_trans:
	gfs2_trans_end(sdp);

fail_ipreserv:
	if (dip->i_alloc.al_rgd)
		gfs2_inplace_release(dip);

fail_quota_locks:
	gfs2_quota_unlock(dip);

fail:
	gfs2_alloc_put(dip);
	return error;
}

static int gfs2_security_init(struct gfs2_inode *dip, struct gfs2_inode *ip)
{
	int err;
	size_t len;
	void *value;
	char *name;
	struct gfs2_ea_request er;

	err = security_inode_init_security(&ip->i_inode, &dip->i_inode,
					   &name, &value, &len);

	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	memset(&er, 0, sizeof(struct gfs2_ea_request));

	er.er_type = GFS2_EATYPE_SECURITY;
	er.er_name = name;
	er.er_data = value;
	er.er_name_len = strlen(name);
	er.er_data_len = len;

	err = gfs2_ea_set_i(ip, &er);

	kfree(value);
	kfree(name);

	return err;
}

/**
 * gfs2_createi - Create a new inode
 * @ghs: An array of two holders
 * @name: The name of the new file
 * @mode: the permissions on the new inode
 *
 * @ghs[0] is an initialized holder for the directory
 * @ghs[1] is the holder for the inode lock
 *
 * If the return value is not NULL, the glocks on both the directory and the new
 * file are held.  A transaction has been started and an inplace reservation
 * is held, as well.
 *
 * Returns: An inode
 */

struct inode *gfs2_createi(struct gfs2_holder *ghs, const struct qstr *name,
			   unsigned int mode, dev_t dev)
{
	struct inode *inode;
	struct gfs2_inode *dip = ghs->gh_gl->gl_object;
	struct inode *dir = &dip->i_inode;
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct gfs2_inum_host inum;
	int error;
	u64 generation;

	if (!name->len || name->len > GFS2_FNAMESIZE)
		return ERR_PTR(-ENAMETOOLONG);

	gfs2_holder_reinit(LM_ST_EXCLUSIVE, 0, ghs);
	error = gfs2_glock_nq(ghs);
	if (error)
		goto fail;

	error = create_ok(dip, name, mode);
	if (error)
		goto fail_gunlock;

	error = pick_formal_ino(sdp, &inum.no_formal_ino);
	if (error)
		goto fail_gunlock;

	error = alloc_dinode(dip, &inum, &generation);
	if (error)
		goto fail_gunlock;

	if (inum.no_addr < dip->i_num.no_addr) {
		gfs2_glock_dq(ghs);

		error = gfs2_glock_nq_num(sdp, inum.no_addr,
					  &gfs2_inode_glops, LM_ST_EXCLUSIVE,
					  GL_SKIP, ghs + 1);
		if (error) {
			return ERR_PTR(error);
		}

		gfs2_holder_reinit(LM_ST_EXCLUSIVE, 0, ghs);
		error = gfs2_glock_nq(ghs);
		if (error) {
			gfs2_glock_dq_uninit(ghs + 1);
			return ERR_PTR(error);
		}

		error = create_ok(dip, name, mode);
		if (error)
			goto fail_gunlock2;
	} else {
		error = gfs2_glock_nq_num(sdp, inum.no_addr,
					  &gfs2_inode_glops, LM_ST_EXCLUSIVE,
					  GL_SKIP, ghs + 1);
		if (error)
			goto fail_gunlock;
	}

	error = make_dinode(dip, ghs[1].gh_gl, mode, &inum, &generation, dev);
	if (error)
		goto fail_gunlock2;

	inode = gfs2_inode_lookup(dir->i_sb, &inum, IF2DT(mode));
	if (IS_ERR(inode))
		goto fail_gunlock2;

	error = gfs2_inode_refresh(GFS2_I(inode));
	if (error)
		goto fail_iput;

	error = gfs2_acl_create(dip, GFS2_I(inode));
	if (error)
		goto fail_iput;

	error = gfs2_security_init(dip, GFS2_I(inode));
	if (error)
		goto fail_iput;

	error = link_dinode(dip, name, GFS2_I(inode));
	if (error)
		goto fail_iput;

	if (!inode)
		return ERR_PTR(-ENOMEM);
	return inode;

fail_iput:
	iput(inode);
fail_gunlock2:
	gfs2_glock_dq_uninit(ghs + 1);
fail_gunlock:
	gfs2_glock_dq(ghs);
fail:
	return ERR_PTR(error);
}

/**
 * gfs2_rmdiri - Remove a directory
 * @dip: The parent directory of the directory to be removed
 * @name: The name of the directory to be removed
 * @ip: The GFS2 inode of the directory to be removed
 *
 * Assumes Glocks on dip and ip are held
 *
 * Returns: errno
 */

int gfs2_rmdiri(struct gfs2_inode *dip, const struct qstr *name,
		struct gfs2_inode *ip)
{
	struct qstr dotname;
	int error;

	if (ip->i_di.di_entries != 2) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(ip);
		return -EIO;
	}

	error = gfs2_dir_del(dip, name);
	if (error)
		return error;

	error = gfs2_change_nlink(dip, -1);
	if (error)
		return error;

	gfs2_str2qstr(&dotname, ".");
	error = gfs2_dir_del(ip, &dotname);
	if (error)
		return error;

	gfs2_str2qstr(&dotname, "..");
	error = gfs2_dir_del(ip, &dotname);
	if (error)
		return error;

	/* It looks odd, but it really should be done twice */
	error = gfs2_change_nlink(ip, -1);
	if (error)
		return error;

	error = gfs2_change_nlink(ip, -1);
	if (error)
		return error;

	return error;
}

/*
 * gfs2_unlink_ok - check to see that a inode is still in a directory
 * @dip: the directory
 * @name: the name of the file
 * @ip: the inode
 *
 * Assumes that the lock on (at least) @dip is held.
 *
 * Returns: 0 if the parent/child relationship is correct, errno if it isn't
 */

int gfs2_unlink_ok(struct gfs2_inode *dip, const struct qstr *name,
		   struct gfs2_inode *ip)
{
	struct gfs2_inum_host inum;
	unsigned int type;
	int error;

	if (IS_IMMUTABLE(&ip->i_inode) || IS_APPEND(&ip->i_inode))
		return -EPERM;

	if ((dip->i_inode.i_mode & S_ISVTX) &&
	    dip->i_inode.i_uid != current->fsuid &&
	    ip->i_inode.i_uid != current->fsuid && !capable(CAP_FOWNER))
		return -EPERM;

	if (IS_APPEND(&dip->i_inode))
		return -EPERM;

	error = permission(&dip->i_inode, MAY_WRITE | MAY_EXEC, NULL);
	if (error)
		return error;

	error = gfs2_dir_search(&dip->i_inode, name, &inum, &type);
	if (error)
		return error;

	if (!gfs2_inum_equal(&inum, &ip->i_num))
		return -ENOENT;

	if (IF2DT(ip->i_inode.i_mode) != type) {
		gfs2_consist_inode(dip);
		return -EIO;
	}

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

int gfs2_ok_to_move(struct gfs2_inode *this, struct gfs2_inode *to)
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

		tmp = gfs2_lookupi(dir, &dotdot, 1, NULL);
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
 * gfs2_readlinki - return the contents of a symlink
 * @ip: the symlink's inode
 * @buf: a pointer to the buffer to be filled
 * @len: a pointer to the length of @buf
 *
 * If @buf is too small, a piece of memory is kmalloc()ed and needs
 * to be freed by the caller.
 *
 * Returns: errno
 */

int gfs2_readlinki(struct gfs2_inode *ip, char **buf, unsigned int *len)
{
	struct gfs2_holder i_gh;
	struct buffer_head *dibh;
	unsigned int x;
	int error;

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &i_gh);
	error = gfs2_glock_nq_atime(&i_gh);
	if (error) {
		gfs2_holder_uninit(&i_gh);
		return error;
	}

	if (!ip->i_di.di_size) {
		gfs2_consist_inode(ip);
		error = -EIO;
		goto out;
	}

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto out;

	x = ip->i_di.di_size + 1;
	if (x > *len) {
		*buf = kmalloc(x, GFP_KERNEL);
		if (!*buf) {
			error = -ENOMEM;
			goto out_brelse;
		}
	}

	memcpy(*buf, dibh->b_data + sizeof(struct gfs2_dinode), x);
	*len = x;

out_brelse:
	brelse(dibh);
out:
	gfs2_glock_dq_uninit(&i_gh);
	return error;
}

/**
 * gfs2_glock_nq_atime - Acquire a hold on an inode's glock, and
 *       conditionally update the inode's atime
 * @gh: the holder to acquire
 *
 * Tests atime (access time) for gfs2_read, gfs2_readdir and gfs2_mmap
 * Update if the difference between the current time and the inode's current
 * atime is greater than an interval specified at mount.
 *
 * Returns: errno
 */

int gfs2_glock_nq_atime(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_inode *ip = gl->gl_object;
	s64 curtime, quantum = gfs2_tune_get(sdp, gt_atime_quantum);
	unsigned int state;
	int flags;
	int error;

	if (gfs2_assert_warn(sdp, gh->gh_flags & GL_ATIME) ||
	    gfs2_assert_warn(sdp, !(gh->gh_flags & GL_ASYNC)) ||
	    gfs2_assert_warn(sdp, gl->gl_ops == &gfs2_inode_glops))
		return -EINVAL;

	state = gh->gh_state;
	flags = gh->gh_flags;

	error = gfs2_glock_nq(gh);
	if (error)
		return error;

	if (test_bit(SDF_NOATIME, &sdp->sd_flags) ||
	    (sdp->sd_vfs->s_flags & MS_RDONLY))
		return 0;

	curtime = get_seconds();
	if (curtime - ip->i_inode.i_atime.tv_sec >= quantum) {
		gfs2_glock_dq(gh);
		gfs2_holder_reinit(LM_ST_EXCLUSIVE, gh->gh_flags & ~LM_FLAG_ANY,
				   gh);
		error = gfs2_glock_nq(gh);
		if (error)
			return error;

		/* Verify that atime hasn't been updated while we were
		   trying to get exclusive lock. */

		curtime = get_seconds();
		if (curtime - ip->i_inode.i_atime.tv_sec >= quantum) {
			struct buffer_head *dibh;
			struct gfs2_dinode *di;

			error = gfs2_trans_begin(sdp, RES_DINODE, 0);
			if (error == -EROFS)
				return 0;
			if (error)
				goto fail;

			error = gfs2_meta_inode_buffer(ip, &dibh);
			if (error)
				goto fail_end_trans;

			ip->i_inode.i_atime.tv_sec = curtime;

			gfs2_trans_add_bh(ip->i_gl, dibh, 1);
			di = (struct gfs2_dinode *)dibh->b_data;
			di->di_atime = cpu_to_be64(ip->i_inode.i_atime.tv_sec);
			brelse(dibh);

			gfs2_trans_end(sdp);
		}

		/* If someone else has asked for the glock,
		   unlock and let them have it. Then reacquire
		   in the original state. */
		if (gfs2_glock_is_blocking(gl)) {
			gfs2_glock_dq(gh);
			gfs2_holder_reinit(state, flags, gh);
			return gfs2_glock_nq(gh);
		}
	}

	return 0;

fail_end_trans:
	gfs2_trans_end(sdp);
fail:
	gfs2_glock_dq(gh);
	return error;
}

/**
 * glock_compare_atime - Compare two struct gfs2_glock structures for sort
 * @arg_a: the first structure
 * @arg_b: the second structure
 *
 * Returns: 1 if A > B
 *         -1 if A < B
 *          0 if A == B
 */

static int glock_compare_atime(const void *arg_a, const void *arg_b)
{
	const struct gfs2_holder *gh_a = *(const struct gfs2_holder **)arg_a;
	const struct gfs2_holder *gh_b = *(const struct gfs2_holder **)arg_b;
	const struct lm_lockname *a = &gh_a->gh_gl->gl_name;
	const struct lm_lockname *b = &gh_b->gh_gl->gl_name;

	if (a->ln_number > b->ln_number)
		return 1;
	if (a->ln_number < b->ln_number)
		return -1;
	if (gh_a->gh_state == LM_ST_SHARED && gh_b->gh_state == LM_ST_EXCLUSIVE)
		return 1;
	if (gh_a->gh_state == LM_ST_SHARED && (gh_b->gh_flags & GL_ATIME))
		return 1;

	return 0;
}

/**
 * gfs2_glock_nq_m_atime - acquire multiple glocks where one may need an
 *      atime update
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 * Returns: 0 on success (all glocks acquired),
 *          errno on failure (no glocks acquired)
 */

int gfs2_glock_nq_m_atime(unsigned int num_gh, struct gfs2_holder *ghs)
{
	struct gfs2_holder **p;
	unsigned int x;
	int error = 0;

	if (!num_gh)
		return 0;

	if (num_gh == 1) {
		ghs->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);
		if (ghs->gh_flags & GL_ATIME)
			error = gfs2_glock_nq_atime(ghs);
		else
			error = gfs2_glock_nq(ghs);
		return error;
	}

	p = kcalloc(num_gh, sizeof(struct gfs2_holder *), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	for (x = 0; x < num_gh; x++)
		p[x] = &ghs[x];

	sort(p, num_gh, sizeof(struct gfs2_holder *), glock_compare_atime,NULL);

	for (x = 0; x < num_gh; x++) {
		p[x]->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);

		if (p[x]->gh_flags & GL_ATIME)
			error = gfs2_glock_nq_atime(p[x]);
		else
			error = gfs2_glock_nq(p[x]);

		if (error) {
			while (x--)
				gfs2_glock_dq(p[x]);
			break;
		}
	}

	kfree(p);
	return error;
}


static int
__gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr)
{
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		error = inode_setattr(&ip->i_inode, attr);
		gfs2_assert_warn(GFS2_SB(&ip->i_inode), !error);
		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(ip, dibh->b_data);
		brelse(dibh);
	}
	return error;
}

/**
 * gfs2_setattr_simple -
 * @ip:
 * @attr:
 *
 * Called with a reference on the vnode.
 *
 * Returns: errno
 */

int gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr)
{
	int error;

	if (current->journal_info)
		return __gfs2_setattr_simple(ip, attr);

	error = gfs2_trans_begin(GFS2_SB(&ip->i_inode), RES_DINODE, 0);
	if (error)
		return error;

	error = __gfs2_setattr_simple(ip, attr);
	gfs2_trans_end(GFS2_SB(&ip->i_inode));
	return error;
}

