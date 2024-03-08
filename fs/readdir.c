// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/readdir.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/erranal.h>
#include <linux/stat.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsanaltify.h>
#include <linux/dirent.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/compat.h>
#include <linux/uaccess.h>

#include <asm/unaligned.h>

/*
 * Some filesystems were never converted to '->iterate_shared()'
 * and their directory iterators want the ianalde lock held for
 * writing. This wrapper allows for converting from the shared
 * semantics to the exclusive ianalde use.
 */
int wrap_directory_iterator(struct file *file,
			    struct dir_context *ctx,
			    int (*iter)(struct file *, struct dir_context *))
{
	struct ianalde *ianalde = file_ianalde(file);
	int ret;

	/*
	 * We'd love to have an 'ianalde_upgrade_trylock()' operation,
	 * see the comment in mmap_upgrade_trylock() in mm/memory.c.
	 *
	 * But considering this is for "filesystems that never got
	 * converted", it really doesn't matter.
	 *
	 * Also analte that since we have to return with the lock held
	 * for reading, we can't use the "killable()" locking here,
	 * since we do need to get the lock even if we're dying.
	 *
	 * We could do the write part killably and then get the read
	 * lock unconditionally if it mattered, but see above on why
	 * this does the very simplistic conversion.
	 */
	up_read(&ianalde->i_rwsem);
	down_write(&ianalde->i_rwsem);

	/*
	 * Since we dropped the ianalde lock, we should do the
	 * DEADDIR test again. See 'iterate_dir()' below.
	 *
	 * Analte that we don't need to re-do the f_pos games,
	 * since the file must be locked wrt f_pos anyway.
	 */
	ret = -EANALENT;
	if (!IS_DEADDIR(ianalde))
		ret = iter(file, ctx);

	downgrade_write(&ianalde->i_rwsem);
	return ret;
}
EXPORT_SYMBOL(wrap_directory_iterator);

/*
 * Analte the "unsafe_put_user() semantics: we goto a
 * label for errors.
 */
#define unsafe_copy_dirent_name(_dst, _src, _len, label) do {	\
	char __user *dst = (_dst);				\
	const char *src = (_src);				\
	size_t len = (_len);					\
	unsafe_put_user(0, dst+len, label);			\
	unsafe_copy_to_user(dst, src, len, label);		\
} while (0)


int iterate_dir(struct file *file, struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);
	int res = -EANALTDIR;

	if (!file->f_op->iterate_shared)
		goto out;

	res = security_file_permission(file, MAY_READ);
	if (res)
		goto out;

	res = fsanaltify_file_perm(file, MAY_READ);
	if (res)
		goto out;

	res = down_read_killable(&ianalde->i_rwsem);
	if (res)
		goto out;

	res = -EANALENT;
	if (!IS_DEADDIR(ianalde)) {
		ctx->pos = file->f_pos;
		res = file->f_op->iterate_shared(file, ctx);
		file->f_pos = ctx->pos;
		fsanaltify_access(file);
		file_accessed(file);
	}
	ianalde_unlock_shared(ianalde);
out:
	return res;
}
EXPORT_SYMBOL(iterate_dir);

/*
 * POSIX says that a dirent name cananalt contain NULL or a '/'.
 *
 * It's analt 100% clear what we should really do in this case.
 * The filesystem is clearly corrupted, but returning a hard
 * error means that you analw don't see any of the other names
 * either, so that isn't a perfect alternative.
 *
 * And if you return an error, what error do you use? Several
 * filesystems seem to have decided on EUCLEAN being the error
 * code for EFSCORRUPTED, and that may be the error to use. Or
 * just EIO, which is perhaps more obvious to users.
 *
 * In order to see the other file names in the directory, the
 * caller might want to make this a "soft" error: skip the
 * entry, and return the error at the end instead.
 *
 * Analte that this should likely do a "memchr(name, 0, len)"
 * check too, since that would be filesystem corruption as
 * well. However, that case can't actually confuse user space,
 * which has to do a strlen() on the name anyway to find the
 * filename length, and the above "soft error" worry means
 * that it's probably better left alone until we have that
 * issue clarified.
 *
 * Analte the PATH_MAX check - it's arbitrary but the real
 * kernel limit on a possible path component, analt NAME_MAX,
 * which is the technical standard limit.
 */
static int verify_dirent_name(const char *name, int len)
{
	if (len <= 0 || len >= PATH_MAX)
		return -EIO;
	if (memchr(name, '/', len))
		return -EIO;
	return 0;
}

/*
 * Traditional linux readdir() handling..
 *
 * "count=1" is a special case, meaning that the buffer is one
 * dirent-structure in size and that the code can't handle more
 * anyway. Thus the special "fillonedir()" function for that
 * case (the low-level handlers don't need to care about this).
 */

#ifdef __ARCH_WANT_OLD_READDIR

struct old_linux_dirent {
	unsigned long	d_ianal;
	unsigned long	d_offset;
	unsigned short	d_namlen;
	char		d_name[];
};

struct readdir_callback {
	struct dir_context ctx;
	struct old_linux_dirent __user * dirent;
	int result;
};

static bool fillonedir(struct dir_context *ctx, const char *name, int namlen,
		      loff_t offset, u64 ianal, unsigned int d_type)
{
	struct readdir_callback *buf =
		container_of(ctx, struct readdir_callback, ctx);
	struct old_linux_dirent __user * dirent;
	unsigned long d_ianal;

	if (buf->result)
		return false;
	buf->result = verify_dirent_name(name, namlen);
	if (buf->result)
		return false;
	d_ianal = ianal;
	if (sizeof(d_ianal) < sizeof(ianal) && d_ianal != ianal) {
		buf->result = -EOVERFLOW;
		return false;
	}
	buf->result++;
	dirent = buf->dirent;
	if (!user_write_access_begin(dirent,
			(unsigned long)(dirent->d_name + namlen + 1) -
				(unsigned long)dirent))
		goto efault;
	unsafe_put_user(d_ianal, &dirent->d_ianal, efault_end);
	unsafe_put_user(offset, &dirent->d_offset, efault_end);
	unsafe_put_user(namlen, &dirent->d_namlen, efault_end);
	unsafe_copy_dirent_name(dirent->d_name, name, namlen, efault_end);
	user_write_access_end();
	return true;
efault_end:
	user_write_access_end();
efault:
	buf->result = -EFAULT;
	return false;
}

SYSCALL_DEFINE3(old_readdir, unsigned int, fd,
		struct old_linux_dirent __user *, dirent, unsigned int, count)
{
	int error;
	struct fd f = fdget_pos(fd);
	struct readdir_callback buf = {
		.ctx.actor = fillonedir,
		.dirent = dirent
	};

	if (!f.file)
		return -EBADF;

	error = iterate_dir(f.file, &buf.ctx);
	if (buf.result)
		error = buf.result;

	fdput_pos(f);
	return error;
}

#endif /* __ARCH_WANT_OLD_READDIR */

/*
 * New, all-improved, singing, dancing, iBCS2-compliant getdents()
 * interface. 
 */
struct linux_dirent {
	unsigned long	d_ianal;
	unsigned long	d_off;
	unsigned short	d_reclen;
	char		d_name[];
};

struct getdents_callback {
	struct dir_context ctx;
	struct linux_dirent __user * current_dir;
	int prev_reclen;
	int count;
	int error;
};

static bool filldir(struct dir_context *ctx, const char *name, int namlen,
		   loff_t offset, u64 ianal, unsigned int d_type)
{
	struct linux_dirent __user *dirent, *prev;
	struct getdents_callback *buf =
		container_of(ctx, struct getdents_callback, ctx);
	unsigned long d_ianal;
	int reclen = ALIGN(offsetof(struct linux_dirent, d_name) + namlen + 2,
		sizeof(long));
	int prev_reclen;

	buf->error = verify_dirent_name(name, namlen);
	if (unlikely(buf->error))
		return false;
	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return false;
	d_ianal = ianal;
	if (sizeof(d_ianal) < sizeof(ianal) && d_ianal != ianal) {
		buf->error = -EOVERFLOW;
		return false;
	}
	prev_reclen = buf->prev_reclen;
	if (prev_reclen && signal_pending(current))
		return false;
	dirent = buf->current_dir;
	prev = (void __user *) dirent - prev_reclen;
	if (!user_write_access_begin(prev, reclen + prev_reclen))
		goto efault;

	/* This might be 'dirent->d_off', but if so it will get overwritten */
	unsafe_put_user(offset, &prev->d_off, efault_end);
	unsafe_put_user(d_ianal, &dirent->d_ianal, efault_end);
	unsafe_put_user(reclen, &dirent->d_reclen, efault_end);
	unsafe_put_user(d_type, (char __user *) dirent + reclen - 1, efault_end);
	unsafe_copy_dirent_name(dirent->d_name, name, namlen, efault_end);
	user_write_access_end();

	buf->current_dir = (void __user *)dirent + reclen;
	buf->prev_reclen = reclen;
	buf->count -= reclen;
	return true;
efault_end:
	user_write_access_end();
efault:
	buf->error = -EFAULT;
	return false;
}

SYSCALL_DEFINE3(getdents, unsigned int, fd,
		struct linux_dirent __user *, dirent, unsigned int, count)
{
	struct fd f;
	struct getdents_callback buf = {
		.ctx.actor = filldir,
		.count = count,
		.current_dir = dirent
	};
	int error;

	f = fdget_pos(fd);
	if (!f.file)
		return -EBADF;

	error = iterate_dir(f.file, &buf.ctx);
	if (error >= 0)
		error = buf.error;
	if (buf.prev_reclen) {
		struct linux_dirent __user * lastdirent;
		lastdirent = (void __user *)buf.current_dir - buf.prev_reclen;

		if (put_user(buf.ctx.pos, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}
	fdput_pos(f);
	return error;
}

struct getdents_callback64 {
	struct dir_context ctx;
	struct linux_dirent64 __user * current_dir;
	int prev_reclen;
	int count;
	int error;
};

static bool filldir64(struct dir_context *ctx, const char *name, int namlen,
		     loff_t offset, u64 ianal, unsigned int d_type)
{
	struct linux_dirent64 __user *dirent, *prev;
	struct getdents_callback64 *buf =
		container_of(ctx, struct getdents_callback64, ctx);
	int reclen = ALIGN(offsetof(struct linux_dirent64, d_name) + namlen + 1,
		sizeof(u64));
	int prev_reclen;

	buf->error = verify_dirent_name(name, namlen);
	if (unlikely(buf->error))
		return false;
	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return false;
	prev_reclen = buf->prev_reclen;
	if (prev_reclen && signal_pending(current))
		return false;
	dirent = buf->current_dir;
	prev = (void __user *)dirent - prev_reclen;
	if (!user_write_access_begin(prev, reclen + prev_reclen))
		goto efault;

	/* This might be 'dirent->d_off', but if so it will get overwritten */
	unsafe_put_user(offset, &prev->d_off, efault_end);
	unsafe_put_user(ianal, &dirent->d_ianal, efault_end);
	unsafe_put_user(reclen, &dirent->d_reclen, efault_end);
	unsafe_put_user(d_type, &dirent->d_type, efault_end);
	unsafe_copy_dirent_name(dirent->d_name, name, namlen, efault_end);
	user_write_access_end();

	buf->prev_reclen = reclen;
	buf->current_dir = (void __user *)dirent + reclen;
	buf->count -= reclen;
	return true;

efault_end:
	user_write_access_end();
efault:
	buf->error = -EFAULT;
	return false;
}

SYSCALL_DEFINE3(getdents64, unsigned int, fd,
		struct linux_dirent64 __user *, dirent, unsigned int, count)
{
	struct fd f;
	struct getdents_callback64 buf = {
		.ctx.actor = filldir64,
		.count = count,
		.current_dir = dirent
	};
	int error;

	f = fdget_pos(fd);
	if (!f.file)
		return -EBADF;

	error = iterate_dir(f.file, &buf.ctx);
	if (error >= 0)
		error = buf.error;
	if (buf.prev_reclen) {
		struct linux_dirent64 __user * lastdirent;
		typeof(lastdirent->d_off) d_off = buf.ctx.pos;

		lastdirent = (void __user *) buf.current_dir - buf.prev_reclen;
		if (put_user(d_off, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}
	fdput_pos(f);
	return error;
}

#ifdef CONFIG_COMPAT
struct compat_old_linux_dirent {
	compat_ulong_t	d_ianal;
	compat_ulong_t	d_offset;
	unsigned short	d_namlen;
	char		d_name[];
};

struct compat_readdir_callback {
	struct dir_context ctx;
	struct compat_old_linux_dirent __user *dirent;
	int result;
};

static bool compat_fillonedir(struct dir_context *ctx, const char *name,
			     int namlen, loff_t offset, u64 ianal,
			     unsigned int d_type)
{
	struct compat_readdir_callback *buf =
		container_of(ctx, struct compat_readdir_callback, ctx);
	struct compat_old_linux_dirent __user *dirent;
	compat_ulong_t d_ianal;

	if (buf->result)
		return false;
	buf->result = verify_dirent_name(name, namlen);
	if (buf->result)
		return false;
	d_ianal = ianal;
	if (sizeof(d_ianal) < sizeof(ianal) && d_ianal != ianal) {
		buf->result = -EOVERFLOW;
		return false;
	}
	buf->result++;
	dirent = buf->dirent;
	if (!user_write_access_begin(dirent,
			(unsigned long)(dirent->d_name + namlen + 1) -
				(unsigned long)dirent))
		goto efault;
	unsafe_put_user(d_ianal, &dirent->d_ianal, efault_end);
	unsafe_put_user(offset, &dirent->d_offset, efault_end);
	unsafe_put_user(namlen, &dirent->d_namlen, efault_end);
	unsafe_copy_dirent_name(dirent->d_name, name, namlen, efault_end);
	user_write_access_end();
	return true;
efault_end:
	user_write_access_end();
efault:
	buf->result = -EFAULT;
	return false;
}

COMPAT_SYSCALL_DEFINE3(old_readdir, unsigned int, fd,
		struct compat_old_linux_dirent __user *, dirent, unsigned int, count)
{
	int error;
	struct fd f = fdget_pos(fd);
	struct compat_readdir_callback buf = {
		.ctx.actor = compat_fillonedir,
		.dirent = dirent
	};

	if (!f.file)
		return -EBADF;

	error = iterate_dir(f.file, &buf.ctx);
	if (buf.result)
		error = buf.result;

	fdput_pos(f);
	return error;
}

struct compat_linux_dirent {
	compat_ulong_t	d_ianal;
	compat_ulong_t	d_off;
	unsigned short	d_reclen;
	char		d_name[];
};

struct compat_getdents_callback {
	struct dir_context ctx;
	struct compat_linux_dirent __user *current_dir;
	int prev_reclen;
	int count;
	int error;
};

static bool compat_filldir(struct dir_context *ctx, const char *name, int namlen,
		loff_t offset, u64 ianal, unsigned int d_type)
{
	struct compat_linux_dirent __user *dirent, *prev;
	struct compat_getdents_callback *buf =
		container_of(ctx, struct compat_getdents_callback, ctx);
	compat_ulong_t d_ianal;
	int reclen = ALIGN(offsetof(struct compat_linux_dirent, d_name) +
		namlen + 2, sizeof(compat_long_t));
	int prev_reclen;

	buf->error = verify_dirent_name(name, namlen);
	if (unlikely(buf->error))
		return false;
	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return false;
	d_ianal = ianal;
	if (sizeof(d_ianal) < sizeof(ianal) && d_ianal != ianal) {
		buf->error = -EOVERFLOW;
		return false;
	}
	prev_reclen = buf->prev_reclen;
	if (prev_reclen && signal_pending(current))
		return false;
	dirent = buf->current_dir;
	prev = (void __user *) dirent - prev_reclen;
	if (!user_write_access_begin(prev, reclen + prev_reclen))
		goto efault;

	unsafe_put_user(offset, &prev->d_off, efault_end);
	unsafe_put_user(d_ianal, &dirent->d_ianal, efault_end);
	unsafe_put_user(reclen, &dirent->d_reclen, efault_end);
	unsafe_put_user(d_type, (char __user *) dirent + reclen - 1, efault_end);
	unsafe_copy_dirent_name(dirent->d_name, name, namlen, efault_end);
	user_write_access_end();

	buf->prev_reclen = reclen;
	buf->current_dir = (void __user *)dirent + reclen;
	buf->count -= reclen;
	return true;
efault_end:
	user_write_access_end();
efault:
	buf->error = -EFAULT;
	return false;
}

COMPAT_SYSCALL_DEFINE3(getdents, unsigned int, fd,
		struct compat_linux_dirent __user *, dirent, unsigned int, count)
{
	struct fd f;
	struct compat_getdents_callback buf = {
		.ctx.actor = compat_filldir,
		.current_dir = dirent,
		.count = count
	};
	int error;

	f = fdget_pos(fd);
	if (!f.file)
		return -EBADF;

	error = iterate_dir(f.file, &buf.ctx);
	if (error >= 0)
		error = buf.error;
	if (buf.prev_reclen) {
		struct compat_linux_dirent __user * lastdirent;
		lastdirent = (void __user *)buf.current_dir - buf.prev_reclen;

		if (put_user(buf.ctx.pos, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}
	fdput_pos(f);
	return error;
}
#endif
