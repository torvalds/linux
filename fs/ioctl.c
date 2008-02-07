/*
 *  linux/fs/ioctl.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/syscalls.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/capability.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/security.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include <asm/ioctls.h>

/**
 * vfs_ioctl - call filesystem specific ioctl methods
 * @filp: [in]     open file to invoke ioctl method on
 * @cmd:  [in]     ioctl command to execute
 * @arg:  [in/out] command-specific argument for ioctl
 *
 * Invokes filesystem specific ->unlocked_ioctl, if one exists; otherwise
 * invokes * filesystem specific ->ioctl method.  If neither method exists,
 * returns -ENOTTY.
 *
 * Returns 0 on success, -errno on error.
 */
long vfs_ioctl(struct file *filp, unsigned int cmd,
	       unsigned long arg)
{
	int error = -ENOTTY;

	if (!filp->f_op)
		goto out;

	if (filp->f_op->unlocked_ioctl) {
		error = filp->f_op->unlocked_ioctl(filp, cmd, arg);
		if (error == -ENOIOCTLCMD)
			error = -EINVAL;
		goto out;
	} else if (filp->f_op->ioctl) {
		lock_kernel();
		error = filp->f_op->ioctl(filp->f_path.dentry->d_inode,
					  filp, cmd, arg);
		unlock_kernel();
	}

 out:
	return error;
}

static int ioctl_fibmap(struct file *filp, int __user *p)
{
	struct address_space *mapping = filp->f_mapping;
	int res, block;

	/* do we support this mess? */
	if (!mapping->a_ops->bmap)
		return -EINVAL;
	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;
	res = get_user(block, p);
	if (res)
		return res;
	lock_kernel();
	res = mapping->a_ops->bmap(mapping, block);
	unlock_kernel();
	return put_user(res, p);
}

static int file_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	struct inode *inode = filp->f_path.dentry->d_inode;
	int __user *p = (int __user *)arg;

	switch (cmd) {
	case FIBMAP:
		return ioctl_fibmap(filp, p);
	case FIGETBSZ:
		return put_user(inode->i_sb->s_blocksize, p);
	case FIONREAD:
		return put_user(i_size_read(inode) - filp->f_pos, p);
	}

	return vfs_ioctl(filp, cmd, arg);
}

static int ioctl_fionbio(struct file *filp, int __user *argp)
{
	unsigned int flag;
	int on, error;

	error = get_user(on, argp);
	if (error)
		return error;
	flag = O_NONBLOCK;
#ifdef __sparc__
	/* SunOS compatibility item. */
	if (O_NONBLOCK != O_NDELAY)
		flag |= O_NDELAY;
#endif
	if (on)
		filp->f_flags |= flag;
	else
		filp->f_flags &= ~flag;
	return error;
}

static int ioctl_fioasync(unsigned int fd, struct file *filp,
			  int __user *argp)
{
	unsigned int flag;
	int on, error;

	error = get_user(on, argp);
	if (error)
		return error;
	flag = on ? FASYNC : 0;

	/* Did FASYNC state change ? */
	if ((flag ^ filp->f_flags) & FASYNC) {
		if (filp->f_op && filp->f_op->fasync) {
			lock_kernel();
			error = filp->f_op->fasync(fd, filp, on);
			unlock_kernel();
		} else
			error = -ENOTTY;
	}
	if (error)
		return error;

	if (on)
		filp->f_flags |= FASYNC;
	else
		filp->f_flags &= ~FASYNC;
	return error;
}

/*
 * When you add any new common ioctls to the switches above and below
 * please update compat_sys_ioctl() too.
 *
 * do_vfs_ioctl() is not for drivers and not intended to be EXPORT_SYMBOL()'d.
 * It's just a simple helper for sys_ioctl and compat_sys_ioctl.
 */
int do_vfs_ioctl(struct file *filp, unsigned int fd, unsigned int cmd,
	     unsigned long arg)
{
	int error = 0;
	int __user *argp = (int __user *)arg;

	switch (cmd) {
	case FIOCLEX:
		set_close_on_exec(fd, 1);
		break;

	case FIONCLEX:
		set_close_on_exec(fd, 0);
		break;

	case FIONBIO:
		error = ioctl_fionbio(filp, argp);
		break;

	case FIOASYNC:
		error = ioctl_fioasync(fd, filp, argp);
		break;

	case FIOQSIZE:
		if (S_ISDIR(filp->f_path.dentry->d_inode->i_mode) ||
		    S_ISREG(filp->f_path.dentry->d_inode->i_mode) ||
		    S_ISLNK(filp->f_path.dentry->d_inode->i_mode)) {
			loff_t res =
				inode_get_bytes(filp->f_path.dentry->d_inode);
			error = copy_to_user((loff_t __user *)arg, &res,
					     sizeof(res)) ? -EFAULT : 0;
		} else
			error = -ENOTTY;
		break;
	default:
		if (S_ISREG(filp->f_path.dentry->d_inode->i_mode))
			error = file_ioctl(filp, cmd, arg);
		else
			error = vfs_ioctl(filp, cmd, arg);
		break;
	}
	return error;
}

asmlinkage long sys_ioctl(unsigned int fd, unsigned int cmd, unsigned long arg)
{
	struct file *filp;
	int error = -EBADF;
	int fput_needed;

	filp = fget_light(fd, &fput_needed);
	if (!filp)
		goto out;

	error = security_file_ioctl(filp, cmd, arg);
	if (error)
		goto out_fput;

	error = do_vfs_ioctl(filp, fd, cmd, arg);
 out_fput:
	fput_light(filp, fput_needed);
 out:
	return error;
}
