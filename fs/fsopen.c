/* Filesystem access-by-fd.
 *
 * Copyright (C) 2017 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#include <linux/fs_context.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/syscalls.h>
#include <linux/security.h>
#include <linux/anon_inodes.h>
#include <linux/namei.h>
#include <linux/file.h>
#include <uapi/linux/mount.h>
#include "mount.h"

/*
 * Allow the user to read back any error, warning or informational messages.
 */
static ssize_t fscontext_read(struct file *file,
			      char __user *_buf, size_t len, loff_t *pos)
{
	struct fs_context *fc = file->private_data;
	struct fc_log *log = fc->log;
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

	fd = anon_inode_getfd("fscontext", &fscontext_fops, fc,
			      O_RDWR | o_flags);
	if (fd < 0)
		put_fs_context(fc);
	return fd;
}

static int fscontext_alloc_log(struct fs_context *fc)
{
	fc->log = kzalloc(sizeof(*fc->log), GFP_KERNEL);
	if (!fc->log)
		return -ENOMEM;
	refcount_set(&fc->log->usage, 1);
	fc->log->owner = fc->fs_type->owner;
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

	if (!ns_capable(current->nsproxy->mnt_ns->user_ns, CAP_SYS_ADMIN))
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
