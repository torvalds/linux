// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/read_write.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/sched/xacct.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/uio.h>
#include <linux/fsnotify.h>
#include <linux/security.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/pagemap.h>
#include <linux/splice.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "internal.h"

#include <linux/uaccess.h>
#include <asm/unistd.h>

const struct file_operations generic_ro_fops = {
	.llseek		= generic_file_llseek,
	.read_iter	= generic_file_read_iter,
	.mmap		= generic_file_readonly_mmap,
	.splice_read	= filemap_splice_read,
};

EXPORT_SYMBOL(generic_ro_fops);

static inline bool unsigned_offsets(struct file *file)
{
	return file->f_op->fop_flags & FOP_UNSIGNED_OFFSET;
}

/**
 * vfs_setpos_cookie - update the file offset for lseek and reset cookie
 * @file:	file structure in question
 * @offset:	file offset to seek to
 * @maxsize:	maximum file size
 * @cookie:	cookie to reset
 *
 * Update the file offset to the value specified by @offset if the given
 * offset is valid and it is not equal to the current file offset and
 * reset the specified cookie to indicate that a seek happened.
 *
 * Return the specified offset on success and -EINVAL on invalid offset.
 */
static loff_t vfs_setpos_cookie(struct file *file, loff_t offset,
				loff_t maxsize, u64 *cookie)
{
	if (offset < 0 && !unsigned_offsets(file))
		return -EINVAL;
	if (offset > maxsize)
		return -EINVAL;

	if (offset != file->f_pos) {
		file->f_pos = offset;
		if (cookie)
			*cookie = 0;
	}
	return offset;
}

/**
 * vfs_setpos - update the file offset for lseek
 * @file:	file structure in question
 * @offset:	file offset to seek to
 * @maxsize:	maximum file size
 *
 * This is a low-level filesystem helper for updating the file offset to
 * the value specified by @offset if the given offset is valid and it is
 * not equal to the current file offset.
 *
 * Return the specified offset on success and -EINVAL on invalid offset.
 */
loff_t vfs_setpos(struct file *file, loff_t offset, loff_t maxsize)
{
	return vfs_setpos_cookie(file, offset, maxsize, NULL);
}
EXPORT_SYMBOL(vfs_setpos);

/**
 * must_set_pos - check whether f_pos has to be updated
 * @file: file to seek on
 * @offset: offset to use
 * @whence: type of seek operation
 * @eof: end of file
 *
 * Check whether f_pos needs to be updated and update @offset according
 * to @whence.
 *
 * Return: 0 if f_pos doesn't need to be updated, 1 if f_pos has to be
 * updated, and negative error code on failure.
 */
static int must_set_pos(struct file *file, loff_t *offset, int whence, loff_t eof)
{
	switch (whence) {
	case SEEK_END:
		*offset += eof;
		break;
	case SEEK_CUR:
		/*
		 * Here we special-case the lseek(fd, 0, SEEK_CUR)
		 * position-querying operation.  Avoid rewriting the "same"
		 * f_pos value back to the file because a concurrent read(),
		 * write() or lseek() might have altered it
		 */
		if (*offset == 0) {
			*offset = file->f_pos;
			return 0;
		}
		break;
	case SEEK_DATA:
		/*
		 * In the generic case the entire file is data, so as long as
		 * offset isn't at the end of the file then the offset is data.
		 */
		if ((unsigned long long)*offset >= eof)
			return -ENXIO;
		break;
	case SEEK_HOLE:
		/*
		 * There is a virtual hole at the end of the file, so as long as
		 * offset isn't i_size or larger, return i_size.
		 */
		if ((unsigned long long)*offset >= eof)
			return -ENXIO;
		*offset = eof;
		break;
	}

	return 1;
}

/**
 * generic_file_llseek_size - generic llseek implementation for regular files
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 * @maxsize:	max size of this file in file system
 * @eof:	offset used for SEEK_END position
 *
 * This is a variant of generic_file_llseek that allows passing in a custom
 * maximum file size and a custom EOF position, for e.g. hashed directories
 *
 * Synchronization:
 * SEEK_SET and SEEK_END are unsynchronized (but atomic on 64bit platforms)
 * SEEK_CUR is synchronized against other SEEK_CURs, but not read/writes.
 * read/writes behave like SEEK_SET against seeks.
 */
loff_t
generic_file_llseek_size(struct file *file, loff_t offset, int whence,
		loff_t maxsize, loff_t eof)
{
	int ret;

	ret = must_set_pos(file, &offset, whence, eof);
	if (ret < 0)
		return ret;
	if (ret == 0)
		return offset;

	if (whence == SEEK_CUR) {
		/*
		 * f_lock protects against read/modify/write race with
		 * other SEEK_CURs. Note that parallel writes and reads
		 * behave like SEEK_SET.
		 */
		guard(spinlock)(&file->f_lock);
		return vfs_setpos(file, file->f_pos + offset, maxsize);
	}

	return vfs_setpos(file, offset, maxsize);
}
EXPORT_SYMBOL(generic_file_llseek_size);

/**
 * generic_llseek_cookie - versioned llseek implementation
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 * @cookie:	cookie to update
 *
 * See generic_file_llseek for a general description and locking assumptions.
 *
 * In contrast to generic_file_llseek, this function also resets a
 * specified cookie to indicate a seek took place.
 */
loff_t generic_llseek_cookie(struct file *file, loff_t offset, int whence,
			     u64 *cookie)
{
	struct inode *inode = file->f_mapping->host;
	loff_t maxsize = inode->i_sb->s_maxbytes;
	loff_t eof = i_size_read(inode);
	int ret;

	if (WARN_ON_ONCE(!cookie))
		return -EINVAL;

	/*
	 * Require that this is only used for directories that guarantee
	 * synchronization between readdir and seek so that an update to
	 * @cookie is correctly synchronized with concurrent readdir.
	 */
	if (WARN_ON_ONCE(!(file->f_mode & FMODE_ATOMIC_POS)))
		return -EINVAL;

	ret = must_set_pos(file, &offset, whence, eof);
	if (ret < 0)
		return ret;
	if (ret == 0)
		return offset;

	/* No need to hold f_lock because we know that f_pos_lock is held. */
	if (whence == SEEK_CUR)
		return vfs_setpos_cookie(file, file->f_pos + offset, maxsize, cookie);

	return vfs_setpos_cookie(file, offset, maxsize, cookie);
}
EXPORT_SYMBOL(generic_llseek_cookie);

/**
 * generic_file_llseek - generic llseek implementation for regular files
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 *
 * This is a generic implemenation of ->llseek useable for all normal local
 * filesystems.  It just updates the file offset to the value specified by
 * @offset and @whence.
 */
loff_t generic_file_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file->f_mapping->host;

	return generic_file_llseek_size(file, offset, whence,
					inode->i_sb->s_maxbytes,
					i_size_read(inode));
}
EXPORT_SYMBOL(generic_file_llseek);

/**
 * fixed_size_llseek - llseek implementation for fixed-sized devices
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 * @size:	size of the file
 *
 */
loff_t fixed_size_llseek(struct file *file, loff_t offset, int whence, loff_t size)
{
	switch (whence) {
	case SEEK_SET: case SEEK_CUR: case SEEK_END:
		return generic_file_llseek_size(file, offset, whence,
						size, size);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(fixed_size_llseek);

/**
 * no_seek_end_llseek - llseek implementation for fixed-sized devices
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 *
 */
loff_t no_seek_end_llseek(struct file *file, loff_t offset, int whence)
{
	switch (whence) {
	case SEEK_SET: case SEEK_CUR:
		return generic_file_llseek_size(file, offset, whence,
						OFFSET_MAX, 0);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(no_seek_end_llseek);

/**
 * no_seek_end_llseek_size - llseek implementation for fixed-sized devices
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 * @size:	maximal offset allowed
 *
 */
loff_t no_seek_end_llseek_size(struct file *file, loff_t offset, int whence, loff_t size)
{
	switch (whence) {
	case SEEK_SET: case SEEK_CUR:
		return generic_file_llseek_size(file, offset, whence,
						size, 0);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(no_seek_end_llseek_size);

/**
 * noop_llseek - No Operation Performed llseek implementation
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 *
 * This is an implementation of ->llseek useable for the rare special case when
 * userspace expects the seek to succeed but the (device) file is actually not
 * able to perform the seek. In this case you use noop_llseek() instead of
 * falling back to the default implementation of ->llseek.
 */
loff_t noop_llseek(struct file *file, loff_t offset, int whence)
{
	return file->f_pos;
}
EXPORT_SYMBOL(noop_llseek);

loff_t default_llseek(struct file *file, loff_t offset, int whence)
{
	struct inode *inode = file_inode(file);
	loff_t retval;

	inode_lock(inode);
	switch (whence) {
		case SEEK_END:
			offset += i_size_read(inode);
			break;
		case SEEK_CUR:
			if (offset == 0) {
				retval = file->f_pos;
				goto out;
			}
			offset += file->f_pos;
			break;
		case SEEK_DATA:
			/*
			 * In the generic case the entire file is data, so as
			 * long as offset isn't at the end of the file then the
			 * offset is data.
			 */
			if (offset >= inode->i_size) {
				retval = -ENXIO;
				goto out;
			}
			break;
		case SEEK_HOLE:
			/*
			 * There is a virtual hole at the end of the file, so
			 * as long as offset isn't i_size or larger, return
			 * i_size.
			 */
			if (offset >= inode->i_size) {
				retval = -ENXIO;
				goto out;
			}
			offset = inode->i_size;
			break;
	}
	retval = -EINVAL;
	if (offset >= 0 || unsigned_offsets(file)) {
		if (offset != file->f_pos)
			file->f_pos = offset;
		retval = offset;
	}
out:
	inode_unlock(inode);
	return retval;
}
EXPORT_SYMBOL(default_llseek);

loff_t vfs_llseek(struct file *file, loff_t offset, int whence)
{
	if (!(file->f_mode & FMODE_LSEEK))
		return -ESPIPE;
	return file->f_op->llseek(file, offset, whence);
}
EXPORT_SYMBOL(vfs_llseek);

static off_t ksys_lseek(unsigned int fd, off_t offset, unsigned int whence)
{
	off_t retval;
	CLASS(fd_pos, f)(fd);
	if (fd_empty(f))
		return -EBADF;

	retval = -EINVAL;
	if (whence <= SEEK_MAX) {
		loff_t res = vfs_llseek(fd_file(f), offset, whence);
		retval = res;
		if (res != (loff_t)retval)
			retval = -EOVERFLOW;	/* LFS: should only happen on 32 bit platforms */
	}
	return retval;
}

SYSCALL_DEFINE3(lseek, unsigned int, fd, off_t, offset, unsigned int, whence)
{
	return ksys_lseek(fd, offset, whence);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE3(lseek, unsigned int, fd, compat_off_t, offset, unsigned int, whence)
{
	return ksys_lseek(fd, offset, whence);
}
#endif

#if !defined(CONFIG_64BIT) || defined(CONFIG_COMPAT) || \
	defined(__ARCH_WANT_SYS_LLSEEK)
SYSCALL_DEFINE5(llseek, unsigned int, fd, unsigned long, offset_high,
		unsigned long, offset_low, loff_t __user *, result,
		unsigned int, whence)
{
	int retval;
	CLASS(fd_pos, f)(fd);
	loff_t offset;

	if (fd_empty(f))
		return -EBADF;

	if (whence > SEEK_MAX)
		return -EINVAL;

	offset = vfs_llseek(fd_file(f), ((loff_t) offset_high << 32) | offset_low,
			whence);

	retval = (int)offset;
	if (offset >= 0) {
		retval = -EFAULT;
		if (!copy_to_user(result, &offset, sizeof(offset)))
			retval = 0;
	}
	return retval;
}
#endif

int rw_verify_area(int read_write, struct file *file, const loff_t *ppos, size_t count)
{
	int mask = read_write == READ ? MAY_READ : MAY_WRITE;
	int ret;

	if (unlikely((ssize_t) count < 0))
		return -EINVAL;

	if (ppos) {
		loff_t pos = *ppos;

		if (unlikely(pos < 0)) {
			if (!unsigned_offsets(file))
				return -EINVAL;
			if (count >= -pos) /* both values are in 0..LLONG_MAX */
				return -EOVERFLOW;
		} else if (unlikely((loff_t) (pos + count) < 0)) {
			if (!unsigned_offsets(file))
				return -EINVAL;
		}
	}

	ret = security_file_permission(file, mask);
	if (ret)
		return ret;

	return fsnotify_file_area_perm(file, mask, ppos, count);
}
EXPORT_SYMBOL(rw_verify_area);

static ssize_t new_sync_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = (ppos ? *ppos : 0);
	iov_iter_ubuf(&iter, ITER_DEST, buf, len);

	ret = filp->f_op->read_iter(&kiocb, &iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ppos)
		*ppos = kiocb.ki_pos;
	return ret;
}

static int warn_unsupported(struct file *file, const char *op)
{
	pr_warn_ratelimited(
		"kernel %s not supported for file %pD4 (pid: %d comm: %.20s)\n",
		op, file, current->pid, current->comm);
	return -EINVAL;
}

ssize_t __kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
	struct kvec iov = {
		.iov_base	= buf,
		.iov_len	= min_t(size_t, count, MAX_RW_COUNT),
	};
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	if (WARN_ON_ONCE(!(file->f_mode & FMODE_READ)))
		return -EINVAL;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;
	/*
	 * Also fail if ->read_iter and ->read are both wired up as that
	 * implies very convoluted semantics.
	 */
	if (unlikely(!file->f_op->read_iter || file->f_op->read))
		return warn_unsupported(file, "read");

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos ? *pos : 0;
	iov_iter_kvec(&iter, ITER_DEST, &iov, 1, iov.iov_len);
	ret = file->f_op->read_iter(&kiocb, &iter);
	if (ret > 0) {
		if (pos)
			*pos = kiocb.ki_pos;
		fsnotify_access(file);
		add_rchar(current, ret);
	}
	inc_syscr(current);
	return ret;
}

ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	ret = rw_verify_area(READ, file, pos, count);
	if (ret)
		return ret;
	return __kernel_read(file, buf, count, pos);
}
EXPORT_SYMBOL(kernel_read);

ssize_t vfs_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;
	if (unlikely(!access_ok(buf, count)))
		return -EFAULT;

	ret = rw_verify_area(READ, file, pos, count);
	if (ret)
		return ret;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;

	if (file->f_op->read)
		ret = file->f_op->read(file, buf, count, pos);
	else if (file->f_op->read_iter)
		ret = new_sync_read(file, buf, count, pos);
	else
		ret = -EINVAL;
	if (ret > 0) {
		fsnotify_access(file);
		add_rchar(current, ret);
	}
	inc_syscr(current);
	return ret;
}

static ssize_t new_sync_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = (ppos ? *ppos : 0);
	iov_iter_ubuf(&iter, ITER_SOURCE, (void __user *)buf, len);

	ret = filp->f_op->write_iter(&kiocb, &iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ret > 0 && ppos)
		*ppos = kiocb.ki_pos;
	return ret;
}

/* caller is responsible for file_start_write/file_end_write */
ssize_t __kernel_write_iter(struct file *file, struct iov_iter *from, loff_t *pos)
{
	struct kiocb kiocb;
	ssize_t ret;

	if (WARN_ON_ONCE(!(file->f_mode & FMODE_WRITE)))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;
	/*
	 * Also fail if ->write_iter and ->write are both wired up as that
	 * implies very convoluted semantics.
	 */
	if (unlikely(!file->f_op->write_iter || file->f_op->write))
		return warn_unsupported(file, "write");

	init_sync_kiocb(&kiocb, file);
	kiocb.ki_pos = pos ? *pos : 0;
	ret = file->f_op->write_iter(&kiocb, from);
	if (ret > 0) {
		if (pos)
			*pos = kiocb.ki_pos;
		fsnotify_modify(file);
		add_wchar(current, ret);
	}
	inc_syscw(current);
	return ret;
}

/* caller is responsible for file_start_write/file_end_write */
ssize_t __kernel_write(struct file *file, const void *buf, size_t count, loff_t *pos)
{
	struct kvec iov = {
		.iov_base	= (void *)buf,
		.iov_len	= min_t(size_t, count, MAX_RW_COUNT),
	};
	struct iov_iter iter;
	iov_iter_kvec(&iter, ITER_SOURCE, &iov, 1, iov.iov_len);
	return __kernel_write_iter(file, &iter, pos);
}
/*
 * This "EXPORT_SYMBOL_GPL()" is more of a "EXPORT_SYMBOL_DONTUSE()",
 * but autofs is one of the few internal kernel users that actually
 * wants this _and_ can be built as a module. So we need to export
 * this symbol for autofs, even though it really isn't appropriate
 * for any other kernel modules.
 */
EXPORT_SYMBOL_GPL(__kernel_write);

ssize_t kernel_write(struct file *file, const void *buf, size_t count,
			    loff_t *pos)
{
	ssize_t ret;

	ret = rw_verify_area(WRITE, file, pos, count);
	if (ret)
		return ret;

	file_start_write(file);
	ret =  __kernel_write(file, buf, count, pos);
	file_end_write(file);
	return ret;
}
EXPORT_SYMBOL(kernel_write);

ssize_t vfs_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	ssize_t ret;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;
	if (unlikely(!access_ok(buf, count)))
		return -EFAULT;

	ret = rw_verify_area(WRITE, file, pos, count);
	if (ret)
		return ret;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;
	file_start_write(file);
	if (file->f_op->write)
		ret = file->f_op->write(file, buf, count, pos);
	else if (file->f_op->write_iter)
		ret = new_sync_write(file, buf, count, pos);
	else
		ret = -EINVAL;
	if (ret > 0) {
		fsnotify_modify(file);
		add_wchar(current, ret);
	}
	inc_syscw(current);
	file_end_write(file);
	return ret;
}

/* file_ppos returns &file->f_pos or NULL if file is stream */
static inline loff_t *file_ppos(struct file *file)
{
	return file->f_mode & FMODE_STREAM ? NULL : &file->f_pos;
}

ssize_t ksys_read(unsigned int fd, char __user *buf, size_t count)
{
	CLASS(fd_pos, f)(fd);
	ssize_t ret = -EBADF;

	if (!fd_empty(f)) {
		loff_t pos, *ppos = file_ppos(fd_file(f));
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_read(fd_file(f), buf, count, ppos);
		if (ret >= 0 && ppos)
			fd_file(f)->f_pos = pos;
	}
	return ret;
}

SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
{
	return ksys_read(fd, buf, count);
}

ssize_t ksys_write(unsigned int fd, const char __user *buf, size_t count)
{
	CLASS(fd_pos, f)(fd);
	ssize_t ret = -EBADF;

	if (!fd_empty(f)) {
		loff_t pos, *ppos = file_ppos(fd_file(f));
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_write(fd_file(f), buf, count, ppos);
		if (ret >= 0 && ppos)
			fd_file(f)->f_pos = pos;
	}

	return ret;
}

SYSCALL_DEFINE3(write, unsigned int, fd, const char __user *, buf,
		size_t, count)
{
	return ksys_write(fd, buf, count);
}

ssize_t ksys_pread64(unsigned int fd, char __user *buf, size_t count,
		     loff_t pos)
{
	if (pos < 0)
		return -EINVAL;

	CLASS(fd, f)(fd);
	if (fd_empty(f))
		return -EBADF;

	if (fd_file(f)->f_mode & FMODE_PREAD)
		return vfs_read(fd_file(f), buf, count, &pos);

	return -ESPIPE;
}

SYSCALL_DEFINE4(pread64, unsigned int, fd, char __user *, buf,
			size_t, count, loff_t, pos)
{
	return ksys_pread64(fd, buf, count, pos);
}

#if defined(CONFIG_COMPAT) && defined(__ARCH_WANT_COMPAT_PREAD64)
COMPAT_SYSCALL_DEFINE5(pread64, unsigned int, fd, char __user *, buf,
		       size_t, count, compat_arg_u64_dual(pos))
{
	return ksys_pread64(fd, buf, count, compat_arg_u64_glue(pos));
}
#endif

ssize_t ksys_pwrite64(unsigned int fd, const char __user *buf,
		      size_t count, loff_t pos)
{
	if (pos < 0)
		return -EINVAL;

	CLASS(fd, f)(fd);
	if (fd_empty(f))
		return -EBADF;

	if (fd_file(f)->f_mode & FMODE_PWRITE)
		return vfs_write(fd_file(f), buf, count, &pos);

	return -ESPIPE;
}

SYSCALL_DEFINE4(pwrite64, unsigned int, fd, const char __user *, buf,
			 size_t, count, loff_t, pos)
{
	return ksys_pwrite64(fd, buf, count, pos);
}

#if defined(CONFIG_COMPAT) && defined(__ARCH_WANT_COMPAT_PWRITE64)
COMPAT_SYSCALL_DEFINE5(pwrite64, unsigned int, fd, const char __user *, buf,
		       size_t, count, compat_arg_u64_dual(pos))
{
	return ksys_pwrite64(fd, buf, count, compat_arg_u64_glue(pos));
}
#endif

static ssize_t do_iter_readv_writev(struct file *filp, struct iov_iter *iter,
		loff_t *ppos, int type, rwf_t flags)
{
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	ret = kiocb_set_rw_flags(&kiocb, flags, type);
	if (ret)
		return ret;
	kiocb.ki_pos = (ppos ? *ppos : 0);

	if (type == READ)
		ret = filp->f_op->read_iter(&kiocb, iter);
	else
		ret = filp->f_op->write_iter(&kiocb, iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ppos)
		*ppos = kiocb.ki_pos;
	return ret;
}

/* Do it by hand, with file-ops */
static ssize_t do_loop_readv_writev(struct file *filp, struct iov_iter *iter,
		loff_t *ppos, int type, rwf_t flags)
{
	ssize_t ret = 0;

	if (flags & ~RWF_HIPRI)
		return -EOPNOTSUPP;

	while (iov_iter_count(iter)) {
		ssize_t nr;

		if (type == READ) {
			nr = filp->f_op->read(filp, iter_iov_addr(iter),
						iter_iov_len(iter), ppos);
		} else {
			nr = filp->f_op->write(filp, iter_iov_addr(iter),
						iter_iov_len(iter), ppos);
		}

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (nr != iter_iov_len(iter))
			break;
		iov_iter_advance(iter, nr);
	}

	return ret;
}

ssize_t vfs_iocb_iter_read(struct file *file, struct kiocb *iocb,
			   struct iov_iter *iter)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!file->f_op->read_iter)
		return -EINVAL;
	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		goto out;
	ret = rw_verify_area(READ, file, &iocb->ki_pos, tot_len);
	if (ret < 0)
		return ret;

	ret = file->f_op->read_iter(iocb, iter);
out:
	if (ret >= 0)
		fsnotify_access(file);
	return ret;
}
EXPORT_SYMBOL(vfs_iocb_iter_read);

ssize_t vfs_iter_read(struct file *file, struct iov_iter *iter, loff_t *ppos,
		      rwf_t flags)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!file->f_op->read_iter)
		return -EINVAL;
	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		goto out;
	ret = rw_verify_area(READ, file, ppos, tot_len);
	if (ret < 0)
		return ret;

	ret = do_iter_readv_writev(file, iter, ppos, READ, flags);
out:
	if (ret >= 0)
		fsnotify_access(file);
	return ret;
}
EXPORT_SYMBOL(vfs_iter_read);

/*
 * Caller is responsible for calling kiocb_end_write() on completion
 * if async iocb was queued.
 */
ssize_t vfs_iocb_iter_write(struct file *file, struct kiocb *iocb,
			    struct iov_iter *iter)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!file->f_op->write_iter)
		return -EINVAL;
	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		return 0;
	ret = rw_verify_area(WRITE, file, &iocb->ki_pos, tot_len);
	if (ret < 0)
		return ret;

	kiocb_start_write(iocb);
	ret = file->f_op->write_iter(iocb, iter);
	if (ret != -EIOCBQUEUED)
		kiocb_end_write(iocb);
	if (ret > 0)
		fsnotify_modify(file);

	return ret;
}
EXPORT_SYMBOL(vfs_iocb_iter_write);

ssize_t vfs_iter_write(struct file *file, struct iov_iter *iter, loff_t *ppos,
		       rwf_t flags)
{
	size_t tot_len;
	ssize_t ret;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;
	if (!file->f_op->write_iter)
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		return 0;

	ret = rw_verify_area(WRITE, file, ppos, tot_len);
	if (ret < 0)
		return ret;

	file_start_write(file);
	ret = do_iter_readv_writev(file, iter, ppos, WRITE, flags);
	if (ret > 0)
		fsnotify_modify(file);
	file_end_write(file);

	return ret;
}
EXPORT_SYMBOL(vfs_iter_write);

static ssize_t vfs_readv(struct file *file, const struct iovec __user *vec,
			 unsigned long vlen, loff_t *pos, rwf_t flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	size_t tot_len;
	ssize_t ret = 0;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;

	ret = import_iovec(ITER_DEST, vec, vlen, ARRAY_SIZE(iovstack), &iov,
			   &iter);
	if (ret < 0)
		return ret;

	tot_len = iov_iter_count(&iter);
	if (!tot_len)
		goto out;

	ret = rw_verify_area(READ, file, pos, tot_len);
	if (ret < 0)
		goto out;

	if (file->f_op->read_iter)
		ret = do_iter_readv_writev(file, &iter, pos, READ, flags);
	else
		ret = do_loop_readv_writev(file, &iter, pos, READ, flags);
out:
	if (ret >= 0)
		fsnotify_access(file);
	kfree(iov);
	return ret;
}

static ssize_t vfs_writev(struct file *file, const struct iovec __user *vec,
			  unsigned long vlen, loff_t *pos, rwf_t flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	size_t tot_len;
	ssize_t ret = 0;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;

	ret = import_iovec(ITER_SOURCE, vec, vlen, ARRAY_SIZE(iovstack), &iov,
			   &iter);
	if (ret < 0)
		return ret;

	tot_len = iov_iter_count(&iter);
	if (!tot_len)
		goto out;

	ret = rw_verify_area(WRITE, file, pos, tot_len);
	if (ret < 0)
		goto out;

	file_start_write(file);
	if (file->f_op->write_iter)
		ret = do_iter_readv_writev(file, &iter, pos, WRITE, flags);
	else
		ret = do_loop_readv_writev(file, &iter, pos, WRITE, flags);
	if (ret > 0)
		fsnotify_modify(file);
	file_end_write(file);
out:
	kfree(iov);
	return ret;
}

static ssize_t do_readv(unsigned long fd, const struct iovec __user *vec,
			unsigned long vlen, rwf_t flags)
{
	CLASS(fd_pos, f)(fd);
	ssize_t ret = -EBADF;

	if (!fd_empty(f)) {
		loff_t pos, *ppos = file_ppos(fd_file(f));
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_readv(fd_file(f), vec, vlen, ppos, flags);
		if (ret >= 0 && ppos)
			fd_file(f)->f_pos = pos;
	}

	if (ret > 0)
		add_rchar(current, ret);
	inc_syscr(current);
	return ret;
}

static ssize_t do_writev(unsigned long fd, const struct iovec __user *vec,
			 unsigned long vlen, rwf_t flags)
{
	CLASS(fd_pos, f)(fd);
	ssize_t ret = -EBADF;

	if (!fd_empty(f)) {
		loff_t pos, *ppos = file_ppos(fd_file(f));
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_writev(fd_file(f), vec, vlen, ppos, flags);
		if (ret >= 0 && ppos)
			fd_file(f)->f_pos = pos;
	}

	if (ret > 0)
		add_wchar(current, ret);
	inc_syscw(current);
	return ret;
}

static inline loff_t pos_from_hilo(unsigned long high, unsigned long low)
{
#define HALF_LONG_BITS (BITS_PER_LONG / 2)
	return (((loff_t)high << HALF_LONG_BITS) << HALF_LONG_BITS) | low;
}

static ssize_t do_preadv(unsigned long fd, const struct iovec __user *vec,
			 unsigned long vlen, loff_t pos, rwf_t flags)
{
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	CLASS(fd, f)(fd);
	if (!fd_empty(f)) {
		ret = -ESPIPE;
		if (fd_file(f)->f_mode & FMODE_PREAD)
			ret = vfs_readv(fd_file(f), vec, vlen, &pos, flags);
	}

	if (ret > 0)
		add_rchar(current, ret);
	inc_syscr(current);
	return ret;
}

static ssize_t do_pwritev(unsigned long fd, const struct iovec __user *vec,
			  unsigned long vlen, loff_t pos, rwf_t flags)
{
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	CLASS(fd, f)(fd);
	if (!fd_empty(f)) {
		ret = -ESPIPE;
		if (fd_file(f)->f_mode & FMODE_PWRITE)
			ret = vfs_writev(fd_file(f), vec, vlen, &pos, flags);
	}

	if (ret > 0)
		add_wchar(current, ret);
	inc_syscw(current);
	return ret;
}

SYSCALL_DEFINE3(readv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen)
{
	return do_readv(fd, vec, vlen, 0);
}

SYSCALL_DEFINE3(writev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen)
{
	return do_writev(fd, vec, vlen, 0);
}

SYSCALL_DEFINE5(preadv, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	return do_preadv(fd, vec, vlen, pos, 0);
}

SYSCALL_DEFINE6(preadv2, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h,
		rwf_t, flags)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	if (pos == -1)
		return do_readv(fd, vec, vlen, flags);

	return do_preadv(fd, vec, vlen, pos, flags);
}

SYSCALL_DEFINE5(pwritev, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	return do_pwritev(fd, vec, vlen, pos, 0);
}

SYSCALL_DEFINE6(pwritev2, unsigned long, fd, const struct iovec __user *, vec,
		unsigned long, vlen, unsigned long, pos_l, unsigned long, pos_h,
		rwf_t, flags)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	if (pos == -1)
		return do_writev(fd, vec, vlen, flags);

	return do_pwritev(fd, vec, vlen, pos, flags);
}

/*
 * Various compat syscalls.  Note that they all pretend to take a native
 * iovec - import_iovec will properly treat those as compat_iovecs based on
 * in_compat_syscall().
 */
#ifdef CONFIG_COMPAT
#ifdef __ARCH_WANT_COMPAT_SYS_PREADV64
COMPAT_SYSCALL_DEFINE4(preadv64, unsigned long, fd,
		const struct iovec __user *, vec,
		unsigned long, vlen, loff_t, pos)
{
	return do_preadv(fd, vec, vlen, pos, 0);
}
#endif

COMPAT_SYSCALL_DEFINE5(preadv, compat_ulong_t, fd,
		const struct iovec __user *, vec,
		compat_ulong_t, vlen, u32, pos_low, u32, pos_high)
{
	loff_t pos = ((loff_t)pos_high << 32) | pos_low;

	return do_preadv(fd, vec, vlen, pos, 0);
}

#ifdef __ARCH_WANT_COMPAT_SYS_PREADV64V2
COMPAT_SYSCALL_DEFINE5(preadv64v2, unsigned long, fd,
		const struct iovec __user *, vec,
		unsigned long, vlen, loff_t, pos, rwf_t, flags)
{
	if (pos == -1)
		return do_readv(fd, vec, vlen, flags);
	return do_preadv(fd, vec, vlen, pos, flags);
}
#endif

COMPAT_SYSCALL_DEFINE6(preadv2, compat_ulong_t, fd,
		const struct iovec __user *, vec,
		compat_ulong_t, vlen, u32, pos_low, u32, pos_high,
		rwf_t, flags)
{
	loff_t pos = ((loff_t)pos_high << 32) | pos_low;

	if (pos == -1)
		return do_readv(fd, vec, vlen, flags);
	return do_preadv(fd, vec, vlen, pos, flags);
}

#ifdef __ARCH_WANT_COMPAT_SYS_PWRITEV64
COMPAT_SYSCALL_DEFINE4(pwritev64, unsigned long, fd,
		const struct iovec __user *, vec,
		unsigned long, vlen, loff_t, pos)
{
	return do_pwritev(fd, vec, vlen, pos, 0);
}
#endif

COMPAT_SYSCALL_DEFINE5(pwritev, compat_ulong_t, fd,
		const struct iovec __user *,vec,
		compat_ulong_t, vlen, u32, pos_low, u32, pos_high)
{
	loff_t pos = ((loff_t)pos_high << 32) | pos_low;

	return do_pwritev(fd, vec, vlen, pos, 0);
}

#ifdef __ARCH_WANT_COMPAT_SYS_PWRITEV64V2
COMPAT_SYSCALL_DEFINE5(pwritev64v2, unsigned long, fd,
		const struct iovec __user *, vec,
		unsigned long, vlen, loff_t, pos, rwf_t, flags)
{
	if (pos == -1)
		return do_writev(fd, vec, vlen, flags);
	return do_pwritev(fd, vec, vlen, pos, flags);
}
#endif

COMPAT_SYSCALL_DEFINE6(pwritev2, compat_ulong_t, fd,
		const struct iovec __user *,vec,
		compat_ulong_t, vlen, u32, pos_low, u32, pos_high, rwf_t, flags)
{
	loff_t pos = ((loff_t)pos_high << 32) | pos_low;

	if (pos == -1)
		return do_writev(fd, vec, vlen, flags);
	return do_pwritev(fd, vec, vlen, pos, flags);
}
#endif /* CONFIG_COMPAT */

static ssize_t do_sendfile(int out_fd, int in_fd, loff_t *ppos,
			   size_t count, loff_t max)
{
	struct inode *in_inode, *out_inode;
	struct pipe_inode_info *opipe;
	loff_t pos;
	loff_t out_pos;
	ssize_t retval;
	int fl;

	/*
	 * Get input file, and verify that it is ok..
	 */
	CLASS(fd, in)(in_fd);
	if (fd_empty(in))
		return -EBADF;
	if (!(fd_file(in)->f_mode & FMODE_READ))
		return -EBADF;
	if (!ppos) {
		pos = fd_file(in)->f_pos;
	} else {
		pos = *ppos;
		if (!(fd_file(in)->f_mode & FMODE_PREAD))
			return -ESPIPE;
	}
	retval = rw_verify_area(READ, fd_file(in), &pos, count);
	if (retval < 0)
		return retval;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;

	/*
	 * Get output file, and verify that it is ok..
	 */
	CLASS(fd, out)(out_fd);
	if (fd_empty(out))
		return -EBADF;
	if (!(fd_file(out)->f_mode & FMODE_WRITE))
		return -EBADF;
	in_inode = file_inode(fd_file(in));
	out_inode = file_inode(fd_file(out));
	out_pos = fd_file(out)->f_pos;

	if (!max)
		max = min(in_inode->i_sb->s_maxbytes, out_inode->i_sb->s_maxbytes);

	if (unlikely(pos + count > max)) {
		if (pos >= max)
			return -EOVERFLOW;
		count = max - pos;
	}

	fl = 0;
#if 0
	/*
	 * We need to debate whether we can enable this or not. The
	 * man page documents EAGAIN return for the output at least,
	 * and the application is arguably buggy if it doesn't expect
	 * EAGAIN on a non-blocking file descriptor.
	 */
	if (fd_file(in)->f_flags & O_NONBLOCK)
		fl = SPLICE_F_NONBLOCK;
#endif
	opipe = get_pipe_info(fd_file(out), true);
	if (!opipe) {
		retval = rw_verify_area(WRITE, fd_file(out), &out_pos, count);
		if (retval < 0)
			return retval;
		retval = do_splice_direct(fd_file(in), &pos, fd_file(out), &out_pos,
					  count, fl);
	} else {
		if (fd_file(out)->f_flags & O_NONBLOCK)
			fl |= SPLICE_F_NONBLOCK;

		retval = splice_file_to_pipe(fd_file(in), opipe, &pos, count, fl);
	}

	if (retval > 0) {
		add_rchar(current, retval);
		add_wchar(current, retval);
		fsnotify_access(fd_file(in));
		fsnotify_modify(fd_file(out));
		fd_file(out)->f_pos = out_pos;
		if (ppos)
			*ppos = pos;
		else
			fd_file(in)->f_pos = pos;
	}

	inc_syscr(current);
	inc_syscw(current);
	if (pos > max)
		retval = -EOVERFLOW;
	return retval;
}

SYSCALL_DEFINE4(sendfile, int, out_fd, int, in_fd, off_t __user *, offset, size_t, count)
{
	loff_t pos;
	off_t off;
	ssize_t ret;

	if (offset) {
		if (unlikely(get_user(off, offset)))
			return -EFAULT;
		pos = off;
		ret = do_sendfile(out_fd, in_fd, &pos, count, MAX_NON_LFS);
		if (unlikely(put_user(pos, offset)))
			return -EFAULT;
		return ret;
	}

	return do_sendfile(out_fd, in_fd, NULL, count, 0);
}

SYSCALL_DEFINE4(sendfile64, int, out_fd, int, in_fd, loff_t __user *, offset, size_t, count)
{
	loff_t pos;
	ssize_t ret;

	if (offset) {
		if (unlikely(copy_from_user(&pos, offset, sizeof(loff_t))))
			return -EFAULT;
		ret = do_sendfile(out_fd, in_fd, &pos, count, 0);
		if (unlikely(put_user(pos, offset)))
			return -EFAULT;
		return ret;
	}

	return do_sendfile(out_fd, in_fd, NULL, count, 0);
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE4(sendfile, int, out_fd, int, in_fd,
		compat_off_t __user *, offset, compat_size_t, count)
{
	loff_t pos;
	off_t off;
	ssize_t ret;

	if (offset) {
		if (unlikely(get_user(off, offset)))
			return -EFAULT;
		pos = off;
		ret = do_sendfile(out_fd, in_fd, &pos, count, MAX_NON_LFS);
		if (unlikely(put_user(pos, offset)))
			return -EFAULT;
		return ret;
	}

	return do_sendfile(out_fd, in_fd, NULL, count, 0);
}

COMPAT_SYSCALL_DEFINE4(sendfile64, int, out_fd, int, in_fd,
		compat_loff_t __user *, offset, compat_size_t, count)
{
	loff_t pos;
	ssize_t ret;

	if (offset) {
		if (unlikely(copy_from_user(&pos, offset, sizeof(loff_t))))
			return -EFAULT;
		ret = do_sendfile(out_fd, in_fd, &pos, count, 0);
		if (unlikely(put_user(pos, offset)))
			return -EFAULT;
		return ret;
	}

	return do_sendfile(out_fd, in_fd, NULL, count, 0);
}
#endif

/*
 * Performs necessary checks before doing a file copy
 *
 * Can adjust amount of bytes to copy via @req_count argument.
 * Returns appropriate error code that caller should return or
 * zero in case the copy should be allowed.
 */
static int generic_copy_file_checks(struct file *file_in, loff_t pos_in,
				    struct file *file_out, loff_t pos_out,
				    size_t *req_count, unsigned int flags)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);
	uint64_t count = *req_count;
	loff_t size_in;
	int ret;

	ret = generic_file_rw_checks(file_in, file_out);
	if (ret)
		return ret;

	/*
	 * We allow some filesystems to handle cross sb copy, but passing
	 * a file of the wrong filesystem type to filesystem driver can result
	 * in an attempt to dereference the wrong type of ->private_data, so
	 * avoid doing that until we really have a good reason.
	 *
	 * nfs and cifs define several different file_system_type structures
	 * and several different sets of file_operations, but they all end up
	 * using the same ->copy_file_range() function pointer.
	 */
	if (flags & COPY_FILE_SPLICE) {
		/* cross sb splice is allowed */
	} else if (file_out->f_op->copy_file_range) {
		if (file_in->f_op->copy_file_range !=
		    file_out->f_op->copy_file_range)
			return -EXDEV;
	} else if (file_inode(file_in)->i_sb != file_inode(file_out)->i_sb) {
		return -EXDEV;
	}

	/* Don't touch certain kinds of inodes */
	if (IS_IMMUTABLE(inode_out))
		return -EPERM;

	if (IS_SWAPFILE(inode_in) || IS_SWAPFILE(inode_out))
		return -ETXTBSY;

	/* Ensure offsets don't wrap. */
	if (pos_in + count < pos_in || pos_out + count < pos_out)
		return -EOVERFLOW;

	/* Shorten the copy to EOF */
	size_in = i_size_read(inode_in);
	if (pos_in >= size_in)
		count = 0;
	else
		count = min(count, size_in - (uint64_t)pos_in);

	ret = generic_write_check_limits(file_out, pos_out, &count);
	if (ret)
		return ret;

	/* Don't allow overlapped copying within the same file. */
	if (inode_in == inode_out &&
	    pos_out + count > pos_in &&
	    pos_out < pos_in + count)
		return -EINVAL;

	*req_count = count;
	return 0;
}

/*
 * copy_file_range() differs from regular file read and write in that it
 * specifically allows return partial success.  When it does so is up to
 * the copy_file_range method.
 */
ssize_t vfs_copy_file_range(struct file *file_in, loff_t pos_in,
			    struct file *file_out, loff_t pos_out,
			    size_t len, unsigned int flags)
{
	ssize_t ret;
	bool splice = flags & COPY_FILE_SPLICE;
	bool samesb = file_inode(file_in)->i_sb == file_inode(file_out)->i_sb;

	if (flags & ~COPY_FILE_SPLICE)
		return -EINVAL;

	ret = generic_copy_file_checks(file_in, pos_in, file_out, pos_out, &len,
				       flags);
	if (unlikely(ret))
		return ret;

	ret = rw_verify_area(READ, file_in, &pos_in, len);
	if (unlikely(ret))
		return ret;

	ret = rw_verify_area(WRITE, file_out, &pos_out, len);
	if (unlikely(ret))
		return ret;

	if (len == 0)
		return 0;

	file_start_write(file_out);

	/*
	 * Cloning is supported by more file systems, so we implement copy on
	 * same sb using clone, but for filesystems where both clone and copy
	 * are supported (e.g. nfs,cifs), we only call the copy method.
	 */
	if (!splice && file_out->f_op->copy_file_range) {
		ret = file_out->f_op->copy_file_range(file_in, pos_in,
						      file_out, pos_out,
						      len, flags);
	} else if (!splice && file_in->f_op->remap_file_range && samesb) {
		ret = file_in->f_op->remap_file_range(file_in, pos_in,
				file_out, pos_out,
				min_t(loff_t, MAX_RW_COUNT, len),
				REMAP_FILE_CAN_SHORTEN);
		/* fallback to splice */
		if (ret <= 0)
			splice = true;
	} else if (samesb) {
		/* Fallback to splice for same sb copy for backward compat */
		splice = true;
	}

	file_end_write(file_out);

	if (!splice)
		goto done;

	/*
	 * We can get here for same sb copy of filesystems that do not implement
	 * ->copy_file_range() in case filesystem does not support clone or in
	 * case filesystem supports clone but rejected the clone request (e.g.
	 * because it was not block aligned).
	 *
	 * In both cases, fall back to kernel copy so we are able to maintain a
	 * consistent story about which filesystems support copy_file_range()
	 * and which filesystems do not, that will allow userspace tools to
	 * make consistent desicions w.r.t using copy_file_range().
	 *
	 * We also get here if caller (e.g. nfsd) requested COPY_FILE_SPLICE
	 * for server-side-copy between any two sb.
	 *
	 * In any case, we call do_splice_direct() and not splice_file_range(),
	 * without file_start_write() held, to avoid possible deadlocks related
	 * to splicing from input file, while file_start_write() is held on
	 * the output file on a different sb.
	 */
	ret = do_splice_direct(file_in, &pos_in, file_out, &pos_out,
			       min_t(size_t, len, MAX_RW_COUNT), 0);
done:
	if (ret > 0) {
		fsnotify_access(file_in);
		add_rchar(current, ret);
		fsnotify_modify(file_out);
		add_wchar(current, ret);
	}

	inc_syscr(current);
	inc_syscw(current);

	return ret;
}
EXPORT_SYMBOL(vfs_copy_file_range);

SYSCALL_DEFINE6(copy_file_range, int, fd_in, loff_t __user *, off_in,
		int, fd_out, loff_t __user *, off_out,
		size_t, len, unsigned int, flags)
{
	loff_t pos_in;
	loff_t pos_out;
	ssize_t ret = -EBADF;

	CLASS(fd, f_in)(fd_in);
	if (fd_empty(f_in))
		return -EBADF;

	CLASS(fd, f_out)(fd_out);
	if (fd_empty(f_out))
		return -EBADF;

	if (off_in) {
		if (copy_from_user(&pos_in, off_in, sizeof(loff_t)))
			return -EFAULT;
	} else {
		pos_in = fd_file(f_in)->f_pos;
	}

	if (off_out) {
		if (copy_from_user(&pos_out, off_out, sizeof(loff_t)))
			return -EFAULT;
	} else {
		pos_out = fd_file(f_out)->f_pos;
	}

	if (flags != 0)
		return -EINVAL;

	ret = vfs_copy_file_range(fd_file(f_in), pos_in, fd_file(f_out), pos_out, len,
				  flags);
	if (ret > 0) {
		pos_in += ret;
		pos_out += ret;

		if (off_in) {
			if (copy_to_user(off_in, &pos_in, sizeof(loff_t)))
				ret = -EFAULT;
		} else {
			fd_file(f_in)->f_pos = pos_in;
		}

		if (off_out) {
			if (copy_to_user(off_out, &pos_out, sizeof(loff_t)))
				ret = -EFAULT;
		} else {
			fd_file(f_out)->f_pos = pos_out;
		}
	}
	return ret;
}

/*
 * Don't operate on ranges the page cache doesn't support, and don't exceed the
 * LFS limits.  If pos is under the limit it becomes a short access.  If it
 * exceeds the limit we return -EFBIG.
 */
int generic_write_check_limits(struct file *file, loff_t pos, loff_t *count)
{
	struct inode *inode = file->f_mapping->host;
	loff_t max_size = inode->i_sb->s_maxbytes;
	loff_t limit = rlimit(RLIMIT_FSIZE);

	if (limit != RLIM_INFINITY) {
		if (pos >= limit) {
			send_sig(SIGXFSZ, current, 0);
			return -EFBIG;
		}
		*count = min(*count, limit - pos);
	}

	if (!(file->f_flags & O_LARGEFILE))
		max_size = MAX_NON_LFS;

	if (unlikely(pos >= max_size))
		return -EFBIG;

	*count = min(*count, max_size - pos);

	return 0;
}
EXPORT_SYMBOL_GPL(generic_write_check_limits);

/* Like generic_write_checks(), but takes size of write instead of iter. */
int generic_write_checks_count(struct kiocb *iocb, loff_t *count)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;

	if (IS_SWAPFILE(inode))
		return -ETXTBSY;

	if (!*count)
		return 0;

	if (iocb->ki_flags & IOCB_APPEND)
		iocb->ki_pos = i_size_read(inode);

	if ((iocb->ki_flags & IOCB_NOWAIT) &&
	    !((iocb->ki_flags & IOCB_DIRECT) ||
	      (file->f_op->fop_flags & FOP_BUFFER_WASYNC)))
		return -EINVAL;

	return generic_write_check_limits(iocb->ki_filp, iocb->ki_pos, count);
}
EXPORT_SYMBOL(generic_write_checks_count);

/*
 * Performs necessary checks before doing a write
 *
 * Can adjust writing position or amount of bytes to write.
 * Returns appropriate error code that caller should return or
 * zero in case that write should be allowed.
 */
ssize_t generic_write_checks(struct kiocb *iocb, struct iov_iter *from)
{
	loff_t count = iov_iter_count(from);
	int ret;

	ret = generic_write_checks_count(iocb, &count);
	if (ret)
		return ret;

	iov_iter_truncate(from, count);
	return iov_iter_count(from);
}
EXPORT_SYMBOL(generic_write_checks);

/*
 * Performs common checks before doing a file copy/clone
 * from @file_in to @file_out.
 */
int generic_file_rw_checks(struct file *file_in, struct file *file_out)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);

	/* Don't copy dirs, pipes, sockets... */
	if (S_ISDIR(inode_in->i_mode) || S_ISDIR(inode_out->i_mode))
		return -EISDIR;
	if (!S_ISREG(inode_in->i_mode) || !S_ISREG(inode_out->i_mode))
		return -EINVAL;

	if (!(file_in->f_mode & FMODE_READ) ||
	    !(file_out->f_mode & FMODE_WRITE) ||
	    (file_out->f_flags & O_APPEND))
		return -EBADF;

	return 0;
}

int generic_atomic_write_valid(struct kiocb *iocb, struct iov_iter *iter)
{
	size_t len = iov_iter_count(iter);

	if (!iter_is_ubuf(iter))
		return -EINVAL;

	if (!is_power_of_2(len))
		return -EINVAL;

	if (!IS_ALIGNED(iocb->ki_pos, len))
		return -EINVAL;

	if (!(iocb->ki_flags & IOCB_DIRECT))
		return -EOPNOTSUPP;

	return 0;
}
EXPORT_SYMBOL_GPL(generic_atomic_write_valid);
