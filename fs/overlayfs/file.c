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
	ovl_revert_creds(old_cred);

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

struct ovl_file {
	struct file *realfile;
	struct file *upperfile;
};

struct ovl_file *ovl_file_alloc(struct file *realfile)
{
	struct ovl_file *of = kzalloc(sizeof(struct ovl_file), GFP_KERNEL);

	if (unlikely(!of))
		return NULL;

	of->realfile = realfile;
	return of;
}

void ovl_file_free(struct ovl_file *of)
{
	fput(of->realfile);
	if (of->upperfile)
		fput(of->upperfile);
	kfree(of);
}

static bool ovl_is_real_file(const struct file *realfile,
			     const struct path *realpath)
{
	return file_inode(realfile) == d_inode(realpath->dentry);
}

static struct file *ovl_real_file_path(const struct file *file,
				       struct path *realpath)
{
	struct ovl_file *of = file->private_data;
	struct file *realfile = of->realfile;

	if (WARN_ON_ONCE(!realpath->dentry))
		return ERR_PTR(-EIO);

	/*
	 * If the realfile that we want is not where the data used to be at
	 * open time, either we'd been copied up, or it's an fsync of a
	 * metacopied file.  We need the upperfile either way, so see if it
	 * is already opened and if it is not then open and store it.
	 */
	if (unlikely(!ovl_is_real_file(realfile, realpath))) {
		struct file *upperfile = READ_ONCE(of->upperfile);
		struct file *old;

		if (!upperfile) { /* Nobody opened upperfile yet */
			upperfile = ovl_open_realfile(file, realpath);
			if (IS_ERR(upperfile))
				return upperfile;

			/* Store the upperfile for later */
			old = cmpxchg_release(&of->upperfile, NULL, upperfile);
			if (old) { /* Someone opened upperfile before us */
				fput(upperfile);
				upperfile = old;
			}
		}
		/*
		 * Stored file must be from the right inode, unless someone's
		 * been corrupting the upper layer.
		 */
		if (WARN_ON_ONCE(!ovl_is_real_file(upperfile, realpath)))
			return ERR_PTR(-EIO);

		realfile = upperfile;
	}

	/* Did the flags change since open? */
	if (unlikely((file->f_flags ^ realfile->f_flags) & ~OVL_OPEN_FLAGS)) {
		int err = ovl_change_flags(realfile, file->f_flags);

		if (err)
			return ERR_PTR(err);
	}

	return realfile;
}

static struct file *ovl_real_file(const struct file *file)
{
	struct dentry *dentry = file_dentry(file);
	struct path realpath;
	int err;

	if (d_is_dir(dentry)) {
		struct file *f = ovl_dir_real_file(file, false);

		if (WARN_ON_ONCE(!f))
			return ERR_PTR(-EIO);
		return f;
	}

	/* lazy lookup and verify of lowerdata */
	err = ovl_verify_lowerdata(dentry);
	if (err)
		return ERR_PTR(err);

	ovl_path_realdata(dentry, &realpath);

	return ovl_real_file_path(file, &realpath);
}

static int ovl_open(struct inode *inode, struct file *file)
{
	struct dentry *dentry = file_dentry(file);
	struct file *realfile;
	struct path realpath;
	struct ovl_file *of;
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

	of = ovl_file_alloc(realfile);
	if (!of) {
		fput(realfile);
		return -ENOMEM;
	}

	file->private_data = of;

	return 0;
}

static int ovl_release(struct inode *inode, struct file *file)
{
	ovl_file_free(file->private_data);
	return 0;
}

static loff_t ovl_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file_inode(file);
	struct file *realfile;
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

	realfile = ovl_real_file(file);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);

	/*
	 * Overlay file f_pos is the master copy that is preserved
	 * through copy up and modified on read/write, but only real
	 * fs knows how to SEEK_HOLE/SEEK_DATA and real fs may impose
	 * limitations that are more strict than ->s_maxbytes for specific
	 * files, so we use the real file to perform seeks.
	 */
	ovl_inode_lock(inode);
	realfile->f_pos = file->f_pos;

	old_cred = ovl_override_creds(inode->i_sb);
	ret = vfs_llseek(realfile, offset, whence);
	ovl_revert_creds(old_cred);

	file->f_pos = realfile->f_pos;
	ovl_inode_unlock(inode);

	return ret;
}

static void ovl_file_modified(struct file *file)
{
	/* Update size/mtime */
	ovl_copyattr(file_inode(file));
}

static void ovl_file_end_write(struct kiocb *iocb, ssize_t ret)
{
	ovl_file_modified(iocb->ki_filp);
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
	struct file *realfile;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(file_inode(file)->i_sb),
		.accessed = ovl_file_accessed,
	};

	if (!iov_iter_count(iter))
		return 0;

	realfile = ovl_real_file(file);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);

	return backing_file_read_iter(realfile, iter, iocb, iocb->ki_flags,
				      &ctx);
}

static ssize_t ovl_write_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct file *realfile;
	ssize_t ret;
	int ifl = iocb->ki_flags;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(inode->i_sb),
		.end_write = ovl_file_end_write,
	};

	if (!iov_iter_count(iter))
		return 0;

	inode_lock(inode);
	/* Update mode */
	ovl_copyattr(inode);

	realfile = ovl_real_file(file);
	ret = PTR_ERR(realfile);
	if (IS_ERR(realfile))
		goto out_unlock;

	if (!ovl_should_sync(OVL_FS(inode->i_sb)))
		ifl &= ~(IOCB_DSYNC | IOCB_SYNC);

	/*
	 * Overlayfs doesn't support deferred completions, don't copy
	 * this property in case it is set by the issuer.
	 */
	ifl &= ~IOCB_DIO_CALLER_COMP;
	ret = backing_file_write_iter(realfile, iter, iocb, ifl, &ctx);

out_unlock:
	inode_unlock(inode);

	return ret;
}

static ssize_t ovl_splice_read(struct file *in, loff_t *ppos,
			       struct pipe_inode_info *pipe, size_t len,
			       unsigned int flags)
{
	struct file *realfile;
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(file_inode(in)->i_sb),
		.accessed = ovl_file_accessed,
	};
	struct kiocb iocb;

	realfile = ovl_real_file(in);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);

	init_sync_kiocb(&iocb, in);
	iocb.ki_pos = *ppos;
	ret = backing_file_splice_read(realfile, &iocb, pipe, len, flags, &ctx);
	*ppos = iocb.ki_pos;

	return ret;
}

/*
 * Calling iter_file_splice_write() directly from overlay's f_op may deadlock
 * due to lock order inversion between pipe->mutex in iter_file_splice_write()
 * and file_start_write(realfile) in ovl_write_iter().
 *
 * So do everything ovl_write_iter() does and call iter_file_splice_write() on
 * the real file.
 */
static ssize_t ovl_splice_write(struct pipe_inode_info *pipe, struct file *out,
				loff_t *ppos, size_t len, unsigned int flags)
{
	struct file *realfile;
	struct inode *inode = file_inode(out);
	ssize_t ret;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(inode->i_sb),
		.end_write = ovl_file_end_write,
	};
	struct kiocb iocb;

	inode_lock(inode);
	/* Update mode */
	ovl_copyattr(inode);

	realfile = ovl_real_file(out);
	ret = PTR_ERR(realfile);
	if (IS_ERR(realfile))
		goto out_unlock;

	init_sync_kiocb(&iocb, out);
	iocb.ki_pos = *ppos;
	ret = backing_file_splice_write(pipe, realfile, &iocb, len, flags, &ctx);
	*ppos = iocb.ki_pos;

out_unlock:
	inode_unlock(inode);

	return ret;
}

static int ovl_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	struct dentry *dentry = file_dentry(file);
	enum ovl_path_type type;
	struct path upperpath;
	struct file *upperfile;
	const struct cred *old_cred;
	int ret;

	ret = ovl_sync_status(OVL_FS(file_inode(file)->i_sb));
	if (ret <= 0)
		return ret;

	/* Don't sync lower file for fear of receiving EROFS error */
	type = ovl_path_type(dentry);
	if (!OVL_TYPE_UPPER(type) || (datasync && OVL_TYPE_MERGE(type)))
		return 0;

	ovl_path_upper(dentry, &upperpath);
	upperfile = ovl_real_file_path(file, &upperpath);
	if (IS_ERR(upperfile))
		return PTR_ERR(upperfile);

	old_cred = ovl_override_creds(file_inode(file)->i_sb);
	ret = vfs_fsync_range(upperfile, start, end, datasync);
	ovl_revert_creds(old_cred);

	return ret;
}

static int ovl_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ovl_file *of = file->private_data;
	struct backing_file_ctx ctx = {
		.cred = ovl_creds(file_inode(file)->i_sb),
		.accessed = ovl_file_accessed,
	};

	return backing_file_mmap(of->realfile, vma, &ctx);
}

static long ovl_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	struct inode *inode = file_inode(file);
	struct file *realfile;
	const struct cred *old_cred;
	int ret;

	inode_lock(inode);
	/* Update mode */
	ovl_copyattr(inode);
	ret = file_remove_privs(file);
	if (ret)
		goto out_unlock;

	realfile = ovl_real_file(file);
	ret = PTR_ERR(realfile);
	if (IS_ERR(realfile))
		goto out_unlock;

	old_cred = ovl_override_creds(file_inode(file)->i_sb);
	ret = vfs_fallocate(realfile, mode, offset, len);
	ovl_revert_creds(old_cred);

	/* Update size */
	ovl_file_modified(file);

out_unlock:
	inode_unlock(inode);

	return ret;
}

static int ovl_fadvise(struct file *file, loff_t offset, loff_t len, int advice)
{
	struct file *realfile;
	const struct cred *old_cred;
	int ret;

	realfile = ovl_real_file(file);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);

	old_cred = ovl_override_creds(file_inode(file)->i_sb);
	ret = vfs_fadvise(realfile, offset, len, advice);
	ovl_revert_creds(old_cred);

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
	struct file *realfile_in, *realfile_out;
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

	realfile_out = ovl_real_file(file_out);
	ret = PTR_ERR(realfile_out);
	if (IS_ERR(realfile_out))
		goto out_unlock;

	realfile_in = ovl_real_file(file_in);
	ret = PTR_ERR(realfile_in);
	if (IS_ERR(realfile_in))
		goto out_unlock;

	old_cred = ovl_override_creds(file_inode(file_out)->i_sb);
	switch (op) {
	case OVL_COPY:
		ret = vfs_copy_file_range(realfile_in, pos_in,
					  realfile_out, pos_out, len, flags);
		break;

	case OVL_CLONE:
		ret = vfs_clone_file_range(realfile_in, pos_in,
					   realfile_out, pos_out, len, flags);
		break;

	case OVL_DEDUPE:
		ret = vfs_dedupe_file_range_one(realfile_in, pos_in,
						realfile_out, pos_out, len,
						flags);
		break;
	}
	ovl_revert_creds(old_cred);

	/* Update size */
	ovl_file_modified(file_out);

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
	struct file *realfile;
	const struct cred *old_cred;
	int err = 0;

	realfile = ovl_real_file(file);
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);

	if (realfile->f_op->flush) {
		old_cred = ovl_override_creds(file_inode(file)->i_sb);
		err = realfile->f_op->flush(realfile, id);
		ovl_revert_creds(old_cred);
	}

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
