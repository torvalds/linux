#include <linux/compiler.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/utime.h>
#include <linux/syscalls.h>
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
asmlinkage long sys_utime(char __user *filename, struct utimbuf __user *times)
{
	struct timespec tv[2];

	if (times) {
		if (get_user(tv[0].tv_sec, &times->actime) ||
		    get_user(tv[1].tv_sec, &times->modtime))
			return -EFAULT;
		tv[0].tv_nsec = 0;
		tv[1].tv_nsec = 0;
	}
	return do_utimes(AT_FDCWD, filename, times ? tv : NULL, 0);
}

#endif

static bool nsec_valid(long nsec)
{
	if (nsec == UTIME_OMIT || nsec == UTIME_NOW)
		return true;

	return nsec >= 0 && nsec <= 999999999;
}

/* If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
long do_utimes(int dfd, char __user *filename, struct timespec *times, int flags)
{
	int error;
	struct nameidata nd;
	struct dentry *dentry;
	struct inode *inode;
	struct iattr newattrs;
	struct file *f = NULL;
	struct vfsmount *mnt;

	error = -EINVAL;
	if (times && (!nsec_valid(times[0].tv_nsec) ||
		      !nsec_valid(times[1].tv_nsec))) {
		goto out;
	}

	if (flags & ~AT_SYMLINK_NOFOLLOW)
		goto out;

	if (filename == NULL && dfd != AT_FDCWD) {
		error = -EINVAL;
		if (flags & AT_SYMLINK_NOFOLLOW)
			goto out;

		error = -EBADF;
		f = fget(dfd);
		if (!f)
			goto out;
		dentry = f->f_path.dentry;
		mnt = f->f_path.mnt;
	} else {
		error = __user_walk_fd(dfd, filename, (flags & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW, &nd);
		if (error)
			goto out;

		dentry = nd.path.dentry;
		mnt = nd.path.mnt;
	}

	inode = dentry->d_inode;

	error = mnt_want_write(mnt);
	if (error)
		goto dput_and_out;

	/* Don't worry, the checks are done in inode_change_ok() */
	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME;
	if (times) {
		error = -EPERM;
                if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
			goto mnt_drop_write_and_out;

		if (times[0].tv_nsec == UTIME_OMIT)
			newattrs.ia_valid &= ~ATTR_ATIME;
		else if (times[0].tv_nsec != UTIME_NOW) {
			newattrs.ia_atime.tv_sec = times[0].tv_sec;
			newattrs.ia_atime.tv_nsec = times[0].tv_nsec;
			newattrs.ia_valid |= ATTR_ATIME_SET;
		}

		if (times[1].tv_nsec == UTIME_OMIT)
			newattrs.ia_valid &= ~ATTR_MTIME;
		else if (times[1].tv_nsec != UTIME_NOW) {
			newattrs.ia_mtime.tv_sec = times[1].tv_sec;
			newattrs.ia_mtime.tv_nsec = times[1].tv_nsec;
			newattrs.ia_valid |= ATTR_MTIME_SET;
		}
	} else {
		error = -EACCES;
                if (IS_IMMUTABLE(inode))
			goto mnt_drop_write_and_out;

		if (!is_owner_or_cap(inode)) {
			if (f) {
				if (!(f->f_mode & FMODE_WRITE))
					goto mnt_drop_write_and_out;
			} else {
				error = vfs_permission(&nd, MAY_WRITE);
				if (error)
					goto mnt_drop_write_and_out;
			}
		}
	}
	mutex_lock(&inode->i_mutex);
	error = notify_change(dentry, &newattrs);
	mutex_unlock(&inode->i_mutex);
mnt_drop_write_and_out:
	mnt_drop_write(mnt);
dput_and_out:
	if (f)
		fput(f);
	else
		path_put(&nd.path);
out:
	return error;
}

asmlinkage long sys_utimensat(int dfd, char __user *filename, struct timespec __user *utimes, int flags)
{
	struct timespec tstimes[2];

	if (utimes) {
		if (copy_from_user(&tstimes, utimes, sizeof(tstimes)))
			return -EFAULT;
		if ((tstimes[0].tv_nsec == UTIME_OMIT ||
		     tstimes[0].tv_nsec == UTIME_NOW) &&
		    tstimes[0].tv_sec != 0)
			return -EINVAL;
		if ((tstimes[1].tv_nsec == UTIME_OMIT ||
		     tstimes[1].tv_nsec == UTIME_NOW) &&
		    tstimes[1].tv_sec != 0)
			return -EINVAL;

		/* Nothing to do, we must not even check the path.  */
		if (tstimes[0].tv_nsec == UTIME_OMIT &&
		    tstimes[1].tv_nsec == UTIME_OMIT)
			return 0;
	}

	return do_utimes(dfd, filename, utimes ? tstimes : NULL, flags);
}

asmlinkage long sys_futimesat(int dfd, char __user *filename, struct timeval __user *utimes)
{
	struct timeval times[2];
	struct timespec tstimes[2];

	if (utimes) {
		if (copy_from_user(&times, utimes, sizeof(times)))
			return -EFAULT;

		/* This test is needed to catch all invalid values.  If we
		   would test only in do_utimes we would miss those invalid
		   values truncated by the multiplication with 1000.  Note
		   that we also catch UTIME_{NOW,OMIT} here which are only
		   valid for utimensat.  */
		if (times[0].tv_usec >= 1000000 || times[0].tv_usec < 0 ||
		    times[1].tv_usec >= 1000000 || times[1].tv_usec < 0)
			return -EINVAL;

		tstimes[0].tv_sec = times[0].tv_sec;
		tstimes[0].tv_nsec = 1000 * times[0].tv_usec;
		tstimes[1].tv_sec = times[1].tv_sec;
		tstimes[1].tv_nsec = 1000 * times[1].tv_usec;
	}

	return do_utimes(dfd, filename, utimes ? tstimes : NULL, 0);
}

asmlinkage long sys_utimes(char __user *filename, struct timeval __user *utimes)
{
	return sys_futimesat(AT_FDCWD, filename, utimes);
}
