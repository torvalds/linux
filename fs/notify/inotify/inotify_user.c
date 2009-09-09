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
 * Copyright (C) 2009 Eric Paris <Red Hat Inc>
 * inotify was largely rewriten to make use of the fsnotify infrastructure
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

#include <linux/file.h>
#include <linux/fs.h> /* struct inode */
#include <linux/fsnotify_backend.h>
#include <linux/idr.h>
#include <linux/init.h> /* module_init */
#include <linux/inotify.h>
#include <linux/kernel.h> /* roundup() */
#include <linux/magic.h> /* superblock magic number */
#include <linux/mount.h> /* mntget */
#include <linux/namei.h> /* LOOKUP_FOLLOW */
#include <linux/path.h> /* struct path */
#include <linux/sched.h> /* struct user */
#include <linux/slab.h> /* struct kmem_cache */
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>

#include "inotify.h"

#include <asm/ioctls.h>

static struct vfsmount *inotify_mnt __read_mostly;

/* this just sits here and wastes global memory.  used to just pad userspace messages with zeros */
static struct inotify_event nul_inotify_event;

/* these are configurable via /proc/sys/fs/inotify/ */
static int inotify_max_user_instances __read_mostly;
static int inotify_max_queued_events __read_mostly;
int inotify_max_user_watches __read_mostly;

static struct kmem_cache *inotify_inode_mark_cachep __read_mostly;
struct kmem_cache *event_priv_cachep __read_mostly;
static struct fsnotify_event *inotify_ignored_event;

/*
 * When inotify registers a new group it increments this and uses that
 * value as an offset to set the fsnotify group "name" and priority.
 */
static atomic_t inotify_grp_num;

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

static inline __u32 inotify_arg_to_mask(u32 arg)
{
	__u32 mask;

	/* everything should accept their own ignored and cares about children */
	mask = (FS_IN_IGNORED | FS_EVENT_ON_CHILD);

	/* mask off the flags used to open the fd */
	mask |= (arg & (IN_ALL_EVENTS | IN_ONESHOT));

	return mask;
}

static inline u32 inotify_mask_to_arg(__u32 mask)
{
	return mask & (IN_ALL_EVENTS | IN_ISDIR | IN_UNMOUNT | IN_IGNORED |
		       IN_Q_OVERFLOW);
}

/* intofiy userspace file descriptor functions */
static unsigned int inotify_poll(struct file *file, poll_table *wait)
{
	struct fsnotify_group *group = file->private_data;
	int ret = 0;

	poll_wait(file, &group->notification_waitq, wait);
	mutex_lock(&group->notification_mutex);
	if (!fsnotify_notify_queue_is_empty(group))
		ret = POLLIN | POLLRDNORM;
	mutex_unlock(&group->notification_mutex);

	return ret;
}

/*
 * Get an inotify_kernel_event if one exists and is small
 * enough to fit in "count". Return an error pointer if
 * not large enough.
 *
 * Called with the group->notification_mutex held.
 */
static struct fsnotify_event *get_one_event(struct fsnotify_group *group,
					    size_t count)
{
	size_t event_size = sizeof(struct inotify_event);
	struct fsnotify_event *event;

	if (fsnotify_notify_queue_is_empty(group))
		return NULL;

	event = fsnotify_peek_notify_event(group);

	event_size += roundup(event->name_len, event_size);

	if (event_size > count)
		return ERR_PTR(-EINVAL);

	/* held the notification_mutex the whole time, so this is the
	 * same event we peeked above */
	fsnotify_remove_notify_event(group);

	return event;
}

/*
 * Copy an event to user space, returning how much we copied.
 *
 * We already checked that the event size is smaller than the
 * buffer we had in "get_one_event()" above.
 */
static ssize_t copy_event_to_user(struct fsnotify_group *group,
				  struct fsnotify_event *event,
				  char __user *buf)
{
	struct inotify_event inotify_event;
	struct fsnotify_event_private_data *fsn_priv;
	struct inotify_event_private_data *priv;
	size_t event_size = sizeof(struct inotify_event);
	size_t name_len;

	/* we get the inotify watch descriptor from the event private data */
	spin_lock(&event->lock);
	fsn_priv = fsnotify_remove_priv_from_event(group, event);
	spin_unlock(&event->lock);

	if (!fsn_priv)
		inotify_event.wd = -1;
	else {
		priv = container_of(fsn_priv, struct inotify_event_private_data,
				    fsnotify_event_priv_data);
		inotify_event.wd = priv->wd;
		inotify_free_event_priv(fsn_priv);
	}

	/* round up event->name_len so it is a multiple of event_size */
	name_len = roundup(event->name_len, event_size);
	inotify_event.len = name_len;

	inotify_event.mask = inotify_mask_to_arg(event->mask);
	inotify_event.cookie = event->sync_cookie;

	/* send the main event */
	if (copy_to_user(buf, &inotify_event, event_size))
		return -EFAULT;

	buf += event_size;

	/*
	 * fsnotify only stores the pathname, so here we have to send the pathname
	 * and then pad that pathname out to a multiple of sizeof(inotify_event)
	 * with zeros.  I get my zeros from the nul_inotify_event.
	 */
	if (name_len) {
		unsigned int len_to_zero = name_len - event->name_len;
		/* copy the path name */
		if (copy_to_user(buf, event->file_name, event->name_len))
			return -EFAULT;
		buf += event->name_len;

		/* fill userspace with 0's from nul_inotify_event */
		if (copy_to_user(buf, &nul_inotify_event, len_to_zero))
			return -EFAULT;
		buf += len_to_zero;
		event_size += name_len;
	}

	return event_size;
}

static ssize_t inotify_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	struct fsnotify_group *group;
	struct fsnotify_event *kevent;
	char __user *start;
	int ret;
	DEFINE_WAIT(wait);

	start = buf;
	group = file->private_data;

	while (1) {
		prepare_to_wait(&group->notification_waitq, &wait, TASK_INTERRUPTIBLE);

		mutex_lock(&group->notification_mutex);
		kevent = get_one_event(group, count);
		mutex_unlock(&group->notification_mutex);

		if (kevent) {
			ret = PTR_ERR(kevent);
			if (IS_ERR(kevent))
				break;
			ret = copy_event_to_user(group, kevent, buf);
			fsnotify_put_event(kevent);
			if (ret < 0)
				break;
			buf += ret;
			count -= ret;
			continue;
		}

		ret = -EAGAIN;
		if (file->f_flags & O_NONBLOCK)
			break;
		ret = -EINTR;
		if (signal_pending(current))
			break;

		if (start != buf)
			break;

		schedule();
	}

	finish_wait(&group->notification_waitq, &wait);
	if (start != buf && ret != -EFAULT)
		ret = buf - start;
	return ret;
}

static int inotify_fasync(int fd, struct file *file, int on)
{
	struct fsnotify_group *group = file->private_data;

	return fasync_helper(fd, file, on, &group->inotify_data.fa) >= 0 ? 0 : -EIO;
}

static int inotify_release(struct inode *ignored, struct file *file)
{
	struct fsnotify_group *group = file->private_data;

	fsnotify_clear_marks_by_group(group);

	/* free this group, matching get was inotify_init->fsnotify_obtain_group */
	fsnotify_put_group(group);

	return 0;
}

static long inotify_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct fsnotify_group *group;
	struct fsnotify_event_holder *holder;
	struct fsnotify_event *event;
	void __user *p;
	int ret = -ENOTTY;
	size_t send_len = 0;

	group = file->private_data;
	p = (void __user *) arg;

	switch (cmd) {
	case FIONREAD:
		mutex_lock(&group->notification_mutex);
		list_for_each_entry(holder, &group->notification_list, event_list) {
			event = holder->event;
			send_len += sizeof(struct inotify_event);
			send_len += roundup(event->name_len,
					     sizeof(struct inotify_event));
		}
		mutex_unlock(&group->notification_mutex);
		ret = put_user(send_len, (int __user *) p);
		break;
	}

	return ret;
}

static const struct file_operations inotify_fops = {
	.poll		= inotify_poll,
	.read		= inotify_read,
	.fasync		= inotify_fasync,
	.release	= inotify_release,
	.unlocked_ioctl	= inotify_ioctl,
	.compat_ioctl	= inotify_ioctl,
};


/*
 * find_inode - resolve a user-given path to a specific inode
 */
static int inotify_find_inode(const char __user *dirname, struct path *path, unsigned flags)
{
	int error;

	error = user_path_at(AT_FDCWD, dirname, flags, path);
	if (error)
		return error;
	/* you can only watch an inode if you have read permissions on it */
	error = inode_permission(path->dentry->d_inode, MAY_READ);
	if (error)
		path_put(path);
	return error;
}

/*
 * Send IN_IGNORED for this wd, remove this wd from the idr, and drop the
 * internal reference help on the mark because it is in the idr.
 */
void inotify_ignored_and_remove_idr(struct fsnotify_mark_entry *entry,
				    struct fsnotify_group *group)
{
	struct inotify_inode_mark_entry *ientry;
	struct inotify_event_private_data *event_priv;
	struct fsnotify_event_private_data *fsn_event_priv;
	struct idr *idr;

	ientry = container_of(entry, struct inotify_inode_mark_entry, fsn_entry);

	event_priv = kmem_cache_alloc(event_priv_cachep, GFP_KERNEL);
	if (unlikely(!event_priv))
		goto skip_send_ignore;

	fsn_event_priv = &event_priv->fsnotify_event_priv_data;

	fsn_event_priv->group = group;
	event_priv->wd = ientry->wd;

	fsnotify_add_notify_event(group, inotify_ignored_event, fsn_event_priv);

	/* did the private data get added? */
	if (list_empty(&fsn_event_priv->event_list))
		inotify_free_event_priv(fsn_event_priv);

skip_send_ignore:

	/* remove this entry from the idr */
	spin_lock(&group->inotify_data.idr_lock);
	idr = &group->inotify_data.idr;
	idr_remove(idr, ientry->wd);
	spin_unlock(&group->inotify_data.idr_lock);

	/* removed from idr, drop that reference */
	fsnotify_put_mark(entry);
}

/* ding dong the mark is dead */
static void inotify_free_mark(struct fsnotify_mark_entry *entry)
{
	struct inotify_inode_mark_entry *ientry = (struct inotify_inode_mark_entry *)entry;

	kmem_cache_free(inotify_inode_mark_cachep, ientry);
}

static int inotify_update_watch(struct fsnotify_group *group, struct inode *inode, u32 arg)
{
	struct fsnotify_mark_entry *entry = NULL;
	struct inotify_inode_mark_entry *ientry;
	int ret = 0;
	int add = (arg & IN_MASK_ADD);
	__u32 mask;
	__u32 old_mask, new_mask;

	/* don't allow invalid bits: we don't want flags set */
	mask = inotify_arg_to_mask(arg);
	if (unlikely(!mask))
		return -EINVAL;

	ientry = kmem_cache_alloc(inotify_inode_mark_cachep, GFP_KERNEL);
	if (unlikely(!ientry))
		return -ENOMEM;
	/* we set the mask at the end after attaching it */
	fsnotify_init_mark(&ientry->fsn_entry, inotify_free_mark);
	ientry->wd = 0;

find_entry:
	spin_lock(&inode->i_lock);
	entry = fsnotify_find_mark_entry(group, inode);
	spin_unlock(&inode->i_lock);
	if (entry) {
		kmem_cache_free(inotify_inode_mark_cachep, ientry);
		ientry = container_of(entry, struct inotify_inode_mark_entry, fsn_entry);
	} else {
		if (atomic_read(&group->inotify_data.user->inotify_watches) >= inotify_max_user_watches) {
			ret = -ENOSPC;
			goto out_err;
		}

		ret = fsnotify_add_mark(&ientry->fsn_entry, group, inode);
		if (ret == -EEXIST)
			goto find_entry;
		else if (ret)
			goto out_err;

		entry = &ientry->fsn_entry;
retry:
		ret = -ENOMEM;
		if (unlikely(!idr_pre_get(&group->inotify_data.idr, GFP_KERNEL)))
			goto out_err;

		spin_lock(&group->inotify_data.idr_lock);
		/* if entry is added to the idr we keep the reference obtained
		 * through fsnotify_mark_add.  remember to drop this reference
		 * when entry is removed from idr */
		ret = idr_get_new_above(&group->inotify_data.idr, entry,
					++group->inotify_data.last_wd,
					&ientry->wd);
		spin_unlock(&group->inotify_data.idr_lock);
		if (ret) {
			if (ret == -EAGAIN)
				goto retry;
			goto out_err;
		}
		atomic_inc(&group->inotify_data.user->inotify_watches);
	}

	spin_lock(&entry->lock);

	old_mask = entry->mask;
	if (add) {
		entry->mask |= mask;
		new_mask = entry->mask;
	} else {
		entry->mask = mask;
		new_mask = entry->mask;
	}

	spin_unlock(&entry->lock);

	if (old_mask != new_mask) {
		/* more bits in old than in new? */
		int dropped = (old_mask & ~new_mask);
		/* more bits in this entry than the inode's mask? */
		int do_inode = (new_mask & ~inode->i_fsnotify_mask);
		/* more bits in this entry than the group? */
		int do_group = (new_mask & ~group->mask);

		/* update the inode with this new entry */
		if (dropped || do_inode)
			fsnotify_recalc_inode_mask(inode);

		/* update the group mask with the new mask */
		if (dropped || do_group)
			fsnotify_recalc_group_mask(group);
	}

	return ientry->wd;

out_err:
	/* see this isn't supposed to happen, just kill the watch */
	if (entry) {
		fsnotify_destroy_mark_by_entry(entry);
		fsnotify_put_mark(entry);
	}
	return ret;
}

static struct fsnotify_group *inotify_new_group(struct user_struct *user, unsigned int max_events)
{
	struct fsnotify_group *group;
	unsigned int grp_num;

	/* fsnotify_obtain_group took a reference to group, we put this when we kill the file in the end */
	grp_num = (INOTIFY_GROUP_NUM - atomic_inc_return(&inotify_grp_num));
	group = fsnotify_obtain_group(grp_num, 0, &inotify_fsnotify_ops);
	if (IS_ERR(group))
		return group;

	group->max_events = max_events;

	spin_lock_init(&group->inotify_data.idr_lock);
	idr_init(&group->inotify_data.idr);
	group->inotify_data.last_wd = 0;
	group->inotify_data.user = user;
	group->inotify_data.fa = NULL;

	return group;
}


/* inotify syscalls */
SYSCALL_DEFINE1(inotify_init1, int, flags)
{
	struct fsnotify_group *group;
	struct user_struct *user;
	struct file *filp;
	int fd, ret;

	/* Check the IN_* constants for consistency.  */
	BUILD_BUG_ON(IN_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON(IN_NONBLOCK != O_NONBLOCK);

	if (flags & ~(IN_CLOEXEC | IN_NONBLOCK))
		return -EINVAL;

	fd = get_unused_fd_flags(flags & O_CLOEXEC);
	if (fd < 0)
		return fd;

	filp = get_empty_filp();
	if (!filp) {
		ret = -ENFILE;
		goto out_put_fd;
	}

	user = get_current_user();
	if (unlikely(atomic_read(&user->inotify_devs) >=
			inotify_max_user_instances)) {
		ret = -EMFILE;
		goto out_free_uid;
	}

	/* fsnotify_obtain_group took a reference to group, we put this when we kill the file in the end */
	group = inotify_new_group(user, inotify_max_queued_events);
	if (IS_ERR(group)) {
		ret = PTR_ERR(group);
		goto out_free_uid;
	}

	filp->f_op = &inotify_fops;
	filp->f_path.mnt = mntget(inotify_mnt);
	filp->f_path.dentry = dget(inotify_mnt->mnt_root);
	filp->f_mapping = filp->f_path.dentry->d_inode->i_mapping;
	filp->f_mode = FMODE_READ;
	filp->f_flags = O_RDONLY | (flags & O_NONBLOCK);
	filp->private_data = group;

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

SYSCALL_DEFINE0(inotify_init)
{
	return sys_inotify_init1(0);
}

SYSCALL_DEFINE3(inotify_add_watch, int, fd, const char __user *, pathname,
		u32, mask)
{
	struct fsnotify_group *group;
	struct inode *inode;
	struct path path;
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

	ret = inotify_find_inode(pathname, &path, flags);
	if (ret)
		goto fput_and_out;

	/* inode held in place by reference to path; group by fget on fd */
	inode = path.dentry->d_inode;
	group = filp->private_data;

	/* create/update an inode mark */
	ret = inotify_update_watch(group, inode, mask);
	if (unlikely(ret))
		goto path_put_and_out;

path_put_and_out:
	path_put(&path);
fput_and_out:
	fput_light(filp, fput_needed);
	return ret;
}

SYSCALL_DEFINE2(inotify_rm_watch, int, fd, __s32, wd)
{
	struct fsnotify_group *group;
	struct fsnotify_mark_entry *entry;
	struct file *filp;
	int ret = 0, fput_needed;

	filp = fget_light(fd, &fput_needed);
	if (unlikely(!filp))
		return -EBADF;

	/* verify that this is indeed an inotify instance */
	if (unlikely(filp->f_op != &inotify_fops)) {
		ret = -EINVAL;
		goto out;
	}

	group = filp->private_data;

	spin_lock(&group->inotify_data.idr_lock);
	entry = idr_find(&group->inotify_data.idr, wd);
	if (unlikely(!entry)) {
		spin_unlock(&group->inotify_data.idr_lock);
		ret = -EINVAL;
		goto out;
	}
	fsnotify_get_mark(entry);
	spin_unlock(&group->inotify_data.idr_lock);

	fsnotify_destroy_mark_by_entry(entry);
	fsnotify_put_mark(entry);

out:
	fput_light(filp, fput_needed);
	return ret;
}

static int
inotify_get_sb(struct file_system_type *fs_type, int flags,
	       const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_pseudo(fs_type, "inotify", NULL,
			INOTIFYFS_SUPER_MAGIC, mnt);
}

static struct file_system_type inotify_fs_type = {
    .name	= "inotifyfs",
    .get_sb	= inotify_get_sb,
    .kill_sb	= kill_anon_super,
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

	inotify_inode_mark_cachep = KMEM_CACHE(inotify_inode_mark_entry, SLAB_PANIC);
	event_priv_cachep = KMEM_CACHE(inotify_event_private_data, SLAB_PANIC);
	inotify_ignored_event = fsnotify_create_event(NULL, FS_IN_IGNORED, NULL, FSNOTIFY_EVENT_NONE, NULL, 0);
	if (!inotify_ignored_event)
		panic("unable to allocate the inotify ignored event\n");

	inotify_max_queued_events = 16384;
	inotify_max_user_instances = 128;
	inotify_max_user_watches = 8192;

	return 0;
}
module_init(inotify_user_setup);
