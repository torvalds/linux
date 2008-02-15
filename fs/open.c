/*
 *  linux/fs/open.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/quotaops.h>
#include <linux/fsnotify.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/vfs.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/rcupdate.h>
#include <linux/audit.h>
#include <linux/falloc.h>

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
			if ((st.f_blocks | st.f_bfree | st.f_bavail) &
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

asmlinkage long sys_statfs(const char __user * path, struct statfs __user * buf)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(path, &nd);
	if (!error) {
		struct statfs tmp;
		error = vfs_statfs_native(nd.path.dentry, &tmp);
		if (!error && copy_to_user(buf, &tmp, sizeof(tmp)))
			error = -EFAULT;
		path_put(&nd.path);
	}
	return error;
}


asmlinkage long sys_statfs64(const char __user *path, size_t sz, struct statfs64 __user *buf)
{
	struct nameidata nd;
	long error;

	if (sz != sizeof(*buf))
		return -EINVAL;
	error = user_path_walk(path, &nd);
	if (!error) {
		struct statfs64 tmp;
		error = vfs_statfs64(nd.path.dentry, &tmp);
		if (!error && copy_to_user(buf, &tmp, sizeof(tmp)))
			error = -EFAULT;
		path_put(&nd.path);
	}
	return error;
}


asmlinkage long sys_fstatfs(unsigned int fd, struct statfs __user * buf)
{
	struct file * file;
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

asmlinkage long sys_fstatfs64(unsigned int fd, size_t sz, struct statfs64 __user *buf)
{
	struct file * file;
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

int do_truncate(struct dentry *dentry, loff_t length, unsigned int time_attrs,
	struct file *filp)
{
	int err;
	struct iattr newattrs;

	/* Not pretty: "inode->i_size" shouldn't really be signed. But it is. */
	if (length < 0)
		return -EINVAL;

	newattrs.ia_size = length;
	newattrs.ia_valid = ATTR_SIZE | time_attrs;
	if (filp) {
		newattrs.ia_file = filp;
		newattrs.ia_valid |= ATTR_FILE;
	}

	/* Remove suid/sgid on truncate too */
	newattrs.ia_valid |= should_remove_suid(dentry);

	mutex_lock(&dentry->d_inode->i_mutex);
	err = notify_change(dentry, &newattrs);
	mutex_unlock(&dentry->d_inode->i_mutex);
	return err;
}

static long do_sys_truncate(const char __user * path, loff_t length)
{
	struct nameidata nd;
	struct inode * inode;
	int error;

	error = -EINVAL;
	if (length < 0)	/* sorry, but loff_t says... */
		goto out;

	error = user_path_walk(path, &nd);
	if (error)
		goto out;
	inode = nd.path.dentry->d_inode;

	/* For directories it's -EISDIR, for other non-regulars - -EINVAL */
	error = -EISDIR;
	if (S_ISDIR(inode->i_mode))
		goto dput_and_out;

	error = -EINVAL;
	if (!S_ISREG(inode->i_mode))
		goto dput_and_out;

	error = vfs_permission(&nd, MAY_WRITE);
	if (error)
		goto dput_and_out;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto dput_and_out;

	error = get_write_access(inode);
	if (error)
		goto dput_and_out;

	/*
	 * Make sure that there are no leases.  get_write_access() protects
	 * against the truncate racing with a lease-granting setlease().
	 */
	error = break_lease(inode, FMODE_WRITE);
	if (error)
		goto put_write_and_out;

	error = locks_verify_truncate(inode, NULL, length);
	if (!error) {
		DQUOT_INIT(inode);
		error = do_truncate(nd.path.dentry, length, 0, NULL);
	}

put_write_and_out:
	put_write_access(inode);
dput_and_out:
	path_put(&nd.path);
out:
	return error;
}

asmlinkage long sys_truncate(const char __user * path, unsigned long length)
{
	/* on 32-bit boxen it will cut the range 2^31--2^32-1 off */
	return do_sys_truncate(path, (long)length);
}

static long do_sys_ftruncate(unsigned int fd, loff_t length, int small)
{
	struct inode * inode;
	struct dentry *dentry;
	struct file * file;
	int error;

	error = -EINVAL;
	if (length < 0)
		goto out;
	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	/* explicitly opened as large or we are on 64-bit box */
	if (file->f_flags & O_LARGEFILE)
		small = 0;

	dentry = file->f_path.dentry;
	inode = dentry->d_inode;
	error = -EINVAL;
	if (!S_ISREG(inode->i_mode) || !(file->f_mode & FMODE_WRITE))
		goto out_putf;

	error = -EINVAL;
	/* Cannot ftruncate over 2^31 bytes without large file support */
	if (small && length > MAX_NON_LFS)
		goto out_putf;

	error = -EPERM;
	if (IS_APPEND(inode))
		goto out_putf;

	error = locks_verify_truncate(inode, file, length);
	if (!error)
		error = do_truncate(dentry, length, ATTR_MTIME|ATTR_CTIME, file);
out_putf:
	fput(file);
out:
	return error;
}

asmlinkage long sys_ftruncate(unsigned int fd, unsigned long length)
{
	long ret = do_sys_ftruncate(fd, length, 1);
	/* avoid REGPARM breakage on x86: */
	prevent_tail_call(ret);
	return ret;
}

/* LFS versions of truncate are only needed on 32 bit machines */
#if BITS_PER_LONG == 32
asmlinkage long sys_truncate64(const char __user * path, loff_t length)
{
	return do_sys_truncate(path, length);
}

asmlinkage long sys_ftruncate64(unsigned int fd, loff_t length)
{
	long ret = do_sys_ftruncate(fd, length, 0);
	/* avoid REGPARM breakage on x86: */
	prevent_tail_call(ret);
	return ret;
}
#endif

asmlinkage long sys_fallocate(int fd, int mode, loff_t offset, loff_t len)
{
	struct file *file;
	struct inode *inode;
	long ret = -EINVAL;

	if (offset < 0 || len <= 0)
		goto out;

	/* Return error if mode is not supported */
	ret = -EOPNOTSUPP;
	if (mode && !(mode & FALLOC_FL_KEEP_SIZE))
		goto out;

	ret = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;
	if (!(file->f_mode & FMODE_WRITE))
		goto out_fput;
	/*
	 * Revalidate the write permissions, in case security policy has
	 * changed since the files were opened.
	 */
	ret = security_file_permission(file, MAY_WRITE);
	if (ret)
		goto out_fput;

	inode = file->f_path.dentry->d_inode;

	ret = -ESPIPE;
	if (S_ISFIFO(inode->i_mode))
		goto out_fput;

	ret = -ENODEV;
	/*
	 * Let individual file system decide if it supports preallocation
	 * for directories or not.
	 */
	if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode))
		goto out_fput;

	ret = -EFBIG;
	/* Check for wrap through zero too */
	if (((offset + len) > inode->i_sb->s_maxbytes) || ((offset + len) < 0))
		goto out_fput;

	if (inode->i_op && inode->i_op->fallocate)
		ret = inode->i_op->fallocate(inode, mode, offset, len);
	else
		ret = -EOPNOTSUPP;

out_fput:
	fput(file);
out:
	return ret;
}

/*
 * access() needs to use the real uid/gid, not the effective uid/gid.
 * We do this by temporarily clearing all FS-related capabilities and
 * switching the fsuid/fsgid around to the real ones.
 */
asmlinkage long sys_faccessat(int dfd, const char __user *filename, int mode)
{
	struct nameidata nd;
	int old_fsuid, old_fsgid;
	kernel_cap_t old_cap;
	int res;

	if (mode & ~S_IRWXO)	/* where's F_OK, X_OK, W_OK, R_OK? */
		return -EINVAL;

	old_fsuid = current->fsuid;
	old_fsgid = current->fsgid;
	old_cap = current->cap_effective;

	current->fsuid = current->uid;
	current->fsgid = current->gid;

	/*
	 * Clear the capabilities if we switch to a non-root user
	 *
	 * FIXME: There is a race here against sys_capset.  The
	 * capabilities can change yet we will restore the old
	 * value below.  We should hold task_capabilities_lock,
	 * but we cannot because user_path_walk can sleep.
	 */
	if (current->uid)
		cap_clear(current->cap_effective);
	else
		current->cap_effective = current->cap_permitted;

	res = __user_walk_fd(dfd, filename, LOOKUP_FOLLOW|LOOKUP_ACCESS, &nd);
	if (res)
		goto out;

	res = vfs_permission(&nd, mode);
	/* SuS v2 requires we report a read only fs too */
	if(res || !(mode & S_IWOTH) ||
	   special_file(nd.path.dentry->d_inode->i_mode))
		goto out_path_release;

	if(IS_RDONLY(nd.path.dentry->d_inode))
		res = -EROFS;

out_path_release:
	path_put(&nd.path);
out:
	current->fsuid = old_fsuid;
	current->fsgid = old_fsgid;
	current->cap_effective = old_cap;

	return res;
}

asmlinkage long sys_access(const char __user *filename, int mode)
{
	return sys_faccessat(AT_FDCWD, filename, mode);
}

asmlinkage long sys_chdir(const char __user * filename)
{
	struct nameidata nd;
	int error;

	error = __user_walk(filename,
			    LOOKUP_FOLLOW|LOOKUP_DIRECTORY|LOOKUP_CHDIR, &nd);
	if (error)
		goto out;

	error = vfs_permission(&nd, MAY_EXEC);
	if (error)
		goto dput_and_out;

	set_fs_pwd(current->fs, &nd.path);

dput_and_out:
	path_put(&nd.path);
out:
	return error;
}

asmlinkage long sys_fchdir(unsigned int fd)
{
	struct file *file;
	struct inode *inode;
	int error;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	inode = file->f_path.dentry->d_inode;

	error = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto out_putf;

	error = file_permission(file, MAY_EXEC);
	if (!error)
		set_fs_pwd(current->fs, &file->f_path);
out_putf:
	fput(file);
out:
	return error;
}

asmlinkage long sys_chroot(const char __user * filename)
{
	struct nameidata nd;
	int error;

	error = __user_walk(filename, LOOKUP_FOLLOW | LOOKUP_DIRECTORY | LOOKUP_NOALT, &nd);
	if (error)
		goto out;

	error = vfs_permission(&nd, MAY_EXEC);
	if (error)
		goto dput_and_out;

	error = -EPERM;
	if (!capable(CAP_SYS_CHROOT))
		goto dput_and_out;

	set_fs_root(current->fs, &nd.path);
	set_fs_altroot();
	error = 0;
dput_and_out:
	path_put(&nd.path);
out:
	return error;
}

asmlinkage long sys_fchmod(unsigned int fd, mode_t mode)
{
	struct inode * inode;
	struct dentry * dentry;
	struct file * file;
	int err = -EBADF;
	struct iattr newattrs;

	file = fget(fd);
	if (!file)
		goto out;

	dentry = file->f_path.dentry;
	inode = dentry->d_inode;

	audit_inode(NULL, dentry);

	err = -EROFS;
	if (IS_RDONLY(inode))
		goto out_putf;
	err = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto out_putf;
	mutex_lock(&inode->i_mutex);
	if (mode == (mode_t) -1)
		mode = inode->i_mode;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	err = notify_change(dentry, &newattrs);
	mutex_unlock(&inode->i_mutex);

out_putf:
	fput(file);
out:
	return err;
}

asmlinkage long sys_fchmodat(int dfd, const char __user *filename,
			     mode_t mode)
{
	struct nameidata nd;
	struct inode * inode;
	int error;
	struct iattr newattrs;

	error = __user_walk_fd(dfd, filename, LOOKUP_FOLLOW, &nd);
	if (error)
		goto out;
	inode = nd.path.dentry->d_inode;

	error = -EROFS;
	if (IS_RDONLY(inode))
		goto dput_and_out;

	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto dput_and_out;

	mutex_lock(&inode->i_mutex);
	if (mode == (mode_t) -1)
		mode = inode->i_mode;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	error = notify_change(nd.path.dentry, &newattrs);
	mutex_unlock(&inode->i_mutex);

dput_and_out:
	path_put(&nd.path);
out:
	return error;
}

asmlinkage long sys_chmod(const char __user *filename, mode_t mode)
{
	return sys_fchmodat(AT_FDCWD, filename, mode);
}

static int chown_common(struct dentry * dentry, uid_t user, gid_t group)
{
	struct inode * inode;
	int error;
	struct iattr newattrs;

	error = -ENOENT;
	if (!(inode = dentry->d_inode)) {
		printk(KERN_ERR "chown_common: NULL inode\n");
		goto out;
	}
	error = -EROFS;
	if (IS_RDONLY(inode))
		goto out;
	error = -EPERM;
	if (IS_IMMUTABLE(inode) || IS_APPEND(inode))
		goto out;
	newattrs.ia_valid =  ATTR_CTIME;
	if (user != (uid_t) -1) {
		newattrs.ia_valid |= ATTR_UID;
		newattrs.ia_uid = user;
	}
	if (group != (gid_t) -1) {
		newattrs.ia_valid |= ATTR_GID;
		newattrs.ia_gid = group;
	}
	if (!S_ISDIR(inode->i_mode))
		newattrs.ia_valid |=
			ATTR_KILL_SUID | ATTR_KILL_SGID | ATTR_KILL_PRIV;
	mutex_lock(&inode->i_mutex);
	error = notify_change(dentry, &newattrs);
	mutex_unlock(&inode->i_mutex);
out:
	return error;
}

asmlinkage long sys_chown(const char __user * filename, uid_t user, gid_t group)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(filename, &nd);
	if (error)
		goto out;
	error = chown_common(nd.path.dentry, user, group);
	path_put(&nd.path);
out:
	return error;
}

asmlinkage long sys_fchownat(int dfd, const char __user *filename, uid_t user,
			     gid_t group, int flag)
{
	struct nameidata nd;
	int error = -EINVAL;
	int follow;

	if ((flag & ~AT_SYMLINK_NOFOLLOW) != 0)
		goto out;

	follow = (flag & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	error = __user_walk_fd(dfd, filename, follow, &nd);
	if (error)
		goto out;
	error = chown_common(nd.path.dentry, user, group);
	path_put(&nd.path);
out:
	return error;
}

asmlinkage long sys_lchown(const char __user * filename, uid_t user, gid_t group)
{
	struct nameidata nd;
	int error;

	error = user_path_walk_link(filename, &nd);
	if (error)
		goto out;
	error = chown_common(nd.path.dentry, user, group);
	path_put(&nd.path);
out:
	return error;
}


asmlinkage long sys_fchown(unsigned int fd, uid_t user, gid_t group)
{
	struct file * file;
	int error = -EBADF;
	struct dentry * dentry;

	file = fget(fd);
	if (!file)
		goto out;

	dentry = file->f_path.dentry;
	audit_inode(NULL, dentry);
	error = chown_common(dentry, user, group);
	fput(file);
out:
	return error;
}

static struct file *__dentry_open(struct dentry *dentry, struct vfsmount *mnt,
					int flags, struct file *f,
					int (*open)(struct inode *, struct file *))
{
	struct inode *inode;
	int error;

	f->f_flags = flags;
	f->f_mode = ((flags+1) & O_ACCMODE) | FMODE_LSEEK |
				FMODE_PREAD | FMODE_PWRITE;
	inode = dentry->d_inode;
	if (f->f_mode & FMODE_WRITE) {
		error = get_write_access(inode);
		if (error)
			goto cleanup_file;
	}

	f->f_mapping = inode->i_mapping;
	f->f_path.dentry = dentry;
	f->f_path.mnt = mnt;
	f->f_pos = 0;
	f->f_op = fops_get(inode->i_fop);
	file_move(f, &inode->i_sb->s_files);

	error = security_dentry_open(f);
	if (error)
		goto cleanup_all;

	if (!open && f->f_op)
		open = f->f_op->open;
	if (open) {
		error = open(inode, f);
		if (error)
			goto cleanup_all;
	}

	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	file_ra_state_init(&f->f_ra, f->f_mapping->host->i_mapping);

	/* NB: we're sure to have correct a_ops only after f_op->open */
	if (f->f_flags & O_DIRECT) {
		if (!f->f_mapping->a_ops ||
		    ((!f->f_mapping->a_ops->direct_IO) &&
		    (!f->f_mapping->a_ops->get_xip_page))) {
			fput(f);
			f = ERR_PTR(-EINVAL);
		}
	}

	return f;

cleanup_all:
	fops_put(f->f_op);
	if (f->f_mode & FMODE_WRITE)
		put_write_access(inode);
	file_kill(f);
	f->f_path.dentry = NULL;
	f->f_path.mnt = NULL;
cleanup_file:
	put_filp(f);
	dput(dentry);
	mntput(mnt);
	return ERR_PTR(error);
}

/*
 * Note that while the flag value (low two bits) for sys_open means:
 *	00 - read-only
 *	01 - write-only
 *	10 - read-write
 *	11 - special
 * it is changed into
 *	00 - no permissions needed
 *	01 - read-permission
 *	10 - write-permission
 *	11 - read-write
 * for the internal routines (ie open_namei()/follow_link() etc). 00 is
 * used by symlinks.
 */
static struct file *do_filp_open(int dfd, const char *filename, int flags,
				 int mode)
{
	int namei_flags, error;
	struct nameidata nd;

	namei_flags = flags;
	if ((namei_flags+1) & O_ACCMODE)
		namei_flags++;

	error = open_namei(dfd, filename, namei_flags, mode, &nd);
	if (!error)
		return nameidata_to_filp(&nd, flags);

	return ERR_PTR(error);
}

struct file *filp_open(const char *filename, int flags, int mode)
{
	return do_filp_open(AT_FDCWD, filename, flags, mode);
}
EXPORT_SYMBOL(filp_open);

/**
 * lookup_instantiate_filp - instantiates the open intent filp
 * @nd: pointer to nameidata
 * @dentry: pointer to dentry
 * @open: open callback
 *
 * Helper for filesystems that want to use lookup open intents and pass back
 * a fully instantiated struct file to the caller.
 * This function is meant to be called from within a filesystem's
 * lookup method.
 * Beware of calling it for non-regular files! Those ->open methods might block
 * (e.g. in fifo_open), leaving you with parent locked (and in case of fifo,
 * leading to a deadlock, as nobody can open that fifo anymore, because
 * another process to open fifo will block on locked parent when doing lookup).
 * Note that in case of error, nd->intent.open.file is destroyed, but the
 * path information remains valid.
 * If the open callback is set to NULL, then the standard f_op->open()
 * filesystem callback is substituted.
 */
struct file *lookup_instantiate_filp(struct nameidata *nd, struct dentry *dentry,
		int (*open)(struct inode *, struct file *))
{
	if (IS_ERR(nd->intent.open.file))
		goto out;
	if (IS_ERR(dentry))
		goto out_err;
	nd->intent.open.file = __dentry_open(dget(dentry), mntget(nd->path.mnt),
					     nd->intent.open.flags - 1,
					     nd->intent.open.file,
					     open);
out:
	return nd->intent.open.file;
out_err:
	release_open_intent(nd);
	nd->intent.open.file = (struct file *)dentry;
	goto out;
}
EXPORT_SYMBOL_GPL(lookup_instantiate_filp);

/**
 * nameidata_to_filp - convert a nameidata to an open filp.
 * @nd: pointer to nameidata
 * @flags: open flags
 *
 * Note that this function destroys the original nameidata
 */
struct file *nameidata_to_filp(struct nameidata *nd, int flags)
{
	struct file *filp;

	/* Pick up the filp from the open intent */
	filp = nd->intent.open.file;
	/* Has the filesystem initialised the file for us? */
	if (filp->f_path.dentry == NULL)
		filp = __dentry_open(nd->path.dentry, nd->path.mnt, flags, filp,
				     NULL);
	else
		path_put(&nd->path);
	return filp;
}

/*
 * dentry_open() will have done dput(dentry) and mntput(mnt) if it returns an
 * error.
 */
struct file *dentry_open(struct dentry *dentry, struct vfsmount *mnt, int flags)
{
	int error;
	struct file *f;

	error = -ENFILE;
	f = get_empty_filp();
	if (f == NULL) {
		dput(dentry);
		mntput(mnt);
		return ERR_PTR(error);
	}

	return __dentry_open(dentry, mnt, flags, f, NULL);
}
EXPORT_SYMBOL(dentry_open);

/*
 * Find an empty file descriptor entry, and mark it busy.
 */
int get_unused_fd_flags(int flags)
{
	struct files_struct * files = current->files;
	int fd, error;
	struct fdtable *fdt;

  	error = -EMFILE;
	spin_lock(&files->file_lock);

repeat:
	fdt = files_fdtable(files);
	fd = find_next_zero_bit(fdt->open_fds->fds_bits, fdt->max_fds,
				files->next_fd);

	/*
	 * N.B. For clone tasks sharing a files structure, this test
	 * will limit the total number of files that can be opened.
	 */
	if (fd >= current->signal->rlim[RLIMIT_NOFILE].rlim_cur)
		goto out;

	/* Do we need to expand the fd array or fd set?  */
	error = expand_files(files, fd);
	if (error < 0)
		goto out;

	if (error) {
		/*
	 	 * If we needed to expand the fs array we
		 * might have blocked - try again.
		 */
		error = -EMFILE;
		goto repeat;
	}

	FD_SET(fd, fdt->open_fds);
	if (flags & O_CLOEXEC)
		FD_SET(fd, fdt->close_on_exec);
	else
		FD_CLR(fd, fdt->close_on_exec);
	files->next_fd = fd + 1;
#if 1
	/* Sanity check */
	if (fdt->fd[fd] != NULL) {
		printk(KERN_WARNING "get_unused_fd: slot %d not NULL!\n", fd);
		fdt->fd[fd] = NULL;
	}
#endif
	error = fd;

out:
	spin_unlock(&files->file_lock);
	return error;
}

int get_unused_fd(void)
{
	return get_unused_fd_flags(0);
}

EXPORT_SYMBOL(get_unused_fd);

static void __put_unused_fd(struct files_struct *files, unsigned int fd)
{
	struct fdtable *fdt = files_fdtable(files);
	__FD_CLR(fd, fdt->open_fds);
	if (fd < files->next_fd)
		files->next_fd = fd;
}

void put_unused_fd(unsigned int fd)
{
	struct files_struct *files = current->files;
	spin_lock(&files->file_lock);
	__put_unused_fd(files, fd);
	spin_unlock(&files->file_lock);
}

EXPORT_SYMBOL(put_unused_fd);

/*
 * Install a file pointer in the fd array.
 *
 * The VFS is full of places where we drop the files lock between
 * setting the open_fds bitmap and installing the file in the file
 * array.  At any such point, we are vulnerable to a dup2() race
 * installing a file in the array before us.  We need to detect this and
 * fput() the struct file we are about to overwrite in this case.
 *
 * It should never happen - if we allow dup2() do it, _really_ bad things
 * will follow.
 */

void fd_install(unsigned int fd, struct file *file)
{
	struct files_struct *files = current->files;
	struct fdtable *fdt;
	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	BUG_ON(fdt->fd[fd] != NULL);
	rcu_assign_pointer(fdt->fd[fd], file);
	spin_unlock(&files->file_lock);
}

EXPORT_SYMBOL(fd_install);

long do_sys_open(int dfd, const char __user *filename, int flags, int mode)
{
	char *tmp = getname(filename);
	int fd = PTR_ERR(tmp);

	if (!IS_ERR(tmp)) {
		fd = get_unused_fd_flags(flags);
		if (fd >= 0) {
			struct file *f = do_filp_open(dfd, tmp, flags, mode);
			if (IS_ERR(f)) {
				put_unused_fd(fd);
				fd = PTR_ERR(f);
			} else {
				fsnotify_open(f->f_path.dentry);
				fd_install(fd, f);
			}
		}
		putname(tmp);
	}
	return fd;
}

asmlinkage long sys_open(const char __user *filename, int flags, int mode)
{
	long ret;

	if (force_o_largefile())
		flags |= O_LARGEFILE;

	ret = do_sys_open(AT_FDCWD, filename, flags, mode);
	/* avoid REGPARM breakage on x86: */
	prevent_tail_call(ret);
	return ret;
}

asmlinkage long sys_openat(int dfd, const char __user *filename, int flags,
			   int mode)
{
	long ret;

	if (force_o_largefile())
		flags |= O_LARGEFILE;

	ret = do_sys_open(dfd, filename, flags, mode);
	/* avoid REGPARM breakage on x86: */
	prevent_tail_call(ret);
	return ret;
}

#ifndef __alpha__

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage long sys_creat(const char __user * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

#endif

/*
 * "id" is the POSIX thread ID. We use the
 * files pointer for this..
 */
int filp_close(struct file *filp, fl_owner_t id)
{
	int retval = 0;

	if (!file_count(filp)) {
		printk(KERN_ERR "VFS: Close: file count is 0\n");
		return 0;
	}

	if (filp->f_op && filp->f_op->flush)
		retval = filp->f_op->flush(filp, id);

	dnotify_flush(filp, id);
	locks_remove_posix(filp, id);
	fput(filp);
	return retval;
}

EXPORT_SYMBOL(filp_close);

/*
 * Careful here! We test whether the file pointer is NULL before
 * releasing the fd. This ensures that one clone task can't release
 * an fd while another clone is opening it.
 */
asmlinkage long sys_close(unsigned int fd)
{
	struct file * filp;
	struct files_struct *files = current->files;
	struct fdtable *fdt;
	int retval;

	spin_lock(&files->file_lock);
	fdt = files_fdtable(files);
	if (fd >= fdt->max_fds)
		goto out_unlock;
	filp = fdt->fd[fd];
	if (!filp)
		goto out_unlock;
	rcu_assign_pointer(fdt->fd[fd], NULL);
	FD_CLR(fd, fdt->close_on_exec);
	__put_unused_fd(files, fd);
	spin_unlock(&files->file_lock);
	retval = filp_close(filp, files);

	/* can't restart close syscall because file table entry was cleared */
	if (unlikely(retval == -ERESTARTSYS ||
		     retval == -ERESTARTNOINTR ||
		     retval == -ERESTARTNOHAND ||
		     retval == -ERESTART_RESTARTBLOCK))
		retval = -EINTR;

	return retval;

out_unlock:
	spin_unlock(&files->file_lock);
	return -EBADF;
}

EXPORT_SYMBOL(sys_close);

/*
 * This routine simulates a hangup on the tty, to arrange that users
 * are given clean terminals at login time.
 */
asmlinkage long sys_vhangup(void)
{
	if (capable(CAP_SYS_TTY_CONFIG)) {
		/* XXX: this needs locking */
		tty_vhangup(current->signal->tty);
		return 0;
	}
	return -EPERM;
}

/*
 * Called when an inode is about to be open.
 * We use this to disallow opening large files on 32bit systems if
 * the caller didn't specify O_LARGEFILE.  On 64bit systems we force
 * on this flag in sys_open.
 */
int generic_file_open(struct inode * inode, struct file * filp)
{
	if (!(filp->f_flags & O_LARGEFILE) && i_size_read(inode) > MAX_NON_LFS)
		return -EOVERFLOW;
	return 0;
}

EXPORT_SYMBOL(generic_file_open);

/*
 * This is used by subsystems that don't want seekable
 * file descriptors
 */
int nonseekable_open(struct inode *inode, struct file *filp)
{
	filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	return 0;
}

EXPORT_SYMBOL(nonseekable_open);
