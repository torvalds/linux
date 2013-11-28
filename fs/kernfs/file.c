/*
 * fs/kernfs/file.c - kernfs file implementation
 *
 * Copyright (c) 2001-3 Patrick Mochel
 * Copyright (c) 2007 SUSE Linux Products GmbH
 * Copyright (c) 2007, 2013 Tejun Heo <tj@kernel.org>
 *
 * This file is released under the GPLv2.
 */

#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/pagemap.h>
#include <linux/poll.h>
#include <linux/sched.h>

#include "kernfs-internal.h"

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

static struct sysfs_open_file *sysfs_of(struct file *file)
{
	return ((struct seq_file *)file->private_data)->private;
}

/*
 * Determine the kernfs_ops for the given sysfs_dirent.  This function must
 * be called while holding an active reference.
 */
static const struct kernfs_ops *kernfs_ops(struct sysfs_dirent *sd)
{
	if (sd->s_flags & SYSFS_FLAG_LOCKDEP)
		lockdep_assert_held(sd);
	return sd->s_attr.ops;
}

static void *kernfs_seq_start(struct seq_file *sf, loff_t *ppos)
{
	struct sysfs_open_file *of = sf->private;
	const struct kernfs_ops *ops;

	/*
	 * @of->mutex nests outside active ref and is just to ensure that
	 * the ops aren't called concurrently for the same open file.
	 */
	mutex_lock(&of->mutex);
	if (!sysfs_get_active(of->sd))
		return ERR_PTR(-ENODEV);

	ops = kernfs_ops(of->sd);
	if (ops->seq_start) {
		return ops->seq_start(sf, ppos);
	} else {
		/*
		 * The same behavior and code as single_open().  Returns
		 * !NULL if pos is at the beginning; otherwise, NULL.
		 */
		return NULL + !*ppos;
	}
}

static void *kernfs_seq_next(struct seq_file *sf, void *v, loff_t *ppos)
{
	struct sysfs_open_file *of = sf->private;
	const struct kernfs_ops *ops = kernfs_ops(of->sd);

	if (ops->seq_next) {
		return ops->seq_next(sf, v, ppos);
	} else {
		/*
		 * The same behavior and code as single_open(), always
		 * terminate after the initial read.
		 */
		++*ppos;
		return NULL;
	}
}

static void kernfs_seq_stop(struct seq_file *sf, void *v)
{
	struct sysfs_open_file *of = sf->private;
	const struct kernfs_ops *ops = kernfs_ops(of->sd);

	if (ops->seq_stop)
		ops->seq_stop(sf, v);

	sysfs_put_active(of->sd);
	mutex_unlock(&of->mutex);
}

static int kernfs_seq_show(struct seq_file *sf, void *v)
{
	struct sysfs_open_file *of = sf->private;

	of->event = atomic_read(&of->sd->s_attr.open->event);

	return of->sd->s_attr.ops->seq_show(sf, v);
}

static const struct seq_operations kernfs_seq_ops = {
	.start = kernfs_seq_start,
	.next = kernfs_seq_next,
	.stop = kernfs_seq_stop,
	.show = kernfs_seq_show,
};

/*
 * As reading a bin file can have side-effects, the exact offset and bytes
 * specified in read(2) call should be passed to the read callback making
 * it difficult to use seq_file.  Implement simplistic custom buffering for
 * bin files.
 */
static ssize_t kernfs_file_direct_read(struct sysfs_open_file *of,
				       char __user *user_buf, size_t count,
				       loff_t *ppos)
{
	ssize_t len = min_t(size_t, count, PAGE_SIZE);
	const struct kernfs_ops *ops;
	char *buf;

	buf = kmalloc(len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/*
	 * @of->mutex nests outside active ref and is just to ensure that
	 * the ops aren't called concurrently for the same open file.
	 */
	mutex_lock(&of->mutex);
	if (!sysfs_get_active(of->sd)) {
		len = -ENODEV;
		mutex_unlock(&of->mutex);
		goto out_free;
	}

	ops = kernfs_ops(of->sd);
	if (ops->read)
		len = ops->read(of, buf, len, *ppos);
	else
		len = -EINVAL;

	sysfs_put_active(of->sd);
	mutex_unlock(&of->mutex);

	if (len < 0)
		goto out_free;

	if (copy_to_user(user_buf, buf, len)) {
		len = -EFAULT;
		goto out_free;
	}

	*ppos += len;

 out_free:
	kfree(buf);
	return len;
}

/**
 * kernfs_file_read - kernfs vfs read callback
 * @file: file pointer
 * @user_buf: data to write
 * @count: number of bytes
 * @ppos: starting offset
 */
static ssize_t kernfs_file_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct sysfs_open_file *of = sysfs_of(file);

	if (of->sd->s_flags & SYSFS_FLAG_HAS_SEQ_SHOW)
		return seq_read(file, user_buf, count, ppos);
	else
		return kernfs_file_direct_read(of, user_buf, count, ppos);
}

/**
 * kernfs_file_write - kernfs vfs write callback
 * @file: file pointer
 * @user_buf: data to write
 * @count: number of bytes
 * @ppos: starting offset
 *
 * Copy data in from userland and pass it to the matching kernfs write
 * operation.
 *
 * There is no easy way for us to know if userspace is only doing a partial
 * write, so we don't support them. We expect the entire buffer to come on
 * the first write.  Hint: if you're writing a value, first read the file,
 * modify only the the value you're changing, then write entire buffer
 * back.
 */
static ssize_t kernfs_file_write(struct file *file, const char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct sysfs_open_file *of = sysfs_of(file);
	ssize_t len = min_t(size_t, count, PAGE_SIZE);
	const struct kernfs_ops *ops;
	char *buf;

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, user_buf, len)) {
		len = -EFAULT;
		goto out_free;
	}
	buf[len] = '\0';	/* guarantee string termination */

	/*
	 * @of->mutex nests outside active ref and is just to ensure that
	 * the ops aren't called concurrently for the same open file.
	 */
	mutex_lock(&of->mutex);
	if (!sysfs_get_active(of->sd)) {
		mutex_unlock(&of->mutex);
		len = -ENODEV;
		goto out_free;
	}

	ops = kernfs_ops(of->sd);
	if (ops->write)
		len = ops->write(of, buf, len, *ppos);
	else
		len = -EINVAL;

	sysfs_put_active(of->sd);
	mutex_unlock(&of->mutex);

	if (len > 0)
		*ppos += len;
out_free:
	kfree(buf);
	return len;
}

static void kernfs_vma_open(struct vm_area_struct *vma)
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

static int kernfs_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
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

static int kernfs_vma_page_mkwrite(struct vm_area_struct *vma,
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

static int kernfs_vma_access(struct vm_area_struct *vma, unsigned long addr,
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
static int kernfs_vma_set_policy(struct vm_area_struct *vma,
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

static struct mempolicy *kernfs_vma_get_policy(struct vm_area_struct *vma,
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

static int kernfs_vma_migrate(struct vm_area_struct *vma,
			      const nodemask_t *from, const nodemask_t *to,
			      unsigned long flags)
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

static const struct vm_operations_struct kernfs_vm_ops = {
	.open		= kernfs_vma_open,
	.fault		= kernfs_vma_fault,
	.page_mkwrite	= kernfs_vma_page_mkwrite,
	.access		= kernfs_vma_access,
#ifdef CONFIG_NUMA
	.set_policy	= kernfs_vma_set_policy,
	.get_policy	= kernfs_vma_get_policy,
	.migrate	= kernfs_vma_migrate,
#endif
};

static int kernfs_file_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct sysfs_open_file *of = sysfs_of(file);
	const struct kernfs_ops *ops;
	int rc;

	mutex_lock(&of->mutex);

	rc = -ENODEV;
	if (!sysfs_get_active(of->sd))
		goto out_unlock;

	ops = kernfs_ops(of->sd);
	if (ops->mmap)
		rc = ops->mmap(of, vma);
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
	vma->vm_ops = &kernfs_vm_ops;
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

static int kernfs_file_open(struct inode *inode, struct file *file)
{
	struct sysfs_dirent *attr_sd = file->f_path.dentry->d_fsdata;
	const struct kernfs_ops *ops;
	struct sysfs_open_file *of;
	bool has_read, has_write, has_mmap;
	int error = -EACCES;

	if (!sysfs_get_active(attr_sd))
		return -ENODEV;

	ops = kernfs_ops(attr_sd);

	has_read = ops->seq_show || ops->read || ops->mmap;
	has_write = ops->write || ops->mmap;
	has_mmap = ops->mmap;

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
	if (ops->seq_show)
		error = seq_open(file, &kernfs_seq_ops);
	else
		error = seq_open(file, NULL);
	if (error)
		goto err_free;

	((struct seq_file *)file->private_data)->private = of;

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
	seq_release(inode, file);
err_free:
	kfree(of);
err_out:
	sysfs_put_active(attr_sd);
	return error;
}

static int kernfs_file_release(struct inode *inode, struct file *filp)
{
	struct sysfs_dirent *sd = filp->f_path.dentry->d_fsdata;
	struct sysfs_open_file *of = sysfs_of(filp);

	sysfs_put_open_dirent(sd, of);
	seq_release(inode, filp);
	kfree(of);

	return 0;
}

void sysfs_unmap_bin_file(struct sysfs_dirent *sd)
{
	struct sysfs_open_dirent *od;
	struct sysfs_open_file *of;

	if (!(sd->s_flags & SYSFS_FLAG_HAS_MMAP))
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
static unsigned int kernfs_file_poll(struct file *filp, poll_table *wait)
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

/**
 * kernfs_notify - notify a kernfs file
 * @sd: file to notify
 *
 * Notify @sd such that poll(2) on @sd wakes up.
 */
void kernfs_notify(struct sysfs_dirent *sd)
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
EXPORT_SYMBOL_GPL(kernfs_notify);

const struct file_operations kernfs_file_operations = {
	.read		= kernfs_file_read,
	.write		= kernfs_file_write,
	.llseek		= generic_file_llseek,
	.mmap		= kernfs_file_mmap,
	.open		= kernfs_file_open,
	.release	= kernfs_file_release,
	.poll		= kernfs_file_poll,
};

/**
 * kernfs_create_file_ns_key - create a file
 * @parent: directory to create the file in
 * @name: name of the file
 * @mode: mode of the file
 * @size: size of the file
 * @ops: kernfs operations for the file
 * @priv: private data for the file
 * @ns: optional namespace tag of the file
 * @key: lockdep key for the file's active_ref, %NULL to disable lockdep
 *
 * Returns the created node on success, ERR_PTR() value on error.
 */
struct sysfs_dirent *kernfs_create_file_ns_key(struct sysfs_dirent *parent,
					       const char *name,
					       umode_t mode, loff_t size,
					       const struct kernfs_ops *ops,
					       void *priv, const void *ns,
					       struct lock_class_key *key)
{
	struct sysfs_addrm_cxt acxt;
	struct sysfs_dirent *sd;
	int rc;

	sd = sysfs_new_dirent(name, (mode & S_IALLUGO) | S_IFREG,
			      SYSFS_KOBJ_ATTR);
	if (!sd)
		return ERR_PTR(-ENOMEM);

	sd->s_attr.ops = ops;
	sd->s_attr.size = size;
	sd->s_ns = ns;
	sd->priv = priv;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (key) {
		lockdep_init_map(&sd->dep_map, "s_active", key, 0);
		sd->s_flags |= SYSFS_FLAG_LOCKDEP;
	}
#endif

	/*
	 * sd->s_attr.ops is accesible only while holding active ref.  We
	 * need to know whether some ops are implemented outside active
	 * ref.  Cache their existence in flags.
	 */
	if (ops->seq_show)
		sd->s_flags |= SYSFS_FLAG_HAS_SEQ_SHOW;
	if (ops->mmap)
		sd->s_flags |= SYSFS_FLAG_HAS_MMAP;

	sysfs_addrm_start(&acxt);
	rc = sysfs_add_one(&acxt, sd, parent);
	sysfs_addrm_finish(&acxt);

	if (rc) {
		kernfs_put(sd);
		return ERR_PTR(rc);
	}
	return sd;
}
