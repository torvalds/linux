// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   Copyright (C) 2016 Namjae Jeon <linkinjeon@kernel.org>
 *   Copyright (C) 2018 Samsung Electronics Co., Ltd.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/filelock.h>
#include <linux/uaccess.h>
#include <linux/backing-dev.h>
#include <linux/writeback.h>
#include <linux/xattr.h>
#include <linux/falloc.h>
#include <linux/fsnotify.h>
#include <linux/dcache.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sched/xacct.h>
#include <linux/crc32c.h>
#include <linux/namei.h>

#include "glob.h"
#include "oplock.h"
#include "connection.h"
#include "vfs.h"
#include "vfs_cache.h"
#include "smbacl.h"
#include "ndr.h"
#include "auth.h"
#include "misc.h"

#include "smb_common.h"
#include "mgmt/share_config.h"
#include "mgmt/tree_connect.h"
#include "mgmt/user_session.h"
#include "mgmt/user_config.h"

static void ksmbd_vfs_inherit_owner(struct ksmbd_work *work,
				    struct inode *parent_inode,
				    struct inode *inode)
{
	if (!test_share_config_flag(work->tcon->share_conf,
				    KSMBD_SHARE_FLAG_INHERIT_OWNER))
		return;

	i_uid_write(inode, i_uid_read(parent_inode));
}

/**
 * ksmbd_vfs_lock_parent() - lock parent dentry if it is stable
 * @parent: parent dentry
 * @child: child dentry
 *
 * Returns: %0 on success, %-ENOENT if the parent dentry is not stable
 */
int ksmbd_vfs_lock_parent(struct dentry *parent, struct dentry *child)
{
	inode_lock_nested(d_inode(parent), I_MUTEX_PARENT);
	if (child->d_parent != parent) {
		inode_unlock(d_inode(parent));
		return -ENOENT;
	}

	return 0;
}

static int ksmbd_vfs_path_lookup_locked(struct ksmbd_share_config *share_conf,
					char *pathname, unsigned int flags,
					struct path *parent_path,
					struct path *path)
{
	struct qstr last;
	struct filename *filename;
	struct path *root_share_path = &share_conf->vfs_path;
	int err, type;
	struct dentry *d;

	if (pathname[0] == '\0') {
		pathname = share_conf->path;
		root_share_path = NULL;
	} else {
		flags |= LOOKUP_BENEATH;
	}

	filename = getname_kernel(pathname);
	if (IS_ERR(filename))
		return PTR_ERR(filename);

	err = vfs_path_parent_lookup(filename, flags,
				     parent_path, &last, &type,
				     root_share_path);
	if (err) {
		putname(filename);
		return err;
	}

	if (unlikely(type != LAST_NORM)) {
		path_put(parent_path);
		putname(filename);
		return -ENOENT;
	}

	err = mnt_want_write(parent_path->mnt);
	if (err) {
		path_put(parent_path);
		putname(filename);
		return -ENOENT;
	}

	inode_lock_nested(parent_path->dentry->d_inode, I_MUTEX_PARENT);
	d = lookup_one_qstr_excl(&last, parent_path->dentry, 0);
	if (IS_ERR(d))
		goto err_out;

	path->dentry = d;
	path->mnt = mntget(parent_path->mnt);

	if (test_share_config_flag(share_conf, KSMBD_SHARE_FLAG_CROSSMNT)) {
		err = follow_down(path, 0);
		if (err < 0) {
			path_put(path);
			goto err_out;
		}
	}

	putname(filename);
	return 0;

err_out:
	inode_unlock(d_inode(parent_path->dentry));
	mnt_drop_write(parent_path->mnt);
	path_put(parent_path);
	putname(filename);
	return -ENOENT;
}

void ksmbd_vfs_query_maximal_access(struct mnt_idmap *idmap,
				   struct dentry *dentry, __le32 *daccess)
{
	*daccess = cpu_to_le32(FILE_READ_ATTRIBUTES | READ_CONTROL);

	if (!inode_permission(idmap, d_inode(dentry), MAY_OPEN | MAY_WRITE))
		*daccess |= cpu_to_le32(WRITE_DAC | WRITE_OWNER | SYNCHRONIZE |
				FILE_WRITE_DATA | FILE_APPEND_DATA |
				FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES |
				FILE_DELETE_CHILD);

	if (!inode_permission(idmap, d_inode(dentry), MAY_OPEN | MAY_READ))
		*daccess |= FILE_READ_DATA_LE | FILE_READ_EA_LE;

	if (!inode_permission(idmap, d_inode(dentry), MAY_OPEN | MAY_EXEC))
		*daccess |= FILE_EXECUTE_LE;

	if (!inode_permission(idmap, d_inode(dentry->d_parent), MAY_EXEC | MAY_WRITE))
		*daccess |= FILE_DELETE_LE;
}

/**
 * ksmbd_vfs_create() - vfs helper for smb create file
 * @work:	work
 * @name:	file name that is relative to share
 * @mode:	file create mode
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_create(struct ksmbd_work *work, const char *name, umode_t mode)
{
	struct path path;
	struct dentry *dentry;
	int err;

	dentry = ksmbd_vfs_kern_path_create(work, name,
					    LOOKUP_NO_SYMLINKS, &path);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		if (err != -ENOENT)
			pr_err("path create failed for %s, err %d\n",
			       name, err);
		return err;
	}

	mode |= S_IFREG;
	err = vfs_create(mnt_idmap(path.mnt), d_inode(path.dentry),
			 dentry, mode, true);
	if (!err) {
		ksmbd_vfs_inherit_owner(work, d_inode(path.dentry),
					d_inode(dentry));
	} else {
		pr_err("File(%s): creation failed (err:%d)\n", name, err);
	}

	done_path_create(&path, dentry);
	return err;
}

/**
 * ksmbd_vfs_mkdir() - vfs helper for smb create directory
 * @work:	work
 * @name:	directory name that is relative to share
 * @mode:	directory create mode
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_mkdir(struct ksmbd_work *work, const char *name, umode_t mode)
{
	struct mnt_idmap *idmap;
	struct path path;
	struct dentry *dentry, *d;
	int err = 0;

	dentry = ksmbd_vfs_kern_path_create(work, name,
					    LOOKUP_NO_SYMLINKS | LOOKUP_DIRECTORY,
					    &path);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		if (err != -EEXIST)
			ksmbd_debug(VFS, "path create failed for %s, err %d\n",
				    name, err);
		return err;
	}

	idmap = mnt_idmap(path.mnt);
	mode |= S_IFDIR;
	d = dentry;
	dentry = vfs_mkdir(idmap, d_inode(path.dentry), dentry, mode);
	if (IS_ERR(dentry))
		err = PTR_ERR(dentry);
	else if (d_is_negative(dentry))
		err = -ENOENT;
	if (!err && dentry != d)
		ksmbd_vfs_inherit_owner(work, d_inode(path.dentry), d_inode(dentry));

	done_path_create(&path, dentry);
	if (err)
		pr_err("mkdir(%s): creation failed (err:%d)\n", name, err);
	return err;
}

static ssize_t ksmbd_vfs_getcasexattr(struct mnt_idmap *idmap,
				      struct dentry *dentry, char *attr_name,
				      int attr_name_len, char **attr_value)
{
	char *name, *xattr_list = NULL;
	ssize_t value_len = -ENOENT, xattr_list_len;

	xattr_list_len = ksmbd_vfs_listxattr(dentry, &xattr_list);
	if (xattr_list_len <= 0)
		goto out;

	for (name = xattr_list; name - xattr_list < xattr_list_len;
			name += strlen(name) + 1) {
		ksmbd_debug(VFS, "%s, len %zd\n", name, strlen(name));
		if (strncasecmp(attr_name, name, attr_name_len))
			continue;

		value_len = ksmbd_vfs_getxattr(idmap,
					       dentry,
					       name,
					       attr_value);
		if (value_len < 0)
			pr_err("failed to get xattr in file\n");
		break;
	}

out:
	kvfree(xattr_list);
	return value_len;
}

static int ksmbd_vfs_stream_read(struct ksmbd_file *fp, char *buf, loff_t *pos,
				 size_t count)
{
	ssize_t v_len;
	char *stream_buf = NULL;

	ksmbd_debug(VFS, "read stream data pos : %llu, count : %zd\n",
		    *pos, count);

	v_len = ksmbd_vfs_getcasexattr(file_mnt_idmap(fp->filp),
				       fp->filp->f_path.dentry,
				       fp->stream.name,
				       fp->stream.size,
				       &stream_buf);
	if ((int)v_len <= 0)
		return (int)v_len;

	if (v_len <= *pos) {
		count = -EINVAL;
		goto free_buf;
	}

	if (v_len - *pos < count)
		count = v_len - *pos;

	memcpy(buf, &stream_buf[*pos], count);

free_buf:
	kvfree(stream_buf);
	return count;
}

/**
 * check_lock_range() - vfs helper for smb byte range file locking
 * @filp:	the file to apply the lock to
 * @start:	lock start byte offset
 * @end:	lock end byte offset
 * @type:	byte range type read/write
 *
 * Return:	0 on success, otherwise error
 */
static int check_lock_range(struct file *filp, loff_t start, loff_t end,
			    unsigned char type)
{
	struct file_lock *flock;
	struct file_lock_context *ctx = locks_inode_context(file_inode(filp));
	int error = 0;

	if (!ctx || list_empty_careful(&ctx->flc_posix))
		return 0;

	spin_lock(&ctx->flc_lock);
	for_each_file_lock(flock, &ctx->flc_posix) {
		/* check conflict locks */
		if (flock->fl_end >= start && end >= flock->fl_start) {
			if (lock_is_read(flock)) {
				if (type == WRITE) {
					pr_err("not allow write by shared lock\n");
					error = 1;
					goto out;
				}
			} else if (lock_is_write(flock)) {
				/* check owner in lock */
				if (flock->c.flc_file != filp) {
					error = 1;
					pr_err("not allow rw access by exclusive lock from other opens\n");
					goto out;
				}
			}
		}
	}
out:
	spin_unlock(&ctx->flc_lock);
	return error;
}

/**
 * ksmbd_vfs_read() - vfs helper for smb file read
 * @work:	smb work
 * @fp:		ksmbd file pointer
 * @count:	read byte count
 * @pos:	file pos
 * @rbuf:	read data buffer
 *
 * Return:	number of read bytes on success, otherwise error
 */
int ksmbd_vfs_read(struct ksmbd_work *work, struct ksmbd_file *fp, size_t count,
		   loff_t *pos, char *rbuf)
{
	struct file *filp = fp->filp;
	ssize_t nbytes = 0;
	struct inode *inode = file_inode(filp);

	if (S_ISDIR(inode->i_mode))
		return -EISDIR;

	if (unlikely(count == 0))
		return 0;

	if (work->conn->connection_type) {
		if (!(fp->daccess & (FILE_READ_DATA_LE | FILE_EXECUTE_LE))) {
			pr_err("no right to read(%pD)\n", fp->filp);
			return -EACCES;
		}
	}

	if (ksmbd_stream_fd(fp))
		return ksmbd_vfs_stream_read(fp, rbuf, pos, count);

	if (!work->tcon->posix_extensions) {
		int ret;

		ret = check_lock_range(filp, *pos, *pos + count - 1, READ);
		if (ret) {
			pr_err("unable to read due to lock\n");
			return -EAGAIN;
		}
	}

	nbytes = kernel_read(filp, rbuf, count, pos);
	if (nbytes < 0) {
		pr_err("smb read failed, err = %zd\n", nbytes);
		return nbytes;
	}

	filp->f_pos = *pos;
	return nbytes;
}

static int ksmbd_vfs_stream_write(struct ksmbd_file *fp, char *buf, loff_t *pos,
				  size_t count)
{
	char *stream_buf = NULL, *wbuf;
	struct mnt_idmap *idmap = file_mnt_idmap(fp->filp);
	size_t size;
	ssize_t v_len;
	int err = 0;

	ksmbd_debug(VFS, "write stream data pos : %llu, count : %zd\n",
		    *pos, count);

	size = *pos + count;
	if (size > XATTR_SIZE_MAX) {
		size = XATTR_SIZE_MAX;
		count = (*pos + count) - XATTR_SIZE_MAX;
	}

	v_len = ksmbd_vfs_getcasexattr(idmap,
				       fp->filp->f_path.dentry,
				       fp->stream.name,
				       fp->stream.size,
				       &stream_buf);
	if (v_len < 0) {
		pr_err("not found stream in xattr : %zd\n", v_len);
		err = v_len;
		goto out;
	}

	if (v_len < size) {
		wbuf = kvzalloc(size, KSMBD_DEFAULT_GFP);
		if (!wbuf) {
			err = -ENOMEM;
			goto out;
		}

		if (v_len > 0)
			memcpy(wbuf, stream_buf, v_len);
		kvfree(stream_buf);
		stream_buf = wbuf;
	}

	memcpy(&stream_buf[*pos], buf, count);

	err = ksmbd_vfs_setxattr(idmap,
				 &fp->filp->f_path,
				 fp->stream.name,
				 (void *)stream_buf,
				 size,
				 0,
				 true);
	if (err < 0)
		goto out;

	fp->filp->f_pos = *pos;
	err = 0;
out:
	kvfree(stream_buf);
	return err;
}

/**
 * ksmbd_vfs_write() - vfs helper for smb file write
 * @work:	work
 * @fp:		ksmbd file pointer
 * @buf:	buf containing data for writing
 * @count:	read byte count
 * @pos:	file pos
 * @sync:	fsync after write
 * @written:	number of bytes written
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_write(struct ksmbd_work *work, struct ksmbd_file *fp,
		    char *buf, size_t count, loff_t *pos, bool sync,
		    ssize_t *written)
{
	struct file *filp;
	loff_t	offset = *pos;
	int err = 0;

	if (work->conn->connection_type) {
		if (!(fp->daccess & (FILE_WRITE_DATA_LE | FILE_APPEND_DATA_LE))) {
			pr_err("no right to write(%pD)\n", fp->filp);
			err = -EACCES;
			goto out;
		}
	}

	filp = fp->filp;

	if (ksmbd_stream_fd(fp)) {
		err = ksmbd_vfs_stream_write(fp, buf, pos, count);
		if (!err)
			*written = count;
		goto out;
	}

	if (!work->tcon->posix_extensions) {
		err = check_lock_range(filp, *pos, *pos + count - 1, WRITE);
		if (err) {
			pr_err("unable to write due to lock\n");
			err = -EAGAIN;
			goto out;
		}
	}

	/* Reserve lease break for parent dir at closing time */
	fp->reserve_lease_break = true;

	/* Do we need to break any of a levelII oplock? */
	smb_break_all_levII_oplock(work, fp, 1);

	err = kernel_write(filp, buf, count, pos);
	if (err < 0) {
		ksmbd_debug(VFS, "smb write failed, err = %d\n", err);
		goto out;
	}

	filp->f_pos = *pos;
	*written = err;
	err = 0;
	if (sync) {
		err = vfs_fsync_range(filp, offset, offset + *written, 0);
		if (err < 0)
			pr_err("fsync failed for filename = %pD, err = %d\n",
			       fp->filp, err);
	}

out:
	return err;
}

/**
 * ksmbd_vfs_getattr() - vfs helper for smb getattr
 * @path:	path of dentry
 * @stat:	pointer to returned kernel stat structure
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_getattr(const struct path *path, struct kstat *stat)
{
	int err;

	err = vfs_getattr(path, stat, STATX_BTIME, AT_STATX_SYNC_AS_STAT);
	if (err)
		pr_err("getattr failed, err %d\n", err);
	return err;
}

/**
 * ksmbd_vfs_fsync() - vfs helper for smb fsync
 * @work:	work
 * @fid:	file id of open file
 * @p_id:	persistent file id
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_fsync(struct ksmbd_work *work, u64 fid, u64 p_id)
{
	struct ksmbd_file *fp;
	int err;

	fp = ksmbd_lookup_fd_slow(work, fid, p_id);
	if (!fp) {
		pr_err("failed to get filp for fid %llu\n", fid);
		return -ENOENT;
	}
	err = vfs_fsync(fp->filp, 0);
	if (err < 0)
		pr_err("smb fsync failed, err = %d\n", err);
	ksmbd_fd_put(work, fp);
	return err;
}

/**
 * ksmbd_vfs_remove_file() - vfs helper for smb rmdir or unlink
 * @work:	work
 * @path:	path of dentry
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_remove_file(struct ksmbd_work *work, const struct path *path)
{
	struct mnt_idmap *idmap;
	struct dentry *parent = path->dentry->d_parent;
	int err;

	if (ksmbd_override_fsids(work))
		return -ENOMEM;

	if (!d_inode(path->dentry)->i_nlink) {
		err = -ENOENT;
		goto out_err;
	}

	idmap = mnt_idmap(path->mnt);
	if (S_ISDIR(d_inode(path->dentry)->i_mode)) {
		err = vfs_rmdir(idmap, d_inode(parent), path->dentry);
		if (err && err != -ENOTEMPTY)
			ksmbd_debug(VFS, "rmdir failed, err %d\n", err);
	} else {
		err = vfs_unlink(idmap, d_inode(parent), path->dentry, NULL);
		if (err)
			ksmbd_debug(VFS, "unlink failed, err %d\n", err);
	}

out_err:
	ksmbd_revert_fsids(work);
	return err;
}

/**
 * ksmbd_vfs_link() - vfs helper for creating smb hardlink
 * @work:	work
 * @oldname:	source file name
 * @newname:	hardlink name that is relative to share
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_link(struct ksmbd_work *work, const char *oldname,
		   const char *newname)
{
	struct path oldpath, newpath;
	struct dentry *dentry;
	int err;

	if (ksmbd_override_fsids(work))
		return -ENOMEM;

	err = kern_path(oldname, LOOKUP_NO_SYMLINKS, &oldpath);
	if (err) {
		pr_err("cannot get linux path for %s, err = %d\n",
		       oldname, err);
		goto out1;
	}

	dentry = ksmbd_vfs_kern_path_create(work, newname,
					    LOOKUP_NO_SYMLINKS | LOOKUP_REVAL,
					    &newpath);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		pr_err("path create err for %s, err %d\n", newname, err);
		goto out2;
	}

	err = -EXDEV;
	if (oldpath.mnt != newpath.mnt) {
		pr_err("vfs_link failed err %d\n", err);
		goto out3;
	}

	err = vfs_link(oldpath.dentry, mnt_idmap(newpath.mnt),
		       d_inode(newpath.dentry),
		       dentry, NULL);
	if (err)
		ksmbd_debug(VFS, "vfs_link failed err %d\n", err);

out3:
	done_path_create(&newpath, dentry);
out2:
	path_put(&oldpath);
out1:
	ksmbd_revert_fsids(work);
	return err;
}

int ksmbd_vfs_rename(struct ksmbd_work *work, const struct path *old_path,
		     char *newname, int flags)
{
	struct dentry *old_parent, *new_dentry, *trap;
	struct dentry *old_child = old_path->dentry;
	struct path new_path;
	struct qstr new_last;
	struct renamedata rd;
	struct filename *to;
	struct ksmbd_share_config *share_conf = work->tcon->share_conf;
	struct ksmbd_file *parent_fp;
	int new_type;
	int err, lookup_flags = LOOKUP_NO_SYMLINKS;
	int target_lookup_flags = LOOKUP_RENAME_TARGET;

	if (ksmbd_override_fsids(work))
		return -ENOMEM;

	to = getname_kernel(newname);
	if (IS_ERR(to)) {
		err = PTR_ERR(to);
		goto revert_fsids;
	}

	/*
	 * explicitly handle file overwrite case, for compatibility with
	 * filesystems that may not support rename flags (e.g: fuse)
	 */
	if (flags & RENAME_NOREPLACE)
		target_lookup_flags |= LOOKUP_EXCL;
	flags &= ~(RENAME_NOREPLACE);

retry:
	err = vfs_path_parent_lookup(to, lookup_flags | LOOKUP_BENEATH,
				     &new_path, &new_last, &new_type,
				     &share_conf->vfs_path);
	if (err)
		goto out1;

	if (old_path->mnt != new_path.mnt) {
		err = -EXDEV;
		goto out2;
	}

	err = mnt_want_write(old_path->mnt);
	if (err)
		goto out2;

	trap = lock_rename_child(old_child, new_path.dentry);
	if (IS_ERR(trap)) {
		err = PTR_ERR(trap);
		goto out_drop_write;
	}

	old_parent = dget(old_child->d_parent);
	if (d_unhashed(old_child)) {
		err = -EINVAL;
		goto out3;
	}

	parent_fp = ksmbd_lookup_fd_inode(old_child->d_parent);
	if (parent_fp) {
		if (parent_fp->daccess & FILE_DELETE_LE) {
			pr_err("parent dir is opened with delete access\n");
			err = -ESHARE;
			ksmbd_fd_put(work, parent_fp);
			goto out3;
		}
		ksmbd_fd_put(work, parent_fp);
	}

	new_dentry = lookup_one_qstr_excl(&new_last, new_path.dentry,
					  lookup_flags | target_lookup_flags);
	if (IS_ERR(new_dentry)) {
		err = PTR_ERR(new_dentry);
		goto out3;
	}

	if (d_is_symlink(new_dentry)) {
		err = -EACCES;
		goto out4;
	}

	if (old_child == trap) {
		err = -EINVAL;
		goto out4;
	}

	if (new_dentry == trap) {
		err = -ENOTEMPTY;
		goto out4;
	}

	rd.old_mnt_idmap	= mnt_idmap(old_path->mnt),
	rd.old_dir		= d_inode(old_parent),
	rd.old_dentry		= old_child,
	rd.new_mnt_idmap	= mnt_idmap(new_path.mnt),
	rd.new_dir		= new_path.dentry->d_inode,
	rd.new_dentry		= new_dentry,
	rd.flags		= flags,
	rd.delegated_inode	= NULL,
	err = vfs_rename(&rd);
	if (err)
		ksmbd_debug(VFS, "vfs_rename failed err %d\n", err);

out4:
	dput(new_dentry);
out3:
	dput(old_parent);
	unlock_rename(old_parent, new_path.dentry);
out_drop_write:
	mnt_drop_write(old_path->mnt);
out2:
	path_put(&new_path);

	if (retry_estale(err, lookup_flags)) {
		lookup_flags |= LOOKUP_REVAL;
		goto retry;
	}
out1:
	putname(to);
revert_fsids:
	ksmbd_revert_fsids(work);
	return err;
}

/**
 * ksmbd_vfs_truncate() - vfs helper for smb file truncate
 * @work:	work
 * @fp:		ksmbd file pointer
 * @size:	truncate to given size
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_truncate(struct ksmbd_work *work,
		       struct ksmbd_file *fp, loff_t size)
{
	int err = 0;
	struct file *filp;

	filp = fp->filp;

	/* Do we need to break any of a levelII oplock? */
	smb_break_all_levII_oplock(work, fp, 1);

	if (!work->tcon->posix_extensions) {
		struct inode *inode = file_inode(filp);

		if (size < inode->i_size) {
			err = check_lock_range(filp, size,
					       inode->i_size - 1, WRITE);
		} else {
			err = check_lock_range(filp, inode->i_size,
					       size - 1, WRITE);
		}

		if (err) {
			pr_err("failed due to lock\n");
			return -EAGAIN;
		}
	}

	err = vfs_truncate(&filp->f_path, size);
	if (err)
		pr_err("truncate failed, err %d\n", err);
	return err;
}

/**
 * ksmbd_vfs_listxattr() - vfs helper for smb list extended attributes
 * @dentry:	dentry of file for listing xattrs
 * @list:	destination buffer
 *
 * Return:	xattr list length on success, otherwise error
 */
ssize_t ksmbd_vfs_listxattr(struct dentry *dentry, char **list)
{
	ssize_t size;
	char *vlist = NULL;

	size = vfs_listxattr(dentry, NULL, 0);
	if (size <= 0)
		return size;

	vlist = kvzalloc(size, KSMBD_DEFAULT_GFP);
	if (!vlist)
		return -ENOMEM;

	*list = vlist;
	size = vfs_listxattr(dentry, vlist, size);
	if (size < 0) {
		ksmbd_debug(VFS, "listxattr failed\n");
		kvfree(vlist);
		*list = NULL;
	}

	return size;
}

static ssize_t ksmbd_vfs_xattr_len(struct mnt_idmap *idmap,
				   struct dentry *dentry, char *xattr_name)
{
	return vfs_getxattr(idmap, dentry, xattr_name, NULL, 0);
}

/**
 * ksmbd_vfs_getxattr() - vfs helper for smb get extended attributes value
 * @idmap:	idmap
 * @dentry:	dentry of file for getting xattrs
 * @xattr_name:	name of xattr name to query
 * @xattr_buf:	destination buffer xattr value
 *
 * Return:	read xattr value length on success, otherwise error
 */
ssize_t ksmbd_vfs_getxattr(struct mnt_idmap *idmap,
			   struct dentry *dentry,
			   char *xattr_name, char **xattr_buf)
{
	ssize_t xattr_len;
	char *buf;

	*xattr_buf = NULL;
	xattr_len = ksmbd_vfs_xattr_len(idmap, dentry, xattr_name);
	if (xattr_len < 0)
		return xattr_len;

	buf = kmalloc(xattr_len + 1, KSMBD_DEFAULT_GFP);
	if (!buf)
		return -ENOMEM;

	xattr_len = vfs_getxattr(idmap, dentry, xattr_name,
				 (void *)buf, xattr_len);
	if (xattr_len > 0)
		*xattr_buf = buf;
	else
		kfree(buf);
	return xattr_len;
}

/**
 * ksmbd_vfs_setxattr() - vfs helper for smb set extended attributes value
 * @idmap:	idmap of the relevant mount
 * @path:	path of dentry to set XATTR at
 * @attr_name:	xattr name for setxattr
 * @attr_value:	xattr value to set
 * @attr_size:	size of xattr value
 * @flags:	destination buffer length
 * @get_write:	get write access to a mount
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_setxattr(struct mnt_idmap *idmap,
		       const struct path *path, const char *attr_name,
		       void *attr_value, size_t attr_size, int flags,
		       bool get_write)
{
	int err;

	if (get_write == true) {
		err = mnt_want_write(path->mnt);
		if (err)
			return err;
	}

	err = vfs_setxattr(idmap,
			   path->dentry,
			   attr_name,
			   attr_value,
			   attr_size,
			   flags);
	if (err)
		ksmbd_debug(VFS, "setxattr failed, err %d\n", err);
	if (get_write == true)
		mnt_drop_write(path->mnt);
	return err;
}

/**
 * ksmbd_vfs_set_fadvise() - convert smb IO caching options to linux options
 * @filp:	file pointer for IO
 * @option:	smb IO options
 */
void ksmbd_vfs_set_fadvise(struct file *filp, __le32 option)
{
	struct address_space *mapping;

	mapping = filp->f_mapping;

	if (!option || !mapping)
		return;

	if (option & FILE_WRITE_THROUGH_LE) {
		filp->f_flags |= O_SYNC;
	} else if (option & FILE_SEQUENTIAL_ONLY_LE) {
		filp->f_ra.ra_pages = inode_to_bdi(mapping->host)->ra_pages * 2;
		spin_lock(&filp->f_lock);
		filp->f_mode &= ~FMODE_RANDOM;
		spin_unlock(&filp->f_lock);
	} else if (option & FILE_RANDOM_ACCESS_LE) {
		spin_lock(&filp->f_lock);
		filp->f_mode |= FMODE_RANDOM;
		spin_unlock(&filp->f_lock);
	}
}

int ksmbd_vfs_zero_data(struct ksmbd_work *work, struct ksmbd_file *fp,
			loff_t off, loff_t len)
{
	smb_break_all_levII_oplock(work, fp, 1);
	if (fp->f_ci->m_fattr & FILE_ATTRIBUTE_SPARSE_FILE_LE)
		return vfs_fallocate(fp->filp,
				     FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
				     off, len);

	return vfs_fallocate(fp->filp,
			     FALLOC_FL_ZERO_RANGE | FALLOC_FL_KEEP_SIZE,
			     off, len);
}

int ksmbd_vfs_fqar_lseek(struct ksmbd_file *fp, loff_t start, loff_t length,
			 struct file_allocated_range_buffer *ranges,
			 unsigned int in_count, unsigned int *out_count)
{
	struct file *f = fp->filp;
	struct inode *inode = file_inode(fp->filp);
	loff_t maxbytes = (u64)inode->i_sb->s_maxbytes, end;
	loff_t extent_start, extent_end;
	int ret = 0;

	if (start > maxbytes)
		return -EFBIG;

	if (!in_count)
		return 0;

	/*
	 * Shrink request scope to what the fs can actually handle.
	 */
	if (length > maxbytes || (maxbytes - length) < start)
		length = maxbytes - start;

	if (start + length > inode->i_size)
		length = inode->i_size - start;

	*out_count = 0;
	end = start + length;
	while (start < end && *out_count < in_count) {
		extent_start = vfs_llseek(f, start, SEEK_DATA);
		if (extent_start < 0) {
			if (extent_start != -ENXIO)
				ret = (int)extent_start;
			break;
		}

		if (extent_start >= end)
			break;

		extent_end = vfs_llseek(f, extent_start, SEEK_HOLE);
		if (extent_end < 0) {
			if (extent_end != -ENXIO)
				ret = (int)extent_end;
			break;
		} else if (extent_start >= extent_end) {
			break;
		}

		ranges[*out_count].file_offset = cpu_to_le64(extent_start);
		ranges[(*out_count)++].length =
			cpu_to_le64(min(extent_end, end) - extent_start);

		start = extent_end;
	}

	return ret;
}

int ksmbd_vfs_remove_xattr(struct mnt_idmap *idmap,
			   const struct path *path, char *attr_name,
			   bool get_write)
{
	int err;

	if (get_write == true) {
		err = mnt_want_write(path->mnt);
		if (err)
			return err;
	}

	err = vfs_removexattr(idmap, path->dentry, attr_name);

	if (get_write == true)
		mnt_drop_write(path->mnt);

	return err;
}

int ksmbd_vfs_unlink(struct file *filp)
{
	int err = 0;
	struct dentry *dir, *dentry = filp->f_path.dentry;
	struct mnt_idmap *idmap = file_mnt_idmap(filp);

	err = mnt_want_write(filp->f_path.mnt);
	if (err)
		return err;

	dir = dget_parent(dentry);
	err = ksmbd_vfs_lock_parent(dir, dentry);
	if (err)
		goto out;
	dget(dentry);

	if (S_ISDIR(d_inode(dentry)->i_mode))
		err = vfs_rmdir(idmap, d_inode(dir), dentry);
	else
		err = vfs_unlink(idmap, d_inode(dir), dentry, NULL);

	dput(dentry);
	inode_unlock(d_inode(dir));
	if (err)
		ksmbd_debug(VFS, "failed to delete, err %d\n", err);
out:
	dput(dir);
	mnt_drop_write(filp->f_path.mnt);

	return err;
}

static bool __dir_empty(struct dir_context *ctx, const char *name, int namlen,
		       loff_t offset, u64 ino, unsigned int d_type)
{
	struct ksmbd_readdir_data *buf;

	buf = container_of(ctx, struct ksmbd_readdir_data, ctx);
	if (!is_dot_dotdot(name, namlen))
		buf->dirent_count++;

	return !buf->dirent_count;
}

/**
 * ksmbd_vfs_empty_dir() - check for empty directory
 * @fp:	ksmbd file pointer
 *
 * Return:	true if directory empty, otherwise false
 */
int ksmbd_vfs_empty_dir(struct ksmbd_file *fp)
{
	int err;
	struct ksmbd_readdir_data readdir_data;

	memset(&readdir_data, 0, sizeof(struct ksmbd_readdir_data));

	set_ctx_actor(&readdir_data.ctx, __dir_empty);
	readdir_data.dirent_count = 0;

	err = iterate_dir(fp->filp, &readdir_data.ctx);
	if (readdir_data.dirent_count)
		err = -ENOTEMPTY;
	else
		err = 0;
	return err;
}

static bool __caseless_lookup(struct dir_context *ctx, const char *name,
			     int namlen, loff_t offset, u64 ino,
			     unsigned int d_type)
{
	struct ksmbd_readdir_data *buf;
	int cmp = -EINVAL;

	buf = container_of(ctx, struct ksmbd_readdir_data, ctx);

	if (buf->used != namlen)
		return true;
	if (IS_ENABLED(CONFIG_UNICODE) && buf->um) {
		const struct qstr q_buf = {.name = buf->private,
					   .len = buf->used};
		const struct qstr q_name = {.name = name,
					    .len = namlen};

		cmp = utf8_strncasecmp(buf->um, &q_buf, &q_name);
	}
	if (cmp < 0)
		cmp = strncasecmp((char *)buf->private, name, namlen);
	if (!cmp) {
		memcpy((char *)buf->private, name, buf->used);
		buf->dirent_count = 1;
		return false;
	}
	return true;
}

/**
 * ksmbd_vfs_lookup_in_dir() - lookup a file in a directory
 * @dir:	path info
 * @name:	filename to lookup
 * @namelen:	filename length
 * @um:		&struct unicode_map to use
 *
 * Return:	0 on success, otherwise error
 */
static int ksmbd_vfs_lookup_in_dir(const struct path *dir, char *name,
				   size_t namelen, struct unicode_map *um)
{
	int ret;
	struct file *dfilp;
	int flags = O_RDONLY | O_LARGEFILE;
	struct ksmbd_readdir_data readdir_data = {
		.ctx.actor	= __caseless_lookup,
		.private	= name,
		.used		= namelen,
		.dirent_count	= 0,
		.um		= um,
	};

	dfilp = dentry_open(dir, flags, current_cred());
	if (IS_ERR(dfilp))
		return PTR_ERR(dfilp);

	ret = iterate_dir(dfilp, &readdir_data.ctx);
	if (readdir_data.dirent_count > 0)
		ret = 0;
	fput(dfilp);
	return ret;
}

/**
 * ksmbd_vfs_kern_path_locked() - lookup a file and get path info
 * @work:	work
 * @name:		file path that is relative to share
 * @flags:		lookup flags
 * @parent_path:	if lookup succeed, return parent_path info
 * @path:		if lookup succeed, return path info
 * @caseless:	caseless filename lookup
 *
 * Return:	0 on success, otherwise error
 */
int ksmbd_vfs_kern_path_locked(struct ksmbd_work *work, char *name,
			       unsigned int flags, struct path *parent_path,
			       struct path *path, bool caseless)
{
	struct ksmbd_share_config *share_conf = work->tcon->share_conf;
	int err;

	err = ksmbd_vfs_path_lookup_locked(share_conf, name, flags, parent_path,
					   path);
	if (!err)
		return 0;

	if (caseless) {
		char *filepath;
		size_t path_len, remain_len;

		filepath = name;
		path_len = strlen(filepath);
		remain_len = path_len;

		*parent_path = share_conf->vfs_path;
		path_get(parent_path);

		while (d_can_lookup(parent_path->dentry)) {
			char *filename = filepath + path_len - remain_len;
			char *next = strchrnul(filename, '/');
			size_t filename_len = next - filename;
			bool is_last = !next[0];

			if (filename_len == 0)
				break;

			err = ksmbd_vfs_lookup_in_dir(parent_path, filename,
						      filename_len,
						      work->conn->um);
			if (err)
				goto out2;

			next[0] = '\0';

			err = vfs_path_lookup(share_conf->vfs_path.dentry,
					      share_conf->vfs_path.mnt,
					      filepath,
					      flags,
					      path);
			if (!is_last)
				next[0] = '/';
			if (err)
				goto out2;
			else if (is_last)
				goto out1;
			path_put(parent_path);
			*parent_path = *path;

			remain_len -= filename_len + 1;
		}

		err = -EINVAL;
out2:
		path_put(parent_path);
	}

out1:
	if (!err) {
		err = mnt_want_write(parent_path->mnt);
		if (err) {
			path_put(path);
			path_put(parent_path);
			return err;
		}

		err = ksmbd_vfs_lock_parent(parent_path->dentry, path->dentry);
		if (err) {
			path_put(path);
			path_put(parent_path);
		}
	}
	return err;
}

void ksmbd_vfs_kern_path_unlock(struct path *parent_path, struct path *path)
{
	inode_unlock(d_inode(parent_path->dentry));
	mnt_drop_write(parent_path->mnt);
	path_put(path);
	path_put(parent_path);
}

struct dentry *ksmbd_vfs_kern_path_create(struct ksmbd_work *work,
					  const char *name,
					  unsigned int flags,
					  struct path *path)
{
	char *abs_name;
	struct dentry *dent;

	abs_name = convert_to_unix_name(work->tcon->share_conf, name);
	if (!abs_name)
		return ERR_PTR(-ENOMEM);

	dent = kern_path_create(AT_FDCWD, abs_name, path, flags);
	kfree(abs_name);
	return dent;
}

int ksmbd_vfs_remove_acl_xattrs(struct mnt_idmap *idmap,
				const struct path *path)
{
	char *name, *xattr_list = NULL;
	ssize_t xattr_list_len;
	int err = 0;

	xattr_list_len = ksmbd_vfs_listxattr(path->dentry, &xattr_list);
	if (xattr_list_len < 0) {
		goto out;
	} else if (!xattr_list_len) {
		ksmbd_debug(SMB, "empty xattr in the file\n");
		goto out;
	}

	err = mnt_want_write(path->mnt);
	if (err)
		goto out;

	for (name = xattr_list; name - xattr_list < xattr_list_len;
	     name += strlen(name) + 1) {
		ksmbd_debug(SMB, "%s, len %zd\n", name, strlen(name));

		if (!strncmp(name, XATTR_NAME_POSIX_ACL_ACCESS,
			     sizeof(XATTR_NAME_POSIX_ACL_ACCESS) - 1) ||
		    !strncmp(name, XATTR_NAME_POSIX_ACL_DEFAULT,
			     sizeof(XATTR_NAME_POSIX_ACL_DEFAULT) - 1)) {
			err = vfs_remove_acl(idmap, path->dentry, name);
			if (err)
				ksmbd_debug(SMB,
					    "remove acl xattr failed : %s\n", name);
		}
	}
	mnt_drop_write(path->mnt);

out:
	kvfree(xattr_list);
	return err;
}

int ksmbd_vfs_remove_sd_xattrs(struct mnt_idmap *idmap, const struct path *path)
{
	char *name, *xattr_list = NULL;
	ssize_t xattr_list_len;
	int err = 0;

	xattr_list_len = ksmbd_vfs_listxattr(path->dentry, &xattr_list);
	if (xattr_list_len < 0) {
		goto out;
	} else if (!xattr_list_len) {
		ksmbd_debug(SMB, "empty xattr in the file\n");
		goto out;
	}

	for (name = xattr_list; name - xattr_list < xattr_list_len;
			name += strlen(name) + 1) {
		ksmbd_debug(SMB, "%s, len %zd\n", name, strlen(name));

		if (!strncmp(name, XATTR_NAME_SD, XATTR_NAME_SD_LEN)) {
			err = ksmbd_vfs_remove_xattr(idmap, path, name, true);
			if (err)
				ksmbd_debug(SMB, "remove xattr failed : %s\n", name);
		}
	}
out:
	kvfree(xattr_list);
	return err;
}

static struct xattr_smb_acl *ksmbd_vfs_make_xattr_posix_acl(struct mnt_idmap *idmap,
							    struct inode *inode,
							    int acl_type)
{
	struct xattr_smb_acl *smb_acl = NULL;
	struct posix_acl *posix_acls;
	struct posix_acl_entry *pa_entry;
	struct xattr_acl_entry *xa_entry;
	int i;

	if (!IS_ENABLED(CONFIG_FS_POSIX_ACL))
		return NULL;

	posix_acls = get_inode_acl(inode, acl_type);
	if (IS_ERR_OR_NULL(posix_acls))
		return NULL;

	smb_acl = kzalloc(sizeof(struct xattr_smb_acl) +
			  sizeof(struct xattr_acl_entry) * posix_acls->a_count,
			  KSMBD_DEFAULT_GFP);
	if (!smb_acl)
		goto out;

	smb_acl->count = posix_acls->a_count;
	pa_entry = posix_acls->a_entries;
	xa_entry = smb_acl->entries;
	for (i = 0; i < posix_acls->a_count; i++, pa_entry++, xa_entry++) {
		switch (pa_entry->e_tag) {
		case ACL_USER:
			xa_entry->type = SMB_ACL_USER;
			xa_entry->uid = posix_acl_uid_translate(idmap, pa_entry);
			break;
		case ACL_USER_OBJ:
			xa_entry->type = SMB_ACL_USER_OBJ;
			break;
		case ACL_GROUP:
			xa_entry->type = SMB_ACL_GROUP;
			xa_entry->gid = posix_acl_gid_translate(idmap, pa_entry);
			break;
		case ACL_GROUP_OBJ:
			xa_entry->type = SMB_ACL_GROUP_OBJ;
			break;
		case ACL_OTHER:
			xa_entry->type = SMB_ACL_OTHER;
			break;
		case ACL_MASK:
			xa_entry->type = SMB_ACL_MASK;
			break;
		default:
			pr_err("unknown type : 0x%x\n", pa_entry->e_tag);
			goto out;
		}

		if (pa_entry->e_perm & ACL_READ)
			xa_entry->perm |= SMB_ACL_READ;
		if (pa_entry->e_perm & ACL_WRITE)
			xa_entry->perm |= SMB_ACL_WRITE;
		if (pa_entry->e_perm & ACL_EXECUTE)
			xa_entry->perm |= SMB_ACL_EXECUTE;
	}
out:
	posix_acl_release(posix_acls);
	return smb_acl;
}

int ksmbd_vfs_set_sd_xattr(struct ksmbd_conn *conn,
			   struct mnt_idmap *idmap,
			   const struct path *path,
			   struct smb_ntsd *pntsd, int len,
			   bool get_write)
{
	int rc;
	struct ndr sd_ndr = {0}, acl_ndr = {0};
	struct xattr_ntacl acl = {0};
	struct xattr_smb_acl *smb_acl, *def_smb_acl = NULL;
	struct dentry *dentry = path->dentry;
	struct inode *inode = d_inode(dentry);

	acl.version = 4;
	acl.hash_type = XATTR_SD_HASH_TYPE_SHA256;
	acl.current_time = ksmbd_UnixTimeToNT(current_time(inode));

	memcpy(acl.desc, "posix_acl", 9);
	acl.desc_len = 10;

	pntsd->osidoffset =
		cpu_to_le32(le32_to_cpu(pntsd->osidoffset) + NDR_NTSD_OFFSETOF);
	pntsd->gsidoffset =
		cpu_to_le32(le32_to_cpu(pntsd->gsidoffset) + NDR_NTSD_OFFSETOF);
	pntsd->dacloffset =
		cpu_to_le32(le32_to_cpu(pntsd->dacloffset) + NDR_NTSD_OFFSETOF);

	acl.sd_buf = (char *)pntsd;
	acl.sd_size = len;

	rc = ksmbd_gen_sd_hash(conn, acl.sd_buf, acl.sd_size, acl.hash);
	if (rc) {
		pr_err("failed to generate hash for ndr acl\n");
		return rc;
	}

	smb_acl = ksmbd_vfs_make_xattr_posix_acl(idmap, inode,
						 ACL_TYPE_ACCESS);
	if (S_ISDIR(inode->i_mode))
		def_smb_acl = ksmbd_vfs_make_xattr_posix_acl(idmap, inode,
							     ACL_TYPE_DEFAULT);

	rc = ndr_encode_posix_acl(&acl_ndr, idmap, inode,
				  smb_acl, def_smb_acl);
	if (rc) {
		pr_err("failed to encode ndr to posix acl\n");
		goto out;
	}

	rc = ksmbd_gen_sd_hash(conn, acl_ndr.data, acl_ndr.offset,
			       acl.posix_acl_hash);
	if (rc) {
		pr_err("failed to generate hash for ndr acl\n");
		goto out;
	}

	rc = ndr_encode_v4_ntacl(&sd_ndr, &acl);
	if (rc) {
		pr_err("failed to encode ndr to posix acl\n");
		goto out;
	}

	rc = ksmbd_vfs_setxattr(idmap, path,
				XATTR_NAME_SD, sd_ndr.data,
				sd_ndr.offset, 0, get_write);
	if (rc < 0)
		pr_err("Failed to store XATTR ntacl :%d\n", rc);

	kfree(sd_ndr.data);
out:
	kfree(acl_ndr.data);
	kfree(smb_acl);
	kfree(def_smb_acl);
	return rc;
}

int ksmbd_vfs_get_sd_xattr(struct ksmbd_conn *conn,
			   struct mnt_idmap *idmap,
			   struct dentry *dentry,
			   struct smb_ntsd **pntsd)
{
	int rc;
	struct ndr n;
	struct inode *inode = d_inode(dentry);
	struct ndr acl_ndr = {0};
	struct xattr_ntacl acl;
	struct xattr_smb_acl *smb_acl = NULL, *def_smb_acl = NULL;
	__u8 cmp_hash[XATTR_SD_HASH_SIZE] = {0};

	rc = ksmbd_vfs_getxattr(idmap, dentry, XATTR_NAME_SD, &n.data);
	if (rc <= 0)
		return rc;

	n.length = rc;
	rc = ndr_decode_v4_ntacl(&n, &acl);
	if (rc)
		goto free_n_data;

	smb_acl = ksmbd_vfs_make_xattr_posix_acl(idmap, inode,
						 ACL_TYPE_ACCESS);
	if (S_ISDIR(inode->i_mode))
		def_smb_acl = ksmbd_vfs_make_xattr_posix_acl(idmap, inode,
							     ACL_TYPE_DEFAULT);

	rc = ndr_encode_posix_acl(&acl_ndr, idmap, inode, smb_acl,
				  def_smb_acl);
	if (rc) {
		pr_err("failed to encode ndr to posix acl\n");
		goto out_free;
	}

	rc = ksmbd_gen_sd_hash(conn, acl_ndr.data, acl_ndr.offset, cmp_hash);
	if (rc) {
		pr_err("failed to generate hash for ndr acl\n");
		goto out_free;
	}

	if (memcmp(cmp_hash, acl.posix_acl_hash, XATTR_SD_HASH_SIZE)) {
		pr_err("hash value diff\n");
		rc = -EINVAL;
		goto out_free;
	}

	*pntsd = acl.sd_buf;
	if (acl.sd_size < sizeof(struct smb_ntsd)) {
		pr_err("sd size is invalid\n");
		goto out_free;
	}

	(*pntsd)->osidoffset = cpu_to_le32(le32_to_cpu((*pntsd)->osidoffset) -
					   NDR_NTSD_OFFSETOF);
	(*pntsd)->gsidoffset = cpu_to_le32(le32_to_cpu((*pntsd)->gsidoffset) -
					   NDR_NTSD_OFFSETOF);
	(*pntsd)->dacloffset = cpu_to_le32(le32_to_cpu((*pntsd)->dacloffset) -
					   NDR_NTSD_OFFSETOF);

	rc = acl.sd_size;
out_free:
	kfree(acl_ndr.data);
	kfree(smb_acl);
	kfree(def_smb_acl);
	if (rc < 0) {
		kfree(acl.sd_buf);
		*pntsd = NULL;
	}

free_n_data:
	kfree(n.data);
	return rc;
}

int ksmbd_vfs_set_dos_attrib_xattr(struct mnt_idmap *idmap,
				   const struct path *path,
				   struct xattr_dos_attrib *da,
				   bool get_write)
{
	struct ndr n;
	int err;

	err = ndr_encode_dos_attr(&n, da);
	if (err)
		return err;

	err = ksmbd_vfs_setxattr(idmap, path, XATTR_NAME_DOS_ATTRIBUTE,
				 (void *)n.data, n.offset, 0, get_write);
	if (err)
		ksmbd_debug(SMB, "failed to store dos attribute in xattr\n");
	kfree(n.data);

	return err;
}

int ksmbd_vfs_get_dos_attrib_xattr(struct mnt_idmap *idmap,
				   struct dentry *dentry,
				   struct xattr_dos_attrib *da)
{
	struct ndr n;
	int err;

	err = ksmbd_vfs_getxattr(idmap, dentry, XATTR_NAME_DOS_ATTRIBUTE,
				 (char **)&n.data);
	if (err > 0) {
		n.length = err;
		if (ndr_decode_dos_attr(&n, da))
			err = -EINVAL;
		kfree(n.data);
	} else {
		ksmbd_debug(SMB, "failed to load dos attribute in xattr\n");
	}

	return err;
}

/**
 * ksmbd_vfs_init_kstat() - convert unix stat information to smb stat format
 * @p:          destination buffer
 * @ksmbd_kstat:      ksmbd kstat wrapper
 *
 * Returns: pointer to the converted &struct file_directory_info
 */
void *ksmbd_vfs_init_kstat(char **p, struct ksmbd_kstat *ksmbd_kstat)
{
	struct file_directory_info *info = (struct file_directory_info *)(*p);
	struct kstat *kstat = ksmbd_kstat->kstat;
	u64 time;

	info->FileIndex = 0;
	info->CreationTime = cpu_to_le64(ksmbd_kstat->create_time);
	time = ksmbd_UnixTimeToNT(kstat->atime);
	info->LastAccessTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(kstat->mtime);
	info->LastWriteTime = cpu_to_le64(time);
	time = ksmbd_UnixTimeToNT(kstat->ctime);
	info->ChangeTime = cpu_to_le64(time);

	if (ksmbd_kstat->file_attributes & FILE_ATTRIBUTE_DIRECTORY_LE) {
		info->EndOfFile = 0;
		info->AllocationSize = 0;
	} else {
		info->EndOfFile = cpu_to_le64(kstat->size);
		info->AllocationSize = cpu_to_le64(kstat->blocks << 9);
	}
	info->ExtFileAttributes = ksmbd_kstat->file_attributes;

	return info;
}

int ksmbd_vfs_fill_dentry_attrs(struct ksmbd_work *work,
				struct mnt_idmap *idmap,
				struct dentry *dentry,
				struct ksmbd_kstat *ksmbd_kstat)
{
	struct ksmbd_share_config *share_conf = work->tcon->share_conf;
	u64 time;
	int rc;
	struct path path = {
		.mnt = share_conf->vfs_path.mnt,
		.dentry = dentry,
	};

	rc = vfs_getattr(&path, ksmbd_kstat->kstat,
			 STATX_BASIC_STATS | STATX_BTIME,
			 AT_STATX_SYNC_AS_STAT);
	if (rc)
		return rc;

	time = ksmbd_UnixTimeToNT(ksmbd_kstat->kstat->ctime);
	ksmbd_kstat->create_time = time;

	/*
	 * set default value for the case that store dos attributes is not yes
	 * or that acl is disable in server's filesystem and the config is yes.
	 */
	if (S_ISDIR(ksmbd_kstat->kstat->mode))
		ksmbd_kstat->file_attributes = FILE_ATTRIBUTE_DIRECTORY_LE;
	else
		ksmbd_kstat->file_attributes = FILE_ATTRIBUTE_ARCHIVE_LE;

	if (test_share_config_flag(work->tcon->share_conf,
				   KSMBD_SHARE_FLAG_STORE_DOS_ATTRS)) {
		struct xattr_dos_attrib da;

		rc = ksmbd_vfs_get_dos_attrib_xattr(idmap, dentry, &da);
		if (rc > 0) {
			ksmbd_kstat->file_attributes = cpu_to_le32(da.attr);
			ksmbd_kstat->create_time = da.create_time;
		} else {
			ksmbd_debug(VFS, "fail to load dos attribute.\n");
		}
	}

	return 0;
}

ssize_t ksmbd_vfs_casexattr_len(struct mnt_idmap *idmap,
				struct dentry *dentry, char *attr_name,
				int attr_name_len)
{
	char *name, *xattr_list = NULL;
	ssize_t value_len = -ENOENT, xattr_list_len;

	xattr_list_len = ksmbd_vfs_listxattr(dentry, &xattr_list);
	if (xattr_list_len <= 0)
		goto out;

	for (name = xattr_list; name - xattr_list < xattr_list_len;
			name += strlen(name) + 1) {
		ksmbd_debug(VFS, "%s, len %zd\n", name, strlen(name));
		if (strncasecmp(attr_name, name, attr_name_len))
			continue;

		value_len = ksmbd_vfs_xattr_len(idmap, dentry, name);
		break;
	}

out:
	kvfree(xattr_list);
	return value_len;
}

int ksmbd_vfs_xattr_stream_name(char *stream_name, char **xattr_stream_name,
				size_t *xattr_stream_name_size, int s_type)
{
	char *type, *buf;

	if (s_type == DIR_STREAM)
		type = ":$INDEX_ALLOCATION";
	else
		type = ":$DATA";

	buf = kasprintf(KSMBD_DEFAULT_GFP, "%s%s%s",
			XATTR_NAME_STREAM, stream_name,	type);
	if (!buf)
		return -ENOMEM;

	*xattr_stream_name = buf;
	*xattr_stream_name_size = strlen(buf) + 1;

	return 0;
}

int ksmbd_vfs_copy_file_ranges(struct ksmbd_work *work,
			       struct ksmbd_file *src_fp,
			       struct ksmbd_file *dst_fp,
			       struct srv_copychunk *chunks,
			       unsigned int chunk_count,
			       unsigned int *chunk_count_written,
			       unsigned int *chunk_size_written,
			       loff_t *total_size_written)
{
	unsigned int i;
	loff_t src_off, dst_off, src_file_size;
	size_t len;
	int ret;

	*chunk_count_written = 0;
	*chunk_size_written = 0;
	*total_size_written = 0;

	if (!(src_fp->daccess & (FILE_READ_DATA_LE | FILE_EXECUTE_LE))) {
		pr_err("no right to read(%pD)\n", src_fp->filp);
		return -EACCES;
	}
	if (!(dst_fp->daccess & (FILE_WRITE_DATA_LE | FILE_APPEND_DATA_LE))) {
		pr_err("no right to write(%pD)\n", dst_fp->filp);
		return -EACCES;
	}

	if (ksmbd_stream_fd(src_fp) || ksmbd_stream_fd(dst_fp))
		return -EBADF;

	smb_break_all_levII_oplock(work, dst_fp, 1);

	if (!work->tcon->posix_extensions) {
		for (i = 0; i < chunk_count; i++) {
			src_off = le64_to_cpu(chunks[i].SourceOffset);
			dst_off = le64_to_cpu(chunks[i].TargetOffset);
			len = le32_to_cpu(chunks[i].Length);

			if (check_lock_range(src_fp->filp, src_off,
					     src_off + len - 1, READ))
				return -EAGAIN;
			if (check_lock_range(dst_fp->filp, dst_off,
					     dst_off + len - 1, WRITE))
				return -EAGAIN;
		}
	}

	src_file_size = i_size_read(file_inode(src_fp->filp));

	for (i = 0; i < chunk_count; i++) {
		src_off = le64_to_cpu(chunks[i].SourceOffset);
		dst_off = le64_to_cpu(chunks[i].TargetOffset);
		len = le32_to_cpu(chunks[i].Length);

		if (src_off + len > src_file_size)
			return -E2BIG;

		ret = vfs_copy_file_range(src_fp->filp, src_off,
					  dst_fp->filp, dst_off, len, 0);
		if (ret == -EOPNOTSUPP || ret == -EXDEV)
			ret = vfs_copy_file_range(src_fp->filp, src_off,
						  dst_fp->filp, dst_off, len,
						  COPY_FILE_SPLICE);
		if (ret < 0)
			return ret;

		*chunk_count_written += 1;
		*total_size_written += ret;
	}
	return 0;
}

void ksmbd_vfs_posix_lock_wait(struct file_lock *flock)
{
	wait_event(flock->c.flc_wait, !flock->c.flc_blocker);
}

void ksmbd_vfs_posix_lock_unblock(struct file_lock *flock)
{
	locks_delete_block(flock);
}

int ksmbd_vfs_set_init_posix_acl(struct mnt_idmap *idmap,
				 struct path *path)
{
	struct posix_acl_state acl_state;
	struct posix_acl *acls;
	struct dentry *dentry = path->dentry;
	struct inode *inode = d_inode(dentry);
	int rc;

	if (!IS_ENABLED(CONFIG_FS_POSIX_ACL))
		return -EOPNOTSUPP;

	ksmbd_debug(SMB, "Set posix acls\n");
	rc = init_acl_state(&acl_state, 1);
	if (rc)
		return rc;

	/* Set default owner group */
	acl_state.owner.allow = (inode->i_mode & 0700) >> 6;
	acl_state.group.allow = (inode->i_mode & 0070) >> 3;
	acl_state.other.allow = inode->i_mode & 0007;
	acl_state.users->aces[acl_state.users->n].uid = inode->i_uid;
	acl_state.users->aces[acl_state.users->n++].perms.allow =
		acl_state.owner.allow;
	acl_state.groups->aces[acl_state.groups->n].gid = inode->i_gid;
	acl_state.groups->aces[acl_state.groups->n++].perms.allow =
		acl_state.group.allow;
	acl_state.mask.allow = 0x07;

	acls = posix_acl_alloc(6, KSMBD_DEFAULT_GFP);
	if (!acls) {
		free_acl_state(&acl_state);
		return -ENOMEM;
	}
	posix_state_to_acl(&acl_state, acls->a_entries);

	rc = set_posix_acl(idmap, dentry, ACL_TYPE_ACCESS, acls);
	if (rc < 0)
		ksmbd_debug(SMB, "Set posix acl(ACL_TYPE_ACCESS) failed, rc : %d\n",
			    rc);
	else if (S_ISDIR(inode->i_mode)) {
		posix_state_to_acl(&acl_state, acls->a_entries);
		rc = set_posix_acl(idmap, dentry, ACL_TYPE_DEFAULT, acls);
		if (rc < 0)
			ksmbd_debug(SMB, "Set posix acl(ACL_TYPE_DEFAULT) failed, rc : %d\n",
				    rc);
	}

	free_acl_state(&acl_state);
	posix_acl_release(acls);
	return rc;
}

int ksmbd_vfs_inherit_posix_acl(struct mnt_idmap *idmap,
				struct path *path, struct inode *parent_inode)
{
	struct posix_acl *acls;
	struct posix_acl_entry *pace;
	struct dentry *dentry = path->dentry;
	struct inode *inode = d_inode(dentry);
	int rc, i;

	if (!IS_ENABLED(CONFIG_FS_POSIX_ACL))
		return -EOPNOTSUPP;

	acls = get_inode_acl(parent_inode, ACL_TYPE_DEFAULT);
	if (IS_ERR_OR_NULL(acls))
		return -ENOENT;
	pace = acls->a_entries;

	for (i = 0; i < acls->a_count; i++, pace++) {
		if (pace->e_tag == ACL_MASK) {
			pace->e_perm = 0x07;
			break;
		}
	}

	rc = set_posix_acl(idmap, dentry, ACL_TYPE_ACCESS, acls);
	if (rc < 0)
		ksmbd_debug(SMB, "Set posix acl(ACL_TYPE_ACCESS) failed, rc : %d\n",
			    rc);
	if (S_ISDIR(inode->i_mode)) {
		rc = set_posix_acl(idmap, dentry, ACL_TYPE_DEFAULT,
				   acls);
		if (rc < 0)
			ksmbd_debug(SMB, "Set posix acl(ACL_TYPE_DEFAULT) failed, rc : %d\n",
				    rc);
	}

	posix_acl_release(acls);
	return rc;
}
