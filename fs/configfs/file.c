// SPDX-License-Identifier: GPL-2.0-or-later
/*
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
#include <linux/uio.h>
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

static ssize_t configfs_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct configfs_buffer *buffer = file->private_data;
	ssize_t retval = 0;

	mutex_lock(&buffer->mutex);
	if (buffer->needs_read_fill) {
		retval = fill_read_buffer(file, buffer);
		if (retval)
			goto out;
	}
	pr_debug("%s: count = %zd, pos = %lld, buf = %s\n",
		 __func__, iov_iter_count(to), iocb->ki_pos, buffer->page);
	if (iocb->ki_pos >= buffer->count)
		goto out;
	retval = copy_to_iter(buffer->page + iocb->ki_pos,
			      buffer->count - iocb->ki_pos, to);
	iocb->ki_pos += retval;
	if (retval == 0)
		retval = -EFAULT;
out:
	mutex_unlock(&buffer->mutex);
	return retval;
}

static ssize_t configfs_bin_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct configfs_fragment *frag = to_frag(file);
	struct configfs_buffer *buffer = file->private_data;
	ssize_t retval = 0;
	ssize_t len;

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

	if (iocb->ki_pos >= buffer->bin_buffer_size)
		goto out;
	retval = copy_to_iter(buffer->bin_buffer + iocb->ki_pos,
			      buffer->bin_buffer_size - iocb->ki_pos, to);
	iocb->ki_pos += retval;
	if (retval == 0)
		retval = -EFAULT;
out:
	mutex_unlock(&buffer->mutex);
	return retval;
}

/* Fill @buffer with data coming from @from. */
static int fill_write_buffer(struct configfs_buffer *buffer,
			     struct iov_iter *from)
{
	int copied;

	if (!buffer->page)
		buffer->page = (char *)__get_free_pages(GFP_KERNEL, 0);
	if (!buffer->page)
		return -ENOMEM;

	copied = copy_from_iter(buffer->page, SIMPLE_ATTR_SIZE - 1, from);
	buffer->needs_read_fill = 1;
	/* if buf is assumed to contain a string, terminate it by \0,
	 * so e.g. sscanf() can scan the string easily */
	buffer->page[copied] = 0;
	return copied ? : -EFAULT;
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


/*
 * There is no easy way for us to know if userspace is only doing a partial
 * write, so we don't support them. We expect the entire buffer to come on the
 * first write.
 * Hint: if you're writing a value, first read the file, modify only the value
 * you're changing, then write entire buffer back.
 */
static ssize_t configfs_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct configfs_buffer *buffer = file->private_data;
	int len;

	mutex_lock(&buffer->mutex);
	len = fill_write_buffer(buffer, from);
	if (len > 0)
		len = flush_write_buffer(file, buffer, len);
	if (len > 0)
		iocb->ki_pos += len;
	mutex_unlock(&buffer->mutex);
	return len;
}

static ssize_t configfs_bin_write_iter(struct kiocb *iocb,
				       struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct configfs_buffer *buffer = file->private_data;
	void *tbuf = NULL;
	size_t end_offset;
	ssize_t len;

	mutex_lock(&buffer->mutex);

	/* we don't support switching read/write modes */
	if (buffer->read_in_progress) {
		len = -ETXTBSY;
		goto out;
	}
	buffer->write_in_progress = true;

	/* buffer grows? */
	end_offset = iocb->ki_pos + iov_iter_count(from);
	if (end_offset > buffer->bin_buffer_size) {
		if (buffer->cb_max_size && end_offset > buffer->cb_max_size) {
			len = -EFBIG;
			goto out;
		}

		tbuf = vmalloc(end_offset);
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
			end_offset - buffer->bin_buffer_size);
		buffer->bin_buffer = tbuf;
		buffer->bin_buffer_size = end_offset;
	}

	len = copy_from_iter(buffer->bin_buffer + iocb->ki_pos,
			     buffer->bin_buffer_size - iocb->ki_pos, from);
	iocb->ki_pos += len;
out:
	mutex_unlock(&buffer->mutex);
	return len ? : -EFAULT;
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
		goto out_free_buffer;

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
		goto out_free_buffer;

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

	if (buffer->write_in_progress) {
		struct configfs_fragment *frag = to_frag(file);

		down_read(&frag->frag_sem);
		if (!frag->frag_dead) {
			/* result of ->release() is ignored */
			buffer->bin_attr->write(buffer->item,
					buffer->bin_buffer,
					buffer->bin_buffer_size);
		}
		up_read(&frag->frag_sem);
	}

	vfree(buffer->bin_buffer);

	configfs_release(inode, file);
	return 0;
}


const struct file_operations configfs_file_operations = {
	.read_iter	= configfs_read_iter,
	.write_iter	= configfs_write_iter,
	.llseek		= generic_file_llseek,
	.open		= configfs_open_file,
	.release	= configfs_release,
};

const struct file_operations configfs_bin_file_operations = {
	.read_iter	= configfs_bin_read_iter,
	.write_iter	= configfs_bin_write_iter,
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
 *	@bin_attr: atrribute descriptor.
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
