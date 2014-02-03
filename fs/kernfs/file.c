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
#include <linux/sched.h>

#include "kernfs-internal.h"

/*
 * There's one kernfs_open_file for each open file and one kernfs_open_node
 * for each kernfs_node with one or more open files.
 *
 * kernfs_node->attr.open points to kernfs_open_node.  attr.open is
 * protected by kernfs_open_node_lock.
 *
 * filp->private_data points to seq_file whose ->private points to
 * kernfs_open_file.  kernfs_open_files are chained at
 * kernfs_open_node->files, which is protected by kernfs_open_file_mutex.
 */
static DEFINE_SPINLOCK(kernfs_open_node_lock);
static DEFINE_MUTEX(kernfs_open_file_mutex);

struct kernfs_open_node {
	atomic_t		refcnt;
	atomic_t		event;
	wait_queue_head_t	poll;
	struct list_head	files; /* goes through kernfs_open_file.list */
};

static struct kernfs_open_file *kernfs_of(struct file *file)
{
	return ((struct seq_file *)file->private_data)->private;
}

/*
 * Determine the kernfs_ops for the given kernfs_node.  This function must
 * be called while holding an active reference.
 */
static const struct kernfs_ops *kernfs_ops(struct kernfs_node *kn)
{
	if (kn->flags & KERNFS_LOCKDEP)
		lockdep_assert_held(kn);
	return kn->attr.ops;
}

/*
 * As kernfs_seq_stop() is also called after kernfs_seq_start() or
 * kernfs_seq_next() failure, it needs to distinguish whether it's stopping
 * a seq_file iteration which is fully initialized with an active reference
 * or an aborted kernfs_seq_start() due to get_active failure.  The
 * position pointer is the only context for each seq_file iteration and
 * thus the stop condition should be encoded in it.  As the return value is
 * directly visible to userland, ERR_PTR(-ENODEV) is the only acceptable
 * choice to indicate get_active failure.
 *
 * Unfortunately, this is complicated due to the optional custom seq_file
 * operations which may return ERR_PTR(-ENODEV) too.  kernfs_seq_stop()
 * can't distinguish whether ERR_PTR(-ENODEV) is from get_active failure or
 * custom seq_file operations and thus can't decide whether put_active
 * should be performed or not only on ERR_PTR(-ENODEV).
 *
 * This is worked around by factoring out the custom seq_stop() and
 * put_active part into kernfs_seq_stop_active(), skipping it from
 * kernfs_seq_stop() if ERR_PTR(-ENODEV) while invoking it directly after
 * custom seq_file operations fail with ERR_PTR(-ENODEV) - this ensures
 * that kernfs_seq_stop_active() is skipped only after get_active failure.
 */
static void kernfs_seq_stop_active(struct seq_file *sf, void *v)
{
	struct kernfs_open_file *of = sf->private;
	const struct kernfs_ops *ops = kernfs_ops(of->kn);

	if (ops->seq_stop)
		ops->seq_stop(sf, v);
	kernfs_put_active(of->kn);
}

static void *kernfs_seq_start(struct seq_file *sf, loff_t *ppos)
{
	struct kernfs_open_file *of = sf->private;
	const struct kernfs_ops *ops;

	/*
	 * @of->mutex nests outside active ref and is just to ensure that
	 * the ops aren't called concurrently for the same open file.
	 */
	mutex_lock(&of->mutex);
	if (!kernfs_get_active(of->kn))
		return ERR_PTR(-ENODEV);

	ops = kernfs_ops(of->kn);
	if (ops->seq_start) {
		void *next = ops->seq_start(sf, ppos);
		/* see the comment above kernfs_seq_stop_active() */
		if (next == ERR_PTR(-ENODEV))
			kernfs_seq_stop_active(sf, next);
		return next;
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
	struct kernfs_open_file *of = sf->private;
	const struct kernfs_ops *ops = kernfs_ops(of->kn);

	if (ops->seq_next) {
		void *next = ops->seq_next(sf, v, ppos);
		/* see the comment above kernfs_seq_stop_active() */
		if (next == ERR_PTR(-ENODEV))
			kernfs_seq_stop_active(sf, next);
		return next;
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
	struct kernfs_open_file *of = sf->private;

	if (v != ERR_PTR(-ENODEV))
		kernfs_seq_stop_active(sf, v);
	mutex_unlock(&of->mutex);
}

static int kernfs_seq_show(struct seq_file *sf, void *v)
{
	struct kernfs_open_file *of = sf->private;

	of->event = atomic_read(&of->kn->attr.open->event);

	return of->kn->attr.ops->seq_show(sf, v);
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
static ssize_t kernfs_file_direct_read(struct kernfs_open_file *of,
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
	if (!kernfs_get_active(of->kn)) {
		len = -ENODEV;
		mutex_unlock(&of->mutex);
		goto out_free;
	}

	ops = kernfs_ops(of->kn);
	if (ops->read)
		len = ops->read(of, buf, len, *ppos);
	else
		len = -EINVAL;

	kernfs_put_active(of->kn);
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
 * kernfs_fop_read - kernfs vfs read callback
 * @file: file pointer
 * @user_buf: data to write
 * @count: number of bytes
 * @ppos: starting offset
 */
static ssize_t kernfs_fop_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct kernfs_open_file *of = kernfs_of(file);

	if (of->kn->flags & KERNFS_HAS_SEQ_SHOW)
		return seq_read(file, user_buf, count, ppos);
	else
		return kernfs_file_direct_read(of, user_buf, count, ppos);
}

/**
 * kernfs_fop_write - kernfs vfs write callback
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
static ssize_t kernfs_fop_write(struct file *file, const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct kernfs_open_file *of = kernfs_of(file);
	const struct kernfs_ops *ops;
	char *buf = NULL;
	ssize_t len;

	/*
	 * @of->mutex nests outside active ref and is just to ensure that
	 * the ops aren't called concurrently for the same open file.
	 */
	mutex_lock(&of->mutex);
	if (!kernfs_get_active(of->kn)) {
		mutex_unlock(&of->mutex);
		return -ENODEV;
	}

	ops = kernfs_ops(of->kn);
	if (!ops->write) {
		len = -EINVAL;
		goto out_unlock;
	}

	if (ops->atomic_write_len) {
		len = count;
		if (len > ops->atomic_write_len) {
			len = -E2BIG;
			goto out_unlock;
		}
	} else {
		len = min_t(size_t, count, PAGE_SIZE);
	}

	buf = kmalloc(len + 1, GFP_KERNEL);
	if (!buf) {
		len = -ENOMEM;
		goto out_unlock;
	}

	if (copy_from_user(buf, user_buf, len)) {
		len = -EFAULT;
		goto out_unlock;
	}
	buf[len] = '\0';	/* guarantee string termination */

	len = ops->write(of, buf, len, *ppos);
out_unlock:
	kernfs_put_active(of->kn);
	mutex_unlock(&of->mutex);

	if (len > 0)
		*ppos += len;

	kfree(buf);
	return len;
}

static void kernfs_vma_open(struct vm_area_struct *vma)
{
	struct file *file = vma->vm_file;
	struct kernfs_open_file *of = kernfs_of(file);

	if (!of->vm_ops)
		return;

	if (!kernfs_get_active(of->kn))
		return;

	if (of->vm_ops->open)
		of->vm_ops->open(vma);

	kernfs_put_active(of->kn);
}

static int kernfs_vma_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct file *file = vma->vm_file;
	struct kernfs_open_file *of = kernfs_of(file);
	int ret;

	if (!of->vm_ops)
		return VM_FAULT_SIGBUS;

	if (!kernfs_get_active(of->kn))
		return VM_FAULT_SIGBUS;

	ret = VM_FAULT_SIGBUS;
	if (of->vm_ops->fault)
		ret = of->vm_ops->fault(vma, vmf);

	kernfs_put_active(of->kn);
	return ret;
}

static int kernfs_vma_page_mkwrite(struct vm_area_struct *vma,
				   struct vm_fault *vmf)
{
	struct file *file = vma->vm_file;
	struct kernfs_open_file *of = kernfs_of(file);
	int ret;

	if (!of->vm_ops)
		return VM_FAULT_SIGBUS;

	if (!kernfs_get_active(of->kn))
		return VM_FAULT_SIGBUS;

	ret = 0;
	if (of->vm_ops->page_mkwrite)
		ret = of->vm_ops->page_mkwrite(vma, vmf);
	else
		file_update_time(file);

	kernfs_put_active(of->kn);
	return ret;
}

static int kernfs_vma_access(struct vm_area_struct *vma, unsigned long addr,
			     void *buf, int len, int write)
{
	struct file *file = vma->vm_file;
	struct kernfs_open_file *of = kernfs_of(file);
	int ret;

	if (!of->vm_ops)
		return -EINVAL;

	if (!kernfs_get_active(of->kn))
		return -EINVAL;

	ret = -EINVAL;
	if (of->vm_ops->access)
		ret = of->vm_ops->access(vma, addr, buf, len, write);

	kernfs_put_active(of->kn);
	return ret;
}

#ifdef CONFIG_NUMA
static int kernfs_vma_set_policy(struct vm_area_struct *vma,
				 struct mempolicy *new)
{
	struct file *file = vma->vm_file;
	struct kernfs_open_file *of = kernfs_of(file);
	int ret;

	if (!of->vm_ops)
		return 0;

	if (!kernfs_get_active(of->kn))
		return -EINVAL;

	ret = 0;
	if (of->vm_ops->set_policy)
		ret = of->vm_ops->set_policy(vma, new);

	kernfs_put_active(of->kn);
	return ret;
}

static struct mempolicy *kernfs_vma_get_policy(struct vm_area_struct *vma,
					       unsigned long addr)
{
	struct file *file = vma->vm_file;
	struct kernfs_open_file *of = kernfs_of(file);
	struct mempolicy *pol;

	if (!of->vm_ops)
		return vma->vm_policy;

	if (!kernfs_get_active(of->kn))
		return vma->vm_policy;

	pol = vma->vm_policy;
	if (of->vm_ops->get_policy)
		pol = of->vm_ops->get_policy(vma, addr);

	kernfs_put_active(of->kn);
	return pol;
}

static int kernfs_vma_migrate(struct vm_area_struct *vma,
			      const nodemask_t *from, const nodemask_t *to,
			      unsigned long flags)
{
	struct file *file = vma->vm_file;
	struct kernfs_open_file *of = kernfs_of(file);
	int ret;

	if (!of->vm_ops)
		return 0;

	if (!kernfs_get_active(of->kn))
		return 0;

	ret = 0;
	if (of->vm_ops->migrate)
		ret = of->vm_ops->migrate(vma, from, to, flags);

	kernfs_put_active(of->kn);
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

static int kernfs_fop_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct kernfs_open_file *of = kernfs_of(file);
	const struct kernfs_ops *ops;
	int rc;

	/*
	 * mmap path and of->mutex are prone to triggering spurious lockdep
	 * warnings and we don't want to add spurious locking dependency
	 * between the two.  Check whether mmap is actually implemented
	 * without grabbing @of->mutex by testing HAS_MMAP flag.  See the
	 * comment in kernfs_file_open() for more details.
	 */
	if (!(of->kn->flags & KERNFS_HAS_MMAP))
		return -ENODEV;

	mutex_lock(&of->mutex);

	rc = -ENODEV;
	if (!kernfs_get_active(of->kn))
		goto out_unlock;

	ops = kernfs_ops(of->kn);
	rc = ops->mmap(of, vma);

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
	kernfs_put_active(of->kn);
out_unlock:
	mutex_unlock(&of->mutex);

	return rc;
}

/**
 *	kernfs_get_open_node - get or create kernfs_open_node
 *	@kn: target kernfs_node
 *	@of: kernfs_open_file for this instance of open
 *
 *	If @kn->attr.open exists, increment its reference count; otherwise,
 *	create one.  @of is chained to the files list.
 *
 *	LOCKING:
 *	Kernel thread context (may sleep).
 *
 *	RETURNS:
 *	0 on success, -errno on failure.
 */
static int kernfs_get_open_node(struct kernfs_node *kn,
				struct kernfs_open_file *of)
{
	struct kernfs_open_node *on, *new_on = NULL;

 retry:
	mutex_lock(&kernfs_open_file_mutex);
	spin_lock_irq(&kernfs_open_node_lock);

	if (!kn->attr.open && new_on) {
		kn->attr.open = new_on;
		new_on = NULL;
	}

	on = kn->attr.open;
	if (on) {
		atomic_inc(&on->refcnt);
		list_add_tail(&of->list, &on->files);
	}

	spin_unlock_irq(&kernfs_open_node_lock);
	mutex_unlock(&kernfs_open_file_mutex);

	if (on) {
		kfree(new_on);
		return 0;
	}

	/* not there, initialize a new one and retry */
	new_on = kmalloc(sizeof(*new_on), GFP_KERNEL);
	if (!new_on)
		return -ENOMEM;

	atomic_set(&new_on->refcnt, 0);
	atomic_set(&new_on->event, 1);
	init_waitqueue_head(&new_on->poll);
	INIT_LIST_HEAD(&new_on->files);
	goto retry;
}

/**
 *	kernfs_put_open_node - put kernfs_open_node
 *	@kn: target kernfs_nodet
 *	@of: associated kernfs_open_file
 *
 *	Put @kn->attr.open and unlink @of from the files list.  If
 *	reference count reaches zero, disassociate and free it.
 *
 *	LOCKING:
 *	None.
 */
static void kernfs_put_open_node(struct kernfs_node *kn,
				 struct kernfs_open_file *of)
{
	struct kernfs_open_node *on = kn->attr.open;
	unsigned long flags;

	mutex_lock(&kernfs_open_file_mutex);
	spin_lock_irqsave(&kernfs_open_node_lock, flags);

	if (of)
		list_del(&of->list);

	if (atomic_dec_and_test(&on->refcnt))
		kn->attr.open = NULL;
	else
		on = NULL;

	spin_unlock_irqrestore(&kernfs_open_node_lock, flags);
	mutex_unlock(&kernfs_open_file_mutex);

	kfree(on);
}

static int kernfs_fop_open(struct inode *inode, struct file *file)
{
	struct kernfs_node *kn = file->f_path.dentry->d_fsdata;
	const struct kernfs_ops *ops;
	struct kernfs_open_file *of;
	bool has_read, has_write, has_mmap;
	int error = -EACCES;

	if (!kernfs_get_active(kn))
		return -ENODEV;

	ops = kernfs_ops(kn);

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

	/* allocate a kernfs_open_file for the file */
	error = -ENOMEM;
	of = kzalloc(sizeof(struct kernfs_open_file), GFP_KERNEL);
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
	 *
	 * Both paths of the branch look the same.  They're supposed to
	 * look that way and give @of->mutex different static lockdep keys.
	 */
	if (has_mmap)
		mutex_init(&of->mutex);
	else
		mutex_init(&of->mutex);

	of->kn = kn;
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

	/* make sure we have open node struct */
	error = kernfs_get_open_node(kn, of);
	if (error)
		goto err_close;

	/* open succeeded, put active references */
	kernfs_put_active(kn);
	return 0;

err_close:
	seq_release(inode, file);
err_free:
	kfree(of);
err_out:
	kernfs_put_active(kn);
	return error;
}

static int kernfs_fop_release(struct inode *inode, struct file *filp)
{
	struct kernfs_node *kn = filp->f_path.dentry->d_fsdata;
	struct kernfs_open_file *of = kernfs_of(filp);

	kernfs_put_open_node(kn, of);
	seq_release(inode, filp);
	kfree(of);

	return 0;
}

void kernfs_unmap_bin_file(struct kernfs_node *kn)
{
	struct kernfs_open_node *on;
	struct kernfs_open_file *of;

	if (!(kn->flags & KERNFS_HAS_MMAP))
		return;

	spin_lock_irq(&kernfs_open_node_lock);
	on = kn->attr.open;
	if (on)
		atomic_inc(&on->refcnt);
	spin_unlock_irq(&kernfs_open_node_lock);
	if (!on)
		return;

	mutex_lock(&kernfs_open_file_mutex);
	list_for_each_entry(of, &on->files, list) {
		struct inode *inode = file_inode(of->file);
		unmap_mapping_range(inode->i_mapping, 0, 0, 1);
	}
	mutex_unlock(&kernfs_open_file_mutex);

	kernfs_put_open_node(kn, NULL);
}

/*
 * Kernfs attribute files are pollable.  The idea is that you read
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
static unsigned int kernfs_fop_poll(struct file *filp, poll_table *wait)
{
	struct kernfs_open_file *of = kernfs_of(filp);
	struct kernfs_node *kn = filp->f_path.dentry->d_fsdata;
	struct kernfs_open_node *on = kn->attr.open;

	/* need parent for the kobj, grab both */
	if (!kernfs_get_active(kn))
		goto trigger;

	poll_wait(filp, &on->poll, wait);

	kernfs_put_active(kn);

	if (of->event != atomic_read(&on->event))
		goto trigger;

	return DEFAULT_POLLMASK;

 trigger:
	return DEFAULT_POLLMASK|POLLERR|POLLPRI;
}

/**
 * kernfs_notify - notify a kernfs file
 * @kn: file to notify
 *
 * Notify @kn such that poll(2) on @kn wakes up.
 */
void kernfs_notify(struct kernfs_node *kn)
{
	struct kernfs_open_node *on;
	unsigned long flags;

	spin_lock_irqsave(&kernfs_open_node_lock, flags);

	if (!WARN_ON(kernfs_type(kn) != KERNFS_FILE)) {
		on = kn->attr.open;
		if (on) {
			atomic_inc(&on->event);
			wake_up_interruptible(&on->poll);
		}
	}

	spin_unlock_irqrestore(&kernfs_open_node_lock, flags);
}
EXPORT_SYMBOL_GPL(kernfs_notify);

const struct file_operations kernfs_file_fops = {
	.read		= kernfs_fop_read,
	.write		= kernfs_fop_write,
	.llseek		= generic_file_llseek,
	.mmap		= kernfs_fop_mmap,
	.open		= kernfs_fop_open,
	.release	= kernfs_fop_release,
	.poll		= kernfs_fop_poll,
};

/**
 * __kernfs_create_file - kernfs internal function to create a file
 * @parent: directory to create the file in
 * @name: name of the file
 * @mode: mode of the file
 * @size: size of the file
 * @ops: kernfs operations for the file
 * @priv: private data for the file
 * @ns: optional namespace tag of the file
 * @static_name: don't copy file name
 * @key: lockdep key for the file's active_ref, %NULL to disable lockdep
 *
 * Returns the created node on success, ERR_PTR() value on error.
 */
struct kernfs_node *__kernfs_create_file(struct kernfs_node *parent,
					 const char *name,
					 umode_t mode, loff_t size,
					 const struct kernfs_ops *ops,
					 void *priv, const void *ns,
					 bool name_is_static,
					 struct lock_class_key *key)
{
	struct kernfs_node *kn;
	unsigned flags;
	int rc;

	flags = KERNFS_FILE;
	if (name_is_static)
		flags |= KERNFS_STATIC_NAME;

	kn = kernfs_new_node(parent, name, (mode & S_IALLUGO) | S_IFREG, flags);
	if (!kn)
		return ERR_PTR(-ENOMEM);

	kn->attr.ops = ops;
	kn->attr.size = size;
	kn->ns = ns;
	kn->priv = priv;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (key) {
		lockdep_init_map(&kn->dep_map, "s_active", key, 0);
		kn->flags |= KERNFS_LOCKDEP;
	}
#endif

	/*
	 * kn->attr.ops is accesible only while holding active ref.  We
	 * need to know whether some ops are implemented outside active
	 * ref.  Cache their existence in flags.
	 */
	if (ops->seq_show)
		kn->flags |= KERNFS_HAS_SEQ_SHOW;
	if (ops->mmap)
		kn->flags |= KERNFS_HAS_MMAP;

	rc = kernfs_add_one(kn);
	if (rc) {
		kernfs_put(kn);
		return ERR_PTR(rc);
	}
	return kn;
}
