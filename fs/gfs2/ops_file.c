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
#include <linux/pagemap.h>
#include <linux/uio.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/gfs2_ondisk.h>
#include <linux/ext2_fs.h>
#include <linux/crc32.h>
#include <linux/lm_interface.h>
#include <linux/writeback.h>
#include <asm/uaccess.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "dir.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "lm.h"
#include "log.h"
#include "meta_io.h"
#include "ops_file.h"
#include "ops_vm.h"
#include "quota.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"
#include "eaops.h"

/*
 * Most fields left uninitialised to catch anybody who tries to
 * use them. f_flags set to prevent file_accessed() from touching
 * any other part of this. Its use is purely as a flag so that we
 * know (in readpage()) whether or not do to locking.
 */
struct file gfs2_internal_file_sentinel = {
	.f_flags = O_NOATIME|O_RDONLY,
};

static int gfs2_read_actor(read_descriptor_t *desc, struct page *page,
			   unsigned long offset, unsigned long size)
{
	char *kaddr;
	unsigned long count = desc->count;

	if (size > count)
		size = count;

	kaddr = kmap(page);
	memcpy(desc->arg.data, kaddr + offset, size);
	kunmap(page);

	desc->count = count - size;
	desc->written += size;
	desc->arg.buf += size;
	return size;
}

int gfs2_internal_read(struct gfs2_inode *ip, struct file_ra_state *ra_state,
		       char *buf, loff_t *pos, unsigned size)
{
	struct inode *inode = &ip->i_inode;
	read_descriptor_t desc;
	desc.written = 0;
	desc.arg.data = buf;
	desc.count = size;
	desc.error = 0;
	do_generic_mapping_read(inode->i_mapping, ra_state,
				&gfs2_internal_file_sentinel, pos, &desc,
				gfs2_read_actor);
	return desc.written ? desc.written : desc.error;
}

/**
 * gfs2_llseek - seek to a location in a file
 * @file: the file
 * @offset: the offset
 * @origin: Where to seek from (SEEK_SET, SEEK_CUR, or SEEK_END)
 *
 * SEEK_END requires the glock for the file because it references the
 * file's size.
 *
 * Returns: The new offset, or errno
 */

static loff_t gfs2_llseek(struct file *file, loff_t offset, int origin)
{
	struct gfs2_inode *ip = GFS2_I(file->f_mapping->host);
	struct gfs2_holder i_gh;
	loff_t error;

	if (origin == 2) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY,
					   &i_gh);
		if (!error) {
			error = remote_llseek(file, offset, origin);
			gfs2_glock_dq_uninit(&i_gh);
		}
	} else
		error = remote_llseek(file, offset, origin);

	return error;
}

/**
 * gfs2_readdir - Read directory entries from a directory
 * @file: The directory to read from
 * @dirent: Buffer for dirents
 * @filldir: Function used to do the copying
 *
 * Returns: errno
 */

static int gfs2_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct inode *dir = file->f_mapping->host;
	struct gfs2_inode *dip = GFS2_I(dir);
	struct gfs2_holder d_gh;
	u64 offset = file->f_pos;
	int error;

	gfs2_holder_init(dip->i_gl, LM_ST_SHARED, GL_ATIME, &d_gh);
	error = gfs2_glock_nq_atime(&d_gh);
	if (error) {
		gfs2_holder_uninit(&d_gh);
		return error;
	}

	error = gfs2_dir_read(dir, &offset, dirent, filldir);

	gfs2_glock_dq_uninit(&d_gh);

	file->f_pos = offset;

	return error;
}

/**
 * fsflags_cvt
 * @table: A table of 32 u32 flags
 * @val: a 32 bit value to convert
 *
 * This function can be used to convert between fsflags values and
 * GFS2's own flags values.
 *
 * Returns: the converted flags
 */
static u32 fsflags_cvt(const u32 *table, u32 val)
{
	u32 res = 0;
	while(val) {
		if (val & 1)
			res |= *table;
		table++;
		val >>= 1;
	}
	return res;
}

static const u32 fsflags_to_gfs2[32] = {
	[3] = GFS2_DIF_SYNC,
	[4] = GFS2_DIF_IMMUTABLE,
	[5] = GFS2_DIF_APPENDONLY,
	[7] = GFS2_DIF_NOATIME,
	[12] = GFS2_DIF_EXHASH,
	[14] = GFS2_DIF_JDATA,
	[20] = GFS2_DIF_DIRECTIO,
};

static const u32 gfs2_to_fsflags[32] = {
	[gfs2fl_Sync] = FS_SYNC_FL,
	[gfs2fl_Immutable] = FS_IMMUTABLE_FL,
	[gfs2fl_AppendOnly] = FS_APPEND_FL,
	[gfs2fl_NoAtime] = FS_NOATIME_FL,
	[gfs2fl_ExHash] = FS_INDEX_FL,
	[gfs2fl_Jdata] = FS_JOURNAL_DATA_FL,
	[gfs2fl_Directio] = FS_DIRECTIO_FL,
	[gfs2fl_InheritDirectio] = FS_DIRECTIO_FL,
	[gfs2fl_InheritJdata] = FS_JOURNAL_DATA_FL,
};

static int gfs2_get_flags(struct file *filp, u32 __user *ptr)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder gh;
	int error;
	u32 fsflags;

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &gh);
	error = gfs2_glock_nq_atime(&gh);
	if (error)
		return error;

	fsflags = fsflags_cvt(gfs2_to_fsflags, ip->i_di.di_flags);
	if (put_user(fsflags, ptr))
		error = -EFAULT;

	gfs2_glock_dq_m(1, &gh);
	gfs2_holder_uninit(&gh);
	return error;
}

void gfs2_set_inode_flags(struct inode *inode)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_dinode_host *di = &ip->i_di;
	unsigned int flags = inode->i_flags;

	flags &= ~(S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);
	if (di->di_flags & GFS2_DIF_IMMUTABLE)
		flags |= S_IMMUTABLE;
	if (di->di_flags & GFS2_DIF_APPENDONLY)
		flags |= S_APPEND;
	if (di->di_flags & GFS2_DIF_NOATIME)
		flags |= S_NOATIME;
	if (di->di_flags & GFS2_DIF_SYNC)
		flags |= S_SYNC;
	inode->i_flags = flags;
}

/* Flags that can be set by user space */
#define GFS2_FLAGS_USER_SET (GFS2_DIF_JDATA|			\
			     GFS2_DIF_DIRECTIO|			\
			     GFS2_DIF_IMMUTABLE|		\
			     GFS2_DIF_APPENDONLY|		\
			     GFS2_DIF_NOATIME|			\
			     GFS2_DIF_SYNC|			\
			     GFS2_DIF_SYSTEM|			\
			     GFS2_DIF_INHERIT_DIRECTIO|		\
			     GFS2_DIF_INHERIT_JDATA)

/**
 * gfs2_set_flags - set flags on an inode
 * @inode: The inode
 * @flags: The flags to set
 * @mask: Indicates which flags are valid
 *
 */
static int do_gfs2_set_flags(struct file *filp, u32 reqflags, u32 mask)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_sbd *sdp = GFS2_SB(inode);
	struct buffer_head *bh;
	struct gfs2_holder gh;
	int error;
	u32 new_flags, flags;

	error = gfs2_glock_nq_init(ip->i_gl, LM_ST_EXCLUSIVE, 0, &gh);
	if (error)
		return error;

	flags = ip->i_di.di_flags;
	new_flags = (flags & ~mask) | (reqflags & mask);
	if ((new_flags ^ flags) == 0)
		goto out;

	if (S_ISDIR(inode->i_mode)) {
		if ((new_flags ^ flags) & GFS2_DIF_JDATA)
			new_flags ^= (GFS2_DIF_JDATA|GFS2_DIF_INHERIT_JDATA);
		if ((new_flags ^ flags) & GFS2_DIF_DIRECTIO)
			new_flags ^= (GFS2_DIF_DIRECTIO|GFS2_DIF_INHERIT_DIRECTIO);
	}

	error = -EINVAL;
	if ((new_flags ^ flags) & ~GFS2_FLAGS_USER_SET)
		goto out;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) && (new_flags & GFS2_DIF_IMMUTABLE))
		goto out;
	if (IS_APPEND(inode) && (new_flags & GFS2_DIF_APPENDONLY))
		goto out;
	if (((new_flags ^ flags) & GFS2_DIF_IMMUTABLE) &&
	    !capable(CAP_LINUX_IMMUTABLE))
		goto out;
	if (!IS_IMMUTABLE(inode)) {
		error = permission(inode, MAY_WRITE, NULL);
		if (error)
			goto out;
	}

	error = gfs2_trans_begin(sdp, RES_DINODE, 0);
	if (error)
		goto out;
	error = gfs2_meta_inode_buffer(ip, &bh);
	if (error)
		goto out_trans_end;
	gfs2_trans_add_bh(ip->i_gl, bh, 1);
	ip->i_di.di_flags = new_flags;
	gfs2_dinode_out(ip, bh->b_data);
	brelse(bh);
	gfs2_set_inode_flags(inode);
out_trans_end:
	gfs2_trans_end(sdp);
out:
	gfs2_glock_dq_uninit(&gh);
	return error;
}

static int gfs2_set_flags(struct file *filp, u32 __user *ptr)
{
	u32 fsflags, gfsflags;
	if (get_user(fsflags, ptr))
		return -EFAULT;
	gfsflags = fsflags_cvt(fsflags_to_gfs2, fsflags);
	return do_gfs2_set_flags(filp, gfsflags, ~0);
}

static long gfs2_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch(cmd) {
	case FS_IOC_GETFLAGS:
		return gfs2_get_flags(filp, (u32 __user *)arg);
	case FS_IOC_SETFLAGS:
		return gfs2_set_flags(filp, (u32 __user *)arg);
	}
	return -ENOTTY;
}


/**
 * gfs2_mmap -
 * @file: The file to map
 * @vma: The VMA which described the mapping
 *
 * Returns: 0 or error code
 */

static int gfs2_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct gfs2_inode *ip = GFS2_I(file->f_mapping->host);
	struct gfs2_holder i_gh;
	int error;

	gfs2_holder_init(ip->i_gl, LM_ST_SHARED, GL_ATIME, &i_gh);
	error = gfs2_glock_nq_atime(&i_gh);
	if (error) {
		gfs2_holder_uninit(&i_gh);
		return error;
	}

	/* This is VM_MAYWRITE instead of VM_WRITE because a call
	   to mprotect() can turn on VM_WRITE later. */

	if ((vma->vm_flags & (VM_MAYSHARE | VM_MAYWRITE)) ==
	    (VM_MAYSHARE | VM_MAYWRITE))
		vma->vm_ops = &gfs2_vm_ops_sharewrite;
	else
		vma->vm_ops = &gfs2_vm_ops_private;

	gfs2_glock_dq_uninit(&i_gh);

	return error;
}

/**
 * gfs2_open - open a file
 * @inode: the inode to open
 * @file: the struct file for this opening
 *
 * Returns: errno
 */

static int gfs2_open(struct inode *inode, struct file *file)
{
	struct gfs2_inode *ip = GFS2_I(inode);
	struct gfs2_holder i_gh;
	struct gfs2_file *fp;
	int error;

	fp = kzalloc(sizeof(struct gfs2_file), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	mutex_init(&fp->f_fl_mutex);

	gfs2_assert_warn(GFS2_SB(inode), !file->private_data);
	file->private_data = fp;

	if (S_ISREG(ip->i_inode.i_mode)) {
		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED, LM_FLAG_ANY,
					   &i_gh);
		if (error)
			goto fail;

		if (!(file->f_flags & O_LARGEFILE) &&
		    ip->i_di.di_size > MAX_NON_LFS) {
			error = -EFBIG;
			goto fail_gunlock;
		}

		/* Listen to the Direct I/O flag */

		if (ip->i_di.di_flags & GFS2_DIF_DIRECTIO)
			file->f_flags |= O_DIRECT;

		gfs2_glock_dq_uninit(&i_gh);
	}

	return 0;

fail_gunlock:
	gfs2_glock_dq_uninit(&i_gh);
fail:
	file->private_data = NULL;
	kfree(fp);
	return error;
}

/**
 * gfs2_close - called to close a struct file
 * @inode: the inode the struct file belongs to
 * @file: the struct file being closed
 *
 * Returns: errno
 */

static int gfs2_close(struct inode *inode, struct file *file)
{
	struct gfs2_sbd *sdp = inode->i_sb->s_fs_info;
	struct gfs2_file *fp;

	fp = file->private_data;
	file->private_data = NULL;

	if (gfs2_assert_warn(sdp, fp))
		return -EIO;

	kfree(fp);

	return 0;
}

/**
 * gfs2_fsync - sync the dirty data for a file (across the cluster)
 * @file: the file that points to the dentry (we ignore this)
 * @dentry: the dentry that points to the inode to sync
 *
 * The VFS will flush "normal" data for us. We only need to worry
 * about metadata here. For journaled data, we just do a log flush
 * as we can't avoid it. Otherwise we can just bale out if datasync
 * is set. For stuffed inodes we must flush the log in order to
 * ensure that all data is on disk.
 *
 * The call to write_inode_now() is there to write back metadata and
 * the inode itself. It does also try and write the data, but thats
 * (hopefully) a no-op due to the VFS having already called filemap_fdatawrite()
 * for us.
 *
 * Returns: errno
 */

static int gfs2_fsync(struct file *file, struct dentry *dentry, int datasync)
{
	struct inode *inode = dentry->d_inode;
	int sync_state = inode->i_state & (I_DIRTY_SYNC|I_DIRTY_DATASYNC);
	int ret = 0;

	if (gfs2_is_jdata(GFS2_I(inode))) {
		gfs2_log_flush(GFS2_SB(inode), GFS2_I(inode)->i_gl);
		return 0;
	}

	if (sync_state != 0) {
		if (!datasync)
			ret = write_inode_now(inode, 0);

		if (gfs2_is_stuffed(GFS2_I(inode)))
			gfs2_log_flush(GFS2_SB(inode), GFS2_I(inode)->i_gl);
	}

	return ret;
}

/**
 * gfs2_setlease - acquire/release a file lease
 * @file: the file pointer
 * @arg: lease type
 * @fl: file lock
 *
 * Returns: errno
 */

static int gfs2_setlease(struct file *file, long arg, struct file_lock **fl)
{
	struct gfs2_sbd *sdp = GFS2_SB(file->f_mapping->host);

	/*
	 * We don't currently have a way to enforce a lease across the whole
	 * cluster; until we do, disable leases (by just returning -EINVAL),
	 * unless the administrator has requested purely local locking.
	 */
	if (!sdp->sd_args.ar_localflocks)
		return -EINVAL;
	return setlease(file, arg, fl);
}

/**
 * gfs2_lock - acquire/release a posix lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int gfs2_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_inode *ip = GFS2_I(file->f_mapping->host);
	struct gfs2_sbd *sdp = GFS2_SB(file->f_mapping->host);
	struct lm_lockname name =
		{ .ln_number = ip->i_no_addr,
		  .ln_type = LM_TYPE_PLOCK };

	if (!(fl->fl_flags & FL_POSIX))
		return -ENOLCK;
	if ((ip->i_inode.i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (sdp->sd_args.ar_localflocks) {
		if (IS_GETLK(cmd)) {
			posix_test_lock(file, fl);
			return 0;
		} else {
			return posix_lock_file_wait(file, fl);
		}
	}

	if (cmd == F_CANCELLK) {
		/* Hack: */
		cmd = F_SETLK;
		fl->fl_type = F_UNLCK;
	}
	if (IS_GETLK(cmd))
		return gfs2_lm_plock_get(sdp, &name, file, fl);
	else if (fl->fl_type == F_UNLCK)
		return gfs2_lm_punlock(sdp, &name, file, fl);
	else
		return gfs2_lm_plock(sdp, &name, file, cmd, fl);
}

static int do_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_file *fp = file->private_data;
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;
	struct gfs2_inode *ip = GFS2_I(file->f_path.dentry->d_inode);
	struct gfs2_glock *gl;
	unsigned int state;
	int flags;
	int error = 0;

	state = (fl->fl_type == F_WRLCK) ? LM_ST_EXCLUSIVE : LM_ST_SHARED;
	flags = (IS_SETLKW(cmd) ? 0 : LM_FLAG_TRY) | GL_EXACT | GL_NOCACHE;

	mutex_lock(&fp->f_fl_mutex);

	gl = fl_gh->gh_gl;
	if (gl) {
		if (fl_gh->gh_state == state)
			goto out;
		gfs2_glock_hold(gl);
		flock_lock_file_wait(file,
				     &(struct file_lock){.fl_type = F_UNLCK});
		gfs2_glock_dq_uninit(fl_gh);
	} else {
		error = gfs2_glock_get(GFS2_SB(&ip->i_inode),
				      ip->i_no_addr, &gfs2_flock_glops,
				      CREATE, &gl);
		if (error)
			goto out;
	}

	gfs2_holder_init(gl, state, flags, fl_gh);
	gfs2_glock_put(gl);

	error = gfs2_glock_nq(fl_gh);
	if (error) {
		gfs2_holder_uninit(fl_gh);
		if (error == GLR_TRYFAILED)
			error = -EAGAIN;
	} else {
		error = flock_lock_file_wait(file, fl);
		gfs2_assert_warn(GFS2_SB(&ip->i_inode), !error);
	}

out:
	mutex_unlock(&fp->f_fl_mutex);
	return error;
}

static void do_unflock(struct file *file, struct file_lock *fl)
{
	struct gfs2_file *fp = file->private_data;
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;

	mutex_lock(&fp->f_fl_mutex);
	flock_lock_file_wait(file, fl);
	if (fl_gh->gh_gl)
		gfs2_glock_dq_uninit(fl_gh);
	mutex_unlock(&fp->f_fl_mutex);
}

/**
 * gfs2_flock - acquire/release a flock lock on a file
 * @file: the file pointer
 * @cmd: either modify or retrieve lock state, possibly wait
 * @fl: type and range of lock
 *
 * Returns: errno
 */

static int gfs2_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct gfs2_inode *ip = GFS2_I(file->f_mapping->host);
	struct gfs2_sbd *sdp = GFS2_SB(file->f_mapping->host);

	if (!(fl->fl_flags & FL_FLOCK))
		return -ENOLCK;
	if ((ip->i_inode.i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return -ENOLCK;

	if (sdp->sd_args.ar_localflocks)
		return flock_lock_file_wait(file, fl);

	if (fl->fl_type == F_UNLCK) {
		do_unflock(file, fl);
		return 0;
	} else {
		return do_flock(file, cmd, fl);
	}
}

const struct file_operations gfs2_file_fops = {
	.llseek		= gfs2_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.unlocked_ioctl	= gfs2_ioctl,
	.mmap		= gfs2_mmap,
	.open		= gfs2_open,
	.release	= gfs2_close,
	.fsync		= gfs2_fsync,
	.lock		= gfs2_lock,
	.flock		= gfs2_flock,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
	.setlease	= gfs2_setlease,
};

const struct file_operations gfs2_dir_fops = {
	.readdir	= gfs2_readdir,
	.unlocked_ioctl	= gfs2_ioctl,
	.open		= gfs2_open,
	.release	= gfs2_close,
	.fsync		= gfs2_fsync,
	.lock		= gfs2_lock,
	.flock		= gfs2_flock,
};

