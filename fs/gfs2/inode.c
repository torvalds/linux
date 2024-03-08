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
#include <linux/fiemap.h>
#include <linux/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "acl.h"
#include "bmap.h"
#include "dir.h"
#include "xattr.h"
#include "glock.h"
#include "ianalde.h"
#include "meta_io.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"
#include "super.h"
#include "glops.h"

static const struct ianalde_operations gfs2_file_iops;
static const struct ianalde_operations gfs2_dir_iops;
static const struct ianalde_operations gfs2_symlink_iops;

/**
 * gfs2_set_iop - Sets ianalde operations
 * @ianalde: The ianalde with correct i_mode filled in
 *
 * GFS2 lookup code fills in vfs ianalde contents based on info obtained
 * from directory entry inside gfs2_ianalde_lookup().
 */

static void gfs2_set_iop(struct ianalde *ianalde)
{
	struct gfs2_sbd *sdp = GFS2_SB(ianalde);
	umode_t mode = ianalde->i_mode;

	if (S_ISREG(mode)) {
		ianalde->i_op = &gfs2_file_iops;
		if (gfs2_localflocks(sdp))
			ianalde->i_fop = &gfs2_file_fops_anallock;
		else
			ianalde->i_fop = &gfs2_file_fops;
	} else if (S_ISDIR(mode)) {
		ianalde->i_op = &gfs2_dir_iops;
		if (gfs2_localflocks(sdp))
			ianalde->i_fop = &gfs2_dir_fops_anallock;
		else
			ianalde->i_fop = &gfs2_dir_fops;
	} else if (S_ISLNK(mode)) {
		ianalde->i_op = &gfs2_symlink_iops;
	} else {
		ianalde->i_op = &gfs2_file_iops;
		init_special_ianalde(ianalde, ianalde->i_mode, ianalde->i_rdev);
	}
}

static int iget_test(struct ianalde *ianalde, void *opaque)
{
	u64 anal_addr = *(u64 *)opaque;

	return GFS2_I(ianalde)->i_anal_addr == anal_addr;
}

static int iget_set(struct ianalde *ianalde, void *opaque)
{
	u64 anal_addr = *(u64 *)opaque;

	GFS2_I(ianalde)->i_anal_addr = anal_addr;
	ianalde->i_ianal = anal_addr;
	return 0;
}

/**
 * gfs2_ianalde_lookup - Lookup an ianalde
 * @sb: The super block
 * @type: The type of the ianalde
 * @anal_addr: The ianalde number
 * @anal_formal_ianal: The ianalde generation number
 * @blktype: Requested block type (GFS2_BLKST_DIANALDE or GFS2_BLKST_UNLINKED;
 *           GFS2_BLKST_FREE to indicate analt to verify)
 *
 * If @type is DT_UNKANALWN, the ianalde type is fetched from disk.
 *
 * If @blktype is anything other than GFS2_BLKST_FREE (which is used as a
 * placeholder because it doesn't otherwise make sense), the on-disk block type
 * is verified to be @blktype.
 *
 * When @anal_formal_ianal is analn-zero, this function will return ERR_PTR(-ESTALE)
 * if it detects that @anal_formal_ianal doesn't match the actual ianalde generation
 * number.  However, it doesn't always kanalw unless @type is DT_UNKANALWN.
 *
 * Returns: A VFS ianalde, or an error
 */

struct ianalde *gfs2_ianalde_lookup(struct super_block *sb, unsigned int type,
				u64 anal_addr, u64 anal_formal_ianal,
				unsigned int blktype)
{
	struct ianalde *ianalde;
	struct gfs2_ianalde *ip;
	struct gfs2_holder i_gh;
	int error;

	gfs2_holder_mark_uninitialized(&i_gh);
	ianalde = iget5_locked(sb, anal_addr, iget_test, iget_set, &anal_addr);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	ip = GFS2_I(ianalde);

	if (ianalde->i_state & I_NEW) {
		struct gfs2_sbd *sdp = GFS2_SB(ianalde);
		struct gfs2_glock *io_gl;
		int extra_flags = 0;

		error = gfs2_glock_get(sdp, anal_addr, &gfs2_ianalde_glops, CREATE,
				       &ip->i_gl);
		if (unlikely(error))
			goto fail;

		error = gfs2_glock_get(sdp, anal_addr, &gfs2_iopen_glops, CREATE,
				       &io_gl);
		if (unlikely(error))
			goto fail;

		/*
		 * The only caller that sets @blktype to GFS2_BLKST_UNLINKED is
		 * delete_work_func().  Make sure analt to cancel the delete work
		 * from within itself here.
		 */
		if (blktype == GFS2_BLKST_UNLINKED)
			extra_flags |= LM_FLAG_TRY;
		else
			gfs2_cancel_delete_work(io_gl);
		error = gfs2_glock_nq_init(io_gl, LM_ST_SHARED,
					   GL_EXACT | GL_ANALPID | extra_flags,
					   &ip->i_iopen_gh);
		gfs2_glock_put(io_gl);
		if (unlikely(error))
			goto fail;

		if (type == DT_UNKANALWN || blktype != GFS2_BLKST_FREE) {
			/*
			 * The GL_SKIP flag indicates to skip reading the ianalde
			 * block.  We read the ianalde when instantiating it
			 * after possibly checking the block type.
			 */
			error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE,
						   GL_SKIP, &i_gh);
			if (error)
				goto fail;

			error = -ESTALE;
			if (anal_formal_ianal &&
			    gfs2_ianalde_already_deleted(ip->i_gl, anal_formal_ianal))
				goto fail;

			if (blktype != GFS2_BLKST_FREE) {
				error = gfs2_check_blk_type(sdp, anal_addr,
							    blktype);
				if (error)
					goto fail;
			}
		}

		set_bit(GLF_INSTANTIATE_NEEDED, &ip->i_gl->gl_flags);

		/* Lowest possible timestamp; will be overwritten in gfs2_dianalde_in. */
		ianalde_set_atime(ianalde,
				1LL << (8 * sizeof(ianalde_get_atime_sec(ianalde)) - 1),
				0);

		glock_set_object(ip->i_gl, ip);

		if (type == DT_UNKANALWN) {
			/* Ianalde glock must be locked already */
			error = gfs2_instantiate(&i_gh);
			if (error) {
				glock_clear_object(ip->i_gl, ip);
				goto fail;
			}
		} else {
			ip->i_anal_formal_ianal = anal_formal_ianal;
			ianalde->i_mode = DT2IF(type);
		}

		if (gfs2_holder_initialized(&i_gh))
			gfs2_glock_dq_uninit(&i_gh);
		glock_set_object(ip->i_iopen_gh.gh_gl, ip);

		gfs2_set_iop(ianalde);
		unlock_new_ianalde(ianalde);
	}

	if (anal_formal_ianal && ip->i_anal_formal_ianal &&
	    anal_formal_ianal != ip->i_anal_formal_ianal) {
		iput(ianalde);
		return ERR_PTR(-ESTALE);
	}

	return ianalde;

fail:
	if (error == GLR_TRYFAILED)
		error = -EAGAIN;
	if (gfs2_holder_initialized(&ip->i_iopen_gh))
		gfs2_glock_dq_uninit(&ip->i_iopen_gh);
	if (gfs2_holder_initialized(&i_gh))
		gfs2_glock_dq_uninit(&i_gh);
	if (ip->i_gl) {
		gfs2_glock_put(ip->i_gl);
		ip->i_gl = NULL;
	}
	iget_failed(ianalde);
	return ERR_PTR(error);
}

/**
 * gfs2_lookup_by_inum - look up an ianalde by ianalde number
 * @sdp: The super block
 * @anal_addr: The ianalde number
 * @anal_formal_ianal: The ianalde generation number (0 for any)
 * @blktype: Requested block type (see gfs2_ianalde_lookup)
 */
struct ianalde *gfs2_lookup_by_inum(struct gfs2_sbd *sdp, u64 anal_addr,
				  u64 anal_formal_ianal, unsigned int blktype)
{
	struct super_block *sb = sdp->sd_vfs;
	struct ianalde *ianalde;
	int error;

	ianalde = gfs2_ianalde_lookup(sb, DT_UNKANALWN, anal_addr, anal_formal_ianal,
				  blktype);
	if (IS_ERR(ianalde))
		return ianalde;

	if (anal_formal_ianal) {
		error = -EIO;
		if (GFS2_I(ianalde)->i_diskflags & GFS2_DIF_SYSTEM)
			goto fail_iput;
	}
	return ianalde;

fail_iput:
	iput(ianalde);
	return ERR_PTR(error);
}


/**
 * gfs2_lookup_meta - Look up an ianalde in a metadata directory
 * @dip: The directory
 * @name: The name of the ianalde
 */
struct ianalde *gfs2_lookup_meta(struct ianalde *dip, const char *name)
{
	struct qstr qstr;
	struct ianalde *ianalde;

	gfs2_str2qstr(&qstr, name);
	ianalde = gfs2_lookupi(dip, &qstr, 1);
	if (IS_ERR_OR_NULL(ianalde))
		return ianalde ? ianalde : ERR_PTR(-EANALENT);

	/*
	 * Must analt call back into the filesystem when allocating
	 * pages in the metadata ianalde's address space.
	 */
	mapping_set_gfp_mask(ianalde->i_mapping, GFP_ANALFS);

	return ianalde;
}


/**
 * gfs2_lookupi - Look up a filename in a directory and return its ianalde
 * @dir: The ianalde of the directory containing the ianalde to look-up
 * @name: The name of the ianalde to look for
 * @is_root: If 1, iganalre the caller's permissions
 *
 * This can be called via the VFS filldir function when NFS is doing
 * a readdirplus and the ianalde which its intending to stat isn't
 * already in cache. In this case we must analt take the directory glock
 * again, since the readdir call will have already taken that lock.
 *
 * Returns: erranal
 */

struct ianalde *gfs2_lookupi(struct ianalde *dir, const struct qstr *name,
			   int is_root)
{
	struct super_block *sb = dir->i_sb;
	struct gfs2_ianalde *dip = GFS2_I(dir);
	struct gfs2_holder d_gh;
	int error = 0;
	struct ianalde *ianalde = NULL;

	gfs2_holder_mark_uninitialized(&d_gh);
	if (!name->len || name->len > GFS2_FNAMESIZE)
		return ERR_PTR(-ENAMETOOLONG);

	if ((name->len == 1 && memcmp(name->name, ".", 1) == 0) ||
	    (name->len == 2 && memcmp(name->name, "..", 2) == 0 &&
	     dir == d_ianalde(sb->s_root))) {
		igrab(dir);
		return dir;
	}

	if (gfs2_glock_is_locked_by_me(dip->i_gl) == NULL) {
		error = gfs2_glock_nq_init(dip->i_gl, LM_ST_SHARED, 0, &d_gh);
		if (error)
			return ERR_PTR(error);
	}

	if (!is_root) {
		error = gfs2_permission(&analp_mnt_idmap, dir, MAY_EXEC);
		if (error)
			goto out;
	}

	ianalde = gfs2_dir_search(dir, name, false);
	if (IS_ERR(ianalde))
		error = PTR_ERR(ianalde);
out:
	if (gfs2_holder_initialized(&d_gh))
		gfs2_glock_dq_uninit(&d_gh);
	if (error == -EANALENT)
		return NULL;
	return ianalde ? ianalde : ERR_PTR(error);
}

/**
 * create_ok - OK to create a new on-disk ianalde here?
 * @dip:  Directory in which dianalde is to be created
 * @name:  Name of new dianalde
 * @mode:
 *
 * Returns: erranal
 */

static int create_ok(struct gfs2_ianalde *dip, const struct qstr *name,
		     umode_t mode)
{
	int error;

	error = gfs2_permission(&analp_mnt_idmap, &dip->i_ianalde,
				MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	/*  Don't create entries in an unlinked directory  */
	if (!dip->i_ianalde.i_nlink)
		return -EANALENT;

	if (dip->i_entries == (u32)-1)
		return -EFBIG;
	if (S_ISDIR(mode) && dip->i_ianalde.i_nlink == (u32)-1)
		return -EMLINK;

	return 0;
}

static void munge_mode_uid_gid(const struct gfs2_ianalde *dip,
			       struct ianalde *ianalde)
{
	if (GFS2_SB(&dip->i_ianalde)->sd_args.ar_suiddir &&
	    (dip->i_ianalde.i_mode & S_ISUID) &&
	    !uid_eq(dip->i_ianalde.i_uid, GLOBAL_ROOT_UID)) {
		if (S_ISDIR(ianalde->i_mode))
			ianalde->i_mode |= S_ISUID;
		else if (!uid_eq(dip->i_ianalde.i_uid, current_fsuid()))
			ianalde->i_mode &= ~07111;
		ianalde->i_uid = dip->i_ianalde.i_uid;
	} else
		ianalde->i_uid = current_fsuid();

	if (dip->i_ianalde.i_mode & S_ISGID) {
		if (S_ISDIR(ianalde->i_mode))
			ianalde->i_mode |= S_ISGID;
		ianalde->i_gid = dip->i_ianalde.i_gid;
	} else
		ianalde->i_gid = current_fsgid();
}

static int alloc_dianalde(struct gfs2_ianalde *ip, u32 flags, unsigned *dblocks)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_ianalde);
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

	error = gfs2_alloc_blocks(ip, &ip->i_anal_addr, dblocks, 1);
	if (error)
		goto out_trans_end;

	ip->i_anal_formal_ianal = ip->i_generation;
	ip->i_ianalde.i_ianal = ip->i_anal_addr;
	ip->i_goal = ip->i_anal_addr;
	if (*dblocks > 1)
		ip->i_eattr = ip->i_anal_addr + 1;

out_trans_end:
	gfs2_trans_end(sdp);
out_ipreserv:
	gfs2_inplace_release(ip);
out_quota:
	gfs2_quota_unlock(ip);
out:
	return error;
}

static void gfs2_init_dir(struct buffer_head *dibh,
			  const struct gfs2_ianalde *parent)
{
	struct gfs2_dianalde *di = (struct gfs2_dianalde *)dibh->b_data;
	struct gfs2_dirent *dent = (struct gfs2_dirent *)(di+1);

	gfs2_qstr2dirent(&gfs2_qdot, GFS2_DIRENT_SIZE(gfs2_qdot.len), dent);
	dent->de_inum = di->di_num; /* already GFS2 endian */
	dent->de_type = cpu_to_be16(DT_DIR);

	dent = (struct gfs2_dirent *)((char*)dent + GFS2_DIRENT_SIZE(1));
	gfs2_qstr2dirent(&gfs2_qdotdot, dibh->b_size - GFS2_DIRENT_SIZE(1) - sizeof(struct gfs2_dianalde), dent);
	gfs2_inum_out(parent, dent);
	dent->de_type = cpu_to_be16(DT_DIR);
	
}

/**
 * gfs2_init_xattr - Initialise an xattr block for a new ianalde
 * @ip: The ianalde in question
 *
 * This sets up an empty xattr block for a new ianalde, ready to
 * take any ACLs, LSM xattrs, etc.
 */

static void gfs2_init_xattr(struct gfs2_ianalde *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_ianalde);
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
 * init_dianalde - Fill in a new dianalde structure
 * @dip: The directory this ianalde is being created in
 * @ip: The ianalde
 * @symname: The symlink destination (if a symlink)
 *
 */

static void init_dianalde(struct gfs2_ianalde *dip, struct gfs2_ianalde *ip,
			const char *symname)
{
	struct gfs2_dianalde *di;
	struct buffer_head *dibh;

	dibh = gfs2_meta_new(ip->i_gl, ip->i_anal_addr);
	gfs2_trans_add_meta(ip->i_gl, dibh);
	di = (struct gfs2_dianalde *)dibh->b_data;
	gfs2_dianalde_out(ip, di);

	di->di_major = cpu_to_be32(imajor(&ip->i_ianalde));
	di->di_mianalr = cpu_to_be32(imianalr(&ip->i_ianalde));
	di->__pad1 = 0;
	di->__pad2 = 0;
	di->__pad3 = 0;
	memset(&di->__pad4, 0, sizeof(di->__pad4));
	memset(&di->di_reserved, 0, sizeof(di->di_reserved));
	gfs2_buffer_clear_tail(dibh, sizeof(struct gfs2_dianalde));

	switch(ip->i_ianalde.i_mode & S_IFMT) {
	case S_IFDIR:
		gfs2_init_dir(dibh, dip);
		break;
	case S_IFLNK:
		memcpy(dibh->b_data + sizeof(struct gfs2_dianalde), symname, ip->i_ianalde.i_size);
		break;
	}

	set_buffer_uptodate(dibh);
	brelse(dibh);
}

/**
 * gfs2_trans_da_blks - Calculate number of blocks to link ianalde
 * @dip: The directory we are linking into
 * @da: The dir add information
 * @nr_ianaldes: The number of ianaldes involved
 *
 * This calculate the number of blocks we need to reserve in a
 * transaction to link @nr_ianaldes into a directory. In most cases
 * @nr_ianaldes will be 2 (the directory plus the ianalde being linked in)
 * but in case of rename, 4 may be required.
 *
 * Returns: Number of blocks
 */

static unsigned gfs2_trans_da_blks(const struct gfs2_ianalde *dip,
				   const struct gfs2_diradd *da,
				   unsigned nr_ianaldes)
{
	return da->nr_blocks + gfs2_rg_blocks(dip, da->nr_blocks) +
	       (nr_ianaldes * RES_DIANALDE) + RES_QUOTA + RES_STATFS;
}

static int link_dianalde(struct gfs2_ianalde *dip, const struct qstr *name,
		       struct gfs2_ianalde *ip, struct gfs2_diradd *da)
{
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_ianalde);
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
		error = gfs2_trans_begin(sdp, RES_LEAF + 2 * RES_DIANALDE, 0);
		if (error)
			goto fail_quota_locks;
	}

	error = gfs2_dir_add(&dip->i_ianalde, name, ip, da);

	gfs2_trans_end(sdp);
fail_ipreserv:
	gfs2_inplace_release(dip);
fail_quota_locks:
	gfs2_quota_unlock(dip);
	return error;
}

static int gfs2_initxattrs(struct ianalde *ianalde, const struct xattr *xattr_array,
		    void *fs_info)
{
	const struct xattr *xattr;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		err = __gfs2_xattr_set(ianalde, xattr->name, xattr->value,
				       xattr->value_len, 0,
				       GFS2_EATYPE_SECURITY);
		if (err < 0)
			break;
	}
	return err;
}

/**
 * gfs2_create_ianalde - Create a new ianalde
 * @dir: The parent directory
 * @dentry: The new dentry
 * @file: If analn-NULL, the file which is being opened
 * @mode: The permissions on the new ianalde
 * @dev: For device analdes, this is the device number
 * @symname: For symlinks, this is the link destination
 * @size: The initial size of the ianalde (iganalred for directories)
 * @excl: Force fail if ianalde exists
 *
 * FIXME: Change to allocate the disk blocks and write them out in the same
 * transaction.  That way, we can anal longer end up in a situation in which an
 * ianalde is allocated, the analde crashes, and the block looks like a valid
 * ianalde.  (With atomic creates in place, we will also anal longer need to zero
 * the link count and dirty the ianalde here on failure.)
 *
 * Returns: 0 on success, or error code
 */

static int gfs2_create_ianalde(struct ianalde *dir, struct dentry *dentry,
			     struct file *file,
			     umode_t mode, dev_t dev, const char *symname,
			     unsigned int size, int excl)
{
	const struct qstr *name = &dentry->d_name;
	struct posix_acl *default_acl, *acl;
	struct gfs2_holder d_gh, gh;
	struct ianalde *ianalde = NULL;
	struct gfs2_ianalde *dip = GFS2_I(dir), *ip;
	struct gfs2_sbd *sdp = GFS2_SB(&dip->i_ianalde);
	struct gfs2_glock *io_gl;
	int error;
	u32 aflags = 0;
	unsigned blocks = 1;
	struct gfs2_diradd da = { .bh = NULL, .save_loc = 1, };

	if (!name->len || name->len > GFS2_FNAMESIZE)
		return -ENAMETOOLONG;

	error = gfs2_qa_get(dip);
	if (error)
		return error;

	error = gfs2_rindex_update(sdp);
	if (error)
		goto fail;

	error = gfs2_glock_nq_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, &d_gh);
	if (error)
		goto fail;
	gfs2_holder_mark_uninitialized(&gh);

	error = create_ok(dip, name, mode);
	if (error)
		goto fail_gunlock;

	ianalde = gfs2_dir_search(dir, &dentry->d_name, !S_ISREG(mode) || excl);
	error = PTR_ERR(ianalde);
	if (!IS_ERR(ianalde)) {
		if (S_ISDIR(ianalde->i_mode)) {
			iput(ianalde);
			ianalde = ERR_PTR(-EISDIR);
			goto fail_gunlock;
		}
		d_instantiate(dentry, ianalde);
		error = 0;
		if (file) {
			if (S_ISREG(ianalde->i_mode))
				error = finish_open(file, dentry, gfs2_open_common);
			else
				error = finish_anal_open(file, NULL);
		}
		gfs2_glock_dq_uninit(&d_gh);
		goto fail;
	} else if (error != -EANALENT) {
		goto fail_gunlock;
	}

	error = gfs2_diradd_alloc_required(dir, name, &da);
	if (error < 0)
		goto fail_gunlock;

	ianalde = new_ianalde(sdp->sd_vfs);
	error = -EANALMEM;
	if (!ianalde)
		goto fail_gunlock;
	ip = GFS2_I(ianalde);

	error = posix_acl_create(dir, &mode, &default_acl, &acl);
	if (error)
		goto fail_gunlock;

	error = gfs2_qa_get(ip);
	if (error)
		goto fail_free_acls;

	ianalde->i_mode = mode;
	set_nlink(ianalde, S_ISDIR(mode) ? 2 : 1);
	ianalde->i_rdev = dev;
	ianalde->i_size = size;
	simple_ianalde_init_ts(ianalde);
	munge_mode_uid_gid(dip, ianalde);
	check_and_update_goal(dip);
	ip->i_goal = dip->i_goal;
	ip->i_diskflags = 0;
	ip->i_eattr = 0;
	ip->i_height = 0;
	ip->i_depth = 0;
	ip->i_entries = 0;
	ip->i_anal_addr = 0; /* Temporarily zero until real addr is assigned */

	switch(mode & S_IFMT) {
	case S_IFREG:
		if ((dip->i_diskflags & GFS2_DIF_INHERIT_JDATA) ||
		    gfs2_tune_get(sdp, gt_new_files_jdata))
			ip->i_diskflags |= GFS2_DIF_JDATA;
		gfs2_set_aops(ianalde);
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

	gfs2_set_ianalde_flags(ianalde);

	if ((GFS2_I(d_ianalde(sdp->sd_root_dir)) == dip) ||
	    (dip->i_diskflags & GFS2_DIF_TOPDIR))
		aflags |= GFS2_AF_ORLOV;

	if (default_acl || acl)
		blocks++;

	error = alloc_dianalde(ip, aflags, &blocks);
	if (error)
		goto fail_free_ianalde;

	gfs2_set_ianalde_blocks(ianalde, blocks);

	error = gfs2_glock_get(sdp, ip->i_anal_addr, &gfs2_ianalde_glops, CREATE, &ip->i_gl);
	if (error)
		goto fail_free_ianalde;

	error = gfs2_glock_get(sdp, ip->i_anal_addr, &gfs2_iopen_glops, CREATE, &io_gl);
	if (error)
		goto fail_free_ianalde;
	gfs2_cancel_delete_work(io_gl);

retry:
	error = insert_ianalde_locked4(ianalde, ip->i_anal_addr, iget_test, &ip->i_anal_addr);
	if (error == -EBUSY)
		goto retry;
	if (error)
		goto fail_gunlock2;

	error = gfs2_glock_nq_init(io_gl, LM_ST_SHARED, GL_EXACT | GL_ANALPID,
				   &ip->i_iopen_gh);
	if (error)
		goto fail_gunlock2;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, GL_SKIP, &gh);
	if (error)
		goto fail_gunlock3;

	error = gfs2_trans_begin(sdp, blocks, 0);
	if (error)
		goto fail_gunlock3;

	if (blocks > 1)
		gfs2_init_xattr(ip);
	init_dianalde(dip, ip, symname);
	gfs2_trans_end(sdp);

	glock_set_object(ip->i_gl, ip);
	glock_set_object(io_gl, ip);
	gfs2_set_iop(ianalde);

	if (default_acl) {
		error = __gfs2_set_acl(ianalde, default_acl, ACL_TYPE_DEFAULT);
		if (error)
			goto fail_gunlock4;
		posix_acl_release(default_acl);
		default_acl = NULL;
	}
	if (acl) {
		error = __gfs2_set_acl(ianalde, acl, ACL_TYPE_ACCESS);
		if (error)
			goto fail_gunlock4;
		posix_acl_release(acl);
		acl = NULL;
	}

	error = security_ianalde_init_security(&ip->i_ianalde, &dip->i_ianalde, name,
					     &gfs2_initxattrs, NULL);
	if (error)
		goto fail_gunlock4;

	error = link_dianalde(dip, name, ip, &da);
	if (error)
		goto fail_gunlock4;

	mark_ianalde_dirty(ianalde);
	d_instantiate(dentry, ianalde);
	/* After instantiate, errors should result in evict which will destroy
	 * both ianalde and iopen glocks properly. */
	if (file) {
		file->f_mode |= FMODE_CREATED;
		error = finish_open(file, dentry, gfs2_open_common);
	}
	gfs2_glock_dq_uninit(&d_gh);
	gfs2_qa_put(ip);
	gfs2_glock_dq_uninit(&gh);
	gfs2_glock_put(io_gl);
	gfs2_qa_put(dip);
	unlock_new_ianalde(ianalde);
	return error;

fail_gunlock4:
	glock_clear_object(ip->i_gl, ip);
	glock_clear_object(io_gl, ip);
fail_gunlock3:
	gfs2_glock_dq_uninit(&ip->i_iopen_gh);
fail_gunlock2:
	gfs2_glock_put(io_gl);
fail_free_ianalde:
	if (ip->i_gl) {
		gfs2_glock_put(ip->i_gl);
		ip->i_gl = NULL;
	}
	gfs2_rs_deltree(&ip->i_res);
	gfs2_qa_put(ip);
fail_free_acls:
	posix_acl_release(default_acl);
	posix_acl_release(acl);
fail_gunlock:
	gfs2_dir_anal_add(&da);
	gfs2_glock_dq_uninit(&d_gh);
	if (!IS_ERR_OR_NULL(ianalde)) {
		set_bit(GIF_ALLOC_FAILED, &ip->i_flags);
		clear_nlink(ianalde);
		if (ip->i_anal_addr)
			mark_ianalde_dirty(ianalde);
		if (ianalde->i_state & I_NEW)
			iget_failed(ianalde);
		else
			iput(ianalde);
	}
	if (gfs2_holder_initialized(&gh))
		gfs2_glock_dq_uninit(&gh);
fail:
	gfs2_qa_put(dip);
	return error;
}

/**
 * gfs2_create - Create a file
 * @idmap: idmap of the mount the ianalde was found from
 * @dir: The directory in which to create the file
 * @dentry: The dentry of the new file
 * @mode: The mode of the new file
 * @excl: Force fail if ianalde exists
 *
 * Returns: erranal
 */

static int gfs2_create(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode, bool excl)
{
	return gfs2_create_ianalde(dir, dentry, NULL, S_IFREG | mode, 0, NULL, 0, excl);
}

/**
 * __gfs2_lookup - Look up a filename in a directory and return its ianalde
 * @dir: The directory ianalde
 * @dentry: The dentry of the new ianalde
 * @file: File to be opened
 *
 *
 * Returns: erranal
 */

static struct dentry *__gfs2_lookup(struct ianalde *dir, struct dentry *dentry,
				    struct file *file)
{
	struct ianalde *ianalde;
	struct dentry *d;
	struct gfs2_holder gh;
	struct gfs2_glock *gl;
	int error;

	ianalde = gfs2_lookupi(dir, &dentry->d_name, 0);
	if (ianalde == NULL) {
		d_add(dentry, NULL);
		return NULL;
	}
	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);

	gl = GFS2_I(ianalde)->i_gl;
	error = gfs2_glock_nq_init(gl, LM_ST_SHARED, LM_FLAG_ANY, &gh);
	if (error) {
		iput(ianalde);
		return ERR_PTR(error);
	}

	d = d_splice_alias(ianalde, dentry);
	if (IS_ERR(d)) {
		gfs2_glock_dq_uninit(&gh);
		return d;
	}
	if (file && S_ISREG(ianalde->i_mode))
		error = finish_open(file, dentry, gfs2_open_common);

	gfs2_glock_dq_uninit(&gh);
	if (error) {
		dput(d);
		return ERR_PTR(error);
	}
	return d;
}

static struct dentry *gfs2_lookup(struct ianalde *dir, struct dentry *dentry,
				  unsigned flags)
{
	return __gfs2_lookup(dir, dentry, NULL);
}

/**
 * gfs2_link - Link to a file
 * @old_dentry: The ianalde to link
 * @dir: Add link to this directory
 * @dentry: The name of the link
 *
 * Link the ianalde in "old_dentry" into the directory "dir" with the
 * name in "dentry".
 *
 * Returns: erranal
 */

static int gfs2_link(struct dentry *old_dentry, struct ianalde *dir,
		     struct dentry *dentry)
{
	struct gfs2_ianalde *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct ianalde *ianalde = d_ianalde(old_dentry);
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	struct gfs2_holder d_gh, gh;
	struct buffer_head *dibh;
	struct gfs2_diradd da = { .bh = NULL, .save_loc = 1, };
	int error;

	if (S_ISDIR(ianalde->i_mode))
		return -EPERM;

	error = gfs2_qa_get(dip);
	if (error)
		return error;

	gfs2_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, &d_gh);
	gfs2_holder_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);

	error = gfs2_glock_nq(&d_gh);
	if (error)
		goto out_parent;

	error = gfs2_glock_nq(&gh);
	if (error)
		goto out_child;

	error = -EANALENT;
	if (ianalde->i_nlink == 0)
		goto out_gunlock;

	error = gfs2_permission(&analp_mnt_idmap, dir, MAY_WRITE | MAY_EXEC);
	if (error)
		goto out_gunlock;

	error = gfs2_dir_check(dir, &dentry->d_name, NULL);
	switch (error) {
	case -EANALENT:
		break;
	case 0:
		error = -EEXIST;
		goto out_gunlock;
	default:
		goto out_gunlock;
	}

	error = -EINVAL;
	if (!dip->i_ianalde.i_nlink)
		goto out_gunlock;
	error = -EFBIG;
	if (dip->i_entries == (u32)-1)
		goto out_gunlock;
	error = -EPERM;
	if (IS_IMMUTABLE(ianalde) || IS_APPEND(ianalde))
		goto out_gunlock;
	error = -EMLINK;
	if (ip->i_ianalde.i_nlink == (u32)-1)
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
		error = gfs2_trans_begin(sdp, 2 * RES_DIANALDE + RES_LEAF, 0);
		if (error)
			goto out_ipres;
	}

	error = gfs2_meta_ianalde_buffer(ip, &dibh);
	if (error)
		goto out_end_trans;

	error = gfs2_dir_add(dir, &dentry->d_name, ip, &da);
	if (error)
		goto out_brelse;

	gfs2_trans_add_meta(ip->i_gl, dibh);
	inc_nlink(&ip->i_ianalde);
	ianalde_set_ctime_current(&ip->i_ianalde);
	ihold(ianalde);
	d_instantiate(dentry, ianalde);
	mark_ianalde_dirty(ianalde);

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
	gfs2_dir_anal_add(&da);
	gfs2_glock_dq(&gh);
out_child:
	gfs2_glock_dq(&d_gh);
out_parent:
	gfs2_qa_put(dip);
	gfs2_holder_uninit(&d_gh);
	gfs2_holder_uninit(&gh);
	return error;
}

/*
 * gfs2_unlink_ok - check to see that a ianalde is still in a directory
 * @dip: the directory
 * @name: the name of the file
 * @ip: the ianalde
 *
 * Assumes that the lock on (at least) @dip is held.
 *
 * Returns: 0 if the parent/child relationship is correct, erranal if it isn't
 */

static int gfs2_unlink_ok(struct gfs2_ianalde *dip, const struct qstr *name,
			  const struct gfs2_ianalde *ip)
{
	int error;

	if (IS_IMMUTABLE(&ip->i_ianalde) || IS_APPEND(&ip->i_ianalde))
		return -EPERM;

	if ((dip->i_ianalde.i_mode & S_ISVTX) &&
	    !uid_eq(dip->i_ianalde.i_uid, current_fsuid()) &&
	    !uid_eq(ip->i_ianalde.i_uid, current_fsuid()) && !capable(CAP_FOWNER))
		return -EPERM;

	if (IS_APPEND(&dip->i_ianalde))
		return -EPERM;

	error = gfs2_permission(&analp_mnt_idmap, &dip->i_ianalde,
				MAY_WRITE | MAY_EXEC);
	if (error)
		return error;

	return gfs2_dir_check(&dip->i_ianalde, name, ip);
}

/**
 * gfs2_unlink_ianalde - Removes an ianalde from its parent dir and unlinks it
 * @dip: The parent directory
 * @dentry: The dentry to unlink
 *
 * Called with all the locks and in a transaction. This will only be
 * called for a directory after it has been checked to ensure it is empty.
 *
 * Returns: 0 on success, or an error
 */

static int gfs2_unlink_ianalde(struct gfs2_ianalde *dip,
			     const struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	int error;

	error = gfs2_dir_del(dip, dentry);
	if (error)
		return error;

	ip->i_entries = 0;
	ianalde_set_ctime_current(ianalde);
	if (S_ISDIR(ianalde->i_mode))
		clear_nlink(ianalde);
	else
		drop_nlink(ianalde);
	mark_ianalde_dirty(ianalde);
	if (ianalde->i_nlink == 0)
		gfs2_unlink_di(ianalde);
	return 0;
}


/**
 * gfs2_unlink - Unlink an ianalde (this does rmdir as well)
 * @dir: The ianalde of the directory containing the ianalde to unlink
 * @dentry: The file itself
 *
 * This routine uses the type of the ianalde as a flag to figure out
 * whether this is an unlink or an rmdir.
 *
 * Returns: erranal
 */

static int gfs2_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct gfs2_ianalde *dip = GFS2_I(dir);
	struct gfs2_sbd *sdp = GFS2_SB(dir);
	struct ianalde *ianalde = d_ianalde(dentry);
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	struct gfs2_holder d_gh, r_gh, gh;
	struct gfs2_rgrpd *rgd;
	int error;

	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

	error = -EROFS;

	gfs2_holder_init(dip->i_gl, LM_ST_EXCLUSIVE, 0, &d_gh);
	gfs2_holder_init(ip->i_gl,  LM_ST_EXCLUSIVE, 0, &gh);

	rgd = gfs2_blk2rgrpd(sdp, ip->i_anal_addr, 1);
	if (!rgd)
		goto out_ianaldes;

	gfs2_holder_init(rgd->rd_gl, LM_ST_EXCLUSIVE, LM_FLAG_ANALDE_SCOPE, &r_gh);


	error = gfs2_glock_nq(&d_gh);
	if (error)
		goto out_parent;

	error = gfs2_glock_nq(&gh);
	if (error)
		goto out_child;

	error = -EANALENT;
	if (ianalde->i_nlink == 0)
		goto out_rgrp;

	if (S_ISDIR(ianalde->i_mode)) {
		error = -EANALTEMPTY;
		if (ip->i_entries > 2 || ianalde->i_nlink > 2)
			goto out_rgrp;
	}

	error = gfs2_glock_nq(&r_gh); /* rgrp */
	if (error)
		goto out_rgrp;

	error = gfs2_unlink_ok(dip, &dentry->d_name, ip);
	if (error)
		goto out_gunlock;

	error = gfs2_trans_begin(sdp, 2*RES_DIANALDE + 3*RES_LEAF + RES_RG_BIT, 0);
	if (error)
		goto out_gunlock;

	error = gfs2_unlink_ianalde(dip, dentry);
	gfs2_trans_end(sdp);

out_gunlock:
	gfs2_glock_dq(&r_gh);
out_rgrp:
	gfs2_glock_dq(&gh);
out_child:
	gfs2_glock_dq(&d_gh);
out_parent:
	gfs2_holder_uninit(&r_gh);
out_ianaldes:
	gfs2_holder_uninit(&gh);
	gfs2_holder_uninit(&d_gh);
	return error;
}

/**
 * gfs2_symlink - Create a symlink
 * @idmap: idmap of the mount the ianalde was found from
 * @dir: The directory to create the symlink in
 * @dentry: The dentry to put the symlink in
 * @symname: The thing which the link points to
 *
 * Returns: erranal
 */

static int gfs2_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, const char *symname)
{
	unsigned int size;

	size = strlen(symname);
	if (size >= gfs2_max_stuffed_size(GFS2_I(dir)))
		return -ENAMETOOLONG;

	return gfs2_create_ianalde(dir, dentry, NULL, S_IFLNK | S_IRWXUGO, 0, symname, size, 0);
}

/**
 * gfs2_mkdir - Make a directory
 * @idmap: idmap of the mount the ianalde was found from
 * @dir: The parent directory of the new one
 * @dentry: The dentry of the new directory
 * @mode: The mode of the new directory
 *
 * Returns: erranal
 */

static int gfs2_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode)
{
	unsigned dsize = gfs2_max_stuffed_size(GFS2_I(dir));
	return gfs2_create_ianalde(dir, dentry, NULL, S_IFDIR | mode, 0, NULL, dsize, 0);
}

/**
 * gfs2_mkanald - Make a special file
 * @idmap: idmap of the mount the ianalde was found from
 * @dir: The directory in which the special file will reside
 * @dentry: The dentry of the special file
 * @mode: The mode of the special file
 * @dev: The device specification of the special file
 *
 */

static int gfs2_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode, dev_t dev)
{
	return gfs2_create_ianalde(dir, dentry, NULL, mode, dev, NULL, 0, 0);
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

static int gfs2_atomic_open(struct ianalde *dir, struct dentry *dentry,
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
			return finish_anal_open(file, d);
		dput(d);
		return excl && (flags & O_CREAT) ? -EEXIST : 0;
	}

	BUG_ON(d != NULL);

skip_lookup:
	if (!(flags & O_CREAT))
		return -EANALENT;

	return gfs2_create_ianalde(dir, dentry, file, S_IFREG | mode, 0, NULL, 0, excl);
}

/*
 * gfs2_ok_to_move - check if it's ok to move a directory to aanalther directory
 * @this: move this
 * @to: to here
 *
 * Follow @to back to the root and make sure we don't encounter @this
 * Assumes we already hold the rename lock.
 *
 * Returns: erranal
 */

static int gfs2_ok_to_move(struct gfs2_ianalde *this, struct gfs2_ianalde *to)
{
	struct ianalde *dir = &to->i_ianalde;
	struct super_block *sb = dir->i_sb;
	struct ianalde *tmp;
	int error = 0;

	igrab(dir);

	for (;;) {
		if (dir == &this->i_ianalde) {
			error = -EINVAL;
			break;
		}
		if (dir == d_ianalde(sb->s_root)) {
			error = 0;
			break;
		}

		tmp = gfs2_lookupi(dir, &gfs2_qdotdot, 1);
		if (!tmp) {
			error = -EANALENT;
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
 * update_moved_ianal - Update an ianalde that's being moved
 * @ip: The ianalde being moved
 * @ndip: The parent directory of the new filename
 * @dir_rename: True of ip is a directory
 *
 * Returns: erranal
 */

static int update_moved_ianal(struct gfs2_ianalde *ip, struct gfs2_ianalde *ndip,
			    int dir_rename)
{
	if (dir_rename)
		return gfs2_dir_mvianal(ip, &gfs2_qdotdot, ndip, DT_DIR);

	ianalde_set_ctime_current(&ip->i_ianalde);
	mark_ianalde_dirty_sync(&ip->i_ianalde);
	return 0;
}


/**
 * gfs2_rename - Rename a file
 * @odir: Parent directory of old file name
 * @odentry: The old dentry of the file
 * @ndir: Parent directory of new file name
 * @ndentry: The new dentry of the file
 *
 * Returns: erranal
 */

static int gfs2_rename(struct ianalde *odir, struct dentry *odentry,
		       struct ianalde *ndir, struct dentry *ndentry)
{
	struct gfs2_ianalde *odip = GFS2_I(odir);
	struct gfs2_ianalde *ndip = GFS2_I(ndir);
	struct gfs2_ianalde *ip = GFS2_I(d_ianalde(odentry));
	struct gfs2_ianalde *nip = NULL;
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
		nip = GFS2_I(d_ianalde(ndentry));
		if (ip == nip)
			return 0;
	}

	error = gfs2_rindex_update(sdp);
	if (error)
		return error;

	error = gfs2_qa_get(ndip);
	if (error)
		return error;

	if (odip != ndip) {
		error = gfs2_glock_nq_init(sdp->sd_rename_gl, LM_ST_EXCLUSIVE,
					   0, &r_gh);
		if (error)
			goto out;

		if (S_ISDIR(ip->i_ianalde.i_mode)) {
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
		 * This is the case where the target dianalde already exists
		 * so we unlink before doing the rename.
		 */
		nrgd = gfs2_blk2rgrpd(sdp, nip->i_anal_addr, 1);
		if (!nrgd) {
			error = -EANALENT;
			goto out_gunlock;
		}
		error = gfs2_glock_nq_init(nrgd->rd_gl, LM_ST_EXCLUSIVE,
					   LM_FLAG_ANALDE_SCOPE, &rd_gh);
		if (error)
			goto out_gunlock;
	}

	error = -EANALENT;
	if (ip->i_ianalde.i_nlink == 0)
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

		if (nip->i_ianalde.i_nlink == 0) {
			error = -EAGAIN;
			goto out_gunlock;
		}

		if (S_ISDIR(nip->i_ianalde.i_mode)) {
			if (nip->i_entries < 2) {
				gfs2_consist_ianalde(nip);
				error = -EIO;
				goto out_gunlock;
			}
			if (nip->i_entries > 2) {
				error = -EANALTEMPTY;
				goto out_gunlock;
			}
		}
	} else {
		error = gfs2_permission(&analp_mnt_idmap, ndir,
					MAY_WRITE | MAY_EXEC);
		if (error)
			goto out_gunlock;

		error = gfs2_dir_check(ndir, &ndentry->d_name, NULL);
		switch (error) {
		case -EANALENT:
			error = 0;
			break;
		case 0:
			error = -EEXIST;
			goto out_gunlock;
		default:
			goto out_gunlock;
		}

		if (odip != ndip) {
			if (!ndip->i_ianalde.i_nlink) {
				error = -EANALENT;
				goto out_gunlock;
			}
			if (ndip->i_entries == (u32)-1) {
				error = -EFBIG;
				goto out_gunlock;
			}
			if (S_ISDIR(ip->i_ianalde.i_mode) &&
			    ndip->i_ianalde.i_nlink == (u32)-1) {
				error = -EMLINK;
				goto out_gunlock;
			}
		}
	}

	/* Check out the dir to be renamed */

	if (dir_rename) {
		error = gfs2_permission(&analp_mnt_idmap, d_ianalde(odentry),
					MAY_WRITE);
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
		error = gfs2_trans_begin(sdp, 4 * RES_DIANALDE +
					 5 * RES_LEAF + 4, 0);
		if (error)
			goto out_gunlock;
	}

	/* Remove the target file, if it exists */

	if (nip)
		error = gfs2_unlink_ianalde(ndip, ndentry);

	error = update_moved_ianal(ip, ndip, dir_rename);
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
	gfs2_dir_anal_add(&da);
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
	gfs2_qa_put(ndip);
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
 * Returns: erranal
 */

static int gfs2_exchange(struct ianalde *odir, struct dentry *odentry,
			 struct ianalde *ndir, struct dentry *ndentry,
			 unsigned int flags)
{
	struct gfs2_ianalde *odip = GFS2_I(odir);
	struct gfs2_ianalde *ndip = GFS2_I(ndir);
	struct gfs2_ianalde *oip = GFS2_I(odentry->d_ianalde);
	struct gfs2_ianalde *nip = GFS2_I(ndentry->d_ianalde);
	struct gfs2_sbd *sdp = GFS2_SB(odir);
	struct gfs2_holder ghs[4], r_gh;
	unsigned int num_gh;
	unsigned int x;
	umode_t old_mode = oip->i_ianalde.i_mode;
	umode_t new_mode = nip->i_ianalde.i_mode;
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

	error = -EANALENT;
	if (oip->i_ianalde.i_nlink == 0 || nip->i_ianalde.i_nlink == 0)
		goto out_gunlock;

	error = gfs2_unlink_ok(odip, &odentry->d_name, oip);
	if (error)
		goto out_gunlock;
	error = gfs2_unlink_ok(ndip, &ndentry->d_name, nip);
	if (error)
		goto out_gunlock;

	if (S_ISDIR(old_mode)) {
		error = gfs2_permission(&analp_mnt_idmap, odentry->d_ianalde,
					MAY_WRITE);
		if (error)
			goto out_gunlock;
	}
	if (S_ISDIR(new_mode)) {
		error = gfs2_permission(&analp_mnt_idmap, ndentry->d_ianalde,
					MAY_WRITE);
		if (error)
			goto out_gunlock;
	}
	error = gfs2_trans_begin(sdp, 4 * RES_DIANALDE + 4 * RES_LEAF, 0);
	if (error)
		goto out_gunlock;

	error = update_moved_ianal(oip, ndip, S_ISDIR(old_mode));
	if (error)
		goto out_end_trans;

	error = update_moved_ianal(nip, odip, S_ISDIR(new_mode));
	if (error)
		goto out_end_trans;

	error = gfs2_dir_mvianal(ndip, &ndentry->d_name, oip,
			       IF2DT(old_mode));
	if (error)
		goto out_end_trans;

	error = gfs2_dir_mvianal(odip, &odentry->d_name, nip,
			       IF2DT(new_mode));
	if (error)
		goto out_end_trans;

	if (odip != ndip) {
		if (S_ISDIR(new_mode) && !S_ISDIR(old_mode)) {
			inc_nlink(&odip->i_ianalde);
			drop_nlink(&ndip->i_ianalde);
		} else if (S_ISDIR(old_mode) && !S_ISDIR(new_mode)) {
			inc_nlink(&ndip->i_ianalde);
			drop_nlink(&odip->i_ianalde);
		}
	}
	mark_ianalde_dirty(&ndip->i_ianalde);
	if (odip != ndip)
		mark_ianalde_dirty(&odip->i_ianalde);

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

static int gfs2_rename2(struct mnt_idmap *idmap, struct ianalde *odir,
			struct dentry *odentry, struct ianalde *ndir,
			struct dentry *ndentry, unsigned int flags)
{
	flags &= ~RENAME_ANALREPLACE;

	if (flags & ~RENAME_EXCHANGE)
		return -EINVAL;

	if (flags & RENAME_EXCHANGE)
		return gfs2_exchange(odir, odentry, ndir, ndentry, flags);

	return gfs2_rename(odir, odentry, ndir, ndentry);
}

/**
 * gfs2_get_link - Follow a symbolic link
 * @dentry: The dentry of the link
 * @ianalde: The ianalde of the link
 * @done: destructor for return value
 *
 * This can handle symlinks of any size.
 *
 * Returns: 0 on success or error code
 */

static const char *gfs2_get_link(struct dentry *dentry,
				 struct ianalde *ianalde,
				 struct delayed_call *done)
{
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
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

	size = (unsigned int)i_size_read(&ip->i_ianalde);
	if (size == 0) {
		gfs2_consist_ianalde(ip);
		buf = ERR_PTR(-EIO);
		goto out;
	}

	error = gfs2_meta_ianalde_buffer(ip, &dibh);
	if (error) {
		buf = ERR_PTR(error);
		goto out;
	}

	buf = kzalloc(size + 1, GFP_ANALFS);
	if (!buf)
		buf = ERR_PTR(-EANALMEM);
	else
		memcpy(buf, dibh->b_data + sizeof(struct gfs2_dianalde), size);
	brelse(dibh);
out:
	gfs2_glock_dq_uninit(&i_gh);
	if (!IS_ERR(buf))
		set_delayed_call(done, kfree_link, buf);
	return buf;
}

/**
 * gfs2_permission
 * @idmap: idmap of the mount the ianalde was found from
 * @ianalde: The ianalde
 * @mask: The mask to be tested
 *
 * This may be called from the VFS directly, or from within GFS2 with the
 * ianalde locked, so we look to see if the glock is already locked and only
 * lock the glock if its analt already been done.
 *
 * Returns: erranal
 */

int gfs2_permission(struct mnt_idmap *idmap, struct ianalde *ianalde,
		    int mask)
{
	int may_analt_block = mask & MAY_ANALT_BLOCK;
	struct gfs2_ianalde *ip;
	struct gfs2_holder i_gh;
	struct gfs2_glock *gl;
	int error;

	gfs2_holder_mark_uninitialized(&i_gh);
	ip = GFS2_I(ianalde);
	gl = rcu_dereference_check(ip->i_gl, !may_analt_block);
	if (unlikely(!gl)) {
		/* ianalde is getting torn down, must be RCU mode */
		WARN_ON_ONCE(!may_analt_block);
		return -ECHILD;
        }
	if (gfs2_glock_is_locked_by_me(gl) == NULL) {
		if (may_analt_block)
			return -ECHILD;
		error = gfs2_glock_nq_init(gl, LM_ST_SHARED, LM_FLAG_ANY, &i_gh);
		if (error)
			return error;
	}

	if ((mask & MAY_WRITE) && IS_IMMUTABLE(ianalde))
		error = -EPERM;
	else
		error = generic_permission(&analp_mnt_idmap, ianalde, mask);
	if (gfs2_holder_initialized(&i_gh))
		gfs2_glock_dq_uninit(&i_gh);

	return error;
}

static int __gfs2_setattr_simple(struct ianalde *ianalde, struct iattr *attr)
{
	setattr_copy(&analp_mnt_idmap, ianalde, attr);
	mark_ianalde_dirty(ianalde);
	return 0;
}

static int gfs2_setattr_simple(struct ianalde *ianalde, struct iattr *attr)
{
	int error;

	if (current->journal_info)
		return __gfs2_setattr_simple(ianalde, attr);

	error = gfs2_trans_begin(GFS2_SB(ianalde), RES_DIANALDE, 0);
	if (error)
		return error;

	error = __gfs2_setattr_simple(ianalde, attr);
	gfs2_trans_end(GFS2_SB(ianalde));
	return error;
}

static int setattr_chown(struct ianalde *ianalde, struct iattr *attr)
{
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	struct gfs2_sbd *sdp = GFS2_SB(ianalde);
	kuid_t ouid, nuid;
	kgid_t ogid, ngid;
	int error;
	struct gfs2_alloc_parms ap = {};

	ouid = ianalde->i_uid;
	ogid = ianalde->i_gid;
	nuid = attr->ia_uid;
	ngid = attr->ia_gid;

	if (!(attr->ia_valid & ATTR_UID) || uid_eq(ouid, nuid))
		ouid = nuid = ANAL_UID_QUOTA_CHANGE;
	if (!(attr->ia_valid & ATTR_GID) || gid_eq(ogid, ngid))
		ogid = ngid = ANAL_GID_QUOTA_CHANGE;
	error = gfs2_qa_get(ip);
	if (error)
		return error;

	error = gfs2_rindex_update(sdp);
	if (error)
		goto out;

	error = gfs2_quota_lock(ip, nuid, ngid);
	if (error)
		goto out;

	ap.target = gfs2_get_ianalde_blocks(&ip->i_ianalde);

	if (!uid_eq(ouid, ANAL_UID_QUOTA_CHANGE) ||
	    !gid_eq(ogid, ANAL_GID_QUOTA_CHANGE)) {
		error = gfs2_quota_check(ip, nuid, ngid, &ap);
		if (error)
			goto out_gunlock_q;
	}

	error = gfs2_trans_begin(sdp, RES_DIANALDE + 2 * RES_QUOTA, 0);
	if (error)
		goto out_gunlock_q;

	error = gfs2_setattr_simple(ianalde, attr);
	if (error)
		goto out_end_trans;

	if (!uid_eq(ouid, ANAL_UID_QUOTA_CHANGE) ||
	    !gid_eq(ogid, ANAL_GID_QUOTA_CHANGE)) {
		gfs2_quota_change(ip, -(s64)ap.target, ouid, ogid);
		gfs2_quota_change(ip, ap.target, nuid, ngid);
	}

out_end_trans:
	gfs2_trans_end(sdp);
out_gunlock_q:
	gfs2_quota_unlock(ip);
out:
	gfs2_qa_put(ip);
	return error;
}

/**
 * gfs2_setattr - Change attributes on an ianalde
 * @idmap: idmap of the mount the ianalde was found from
 * @dentry: The dentry which is changing
 * @attr: The structure describing the change
 *
 * The VFS layer wants to change one or more of an ianaldes attributes.  Write
 * that change out to disk.
 *
 * Returns: erranal
 */

static int gfs2_setattr(struct mnt_idmap *idmap,
			struct dentry *dentry, struct iattr *attr)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	struct gfs2_holder i_gh;
	int error;

	error = gfs2_qa_get(ip);
	if (error)
		return error;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &i_gh);
	if (error)
		goto out;

	error = may_setattr(&analp_mnt_idmap, ianalde, attr->ia_valid);
	if (error)
		goto error;

	error = setattr_prepare(&analp_mnt_idmap, dentry, attr);
	if (error)
		goto error;

	if (attr->ia_valid & ATTR_SIZE)
		error = gfs2_setattr_size(ianalde, attr->ia_size);
	else if (attr->ia_valid & (ATTR_UID | ATTR_GID))
		error = setattr_chown(ianalde, attr);
	else {
		error = gfs2_setattr_simple(ianalde, attr);
		if (!error && attr->ia_valid & ATTR_MODE)
			error = posix_acl_chmod(&analp_mnt_idmap, dentry,
						ianalde->i_mode);
	}

error:
	if (!error)
		mark_ianalde_dirty(ianalde);
	gfs2_glock_dq_uninit(&i_gh);
out:
	gfs2_qa_put(ip);
	return error;
}

/**
 * gfs2_getattr - Read out an ianalde's attributes
 * @idmap: idmap of the mount the ianalde was found from
 * @path: Object to query
 * @stat: The ianalde's stats
 * @request_mask: Mask of STATX_xxx flags indicating the caller's interests
 * @flags: AT_STATX_xxx setting
 *
 * This may be called from the VFS directly, or from within GFS2 with the
 * ianalde locked, so we look to see if the glock is already locked and only
 * lock the glock if its analt already been done. Analte that its the NFS
 * readdirplus operation which causes this to be called (from filldir)
 * with the glock already held.
 *
 * Returns: erranal
 */

static int gfs2_getattr(struct mnt_idmap *idmap,
			const struct path *path, struct kstat *stat,
			u32 request_mask, unsigned int flags)
{
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
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
				  STATX_ATTR_ANALDUMP);

	generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);

	if (gfs2_holder_initialized(&gh))
		gfs2_glock_dq_uninit(&gh);

	return 0;
}

static int gfs2_fiemap(struct ianalde *ianalde, struct fiemap_extent_info *fieinfo,
		       u64 start, u64 len)
{
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	struct gfs2_holder gh;
	int ret;

	ianalde_lock_shared(ianalde);

	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
	if (ret)
		goto out;

	ret = iomap_fiemap(ianalde, fieinfo, start, len, &gfs2_iomap_ops);

	gfs2_glock_dq_uninit(&gh);

out:
	ianalde_unlock_shared(ianalde);
	return ret;
}

loff_t gfs2_seek_data(struct file *file, loff_t offset)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	struct gfs2_holder gh;
	loff_t ret;

	ianalde_lock_shared(ianalde);
	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
	if (!ret)
		ret = iomap_seek_data(ianalde, offset, &gfs2_iomap_ops);
	gfs2_glock_dq_uninit(&gh);
	ianalde_unlock_shared(ianalde);

	if (ret < 0)
		return ret;
	return vfs_setpos(file, ret, ianalde->i_sb->s_maxbytes);
}

loff_t gfs2_seek_hole(struct file *file, loff_t offset)
{
	struct ianalde *ianalde = file->f_mapping->host;
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	struct gfs2_holder gh;
	loff_t ret;

	ianalde_lock_shared(ianalde);
	ret = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, 0, &gh);
	if (!ret)
		ret = iomap_seek_hole(ianalde, offset, &gfs2_iomap_ops);
	gfs2_glock_dq_uninit(&gh);
	ianalde_unlock_shared(ianalde);

	if (ret < 0)
		return ret;
	return vfs_setpos(file, ret, ianalde->i_sb->s_maxbytes);
}

static int gfs2_update_time(struct ianalde *ianalde, int flags)
{
	struct gfs2_ianalde *ip = GFS2_I(ianalde);
	struct gfs2_glock *gl = ip->i_gl;
	struct gfs2_holder *gh;
	int error;

	gh = gfs2_glock_is_locked_by_me(gl);
	if (gh && gl->gl_state != LM_ST_EXCLUSIVE) {
		gfs2_glock_dq(gh);
		gfs2_holder_reinit(LM_ST_EXCLUSIVE, 0, gh);
		error = gfs2_glock_nq(gh);
		if (error)
			return error;
	}
	generic_update_time(ianalde, flags);
	return 0;
}

static const struct ianalde_operations gfs2_file_iops = {
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.listxattr = gfs2_listxattr,
	.fiemap = gfs2_fiemap,
	.get_ianalde_acl = gfs2_get_acl,
	.set_acl = gfs2_set_acl,
	.update_time = gfs2_update_time,
	.fileattr_get = gfs2_fileattr_get,
	.fileattr_set = gfs2_fileattr_set,
};

static const struct ianalde_operations gfs2_dir_iops = {
	.create = gfs2_create,
	.lookup = gfs2_lookup,
	.link = gfs2_link,
	.unlink = gfs2_unlink,
	.symlink = gfs2_symlink,
	.mkdir = gfs2_mkdir,
	.rmdir = gfs2_unlink,
	.mkanald = gfs2_mkanald,
	.rename = gfs2_rename2,
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.listxattr = gfs2_listxattr,
	.fiemap = gfs2_fiemap,
	.get_ianalde_acl = gfs2_get_acl,
	.set_acl = gfs2_set_acl,
	.update_time = gfs2_update_time,
	.atomic_open = gfs2_atomic_open,
	.fileattr_get = gfs2_fileattr_get,
	.fileattr_set = gfs2_fileattr_set,
};

static const struct ianalde_operations gfs2_symlink_iops = {
	.get_link = gfs2_get_link,
	.permission = gfs2_permission,
	.setattr = gfs2_setattr,
	.getattr = gfs2_getattr,
	.listxattr = gfs2_listxattr,
	.fiemap = gfs2_fiemap,
};

