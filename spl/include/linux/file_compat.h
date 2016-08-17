/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_FILE_COMPAT_H
#define _SPL_FILE_COMPAT_H

#include <linux/fs.h>
#include <linux/uaccess.h>
#ifdef HAVE_FDTABLE_HEADER
#include <linux/fdtable.h>
#endif

static inline struct file *
spl_filp_open(const char *name, int flags, int mode, int *err)
{
        struct file *filp = NULL;
        int rc;

        filp = filp_open(name, flags, mode);
        if (IS_ERR(filp)) {
                rc = PTR_ERR(filp);
                if (err)
                        *err = rc;
                filp = NULL;
        }
        return filp;
}

#define spl_filp_close(f)		filp_close(f, NULL)
#define spl_filp_poff(f)		(&(f)->f_pos)
#define spl_filp_write(fp, b, s, p)	(fp)->f_op->write((fp), (b), (s), p)

static inline int
spl_filp_fallocate(struct file *fp, int mode, loff_t offset, loff_t len)
{
	int error = -EOPNOTSUPP;

#ifdef HAVE_FILE_FALLOCATE
	if (fp->f_op->fallocate)
		error = fp->f_op->fallocate(fp, mode, offset, len);
#else
#ifdef HAVE_INODE_FALLOCATE
	if (fp->f_dentry && fp->f_dentry->d_inode &&
	    fp->f_dentry->d_inode->i_op->fallocate)
		error = fp->f_dentry->d_inode->i_op->fallocate(
		    fp->f_dentry->d_inode, mode, offset, len);
#endif /* HAVE_INODE_FALLOCATE */
#endif /*HAVE_FILE_FALLOCATE */

	return (error);
}

static inline ssize_t
spl_kernel_write(struct file *file, const void *buf, size_t count, loff_t *pos)
{
#if defined(HAVE_KERNEL_WRITE_PPOS)
	return (kernel_write(file, buf, count, pos));
#else
	mm_segment_t saved_fs;
	ssize_t ret;

	saved_fs = get_fs();
	set_fs(get_ds());

	ret = vfs_write(file, (__force const char __user *)buf, count, pos);

	set_fs(saved_fs);

	return (ret);
#endif
}

static inline ssize_t
spl_kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
#if defined(HAVE_KERNEL_READ_PPOS)
	return (kernel_read(file, buf, count, pos));
#else
	mm_segment_t saved_fs;
	ssize_t ret;

	saved_fs = get_fs();
	set_fs(get_ds());

	ret = vfs_read(file, (void __user *)buf, count, pos);

	set_fs(saved_fs);

	return (ret);
#endif
}

#ifdef HAVE_2ARGS_VFS_FSYNC
#define	spl_filp_fsync(fp, sync)	vfs_fsync(fp, sync)
#else
#define	spl_filp_fsync(fp, sync)	vfs_fsync(fp, (fp)->f_dentry, sync)
#endif /* HAVE_2ARGS_VFS_FSYNC */

#ifdef HAVE_INODE_LOCK_SHARED
#define	spl_inode_lock(ip)		inode_lock(ip)
#define	spl_inode_unlock(ip)		inode_unlock(ip)
#define	spl_inode_lock_shared(ip)	inode_lock_shared(ip)
#define	spl_inode_unlock_shared(ip)	inode_unlock_shared(ip)
#define	spl_inode_trylock(ip)		inode_trylock(ip)
#define	spl_inode_trylock_shared(ip)	inode_trylock_shared(ip)
#define	spl_inode_is_locked(ip)		inode_is_locked(ip)
#define	spl_inode_lock_nested(ip, s)	inode_lock_nested(ip, s)
#else
#define	spl_inode_lock(ip)		mutex_lock(&(ip)->i_mutex)
#define	spl_inode_unlock(ip)		mutex_unlock(&(ip)->i_mutex)
#define	spl_inode_lock_shared(ip)	mutex_lock(&(ip)->i_mutex)
#define	spl_inode_unlock_shared(ip)	mutex_unlock(&(ip)->i_mutex)
#define	spl_inode_trylock(ip)		mutex_trylock(&(ip)->i_mutex)
#define	spl_inode_trylock_shared(ip)	mutex_trylock(&(ip)->i_mutex)
#define	spl_inode_is_locked(ip)		mutex_is_locked(&(ip)->i_mutex)
#define	spl_inode_lock_nested(ip, s)	mutex_lock_nested(&(ip)->i_mutex, s)
#endif

#endif /* SPL_FILE_COMPAT_H */

