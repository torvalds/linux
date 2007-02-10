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

static int
fill_read(struct dentry *dentry, char *buffer, loff_t off, size_t count)
{
	struct bin_attribute * attr = to_bin_attr(dentry);
	struct kobject * kobj = to_kobj(dentry->d_parent);

	if (!attr->read)
		return -EIO;

	return attr->read(kobj, buffer, off, count);
}

static ssize_t
read(struct file * file, char __user * userbuf, size_t count, loff_t * off)
{
	char *buffer = file->private_data;
	struct dentry *dentry = file->f_path.dentry;
	int size = dentry->d_inode->i_size;
	loff_t offs = *off;
	int ret;

	if (count > PAGE_SIZE)
		count = PAGE_SIZE;

	if (size) {
		if (offs > size)
			return 0;
		if (offs + count > size)
			count = size - offs;
	}

	ret = fill_read(dentry, buffer, offs, count);
	if (ret < 0) 
		return ret;
	count = ret;

	if (copy_to_user(userbuf, buffer, count))
		return -EFAULT;

	pr_debug("offs = %lld, *off = %lld, count = %zd\n", offs, *off, count);

	*off = offs + count;

	return count;
}

static int
flush_write(struct dentry *dentry, char *buffer, loff_t offset, size_t count)
{
	struct bin_attribute *attr = to_bin_attr(dentry);
	struct kobject *kobj = to_kobj(dentry->d_parent);

	if (!attr->write)
		return -EIO;

	return attr->write(kobj, buffer, offset, count);
}

static ssize_t write(struct file * file, const char __user * userbuf,
		     size_t count, loff_t * off)
{
	char *buffer = file->private_data;
	struct dentry *dentry = file->f_path.dentry;
	int size = dentry->d_inode->i_size;
	loff_t offs = *off;

	if (count > PAGE_SIZE)
		count = PAGE_SIZE;
	if (size) {
		if (offs > size)
			return 0;
		if (offs + count > size)
			count = size - offs;
	}

	if (copy_from_user(buffer, userbuf, count))
		return -EFAULT;

	count = flush_write(dentry, buffer, offs, count);
	if (count > 0)
		*off = offs + count;
	return count;
}

static int mmap(struct file *file, struct vm_area_struct *vma)
{
	struct dentry *dentry = file->f_path.dentry;
	struct bin_attribute *attr = to_bin_attr(dentry);
	struct kobject *kobj = to_kobj(dentry->d_parent);

	if (!attr->mmap)
		return -EINVAL;

	return attr->mmap(kobj, attr, vma);
}

static int open(struct inode * inode, struct file * file)
{
	struct kobject *kobj = sysfs_get_kobject(file->f_path.dentry->d_parent);
	struct bin_attribute * attr = to_bin_attr(file->f_path.dentry);
	int error = -EINVAL;

	if (!kobj || !attr)
		goto Done;

	/* Grab the module reference for this attribute if we have one */
	error = -ENODEV;
	if (!try_module_get(attr->attr.owner)) 
		goto Done;

	error = -EACCES;
	if ((file->f_mode & FMODE_WRITE) && !(attr->write || attr->mmap))
		goto Error;
	if ((file->f_mode & FMODE_READ) && !(attr->read || attr->mmap))
		goto Error;

	error = -ENOMEM;
	file->private_data = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!file->private_data)
		goto Error;

	error = 0;
    goto Done;

 Error:
	module_put(attr->attr.owner);
 Done:
	if (error)
		kobject_put(kobj);
	return error;
}

static int release(struct inode * inode, struct file * file)
{
	struct kobject * kobj = to_kobj(file->f_path.dentry->d_parent);
	struct bin_attribute * attr = to_bin_attr(file->f_path.dentry);
	u8 * buffer = file->private_data;

	kobject_put(kobj);
	module_put(attr->attr.owner);
	kfree(buffer);
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
	BUG_ON(!kobj || !kobj->dentry || !attr);

	return sysfs_add_file(kobj->dentry, &attr->attr, SYSFS_KOBJ_BIN_ATTR);
}


/**
 *	sysfs_remove_bin_file - remove binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 */

void sysfs_remove_bin_file(struct kobject * kobj, struct bin_attribute * attr)
{
	if (sysfs_hash_and_remove(kobj->dentry, attr->attr.name) < 0) {
		printk(KERN_ERR "%s: "
			"bad dentry or inode or no such file: \"%s\"\n",
			__FUNCTION__, attr->attr.name);
		dump_stack();
	}
}

EXPORT_SYMBOL_GPL(sysfs_create_bin_file);
EXPORT_SYMBOL_GPL(sysfs_remove_bin_file);
