/*
 *  linux/fs/ext4/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 *	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd2.h>
#include <linux/ext4_fs.h>
#include <linux/ext4_jbd2.h>
#include "xattr.h"
#include "acl.h"

/*
 * Called when an inode is released. Note that this is different
 * from ext4_file_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ext4_release_file (struct inode * inode, struct file * filp)
{
	/* if we are the last writer on the inode, drop the block reservation */
	if ((filp->f_mode & FMODE_WRITE) &&
			(atomic_read(&inode->i_writecount) == 1))
	{
		mutex_lock(&EXT4_I(inode)->truncate_mutex);
		ext4_discard_reservation(inode);
		mutex_unlock(&EXT4_I(inode)->truncate_mutex);
	}
	if (is_dx(inode) && filp->private_data)
		ext4_htree_free_dir_info(filp->private_data);

	return 0;
}

static ssize_t
ext4_file_write(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_path.dentry->d_inode;
	ssize_t ret;
	int err;

	ret = generic_file_aio_write(iocb, iov, nr_segs, pos);

	/*
	 * Skip flushing if there was an error, or if nothing was written.
	 */
	if (ret <= 0)
		return ret;

	/*
	 * If the inode is IS_SYNC, or is O_SYNC and we are doing data
	 * journalling then we need to make sure that we force the transaction
	 * to disk to keep all metadata uptodate synchronously.
	 */
	if (file->f_flags & O_SYNC) {
		/*
		 * If we are non-data-journaled, then the dirty data has
		 * already been flushed to backing store by generic_osync_inode,
		 * and the inode has been flushed too if there have been any
		 * modifications other than mere timestamp updates.
		 *
		 * Open question --- do we care about flushing timestamps too
		 * if the inode is IS_SYNC?
		 */
		if (!ext4_should_journal_data(inode))
			return ret;

		goto force_commit;
	}

	/*
	 * So we know that there has been no forced data flush.  If the inode
	 * is marked IS_SYNC, we need to force one ourselves.
	 */
	if (!IS_SYNC(inode))
		return ret;

	/*
	 * Open question #2 --- should we force data to disk here too?  If we
	 * don't, the only impact is that data=writeback filesystems won't
	 * flush data to disk automatically on IS_SYNC, only metadata (but
	 * historically, that is what ext2 has done.)
	 */

force_commit:
	err = ext4_force_commit(inode->i_sb);
	if (err)
		return err;
	return ret;
}

const struct file_operations ext4_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= ext4_file_write,
	.ioctl		= ext4_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext4_compat_ioctl,
#endif
	.mmap		= generic_file_mmap,
	.open		= generic_file_open,
	.release	= ext4_release_file,
	.fsync		= ext4_sync_file,
	.sendfile	= generic_file_sendfile,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
};

struct inode_operations ext4_file_inode_operations = {
	.truncate	= ext4_truncate,
	.setattr	= ext4_setattr,
#ifdef CONFIG_EXT4DEV_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= ext4_listxattr,
	.removexattr	= generic_removexattr,
#endif
	.permission	= ext4_permission,
};

