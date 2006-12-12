#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/utime.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

#ifdef __ARCH_WANT_SYS_UTIME

/*
 * sys_utime() can be implemented in user-level using sys_utimes().
 * Is this for backwards compatibility?  If so, why not move it
 * into the appropriate arch directory (for those architectures that
 * need it).
 */

/* If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
asmlinkage long sys_utime(char __user * filename, struct utimbuf __user * times)
{
	int error;
	struct nameidata nd;
	struct inode * inode;
	struct iattr newattrs;

	error = user_path_walk(filename, &nd);
	if (error)
		goto out;
	inode = nd.dentry->d_inode;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	/* Don't worry, the checks are done in inode_change_ok() */
	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME;
	if (times) {
		error = -EPERM;
		if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
			goto dput_and_out;

		error = get_user(newattrs.ia_atime.tv_sec, &times->actime);
		newattrs.ia_atime.tv_nsec = 0;
		if (!error)
			error = get_user(newattrs.ia_mtime.tv_sec, &times->modtime);
		newattrs.ia_mtime.tv_nsec = 0;
		if (error)
			goto dput_and_out;

		newattrs.ia_valid |= ATTR_ATIME_SET | ATTR_MTIME_SET;
	} else {
                error = -EACCES;
                if (IS_IMMUTABLE(inode))
                        goto dput_and_out;

		if (current->fsuid != inode->i_uid &&
		    (error = vfs_permission(&nd, MAY_WRITE)) != 0)
			goto dput_and_out;
	}
	mutex_lock(&inode->i_mutex);
	error = notify_change(nd.dentry, &newattrs);
	mutex_unlock(&inode->i_mutex);
dput_and_out:
	path_release(&nd);
out:
	return error;
}

#endif

/* If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
long do_utimes(int dfd, char __user *filename, struct timeval *times)
{
	int error;
	struct nameidata nd;
	struct inode * inode;
	struct iattr newattrs;

	error = __user_walk_fd(dfd, filename, LOOKUP_FOLLOW, &nd);

	if (error)
		goto out;
	inode = nd.dentry->d_inode;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	/* Don't worry, the checks are done in inode_change_ok() */
	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME;
	if (times) {
		error = -EPERM;
                if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
                        goto dput_and_out;

		newattrs.ia_atime.tv_sec = times[0].tv_sec;
		newattrs.ia_atime.tv_nsec = times[0].tv_usec * 1000;
		newattrs.ia_mtime.tv_sec = times[1].tv_sec;
		newattrs.ia_mtime.tv_nsec = times[1].tv_usec * 1000;
		newattrs.ia_valid |= ATTR_ATIME_SET | ATTR_MTIME_SET;
	} else {
		error = -EACCES;
                if (IS_IMMUTABLE(inode))
                        goto dput_and_out;

		if (current->fsuid != inode->i_uid &&
		    (error = vfs_permission(&nd, MAY_WRITE)) != 0)
			goto dput_and_out;
	}
	mutex_lock(&inode->i_mutex);
	error = notify_change(nd.dentry, &newattrs);
	mutex_unlock(&inode->i_mutex);
dput_and_out:
	path_release(&nd);
out:
	return error;
}

asmlinkage long sys_futimesat(int dfd, char __user *filename, struct timeval __user *utimes)
{
	struct timeval times[2];

	if (utimes && copy_from_user(&times, utimes, sizeof(times)))
		return -EFAULT;
	return do_utimes(dfd, filename, utimes ? times : NULL);
}

asmlinkage long sys_utimes(char __user *filename, struct timeval __user *utimes)
{
	return sys_futimesat(AT_FDCWD, filename, utimes);
}
