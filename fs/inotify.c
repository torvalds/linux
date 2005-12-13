/*
 * fs/inotify.c - inode-based file event notifications
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@novell.com>
 *
 * Copyright (C) 2005 John McCutchan
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/idr.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/writeback.h>
#include <linux/inotify.h>

#include <asm/ioctls.h>

static atomic_t inotify_cookie;
static atomic_t inotify_watches;

static kmem_cache_t *watch_cachep;
static kmem_cache_t *event_cachep;

static struct vfsmount *inotify_mnt;

/* these are configurable via /proc/sys/fs/inotify/ */
int inotify_max_user_instances;
int inotify_max_user_watches;
int inotify_max_queued_events;

/*
 * Lock ordering:
 *
 * dentry->d_lock (used to keep d_move() away from dentry->d_parent)
 * iprune_sem (synchronize shrink_icache_memory())
 * 	inode_lock (protects the super_block->s_inodes list)
 * 	inode->inotify_sem (protects inode->inotify_watches and watches->i_list)
 * 		inotify_dev->sem (protects inotify_device and watches->d_list)
 */

/*
 * Lifetimes of the three main data structures--inotify_device, inode, and
 * inotify_watch--are managed by reference count.
 *
 * inotify_device: Lifetime is from inotify_init() until release.  Additional
 * references can bump the count via get_inotify_dev() and drop the count via
 * put_inotify_dev().
 *
 * inotify_watch: Lifetime is from create_watch() to destory_watch().
 * Additional references can bump the count via get_inotify_watch() and drop
 * the count via put_inotify_watch().
 *
 * inode: Pinned so long as the inode is associated with a watch, from
 * create_watch() to put_inotify_watch().
 */

/*
 * struct inotify_device - represents an inotify instance
 *
 * This structure is protected by the semaphore 'sem'.
 */
struct inotify_device {
	wait_queue_head_t 	wq;		/* wait queue for i/o */
	struct idr		idr;		/* idr mapping wd -> watch */
	struct semaphore	sem;		/* protects this bad boy */
	struct list_head 	events;		/* list of queued events */
	struct list_head	watches;	/* list of watches */
	atomic_t		count;		/* reference count */
	struct user_struct	*user;		/* user who opened this dev */
	unsigned int		queue_size;	/* size of the queue (bytes) */
	unsigned int		event_count;	/* number of pending events */
	unsigned int		max_events;	/* maximum number of events */
	u32			last_wd;	/* the last wd allocated */
};

/*
 * struct inotify_kernel_event - An inotify event, originating from a watch and
 * queued for user-space.  A list of these is attached to each instance of the
 * device.  In read(), this list is walked and all events that can fit in the
 * buffer are returned.
 *
 * Protected by dev->sem of the device in which we are queued.
 */
struct inotify_kernel_event {
	struct inotify_event	event;	/* the user-space event */
	struct list_head        list;	/* entry in inotify_device's list */
	char			*name;	/* filename, if any */
};

/*
 * struct inotify_watch - represents a watch request on a specific inode
 *
 * d_list is protected by dev->sem of the associated watch->dev.
 * i_list and mask are protected by inode->inotify_sem of the associated inode.
 * dev, inode, and wd are never written to once the watch is created.
 */
struct inotify_watch {
	struct list_head	d_list;	/* entry in inotify_device's list */
	struct list_head	i_list;	/* entry in inode's list */
	atomic_t		count;	/* reference count */
	struct inotify_device	*dev;	/* associated device */
	struct inode		*inode;	/* associated inode */
	s32 			wd;	/* watch descriptor */
	u32			mask;	/* event mask for this watch */
};

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

static int zero;

ctl_table inotify_table[] = {
	{
		.ctl_name	= INOTIFY_MAX_USER_INSTANCES,
		.procname	= "max_user_instances",
		.data		= &inotify_max_user_instances,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero,
	},
	{
		.ctl_name	= INOTIFY_MAX_USER_WATCHES,
		.procname	= "max_user_watches",
		.data		= &inotify_max_user_watches,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &zero, 
	},
	{
		.ctl_name	= INOTIFY_MAX_QUEUED_EVENTS,
		.procname	= "max_queued_events",
		.data		= &inotify_max_queued_events,
		.maxlen		= sizeof(int),
		.mode		= 0644, 
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec, 
		.extra1		= &zero
	},
	{ .ctl_name = 0 }
};
#endif /* CONFIG_SYSCTL */

static inline void get_inotify_dev(struct inotify_device *dev)
{
	atomic_inc(&dev->count);
}

static inline void put_inotify_dev(struct inotify_device *dev)
{
	if (atomic_dec_and_test(&dev->count)) {
		atomic_dec(&dev->user->inotify_devs);
		free_uid(dev->user);
		idr_destroy(&dev->idr);
		kfree(dev);
	}
}

static inline void get_inotify_watch(struct inotify_watch *watch)
{
	atomic_inc(&watch->count);
}

/*
 * put_inotify_watch - decrements the ref count on a given watch.  cleans up
 * the watch and its references if the count reaches zero.
 */
static inline void put_inotify_watch(struct inotify_watch *watch)
{
	if (atomic_dec_and_test(&watch->count)) {
		put_inotify_dev(watch->dev);
		iput(watch->inode);
		kmem_cache_free(watch_cachep, watch);
	}
}

/*
 * kernel_event - create a new kernel event with the given parameters
 *
 * This function can sleep.
 */
static struct inotify_kernel_event * kernel_event(s32 wd, u32 mask, u32 cookie,
						  const char *name)
{
	struct inotify_kernel_event *kevent;

	kevent = kmem_cache_alloc(event_cachep, GFP_KERNEL);
	if (unlikely(!kevent))
		return NULL;

	/* we hand this out to user-space, so zero it just in case */
	memset(&kevent->event, 0, sizeof(struct inotify_event));

	kevent->event.wd = wd;
	kevent->event.mask = mask;
	kevent->event.cookie = cookie;

	INIT_LIST_HEAD(&kevent->list);

	if (name) {
		size_t len, rem, event_size = sizeof(struct inotify_event);

		/*
		 * We need to pad the filename so as to properly align an
		 * array of inotify_event structures.  Because the structure is
		 * small and the common case is a small filename, we just round
		 * up to the next multiple of the structure's sizeof.  This is
		 * simple and safe for all architectures.
		 */
		len = strlen(name) + 1;
		rem = event_size - len;
		if (len > event_size) {
			rem = event_size - (len % event_size);
			if (len % event_size == 0)
				rem = 0;
		}

		kevent->name = kmalloc(len + rem, GFP_KERNEL);
		if (unlikely(!kevent->name)) {
			kmem_cache_free(event_cachep, kevent);
			return NULL;
		}
		memcpy(kevent->name, name, len);
		if (rem)
			memset(kevent->name + len, 0, rem);		
		kevent->event.len = len + rem;
	} else {
		kevent->event.len = 0;
		kevent->name = NULL;
	}

	return kevent;
}

/*
 * inotify_dev_get_event - return the next event in the given dev's queue
 *
 * Caller must hold dev->sem.
 */
static inline struct inotify_kernel_event *
inotify_dev_get_event(struct inotify_device *dev)
{
	return list_entry(dev->events.next, struct inotify_kernel_event, list);
}

/*
 * inotify_dev_queue_event - add a new event to the given device
 *
 * Caller must hold dev->sem.  Can sleep (calls kernel_event()).
 */
static void inotify_dev_queue_event(struct inotify_device *dev,
				    struct inotify_watch *watch, u32 mask,
				    u32 cookie, const char *name)
{
	struct inotify_kernel_event *kevent, *last;

	/* coalescing: drop this event if it is a dupe of the previous */
	last = inotify_dev_get_event(dev);
	if (last && last->event.mask == mask && last->event.wd == watch->wd &&
			last->event.cookie == cookie) {
		const char *lastname = last->name;

		if (!name && !lastname)
			return;
		if (name && lastname && !strcmp(lastname, name))
			return;
	}

	/* the queue overflowed and we already sent the Q_OVERFLOW event */
	if (unlikely(dev->event_count > dev->max_events))
		return;

	/* if the queue overflows, we need to notify user space */
	if (unlikely(dev->event_count == dev->max_events))
		kevent = kernel_event(-1, IN_Q_OVERFLOW, cookie, NULL);
	else
		kevent = kernel_event(watch->wd, mask, cookie, name);

	if (unlikely(!kevent))
		return;

	/* queue the event and wake up anyone waiting */
	dev->event_count++;
	dev->queue_size += sizeof(struct inotify_event) + kevent->event.len;
	list_add_tail(&kevent->list, &dev->events);
	wake_up_interruptible(&dev->wq);
}

/*
 * remove_kevent - cleans up and ultimately frees the given kevent
 *
 * Caller must hold dev->sem.
 */
static void remove_kevent(struct inotify_device *dev,
			  struct inotify_kernel_event *kevent)
{
	list_del(&kevent->list);

	dev->event_count--;
	dev->queue_size -= sizeof(struct inotify_event) + kevent->event.len;

	kfree(kevent->name);
	kmem_cache_free(event_cachep, kevent);
}

/*
 * inotify_dev_event_dequeue - destroy an event on the given device
 *
 * Caller must hold dev->sem.
 */
static void inotify_dev_event_dequeue(struct inotify_device *dev)
{
	if (!list_empty(&dev->events)) {
		struct inotify_kernel_event *kevent;
		kevent = inotify_dev_get_event(dev);
		remove_kevent(dev, kevent);
	}
}

/*
 * inotify_dev_get_wd - returns the next WD for use by the given dev
 *
 * Callers must hold dev->sem.  This function can sleep.
 */
static int inotify_dev_get_wd(struct inotify_device *dev,
			      struct inotify_watch *watch)
{
	int ret;

	do {
		if (unlikely(!idr_pre_get(&dev->idr, GFP_KERNEL)))
			return -ENOSPC;
		ret = idr_get_new_above(&dev->idr, watch, dev->last_wd+1, &watch->wd);
	} while (ret == -EAGAIN);

	return ret;
}

/*
 * find_inode - resolve a user-given path to a specific inode and return a nd
 */
static int find_inode(const char __user *dirname, struct nameidata *nd,
		      unsigned flags)
{
	int error;

	error = __user_walk(dirname, flags, nd);
	if (error)
		return error;
	/* you can only watch an inode if you have read permissions on it */
	error = vfs_permission(nd, MAY_READ);
	if (error) 
		path_release(nd);
	return error;
}

/*
 * create_watch - creates a watch on the given device.
 *
 * Callers must hold dev->sem.  Calls inotify_dev_get_wd() so may sleep.
 * Both 'dev' and 'inode' (by way of nameidata) need to be pinned.
 */
static struct inotify_watch *create_watch(struct inotify_device *dev,
					  u32 mask, struct inode *inode)
{
	struct inotify_watch *watch;
	int ret;

	if (atomic_read(&dev->user->inotify_watches) >=
			inotify_max_user_watches)
		return ERR_PTR(-ENOSPC);

	watch = kmem_cache_alloc(watch_cachep, GFP_KERNEL);
	if (unlikely(!watch))
		return ERR_PTR(-ENOMEM);

	ret = inotify_dev_get_wd(dev, watch);
	if (unlikely(ret)) {
		kmem_cache_free(watch_cachep, watch);
		return ERR_PTR(ret);
	}

	dev->last_wd = watch->wd;
	watch->mask = mask;
	atomic_set(&watch->count, 0);
	INIT_LIST_HEAD(&watch->d_list);
	INIT_LIST_HEAD(&watch->i_list);

	/* save a reference to device and bump the count to make it official */
	get_inotify_dev(dev);
	watch->dev = dev;

	/*
	 * Save a reference to the inode and bump the ref count to make it
	 * official.  We hold a reference to nameidata, which makes this safe.
	 */
	watch->inode = igrab(inode);

	/* bump our own count, corresponding to our entry in dev->watches */
	get_inotify_watch(watch);

	atomic_inc(&dev->user->inotify_watches);
	atomic_inc(&inotify_watches);

	return watch;
}

/*
 * inotify_find_dev - find the watch associated with the given inode and dev
 *
 * Callers must hold inode->inotify_sem.
 */
static struct inotify_watch *inode_find_dev(struct inode *inode,
					    struct inotify_device *dev)
{
	struct inotify_watch *watch;

	list_for_each_entry(watch, &inode->inotify_watches, i_list) {
		if (watch->dev == dev)
			return watch;
	}

	return NULL;
}

/*
 * remove_watch_no_event - remove_watch() without the IN_IGNORED event.
 */
static void remove_watch_no_event(struct inotify_watch *watch,
				  struct inotify_device *dev)
{
	list_del(&watch->i_list);
	list_del(&watch->d_list);

	atomic_dec(&dev->user->inotify_watches);
	atomic_dec(&inotify_watches);
	idr_remove(&dev->idr, watch->wd);
	put_inotify_watch(watch);
}

/*
 * remove_watch - Remove a watch from both the device and the inode.  Sends
 * the IN_IGNORED event to the given device signifying that the inode is no
 * longer watched.
 *
 * Callers must hold both inode->inotify_sem and dev->sem.  We drop a
 * reference to the inode before returning.
 *
 * The inode is not iput() so as to remain atomic.  If the inode needs to be
 * iput(), the call returns one.  Otherwise, it returns zero.
 */
static void remove_watch(struct inotify_watch *watch,struct inotify_device *dev)
{
	inotify_dev_queue_event(dev, watch, IN_IGNORED, 0, NULL);
	remove_watch_no_event(watch, dev);
}

/*
 * inotify_inode_watched - returns nonzero if there are watches on this inode
 * and zero otherwise.  We call this lockless, we do not care if we race.
 */
static inline int inotify_inode_watched(struct inode *inode)
{
	return !list_empty(&inode->inotify_watches);
}

/* Kernel API */

/**
 * inotify_inode_queue_event - queue an event to all watches on this inode
 * @inode: inode event is originating from
 * @mask: event mask describing this event
 * @cookie: cookie for synchronization, or zero
 * @name: filename, if any
 */
void inotify_inode_queue_event(struct inode *inode, u32 mask, u32 cookie,
			       const char *name)
{
	struct inotify_watch *watch, *next;

	if (!inotify_inode_watched(inode))
		return;

	down(&inode->inotify_sem);
	list_for_each_entry_safe(watch, next, &inode->inotify_watches, i_list) {
		u32 watch_mask = watch->mask;
		if (watch_mask & mask) {
			struct inotify_device *dev = watch->dev;
			get_inotify_watch(watch);
			down(&dev->sem);
			inotify_dev_queue_event(dev, watch, mask, cookie, name);
			if (watch_mask & IN_ONESHOT)
				remove_watch_no_event(watch, dev);
			up(&dev->sem);
			put_inotify_watch(watch);
		}
	}
	up(&inode->inotify_sem);
}
EXPORT_SYMBOL_GPL(inotify_inode_queue_event);

/**
 * inotify_dentry_parent_queue_event - queue an event to a dentry's parent
 * @dentry: the dentry in question, we queue against this dentry's parent
 * @mask: event mask describing this event
 * @cookie: cookie for synchronization, or zero
 * @name: filename, if any
 */
void inotify_dentry_parent_queue_event(struct dentry *dentry, u32 mask,
				       u32 cookie, const char *name)
{
	struct dentry *parent;
	struct inode *inode;

	if (!atomic_read (&inotify_watches))
		return;

	spin_lock(&dentry->d_lock);
	parent = dentry->d_parent;
	inode = parent->d_inode;

	if (inotify_inode_watched(inode)) {
		dget(parent);
		spin_unlock(&dentry->d_lock);
		inotify_inode_queue_event(inode, mask, cookie, name);
		dput(parent);
	} else
		spin_unlock(&dentry->d_lock);
}
EXPORT_SYMBOL_GPL(inotify_dentry_parent_queue_event);

/**
 * inotify_get_cookie - return a unique cookie for use in synchronizing events.
 */
u32 inotify_get_cookie(void)
{
	return atomic_inc_return(&inotify_cookie);
}
EXPORT_SYMBOL_GPL(inotify_get_cookie);

/**
 * inotify_unmount_inodes - an sb is unmounting.  handle any watched inodes.
 * @list: list of inodes being unmounted (sb->s_inodes)
 *
 * Called with inode_lock held, protecting the unmounting super block's list
 * of inodes, and with iprune_sem held, keeping shrink_icache_memory() at bay.
 * We temporarily drop inode_lock, however, and CAN block.
 */
void inotify_unmount_inodes(struct list_head *list)
{
	struct inode *inode, *next_i, *need_iput = NULL;

	list_for_each_entry_safe(inode, next_i, list, i_sb_list) {
		struct inotify_watch *watch, *next_w;
		struct inode *need_iput_tmp;
		struct list_head *watches;

		/*
		 * If i_count is zero, the inode cannot have any watches and
		 * doing an __iget/iput with MS_ACTIVE clear would actually
		 * evict all inodes with zero i_count from icache which is
		 * unnecessarily violent and may in fact be illegal to do.
		 */
		if (!atomic_read(&inode->i_count))
			continue;

		/*
		 * We cannot __iget() an inode in state I_CLEAR, I_FREEING, or
		 * I_WILL_FREE which is fine because by that point the inode
		 * cannot have any associated watches.
		 */
		if (inode->i_state & (I_CLEAR | I_FREEING | I_WILL_FREE))
			continue;

		need_iput_tmp = need_iput;
		need_iput = NULL;
		/* In case the remove_watch() drops a reference. */
		if (inode != need_iput_tmp)
			__iget(inode);
		else
			need_iput_tmp = NULL;
		/* In case the dropping of a reference would nuke next_i. */
		if ((&next_i->i_sb_list != list) &&
				atomic_read(&next_i->i_count) &&
				!(next_i->i_state & (I_CLEAR | I_FREEING |
					I_WILL_FREE))) {
			__iget(next_i);
			need_iput = next_i;
		}

		/*
		 * We can safely drop inode_lock here because we hold
		 * references on both inode and next_i.  Also no new inodes
		 * will be added since the umount has begun.  Finally,
		 * iprune_sem keeps shrink_icache_memory() away.
		 */
		spin_unlock(&inode_lock);

		if (need_iput_tmp)
			iput(need_iput_tmp);

		/* for each watch, send IN_UNMOUNT and then remove it */
		down(&inode->inotify_sem);
		watches = &inode->inotify_watches;
		list_for_each_entry_safe(watch, next_w, watches, i_list) {
			struct inotify_device *dev = watch->dev;
			down(&dev->sem);
			inotify_dev_queue_event(dev, watch, IN_UNMOUNT,0,NULL);
			remove_watch(watch, dev);
			up(&dev->sem);
		}
		up(&inode->inotify_sem);
		iput(inode);		

		spin_lock(&inode_lock);
	}
}
EXPORT_SYMBOL_GPL(inotify_unmount_inodes);

/**
 * inotify_inode_is_dead - an inode has been deleted, cleanup any watches
 * @inode: inode that is about to be removed
 */
void inotify_inode_is_dead(struct inode *inode)
{
	struct inotify_watch *watch, *next;

	down(&inode->inotify_sem);
	list_for_each_entry_safe(watch, next, &inode->inotify_watches, i_list) {
		struct inotify_device *dev = watch->dev;
		down(&dev->sem);
		remove_watch(watch, dev);
		up(&dev->sem);
	}
	up(&inode->inotify_sem);
}
EXPORT_SYMBOL_GPL(inotify_inode_is_dead);

/* Device Interface */

static unsigned int inotify_poll(struct file *file, poll_table *wait)
{
	struct inotify_device *dev = file->private_data;
	int ret = 0;

	poll_wait(file, &dev->wq, wait);
	down(&dev->sem);
	if (!list_empty(&dev->events))
		ret = POLLIN | POLLRDNORM;
	up(&dev->sem);

	return ret;
}

static ssize_t inotify_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	size_t event_size = sizeof (struct inotify_event);
	struct inotify_device *dev;
	char __user *start;
	int ret;
	DEFINE_WAIT(wait);

	start = buf;
	dev = file->private_data;

	while (1) {
		int events;

		prepare_to_wait(&dev->wq, &wait, TASK_INTERRUPTIBLE);

		down(&dev->sem);
		events = !list_empty(&dev->events);
		up(&dev->sem);
		if (events) {
			ret = 0;
			break;
		}

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}

		schedule();
	}

	finish_wait(&dev->wq, &wait);
	if (ret)
		return ret;

	down(&dev->sem);
	while (1) {
		struct inotify_kernel_event *kevent;

		ret = buf - start;
		if (list_empty(&dev->events))
			break;

		kevent = inotify_dev_get_event(dev);
		if (event_size + kevent->event.len > count)
			break;

		if (copy_to_user(buf, &kevent->event, event_size)) {
			ret = -EFAULT;
			break;
		}
		buf += event_size;
		count -= event_size;

		if (kevent->name) {
			if (copy_to_user(buf, kevent->name, kevent->event.len)){
				ret = -EFAULT;
				break;
			}
			buf += kevent->event.len;
			count -= kevent->event.len;
		}

		remove_kevent(dev, kevent);
	}
	up(&dev->sem);

	return ret;
}

static int inotify_release(struct inode *ignored, struct file *file)
{
	struct inotify_device *dev = file->private_data;

	/*
	 * Destroy all of the watches on this device.  Unfortunately, not very
	 * pretty.  We cannot do a simple iteration over the list, because we
	 * do not know the inode until we iterate to the watch.  But we need to
	 * hold inode->inotify_sem before dev->sem.  The following works.
	 */
	while (1) {
		struct inotify_watch *watch;
		struct list_head *watches;
		struct inode *inode;

		down(&dev->sem);
		watches = &dev->watches;
		if (list_empty(watches)) {
			up(&dev->sem);
			break;
		}
		watch = list_entry(watches->next, struct inotify_watch, d_list);
		get_inotify_watch(watch);
		up(&dev->sem);

		inode = watch->inode;
		down(&inode->inotify_sem);
		down(&dev->sem);
		remove_watch_no_event(watch, dev);
		up(&dev->sem);
		up(&inode->inotify_sem);
		put_inotify_watch(watch);
	}

	/* destroy all of the events on this device */
	down(&dev->sem);
	while (!list_empty(&dev->events))
		inotify_dev_event_dequeue(dev);
	up(&dev->sem);

	/* free this device: the put matching the get in inotify_init() */
	put_inotify_dev(dev);

	return 0;
}

/*
 * inotify_ignore - remove a given wd from this inotify instance.
 *
 * Can sleep.
 */
static int inotify_ignore(struct inotify_device *dev, s32 wd)
{
	struct inotify_watch *watch;
	struct inode *inode;

	down(&dev->sem);
	watch = idr_find(&dev->idr, wd);
	if (unlikely(!watch)) {
		up(&dev->sem);
		return -EINVAL;
	}
	get_inotify_watch(watch);
	inode = watch->inode;
	up(&dev->sem);

	down(&inode->inotify_sem);
	down(&dev->sem);

	/* make sure that we did not race */
	watch = idr_find(&dev->idr, wd);
	if (likely(watch))
		remove_watch(watch, dev);

	up(&dev->sem);
	up(&inode->inotify_sem);
	put_inotify_watch(watch);

	return 0;
}

static long inotify_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct inotify_device *dev;
	void __user *p;
	int ret = -ENOTTY;

	dev = file->private_data;
	p = (void __user *) arg;

	switch (cmd) {
	case FIONREAD:
		ret = put_user(dev->queue_size, (int __user *) p);
		break;
	}

	return ret;
}

static struct file_operations inotify_fops = {
	.poll           = inotify_poll,
	.read           = inotify_read,
	.release        = inotify_release,
	.unlocked_ioctl = inotify_ioctl,
	.compat_ioctl	= inotify_ioctl,
};

asmlinkage long sys_inotify_init(void)
{
	struct inotify_device *dev;
	struct user_struct *user;
	struct file *filp;	
	int fd, ret;

	fd = get_unused_fd();
	if (fd < 0)
		return fd;

	filp = get_empty_filp();
	if (!filp) {
		ret = -ENFILE;
		goto out_put_fd;
	}

	user = get_uid(current->user);
	if (unlikely(atomic_read(&user->inotify_devs) >=
			inotify_max_user_instances)) {
		ret = -EMFILE;
		goto out_free_uid;
	}

	dev = kmalloc(sizeof(struct inotify_device), GFP_KERNEL);
	if (unlikely(!dev)) {
		ret = -ENOMEM;
		goto out_free_uid;
	}

	filp->f_op = &inotify_fops;
	filp->f_vfsmnt = mntget(inotify_mnt);
	filp->f_dentry = dget(inotify_mnt->mnt_root);
	filp->f_mapping = filp->f_dentry->d_inode->i_mapping;
	filp->f_mode = FMODE_READ;
	filp->f_flags = O_RDONLY;
	filp->private_data = dev;

	idr_init(&dev->idr);
	INIT_LIST_HEAD(&dev->events);
	INIT_LIST_HEAD(&dev->watches);
	init_waitqueue_head(&dev->wq);
	sema_init(&dev->sem, 1);
	dev->event_count = 0;
	dev->queue_size = 0;
	dev->max_events = inotify_max_queued_events;
	dev->user = user;
	dev->last_wd = 0;
	atomic_set(&dev->count, 0);

	get_inotify_dev(dev);
	atomic_inc(&user->inotify_devs);
	fd_install(fd, filp);

	return fd;
out_free_uid:
	free_uid(user);
	put_filp(filp);
out_put_fd:
	put_unused_fd(fd);
	return ret;
}

asmlinkage long sys_inotify_add_watch(int fd, const char __user *path, u32 mask)
{
	struct inotify_watch *watch, *old;
	struct inode *inode;
	struct inotify_device *dev;
	struct nameidata nd;
	struct file *filp;
	int ret, fput_needed;
	int mask_add = 0;
	unsigned flags = 0;

	filp = fget_light(fd, &fput_needed);
	if (unlikely(!filp))
		return -EBADF;

	/* verify that this is indeed an inotify instance */
	if (unlikely(filp->f_op != &inotify_fops)) {
		ret = -EINVAL;
		goto fput_and_out;
	}

	if (!(mask & IN_DONT_FOLLOW))
		flags |= LOOKUP_FOLLOW;
	if (mask & IN_ONLYDIR)
		flags |= LOOKUP_DIRECTORY;

	ret = find_inode(path, &nd, flags);
	if (unlikely(ret))
		goto fput_and_out;

	/* inode held in place by reference to nd; dev by fget on fd */
	inode = nd.dentry->d_inode;
	dev = filp->private_data;

	down(&inode->inotify_sem);
	down(&dev->sem);

	if (mask & IN_MASK_ADD)
		mask_add = 1;

	/* don't let user-space set invalid bits: we don't want flags set */
	mask &= IN_ALL_EVENTS;
	if (unlikely(!mask)) {
		ret = -EINVAL;
		goto out;
	}

	/*
	 * Handle the case of re-adding a watch on an (inode,dev) pair that we
	 * are already watching.  We just update the mask and return its wd.
	 */
	old = inode_find_dev(inode, dev);
	if (unlikely(old)) {
		if (mask_add)
			old->mask |= mask;
		else
			old->mask = mask;
		ret = old->wd;
		goto out;
	}

	watch = create_watch(dev, mask, inode);
	if (unlikely(IS_ERR(watch))) {
		ret = PTR_ERR(watch);
		goto out;
	}

	/* Add the watch to the device's and the inode's list */
	list_add(&watch->d_list, &dev->watches);
	list_add(&watch->i_list, &inode->inotify_watches);
	ret = watch->wd;
out:
	up(&dev->sem);
	up(&inode->inotify_sem);
	path_release(&nd);
fput_and_out:
	fput_light(filp, fput_needed);
	return ret;
}

asmlinkage long sys_inotify_rm_watch(int fd, u32 wd)
{
	struct file *filp;
	struct inotify_device *dev;
	int ret, fput_needed;

	filp = fget_light(fd, &fput_needed);
	if (unlikely(!filp))
		return -EBADF;

	/* verify that this is indeed an inotify instance */
	if (unlikely(filp->f_op != &inotify_fops)) {
		ret = -EINVAL;
		goto out;
	}

	dev = filp->private_data;
	ret = inotify_ignore(dev, wd);

out:
	fput_light(filp, fput_needed);
	return ret;
}

static struct super_block *
inotify_get_sb(struct file_system_type *fs_type, int flags,
	       const char *dev_name, void *data)
{
    return get_sb_pseudo(fs_type, "inotify", NULL, 0xBAD1DEA);
}

static struct file_system_type inotify_fs_type = {
    .name           = "inotifyfs",
    .get_sb         = inotify_get_sb,
    .kill_sb        = kill_anon_super,
};

/*
 * inotify_setup - Our initialization function.  Note that we cannnot return
 * error because we have compiled-in VFS hooks.  So an (unlikely) failure here
 * must result in panic().
 */
static int __init inotify_setup(void)
{
	int ret;

	ret = register_filesystem(&inotify_fs_type);
	if (unlikely(ret))
		panic("inotify: register_filesystem returned %d!\n", ret);

	inotify_mnt = kern_mount(&inotify_fs_type);
	if (IS_ERR(inotify_mnt))
		panic("inotify: kern_mount ret %ld!\n", PTR_ERR(inotify_mnt));

	inotify_max_queued_events = 16384;
	inotify_max_user_instances = 128;
	inotify_max_user_watches = 8192;

	atomic_set(&inotify_cookie, 0);
	atomic_set(&inotify_watches, 0);

	watch_cachep = kmem_cache_create("inotify_watch_cache",
					 sizeof(struct inotify_watch),
					 0, SLAB_PANIC, NULL, NULL);
	event_cachep = kmem_cache_create("inotify_event_cache",
					 sizeof(struct inotify_kernel_event),
					 0, SLAB_PANIC, NULL, NULL);

	return 0;
}

module_init(inotify_setup);
