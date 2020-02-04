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
	.splice_read	= generic_file_splice_read,
};

EXPORT_SYMBOL(generic_ro_fops);

static inline bool unsigned_offsets(struct file *file)
{
	return file->f_mode & FMODE_UNSIGNED_OFFSET;
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
	if (offset < 0 && !unsigned_offsets(file))
		return -EINVAL;
	if (offset > maxsize)
		return -EINVAL;

	if (offset != file->f_pos) {
		file->f_pos = offset;
		file->f_version = 0;
	}
	return offset;
}
EXPORT_SYMBOL(vfs_setpos);

/**
 * generic_file_llseek_size - generic llseek implementation for regular files
 * @file:	file structure to seek on
 * @offset:	file offset to seek to
 * @whence:	type of seek
 * @size:	max size of this file in file system
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
	switch (whence) {
	case SEEK_END:
		offset += eof;
		break;
	case SEEK_CUR:
		/*
		 * Here we special-case the lseek(fd, 0, SEEK_CUR)
		 * position-querying operation.  Avoid rewriting the "same"
		 * f_pos value back to the file because a concurrent read(),
		 * write() or lseek() might have altered it
		 */
		if (offset == 0)
			return file->f_pos;
		/*
		 * f_lock protects against read/modify/write race with other
		 * SEEK_CURs. Note that parallel writes and reads behave
		 * like SEEK_SET.
		 */
		spin_lock(&file->f_lock);
		offset = vfs_setpos(file, file->f_pos + offset, maxsize);
		spin_unlock(&file->f_lock);
		return offset;
	case SEEK_DATA:
		/*
		 * In the generic case the entire file is data, so as long as
		 * offset isn't at the end of the file then the offset is data.
		 */
		if ((unsigned long long)offset >= eof)
			return -ENXIO;
		break;
	case SEEK_HOLE:
		/*
		 * There is a virtual hole at the end of the file, so as long as
		 * offset isn't i_size or larger, return i_size.
		 */
		if ((unsigned long long)offset >= eof)
			return -ENXIO;
		offset = eof;
		break;
	}

	return vfs_setpos(file, offset, maxsize);
}
EXPORT_SYMBOL(generic_file_llseek_size);

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

loff_t no_llseek(struct file *file, loff_t offset, int whence)
{
	return -ESPIPE;
}
EXPORT_SYMBOL(no_llseek);

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
		if (offset != file->f_pos) {
			file->f_pos = offset;
			file->f_version = 0;
		}
		retval = offset;
	}
out:
	inode_unlock(inode);
	return retval;
}
EXPORT_SYMBOL(default_llseek);

loff_t vfs_llseek(struct file *file, loff_t offset, int whence)
{
	loff_t (*fn)(struct file *, loff_t, int);

	fn = no_llseek;
	if (file->f_mode & FMODE_LSEEK) {
		if (file->f_op->llseek)
			fn = file->f_op->llseek;
	}
	return fn(file, offset, whence);
}
EXPORT_SYMBOL(vfs_llseek);

off_t ksys_lseek(unsigned int fd, off_t offset, unsigned int whence)
{
	off_t retval;
	struct fd f = fdget_pos(fd);
	if (!f.file)
		return -EBADF;

	retval = -EINVAL;
	if (whence <= SEEK_MAX) {
		loff_t res = vfs_llseek(f.file, offset, whence);
		retval = res;
		if (res != (loff_t)retval)
			retval = -EOVERFLOW;	/* LFS: should only happen on 32 bit platforms */
	}
	fdput_pos(f);
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

#if !defined(CONFIG_64BIT) || defined(CONFIG_COMPAT)
SYSCALL_DEFINE5(llseek, unsigned int, fd, unsigned long, offset_high,
		unsigned long, offset_low, loff_t __user *, result,
		unsigned int, whence)
{
	int retval;
	struct fd f = fdget_pos(fd);
	loff_t offset;

	if (!f.file)
		return -EBADF;

	retval = -EINVAL;
	if (whence > SEEK_MAX)
		goto out_putf;

	offset = vfs_llseek(f.file, ((loff_t) offset_high << 32) | offset_low,
			whence);

	retval = (int)offset;
	if (offset >= 0) {
		retval = -EFAULT;
		if (!copy_to_user(result, &offset, sizeof(offset)))
			retval = 0;
	}
out_putf:
	fdput_pos(f);
	return retval;
}
#endif

int rw_verify_area(int read_write, struct file *file, const loff_t *ppos, size_t count)
{
	struct inode *inode;
	int retval = -EINVAL;

	inode = file_inode(file);
	if (unlikely((ssize_t) count < 0))
		return retval;

	/*
	 * ranged mandatory locking does not apply to streams - it makes sense
	 * only for files where position has a meaning.
	 */
	if (ppos) {
		loff_t pos = *ppos;

		if (unlikely(pos < 0)) {
			if (!unsigned_offsets(file))
				return retval;
			if (count >= -pos) /* both values are in 0..LLONG_MAX */
				return -EOVERFLOW;
		} else if (unlikely((loff_t) (pos + count) < 0)) {
			if (!unsigned_offsets(file))
				return retval;
		}

		if (unlikely(inode->i_flctx && mandatory_lock(inode))) {
			retval = locks_mandatory_area(inode, file, pos, pos + count - 1,
					read_write == READ ? F_RDLCK : F_WRLCK);
			if (retval < 0)
				return retval;
		}
	}

	return security_file_permission(file,
				read_write == READ ? MAY_READ : MAY_WRITE);
}

static ssize_t new_sync_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = buf, .iov_len = len };
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = (ppos ? *ppos : 0);
	iov_iter_init(&iter, READ, &iov, 1, len);

	ret = call_read_iter(filp, &kiocb, &iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ppos)
		*ppos = kiocb.ki_pos;
	return ret;
}

ssize_t __vfs_read(struct file *file, char __user *buf, size_t count,
		   loff_t *pos)
{
	if (file->f_op->read)
		return file->f_op->read(file, buf, count, pos);
	else if (file->f_op->read_iter)
		return new_sync_read(file, buf, count, pos);
	else
		return -EINVAL;
}

ssize_t kernel_read(struct file *file, void *buf, size_t count, loff_t *pos)
{
	mm_segment_t old_fs;
	ssize_t result;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	/* The cast to a user pointer is valid due to the set_fs() */
	result = vfs_read(file, (void __user *)buf, count, pos);
	set_fs(old_fs);
	return result;
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
	if (!ret) {
		if (count > MAX_RW_COUNT)
			count =  MAX_RW_COUNT;
		ret = __vfs_read(file, buf, count, pos);
		if (ret > 0) {
			fsnotify_access(file);
			add_rchar(current, ret);
		}
		inc_syscr(current);
	}

	return ret;
}

static ssize_t new_sync_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{
	struct iovec iov = { .iov_base = (void __user *)buf, .iov_len = len };
	struct kiocb kiocb;
	struct iov_iter iter;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	kiocb.ki_pos = (ppos ? *ppos : 0);
	iov_iter_init(&iter, WRITE, &iov, 1, len);

	ret = call_write_iter(filp, &kiocb, &iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ret > 0 && ppos)
		*ppos = kiocb.ki_pos;
	return ret;
}

static ssize_t __vfs_write(struct file *file, const char __user *p,
			   size_t count, loff_t *pos)
{
	if (file->f_op->write)
		return file->f_op->write(file, p, count, pos);
	else if (file->f_op->write_iter)
		return new_sync_write(file, p, count, pos);
	else
		return -EINVAL;
}

ssize_t __kernel_write(struct file *file, const void *buf, size_t count, loff_t *pos)
{
	mm_segment_t old_fs;
	const char __user *p;
	ssize_t ret;

	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	p = (__force const char __user *)buf;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;
	ret = __vfs_write(file, p, count, pos);
	set_fs(old_fs);
	if (ret > 0) {
		fsnotify_modify(file);
		add_wchar(current, ret);
	}
	inc_syscw(current);
	return ret;
}
EXPORT_SYMBOL(__kernel_write);

ssize_t kernel_write(struct file *file, const void *buf, size_t count,
			    loff_t *pos)
{
	mm_segment_t old_fs;
	ssize_t res;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	/* The cast to a user pointer is valid due to the set_fs() */
	res = vfs_write(file, (__force const char __user *)buf, count, pos);
	set_fs(old_fs);

	return res;
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
	if (!ret) {
		if (count > MAX_RW_COUNT)
			count =  MAX_RW_COUNT;
		file_start_write(file);
		ret = __vfs_write(file, buf, count, pos);
		if (ret > 0) {
			fsnotify_modify(file);
			add_wchar(current, ret);
		}
		inc_syscw(current);
		file_end_write(file);
	}

	return ret;
}

/* file_ppos returns &file->f_pos or NULL if file is stream */
static inline loff_t *file_ppos(struct file *file)
{
	return file->f_mode & FMODE_STREAM ? NULL : &file->f_pos;
}

ssize_t ksys_read(unsigned int fd, char __user *buf, size_t count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_read(f.file, buf, count, ppos);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		fdput_pos(f);
	}
	return ret;
}

SYSCALL_DEFINE3(read, unsigned int, fd, char __user *, buf, size_t, count)
{
	return ksys_read(fd, buf, count);
}

ssize_t ksys_write(unsigned int fd, const char __user *buf, size_t count)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_write(f.file, buf, count, ppos);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		fdput_pos(f);
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
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PREAD)
			ret = vfs_read(f.file, buf, count, &pos);
		fdput(f);
	}

	return ret;
}

SYSCALL_DEFINE4(pread64, unsigned int, fd, char __user *, buf,
			size_t, count, loff_t, pos)
{
	return ksys_pread64(fd, buf, count, pos);
}

ssize_t ksys_pwrite64(unsigned int fd, const char __user *buf,
		      size_t count, loff_t pos)
{
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PWRITE)  
			ret = vfs_write(f.file, buf, count, &pos);
		fdput(f);
	}

	return ret;
}

SYSCALL_DEFINE4(pwrite64, unsigned int, fd, const char __user *, buf,
			 size_t, count, loff_t, pos)
{
	return ksys_pwrite64(fd, buf, count, pos);
}

static ssize_t do_iter_readv_writev(struct file *filp, struct iov_iter *iter,
		loff_t *ppos, int type, rwf_t flags)
{
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	ret = kiocb_set_rw_flags(&kiocb, flags);
	if (ret)
		return ret;
	kiocb.ki_pos = (ppos ? *ppos : 0);

	if (type == READ)
		ret = call_read_iter(filp, &kiocb, iter);
	else
		ret = call_write_iter(filp, &kiocb, iter);
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
		struct iovec iovec = iov_iter_iovec(iter);
		ssize_t nr;

		if (type == READ) {
			nr = filp->f_op->read(filp, iovec.iov_base,
					      iovec.iov_len, ppos);
		} else {
			nr = filp->f_op->write(filp, iovec.iov_base,
					       iovec.iov_len, ppos);
		}

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (nr != iovec.iov_len)
			break;
		iov_iter_advance(iter, nr);
	}

	return ret;
}

/**
 * rw_copy_check_uvector() - Copy an array of &struct iovec from userspace
 *     into the kernel and check that it is valid.
 *
 * @type: One of %CHECK_IOVEC_ONLY, %READ, or %WRITE.
 * @uvector: Pointer to the userspace array.
 * @nr_segs: Number of elements in userspace array.
 * @fast_segs: Number of elements in @fast_pointer.
 * @fast_pointer: Pointer to (usually small on-stack) kernel array.
 * @ret_pointer: (output parameter) Pointer to a variable that will point to
 *     either @fast_pointer, a newly allocated kernel array, or NULL,
 *     depending on which array was used.
 *
 * This function copies an array of &struct iovec of @nr_segs from
 * userspace into the kernel and checks that each element is valid (e.g.
 * it does not point to a kernel address or cause overflow by being too
 * large, etc.).
 *
 * As an optimization, the caller may provide a pointer to a small
 * on-stack array in @fast_pointer, typically %UIO_FASTIOV elements long
 * (the size of this array, or 0 if unused, should be given in @fast_segs).
 *
 * @ret_pointer will always point to the array that was used, so the
 * caller must take care not to call kfree() on it e.g. in case the
 * @fast_pointer array was used and it was allocated on the stack.
 *
 * Return: The total number of bytes covered by the iovec array on success
 *   or a negative error code on error.
 */
ssize_t rw_copy_check_uvector(int type, const struct iovec __user * uvector,
			      unsigned long nr_segs, unsigned long fast_segs,
			      struct iovec *fast_pointer,
			      struct iovec **ret_pointer)
{
	unsigned long seg;
	ssize_t ret;
	struct iovec *iov = fast_pointer;

	/*
	 * SuS says "The readv() function *may* fail if the iovcnt argument
	 * was less than or equal to 0, or greater than {IOV_MAX}.  Linux has
	 * traditionally returned zero for zero segments, so...
	 */
	if (nr_segs == 0) {
		ret = 0;
		goto out;
	}

	/*
	 * First get the "struct iovec" from user memory and
	 * verify all the pointers
	 */
	if (nr_segs > UIO_MAXIOV) {
		ret = -EINVAL;
		goto out;
	}
	if (nr_segs > fast_segs) {
		iov = kmalloc_array(nr_segs, sizeof(struct iovec), GFP_KERNEL);
		if (iov == NULL) {
			ret = -ENOMEM;
			goto out;
		}
	}
	if (copy_from_user(iov, uvector, nr_segs*sizeof(*uvector))) {
		ret = -EFAULT;
		goto out;
	}

	/*
	 * According to the Single Unix Specification we should return EINVAL
	 * if an element length is < 0 when cast to ssize_t or if the
	 * total length would overflow the ssize_t return value of the
	 * system call.
	 *
	 * Linux caps all read/write calls to MAX_RW_COUNT, and avoids the
	 * overflow case.
	 */
	ret = 0;
	for (seg = 0; seg < nr_segs; seg++) {
		void __user *buf = iov[seg].iov_base;
		ssize_t len = (ssize_t)iov[seg].iov_len;

		/* see if we we're about to use an invalid len or if
		 * it's about to overflow ssize_t */
		if (len < 0) {
			ret = -EINVAL;
			goto out;
		}
		if (type >= 0
		    && unlikely(!access_ok(buf, len))) {
			ret = -EFAULT;
			goto out;
		}
		if (len > MAX_RW_COUNT - ret) {
			len = MAX_RW_COUNT - ret;
			iov[seg].iov_len = len;
		}
		ret += len;
	}
out:
	*ret_pointer = iov;
	return ret;
}

#ifdef CONFIG_COMPAT
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
	if (nr_segs > UIO_MAXIOV)
		goto out;
	if (nr_segs > fast_segs) {
		ret = -ENOMEM;
		iov = kmalloc_array(nr_segs, sizeof(struct iovec), GFP_KERNEL);
		if (iov == NULL)
			goto out;
	}
	*ret_pointer = iov;

	ret = -EFAULT;
	if (!access_ok(uvector, nr_segs*sizeof(*uvector)))
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
		    !access_ok(compat_ptr(buf), len)) {
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
#endif

static ssize_t do_iter_read(struct file *file, struct iov_iter *iter,
		loff_t *pos, rwf_t flags)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!(file->f_mode & FMODE_READ))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_READ))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		goto out;
	ret = rw_verify_area(READ, file, pos, tot_len);
	if (ret < 0)
		return ret;

	if (file->f_op->read_iter)
		ret = do_iter_readv_writev(file, iter, pos, READ, flags);
	else
		ret = do_loop_readv_writev(file, iter, pos, READ, flags);
out:
	if (ret >= 0)
		fsnotify_access(file);
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

	ret = call_read_iter(file, iocb, iter);
out:
	if (ret >= 0)
		fsnotify_access(file);
	return ret;
}
EXPORT_SYMBOL(vfs_iocb_iter_read);

ssize_t vfs_iter_read(struct file *file, struct iov_iter *iter, loff_t *ppos,
		rwf_t flags)
{
	if (!file->f_op->read_iter)
		return -EINVAL;
	return do_iter_read(file, iter, ppos, flags);
}
EXPORT_SYMBOL(vfs_iter_read);

static ssize_t do_iter_write(struct file *file, struct iov_iter *iter,
		loff_t *pos, rwf_t flags)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		return 0;
	ret = rw_verify_area(WRITE, file, pos, tot_len);
	if (ret < 0)
		return ret;

	if (file->f_op->write_iter)
		ret = do_iter_readv_writev(file, iter, pos, WRITE, flags);
	else
		ret = do_loop_readv_writev(file, iter, pos, WRITE, flags);
	if (ret > 0)
		fsnotify_modify(file);
	return ret;
}

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

	ret = call_write_iter(file, iocb, iter);
	if (ret > 0)
		fsnotify_modify(file);

	return ret;
}
EXPORT_SYMBOL(vfs_iocb_iter_write);

ssize_t vfs_iter_write(struct file *file, struct iov_iter *iter, loff_t *ppos,
		rwf_t flags)
{
	if (!file->f_op->write_iter)
		return -EINVAL;
	return do_iter_write(file, iter, ppos, flags);
}
EXPORT_SYMBOL(vfs_iter_write);

ssize_t vfs_readv(struct file *file, const struct iovec __user *vec,
		  unsigned long vlen, loff_t *pos, rwf_t flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	ssize_t ret;

	ret = import_iovec(READ, vec, vlen, ARRAY_SIZE(iovstack), &iov, &iter);
	if (ret >= 0) {
		ret = do_iter_read(file, &iter, pos, flags);
		kfree(iov);
	}

	return ret;
}

static ssize_t vfs_writev(struct file *file, const struct iovec __user *vec,
		   unsigned long vlen, loff_t *pos, rwf_t flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	ssize_t ret;

	ret = import_iovec(WRITE, vec, vlen, ARRAY_SIZE(iovstack), &iov, &iter);
	if (ret >= 0) {
		file_start_write(file);
		ret = do_iter_write(file, &iter, pos, flags);
		file_end_write(file);
		kfree(iov);
	}
	return ret;
}

static ssize_t do_readv(unsigned long fd, const struct iovec __user *vec,
			unsigned long vlen, rwf_t flags)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_readv(f.file, vec, vlen, ppos, flags);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		fdput_pos(f);
	}

	if (ret > 0)
		add_rchar(current, ret);
	inc_syscr(current);
	return ret;
}

static ssize_t do_writev(unsigned long fd, const struct iovec __user *vec,
			 unsigned long vlen, rwf_t flags)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);
		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_writev(f.file, vec, vlen, ppos, flags);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		fdput_pos(f);
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
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PREAD)
			ret = vfs_readv(f.file, vec, vlen, &pos, flags);
		fdput(f);
	}

	if (ret > 0)
		add_rchar(current, ret);
	inc_syscr(current);
	return ret;
}

static ssize_t do_pwritev(unsigned long fd, const struct iovec __user *vec,
			  unsigned long vlen, loff_t pos, rwf_t flags)
{
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PWRITE)
			ret = vfs_writev(f.file, vec, vlen, &pos, flags);
		fdput(f);
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

#ifdef CONFIG_COMPAT
static size_t compat_readv(struct file *file,
			   const struct compat_iovec __user *vec,
			   unsigned long vlen, loff_t *pos, rwf_t flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	ssize_t ret;

	ret = compat_import_iovec(READ, vec, vlen, UIO_FASTIOV, &iov, &iter);
	if (ret >= 0) {
		ret = do_iter_read(file, &iter, pos, flags);
		kfree(iov);
	}
	if (ret > 0)
		add_rchar(current, ret);
	inc_syscr(current);
	return ret;
}

static size_t do_compat_readv(compat_ulong_t fd,
				 const struct compat_iovec __user *vec,
				 compat_ulong_t vlen, rwf_t flags)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret;
	loff_t pos;

	if (!f.file)
		return -EBADF;
	pos = f.file->f_pos;
	ret = compat_readv(f.file, vec, vlen, &pos, flags);
	if (ret >= 0)
		f.file->f_pos = pos;
	fdput_pos(f);
	return ret;

}

COMPAT_SYSCALL_DEFINE3(readv, compat_ulong_t, fd,
		const struct compat_iovec __user *,vec,
		compat_ulong_t, vlen)
{
	return do_compat_readv(fd, vec, vlen, 0);
}

static long do_compat_preadv64(unsigned long fd,
				  const struct compat_iovec __user *vec,
				  unsigned long vlen, loff_t pos, rwf_t flags)
{
	struct fd f;
	ssize_t ret;

	if (pos < 0)
		return -EINVAL;
	f = fdget(fd);
	if (!f.file)
		return -EBADF;
	ret = -ESPIPE;
	if (f.file->f_mode & FMODE_PREAD)
		ret = compat_readv(f.file, vec, vlen, &pos, flags);
	fdput(f);
	return ret;
}

#ifdef __ARCH_WANT_COMPAT_SYS_PREADV64
COMPAT_SYSCALL_DEFINE4(preadv64, unsigned long, fd,
		const struct compat_iovec __user *,vec,
		unsigned long, vlen, loff_t, pos)
{
	return do_compat_preadv64(fd, vec, vlen, pos, 0);
}
#endif

COMPAT_SYSCALL_DEFINE5(preadv, compat_ulong_t, fd,
		const struct compat_iovec __user *,vec,
		compat_ulong_t, vlen, u32, pos_low, u32, pos_high)
{
	loff_t pos = ((loff_t)pos_high << 32) | pos_low;

	return do_compat_preadv64(fd, vec, vlen, pos, 0);
}

#ifdef __ARCH_WANT_COMPAT_SYS_PREADV64V2
COMPAT_SYSCALL_DEFINE5(preadv64v2, unsigned long, fd,
		const struct compat_iovec __user *,vec,
		unsigned long, vlen, loff_t, pos, rwf_t, flags)
{
	if (pos == -1)
		return do_compat_readv(fd, vec, vlen, flags);

	return do_compat_preadv64(fd, vec, vlen, pos, flags);
}
#endif

COMPAT_SYSCALL_DEFINE6(preadv2, compat_ulong_t, fd,
		const struct compat_iovec __user *,vec,
		compat_ulong_t, vlen, u32, pos_low, u32, pos_high,
		rwf_t, flags)
{
	loff_t pos = ((loff_t)pos_high << 32) | pos_low;

	if (pos == -1)
		return do_compat_readv(fd, vec, vlen, flags);

	return do_compat_preadv64(fd, vec, vlen, pos, flags);
}

static size_t compat_writev(struct file *file,
			    const struct compat_iovec __user *vec,
			    unsigned long vlen, loff_t *pos, rwf_t flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	ssize_t ret;

	ret = compat_import_iovec(WRITE, vec, vlen, UIO_FASTIOV, &iov, &iter);
	if (ret >= 0) {
		file_start_write(file);
		ret = do_iter_write(file, &iter, pos, flags);
		file_end_write(file);
		kfree(iov);
	}
	if (ret > 0)
		add_wchar(current, ret);
	inc_syscw(current);
	return ret;
}

static size_t do_compat_writev(compat_ulong_t fd,
				  const struct compat_iovec __user* vec,
				  compat_ulong_t vlen, rwf_t flags)
{
	struct fd f = fdget_pos(fd);
	ssize_t ret;
	loff_t pos;

	if (!f.file)
		return -EBADF;
	pos = f.file->f_pos;
	ret = compat_writev(f.file, vec, vlen, &pos, flags);
	if (ret >= 0)
		f.file->f_pos = pos;
	fdput_pos(f);
	return ret;
}

COMPAT_SYSCALL_DEFINE3(writev, compat_ulong_t, fd,
		const struct compat_iovec __user *, vec,
		compat_ulong_t, vlen)
{
	return do_compat_writev(fd, vec, vlen, 0);
}

static long do_compat_pwritev64(unsigned long fd,
				   const struct compat_iovec __user *vec,
				   unsigned long vlen, loff_t pos, rwf_t flags)
{
	struct fd f;
	ssize_t ret;

	if (pos < 0)
		return -EINVAL;
	f = fdget(fd);
	if (!f.file)
		return -EBADF;
	ret = -ESPIPE;
	if (f.file->f_mode & FMODE_PWRITE)
		ret = compat_writev(f.file, vec, vlen, &pos, flags);
	fdput(f);
	return ret;
}

#ifdef __ARCH_WANT_COMPAT_SYS_PWRITEV64
COMPAT_SYSCALL_DEFINE4(pwritev64, unsigned long, fd,
		const struct compat_iovec __user *,vec,
		unsigned long, vlen, loff_t, pos)
{
	return do_compat_pwritev64(fd, vec, vlen, pos, 0);
}
#endif

COMPAT_SYSCALL_DEFINE5(pwritev, compat_ulong_t, fd,
		const struct compat_iovec __user *,vec,
		compat_ulong_t, vlen, u32, pos_low, u32, pos_high)
{
	loff_t pos = ((loff_t)pos_high << 32) | pos_low;

	return do_compat_pwritev64(fd, vec, vlen, pos, 0);
}

#ifdef __ARCH_WANT_COMPAT_SYS_PWRITEV64V2
COMPAT_SYSCALL_DEFINE5(pwritev64v2, unsigned long, fd,
		const struct compat_iovec __user *,vec,
		unsigned long, vlen, loff_t, pos, rwf_t, flags)
{
	if (pos == -1)
		return do_compat_writev(fd, vec, vlen, flags);

	return do_compat_pwritev64(fd, vec, vlen, pos, flags);
}
#endif

COMPAT_SYSCALL_DEFINE6(pwritev2, compat_ulong_t, fd,
		const struct compat_iovec __user *,vec,
		compat_ulong_t, vlen, u32, pos_low, u32, pos_high, rwf_t, flags)
{
	loff_t pos = ((loff_t)pos_high << 32) | pos_low;

	if (pos == -1)
		return do_compat_writev(fd, vec, vlen, flags);

	return do_compat_pwritev64(fd, vec, vlen, pos, flags);
}

#endif

static ssize_t do_sendfile(int out_fd, int in_fd, loff_t *ppos,
		  	   size_t count, loff_t max)
{
	struct fd in, out;
	struct inode *in_inode, *out_inode;
	loff_t pos;
	loff_t out_pos;
	ssize_t retval;
	int fl;

	/*
	 * Get input file, and verify that it is ok..
	 */
	retval = -EBADF;
	in = fdget(in_fd);
	if (!in.file)
		goto out;
	if (!(in.file->f_mode & FMODE_READ))
		goto fput_in;
	retval = -ESPIPE;
	if (!ppos) {
		pos = in.file->f_pos;
	} else {
		pos = *ppos;
		if (!(in.file->f_mode & FMODE_PREAD))
			goto fput_in;
	}
	retval = rw_verify_area(READ, in.file, &pos, count);
	if (retval < 0)
		goto fput_in;
	if (count > MAX_RW_COUNT)
		count =  MAX_RW_COUNT;

	/*
	 * Get output file, and verify that it is ok..
	 */
	retval = -EBADF;
	out = fdget(out_fd);
	if (!out.file)
		goto fput_in;
	if (!(out.file->f_mode & FMODE_WRITE))
		goto fput_out;
	in_inode = file_inode(in.file);
	out_inode = file_inode(out.file);
	out_pos = out.file->f_pos;
	retval = rw_verify_area(WRITE, out.file, &out_pos, count);
	if (retval < 0)
		goto fput_out;

	if (!max)
		max = min(in_inode->i_sb->s_maxbytes, out_inode->i_sb->s_maxbytes);

	if (unlikely(pos + count > max)) {
		retval = -EOVERFLOW;
		if (pos >= max)
			goto fput_out;
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
	if (in.file->f_flags & O_NONBLOCK)
		fl = SPLICE_F_NONBLOCK;
#endif
	file_start_write(out.file);
	retval = do_splice_direct(in.file, &pos, out.file, &out_pos, count, fl);
	file_end_write(out.file);

	if (retval > 0) {
		add_rchar(current, retval);
		add_wchar(current, retval);
		fsnotify_access(in.file);
		fsnotify_modify(out.file);
		out.file->f_pos = out_pos;
		if (ppos)
			*ppos = pos;
		else
			in.file->f_pos = pos;
	}

	inc_syscr(current);
	inc_syscw(current);
	if (pos > max)
		retval = -EOVERFLOW;

fput_out:
	fdput(out);
fput_in:
	fdput(in);
out:
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

/**
 * generic_copy_file_range - copy data between two files
 * @file_in:	file structure to read from
 * @pos_in:	file offset to read from
 * @file_out:	file structure to write data to
 * @pos_out:	file offset to write data to
 * @len:	amount of data to copy
 * @flags:	copy flags
 *
 * This is a generic filesystem helper to copy data from one file to another.
 * It has no constraints on the source or destination file owners - the files
 * can belong to different superblocks and different filesystem types. Short
 * copies are allowed.
 *
 * This should be called from the @file_out filesystem, as per the
 * ->copy_file_range() method.
 *
 * Returns the number of bytes copied or a negative error indicating the
 * failure.
 */

ssize_t generic_copy_file_range(struct file *file_in, loff_t pos_in,
				struct file *file_out, loff_t pos_out,
				size_t len, unsigned int flags)
{
	return do_splice_direct(file_in, &pos_in, file_out, &pos_out,
				len > MAX_RW_COUNT ? MAX_RW_COUNT : len, 0);
}
EXPORT_SYMBOL(generic_copy_file_range);

static ssize_t do_copy_file_range(struct file *file_in, loff_t pos_in,
				  struct file *file_out, loff_t pos_out,
				  size_t len, unsigned int flags)
{
	/*
	 * Although we now allow filesystems to handle cross sb copy, passing
	 * a file of the wrong filesystem type to filesystem driver can result
	 * in an attempt to dereference the wrong type of ->private_data, so
	 * avoid doing that until we really have a good reason.  NFS defines
	 * several different file_system_type structures, but they all end up
	 * using the same ->copy_file_range() function pointer.
	 */
	if (file_out->f_op->copy_file_range &&
	    file_out->f_op->copy_file_range == file_in->f_op->copy_file_range)
		return file_out->f_op->copy_file_range(file_in, pos_in,
						       file_out, pos_out,
						       len, flags);

	return generic_copy_file_range(file_in, pos_in, file_out, pos_out, len,
				       flags);
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

	if (flags != 0)
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
	 * Try cloning first, this is supported by more file systems, and
	 * more efficient if both clone and copy are supported (e.g. NFS).
	 */
	if (file_in->f_op->remap_file_range &&
	    file_inode(file_in)->i_sb == file_inode(file_out)->i_sb) {
		loff_t cloned;

		cloned = file_in->f_op->remap_file_range(file_in, pos_in,
				file_out, pos_out,
				min_t(loff_t, MAX_RW_COUNT, len),
				REMAP_FILE_CAN_SHORTEN);
		if (cloned > 0) {
			ret = cloned;
			goto done;
		}
	}

	ret = do_copy_file_range(file_in, pos_in, file_out, pos_out, len,
				flags);
	WARN_ON_ONCE(ret == -EOPNOTSUPP);
done:
	if (ret > 0) {
		fsnotify_access(file_in);
		add_rchar(current, ret);
		fsnotify_modify(file_out);
		add_wchar(current, ret);
	}

	inc_syscr(current);
	inc_syscw(current);

	file_end_write(file_out);

	return ret;
}
EXPORT_SYMBOL(vfs_copy_file_range);

SYSCALL_DEFINE6(copy_file_range, int, fd_in, loff_t __user *, off_in,
		int, fd_out, loff_t __user *, off_out,
		size_t, len, unsigned int, flags)
{
	loff_t pos_in;
	loff_t pos_out;
	struct fd f_in;
	struct fd f_out;
	ssize_t ret = -EBADF;

	f_in = fdget(fd_in);
	if (!f_in.file)
		goto out2;

	f_out = fdget(fd_out);
	if (!f_out.file)
		goto out1;

	ret = -EFAULT;
	if (off_in) {
		if (copy_from_user(&pos_in, off_in, sizeof(loff_t)))
			goto out;
	} else {
		pos_in = f_in.file->f_pos;
	}

	if (off_out) {
		if (copy_from_user(&pos_out, off_out, sizeof(loff_t)))
			goto out;
	} else {
		pos_out = f_out.file->f_pos;
	}

	ret = vfs_copy_file_range(f_in.file, pos_in, f_out.file, pos_out, len,
				  flags);
	if (ret > 0) {
		pos_in += ret;
		pos_out += ret;

		if (off_in) {
			if (copy_to_user(off_in, &pos_in, sizeof(loff_t)))
				ret = -EFAULT;
		} else {
			f_in.file->f_pos = pos_in;
		}

		if (off_out) {
			if (copy_to_user(off_out, &pos_out, sizeof(loff_t)))
				ret = -EFAULT;
		} else {
			f_out.file->f_pos = pos_out;
		}
	}

out:
	fdput(f_out);
out1:
	fdput(f_in);
out2:
	return ret;
}

static int remap_verify_area(struct file *file, loff_t pos, loff_t len,
			     bool write)
{
	struct inode *inode = file_inode(file);

	if (unlikely(pos < 0 || len < 0))
		return -EINVAL;

	 if (unlikely((loff_t) (pos + len) < 0))
		return -EINVAL;

	if (unlikely(inode->i_flctx && mandatory_lock(inode))) {
		loff_t end = len ? pos + len - 1 : OFFSET_MAX;
		int retval;

		retval = locks_mandatory_area(inode, file, pos, end,
				write ? F_WRLCK : F_RDLCK);
		if (retval < 0)
			return retval;
	}

	return security_file_permission(file, write ? MAY_WRITE : MAY_READ);
}
/*
 * Ensure that we don't remap a partial EOF block in the middle of something
 * else.  Assume that the offsets have already been checked for block
 * alignment.
 *
 * For clone we only link a partial EOF block above or at the destination file's
 * EOF.  For deduplication we accept a partial EOF block only if it ends at the
 * destination file's EOF (can not link it into the middle of a file).
 *
 * Shorten the request if possible.
 */
static int generic_remap_check_len(struct inode *inode_in,
				   struct inode *inode_out,
				   loff_t pos_out,
				   loff_t *len,
				   unsigned int remap_flags)
{
	u64 blkmask = i_blocksize(inode_in) - 1;
	loff_t new_len = *len;

	if ((*len & blkmask) == 0)
		return 0;

	if (pos_out + *len < i_size_read(inode_out))
		new_len &= ~blkmask;

	if (new_len == *len)
		return 0;

	if (remap_flags & REMAP_FILE_CAN_SHORTEN) {
		*len = new_len;
		return 0;
	}

	return (remap_flags & REMAP_FILE_DEDUP) ? -EBADE : -EINVAL;
}

/* Read a page's worth of file data into the page cache. */
static struct page *vfs_dedupe_get_page(struct inode *inode, loff_t offset)
{
	struct page *page;

	page = read_mapping_page(inode->i_mapping, offset >> PAGE_SHIFT, NULL);
	if (IS_ERR(page))
		return page;
	if (!PageUptodate(page)) {
		put_page(page);
		return ERR_PTR(-EIO);
	}
	return page;
}

/*
 * Lock two pages, ensuring that we lock in offset order if the pages are from
 * the same file.
 */
static void vfs_lock_two_pages(struct page *page1, struct page *page2)
{
	/* Always lock in order of increasing index. */
	if (page1->index > page2->index)
		swap(page1, page2);

	lock_page(page1);
	if (page1 != page2)
		lock_page(page2);
}

/* Unlock two pages, being careful not to unlock the same page twice. */
static void vfs_unlock_two_pages(struct page *page1, struct page *page2)
{
	unlock_page(page1);
	if (page1 != page2)
		unlock_page(page2);
}

/*
 * Compare extents of two files to see if they are the same.
 * Caller must have locked both inodes to prevent write races.
 */
static int vfs_dedupe_file_range_compare(struct inode *src, loff_t srcoff,
					 struct inode *dest, loff_t destoff,
					 loff_t len, bool *is_same)
{
	loff_t src_poff;
	loff_t dest_poff;
	void *src_addr;
	void *dest_addr;
	struct page *src_page;
	struct page *dest_page;
	loff_t cmp_len;
	bool same;
	int error;

	error = -EINVAL;
	same = true;
	while (len) {
		src_poff = srcoff & (PAGE_SIZE - 1);
		dest_poff = destoff & (PAGE_SIZE - 1);
		cmp_len = min(PAGE_SIZE - src_poff,
			      PAGE_SIZE - dest_poff);
		cmp_len = min(cmp_len, len);
		if (cmp_len <= 0)
			goto out_error;

		src_page = vfs_dedupe_get_page(src, srcoff);
		if (IS_ERR(src_page)) {
			error = PTR_ERR(src_page);
			goto out_error;
		}
		dest_page = vfs_dedupe_get_page(dest, destoff);
		if (IS_ERR(dest_page)) {
			error = PTR_ERR(dest_page);
			put_page(src_page);
			goto out_error;
		}

		vfs_lock_two_pages(src_page, dest_page);

		/*
		 * Now that we've locked both pages, make sure they're still
		 * mapped to the file data we're interested in.  If not,
		 * someone is invalidating pages on us and we lose.
		 */
		if (!PageUptodate(src_page) || !PageUptodate(dest_page) ||
		    src_page->mapping != src->i_mapping ||
		    dest_page->mapping != dest->i_mapping) {
			same = false;
			goto unlock;
		}

		src_addr = kmap_atomic(src_page);
		dest_addr = kmap_atomic(dest_page);

		flush_dcache_page(src_page);
		flush_dcache_page(dest_page);

		if (memcmp(src_addr + src_poff, dest_addr + dest_poff, cmp_len))
			same = false;

		kunmap_atomic(dest_addr);
		kunmap_atomic(src_addr);
unlock:
		vfs_unlock_two_pages(src_page, dest_page);
		put_page(dest_page);
		put_page(src_page);

		if (!same)
			break;

		srcoff += cmp_len;
		destoff += cmp_len;
		len -= cmp_len;
	}

	*is_same = same;
	return 0;

out_error:
	return error;
}

/*
 * Check that the two inodes are eligible for cloning, the ranges make
 * sense, and then flush all dirty data.  Caller must ensure that the
 * inodes have been locked against any other modifications.
 *
 * If there's an error, then the usual negative error code is returned.
 * Otherwise returns 0 with *len set to the request length.
 */
int generic_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				  struct file *file_out, loff_t pos_out,
				  loff_t *len, unsigned int remap_flags)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);
	bool same_inode = (inode_in == inode_out);
	int ret;

	/* Don't touch certain kinds of inodes */
	if (IS_IMMUTABLE(inode_out))
		return -EPERM;

	if (IS_SWAPFILE(inode_in) || IS_SWAPFILE(inode_out))
		return -ETXTBSY;

	/* Don't reflink dirs, pipes, sockets... */
	if (S_ISDIR(inode_in->i_mode) || S_ISDIR(inode_out->i_mode))
		return -EISDIR;
	if (!S_ISREG(inode_in->i_mode) || !S_ISREG(inode_out->i_mode))
		return -EINVAL;

	/* Zero length dedupe exits immediately; reflink goes to EOF. */
	if (*len == 0) {
		loff_t isize = i_size_read(inode_in);

		if ((remap_flags & REMAP_FILE_DEDUP) || pos_in == isize)
			return 0;
		if (pos_in > isize)
			return -EINVAL;
		*len = isize - pos_in;
		if (*len == 0)
			return 0;
	}

	/* Check that we don't violate system file offset limits. */
	ret = generic_remap_checks(file_in, pos_in, file_out, pos_out, len,
			remap_flags);
	if (ret)
		return ret;

	/* Wait for the completion of any pending IOs on both files */
	inode_dio_wait(inode_in);
	if (!same_inode)
		inode_dio_wait(inode_out);

	ret = filemap_write_and_wait_range(inode_in->i_mapping,
			pos_in, pos_in + *len - 1);
	if (ret)
		return ret;

	ret = filemap_write_and_wait_range(inode_out->i_mapping,
			pos_out, pos_out + *len - 1);
	if (ret)
		return ret;

	/*
	 * Check that the extents are the same.
	 */
	if (remap_flags & REMAP_FILE_DEDUP) {
		bool		is_same = false;

		ret = vfs_dedupe_file_range_compare(inode_in, pos_in,
				inode_out, pos_out, *len, &is_same);
		if (ret)
			return ret;
		if (!is_same)
			return -EBADE;
	}

	ret = generic_remap_check_len(inode_in, inode_out, pos_out, len,
			remap_flags);
	if (ret)
		return ret;

	/* If can't alter the file contents, we're done. */
	if (!(remap_flags & REMAP_FILE_DEDUP))
		ret = file_modified(file_out);

	return ret;
}
EXPORT_SYMBOL(generic_remap_file_range_prep);

loff_t do_clone_file_range(struct file *file_in, loff_t pos_in,
			   struct file *file_out, loff_t pos_out,
			   loff_t len, unsigned int remap_flags)
{
	loff_t ret;

	WARN_ON_ONCE(remap_flags & REMAP_FILE_DEDUP);

	/*
	 * FICLONE/FICLONERANGE ioctls enforce that src and dest files are on
	 * the same mount. Practically, they only need to be on the same file
	 * system.
	 */
	if (file_inode(file_in)->i_sb != file_inode(file_out)->i_sb)
		return -EXDEV;

	ret = generic_file_rw_checks(file_in, file_out);
	if (ret < 0)
		return ret;

	if (!file_in->f_op->remap_file_range)
		return -EOPNOTSUPP;

	ret = remap_verify_area(file_in, pos_in, len, false);
	if (ret)
		return ret;

	ret = remap_verify_area(file_out, pos_out, len, true);
	if (ret)
		return ret;

	ret = file_in->f_op->remap_file_range(file_in, pos_in,
			file_out, pos_out, len, remap_flags);
	if (ret < 0)
		return ret;

	fsnotify_access(file_in);
	fsnotify_modify(file_out);
	return ret;
}
EXPORT_SYMBOL(do_clone_file_range);

loff_t vfs_clone_file_range(struct file *file_in, loff_t pos_in,
			    struct file *file_out, loff_t pos_out,
			    loff_t len, unsigned int remap_flags)
{
	loff_t ret;

	file_start_write(file_out);
	ret = do_clone_file_range(file_in, pos_in, file_out, pos_out, len,
				  remap_flags);
	file_end_write(file_out);

	return ret;
}
EXPORT_SYMBOL(vfs_clone_file_range);

/* Check whether we are allowed to dedupe the destination file */
static bool allow_file_dedupe(struct file *file)
{
	if (capable(CAP_SYS_ADMIN))
		return true;
	if (file->f_mode & FMODE_WRITE)
		return true;
	if (uid_eq(current_fsuid(), file_inode(file)->i_uid))
		return true;
	if (!inode_permission(file_inode(file), MAY_WRITE))
		return true;
	return false;
}

loff_t vfs_dedupe_file_range_one(struct file *src_file, loff_t src_pos,
				 struct file *dst_file, loff_t dst_pos,
				 loff_t len, unsigned int remap_flags)
{
	loff_t ret;

	WARN_ON_ONCE(remap_flags & ~(REMAP_FILE_DEDUP |
				     REMAP_FILE_CAN_SHORTEN));

	ret = mnt_want_write_file(dst_file);
	if (ret)
		return ret;

	ret = remap_verify_area(dst_file, dst_pos, len, true);
	if (ret < 0)
		goto out_drop_write;

	ret = -EPERM;
	if (!allow_file_dedupe(dst_file))
		goto out_drop_write;

	ret = -EXDEV;
	if (src_file->f_path.mnt != dst_file->f_path.mnt)
		goto out_drop_write;

	ret = -EISDIR;
	if (S_ISDIR(file_inode(dst_file)->i_mode))
		goto out_drop_write;

	ret = -EINVAL;
	if (!dst_file->f_op->remap_file_range)
		goto out_drop_write;

	if (len == 0) {
		ret = 0;
		goto out_drop_write;
	}

	ret = dst_file->f_op->remap_file_range(src_file, src_pos, dst_file,
			dst_pos, len, remap_flags | REMAP_FILE_DEDUP);
out_drop_write:
	mnt_drop_write_file(dst_file);

	return ret;
}
EXPORT_SYMBOL(vfs_dedupe_file_range_one);

int vfs_dedupe_file_range(struct file *file, struct file_dedupe_range *same)
{
	struct file_dedupe_range_info *info;
	struct inode *src = file_inode(file);
	u64 off;
	u64 len;
	int i;
	int ret;
	u16 count = same->dest_count;
	loff_t deduped;

	if (!(file->f_mode & FMODE_READ))
		return -EINVAL;

	if (same->reserved1 || same->reserved2)
		return -EINVAL;

	off = same->src_offset;
	len = same->src_length;

	if (S_ISDIR(src->i_mode))
		return -EISDIR;

	if (!S_ISREG(src->i_mode))
		return -EINVAL;

	if (!file->f_op->remap_file_range)
		return -EOPNOTSUPP;

	ret = remap_verify_area(file, off, len, false);
	if (ret < 0)
		return ret;
	ret = 0;

	if (off + len > i_size_read(src))
		return -EINVAL;

	/* Arbitrary 1G limit on a single dedupe request, can be raised. */
	len = min_t(u64, len, 1 << 30);

	/* pre-format output fields to sane values */
	for (i = 0; i < count; i++) {
		same->info[i].bytes_deduped = 0ULL;
		same->info[i].status = FILE_DEDUPE_RANGE_SAME;
	}

	for (i = 0, info = same->info; i < count; i++, info++) {
		struct fd dst_fd = fdget(info->dest_fd);
		struct file *dst_file = dst_fd.file;

		if (!dst_file) {
			info->status = -EBADF;
			goto next_loop;
		}

		if (info->reserved) {
			info->status = -EINVAL;
			goto next_fdput;
		}

		deduped = vfs_dedupe_file_range_one(file, off, dst_file,
						    info->dest_offset, len,
						    REMAP_FILE_CAN_SHORTEN);
		if (deduped == -EBADE)
			info->status = FILE_DEDUPE_RANGE_DIFFERS;
		else if (deduped < 0)
			info->status = deduped;
		else
			info->bytes_deduped = len;

next_fdput:
		fdput(dst_fd);
next_loop:
		if (fatal_signal_pending(current))
			break;
	}
	return ret;
}
EXPORT_SYMBOL(vfs_dedupe_file_range);
