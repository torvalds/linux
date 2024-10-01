// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * locks.c
 *
 * Userspace file locking support
 *
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/fcntl.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "dlmglue.h"
#include "file.h"
#include "inode.h"
#include "locks.h"

static int ocfs2_do_flock(struct file *file, struct inode *inode,
			  int cmd, struct file_lock *fl)
{
	int ret = 0, level = 0, trylock = 0;
	struct ocfs2_file_private *fp = file->private_data;
	struct ocfs2_lock_res *lockres = &fp->fp_flock;

	if (lock_is_write(fl))
		level = 1;
	if (!IS_SETLKW(cmd))
		trylock = 1;

	mutex_lock(&fp->fp_mutex);

	if (lockres->l_flags & OCFS2_LOCK_ATTACHED &&
	    lockres->l_level > LKM_NLMODE) {
		int old_level = 0;
		struct file_lock request;

		if (lockres->l_level == LKM_EXMODE)
			old_level = 1;

		if (level == old_level)
			goto out;

		/*
		 * Converting an existing lock is not guaranteed to be
		 * atomic, so we can get away with simply unlocking
		 * here and allowing the lock code to try at the new
		 * level.
		 */

		locks_init_lock(&request);
		request.c.flc_type = F_UNLCK;
		request.c.flc_flags = FL_FLOCK;
		locks_lock_file_wait(file, &request);

		ocfs2_file_unlock(file);
	}

	ret = ocfs2_file_lock(file, level, trylock);
	if (ret) {
		if (ret == -EAGAIN && trylock)
			ret = -EWOULDBLOCK;
		else
			mlog_errno(ret);
		goto out;
	}

	ret = locks_lock_file_wait(file, fl);
	if (ret)
		ocfs2_file_unlock(file);

out:
	mutex_unlock(&fp->fp_mutex);

	return ret;
}

static int ocfs2_do_funlock(struct file *file, int cmd, struct file_lock *fl)
{
	int ret;
	struct ocfs2_file_private *fp = file->private_data;

	mutex_lock(&fp->fp_mutex);
	ocfs2_file_unlock(file);
	ret = locks_lock_file_wait(file, fl);
	mutex_unlock(&fp->fp_mutex);

	return ret;
}

/*
 * Overall flow of ocfs2_flock() was influenced by gfs2_flock().
 */
int ocfs2_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct inode *inode = file->f_mapping->host;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (!(fl->c.flc_flags & FL_FLOCK))
		return -ENOLCK;

	if ((osb->s_mount_opt & OCFS2_MOUNT_LOCALFLOCKS) ||
	    ocfs2_mount_local(osb))
		return locks_lock_file_wait(file, fl);

	if (lock_is_unlock(fl))
		return ocfs2_do_funlock(file, cmd, fl);
	else
		return ocfs2_do_flock(file, inode, cmd, fl);
}

int ocfs2_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct inode *inode = file->f_mapping->host;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (!(fl->c.flc_flags & FL_POSIX))
		return -ENOLCK;

	return ocfs2_plock(osb->cconn, OCFS2_I(inode)->ip_blkno, file, cmd, fl);
}
