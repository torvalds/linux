/*
 * bin.c - binary file operations for sysfs.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Matthew Wilcox
 * Copyright (c) 2004 Silicon Graphics, Inc.
 */

#undef DEBUG

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/uaccess.h>
#include <asm/semaphore.h>

#include "sysfs.h"

struct bin_buffer {
	struct mutex	mutex;
	void		*buffer;
	int		mmapped;
};

static int
fill_read(struct dentry *dentry, char *buffer, loff_t off, size_t count)
{
	struct sysfs_dirent *attr_sd = dentry->d_fsdata;
	struct bin_attribute *attr = attr_sd->s_elem.bin_attr.bin_attr;
	struct kobject *kobj = attr_sd->s_parent->s_elem.dir.kobj;
	int rc;

	/* need attr_sd for attr, its parent for kobj */
	if (!sysfs_get_active_two(attr_sd))
		return -ENODEV;

	rc = -EIO;
	if (attr->read)
		rc = attr->read(kobj, attr, buffer, off, count);

	sysfs_put_active_two(attr_sd);

	return rc;
}

static ssize_t
read(struct file *file, char __user *userbuf, size_t bytes, loff_t *off)
{
	struct bin_buffer *bb = file->private_data;
	struct dentry *dentry = file->f_path.dentry;
	int size = dentry->d_inode->i_size;
	loff_t offs = *off;
	int count = min_t(size_t, bytes, PAGE_SIZE);

	if (size) {
		if (offs > size)
			return 0;
		if (offs + count > size)
			count = size - offs;
	}

	mutex_lock(&bb->mutex);

	count = fill_read(dentry, bb->buffer, offs, count);
	if (count < 0)
		goto out_unlock;

	if (copy_to_user(userbuf, bb->buffer, count)) {
		count = -EFAULT;
		goto out_unlock;
	}

	pr_debug("offs = %lld, *off = %lld, count = %d\n", offs, *off, count);

	*off = offs + count;

 out_unlock:
	mutex_unlock(&bb->mutex);
	return count;
}

static int
flush_write(struct dentry *dentry, char *buffer, loff_t offset, size_t count)
{
	struct sysfs_dirent *attr_sd = dentry->d_fsdata;
	struct bin_attribute *attr = attr_sd->s_elem.bin_attr.bin_attr;
	struct kobject *kobj = attr_sd->s_parent->s_elem.dir.kobj;
	int rc;

	/* need attr_sd for attr, its parent for kobj */
	if (!sysfs_get_active_two(attr_sd))
		return -ENODEV;

	rc = -EIO;
	if (attr->write)
		rc = attr->write(kobj, attr, buffer, offset, count);

	sysfs_put_active_two(attr_sd);

	return rc;
}

static ssize_t write(struct file *file, const char __user *userbuf,
		     size_t bytes, loff_t *off)
{
	struct bin_buffer *bb = file->private_data;
	struct dentry *dentry = file->f_path.dentry;
	int size = dentry->d_inode->i_size;
	loff_t offs = *off;
	int count = min_t(size_t, bytes, PAGE_SIZE);

	if (size) {
		if (offs > size)
			return 0;
		if (offs + count > size)
			count = size - offs;
	}

	mutex_lock(&bb->mutex);

	if (copy_from_user(bb->buffer, userbuf, count)) {
		count = -EFAULT;
		goto out_unlock;
	}

	count = flush_write(dentry, bb->buffer, offs, count);
	if (count > 0)
		*off = offs + count;

 out_unlock:
	mutex_unlock(&bb->mutex);
	return count;
}

static int mmap(struct file *file, struct vm_area_struct *vma)
{
	struct bin_buffer *bb = file->private_data;
	struct sysfs_dirent *attr_sd = file->f_path.dentry->d_fsdata;
	struct bin_attribute *attr = attr_sd->s_elem.bin_attr.bin_attr;
	struct kobject *kobj = attr_sd->s_parent->s_elem.dir.kobj;
	int rc;

	mutex_lock(&bb->mutex);

	/* need attr_sd for attr, its parent for kobj */
	if (!sysfs_get_active_two(attr_sd))
		return -ENODEV;

	rc = -EINVAL;
	if (attr->mmap)
		rc = attr->mmap(kobj, attr, vma);

	if (rc == 0 && !bb->mmapped)
		bb->mmapped = 1;
	else
		sysfs_put_active_two(attr_sd);

	mutex_unlock(&bb->mutex);

	return rc;
}

static int open(struct inode * inode, struct file * file)
{
	struct sysfs_dirent *attr_sd = file->f_path.dentry->d_fsdata;
	struct bin_attribute *attr = attr_sd->s_elem.bin_attr.bin_attr;
	struct bin_buffer *bb = NULL;
	int error;

	/* need attr_sd for attr */
	if (!sysfs_get_active(attr_sd))
		return -ENODEV;

	error = -EACCES;
	if ((file->f_mode & FMODE_WRITE) && !(attr->write || attr->mmap))
		goto err_out;
	if ((file->f_mode & FMODE_READ) && !(attr->read || attr->mmap))
		goto err_out;

	error = -ENOMEM;
	bb = kzalloc(sizeof(*bb), GFP_KERNEL);
	if (!bb)
		goto err_out;

	bb->buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!bb->buffer)
		goto err_out;

	mutex_init(&bb->mutex);
	file->private_data = bb;

	/* open succeeded, put active reference and pin attr_sd */
	sysfs_put_active(attr_sd);
	sysfs_get(attr_sd);
	return 0;

 err_out:
	sysfs_put_active(attr_sd);
	kfree(bb);
	return error;
}

static int release(struct inode * inode, struct file * file)
{
	struct sysfs_dirent *attr_sd = file->f_path.dentry->d_fsdata;
	struct bin_buffer *bb = file->private_data;

	if (bb->mmapped)
		sysfs_put_active_two(attr_sd);
	sysfs_put(attr_sd);
	kfree(bb->buffer);
	kfree(bb);
	return 0;
}

const struct file_operations bin_fops = {
	.read		= read,
	.write		= write,
	.mmap		= mmap,
	.llseek		= generic_file_llseek,
	.open		= open,
	.release	= release,
};

/**
 *	sysfs_create_bin_file - create binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 */

int sysfs_create_bin_file(struct kobject * kobj, struct bin_attribute * attr)
{
	BUG_ON(!kobj || !kobj->sd || !attr);

	return sysfs_add_file(kobj->sd, &attr->attr, SYSFS_KOBJ_BIN_ATTR);
}


/**
 *	sysfs_remove_bin_file - remove binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 */

void sysfs_remove_bin_file(struct kobject * kobj, struct bin_attribute * attr)
{
	if (sysfs_hash_and_remove(kobj->sd, attr->attr.name) < 0) {
		printk(KERN_ERR "%s: "
			"bad dentry or inode or no such file: \"%s\"\n",
			__FUNCTION__, attr->attr.name);
		dump_stack();
	}
}

EXPORT_SYMBOL_GPL(sysfs_create_bin_file);
EXPORT_SYMBOL_GPL(sysfs_remove_bin_file);
