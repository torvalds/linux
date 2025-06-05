// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/stat.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/blkdev.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/errno.h>
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

#include <trace/events/timestamp.h>

#include "internal.h"
#include "mount.h"

/**
 * fill_mg_cmtime - Fill in the mtime and ctime and flag ctime as QUERIED
 * @stat: where to store the resulting values
 * @request_mask: STATX_* values requested
 * @inode: inode from which to grab the c/mtime
 *
 * Given @inode, grab the ctime and mtime out if it and store the result
 * in @stat. When fetching the value, flag it as QUERIED (if not already)
 * so the next write will record a distinct timestamp.
 *
 * NB: The QUERIED flag is tracked in the ctime, but we set it there even
 * if only the mtime was requested, as that ensures that the next mtime
 * change will be distinct.
 */
void fill_mg_cmtime(struct kstat *stat, u32 request_mask, struct inode *inode)
{
	atomic_t *pcn = (atomic_t *)&inode->i_ctime_nsec;

	/* If neither time was requested, then don't report them */
	if (!(request_mask & (STATX_CTIME|STATX_MTIME))) {
		stat->result_mask &= ~(STATX_CTIME|STATX_MTIME);
		return;
	}

	stat->mtime = inode_get_mtime(inode);
	stat->ctime.tv_sec = inode->i_ctime_sec;
	stat->ctime.tv_nsec = (u32)atomic_read(pcn);
	if (!(stat->ctime.tv_nsec & I_CTIME_QUERIED))
		stat->ctime.tv_nsec = ((u32)atomic_fetch_or(I_CTIME_QUERIED, pcn));
	stat->ctime.tv_nsec &= ~I_CTIME_QUERIED;
	trace_fill_mg_cmtime(inode, &stat->ctime, &stat->mtime);
}
EXPORT_SYMBOL(fill_mg_cmtime);

/**
 * generic_fillattr - Fill in the basic attributes from the inode struct
 * @idmap:		idmap of the mount the inode was found from
 * @request_mask:	statx request_mask
 * @inode:		Inode to use as the source
 * @stat:		Where to fill in the attributes
 *
 * Fill in the basic attributes in the kstat structure from data that's to be
 * found on the VFS inode structure.  This is the default if no getattr inode
 * operation is supplied.
 *
 * If the inode has been found through an idmapped mount the idmap of
 * the vfsmount must be passed through @idmap. This function will then
 * take care to map the inode according to @idmap before filling in the
 * uid and gid filds. On non-idmapped mounts or if permission checking is to be
 * performed on the raw inode simply pass @nop_mnt_idmap.
 */
void generic_fillattr(struct mnt_idmap *idmap, u32 request_mask,
		      struct inode *inode, struct kstat *stat)
{
	vfsuid_t vfsuid = i_uid_into_vfsuid(idmap, inode);
	vfsgid_t vfsgid = i_gid_into_vfsgid(idmap, inode);

	stat->dev = inode->i_sb->s_dev;
	stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->nlink = inode->i_nlink;
	stat->uid = vfsuid_into_kuid(vfsuid);
	stat->gid = vfsgid_into_kgid(vfsgid);
	stat->rdev = inode->i_rdev;
	stat->size = i_size_read(inode);
	stat->atime = inode_get_atime(inode);

	if (is_mgtime(inode)) {
		fill_mg_cmtime(stat, request_mask, inode);
	} else {
		stat->ctime = inode_get_ctime(inode);
		stat->mtime = inode_get_mtime(inode);
	}

	stat->blksize = i_blocksize(inode);
	stat->blocks = inode->i_blocks;

	if ((request_mask & STATX_CHANGE_COOKIE) && IS_I_VERSION(inode)) {
		stat->result_mask |= STATX_CHANGE_COOKIE;
		stat->change_cookie = inode_query_iversion(inode);
	}

}
EXPORT_SYMBOL(generic_fillattr);

/**
 * generic_fill_statx_attr - Fill in the statx attributes from the inode flags
 * @inode:	Inode to use as the source
 * @stat:	Where to fill in the attribute flags
 *
 * Fill in the STATX_ATTR_* flags in the kstat structure for properties of the
 * inode that are published on i_flags and enforced by the VFS.
 */
void generic_fill_statx_attr(struct inode *inode, struct kstat *stat)
{
	if (inode->i_flags & S_IMMUTABLE)
		stat->attributes |= STATX_ATTR_IMMUTABLE;
	if (inode->i_flags & S_APPEND)
		stat->attributes |= STATX_ATTR_APPEND;
	stat->attributes_mask |= KSTAT_ATTR_VFS_FLAGS;
}
EXPORT_SYMBOL(generic_fill_statx_attr);

/**
 * generic_fill_statx_atomic_writes - Fill in atomic writes statx attributes
 * @stat:	Where to fill in the attribute flags
 * @unit_min:	Minimum supported atomic write length in bytes
 * @unit_max:	Maximum supported atomic write length in bytes
 *
 * Fill in the STATX{_ATTR}_WRITE_ATOMIC flags in the kstat structure from
 * atomic write unit_min and unit_max values.
 */
void generic_fill_statx_atomic_writes(struct kstat *stat,
				      unsigned int unit_min,
				      unsigned int unit_max)
{
	/* Confirm that the request type is known */
	stat->result_mask |= STATX_WRITE_ATOMIC;

	/* Confirm that the file attribute type is known */
	stat->attributes_mask |= STATX_ATTR_WRITE_ATOMIC;

	if (unit_min) {
		stat->atomic_write_unit_min = unit_min;
		stat->atomic_write_unit_max = unit_max;
		/* Initially only allow 1x segment */
		stat->atomic_write_segments_max = 1;

		/* Confirm atomic writes are actually supported */
		stat->attributes |= STATX_ATTR_WRITE_ATOMIC;
	}
}
EXPORT_SYMBOL_GPL(generic_fill_statx_atomic_writes);

/**
 * vfs_getattr_nosec - getattr without security checks
 * @path: file to get attributes from
 * @stat: structure to return attributes in
 * @request_mask: STATX_xxx flags indicating what the caller wants
 * @query_flags: Query mode (AT_STATX_SYNC_TYPE)
 *
 * Get attributes without calling security_inode_getattr.
 *
 * Currently the only caller other than vfs_getattr is internal to the
 * filehandle lookup code, which uses only the inode number and returns no
 * attributes to any user.  Any other code probably wants vfs_getattr.
 */
int vfs_getattr_nosec(const struct path *path, struct kstat *stat,
		      u32 request_mask, unsigned int query_flags)
{
	struct mnt_idmap *idmap;
	struct inode *inode = d_backing_inode(path->dentry);

	memset(stat, 0, sizeof(*stat));
	stat->result_mask |= STATX_BASIC_STATS;
	query_flags &= AT_STATX_SYNC_TYPE;

	/* allow the fs to override these if it really wants to */
	/* SB_NOATIME means filesystem supplies dummy atime value */
	if (inode->i_sb->s_flags & SB_NOATIME)
		stat->result_mask &= ~STATX_ATIME;

	/*
	 * Note: If you add another clause to set an attribute flag, please
	 * update attributes_mask below.
	 */
	if (IS_AUTOMOUNT(inode))
		stat->attributes |= STATX_ATTR_AUTOMOUNT;

	if (IS_DAX(inode))
		stat->attributes |= STATX_ATTR_DAX;

	stat->attributes_mask |= (STATX_ATTR_AUTOMOUNT |
				  STATX_ATTR_DAX);

	idmap = mnt_idmap(path->mnt);
	if (inode->i_op->getattr) {
		int ret;

		ret = inode->i_op->getattr(idmap, path, stat, request_mask,
				query_flags);
		if (ret)
			return ret;
	} else {
		generic_fillattr(idmap, request_mask, inode, stat);
	}

	/*
	 * If this is a block device inode, override the filesystem attributes
	 * with the block device specific parameters that need to be obtained
	 * from the bdev backing inode.
	 */
	if (S_ISBLK(stat->mode))
		bdev_statx(path, stat, request_mask);

	return 0;
}
EXPORT_SYMBOL(vfs_getattr_nosec);

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
 * caller wants retrieving.  Any such attribute not requested may be returned
 * anyway, but the value may be approximate, and, if remote, may not have been
 * synchronised with the server.
 *
 * 0 will be returned on success, and a -ve error code if unsuccessful.
 */
int vfs_getattr(const struct path *path, struct kstat *stat,
		u32 request_mask, unsigned int query_flags)
{
	int retval;

	retval = security_inode_getattr(path);
	if (retval)
		return retval;
	return vfs_getattr_nosec(path, stat, request_mask, query_flags);
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
	CLASS(fd_raw, f)(fd);
	if (fd_empty(f))
		return -EBADF;
	return vfs_getattr(&fd_file(f)->f_path, stat, STATX_BASIC_STATS, 0);
}

static int statx_lookup_flags(int flags)
{
	int lookup_flags = 0;

	if (!(flags & AT_SYMLINK_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;
	if (!(flags & AT_NO_AUTOMOUNT))
		lookup_flags |= LOOKUP_AUTOMOUNT;

	return lookup_flags;
}

static int vfs_statx_path(struct path *path, int flags, struct kstat *stat,
			  u32 request_mask)
{
	int error = vfs_getattr(path, stat, request_mask, flags);
	if (error)
		return error;

	if (request_mask & STATX_MNT_ID_UNIQUE) {
		stat->mnt_id = real_mount(path->mnt)->mnt_id_unique;
		stat->result_mask |= STATX_MNT_ID_UNIQUE;
	} else {
		stat->mnt_id = real_mount(path->mnt)->mnt_id;
		stat->result_mask |= STATX_MNT_ID;
	}

	if (path_mounted(path))
		stat->attributes |= STATX_ATTR_MOUNT_ROOT;
	stat->attributes_mask |= STATX_ATTR_MOUNT_ROOT;
	return 0;
}

static int vfs_statx_fd(int fd, int flags, struct kstat *stat,
			  u32 request_mask)
{
	CLASS(fd_raw, f)(fd);
	if (fd_empty(f))
		return -EBADF;
	return vfs_statx_path(&fd_file(f)->f_path, flags, stat, request_mask);
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
 * Additionally, the use of AT_SYMLINK_NOFOLLOW in flags will prevent a symlink
 * at the given name from being referenced.
 *
 * 0 will be returned on success, and a -ve error code if unsuccessful.
 */
static int vfs_statx(int dfd, struct filename *filename, int flags,
	      struct kstat *stat, u32 request_mask)
{
	struct path path;
	unsigned int lookup_flags = statx_lookup_flags(flags);
	int error;

	if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT | AT_EMPTY_PATH |
		      AT_STATX_SYNC_TYPE))
		return -EINVAL;

retry:
	error = filename_lookup(dfd, filename, lookup_flags, &path, NULL);
	if (error)
		return error;
	error = vfs_statx_path(&path, flags, stat, request_mask);
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
	return error;
}

int vfs_fstatat(int dfd, const char __user *filename,
			      struct kstat *stat, int flags)
{
	int ret;
	int statx_flags = flags | AT_NO_AUTOMOUNT;
	struct filename *name = getname_maybe_null(filename, flags);

	if (!name && dfd >= 0)
		return vfs_fstat(dfd, stat);

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
#if BITS_PER_LONG == 32
	if (stat->size > MAX_NON_LFS)
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
	if (stat->size > MAX_NON_LFS)
		return -EOVERFLOW;
#endif

	INIT_STRUCT_STAT_PADDING(tmp);
	tmp.st_dev = new_encode_dev(stat->dev);
	tmp.st_ino = stat->ino;
	if (sizeof(tmp.st_ino) < sizeof(stat->ino) && tmp.st_ino != stat->ino)
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
	struct filename *name;
	int error;
	unsigned int lookup_flags = LOOKUP_EMPTY;

	if (bufsiz <= 0)
		return -EINVAL;

retry:
	name = getname_flags(pathname, lookup_flags);
	error = filename_lookup(dfd, name, lookup_flags, &path, NULL);
	if (unlikely(error)) {
		putname(name);
		return error;
	}

	/*
	 * AFS mountpoints allow readlink(2) but are not symlinks
	 */
	if (d_is_symlink(path.dentry) ||
	    d_backing_inode(path.dentry)->i_op->readlink) {
		error = security_inode_readlink(path.dentry);
		if (!error) {
			touch_atime(&path);
			error = vfs_readlink(path.dentry, buf, bufsiz);
		}
	} else {
		error = (name->name[0] == '\0') ? -ENOENT : -EINVAL;
	}
	path_put(&path);
	putname(name);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
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
	tmp.st_ino = stat->ino;
	if (sizeof(tmp.st_ino) < sizeof(stat->ino) && tmp.st_ino != stat->ino)
		return -EOVERFLOW;
#ifdef STAT64_HAS_BROKEN_ST_INO
	tmp.__st_ino = stat->ino;
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

static noinline_for_stack int
cp_statx(const struct kstat *stat, struct statx __user *buffer)
{
	struct statx tmp;

	memset(&tmp, 0, sizeof(tmp));

	/* STATX_CHANGE_COOKIE is kernel-only for now */
	tmp.stx_mask = stat->result_mask & ~STATX_CHANGE_COOKIE;
	tmp.stx_blksize = stat->blksize;
	/* STATX_ATTR_CHANGE_MONOTONIC is kernel-only for now */
	tmp.stx_attributes = stat->attributes & ~STATX_ATTR_CHANGE_MONOTONIC;
	tmp.stx_nlink = stat->nlink;
	tmp.stx_uid = from_kuid_munged(current_user_ns(), stat->uid);
	tmp.stx_gid = from_kgid_munged(current_user_ns(), stat->gid);
	tmp.stx_mode = stat->mode;
	tmp.stx_ino = stat->ino;
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
	tmp.stx_rdev_minor = MINOR(stat->rdev);
	tmp.stx_dev_major = MAJOR(stat->dev);
	tmp.stx_dev_minor = MINOR(stat->dev);
	tmp.stx_mnt_id = stat->mnt_id;
	tmp.stx_dio_mem_align = stat->dio_mem_align;
	tmp.stx_dio_offset_align = stat->dio_offset_align;
	tmp.stx_dio_read_offset_align = stat->dio_read_offset_align;
	tmp.stx_subvol = stat->subvol;
	tmp.stx_atomic_write_unit_min = stat->atomic_write_unit_min;
	tmp.stx_atomic_write_unit_max = stat->atomic_write_unit_max;
	tmp.stx_atomic_write_segments_max = stat->atomic_write_segments_max;

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

	/*
	 * STATX_CHANGE_COOKIE is kernel-only for now. Ignore requests
	 * from userland.
	 */
	mask &= ~STATX_CHANGE_COOKIE;

	error = vfs_statx(dfd, filename, flags, &stat, mask);
	if (error)
		return error;

	return cp_statx(&stat, buffer);
}

int do_statx_fd(int fd, unsigned int flags, unsigned int mask,
	     struct statx __user *buffer)
{
	struct kstat stat;
	int error;

	if (mask & STATX__RESERVED)
		return -EINVAL;
	if ((flags & AT_STATX_SYNC_TYPE) == AT_STATX_SYNC_TYPE)
		return -EINVAL;

	/*
	 * STATX_CHANGE_COOKIE is kernel-only for now. Ignore requests
	 * from userland.
	 */
	mask &= ~STATX_CHANGE_COOKIE;

	error = vfs_statx_fd(fd, flags, &stat, mask);
	if (error)
		return error;

	return cp_statx(&stat, buffer);
}

/**
 * sys_statx - System call to get enhanced stats
 * @dfd: Base directory to pathwalk from *or* fd to stat.
 * @filename: File to stat or either NULL or "" with AT_EMPTY_PATH
 * @flags: AT_* flags to control pathwalk.
 * @mask: Parts of statx struct actually required.
 * @buffer: Result buffer.
 *
 * Note that fstat() can be emulated by setting dfd to the fd of interest,
 * supplying "" (or preferably NULL) as the filename and setting AT_EMPTY_PATH
 * in the flags.
 */
SYSCALL_DEFINE5(statx,
		int, dfd, const char __user *, filename, unsigned, flags,
		unsigned int, mask,
		struct statx __user *, buffer)
{
	int ret;
	struct filename *name = getname_maybe_null(filename, flags);

	if (!name && dfd >= 0)
		return do_statx_fd(dfd, flags & ~AT_NO_AUTOMOUNT, mask, buffer);

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
	tmp.st_ino = stat->ino;
	if (sizeof(tmp.st_ino) < sizeof(stat->ino) && tmp.st_ino != stat->ino)
		return -EOVERFLOW;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	if (tmp.st_nlink != stat->nlink)
		return -EOVERFLOW;
	SET_UID(tmp.st_uid, from_kuid_munged(current_user_ns(), stat->uid));
	SET_GID(tmp.st_gid, from_kgid_munged(current_user_ns(), stat->gid));
	tmp.st_rdev = new_encode_dev(stat->rdev);
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
#endif

/* Caller is here responsible for sufficient locking (ie. inode->i_lock) */
void __inode_add_bytes(struct inode *inode, loff_t bytes)
{
	inode->i_blocks += bytes >> 9;
	bytes &= 511;
	inode->i_bytes += bytes;
	if (inode->i_bytes >= 512) {
		inode->i_blocks++;
		inode->i_bytes -= 512;
	}
}
EXPORT_SYMBOL(__inode_add_bytes);

void inode_add_bytes(struct inode *inode, loff_t bytes)
{
	spin_lock(&inode->i_lock);
	__inode_add_bytes(inode, bytes);
	spin_unlock(&inode->i_lock);
}

EXPORT_SYMBOL(inode_add_bytes);

void __inode_sub_bytes(struct inode *inode, loff_t bytes)
{
	inode->i_blocks -= bytes >> 9;
	bytes &= 511;
	if (inode->i_bytes < bytes) {
		inode->i_blocks--;
		inode->i_bytes += 512;
	}
	inode->i_bytes -= bytes;
}

EXPORT_SYMBOL(__inode_sub_bytes);

void inode_sub_bytes(struct inode *inode, loff_t bytes)
{
	spin_lock(&inode->i_lock);
	__inode_sub_bytes(inode, bytes);
	spin_unlock(&inode->i_lock);
}

EXPORT_SYMBOL(inode_sub_bytes);

loff_t inode_get_bytes(struct inode *inode)
{
	loff_t ret;

	spin_lock(&inode->i_lock);
	ret = __inode_get_bytes(inode);
	spin_unlock(&inode->i_lock);
	return ret;
}

EXPORT_SYMBOL(inode_get_bytes);

void inode_set_bytes(struct inode *inode, loff_t bytes)
{
	/* Caller is here responsible for sufficient locking
	 * (ie. inode->i_lock) */
	inode->i_blocks = bytes >> 9;
	inode->i_bytes = bytes & 511;
}

EXPORT_SYMBOL(inode_set_bytes);
