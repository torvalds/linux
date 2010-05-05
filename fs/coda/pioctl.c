/*
 * Pioctl operations for Coda.
 * Original version: (C) 1996 Peter Braam
 * Rewritten for Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/namei.h>
#include <linux/module.h>
#include <asm/uaccess.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_psdev.h>

#include <linux/smp_lock.h>

/* pioctl ops */
static int coda_ioctl_permission(struct inode *inode, int mask);
static long coda_pioctl(struct file *filp, unsigned int cmd,
			unsigned long user_data);

/* exported from this file */
const struct inode_operations coda_ioctl_inode_operations = {
	.permission	= coda_ioctl_permission,
	.setattr	= coda_setattr,
};

const struct file_operations coda_ioctl_operations = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= coda_pioctl,
};

/* the coda pioctl inode ops */
static int coda_ioctl_permission(struct inode *inode, int mask)
{
	return (mask & MAY_EXEC) ? -EACCES : 0;
}

static long coda_pioctl(struct file *filp, unsigned int cmd,
			unsigned long user_data)
{
	struct path path;
	int error;
	struct PioctlData data;
	struct inode *inode = filp->f_dentry->d_inode;
	struct inode *target_inode = NULL;
	struct coda_inode_info *cnp;

	lock_kernel();

	/* get the Pioctl data arguments from user space */
	if (copy_from_user(&data, (void __user *)user_data, sizeof(data))) {
		error = -EINVAL;
		goto out;
	}

	/*
	 * Look up the pathname. Note that the pathname is in
	 * user memory, and namei takes care of this
	 */
	if (data.follow)
		error = user_path(data.path, &path);
	else
		error = user_lpath(data.path, &path);

	if (error)
		goto out;
	else
		target_inode = path.dentry->d_inode;

	/* return if it is not a Coda inode */
	if (target_inode->i_sb != inode->i_sb) {
		path_put(&path);
		error = -EINVAL;
		goto out;
	}

	/* now proceed to make the upcall */
	cnp = ITOC(target_inode);

	error = venus_pioctl(inode->i_sb, &(cnp->c_fid), cmd, &data);

	path_put(&path);

out:
	unlock_kernel();
	return error;
}
