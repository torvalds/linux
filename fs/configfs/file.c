/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file.c - operations for regular (text) files.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>

#include <linux/configfs.h>
#include "configfs_internal.h"


struct configfs_buffer {
	size_t			count;
	loff_t			pos;
	char			* page;
	struct configfs_item_operations	* ops;
	struct semaphore	sem;
	int			needs_read_fill;
};


/**
 *	fill_read_buffer - allocate and fill buffer from item.
 *	@dentry:	dentry pointer.
 *	@buffer:	data buffer for file.
 *
 *	Allocate @buffer->page, if it hasn't been already, then call the
 *	config_item's show() method to fill the buffer with this attribute's
 *	data.
 *	This is called only once, on the file's first read.
 */
static int fill_read_buffer(struct dentry * dentry, struct configfs_buffer * buffer)
{
	struct configfs_attribute * attr = to_attr(dentry);
	struct config_item * item = to_item(dentry->d_parent);
	struct configfs_item_operations * ops = buffer->ops;
	int ret = 0;
	ssize_t count;

	if (!buffer->page)
		buffer->page = (char *) get_zeroed_page(GFP_KERNEL);
	if (!buffer->page)
		return -ENOMEM;

	count = ops->show_attribute(item,attr,buffer->page);
	buffer->needs_read_fill = 0;
	BUG_ON(count > (ssize_t)PAGE_SIZE);
	if (count >= 0)
		buffer->count = count;
	else
		ret = count;
	return ret;
}


/**
 *	flush_read_buffer - push buffer to userspace.
 *	@buffer:	data buffer for file.
 *	@userbuf:	user-passed buffer.
 *	@count:		number of bytes requested.
 *	@ppos:		file position.
 *
 *	Copy the buffer we filled in fill_read_buffer() to userspace.
 *	This is done at the reader's leisure, copying and advancing
 *	the amount they specify each time.
 *	This may be called continuously until the buffer is empty.
 */
static int flush_read_buffer(struct configfs_buffer * buffer, char __user * buf,
			     size_t count, loff_t * ppos)
{
	int error;

	if (*ppos > buffer->count)
		return 0;

	if (count > (buffer->count - *ppos))
		count = buffer->count - *ppos;

	error = copy_to_user(buf,buffer->page + *ppos,count);
	if (!error)
		*ppos += count;
	return error ? -EFAULT : count;
}

/**
 *	configfs_read_file - read an attribute.
 *	@file:	file pointer.
 *	@buf:	buffer to fill.
 *	@count:	number of bytes to read.
 *	@ppos:	starting offset in file.
 *
 *	Userspace wants to read an attribute file. The attribute descriptor
 *	is in the file's ->d_fsdata. The target item is in the directory's
 *	->d_fsdata.
 *
 *	We call fill_read_buffer() to allocate and fill the buffer from the
 *	item's show() method exactly once (if the read is happening from
 *	the beginning of the file). That should fill the entire buffer with
 *	all the data the item has to offer for that attribute.
 *	We then call flush_read_buffer() to copy the buffer to userspace
 *	in the increments specified.
 */

static ssize_t
configfs_read_file(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct configfs_buffer * buffer = file->private_data;
	ssize_t retval = 0;

	down(&buffer->sem);
	if (buffer->needs_read_fill) {
		if ((retval = fill_read_buffer(file->f_path.dentry,buffer)))
			goto out;
	}
	pr_debug("%s: count = %zd, ppos = %lld, buf = %s\n",
		 __FUNCTION__, count, *ppos, buffer->page);
	retval = flush_read_buffer(buffer,buf,count,ppos);
out:
	up(&buffer->sem);
	return retval;
}


/**
 *	fill_write_buffer - copy buffer from userspace.
 *	@buffer:	data buffer for file.
 *	@buf:		data from user.
 *	@count:		number of bytes in @userbuf.
 *
 *	Allocate @buffer->page if it hasn't been already, then
 *	copy the user-supplied buffer into it.
 */

static int
fill_write_buffer(struct configfs_buffer * buffer, const char __user * buf, size_t count)
{
	int error;

	if (!buffer->page)
		buffer->page = (char *)__get_free_pages(GFP_KERNEL, 0);
	if (!buffer->page)
		return -ENOMEM;

	if (count >= PAGE_SIZE)
		count = PAGE_SIZE - 1;
	error = copy_from_user(buffer->page,buf,count);
	buffer->needs_read_fill = 1;
	/* if buf is assumed to contain a string, terminate it by \0,
	 * so e.g. sscanf() can scan the string easily */
	buffer->page[count] = 0;
	return error ? -EFAULT : count;
}


/**
 *	flush_write_buffer - push buffer to config_item.
 *	@dentry:	dentry to the attribute
 *	@buffer:	data buffer for file.
 *	@count:		number of bytes
 *
 *	Get the correct pointers for the config_item and the attribute we're
 *	dealing with, then call the store() method for the attribute,
 *	passing the buffer that we acquired in fill_write_buffer().
 */

static int
flush_write_buffer(struct dentry * dentry, struct configfs_buffer * buffer, size_t count)
{
	struct configfs_attribute * attr = to_attr(dentry);
	struct config_item * item = to_item(dentry->d_parent);
	struct configfs_item_operations * ops = buffer->ops;

	return ops->store_attribute(item,attr,buffer->page,count);
}


/**
 *	configfs_write_file - write an attribute.
 *	@file:	file pointer
 *	@buf:	data to write
 *	@count:	number of bytes
 *	@ppos:	starting offset
 *
 *	Similar to configfs_read_file(), though working in the opposite direction.
 *	We allocate and fill the data from the user in fill_write_buffer(),
 *	then push it to the config_item in flush_write_buffer().
 *	There is no easy way for us to know if userspace is only doing a partial
 *	write, so we don't support them. We expect the entire buffer to come
 *	on the first write.
 *	Hint: if you're writing a value, first read the file, modify only the
 *	the value you're changing, then write entire buffer back.
 */

static ssize_t
configfs_write_file(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct configfs_buffer * buffer = file->private_data;
	ssize_t len;

	down(&buffer->sem);
	len = fill_write_buffer(buffer, buf, count);
	if (len > 0)
		len = flush_write_buffer(file->f_path.dentry, buffer, count);
	if (len > 0)
		*ppos += len;
	up(&buffer->sem);
	return len;
}

static int check_perm(struct inode * inode, struct file * file)
{
	struct config_item *item = configfs_get_config_item(file->f_path.dentry->d_parent);
	struct configfs_attribute * attr = to_attr(file->f_path.dentry);
	struct configfs_buffer * buffer;
	struct configfs_item_operations * ops = NULL;
	int error = 0;

	if (!item || !attr)
		goto Einval;

	/* Grab the module reference for this attribute if we have one */
	if (!try_module_get(attr->ca_owner)) {
		error = -ENODEV;
		goto Done;
	}

	if (item->ci_type)
		ops = item->ci_type->ct_item_ops;
	else
		goto Eaccess;

	/* File needs write support.
	 * The inode's perms must say it's ok,
	 * and we must have a store method.
	 */
	if (file->f_mode & FMODE_WRITE) {

		if (!(inode->i_mode & S_IWUGO) || !ops->store_attribute)
			goto Eaccess;

	}

	/* File needs read support.
	 * The inode's perms must say it's ok, and we there
	 * must be a show method for it.
	 */
	if (file->f_mode & FMODE_READ) {
		if (!(inode->i_mode & S_IRUGO) || !ops->show_attribute)
			goto Eaccess;
	}

	/* No error? Great, allocate a buffer for the file, and store it
	 * it in file->private_data for easy access.
	 */
	buffer = kzalloc(sizeof(struct configfs_buffer),GFP_KERNEL);
	if (!buffer) {
		error = -ENOMEM;
		goto Enomem;
	}
	init_MUTEX(&buffer->sem);
	buffer->needs_read_fill = 1;
	buffer->ops = ops;
	file->private_data = buffer;
	goto Done;

 Einval:
	error = -EINVAL;
	goto Done;
 Eaccess:
	error = -EACCES;
 Enomem:
	module_put(attr->ca_owner);
 Done:
	if (error && item)
		config_item_put(item);
	return error;
}

static int configfs_open_file(struct inode * inode, struct file * filp)
{
	return check_perm(inode,filp);
}

static int configfs_release(struct inode * inode, struct file * filp)
{
	struct config_item * item = to_item(filp->f_path.dentry->d_parent);
	struct configfs_attribute * attr = to_attr(filp->f_path.dentry);
	struct module * owner = attr->ca_owner;
	struct configfs_buffer * buffer = filp->private_data;

	if (item)
		config_item_put(item);
	/* After this point, attr should not be accessed. */
	module_put(owner);

	if (buffer) {
		if (buffer->page)
			free_page((unsigned long)buffer->page);
		kfree(buffer);
	}
	return 0;
}

const struct file_operations configfs_file_operations = {
	.read		= configfs_read_file,
	.write		= configfs_write_file,
	.llseek		= generic_file_llseek,
	.open		= configfs_open_file,
	.release	= configfs_release,
};


int configfs_add_file(struct dentry * dir, const struct configfs_attribute * attr, int type)
{
	struct configfs_dirent * parent_sd = dir->d_fsdata;
	umode_t mode = (attr->ca_mode & S_IALLUGO) | S_IFREG;
	int error = 0;

	mutex_lock(&dir->d_inode->i_mutex);
	error = configfs_make_dirent(parent_sd, NULL, (void *) attr, mode, type);
	mutex_unlock(&dir->d_inode->i_mutex);

	return error;
}


/**
 *	configfs_create_file - create an attribute file for an item.
 *	@item:	item we're creating for.
 *	@attr:	atrribute descriptor.
 */

int configfs_create_file(struct config_item * item, const struct configfs_attribute * attr)
{
	BUG_ON(!item || !item->ci_dentry || !attr);

	return configfs_add_file(item->ci_dentry, attr,
				 CONFIGFS_ITEM_ATTR);
}

