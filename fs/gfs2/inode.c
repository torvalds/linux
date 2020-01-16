// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2011 Red Hat, Inc.  All rights reserved.
 */

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/namei.h>
#include <linux/mm.h>
#include <linux/cred.h>
#include <linux/xattr.h>
#include <linux/posix_acl.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/iomap.h>
#include <linux/security.h>
#include <linux/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "bmap.h"
#include "dir.h"
#include "xattr.h"
#include "glock.h"
#include "iyesde.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"
#include "super.h"
#include "glops.h"

static int iget_test(struct iyesde *iyesde, void *opaque)
{
	u64 yes_addr = *(u64 *)opaque;

	return GFS2_I(iyesde)->i_yes_addr == yes_addr;
}

static int iget_set(struct iyesde *iyesde, void *opaque)
{
	u64 yes_addr = *(u64 *)opaque;

	GFS2_I(iyesde)->i_yes_addr = yes_addr;
	iyesde->i_iyes = yes_addr;
	return 0;
}

static struct iyesde *gfs2_iget(struct super_block *sb, u64 yes_addr)
{
	struct iyesde *iyesde;

repeat:
	iyesde = iget5_locked(sb, yes_addr, iget_test, iget_set, &yes_addr);
	if (!iyesde)
		return iyesde;
	if (is_bad_iyesde(iyesde)) {
		iput(iyesde);
		goto repeat;
	}
	return iyesde;
}

/**
 * gfs2_set_iop - Sets iyesde operations
 * @iyesde: The iyesde with correct i_mode filled in
 *
 * GFS2 lookup code fills in vfs iyesde contents based on info obtained
 * from directory entry inside gfs2_iyesde_lookup().
 */

static void gfs2_set_iop(struct iyesde *iyesde)
{
	struct gfs2_sbd *sdp = GFS2_SB(iyesde);
	umode_t mode = iyesde->i_mode;

	if (S_ISREG(mode)) {
		iyesde->i_op = &gfs2_file_iops;
		if (gfs2_localflocks(sdp))
			iyesde->i_fop = &gfs2_file_fops_yeslock;
		else
			iyesde->i_fop = &gfs2_file_fops;
	} else if (S_ISDIR(mode)) {
		iyesde->i_op = &gfs2_dir_iops;
		if (gfs2_localflocks(sdp))
			iyesde->i_fop = &gfs2_dir_fops_yeslock;
		else
			iyesde->i_fop = &gfs2_dir_fops;
	} else if (S_ISLNK(mode)) {
		iyesde->i_op = &gfs2_symlink_iops;
	} else {
		iyesde->i_op = &gfs2_file_iops;
		init_special_iyesde(iyesde, iyesde->i_mode, iyesde->i_rdev);
	}
}

/**
 * gfs2_iyesde_lookup - Lookup an iyesde
 * @sb: The super block
 * @type: The type of the iyesde
 * @yes_addr: The iyesde number
 * @yes_formal_iyes: The iyesde generation number
 * @blktype: Requested block type (GFS2_BLKST_DINODE or GFS2_BLKST_UNLINKED;
 *           GFS2_BLKST_FREE to indicate yest to verify)
 *
 * If @type is DT_UNKNOWN, the iyesde type is fetched from disk.
 *
 * If @blktype is anything other than GFS2_BLKST_FREE (which is used as a
 * placeholder because it doesn't otherwise make sense), the on-disk block type
 * is verified to be @blktype.
 *
 * Returns: A VFS iyesde, or an error
 */

struct iyesde *gfs2_iyesde_lookup(struct super_block *sb, unsigned int type,
				u64 yes_addr, u64 yes_formal_iyes,
				unsigned int blktype)
{
	struct iyesde *iyesde;
	struct gfs2_iyesde *ip;
	struct gfs2_glock *io_gl = NULL;
	struct gfs2_holder i_gh;
	int error;

	gfs2_holder_mark_uninitialized(&i_gh);
	iyesde = gfs2_iget(sb, yes_addr);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	ip = GFS2_I(iyesde);

	if (iyesde->i_state & I_NEW) {
		struct gfs2_sbd *sdp = GFS2_SB(iyesde);
		ip->i_yes_formal_iyes = yes_formal_iyes;

		error = gfs2_glock_get(sdp, yes_addr, &gfs2_iyesde_glops, CREATE, &ip->i_gl);
		if (unlikely(error))
			goto fail;
		flush_delayed_work(&ip->i_gl->gl_work);

		error = gfs2_glock_get(sdp, yes_addr, &gfs2_iopen_glops, CREATE, &io_gl);
		if (unlikely(error))
			goto fail_put;

		if (type == DT_UNKNOWN || blktype != GFS2_BLKST_FREE) {
			/*
			 * The GL_SKIP flag indicates to skip reading the iyesde
			 * block.  We read the iyesde with gfs2_iyesde_refresh
			 * after possibly checking the block type.
			 */
			error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE,
						   GL_SKIP, &i_gh);
			if (error)
				goto fail_put;

			if (blktype != GFS2_BLKST_FREE) {
				error = gfs2_check_blk_type(sdp, yes_addr,
							    blktype);
				if (error)
					goto fail_put;
			}
		}

		glock_set_object(ip->i_gl, ip);
		set_bit(GIF_INVALID, &ip->i_flags);
		error = gfs2_glock_nq_init(io_gl, LM_ST_SHARED, GL_EXACT, &ip->i_iopen_gh);
		if (unlikely(error))
			goto fail_put;
		glock_set_object(ip->i_iopen_gh.gh_gl, ip);
		gfs2_glock_put(io_gl);
		io_gl = NULL;

		if (type == DT_UNKNOWN) {
			/* Iyesde glock must be locked already */
			error = gfs2_iyesde_refresh(GFS2_I(iyesde));
			if (error)
				goto fail_refresh;
		} else {
			iyesde->i_mode = DT2IF(type);
		}

		gfs2_set_iop(iyesde);

		/* Lowest possible timestamp; will be overwritten in gfs2_diyesde_in. */
		iyesde->i_atime.tv_sec = 1LL << (8 * sizeof(iyesde->i_atime.tv_sec) - 1);
		iyesde->i_atime.tv_nsec = 0;

		unlock_new_iyesde(iyesde);
	}

	if (gfs2_holder_initialized(&i_gh))
		gfs2_glock_dq_uninit(&i_gh);
	return iyesde;

fail_refresh:
	ip->i_iopen_gh.gh_flags |= GL_NOCACHE;
	glock_clear_object(ip->i_iopen_gh.gh_gl, ip);
	gfs2_glock_dq_uninit(&ip->i_iopen_gh);
fail_put:
	if (io_gl)
		gfs2_glock_put(io_gl);
	glock_clear_object(ip->i_gl, ip);
	if (gfs2_holder_initialized(&i_gh))
		gfs2_glock_dq_uninit(&i_gh);
fail:
	iget_failed(iyesde);
	return ERR_PTR(error);
}

struct iyesde *gfs2_lookup_by_inum(struct gfs2_sbd *sdp, u64 yes_addr,
				  u64 *yes_formal_iyes, unsigned int blktype)
{
	struct super_block *sb = sdp->sd_vfs;
	struct iyesde *iyesde;
	int error;

	iyesde = gfs2_iyesde_lookup(sb, DT_UNKNOWN, yes_addr, 0, blktype);
	if (IS_ERR(iyesde))
		return iyesde;

	/* Two extra checks for NFS only */
	if (yes_formal_iyes) {
		error = -ESTALE;
		if (GFS2_I(iyesde)->i_yes_formal_iyes != *yes_formal_iyes)
			goto fail_iput;

		error = -EIO;
		if (GFS2_I(iyesde)->i_diskflags & GFS2_DIF_SYSTEM)
			goto fail_iput;
	}
	return iyesde;

fail_iput:
	iput(iyesde);
	return ERR_PTR(error);
}


struct iyesde *gfs2_lookup_simple(struct iyesde *dip, const char *name)
{
	struct qstr qstr;
	struct iyesde *iyesde;
	gfs2_str2qstr(&qstr, name);
	iyesde = gfs2_lookupi(dip, &qstr, 1);
	/* gfs2_lookupi has inconsistent callers: vfs
	 * related routines expect NULL for yes entry found,
	 * gfs2_lookup_simple callers expect ENOENT
	 * and do yest check for NULL.
	 */
	if (iyesde == NULL)
		return ERR_PTR(-ENOENT);
	else
		return iyesde;
}


/**
 * gfs2_lookupi - Look up a filename in a directory and return its iyesde
 * @d_gh: An initialized holder for the directory glock
 * @name: The name of the iyesde to look for
 * @is_root: If 1, igyesre the caller's permissions
 * @i_gh: An uninitialized holder for the new iyesde glock
 *
 * This can be called via the VFS filldir function when NFS is doing
 * a readdirplus and the iyesde which its intending to stat isn't
 * already in cache. In this case we must yest take the directory glock
 * again, since the readdir call will have already taken that lock.
 *
 * Returns: erryes
 */

struct iyesde *gfs2_lookupi(struct iyesde *dir, const struct qstr *name,
			   int is_root)
{
	struct super_block *sb = dir->i_sb;
	struct gfs2_iyesde *dip = GFS2_I(dir);
	struct gfs2_holder d_gh;
	int error = 0;
	struct iyesde *iyesde = NULL;

	gfs2_holder_mark_uninitialized(&d_gh);
	if (!name->len || name->len > GFS2_FNAMESIZE)
		return ERR_PTR(-ENAMETOOLONG);

	if ((name->len == 1 && memcmp(name->name, ".", 1) == 0) ||
	    (name->len == 2 && memcmp(name->name, "..", 2) == 0 &&
	     dir == d_iyesde(sb->s_root))) {
		igrab(dir);
		return dir;
	}

	if (gfs2_glock_is_locked_by_me(dip->i_gl) == NULL) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
		if (error)
			return ERR_PTR(error);
	}

	if (!is_root) {
		error = gfs2_permission(dir, MAY_EXEC);
		if (error)
			goto out;
	}

	iyesde = gfs2_dir_search(dir, name, false);
	if (IS_ERR(iyesde))
		error = PTR_ERR(iyesde);
out:
	if (gfs2_holder_initialized(&d_gh))
		gfs2_glock_dq_uninit(&d_gh);
	if (error == -ENOENT)
		return NULL;
	return iyesde ? iyesde : ERR_PTR(error);
}

/**
 * create_ok - OK to create a new on-disk iyesde here?
 * @dip:  Directory in which diyesde is to be created
 * @name:  Name of new diyesde
 * @mode:
 *
 * Returns: erryes
 */

static int create_ok(struct gfs2_iyesde *dip, const struct qstr *name,
		     umode_t mode)
{
	int error;

	error = gfs2_permission(&dip->i_iyesde, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	/*  Don't create entries in an unlinked directory  */
	if (!dip->i_iyesde.i_nlink)
		return -ENOENT;

	if (dip->i_entries == (u32)-1)
		return -EFBIG;
	if (S_ISDIR(mode) && dip->i_iyesde.i_nlink == (u32)-1)
		return -EMLINK;

	return 0;
}

static void munge_mode_uid_gid(const struct gfs2_iyesde *dip,
			       struct iyesde *iyesde)
{
	if (GFS2_SB(&dip->i_iyesde)->sd_args.ar_suiddir &&
	    (dip->i_iyesde.i_mode & S_ISUID) &&
	    !uid_eq(dip->i_iyesde.i_uid, GLOBAL_ROOT_UID)) {
		if (S_ISDIR(iyesde->i_mode))
			iyesde->i_mode |= S_ISUID;
		else if (!uid_eq(dip->i_iyesde.i_uid, current_fsuid()))
			iyesde->i_mode &= ~07111;
		iyesde->i_uid = dip->i_iyesde.i_uid;
	} else
		iyesde->i_uid = current_fsuid();

	if (dip->i_iyesde.i_mode & S_ISGID) {
		if (S_ISDIR(iyesde->i_mode))
			iyesde->i_mode |= S_ISGID;
		iyesde->i_gid = dip->i_iyesde.i_gid;
	} else
		iyesde->i_gid = current_fsgid();
}

static int alloc_diyesde(struct gfs2_iyesde *ip, u32 flags, unsigned *dblocks)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_iyesde);
	struct gfs2_alloc_parms ap = { .target = *dblocks, .aflags = flags, };
	int error;

	error = gfs2_quota_lock_check(ip, &ap);
	if (error)
		goto out;

	error = gfs2_inplace_reserve(ip, &ap);
	if (error)
		goto out_quota;

	error = gfs2_trans_begin(sdp, (*dblocks * RES_RG_BIT) + RES_STATFS + RES_QUOTA, 0);
	if (error)
		goto out_ipreserv;

	error = gfs2_alloc_blocks(ip, &ip->i_yes_addr, dblocks, 1, &ip->i_generation);
	ip->i_yes_formal_iyes = ip->i_generation;
	ip->i_iyesde.i_iyes = ip->i_yes_addr;
	ip->i_goal = ip->i_yes_addr;

	gfs2_trans_end(sdp);

out_ipreserv:
	gfs2_inplace_release(ip);
out_quota:
	gfs2_quota_unlock(ip);
out:
	return error;
}

static void gfs2_init_dir(struct buffer_head *dibh,
			  const struct gfs2_iyesde *parent)
{
	struct gfs2_diyesde *di = (struct gfs2_diyesde *)dibh->b_data;
	struct gfs2_dirent *dent = (struct gfs2_dirent *)(di+1);

	gfs2_qstr2dirent(&gfs2_qdot, GFS2_DIRENT_SIZE(gfs2_qdot.len), dent);
	dent->de_inum = di->di_num; /* already GFS2 endian */
	dent->de_type = cpu_to_be16(DT_DIR);

	dent = (struct gfs2_dirent *)((char*)dent + GFS2_DIRENT_SIZE(1));
	gfs2_qstr2dirent(&gfs2_qdotdot, dibh->b_size - GFS2_DIRENT_SIZE(1) - sizeof(struct gfs2_diyesde), dent);
	gfs2_inum_out(parent, dent);
	dent->de_type = cpu_to_be16(DT_DIR);
	
}

/**
 * gfs2_init_xattr - Initialise an xattr block for a new iyesde
 * @ip: The iyesde in question
 *
 * This sets up an empty xattr block for a new iyesde, ready to
 * take any ACLs, LSM xattrs, etc.
 */

static void gfs2_init_xattr(struct gfs2_iyesde *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_iyesde);
	struct buffer_head *bh;
	struct gfs2_ea_header *ea;

	bh = gfs2_meta_new(ip->i_gl, ip->i_eattr);
	gfs2_trans_add_meta(ip->i_gl, bh);
	gfs2_metatype_set(bh, GFS2_METATYPE_EA, GFS2_FORMAT_EA);
	gfs2_buffer_clear_tail(bh, sizeof(struct gfs2_meta_header));

	ea = GFS2_EA_BH2FIRST(bh);
	ea->ea_rec_len = cpu_to_be32(sdp->sd_jbsize);
	ea->ea_type = GFS2_EATYPE_UNUSED;
	ea->ea_flags = GFS2_EAFLAG_LAST;

	brelse(bh);
}

/**
 * init_diyesde - Fill in a new diyesde structure
 * @dip: The directory this iyesde is being created in
 * @ip: The iyesde
 * @symname: The symlink destination (if a symlink)
 * @bhp: The buffer head (returned to caller)
 *
 */

static void init_diyesde(struct gfs2_iyesde *dip, struct gfs2_iyesde *ip,
			const char *symname)
{
	struct gfs2_diyesde *di;
	struct buffer_head *dibh;

	dibh = gfs2_meta_new(ip->i_gl, ip->i_yes_addr);
	gfs2_trans_add_meta(ip->i_gl, dibh);
	di = (struct gfs2_diyesde *)dibh->b_data;
	gfs2_diyesde_out(ip, di);

	di->di_major = cpu_to_be32(MAJOR(ip->i_iyesde.i_rdev));
	di->di_miyesr = cpu_to_be32(MINOR(ip->i_iyesde.i_rdev));
	di->__pad1 = 0;
	di->__pad2 = 0;
	di->__pad3 = 0;
	memset(&di->__pad4, 0, sizeof(di->__pad4));
	memset(&di->di_reserved, 0, sizeof(di->di_reserved));
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_diyesde));

	switch(ip->i_iyesde.i_mode & S_IFMT) {
	case S_IFDIR:
		gfs2_init_dir(dibh, dip);
		break;
	case S_IFLNK:
		memcpy(dibh->b_data + sizeof(struct gfs2_diyesde), symname, ip->i_iyesde.i_size);
		break;
	}

	set_buffer_uptodate(dibh);
	brelse(dibh);
}

/**
 * gfs2_trans_da_blocks - Calculate number of blocks to link iyesde
 * @dip: The directory we are linking into
 * @da: The dir add information
 * @nr_iyesdes: The number of iyesdes involved
 *
 * This calculate the number of blocks we need to reserve in a
 * transaction to link @nr_iyesdes into a directory. In most cases
 * @nr_iyesdes will be 2 (the directory plus the iyesde being linked in)
 * but in case of rename, 4 may be required.
 *
 * Returns: Number of blocks
 */

static unsigned gfs2_trans_da_blks(const struct gfs2_iyesde *dip,
				   const struct gfs2_diradd *da,
				   unsigned nr_iyesdes)
{
	return da->nr_blocks + gfs2_rg_blocks(dip, da->nr_blocks) +
	       (nr_iyesdes * RES_DINODE) + RES_QUOTA + RES_STATFS;
}

static int link_diyesde(struct gfs2_iyesde *dip, const struct qstr *name,
		       struct gfs2_iyesde *ip, struct gfs2_diradd *da)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_iyesde);
	struct gfs2_alloc_parms ap = { .target = da->nr_blocks, };
	int error;

	if (da->nr_blocks) {
		error = gfs2_quota_lock_check(dip, &ap);
		if (error)
			goto fail_quota_locks;

		error = gfs2_inplace_reserve(dip, &ap);
		if (error)
			goto fail_quota_locks;

		error = gfs2_trans_begin(sdp, gfs2_trans_da_blks(dip, da, 2), 0);
		if (error)
			goto fail_ipreserv;
	} else {
		error = gfs2_trans_begin(sdp, RES_LEAF + 2 * RES_DINODE, 0);
		if (error)
			goto fail_quota_locks;
	}

	error = gfs2_dir_add(&dip->i_iyesde, name, ip, da);

	gfs2_trans_end(sdp);
fail_ipreserv:
	gfs2_inplace_release(dip);
fail_quota_locks:
	gfs2_quota_unlock(dip);
	return error;
}

static int gfs2_initxattrs(struct iyesde *iyesde, const struct xattr *xattr_array,
		    void *fs_info)
{
	const struct xattr *xattr;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		err = __gfs2_xattr_set(iyesde, xattr->name, xattr->value,
				       xattr->value_len, 0,
				       GFS2_EATYPE_SECURITY);
		if (err < 0)
			break;
	}
	return err;
}

/**
 * gfs2_create_iyesde - Create a new iyesde
 * @dir: The parent directory
 * @dentry: The new dentry
 * @file: If yesn-NULL, the file which is being opened
 * @mode: The permissions on the new iyesde
 * @dev: For device yesdes, this is the device number
 * @symname: For symlinks, this is the link destination
 * @size: The initial size of the iyesde (igyesred for directories)
 *
 * Returns: 0 on success, or error code
 */

static int gfs2_create_iyesde(struct iyesde *dir, struct dentry *dentry,
			     struct file *file,
			     umode_t mode, dev_t dev, const char *symname,
			     unsigned int size, int excl)
{
	const struct qstr *name = &dentry->d_name;
	struct posix_acl *default_acl, *acl;
	struct gfs2_holder ghs[2];
	struct iyesde *iyesde = NULL;
	struct gfs2_iyesde *dip = GFS2_I(dir), *ip;
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_iyesde);
	struct gfs2_glock *io_gl = NULL;
	int error, free_vfs_iyesde = 1;
	u32 aflags = 0;
	unsigned blocks = 1;
	struct gfs2_diradd da = { .bh = NULL, .save_loc = 1, };

	if (!name->len || name->len > GFS2_FNAMESIZE)
		return -ENAMETOOLONG;

	error = gfs2_rsqa_alloc(dip);
	if (error)
		return error;

	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

	error = gfs2_glock_nq_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, ghs);
	if (error)
		goto fail;
	gfs2_holder_mark_uninitialized(ghs + 1);

	error = create_ok(dip, name, mode);
	if (error)
		goto fail_gunlock;

	iyesde = gfs2_dir_search(dir, &dentry->d_name, !S_ISREG(mode) || excl);
	error = PTR_ERR(iyesde);
	if (!IS_ERR(iyesde)) {
		if (S_ISDIR(iyesde->i_mode)) {
			iput(iyesde);
			iyesde = ERR_PTR(-EISDIR);
			goto fail_gunlock;
		}
		d_instantiate(dentry, iyesde);
		error = 0;
		if (file) {
			if (S_ISREG(iyesde->i_mode))
				error = finish_open(file, dentry, gfs2_open_common);
			else
				error = finish_yes_open(file, NULL);
		}
		gfs2_glock_dq_uninit(ghs);
		return error;
	} else if (error != -ENOENT) {
		goto fail_gunlock;
	}

	error = gfs2_diradd_alloc_required(dir, name, &da);
	if (error < 0)
		goto fail_gunlock;

	iyesde = new_iyesde(sdp->sd_vfs);
	error = -ENOMEM;
	if (!iyesde)
		goto fail_gunlock;

	error = posix_acl_create(dir, &mode, &default_acl, &acl);
	if (error)
		goto fail_gunlock;

	ip = GFS2_I(iyesde);
	error = gfs2_rsqa_alloc(ip);
	if (error)
		goto fail_free_acls;

	iyesde->i_mode = mode;
	set_nlink(iyesde, S_ISDIR(mode) ? 2 : 1);
	iyesde->i_rdev = dev;
	iyesde->i_size = size;
	iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
	munge_mode_uid_gid(dip, iyesde);
	check_and_update_goal(dip);
	ip->i_goal = dip->i_goal;
	ip->i_diskflags = 0;
	ip->i_eattr = 0;
	ip->i_height = 0;
	ip->i_depth = 0;
	ip->i_entries = 0;
	ip->i_yes_addr = 0; /* Temporarily zero until real addr is assigned */

	switch(mode & S_IFMT) {
	case S_IFREG:
		if ((dip->i_diskflags & GFS2_DIF_INHERIT_JDATA) ||
		    gfs2_tune_get(sdp, gt_new_files_jdata))
			ip->i_diskflags |= GFS2_DIF_JDATA;
		gfs2_set_aops(iyesde);
		break;
	case S_IFDIR:
		ip->i_diskflags |= (dip->i_diskflags & GFS2_DIF_INHERIT_JDATA);
		ip->i_diskflags |= GFS2_DIF_JDATA;
		ip->i_entries = 2;
		break;
	}

	/* Force SYSTEM flag on all files and subdirs of a SYSTEM directory */
	if (dip->i_diskflags & GFS2_DIF_SYSTEM)
		ip->i_diskflags |= GFS2_DIF_SYSTEM;

	gfs2_set_iyesde_flags(iyesde);

	if ((GFS2_I(d_iyesde(sdp->sd_root_dir)) == dip) ||
	    (dip->i_diskflags & GFS2_DIF_TOPDIR))
		aflags |= GFS2_AF_ORLOV;

	if (default_acl || acl)
		blocks++;

	error = alloc_diyesde(ip, aflags, &blocks);
	if (error)
		goto fail_free_iyesde;

	gfs2_set_iyesde_blocks(iyesde, blocks);

	error = gfs2_glock_get(sdp, ip->i_yes_addr, &gfs2_iyesde_glops, CREATE, &ip->i_gl);
	if (error)
		goto fail_free_iyesde;
	flush_delayed_work(&ip->i_gl->gl_work);
	glock_set_object(ip->i_gl, ip);

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_SKIP, ghs + 1);
	if (error)
		goto fail_free_iyesde;

	error = gfs2_trans_begin(sdp, blocks, 0);
	if (error)
		goto fail_free_iyesde;

	if (blocks > 1) {
		ip->i_eattr = ip->i_yes_addr + 1;
		gfs2_init_xattr(ip);
	}
	init_diyesde(dip, ip, symname);
	gfs2_trans_end(sdp);

	error = gfs2_glock_get(sdp, ip->i_yes_addr, &gfs2_iopen_glops, CREATE, &io_gl);
	if (error)
		goto fail_free_iyesde;

	BUG_ON(test_and_set_bit(GLF_INODE_CREATING, &io_gl->gl_flags));

	error = gfs2_glock_nq_init(io_gl, LM_ST_SHARED, GL_EXACT, &ip->i_iopen_gh);
	if (error)
		goto fail_gunlock2;

	glock_set_object(ip->i_iopen_gh.gh_gl, ip);
	gfs2_set_iop(iyesde);
	insert_iyesde_hash(iyesde);

	free_vfs_iyesde = 0; /* After this point, the iyesde is yes longer
			       considered free. Any failures need to undo
			       the gfs2 structures. */
	if (default_acl) {
		error = __gfs2_set_acl(iyesde, default_acl, ACL_TYPE_DEFAULT);
		if (error)
			goto fail_gunlock3;
		posix_acl_release(default_acl);
		default_acl = NULL;
	}
	if (acl) {
		error = __gfs2_set_acl(iyesde, acl, ACL_TYPE_ACCESS);
		if (error)
			goto fail_gunlock3;
		posix_acl_release(acl);
		acl = NULL;
	}

	error = security_iyesde_init_security(&ip->i_iyesde, &dip->i_iyesde, name,
					     &gfs2_initxattrs, NULL);
	if (error)
		goto fail_gunlock3;

	error = link_diyesde(dip, name, ip, &da);
	if (error)
		goto fail_gunlock3;

	mark_iyesde_dirty(iyesde);
	d_instantiate(dentry, iyesde);
	/* After instantiate, errors should result in evict which will destroy
	 * both iyesde and iopen glocks properly. */
	if (file) {
		file->f_mode |= FMODE_CREATED;
		error = finish_open(file, dentry, gfs2_open_common);
	}
	gfs2_glock_dq_uninit(ghs);
	gfs2_glock_dq_uninit(ghs + 1);
	clear_bit(GLF_INODE_CREATING, &io_gl->gl_flags);
	gfs2_glock_put(io_gl);
	return error;

fail_gunlock3:
	glock_clear_object(io_gl, ip);
	gfs2_glock_dq_uninit(&ip->i_iopen_gh);
fail_gunlock2:
	clear_bit(GLF_INODE_CREATING, &io_gl->gl_flags);
	gfs2_glock_put(io_gl);
fail_free_iyesde:
	if (ip->i_gl) {
		glock_clear_object(ip->i_gl, ip);
		gfs2_glock_put(ip->i_gl);
	}
	gfs2_rsqa_delete(ip, NULL);
fail_free_acls:
	posix_acl_release(default_acl);
	posix_acl_release(acl);
fail_gunlock:
	gfs2_dir_yes_add(&da);
	gfs2_glock_dq_uninit(ghs);
	if (!IS_ERR_OR_NULL(iyesde)) {
		clear_nlink(iyesde);
		if (!free_vfs_iyesde)
			mark_iyesde_dirty(iyesde);
		set_bit(free_vfs_iyesde ? GIF_FREE_VFS_INODE : GIF_ALLOC_FAILED,
			&GFS2_I(iyesde)->i_flags);
		iput(iyesde);
	}
	if (gfs2_holder_initialized(ghs + 1))
		gfs2_glock_dq_uninit(ghs + 1);
fail:
	return error;
}

/**
 * gfs2_create - Create a file
 * @dir: The directory in which to create the file
 * @dentry: The dentry of the new file
 * @mode: The mode of the new file
 *
 * Returns: erryes
 */

static int gfs2_create(struct iyesde *dir, struct dentry *dentry,
		       umode_t mode, bool excl)
{
	return gfs2_create_iyesde(dir, dentry, NULL, S_IFREG | mode, 0, NULL, 0, excl);
}

/**
 * __gfs2_lookup - Look up a filename in a directory and return its iyesde
 * @dir: The directory iyesde
 * @dentry: The dentry of the new iyesde
 * @file: File to be opened
 *
 *
 * Returns: erryes
 */

static struct dentry *__gfs2_lookup(struct iyesde *dir, struct dentry *dentry,
				    struct file *file)
{
	struct iyesde *iyesde;
	struct dentry *d;
	struct gfs2_holder gh;
	struct gfs2_glock *gl;
	int error;

	iyesde = gfs2_lookupi(dir, &dentry->d_name, 0);
	if (iyesde == NULL) {
		d_add(dentry, NULL);
		return NULL;
	}
	if (IS_ERR(iyesde))
		return ERR_CAST(iyesde);

	gl = GFS2_I(iyesde)->i_gl;
	error = gfs2_glock_nq_init(gl, LM_ST_SHARED, LM_FLAG_ANY, &gh);
	if (error) {
		iput(iyesde);
		return ERR_PTR(error);
	}

	d = d_splice_alias(iyesde, dentry);
	if (IS_ERR(d)) {
		gfs2_glock_dq_uninit(&gh);
		return d;
	}
	if (file && S_ISREG(iyesde->i_mode))
		error = finish_open(file, dentry, gfs2_open_common);

	gfs2_glock_dq_uninit(&gh);
	if (error) {
		dput(d);
		return ERR_PTR(error);
	}
	return d;
}

static struct dentry *gfs2_lookup(struct iyesde *dir, struct dentry *dentry,
				  unsigned flags)
{
	return __gfs2_lookup(dir, dentry, NULL);
}

/**
 * gfs2_link - Link to a file
 * @old_dentry: The iyesde to link
 * @dir: Add link to this directory
 * @dentry: The name of the link
 *
 * Link the iyesde in "old_dentry" into the directory "dir" with the
 * name in "dentry".
 *
 * Returns: erryes
 */

static int gfs2_link(struct dentry *old_dentry, struct iyesde *dir,
		     struct dentry *dentry)
{
	struct gfs2_iyesde *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct iyesde *iyesde = d_iyesde(old_dentry);
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_holder ghs[2];
	struct buffer_head *dibh;
	struct gfs2_diradd da = { .bh = NULL, .save_loc = 1, };
	int error;

	if (S_ISDIR(iyesde->i_mode))
		return -EPERM;

	error = gfs2_rsqa_alloc(dip);
	if (error)
		return error;

	gfs2_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, ghs);
	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, ghs + 1);

	error = gfs2_glock_nq(ghs); /* parent */
	if (error)
		goto out_parent;

	error = gfs2_glock_nq(ghs + 1); /* child */
	if (error)
		goto out_child;

	error = -ENOENT;
	if (iyesde->i_nlink == 0)
		goto out_gunlock;

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
	if (!dip->i_iyesde.i_nlink)
		goto out_gunlock;
	error = -EFBIG;
	if (dip->i_entries == (u32)-1)
		goto out_gunlock;
	error = -EPERM;
	if (IS_IMMUTABLE(iyesde) || IS_APPEND(iyesde))
		goto out_gunlock;
	error = -EINVAL;
	if (!ip->i_iyesde.i_nlink)
		goto out_gunlock;
	error = -EMLINK;
	if (ip->i_iyesde.i_nlink == (u32)-1)
		goto out_gunlock;

	error = gfs2_diradd_alloc_required(dir, &dentry->d_name, &da);
	if (error < 0)
		goto out_gunlock;

	if (da.nr_blocks) {
		struct gfs2_alloc_parms ap = { .target = da.nr_blocks, };
		error = gfs2_quota_lock_check(dip, &ap);
		if (error)
			goto out_gunlock;

		error = gfs2_inplace_reserve(dip, &ap);
		if (error)
			goto out_gunlock_q;

		error = gfs2_trans_begin(sdp, gfs2_trans_da_blks(dip, &da, 2), 0);
		if (error)
			goto out_ipres;
	} else {
		error = gfs2_trans_begin(sdp, 2 * RES_DINODE + RES_LEAF, 0);
		if (error)
			goto out_ipres;
	}

	error = gfs2_meta_iyesde_buffer(ip, &dibh);
	if (error)
		goto out_end_trans;

	error = gfs2_dir_add(dir, &dentry->d_name, ip, &da);
	if (error)
		goto out_brelse;

	gfs2_trans_add_meta(ip->i_gl, dibh);
	inc_nlink(&ip->i_iyesde);
	ip->i_iyesde.i_ctime = current_time(&ip->i_iyesde);
	ihold(iyesde);
	d_instantiate(dentry, iyesde);
	mark_iyesde_dirty(iyesde);

out_brelse:
	brelse(dibh);
out_end_trans:
	gfs2_trans_end(sdp);
out_ipres:
	if (da.nr_blocks)
		gfs2_inplace_release(dip);
out_gunlock_q:
	if (da.nr_blocks)
		gfs2_quota_unlock(dip);
out_gunlock:
	gfs2_dir_yes_add(&da);
	gfs2_glock_dq(ghs + 1);
out_child:
	gfs2_glock_dq(ghs);
out_parent:
	gfs2_holder_uninit(ghs);
	gfs2_holder_uninit(ghs + 1);
	return error;
}

/*
 * gfs2_unlink_ok - check to see that a iyesde is still in a directory
 * @dip: the directory
 * @name: the name of the file
 * @ip: the iyesde
 *
 * Assumes that the lock on (at least) @dip is held.
 *
 * Returns: 0 if the parent/child relationship is correct, erryes if it isn't
 */

static int gfs2_unlink_ok(struct gfs2_iyesde *dip, const struct qstr *name,
			  const struct gfs2_iyesde *ip)
{
	int error;

	if (IS_IMMUTABLE(&ip->i_iyesde) || IS_APPEND(&ip->i_iyesde))
		return -EPERM;

	if ((dip->i_iyesde.i_mode & S_ISVTX) &&
	    !uid_eq(dip->i_iyesde.i_uid, current_fsuid()) &&
	    !uid_eq(ip->i_iyesde.i_uid, current_fsuid()) && !capable(CAP_FOWNER))
		return -EPERM;

	if (IS_APPEND(&dip->i_iyesde))
		return -EPERM;

	error = gfs2_permission(&dip->i_iyesde, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	return gfs2_dir_check(&dip->i_iyesde, name, ip);
}

/**
 * gfs2_unlink_iyesde - Removes an iyesde from its parent dir and unlinks it
 * @dip: The parent directory
 * @name: The name of the entry in the parent directory
 * @iyesde: The iyesde to be removed
 *
 * Called with all the locks and in a transaction. This will only be
 * called for a directory after it has been checked to ensure it is empty.
 *
 * Returns: 0 on success, or an error
 */

static int gfs2_unlink_iyesde(struct gfs2_iyesde *dip,
			     const struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	int error;

	error = gfs2_dir_del(dip, dentry);
	if (error)
		return error;

	ip->i_entries = 0;
	iyesde->i_ctime = current_time(iyesde);
	if (S_ISDIR(iyesde->i_mode))
		clear_nlink(iyesde);
	else
		drop_nlink(iyesde);
	mark_iyesde_dirty(iyesde);
	if (iyesde->i_nlink == 0)
		gfs2_unlink_di(iyesde);
	return 0;
}


/**
 * gfs2_unlink - Unlink an iyesde (this does rmdir as well)
 * @dir: The iyesde of the directory containing the iyesde to unlink
 * @dentry: The file itself
 *
 * This routine uses the type of the iyesde as a flag to figure out
 * whether this is an unlink or an rmdir.
 *
 * Returns: erryes
 */

static int gfs2_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct gfs2_iyesde *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct iyesde *iyesde = d_iyesde(dentry);
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_holder ghs[3];
	struct gfs2_rgrpd *rgd;
	int error;

	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

	error = -EROFS;

	gfs2_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, ghs);
	gfs2_holder_init(ip->i_gl,  LM_ST_EXCLUSIVE, 0, ghs + 1);

	rgd = gfs2_blk2rgrpd(sdp, ip->i_yes_addr, 1);
	if (!rgd)
		goto out_iyesdes;

	gfs2_holder_init(rgd->rd_gl, LM_ST_EXCLUSIVE, 0, ghs + 2);


	error = gfs2_glock_nq(ghs); /* parent */
	if (error)
		goto out_parent;

	error = gfs2_glock_nq(ghs + 1); /* child */
	if (error)
		goto out_child;

	error = -ENOENT;
	if (iyesde->i_nlink == 0)
		goto out_rgrp;

	if (S_ISDIR(iyesde->i_mode)) {
		error = -ENOTEMPTY;
		if (ip->i_entries > 2 || iyesde->i_nlink > 2)
			goto out_rgrp;
	}

	error = gfs2_glock_nq(ghs + 2); /* rgrp */
	if (error)
		goto out_rgrp;

	error = gfs2_unlink_ok(dip, &dentry->d_name, ip);
	if (error)
		goto out_gunlock;

	error = gfs2_trans_begin(sdp, 2*RES_DINODE + 3*RES_LEAF + RES_RG_BIT, 0);
	if (error)
		goto out_gunlock;

	error = gfs2_unlink_iyesde(dip, dentry);
	gfs2_trans_end(sdp);

out_gunlock:
	gfs2_glock_dq(ghs + 2);
out_rgrp:
	gfs2_glock_dq(ghs + 1);
out_child:
	gfs2_glock_dq(ghs);
out_parent:
	gfs2_holder_uninit(ghs + 2);
out_iyesdes:
	gfs2_holder_uninit(ghs + 1);
	gfs2_holder_uninit(ghs);
	return error;
}

/**
 * gfs2_symlink - Create a symlink
 * @dir: The directory to create the symlink in
 * @dentry: The dentry to put the symlink in
 * @symname: The thing which the link points to
 *
 * Returns: erryes
 */

static int gfs2_symlink(struct iyesde *dir, struct dentry *dentry,
			const char *symname)
{
	unsigned int size;

	size = strlen(symname);
	if (size >= gfs2_max_stuffed_size(GFS2_I(dir)))
		return -ENAMETOOLONG;

	return gfs2_create_iyesde(dir, dentry, NULL, S_IFLNK | S_IRWXUGO, 0, symname, size, 0);
}

/**
 * gfs2_mkdir - Make a directory
 * @dir: The parent directory of the new one
 * @dentry: The dentry of the new directory
 * @mode: The mode of the new directory
 *
 * Returns: erryes
 */

static int gfs2_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	unsigned dsize = gfs2_max_stuffed_size(GFS2_I(dir));
	return gfs2_create_iyesde(dir, dentry, NULL, S_IFDIR | mode, 0, NULL, dsize, 0);
}

/**
 * gfs2_mkyesd - Make a special file
 * @dir: The directory in which the special file will reside
 * @dentry: The dentry of the special file
 * @mode: The mode of the special file
 * @dev: The device specification of the special file
 *
 */

static int gfs2_mkyesd(struct iyesde *dir, struct dentry *dentry, umode_t mode,
		      dev_t dev)
{
	return gfs2_create_iyesde(dir, dentry, NULL, mode, dev, NULL, 0, 0);
}

/**
 * gfs2_atomic_open - Atomically open a file
 * @dir: The directory
 * @dentry: The proposed new entry
 * @file: The proposed new struct file
 * @flags: open flags
 * @mode: File mode
 *
 * Returns: error code or 0 for success
 */

static int gfs2_atomic_open(struct iyesde *dir, struct dentry *dentry,
			    struct file *file, unsigned flags,
			    umode_t mode)
{
	struct dentry *d;
	bool excl = !!(flags & O_EXCL);

	if (!d_in_lookup(dentry))
		goto skip_lookup;

	d = __gfs2_lookup(dir, dentry, file);
	if (IS_ERR(d))
		return PTR_ERR(d);
	if (d != NULL)
		dentry = d;
	if (d_really_is_positive(dentry)) {
		if (!(file->f_mode & FMODE_OPENED))
			return finish_yes_open(file, d);
		dput(d);
		return 0;
	}

	BUG_ON(d != NULL);

skip_lookup:
	if (!(flags & O_CREAT))
		return -ENOENT;

	return gfs2_create_iyesde(dir, dentry, file, S_IFREG | mode, 0, NULL, 0, excl);
}

/*
 * gfs2_ok_to_move - check if it's ok to move a directory to ayesther directory
 * @this: move this
 * @to: to here
 *
 * Follow @to back to the root and make sure we don't encounter @this
 * Assumes we already hold the rename lock.
 *
 * Returns: erryes
 */

static int gfs2_ok_to_move(struct gfs2_iyesde *this, struct gfs2_iyesde *to)
{
	struct iyesde *dir = &to->i_iyesde;
	struct super_block *sb = dir->i_sb;
	struct iyesde *tmp;
	int error = 0;

	igrab(dir);

	for (;;) {
		if (dir == &this->i_iyesde) {
			error = -EINVAL;
			break;
		}
		if (dir == d_iyesde(sb->s_root)) {
			error = 0;
			break;
		}

		tmp = gfs2_lookupi(dir, &gfs2_qdotdot, 1);
		if (!tmp) {
			error = -ENOENT;
			break;
		}
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
 * update_moved_iyes - Update an iyesde that's being moved
 * @ip: The iyesde being moved
 * @ndip: The parent directory of the new filename
 * @dir_rename: True of ip is a directory
 *
 * Returns: erryes
 */

static int update_moved_iyes(struct gfs2_iyesde *ip, struct gfs2_iyesde *ndip,
			    int dir_rename)
{
	if (dir_rename)
		return gfs2_dir_mviyes(ip, &gfs2_qdotdot, ndip, DT_DIR);

	ip->i_iyesde.i_ctime = current_time(&ip->i_iyesde);
	mark_iyesde_dirty_sync(&ip->i_iyesde);
	return 0;
}


/**
 * gfs2_rename - Rename a file
 * @odir: Parent directory of old file name
 * @odentry: The old dentry of the file
 * @ndir: Parent directory of new file name
 * @ndentry: The new dentry of the file
 *
 * Returns: erryes
 */

static int gfs2_rename(struct iyesde *odir, struct dentry *odentry,
		       struct iyesde *ndir, struct dentry *ndentry)
{
	struct gfs2_iyesde *odip = GFS2_I(odir);
	struct gfs2_iyesde *ndip = GFS2_I(ndir);
	struct gfs2_iyesde *ip = GFS2_I(d_iyesde(odentry));
	struct gfs2_iyesde *nip = NULL;
	struct gfs2_sbd *sdp = GFS2_SB(odir);
	struct gfs2_holder ghs[4], r_gh, rd_gh;
	struct gfs2_rgrpd *nrgd;
	unsigned int num_gh;
	int dir_rename = 0;
	struct gfs2_diradd da = { .nr_blocks = 0, .save_loc = 0, };
	unsigned int x;
	int error;

	gfs2_holder_mark_uninitialized(&r_gh);
	gfs2_holder_mark_uninitialized(&rd_gh);
	if (d_really_is_positive(ndentry)) {
		nip = GFS2_I(d_iyesde(ndentry));
		if (ip == nip)
			return 0;
	}

	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

	error = gfs2_rsqa_alloc(ndip);
	if (error)
		return error;

	if (odip != ndip) {
		error = gfs2_glock_nq_init(sdp->sd_rename_gl, LM_ST_EXCLUSIVE,
					   0, &r_gh);
		if (error)
			goto out;

		if (S_ISDIR(ip->i_iyesde.i_mode)) {
			dir_rename = 1;
			/* don't move a directory into its subdir */
			error = gfs2_ok_to_move(ip, ndip);
			if (error)
				goto out_gunlock_r;
		}
	}

	num_gh = 1;
	gfs2_holder_init(odip->i_gl, LM_ST_EXCLUSIVE, GL_ASYNC, ghs);
	if (odip != ndip) {
		gfs2_holder_init(ndip->i_gl, LM_ST_EXCLUSIVE,GL_ASYNC,
				 ghs + num_gh);
		num_gh++;
	}
	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_ASYNC, ghs + num_gh);
	num_gh++;

	if (nip) {
		gfs2_holder_init(nip->i_gl, LM_ST_EXCLUSIVE, GL_ASYNC,
				 ghs + num_gh);
		num_gh++;
	}

	for (x = 0; x < num_gh; x++) {
		error = gfs2_glock_nq(ghs + x);
		if (error)
			goto out_gunlock;
	}
	error = gfs2_glock_async_wait(num_gh, ghs);
	if (error)
		goto out_gunlock;

	if (nip) {
		/* Grab the resource group glock for unlink flag twiddling.
		 * This is the case where the target diyesde already exists
		 * so we unlink before doing the rename.
		 */
		nrgd = gfs2_blk2rgrpd(sdp, nip->i_yes_addr, 1);
		if (!nrgd) {
			error = -ENOENT;
			goto out_gunlock;
		}
		error = gfs2_glock_nq_init(nrgd->rd_gl, LM_ST_EXCLUSIVE, 0,
					   &rd_gh);
		if (error)
			goto out_gunlock;
	}

	error = -ENOENT;
	if (ip->i_iyesde.i_nlink == 0)
		goto out_gunlock;

	/* Check out the old directory */

	error = gfs2_unlink_ok(odip, &odentry->d_name, ip);
	if (error)
		goto out_gunlock;

	/* Check out the new directory */

	if (nip) {
		error = gfs2_unlink_ok(ndip, &ndentry->d_name, nip);
		if (error)
			goto out_gunlock;

		if (nip->i_iyesde.i_nlink == 0) {
			error = -EAGAIN;
			goto out_gunlock;
		}

		if (S_ISDIR(nip->i_iyesde.i_mode)) {
			if (nip->i_entries < 2) {
				gfs2_consist_iyesde(nip);
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
		}

		if (odip != ndip) {
			if (!ndip->i_iyesde.i_nlink) {
				error = -ENOENT;
				goto out_gunlock;
			}
			if (ndip->i_entries == (u32)-1) {
				error = -EFBIG;
				goto out_gunlock;
			}
			if (S_ISDIR(ip->i_iyesde.i_mode) &&
			    ndip->i_iyesde.i_nlink == (u32)-1) {
				error = -EMLINK;
				goto out_gunlock;
			}
		}
	}

	/* Check out the dir to be renamed */

	if (dir_rename) {
		error = gfs2_permission(d_iyesde(odentry), MAY_WRITE);
		if (error)
			goto out_gunlock;
	}

	if (nip == NULL) {
		error = gfs2_diradd_alloc_required(ndir, &ndentry->d_name, &da);
		if (error)
			goto out_gunlock;
	}

	if (da.nr_blocks) {
		struct gfs2_alloc_parms ap = { .target = da.nr_blocks, };
		error = gfs2_quota_lock_check(ndip, &ap);
		if (error)
			goto out_gunlock;

		error = gfs2_inplace_reserve(ndip, &ap);
		if (error)
			goto out_gunlock_q;

		error = gfs2_trans_begin(sdp, gfs2_trans_da_blks(ndip, &da, 4) +
					 4 * RES_LEAF + 4, 0);
		if (error)
			goto out_ipreserv;
	} else {
		error = gfs2_trans_begin(sdp, 4 * RES_DINODE +
					 5 * RES_LEAF + 4, 0);
		if (error)
			goto out_gunlock;
	}

	/* Remove the target file, if it exists */

	if (nip)
		error = gfs2_unlink_iyesde(ndip, ndentry);

	error = update_moved_iyes(ip, ndip, dir_rename);
	if (error)
		goto out_end_trans;

	error = gfs2_dir_del(odip, odentry);
	if (error)
		goto out_end_trans;

	error = gfs2_dir_add(ndir, &ndentry->d_name, ip, &da);
	if (error)
		goto out_end_trans;

out_end_trans:
	gfs2_trans_end(sdp);
out_ipreserv:
	if (da.nr_blocks)
		gfs2_inplace_release(ndip);
out_gunlock_q:
	if (da.nr_blocks)
		gfs2_quota_unlock(ndip);
out_gunlock:
	gfs2_dir_yes_add(&da);
	if (gfs2_holder_initialized(&rd_gh))
		gfs2_glock_dq_uninit(&rd_gh);

	while (x--) {
		if (gfs2_holder_queued(ghs + x))
			gfs2_glock_dq(ghs + x);
		gfs2_holder_uninit(ghs + x);
	}
out_gunlock_r:
	if (gfs2_holder_initialized(&r_gh))
		gfs2_glock_dq_uninit(&r_gh);
out:
	return error;
}

/**
 * gfs2_exchange - exchange two files
 * @odir: Parent directory of old file name
 * @odentry: The old dentry of the file
 * @ndir: Parent directory of new file name
 * @ndentry: The new dentry of the file
 * @flags: The rename flags
 *
 * Returns: erryes
 */

static int gfs2_exchange(struct iyesde *odir, struct dentry *odentry,
			 struct iyesde *ndir, struct dentry *ndentry,
			 unsigned int flags)
{
	struct gfs2_iyesde *odip = GFS2_I(odir);
	struct gfs2_iyesde *ndip = GFS2_I(ndir);
	struct gfs2_iyesde *oip = GFS2_I(odentry->d_iyesde);
	struct gfs2_iyesde *nip = GFS2_I(ndentry->d_iyesde);
	struct gfs2_sbd *sdp = GFS2_SB(odir);
	struct gfs2_holder ghs[4], r_gh;
	unsigned int num_gh;
	unsigned int x;
	umode_t old_mode = oip->i_iyesde.i_mode;
	umode_t new_mode = nip->i_iyesde.i_mode;
	int error;

	gfs2_holder_mark_uninitialized(&r_gh);
	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

	if (odip != ndip) {
		error = gfs2_glock_nq_init(sdp->sd_rename_gl, LM_ST_EXCLUSIVE,
					   0, &r_gh);
		if (error)
			goto out;

		if (S_ISDIR(old_mode)) {
			/* don't move a directory into its subdir */
			error = gfs2_ok_to_move(oip, ndip);
			if (error)
				goto out_gunlock_r;
		}

		if (S_ISDIR(new_mode)) {
			/* don't move a directory into its subdir */
			error = gfs2_ok_to_move(nip, odip);
			if (error)
				goto out_gunlock_r;
		}
	}

	num_gh = 1;
	gfs2_holder_init(odip->i_gl, LM_ST_EXCLUSIVE, GL_ASYNC, ghs);
	if (odip != ndip) {
		gfs2_holder_init(ndip->i_gl, LM_ST_EXCLUSIVE, GL_ASYNC,
				 ghs + num_gh);
		num_gh++;
	}
	gfs2_holder_init(oip->i_gl, LM_ST_EXCLUSIVE, GL_ASYNC, ghs + num_gh);
	num_gh++;

	gfs2_holder_init(nip->i_gl, LM_ST_EXCLUSIVE, GL_ASYNC, ghs + num_gh);
	num_gh++;

	for (x = 0; x < num_gh; x++) {
		error = gfs2_glock_nq(ghs + x);
		if (error)
			goto out_gunlock;
	}

	error = gfs2_glock_async_wait(num_gh, ghs);
	if (error)
		goto out_gunlock;

	error = -ENOENT;
	if (oip->i_iyesde.i_nlink == 0 || nip->i_iyesde.i_nlink == 0)
		goto out_gunlock;

	error = gfs2_unlink_ok(odip, &odentry->d_name, oip);
	if (error)
		goto out_gunlock;
	error = gfs2_unlink_ok(ndip, &ndentry->d_name, nip);
	if (error)
		goto out_gunlock;

	if (S_ISDIR(old_mode)) {
		error = gfs2_permission(odentry->d_iyesde, MAY_WRITE);
		if (error)
			goto out_gunlock;
	}
	if (S_ISDIR(new_mode)) {
		error = gfs2_permission(ndentry->d_iyesde, MAY_WRITE);
		if (error)
			goto out_gunlock;
	}
	error = gfs2_trans_begin(sdp, 4 * RES_DINODE + 4 * RES_LEAF, 0);
	if (error)
		goto out_gunlock;

	error = update_moved_iyes(oip, ndip, S_ISDIR(old_mode));
	if (error)
		goto out_end_trans;

	error = update_moved_iyes(nip, odip, S_ISDIR(new_mode));
	if (error)
		goto out_end_trans;

	error = gfs2_dir_mviyes(ndip, &ndentry->d_name, oip,
			       IF2DT(old_mode));
	if (error)
		goto out_end_trans;

	error = gfs2_dir_mviyes(odip, &odentry->d_name, nip,
			       IF2DT(new_mode));
	if (error)
		goto out_end_trans;

	if (odip != ndip) {
		if (S_ISDIR(new_mode) && !S_ISDIR(old_mode)) {
			inc_nlink(&odip->i_iyesde);
			drop_nlink(&ndip->i_iyesde);
		} else if (S_ISDIR(old_mode) && !S_ISDIR(new_mode)) {
			inc_nlink(&ndip->i_iyesde);
			drop_nlink(&odip->i_iyesde);
		}
	}
	mark_iyesde_dirty(&ndip->i_iyesde);
	if (odip != ndip)
		mark_iyesde_dirty(&odip->i_iyesde);

out_end_trans:
	gfs2_trans_end(sdp);
out_gunlock:
	while (x--) {
		if (gfs2_holder_queued(ghs + x))
			gfs2_glock_dq(ghs + x);
		gfs2_holder_uninit(ghs + x);
	}
out_gunlock_r:
	if (gfs2_holder_initialized(&r_gh))
		gfs2_glock_dq_uninit(&r_gh);
out:
	return error;
}

static int gfs2_rename2(struct iyesde *odir, struct dentry *odentry,
			struct iyesde *ndir, struct dentry *ndentry,
			unsigned int flags)
{
	flags &= ~RENAME_NOREPLACE;

	if (flags & ~RENAME_EXCHANGE)
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return gfs2_exchange(odir, odentry, ndir, ndentry, flags);

	return gfs2_rename(odir, odentry, ndir, ndentry);
}

/**
 * gfs2_get_link - Follow a symbolic link
 * @dentry: The dentry of the link
 * @iyesde: The iyesde of the link
 * @done: destructor for return value
 *
 * This can handle symlinks of any size.
 *
 * Returns: 0 on success or error code
 */

static const char *gfs2_get_link(struct dentry *dentry,
				 struct iyesde *iyesde,
				 struct delayed_call *done)
{
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_holder i_gh;
	struct buffer_head *dibh;
	unsigned int size;
	char *buf;
	int error;

	if (!dentry)
		return ERR_PTR(-ECHILD);

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, 0, &i_gh);
	error = gfs2_glock_nq(&i_gh);
	if (error) {
		gfs2_holder_uninit(&i_gh);
		return ERR_PTR(error);
	}

	size = (unsigned int)i_size_read(&ip->i_iyesde);
	if (size == 0) {
		gfs2_consist_iyesde(ip);
		buf = ERR_PTR(-EIO);
		goto out;
	}

	error = gfs2_meta_iyesde_buffer(ip, &dibh);
	if (error) {
		buf = ERR_PTR(error);
		goto out;
	}

	buf = kzalloc(size + 1, GFP_NOFS);
	if (!buf)
		buf = ERR_PTR(-ENOMEM);
	else
		memcpy(buf, dibh->b_data + sizeof(struct gfs2_diyesde), size);
	brelse(dibh);
out:
	gfs2_glock_dq_uninit(&i_gh);
	if (!IS_ERR(buf))
		set_delayed_call(done, kfree_link, buf);
	return buf;
}

/**
 * gfs2_permission -
 * @iyesde: The iyesde
 * @mask: The mask to be tested
 * @flags: Indicates whether this is an RCU path walk or yest
 *
 * This may be called from the VFS directly, or from within GFS2 with the
 * iyesde locked, so we look to see if the glock is already locked and only
 * lock the glock if its yest already been done.
 *
 * Returns: erryes
 */

int gfs2_permission(struct iyesde *iyesde, int mask)
{
	struct gfs2_iyesde *ip;
	struct gfs2_holder i_gh;
	int error;

	gfs2_holder_mark_uninitialized(&i_gh);
	ip = GFS2_I(iyesde);
	if (gfs2_glock_is_locked_by_me(ip->i_gl) == NULL) {
		if (mask & MAY_NOT_BLOCK)
			return -ECHILD;
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
		if (error)
			return error;
	}

	if ((mask & MAY_WRITE) && IS_IMMUTABLE(iyesde))
		error = -EPERM;
	else
		error = generic_permission(iyesde, mask);
	if (gfs2_holder_initialized(&i_gh))
		gfs2_glock_dq_uninit(&i_gh);

	return error;
}

static int __gfs2_setattr_simple(struct iyesde *iyesde, struct iattr *attr)
{
	setattr_copy(iyesde, attr);
	mark_iyesde_dirty(iyesde);
	return 0;
}

/**
 * gfs2_setattr_simple -
 * @ip:
 * @attr:
 *
 * Returns: erryes
 */

int gfs2_setattr_simple(struct iyesde *iyesde, struct iattr *attr)
{
	int error;

	if (current->journal_info)
		return __gfs2_setattr_simple(iyesde, attr);

	error = gfs2_trans_begin(GFS2_SB(iyesde), RES_DINODE, 0);
	if (error)
		return error;

	error = __gfs2_setattr_simple(iyesde, attr);
	gfs2_trans_end(GFS2_SB(iyesde));
	return error;
}

static int setattr_chown(struct iyesde *iyesde, struct iattr *attr)
{
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_sbd *sdp = GFS2_SB(iyesde);
	kuid_t ouid, nuid;
	kgid_t ogid, ngid;
	int error;
	struct gfs2_alloc_parms ap;

	ouid = iyesde->i_uid;
	ogid = iyesde->i_gid;
	nuid = attr->ia_uid;
	ngid = attr->ia_gid;

	if (!(attr->ia_valid & ATTR_UID) || uid_eq(ouid, nuid))
		ouid = nuid = NO_UID_QUOTA_CHANGE;
	if (!(attr->ia_valid & ATTR_GID) || gid_eq(ogid, ngid))
		ogid = ngid = NO_GID_QUOTA_CHANGE;

	error = gfs2_rsqa_alloc(ip);
	if (error)
		goto out;

	error = gfs2_rindex_update(sdp);
	if (error)
		goto out;

	error = gfs2_quota_lock(ip, nuid, ngid);
	if (error)
		goto out;

	ap.target = gfs2_get_iyesde_blocks(&ip->i_iyesde);

	if (!uid_eq(ouid, NO_UID_QUOTA_CHANGE) ||
	    !gid_eq(ogid, NO_GID_QUOTA_CHANGE)) {
		error = gfs2_quota_check(ip, nuid, ngid, &ap);
		if (error)
			goto out_gunlock_q;
	}

	error = gfs2_trans_begin(sdp, RES_DINODE + 2 * RES_QUOTA, 0);
	if (error)
		goto out_gunlock_q;

	error = gfs2_setattr_simple(iyesde, attr);
	if (error)
		goto out_end_trans;

	if (!uid_eq(ouid, NO_UID_QUOTA_CHANGE) ||
	    !gid_eq(ogid, NO_GID_QUOTA_CHANGE)) {
		gfs2_quota_change(ip, -(s64)ap.target, ouid, ogid);
		gfs2_quota_change(ip, ap.target, nuid, ngid);
	}

out_end_trans:
	gfs2_trans_end(sdp);
out_gunlock_q:
	gfs2_quota_unlock(ip);
out:
	return error;
}

/**
 * gfs2_setattr - Change attributes on an iyesde
 * @dentry: The dentry which is changing
 * @attr: The structure describing the change
 *
 * The VFS layer wants to change one or more of an iyesdes attributes.  Write
 * that change out to disk.
 *
 * Returns: erryes
 */

static int gfs2_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_holder i_gh;
	int error;

	error = gfs2_rsqa_alloc(ip);
	if (error)
		return error;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		return error;

	error = -EPERM;
	if (IS_IMMUTABLE(iyesde) || IS_APPEND(iyesde))
		goto out;

	error = setattr_prepare(dentry, attr);
	if (error)
		goto out;

	if (attr->ia_valid & ATTR_SIZE)
		error = gfs2_setattr_size(iyesde, attr->ia_size);
	else if (attr->ia_valid & (ATTR_UID | ATTR_GID))
		error = setattr_chown(iyesde, attr);
	else {
		error = gfs2_setattr_simple(iyesde, attr);
		if (!error && attr->ia_valid & ATTR_MODE)
			error = posix_acl_chmod(iyesde, iyesde->i_mode);
	}

out:
	if (!error)
		mark_iyesde_dirty(iyesde);
	gfs2_glock_dq_uninit(&i_gh);
	return error;
}

/**
 * gfs2_getattr - Read out an iyesde's attributes
 * @path: Object to query
 * @stat: The iyesde's stats
 * @request_mask: Mask of STATX_xxx flags indicating the caller's interests
 * @flags: AT_STATX_xxx setting
 *
 * This may be called from the VFS directly, or from within GFS2 with the
 * iyesde locked, so we look to see if the glock is already locked and only
 * lock the glock if its yest already been done. Note that its the NFS
 * readdirplus operation which causes this to be called (from filldir)
 * with the glock already held.
 *
 * Returns: erryes
 */

static int gfs2_getattr(const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_holder gh;
	u32 gfsflags;
	int error;

	gfs2_holder_mark_uninitialized(&gh);
	if (gfs2_glock_is_locked_by_me(ip->i_gl) == NULL) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY, &gh);
		if (error)
			return error;
	}

	gfsflags = ip->i_diskflags;
	if (gfsflags & GFS2_DIF_APPENDONLY)
		stat->attributes |= STATX_ATTR_APPEND;
	if (gfsflags & GFS2_DIF_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;

	stat->attributes_mask |= (STATX_ATTR_APPEND |
				  STATX_ATTR_COMPRESSED |
				  STATX_ATTR_ENCRYPTED |
				  STATX_ATTR_IMMUTABLE |
				  STATX_ATTR_NODUMP);

	generic_fillattr(iyesde, stat);

	if (gfs2_holder_initialized(&gh))
		gfs2_glock_dq_uninit(&gh);

	return 0;
}

static int gfs2_fiemap(struct iyesde *iyesde, struct fiemap_extent_info *fieinfo,
		       u64 start, u64 len)
{
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_holder gh;
	int ret;

	iyesde_lock_shared(iyesde);

	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
	if (ret)
		goto out;

	ret = iomap_fiemap(iyesde, fieinfo, start, len, &gfs2_iomap_ops);

	gfs2_glock_dq_uninit(&gh);

out:
	iyesde_unlock_shared(iyesde);
	return ret;
}

loff_t gfs2_seek_data(struct file *file, loff_t offset)
{
	struct iyesde *iyesde = file->f_mapping->host;
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_holder gh;
	loff_t ret;

	iyesde_lock_shared(iyesde);
	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
	if (!ret)
		ret = iomap_seek_data(iyesde, offset, &gfs2_iomap_ops);
	gfs2_glock_dq_uninit(&gh);
	iyesde_unlock_shared(iyesde);

	if (ret < 0)
		return ret;
	return vfs_setpos(file, ret, iyesde->i_sb->s_maxbytes);
}

loff_t gfs2_seek_hole(struct file *file, loff_t offset)
{
	struct iyesde *iyesde = file->f_mapping->host;
	struct gfs2_iyesde *ip = GFS2_I(iyesde);
	struct gfs2_holder gh;
	loff_t ret;

	iyesde_lock_shared(iyesde);
	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
	if (!ret)
		ret = iomap_seek_hole(iyesde, offset, &gfs2_iomap_ops);
	gfs2_glock_dq_uninit(&gh);
	iyesde_unlock_shared(iyesde);

	if (ret < 0)
		return ret;
	return vfs_setpos(file, ret, iyesde->i_sb->s_maxbytes);
}

const struct iyesde_operations gfs2_file_iops = {
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.listxattr = gfs2_listxattr,
	.fiemap = gfs2_fiemap,
	.get_acl = gfs2_get_acl,
	.set_acl = gfs2_set_acl,
};

const struct iyesde_operations gfs2_dir_iops = {
	.create = gfs2_create,
	.lookup = gfs2_lookup,
	.link = gfs2_link,
	.unlink = gfs2_unlink,
	.symlink = gfs2_symlink,
	.mkdir = gfs2_mkdir,
	.rmdir = gfs2_unlink,
	.mkyesd = gfs2_mkyesd,
	.rename = gfs2_rename2,
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.listxattr = gfs2_listxattr,
	.fiemap = gfs2_fiemap,
	.get_acl = gfs2_get_acl,
	.set_acl = gfs2_set_acl,
	.atomic_open = gfs2_atomic_open,
};

const struct iyesde_operations gfs2_symlink_iops = {
	.get_link = gfs2_get_link,
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.listxattr = gfs2_listxattr,
	.fiemap = gfs2_fiemap,
};

