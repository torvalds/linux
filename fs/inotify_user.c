/*
 * fs/inotify_user.c - inotify support for userspace
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@novell.com>
 *
 * Copyright (C) 2005 John McCutchan
 * Copyright 2006 Hewlett-Packard Development Company, L.P.
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

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/inotify.h>
#include <linux/syscalls.h>

#include <asm/ioctls.h>

static struct kmem_cache *watch_cachep __read_mostly;
static struct kmem_cache *event_cachep __read_mostly;

static struct vfsmount *inotify_mnt __read_mostly;

/* these are configurable via /proc/sys/fs/inotify/ */
int inotify_max_user_instances __read_mostly;
int inotify_max_user_watches __read_mostly;
int inotify_max_queued_events __read_mostly;

/*
 * Lock ordering:
 *
 * inotify_dev->up_mutex (ensures we don't re-add the same watch)
 * 	inode->inotify_mutex (protects inode's watch list)
 * 		inotify_handle->mutex (protects inotify_handle's watch list)
 * 			inotify_dev->ev_mutex (protects device's event queue)
 */

/*
 * Lifetimes of the main data structures:
 *
 * inotify_device: Lifetime is managed by reference count, from
 * sys_inotify_init() until release.  Additional references can bump the count
 * via get_inotify_dev() and drop the count via put_inotify_dev().
 *
 * inotify_user_watch: Lifetime is from create_watch() to the receipt of an
 * IN_IGNORED event from inotify, or when using IN_ONESHOT, to receipt of the
 * first event, or to inotify_destroy().
 */

/*
 * struct inotify_device - represents an inotify instance
 *
 * This structure is protected by the mutex 'mutex'.
 */
struct inotify_device {
	wait_queue_head_t 	wq;		/* wait queue for i/o */
	struct mutex		ev_mutex;	/* protects event queue */
	struct mutex		up_mutex;	/* synchronizes watch updates */
	struct list_head 	events;		/* list of queued events */
	atomic_t		count;		/* reference count */
	struct user_struct	*user;		/* user who opened this dev */
	struct inotify_handle	*ih;		/* inotify handle */
	unsigned int		queue_size;	/* size of the queue (bytes) */
	unsigned int		event_count;	/* number of pending events */
	unsigned int		max_events;	/* maximum number of events */
};

/*
 * struct inotify_kernel_event - An inotify event, originating from a watch and
 * queued for user-space.  A list of these is attached to each instance of the
 * device.  In read(), this list is walked and all events that can fit in the
 * buffer are returned.
 *
 * Protected by dev->ev_mutex of the device in which we are queued.
 */
struct inotify_kernel_event {
	struct inotify_event	event;	/* the user-space event */
	struct list_head        list;	/* entry in inotify_device's list */
	char			*name;	/* filename, if any */
};

/*
 * struct inotify_user_watch - our version of an inotify_watch, we add
 * a reference to the associated inotify_device.
 */
struct inotify_user_watch {
	struct inotify_device	*dev;	/* associated device */
	struct inotify_watch	wdata;	/* inotify watch data */
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
		kfree(dev);
	}
}

/*
 * free_inotify_user_watch - cleans up the watch and its references
 */
static void free_inotify_user_watch(struct inotify_watch *w)
{
	struct inotify_user_watch *watch;
	struct inotify_device *dev;

	watch = container_of(w, struct inotify_user_watch, wdata);
	dev = watch->dev;

	atomic_dec(&dev->user->inotify_watches);
	put_inotify_dev(dev);
	kmem_cache_free(watch_cachep, watch);
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

	kevent = kmem_cache_alloc(event_cachep, GFP_NOFS);
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
 * Caller must hold dev->ev_mutex.
 */
static inline struct inotify_kernel_event *
inotify_dev_get_event(struct inotify_device *dev)
{
	return list_entry(dev->events.next, struct inotify_kernel_event, list);
}

/*
 * inotify_dev_queue_event - event handler registered with core inotify, adds
 * a new event to the given device
 *
 * Can sleep (calls kernel_event()).
 */
static void inotify_dev_queue_event(struct inotify_watch *w, u32 wd, u32 mask,
				    u32 cookie, const char *name,
				    struct inode *ignored)
{
	struct inotify_user_watch *watch;
	struct inotify_device *dev;
	struct inotify_kernel_event *kevent, *last;

	watch = container_of(w, struct inotify_user_watch, wdata);
	dev = watch->dev;

	mutex_lock(&dev->ev_mutex);

	/* we can safely put the watch as we don't reference it while
	 * generating the event
	 */
	if (mask & IN_IGNORED || mask & IN_ONESHOT)
		put_inotify_watch(w); /* final put */

	/* coalescing: drop this event if it is a dupe of the previous */
	last = inotify_dev_get_event(dev);
	if (last && last->event.mask == mask && last->event.wd == wd &&
			last->event.cookie == cookie) {
		const char *lastname = last->name;

		if (!name && !lastname)
			goto out;
		if (name && lastname && !strcmp(lastname, name))
			goto out;
	}

	/* the queue overflowed and we already sent the Q_OVERFLOW event */
	if (unlikely(dev->event_count > dev->max_events))
		goto out;

	/* if the queue overflows, we need to notify user space */
	if (unlikely(dev->event_count == dev->max_events))
		kevent = kernel_event(-1, IN_Q_OVERFLOW, cookie, NULL);
	else
		kevent = kernel_event(wd, mask, cookie, name);

	if (unlikely(!kevent))
		goto out;

	/* queue the event and wake up anyone waiting */
	dev->event_count++;
	dev->queue_size += sizeof(struct inotify_event) + kevent->event.len;
	list_add_tail(&kevent->list, &dev->events);
	wake_up_interruptible(&dev->wq);

out:
	mutex_unlock(&dev->ev_mutex);
}

/*
 * remove_kevent - cleans up and ultimately frees the given kevent
 *
 * Caller must hold dev->ev_mutex.
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
 * Caller must hold dev->ev_mutex.
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
 * Callers must hold dev->up_mutex.
 */
static int create_watch(struct inotify_device *dev, struct inode *inode,
			u32 mask)
{
	struct inotify_user_watch *watch;
	int ret;

	if (atomic_read(&dev->user->inotify_watches) >=
			inotify_max_user_watches)
		return -ENOSPC;

	watch = kmem_cache_alloc(watch_cachep, GFP_KERNEL);
	if (unlikely(!watch))
		return -ENOMEM;

	/* save a reference to device and bump the count to make it official */
	get_inotify_dev(dev);
	watch->dev = dev;

	atomic_inc(&dev->user->inotify_watches);

	inotify_init_watch(&watch->wdata);
	ret = inotify_add_watch(dev->ih, &watch->wdata, inode, mask);
	if (ret < 0)
		free_inotify_user_watch(&watch->wdata);

	return ret;
}

/* Device Interface */

static unsigned int inotify_poll(struct file *file, poll_table *wait)
{
	struct inotify_device *dev = file->private_data;
	int ret = 0;

	poll_wait(file, &dev->wq, wait);
	mutex_lock(&dev->ev_mutex);
	if (!list_empty(&dev->events))
		ret = POLLIN | POLLRDNORM;
	mutex_unlock(&dev->ev_mutex);

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

		mutex_lock(&dev->ev_mutex);
		events = !list_empty(&dev->events);
		mutex_unlock(&dev->ev_mutex);
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

	mutex_lock(&dev->ev_mutex);
	while (1) {
		struct inotify_kernel_event *kevent;

		ret = buf - start;
		if (list_empty(&dev->events))
			break;

		kevent = inotify_dev_get_event(dev);
		if (event_size + kevent->event.len > count) {
			if (ret == 0 && count > 0) {
				/*
				 * could not get a single event because we
				 * didn't have enough buffer space.
				 */
				ret = -EINVAL;
			}
			break;
		}

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
	mutex_unlock(&dev->ev_mutex);

	return ret;
}

static int inotify_release(struct inode *ignored, struct file *file)
{
	struct inotify_device *dev = file->private_data;

	inotify_destroy(dev->ih);

	/* destroy all of the events on this device */
	mutex_lock(&dev->ev_mutex);
	while (!list_empty(&dev->events))
		inotify_dev_event_dequeue(dev);
	mutex_unlock(&dev->ev_mutex);

	/* free this device: the put matching the get in inotify_init() */
	put_inotify_dev(dev);

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

static const struct file_operations inotify_fops = {
	.poll           = inotify_poll,
	.read           = inotify_read,
	.release        = inotify_release,
	.unlocked_ioctl = inotify_ioctl,
	.compat_ioctl	= inotify_ioctl,
};

static const struct inotify_operations inotify_user_ops = {
	.handle_event	= inotify_dev_queue_event,
	.destroy_watch	= free_inotify_user_watch,
};

asmlinkage long sys_inotify_init(void)
{
	struct inotify_device *dev;
	struct inotify_handle *ih;
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

	ih = inotify_init(&inotify_user_ops);
	if (unlikely(IS_ERR(ih))) {
		ret = PTR_ERR(ih);
		goto out_free_dev;
	}
	dev->ih = ih;

	filp->f_op = &inotify_fops;
	filp->f_path.mnt = mntget(inotify_mnt);
	filp->f_path.dentry = dget(inotify_mnt->mnt_root);
	filp->f_mapping = filp->f_path.dentry->d_inode->i_mapping;
	filp->f_mode = FMODE_READ;
	filp->f_flags = O_RDONLY;
	filp->private_data = dev;

	INIT_LIST_HEAD(&dev->events);
	init_waitqueue_head(&dev->wq);
	mutex_init(&dev->ev_mutex);
	mutex_init(&dev->up_mutex);
	dev->event_count = 0;
	dev->queue_size = 0;
	dev->max_events = inotify_max_queued_events;
	dev->user = user;
	atomic_set(&dev->count, 0);

	get_inotify_dev(dev);
	atomic_inc(&user->inotify_devs);
	fd_install(fd, filp);

	return fd;
out_free_dev:
	kfree(dev);
out_free_uid:
	free_uid(user);
	put_filp(filp);
out_put_fd:
	put_unused_fd(fd);
	return ret;
}

asmlinkage long sys_inotify_add_watch(int fd, const char __user *path, u32 mask)
{
	struct inode *inode;
	struct inotify_device *dev;
	struct nameidata nd;
	struct file *filp;
	int ret, fput_needed;
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

	mutex_lock(&dev->up_mutex);
	ret = inotify_find_update_watch(dev->ih, inode, mask);
	if (ret == -ENOENT)
		ret = create_watch(dev, inode, mask);
	mutex_unlock(&dev->up_mutex);

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

	/* we free our watch data when we get IN_IGNORED */
	ret = inotify_rm_wd(dev->ih, wd);

out:
	fput_light(filp, fput_needed);
	return ret;
}

static int
inotify_get_sb(struct file_system_type *fs_type, int flags,
	       const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "inotify", NULL, 0xBAD1DEA, mnt);
}

static struct file_system_type inotify_fs_type = {
    .name           = "inotifyfs",
    .get_sb         = inotify_get_sb,
    .kill_sb        = kill_anon_super,
};

/*
 * inotify_user_setup - Our initialization function.  Note that we cannnot return
 * error because we have compiled-in VFS hooks.  So an (unlikely) failure here
 * must result in panic().
 */
static int __init inotify_user_setup(void)
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

	watch_cachep = kmem_cache_create("inotify_watch_cache",
					 sizeof(struct inotify_user_watch),
					 0, SLAB_PANIC, NULL);
	event_cachep = kmem_cache_create("inotify_event_cache",
					 sizeof(struct inotify_kernel_event),
					 0, SLAB_PANIC, NULL);

	return 0;
}

module_init(inotify_user_setup);
