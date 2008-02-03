/*
 *  linux/fs/readdir.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/dirent.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>

#include <asm/uaccess.h>

int vfs_readdir(struct file *file, filldir_t filler, void *buf)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	int res = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;

	res = security_file_permission(file, MAY_READ);
	if (res)
		goto out;

	res = mutex_lock_killable(&inode->i_mutex);
	if (res)
		goto out;

	res = -ENOENT;
	if (!IS_DEADDIR(inode)) {
		res = file->f_op->readdir(file, buf, filler);
		file_accessed(file);
	}
	mutex_unlock(&inode->i_mutex);
out:
	return res;
}

EXPORT_SYMBOL(vfs_readdir);

/*
 * Traditional linux readdir() handling..
 *
 * "count=1" is a special case, meaning that the buffer is one
 * dirent-structure in size and that the code can't handle more
 * anyway. Thus the special "fillonedir()" function for that
 * case (the low-level handlers don't need to care about this).
 */
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char __user *) (de)))

#ifdef __ARCH_WANT_OLD_READDIR

struct old_linux_dirent {
	unsigned long	d_ino;
	unsigned long	d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct readdir_callback {
	struct old_linux_dirent __user * dirent;
	int result;
};

static int fillonedir(void * __buf, const char * name, int namlen, loff_t offset,
		      u64 ino, unsigned int d_type)
{
	struct readdir_callback * buf = (struct readdir_callback *) __buf;
	struct old_linux_dirent __user * dirent;
	unsigned long d_ino;

	if (buf->result)
		return -EINVAL;
	d_ino = ino;
	if (sizeof(d_ino) < sizeof(ino) && d_ino != ino)
		return -EOVERFLOW;
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

asmlinkage long old_readdir(unsigned int fd, struct old_linux_dirent __user * dirent, unsigned int count)
{
	int error;
	struct file * file;
	struct readdir_callback buf;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.result = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, fillonedir, &buf);
	if (error >= 0)
		error = buf.result;

	fput(file);
out:
	return error;
}

#endif /* __ARCH_WANT_OLD_READDIR */

/*
 * New, all-improved, singing, dancing, iBCS2-compliant getdents()
 * interface. 
 */
struct linux_dirent {
	unsigned long	d_ino;
	unsigned long	d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct getdents_callback {
	struct linux_dirent __user * current_dir;
	struct linux_dirent __user * previous;
	int count;
	int error;
};

static int filldir(void * __buf, const char * name, int namlen, loff_t offset,
		   u64 ino, unsigned int d_type)
{
	struct linux_dirent __user * dirent;
	struct getdents_callback * buf = (struct getdents_callback *) __buf;
	unsigned long d_ino;
	int reclen = ALIGN(NAME_OFFSET(dirent) + namlen + 2, sizeof(long));

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	d_ino = ino;
	if (sizeof(d_ino) < sizeof(ino) && d_ino != ino)
		return -EOVERFLOW;
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
	if (__put_user(d_type, (char __user *) dirent + reclen - 1))
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

asmlinkage long sys_getdents(unsigned int fd, struct linux_dirent __user * dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent __user * lastdirent;
	struct getdents_callback buf;
	int error;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, count))
		goto out;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, filldir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		if (put_user(file->f_pos, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

struct getdents_callback64 {
	struct linux_dirent64 __user * current_dir;
	struct linux_dirent64 __user * previous;
	int count;
	int error;
};

static int filldir64(void * __buf, const char * name, int namlen, loff_t offset,
		     u64 ino, unsigned int d_type)
{
	struct linux_dirent64 __user *dirent;
	struct getdents_callback64 * buf = (struct getdents_callback64 *) __buf;
	int reclen = ALIGN(NAME_OFFSET(dirent) + namlen + 1, sizeof(u64));

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent) {
		if (__put_user(offset, &dirent->d_off))
			goto efault;
	}
	dirent = buf->current_dir;
	if (__put_user(ino, &dirent->d_ino))
		goto efault;
	if (__put_user(0, &dirent->d_off))
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

asmlinkage long sys_getdents64(unsigned int fd, struct linux_dirent64 __user * dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent64 __user * lastdirent;
	struct getdents_callback64 buf;
	int error;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, count))
		goto out;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, filldir64, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		typeof(lastdirent->d_off) d_off = file->f_pos;
		error = -EFAULT;
		if (__put_user(d_off, &lastdirent->d_off))
			goto out_putf;
		error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}
