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
#include <linux/posix_acl.h>
#include <linux/sort.h>
#include <asm/semaphore.h>

#include "gfs2.h"
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
#include "unlinked.h"

/**
 * inode_attr_in - Copy attributes from the dinode into the VFS inode
 * @ip: The GFS2 inode (with embedded disk inode data)
 * @inode:  The Linux VFS inode
 *
 */

static void inode_attr_in(struct gfs2_inode *ip, struct inode *inode)
{
	inode->i_ino = ip->i_num.no_formal_ino;

	switch (ip->i_di.di_mode & S_IFMT) {
	case S_IFBLK:
	case S_IFCHR:
		inode->i_rdev = MKDEV(ip->i_di.di_major, ip->i_di.di_minor);
		break;
	default:
		inode->i_rdev = 0;
		break;
	};

	inode->i_mode = ip->i_di.di_mode;
	inode->i_nlink = ip->i_di.di_nlink;
	inode->i_uid = ip->i_di.di_uid;
	inode->i_gid = ip->i_di.di_gid;
	i_size_write(inode, ip->i_di.di_size);
	inode->i_atime.tv_sec = ip->i_di.di_atime;
	inode->i_mtime.tv_sec = ip->i_di.di_mtime;
	inode->i_ctime.tv_sec = ip->i_di.di_ctime;
	inode->i_atime.tv_nsec = 0;
	inode->i_mtime.tv_nsec = 0;
	inode->i_ctime.tv_nsec = 0;
	inode->i_blksize = PAGE_SIZE;
	inode->i_blocks = ip->i_di.di_blocks <<
		(ip->i_sbd->sd_sb.sb_bsize_shift - GFS2_BASIC_BLOCK_SHIFT);

	if (ip->i_di.di_flags & GFS2_DIF_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	else
		inode->i_flags &= ~S_IMMUTABLE;

	if (ip->i_di.di_flags & GFS2_DIF_APPENDONLY)
		inode->i_flags |= S_APPEND;
	else
		inode->i_flags &= ~S_APPEND;
}

/**
 * gfs2_inode_attr_in - Copy attributes from the dinode into the VFS inode
 * @ip: The GFS2 inode (with embedded disk inode data)
 *
 */

void gfs2_inode_attr_in(struct gfs2_inode *ip)
{
	struct inode *inode;

	inode = gfs2_ip2v_lookup(ip);
	if (inode) {
		inode_attr_in(ip, inode);
		iput(inode);
	}
}

/**
 * gfs2_inode_attr_out - Copy attributes from VFS inode into the dinode
 * @ip: The GFS2 inode
 *
 * Only copy out the attributes that we want the VFS layer
 * to be able to modify.
 */

void gfs2_inode_attr_out(struct gfs2_inode *ip)
{
	struct inode *inode = ip->i_vnode;

	gfs2_assert_withdraw(ip->i_sbd,
		(ip->i_di.di_mode & S_IFMT) == (inode->i_mode & S_IFMT));
	ip->i_di.di_mode = inode->i_mode;
	ip->i_di.di_uid = inode->i_uid;
	ip->i_di.di_gid = inode->i_gid;
	ip->i_di.di_atime = inode->i_atime.tv_sec;
	ip->i_di.di_mtime = inode->i_mtime.tv_sec;
	ip->i_di.di_ctime = inode->i_ctime.tv_sec;
}

/**
 * gfs2_ip2v_lookup - Get the struct inode for a struct gfs2_inode
 * @ip: the struct gfs2_inode to get the struct inode for
 *
 * Returns: A VFS inode, or NULL if none
 */

struct inode *gfs2_ip2v_lookup(struct gfs2_inode *ip)
{
	struct inode *inode = NULL;

	gfs2_assert_warn(ip->i_sbd, test_bit(GIF_MIN_INIT, &ip->i_flags));

	spin_lock(&ip->i_spin);
	if (ip->i_vnode)
		inode = igrab(ip->i_vnode);
	spin_unlock(&ip->i_spin);

	return inode;
}

/**
 * gfs2_ip2v - Get/Create a struct inode for a struct gfs2_inode
 * @ip: the struct gfs2_inode to get the struct inode for
 *
 * Returns: A VFS inode, or NULL if no mem
 */

struct inode *gfs2_ip2v(struct gfs2_inode *ip)
{
	struct inode *inode, *tmp;

	inode = gfs2_ip2v_lookup(ip);
	if (inode)
		return inode;

	tmp = new_inode(ip->i_sbd->sd_vfs);
	if (!tmp)
		return NULL;

	inode_attr_in(ip, tmp);

	if (S_ISREG(ip->i_di.di_mode)) {
		tmp->i_op = &gfs2_file_iops;
		tmp->i_fop = &gfs2_file_fops;
		tmp->i_mapping->a_ops = &gfs2_file_aops;
	} else if (S_ISDIR(ip->i_di.di_mode)) {
		tmp->i_op = &gfs2_dir_iops;
		tmp->i_fop = &gfs2_dir_fops;
	} else if (S_ISLNK(ip->i_di.di_mode)) {
		tmp->i_op = &gfs2_symlink_iops;
	} else {
		tmp->i_op = &gfs2_dev_iops;
		init_special_inode(tmp, tmp->i_mode, tmp->i_rdev);
	}

	set_v2ip(tmp, NULL);

	for (;;) {
		spin_lock(&ip->i_spin);
		if (!ip->i_vnode)
			break;
		inode = igrab(ip->i_vnode);
		spin_unlock(&ip->i_spin);

		if (inode) {
			iput(tmp);
			return inode;
		}
		yield();
	}

	inode = tmp;

	gfs2_inode_hold(ip);
	ip->i_vnode = inode;
	set_v2ip(inode, ip);

	spin_unlock(&ip->i_spin);

	insert_inode_hash(inode);

	return inode;
}

static int iget_test(struct inode *inode, void *opaque)
{
	struct gfs2_inode *ip = get_v2ip(inode);
	struct gfs2_inum *inum = (struct gfs2_inum *)opaque;

	if (ip && ip->i_num.no_addr == inum->no_addr)
		return 1;

	return 0;
}

struct inode *gfs2_iget(struct super_block *sb, struct gfs2_inum *inum)
{
	return ilookup5(sb, (unsigned long)inum->no_formal_ino,
			iget_test, inum);
}

void gfs2_inode_min_init(struct gfs2_inode *ip, unsigned int type)
{
	spin_lock(&ip->i_spin);
	if (!test_and_set_bit(GIF_MIN_INIT, &ip->i_flags)) {
		ip->i_di.di_nlink = 1;
		ip->i_di.di_mode = DT2IF(type);
	}
	spin_unlock(&ip->i_spin);
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

	if (gfs2_metatype_check(ip->i_sbd, dibh, GFS2_METATYPE_DI)) {
		brelse(dibh);
		return -EIO;
	}

	spin_lock(&ip->i_spin);
	gfs2_dinode_in(&ip->i_di, dibh->b_data);
	set_bit(GIF_MIN_INIT, &ip->i_flags);
	spin_unlock(&ip->i_spin);

	brelse(dibh);

	if (ip->i_num.no_addr != ip->i_di.di_num.no_addr) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		return -EIO;
	}
	if (ip->i_num.no_formal_ino != ip->i_di.di_num.no_formal_ino)
		return -ESTALE;

	ip->i_vn = ip->i_gl->gl_vn;

	return 0;
}

/**
 * inode_create - create a struct gfs2_inode
 * @i_gl: The glock covering the inode
 * @inum: The inode number
 * @io_gl: the iopen glock to acquire/hold (using holder in new gfs2_inode)
 * @io_state: the state the iopen glock should be acquired in
 * @ipp: pointer to put the returned inode in
 *
 * Returns: errno
 */

static int inode_create(struct gfs2_glock *i_gl, struct gfs2_inum *inum,
			struct gfs2_glock *io_gl, unsigned int io_state,
			struct gfs2_inode **ipp)
{
	struct gfs2_sbd *sdp = i_gl->gl_sbd;
	struct gfs2_inode *ip;
	int error = 0;

	ip = kmem_cache_alloc(gfs2_inode_cachep, GFP_KERNEL);
	if (!ip)
		return -ENOMEM;
	memset(ip, 0, sizeof(struct gfs2_inode));

	ip->i_num = *inum;

	atomic_set(&ip->i_count, 1);

	ip->i_vn = i_gl->gl_vn - 1;

	ip->i_gl = i_gl;
	ip->i_sbd = sdp;

	spin_lock_init(&ip->i_spin);
	init_rwsem(&ip->i_rw_mutex);

	ip->i_greedy = gfs2_tune_get(sdp, gt_greedy_default);

	error = gfs2_glock_nq_init(io_gl,
				   io_state, GL_LOCAL_EXCL | GL_EXACT,
				   &ip->i_iopen_gh);
	if (error)
		goto fail;
	ip->i_iopen_gh.gh_owner = NULL;

	spin_lock(&io_gl->gl_spin);
	gfs2_glock_hold(i_gl);
	set_gl2gl(io_gl, i_gl);
	spin_unlock(&io_gl->gl_spin);

	gfs2_glock_hold(i_gl);
	set_gl2ip(i_gl, ip);

	atomic_inc(&sdp->sd_inode_count);

	*ipp = ip;

	return 0;

 fail:
	gfs2_meta_cache_flush(ip);
	kmem_cache_free(gfs2_inode_cachep, ip);
	*ipp = NULL;

	return error;
}

/**
 * gfs2_inode_get - Create or get a reference on an inode
 * @i_gl: The glock covering the inode
 * @inum: The inode number
 * @create:
 * @ipp: pointer to put the returned inode in
 *
 * Returns: errno
 */

int gfs2_inode_get(struct gfs2_glock *i_gl, struct gfs2_inum *inum, int create,
		   struct gfs2_inode **ipp)
{
	struct gfs2_sbd *sdp = i_gl->gl_sbd;
	struct gfs2_glock *io_gl;
	int error = 0;

	gfs2_glmutex_lock(i_gl);

	*ipp = get_gl2ip(i_gl);
	if (*ipp) {
		error = -ESTALE;
		if ((*ipp)->i_num.no_formal_ino != inum->no_formal_ino)
			goto out;
		atomic_inc(&(*ipp)->i_count);
		error = 0;
		goto out;
	}

	if (!create)
		goto out;

	error = gfs2_glock_get(sdp, inum->no_addr, &gfs2_iopen_glops,
			       CREATE, &io_gl);
	if (!error) {
		error = inode_create(i_gl, inum, io_gl, LM_ST_SHARED, ipp);
		gfs2_glock_put(io_gl);
	}

 out:
	gfs2_glmutex_unlock(i_gl);

	return error;
}

void gfs2_inode_hold(struct gfs2_inode *ip)
{
	gfs2_assert(ip->i_sbd, atomic_read(&ip->i_count) > 0);
	atomic_inc(&ip->i_count);
}

void gfs2_inode_put(struct gfs2_inode *ip)
{
	gfs2_assert(ip->i_sbd, atomic_read(&ip->i_count) > 0);
	atomic_dec(&ip->i_count);
}

void gfs2_inode_destroy(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_glock *io_gl = ip->i_iopen_gh.gh_gl;
	struct gfs2_glock *i_gl = ip->i_gl;

	gfs2_assert_warn(sdp, !atomic_read(&ip->i_count));
	gfs2_assert(sdp, get_gl2gl(io_gl) == i_gl);

	spin_lock(&io_gl->gl_spin);
	set_gl2gl(io_gl, NULL);
	gfs2_glock_put(i_gl);
	spin_unlock(&io_gl->gl_spin);

	gfs2_glock_dq_uninit(&ip->i_iopen_gh);

	gfs2_meta_cache_flush(ip);
	kmem_cache_free(gfs2_inode_cachep, ip);

	set_gl2ip(i_gl, NULL);
	gfs2_glock_put(i_gl);

	atomic_dec(&sdp->sd_inode_count);
}

static int dinode_dealloc(struct gfs2_inode *ip, struct gfs2_unlinked *ul)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_alloc *al;
	struct gfs2_rgrpd *rgd;
	int error;

	if (ip->i_di.di_blocks != 1) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
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

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_UNLINKED +
				 RES_STATFS + RES_QUOTA, 1);
	if (error)
		goto out_rg_gunlock;

	gfs2_trans_add_gl(ip->i_gl);

	gfs2_free_di(rgd, ip);

	error = gfs2_unlinked_ondisk_rm(sdp, ul);

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
 * inode_dealloc - Deallocate all on-disk blocks for an inode (dinode)
 * @sdp: the filesystem
 * @inum: the inode number to deallocate
 * @io_gh: a holder for the iopen glock for this inode
 *
 * Returns: errno
 */

static int inode_dealloc(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul,
			 struct gfs2_holder *io_gh)
{
	struct gfs2_inode *ip;
	struct gfs2_holder i_gh;
	int error;

	error = gfs2_glock_nq_num(sdp,
				  ul->ul_ut.ut_inum.no_addr, &gfs2_inode_glops,
				  LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		return error;

	/* We reacquire the iopen lock here to avoid a race with the NFS server
	   calling gfs2_read_inode() with the inode number of a inode we're in
	   the process of deallocating.  And we can't keep our hold on the lock
	   from inode_dealloc_init() for deadlock reasons. */

	gfs2_holder_reinit(LM_ST_EXCLUSIVE, LM_FLAG_TRY, io_gh);
	error = gfs2_glock_nq(io_gh);
	switch (error) {
	case 0:
		break;
	case GLR_TRYFAILED:
		error = 1;
	default:
		goto out;
	}

	gfs2_assert_warn(sdp, !get_gl2ip(i_gh.gh_gl));
	error = inode_create(i_gh.gh_gl, &ul->ul_ut.ut_inum, io_gh->gh_gl,
			     LM_ST_EXCLUSIVE, &ip);

	gfs2_glock_dq(io_gh);

	if (error)
		goto out;

	error = gfs2_inode_refresh(ip);
	if (error)
		goto out_iput;

	if (ip->i_di.di_nlink) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		error = -EIO;
		goto out_iput;
	}

	if (S_ISDIR(ip->i_di.di_mode) &&
	    (ip->i_di.di_flags & GFS2_DIF_EXHASH)) {
		error = gfs2_dir_exhash_dealloc(ip);
		if (error)
			goto out_iput;
	}

	if (ip->i_di.di_eattr) {
		error = gfs2_ea_dealloc(ip);
		if (error)
			goto out_iput;
	}

	if (!gfs2_is_stuffed(ip)) {
		error = gfs2_file_dealloc(ip);
		if (error)
			goto out_iput;
	}

	error = dinode_dealloc(ip, ul);
	if (error)
		goto out_iput;

 out_iput:
	gfs2_glmutex_lock(i_gh.gh_gl);
	gfs2_inode_put(ip);
	gfs2_inode_destroy(ip);
	gfs2_glmutex_unlock(i_gh.gh_gl);

 out:
	gfs2_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * try_inode_dealloc - Try to deallocate an inode and all its blocks
 * @sdp: the filesystem
 *
 * Returns: 0 on success, -errno on error, 1 on busy (inode open)
 */

static int try_inode_dealloc(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	struct gfs2_holder io_gh;
	int error = 0;

	gfs2_try_toss_inode(sdp, &ul->ul_ut.ut_inum);

	error = gfs2_glock_nq_num(sdp,
				  ul->ul_ut.ut_inum.no_addr, &gfs2_iopen_glops,
				  LM_ST_EXCLUSIVE, LM_FLAG_TRY_1CB, &io_gh);
	switch (error) {
	case 0:
		break;
	case GLR_TRYFAILED:
		return 1;
	default:
		return error;
	}

	gfs2_glock_dq(&io_gh);
	error = inode_dealloc(sdp, ul, &io_gh);
	gfs2_holder_uninit(&io_gh);

	return error;
}

static int inode_dealloc_uninit(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	struct gfs2_rgrpd *rgd;
	struct gfs2_holder ri_gh, rgd_gh;
	int error;

	error = gfs2_rindex_hold(sdp, &ri_gh);
	if (error)
		return error;

	rgd = gfs2_blk2rgrpd(sdp, ul->ul_ut.ut_inum.no_addr);
	if (!rgd) {
		gfs2_consist(sdp);
		error = -EIO;
		goto out;
	}

	error = gfs2_glock_nq_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, &rgd_gh);
	if (error)
		goto out;

	error = gfs2_trans_begin(sdp,
				 RES_RG_BIT + RES_UNLINKED + RES_STATFS,
				 0);
	if (error)
		goto out_gunlock;

	gfs2_free_uninit_di(rgd, ul->ul_ut.ut_inum.no_addr);
	gfs2_unlinked_ondisk_rm(sdp, ul);

	gfs2_trans_end(sdp);

 out_gunlock:
	gfs2_glock_dq_uninit(&rgd_gh);
 out:
	gfs2_glock_dq_uninit(&ri_gh);

	return error;
}

int gfs2_inode_dealloc(struct gfs2_sbd *sdp, struct gfs2_unlinked *ul)
{
	if (ul->ul_ut.ut_flags & GFS2_UTF_UNINIT)
		return inode_dealloc_uninit(sdp, ul);
	else
		return try_inode_dealloc(sdp, ul);
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
	uint32_t nlink;
	int error;

	nlink = ip->i_di.di_nlink + diff;

	/* If we are reducing the nlink count, but the new value ends up being
	   bigger than the old one, we must have underflowed. */
	if (diff < 0 && nlink > ip->i_di.di_nlink) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		return -EIO;
	}

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		return error;

	ip->i_di.di_nlink = nlink;
	ip->i_di.di_ctime = get_seconds();

	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	return 0;
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

int gfs2_lookupi(struct gfs2_inode *dip, struct qstr *name, int is_root,
		 struct gfs2_inode **ipp)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_holder d_gh;
	struct gfs2_inum inum;
	unsigned int type;
	struct gfs2_glock *gl;
	int error;

	if (!name->len || name->len > GFS2_FNAMESIZE)
		return -ENAMETOOLONG;

	if (gfs2_filecmp(name, ".", 1) ||
	    (gfs2_filecmp(name, "..", 2) && dip == sdp->sd_root_dir)) {
		gfs2_inode_hold(dip);
		*ipp = dip;
		return 0;
	}

	error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
	if (error)
		return error;

	if (!is_root) {
		error = gfs2_repermission(dip->i_vnode, MAY_EXEC, NULL);
		if (error)
			goto out;
	}

	error = gfs2_dir_search(dip, name, &inum, &type);
	if (error)
		goto out;

	error = gfs2_glock_get(sdp, inum.no_addr, &gfs2_inode_glops,
			       CREATE, &gl);
	if (error)
		goto out;

	error = gfs2_inode_get(gl, &inum, CREATE, ipp);
	if (!error)
		gfs2_inode_min_init(*ipp, type);

	gfs2_glock_put(gl);

 out:
	gfs2_glock_dq_uninit(&d_gh);

	return error;
}

static int pick_formal_ino_1(struct gfs2_sbd *sdp, uint64_t *formal_ino)
{
	struct gfs2_inode *ip = sdp->sd_ir_inode;
	struct buffer_head *bh;
	struct gfs2_inum_range ir;
	int error;

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		return error;
	down(&sdp->sd_inum_mutex);

	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error) {
		up(&sdp->sd_inum_mutex);
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
		up(&sdp->sd_inum_mutex);
		gfs2_trans_end(sdp);
		return 0;
	}

	brelse(bh);

	up(&sdp->sd_inum_mutex);
	gfs2_trans_end(sdp);

	return 1;
}

static int pick_formal_ino_2(struct gfs2_sbd *sdp, uint64_t *formal_ino)
{
	struct gfs2_inode *ip = sdp->sd_ir_inode;
	struct gfs2_inode *m_ip = sdp->sd_inum_inode;
	struct gfs2_holder gh;
	struct buffer_head *bh;
	struct gfs2_inum_range ir;
	int error;

	error = gfs2_glock_nq_init(m_ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (error)
		return error;

	error = gfs2_trans_begin(sdp, 2 * RES_DINODE, 0);
	if (error)
		goto out;
	down(&sdp->sd_inum_mutex);

	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		goto out_end_trans;
	
	gfs2_inum_range_in(&ir, bh->b_data + sizeof(struct gfs2_dinode));

	if (!ir.ir_length) {
		struct buffer_head *m_bh;
		uint64_t x, y;

		error = gfs2_meta_inode_buffer(m_ip, &m_bh);
		if (error)
			goto out_brelse;

		x = *(uint64_t *)(m_bh->b_data + sizeof(struct gfs2_dinode));
		x = y = be64_to_cpu(x);
		ir.ir_start = x;
		ir.ir_length = GFS2_INUM_QUANTUM;
		x += GFS2_INUM_QUANTUM;
		if (x < y)
			gfs2_consist_inode(m_ip);
		x = cpu_to_be64(x);
		gfs2_trans_add_bh(m_ip->i_gl, m_bh, 1);
		*(uint64_t *)(m_bh->b_data + sizeof(struct gfs2_dinode)) = x;

		brelse(m_bh);
	}

	*formal_ino = ir.ir_start++;
	ir.ir_length--;

	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	gfs2_inum_range_out(&ir, bh->b_data + sizeof(struct gfs2_dinode));

 out_brelse:
	brelse(bh);

 out_end_trans:
	up(&sdp->sd_inum_mutex);
	gfs2_trans_end(sdp);

 out:
	gfs2_glock_dq_uninit(&gh);

	return error;
}

static int pick_formal_ino(struct gfs2_sbd *sdp, uint64_t *inum)
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

static int create_ok(struct gfs2_inode *dip, struct qstr *name,
		     unsigned int mode)
{
	int error;

	error = gfs2_repermission(dip->i_vnode, MAY_WRITE | MAY_EXEC, NULL);
	if (error)
		return error;

	/*  Don't create entries in an unlinked directory  */
	if (!dip->i_di.di_nlink)
		return -EPERM;

	error = gfs2_dir_search(dip, name, NULL, NULL);
	switch (error) {
	case -ENOENT:
		error = 0;
		break;
	case 0:
		return -EEXIST;
	default:
		return error;
	}

	if (dip->i_di.di_entries == (uint32_t)-1)
		return -EFBIG;
	if (S_ISDIR(mode) && dip->i_di.di_nlink == (uint32_t)-1)
		return -EMLINK;

	return 0;
}

static void munge_mode_uid_gid(struct gfs2_inode *dip, unsigned int *mode,
			       unsigned int *uid, unsigned int *gid)
{
	if (dip->i_sbd->sd_args.ar_suiddir &&
	    (dip->i_di.di_mode & S_ISUID) &&
	    dip->i_di.di_uid) {
		if (S_ISDIR(*mode))
			*mode |= S_ISUID;
		else if (dip->i_di.di_uid != current->fsuid)
			*mode &= ~07111;
		*uid = dip->i_di.di_uid;
	} else
		*uid = current->fsuid;

	if (dip->i_di.di_mode & S_ISGID) {
		if (S_ISDIR(*mode))
			*mode |= S_ISGID;
		*gid = dip->i_di.di_gid;
	} else
		*gid = current->fsgid;
}

static int alloc_dinode(struct gfs2_inode *dip, struct gfs2_unlinked *ul)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	int error;

	gfs2_alloc_get(dip);

	dip->i_alloc.al_requested = RES_DINODE;
	error = gfs2_inplace_reserve(dip);
	if (error)
		goto out;

	error = gfs2_trans_begin(sdp, RES_RG_BIT + RES_UNLINKED +
				 RES_STATFS, 0);
	if (error)
		goto out_ipreserv;

	ul->ul_ut.ut_inum.no_addr = gfs2_alloc_di(dip);

	ul->ul_ut.ut_flags = GFS2_UTF_UNINIT;
	error = gfs2_unlinked_ondisk_add(sdp, ul);

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
			struct gfs2_inum *inum, unsigned int mode,
			unsigned int uid, unsigned int gid)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_dinode *di;
	struct buffer_head *dibh;

	dibh = gfs2_meta_new(gl, inum->no_addr);
	gfs2_trans_add_bh(gl, dibh, 1);
	gfs2_metatype_set(dibh, GFS2_METATYPE_DI, GFS2_FORMAT_DI);
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dinode));
	di = (struct gfs2_dinode *)dibh->b_data;

	di->di_num = *inum;
	di->di_mode = cpu_to_be32(mode);
	di->di_uid = cpu_to_be32(uid);
	di->di_gid = cpu_to_be32(gid);
	di->di_nlink = cpu_to_be32(0);
	di->di_size = cpu_to_be64(0);
	di->di_blocks = cpu_to_be64(1);
	di->di_atime = di->di_mtime = di->di_ctime = cpu_to_be64(get_seconds());
	di->di_major = di->di_minor = cpu_to_be32(0);
	di->di_goal_meta = di->di_goal_data = cpu_to_be64(inum->no_addr);
	di->__pad[0] = di->__pad[1] = 0;
	di->di_flags = cpu_to_be32(0);

	if (S_ISREG(mode)) {
		if ((dip->i_di.di_flags & GFS2_DIF_INHERIT_JDATA) ||
		    gfs2_tune_get(sdp, gt_new_files_jdata))
			di->di_flags |= cpu_to_be32(GFS2_DIF_JDATA);
		if ((dip->i_di.di_flags & GFS2_DIF_INHERIT_DIRECTIO) ||
		    gfs2_tune_get(sdp, gt_new_files_directio))
			di->di_flags |= cpu_to_be32(GFS2_DIF_DIRECTIO);
	} else if (S_ISDIR(mode)) {
		di->di_flags |= cpu_to_be32(dip->i_di.di_flags & GFS2_DIF_INHERIT_DIRECTIO);
		di->di_flags |= cpu_to_be32(dip->i_di.di_flags & GFS2_DIF_INHERIT_JDATA);
	}

	di->__pad1 = 0;
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
		       unsigned int mode, struct gfs2_unlinked *ul)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
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

	error = gfs2_trans_begin(sdp, RES_DINODE + RES_UNLINKED +
				 RES_QUOTA, 0);
	if (error)
		goto out_quota;

	ul->ul_ut.ut_flags = 0;
	error = gfs2_unlinked_ondisk_munge(sdp, ul);

	init_dinode(dip, gl, &ul->ul_ut.ut_inum,
		     mode, uid, gid);

	gfs2_quota_change(dip, +1, uid, gid);

	gfs2_trans_end(sdp);

 out_quota:
	gfs2_quota_unlock(dip);

 out:
	gfs2_alloc_put(dip);

	return error;
}

static int link_dinode(struct gfs2_inode *dip, struct qstr *name,
		       struct gfs2_inode *ip, struct gfs2_unlinked *ul)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_alloc *al;
	int alloc_required;
	struct buffer_head *dibh;
	int error;

	al = gfs2_alloc_get(dip);

	error = gfs2_quota_lock(dip, NO_QUOTA_CHANGE, NO_QUOTA_CHANGE);
	if (error)
		goto fail;

	error = gfs2_diradd_alloc_required(dip, name, &alloc_required);
	if (alloc_required) {
		error = gfs2_quota_check(dip, dip->i_di.di_uid,
					 dip->i_di.di_gid);
		if (error)
			goto fail_quota_locks;

		al->al_requested = sdp->sd_max_dirres;

		error = gfs2_inplace_reserve(dip);
		if (error)
			goto fail_quota_locks;

		error = gfs2_trans_begin(sdp,
					 sdp->sd_max_dirres +
					 al->al_rgd->rd_ri.ri_length +
					 2 * RES_DINODE + RES_UNLINKED +
					 RES_STATFS + RES_QUOTA, 0);
		if (error)
			goto fail_ipreserv;
	} else {
		error = gfs2_trans_begin(sdp,
					 RES_LEAF +
					 2 * RES_DINODE +
					 RES_UNLINKED, 0);
		if (error)
			goto fail_quota_locks;
	}

	error = gfs2_dir_add(dip, name, &ip->i_num, IF2DT(ip->i_di.di_mode));
	if (error)
		goto fail_end_trans;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (error)
		goto fail_end_trans;
	ip->i_di.di_nlink = 1;
	gfs2_trans_add_bh(ip->i_gl, dibh, 1);
	gfs2_dinode_out(&ip->i_di, dibh->b_data);
	brelse(dibh);

	error = gfs2_unlinked_ondisk_rm(sdp, ul);
	if (error)
		goto fail_end_trans;

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

/**
 * gfs2_createi - Create a new inode
 * @ghs: An array of two holders
 * @name: The name of the new file
 * @mode: the permissions on the new inode
 *
 * @ghs[0] is an initialized holder for the directory
 * @ghs[1] is the holder for the inode lock
 *
 * If the return value is 0, the glocks on both the directory and the new
 * file are held.  A transaction has been started and an inplace reservation
 * is held, as well.
 *
 * Returns: errno
 */

int gfs2_createi(struct gfs2_holder *ghs, struct qstr *name, unsigned int mode)
{
	struct gfs2_inode *dip = get_gl2ip(ghs->gh_gl);
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct gfs2_unlinked *ul;
	struct gfs2_inode *ip;
	int error;

	if (!name->len || name->len > GFS2_FNAMESIZE)
		return -ENAMETOOLONG;

	error = gfs2_unlinked_get(sdp, &ul);
	if (error)
		return error;

	gfs2_holder_reinit(LM_ST_EXCLUSIVE, 0, ghs);
	error = gfs2_glock_nq(ghs);
	if (error)
		goto fail;

	error = create_ok(dip, name, mode);
	if (error)
		goto fail_gunlock;

	error = pick_formal_ino(sdp, &ul->ul_ut.ut_inum.no_formal_ino);
	if (error)
		goto fail_gunlock;

	error = alloc_dinode(dip, ul);
	if (error)
		goto fail_gunlock;

	if (ul->ul_ut.ut_inum.no_addr < dip->i_num.no_addr) {
		gfs2_glock_dq(ghs);

		error = gfs2_glock_nq_num(sdp,
					  ul->ul_ut.ut_inum.no_addr,
					  &gfs2_inode_glops,
					  LM_ST_EXCLUSIVE, GL_SKIP,
					  ghs + 1);
		if (error) {
			gfs2_unlinked_put(sdp, ul);
			return error;
		}

		gfs2_holder_reinit(LM_ST_EXCLUSIVE, 0, ghs);
		error = gfs2_glock_nq(ghs);
		if (error) {
			gfs2_glock_dq_uninit(ghs + 1);
			gfs2_unlinked_put(sdp, ul);
			return error;
		}

		error = create_ok(dip, name, mode);
		if (error)
			goto fail_gunlock2;
	} else {
		error = gfs2_glock_nq_num(sdp,
					  ul->ul_ut.ut_inum.no_addr,
					  &gfs2_inode_glops,
					  LM_ST_EXCLUSIVE, GL_SKIP,
					  ghs + 1);
		if (error)
			goto fail_gunlock;
	}

	error = make_dinode(dip, ghs[1].gh_gl, mode, ul);
	if (error)
		goto fail_gunlock2;

	error = gfs2_inode_get(ghs[1].gh_gl, &ul->ul_ut.ut_inum, CREATE, &ip);
	if (error)
		goto fail_gunlock2;

	error = gfs2_inode_refresh(ip);
	if (error)
		goto fail_iput;

	error = gfs2_acl_create(dip, ip);
	if (error)
		goto fail_iput;

	error = link_dinode(dip, name, ip, ul);
	if (error)
		goto fail_iput;

	gfs2_unlinked_put(sdp, ul);

	return 0;

 fail_iput:
	gfs2_inode_put(ip);

 fail_gunlock2:
	gfs2_glock_dq_uninit(ghs + 1);

 fail_gunlock:
	gfs2_glock_dq(ghs);

 fail:
	gfs2_unlinked_put(sdp, ul);

	return error;
}

/**
 * gfs2_unlinki - Unlink a file
 * @dip: The inode of the directory
 * @name: The name of the file to be unlinked
 * @ip: The inode of the file to be removed
 *
 * Assumes Glocks on both dip and ip are held.
 *
 * Returns: errno
 */

int gfs2_unlinki(struct gfs2_inode *dip, struct qstr *name,
		 struct gfs2_inode *ip, struct gfs2_unlinked *ul)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	int error;

	error = gfs2_dir_del(dip, name);
	if (error)
		return error;

	error = gfs2_change_nlink(ip, -1);
	if (error)
		return error;

	/* If this inode is being unlinked from the directory structure,
	   we need to mark that in the log so that it isn't lost during
	   a crash. */

	if (!ip->i_di.di_nlink) {
		ul->ul_ut.ut_inum = ip->i_num;
		error = gfs2_unlinked_ondisk_add(sdp, ul);
		if (!error)
			set_bit(GLF_STICKY, &ip->i_gl->gl_flags);
	}

	return error;
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

int gfs2_rmdiri(struct gfs2_inode *dip, struct qstr *name,
		struct gfs2_inode *ip, struct gfs2_unlinked *ul)
{
	struct gfs2_sbd *sdp = dip->i_sbd;
	struct qstr dotname;
	int error;

	if (ip->i_di.di_entries != 2) {
		if (gfs2_consist_inode(ip))
			gfs2_dinode_print(&ip->i_di);
		return -EIO;
	}

	error = gfs2_dir_del(dip, name);
	if (error)
		return error;

	error = gfs2_change_nlink(dip, -1);
	if (error)
		return error;

	dotname.len = 1;
	dotname.name = ".";
	error = gfs2_dir_del(ip, &dotname);
	if (error)
		return error;

	dotname.len = 2;
	dotname.name = "..";
	error = gfs2_dir_del(ip, &dotname);
	if (error)
		return error;

	error = gfs2_change_nlink(ip, -2);
	if (error)
		return error;

	/* This inode is being unlinked from the directory structure and
	   we need to mark that in the log so that it isn't lost during
	   a crash. */

	ul->ul_ut.ut_inum = ip->i_num;
	error = gfs2_unlinked_ondisk_add(sdp, ul);
	if (!error)
		set_bit(GLF_STICKY, &ip->i_gl->gl_flags);

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

int gfs2_unlink_ok(struct gfs2_inode *dip, struct qstr *name,
		   struct gfs2_inode *ip)
{
	struct gfs2_inum inum;
	unsigned int type;
	int error;

	if (IS_IMMUTABLE(ip->i_vnode) || IS_APPEND(ip->i_vnode))
		return -EPERM;

	if ((dip->i_di.di_mode & S_ISVTX) &&
	    dip->i_di.di_uid != current->fsuid &&
	    ip->i_di.di_uid != current->fsuid &&
	    !capable(CAP_FOWNER))
		return -EPERM;

	if (IS_APPEND(dip->i_vnode))
		return -EPERM;

	error = gfs2_repermission(dip->i_vnode, MAY_WRITE | MAY_EXEC, NULL);
	if (error)
		return error;

	error = gfs2_dir_search(dip, name, &inum, &type);
	if (error)
		return error;

	if (!gfs2_inum_equal(&inum, &ip->i_num))
		return -ENOENT;

	if (IF2DT(ip->i_di.di_mode) != type) {
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
	struct gfs2_sbd *sdp = this->i_sbd;
	struct gfs2_inode *tmp;
	struct qstr dotdot;
	int error = 0;

	memset(&dotdot, 0, sizeof(struct qstr));
	dotdot.name = "..";
	dotdot.len = 2;

	gfs2_inode_hold(to);

	for (;;) {
		if (to == this) {
			error = -EINVAL;
			break;
		}
		if (to == sdp->sd_root_dir) {
			error = 0;
			break;
		}

		error = gfs2_lookupi(to, &dotdot, 1, &tmp);
		if (error)
			break;

		gfs2_inode_put(to);
		to = tmp;
	}

	gfs2_inode_put(to);

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
	struct gfs2_inode *ip = get_gl2ip(gl);
	int64_t curtime, quantum = gfs2_tune_get(sdp, gt_atime_quantum);
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
	if (curtime - ip->i_di.di_atime >= quantum) {
		gfs2_glock_dq(gh);
		gfs2_holder_reinit(LM_ST_EXCLUSIVE,
				  gh->gh_flags & ~LM_FLAG_ANY,
				  gh);
		error = gfs2_glock_nq(gh);
		if (error)
			return error;

		/* Verify that atime hasn't been updated while we were
		   trying to get exclusive lock. */

		curtime = get_seconds();
		if (curtime - ip->i_di.di_atime >= quantum) {
			struct buffer_head *dibh;

			error = gfs2_trans_begin(sdp, RES_DINODE, 0);
			if (error == -EROFS)
				return 0;
			if (error)
				goto fail;

			error = gfs2_meta_inode_buffer(ip, &dibh);
			if (error)
				goto fail_end_trans;

			ip->i_di.di_atime = curtime;

			gfs2_trans_add_bh(ip->i_gl, dibh, 1);
			gfs2_dinode_out(&ip->i_di, dibh->b_data);
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
 *          0 if A = B
 */

static int glock_compare_atime(const void *arg_a, const void *arg_b)
{
	struct gfs2_holder *gh_a = *(struct gfs2_holder **)arg_a;
	struct gfs2_holder *gh_b = *(struct gfs2_holder **)arg_b;
	struct lm_lockname *a = &gh_a->gh_gl->gl_name;
	struct lm_lockname *b = &gh_b->gh_gl->gl_name;
	int ret = 0;

	if (a->ln_number > b->ln_number)
		ret = 1;
	else if (a->ln_number < b->ln_number)
		ret = -1;
	else {
		if (gh_a->gh_state == LM_ST_SHARED &&
		    gh_b->gh_state == LM_ST_EXCLUSIVE)
			ret = 1;
		else if (gh_a->gh_state == LM_ST_SHARED &&
			 (gh_b->gh_flags & GL_ATIME))
			ret = 1;
	}

	return ret;
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

/**
 * gfs2_try_toss_vnode - See if we can toss a vnode from memory
 * @ip: the inode
 *
 * Returns:  1 if the vnode was tossed
 */

void gfs2_try_toss_vnode(struct gfs2_inode *ip)
{
	struct inode *inode;

	inode = gfs2_ip2v_lookup(ip);
	if (!inode)
		return;

	d_prune_aliases(inode);

	if (S_ISDIR(ip->i_di.di_mode)) {
		struct list_head *head = &inode->i_dentry;
		struct dentry *d = NULL;

		spin_lock(&dcache_lock);
		if (list_empty(head))
			spin_unlock(&dcache_lock);
		else {
			d = list_entry(head->next, struct dentry, d_alias);
			dget_locked(d);
			spin_unlock(&dcache_lock);

			if (have_submounts(d))
				dput(d);
			else {
				shrink_dcache_parent(d);
				dput(d);
				d_prune_aliases(inode);
			}
		}
	}

	inode->i_nlink = 0;
	iput(inode);
}


static int
__gfs2_setattr_simple(struct gfs2_inode *ip, struct iattr *attr)
{
	struct buffer_head *dibh;
	int error;

	error = gfs2_meta_inode_buffer(ip, &dibh);
	if (!error) {
		error = inode_setattr(ip->i_vnode, attr);
		gfs2_assert_warn(ip->i_sbd, !error);
		gfs2_inode_attr_out(ip);

		gfs2_trans_add_bh(ip->i_gl, dibh, 1);
		gfs2_dinode_out(&ip->i_di, dibh->b_data);
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

	if (get_transaction)
		return __gfs2_setattr_simple(ip, attr);

	error = gfs2_trans_begin(ip->i_sbd, RES_DINODE, 0);
	if (error)
		return error;

	error = __gfs2_setattr_simple(ip, attr);

	gfs2_trans_end(ip->i_sbd);

	return error;
}

int gfs2_repermission(struct inode *inode, int mask, struct nameidata *nd)
{
	return permission(inode, mask, nd);
}

