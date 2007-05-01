/*
 * file.c - operations for regular (text) files.
 */

#include <linux/module.h>
#include <linux/fsnotify.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/list.h>
#include <asm/uaccess.h>
#include <asm/semaphore.h>

#include "sysfs.h"

#define to_subsys(k) container_of(k,struct subsystem,kset.kobj)
#define to_sattr(a) container_of(a,struct subsys_attribute,attr)

/*
 * Subsystem file operations.
 * These operations allow subsystems to have files that can be 
 * read/written. 
 */
static ssize_t 
subsys_attr_show(struct kobject * kobj, struct attribute * attr, char * page)
{
	struct subsystem * s = to_subsys(kobj);
	struct subsys_attribute * sattr = to_sattr(attr);
	ssize_t ret = -EIO;

	if (sattr->show)
		ret = sattr->show(s,page);
	return ret;
}

static ssize_t 
subsys_attr_store(struct kobject * kobj, struct attribute * attr, 
		  const char * page, size_t count)
{
	struct subsystem * s = to_subsys(kobj);
	struct subsys_attribute * sattr = to_sattr(attr);
	ssize_t ret = -EIO;

	if (sattr->store)
		ret = sattr->store(s,page,count);
	return ret;
}

static struct sysfs_ops subsys_sysfs_ops = {
	.show	= subsys_attr_show,
	.store	= subsys_attr_store,
};

/**
 *	add_to_collection - add buffer to a collection
 *	@buffer:	buffer to be added
 *	@node:		inode of set to add to
 */

static inline void
add_to_collection(struct sysfs_buffer *buffer, struct inode *node)
{
	struct sysfs_buffer_collection *set = node->i_private;

	mutex_lock(&node->i_mutex);
	list_add(&buffer->associates, &set->associates);
	mutex_unlock(&node->i_mutex);
}

static inline void
remove_from_collection(struct sysfs_buffer *buffer, struct inode *node)
{
	mutex_lock(&node->i_mutex);
	list_del(&buffer->associates);
	mutex_unlock(&node->i_mutex);
}

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
	struct sysfs_dirent * sd = dentry->d_fsdata;
	struct attribute * attr = to_attr(dentry);
	struct kobject * kobj = to_kobj(dentry->d_parent);
	struct sysfs_ops * ops = buffer->ops;
	int ret = 0;
	ssize_t count;

	if (!buffer->page)
		buffer->page = (char *) get_zeroed_page(GFP_KERNEL);
	if (!buffer->page)
		return -ENOMEM;

	buffer->event = atomic_read(&sd->s_event);
	count = ops->show(kobj,attr,buffer->page);
	BUG_ON(count > (ssize_t)PAGE_SIZE);
	if (count >= 0) {
		buffer->needs_read_fill = 0;
		buffer->count = count;
	} else {
		ret = count;
	}
	return ret;
}


/**
 *	flush_read_buffer - push buffer to userspace.
 *	@buffer:	data buffer for file.
 *	@buf:		user-passed buffer.
 *	@count:		number of bytes requested.
 *	@ppos:		file position.
 *
 *	Copy the buffer we filled in fill_read_buffer() to userspace.
 *	This is done at the reader's leisure, copying and advancing 
 *	the amount they specify each time.
 *	This may be called continuously until the buffer is empty.
 */
static int flush_read_buffer(struct sysfs_buffer * buffer, char __user * buf,
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

	down(&buffer->sem);
	if (buffer->needs_read_fill) {
		if (buffer->orphaned)
			retval = -ENODEV;
		else
			retval = fill_read_buffer(file->f_path.dentry,buffer);
		if (retval)
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
	struct attribute * attr = to_attr(dentry);
	struct kobject * kobj = to_kobj(dentry->d_parent);
	struct sysfs_ops * ops = buffer->ops;

	return ops->store(kobj,attr,buffer->page,count);
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

	down(&buffer->sem);
	if (buffer->orphaned) {
		len = -ENODEV;
		goto out;
	}
	len = fill_write_buffer(buffer, buf, count);
	if (len > 0)
		len = flush_write_buffer(file->f_path.dentry, buffer, len);
	if (len > 0)
		*ppos += len;
out:
	up(&buffer->sem);
	return len;
}

static int sysfs_open_file(struct inode *inode, struct file *file)
{
	struct kobject *kobj = sysfs_get_kobject(file->f_path.dentry->d_parent);
	struct attribute * attr = to_attr(file->f_path.dentry);
	struct sysfs_buffer_collection *set;
	struct sysfs_buffer * buffer;
	struct sysfs_ops * ops = NULL;
	int error = 0;

	if (!kobj || !attr)
		goto Einval;

	/* Grab the module reference for this attribute if we have one */
	if (!try_module_get(attr->owner)) {
		error = -ENODEV;
		goto Done;
	}

	/* if the kobject has no ktype, then we assume that it is a subsystem
	 * itself, and use ops for it.
	 */
	if (kobj->kset && kobj->kset->ktype)
		ops = kobj->kset->ktype->sysfs_ops;
	else if (kobj->ktype)
		ops = kobj->ktype->sysfs_ops;
	else
		ops = &subsys_sysfs_ops;

	/* No sysfs operations, either from having no subsystem,
	 * or the subsystem have no operations.
	 */
	if (!ops)
		goto Eaccess;

	/* make sure we have a collection to add our buffers to */
	mutex_lock(&inode->i_mutex);
	if (!(set = inode->i_private)) {
		if (!(set = inode->i_private = kmalloc(sizeof(struct sysfs_buffer_collection), GFP_KERNEL))) {
			error = -ENOMEM;
			goto Done;
		} else {
			INIT_LIST_HEAD(&set->associates);
		}
	}
	mutex_unlock(&inode->i_mutex);

	/* File needs write support.
	 * The inode's perms must say it's ok, 
	 * and we must have a store method.
	 */
	if (file->f_mode & FMODE_WRITE) {

		if (!(inode->i_mode & S_IWUGO) || !ops->store)
			goto Eaccess;

	}

	/* File needs read support.
	 * The inode's perms must say it's ok, and we there
	 * must be a show method for it.
	 */
	if (file->f_mode & FMODE_READ) {
		if (!(inode->i_mode & S_IRUGO) || !ops->show)
			goto Eaccess;
	}

	/* No error? Great, allocate a buffer for the file, and store it
	 * it in file->private_data for easy access.
	 */
	buffer = kzalloc(sizeof(struct sysfs_buffer), GFP_KERNEL);
	if (buffer) {
		INIT_LIST_HEAD(&buffer->associates);
		init_MUTEX(&buffer->sem);
		buffer->needs_read_fill = 1;
		buffer->ops = ops;
		add_to_collection(buffer, inode);
		file->private_data = buffer;
	} else
		error = -ENOMEM;
	goto Done;

 Einval:
	error = -EINVAL;
	goto Done;
 Eaccess:
	error = -EACCES;
	module_put(attr->owner);
 Done:
	if (error)
		kobject_put(kobj);
	return error;
}

static int sysfs_release(struct inode * inode, struct file * filp)
{
	struct kobject * kobj = to_kobj(filp->f_path.dentry->d_parent);
	struct attribute * attr = to_attr(filp->f_path.dentry);
	struct module * owner = attr->owner;
	struct sysfs_buffer * buffer = filp->private_data;

	if (buffer)
		remove_from_collection(buffer, inode);
	kobject_put(kobj);
	/* After this point, attr should not be accessed. */
	module_put(owner);

	if (buffer) {
		if (buffer->page)
			free_page((unsigned long)buffer->page);
		kfree(buffer);
	}
	return 0;
}

/* Sysfs attribute files are pollable.  The idea is that you read
 * the content and then you use 'poll' or 'select' to wait for
 * the content to change.  When the content changes (assuming the
 * manager for the kobject supports notification), poll will
 * return POLLERR|POLLPRI, and select will return the fd whether
 * it is waiting for read, write, or exceptions.
 * Once poll/select indicates that the value has changed, you
 * need to close and re-open the file, as simply seeking and reading
 * again will not get new data, or reset the state of 'poll'.
 * Reminder: this only works for attributes which actively support
 * it, and it is not possible to test an attribute from userspace
 * to see if it supports poll (Nether 'poll' or 'select' return
 * an appropriate error code).  When in doubt, set a suitable timeout value.
 */
static unsigned int sysfs_poll(struct file *filp, poll_table *wait)
{
	struct sysfs_buffer * buffer = filp->private_data;
	struct kobject * kobj = to_kobj(filp->f_path.dentry->d_parent);
	struct sysfs_dirent * sd = filp->f_path.dentry->d_fsdata;
	int res = 0;

	poll_wait(filp, &kobj->poll, wait);

	if (buffer->event != atomic_read(&sd->s_event)) {
		res = POLLERR|POLLPRI;
		buffer->needs_read_fill = 1;
	}

	return res;
}


static struct dentry *step_down(struct dentry *dir, const char * name)
{
	struct dentry * de;

	if (dir == NULL || dir->d_inode == NULL)
		return NULL;

	mutex_lock(&dir->d_inode->i_mutex);
	de = lookup_one_len(name, dir, strlen(name));
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
	if (IS_ERR(de))
		return NULL;
	if (de->d_inode == NULL) {
		dput(de);
		return NULL;
	}
	return de;
}

void sysfs_notify(struct kobject * k, char *dir, char *attr)
{
	struct dentry *de = k->dentry;
	if (de)
		dget(de);
	if (de && dir)
		de = step_down(de, dir);
	if (de && attr)
		de = step_down(de, attr);
	if (de) {
		struct sysfs_dirent * sd = de->d_fsdata;
		if (sd)
			atomic_inc(&sd->s_event);
		wake_up_interruptible(&k->poll);
		dput(de);
	}
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


int sysfs_add_file(struct dentry * dir, const struct attribute * attr, int type)
{
	struct sysfs_dirent * parent_sd = dir->d_fsdata;
	umode_t mode = (attr->mode & S_IALLUGO) | S_IFREG;
	int error = -EEXIST;

	mutex_lock(&dir->d_inode->i_mutex);
	if (!sysfs_dirent_exist(parent_sd, attr->name))
		error = sysfs_make_dirent(parent_sd, NULL, (void *)attr,
					  mode, type);
	mutex_unlock(&dir->d_inode->i_mutex);

	return error;
}


/**
 *	sysfs_create_file - create an attribute file for an object.
 *	@kobj:	object we're creating for. 
 *	@attr:	atrribute descriptor.
 */

int sysfs_create_file(struct kobject * kobj, const struct attribute * attr)
{
	BUG_ON(!kobj || !kobj->dentry || !attr);

	return sysfs_add_file(kobj->dentry, attr, SYSFS_KOBJ_ATTR);

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
	struct dentry *dir;
	int error;

	dir = lookup_one_len(group, kobj->dentry, strlen(group));
	if (IS_ERR(dir))
		error = PTR_ERR(dir);
	else {
		error = sysfs_add_file(dir, attr, SYSFS_KOBJ_ATTR);
		dput(dir);
	}
	return error;
}
EXPORT_SYMBOL_GPL(sysfs_add_file_to_group);


/**
 * sysfs_update_file - update the modified timestamp on an object attribute.
 * @kobj: object we're acting for.
 * @attr: attribute descriptor.
 */
int sysfs_update_file(struct kobject * kobj, const struct attribute * attr)
{
	struct dentry * dir = kobj->dentry;
	struct dentry * victim;
	int res = -ENOENT;

	mutex_lock(&dir->d_inode->i_mutex);
	victim = lookup_one_len(attr->name, dir, strlen(attr->name));
	if (!IS_ERR(victim)) {
		/* make sure dentry is really there */
		if (victim->d_inode && 
		    (victim->d_parent->d_inode == dir->d_inode)) {
			victim->d_inode->i_mtime = CURRENT_TIME;
			fsnotify_modify(victim);
			res = 0;
		} else
			d_drop(victim);
		
		/**
		 * Drop the reference acquired from lookup_one_len() above.
		 */
		dput(victim);
	}
	mutex_unlock(&dir->d_inode->i_mutex);

	return res;
}


/**
 * sysfs_chmod_file - update the modified mode value on an object attribute.
 * @kobj: object we're acting for.
 * @attr: attribute descriptor.
 * @mode: file permissions.
 *
 */
int sysfs_chmod_file(struct kobject *kobj, struct attribute *attr, mode_t mode)
{
	struct dentry *dir = kobj->dentry;
	struct dentry *victim;
	struct inode * inode;
	struct iattr newattrs;
	int res = -ENOENT;

	mutex_lock(&dir->d_inode->i_mutex);
	victim = lookup_one_len(attr->name, dir, strlen(attr->name));
	if (!IS_ERR(victim)) {
		if (victim->d_inode &&
		    (victim->d_parent->d_inode == dir->d_inode)) {
			inode = victim->d_inode;
			mutex_lock(&inode->i_mutex);
			newattrs.ia_mode = (mode & S_IALLUGO) |
						(inode->i_mode & ~S_IALLUGO);
			newattrs.ia_valid = ATTR_MODE | ATTR_CTIME;
			res = notify_change(victim, &newattrs);
			mutex_unlock(&inode->i_mutex);
		}
		dput(victim);
	}
	mutex_unlock(&dir->d_inode->i_mutex);

	return res;
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
	sysfs_hash_and_remove(kobj->dentry, attr->name);
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
	struct dentry *dir;

	dir = lookup_one_len(group, kobj->dentry, strlen(group));
	if (!IS_ERR(dir)) {
		sysfs_hash_and_remove(dir, attr->name);
		dput(dir);
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
EXPORT_SYMBOL_GPL(sysfs_update_file);
