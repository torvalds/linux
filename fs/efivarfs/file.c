// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@canonical.com>
 */

#include <linux/efi.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mount.h>

#include "internal.h"

static ssize_t efivarfs_file_write(struct file *file,
		const char __user *userbuf, size_t count, loff_t *ppos)
{
	struct efivar_entry *var = file->private_data;
	void *data;
	u32 attributes;
	struct inode *inode = file->f_mapping->host;
	unsigned long datasize = count - sizeof(attributes);
	ssize_t bytes;
	bool set = false;

	if (count < sizeof(attributes))
		return -EINVAL;

	if (copy_from_user(&attributes, userbuf, sizeof(attributes)))
		return -EFAULT;

	if (attributes & ~(EFI_VARIABLE_MASK))
		return -EINVAL;

	data = memdup_user(userbuf + sizeof(attributes), datasize);
	if (IS_ERR(data))
		return PTR_ERR(data);

	bytes = efivar_entry_set_get_size(var, attributes, &datasize,
					  data, &set);
	if (!set && bytes) {
		if (bytes == -ENOENT)
			bytes = -EIO;
		goto out;
	}

	if (bytes == -ENOENT) {
		drop_nlink(inode);
		d_delete(file->f_path.dentry);
		dput(file->f_path.dentry);
	} else {
		inode_lock(inode);
		i_size_write(inode, datasize + sizeof(attributes));
		inode->i_mtime = current_time(inode);
		inode_unlock(inode);
	}

	bytes = count;

out:
	kfree(data);

	return bytes;
}

static ssize_t efivarfs_file_read(struct file *file, char __user *userbuf,
		size_t count, loff_t *ppos)
{
	struct efivar_entry *var = file->private_data;
	unsigned long datasize = 0;
	u32 attributes;
	void *data;
	ssize_t size = 0;
	int err;

	while (!__ratelimit(&file->f_cred->user->ratelimit))
		msleep(50);

	err = efivar_entry_size(var, &datasize);

	/*
	 * efivarfs represents uncommitted variables with
	 * zero-length files. Reading them should return EOF.
	 */
	if (err == -ENOENT)
		return 0;
	else if (err)
		return err;

	data = kmalloc(datasize + sizeof(attributes), GFP_KERNEL);

	if (!data)
		return -ENOMEM;

	size = efivar_entry_get(var, &attributes, &datasize,
				data + sizeof(attributes));
	if (size)
		goto out_free;

	memcpy(data, &attributes, sizeof(attributes));
	size = simple_read_from_buffer(userbuf, count, ppos,
				       data, datasize + sizeof(attributes));
out_free:
	kfree(data);

	return size;
}

static inline unsigned int efivarfs_getflags(struct inode *inode)
{
	unsigned int i_flags;
	unsigned int flags = 0;

	i_flags = inode->i_flags;
	if (i_flags & S_IMMUTABLE)
		flags |= FS_IMMUTABLE_FL;
	return flags;
}

static int
efivarfs_ioc_getxflags(struct file *file, void __user *arg)
{
	struct inode *inode = file->f_mapping->host;
	unsigned int flags = efivarfs_getflags(inode);

	if (copy_to_user(arg, &flags, sizeof(flags)))
		return -EFAULT;
	return 0;
}

static int
efivarfs_ioc_setxflags(struct file *file, void __user *arg)
{
	struct inode *inode = file->f_mapping->host;
	unsigned int flags;
	unsigned int i_flags = 0;
	unsigned int oldflags = efivarfs_getflags(inode);
	int error;

	if (!inode_owner_or_capable(&init_user_ns, inode))
		return -EACCES;

	if (copy_from_user(&flags, arg, sizeof(flags)))
		return -EFAULT;

	if (flags & ~FS_IMMUTABLE_FL)
		return -EOPNOTSUPP;

	if (flags & FS_IMMUTABLE_FL)
		i_flags |= S_IMMUTABLE;


	error = mnt_want_write_file(file);
	if (error)
		return error;

	inode_lock(inode);

	error = vfs_ioc_setflags_prepare(inode, oldflags, flags);
	if (error)
		goto out;

	inode_set_flags(inode, i_flags, S_IMMUTABLE);
out:
	inode_unlock(inode);
	mnt_drop_write_file(file);
	return error;
}

static long
efivarfs_file_ioctl(struct file *file, unsigned int cmd, unsigned long p)
{
	void __user *arg = (void __user *)p;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		return efivarfs_ioc_getxflags(file, arg);
	case FS_IOC_SETFLAGS:
		return efivarfs_ioc_setxflags(file, arg);
	}

	return -ENOTTY;
}

const struct file_operations efivarfs_file_operations = {
	.open	= simple_open,
	.read	= efivarfs_file_read,
	.write	= efivarfs_file_write,
	.llseek	= no_llseek,
	.unlocked_ioctl = efivarfs_file_ioctl,
};
