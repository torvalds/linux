/*
 * fs/sysfs/file.c - sysfs regular (text) file implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007 Tejun Heo <teheo@suse.de>
 *
 * This file is released under the GPLv2.
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/kallsyms.h>
#include <linux/slab.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <asm/uaccess.h>

#include "sysfs.h"

/*
 * There's one sysfs_buffer for each open file and one
 * sysfs_open_dirent for each sysfs_dirent with one or more open
 * files.
 *
 * filp->private_data points to sysfs_buffer and
 * sysfs_dirent->s_attr.open points to sysfs_open_dirent.  s_attr.open
 * is protected by sysfs_open_dirent_lock.
 */
static DEFINE_SPINLOCK(sysfs_open_dirent_lock);

struct sysfs_open_dirent {
	atomic_t		refcnt;
	atomic_t		event;
	wait_queue_head_t	poll;
	struct list_head	buffers; /* goes through sysfs_buffer.list */
};

struct sysfs_buffer {
	size_t			count;
	loff_t			pos;
	char			* page;
	struct sysfs_ops	* ops;
	struct mutex		mutex;
	int			needs_read_fill;
	int			event;
	struct list_head	list;
};

/**
 *	fill_read_buffer - allocate and fill buffer from object.
 *	@dentry:	dentry pointer.
 *	@buffer:	data buffer for file.
 *
 *	Allocate @buffer->page, if it hasn't been already, then call the
 *	kobject's show() method to fill the buffer with this attribute's 
 *	data. 
 *	This is called only once, on the file's first read unless an error
 *	is returned.
 */
static int fill_read_buffer(struct dentry * dentry, struct sysfs_buffer * buffer)
{
	struct sysfs_dirent *attr_sd = dentry->d_fsdata;
	struct kobject *kobj = attr_sd->s_parent->s_dir.kobj;
	struct sysfs_ops * ops = buffer->ops;
	int ret = 0;
	ssize_t count;

	if (!buffer->page)
		buffer->page = (char *) get_zeroed_page(GFP_KERNEL);
	if (!buffer->page)
		return -ENOMEM;

	/* need attr_sd for attr and ops, its parent for kobj */
	if (!sysfs_get_active_two(attr_sd))
		return -ENODEV;

	buffer->event = atomic_read(&attr_sd->s_attr.open->event);
	count = ops->show(kobj, attr_sd->s_attr.attr, buffer->page);

	sysfs_put_active_two(attr_sd);

	/*
	 * The code works fine with PAGE_SIZE return but it's likely to
	 * indicate truncated result or overflow in normal use cases.
	 */
	if (count >= (ssize_t)PAGE_SIZE) {
		print_symbol("fill_read_buffer: %s returned bad count\n",
			(unsigned long)ops->show);
		/* Try to struggle along */
		count = PAGE_SIZE - 1;
	}
	if (count >= 0) {
		buffer->needs_read_fill = 0;
		buffer->count = count;
	} else {
		ret = count;
	}
	return ret;
}

/**
 *	sysfs_read_file - read an attribute. 
 *	@file:	file pointer.
 *	@buf:	buffer to fill.
 *	@count:	number of bytes to read.
 *	@ppos:	starting offset in file.
 *
 *	Userspace wants to read an attribute file. The attribute descriptor
 *	is in the file's ->d_fsdata. The target object is in the directory's
 *	->d_fsdata.
 *
 *	We call fill_read_buffer() to allocate and fill the buffer from the
 *	object's show() method exactly once (if the read is happening from
 *	the beginning of the file). That should fill the entire buffer with
 *	all the data the object has to offer for that attribute.
 *	We then call flush_read_buffer() to copy the buffer to userspace
 *	in the increments specified.
 */

static ssize_t
sysfs_read_file(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	struct sysfs_buffer * buffer = file->private_data;
	ssize_t retval = 0;

	mutex_lock(&buffer->mutex);
	if (buffer->needs_read_fill || *ppos == 0) {
		retval = fill_read_buffer(file->f_path.dentry,buffer);
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
 *	fill_write_buffer - copy buffer from userspace.
 *	@buffer:	data buffer for file.
 *	@buf:		data from user.
 *	@count:		number of bytes in @userbuf.
 *
 *	Allocate @buffer->page if it hasn't been already, then
 *	copy the user-supplied buffer into it.
 */

static int 
fill_write_buffer(struct sysfs_buffer * buffer, const char __user * buf, size_t count)
{
	int error;

	if (!buffer->page)
		buffer->page = (char *)get_zeroed_page(GFP_KERNEL);
	if (!buffer->page)
		return -ENOMEM;

	if (count >= PAGE_SIZE)
		count = PAGE_SIZE - 1;
	error = copy_from_user(buffer->page,buf,count);
	buffer->needs_read_fill = 1;
	/* if buf is assumed to contain a string, terminate it by \0,
	   so e.g. sscanf() can scan the string easily */
	buffer->page[count] = 0;
	return error ? -EFAULT : count;
}


/**
 *	flush_write_buffer - push buffer to kobject.
 *	@dentry:	dentry to the attribute
 *	@buffer:	data buffer for file.
 *	@count:		number of bytes
 *
 *	Get the correct pointers for the kobject and the attribute we're
 *	dealing with, then call the store() method for the attribute, 
 *	passing the buffer that we acquired in fill_write_buffer().
 */

static int
flush_write_buffer(struct dentry * dentry, struct sysfs_buffer * buffer, size_t count)
{
	struct sysfs_dirent *attr_sd = dentry->d_fsdata;
	struct kobject *kobj = attr_sd->s_parent->s_dir.kobj;
	struct sysfs_ops * ops = buffer->ops;
	int rc;

	/* need attr_sd for attr and ops, its parent for kobj */
	if (!sysfs_get_active_two(attr_sd))
		return -ENODEV;

	rc = ops->store(kobj, attr_sd->s_attr.attr, buffer->page, count);

	sysfs_put_active_two(attr_sd);

	return rc;
}


/**
 *	sysfs_write_file - write an attribute.
 *	@file:	file pointer
 *	@buf:	data to write
 *	@count:	number of bytes
 *	@ppos:	starting offset
 *
 *	Similar to sysfs_read_file(), though working in the opposite direction.
 *	We allocate and fill the data from the user in fill_write_buffer(),
 *	then push it to the kobject in flush_write_buffer().
 *	There is no easy way for us to know if userspace is only doing a partial
 *	write, so we don't support them. We expect the entire buffer to come
 *	on the first write. 
 *	Hint: if you're writing a value, first read the file, modify only the
 *	the value you're changing, then write entire buffer back. 
 */

static ssize_t
sysfs_write_file(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct sysfs_buffer * buffer = file->private_data;
	ssize_t len;

	mutex_lock(&buffer->mutex);
	len = fill_write_buffer(buffer, buf, count);
	if (len > 0)
		len = flush_write_buffer(file->f_path.dentry, buffer, len);
	if (len > 0)
		*ppos += len;
	mutex_unlock(&buffer->mutex);
	return len;
}

/**
 *	sysfs_get_open_dirent - get or create sysfs_open_dirent
 *	@sd: target sysfs_dirent
 *	@buffer: sysfs_buffer for this instance of open
 *
 *	If @sd->s_attr.open exists, increment its reference count;
 *	otherwise, create one.  @buffer is chained to the buffers
 *	list.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int sysfs_get_open_dirent(struct sysfs_dirent *sd,
				 struct sysfs_buffer *buffer)
{
	struct sysfs_open_dirent *od, *new_od = NULL;

 retry:
	spin_lock(&sysfs_open_dirent_lock);

	if (!sd->s_attr.open && new_od) {
		sd->s_attr.open = new_od;
		new_od = NULL;
	}

	od = sd->s_attr.open;
	if (od) {
		atomic_inc(&od->refcnt);
		list_add_tail(&buffer->list, &od->buffers);
	}

	spin_unlock(&sysfs_open_dirent_lock);

	if (od) {
		kfree(new_od);
		return 0;
	}

	/* not there, initialize a new one and retry */
	new_od = kmalloc(sizeof(*new_od), GFP_KERNEL);
	if (!new_od)
		return -ENOMEM;

	atomic_set(&new_od->refcnt, 0);
	atomic_set(&new_od->event, 1);
	init_waitqueue_head(&new_od->poll);
	INIT_LIST_HEAD(&new_od->buffers);
	goto retry;
}

/**
 *	sysfs_put_open_dirent - put sysfs_open_dirent
 *	@sd: target sysfs_dirent
 *	@buffer: associated sysfs_buffer
 *
 *	Put @sd->s_attr.open and unlink @buffer from the buffers list.
 *	If reference count reaches zero, disassociate and free it.
 *
 *	LOCKING:
 *	None.
 */
static void sysfs_put_open_dirent(struct sysfs_dirent *sd,
				  struct sysfs_buffer *buffer)
{
	struct sysfs_open_dirent *od = sd->s_attr.open;

	spin_lock(&sysfs_open_dirent_lock);

	list_del(&buffer->list);
	if (atomic_dec_and_test(&od->refcnt))
		sd->s_attr.open = NULL;
	else
		od = NULL;

	spin_unlock(&sysfs_open_dirent_lock);

	kfree(od);
}

static int sysfs_open_file(struct inode *inode, struct file *file)
{
	struct sysfs_dirent *attr_sd = file->f_path.dentry->d_fsdata;
	struct kobject *kobj = attr_sd->s_parent->s_dir.kobj;
	struct sysfs_buffer *buffer;
	struct sysfs_ops *ops;
	int error = -EACCES;

	/* need attr_sd for attr and ops, its parent for kobj */
	if (!sysfs_get_active_two(attr_sd))
		return -ENODEV;

	/* every kobject with an attribute needs a ktype assigned */
	if (kobj->ktype && kobj->ktype->sysfs_ops)
		ops = kobj->ktype->sysfs_ops;
	else {
		printk(KERN_ERR "missing sysfs attribute operations for "
		       "kobject: %s\n", kobject_name(kobj));
		WARN_ON(1);
		goto err_out;
	}

	/* File needs write support.
	 * The inode's perms must say it's ok, 
	 * and we must have a store method.
	 */
	if (file->f_mode & FMODE_WRITE) {
		if (!(inode->i_mode & S_IWUGO) || !ops->store)
			goto err_out;
	}

	/* File needs read support.
	 * The inode's perms must say it's ok, and we there
	 * must be a show method for it.
	 */
	if (file->f_mode & FMODE_READ) {
		if (!(inode->i_mode & S_IRUGO) || !ops->show)
			goto err_out;
	}

	/* No error? Great, allocate a buffer for the file, and store it
	 * it in file->private_data for easy access.
	 */
	error = -ENOMEM;
	buffer = kzalloc(sizeof(struct sysfs_buffer), GFP_KERNEL);
	if (!buffer)
		goto err_out;

	mutex_init(&buffer->mutex);
	buffer->needs_read_fill = 1;
	buffer->ops = ops;
	file->private_data = buffer;

	/* make sure we have open dirent struct */
	error = sysfs_get_open_dirent(attr_sd, buffer);
	if (error)
		goto err_free;

	/* open succeeded, put active references */
	sysfs_put_active_two(attr_sd);
	return 0;

 err_free:
	kfree(buffer);
 err_out:
	sysfs_put_active_two(attr_sd);
	return error;
}

static int sysfs_release(struct inode *inode, struct file *filp)
{
	struct sysfs_dirent *sd = filp->f_path.dentry->d_fsdata;
	struct sysfs_buffer *buffer = filp->private_data;

	sysfs_put_open_dirent(sd, buffer);

	if (buffer->page)
		free_page((unsigned long)buffer->page);
	kfree(buffer);

	return 0;
}

/* Sysfs attribute files are pollable.  The idea is that you read
 * the content and then you use 'poll' or 'select' to wait for
 * the content to change.  When the content changes (assuming the
 * manager for the kobject supports notification), poll will
 * return POLLERR|POLLPRI, and select will return the fd whether
 * it is waiting for read, write, or exceptions.
 * Once poll/select indicates that the value has changed, you
 * need to close and re-open the file, or seek to 0 and read again.
 * Reminder: this only works for attributes which actively support
 * it, and it is not possible to test an attribute from userspace
 * to see if it supports poll (Neither 'poll' nor 'select' return
 * an appropriate error code).  When in doubt, set a suitable timeout value.
 */
static unsigned int sysfs_poll(struct file *filp, poll_table *wait)
{
	struct sysfs_buffer * buffer = filp->private_data;
	struct sysfs_dirent *attr_sd = filp->f_path.dentry->d_fsdata;
	struct sysfs_open_dirent *od = attr_sd->s_attr.open;

	/* need parent for the kobj, grab both */
	if (!sysfs_get_active_two(attr_sd))
		goto trigger;

	poll_wait(filp, &od->poll, wait);

	sysfs_put_active_two(attr_sd);

	if (buffer->event != atomic_read(&od->event))
		goto trigger;

	return 0;

 trigger:
	buffer->needs_read_fill = 1;
	return POLLERR|POLLPRI;
}

void sysfs_notify(struct kobject *k, char *dir, char *attr)
{
	struct sysfs_dirent *sd = k->sd;

	mutex_lock(&sysfs_mutex);

	if (sd && dir)
		sd = sysfs_find_dirent(sd, dir);
	if (sd && attr)
		sd = sysfs_find_dirent(sd, attr);
	if (sd) {
		struct sysfs_open_dirent *od;

		spin_lock(&sysfs_open_dirent_lock);

		od = sd->s_attr.open;
		if (od) {
			atomic_inc(&od->event);
			wake_up_interruptible(&od->poll);
		}

		spin_unlock(&sysfs_open_dirent_lock);
	}

	mutex_unlock(&sysfs_mutex);
}
EXPORT_SYMBOL_GPL(sysfs_notify);

const struct file_operations sysfs_file_operations = {
	.read		= sysfs_read_file,
	.write		= sysfs_write_file,
	.llseek		= generic_file_llseek,
	.open		= sysfs_open_file,
	.release	= sysfs_release,
	.poll		= sysfs_poll,
};

int sysfs_add_file_mode(struct sysfs_dirent *dir_sd,
			const struct attribute *attr, int type, mode_t amode)
{
	umode_t mode = (amode & S_IALLUGO) | S_IFREG;
	struct sysfs_addrm_cxt acxt;
	struct sysfs_dirent *sd;
	int rc;

	sd = sysfs_new_dirent(attr->name, mode, type);
	if (!sd)
		return -ENOMEM;
	sd->s_attr.attr = (void *)attr;

	sysfs_addrm_start(&acxt, dir_sd);
	rc = sysfs_add_one(&acxt, sd);
	sysfs_addrm_finish(&acxt);

	if (rc)
		sysfs_put(sd);

	return rc;
}


int sysfs_add_file(struct sysfs_dirent *dir_sd, const struct attribute *attr,
		   int type)
{
	return sysfs_add_file_mode(dir_sd, attr, type, attr->mode);
}


/**
 *	sysfs_create_file - create an attribute file for an object.
 *	@kobj:	object we're creating for. 
 *	@attr:	attribute descriptor.
 */

int sysfs_create_file(struct kobject * kobj, const struct attribute * attr)
{
	BUG_ON(!kobj || !kobj->sd || !attr);

	return sysfs_add_file(kobj->sd, attr, SYSFS_KOBJ_ATTR);

}


/**
 * sysfs_add_file_to_group - add an attribute file to a pre-existing group.
 * @kobj: object we're acting for.
 * @attr: attribute descriptor.
 * @group: group name.
 */
int sysfs_add_file_to_group(struct kobject *kobj,
		const struct attribute *attr, const char *group)
{
	struct sysfs_dirent *dir_sd;
	int error;

	if (group)
		dir_sd = sysfs_get_dirent(kobj->sd, group);
	else
		dir_sd = sysfs_get(kobj->sd);

	if (!dir_sd)
		return -ENOENT;

	error = sysfs_add_file(dir_sd, attr, SYSFS_KOBJ_ATTR);
	sysfs_put(dir_sd);

	return error;
}
EXPORT_SYMBOL_GPL(sysfs_add_file_to_group);

/**
 * sysfs_chmod_file - update the modified mode value on an object attribute.
 * @kobj: object we're acting for.
 * @attr: attribute descriptor.
 * @mode: file permissions.
 *
 */
int sysfs_chmod_file(struct kobject *kobj, struct attribute *attr, mode_t mode)
{
	struct sysfs_dirent *victim_sd = NULL;
	struct dentry *victim = NULL;
	struct inode * inode;
	struct iattr newattrs;
	int rc;

	rc = -ENOENT;
	victim_sd = sysfs_get_dirent(kobj->sd, attr->name);
	if (!victim_sd)
		goto out;

	mutex_lock(&sysfs_rename_mutex);
	victim = sysfs_get_dentry(victim_sd);
	mutex_unlock(&sysfs_rename_mutex);
	if (IS_ERR(victim)) {
		rc = PTR_ERR(victim);
		victim = NULL;
		goto out;
	}

	inode = victim->d_inode;

	mutex_lock(&inode->i_mutex);

	newattrs.ia_mode = (mode & S_IALLUGO) | (inode->i_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
	rc = notify_change(victim, &newattrs);

	if (rc == 0) {
		mutex_lock(&sysfs_mutex);
		victim_sd->s_mode = newattrs.ia_mode;
		mutex_unlock(&sysfs_mutex);
	}

	mutex_unlock(&inode->i_mutex);
 out:
	dput(victim);
	sysfs_put(victim_sd);
	return rc;
}
EXPORT_SYMBOL_GPL(sysfs_chmod_file);


/**
 *	sysfs_remove_file - remove an object attribute.
 *	@kobj:	object we're acting for.
 *	@attr:	attribute descriptor.
 *
 *	Hash the attribute name and kill the victim.
 */

void sysfs_remove_file(struct kobject * kobj, const struct attribute * attr)
{
	sysfs_hash_and_remove(kobj->sd, attr->name);
}


/**
 * sysfs_remove_file_from_group - remove an attribute file from a group.
 * @kobj: object we're acting for.
 * @attr: attribute descriptor.
 * @group: group name.
 */
void sysfs_remove_file_from_group(struct kobject *kobj,
		const struct attribute *attr, const char *group)
{
	struct sysfs_dirent *dir_sd;

	if (group)
		dir_sd = sysfs_get_dirent(kobj->sd, group);
	else
		dir_sd = sysfs_get(kobj->sd);
	if (dir_sd) {
		sysfs_hash_and_remove(dir_sd, attr->name);
		sysfs_put(dir_sd);
	}
}
EXPORT_SYMBOL_GPL(sysfs_remove_file_from_group);

struct sysfs_schedule_callback_struct {
	struct kobject 		*kobj;
	void			(*func)(void *);
	void			*data;
	struct module		*owner;
	struct work_struct	work;
};

static void sysfs_schedule_callback_work(struct work_struct *work)
{
	struct sysfs_schedule_callback_struct *ss = container_of(work,
			struct sysfs_schedule_callback_struct, work);

	(ss->func)(ss->data);
	kobject_put(ss->kobj);
	module_put(ss->owner);
	kfree(ss);
}

/**
 * sysfs_schedule_callback - helper to schedule a callback for a kobject
 * @kobj: object we're acting for.
 * @func: callback function to invoke later.
 * @data: argument to pass to @func.
 * @owner: module owning the callback code
 *
 * sysfs attribute methods must not unregister themselves or their parent
 * kobject (which would amount to the same thing).  Attempts to do so will
 * deadlock, since unregistration is mutually exclusive with driver
 * callbacks.
 *
 * Instead methods can call this routine, which will attempt to allocate
 * and schedule a workqueue request to call back @func with @data as its
 * argument in the workqueue's process context.  @kobj will be pinned
 * until @func returns.
 *
 * Returns 0 if the request was submitted, -ENOMEM if storage could not
 * be allocated, -ENODEV if a reference to @owner isn't available.
 */
int sysfs_schedule_callback(struct kobject *kobj, void (*func)(void *),
		void *data, struct module *owner)
{
	struct sysfs_schedule_callback_struct *ss;

	if (!try_module_get(owner))
		return -ENODEV;
	ss = kmalloc(sizeof(*ss), GFP_KERNEL);
	if (!ss) {
		module_put(owner);
		return -ENOMEM;
	}
	kobject_get(kobj);
	ss->kobj = kobj;
	ss->func = func;
	ss->data = data;
	ss->owner = owner;
	INIT_WORK(&ss->work, sysfs_schedule_callback_work);
	schedule_work(&ss->work);
	return 0;
}
EXPORT_SYMBOL_GPL(sysfs_schedule_callback);


EXPORT_SYMBOL_GPL(sysfs_create_file);
EXPORT_SYMBOL_GPL(sysfs_remove_file);
