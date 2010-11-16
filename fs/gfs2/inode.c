/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
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
#include <linux/security.h>
#include <linux/time.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "bmap.h"
#include "dir.h"
#include "xattr.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "log.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"

struct gfs2_inum_range_host {
	u64 ir_start;
	u64 ir_length;
};

static int iget_test(struct inode *inode, void *opaque)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	u64 *no_addr = opaque;

	if (ip->i_no_addr == *no_addr)
		return 1;

	return 0;
}

static int iget_set(struct inode *inode, void *opaque)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	u64 *no_addr = opaque;

	inode->i_ino = (unsigned long)*no_addr;
	ip->i_no_addr = *no_addr;
	return 0;
}

struct inode *gfs2_ilookup(struct super_block *sb, u64 no_addr)
{
	unsigned long hash = (unsigned long)no_addr;
	return ilookup5(sb, hash, iget_test, &no_addr);
}

static struct inode *gfs2_iget(struct super_block *sb, u64 no_addr)
{
	unsigned long hash = (unsigned long)no_addr;
	return iget5_locked(sb, hash, iget_test, iget_set, &no_addr);
}

/**
 * GFS2 lookup code fills in vfs inode contents based on info obtained
 * from directory entry inside gfs2_inode_lookup(). This has caused issues
 * with NFS code path since its get_dentry routine doesn't have the relevant
 * directory entry when gfs2_inode_lookup() is invoked. Part of the code
 * segment inside gfs2_inode_lookup code needs to get moved around.
 *
 * Clears I_NEW as well.
 **/

void gfs2_set_iop(struct inode *inode)
{
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	umode_t mode = inode->i_mode;

	if (S_ISREG(mode)) {
		inode->i_op = &gfs2_file_iops;
		if (gfs2_localflocks(sdp))
			inode->i_fop = &gfs2_file_fops_nolock;
		else
			inode->i_fop = &gfs2_file_fops;
	} else if (S_ISDIR(mode)) {
		inode->i_op = &gfs2_dir_iops;
		if (gfs2_localflocks(sdp))
			inode->i_fop = &gfs2_dir_fops_nolock;
		else
			inode->i_fop = &gfs2_dir_fops;
	} else if (S_ISLNK(mode)) {
		inode->i_op = &gfs2_symlink_iops;
	} else {
		inode->i_op = &gfs2_file_iops;
		init_special_inode(inode, inode->i_mode, inode->i_rdev);
	}

	unlock_new_inode(inode);
}

/**
 * gfs2_inode_lookup - Lookup an inode
 * @sb: The super block
 * @no_addr: The inode number
 * @type: The type of the inode
 *
 * Returns: A VFS inode, or an error
 */

struct inode *gfs2_inode_lookup(struct super_block *sb,
				unsigned int type,
				u64 no_addr,
				u64 no_formal_ino)
{
	struct inode *inode;
	struct gfs2_inode *ip;
	struct gfs2_glock *io_gl = NULL;
	int error;

	inode = gfs2_iget(sb, no_addr);
	ip = GFS2_I(inode);

	if (!inode)
		return ERR_PTR(-ENOBUFS);

	if (inode->i_state & I_NEW) {
		struct gfs2_sbd *sdp = GFS2_SB(inode);
		ip->i_no_formal_ino = no_formal_ino;

		error = gfs2_glock_get(sdp, no_addr, &gfs2_inode_glops, CREATE, &ip->i_gl);
		if (unlikely(error))
			goto fail;
		ip->i_gl->gl_object = ip;

		error = gfs2_glock_get(sdp, no_addr, &gfs2_iopen_glops, CREATE, &io_gl);
		if (unlikely(error))
			goto fail_put;

		set_bit(GIF_INVALID, &ip->i_flags);
		error = gfs2_glock_nq_init(io_gl, LM_ST_SHARED, GL_EXACT, &ip->i_iopen_gh);
		if (unlikely(error))
			goto fail_iopen;
		ip->i_iopen_gh.gh_gl->gl_object = ip;

		gfs2_glock_put(io_gl);
		io_gl = NULL;

		if ((type == DT_UNKNOWN) && (no_formal_ino == 0))
			goto gfs2_nfsbypass;

		inode->i_mode = DT2IF(type);

		/*
		 * We must read the inode in order to work out its type in
		 * this case. Note that this doesn't happen often as we normally
		 * know the type beforehand. This code path only occurs during
		 * unlinked inode recovery (where it is safe to do this glock,
		 * which is not true in the general case).
		 */
		if (type == DT_UNKNOWN) {
			struct gfs2_holder gh;
			error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
			if (unlikely(error))
				goto fail_glock;
			/* Inode is now uptodate */
			gfs2_glock_dq_uninit(&gh);
		}

		gfs2_set_iop(inode);
	}

gfs2_nfsbypass:
	return inode;
fail_glock:
	gfs2_glock_dq(&ip->i_iopen_gh);
fail_iopen:
	if (io_gl)
		gfs2_glock_put(io_gl);
fail_put:
	if (inode->i_state & I_NEW)
		ip->i_gl->gl_object = NULL;
	gfs2_glock_put(ip->i_gl);
fail:
	if (inode->i_state & I_NEW)
		iget_failed(inode);
	else
		iput(inode);
	return ERR_PTR(error);
}

struct inode *gfs2_lookup_by_inum(struct gfs2_sbd *sdp, u64 no_addr,
				  u64 *no_formal_ino, unsigned int blktype)
{
	struct super_block *sb = sdp->sd_vfs;
	struct gfs2_holder i_gh;
	struct inode *inode;
	int error;

	error = gfs2_glock_nq_num(sdp, no_addr, &gfs2_inode_glops,
				  LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
	if (error)
		return ERR_PTR(error);

	error = gfs2_check_blk_type(sdp, no_addr, blktype);
	if (error)
		goto fail;

	inode = gfs2_inode_lookup(sb, DT_UNKNOWN, no_addr, 0);
	if (IS_ERR(inode))
		goto fail;

	error = gfs2_inode_refresh(GFS2_I(inode));
	if (error)
		goto fail_iput;

	/* Pick up the works we bypass in gfs2_inode_lookup */
	if (inode->i_state & I_NEW) 
		gfs2_set_iop(inode);

	/* Two extra checks for NFS only */
	if (no_formal_ino) {
		error = -ESTALE;
		if (GFS2_I(inode)->i_no_formal_ino != *no_formal_ino)
			goto fail_iput;

		error = -EIO;
		if (GFS2_I(inode)->i_diskflags & GFS2_DIF_SYSTEM)
			goto fail_iput;

		error = 0;
	}

fail:
	gfs2_glock_dq_uninit(&i_gh);
	return error ? ERR_PTR(error) : inode;
fail_iput:
	iput(inode);
	goto fail;
}

static int gfs2_dinode_in(struct gfs2_inode *ip, const void *buf)
{
	const struct gfs2_dinode *str = buf;
	struct timespec atime;
	u16 height, depth;

	if (unlikely(ip->i_no_addr != be64_to_cpu(str->di_num.no_addr)))
		goto corrupt;
	ip->i_no_formal_ino = be64_to_cpu(str->di_num.no_formal_ino);
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
	i_size_write(&ip->i_inode, be64_to_cpu(str->di_size));
	gfs2_set_inode_blocks(&ip->i_inode, be64_to_cpu(str->di_blocks));
	atime.tv_sec = be64_to_cpu(str->di_atime);
	atime.tv_nsec = be32_to_cpu(str->di_atime_nsec);
	if (timespec_compare(&ip->i_inode.i_atime, &atime) < 0)
		ip->i_inode.i_atime = atime;
	ip->i_inode.i_mtime.tv_sec = be64_to_cpu(str->di_mtime);
	ip->i_inode.i_mtime.tv_nsec = be32_to_cpu(str->di_mtime_nsec);
	ip->i_inode.i_ctime.tv_sec = be64_to_cpu(str->di_ctime);
	ip->i_inode.i_ctime.tv_nsec = be32_to_cpu(str->di_ctime_nsec);

	ip->i_goal = be64_to_cpu(str->di_goal_meta);
	ip->i_generation = be64_to_cpu(str->di_generation);

	ip->i_diskflags = be32_to_cpu(str->di_flags);
	gfs2_set_inode_flags(&ip->i_inode);
	height = be16_to_cpu(str->di_height);
	if (unlikely(height > GFS2_MAX_META_HEIGHT))
		goto corrupt;
	ip->i_height = (u8)height;

	depth = be16_to_cpu(str->di_depth);
	if (unlikely(depth > GFS2_DIR_MAX_DEPTH))
		goto corrupt;
	ip->i_depth = (u8)depth;
	ip->i_entries = be32_to_cpu(str->di_entries);

	ip->i_eattr = be64_to_cpu(str->di_eattr);
	if (S_ISREG(ip->i_inode.i_mode))
		gfs2_set_aops(&ip->i_inode);

	return 0;
corrupt:
	if (gfs2_consist_inode(ip))
		gfs2_dinode_print(ip);
	return -EIO;
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
	clear_bit(GIF_INVALID, &ip->i_flags);

	return error;
}

int gfs2_dinode_dealloc(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);
	struct gfs2_alloc *al;
	struct gfs2_rgrpd *rgd;
	int error;

	if (gfs2_get_inode_blocks(&ip->i_inode) != 1) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(ip);
		return -EIO;
	}

	al = gfs2_alloc_get(ip);
	if (!al)
		return -ENOMEM;

	error = gfs2_quota_hold(ip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto out;

	error = gfs2_rindex_hold(sdp, &al->al_ri_gh);
	if (error)
		goto out_qs;

	rgd = gfs2_blk2rgrpd(sdp, ip->i_no_addr);
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

	set_bit(GLF_DIRTY, &ip->i_gl->gl_flags);
	set_bit(GLF_LFLUSH, &ip->i_gl->gl_flags);

	gfs2_free_di(rgd, ip);

	gfs2_trans_end(sdp);

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

	ip->i_inode.i_ctime = CURRENT_TIME;

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);
	mark_inode_dirty(&ip->i_inode);

	if (ip->i_inode.i_nlink == 0)
		gfs2_unlink_di(&ip->i_inode); /* mark inode unlinked */

	return error;
}

struct inode *gfs2_lookup_simple(struct inode *dip, const char *name)
{
	struct qstr qstr;
	struct inode *inode;
	gfs2_str2qstr(&qstr, name);
	inode = gfs2_lookupi(dip, &qstr, 1);
	/* gfs2_lookupi has inconsistent callers: vfs
	 * related routines expect NULL for no entry found,
	 * gfs2_lookup_simple callers expect ENOENT
	 * and do not check for NULL.
	 */
	if (inode == NULL)
		return ERR_PTR(-ENOENT);
	else
		return inode;
}


/**
 * gfs2_lookupi - Look up a filename in a directory and return its inode
 * @d_gh: An initialized holder for the directory glock
 * @name: The name of the inode to look for
 * @is_root: If 1, ignore the caller's permissions
 * @i_gh: An uninitialized holder for the new inode glock
 *
 * This can be called via the VFS filldir function when NFS is doing
 * a readdirplus and the inode which its intending to stat isn't
 * already in cache. In this case we must not take the directory glock
 * again, since the readdir call will have already taken that lock.
 *
 * Returns: errno
 */

struct inode *gfs2_lookupi(struct inode *dir, const struct qstr *name,
			   int is_root)
{
	struct super_block *sb = dir->i_sb;
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_holder d_gh;
	int error = 0;
	struct inode *inode = NULL;
	int unlock = 0;

	if (!name->len || name->len > GFS2_FNAMESIZE)
		return ERR_PTR(-ENAMETOOLONG);

	if ((name->len == 1 && memcmp(name->name, ".", 1) == 0) ||
	    (name->len == 2 && memcmp(name->name, "..", 2) == 0 &&
	     dir == sb->s_root->d_inode)) {
		igrab(dir);
		return dir;
	}

	if (gfs2_glock_is_locked_by_me(dip->i_gl) == NULL) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
		if (error)
			return ERR_PTR(error);
		unlock = 1;
	}

	if (!is_root) {
		error = gfs2_permission(dir, MAY_EXEC);
		if (error)
			goto out;
	}

	inode = gfs2_dir_search(dir, name);
	if (IS_ERR(inode))
		error = PTR_ERR(inode);
out:
	if (unlock)
		gfs2_glock_dq_uninit(&d_gh);
	if (error == -ENOENT)
		return NULL;
	return inode ? inode : ERR_PTR(error);
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

	error = gfs2_permission(&dip->i_inode, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	/*  Don't create entries in an unlinked directory  */
	if (!dip->i_inode.i_nlink)
		return -EPERM;

	error = gfs2_dir_check(&dip->i_inode, name, NULL);
	switch (error) {
	case -ENOENT:
		error = 0;
		break;
	case 0:
		return -EEXIST;
	default:
		return error;
	}

	if (dip->i_entries == (u32)-1)
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
		else if (dip->i_inode.i_uid != current_fsuid())
			*mode &= ~07111;
		*uid = dip->i_inode.i_uid;
	} else
		*uid = current_fsuid();

	if (dip->i_inode.i_mode & S_ISGID) {
		if (S_ISDIR(*mode))
			*mode |= S_ISGID;
		*gid = dip->i_inode.i_gid;
	} else
		*gid = current_fsgid();
}

static int alloc_dinode(struct gfs2_inode *dip, u64 *no_addr, u64 *generation)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	int error;

	if (gfs2_alloc_get(dip) == NULL)
		return -ENOMEM;

	dip->i_alloc->al_requested = RES_DINODE;
	error = gfs2_inplace_reserve(dip);
	if (error)
		goto out;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_STATFS, 0);
	if (error)
		goto out_ipreserv;

	error = gfs2_alloc_di(dip, no_addr, generation);

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
			const u64 *generation, dev_t dev, struct buffer_head **bhp)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct gfs2_dinode *di;
	struct buffer_head *dibh;
	struct timespec tv = CURRENT_TIME;

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
	di->di_nlink = 0;
	di->di_size = 0;
	di->di_blocks = cpu_to_be64(1);
	di->di_atime = di->di_mtime = di->di_ctime = cpu_to_be64(tv.tv_sec);
	di->di_major = cpu_to_be32(MAJOR(dev));
	di->di_minor = cpu_to_be32(MINOR(dev));
	di->di_goal_meta = di->di_goal_data = cpu_to_be64(inum->no_addr);
	di->di_generation = cpu_to_be64(*generation);
	di->di_flags = 0;

	if (S_ISREG(mode)) {
		if ((dip->i_diskflags & GFS2_DIF_INHERIT_JDATA) ||
		    gfs2_tune_get(sdp, gt_new_files_jdata))
			di->di_flags |= cpu_to_be32(GFS2_DIF_JDATA);
	} else if (S_ISDIR(mode)) {
		di->di_flags |= cpu_to_be32(dip->i_diskflags &
					    GFS2_DIF_INHERIT_JDATA);
	}

	di->__pad1 = 0;
	di->di_payload_format = cpu_to_be32(S_ISDIR(mode) ? GFS2_FORMAT_DE : 0);
	di->di_height = 0;
	di->__pad2 = 0;
	di->__pad3 = 0;
	di->di_depth = 0;
	di->di_entries = 0;
	memset(&di->__pad4, 0, sizeof(di->__pad4));
	di->di_eattr = 0;
	di->di_atime_nsec = cpu_to_be32(tv.tv_nsec);
	di->di_mtime_nsec = cpu_to_be32(tv.tv_nsec);
	di->di_ctime_nsec = cpu_to_be32(tv.tv_nsec);
	memset(&di->di_reserved, 0, sizeof(di->di_reserved));
	
	set_buffer_uptodate(dibh);

	*bhp = dibh;
}

static int make_dinode(struct gfs2_inode *dip, struct gfs2_glock *gl,
		       unsigned int mode, const struct gfs2_inum_host *inum,
		       const u64 *generation, dev_t dev, struct buffer_head **bhp)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	unsigned int uid, gid;
	int error;

	munge_mode_uid_gid(dip, &mode, &uid, &gid);
	if (!gfs2_alloc_get(dip))
		return -ENOMEM;

	error = gfs2_quota_lock(dip, uid, gid);
	if (error)
		goto out;

	error = gfs2_quota_check(dip, uid, gid);
	if (error)
		goto out_quota;

	error = gfs2_trans_begin(sdp, RES_DINODE + RES_QUOTA, 0);
	if (error)
		goto out_quota;

	init_dinode(dip, gl, inum, mode, uid, gid, generation, dev, bhp);
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
	if (!al)
		return -ENOMEM;

	error = gfs2_quota_lock(dip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto fail;

	error = alloc_required = gfs2_diradd_alloc_required(&dip->i_inode, name);
	if (alloc_required < 0)
		goto fail_quota_locks;
	if (alloc_required) {
		error = gfs2_quota_check(dip, dip->i_inode.i_uid, dip->i_inode.i_gid);
		if (error)
			goto fail_quota_locks;

		al->al_requested = sdp->sd_max_dirres;

		error = gfs2_inplace_reserve(dip);
		if (error)
			goto fail_quota_locks;

		error = gfs2_trans_begin(sdp, sdp->sd_max_dirres +
					 al->al_rgd->rd_length +
					 2 * RES_DINODE +
					 RES_STATFS + RES_QUOTA, 0);
		if (error)
			goto fail_ipreserv;
	} else {
		error = gfs2_trans_begin(sdp, RES_LEAF + 2 * RES_DINODE, 0);
		if (error)
			goto fail_quota_locks;
	}

	error = gfs2_dir_add(&dip->i_inode, name, ip, IF2DT(ip->i_inode.i_mode));
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
	if (dip->i_alloc->al_rgd)
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

	err = security_inode_init_security(&ip->i_inode, &dip->i_inode,
					   &name, &value, &len);

	if (err) {
		if (err == -EOPNOTSUPP)
			return 0;
		return err;
	}

	err = __gfs2_xattr_set(&ip->i_inode, name, value, len, 0,
			       GFS2_EATYPE_SECURITY);
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
	struct inode *inode = NULL;
	struct gfs2_inode *dip = ghs->gh_gl->gl_object;
	struct inode *dir = &dip->i_inode;
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_inode);
	struct gfs2_inum_host inum = { .no_addr = 0, .no_formal_ino = 0 };
	int error;
	u64 generation;
	struct buffer_head *bh = NULL;

	if (!name->len || name->len > GFS2_FNAMESIZE)
		return ERR_PTR(-ENAMETOOLONG);

	gfs2_holder_reinit(LM_ST_EXCLUSIVE, 0, ghs);
	error = gfs2_glock_nq(ghs);
	if (error)
		goto fail;

	error = create_ok(dip, name, mode);
	if (error)
		goto fail_gunlock;

	error = alloc_dinode(dip, &inum.no_addr, &generation);
	if (error)
		goto fail_gunlock;
	inum.no_formal_ino = generation;

	error = gfs2_glock_nq_num(sdp, inum.no_addr, &gfs2_inode_glops,
				  LM_ST_EXCLUSIVE, GL_SKIP, ghs + 1);
	if (error)
		goto fail_gunlock;

	error = make_dinode(dip, ghs[1].gh_gl, mode, &inum, &generation, dev, &bh);
	if (error)
		goto fail_gunlock2;

	inode = gfs2_inode_lookup(dir->i_sb, IF2DT(mode), inum.no_addr,
				  inum.no_formal_ino);
	if (IS_ERR(inode))
		goto fail_gunlock2;

	error = gfs2_inode_refresh(GFS2_I(inode));
	if (error)
		goto fail_gunlock2;

	error = gfs2_acl_create(dip, inode);
	if (error)
		goto fail_gunlock2;

	error = gfs2_security_init(dip, GFS2_I(inode));
	if (error)
		goto fail_gunlock2;

	error = link_dinode(dip, name, GFS2_I(inode));
	if (error)
		goto fail_gunlock2;

	if (bh)
		brelse(bh);
	return inode;

fail_gunlock2:
	gfs2_glock_dq_uninit(ghs + 1);
	if (inode && !IS_ERR(inode))
		iput(inode);
fail_gunlock:
	gfs2_glock_dq(ghs);
fail:
	if (bh)
		brelse(bh);
	return ERR_PTR(error);
}

static int __gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr)
{
	struct inode *inode = &ip->i_inode;
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	if ((attr->ia_valid & ATTR_SIZE) &&
	    attr->ia_size != i_size_read(inode)) {
		error = vmtruncate(inode, attr->ia_size);
		if (error)
			return error;
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);

	gfs2_assert_warn(GFS2_SB(inode), !error);
	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(ip, dibh->b_data);
	brelse(dibh);
	return 0;
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

void gfs2_dinode_out(const struct gfs2_inode *ip, void *buf)
{
	struct gfs2_dinode *str = buf;

	str->di_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	str->di_header.mh_type = cpu_to_be32(GFS2_METATYPE_DI);
	str->di_header.mh_format = cpu_to_be32(GFS2_FORMAT_DI);
	str->di_num.no_addr = cpu_to_be64(ip->i_no_addr);
	str->di_num.no_formal_ino = cpu_to_be64(ip->i_no_formal_ino);
	str->di_mode = cpu_to_be32(ip->i_inode.i_mode);
	str->di_uid = cpu_to_be32(ip->i_inode.i_uid);
	str->di_gid = cpu_to_be32(ip->i_inode.i_gid);
	str->di_nlink = cpu_to_be32(ip->i_inode.i_nlink);
	str->di_size = cpu_to_be64(i_size_read(&ip->i_inode));
	str->di_blocks = cpu_to_be64(gfs2_get_inode_blocks(&ip->i_inode));
	str->di_atime = cpu_to_be64(ip->i_inode.i_atime.tv_sec);
	str->di_mtime = cpu_to_be64(ip->i_inode.i_mtime.tv_sec);
	str->di_ctime = cpu_to_be64(ip->i_inode.i_ctime.tv_sec);

	str->di_goal_meta = cpu_to_be64(ip->i_goal);
	str->di_goal_data = cpu_to_be64(ip->i_goal);
	str->di_generation = cpu_to_be64(ip->i_generation);

	str->di_flags = cpu_to_be32(ip->i_diskflags);
	str->di_height = cpu_to_be16(ip->i_height);
	str->di_payload_format = cpu_to_be32(S_ISDIR(ip->i_inode.i_mode) &&
					     !(ip->i_diskflags & GFS2_DIF_EXHASH) ?
					     GFS2_FORMAT_DE : 0);
	str->di_depth = cpu_to_be16(ip->i_depth);
	str->di_entries = cpu_to_be32(ip->i_entries);

	str->di_eattr = cpu_to_be64(ip->i_eattr);
	str->di_atime_nsec = cpu_to_be32(ip->i_inode.i_atime.tv_nsec);
	str->di_mtime_nsec = cpu_to_be32(ip->i_inode.i_mtime.tv_nsec);
	str->di_ctime_nsec = cpu_to_be32(ip->i_inode.i_ctime.tv_nsec);
}

void gfs2_dinode_print(const struct gfs2_inode *ip)
{
	printk(KERN_INFO "  no_formal_ino = %llu\n",
	       (unsigned long long)ip->i_no_formal_ino);
	printk(KERN_INFO "  no_addr = %llu\n",
	       (unsigned long long)ip->i_no_addr);
	printk(KERN_INFO "  i_size = %llu\n",
	       (unsigned long long)i_size_read(&ip->i_inode));
	printk(KERN_INFO "  blocks = %llu\n",
	       (unsigned long long)gfs2_get_inode_blocks(&ip->i_inode));
	printk(KERN_INFO "  i_goal = %llu\n",
	       (unsigned long long)ip->i_goal);
	printk(KERN_INFO "  i_diskflags = 0x%.8X\n", ip->i_diskflags);
	printk(KERN_INFO "  i_height = %u\n", ip->i_height);
	printk(KERN_INFO "  i_depth = %u\n", ip->i_depth);
	printk(KERN_INFO "  i_entries = %u\n", ip->i_entries);
	printk(KERN_INFO "  i_eattr = %llu\n",
	       (unsigned long long)ip->i_eattr);
}

