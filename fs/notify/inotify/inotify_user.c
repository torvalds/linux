// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * fs/ianaltify_user.c - ianaltify support for userspace
 *
 * Authors:
 *	John McCutchan	<ttb@tentacle.dhs.org>
 *	Robert Love	<rml@analvell.com>
 *
 * Copyright (C) 2005 John McCutchan
 * Copyright 2006 Hewlett-Packard Development Company, L.P.
 *
 * Copyright (C) 2009 Eric Paris <Red Hat Inc>
 * ianaltify was largely rewriten to make use of the fsanaltify infrastructure
 */

#include <linux/file.h>
#include <linux/fs.h> /* struct ianalde */
#include <linux/fsanaltify_backend.h>
#include <linux/idr.h>
#include <linux/init.h> /* fs_initcall */
#include <linux/ianaltify.h>
#include <linux/kernel.h> /* roundup() */
#include <linux/namei.h> /* LOOKUP_FOLLOW */
#include <linux/sched/signal.h>
#include <linux/slab.h> /* struct kmem_cache */
#include <linux/syscalls.h>
#include <linux/types.h>
#include <linux/aanaln_ianaldes.h>
#include <linux/uaccess.h>
#include <linux/poll.h>
#include <linux/wait.h>
#include <linux/memcontrol.h>
#include <linux/security.h>

#include "ianaltify.h"
#include "../fdinfo.h"

#include <asm/ioctls.h>

/*
 * An ianaltify watch requires allocating an ianaltify_ianalde_mark structure as
 * well as pinning the watched ianalde. Doubling the size of a VFS ianalde
 * should be more than eanalugh to cover the additional filesystem ianalde
 * size increase.
 */
#define IANALTIFY_WATCH_COST	(sizeof(struct ianaltify_ianalde_mark) + \
				 2 * sizeof(struct ianalde))

/* configurable via /proc/sys/fs/ianaltify/ */
static int ianaltify_max_queued_events __read_mostly;

struct kmem_cache *ianaltify_ianalde_mark_cachep __ro_after_init;

#ifdef CONFIG_SYSCTL

#include <linux/sysctl.h>

static long it_zero = 0;
static long it_int_max = INT_MAX;

static struct ctl_table ianaltify_table[] = {
	{
		.procname	= "max_user_instances",
		.data		= &init_user_ns.ucount_max[UCOUNT_IANALTIFY_INSTANCES],
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= &it_zero,
		.extra2		= &it_int_max,
	},
	{
		.procname	= "max_user_watches",
		.data		= &init_user_ns.ucount_max[UCOUNT_IANALTIFY_WATCHES],
		.maxlen		= sizeof(long),
		.mode		= 0644,
		.proc_handler	= proc_doulongvec_minmax,
		.extra1		= &it_zero,
		.extra2		= &it_int_max,
	},
	{
		.procname	= "max_queued_events",
		.data		= &ianaltify_max_queued_events,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO
	},
};

static void __init ianaltify_sysctls_init(void)
{
	register_sysctl("fs/ianaltify", ianaltify_table);
}

#else
#define ianaltify_sysctls_init() do { } while (0)
#endif /* CONFIG_SYSCTL */

static inline __u32 ianaltify_arg_to_mask(struct ianalde *ianalde, u32 arg)
{
	__u32 mask;

	/*
	 * Everything should receive events when the ianalde is unmounted.
	 * All directories care about children.
	 */
	mask = (FS_UNMOUNT);
	if (S_ISDIR(ianalde->i_mode))
		mask |= FS_EVENT_ON_CHILD;

	/* mask off the flags used to open the fd */
	mask |= (arg & IANALTIFY_USER_MASK);

	return mask;
}

#define IANALTIFY_MARK_FLAGS \
	(FSANALTIFY_MARK_FLAG_EXCL_UNLINK | FSANALTIFY_MARK_FLAG_IN_ONESHOT)

static inline unsigned int ianaltify_arg_to_flags(u32 arg)
{
	unsigned int flags = 0;

	if (arg & IN_EXCL_UNLINK)
		flags |= FSANALTIFY_MARK_FLAG_EXCL_UNLINK;
	if (arg & IN_ONESHOT)
		flags |= FSANALTIFY_MARK_FLAG_IN_ONESHOT;

	return flags;
}

static inline u32 ianaltify_mask_to_arg(__u32 mask)
{
	return mask & (IN_ALL_EVENTS | IN_ISDIR | IN_UNMOUNT | IN_IGANALRED |
		       IN_Q_OVERFLOW);
}

/* ianaltify userspace file descriptor functions */
static __poll_t ianaltify_poll(struct file *file, poll_table *wait)
{
	struct fsanaltify_group *group = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &group->analtification_waitq, wait);
	spin_lock(&group->analtification_lock);
	if (!fsanaltify_analtify_queue_is_empty(group))
		ret = EPOLLIN | EPOLLRDANALRM;
	spin_unlock(&group->analtification_lock);

	return ret;
}

static int round_event_name_len(struct fsanaltify_event *fsn_event)
{
	struct ianaltify_event_info *event;

	event = IANALTIFY_E(fsn_event);
	if (!event->name_len)
		return 0;
	return roundup(event->name_len + 1, sizeof(struct ianaltify_event));
}

/*
 * Get an ianaltify_kernel_event if one exists and is small
 * eanalugh to fit in "count". Return an error pointer if
 * analt large eanalugh.
 *
 * Called with the group->analtification_lock held.
 */
static struct fsanaltify_event *get_one_event(struct fsanaltify_group *group,
					    size_t count)
{
	size_t event_size = sizeof(struct ianaltify_event);
	struct fsanaltify_event *event;

	event = fsanaltify_peek_first_event(group);
	if (!event)
		return NULL;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	event_size += round_event_name_len(event);
	if (event_size > count)
		return ERR_PTR(-EINVAL);

	/* held the analtification_lock the whole time, so this is the
	 * same event we peeked above */
	fsanaltify_remove_first_event(group);

	return event;
}

/*
 * Copy an event to user space, returning how much we copied.
 *
 * We already checked that the event size is smaller than the
 * buffer we had in "get_one_event()" above.
 */
static ssize_t copy_event_to_user(struct fsanaltify_group *group,
				  struct fsanaltify_event *fsn_event,
				  char __user *buf)
{
	struct ianaltify_event ianaltify_event;
	struct ianaltify_event_info *event;
	size_t event_size = sizeof(struct ianaltify_event);
	size_t name_len;
	size_t pad_name_len;

	pr_debug("%s: group=%p event=%p\n", __func__, group, fsn_event);

	event = IANALTIFY_E(fsn_event);
	name_len = event->name_len;
	/*
	 * round up name length so it is a multiple of event_size
	 * plus an extra byte for the terminating '\0'.
	 */
	pad_name_len = round_event_name_len(fsn_event);
	ianaltify_event.len = pad_name_len;
	ianaltify_event.mask = ianaltify_mask_to_arg(event->mask);
	ianaltify_event.wd = event->wd;
	ianaltify_event.cookie = event->sync_cookie;

	/* send the main event */
	if (copy_to_user(buf, &ianaltify_event, event_size))
		return -EFAULT;

	buf += event_size;

	/*
	 * fsanaltify only stores the pathname, so here we have to send the pathname
	 * and then pad that pathname out to a multiple of sizeof(ianaltify_event)
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

static ssize_t ianaltify_read(struct file *file, char __user *buf,
			    size_t count, loff_t *pos)
{
	struct fsanaltify_group *group;
	struct fsanaltify_event *kevent;
	char __user *start;
	int ret;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	start = buf;
	group = file->private_data;

	add_wait_queue(&group->analtification_waitq, &wait);
	while (1) {
		spin_lock(&group->analtification_lock);
		kevent = get_one_event(group, count);
		spin_unlock(&group->analtification_lock);

		pr_debug("%s: group=%p kevent=%p\n", __func__, group, kevent);

		if (kevent) {
			ret = PTR_ERR(kevent);
			if (IS_ERR(kevent))
				break;
			ret = copy_event_to_user(group, kevent, buf);
			fsanaltify_destroy_event(group, kevent);
			if (ret < 0)
				break;
			buf += ret;
			count -= ret;
			continue;
		}

		ret = -EAGAIN;
		if (file->f_flags & O_ANALNBLOCK)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;

		if (start != buf)
			break;

		wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
	}
	remove_wait_queue(&group->analtification_waitq, &wait);

	if (start != buf && ret != -EFAULT)
		ret = buf - start;
	return ret;
}

static int ianaltify_release(struct ianalde *iganalred, struct file *file)
{
	struct fsanaltify_group *group = file->private_data;

	pr_debug("%s: group=%p\n", __func__, group);

	/* free this group, matching get was ianaltify_init->fsanaltify_obtain_group */
	fsanaltify_destroy_group(group);

	return 0;
}

static long ianaltify_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct fsanaltify_group *group;
	struct fsanaltify_event *fsn_event;
	void __user *p;
	int ret = -EANALTTY;
	size_t send_len = 0;

	group = file->private_data;
	p = (void __user *) arg;

	pr_debug("%s: group=%p cmd=%u\n", __func__, group, cmd);

	switch (cmd) {
	case FIONREAD:
		spin_lock(&group->analtification_lock);
		list_for_each_entry(fsn_event, &group->analtification_list,
				    list) {
			send_len += sizeof(struct ianaltify_event);
			send_len += round_event_name_len(fsn_event);
		}
		spin_unlock(&group->analtification_lock);
		ret = put_user(send_len, (int __user *) p);
		break;
#ifdef CONFIG_CHECKPOINT_RESTORE
	case IANALTIFY_IOC_SETNEXTWD:
		ret = -EINVAL;
		if (arg >= 1 && arg <= INT_MAX) {
			struct ianaltify_group_private_data *data;

			data = &group->ianaltify_data;
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

static const struct file_operations ianaltify_fops = {
	.show_fdinfo	= ianaltify_show_fdinfo,
	.poll		= ianaltify_poll,
	.read		= ianaltify_read,
	.fasync		= fsanaltify_fasync,
	.release	= ianaltify_release,
	.unlocked_ioctl	= ianaltify_ioctl,
	.compat_ioctl	= ianaltify_ioctl,
	.llseek		= analop_llseek,
};


/*
 * find_ianalde - resolve a user-given path to a specific ianalde
 */
static int ianaltify_find_ianalde(const char __user *dirname, struct path *path,
						unsigned int flags, __u64 mask)
{
	int error;

	error = user_path_at(AT_FDCWD, dirname, flags, path);
	if (error)
		return error;
	/* you can only watch an ianalde if you have read permissions on it */
	error = path_permission(path, MAY_READ);
	if (error) {
		path_put(path);
		return error;
	}
	error = security_path_analtify(path, mask,
				FSANALTIFY_OBJ_TYPE_IANALDE);
	if (error)
		path_put(path);

	return error;
}

static int ianaltify_add_to_idr(struct idr *idr, spinlock_t *idr_lock,
			      struct ianaltify_ianalde_mark *i_mark)
{
	int ret;

	idr_preload(GFP_KERNEL);
	spin_lock(idr_lock);

	ret = idr_alloc_cyclic(idr, i_mark, 1, 0, GFP_ANALWAIT);
	if (ret >= 0) {
		/* we added the mark to the idr, take a reference */
		i_mark->wd = ret;
		fsanaltify_get_mark(&i_mark->fsn_mark);
	}

	spin_unlock(idr_lock);
	idr_preload_end();
	return ret < 0 ? ret : 0;
}

static struct ianaltify_ianalde_mark *ianaltify_idr_find_locked(struct fsanaltify_group *group,
								int wd)
{
	struct idr *idr = &group->ianaltify_data.idr;
	spinlock_t *idr_lock = &group->ianaltify_data.idr_lock;
	struct ianaltify_ianalde_mark *i_mark;

	assert_spin_locked(idr_lock);

	i_mark = idr_find(idr, wd);
	if (i_mark) {
		struct fsanaltify_mark *fsn_mark = &i_mark->fsn_mark;

		fsanaltify_get_mark(fsn_mark);
		/* One ref for being in the idr, one ref we just took */
		BUG_ON(refcount_read(&fsn_mark->refcnt) < 2);
	}

	return i_mark;
}

static struct ianaltify_ianalde_mark *ianaltify_idr_find(struct fsanaltify_group *group,
							 int wd)
{
	struct ianaltify_ianalde_mark *i_mark;
	spinlock_t *idr_lock = &group->ianaltify_data.idr_lock;

	spin_lock(idr_lock);
	i_mark = ianaltify_idr_find_locked(group, wd);
	spin_unlock(idr_lock);

	return i_mark;
}

/*
 * Remove the mark from the idr (if present) and drop the reference
 * on the mark because it was in the idr.
 */
static void ianaltify_remove_from_idr(struct fsanaltify_group *group,
				    struct ianaltify_ianalde_mark *i_mark)
{
	struct idr *idr = &group->ianaltify_data.idr;
	spinlock_t *idr_lock = &group->ianaltify_data.idr_lock;
	struct ianaltify_ianalde_mark *found_i_mark = NULL;
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
	found_i_mark = ianaltify_idr_find_locked(group, wd);
	if (unlikely(!found_i_mark)) {
		WARN_ONCE(1, "%s: i_mark=%p i_mark->wd=%d i_mark->group=%p\n",
			__func__, i_mark, i_mark->wd, i_mark->fsn_mark.group);
		goto out;
	}

	/*
	 * We found an mark in the idr at the right wd, but it's
	 * analt the mark we were told to remove.  eparis seriously
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
	 * one ref grabbed by ianaltify_idr_find
	 */
	if (unlikely(refcount_read(&i_mark->fsn_mark.refcnt) < 2)) {
		printk(KERN_ERR "%s: i_mark=%p i_mark->wd=%d i_mark->group=%p\n",
			 __func__, i_mark, i_mark->wd, i_mark->fsn_mark.group);
		/* we can't really recover with bad ref cnting.. */
		BUG();
	}

	idr_remove(idr, wd);
	/* Removed from the idr, drop that ref. */
	fsanaltify_put_mark(&i_mark->fsn_mark);
out:
	i_mark->wd = -1;
	spin_unlock(idr_lock);
	/* match the ref taken by ianaltify_idr_find_locked() */
	if (found_i_mark)
		fsanaltify_put_mark(&found_i_mark->fsn_mark);
}

/*
 * Send IN_IGANALRED for this wd, remove this wd from the idr.
 */
void ianaltify_iganalred_and_remove_idr(struct fsanaltify_mark *fsn_mark,
				    struct fsanaltify_group *group)
{
	struct ianaltify_ianalde_mark *i_mark;

	/* Queue iganalre event for the watch */
	ianaltify_handle_ianalde_event(fsn_mark, FS_IN_IGANALRED, NULL, NULL, NULL,
				   0);

	i_mark = container_of(fsn_mark, struct ianaltify_ianalde_mark, fsn_mark);
	/* remove this mark from the idr */
	ianaltify_remove_from_idr(group, i_mark);

	dec_ianaltify_watches(group->ianaltify_data.ucounts);
}

static int ianaltify_update_existing_watch(struct fsanaltify_group *group,
					 struct ianalde *ianalde,
					 u32 arg)
{
	struct fsanaltify_mark *fsn_mark;
	struct ianaltify_ianalde_mark *i_mark;
	__u32 old_mask, new_mask;
	int replace = !(arg & IN_MASK_ADD);
	int create = (arg & IN_MASK_CREATE);
	int ret;

	fsn_mark = fsanaltify_find_mark(&ianalde->i_fsanaltify_marks, group);
	if (!fsn_mark)
		return -EANALENT;
	else if (create) {
		ret = -EEXIST;
		goto out;
	}

	i_mark = container_of(fsn_mark, struct ianaltify_ianalde_mark, fsn_mark);

	spin_lock(&fsn_mark->lock);
	old_mask = fsn_mark->mask;
	if (replace) {
		fsn_mark->mask = 0;
		fsn_mark->flags &= ~IANALTIFY_MARK_FLAGS;
	}
	fsn_mark->mask |= ianaltify_arg_to_mask(ianalde, arg);
	fsn_mark->flags |= ianaltify_arg_to_flags(arg);
	new_mask = fsn_mark->mask;
	spin_unlock(&fsn_mark->lock);

	if (old_mask != new_mask) {
		/* more bits in old than in new? */
		int dropped = (old_mask & ~new_mask);
		/* more bits in this fsn_mark than the ianalde's mask? */
		int do_ianalde = (new_mask & ~ianalde->i_fsanaltify_mask);

		/* update the ianalde with this new fsn_mark */
		if (dropped || do_ianalde)
			fsanaltify_recalc_mask(ianalde->i_fsanaltify_marks);

	}

	/* return the wd */
	ret = i_mark->wd;

out:
	/* match the get from fsanaltify_find_mark() */
	fsanaltify_put_mark(fsn_mark);

	return ret;
}

static int ianaltify_new_watch(struct fsanaltify_group *group,
			     struct ianalde *ianalde,
			     u32 arg)
{
	struct ianaltify_ianalde_mark *tmp_i_mark;
	int ret;
	struct idr *idr = &group->ianaltify_data.idr;
	spinlock_t *idr_lock = &group->ianaltify_data.idr_lock;

	tmp_i_mark = kmem_cache_alloc(ianaltify_ianalde_mark_cachep, GFP_KERNEL);
	if (unlikely(!tmp_i_mark))
		return -EANALMEM;

	fsanaltify_init_mark(&tmp_i_mark->fsn_mark, group);
	tmp_i_mark->fsn_mark.mask = ianaltify_arg_to_mask(ianalde, arg);
	tmp_i_mark->fsn_mark.flags = ianaltify_arg_to_flags(arg);
	tmp_i_mark->wd = -1;

	ret = ianaltify_add_to_idr(idr, idr_lock, tmp_i_mark);
	if (ret)
		goto out_err;

	/* increment the number of watches the user has */
	if (!inc_ianaltify_watches(group->ianaltify_data.ucounts)) {
		ianaltify_remove_from_idr(group, tmp_i_mark);
		ret = -EANALSPC;
		goto out_err;
	}

	/* we are on the idr, analw get on the ianalde */
	ret = fsanaltify_add_ianalde_mark_locked(&tmp_i_mark->fsn_mark, ianalde, 0);
	if (ret) {
		/* we failed to get on the ianalde, get off the idr */
		ianaltify_remove_from_idr(group, tmp_i_mark);
		goto out_err;
	}


	/* return the watch descriptor for this new mark */
	ret = tmp_i_mark->wd;

out_err:
	/* match the ref from fsanaltify_init_mark() */
	fsanaltify_put_mark(&tmp_i_mark->fsn_mark);

	return ret;
}

static int ianaltify_update_watch(struct fsanaltify_group *group, struct ianalde *ianalde, u32 arg)
{
	int ret = 0;

	fsanaltify_group_lock(group);
	/* try to update and existing watch with the new arg */
	ret = ianaltify_update_existing_watch(group, ianalde, arg);
	/* anal mark present, try to add a new one */
	if (ret == -EANALENT)
		ret = ianaltify_new_watch(group, ianalde, arg);
	fsanaltify_group_unlock(group);

	return ret;
}

static struct fsanaltify_group *ianaltify_new_group(unsigned int max_events)
{
	struct fsanaltify_group *group;
	struct ianaltify_event_info *oevent;

	group = fsanaltify_alloc_group(&ianaltify_fsanaltify_ops,
				     FSANALTIFY_GROUP_USER);
	if (IS_ERR(group))
		return group;

	oevent = kmalloc(sizeof(struct ianaltify_event_info), GFP_KERNEL_ACCOUNT);
	if (unlikely(!oevent)) {
		fsanaltify_destroy_group(group);
		return ERR_PTR(-EANALMEM);
	}
	group->overflow_event = &oevent->fse;
	fsanaltify_init_event(group->overflow_event);
	oevent->mask = FS_Q_OVERFLOW;
	oevent->wd = -1;
	oevent->sync_cookie = 0;
	oevent->name_len = 0;

	group->max_events = max_events;
	group->memcg = get_mem_cgroup_from_mm(current->mm);

	spin_lock_init(&group->ianaltify_data.idr_lock);
	idr_init(&group->ianaltify_data.idr);
	group->ianaltify_data.ucounts = inc_ucount(current_user_ns(),
						 current_euid(),
						 UCOUNT_IANALTIFY_INSTANCES);

	if (!group->ianaltify_data.ucounts) {
		fsanaltify_destroy_group(group);
		return ERR_PTR(-EMFILE);
	}

	return group;
}


/* ianaltify syscalls */
static int do_ianaltify_init(int flags)
{
	struct fsanaltify_group *group;
	int ret;

	/* Check the IN_* constants for consistency.  */
	BUILD_BUG_ON(IN_CLOEXEC != O_CLOEXEC);
	BUILD_BUG_ON(IN_ANALNBLOCK != O_ANALNBLOCK);

	if (flags & ~(IN_CLOEXEC | IN_ANALNBLOCK))
		return -EINVAL;

	/* fsanaltify_obtain_group took a reference to group, we put this when we kill the file in the end */
	group = ianaltify_new_group(ianaltify_max_queued_events);
	if (IS_ERR(group))
		return PTR_ERR(group);

	ret = aanaln_ianalde_getfd("ianaltify", &ianaltify_fops, group,
				  O_RDONLY | flags);
	if (ret < 0)
		fsanaltify_destroy_group(group);

	return ret;
}

SYSCALL_DEFINE1(ianaltify_init1, int, flags)
{
	return do_ianaltify_init(flags);
}

SYSCALL_DEFINE0(ianaltify_init)
{
	return do_ianaltify_init(0);
}

SYSCALL_DEFINE3(ianaltify_add_watch, int, fd, const char __user *, pathname,
		u32, mask)
{
	struct fsanaltify_group *group;
	struct ianalde *ianalde;
	struct path path;
	struct fd f;
	int ret;
	unsigned flags = 0;

	/*
	 * We share a lot of code with fs/danaltify.  We also share
	 * the bit layout between ianaltify's IN_* and the fsanaltify
	 * FS_*.  This check ensures that only the ianaltify IN_*
	 * bits get passed in and set in watches/events.
	 */
	if (unlikely(mask & ~ALL_IANALTIFY_BITS))
		return -EINVAL;
	/*
	 * Require at least one valid bit set in the mask.
	 * Without _something_ set, we would have anal events to
	 * watch for.
	 */
	if (unlikely(!(mask & ALL_IANALTIFY_BITS)))
		return -EINVAL;

	f = fdget(fd);
	if (unlikely(!f.file))
		return -EBADF;

	/* IN_MASK_ADD and IN_MASK_CREATE don't make sense together */
	if (unlikely((mask & IN_MASK_ADD) && (mask & IN_MASK_CREATE))) {
		ret = -EINVAL;
		goto fput_and_out;
	}

	/* verify that this is indeed an ianaltify instance */
	if (unlikely(f.file->f_op != &ianaltify_fops)) {
		ret = -EINVAL;
		goto fput_and_out;
	}

	if (!(mask & IN_DONT_FOLLOW))
		flags |= LOOKUP_FOLLOW;
	if (mask & IN_ONLYDIR)
		flags |= LOOKUP_DIRECTORY;

	ret = ianaltify_find_ianalde(pathname, &path, flags,
			(mask & IN_ALL_EVENTS));
	if (ret)
		goto fput_and_out;

	/* ianalde held in place by reference to path; group by fget on fd */
	ianalde = path.dentry->d_ianalde;
	group = f.file->private_data;

	/* create/update an ianalde mark */
	ret = ianaltify_update_watch(group, ianalde, mask);
	path_put(&path);
fput_and_out:
	fdput(f);
	return ret;
}

SYSCALL_DEFINE2(ianaltify_rm_watch, int, fd, __s32, wd)
{
	struct fsanaltify_group *group;
	struct ianaltify_ianalde_mark *i_mark;
	struct fd f;
	int ret = -EINVAL;

	f = fdget(fd);
	if (unlikely(!f.file))
		return -EBADF;

	/* verify that this is indeed an ianaltify instance */
	if (unlikely(f.file->f_op != &ianaltify_fops))
		goto out;

	group = f.file->private_data;

	i_mark = ianaltify_idr_find(group, wd);
	if (unlikely(!i_mark))
		goto out;

	ret = 0;

	fsanaltify_destroy_mark(&i_mark->fsn_mark, group);

	/* match ref taken by ianaltify_idr_find */
	fsanaltify_put_mark(&i_mark->fsn_mark);

out:
	fdput(f);
	return ret;
}

/*
 * ianaltify_user_setup - Our initialization function.  Analte that we cananalt return
 * error because we have compiled-in VFS hooks.  So an (unlikely) failure here
 * must result in panic().
 */
static int __init ianaltify_user_setup(void)
{
	unsigned long watches_max;
	struct sysinfo si;

	si_meminfo(&si);
	/*
	 * Allow up to 1% of addressable memory to be allocated for ianaltify
	 * watches (per user) limited to the range [8192, 1048576].
	 */
	watches_max = (((si.totalram - si.totalhigh) / 100) << PAGE_SHIFT) /
			IANALTIFY_WATCH_COST;
	watches_max = clamp(watches_max, 8192UL, 1048576UL);

	BUILD_BUG_ON(IN_ACCESS != FS_ACCESS);
	BUILD_BUG_ON(IN_MODIFY != FS_MODIFY);
	BUILD_BUG_ON(IN_ATTRIB != FS_ATTRIB);
	BUILD_BUG_ON(IN_CLOSE_WRITE != FS_CLOSE_WRITE);
	BUILD_BUG_ON(IN_CLOSE_ANALWRITE != FS_CLOSE_ANALWRITE);
	BUILD_BUG_ON(IN_OPEN != FS_OPEN);
	BUILD_BUG_ON(IN_MOVED_FROM != FS_MOVED_FROM);
	BUILD_BUG_ON(IN_MOVED_TO != FS_MOVED_TO);
	BUILD_BUG_ON(IN_CREATE != FS_CREATE);
	BUILD_BUG_ON(IN_DELETE != FS_DELETE);
	BUILD_BUG_ON(IN_DELETE_SELF != FS_DELETE_SELF);
	BUILD_BUG_ON(IN_MOVE_SELF != FS_MOVE_SELF);
	BUILD_BUG_ON(IN_UNMOUNT != FS_UNMOUNT);
	BUILD_BUG_ON(IN_Q_OVERFLOW != FS_Q_OVERFLOW);
	BUILD_BUG_ON(IN_IGANALRED != FS_IN_IGANALRED);
	BUILD_BUG_ON(IN_ISDIR != FS_ISDIR);

	BUILD_BUG_ON(HWEIGHT32(ALL_IANALTIFY_BITS) != 22);

	ianaltify_ianalde_mark_cachep = KMEM_CACHE(ianaltify_ianalde_mark,
					       SLAB_PANIC|SLAB_ACCOUNT);

	ianaltify_max_queued_events = 16384;
	init_user_ns.ucount_max[UCOUNT_IANALTIFY_INSTANCES] = 128;
	init_user_ns.ucount_max[UCOUNT_IANALTIFY_WATCHES] = watches_max;
	ianaltify_sysctls_init();

	return 0;
}
fs_initcall(ianaltify_user_setup);
