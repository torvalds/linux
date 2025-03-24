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

	inode_lock(inode);
	if (var->removed) {
		/*
		 * file got removed; don't allow a set.  Caused by an
		 * unsuccessful create or successful delete write
		 * racing with us.
		 */
		bytes = -EIO;
		goto out;
	}

	bytes = efivar_entry_set_get_size(var, attributes, &datasize,
					  data, &set);
	if (!set) {
		if (bytes == -ENOENT)
			bytes = -EIO;
		goto out;
	}

	if (bytes == -ENOENT) {
		/*
		 * FIXME: temporary workaround for fwupdate, signal
		 * failed write with a 1 to keep created but not
		 * written files
		 */
		i_size_write(inode, 1);
	} else {
		i_size_write(inode, datasize + sizeof(attributes));
		inode_set_mtime_to_ts(inode, inode_set_ctime_current(inode));
	}

	bytes = count;

out:
	inode_unlock(inode);

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

static int efivarfs_file_release(struct inode *inode, struct file *file)
{
	struct efivar_entry *var = inode->i_private;

	inode_lock(inode);
	/* FIXME: temporary work around for fwupdate */
	var->removed = (--var->open_count == 0 && i_size_read(inode) == 1);
	inode_unlock(inode);

	if (var->removed)
		simple_recursive_removal(file->f_path.dentry, NULL);

	return 0;
}

static int efivarfs_file_open(struct inode *inode, struct file *file)
{
	struct efivar_entry *entry = inode->i_private;

	file->private_data = entry;

	inode_lock(inode);
	entry->open_count++;
	inode_unlock(inode);

	return 0;
}

const struct file_operations efivarfs_file_operations = {
	.open		= efivarfs_file_open,
	.read		= efivarfs_file_read,
	.write		= efivarfs_file_write,
	.release	= efivarfs_file_release,
};
