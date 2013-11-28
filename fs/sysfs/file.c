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
#include <linux/fsnotify.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/limits.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/mm.h>

#include "sysfs.h"

/*
 * There's one sysfs_open_file for each open file and one sysfs_open_dirent
 * for each sysfs_dirent with one or more open files.
 *
 * sysfs_dirent->s_attr.open points to sysfs_open_dirent.  s_attr.open is
 * protected by sysfs_open_dirent_lock.
 *
 * filp->private_data points to seq_file whose ->private points to
 * sysfs_open_file.  sysfs_open_files are chained at
 * sysfs_open_dirent->files, which is protected by sysfs_open_file_mutex.
 */
static DEFINE_SPINLOCK(sysfs_open_dirent_lock);
static DEFINE_MUTEX(sysfs_open_file_mutex);

struct sysfs_open_dirent {
	atomic_t		refcnt;
	atomic_t		event;
	wait_queue_head_t	poll;
	struct list_head	files; /* goes through sysfs_open_file.list */
};

struct sysfs_open_file {
	struct sysfs_dirent	*sd;
	struct file		*file;
	struct mutex		mutex;
	int			event;
	struct list_head	list;

	bool			mmapped;
	const struct vm_operations_struct *vm_ops;
};

static bool sysfs_is_bin(struct sysfs_dirent *sd)
{
	return sysfs_type(sd) == SYSFS_KOBJ_BIN_ATTR;
}

static struct sysfs_open_file *sysfs_of(struct file *file)
{
	return ((struct seq_file *)file->private_data)->private;
}

/*
 * Determine ktype->sysfs_ops for the given sysfs_dirent.  This function
 * must be called while holding an active reference.
 */
static const struct sysfs_ops *sysfs_file_ops(struct sysfs_dirent *sd)
{
	struct kobject *kobj = sd->s_parent->priv;

	if (!sysfs_ignore_lockdep(sd))
		lockdep_assert_held(sd);
	return kobj->ktype ? kobj->ktype->sysfs_ops : NULL;
}

/*
 * Reads on sysfs are handled through seq_file, which takes care of hairy
 * details like buffering and seeking.  The following function pipes
 * sysfs_ops->show() result through seq_file.
 */
static int sysfs_seq_show(struct seq_file *sf, void *v)
{
	struct sysfs_open_file *of = sf->private;
	struct kobject *kobj = of->sd->s_parent->priv;
	const struct sysfs_ops *ops;
	char *buf;
	ssize_t count;

	/* acquire buffer and ensure that it's >= PAGE_SIZE */
	count = seq_get_buf(sf, &buf);
	if (count < PAGE_SIZE) {
		seq_commit(sf, -1);
		return 0;
	}

	/*
	 * Need @of->sd for attr and ops, its parent for kobj.  @of->mutex
	 * nests outside active ref and is just to ensure that the ops
	 * aren't called concurrently for the same open file.
	 */
	mutex_lock(&of->mutex);
	if (!sysfs_get_active(of->sd)) {
		mutex_unlock(&of->mutex);
		return -ENODEV;
	}

	of->event = atomic_read(&of->sd->s_attr.open->event);

	/*
	 * Lookup @ops and invoke show().  Control may reach here via seq
	 * file lseek even if @ops->show() isn't implemented.
	 */
	ops = sysfs_file_ops(of->sd);
	if (ops->show)
		count = ops->show(kobj, of->sd->priv, buf);
	else
		count = 0;

	sysfs_put_active(of->sd);
	mutex_unlock(&of->mutex);

	if (count < 0)
		return count;

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
	seq_commit(sf, count);
	return 0;
}

/*
 * Read method for bin files.  As reading a bin file can have side-effects,
 * the exact offset and bytes specified in read(2) call should be passed to
 * the read callback making it difficult to use seq_file.  Implement
 * simplistic custom buffering for bin files.
 */
static ssize_t sysfs_bin_read(struct file *file, char __user *userbuf,
			      size_t bytes, loff_t *off)
{
	struct sysfs_open_file *of = sysfs_of(file);
	struct bin_attribute *battr = of->sd->priv;
	struct kobject *kobj = of->sd->s_parent->priv;
	loff_t size = file_inode(file)->i_size;
	int count = min_t(size_t, bytes, PAGE_SIZE);
	loff_t offs = *off;
	char *buf;

	if (!bytes)
		return 0;

	if (size) {
		if (offs > size)
			return 0;
		if (offs + count > size)
			count = size - offs;
	}

	buf = kmalloc(count, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* need of->sd for battr, its parent for kobj */
	mutex_lock(&of->mutex);
	if (!sysfs_get_active(of->sd)) {
		count = -ENODEV;
		mutex_unlock(&of->mutex);
		goto out_free;
	}

	if (battr->read)
		count = battr->read(file, kobj, battr, buf, offs, count);
	else
		count = -EIO;

	sysfs_put_active(of->sd);
	mutex_unlock(&of->mutex);

	if (count < 0)
		goto out_free;

	if (copy_to_user(userbuf, buf, count)) {
		count = -EFAULT;
		goto out_free;
	}

	pr_debug("offs = %lld, *off = %lld, count = %d\n", offs, *off, count);

	*off = offs + count;

 out_free:
	kfree(buf);
	return count;
}

/**
 * flush_write_buffer - push buffer to kobject
 * @of: open file
 * @buf: data buffer for file
 * @off: file offset to write to
 * @count: number of bytes
 *
 * Get the correct pointers for the kobject and the attribute we're dealing
 * with, then call the store() method for it with @buf.
 */
static int flush_write_buffer(struct sysfs_open_file *of, char *buf, loff_t off,
			      size_t count)
{
	struct kobject *kobj = of->sd->s_parent->priv;
	int rc = 0;

	/*
	 * Need @of->sd for attr and ops, its parent for kobj.  @of->mutex
	 * nests outside active ref and is just to ensure that the ops
	 * aren't called concurrently for the same open file.
	 */
	mutex_lock(&of->mutex);
	if (!sysfs_get_active(of->sd)) {
		mutex_unlock(&of->mutex);
		return -ENODEV;
	}

	if (sysfs_is_bin(of->sd)) {
		struct bin_attribute *battr = of->sd->priv;

		rc = -EIO;
		if (battr->write)
			rc = battr->write(of->file, kobj, battr, buf, off,
					  count);
	} else {
		const struct sysfs_ops *ops = sysfs_file_ops(of->sd);

		rc = ops->store(kobj, of->sd->priv, buf, count);
	}

	sysfs_put_active(of->sd);
	mutex_unlock(&of->mutex);

	return rc;
}

/**
 * sysfs_write_file - write an attribute
 * @file: file pointer
 * @user_buf: data to write
 * @count: number of bytes
 * @ppos: starting offset
 *
 * Copy data in from userland and pass it to the matching
 * sysfs_ops->store() by invoking flush_write_buffer().
 *
 * There is no easy way for us to know if userspace is only doing a partial
 * write, so we don't support them. We expect the entire buffer to come on
 * the first write.  Hint: if you're writing a value, first read the file,
 * modify only the the value you're changing, then write entire buffer
 * back.
 */
static ssize_t sysfs_write_file(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct sysfs_open_file *of = sysfs_of(file);
	ssize_t len = min_t(size_t, count, PAGE_SIZE);
	loff_t size = file_inode(file)->i_size;
	char *buf;

	if (sysfs_is_bin(of->sd) && size) {
		if (size <= *ppos)
			return 0;
		len = min_t(ssize_t, len, size - *ppos);
	}

	if (!len)
		return 0;

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, len)) {
		len = -EFAULT;
		goto out_free;
	}
	buf[len] = '\0';	/* guarantee string termination */

	len = flush_write_buffer(of, buf, *ppos, len);
	if (len > 0)
		*ppos += len;
out_free:
	kfree(buf);
	return len;
}

static void sysfs_bin_vma_open(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct sysfs_open_file *of = sysfs_of(file);

	if (!of->vm_ops)
		return;

	if (!sysfs_get_active(of->sd))
		return;

	if (of->vm_ops->open)
		of->vm_ops->open(vma);

	sysfs_put_active(of->sd);
}

static int sysfs_bin_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct file *file = vma->vm_file;
	struct sysfs_open_file *of = sysfs_of(file);
	int ret;

	if (!of->vm_ops)
		return VM_FAULT_SIGBUS;

	if (!sysfs_get_active(of->sd))
		return VM_FAULT_SIGBUS;

	ret = VM_FAULT_SIGBUS;
	if (of->vm_ops->fault)
		ret = of->vm_ops->fault(vma, vmf);

	sysfs_put_active(of->sd);
	return ret;
}

static int sysfs_bin_page_mkwrite(struct vm_area_struct *vma,
				  struct vm_fault *vmf)
{
	struct file *file = vma->vm_file;
	struct sysfs_open_file *of = sysfs_of(file);
	int ret;

	if (!of->vm_ops)
		return VM_FAULT_SIGBUS;

	if (!sysfs_get_active(of->sd))
		return VM_FAULT_SIGBUS;

	ret = 0;
	if (of->vm_ops->page_mkwrite)
		ret = of->vm_ops->page_mkwrite(vma, vmf);
	else
		file_update_time(file);

	sysfs_put_active(of->sd);
	return ret;
}

static int sysfs_bin_access(struct vm_area_struct *vma, unsigned long addr,
			    void *buf, int len, int write)
{
	struct file *file = vma->vm_file;
	struct sysfs_open_file *of = sysfs_of(file);
	int ret;

	if (!of->vm_ops)
		return -EINVAL;

	if (!sysfs_get_active(of->sd))
		return -EINVAL;

	ret = -EINVAL;
	if (of->vm_ops->access)
		ret = of->vm_ops->access(vma, addr, buf, len, write);

	sysfs_put_active(of->sd);
	return ret;
}

#ifdef CONFIG_NUMA
static int sysfs_bin_set_policy(struct vm_area_struct *vma,
				struct mempolicy *new)
{
	struct file *file = vma->vm_file;
	struct sysfs_open_file *of = sysfs_of(file);
	int ret;

	if (!of->vm_ops)
		return 0;

	if (!sysfs_get_active(of->sd))
		return -EINVAL;

	ret = 0;
	if (of->vm_ops->set_policy)
		ret = of->vm_ops->set_policy(vma, new);

	sysfs_put_active(of->sd);
	return ret;
}

static struct mempolicy *sysfs_bin_get_policy(struct vm_area_struct *vma,
					      unsigned long addr)
{
	struct file *file = vma->vm_file;
	struct sysfs_open_file *of = sysfs_of(file);
	struct mempolicy *pol;

	if (!of->vm_ops)
		return vma->vm_policy;

	if (!sysfs_get_active(of->sd))
		return vma->vm_policy;

	pol = vma->vm_policy;
	if (of->vm_ops->get_policy)
		pol = of->vm_ops->get_policy(vma, addr);

	sysfs_put_active(of->sd);
	return pol;
}

static int sysfs_bin_migrate(struct vm_area_struct *vma, const nodemask_t *from,
			     const nodemask_t *to, unsigned long flags)
{
	struct file *file = vma->vm_file;
	struct sysfs_open_file *of = sysfs_of(file);
	int ret;

	if (!of->vm_ops)
		return 0;

	if (!sysfs_get_active(of->sd))
		return 0;

	ret = 0;
	if (of->vm_ops->migrate)
		ret = of->vm_ops->migrate(vma, from, to, flags);

	sysfs_put_active(of->sd);
	return ret;
}
#endif

static const struct vm_operations_struct sysfs_bin_vm_ops = {
	.open		= sysfs_bin_vma_open,
	.fault		= sysfs_bin_fault,
	.page_mkwrite	= sysfs_bin_page_mkwrite,
	.access		= sysfs_bin_access,
#ifdef CONFIG_NUMA
	.set_policy	= sysfs_bin_set_policy,
	.get_policy	= sysfs_bin_get_policy,
	.migrate	= sysfs_bin_migrate,
#endif
};

static int sysfs_bin_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sysfs_open_file *of = sysfs_of(file);
	struct bin_attribute *battr = of->sd->priv;
	struct kobject *kobj = of->sd->s_parent->priv;
	int rc;

	mutex_lock(&of->mutex);

	/* need of->sd for battr, its parent for kobj */
	rc = -ENODEV;
	if (!sysfs_get_active(of->sd))
		goto out_unlock;

	if (!battr->mmap)
		goto out_put;

	rc = battr->mmap(file, kobj, battr, vma);
	if (rc)
		goto out_put;

	/*
	 * PowerPC's pci_mmap of legacy_mem uses shmem_zero_setup()
	 * to satisfy versions of X which crash if the mmap fails: that
	 * substitutes a new vm_file, and we don't then want bin_vm_ops.
	 */
	if (vma->vm_file != file)
		goto out_put;

	rc = -EINVAL;
	if (of->mmapped && of->vm_ops != vma->vm_ops)
		goto out_put;

	/*
	 * It is not possible to successfully wrap close.
	 * So error if someone is trying to use close.
	 */
	rc = -EINVAL;
	if (vma->vm_ops && vma->vm_ops->close)
		goto out_put;

	rc = 0;
	of->mmapped = 1;
	of->vm_ops = vma->vm_ops;
	vma->vm_ops = &sysfs_bin_vm_ops;
out_put:
	sysfs_put_active(of->sd);
out_unlock:
	mutex_unlock(&of->mutex);

	return rc;
}

/**
 *	sysfs_get_open_dirent - get or create sysfs_open_dirent
 *	@sd: target sysfs_dirent
 *	@of: sysfs_open_file for this instance of open
 *
 *	If @sd->s_attr.open exists, increment its reference count;
 *	otherwise, create one.  @of is chained to the files list.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int sysfs_get_open_dirent(struct sysfs_dirent *sd,
				 struct sysfs_open_file *of)
{
	struct sysfs_open_dirent *od, *new_od = NULL;

 retry:
	mutex_lock(&sysfs_open_file_mutex);
	spin_lock_irq(&sysfs_open_dirent_lock);

	if (!sd->s_attr.open && new_od) {
		sd->s_attr.open = new_od;
		new_od = NULL;
	}

	od = sd->s_attr.open;
	if (od) {
		atomic_inc(&od->refcnt);
		list_add_tail(&of->list, &od->files);
	}

	spin_unlock_irq(&sysfs_open_dirent_lock);
	mutex_unlock(&sysfs_open_file_mutex);

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
	INIT_LIST_HEAD(&new_od->files);
	goto retry;
}

/**
 *	sysfs_put_open_dirent - put sysfs_open_dirent
 *	@sd: target sysfs_dirent
 *	@of: associated sysfs_open_file
 *
 *	Put @sd->s_attr.open and unlink @of from the files list.  If
 *	reference count reaches zero, disassociate and free it.
 *
 *	LOCKING:
 *	None.
 */
static void sysfs_put_open_dirent(struct sysfs_dirent *sd,
				  struct sysfs_open_file *of)
{
	struct sysfs_open_dirent *od = sd->s_attr.open;
	unsigned long flags;

	mutex_lock(&sysfs_open_file_mutex);
	spin_lock_irqsave(&sysfs_open_dirent_lock, flags);

	if (of)
		list_del(&of->list);

	if (atomic_dec_and_test(&od->refcnt))
		sd->s_attr.open = NULL;
	else
		od = NULL;

	spin_unlock_irqrestore(&sysfs_open_dirent_lock, flags);
	mutex_unlock(&sysfs_open_file_mutex);

	kfree(od);
}

static int sysfs_open_file(struct inode *inode, struct file *file)
{
	struct sysfs_dirent *attr_sd = file->f_path.dentry->d_fsdata;
	struct kobject *kobj = attr_sd->s_parent->priv;
	struct sysfs_open_file *of;
	bool has_read, has_write, has_mmap;
	int error = -EACCES;

	/* need attr_sd for attr and ops, its parent for kobj */
	if (!sysfs_get_active(attr_sd))
		return -ENODEV;

	if (sysfs_is_bin(attr_sd)) {
		struct bin_attribute *battr = attr_sd->priv;

		has_read = battr->read || battr->mmap;
		has_write = battr->write || battr->mmap;
		has_mmap = battr->mmap;
	} else {
		const struct sysfs_ops *ops = sysfs_file_ops(attr_sd);

		/* every kobject with an attribute needs a ktype assigned */
		if (WARN(!ops, KERN_ERR
			 "missing sysfs attribute operations for kobject: %s\n",
			 kobject_name(kobj)))
			goto err_out;

		has_read = ops->show;
		has_write = ops->store;
		has_mmap = false;
	}

	/* check perms and supported operations */
	if ((file->f_mode & FMODE_WRITE) &&
	    (!(inode->i_mode & S_IWUGO) || !has_write))
		goto err_out;

	if ((file->f_mode & FMODE_READ) &&
	    (!(inode->i_mode & S_IRUGO) || !has_read))
		goto err_out;

	/* allocate a sysfs_open_file for the file */
	error = -ENOMEM;
	of = kzalloc(sizeof(struct sysfs_open_file), GFP_KERNEL);
	if (!of)
		goto err_out;

	/*
	 * The following is done to give a different lockdep key to
	 * @of->mutex for files which implement mmap.  This is a rather
	 * crude way to avoid false positive lockdep warning around
	 * mm->mmap_sem - mmap nests @of->mutex under mm->mmap_sem and
	 * reading /sys/block/sda/trace/act_mask grabs sr_mutex, under
	 * which mm->mmap_sem nests, while holding @of->mutex.  As each
	 * open file has a separate mutex, it's okay as long as those don't
	 * happen on the same file.  At this point, we can't easily give
	 * each file a separate locking class.  Let's differentiate on
	 * whether the file has mmap or not for now.
	 */
	if (has_mmap)
		mutex_init(&of->mutex);
	else
		mutex_init(&of->mutex);

	of->sd = attr_sd;
	of->file = file;

	/*
	 * Always instantiate seq_file even if read access doesn't use
	 * seq_file or is not requested.  This unifies private data access
	 * and readable regular files are the vast majority anyway.
	 */
	if (sysfs_is_bin(attr_sd))
		error = single_open(file, NULL, of);
	else
		error = single_open(file, sysfs_seq_show, of);
	if (error)
		goto err_free;

	/* seq_file clears PWRITE unconditionally, restore it if WRITE */
	if (file->f_mode & FMODE_WRITE)
		file->f_mode |= FMODE_PWRITE;

	/* make sure we have open dirent struct */
	error = sysfs_get_open_dirent(attr_sd, of);
	if (error)
		goto err_close;

	/* open succeeded, put active references */
	sysfs_put_active(attr_sd);
	return 0;

err_close:
	single_release(inode, file);
err_free:
	kfree(of);
err_out:
	sysfs_put_active(attr_sd);
	return error;
}

static int sysfs_release(struct inode *inode, struct file *filp)
{
	struct sysfs_dirent *sd = filp->f_path.dentry->d_fsdata;
	struct sysfs_open_file *of = sysfs_of(filp);

	sysfs_put_open_dirent(sd, of);
	single_release(inode, filp);
	kfree(of);

	return 0;
}

void sysfs_unmap_bin_file(struct sysfs_dirent *sd)
{
	struct sysfs_open_dirent *od;
	struct sysfs_open_file *of;

	if (!sysfs_is_bin(sd))
		return;

	spin_lock_irq(&sysfs_open_dirent_lock);
	od = sd->s_attr.open;
	if (od)
		atomic_inc(&od->refcnt);
	spin_unlock_irq(&sysfs_open_dirent_lock);
	if (!od)
		return;

	mutex_lock(&sysfs_open_file_mutex);
	list_for_each_entry(of, &od->files, list) {
		struct inode *inode = file_inode(of->file);
		unmap_mapping_range(inode->i_mapping, 0, 0, 1);
	}
	mutex_unlock(&sysfs_open_file_mutex);

	sysfs_put_open_dirent(sd, NULL);
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
	struct sysfs_open_file *of = sysfs_of(filp);
	struct sysfs_dirent *attr_sd = filp->f_path.dentry->d_fsdata;
	struct sysfs_open_dirent *od = attr_sd->s_attr.open;

	/* need parent for the kobj, grab both */
	if (!sysfs_get_active(attr_sd))
		goto trigger;

	poll_wait(filp, &od->poll, wait);

	sysfs_put_active(attr_sd);

	if (of->event != atomic_read(&od->event))
		goto trigger;

	return DEFAULT_POLLMASK;

 trigger:
	return DEFAULT_POLLMASK|POLLERR|POLLPRI;
}

void sysfs_notify_dirent(struct sysfs_dirent *sd)
{
	struct sysfs_open_dirent *od;
	unsigned long flags;

	spin_lock_irqsave(&sysfs_open_dirent_lock, flags);

	if (!WARN_ON(sysfs_type(sd) != SYSFS_KOBJ_ATTR)) {
		od = sd->s_attr.open;
		if (od) {
			atomic_inc(&od->event);
			wake_up_interruptible(&od->poll);
		}
	}

	spin_unlock_irqrestore(&sysfs_open_dirent_lock, flags);
}
EXPORT_SYMBOL_GPL(sysfs_notify_dirent);

void sysfs_notify(struct kobject *k, const char *dir, const char *attr)
{
	struct sysfs_dirent *sd = k->sd;

	mutex_lock(&sysfs_mutex);

	if (sd && dir)
		sd = sysfs_find_dirent(sd, dir, NULL);
	if (sd && attr)
		sd = sysfs_find_dirent(sd, attr, NULL);
	if (sd)
		sysfs_notify_dirent(sd);

	mutex_unlock(&sysfs_mutex);
}
EXPORT_SYMBOL_GPL(sysfs_notify);

const struct file_operations sysfs_file_operations = {
	.read		= seq_read,
	.write		= sysfs_write_file,
	.llseek		= generic_file_llseek,
	.open		= sysfs_open_file,
	.release	= sysfs_release,
	.poll		= sysfs_poll,
};

const struct file_operations sysfs_bin_operations = {
	.read		= sysfs_bin_read,
	.write		= sysfs_write_file,
	.llseek		= generic_file_llseek,
	.mmap		= sysfs_bin_mmap,
	.open		= sysfs_open_file,
	.release	= sysfs_release,
	.poll		= sysfs_poll,
};

int sysfs_add_file_mode_ns(struct sysfs_dirent *dir_sd,
			   const struct attribute *attr, int type,
			   umode_t amode, const void *ns)
{
	umode_t mode = (amode & S_IALLUGO) | S_IFREG;
	struct sysfs_addrm_cxt acxt;
	struct sysfs_dirent *sd;
	int rc;

	sd = sysfs_new_dirent(attr->name, mode, type);
	if (!sd)
		return -ENOMEM;

	sd->s_ns = ns;
	sd->priv = (void *)attr;
	sysfs_dirent_init_lockdep(sd);

	sysfs_addrm_start(&acxt);
	rc = sysfs_add_one(&acxt, sd, dir_sd);
	sysfs_addrm_finish(&acxt);

	if (rc)
		sysfs_put(sd);

	return rc;
}


int sysfs_add_file(struct sysfs_dirent *dir_sd, const struct attribute *attr,
		   int type)
{
	return sysfs_add_file_mode_ns(dir_sd, attr, type, attr->mode, NULL);
}

/**
 * sysfs_create_file_ns - create an attribute file for an object with custom ns
 * @kobj: object we're creating for
 * @attr: attribute descriptor
 * @ns: namespace the new file should belong to
 */
int sysfs_create_file_ns(struct kobject *kobj, const struct attribute *attr,
			 const void *ns)
{
	BUG_ON(!kobj || !kobj->sd || !attr);

	return sysfs_add_file_mode_ns(kobj->sd, attr, SYSFS_KOBJ_ATTR,
				      attr->mode, ns);

}
EXPORT_SYMBOL_GPL(sysfs_create_file_ns);

int sysfs_create_files(struct kobject *kobj, const struct attribute **ptr)
{
	int err = 0;
	int i;

	for (i = 0; ptr[i] && !err; i++)
		err = sysfs_create_file(kobj, ptr[i]);
	if (err)
		while (--i >= 0)
			sysfs_remove_file(kobj, ptr[i]);
	return err;
}
EXPORT_SYMBOL_GPL(sysfs_create_files);

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
int sysfs_chmod_file(struct kobject *kobj, const struct attribute *attr,
		     umode_t mode)
{
	struct sysfs_dirent *sd;
	struct iattr newattrs;
	int rc;

	sd = sysfs_get_dirent(kobj->sd, attr->name);
	if (!sd)
		return -ENOENT;

	newattrs.ia_mode = (mode & S_IALLUGO) | (sd->s_mode & ~S_IALLUGO);
	newattrs.ia_valid = ATTR_MODE;

	rc = kernfs_setattr(sd, &newattrs);

	sysfs_put(sd);
	return rc;
}
EXPORT_SYMBOL_GPL(sysfs_chmod_file);

/**
 * sysfs_remove_file_ns - remove an object attribute with a custom ns tag
 * @kobj: object we're acting for
 * @attr: attribute descriptor
 * @ns: namespace tag of the file to remove
 *
 * Hash the attribute name and namespace tag and kill the victim.
 */
void sysfs_remove_file_ns(struct kobject *kobj, const struct attribute *attr,
			  const void *ns)
{
	struct sysfs_dirent *dir_sd = kobj->sd;

	kernfs_remove_by_name_ns(dir_sd, attr->name, ns);
}
EXPORT_SYMBOL_GPL(sysfs_remove_file_ns);

void sysfs_remove_files(struct kobject *kobj, const struct attribute **ptr)
{
	int i;
	for (i = 0; ptr[i]; i++)
		sysfs_remove_file(kobj, ptr[i]);
}
EXPORT_SYMBOL_GPL(sysfs_remove_files);

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
		kernfs_remove_by_name(dir_sd, attr->name);
		sysfs_put(dir_sd);
	}
}
EXPORT_SYMBOL_GPL(sysfs_remove_file_from_group);

/**
 *	sysfs_create_bin_file - create binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 */
int sysfs_create_bin_file(struct kobject *kobj,
			  const struct bin_attribute *attr)
{
	BUG_ON(!kobj || !kobj->sd || !attr);

	return sysfs_add_file(kobj->sd, &attr->attr, SYSFS_KOBJ_BIN_ATTR);
}
EXPORT_SYMBOL_GPL(sysfs_create_bin_file);

/**
 *	sysfs_remove_bin_file - remove binary file for object.
 *	@kobj:	object.
 *	@attr:	attribute descriptor.
 */
void sysfs_remove_bin_file(struct kobject *kobj,
			   const struct bin_attribute *attr)
{
	kernfs_remove_by_name(kobj->sd, attr->attr.name);
}
EXPORT_SYMBOL_GPL(sysfs_remove_bin_file);

struct sysfs_schedule_callback_struct {
	struct list_head	workq_list;
	struct kobject		*kobj;
	void			(*func)(void *);
	void			*data;
	struct module		*owner;
	struct work_struct	work;
};

static struct workqueue_struct *sysfs_workqueue;
static DEFINE_MUTEX(sysfs_workq_mutex);
static LIST_HEAD(sysfs_workq);
static void sysfs_schedule_callback_work(struct work_struct *work)
{
	struct sysfs_schedule_callback_struct *ss = container_of(work,
			struct sysfs_schedule_callback_struct, work);

	(ss->func)(ss->data);
	kobject_put(ss->kobj);
	module_put(ss->owner);
	mutex_lock(&sysfs_workq_mutex);
	list_del(&ss->workq_list);
	mutex_unlock(&sysfs_workq_mutex);
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
 * be allocated, -ENODEV if a reference to @owner isn't available,
 * -EAGAIN if a callback has already been scheduled for @kobj.
 */
int sysfs_schedule_callback(struct kobject *kobj, void (*func)(void *),
		void *data, struct module *owner)
{
	struct sysfs_schedule_callback_struct *ss, *tmp;

	if (!try_module_get(owner))
		return -ENODEV;

	mutex_lock(&sysfs_workq_mutex);
	list_for_each_entry_safe(ss, tmp, &sysfs_workq, workq_list)
		if (ss->kobj == kobj) {
			module_put(owner);
			mutex_unlock(&sysfs_workq_mutex);
			return -EAGAIN;
		}
	mutex_unlock(&sysfs_workq_mutex);

	if (sysfs_workqueue == NULL) {
		sysfs_workqueue = create_singlethread_workqueue("sysfsd");
		if (sysfs_workqueue == NULL) {
			module_put(owner);
			return -ENOMEM;
		}
	}

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
	INIT_LIST_HEAD(&ss->workq_list);
	mutex_lock(&sysfs_workq_mutex);
	list_add_tail(&ss->workq_list, &sysfs_workq);
	mutex_unlock(&sysfs_workq_mutex);
	queue_work(sysfs_workqueue, &ss->work);
	return 0;
}
EXPORT_SYMBOL_GPL(sysfs_schedule_callback);
