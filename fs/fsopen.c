// SPDX-License-Identifier: GPL-2.0-or-later
/* Filesystem access-by-fd.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/anon_inodes.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <uapi/linux/mount.h>
#include "internal.h"
#include "mount.h"

/*
 * Allow the user to read back any error, warning or informational messages.
 */
static ssize_t fscontext_read(struct file *file,
			      char __user *_buf, size_t len, loff_t *pos)
{
	struct fs_context *fc = file->private_data;
	struct fc_log *log = fc->log.log;
	unsigned int logsize = ARRAY_SIZE(log->buffer);
	ssize_t ret;
	char *p;
	bool need_free;
	int index, n;

	ret = mutex_lock_interruptible(&fc->uapi_mutex);
	if (ret < 0)
		return ret;

	if (log->head == log->tail) {
		mutex_unlock(&fc->uapi_mutex);
		return -ENODATA;
	}

	index = log->tail & (logsize - 1);
	p = log->buffer[index];
	need_free = log->need_free & (1 << index);
	log->buffer[index] = NULL;
	log->need_free &= ~(1 << index);
	log->tail++;
	mutex_unlock(&fc->uapi_mutex);

	ret = -EMSGSIZE;
	n = strlen(p);
	if (n > len)
		goto err_free;
	ret = -EFAULT;
	if (copy_to_user(_buf, p, n) != 0)
		goto err_free;
	ret = n;

err_free:
	if (need_free)
		kfree(p);
	return ret;
}

static int fscontext_release(struct inode *inode, struct file *file)
{
	struct fs_context *fc = file->private_data;

	if (fc) {
		file->private_data = NULL;
		put_fs_context(fc);
	}
	return 0;
}

const struct file_operations fscontext_fops = {
	.read		= fscontext_read,
	.release	= fscontext_release,
	.llseek		= no_llseek,
};

/*
 * Attach a filesystem context to a file and an fd.
 */
static int fscontext_create_fd(struct fs_context *fc, unsigned int o_flags)
{
	int fd;

	fd = anon_inode_getfd("[fscontext]", &fscontext_fops, fc,
			      O_RDWR | o_flags);
	if (fd < 0)
		put_fs_context(fc);
	return fd;
}

static int fscontext_alloc_log(struct fs_context *fc)
{
	fc->log.log = kzalloc(sizeof(*fc->log.log), GFP_KERNEL);
	if (!fc->log.log)
		return -ENOMEM;
	refcount_set(&fc->log.log->usage, 1);
	fc->log.log->owner = fc->fs_type->owner;
	return 0;
}

/*
 * Open a filesystem by name so that it can be configured for mounting.
 *
 * We are allowed to specify a container in which the filesystem will be
 * opened, thereby indicating which namespaces will be used (notably, which
 * network namespace will be used for network filesystems).
 */
SYSCALL_DEFINE2(fsopen, const char __user *, _fs_name, unsigned int, flags)
{
	struct file_system_type *fs_type;
	struct fs_context *fc;
	const char *fs_name;
	int ret;

	if (!may_mount())
		return -EPERM;

	if (flags & ~FSOPEN_CLOEXEC)
		return -EINVAL;

	fs_name = strndup_user(_fs_name, PAGE_SIZE);
	if (IS_ERR(fs_name))
		return PTR_ERR(fs_name);

	fs_type = get_fs_type(fs_name);
	kfree(fs_name);
	if (!fs_type)
		return -ENODEV;

	fc = fs_context_for_mount(fs_type, 0);
	put_filesystem(fs_type);
	if (IS_ERR(fc))
		return PTR_ERR(fc);

	fc->phase = FS_CONTEXT_CREATE_PARAMS;

	ret = fscontext_alloc_log(fc);
	if (ret < 0)
		goto err_fc;

	return fscontext_create_fd(fc, flags & FSOPEN_CLOEXEC ? O_CLOEXEC : 0);

err_fc:
	put_fs_context(fc);
	return ret;
}

/*
 * Pick a superblock into a context for reconfiguration.
 */
SYSCALL_DEFINE3(fspick, int, dfd, const char __user *, path, unsigned int, flags)
{
	struct fs_context *fc;
	struct path target;
	unsigned int lookup_flags;
	int ret;

	if (!may_mount())
		return -EPERM;

	if ((flags & ~(FSPICK_CLOEXEC |
		       FSPICK_SYMLINK_NOFOLLOW |
		       FSPICK_NO_AUTOMOUNT |
		       FSPICK_EMPTY_PATH)) != 0)
		return -EINVAL;

	lookup_flags = LOOKUP_FOLLOW | LOOKUP_AUTOMOUNT;
	if (flags & FSPICK_SYMLINK_NOFOLLOW)
		lookup_flags &= ~LOOKUP_FOLLOW;
	if (flags & FSPICK_NO_AUTOMOUNT)
		lookup_flags &= ~LOOKUP_AUTOMOUNT;
	if (flags & FSPICK_EMPTY_PATH)
		lookup_flags |= LOOKUP_EMPTY;
	ret = user_path_at(dfd, path, lookup_flags, &target);
	if (ret < 0)
		goto err;

	ret = -EINVAL;
	if (target.mnt->mnt_root != target.dentry)
		goto err_path;

	fc = fs_context_for_reconfigure(target.dentry, 0, 0);
	if (IS_ERR(fc)) {
		ret = PTR_ERR(fc);
		goto err_path;
	}

	fc->phase = FS_CONTEXT_RECONF_PARAMS;

	ret = fscontext_alloc_log(fc);
	if (ret < 0)
		goto err_fc;

	path_put(&target);
	return fscontext_create_fd(fc, flags & FSPICK_CLOEXEC ? O_CLOEXEC : 0);

err_fc:
	put_fs_context(fc);
err_path:
	path_put(&target);
err:
	return ret;
}

static int vfs_cmd_create(struct fs_context *fc)
{
	struct super_block *sb;
	int ret;

	if (fc->phase != FS_CONTEXT_CREATE_PARAMS)
		return -EBUSY;

	if (!mount_capable(fc))
		return -EPERM;

	fc->phase = FS_CONTEXT_CREATING;

	ret = vfs_get_tree(fc);
	if (ret) {
		fc->phase = FS_CONTEXT_FAILED;
		return ret;
	}

	sb = fc->root->d_sb;
	ret = security_sb_kern_mount(sb);
	if (unlikely(ret)) {
		fc_drop_locked(fc);
		fc->phase = FS_CONTEXT_FAILED;
		return ret;
	}

	/* vfs_get_tree() callchains will have grabbed @s_umount */
	up_write(&sb->s_umount);
	fc->phase = FS_CONTEXT_AWAITING_MOUNT;
	return 0;
}

static int vfs_cmd_reconfigure(struct fs_context *fc)
{
	struct super_block *sb;
	int ret;

	if (fc->phase != FS_CONTEXT_RECONF_PARAMS)
		return -EBUSY;

	fc->phase = FS_CONTEXT_RECONFIGURING;

	sb = fc->root->d_sb;
	if (!ns_capable(sb->s_user_ns, CAP_SYS_ADMIN)) {
		fc->phase = FS_CONTEXT_FAILED;
		return -EPERM;
	}

	down_write(&sb->s_umount);
	ret = reconfigure_super(fc);
	up_write(&sb->s_umount);
	if (ret) {
		fc->phase = FS_CONTEXT_FAILED;
		return ret;
	}

	vfs_clean_context(fc);
	return 0;
}

/*
 * Check the state and apply the configuration.  Note that this function is
 * allowed to 'steal' the value by setting param->xxx to NULL before returning.
 */
static int vfs_fsconfig_locked(struct fs_context *fc, int cmd,
			       struct fs_parameter *param)
{
	int ret;

	ret = finish_clean_context(fc);
	if (ret)
		return ret;
	switch (cmd) {
	case FSCONFIG_CMD_CREATE:
		return vfs_cmd_create(fc);
	case FSCONFIG_CMD_RECONFIGURE:
		return vfs_cmd_reconfigure(fc);
	default:
		if (fc->phase != FS_CONTEXT_CREATE_PARAMS &&
		    fc->phase != FS_CONTEXT_RECONF_PARAMS)
			return -EBUSY;

		return vfs_parse_fs_param(fc, param);
	}
}

/**
 * sys_fsconfig - Set parameters and trigger actions on a context
 * @fd: The filesystem context to act upon
 * @cmd: The action to take
 * @_key: Where appropriate, the parameter key to set
 * @_value: Where appropriate, the parameter value to set
 * @aux: Additional information for the value
 *
 * This system call is used to set parameters on a context, including
 * superblock settings, data source and security labelling.
 *
 * Actions include triggering the creation of a superblock and the
 * reconfiguration of the superblock attached to the specified context.
 *
 * When setting a parameter, @cmd indicates the type of value being proposed
 * and @_key indicates the parameter to be altered.
 *
 * @_value and @aux are used to specify the value, should a value be required:
 *
 * (*) fsconfig_set_flag: No value is specified.  The parameter must be boolean
 *     in nature.  The key may be prefixed with "no" to invert the
 *     setting. @_value must be NULL and @aux must be 0.
 *
 * (*) fsconfig_set_string: A string value is specified.  The parameter can be
 *     expecting boolean, integer, string or take a path.  A conversion to an
 *     appropriate type will be attempted (which may include looking up as a
 *     path).  @_value points to a NUL-terminated string and @aux must be 0.
 *
 * (*) fsconfig_set_binary: A binary blob is specified.  @_value points to the
 *     blob and @aux indicates its size.  The parameter must be expecting a
 *     blob.
 *
 * (*) fsconfig_set_path: A non-empty path is specified.  The parameter must be
 *     expecting a path object.  @_value points to a NUL-terminated string that
 *     is the path and @aux is a file descriptor at which to start a relative
 *     lookup or AT_FDCWD.
 *
 * (*) fsconfig_set_path_empty: As fsconfig_set_path, but with AT_EMPTY_PATH
 *     implied.
 *
 * (*) fsconfig_set_fd: An open file descriptor is specified.  @_value must be
 *     NULL and @aux indicates the file descriptor.
 */
SYSCALL_DEFINE5(fsconfig,
		int, fd,
		unsigned int, cmd,
		const char __user *, _key,
		const void __user *, _value,
		int, aux)
{
	struct fs_context *fc;
	struct fd f;
	int ret;
	int lookup_flags = 0;

	struct fs_parameter param = {
		.type	= fs_value_is_undefined,
	};

	if (fd < 0)
		return -EINVAL;

	switch (cmd) {
	case FSCONFIG_SET_FLAG:
		if (!_key || _value || aux)
			return -EINVAL;
		break;
	case FSCONFIG_SET_STRING:
		if (!_key || !_value || aux)
			return -EINVAL;
		break;
	case FSCONFIG_SET_BINARY:
		if (!_key || !_value || aux <= 0 || aux > 1024 * 1024)
			return -EINVAL;
		break;
	case FSCONFIG_SET_PATH:
	case FSCONFIG_SET_PATH_EMPTY:
		if (!_key || !_value || (aux != AT_FDCWD && aux < 0))
			return -EINVAL;
		break;
	case FSCONFIG_SET_FD:
		if (!_key || _value || aux < 0)
			return -EINVAL;
		break;
	case FSCONFIG_CMD_CREATE:
	case FSCONFIG_CMD_RECONFIGURE:
		if (_key || _value || aux)
			return -EINVAL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	f = fdget(fd);
	if (!f.file)
		return -EBADF;
	ret = -EINVAL;
	if (f.file->f_op != &fscontext_fops)
		goto out_f;

	fc = f.file->private_data;
	if (fc->ops == &legacy_fs_context_ops) {
		switch (cmd) {
		case FSCONFIG_SET_BINARY:
		case FSCONFIG_SET_PATH:
		case FSCONFIG_SET_PATH_EMPTY:
		case FSCONFIG_SET_FD:
			ret = -EOPNOTSUPP;
			goto out_f;
		}
	}

	if (_key) {
		param.key = strndup_user(_key, 256);
		if (IS_ERR(param.key)) {
			ret = PTR_ERR(param.key);
			goto out_f;
		}
	}

	switch (cmd) {
	case FSCONFIG_SET_FLAG:
		param.type = fs_value_is_flag;
		break;
	case FSCONFIG_SET_STRING:
		param.type = fs_value_is_string;
		param.string = strndup_user(_value, 256);
		if (IS_ERR(param.string)) {
			ret = PTR_ERR(param.string);
			goto out_key;
		}
		param.size = strlen(param.string);
		break;
	case FSCONFIG_SET_BINARY:
		param.type = fs_value_is_blob;
		param.size = aux;
		param.blob = memdup_user_nul(_value, aux);
		if (IS_ERR(param.blob)) {
			ret = PTR_ERR(param.blob);
			goto out_key;
		}
		break;
	case FSCONFIG_SET_PATH_EMPTY:
		lookup_flags = LOOKUP_EMPTY;
		fallthrough;
	case FSCONFIG_SET_PATH:
		param.type = fs_value_is_filename;
		param.name = getname_flags(_value, lookup_flags, NULL);
		if (IS_ERR(param.name)) {
			ret = PTR_ERR(param.name);
			goto out_key;
		}
		param.dirfd = aux;
		param.size = strlen(param.name->name);
		break;
	case FSCONFIG_SET_FD:
		param.type = fs_value_is_file;
		ret = -EBADF;
		param.file = fget(aux);
		if (!param.file)
			goto out_key;
		break;
	default:
		break;
	}

	ret = mutex_lock_interruptible(&fc->uapi_mutex);
	if (ret == 0) {
		ret = vfs_fsconfig_locked(fc, cmd, &param);
		mutex_unlock(&fc->uapi_mutex);
	}

	/* Clean up the our record of any value that we obtained from
	 * userspace.  Note that the value may have been stolen by the LSM or
	 * filesystem, in which case the value pointer will have been cleared.
	 */
	switch (cmd) {
	case FSCONFIG_SET_STRING:
	case FSCONFIG_SET_BINARY:
		kfree(param.string);
		break;
	case FSCONFIG_SET_PATH:
	case FSCONFIG_SET_PATH_EMPTY:
		if (param.name)
			putname(param.name);
		break;
	case FSCONFIG_SET_FD:
		if (param.file)
			fput(param.file);
		break;
	default:
		break;
	}
out_key:
	kfree(param.key);
out_f:
	fdput(f);
	return ret;
}
