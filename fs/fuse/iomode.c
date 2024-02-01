// SPDX-License-Identifier: GPL-2.0
/*
 * FUSE inode io modes.
 *
 * Copyright (c) 2024 CTERA Networks.
 */

#include "fuse_i.h"

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/fs.h>

/*
 * Start cached io mode, where parallel dio writes are not allowed.
 */
int fuse_file_cached_io_start(struct inode *inode, struct fuse_file *ff)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	int err = 0;

	/* There are no io modes if server does not implement open */
	if (!ff->release_args)
		return 0;

	spin_lock(&fi->lock);
	if (fi->iocachectr < 0) {
		err = -ETXTBSY;
		goto unlock;
	}
	WARN_ON(ff->iomode == IOM_UNCACHED);
	if (ff->iomode == IOM_NONE) {
		ff->iomode = IOM_CACHED;
		if (fi->iocachectr == 0)
			set_bit(FUSE_I_CACHE_IO_MODE, &fi->state);
		fi->iocachectr++;
	}
unlock:
	spin_unlock(&fi->lock);
	return err;
}

static void fuse_file_cached_io_end(struct inode *inode, struct fuse_file *ff)
{
	struct fuse_inode *fi = get_fuse_inode(inode);

	spin_lock(&fi->lock);
	WARN_ON(fi->iocachectr <= 0);
	WARN_ON(ff->iomode != IOM_CACHED);
	ff->iomode = IOM_NONE;
	fi->iocachectr--;
	if (fi->iocachectr == 0)
		clear_bit(FUSE_I_CACHE_IO_MODE, &fi->state);
	spin_unlock(&fi->lock);
}

/* Start strictly uncached io mode where cache access is not allowed */
static int fuse_file_uncached_io_start(struct inode *inode, struct fuse_file *ff)
{
	struct fuse_inode *fi = get_fuse_inode(inode);
	int err = 0;

	spin_lock(&fi->lock);
	if (fi->iocachectr > 0) {
		err = -ETXTBSY;
		goto unlock;
	}
	WARN_ON(ff->iomode != IOM_NONE);
	fi->iocachectr--;
	ff->iomode = IOM_UNCACHED;
unlock:
	spin_unlock(&fi->lock);
	return err;
}

static void fuse_file_uncached_io_end(struct inode *inode, struct fuse_file *ff)
{
	struct fuse_inode *fi = get_fuse_inode(inode);

	spin_lock(&fi->lock);
	WARN_ON(fi->iocachectr >= 0);
	WARN_ON(ff->iomode != IOM_UNCACHED);
	ff->iomode = IOM_NONE;
	fi->iocachectr++;
	spin_unlock(&fi->lock);
}

/* Request access to submit new io to inode via open file */
int fuse_file_io_open(struct file *file, struct inode *inode)
{
	struct fuse_file *ff = file->private_data;
	int err;

	/*
	 * io modes are not relevant with DAX and with server that does not
	 * implement open.
	 */
	if (FUSE_IS_DAX(inode) || !ff->release_args)
		return 0;

	/*
	 * FOPEN_PARALLEL_DIRECT_WRITES requires FOPEN_DIRECT_IO.
	 */
	if (!(ff->open_flags & FOPEN_DIRECT_IO))
		ff->open_flags &= ~FOPEN_PARALLEL_DIRECT_WRITES;

	/*
	 * First parallel dio open denies caching inode io mode.
	 * First caching file open enters caching inode io mode.
	 *
	 * Note that if user opens a file open with O_DIRECT, but server did
	 * not specify FOPEN_DIRECT_IO, a later fcntl() could remove O_DIRECT,
	 * so we put the inode in caching mode to prevent parallel dio.
	 */
	if (ff->open_flags & FOPEN_DIRECT_IO) {
		if (ff->open_flags & FOPEN_PARALLEL_DIRECT_WRITES)
			err = fuse_file_uncached_io_start(inode, ff);
		else
			return 0;
	} else {
		err = fuse_file_cached_io_start(inode, ff);
	}
	if (err)
		goto fail;

	return 0;

fail:
	pr_debug("failed to open file in requested io mode (open_flags=0x%x, err=%i).\n",
		 ff->open_flags, err);
	/*
	 * The file open mode determines the inode io mode.
	 * Using incorrect open mode is a server mistake, which results in
	 * user visible failure of open() with EIO error.
	 */
	return -EIO;
}

/* No more pending io and no new io possible to inode via open/mmapped file */
void fuse_file_io_release(struct fuse_file *ff, struct inode *inode)
{
	/*
	 * Last parallel dio close allows caching inode io mode.
	 * Last caching file close exits caching inode io mode.
	 */
	switch (ff->iomode) {
	case IOM_NONE:
		/* Nothing to do */
		break;
	case IOM_UNCACHED:
		fuse_file_uncached_io_end(inode, ff);
		break;
	case IOM_CACHED:
		fuse_file_cached_io_end(inode, ff);
		break;
	}
}
