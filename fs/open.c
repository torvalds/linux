/*
 *  linux/fs/open.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/string.h>
#include <linux/mm.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fsnotify.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include <linux/capability.h>
#include <linux/securebits.h>
#include <linux/security.h>
#include <linux/mount.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/personality.h>
#include <linux/pagemap.h>
#include <linux/syscalls.h>
#include <linux/rcupdate.h>
#include <linux/audit.h>
#include <linux/falloc.h>
#include <linux/fs_struct.h>
#include <linux/ima.h>
#include <linux/dnotify.h>

#include "internal.h"

int do_truncate(struct dentry *dentry, loff_t length, unsigned int time_attrs,
	struct file *filp)
{
	int ret;
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
	ret = should_remove_suid(dentry);
	if (ret)
		newattrs.ia_valid |= ret | ATTR_FORCE;

	mutex_lock(&dentry->d_inode->i_mutex);
	ret = notify_change(dentry, &newattrs);
	mutex_unlock(&dentry->d_inode->i_mutex);
	return ret;
}

static long do_sys_truncate(const char __user *pathname, loff_t length)
{
	struct path path;
	struct inode *inode;
	int error;

	error = -EINVAL;
	if (length < 0)	/* sorry, but loff_t says... */
		goto out;

	error = user_path(pathname, &path);
	if (error)
		goto out;
	inode = path.dentry->d_inode;

	/* For directories it's -EISDIR, for other non-regulars - -EINVAL */
	error = -EISDIR;
	if (S_ISDIR(inode->i_mode))
		goto dput_and_out;

	error = -EINVAL;
	if (!S_ISREG(inode->i_mode))
		goto dput_and_out;

	error = mnt_want_write(path.mnt);
	if (error)
		goto dput_and_out;

	error = inode_permission(inode, MAY_WRITE);
	if (error)
		goto mnt_drop_write_and_out;

	error = -EPERM;
	if (IS_APPEND(inode))
		goto mnt_drop_write_and_out;

	error = get_write_access(inode);
	if (error)
		goto mnt_drop_write_and_out;

	/*
	 * Make sure that there are no leases.  get_write_access() protects
	 * against the truncate racing with a lease-granting setlease().
	 */
	error = break_lease(inode, O_WRONLY);
	if (error)
		goto put_write_and_out;

	error = locks_verify_truncate(inode, NULL, length);
	if (!error)
		error = security_path_truncate(&path);
	if (!error)
		error = do_truncate(path.dentry, length, 0, NULL);

put_write_and_out:
	put_write_access(inode);
mnt_drop_write_and_out:
	mnt_drop_write(path.mnt);
dput_and_out:
	path_put(&path);
out:
	return error;
}

SYSCALL_DEFINE2(truncate, const char __user *, path, long, length)
{
	return do_sys_truncate(path, length);
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
		error = security_path_truncate(&file->f_path);
	if (!error)
		error = do_truncate(dentry, length, ATTR_MTIME|ATTR_CTIME, file);
out_putf:
	fput(file);
out:
	return error;
}

SYSCALL_DEFINE2(ftruncate, unsigned int, fd, unsigned long, length)
{
	long ret = do_sys_ftruncate(fd, length, 1);
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(2, ret, fd, length);
	return ret;
}

/* LFS versions of truncate are only needed on 32 bit machines */
#if BITS_PER_LONG == 32
SYSCALL_DEFINE(truncate64)(const char __user * path, loff_t length)
{
	return do_sys_truncate(path, length);
}
#ifdef CONFIG_HAVE_SYSCALL_WRAPPERS
asmlinkage long SyS_truncate64(long path, loff_t length)
{
	return SYSC_truncate64((const char __user *) path, length);
}
SYSCALL_ALIAS(sys_truncate64, SyS_truncate64);
#endif

SYSCALL_DEFINE(ftruncate64)(unsigned int fd, loff_t length)
{
	long ret = do_sys_ftruncate(fd, length, 0);
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(2, ret, fd, length);
	return ret;
}
#ifdef CONFIG_HAVE_SYSCALL_WRAPPERS
asmlinkage long SyS_ftruncate64(long fd, loff_t length)
{
	return SYSC_ftruncate64((unsigned int) fd, length);
}
SYSCALL_ALIAS(sys_ftruncate64, SyS_ftruncate64);
#endif
#endif /* BITS_PER_LONG == 32 */


int do_fallocate(struct file *file, int mode, loff_t offset, loff_t len)
{
	struct inode *inode = file->f_path.dentry->d_inode;
	long ret;

	if (offset < 0 || len <= 0)
		return -EINVAL;

	/* Return error if mode is not supported */
	if (mode & ~(FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE))
		return -EOPNOTSUPP;

	/* Punch hole must have keep size set */
	if ((mode & FALLOC_FL_PUNCH_HOLE) &&
	    !(mode & FALLOC_FL_KEEP_SIZE))
		return -EOPNOTSUPP;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;

	/* It's not possible punch hole on append only file */
	if (mode & FALLOC_FL_PUNCH_HOLE && IS_APPEND(inode))
		return -EPERM;

	if (IS_IMMUTABLE(inode))
		return -EPERM;

	/*
	 * Revalidate the write permissions, in case security policy has
	 * changed since the files were opened.
	 */
	ret = security_file_permission(file, MAY_WRITE);
	if (ret)
		return ret;

	if (S_ISFIFO(inode->i_mode))
		return -ESPIPE;

	/*
	 * Let individual file system decide if it supports preallocation
	 * for directories or not.
	 */
	if (!S_ISREG(inode->i_mode) && !S_ISDIR(inode->i_mode))
		return -ENODEV;

	/* Check for wrap through zero too */
	if (((offset + len) > inode->i_sb->s_maxbytes) || ((offset + len) < 0))
		return -EFBIG;

	if (!file->f_op->fallocate)
		return -EOPNOTSUPP;

	return file->f_op->fallocate(file, mode, offset, len);
}

SYSCALL_DEFINE(fallocate)(int fd, int mode, loff_t offset, loff_t len)
{
	struct file *file;
	int error = -EBADF;

	file = fget(fd);
	if (file) {
		error = do_fallocate(file, mode, offset, len);
		fput(file);
	}

	return error;
}

#ifdef CONFIG_HAVE_SYSCALL_WRAPPERS
asmlinkage long SyS_fallocate(long fd, long mode, loff_t offset, loff_t len)
{
	return SYSC_fallocate((int)fd, (int)mode, offset, len);
}
SYSCALL_ALIAS(sys_fallocate, SyS_fallocate);
#endif

/*
 * access() needs to use the real uid/gid, not the effective uid/gid.
 * We do this by temporarily clearing all FS-related capabilities and
 * switching the fsuid/fsgid around to the real ones.
 */
SYSCALL_DEFINE3(faccessat, int, dfd, const char __user *, filename, int, mode)
{
	const struct cred *old_cred;
	struct cred *override_cred;
	struct path path;
	struct inode *inode;
	int res;

	if (mode & ~S_IRWXO)	/* where's F_OK, X_OK, W_OK, R_OK? */
		return -EINVAL;

	override_cred = prepare_creds();
	if (!override_cred)
		return -ENOMEM;

	override_cred->fsuid = override_cred->uid;
	override_cred->fsgid = override_cred->gid;

	if (!issecure(SECURE_NO_SETUID_FIXUP)) {
		/* Clear the capabilities if we switch to a non-root user */
		if (override_cred->uid)
			cap_clear(override_cred->cap_effective);
		else
			override_cred->cap_effective =
				override_cred->cap_permitted;
	}

	old_cred = override_creds(override_cred);

	res = user_path_at(dfd, filename, LOOKUP_FOLLOW, &path);
	if (res)
		goto out;

	inode = path.dentry->d_inode;

	if ((mode & MAY_EXEC) && S_ISREG(inode->i_mode)) {
		/*
		 * MAY_EXEC on regular files is denied if the fs is mounted
		 * with the "noexec" flag.
		 */
		res = -EACCES;
		if (path.mnt->mnt_flags & MNT_NOEXEC)
			goto out_path_release;
	}

	res = inode_permission(inode, mode | MAY_ACCESS);
	/* SuS v2 requires we report a read only fs too */
	if (res || !(mode & S_IWOTH) || special_file(inode->i_mode))
		goto out_path_release;
	/*
	 * This is a rare case where using __mnt_is_readonly()
	 * is OK without a mnt_want/drop_write() pair.  Since
	 * no actual write to the fs is performed here, we do
	 * not need to telegraph to that to anyone.
	 *
	 * By doing this, we accept that this access is
	 * inherently racy and know that the fs may change
	 * state before we even see this result.
	 */
	if (__mnt_is_readonly(path.mnt))
		res = -EROFS;

out_path_release:
	path_put(&path);
out:
	revert_creds(old_cred);
	put_cred(override_cred);
	return res;
}

SYSCALL_DEFINE2(access, const char __user *, filename, int, mode)
{
	return sys_faccessat(AT_FDCWD, filename, mode);
}

SYSCALL_DEFINE1(chdir, const char __user *, filename)
{
	struct path path;
	int error;

	error = user_path_dir(filename, &path);
	if (error)
		goto out;

	error = inode_permission(path.dentry->d_inode, MAY_EXEC | MAY_CHDIR);
	if (error)
		goto dput_and_out;

	set_fs_pwd(current->fs, &path);

dput_and_out:
	path_put(&path);
out:
	return error;
}

SYSCALL_DEFINE1(fchdir, unsigned int, fd)
{
	struct file *file;
	struct inode *inode;
	int error, fput_needed;

	error = -EBADF;
	file = fget_raw_light(fd, &fput_needed);
	if (!file)
		goto out;

	inode = file->f_path.dentry->d_inode;

	error = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto out_putf;

	error = inode_permission(inode, MAY_EXEC | MAY_CHDIR);
	if (!error)
		set_fs_pwd(current->fs, &file->f_path);
out_putf:
	fput_light(file, fput_needed);
out:
	return error;
}

SYSCALL_DEFINE1(chroot, const char __user *, filename)
{
	struct path path;
	int error;

	error = user_path_dir(filename, &path);
	if (error)
		goto out;

	error = inode_permission(path.dentry->d_inode, MAY_EXEC | MAY_CHDIR);
	if (error)
		goto dput_and_out;

	error = -EPERM;
	if (!capable(CAP_SYS_CHROOT))
		goto dput_and_out;
	error = security_path_chroot(&path);
	if (error)
		goto dput_and_out;

	set_fs_root(current->fs, &path);
	error = 0;
dput_and_out:
	path_put(&path);
out:
	return error;
}

static int chmod_common(struct path *path, umode_t mode)
{
	struct inode *inode = path->dentry->d_inode;
	struct iattr newattrs;
	int error;

	error = mnt_want_write(path->mnt);
	if (error)
		return error;
	mutex_lock(&inode->i_mutex);
	error = security_path_chmod(path, mode);
	if (error)
		goto out_unlock;
	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	error = notify_change(path->dentry, &newattrs);
out_unlock:
	mutex_unlock(&inode->i_mutex);
	mnt_drop_write(path->mnt);
	return error;
}

SYSCALL_DEFINE2(fchmod, unsigned int, fd, umode_t, mode)
{
	struct file * file;
	int err = -EBADF;

	file = fget(fd);
	if (file) {
		audit_inode(NULL, file->f_path.dentry);
		err = chmod_common(&file->f_path, mode);
		fput(file);
	}
	return err;
}

SYSCALL_DEFINE3(fchmodat, int, dfd, const char __user *, filename, umode_t, mode)
{
	struct path path;
	int error;

	error = user_path_at(dfd, filename, LOOKUP_FOLLOW, &path);
	if (!error) {
		error = chmod_common(&path, mode);
		path_put(&path);
	}
	return error;
}

SYSCALL_DEFINE2(chmod, const char __user *, filename, umode_t, mode)
{
	return sys_fchmodat(AT_FDCWD, filename, mode);
}

static int chown_common(struct path *path, uid_t user, gid_t group)
{
	struct inode *inode = path->dentry->d_inode;
	int error;
	struct iattr newattrs;

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
	error = security_path_chown(path, user, group);
	if (!error)
		error = notify_change(path->dentry, &newattrs);
	mutex_unlock(&inode->i_mutex);

	return error;
}

SYSCALL_DEFINE3(chown, const char __user *, filename, uid_t, user, gid_t, group)
{
	struct path path;
	int error;

	error = user_path(filename, &path);
	if (error)
		goto out;
	error = mnt_want_write(path.mnt);
	if (error)
		goto out_release;
	error = chown_common(&path, user, group);
	mnt_drop_write(path.mnt);
out_release:
	path_put(&path);
out:
	return error;
}

SYSCALL_DEFINE5(fchownat, int, dfd, const char __user *, filename, uid_t, user,
		gid_t, group, int, flag)
{
	struct path path;
	int error = -EINVAL;
	int lookup_flags;

	if ((flag & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH)) != 0)
		goto out;

	lookup_flags = (flag & AT_SYMLINK_NOFOLLOW) ? 0 : LOOKUP_FOLLOW;
	if (flag & AT_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;
	error = user_path_at(dfd, filename, lookup_flags, &path);
	if (error)
		goto out;
	error = mnt_want_write(path.mnt);
	if (error)
		goto out_release;
	error = chown_common(&path, user, group);
	mnt_drop_write(path.mnt);
out_release:
	path_put(&path);
out:
	return error;
}

SYSCALL_DEFINE3(lchown, const char __user *, filename, uid_t, user, gid_t, group)
{
	struct path path;
	int error;

	error = user_lpath(filename, &path);
	if (error)
		goto out;
	error = mnt_want_write(path.mnt);
	if (error)
		goto out_release;
	error = chown_common(&path, user, group);
	mnt_drop_write(path.mnt);
out_release:
	path_put(&path);
out:
	return error;
}

SYSCALL_DEFINE3(fchown, unsigned int, fd, uid_t, user, gid_t, group)
{
	struct file * file;
	int error = -EBADF;
	struct dentry * dentry;

	file = fget(fd);
	if (!file)
		goto out;

	error = mnt_want_write_file(file);
	if (error)
		goto out_fput;
	dentry = file->f_path.dentry;
	audit_inode(NULL, dentry);
	error = chown_common(&file->f_path, user, group);
	mnt_drop_write_file(file);
out_fput:
	fput(file);
out:
	return error;
}

/*
 * You have to be very careful that these write
 * counts get cleaned up in error cases and
 * upon __fput().  This should probably never
 * be called outside of __dentry_open().
 */
static inline int __get_file_write_access(struct inode *inode,
					  struct vfsmount *mnt)
{
	int error;
	error = get_write_access(inode);
	if (error)
		return error;
	/*
	 * Do not take mount writer counts on
	 * special files since no writes to
	 * the mount itself will occur.
	 */
	if (!special_file(inode->i_mode)) {
		/*
		 * Balanced in __fput()
		 */
		error = mnt_want_write(mnt);
		if (error)
			put_write_access(inode);
	}
	return error;
}

static struct file *__dentry_open(struct dentry *dentry, struct vfsmount *mnt,
					struct file *f,
					int (*open)(struct inode *, struct file *),
					const struct cred *cred)
{
	static const struct file_operations empty_fops = {};
	struct inode *inode;
	int error;

	f->f_mode = OPEN_FMODE(f->f_flags) | FMODE_LSEEK |
				FMODE_PREAD | FMODE_PWRITE;

	if (unlikely(f->f_flags & O_PATH))
		f->f_mode = FMODE_PATH;

	inode = dentry->d_inode;
	if (f->f_mode & FMODE_WRITE) {
		error = __get_file_write_access(inode, mnt);
		if (error)
			goto cleanup_file;
		if (!special_file(inode->i_mode))
			file_take_write(f);
	}

	f->f_mapping = inode->i_mapping;
	f->f_path.dentry = dentry;
	f->f_path.mnt = mnt;
	f->f_pos = 0;
	file_sb_list_add(f, inode->i_sb);

	if (unlikely(f->f_mode & FMODE_PATH)) {
		f->f_op = &empty_fops;
		return f;
	}

	f->f_op = fops_get(inode->i_fop);

	error = security_dentry_open(f, cred);
	if (error)
		goto cleanup_all;

	error = break_lease(inode, f->f_flags);
	if (error)
		goto cleanup_all;

	if (!open && f->f_op)
		open = f->f_op->open;
	if (open) {
		error = open(inode, f);
		if (error)
			goto cleanup_all;
	}
	if ((f->f_mode & (FMODE_READ | FMODE_WRITE)) == FMODE_READ)
		i_readcount_inc(inode);

	f->f_flags &= ~(O_CREAT | O_EXCL | O_NOCTTY | O_TRUNC);

	file_ra_state_init(&f->f_ra, f->f_mapping->host->i_mapping);

	/* NB: we're sure to have correct a_ops only after f_op->open */
	if (f->f_flags & O_DIRECT) {
		if (!f->f_mapping->a_ops ||
		    ((!f->f_mapping->a_ops->direct_IO) &&
		    (!f->f_mapping->a_ops->get_xip_mem))) {
			fput(f);
			f = ERR_PTR(-EINVAL);
		}
	}

	return f;

cleanup_all:
	fops_put(f->f_op);
	if (f->f_mode & FMODE_WRITE) {
		put_write_access(inode);
		if (!special_file(inode->i_mode)) {
			/*
			 * We don't consider this a real
			 * mnt_want/drop_write() pair
			 * because it all happenend right
			 * here, so just reset the state.
			 */
			file_reset_write(f);
			mnt_drop_write(mnt);
		}
	}
	file_sb_list_del(f);
	f->f_path.dentry = NULL;
	f->f_path.mnt = NULL;
cleanup_file:
	put_filp(f);
	dput(dentry);
	mntput(mnt);
	return ERR_PTR(error);
}

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
	const struct cred *cred = current_cred();

	if (IS_ERR(nd->intent.open.file))
		goto out;
	if (IS_ERR(dentry))
		goto out_err;
	nd->intent.open.file = __dentry_open(dget(dentry), mntget(nd->path.mnt),
					     nd->intent.open.file,
					     open, cred);
out:
	return nd->intent.open.file;
out_err:
	release_open_intent(nd);
	nd->intent.open.file = ERR_CAST(dentry);
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
struct file *nameidata_to_filp(struct nameidata *nd)
{
	const struct cred *cred = current_cred();
	struct file *filp;

	/* Pick up the filp from the open intent */
	filp = nd->intent.open.file;
	nd->intent.open.file = NULL;

	/* Has the filesystem initialised the file for us? */
	if (filp->f_path.dentry == NULL) {
		path_get(&nd->path);
		filp = __dentry_open(nd->path.dentry, nd->path.mnt, filp,
				     NULL, cred);
	}
	return filp;
}

/*
 * dentry_open() will have done dput(dentry) and mntput(mnt) if it returns an
 * error.
 */
struct file *dentry_open(struct dentry *dentry, struct vfsmount *mnt, int flags,
			 const struct cred *cred)
{
	int error;
	struct file *f;

	validate_creds(cred);

	/* We must always pass in a valid mount pointer. */
	BUG_ON(!mnt);

	error = -ENFILE;
	f = get_empty_filp();
	if (f == NULL) {
		dput(dentry);
		mntput(mnt);
		return ERR_PTR(error);
	}

	f->f_flags = flags;
	return __dentry_open(dentry, mnt, f, NULL, cred);
}
EXPORT_SYMBOL(dentry_open);

static void __put_unused_fd(struct files_struct *files, unsigned int fd)
{
	struct fdtable *fdt = files_fdtable(files);
	__clear_open_fd(fd, fdt);
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

static inline int build_open_flags(int flags, umode_t mode, struct open_flags *op)
{
	int lookup_flags = 0;
	int acc_mode;

	if (flags & O_CREAT)
		op->mode = (mode & S_IALLUGO) | S_IFREG;
	else
		op->mode = 0;

	/* Must never be set by userspace */
	flags &= ~FMODE_NONOTIFY;

	/*
	 * O_SYNC is implemented as __O_SYNC|O_DSYNC.  As many places only
	 * check for O_DSYNC if the need any syncing at all we enforce it's
	 * always set instead of having to deal with possibly weird behaviour
	 * for malicious applications setting only __O_SYNC.
	 */
	if (flags & __O_SYNC)
		flags |= O_DSYNC;

	/*
	 * If we have O_PATH in the open flag. Then we
	 * cannot have anything other than the below set of flags
	 */
	if (flags & O_PATH) {
		flags &= O_DIRECTORY | O_NOFOLLOW | O_PATH;
		acc_mode = 0;
	} else {
		acc_mode = MAY_OPEN | ACC_MODE(flags);
	}

	op->open_flag = flags;

	/* O_TRUNC implies we need access checks for write permissions */
	if (flags & O_TRUNC)
		acc_mode |= MAY_WRITE;

	/* Allow the LSM permission hook to distinguish append
	   access from general write access. */
	if (flags & O_APPEND)
		acc_mode |= MAY_APPEND;

	op->acc_mode = acc_mode;

	op->intent = flags & O_PATH ? 0 : LOOKUP_OPEN;

	if (flags & O_CREAT) {
		op->intent |= LOOKUP_CREATE;
		if (flags & O_EXCL)
			op->intent |= LOOKUP_EXCL;
	}

	if (flags & O_DIRECTORY)
		lookup_flags |= LOOKUP_DIRECTORY;
	if (!(flags & O_NOFOLLOW))
		lookup_flags |= LOOKUP_FOLLOW;
	return lookup_flags;
}

/**
 * filp_open - open file and return file pointer
 *
 * @filename:	path to open
 * @flags:	open flags as per the open(2) second argument
 * @mode:	mode for the new file if O_CREAT is set, else ignored
 *
 * This is the helper to open a file from kernelspace if you really
 * have to.  But in generally you should not do this, so please move
 * along, nothing to see here..
 */
struct file *filp_open(const char *filename, int flags, umode_t mode)
{
	struct open_flags op;
	int lookup = build_open_flags(flags, mode, &op);
	return do_filp_open(AT_FDCWD, filename, &op, lookup);
}
EXPORT_SYMBOL(filp_open);

struct file *file_open_root(struct dentry *dentry, struct vfsmount *mnt,
			    const char *filename, int flags)
{
	struct open_flags op;
	int lookup = build_open_flags(flags, 0, &op);
	if (flags & O_CREAT)
		return ERR_PTR(-EINVAL);
	if (!filename && (flags & O_DIRECTORY))
		if (!dentry->d_inode->i_op->lookup)
			return ERR_PTR(-ENOTDIR);
	return do_file_open_root(dentry, mnt, filename, &op, lookup);
}
EXPORT_SYMBOL(file_open_root);

long do_sys_open(int dfd, const char __user *filename, int flags, umode_t mode)
{
	struct open_flags op;
	int lookup = build_open_flags(flags, mode, &op);
	char *tmp = getname(filename);
	int fd = PTR_ERR(tmp);

	if (!IS_ERR(tmp)) {
		fd = get_unused_fd_flags(flags);
		if (fd >= 0) {
			struct file *f = do_filp_open(dfd, tmp, &op, lookup);
			if (IS_ERR(f)) {
				put_unused_fd(fd);
				fd = PTR_ERR(f);
			} else {
				fsnotify_open(f);
				fd_install(fd, f);
			}
		}
		putname(tmp);
	}
	return fd;
}

SYSCALL_DEFINE3(open, const char __user *, filename, int, flags, umode_t, mode)
{
	long ret;

	if (force_o_largefile())
		flags |= O_LARGEFILE;

	ret = do_sys_open(AT_FDCWD, filename, flags, mode);
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(3, ret, filename, flags, mode);
	return ret;
}

SYSCALL_DEFINE4(openat, int, dfd, const char __user *, filename, int, flags,
		umode_t, mode)
{
	long ret;

	if (force_o_largefile())
		flags |= O_LARGEFILE;

	ret = do_sys_open(dfd, filename, flags, mode);
	/* avoid REGPARM breakage on x86: */
	asmlinkage_protect(4, ret, dfd, filename, flags, mode);
	return ret;
}

#ifndef __alpha__

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
SYSCALL_DEFINE2(creat, const char __user *, pathname, umode_t, mode)
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

	if (likely(!(filp->f_mode & FMODE_PATH))) {
		dnotify_flush(filp, id);
		locks_remove_posix(filp, id);
	}
	fput(filp);
	return retval;
}

EXPORT_SYMBOL(filp_close);

/*
 * Careful here! We test whether the file pointer is NULL before
 * releasing the fd. This ensures that one clone task can't release
 * an fd while another clone is opening it.
 */
SYSCALL_DEFINE1(close, unsigned int, fd)
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
	__clear_close_on_exec(fd, fdt);
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
SYSCALL_DEFINE0(vhangup)
{
	if (capable(CAP_SYS_TTY_CONFIG)) {
		tty_vhangup_self();
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
 * file descriptors. The function is not supposed to ever fail, the only
 * reason it returns an 'int' and not 'void' is so that it can be plugged
 * directly into file_operations structure.
 */
int nonseekable_open(struct inode *inode, struct file *filp)
{
	filp->f_mode &= ~(FMODE_LSEEK | FMODE_PREAD | FMODE_PWRITE);
	return 0;
}

EXPORT_SYMBOL(nonseekable_open);
