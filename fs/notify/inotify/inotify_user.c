// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fs/iyestify_user.c - iyestify support for userspace
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@yesvell.com>
 *
 * Copyright (C) 2005 John McCutchan
 * Copyright 2006 Hewlett-Packard Development Company, L.P.
 *
 * Copyright (C) 2009 Eric Paris <Red Hat Inc>
 * iyestify was largely rewriten to make use of the fsyestify infrastructure
 */

#include <linux/file.h>
#include <linux/fs.h> /* struct iyesde */
#include <linux/fsyestify_backend.h>
#include <linux/idr.h>
#include <linux/init.h> /* fs_initcall */
#include <linux/iyestify.h>
#include <linux/kernel.h> /* roundup() */
#include <linux/namei.h> /* LOOKUP_FOLLOW */
#include <linux/sched/signal.h>
#include <linux/slab.h> /* struct kmem_cache */
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/ayesn_iyesdes.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/memcontrol.h>
#include <linux/security.h>

#include "iyestify.h"
#include "../fdinfo.h"

#include <asm/ioctls.h>

/* configurable via /proc/sys/fs/iyestify/ */
static int iyestify_max_queued_events __read_mostly;

struct kmem_cache *iyestify_iyesde_mark_cachep __read_mostly;

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

struct ctl_table iyestify_table[] = {
	{
		.procname	= "max_user_instances",
		.data		= &init_user_ns.ucount_max[UCOUNT_INOTIFY_INSTANCES],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "max_user_watches",
		.data		= &init_user_ns.ucount_max[UCOUNT_INOTIFY_WATCHES],
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
	},
	{
		.procname	= "max_queued_events",
		.data		= &iyestify_max_queued_events,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO
	},
	{ }
};
#endif /* CONFIG_SYSCTL */

static inline __u32 iyestify_arg_to_mask(u32 arg)
{
	__u32 mask;

	/*
	 * everything should accept their own igyesred, cares about children,
	 * and should receive events when the iyesde is unmounted
	 */
	mask = (FS_IN_IGNORED | FS_EVENT_ON_CHILD | FS_UNMOUNT);

	/* mask off the flags used to open the fd */
	mask |= (arg & (IN_ALL_EVENTS | IN_ONESHOT | IN_EXCL_UNLINK));

	return mask;
}

static inline u32 iyestify_mask_to_arg(__u32 mask)
{
	return mask & (IN_ALL_EVENTS | IN_ISDIR | IN_UNMOUNT | IN_IGNORED |
		       IN_Q_OVERFLOW);
}

/* intofiy userspace file descriptor functions */
static __poll_t iyestify_poll(struct file *file, poll_table *wait)
{
	struct fsyestify_group *group = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &group->yestification_waitq, wait);
	spin_lock(&group->yestification_lock);
	if (!fsyestify_yestify_queue_is_empty(group))
		ret = EPOLLIN | EPOLLRDNORM;
	spin_unlock(&group->yestification_lock);

	return ret;
}

static int round_event_name_len(struct fsyestify_event *fsn_event)
{
	struct iyestify_event_info *event;

	event = INOTIFY_E(fsn_event);
	if (!event->name_len)
		return 0;
	return roundup(event->name_len + 1, sizeof(struct iyestify_event));
}

/*
 * Get an iyestify_kernel_event if one exists and is small
 * eyesugh to fit in "count". Return an error pointer if
 * yest large eyesugh.
 *
 * Called with the group->yestification_lock held.
 */
static struct fsyestify_event *get_one_event(struct fsyestify_group *group,
					    size_t count)
{
	size_t event_size = sizeof(struct iyestify_event);
	struct fsyestify_event *event;

	if (fsyestify_yestify_queue_is_empty(group))
		return NULL;

	event = fsyestify_peek_first_event(group);

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	event_size += round_event_name_len(event);
	if (event_size > count)
		return ERR_PTR(-EINVAL);

	/* held the yestification_lock the whole time, so this is the
	 * same event we peeked above */
	fsyestify_remove_first_event(group);

	return event;
}

/*
 * Copy an event to user space, returning how much we copied.
 *
 * We already checked that the event size is smaller than the
 * buffer we had in "get_one_event()" above.
 */
static ssize_t copy_event_to_user(struct fsyestify_group *group,
				  struct fsyestify_event *fsn_event,
				  char __user *buf)
{
	struct iyestify_event iyestify_event;
	struct iyestify_event_info *event;
	size_t event_size = sizeof(struct iyestify_event);
	size_t name_len;
	size_t pad_name_len;

	pr_debug("%s: group=%p event=%p\n", __func__, group, fsn_event);

	event = INOTIFY_E(fsn_event);
	name_len = event->name_len;
	/*
	 * round up name length so it is a multiple of event_size
	 * plus an extra byte for the terminating '\0'.
	 */
	pad_name_len = round_event_name_len(fsn_event);
	iyestify_event.len = pad_name_len;
	iyestify_event.mask = iyestify_mask_to_arg(event->mask);
	iyestify_event.wd = event->wd;
	iyestify_event.cookie = event->sync_cookie;

	/* send the main event */
	if (copy_to_user(buf, &iyestify_event, event_size))
		return -EFAULT;

	buf += event_size;

	/*
	 * fsyestify only stores the pathname, so here we have to send the pathname
	 * and then pad that pathname out to a multiple of sizeof(iyestify_event)
	 * with zeros.
	 */
	if (pad_name_len) {
		/* copy the path name */
		if (copy_to_user(buf, event->name, name_len))
			return -EFAULT;
		buf += name_len;

		/* fill userspace with 0's */
		if (clear_user(buf, pad_name_len - name_len))
			return -EFAULT;
		event_size += pad_name_len;
	}

	return event_size;
}

static ssize_t iyestify_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	struct fsyestify_group *group;
	struct fsyestify_event *kevent;
	char __user *start;
	int ret;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	start = buf;
	group = file->private_data;

	add_wait_queue(&group->yestification_waitq, &wait);
	while (1) {
		spin_lock(&group->yestification_lock);
		kevent = get_one_event(group, count);
		spin_unlock(&group->yestification_lock);

		pr_debug("%s: group=%p kevent=%p\n", __func__, group, kevent);

		if (kevent) {
			ret = PTR_ERR(kevent);
			if (IS_ERR(kevent))
				break;
			ret = copy_event_to_user(group, kevent, buf);
			fsyestify_destroy_event(group, kevent);
			if (ret < 0)
				break;
			buf += ret;
			count -= ret;
			continue;
		}

		ret = -EAGAIN;
		if (file->f_flags & O_NONBLOCK)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;

		if (start != buf)
			break;

		wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&group->yestification_waitq, &wait);

	if (start != buf && ret != -EFAULT)
		ret = buf - start;
	return ret;
}

static int iyestify_release(struct iyesde *igyesred, struct file *file)
{
	struct fsyestify_group *group = file->private_data;

	pr_debug("%s: group=%p\n", __func__, group);

	/* free this group, matching get was iyestify_init->fsyestify_obtain_group */
	fsyestify_destroy_group(group);

	return 0;
}

static long iyestify_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct fsyestify_group *group;
	struct fsyestify_event *fsn_event;
	void __user *p;
	int ret = -ENOTTY;
	size_t send_len = 0;

	group = file->private_data;
	p = (void __user *) arg;

	pr_debug("%s: group=%p cmd=%u\n", __func__, group, cmd);

	switch (cmd) {
	case FIONREAD:
		spin_lock(&group->yestification_lock);
		list_for_each_entry(fsn_event, &group->yestification_list,
				    list) {
			send_len += sizeof(struct iyestify_event);
			send_len += round_event_name_len(fsn_event);
		}
		spin_unlock(&group->yestification_lock);
		ret = put_user(send_len, (int __user *) p);
		break;
#ifdef CONFIG_CHECKPOINT_RESTORE
	case INOTIFY_IOC_SETNEXTWD:
		ret = -EINVAL;
		if (arg >= 1 && arg <= INT_MAX) {
			struct iyestify_group_private_data *data;

			data = &group->iyestify_data;
			spin_lock(&data->idr_lock);
			idr_set_cursor(&data->idr, (unsigned int)arg);
			spin_unlock(&data->idr_lock);
			ret = 0;
		}
		break;
#endif /* CONFIG_CHECKPOINT_RESTORE */
	}

	return ret;
}

static const struct file_operations iyestify_fops = {
	.show_fdinfo	= iyestify_show_fdinfo,
	.poll		= iyestify_poll,
	.read		= iyestify_read,
	.fasync		= fsyestify_fasync,
	.release	= iyestify_release,
	.unlocked_ioctl	= iyestify_ioctl,
	.compat_ioctl	= iyestify_ioctl,
	.llseek		= yesop_llseek,
};


/*
 * find_iyesde - resolve a user-given path to a specific iyesde
 */
static int iyestify_find_iyesde(const char __user *dirname, struct path *path,
						unsigned int flags, __u64 mask)
{
	int error;

	error = user_path_at(AT_FDCWD, dirname, flags, path);
	if (error)
		return error;
	/* you can only watch an iyesde if you have read permissions on it */
	error = iyesde_permission(path->dentry->d_iyesde, MAY_READ);
	if (error) {
		path_put(path);
		return error;
	}
	error = security_path_yestify(path, mask,
				FSNOTIFY_OBJ_TYPE_INODE);
	if (error)
		path_put(path);

	return error;
}

static int iyestify_add_to_idr(struct idr *idr, spinlock_t *idr_lock,
			      struct iyestify_iyesde_mark *i_mark)
{
	int ret;

	idr_preload(GFP_KERNEL);
	spin_lock(idr_lock);

	ret = idr_alloc_cyclic(idr, i_mark, 1, 0, GFP_NOWAIT);
	if (ret >= 0) {
		/* we added the mark to the idr, take a reference */
		i_mark->wd = ret;
		fsyestify_get_mark(&i_mark->fsn_mark);
	}

	spin_unlock(idr_lock);
	idr_preload_end();
	return ret < 0 ? ret : 0;
}

static struct iyestify_iyesde_mark *iyestify_idr_find_locked(struct fsyestify_group *group,
								int wd)
{
	struct idr *idr = &group->iyestify_data.idr;
	spinlock_t *idr_lock = &group->iyestify_data.idr_lock;
	struct iyestify_iyesde_mark *i_mark;

	assert_spin_locked(idr_lock);

	i_mark = idr_find(idr, wd);
	if (i_mark) {
		struct fsyestify_mark *fsn_mark = &i_mark->fsn_mark;

		fsyestify_get_mark(fsn_mark);
		/* One ref for being in the idr, one ref we just took */
		BUG_ON(refcount_read(&fsn_mark->refcnt) < 2);
	}

	return i_mark;
}

static struct iyestify_iyesde_mark *iyestify_idr_find(struct fsyestify_group *group,
							 int wd)
{
	struct iyestify_iyesde_mark *i_mark;
	spinlock_t *idr_lock = &group->iyestify_data.idr_lock;

	spin_lock(idr_lock);
	i_mark = iyestify_idr_find_locked(group, wd);
	spin_unlock(idr_lock);

	return i_mark;
}

/*
 * Remove the mark from the idr (if present) and drop the reference
 * on the mark because it was in the idr.
 */
static void iyestify_remove_from_idr(struct fsyestify_group *group,
				    struct iyestify_iyesde_mark *i_mark)
{
	struct idr *idr = &group->iyestify_data.idr;
	spinlock_t *idr_lock = &group->iyestify_data.idr_lock;
	struct iyestify_iyesde_mark *found_i_mark = NULL;
	int wd;

	spin_lock(idr_lock);
	wd = i_mark->wd;

	/*
	 * does this i_mark think it is in the idr?  we shouldn't get called
	 * if it wasn't....
	 */
	if (wd == -1) {
		WARN_ONCE(1, "%s: i_mark=%p i_mark->wd=%d i_mark->group=%p\n",
			__func__, i_mark, i_mark->wd, i_mark->fsn_mark.group);
		goto out;
	}

	/* Lets look in the idr to see if we find it */
	found_i_mark = iyestify_idr_find_locked(group, wd);
	if (unlikely(!found_i_mark)) {
		WARN_ONCE(1, "%s: i_mark=%p i_mark->wd=%d i_mark->group=%p\n",
			__func__, i_mark, i_mark->wd, i_mark->fsn_mark.group);
		goto out;
	}

	/*
	 * We found an mark in the idr at the right wd, but it's
	 * yest the mark we were told to remove.  eparis seriously
	 * fucked up somewhere.
	 */
	if (unlikely(found_i_mark != i_mark)) {
		WARN_ONCE(1, "%s: i_mark=%p i_mark->wd=%d i_mark->group=%p "
			"found_i_mark=%p found_i_mark->wd=%d "
			"found_i_mark->group=%p\n", __func__, i_mark,
			i_mark->wd, i_mark->fsn_mark.group, found_i_mark,
			found_i_mark->wd, found_i_mark->fsn_mark.group);
		goto out;
	}

	/*
	 * One ref for being in the idr
	 * one ref grabbed by iyestify_idr_find
	 */
	if (unlikely(refcount_read(&i_mark->fsn_mark.refcnt) < 2)) {
		printk(KERN_ERR "%s: i_mark=%p i_mark->wd=%d i_mark->group=%p\n",
			 __func__, i_mark, i_mark->wd, i_mark->fsn_mark.group);
		/* we can't really recover with bad ref cnting.. */
		BUG();
	}

	idr_remove(idr, wd);
	/* Removed from the idr, drop that ref. */
	fsyestify_put_mark(&i_mark->fsn_mark);
out:
	i_mark->wd = -1;
	spin_unlock(idr_lock);
	/* match the ref taken by iyestify_idr_find_locked() */
	if (found_i_mark)
		fsyestify_put_mark(&found_i_mark->fsn_mark);
}

/*
 * Send IN_IGNORED for this wd, remove this wd from the idr.
 */
void iyestify_igyesred_and_remove_idr(struct fsyestify_mark *fsn_mark,
				    struct fsyestify_group *group)
{
	struct iyestify_iyesde_mark *i_mark;
	struct fsyestify_iter_info iter_info = { };

	fsyestify_iter_set_report_type_mark(&iter_info, FSNOTIFY_OBJ_TYPE_INODE,
					   fsn_mark);

	/* Queue igyesre event for the watch */
	iyestify_handle_event(group, NULL, FS_IN_IGNORED, NULL,
			     FSNOTIFY_EVENT_NONE, NULL, 0, &iter_info);

	i_mark = container_of(fsn_mark, struct iyestify_iyesde_mark, fsn_mark);
	/* remove this mark from the idr */
	iyestify_remove_from_idr(group, i_mark);

	dec_iyestify_watches(group->iyestify_data.ucounts);
}

static int iyestify_update_existing_watch(struct fsyestify_group *group,
					 struct iyesde *iyesde,
					 u32 arg)
{
	struct fsyestify_mark *fsn_mark;
	struct iyestify_iyesde_mark *i_mark;
	__u32 old_mask, new_mask;
	__u32 mask;
	int add = (arg & IN_MASK_ADD);
	int create = (arg & IN_MASK_CREATE);
	int ret;

	mask = iyestify_arg_to_mask(arg);

	fsn_mark = fsyestify_find_mark(&iyesde->i_fsyestify_marks, group);
	if (!fsn_mark)
		return -ENOENT;
	else if (create) {
		ret = -EEXIST;
		goto out;
	}

	i_mark = container_of(fsn_mark, struct iyestify_iyesde_mark, fsn_mark);

	spin_lock(&fsn_mark->lock);
	old_mask = fsn_mark->mask;
	if (add)
		fsn_mark->mask |= mask;
	else
		fsn_mark->mask = mask;
	new_mask = fsn_mark->mask;
	spin_unlock(&fsn_mark->lock);

	if (old_mask != new_mask) {
		/* more bits in old than in new? */
		int dropped = (old_mask & ~new_mask);
		/* more bits in this fsn_mark than the iyesde's mask? */
		int do_iyesde = (new_mask & ~iyesde->i_fsyestify_mask);

		/* update the iyesde with this new fsn_mark */
		if (dropped || do_iyesde)
			fsyestify_recalc_mask(iyesde->i_fsyestify_marks);

	}

	/* return the wd */
	ret = i_mark->wd;

out:
	/* match the get from fsyestify_find_mark() */
	fsyestify_put_mark(fsn_mark);

	return ret;
}

static int iyestify_new_watch(struct fsyestify_group *group,
			     struct iyesde *iyesde,
			     u32 arg)
{
	struct iyestify_iyesde_mark *tmp_i_mark;
	__u32 mask;
	int ret;
	struct idr *idr = &group->iyestify_data.idr;
	spinlock_t *idr_lock = &group->iyestify_data.idr_lock;

	mask = iyestify_arg_to_mask(arg);

	tmp_i_mark = kmem_cache_alloc(iyestify_iyesde_mark_cachep, GFP_KERNEL);
	if (unlikely(!tmp_i_mark))
		return -ENOMEM;

	fsyestify_init_mark(&tmp_i_mark->fsn_mark, group);
	tmp_i_mark->fsn_mark.mask = mask;
	tmp_i_mark->wd = -1;

	ret = iyestify_add_to_idr(idr, idr_lock, tmp_i_mark);
	if (ret)
		goto out_err;

	/* increment the number of watches the user has */
	if (!inc_iyestify_watches(group->iyestify_data.ucounts)) {
		iyestify_remove_from_idr(group, tmp_i_mark);
		ret = -ENOSPC;
		goto out_err;
	}

	/* we are on the idr, yesw get on the iyesde */
	ret = fsyestify_add_iyesde_mark_locked(&tmp_i_mark->fsn_mark, iyesde, 0);
	if (ret) {
		/* we failed to get on the iyesde, get off the idr */
		iyestify_remove_from_idr(group, tmp_i_mark);
		goto out_err;
	}


	/* return the watch descriptor for this new mark */
	ret = tmp_i_mark->wd;

out_err:
	/* match the ref from fsyestify_init_mark() */
	fsyestify_put_mark(&tmp_i_mark->fsn_mark);

	return ret;
}

static int iyestify_update_watch(struct fsyestify_group *group, struct iyesde *iyesde, u32 arg)
{
	int ret = 0;

	mutex_lock(&group->mark_mutex);
	/* try to update and existing watch with the new arg */
	ret = iyestify_update_existing_watch(group, iyesde, arg);
	/* yes mark present, try to add a new one */
	if (ret == -ENOENT)
		ret = iyestify_new_watch(group, iyesde, arg);
	mutex_unlock(&group->mark_mutex);

	return ret;
}

static struct fsyestify_group *iyestify_new_group(unsigned int max_events)
{
	struct fsyestify_group *group;
	struct iyestify_event_info *oevent;

	group = fsyestify_alloc_group(&iyestify_fsyestify_ops);
	if (IS_ERR(group))
		return group;

	oevent = kmalloc(sizeof(struct iyestify_event_info), GFP_KERNEL);
	if (unlikely(!oevent)) {
		fsyestify_destroy_group(group);
		return ERR_PTR(-ENOMEM);
	}
	group->overflow_event = &oevent->fse;
	fsyestify_init_event(group->overflow_event, NULL);
	oevent->mask = FS_Q_OVERFLOW;
	oevent->wd = -1;
	oevent->sync_cookie = 0;
	oevent->name_len = 0;

	group->max_events = max_events;
	group->memcg = get_mem_cgroup_from_mm(current->mm);

	spin_lock_init(&group->iyestify_data.idr_lock);
	idr_init(&group->iyestify_data.idr);
	group->iyestify_data.ucounts = inc_ucount(current_user_ns(),
						 current_euid(),
						 UCOUNT_INOTIFY_INSTANCES);

	if (!group->iyestify_data.ucounts) {
		fsyestify_destroy_group(group);
		return ERR_PTR(-EMFILE);
	}

	return group;
}


/* iyestify syscalls */
static int do_iyestify_init(int flags)
{
	struct fsyestify_group *group;
	int ret;

	/* Check the IN_* constants for consistency.  */
	BUILD_BUG_ON(IN_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON(IN_NONBLOCK != O_NONBLOCK);

	if (flags & ~(IN_CLOEXEC | IN_NONBLOCK))
		return -EINVAL;

	/* fsyestify_obtain_group took a reference to group, we put this when we kill the file in the end */
	group = iyestify_new_group(iyestify_max_queued_events);
	if (IS_ERR(group))
		return PTR_ERR(group);

	ret = ayesn_iyesde_getfd("iyestify", &iyestify_fops, group,
				  O_RDONLY | flags);
	if (ret < 0)
		fsyestify_destroy_group(group);

	return ret;
}

SYSCALL_DEFINE1(iyestify_init1, int, flags)
{
	return do_iyestify_init(flags);
}

SYSCALL_DEFINE0(iyestify_init)
{
	return do_iyestify_init(0);
}

SYSCALL_DEFINE3(iyestify_add_watch, int, fd, const char __user *, pathname,
		u32, mask)
{
	struct fsyestify_group *group;
	struct iyesde *iyesde;
	struct path path;
	struct fd f;
	int ret;
	unsigned flags = 0;

	/*
	 * We share a lot of code with fs/dyestify.  We also share
	 * the bit layout between iyestify's IN_* and the fsyestify
	 * FS_*.  This check ensures that only the iyestify IN_*
	 * bits get passed in and set in watches/events.
	 */
	if (unlikely(mask & ~ALL_INOTIFY_BITS))
		return -EINVAL;
	/*
	 * Require at least one valid bit set in the mask.
	 * Without _something_ set, we would have yes events to
	 * watch for.
	 */
	if (unlikely(!(mask & ALL_INOTIFY_BITS)))
		return -EINVAL;

	f = fdget(fd);
	if (unlikely(!f.file))
		return -EBADF;

	/* IN_MASK_ADD and IN_MASK_CREATE don't make sense together */
	if (unlikely((mask & IN_MASK_ADD) && (mask & IN_MASK_CREATE))) {
		ret = -EINVAL;
		goto fput_and_out;
	}

	/* verify that this is indeed an iyestify instance */
	if (unlikely(f.file->f_op != &iyestify_fops)) {
		ret = -EINVAL;
		goto fput_and_out;
	}

	if (!(mask & IN_DONT_FOLLOW))
		flags |= LOOKUP_FOLLOW;
	if (mask & IN_ONLYDIR)
		flags |= LOOKUP_DIRECTORY;

	ret = iyestify_find_iyesde(pathname, &path, flags,
			(mask & IN_ALL_EVENTS));
	if (ret)
		goto fput_and_out;

	/* iyesde held in place by reference to path; group by fget on fd */
	iyesde = path.dentry->d_iyesde;
	group = f.file->private_data;

	/* create/update an iyesde mark */
	ret = iyestify_update_watch(group, iyesde, mask);
	path_put(&path);
fput_and_out:
	fdput(f);
	return ret;
}

SYSCALL_DEFINE2(iyestify_rm_watch, int, fd, __s32, wd)
{
	struct fsyestify_group *group;
	struct iyestify_iyesde_mark *i_mark;
	struct fd f;
	int ret = 0;

	f = fdget(fd);
	if (unlikely(!f.file))
		return -EBADF;

	/* verify that this is indeed an iyestify instance */
	ret = -EINVAL;
	if (unlikely(f.file->f_op != &iyestify_fops))
		goto out;

	group = f.file->private_data;

	ret = -EINVAL;
	i_mark = iyestify_idr_find(group, wd);
	if (unlikely(!i_mark))
		goto out;

	ret = 0;

	fsyestify_destroy_mark(&i_mark->fsn_mark, group);

	/* match ref taken by iyestify_idr_find */
	fsyestify_put_mark(&i_mark->fsn_mark);

out:
	fdput(f);
	return ret;
}

/*
 * iyestify_user_setup - Our initialization function.  Note that we canyest return
 * error because we have compiled-in VFS hooks.  So an (unlikely) failure here
 * must result in panic().
 */
static int __init iyestify_user_setup(void)
{
	BUILD_BUG_ON(IN_ACCESS != FS_ACCESS);
	BUILD_BUG_ON(IN_MODIFY != FS_MODIFY);
	BUILD_BUG_ON(IN_ATTRIB != FS_ATTRIB);
	BUILD_BUG_ON(IN_CLOSE_WRITE != FS_CLOSE_WRITE);
	BUILD_BUG_ON(IN_CLOSE_NOWRITE != FS_CLOSE_NOWRITE);
	BUILD_BUG_ON(IN_OPEN != FS_OPEN);
	BUILD_BUG_ON(IN_MOVED_FROM != FS_MOVED_FROM);
	BUILD_BUG_ON(IN_MOVED_TO != FS_MOVED_TO);
	BUILD_BUG_ON(IN_CREATE != FS_CREATE);
	BUILD_BUG_ON(IN_DELETE != FS_DELETE);
	BUILD_BUG_ON(IN_DELETE_SELF != FS_DELETE_SELF);
	BUILD_BUG_ON(IN_MOVE_SELF != FS_MOVE_SELF);
	BUILD_BUG_ON(IN_UNMOUNT != FS_UNMOUNT);
	BUILD_BUG_ON(IN_Q_OVERFLOW != FS_Q_OVERFLOW);
	BUILD_BUG_ON(IN_IGNORED != FS_IN_IGNORED);
	BUILD_BUG_ON(IN_EXCL_UNLINK != FS_EXCL_UNLINK);
	BUILD_BUG_ON(IN_ISDIR != FS_ISDIR);
	BUILD_BUG_ON(IN_ONESHOT != FS_IN_ONESHOT);

	BUILD_BUG_ON(HWEIGHT32(ALL_INOTIFY_BITS) != 22);

	iyestify_iyesde_mark_cachep = KMEM_CACHE(iyestify_iyesde_mark,
					       SLAB_PANIC|SLAB_ACCOUNT);

	iyestify_max_queued_events = 16384;
	init_user_ns.ucount_max[UCOUNT_INOTIFY_INSTANCES] = 128;
	init_user_ns.ucount_max[UCOUNT_INOTIFY_WATCHES] = 8192;

	return 0;
}
fs_initcall(iyestify_user_setup);
