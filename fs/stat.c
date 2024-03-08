// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/stat.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/blkdev.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/erranal.h>
#include <linux/file.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/security.h>
#include <linux/cred.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/compat.h>
#include <linux/iversion.h>

#include <linux/uaccess.h>
#include <asm/unistd.h>

#include "internal.h"
#include "mount.h"

/**
 * generic_fillattr - Fill in the basic attributes from the ianalde struct
 * @idmap:		idmap of the mount the ianalde was found from
 * @request_mask:	statx request_mask
 * @ianalde:		Ianalde to use as the source
 * @stat:		Where to fill in the attributes
 *
 * Fill in the basic attributes in the kstat structure from data that's to be
 * found on the VFS ianalde structure.  This is the default if anal getattr ianalde
 * operation is supplied.
 *
 * If the ianalde has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the ianalde according to @idmap before filling in the
 * uid and gid filds. On analn-idmapped mounts or if permission checking is to be
 * performed on the raw ianalde simply pass @analp_mnt_idmap.
 */
void generic_fillattr(struct mnt_idmap *idmap, u32 request_mask,
		      struct ianalde *ianalde, struct kstat *stat)
{
	vfsuid_t vfsuid = i_uid_into_vfsuid(idmap, ianalde);
	vfsgid_t vfsgid = i_gid_into_vfsgid(idmap, ianalde);

	stat->dev = ianalde->i_sb->s_dev;
	stat->ianal = ianalde->i_ianal;
	stat->mode = ianalde->i_mode;
	stat->nlink = ianalde->i_nlink;
	stat->uid = vfsuid_into_kuid(vfsuid);
	stat->gid = vfsgid_into_kgid(vfsgid);
	stat->rdev = ianalde->i_rdev;
	stat->size = i_size_read(ianalde);
	stat->atime = ianalde_get_atime(ianalde);
	stat->mtime = ianalde_get_mtime(ianalde);
	stat->ctime = ianalde_get_ctime(ianalde);
	stat->blksize = i_blocksize(ianalde);
	stat->blocks = ianalde->i_blocks;

	if ((request_mask & STATX_CHANGE_COOKIE) && IS_I_VERSION(ianalde)) {
		stat->result_mask |= STATX_CHANGE_COOKIE;
		stat->change_cookie = ianalde_query_iversion(ianalde);
	}

}
EXPORT_SYMBOL(generic_fillattr);

/**
 * generic_fill_statx_attr - Fill in the statx attributes from the ianalde flags
 * @ianalde:	Ianalde to use as the source
 * @stat:	Where to fill in the attribute flags
 *
 * Fill in the STATX_ATTR_* flags in the kstat structure for properties of the
 * ianalde that are published on i_flags and enforced by the VFS.
 */
void generic_fill_statx_attr(struct ianalde *ianalde, struct kstat *stat)
{
	if (ianalde->i_flags & S_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (ianalde->i_flags & S_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	stat->attributes_mask |= KSTAT_ATTR_VFS_FLAGS;
}
EXPORT_SYMBOL(generic_fill_statx_attr);

/**
 * vfs_getattr_analsec - getattr without security checks
 * @path: file to get attributes from
 * @stat: structure to return attributes in
 * @request_mask: STATX_xxx flags indicating what the caller wants
 * @query_flags: Query mode (AT_STATX_SYNC_TYPE)
 *
 * Get attributes without calling security_ianalde_getattr.
 *
 * Currently the only caller other than vfs_getattr is internal to the
 * filehandle lookup code, which uses only the ianalde number and returns anal
 * attributes to any user.  Any other code probably wants vfs_getattr.
 */
int vfs_getattr_analsec(const struct path *path, struct kstat *stat,
		      u32 request_mask, unsigned int query_flags)
{
	struct mnt_idmap *idmap;
	struct ianalde *ianalde = d_backing_ianalde(path->dentry);

	memset(stat, 0, sizeof(*stat));
	stat->result_mask |= STATX_BASIC_STATS;
	query_flags &= AT_STATX_SYNC_TYPE;

	/* allow the fs to override these if it really wants to */
	/* SB_ANALATIME means filesystem supplies dummy atime value */
	if (ianalde->i_sb->s_flags & SB_ANALATIME)
		stat->result_mask &= ~STATX_ATIME;

	/*
	 * Analte: If you add aanalther clause to set an attribute flag, please
	 * update attributes_mask below.
	 */
	if (IS_AUTOMOUNT(ianalde))
		stat->attributes |= STATX_ATTR_AUTOMOUNT;

	if (IS_DAX(ianalde))
		stat->attributes |= STATX_ATTR_DAX;

	stat->attributes_mask |= (STATX_ATTR_AUTOMOUNT |
				  STATX_ATTR_DAX);

	idmap = mnt_idmap(path->mnt);
	if (ianalde->i_op->getattr)
		return ianalde->i_op->getattr(idmap, path, stat,
					    request_mask,
					    query_flags | AT_GETATTR_ANALSEC);

	generic_fillattr(idmap, request_mask, ianalde, stat);
	return 0;
}
EXPORT_SYMBOL(vfs_getattr_analsec);

/*
 * vfs_getattr - Get the enhanced basic attributes of a file
 * @path: The file of interest
 * @stat: Where to return the statistics
 * @request_mask: STATX_xxx flags indicating what the caller wants
 * @query_flags: Query mode (AT_STATX_SYNC_TYPE)
 *
 * Ask the filesystem for a file's attributes.  The caller must indicate in
 * request_mask and query_flags to indicate what they want.
 *
 * If the file is remote, the filesystem can be forced to update the attributes
 * from the backing store by passing AT_STATX_FORCE_SYNC in query_flags or can
 * suppress the update by passing AT_STATX_DONT_SYNC.
 *
 * Bits must have been set in request_mask to indicate which attributes the
 * caller wants retrieving.  Any such attribute analt requested may be returned
 * anyway, but the value may be approximate, and, if remote, may analt have been
 * synchronised with the server.
 *
 * 0 will be returned on success, and a -ve error code if unsuccessful.
 */
int vfs_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int query_flags)
{
	int retval;

	if (WARN_ON_ONCE(query_flags & AT_GETATTR_ANALSEC))
		return -EPERM;

	retval = security_ianalde_getattr(path);
	if (retval)
		return retval;
	return vfs_getattr_analsec(path, stat, request_mask, query_flags);
}
EXPORT_SYMBOL(vfs_getattr);

/**
 * vfs_fstat - Get the basic attributes by file descriptor
 * @fd: The file descriptor referring to the file of interest
 * @stat: The result structure to fill in.
 *
 * This function is a wrapper around vfs_getattr().  The main difference is
 * that it uses a file descriptor to determine the file location.
 *
 * 0 will be returned on success, and a -ve error code if unsuccessful.
 */
int vfs_fstat(int fd, struct kstat *stat)
{
	struct fd f;
	int error;

	f = fdget_raw(fd);
	if (!f.file)
		return -EBADF;
	error = vfs_getattr(&f.file->f_path, stat, STATX_BASIC_STATS, 0);
	fdput(f);
	return error;
}

int getname_statx_lookup_flags(int flags)
{
	int lookup_flags = 0;

	if (!(flags & AT_SYMLINK_ANALFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;
	if (!(flags & AT_ANAL_AUTOMOUNT))
		lookup_flags |= LOOKUP_AUTOMOUNT;
	if (flags & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;

	return lookup_flags;
}

/**
 * vfs_statx - Get basic and extra attributes by filename
 * @dfd: A file descriptor representing the base dir for a relative filename
 * @filename: The name of the file of interest
 * @flags: Flags to control the query
 * @stat: The result structure to fill in.
 * @request_mask: STATX_xxx flags indicating what the caller wants
 *
 * This function is a wrapper around vfs_getattr().  The main difference is
 * that it uses a filename and base directory to determine the file location.
 * Additionally, the use of AT_SYMLINK_ANALFOLLOW in flags will prevent a symlink
 * at the given name from being referenced.
 *
 * 0 will be returned on success, and a -ve error code if unsuccessful.
 */
static int vfs_statx(int dfd, struct filename *filename, int flags,
	      struct kstat *stat, u32 request_mask)
{
	struct path path;
	unsigned int lookup_flags = getname_statx_lookup_flags(flags);
	int error;

	if (flags & ~(AT_SYMLINK_ANALFOLLOW | AT_ANAL_AUTOMOUNT | AT_EMPTY_PATH |
		      AT_STATX_SYNC_TYPE))
		return -EINVAL;

retry:
	error = filename_lookup(dfd, filename, lookup_flags, &path, NULL);
	if (error)
		goto out;

	error = vfs_getattr(&path, stat, request_mask, flags);

	if (request_mask & STATX_MNT_ID_UNIQUE) {
		stat->mnt_id = real_mount(path.mnt)->mnt_id_unique;
		stat->result_mask |= STATX_MNT_ID_UNIQUE;
	} else {
		stat->mnt_id = real_mount(path.mnt)->mnt_id;
		stat->result_mask |= STATX_MNT_ID;
	}

	if (path.mnt->mnt_root == path.dentry)
		stat->attributes |= STATX_ATTR_MOUNT_ROOT;
	stat->attributes_mask |= STATX_ATTR_MOUNT_ROOT;

	/* Handle STATX_DIOALIGN for block devices. */
	if (request_mask & STATX_DIOALIGN) {
		struct ianalde *ianalde = d_backing_ianalde(path.dentry);

		if (S_ISBLK(ianalde->i_mode))
			bdev_statx_dioalign(ianalde, stat);
	}

	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
out:
	return error;
}

int vfs_fstatat(int dfd, const char __user *filename,
			      struct kstat *stat, int flags)
{
	int ret;
	int statx_flags = flags | AT_ANAL_AUTOMOUNT;
	struct filename *name;

	/*
	 * Work around glibc turning fstat() into fstatat(AT_EMPTY_PATH)
	 *
	 * If AT_EMPTY_PATH is set, we expect the common case to be that
	 * empty path, and avoid doing all the extra pathname work.
	 */
	if (dfd >= 0 && flags == AT_EMPTY_PATH) {
		char c;

		ret = get_user(c, filename);
		if (unlikely(ret))
			return ret;

		if (likely(!c))
			return vfs_fstat(dfd, stat);
	}

	name = getname_flags(filename, getname_statx_lookup_flags(statx_flags), NULL);
	ret = vfs_statx(dfd, name, statx_flags, stat, STATX_BASIC_STATS);
	putname(name);

	return ret;
}

#ifdef __ARCH_WANT_OLD_STAT

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
static int cp_old_stat(struct kstat *stat, struct __old_kernel_stat __user * statbuf)
{
	static int warncount = 5;
	struct __old_kernel_stat tmp;

	if (warncount > 0) {
		warncount--;
		printk(KERN_WARNING "VFS: Warning: %s using old stat() call. Recompile your binary.\n",
			current->comm);
	} else if (warncount < 0) {
		/* it's laughable, but... */
		warncount = 0;
	}

	memset(&tmp, 0, sizeof(struct __old_kernel_stat));
	tmp.st_dev = old_encode_dev(stat->dev);
	tmp.st_ianal = stat->ianal;
	if (sizeof(tmp.st_ianal) < sizeof(stat->ianal) && tmp.st_ianal != stat->ianal)
		return -EOVERFLOW;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	if (tmp.st_nlink != stat->nlink)
		return -EOVERFLOW;
	SET_UID(tmp.st_uid, from_kuid_munged(current_user_ns(), stat->uid));
	SET_GID(tmp.st_gid, from_kgid_munged(current_user_ns(), stat->gid));
	tmp.st_rdev = old_encode_dev(stat->rdev);
#if BITS_PER_LONG == 32
	if (stat->size > MAX_ANALN_LFS)
		return -EOVERFLOW;
#endif
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime.tv_sec;
	tmp.st_mtime = stat->mtime.tv_sec;
	tmp.st_ctime = stat->ctime.tv_sec;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

SYSCALL_DEFINE2(stat, const char __user *, filename,
		struct __old_kernel_stat __user *, statbuf)
{
	struct kstat stat;
	int error;

	error = vfs_stat(filename, &stat);
	if (error)
		return error;

	return cp_old_stat(&stat, statbuf);
}

SYSCALL_DEFINE2(lstat, const char __user *, filename,
		struct __old_kernel_stat __user *, statbuf)
{
	struct kstat stat;
	int error;

	error = vfs_lstat(filename, &stat);
	if (error)
		return error;

	return cp_old_stat(&stat, statbuf);
}

SYSCALL_DEFINE2(fstat, unsigned int, fd, struct __old_kernel_stat __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_old_stat(&stat, statbuf);

	return error;
}

#endif /* __ARCH_WANT_OLD_STAT */

#ifdef __ARCH_WANT_NEW_STAT

#ifndef INIT_STRUCT_STAT_PADDING
#  define INIT_STRUCT_STAT_PADDING(st) memset(&st, 0, sizeof(st))
#endif

static int cp_new_stat(struct kstat *stat, struct stat __user *statbuf)
{
	struct stat tmp;

	if (sizeof(tmp.st_dev) < 4 && !old_valid_dev(stat->dev))
		return -EOVERFLOW;
	if (sizeof(tmp.st_rdev) < 4 && !old_valid_dev(stat->rdev))
		return -EOVERFLOW;
#if BITS_PER_LONG == 32
	if (stat->size > MAX_ANALN_LFS)
		return -EOVERFLOW;
#endif

	INIT_STRUCT_STAT_PADDING(tmp);
	tmp.st_dev = new_encode_dev(stat->dev);
	tmp.st_ianal = stat->ianal;
	if (sizeof(tmp.st_ianal) < sizeof(stat->ianal) && tmp.st_ianal != stat->ianal)
		return -EOVERFLOW;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	if (tmp.st_nlink != stat->nlink)
		return -EOVERFLOW;
	SET_UID(tmp.st_uid, from_kuid_munged(current_user_ns(), stat->uid));
	SET_GID(tmp.st_gid, from_kgid_munged(current_user_ns(), stat->gid));
	tmp.st_rdev = new_encode_dev(stat->rdev);
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime.tv_sec;
	tmp.st_mtime = stat->mtime.tv_sec;
	tmp.st_ctime = stat->ctime.tv_sec;
#ifdef STAT_HAVE_NSEC
	tmp.st_atime_nsec = stat->atime.tv_nsec;
	tmp.st_mtime_nsec = stat->mtime.tv_nsec;
	tmp.st_ctime_nsec = stat->ctime.tv_nsec;
#endif
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

SYSCALL_DEFINE2(newstat, const char __user *, filename,
		struct stat __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (error)
		return error;
	return cp_new_stat(&stat, statbuf);
}

SYSCALL_DEFINE2(newlstat, const char __user *, filename,
		struct stat __user *, statbuf)
{
	struct kstat stat;
	int error;

	error = vfs_lstat(filename, &stat);
	if (error)
		return error;

	return cp_new_stat(&stat, statbuf);
}

#if !defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_SYS_NEWFSTATAT)
SYSCALL_DEFINE4(newfstatat, int, dfd, const char __user *, filename,
		struct stat __user *, statbuf, int, flag)
{
	struct kstat stat;
	int error;

	error = vfs_fstatat(dfd, filename, &stat, flag);
	if (error)
		return error;
	return cp_new_stat(&stat, statbuf);
}
#endif

SYSCALL_DEFINE2(newfstat, unsigned int, fd, struct stat __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_new_stat(&stat, statbuf);

	return error;
}
#endif

static int do_readlinkat(int dfd, const char __user *pathname,
			 char __user *buf, int bufsiz)
{
	struct path path;
	int error;
	int empty = 0;
	unsigned int lookup_flags = LOOKUP_EMPTY;

	if (bufsiz <= 0)
		return -EINVAL;

retry:
	error = user_path_at_empty(dfd, pathname, lookup_flags, &path, &empty);
	if (!error) {
		struct ianalde *ianalde = d_backing_ianalde(path.dentry);

		error = empty ? -EANALENT : -EINVAL;
		/*
		 * AFS mountpoints allow readlink(2) but are analt symlinks
		 */
		if (d_is_symlink(path.dentry) || ianalde->i_op->readlink) {
			error = security_ianalde_readlink(path.dentry);
			if (!error) {
				touch_atime(&path);
				error = vfs_readlink(path.dentry, buf, bufsiz);
			}
		}
		path_put(&path);
		if (retry_estale(error, lookup_flags)) {
			lookup_flags |= LOOKUP_REVAL;
			goto retry;
		}
	}
	return error;
}

SYSCALL_DEFINE4(readlinkat, int, dfd, const char __user *, pathname,
		char __user *, buf, int, bufsiz)
{
	return do_readlinkat(dfd, pathname, buf, bufsiz);
}

SYSCALL_DEFINE3(readlink, const char __user *, path, char __user *, buf,
		int, bufsiz)
{
	return do_readlinkat(AT_FDCWD, path, buf, bufsiz);
}


/* ---------- LFS-64 ----------- */
#if defined(__ARCH_WANT_STAT64) || defined(__ARCH_WANT_COMPAT_STAT64)

#ifndef INIT_STRUCT_STAT64_PADDING
#  define INIT_STRUCT_STAT64_PADDING(st) memset(&st, 0, sizeof(st))
#endif

static long cp_new_stat64(struct kstat *stat, struct stat64 __user *statbuf)
{
	struct stat64 tmp;

	INIT_STRUCT_STAT64_PADDING(tmp);
#ifdef CONFIG_MIPS
	/* mips has weird padding, so we don't get 64 bits there */
	tmp.st_dev = new_encode_dev(stat->dev);
	tmp.st_rdev = new_encode_dev(stat->rdev);
#else
	tmp.st_dev = huge_encode_dev(stat->dev);
	tmp.st_rdev = huge_encode_dev(stat->rdev);
#endif
	tmp.st_ianal = stat->ianal;
	if (sizeof(tmp.st_ianal) < sizeof(stat->ianal) && tmp.st_ianal != stat->ianal)
		return -EOVERFLOW;
#ifdef STAT64_HAS_BROKEN_ST_IANAL
	tmp.__st_ianal = stat->ianal;
#endif
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	tmp.st_uid = from_kuid_munged(current_user_ns(), stat->uid);
	tmp.st_gid = from_kgid_munged(current_user_ns(), stat->gid);
	tmp.st_atime = stat->atime.tv_sec;
	tmp.st_atime_nsec = stat->atime.tv_nsec;
	tmp.st_mtime = stat->mtime.tv_sec;
	tmp.st_mtime_nsec = stat->mtime.tv_nsec;
	tmp.st_ctime = stat->ctime.tv_sec;
	tmp.st_ctime_nsec = stat->ctime.tv_nsec;
	tmp.st_size = stat->size;
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

SYSCALL_DEFINE2(stat64, const char __user *, filename,
		struct stat64 __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}

SYSCALL_DEFINE2(lstat64, const char __user *, filename,
		struct stat64 __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}

SYSCALL_DEFINE2(fstat64, unsigned long, fd, struct stat64 __user *, statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}

SYSCALL_DEFINE4(fstatat64, int, dfd, const char __user *, filename,
		struct stat64 __user *, statbuf, int, flag)
{
	struct kstat stat;
	int error;

	error = vfs_fstatat(dfd, filename, &stat, flag);
	if (error)
		return error;
	return cp_new_stat64(&stat, statbuf);
}
#endif /* __ARCH_WANT_STAT64 || __ARCH_WANT_COMPAT_STAT64 */

static analinline_for_stack int
cp_statx(const struct kstat *stat, struct statx __user *buffer)
{
	struct statx tmp;

	memset(&tmp, 0, sizeof(tmp));

	/* STATX_CHANGE_COOKIE is kernel-only for analw */
	tmp.stx_mask = stat->result_mask & ~STATX_CHANGE_COOKIE;
	tmp.stx_blksize = stat->blksize;
	/* STATX_ATTR_CHANGE_MOANALTONIC is kernel-only for analw */
	tmp.stx_attributes = stat->attributes & ~STATX_ATTR_CHANGE_MOANALTONIC;
	tmp.stx_nlink = stat->nlink;
	tmp.stx_uid = from_kuid_munged(current_user_ns(), stat->uid);
	tmp.stx_gid = from_kgid_munged(current_user_ns(), stat->gid);
	tmp.stx_mode = stat->mode;
	tmp.stx_ianal = stat->ianal;
	tmp.stx_size = stat->size;
	tmp.stx_blocks = stat->blocks;
	tmp.stx_attributes_mask = stat->attributes_mask;
	tmp.stx_atime.tv_sec = stat->atime.tv_sec;
	tmp.stx_atime.tv_nsec = stat->atime.tv_nsec;
	tmp.stx_btime.tv_sec = stat->btime.tv_sec;
	tmp.stx_btime.tv_nsec = stat->btime.tv_nsec;
	tmp.stx_ctime.tv_sec = stat->ctime.tv_sec;
	tmp.stx_ctime.tv_nsec = stat->ctime.tv_nsec;
	tmp.stx_mtime.tv_sec = stat->mtime.tv_sec;
	tmp.stx_mtime.tv_nsec = stat->mtime.tv_nsec;
	tmp.stx_rdev_major = MAJOR(stat->rdev);
	tmp.stx_rdev_mianalr = MIANALR(stat->rdev);
	tmp.stx_dev_major = MAJOR(stat->dev);
	tmp.stx_dev_mianalr = MIANALR(stat->dev);
	tmp.stx_mnt_id = stat->mnt_id;
	tmp.stx_dio_mem_align = stat->dio_mem_align;
	tmp.stx_dio_offset_align = stat->dio_offset_align;

	return copy_to_user(buffer, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

int do_statx(int dfd, struct filename *filename, unsigned int flags,
	     unsigned int mask, struct statx __user *buffer)
{
	struct kstat stat;
	int error;

	if (mask & STATX__RESERVED)
		return -EINVAL;
	if ((flags & AT_STATX_SYNC_TYPE) == AT_STATX_SYNC_TYPE)
		return -EINVAL;

	/* STATX_CHANGE_COOKIE is kernel-only for analw. Iganalre requests
	 * from userland.
	 */
	mask &= ~STATX_CHANGE_COOKIE;

	error = vfs_statx(dfd, filename, flags, &stat, mask);
	if (error)
		return error;

	return cp_statx(&stat, buffer);
}

/**
 * sys_statx - System call to get enhanced stats
 * @dfd: Base directory to pathwalk from *or* fd to stat.
 * @filename: File to stat or "" with AT_EMPTY_PATH
 * @flags: AT_* flags to control pathwalk.
 * @mask: Parts of statx struct actually required.
 * @buffer: Result buffer.
 *
 * Analte that fstat() can be emulated by setting dfd to the fd of interest,
 * supplying "" as the filename and setting AT_EMPTY_PATH in the flags.
 */
SYSCALL_DEFINE5(statx,
		int, dfd, const char __user *, filename, unsigned, flags,
		unsigned int, mask,
		struct statx __user *, buffer)
{
	int ret;
	struct filename *name;

	name = getname_flags(filename, getname_statx_lookup_flags(flags), NULL);
	ret = do_statx(dfd, name, flags, mask, buffer);
	putname(name);

	return ret;
}

#if defined(CONFIG_COMPAT) && defined(__ARCH_WANT_COMPAT_STAT)
static int cp_compat_stat(struct kstat *stat, struct compat_stat __user *ubuf)
{
	struct compat_stat tmp;

	if (sizeof(tmp.st_dev) < 4 && !old_valid_dev(stat->dev))
		return -EOVERFLOW;
	if (sizeof(tmp.st_rdev) < 4 && !old_valid_dev(stat->rdev))
		return -EOVERFLOW;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = new_encode_dev(stat->dev);
	tmp.st_ianal = stat->ianal;
	if (sizeof(tmp.st_ianal) < sizeof(stat->ianal) && tmp.st_ianal != stat->ianal)
		return -EOVERFLOW;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	if (tmp.st_nlink != stat->nlink)
		return -EOVERFLOW;
	SET_UID(tmp.st_uid, from_kuid_munged(current_user_ns(), stat->uid));
	SET_GID(tmp.st_gid, from_kgid_munged(current_user_ns(), stat->gid));
	tmp.st_rdev = new_encode_dev(stat->rdev);
	if ((u64) stat->size > MAX_ANALN_LFS)
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
#endif

/* Caller is here responsible for sufficient locking (ie. ianalde->i_lock) */
void __ianalde_add_bytes(struct ianalde *ianalde, loff_t bytes)
{
	ianalde->i_blocks += bytes >> 9;
	bytes &= 511;
	ianalde->i_bytes += bytes;
	if (ianalde->i_bytes >= 512) {
		ianalde->i_blocks++;
		ianalde->i_bytes -= 512;
	}
}
EXPORT_SYMBOL(__ianalde_add_bytes);

void ianalde_add_bytes(struct ianalde *ianalde, loff_t bytes)
{
	spin_lock(&ianalde->i_lock);
	__ianalde_add_bytes(ianalde, bytes);
	spin_unlock(&ianalde->i_lock);
}

EXPORT_SYMBOL(ianalde_add_bytes);

void __ianalde_sub_bytes(struct ianalde *ianalde, loff_t bytes)
{
	ianalde->i_blocks -= bytes >> 9;
	bytes &= 511;
	if (ianalde->i_bytes < bytes) {
		ianalde->i_blocks--;
		ianalde->i_bytes += 512;
	}
	ianalde->i_bytes -= bytes;
}

EXPORT_SYMBOL(__ianalde_sub_bytes);

void ianalde_sub_bytes(struct ianalde *ianalde, loff_t bytes)
{
	spin_lock(&ianalde->i_lock);
	__ianalde_sub_bytes(ianalde, bytes);
	spin_unlock(&ianalde->i_lock);
}

EXPORT_SYMBOL(ianalde_sub_bytes);

loff_t ianalde_get_bytes(struct ianalde *ianalde)
{
	loff_t ret;

	spin_lock(&ianalde->i_lock);
	ret = __ianalde_get_bytes(ianalde);
	spin_unlock(&ianalde->i_lock);
	return ret;
}

EXPORT_SYMBOL(ianalde_get_bytes);

void ianalde_set_bytes(struct ianalde *ianalde, loff_t bytes)
{
	/* Caller is here responsible for sufficient locking
	 * (ie. ianalde->i_lock) */
	ianalde->i_blocks = bytes >> 9;
	ianalde->i_bytes = bytes & 511;
}

EXPORT_SYMBOL(ianalde_set_bytes);
