/*
 * linux/fs/ocfs2/ioctl.c
 *
 * Copyright (C) 2006 Herbert Poetzl
 * adapted from Remy Card's ext2/ioctl.c
 */

#include <linux/fs.h>
#include <linux/mount.h>

#define MLOG_MASK_PREFIX ML_INODE
#include <cluster/masklog.h>

#include "ocfs2.h"
#include "alloc.h"
#include "dlmglue.h"
#include "inode.h"
#include "journal.h"

#include "ocfs2_fs.h"
#include "ioctl.h"

#include <linux/ext2_fs.h>

static int ocfs2_get_inode_attr(struct inode *inode, unsigned *flags)
{
	int status;

	status = ocfs2_meta_lock(inode, NULL, 0);
	if (status < 0) {
		mlog_errno(status);
		return status;
	}
	*flags = OCFS2_I(inode)->ip_attr;
	ocfs2_meta_unlock(inode, 0);

	mlog_exit(status);
	return status;
}

static int ocfs2_set_inode_attr(struct inode *inode, unsigned flags,
				unsigned mask)
{
	struct ocfs2_inode_info *ocfs2_inode = OCFS2_I(inode);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_journal_handle *handle = NULL;
	struct buffer_head *bh = NULL;
	unsigned oldflags;
	int status;

	mutex_lock(&inode->i_mutex);

	status = ocfs2_meta_lock(inode, &bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = -EROFS;
	if (IS_RDONLY(inode))
		goto bail_unlock;

	status = -EACCES;
	if ((current->fsuid != inode->i_uid) && !capable(CAP_FOWNER))
		goto bail_unlock;

	if (!S_ISDIR(inode->i_mode))
		flags &= ~OCFS2_DIRSYNC_FL;

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto bail_unlock;
	}

	oldflags = ocfs2_inode->ip_attr;
	flags = flags & mask;
	flags |= oldflags & ~mask;

	/*
	 * The IMMUTABLE and APPEND_ONLY flags can only be changed by
	 * the relevant capability.
	 */
	status = -EPERM;
	if ((oldflags & OCFS2_IMMUTABLE_FL) || ((flags ^ oldflags) &
		(OCFS2_APPEND_FL | OCFS2_IMMUTABLE_FL))) {
		if (!capable(CAP_LINUX_IMMUTABLE))
			goto bail_unlock;
	}

	ocfs2_inode->ip_attr = flags;
	ocfs2_set_inode_flags(inode);

	status = ocfs2_mark_inode_dirty(handle, inode, bh);
	if (status < 0)
		mlog_errno(status);

	ocfs2_commit_trans(osb, handle);
bail_unlock:
	ocfs2_meta_unlock(inode, 1);
bail:
	mutex_unlock(&inode->i_mutex);

	if (bh)
		brelse(bh);

	mlog_exit(status);
	return status;
}

int ocfs2_ioctl(struct inode * inode, struct file * filp,
	unsigned int cmd, unsigned long arg)
{
	unsigned int flags;
	int status;

	switch (cmd) {
	case OCFS2_IOC_GETFLAGS:
		status = ocfs2_get_inode_attr(inode, &flags);
		if (status < 0)
			return status;

		flags &= OCFS2_FL_VISIBLE;
		return put_user(flags, (int __user *) arg);
	case OCFS2_IOC_SETFLAGS:
		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		return ocfs2_set_inode_attr(inode, flags,
			OCFS2_FL_MODIFIABLE);
	default:
		return -ENOTTY;
	}
}

