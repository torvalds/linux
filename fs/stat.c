/*
 *  linux/fs/stat.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

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

#include <linux/uaccess.h>
#include <asm/unistd.h>

/**
 * generic_fillattr - Fill in the basic attributes from the inode struct
 * @inode: Inode to use as the source
 * @stat: Where to fill in the attributes
 *
 * Fill in the basic attributes in the kstat structure from data that's to be
 * found on the VFS inode structure.  This is the default if no getattr inode
 * operation is supplied.
 */
void generic_fillattr(struct inode *inode, struct kstat *stat)
{
	stat->dev = inode->i_sb->s_dev;
	stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->nlink = inode->i_nlink;
	stat->uid = inode->i_uid;
	stat->gid = inode->i_gid;
	stat->rdev = inode->i_rdev;
	stat->size = i_size_read(inode);
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
	stat->blksize = i_blocksize(inode);
	stat->blocks = inode->i_blocks;

	if (IS_NOATIME(inode))
		stat->result_mask &= ~STATX_ATIME;
	if (IS_AUTOMOUNT(inode))
		stat->attributes |= STATX_ATTR_AUTOMOUNT;
}
EXPORT_SYMBOL(generic_fillattr);

/**
 * vfs_getattr_nosec - getattr without security checks
 * @path: file to get attributes from
 * @stat: structure to return attributes in
 * @request_mask: STATX_xxx flags indicating what the caller wants
 * @query_flags: Query mode (KSTAT_QUERY_FLAGS)
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
	struct inode *inode = d_backing_inode(path->dentry);

	memset(stat, 0, sizeof(*stat));
	stat->result_mask |= STATX_BASIC_STATS;
	request_mask &= STATX_ALL;
	query_flags &= KSTAT_QUERY_FLAGS;
	if (inode->i_op->getattr)
		return inode->i_op->getattr(path, stat, request_mask,
					    query_flags);

	generic_fillattr(inode, stat);
	return 0;
}
EXPORT_SYMBOL(vfs_getattr_nosec);

/*
 * vfs_getattr - Get the enhanced basic attributes of a file
 * @path: The file of interest
 * @stat: Where to return the statistics
 * @request_mask: STATX_xxx flags indicating what the caller wants
 * @query_flags: Query mode (KSTAT_QUERY_FLAGS)
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
 * vfs_statx_fd - Get the enhanced basic attributes by file descriptor
 * @fd: The file descriptor referring to the file of interest
 * @stat: The result structure to fill in.
 * @request_mask: STATX_xxx flags indicating what the caller wants
 * @query_flags: Query mode (KSTAT_QUERY_FLAGS)
 *
 * This function is a wrapper around vfs_getattr().  The main difference is
 * that it uses a file descriptor to determine the file location.
 *
 * 0 will be returned on success, and a -ve error code if unsuccessful.
 */
int vfs_statx_fd(unsigned int fd, struct kstat *stat,
		 u32 request_mask, unsigned int query_flags)
{
	struct fd f;
	int error = -EBADF;

	if (query_flags & ~KSTAT_QUERY_FLAGS)
		return -EINVAL;

	f = fdget_raw(fd);
	if (f.file) {
		error = vfs_getattr(&f.file->f_path, stat,
				    request_mask, query_flags);
		fdput(f);
	}
	return error;
}
EXPORT_SYMBOL(vfs_statx_fd);

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
int vfs_statx(int dfd, const char __user *filename, int flags,
	      struct kstat *stat, u32 request_mask)
{
	struct path path;
	int error = -EINVAL;
	unsigned int lookup_flags = LOOKUP_FOLLOW | LOOKUP_AUTOMOUNT;

	if ((flags & ~(AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT |
		       AT_EMPTY_PATH | KSTAT_QUERY_FLAGS)) != 0)
		return -EINVAL;

	if (flags & AT_SYMLINK_NOFOLLOW)
		lookup_flags &= ~LOOKUP_FOLLOW;
	if (flags & AT_NO_AUTOMOUNT)
		lookup_flags &= ~LOOKUP_AUTOMOUNT;
	if (flags & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;

retry:
	error = user_path_at(dfd, filename, lookup_flags, &path);
	if (error)
		goto out;

	error = vfs_getattr(&path, stat, request_mask, flags);
	path_put(&path);
	if (retry_estale(error, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
out:
	return error;
}
EXPORT_SYMBOL(vfs_statx);


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

#if BITS_PER_LONG == 32
#  define choose_32_64(a,b) a
#else
#  define choose_32_64(a,b) b
#endif

#define valid_dev(x)  choose_32_64(old_valid_dev(x),true)
#define encode_dev(x) choose_32_64(old_encode_dev,new_encode_dev)(x)

#ifndef INIT_STRUCT_STAT_PADDING
#  define INIT_STRUCT_STAT_PADDING(st) memset(&st, 0, sizeof(st))
#endif

static int cp_new_stat(struct kstat *stat, struct stat __user *statbuf)
{
	struct stat tmp;

	if (!valid_dev(stat->dev) || !valid_dev(stat->rdev))
		return -EOVERFLOW;
#if BITS_PER_LONG == 32
	if (stat->size > MAX_NON_LFS)
		return -EOVERFLOW;
#endif

	INIT_STRUCT_STAT_PADDING(tmp);
	tmp.st_dev = encode_dev(stat->dev);
	tmp.st_ino = stat->ino;
	if (sizeof(tmp.st_ino) < sizeof(stat->ino) && tmp.st_ino != stat->ino)
		return -EOVERFLOW;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	if (tmp.st_nlink != stat->nlink)
		return -EOVERFLOW;
	SET_UID(tmp.st_uid, from_kuid_munged(current_user_ns(), stat->uid));
	SET_GID(tmp.st_gid, from_kgid_munged(current_user_ns(), stat->gid));
	tmp.st_rdev = encode_dev(stat->rdev);
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

SYSCALL_DEFINE4(readlinkat, int, dfd, const char __user *, pathname,
		char __user *, buf, int, bufsiz)
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
		struct inode *inode = d_backing_inode(path.dentry);

		error = empty ? -ENOENT : -EINVAL;
		/*
		 * AFS mountpoints allow readlink(2) but are not symlinks
		 */
		if (d_is_symlink(path.dentry) || inode->i_op->readlink) {
			error = security_inode_readlink(path.dentry);
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

SYSCALL_DEFINE3(readlink, const char __user *, path, char __user *, buf,
		int, bufsiz)
{
	return sys_readlinkat(AT_FDCWD, path, buf, bufsiz);
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

	tmp.stx_mask = stat->result_mask;
	tmp.stx_blksize = stat->blksize;
	tmp.stx_attributes = stat->attributes;
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

	return copy_to_user(buffer, &tmp, sizeof(tmp)) ? -EFAULT : 0;
}

/**
 * sys_statx - System call to get enhanced stats
 * @dfd: Base directory to pathwalk from *or* fd to stat.
 * @filename: File to stat or "" with AT_EMPTY_PATH
 * @flags: AT_* flags to control pathwalk.
 * @mask: Parts of statx struct actually required.
 * @buffer: Result buffer.
 *
 * Note that fstat() can be emulated by setting dfd to the fd of interest,
 * supplying "" as the filename and setting AT_EMPTY_PATH in the flags.
 */
SYSCALL_DEFINE5(statx,
		int, dfd, const char __user *, filename, unsigned, flags,
		unsigned int, mask,
		struct statx __user *, buffer)
{
	struct kstat stat;
	int error;

	if (mask & STATX__RESERVED)
		return -EINVAL;
	if ((flags & AT_STATX_SYNC_TYPE) == AT_STATX_SYNC_TYPE)
		return -EINVAL;

	error = vfs_statx(dfd, filename, flags, &stat, mask);
	if (error)
		return error;

	return cp_statx(&stat, buffer);
}

#ifdef CONFIG_COMPAT
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
