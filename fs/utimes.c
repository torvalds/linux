// SPDX-License-Identifier: GPL-2.0
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/utime.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <asm/unistd.h>

static bool nsec_valid(long nsec)
{
	if (nsec == UTIME_OMIT || nsec == UTIME_NOW)
		return true;

	return nsec >= 0 && nsec <= 999999999;
}

static int utimes_common(const struct path *path, struct timespec64 *times)
{
	int error;
	struct iattr newattrs;
	struct inode *inode = path->dentry->d_inode;
	struct inode *delegated_inode = NULL;

	error = mnt_want_write(path->mnt);
	if (error)
		goto out;

	if (times && times[0].tv_nsec == UTIME_NOW &&
		     times[1].tv_nsec == UTIME_NOW)
		times = NULL;

	newattrs.ia_valid = ATTR_CTIME | ATTR_MTIME | ATTR_ATIME;
	if (times) {
		if (times[0].tv_nsec == UTIME_OMIT)
			newattrs.ia_valid &= ~ATTR_ATIME;
		else if (times[0].tv_nsec != UTIME_NOW) {
			newattrs.ia_atime = timestamp_truncate(times[0], inode);
			newattrs.ia_valid |= ATTR_ATIME_SET;
		}

		if (times[1].tv_nsec == UTIME_OMIT)
			newattrs.ia_valid &= ~ATTR_MTIME;
		else if (times[1].tv_nsec != UTIME_NOW) {
			newattrs.ia_mtime = timestamp_truncate(times[1], inode);
			newattrs.ia_valid |= ATTR_MTIME_SET;
		}
		/*
		 * Tell setattr_prepare(), that this is an explicit time
		 * update, even if neither ATTR_ATIME_SET nor ATTR_MTIME_SET
		 * were used.
		 */
		newattrs.ia_valid |= ATTR_TIMES_SET;
	} else {
		newattrs.ia_valid |= ATTR_TOUCH;
	}
retry_deleg:
	inode_lock(inode);
	error = notify_change(path->dentry, &newattrs, &delegated_inode);
	inode_unlock(inode);
	if (delegated_inode) {
		error = break_deleg_wait(&delegated_inode);
		if (!error)
			goto retry_deleg;
	}

	mnt_drop_write(path->mnt);
out:
	return error;
}

/*
 * do_utimes - change times on filename or file descriptor
 * @dfd: open file descriptor, -1 or AT_FDCWD
 * @filename: path name or NULL
 * @times: new times or NULL
 * @flags: zero or more flags (only AT_SYMLINK_NOFOLLOW for the moment)
 *
 * If filename is NULL and dfd refers to an open file, then operate on
 * the file.  Otherwise look up filename, possibly using dfd as a
 * starting point.
 *
 * If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
long do_utimes(int dfd, const char __user *filename, struct timespec64 *times,
	       int flags)
{
	int error = -EINVAL;

	if (times && (!nsec_valid(times[0].tv_nsec) ||
		      !nsec_valid(times[1].tv_nsec))) {
		goto out;
	}

	if (flags & ~AT_SYMLINK_NOFOLLOW)
		goto out;

	if (filename == NULL && dfd != AT_FDCWD) {
		struct fd f;

		if (flags & AT_SYMLINK_NOFOLLOW)
			goto out;

		f = fdget(dfd);
		error = -EBADF;
		if (!f.file)
			goto out;

		error = utimes_common(&f.file->f_path, times);
		fdput(f);
	} else {
		struct path path;
		int lookup_flags = 0;

		if (!(flags & AT_SYMLINK_NOFOLLOW))
			lookup_flags |= LOOKUP_FOLLOW;
retry:
		error = user_path_at(dfd, filename, lookup_flags, &path);
		if (error)
			goto out;

		error = utimes_common(&path, times);
		path_put(&path);
		if (retry_estale(error, lookup_flags)) {
			lookup_flags |= LOOKUP_REVAL;
			goto retry;
		}
	}

out:
	return error;
}

SYSCALL_DEFINE4(utimensat, int, dfd, const char __user *, filename,
		struct __kernel_timespec __user *, utimes, int, flags)
{
	struct timespec64 tstimes[2];

	if (utimes) {
		if ((get_timespec64(&tstimes[0], &utimes[0]) ||
			get_timespec64(&tstimes[1], &utimes[1])))
			return -EFAULT;

		/* Nothing to do, we must not even check the path.  */
		if (tstimes[0].tv_nsec == UTIME_OMIT &&
		    tstimes[1].tv_nsec == UTIME_OMIT)
			return 0;
	}

	return do_utimes(dfd, filename, utimes ? tstimes : NULL, flags);
}

#ifdef __ARCH_WANT_SYS_UTIME
/*
 * futimesat(), utimes() and utime() are older versions of utimensat()
 * that are provided for compatibility with traditional C libraries.
 * On modern architectures, we always use libc wrappers around
 * utimensat() instead.
 */
static long do_futimesat(int dfd, const char __user *filename,
			 struct __kernel_old_timeval __user *utimes)
{
	struct __kernel_old_timeval times[2];
	struct timespec64 tstimes[2];

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


SYSCALL_DEFINE3(futimesat, int, dfd, const char __user *, filename,
		struct __kernel_old_timeval __user *, utimes)
{
	return do_futimesat(dfd, filename, utimes);
}

SYSCALL_DEFINE2(utimes, char __user *, filename,
		struct __kernel_old_timeval __user *, utimes)
{
	return do_futimesat(AT_FDCWD, filename, utimes);
}

SYSCALL_DEFINE2(utime, char __user *, filename, struct utimbuf __user *, times)
{
	struct timespec64 tv[2];

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

#ifdef CONFIG_COMPAT_32BIT_TIME
/*
 * Not all architectures have sys_utime, so implement this in terms
 * of sys_utimes.
 */
#ifdef __ARCH_WANT_SYS_UTIME32
SYSCALL_DEFINE2(utime32, const char __user *, filename,
		struct old_utimbuf32 __user *, t)
{
	struct timespec64 tv[2];

	if (t) {
		if (get_user(tv[0].tv_sec, &t->actime) ||
		    get_user(tv[1].tv_sec, &t->modtime))
			return -EFAULT;
		tv[0].tv_nsec = 0;
		tv[1].tv_nsec = 0;
	}
	return do_utimes(AT_FDCWD, filename, t ? tv : NULL, 0);
}
#endif

SYSCALL_DEFINE4(utimensat_time32, unsigned int, dfd, const char __user *, filename, struct old_timespec32 __user *, t, int, flags)
{
	struct timespec64 tv[2];

	if  (t) {
		if (get_old_timespec32(&tv[0], &t[0]) ||
		    get_old_timespec32(&tv[1], &t[1]))
			return -EFAULT;

		if (tv[0].tv_nsec == UTIME_OMIT && tv[1].tv_nsec == UTIME_OMIT)
			return 0;
	}
	return do_utimes(dfd, filename, t ? tv : NULL, flags);
}

#ifdef __ARCH_WANT_SYS_UTIME32
static long do_compat_futimesat(unsigned int dfd, const char __user *filename,
				struct old_timeval32 __user *t)
{
	struct timespec64 tv[2];

	if (t) {
		if (get_user(tv[0].tv_sec, &t[0].tv_sec) ||
		    get_user(tv[0].tv_nsec, &t[0].tv_usec) ||
		    get_user(tv[1].tv_sec, &t[1].tv_sec) ||
		    get_user(tv[1].tv_nsec, &t[1].tv_usec))
			return -EFAULT;
		if (tv[0].tv_nsec >= 1000000 || tv[0].tv_nsec < 0 ||
		    tv[1].tv_nsec >= 1000000 || tv[1].tv_nsec < 0)
			return -EINVAL;
		tv[0].tv_nsec *= 1000;
		tv[1].tv_nsec *= 1000;
	}
	return do_utimes(dfd, filename, t ? tv : NULL, 0);
}

SYSCALL_DEFINE3(futimesat_time32, unsigned int, dfd,
		       const char __user *, filename,
		       struct old_timeval32 __user *, t)
{
	return do_compat_futimesat(dfd, filename, t);
}

SYSCALL_DEFINE2(utimes_time32, const char __user *, filename, struct old_timeval32 __user *, t)
{
	return do_compat_futimesat(AT_FDCWD, filename, t);
}
#endif
#endif
