/*
 *  linux/fs/compat.c
 *
 *  Kernel compatibililty routines for e.g. 32 bit syscall support
 *  on 64 bit kernels.
 *
 *  Copyright (C) 2002       Stephen Rothwell, IBM Corporation
 *  Copyright (C) 1997-2000  Jakub Jelinek  (jakub@redhat.com)
 *  Copyright (C) 1998       Eddie C. Dost  (ecd@skynet.be)
 *  Copyright (C) 2001,2002  Andi Kleen, SuSE Labs 
 *  Copyright (C) 2003       Pavel Machek (pavel@ucw.cz)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/compat.h>
#include <linux/errno.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/vfs.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/ncp_mount.h>
#include <linux/nfs4_mount.h>
#include <linux/syscalls.h>
#include <linux/ctype.h>
#include <linux/dirent.h>
#include <linux/fsnotify.h>
#include <linux/highuid.h>
#include <linux/personality.h>
#include <linux/rwsem.h>
#include <linux/tsacct_kern.h>
#include <linux/security.h>
#include <linux/highmem.h>
#include <linux/signal.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/fs_struct.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/aio.h>

#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <asm/ioctls.h>
#include "internal.h"

int compat_log = 1;

int compat_printk(const char *fmt, ...)
{
	va_list ap;
	int ret;
	if (!compat_log)
		return 0;
	va_start(ap, fmt);
	ret = vprintk(fmt, ap);
	va_end(ap);
	return ret;
}

/*
 * Not all architectures have sys_utime, so implement this in terms
 * of sys_utimes.
 */
COMPAT_SYSCALL_DEFINE2(utime, const char __user *, filename,
		       struct compat_utimbuf __user *, t)
{
	struct timespec tv[2];

	if (t) {
		if (get_user(tv[0].tv_sec, &t->actime) ||
		    get_user(tv[1].tv_sec, &t->modtime))
			return -EFAULT;
		tv[0].tv_nsec = 0;
		tv[1].tv_nsec = 0;
	}
	return do_utimes(AT_FDCWD, filename, t ? tv : NULL, 0);
}

COMPAT_SYSCALL_DEFINE4(utimensat, unsigned int, dfd, const char __user *, filename, struct compat_timespec __user *, t, int, flags)
{
	struct timespec tv[2];

	if  (t) {
		if (compat_get_timespec(&tv[0], &t[0]) ||
		    compat_get_timespec(&tv[1], &t[1]))
			return -EFAULT;

		if (tv[0].tv_nsec == UTIME_OMIT && tv[1].tv_nsec == UTIME_OMIT)
			return 0;
	}
	return do_utimes(dfd, filename, t ? tv : NULL, flags);
}

COMPAT_SYSCALL_DEFINE3(futimesat, unsigned int, dfd, const char __user *, filename, struct compat_timeval __user *, t)
{
	struct timespec tv[2];

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

COMPAT_SYSCALL_DEFINE2(utimes, const char __user *, filename, struct compat_timeval __user *, t)
{
	return compat_sys_futimesat(AT_FDCWD, filename, t);
}

static int cp_compat_stat(struct kstat *stat, struct compat_stat __user *ubuf)
{
	struct compat_stat tmp;

	if (!old_valid_dev(stat->dev) || !old_valid_dev(stat->rdev))
		return -EOVERFLOW;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = old_encode_dev(stat->dev);
	tmp.st_ino = stat->ino;
	if (sizeof(tmp.st_ino) < sizeof(stat->ino) && tmp.st_ino != stat->ino)
		return -EOVERFLOW;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	if (tmp.st_nlink != stat->nlink)
		return -EOVERFLOW;
	SET_UID(tmp.st_uid, from_kuid_munged(current_user_ns(), stat->uid));
	SET_GID(tmp.st_gid, from_kgid_munged(current_user_ns(), stat->gid));
	tmp.st_rdev = old_encode_dev(stat->rdev);
	if ((u64) stat->size > MAX_NON_LFS)
		return -EOVERFLOW;
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime.tv_sec;
	tmp.st_atime_nsec = stat->atime.tv_nsec;
	tmp.st_mtime = stat->mtime.tv_sec;
	tmp.st_mtime_nsec = stat->mtime.tv_nsec;
	tmp.st_ctime = stat->ctime.tv_sec;
	tmp.st_ctime_nsec = stat->ctime.tv_nsec;
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(ubuf, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

COMPAT_SYSCALL_DEFINE2(newstat, const char __user *, filename,
		       struct compat_stat __user *, statbuf)
{
	struct kstat stat;
	int error;

	error = vfs_stat(filename, &stat);
	if (error)
		return error;
	return cp_compat_stat(&stat, statbuf);
}

COMPAT_SYSCALL_DEFINE2(newlstat, const char __user *, filename,
		       struct compat_stat __user *, statbuf)
{
	struct kstat stat;
	int error;

	error = vfs_lstat(filename, &stat);
	if (error)
		return error;
	return cp_compat_stat(&stat, statbuf);
}

#ifndef __ARCH_WANT_STAT64
COMPAT_SYSCALL_DEFINE4(newfstatat, unsigned int, dfd,
		       const char __user *, filename,
		       struct compat_stat __user *, statbuf, int, flag)
{
	struct kstat stat;
	int error;

	error = vfs_fstatat(dfd, filename, &stat, flag);
	if (error)
		return error;
	return cp_compat_stat(&stat, statbuf);
}
#endif

COMPAT_SYSCALL_DEFINE2(newfstat, unsigned int, fd,
		       struct compat_stat __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_compat_stat(&stat, statbuf);
	return error;
}

static int put_compat_statfs(struct compat_statfs __user *ubuf, struct kstatfs *kbuf)
{
	
	if (sizeof ubuf->f_blocks == 4) {
		if ((kbuf->f_blocks | kbuf->f_bfree | kbuf->f_bavail |
		     kbuf->f_bsize | kbuf->f_frsize) & 0xffffffff00000000ULL)
			return -EOVERFLOW;
		/* f_files and f_ffree may be -1; it's okay
		 * to stuff that into 32 bits */
		if (kbuf->f_files != 0xffffffffffffffffULL
		 && (kbuf->f_files & 0xffffffff00000000ULL))
			return -EOVERFLOW;
		if (kbuf->f_ffree != 0xffffffffffffffffULL
		 && (kbuf->f_ffree & 0xffffffff00000000ULL))
			return -EOVERFLOW;
	}
	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(*ubuf)) ||
	    __put_user(kbuf->f_type, &ubuf->f_type) ||
	    __put_user(kbuf->f_bsize, &ubuf->f_bsize) ||
	    __put_user(kbuf->f_blocks, &ubuf->f_blocks) ||
	    __put_user(kbuf->f_bfree, &ubuf->f_bfree) ||
	    __put_user(kbuf->f_bavail, &ubuf->f_bavail) ||
	    __put_user(kbuf->f_files, &ubuf->f_files) ||
	    __put_user(kbuf->f_ffree, &ubuf->f_ffree) ||
	    __put_user(kbuf->f_namelen, &ubuf->f_namelen) ||
	    __put_user(kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]) ||
	    __put_user(kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]) ||
	    __put_user(kbuf->f_frsize, &ubuf->f_frsize) ||
	    __put_user(kbuf->f_flags, &ubuf->f_flags) ||
	    __clear_user(ubuf->f_spare, sizeof(ubuf->f_spare)))
		return -EFAULT;
	return 0;
}

/*
 * The following statfs calls are copies of code from fs/statfs.c and
 * should be checked against those from time to time
 */
COMPAT_SYSCALL_DEFINE2(statfs, const char __user *, pathname, struct compat_statfs __user *, buf)
{
	struct kstatfs tmp;
	int error = user_statfs(pathname, &tmp);
	if (!error)
		error = put_compat_statfs(buf, &tmp);
	return error;
}

COMPAT_SYSCALL_DEFINE2(fstatfs, unsigned int, fd, struct compat_statfs __user *, buf)
{
	struct kstatfs tmp;
	int error = fd_statfs(fd, &tmp);
	if (!error)
		error = put_compat_statfs(buf, &tmp);
	return error;
}

static int put_compat_statfs64(struct compat_statfs64 __user *ubuf, struct kstatfs *kbuf)
{
	if (sizeof ubuf->f_blocks == 4) {
		if ((kbuf->f_blocks | kbuf->f_bfree | kbuf->f_bavail |
		     kbuf->f_bsize | kbuf->f_frsize) & 0xffffffff00000000ULL)
			return -EOVERFLOW;
		/* f_files and f_ffree may be -1; it's okay
		 * to stuff that into 32 bits */
		if (kbuf->f_files != 0xffffffffffffffffULL
		 && (kbuf->f_files & 0xffffffff00000000ULL))
			return -EOVERFLOW;
		if (kbuf->f_ffree != 0xffffffffffffffffULL
		 && (kbuf->f_ffree & 0xffffffff00000000ULL))
			return -EOVERFLOW;
	}
	if (!access_ok(VERIFY_WRITE, ubuf, sizeof(*ubuf)) ||
	    __put_user(kbuf->f_type, &ubuf->f_type) ||
	    __put_user(kbuf->f_bsize, &ubuf->f_bsize) ||
	    __put_user(kbuf->f_blocks, &ubuf->f_blocks) ||
	    __put_user(kbuf->f_bfree, &ubuf->f_bfree) ||
	    __put_user(kbuf->f_bavail, &ubuf->f_bavail) ||
	    __put_user(kbuf->f_files, &ubuf->f_files) ||
	    __put_user(kbuf->f_ffree, &ubuf->f_ffree) ||
	    __put_user(kbuf->f_namelen, &ubuf->f_namelen) ||
	    __put_user(kbuf->f_fsid.val[0], &ubuf->f_fsid.val[0]) ||
	    __put_user(kbuf->f_fsid.val[1], &ubuf->f_fsid.val[1]) ||
	    __put_user(kbuf->f_frsize, &ubuf->f_frsize) ||
	    __put_user(kbuf->f_flags, &ubuf->f_flags) ||
	    __clear_user(ubuf->f_spare, sizeof(ubuf->f_spare)))
		return -EFAULT;
	return 0;
}

COMPAT_SYSCALL_DEFINE3(statfs64, const char __user *, pathname, compat_size_t, sz, struct compat_statfs64 __user *, buf)
{
	struct kstatfs tmp;
	int error;

	if (sz != sizeof(*buf))
		return -EINVAL;

	error = user_statfs(pathname, &tmp);
	if (!error)
		error = put_compat_statfs64(buf, &tmp);
	return error;
}

COMPAT_SYSCALL_DEFINE3(fstatfs64, unsigned int, fd, compat_size_t, sz, struct compat_statfs64 __user *, buf)
{
	struct kstatfs tmp;
	int error;

	if (sz != sizeof(*buf))
		return -EINVAL;

	error = fd_statfs(fd, &tmp);
	if (!error)
		error = put_compat_statfs64(buf, &tmp);
	return error;
}

/*
 * This is a copy of sys_ustat, just dealing with a structure layout.
 * Given how simple this syscall is that apporach is more maintainable
 * than the various conversion hacks.
 */
COMPAT_SYSCALL_DEFINE2(ustat, unsigned, dev, struct compat_ustat __user *, u)
{
	struct compat_ustat tmp;
	struct kstatfs sbuf;
	int err = vfs_ustat(new_decode_dev(dev), &sbuf);
	if (err)
		return err;

	memset(&tmp, 0, sizeof(struct compat_ustat));
	tmp.f_tfree = sbuf.f_bfree;
	tmp.f_tinode = sbuf.f_ffree;
	if (copy_to_user(u, &tmp, sizeof(struct compat_ustat)))
		return -EFAULT;
	return 0;
}

static int get_compat_flock(struct flock *kfl, struct compat_flock __user *ufl)
{
	if (!access_ok(VERIFY_READ, ufl, sizeof(*ufl)) ||
	    __get_user(kfl->l_type, &ufl->l_type) ||
	    __get_user(kfl->l_whence, &ufl->l_whence) ||
	    __get_user(kfl->l_start, &ufl->l_start) ||
	    __get_user(kfl->l_len, &ufl->l_len) ||
	    __get_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}

static int put_compat_flock(struct flock *kfl, struct compat_flock __user *ufl)
{
	if (!access_ok(VERIFY_WRITE, ufl, sizeof(*ufl)) ||
	    __put_user(kfl->l_type, &ufl->l_type) ||
	    __put_user(kfl->l_whence, &ufl->l_whence) ||
	    __put_user(kfl->l_start, &ufl->l_start) ||
	    __put_user(kfl->l_len, &ufl->l_len) ||
	    __put_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}

#ifndef HAVE_ARCH_GET_COMPAT_FLOCK64
static int get_compat_flock64(struct flock *kfl, struct compat_flock64 __user *ufl)
{
	if (!access_ok(VERIFY_READ, ufl, sizeof(*ufl)) ||
	    __get_user(kfl->l_type, &ufl->l_type) ||
	    __get_user(kfl->l_whence, &ufl->l_whence) ||
	    __get_user(kfl->l_start, &ufl->l_start) ||
	    __get_user(kfl->l_len, &ufl->l_len) ||
	    __get_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}
#endif

#ifndef HAVE_ARCH_PUT_COMPAT_FLOCK64
static int put_compat_flock64(struct flock *kfl, struct compat_flock64 __user *ufl)
{
	if (!access_ok(VERIFY_WRITE, ufl, sizeof(*ufl)) ||
	    __put_user(kfl->l_type, &ufl->l_type) ||
	    __put_user(kfl->l_whence, &ufl->l_whence) ||
	    __put_user(kfl->l_start, &ufl->l_start) ||
	    __put_user(kfl->l_len, &ufl->l_len) ||
	    __put_user(kfl->l_pid, &ufl->l_pid))
		return -EFAULT;
	return 0;
}
#endif

static unsigned int
convert_fcntl_cmd(unsigned int cmd)
{
	switch (cmd) {
	case F_GETLK64:
		return F_GETLK;
	case F_SETLK64:
		return F_SETLK;
	case F_SETLKW64:
		return F_SETLKW;
	}

	return cmd;
}

COMPAT_SYSCALL_DEFINE3(fcntl64, unsigned int, fd, unsigned int, cmd,
		       compat_ulong_t, arg)
{
	mm_segment_t old_fs;
	struct flock f;
	long ret;
	unsigned int conv_cmd;

	switch (cmd) {
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		ret = get_compat_flock(&f, compat_ptr(arg));
		if (ret != 0)
			break;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_fcntl(fd, cmd, (unsigned long)&f);
		set_fs(old_fs);
		if (cmd == F_GETLK && ret == 0) {
			/* GETLK was successful and we need to return the data...
			 * but it needs to fit in the compat structure.
			 * l_start shouldn't be too big, unless the original
			 * start + end is greater than COMPAT_OFF_T_MAX, in which
			 * case the app was asking for trouble, so we return
			 * -EOVERFLOW in that case.
			 * l_len could be too big, in which case we just truncate it,
			 * and only allow the app to see that part of the conflicting
			 * lock that might make sense to it anyway
			 */

			if (f.l_start > COMPAT_OFF_T_MAX)
				ret = -EOVERFLOW;
			if (f.l_len > COMPAT_OFF_T_MAX)
				f.l_len = COMPAT_OFF_T_MAX;
			if (ret == 0)
				ret = put_compat_flock(&f, compat_ptr(arg));
		}
		break;

	case F_GETLK64:
	case F_SETLK64:
	case F_SETLKW64:
	case F_OFD_GETLK:
	case F_OFD_SETLK:
	case F_OFD_SETLKW:
		ret = get_compat_flock64(&f, compat_ptr(arg));
		if (ret != 0)
			break;
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		conv_cmd = convert_fcntl_cmd(cmd);
		ret = sys_fcntl(fd, conv_cmd, (unsigned long)&f);
		set_fs(old_fs);
		if ((conv_cmd == F_GETLK || conv_cmd == F_OFD_GETLK) && ret == 0) {
			/* need to return lock information - see above for commentary */
			if (f.l_start > COMPAT_LOFF_T_MAX)
				ret = -EOVERFLOW;
			if (f.l_len > COMPAT_LOFF_T_MAX)
				f.l_len = COMPAT_LOFF_T_MAX;
			if (ret == 0)
				ret = put_compat_flock64(&f, compat_ptr(arg));
		}
		break;

	default:
		ret = sys_fcntl(fd, cmd, arg);
		break;
	}
	return ret;
}

COMPAT_SYSCALL_DEFINE3(fcntl, unsigned int, fd, unsigned int, cmd,
		       compat_ulong_t, arg)
{
	switch (cmd) {
	case F_GETLK64:
	case F_SETLK64:
	case F_SETLKW64:
	case F_OFD_GETLK:
	case F_OFD_SETLK:
	case F_OFD_SETLKW:
		return -EINVAL;
	}
	return compat_sys_fcntl64(fd, cmd, arg);
}

COMPAT_SYSCALL_DEFINE2(io_setup, unsigned, nr_reqs, u32 __user *, ctx32p)
{
	long ret;
	aio_context_t ctx64;

	mm_segment_t oldfs = get_fs();
	if (unlikely(get_user(ctx64, ctx32p)))
		return -EFAULT;

	set_fs(KERNEL_DS);
	/* The __user pointer cast is valid because of the set_fs() */
	ret = sys_io_setup(nr_reqs, (aio_context_t __user *) &ctx64);
	set_fs(oldfs);
	/* truncating is ok because it's a user address */
	if (!ret)
		ret = put_user((u32) ctx64, ctx32p);
	return ret;
}

COMPAT_SYSCALL_DEFINE5(io_getevents, compat_aio_context_t, ctx_id,
		       compat_long_t, min_nr,
		       compat_long_t, nr,
		       struct io_event __user *, events,
		       struct compat_timespec __user *, timeout)
{
	struct timespec t;
	struct timespec __user *ut = NULL;

	if (timeout) {
		if (compat_get_timespec(&t, timeout))
			return -EFAULT;

		ut = compat_alloc_user_space(sizeof(*ut));
		if (copy_to_user(ut, &t, sizeof(t)) )
			return -EFAULT;
	} 
	return sys_io_getevents(ctx_id, min_nr, nr, events, ut);
}

/* A write operation does a read from user space and vice versa */
#define vrfy_dir(type) ((type) == READ ? VERIFY_WRITE : VERIFY_READ)

ssize_t compat_rw_copy_check_uvector(int type,
		const struct compat_iovec __user *uvector, unsigned long nr_segs,
		unsigned long fast_segs, struct iovec *fast_pointer,
		struct iovec **ret_pointer)
{
	compat_ssize_t tot_len;
	struct iovec *iov = *ret_pointer = fast_pointer;
	ssize_t ret = 0;
	int seg;

	/*
	 * SuS says "The readv() function *may* fail if the iovcnt argument
	 * was less than or equal to 0, or greater than {IOV_MAX}.  Linux has
	 * traditionally returned zero for zero segments, so...
	 */
	if (nr_segs == 0)
		goto out;

	ret = -EINVAL;
	if (nr_segs > UIO_MAXIOV || nr_segs < 0)
		goto out;
	if (nr_segs > fast_segs) {
		ret = -ENOMEM;
		iov = kmalloc(nr_segs*sizeof(struct iovec), GFP_KERNEL);
		if (iov == NULL)
			goto out;
	}
	*ret_pointer = iov;

	ret = -EFAULT;
	if (!access_ok(VERIFY_READ, uvector, nr_segs*sizeof(*uvector)))
		goto out;

	/*
	 * Single unix specification:
	 * We should -EINVAL if an element length is not >= 0 and fitting an
	 * ssize_t.
	 *
	 * In Linux, the total length is limited to MAX_RW_COUNT, there is
	 * no overflow possibility.
	 */
	tot_len = 0;
	ret = -EINVAL;
	for (seg = 0; seg < nr_segs; seg++) {
		compat_uptr_t buf;
		compat_ssize_t len;

		if (__get_user(len, &uvector->iov_len) ||
		   __get_user(buf, &uvector->iov_base)) {
			ret = -EFAULT;
			goto out;
		}
		if (len < 0)	/* size_t not fitting in compat_ssize_t .. */
			goto out;
		if (type >= 0 &&
		    !access_ok(vrfy_dir(type), compat_ptr(buf), len)) {
			ret = -EFAULT;
			goto out;
		}
		if (len > MAX_RW_COUNT - tot_len)
			len = MAX_RW_COUNT - tot_len;
		tot_len += len;
		iov->iov_base = compat_ptr(buf);
		iov->iov_len = (compat_size_t) len;
		uvector++;
		iov++;
	}
	ret = tot_len;

out:
	return ret;
}

static inline long
copy_iocb(long nr, u32 __user *ptr32, struct iocb __user * __user *ptr64)
{
	compat_uptr_t uptr;
	int i;

	for (i = 0; i < nr; ++i) {
		if (get_user(uptr, ptr32 + i))
			return -EFAULT;
		if (put_user(compat_ptr(uptr), ptr64 + i))
			return -EFAULT;
	}
	return 0;
}

#define MAX_AIO_SUBMITS 	(PAGE_SIZE/sizeof(struct iocb *))

COMPAT_SYSCALL_DEFINE3(io_submit, compat_aio_context_t, ctx_id,
		       int, nr, u32 __user *, iocb)
{
	struct iocb __user * __user *iocb64; 
	long ret;

	if (unlikely(nr < 0))
		return -EINVAL;

	if (nr > MAX_AIO_SUBMITS)
		nr = MAX_AIO_SUBMITS;
	
	iocb64 = compat_alloc_user_space(nr * sizeof(*iocb64));
	ret = copy_iocb(nr, iocb, iocb64);
	if (!ret)
		ret = do_io_submit(ctx_id, nr, iocb64, 1);
	return ret;
}

struct compat_ncp_mount_data {
	compat_int_t version;
	compat_uint_t ncp_fd;
	__compat_uid_t mounted_uid;
	compat_pid_t wdog_pid;
	unsigned char mounted_vol[NCP_VOLNAME_LEN + 1];
	compat_uint_t time_out;
	compat_uint_t retry_count;
	compat_uint_t flags;
	__compat_uid_t uid;
	__compat_gid_t gid;
	compat_mode_t file_mode;
	compat_mode_t dir_mode;
};

struct compat_ncp_mount_data_v4 {
	compat_int_t version;
	compat_ulong_t flags;
	compat_ulong_t mounted_uid;
	compat_long_t wdog_pid;
	compat_uint_t ncp_fd;
	compat_uint_t time_out;
	compat_uint_t retry_count;
	compat_ulong_t uid;
	compat_ulong_t gid;
	compat_ulong_t file_mode;
	compat_ulong_t dir_mode;
};

static void *do_ncp_super_data_conv(void *raw_data)
{
	int version = *(unsigned int *)raw_data;

	if (version == 3) {
		struct compat_ncp_mount_data *c_n = raw_data;
		struct ncp_mount_data *n = raw_data;

		n->dir_mode = c_n->dir_mode;
		n->file_mode = c_n->file_mode;
		n->gid = c_n->gid;
		n->uid = c_n->uid;
		memmove (n->mounted_vol, c_n->mounted_vol, (sizeof (c_n->mounted_vol) + 3 * sizeof (unsigned int)));
		n->wdog_pid = c_n->wdog_pid;
		n->mounted_uid = c_n->mounted_uid;
	} else if (version == 4) {
		struct compat_ncp_mount_data_v4 *c_n = raw_data;
		struct ncp_mount_data_v4 *n = raw_data;

		n->dir_mode = c_n->dir_mode;
		n->file_mode = c_n->file_mode;
		n->gid = c_n->gid;
		n->uid = c_n->uid;
		n->retry_count = c_n->retry_count;
		n->time_out = c_n->time_out;
		n->ncp_fd = c_n->ncp_fd;
		n->wdog_pid = c_n->wdog_pid;
		n->mounted_uid = c_n->mounted_uid;
		n->flags = c_n->flags;
	} else if (version != 5) {
		return NULL;
	}

	return raw_data;
}


struct compat_nfs_string {
	compat_uint_t len;
	compat_uptr_t data;
};

static inline void compat_nfs_string(struct nfs_string *dst,
				     struct compat_nfs_string *src)
{
	dst->data = compat_ptr(src->data);
	dst->len = src->len;
}

struct compat_nfs4_mount_data_v1 {
	compat_int_t version;
	compat_int_t flags;
	compat_int_t rsize;
	compat_int_t wsize;
	compat_int_t timeo;
	compat_int_t retrans;
	compat_int_t acregmin;
	compat_int_t acregmax;
	compat_int_t acdirmin;
	compat_int_t acdirmax;
	struct compat_nfs_string client_addr;
	struct compat_nfs_string mnt_path;
	struct compat_nfs_string hostname;
	compat_uint_t host_addrlen;
	compat_uptr_t host_addr;
	compat_int_t proto;
	compat_int_t auth_flavourlen;
	compat_uptr_t auth_flavours;
};

static int do_nfs4_super_data_conv(void *raw_data)
{
	int version = *(compat_uint_t *) raw_data;

	if (version == 1) {
		struct compat_nfs4_mount_data_v1 *raw = raw_data;
		struct nfs4_mount_data *real = raw_data;

		/* copy the fields backwards */
		real->auth_flavours = compat_ptr(raw->auth_flavours);
		real->auth_flavourlen = raw->auth_flavourlen;
		real->proto = raw->proto;
		real->host_addr = compat_ptr(raw->host_addr);
		real->host_addrlen = raw->host_addrlen;
		compat_nfs_string(&real->hostname, &raw->hostname);
		compat_nfs_string(&real->mnt_path, &raw->mnt_path);
		compat_nfs_string(&real->client_addr, &raw->client_addr);
		real->acdirmax = raw->acdirmax;
		real->acdirmin = raw->acdirmin;
		real->acregmax = raw->acregmax;
		real->acregmin = raw->acregmin;
		real->retrans = raw->retrans;
		real->timeo = raw->timeo;
		real->wsize = raw->wsize;
		real->rsize = raw->rsize;
		real->flags = raw->flags;
		real->version = raw->version;
	}

	return 0;
}

#define NCPFS_NAME      "ncpfs"
#define NFS4_NAME	"nfs4"

COMPAT_SYSCALL_DEFINE5(mount, const char __user *, dev_name,
		       const char __user *, dir_name,
		       const char __user *, type, compat_ulong_t, flags,
		       const void __user *, data)
{
	char *kernel_type;
	void *options;
	char *kernel_dev;
	int retval;

	kernel_type = copy_mount_string(type);
	retval = PTR_ERR(kernel_type);
	if (IS_ERR(kernel_type))
		goto out;

	kernel_dev = copy_mount_string(dev_name);
	retval = PTR_ERR(kernel_dev);
	if (IS_ERR(kernel_dev))
		goto out1;

	options = copy_mount_options(data);
	retval = PTR_ERR(options);
	if (IS_ERR(options))
		goto out2;

	if (kernel_type && options) {
		if (!strcmp(kernel_type, NCPFS_NAME)) {
			do_ncp_super_data_conv(options);
		} else if (!strcmp(kernel_type, NFS4_NAME)) {
			retval = -EINVAL;
			if (do_nfs4_super_data_conv(options))
				goto out3;
		}
	}

	retval = do_mount(kernel_dev, dir_name, kernel_type, flags, options);

 out3:
	kfree(options);
 out2:
	kfree(kernel_dev);
 out1:
	kfree(kernel_type);
 out:
	return retval;
}

struct compat_old_linux_dirent {
	compat_ulong_t	d_ino;
	compat_ulong_t	d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct compat_readdir_callback {
	struct dir_context ctx;
	struct compat_old_linux_dirent __user *dirent;
	int result;
};

static int compat_fillonedir(struct dir_context *ctx, const char *name,
			     int namlen, loff_t offset, u64 ino,
			     unsigned int d_type)
{
	struct compat_readdir_callback *buf =
		container_of(ctx, struct compat_readdir_callback, ctx);
	struct compat_old_linux_dirent __user *dirent;
	compat_ulong_t d_ino;

	if (buf->result)
		return -EINVAL;
	d_ino = ino;
	if (sizeof(d_ino) < sizeof(ino) && d_ino != ino) {
		buf->result = -EOVERFLOW;
		return -EOVERFLOW;
	}
	buf->result++;
	dirent = buf->dirent;
	if (!access_ok(VERIFY_WRITE, dirent,
			(unsigned long)(dirent->d_name + namlen + 1) -
				(unsigned long)dirent))
		goto efault;
	if (	__put_user(d_ino, &dirent->d_ino) ||
		__put_user(offset, &dirent->d_offset) ||
		__put_user(namlen, &dirent->d_namlen) ||
		__copy_to_user(dirent->d_name, name, namlen) ||
		__put_user(0, dirent->d_name + namlen))
		goto efault;
	return 0;
efault:
	buf->result = -EFAULT;
	return -EFAULT;
}

COMPAT_SYSCALL_DEFINE3(old_readdir, unsigned int, fd,
		struct compat_old_linux_dirent __user *, dirent, unsigned int, count)
{
	int error;
	struct fd f = fdget(fd);
	struct compat_readdir_callback buf = {
		.ctx.actor = compat_fillonedir,
		.dirent = dirent
	};

	if (!f.file)
		return -EBADF;

	error = iterate_dir(f.file, &buf.ctx);
	if (buf.result)
		error = buf.result;

	fdput(f);
	return error;
}

struct compat_linux_dirent {
	compat_ulong_t	d_ino;
	compat_ulong_t	d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct compat_getdents_callback {
	struct dir_context ctx;
	struct compat_linux_dirent __user *current_dir;
	struct compat_linux_dirent __user *previous;
	int count;
	int error;
};

static int compat_filldir(struct dir_context *ctx, const char *name, int namlen,
		loff_t offset, u64 ino, unsigned int d_type)
{
	struct compat_linux_dirent __user * dirent;
	struct compat_getdents_callback *buf =
		container_of(ctx, struct compat_getdents_callback, ctx);
	compat_ulong_t d_ino;
	int reclen = ALIGN(offsetof(struct compat_linux_dirent, d_name) +
		namlen + 2, sizeof(compat_long_t));

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	d_ino = ino;
	if (sizeof(d_ino) < sizeof(ino) && d_ino != ino) {
		buf->error = -EOVERFLOW;
		return -EOVERFLOW;
	}
	dirent = buf->previous;
	if (dirent) {
		if (__put_user(offset, &dirent->d_off))
			goto efault;
	}
	dirent = buf->current_dir;
	if (__put_user(d_ino, &dirent->d_ino))
		goto efault;
	if (__put_user(reclen, &dirent->d_reclen))
		goto efault;
	if (copy_to_user(dirent->d_name, name, namlen))
		goto efault;
	if (__put_user(0, dirent->d_name + namlen))
		goto efault;
	if (__put_user(d_type, (char  __user *) dirent + reclen - 1))
		goto efault;
	buf->previous = dirent;
	dirent = (void __user *)dirent + reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
efault:
	buf->error = -EFAULT;
	return -EFAULT;
}

COMPAT_SYSCALL_DEFINE3(getdents, unsigned int, fd,
		struct compat_linux_dirent __user *, dirent, unsigned int, count)
{
	struct fd f;
	struct compat_linux_dirent __user * lastdirent;
	struct compat_getdents_callback buf = {
		.ctx.actor = compat_filldir,
		.current_dir = dirent,
		.count = count
	};
	int error;

	if (!access_ok(VERIFY_WRITE, dirent, count))
		return -EFAULT;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;

	error = iterate_dir(f.file, &buf.ctx);
	if (error >= 0)
		error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		if (put_user(buf.ctx.pos, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}
	fdput(f);
	return error;
}

#ifdef __ARCH_WANT_COMPAT_SYS_GETDENTS64

struct compat_getdents_callback64 {
	struct dir_context ctx;
	struct linux_dirent64 __user *current_dir;
	struct linux_dirent64 __user *previous;
	int count;
	int error;
};

static int compat_filldir64(struct dir_context *ctx, const char *name,
			    int namlen, loff_t offset, u64 ino,
			    unsigned int d_type)
{
	struct linux_dirent64 __user *dirent;
	struct compat_getdents_callback64 *buf =
		container_of(ctx, struct compat_getdents_callback64, ctx);
	int reclen = ALIGN(offsetof(struct linux_dirent64, d_name) + namlen + 1,
		sizeof(u64));
	u64 off;

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;

	if (dirent) {
		if (__put_user_unaligned(offset, &dirent->d_off))
			goto efault;
	}
	dirent = buf->current_dir;
	if (__put_user_unaligned(ino, &dirent->d_ino))
		goto efault;
	off = 0;
	if (__put_user_unaligned(off, &dirent->d_off))
		goto efault;
	if (__put_user(reclen, &dirent->d_reclen))
		goto efault;
	if (__put_user(d_type, &dirent->d_type))
		goto efault;
	if (copy_to_user(dirent->d_name, name, namlen))
		goto efault;
	if (__put_user(0, dirent->d_name + namlen))
		goto efault;
	buf->previous = dirent;
	dirent = (void __user *)dirent + reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
efault:
	buf->error = -EFAULT;
	return -EFAULT;
}

COMPAT_SYSCALL_DEFINE3(getdents64, unsigned int, fd,
		struct linux_dirent64 __user *, dirent, unsigned int, count)
{
	struct fd f;
	struct linux_dirent64 __user * lastdirent;
	struct compat_getdents_callback64 buf = {
		.ctx.actor = compat_filldir64,
		.current_dir = dirent,
		.count = count
	};
	int error;

	if (!access_ok(VERIFY_WRITE, dirent, count))
		return -EFAULT;

	f = fdget(fd);
	if (!f.file)
		return -EBADF;

	error = iterate_dir(f.file, &buf.ctx);
	if (error >= 0)
		error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		typeof(lastdirent->d_off) d_off = buf.ctx.pos;
		if (__put_user_unaligned(d_off, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}
	fdput(f);
	return error;
}
#endif /* __ARCH_WANT_COMPAT_SYS_GETDENTS64 */

/*
 * Exactly like fs/open.c:sys_open(), except that it doesn't set the
 * O_LARGEFILE flag.
 */
COMPAT_SYSCALL_DEFINE3(open, const char __user *, filename, int, flags, umode_t, mode)
{
	return do_sys_open(AT_FDCWD, filename, flags, mode);
}

/*
 * Exactly like fs/open.c:sys_openat(), except that it doesn't set the
 * O_LARGEFILE flag.
 */
COMPAT_SYSCALL_DEFINE4(openat, int, dfd, const char __user *, filename, int, flags, umode_t, mode)
{
	return do_sys_open(dfd, filename, flags, mode);
}

#define __COMPAT_NFDBITS       (8 * sizeof(compat_ulong_t))

static int poll_select_copy_remaining(struct timespec *end_time, void __user *p,
				      int timeval, int ret)
{
	struct timespec ts;

	if (!p)
		return ret;

	if (current->personality & STICKY_TIMEOUTS)
		goto sticky;

	/* No update for zero timeout */
	if (!end_time->tv_sec && !end_time->tv_nsec)
		return ret;

	ktime_get_ts(&ts);
	ts = timespec_sub(*end_time, ts);
	if (ts.tv_sec < 0)
		ts.tv_sec = ts.tv_nsec = 0;

	if (timeval) {
		struct compat_timeval rtv;

		rtv.tv_sec = ts.tv_sec;
		rtv.tv_usec = ts.tv_nsec / NSEC_PER_USEC;

		if (!copy_to_user(p, &rtv, sizeof(rtv)))
			return ret;
	} else {
		struct compat_timespec rts;

		rts.tv_sec = ts.tv_sec;
		rts.tv_nsec = ts.tv_nsec;

		if (!copy_to_user(p, &rts, sizeof(rts)))
			return ret;
	}
	/*
	 * If an application puts its timeval in read-only memory, we
	 * don't want the Linux-specific update to the timeval to
	 * cause a fault after the select has completed
	 * successfully. However, because we're not updating the
	 * timeval, we can't restart the system call.
	 */

sticky:
	if (ret == -ERESTARTNOHAND)
		ret = -EINTR;
	return ret;
}

/*
 * Ooo, nasty.  We need here to frob 32-bit unsigned longs to
 * 64-bit unsigned longs.
 */
static
int compat_get_fd_set(unsigned long nr, compat_ulong_t __user *ufdset,
			unsigned long *fdset)
{
	nr = DIV_ROUND_UP(nr, __COMPAT_NFDBITS);
	if (ufdset) {
		unsigned long odd;

		if (!access_ok(VERIFY_WRITE, ufdset, nr*sizeof(compat_ulong_t)))
			return -EFAULT;

		odd = nr & 1UL;
		nr &= ~1UL;
		while (nr) {
			unsigned long h, l;
			if (__get_user(l, ufdset) || __get_user(h, ufdset+1))
				return -EFAULT;
			ufdset += 2;
			*fdset++ = h << 32 | l;
			nr -= 2;
		}
		if (odd && __get_user(*fdset, ufdset))
			return -EFAULT;
	} else {
		/* Tricky, must clear full unsigned long in the
		 * kernel fdset at the end, this makes sure that
		 * actually happens.
		 */
		memset(fdset, 0, ((nr + 1) & ~1)*sizeof(compat_ulong_t));
	}
	return 0;
}

static
int compat_set_fd_set(unsigned long nr, compat_ulong_t __user *ufdset,
		      unsigned long *fdset)
{
	unsigned long odd;
	nr = DIV_ROUND_UP(nr, __COMPAT_NFDBITS);

	if (!ufdset)
		return 0;

	odd = nr & 1UL;
	nr &= ~1UL;
	while (nr) {
		unsigned long h, l;
		l = *fdset++;
		h = l >> 32;
		if (__put_user(l, ufdset) || __put_user(h, ufdset+1))
			return -EFAULT;
		ufdset += 2;
		nr -= 2;
	}
	if (odd && __put_user(*fdset, ufdset))
		return -EFAULT;
	return 0;
}


/*
 * This is a virtual copy of sys_select from fs/select.c and probably
 * should be compared to it from time to time
 */

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
int compat_core_sys_select(int n, compat_ulong_t __user *inp,
	compat_ulong_t __user *outp, compat_ulong_t __user *exp,
	struct timespec *end_time)
{
	fd_set_bits fds;
	void *bits;
	int size, max_fds, ret = -EINVAL;
	struct fdtable *fdt;
	long stack_fds[SELECT_STACK_ALLOC/sizeof(long)];

	if (n < 0)
		goto out_nofds;

	/* max_fds can increase, so grab it once to avoid race */
	rcu_read_lock();
	fdt = files_fdtable(current->files);
	max_fds = fdt->max_fds;
	rcu_read_unlock();
	if (n > max_fds)
		n = max_fds;

	/*
	 * We need 6 bitmaps (in/out/ex for both incoming and outgoing),
	 * since we used fdset we need to allocate memory in units of
	 * long-words.
	 */
	size = FDS_BYTES(n);
	bits = stack_fds;
	if (size > sizeof(stack_fds) / 6) {
		bits = kmalloc(6 * size, GFP_KERNEL);
		ret = -ENOMEM;
		if (!bits)
			goto out_nofds;
	}
	fds.in      = (unsigned long *)  bits;
	fds.out     = (unsigned long *) (bits +   size);
	fds.ex      = (unsigned long *) (bits + 2*size);
	fds.res_in  = (unsigned long *) (bits + 3*size);
	fds.res_out = (unsigned long *) (bits + 4*size);
	fds.res_ex  = (unsigned long *) (bits + 5*size);

	if ((ret = compat_get_fd_set(n, inp, fds.in)) ||
	    (ret = compat_get_fd_set(n, outp, fds.out)) ||
	    (ret = compat_get_fd_set(n, exp, fds.ex)))
		goto out;
	zero_fd_set(n, fds.res_in);
	zero_fd_set(n, fds.res_out);
	zero_fd_set(n, fds.res_ex);

	ret = do_select(n, &fds, end_time);

	if (ret < 0)
		goto out;
	if (!ret) {
		ret = -ERESTARTNOHAND;
		if (signal_pending(current))
			goto out;
		ret = 0;
	}

	if (compat_set_fd_set(n, inp, fds.res_in) ||
	    compat_set_fd_set(n, outp, fds.res_out) ||
	    compat_set_fd_set(n, exp, fds.res_ex))
		ret = -EFAULT;
out:
	if (bits != stack_fds)
		kfree(bits);
out_nofds:
	return ret;
}

COMPAT_SYSCALL_DEFINE5(select, int, n, compat_ulong_t __user *, inp,
	compat_ulong_t __user *, outp, compat_ulong_t __user *, exp,
	struct compat_timeval __user *, tvp)
{
	struct timespec end_time, *to = NULL;
	struct compat_timeval tv;
	int ret;

	if (tvp) {
		if (copy_from_user(&tv, tvp, sizeof(tv)))
			return -EFAULT;

		to = &end_time;
		if (poll_select_set_timeout(to,
				tv.tv_sec + (tv.tv_usec / USEC_PER_SEC),
				(tv.tv_usec % USEC_PER_SEC) * NSEC_PER_USEC))
			return -EINVAL;
	}

	ret = compat_core_sys_select(n, inp, outp, exp, to);
	ret = poll_select_copy_remaining(&end_time, tvp, 1, ret);

	return ret;
}

struct compat_sel_arg_struct {
	compat_ulong_t n;
	compat_uptr_t inp;
	compat_uptr_t outp;
	compat_uptr_t exp;
	compat_uptr_t tvp;
};

COMPAT_SYSCALL_DEFINE1(old_select, struct compat_sel_arg_struct __user *, arg)
{
	struct compat_sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	return compat_sys_select(a.n, compat_ptr(a.inp), compat_ptr(a.outp),
				 compat_ptr(a.exp), compat_ptr(a.tvp));
}

static long do_compat_pselect(int n, compat_ulong_t __user *inp,
	compat_ulong_t __user *outp, compat_ulong_t __user *exp,
	struct compat_timespec __user *tsp, compat_sigset_t __user *sigmask,
	compat_size_t sigsetsize)
{
	compat_sigset_t ss32;
	sigset_t ksigmask, sigsaved;
	struct compat_timespec ts;
	struct timespec end_time, *to = NULL;
	int ret;

	if (tsp) {
		if (copy_from_user(&ts, tsp, sizeof(ts)))
			return -EFAULT;

		to = &end_time;
		if (poll_select_set_timeout(to, ts.tv_sec, ts.tv_nsec))
			return -EINVAL;
	}

	if (sigmask) {
		if (sigsetsize != sizeof(compat_sigset_t))
			return -EINVAL;
		if (copy_from_user(&ss32, sigmask, sizeof(ss32)))
			return -EFAULT;
		sigset_from_compat(&ksigmask, &ss32);

		sigdelsetmask(&ksigmask, sigmask(SIGKILL)|sigmask(SIGSTOP));
		sigprocmask(SIG_SETMASK, &ksigmask, &sigsaved);
	}

	ret = compat_core_sys_select(n, inp, outp, exp, to);
	ret = poll_select_copy_remaining(&end_time, tsp, 0, ret);

	if (ret == -ERESTARTNOHAND) {
		/*
		 * Don't restore the signal mask yet. Let do_signal() deliver
		 * the signal on the way back to userspace, before the signal
		 * mask is restored.
		 */
		if (sigmask) {
			memcpy(&current->saved_sigmask, &sigsaved,
					sizeof(sigsaved));
			set_restore_sigmask();
		}
	} else if (sigmask)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	return ret;
}

COMPAT_SYSCALL_DEFINE6(pselect6, int, n, compat_ulong_t __user *, inp,
	compat_ulong_t __user *, outp, compat_ulong_t __user *, exp,
	struct compat_timespec __user *, tsp, void __user *, sig)
{
	compat_size_t sigsetsize = 0;
	compat_uptr_t up = 0;

	if (sig) {
		if (!access_ok(VERIFY_READ, sig,
				sizeof(compat_uptr_t)+sizeof(compat_size_t)) ||
		    	__get_user(up, (compat_uptr_t __user *)sig) ||
		    	__get_user(sigsetsize,
				(compat_size_t __user *)(sig+sizeof(up))))
			return -EFAULT;
	}
	return do_compat_pselect(n, inp, outp, exp, tsp, compat_ptr(up),
				 sigsetsize);
}

COMPAT_SYSCALL_DEFINE5(ppoll, struct pollfd __user *, ufds,
	unsigned int,  nfds, struct compat_timespec __user *, tsp,
	const compat_sigset_t __user *, sigmask, compat_size_t, sigsetsize)
{
	compat_sigset_t ss32;
	sigset_t ksigmask, sigsaved;
	struct compat_timespec ts;
	struct timespec end_time, *to = NULL;
	int ret;

	if (tsp) {
		if (copy_from_user(&ts, tsp, sizeof(ts)))
			return -EFAULT;

		to = &end_time;
		if (poll_select_set_timeout(to, ts.tv_sec, ts.tv_nsec))
			return -EINVAL;
	}

	if (sigmask) {
		if (sigsetsize != sizeof(compat_sigset_t))
			return -EINVAL;
		if (copy_from_user(&ss32, sigmask, sizeof(ss32)))
			return -EFAULT;
		sigset_from_compat(&ksigmask, &ss32);

		sigdelsetmask(&ksigmask, sigmask(SIGKILL)|sigmask(SIGSTOP));
		sigprocmask(SIG_SETMASK, &ksigmask, &sigsaved);
	}

	ret = do_sys_poll(ufds, nfds, to);

	/* We can restart this syscall, usually */
	if (ret == -EINTR) {
		/*
		 * Don't restore the signal mask yet. Let do_signal() deliver
		 * the signal on the way back to userspace, before the signal
		 * mask is restored.
		 */
		if (sigmask) {
			memcpy(&current->saved_sigmask, &sigsaved,
				sizeof(sigsaved));
			set_restore_sigmask();
		}
		ret = -ERESTARTNOHAND;
	} else if (sigmask)
		sigprocmask(SIG_SETMASK, &sigsaved, NULL);

	ret = poll_select_copy_remaining(&end_time, tsp, 0, ret);

	return ret;
}

#ifdef CONFIG_FHANDLE
/*
 * Exactly like fs/open.c:sys_open_by_handle_at(), except that it
 * doesn't set the O_LARGEFILE flag.
 */
COMPAT_SYSCALL_DEFINE3(open_by_handle_at, int, mountdirfd,
			     struct file_handle __user *, handle, int, flags)
{
	return do_handle_open(mountdirfd, handle, flags);
}
#endif
