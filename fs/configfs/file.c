// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * file.c - operations for regular (text) files.
 *
 * Based on sysfs:
 * 	sysfs is Copyright (C) 2001, 2002, 2003 Patrick Mochel
 *
 * configfs Copyright (C) 2005 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

#include <linux/configfs.h>
#include "configfs_internal.h"

/*
 * A simple attribute can only be 4096 characters.  Why 4k?  Because the
 * original code limited it to PAGE_SIZE.  That's a bad idea, though,
 * because an attribute of 16k on ia64 won't work on x86.  So we limit to
 * 4k, our minimum common page size.
 */
#define SIMPLE_ATTR_SIZE 4096

struct configfs_buffer {
	size_t			count;
	loff_t			pos;
	char			* page;
	struct configfs_item_operations	* ops;
	struct mutex		mutex;
	int			needs_read_fill;
	bool			read_in_progress;
	bool			write_in_progress;
	char			*bin_buffer;
	int			bin_buffer_size;
	int			cb_max_size;
	struct config_item	*item;
	struct module		*owner;
	union {
		struct configfs_attribute	*attr;
		struct configfs_bin_attribute	*bin_attr;
	};
};

static inline struct configfs_fragment *to_frag(struct file *file)
{
	struct configfs_dirent *sd = file->f_path.dentry->d_fsdata;

	return sd->s_frag;
}

static int fill_read_buffer(struct file *file, struct configfs_buffer *buffer)
{
	struct configfs_fragment *frag = to_frag(file);
	ssize_t count = -ENOENT;

	if (!buffer->page)
		buffer->page = (char *) get_zeroed_page(GFP_KERNEL);
	if (!buffer->page)
		return -ENOMEM;

	down_read(&frag->frag_sem);
	if (!frag->frag_dead)
		count = buffer->attr->show(buffer->item, buffer->page);
	up_read(&frag->frag_sem);

	if (count < 0)
		return count;
	if (WARN_ON_ONCE(count > (ssize_t)SIMPLE_ATTR_SIZE))
		return -EIO;
	buffer->needs_read_fill = 0;
	buffer->count = count;
	return 0;
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
	struct configfs_buffer *buffer = file->private_data;
	ssize_t retval = 0;

	mutex_lock(&buffer->mutex);
	if (buffer->needs_read_fill) {
		retval = fill_read_buffer(file, buffer);
		if (retval)
			goto out;
	}
	pr_debug("%s: count = %zd, ppos = %lld, buf = %s\n",
		 __func__, count, *ppos, buffer->page);
	retval = simple_read_from_buffer(buf, count, ppos, buffer->page,
					 buffer->count);
out:
	mutex_unlock(&buffer->mutex);
	return retval;
}

/**
 *	configfs_read_bin_file - read a binary attribute.
 *	@file:	file pointer.
 *	@buf:	buffer to fill.
 *	@count:	number of bytes to read.
 *	@ppos:	starting offset in file.
 *
 *	Userspace wants to read a binary attribute file. The attribute
 *	descriptor is in the file's ->d_fsdata. The target item is in the
 *	directory's ->d_fsdata.
 *
 *	We check whether we need to refill the buffer. If so we will
 *	call the attributes' attr->read() twice. The first time we
 *	will pass a NULL as a buffer pointer, which the attributes' method
 *	will use to return the size of the buffer required. If no error
 *	occurs we will allocate the buffer using vmalloc and call
 *	attr->read() again passing that buffer as an argument.
 *	Then we just copy to user-space using simple_read_from_buffer.
 */

static ssize_t
configfs_read_bin_file(struct file *file, char __user *buf,
		       size_t count, loff_t *ppos)
{
	struct configfs_fragment *frag = to_frag(file);
	struct configfs_buffer *buffer = file->private_data;
	ssize_t retval = 0;
	ssize_t len = min_t(size_t, count, PAGE_SIZE);

	mutex_lock(&buffer->mutex);

	/* we don't support switching read/write modes */
	if (buffer->write_in_progress) {
		retval = -ETXTBSY;
		goto out;
	}
	buffer->read_in_progress = true;

	if (buffer->needs_read_fill) {
		/* perform first read with buf == NULL to get extent */
		down_read(&frag->frag_sem);
		if (!frag->frag_dead)
			len = buffer->bin_attr->read(buffer->item, NULL, 0);
		else
			len = -ENOENT;
		up_read(&frag->frag_sem);
		if (len <= 0) {
			retval = len;
			goto out;
		}

		/* do not exceed the maximum value */
		if (buffer->cb_max_size && len > buffer->cb_max_size) {
			retval = -EFBIG;
			goto out;
		}

		buffer->bin_buffer = vmalloc(len);
		if (buffer->bin_buffer == NULL) {
			retval = -ENOMEM;
			goto out;
		}
		buffer->bin_buffer_size = len;

		/* perform second read to fill buffer */
		down_read(&frag->frag_sem);
		if (!frag->frag_dead)
			len = buffer->bin_attr->read(buffer->item,
						     buffer->bin_buffer, len);
		else
			len = -ENOENT;
		up_read(&frag->frag_sem);
		if (len < 0) {
			retval = len;
			vfree(buffer->bin_buffer);
			buffer->bin_buffer_size = 0;
			buffer->bin_buffer = NULL;
			goto out;
		}

		buffer->needs_read_fill = 0;
	}

	retval = simple_read_from_buffer(buf, count, ppos, buffer->bin_buffer,
					buffer->bin_buffer_size);
out:
	mutex_unlock(&buffer->mutex);
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

	if (count >= SIMPLE_ATTR_SIZE)
		count = SIMPLE_ATTR_SIZE - 1;
	error = copy_from_user(buffer->page,buf,count);
	buffer->needs_read_fill = 1;
	/* if buf is assumed to contain a string, terminate it by \0,
	 * so e.g. sscanf() can scan the string easily */
	buffer->page[count] = 0;
	return error ? -EFAULT : count;
}

static int
flush_write_buffer(struct file *file, struct configfs_buffer *buffer, size_t count)
{
	struct configfs_fragment *frag = to_frag(file);
	int res = -ENOENT;

	down_read(&frag->frag_sem);
	if (!frag->frag_dead)
		res = buffer->attr->store(buffer->item, buffer->page, count);
	up_read(&frag->frag_sem);
	return res;
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
	struct configfs_buffer *buffer = file->private_data;
	ssize_t len;

	mutex_lock(&buffer->mutex);
	len = fill_write_buffer(buffer, buf, count);
	if (len > 0)
		len = flush_write_buffer(file, buffer, len);
	if (len > 0)
		*ppos += len;
	mutex_unlock(&buffer->mutex);
	return len;
}

/**
 *	configfs_write_bin_file - write a binary attribute.
 *	@file:	file pointer
 *	@buf:	data to write
 *	@count:	number of bytes
 *	@ppos:	starting offset
 *
 *	Writing to a binary attribute file is similar to a normal read.
 *	We buffer the consecutive writes (binary attribute files do not
 *	support lseek) in a continuously growing buffer, but we don't
 *	commit until the close of the file.
 */

static ssize_t
configfs_write_bin_file(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	struct configfs_buffer *buffer = file->private_data;
	void *tbuf = NULL;
	ssize_t len;

	mutex_lock(&buffer->mutex);

	/* we don't support switching read/write modes */
	if (buffer->read_in_progress) {
		len = -ETXTBSY;
		goto out;
	}
	buffer->write_in_progress = true;

	/* buffer grows? */
	if (*ppos + count > buffer->bin_buffer_size) {

		if (buffer->cb_max_size &&
			*ppos + count > buffer->cb_max_size) {
			len = -EFBIG;
			goto out;
		}

		tbuf = vmalloc(*ppos + count);
		if (tbuf == NULL) {
			len = -ENOMEM;
			goto out;
		}

		/* copy old contents */
		if (buffer->bin_buffer) {
			memcpy(tbuf, buffer->bin_buffer,
				buffer->bin_buffer_size);
			vfree(buffer->bin_buffer);
		}

		/* clear the new area */
		memset(tbuf + buffer->bin_buffer_size, 0,
			*ppos + count - buffer->bin_buffer_size);
		buffer->bin_buffer = tbuf;
		buffer->bin_buffer_size = *ppos + count;
	}

	len = simple_write_to_buffer(buffer->bin_buffer,
			buffer->bin_buffer_size, ppos, buf, count);
out:
	mutex_unlock(&buffer->mutex);
	return len;
}

static int __configfs_open_file(struct inode *inode, struct file *file, int type)
{
	struct dentry *dentry = file->f_path.dentry;
	struct configfs_fragment *frag = to_frag(file);
	struct configfs_attribute *attr;
	struct configfs_buffer *buffer;
	int error;

	error = -ENOMEM;
	buffer = kzalloc(sizeof(struct configfs_buffer), GFP_KERNEL);
	if (!buffer)
		goto out;

	error = -ENOENT;
	down_read(&frag->frag_sem);
	if (unlikely(frag->frag_dead))
		goto out_free_buffer;

	error = -EINVAL;
	buffer->item = to_item(dentry->d_parent);
	if (!buffer->item)
		goto out_free_buffer;

	attr = to_attr(dentry);
	if (!attr)
		goto out_put_item;

	if (type & CONFIGFS_ITEM_BIN_ATTR) {
		buffer->bin_attr = to_bin_attr(dentry);
		buffer->cb_max_size = buffer->bin_attr->cb_max_size;
	} else {
		buffer->attr = attr;
	}

	buffer->owner = attr->ca_owner;
	/* Grab the module reference for this attribute if we have one */
	error = -ENODEV;
	if (!try_module_get(buffer->owner))
		goto out_put_item;

	error = -EACCES;
	if (!buffer->item->ci_type)
		goto out_put_module;

	buffer->ops = buffer->item->ci_type->ct_item_ops;

	/* File needs write support.
	 * The inode's perms must say it's ok,
	 * and we must have a store method.
	 */
	if (file->f_mode & FMODE_WRITE) {
		if (!(inode->i_mode & S_IWUGO))
			goto out_put_module;
		if ((type & CONFIGFS_ITEM_ATTR) && !attr->store)
			goto out_put_module;
		if ((type & CONFIGFS_ITEM_BIN_ATTR) && !buffer->bin_attr->write)
			goto out_put_module;
	}

	/* File needs read support.
	 * The inode's perms must say it's ok, and we there
	 * must be a show method for it.
	 */
	if (file->f_mode & FMODE_READ) {
		if (!(inode->i_mode & S_IRUGO))
			goto out_put_module;
		if ((type & CONFIGFS_ITEM_ATTR) && !attr->show)
			goto out_put_module;
		if ((type & CONFIGFS_ITEM_BIN_ATTR) && !buffer->bin_attr->read)
			goto out_put_module;
	}

	mutex_init(&buffer->mutex);
	buffer->needs_read_fill = 1;
	buffer->read_in_progress = false;
	buffer->write_in_progress = false;
	file->private_data = buffer;
	up_read(&frag->frag_sem);
	return 0;

out_put_module:
	module_put(buffer->owner);
out_put_item:
	config_item_put(buffer->item);
out_free_buffer:
	up_read(&frag->frag_sem);
	kfree(buffer);
out:
	return error;
}

static int configfs_release(struct inode *inode, struct file *filp)
{
	struct configfs_buffer *buffer = filp->private_data;

	module_put(buffer->owner);
	if (buffer->page)
		free_page((unsigned long)buffer->page);
	mutex_destroy(&buffer->mutex);
	kfree(buffer);
	return 0;
}

static int configfs_open_file(struct inode *inode, struct file *filp)
{
	return __configfs_open_file(inode, filp, CONFIGFS_ITEM_ATTR);
}

static int configfs_open_bin_file(struct inode *inode, struct file *filp)
{
	return __configfs_open_file(inode, filp, CONFIGFS_ITEM_BIN_ATTR);
}

static int configfs_release_bin_file(struct inode *inode, struct file *file)
{
	struct configfs_buffer *buffer = file->private_data;

	buffer->read_in_progress = false;

	if (buffer->write_in_progress) {
		struct configfs_fragment *frag = to_frag(file);
		buffer->write_in_progress = false;

		down_read(&frag->frag_sem);
		if (!frag->frag_dead) {
			/* result of ->release() is ignored */
			buffer->bin_attr->write(buffer->item,
					buffer->bin_buffer,
					buffer->bin_buffer_size);
		}
		up_read(&frag->frag_sem);
		/* vfree on NULL is safe */
		vfree(buffer->bin_buffer);
		buffer->bin_buffer = NULL;
		buffer->bin_buffer_size = 0;
		buffer->needs_read_fill = 1;
	}

	configfs_release(inode, file);
	return 0;
}


const struct file_operations configfs_file_operations = {
	.read		= configfs_read_file,
	.write		= configfs_write_file,
	.llseek		= generic_file_llseek,
	.open		= configfs_open_file,
	.release	= configfs_release,
};

const struct file_operations configfs_bin_file_operations = {
	.read		= configfs_read_bin_file,
	.write		= configfs_write_bin_file,
	.llseek		= NULL,		/* bin file is not seekable */
	.open		= configfs_open_bin_file,
	.release	= configfs_release_bin_file,
};

/**
 *	configfs_create_file - create an attribute file for an item.
 *	@item:	item we're creating for.
 *	@attr:	atrribute descriptor.
 */

int configfs_create_file(struct config_item * item, const struct configfs_attribute * attr)
{
	struct dentry *dir = item->ci_dentry;
	struct configfs_dirent *parent_sd = dir->d_fsdata;
	umode_t mode = (attr->ca_mode & S_IALLUGO) | S_IFREG;
	int error = 0;

	inode_lock_nested(d_inode(dir), I_MUTEX_NORMAL);
	error = configfs_make_dirent(parent_sd, NULL, (void *) attr, mode,
				     CONFIGFS_ITEM_ATTR, parent_sd->s_frag);
	inode_unlock(d_inode(dir));

	return error;
}

/**
 *	configfs_create_bin_file - create a binary attribute file for an item.
 *	@item:	item we're creating for.
 *	@attr:	atrribute descriptor.
 */

int configfs_create_bin_file(struct config_item *item,
		const struct configfs_bin_attribute *bin_attr)
{
	struct dentry *dir = item->ci_dentry;
	struct configfs_dirent *parent_sd = dir->d_fsdata;
	umode_t mode = (bin_attr->cb_attr.ca_mode & S_IALLUGO) | S_IFREG;
	int error = 0;

	inode_lock_nested(dir->d_inode, I_MUTEX_NORMAL);
	error = configfs_make_dirent(parent_sd, NULL, (void *) bin_attr, mode,
				     CONFIGFS_ITEM_BIN_ATTR, parent_sd->s_frag);
	inode_unlock(dir->d_inode);

	return error;
}
