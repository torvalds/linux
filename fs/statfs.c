#include <linux/syscalls.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/namei.h>
#include <linux/statfs.h>
#include <linux/security.h>
#include <linux/uaccess.h>

int vfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	int retval = -ENODEV;

	if (dentry) {
		retval = -ENOSYS;
		if (dentry->d_sb->s_op->statfs) {
			memset(buf, 0, sizeof(*buf));
			retval = security_sb_statfs(dentry);
			if (retval)
				return retval;
			retval = dentry->d_sb->s_op->statfs(dentry, buf);
			if (retval == 0 && buf->f_frsize == 0)
				buf->f_frsize = buf->f_bsize;
		}
	}
	return retval;
}

EXPORT_SYMBOL(vfs_statfs);

static int vfs_statfs_native(struct dentry *dentry, struct statfs *buf)
{
	struct kstatfs st;
	int retval;

	retval = vfs_statfs(dentry, &st);
	if (retval)
		return retval;

	if (sizeof(*buf) == sizeof(st))
		memcpy(buf, &st, sizeof(st));
	else {
		if (sizeof buf->f_blocks == 4) {
			if ((st.f_blocks | st.f_bfree | st.f_bavail |
			     st.f_bsize | st.f_frsize) &
			    0xffffffff00000000ULL)
				return -EOVERFLOW;
			/*
			 * f_files and f_ffree may be -1; it's okay to stuff
			 * that into 32 bits
			 */
			if (st.f_files != -1 &&
			    (st.f_files & 0xffffffff00000000ULL))
				return -EOVERFLOW;
			if (st.f_ffree != -1 &&
			    (st.f_ffree & 0xffffffff00000000ULL))
				return -EOVERFLOW;
		}

		buf->f_type = st.f_type;
		buf->f_bsize = st.f_bsize;
		buf->f_blocks = st.f_blocks;
		buf->f_bfree = st.f_bfree;
		buf->f_bavail = st.f_bavail;
		buf->f_files = st.f_files;
		buf->f_ffree = st.f_ffree;
		buf->f_fsid = st.f_fsid;
		buf->f_namelen = st.f_namelen;
		buf->f_frsize = st.f_frsize;
		memset(buf->f_spare, 0, sizeof(buf->f_spare));
	}
	return 0;
}

static int vfs_statfs64(struct dentry *dentry, struct statfs64 *buf)
{
	struct kstatfs st;
	int retval;

	retval = vfs_statfs(dentry, &st);
	if (retval)
		return retval;

	if (sizeof(*buf) == sizeof(st))
		memcpy(buf, &st, sizeof(st));
	else {
		buf->f_type = st.f_type;
		buf->f_bsize = st.f_bsize;
		buf->f_blocks = st.f_blocks;
		buf->f_bfree = st.f_bfree;
		buf->f_bavail = st.f_bavail;
		buf->f_files = st.f_files;
		buf->f_ffree = st.f_ffree;
		buf->f_fsid = st.f_fsid;
		buf->f_namelen = st.f_namelen;
		buf->f_frsize = st.f_frsize;
		memset(buf->f_spare, 0, sizeof(buf->f_spare));
	}
	return 0;
}

SYSCALL_DEFINE2(statfs, const char __user *, pathname, struct statfs __user *, buf)
{
	struct path path;
	int error;

	error = user_path(pathname, &path);
	if (!error) {
		struct statfs tmp;
		error = vfs_statfs_native(path.dentry, &tmp);
		if (!error && copy_to_user(buf, &tmp, sizeof(tmp)))
			error = -EFAULT;
		path_put(&path);
	}
	return error;
}

SYSCALL_DEFINE3(statfs64, const char __user *, pathname, size_t, sz, struct statfs64 __user *, buf)
{
	struct path path;
	long error;

	if (sz != sizeof(*buf))
		return -EINVAL;
	error = user_path(pathname, &path);
	if (!error) {
		struct statfs64 tmp;
		error = vfs_statfs64(path.dentry, &tmp);
		if (!error && copy_to_user(buf, &tmp, sizeof(tmp)))
			error = -EFAULT;
		path_put(&path);
	}
	return error;
}

SYSCALL_DEFINE2(fstatfs, unsigned int, fd, struct statfs __user *, buf)
{
	struct file *file;
	struct statfs tmp;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = vfs_statfs_native(file->f_path.dentry, &tmp);
	if (!error && copy_to_user(buf, &tmp, sizeof(tmp)))
		error = -EFAULT;
	fput(file);
out:
	return error;
}

SYSCALL_DEFINE3(fstatfs64, unsigned int, fd, size_t, sz, struct statfs64 __user *, buf)
{
	struct file *file;
	struct statfs64 tmp;
	int error;

	if (sz != sizeof(*buf))
		return -EINVAL;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	error = vfs_statfs64(file->f_path.dentry, &tmp);
	if (!error && copy_to_user(buf, &tmp, sizeof(tmp)))
		error = -EFAULT;
	fput(file);
out:
	return error;
}

SYSCALL_DEFINE2(ustat, unsigned, dev, struct ustat __user *, ubuf)
{
	struct super_block *s;
	struct ustat tmp;
	struct kstatfs sbuf;
	int err;

	s = user_get_super(new_decode_dev(dev));
	if (!s)
		return -EINVAL;

	err = vfs_statfs(s->s_root, &sbuf);
	drop_super(s);
	if (err)
		return err;

	memset(&tmp,0,sizeof(struct ustat));
	tmp.f_tfree = sbuf.f_bfree;
	tmp.f_tinode = sbuf.f_ffree;

	return copy_to_user(ubuf, &tmp, sizeof(struct ustat)) ? -EFAULT : 0;
}
