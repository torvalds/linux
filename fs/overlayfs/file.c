// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 Red Hat, Inc.
 */

#include <linux/cred.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/xattr.h>
#include <linux/uio.h>
#include <linux/uaccess.h>
#include <linux/security.h>
#include <linux/fs.h>
#include <linux/backing-file.h>
#include "overlayfs.h"

static char ovl_whatisit(struct inode *inode, struct inode *realinode)
{
	if (realinode != ovl_inode_upper(inode))
		return 'l';
	if (ovl_has_upperdata(inode))
		return 'u';
	else
		return 'm';
}

static struct file *ovl_open_realfile(const struct file *file,
				      const struct path *realpath)
{
	struct inode *realinode = d_inode(realpath->dentry);
	struct inode *inode = file_inode(file);
	struct mnt_idmap *real_idmap;
	struct file *realfile;
	const struct cred *old_cred;
	int flags = file->f_flags | OVL_OPEN_FLAGS;
	int acc_mode = ACC_MODE(flags);
	int err;

	if (flags & O_APPEND)
		acc_mode |= MAY_APPEND;

	old_cred = ovl_override_creds(inode->i_sb);
	real_idmap = mnt_idmap(realpath->mnt);
	err = inode_permission(real_idmap, realinode, MAY_OPEN | acc_mode);
	if (err) {
		realfile = ERR_PTR(err);
	} else {
		if (!inode_owner_or_capable(real_idmap, realinode))
			flags &= ~O_NOATIME;

		realfile = backing_file_open(&file->f_path, flags, realpath,
					     current_cred());
	}
	revert_creds(old_cred);

	pr_debug("open(%p[%pD2/%c], 0%o) -> (%p, 0%o)\n",
		 file, file, ovl_whatisit(inode, realinode), file->f_flags,
		 realfile, IS_ERR(realfile) ? 0 : realfile->f_flags);

	return realfile;
}

#define OVL_SETFL_MASK (O_APPEND | O_NONBLOCK | O_NDELAY | O_DIRECT)

static int ovl_change_flags(struct file *file, unsigned int flags)
{
	struct inode *inode = file_inode(file);
	int err;

	flags &= OVL_SETFL_MASK;

	if (((flags ^ file->f_flags) & O_APPEND) && IS_APPEND(inode))
		return -EPERM;

	if ((flags & O_DIRECT) && !(file->f_mode & FMODE_CAN_ODIRECT))
		return -EINVAL;

	if (file->f_op->check_flags) {
		err = file->f_op->check_flags(flags);
		if (err)
			return err;
	}

	spin_lock(&file->f_lock);
	file->f_flags = (file->f_flags & ~OVL_SETFL_MASK) | flags;
	file->f_iocb_flags = iocb_flags(file);
	spin_unlock(&file->f_lock);

	return 0;
}

static int ovl_real_fdget_meta(const struct file *file, struct fd *real,
			       bool allow_meta)
{
	struct dentry *dentry = file_dentry(file);
	struct file *realfile = file->private_data;
	struct path realpath;
	int err;

	real->word = (unsigned long)realfile;

	if (allow_meta) {
		ovl_path_real(dentry, &realpath);
	} else {
		/* lazy lookup and verify of lowerdata */
		err = ovl_verify_lowerdata(dentry);
		if (err)
			return err;

		ovl_path_realdata(dentry, &realpath);
	}
	if (!realpath.dentry)
		return -EIO;

	/* Has it been copied up since we'd opened it? */
	if (unlikely(file_inode(realfile) != d_inode(realpath.dentry))) {
		struct file *f = ovl_open_realfile(file, &realpath);
		if (IS_ERR(f))
			return PTR_ERR(f);
		real->word = (unsigned long)f | FDPUT_FPUT;
		return 0;
	}

	/* Did the flags change since open? */
	if (unlikely((file->f_flags ^ realfile->f_flags) & ~OVL_OPEN_FLAGS))
		return ovl_change_flags(realfile, file->f_flags);

	return 0;
}

static int ovl_real_fdget(const struct file *file, struct fd *real)
{
	if (d_is_dir(file_dentry(file))) {
		struct file *f = ovl_dir_real_file(file, false);
		if (IS_ERR(f))
			return PTR_ERR(f);
		real->word = (unsigned long)f;
		return 0;
	}

	return ovl_real_fdget_meta(file, real, false);
}

static int ovl_open(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file_dentry(file);
	struct file *realfile;
	struct path realpath;
	int err;

	/* lazy lookup and verify lowerdata */
	err = ovl_verify_lowerdata(dentry);
	if (err)
		return err;

	err = ovl_maybe_copy_up(dentry, file->f_flags);
	if (err)
		return err;

	/* No longer need these flags, so don't pass them on to underlying fs */
	file->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	ovl_path_realdata(dentry, &realpath);
	if (!realpath.dentry)
		return -EIO;

	realfile = ovl_open_realfile(file, &realpath);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);

	file->private_data = realfile;

	return 0;
}

static int ovl_release(struct inode *inode, struct file *file)
{
	fput(file->private_data);

	return 0;
}

static loff_t ovl_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file_inode(file);
	struct fd real;
	const struct cred *old_cred;
	loff_t ret;

	/*
	 * The two special cases below do not need to involve real fs,
	 * so we can optimizing concurrent callers.
	 */
	if (offset == 0) {
		if (whence == SEEK_CUR)
			return file->f_pos;

		if (whence == SEEK_SET)
			return vfs_setpos(file, 0, 0);
	}

	ret = ovl_real_fdget(file, &real);
	if (ret)
		return ret;

	/*
	 * Overlay file f_pos is the master copy that is preserved
	 * through copy up and modified on read/write, but only real
	 * fs knows how to SEEK_HOLE/SEEK_DATA and real fs may impose
	 * limitations that are more strict than ->s_maxbytes for specific
	 * files, so we use the real file to perform seeks.
	 */
	ovl_inode_lock(inode);
	fd_file(real)->f_pos = file->f_pos;

	old_cred = ovl_override_creds(inode->i_sb);
	ret = vfs_llseek(fd_file(real), offset, whence);
	revert_creds(old_cred);

	file->f_pos = fd_file(real)->f_pos;
	ovl_inode_unlock(inode);

	fdput(real);

	return ret;
}

static void ovl_file_modified(struct file *file)
{
	/* Update size/mtime */
	ovl_copyattr(file_inode(file));
}

static void ovl_file_end_write(struct file *file, loff_t pos, ssize_t ret)
{
	ovl_file_modified(file);
}

static void ovl_file_accessed(struct file *file)
{
	struct inode *inode, *upperinode;
	struct timespec64 ctime, uctime;
	struct timespec64 mtime, umtime;

	if (file->f_flags & O_NOATIME)
		return;

	inode = file_inode(file);
	upperinode = ovl_inode_upper(inode);

	if (!upperinode)
		return;

	ctime = inode_get_ctime(inode);
	uctime = inode_get_ctime(upperinode);
	mtime = inode_get_mtime(inode);
	umtime = inode_get_mtime(upperinode);
	if ((!timespec64_equal(&mtime, &umtime)) ||
	     !timespec64_equal(&ctime, &uctime)) {
		inode_set_mtime_to_ts(inode, inode_get_mtime(upperinode));
		inode_set_ctime_to_ts(inode, uctime);
	}

	touch_atime(&file->f_path);
}

static ssize_t ovl_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct fd real;
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(file_inode(file)->i_sb),
		.user_file = file,
		.accessed = ovl_file_accessed,
	};

	if (!iov_iter_count(iter))
		return 0;

	ret = ovl_real_fdget(file, &real);
	if (ret)
		return ret;

	ret = backing_file_read_iter(fd_file(real), iter, iocb, iocb->ki_flags,
				     &ctx);
	fdput(real);

	return ret;
}

static ssize_t ovl_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct fd real;
	ssize_t ret;
	int ifl = iocb->ki_flags;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(inode->i_sb),
		.user_file = file,
		.end_write = ovl_file_end_write,
	};

	if (!iov_iter_count(iter))
		return 0;

	inode_lock(inode);
	/* Update mode */
	ovl_copyattr(inode);

	ret = ovl_real_fdget(file, &real);
	if (ret)
		goto out_unlock;

	if (!ovl_should_sync(OVL_FS(inode->i_sb)))
		ifl &= ~(IOCB_DSYNC | IOCB_SYNC);

	/*
	 * Overlayfs doesn't support deferred completions, don't copy
	 * this property in case it is set by the issuer.
	 */
	ifl &= ~IOCB_DIO_CALLER_COMP;
	ret = backing_file_write_iter(fd_file(real), iter, iocb, ifl, &ctx);
	fdput(real);

out_unlock:
	inode_unlock(inode);

	return ret;
}

static ssize_t ovl_splice_read(struct file *in, loff_t *ppos,
			       struct pipe_inode_info *pipe, size_t len,
			       unsigned int flags)
{
	struct fd real;
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(file_inode(in)->i_sb),
		.user_file = in,
		.accessed = ovl_file_accessed,
	};

	ret = ovl_real_fdget(in, &real);
	if (ret)
		return ret;

	ret = backing_file_splice_read(fd_file(real), ppos, pipe, len, flags, &ctx);
	fdput(real);

	return ret;
}

/*
 * Calling iter_file_splice_write() directly from overlay's f_op may deadlock
 * due to lock order inversion between pipe->mutex in iter_file_splice_write()
 * and file_start_write(fd_file(real)) in ovl_write_iter().
 *
 * So do everything ovl_write_iter() does and call iter_file_splice_write() on
 * the real file.
 */
static ssize_t ovl_splice_write(struct pipe_inode_info *pipe, struct file *out,
				loff_t *ppos, size_t len, unsigned int flags)
{
	struct fd real;
	struct inode *inode = file_inode(out);
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(inode->i_sb),
		.user_file = out,
		.end_write = ovl_file_end_write,
	};

	inode_lock(inode);
	/* Update mode */
	ovl_copyattr(inode);

	ret = ovl_real_fdget(out, &real);
	if (ret)
		goto out_unlock;

	ret = backing_file_splice_write(pipe, fd_file(real), ppos, len, flags, &ctx);
	fdput(real);

out_unlock:
	inode_unlock(inode);

	return ret;
}

static int ovl_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct fd real;
	const struct cred *old_cred;
	int ret;

	ret = ovl_sync_status(OVL_FS(file_inode(file)->i_sb));
	if (ret <= 0)
		return ret;

	ret = ovl_real_fdget_meta(file, &real, !datasync);
	if (ret)
		return ret;

	/* Don't sync lower file for fear of receiving EROFS error */
	if (file_inode(fd_file(real)) == ovl_inode_upper(file_inode(file))) {
		old_cred = ovl_override_creds(file_inode(file)->i_sb);
		ret = vfs_fsync_range(fd_file(real), start, end, datasync);
		revert_creds(old_cred);
	}

	fdput(real);

	return ret;
}

static int ovl_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct file *realfile = file->private_data;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(file_inode(file)->i_sb),
		.user_file = file,
		.accessed = ovl_file_accessed,
	};

	return backing_file_mmap(realfile, vma, &ctx);
}

static long ovl_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	struct fd real;
	const struct cred *old_cred;
	int ret;

	inode_lock(inode);
	/* Update mode */
	ovl_copyattr(inode);
	ret = file_remove_privs(file);
	if (ret)
		goto out_unlock;

	ret = ovl_real_fdget(file, &real);
	if (ret)
		goto out_unlock;

	old_cred = ovl_override_creds(file_inode(file)->i_sb);
	ret = vfs_fallocate(fd_file(real), mode, offset, len);
	revert_creds(old_cred);

	/* Update size */
	ovl_file_modified(file);

	fdput(real);

out_unlock:
	inode_unlock(inode);

	return ret;
}

static int ovl_fadvise(struct file *file, loff_t offset, loff_t len, int advice)
{
	struct fd real;
	const struct cred *old_cred;
	int ret;

	ret = ovl_real_fdget(file, &real);
	if (ret)
		return ret;

	old_cred = ovl_override_creds(file_inode(file)->i_sb);
	ret = vfs_fadvise(fd_file(real), offset, len, advice);
	revert_creds(old_cred);

	fdput(real);

	return ret;
}

enum ovl_copyop {
	OVL_COPY,
	OVL_CLONE,
	OVL_DEDUPE,
};

static loff_t ovl_copyfile(struct file *file_in, loff_t pos_in,
			    struct file *file_out, loff_t pos_out,
			    loff_t len, unsigned int flags, enum ovl_copyop op)
{
	struct inode *inode_out = file_inode(file_out);
	struct fd real_in, real_out;
	const struct cred *old_cred;
	loff_t ret;

	inode_lock(inode_out);
	if (op != OVL_DEDUPE) {
		/* Update mode */
		ovl_copyattr(inode_out);
		ret = file_remove_privs(file_out);
		if (ret)
			goto out_unlock;
	}

	ret = ovl_real_fdget(file_out, &real_out);
	if (ret)
		goto out_unlock;

	ret = ovl_real_fdget(file_in, &real_in);
	if (ret) {
		fdput(real_out);
		goto out_unlock;
	}

	old_cred = ovl_override_creds(file_inode(file_out)->i_sb);
	switch (op) {
	case OVL_COPY:
		ret = vfs_copy_file_range(fd_file(real_in), pos_in,
					  fd_file(real_out), pos_out, len, flags);
		break;

	case OVL_CLONE:
		ret = vfs_clone_file_range(fd_file(real_in), pos_in,
					   fd_file(real_out), pos_out, len, flags);
		break;

	case OVL_DEDUPE:
		ret = vfs_dedupe_file_range_one(fd_file(real_in), pos_in,
						fd_file(real_out), pos_out, len,
						flags);
		break;
	}
	revert_creds(old_cred);

	/* Update size */
	ovl_file_modified(file_out);

	fdput(real_in);
	fdput(real_out);

out_unlock:
	inode_unlock(inode_out);

	return ret;
}

static ssize_t ovl_copy_file_range(struct file *file_in, loff_t pos_in,
				   struct file *file_out, loff_t pos_out,
				   size_t len, unsigned int flags)
{
	return ovl_copyfile(file_in, pos_in, file_out, pos_out, len, flags,
			    OVL_COPY);
}

static loff_t ovl_remap_file_range(struct file *file_in, loff_t pos_in,
				   struct file *file_out, loff_t pos_out,
				   loff_t len, unsigned int remap_flags)
{
	enum ovl_copyop op;

	if (remap_flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_ADVISORY))
		return -EINVAL;

	if (remap_flags & REMAP_FILE_DEDUP)
		op = OVL_DEDUPE;
	else
		op = OVL_CLONE;

	/*
	 * Don't copy up because of a dedupe request, this wouldn't make sense
	 * most of the time (data would be duplicated instead of deduplicated).
	 */
	if (op == OVL_DEDUPE &&
	    (!ovl_inode_upper(file_inode(file_in)) ||
	     !ovl_inode_upper(file_inode(file_out))))
		return -EPERM;

	return ovl_copyfile(file_in, pos_in, file_out, pos_out, len,
			    remap_flags, op);
}

static int ovl_flush(struct file *file, fl_owner_t id)
{
	struct fd real;
	const struct cred *old_cred;
	int err;

	err = ovl_real_fdget(file, &real);
	if (err)
		return err;

	if (fd_file(real)->f_op->flush) {
		old_cred = ovl_override_creds(file_inode(file)->i_sb);
		err = fd_file(real)->f_op->flush(fd_file(real), id);
		revert_creds(old_cred);
	}
	fdput(real);

	return err;
}

const struct file_operations ovl_file_operations = {
	.open		= ovl_open,
	.release	= ovl_release,
	.llseek		= ovl_llseek,
	.read_iter	= ovl_read_iter,
	.write_iter	= ovl_write_iter,
	.fsync		= ovl_fsync,
	.mmap		= ovl_mmap,
	.fallocate	= ovl_fallocate,
	.fadvise	= ovl_fadvise,
	.flush		= ovl_flush,
	.splice_read    = ovl_splice_read,
	.splice_write   = ovl_splice_write,

	.copy_file_range	= ovl_copy_file_range,
	.remap_file_range	= ovl_remap_file_range,
};
