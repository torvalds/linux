// SPDX-License-Identifier: GPL-2.0
#include <linux/fanotify.h>
#include <linux/fcntl.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/fsnotify_backend.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/poll.h>
#include <linux/security.h>
#include <linux/syscalls.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/compat.h>
#include <linux/sched/signal.h>

#include <asm/ioctls.h>

#include "../../mount.h"
#include "../fdinfo.h"
#include "fanotify.h"

#define FANOTIFY_DEFAULT_MAX_EVENTS	16384
#define FANOTIFY_DEFAULT_MAX_MARKS	8192
#define FANOTIFY_DEFAULT_MAX_LISTENERS	128

/*
 * All flags that may be specified in parameter event_f_flags of fanotify_init.
 *
 * Internal and external open flags are stored together in field f_flags of
 * struct file. Only external open flags shall be allowed in event_f_flags.
 * Internal flags like FMODE_NONOTIFY, FMODE_EXEC, FMODE_NOCMTIME shall be
 * excluded.
 */
#define	FANOTIFY_INIT_ALL_EVENT_F_BITS				( \
		O_ACCMODE	| O_APPEND	| O_NONBLOCK	| \
		__O_SYNC	| O_DSYNC	| O_CLOEXEC     | \
		O_LARGEFILE	| O_NOATIME	)

extern const struct fsnotify_ops fanotify_fsnotify_ops;

struct kmem_cache *fanotify_mark_cache __read_mostly;
struct kmem_cache *fanotify_event_cachep __read_mostly;
struct kmem_cache *fanotify_perm_event_cachep __read_mostly;

/*
 * Get an fsnotify notification event if one exists and is small
 * enough to fit in "count". Return an error pointer if the count
 * is not large enough.
 *
 * Called with the group->notification_lock held.
 */
static struct fsnotify_event *get_one_event(struct fsnotify_group *group,
					    size_t count)
{
	assert_spin_locked(&group->notification_lock);

	pr_debug("%s: group=%p count=%zd\n", __func__, group, count);

	if (fsnotify_notify_queue_is_empty(group))
		return NULL;

	if (FAN_EVENT_METADATA_LEN > count)
		return ERR_PTR(-EINVAL);

	/* held the notification_lock the whole time, so this is the
	 * same event we peeked above */
	return fsnotify_remove_first_event(group);
}

static int create_fd(struct fsnotify_group *group,
		     struct fanotify_event_info *event,
		     struct file **file)
{
	int client_fd;
	struct file *new_file;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	client_fd = get_unused_fd_flags(group->fanotify_data.f_flags);
	if (client_fd < 0)
		return client_fd;

	/*
	 * we need a new file handle for the userspace program so it can read even if it was
	 * originally opened O_WRONLY.
	 */
	/* it's possible this event was an overflow event.  in that case dentry and mnt
	 * are NULL;  That's fine, just don't call dentry open */
	if (event->path.dentry && event->path.mnt)
		new_file = dentry_open(&event->path,
				       group->fanotify_data.f_flags | FMODE_NONOTIFY,
				       current_cred());
	else
		new_file = ERR_PTR(-EOVERFLOW);
	if (IS_ERR(new_file)) {
		/*
		 * we still send an event even if we can't open the file.  this
		 * can happen when say tasks are gone and we try to open their
		 * /proc files or we try to open a WRONLY file like in sysfs
		 * we just send the errno to userspace since there isn't much
		 * else we can do.
		 */
		put_unused_fd(client_fd);
		client_fd = PTR_ERR(new_file);
	} else {
		*file = new_file;
	}

	return client_fd;
}

static int fill_event_metadata(struct fsnotify_group *group,
			       struct fanotify_event_metadata *metadata,
			       struct fsnotify_event *fsn_event,
			       struct file **file)
{
	int ret = 0;
	struct fanotify_event_info *event;

	pr_debug("%s: group=%p metadata=%p event=%p\n", __func__,
		 group, metadata, fsn_event);

	*file = NULL;
	event = container_of(fsn_event, struct fanotify_event_info, fse);
	metadata->event_len = FAN_EVENT_METADATA_LEN;
	metadata->metadata_len = FAN_EVENT_METADATA_LEN;
	metadata->vers = FANOTIFY_METADATA_VERSION;
	metadata->reserved = 0;
	metadata->mask = fsn_event->mask & FAN_ALL_OUTGOING_EVENTS;
	metadata->pid = pid_vnr(event->tgid);
	if (unlikely(fsn_event->mask & FAN_Q_OVERFLOW))
		metadata->fd = FAN_NOFD;
	else {
		metadata->fd = create_fd(group, event, file);
		if (metadata->fd < 0)
			ret = metadata->fd;
	}

	return ret;
}

static struct fanotify_perm_event_info *dequeue_event(
				struct fsnotify_group *group, int fd)
{
	struct fanotify_perm_event_info *event, *return_e = NULL;

	spin_lock(&group->notification_lock);
	list_for_each_entry(event, &group->fanotify_data.access_list,
			    fae.fse.list) {
		if (event->fd != fd)
			continue;

		list_del_init(&event->fae.fse.list);
		return_e = event;
		break;
	}
	spin_unlock(&group->notification_lock);

	pr_debug("%s: found return_re=%p\n", __func__, return_e);

	return return_e;
}

static int process_access_response(struct fsnotify_group *group,
				   struct fanotify_response *response_struct)
{
	struct fanotify_perm_event_info *event;
	int fd = response_struct->fd;
	int response = response_struct->response;

	pr_debug("%s: group=%p fd=%d response=%d\n", __func__, group,
		 fd, response);
	/*
	 * make sure the response is valid, if invalid we do nothing and either
	 * userspace can send a valid response or we will clean it up after the
	 * timeout
	 */
	switch (response & ~FAN_AUDIT) {
	case FAN_ALLOW:
	case FAN_DENY:
		break;
	default:
		return -EINVAL;
	}

	if (fd < 0)
		return -EINVAL;

	if ((response & FAN_AUDIT) && !group->fanotify_data.audit)
		return -EINVAL;

	event = dequeue_event(group, fd);
	if (!event)
		return -ENOENT;

	event->response = response;
	wake_up(&group->fanotify_data.access_waitq);

	return 0;
}

static ssize_t copy_event_to_user(struct fsnotify_group *group,
				  struct fsnotify_event *event,
				  char __user *buf)
{
	struct fanotify_event_metadata fanotify_event_metadata;
	struct file *f;
	int fd, ret;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	ret = fill_event_metadata(group, &fanotify_event_metadata, event, &f);
	if (ret < 0)
		return ret;

	fd = fanotify_event_metadata.fd;
	ret = -EFAULT;
	if (copy_to_user(buf, &fanotify_event_metadata,
			 fanotify_event_metadata.event_len))
		goto out_close_fd;

	if (fanotify_is_perm_event(event->mask))
		FANOTIFY_PE(event)->fd = fd;

	if (fd != FAN_NOFD)
		fd_install(fd, f);
	return fanotify_event_metadata.event_len;

out_close_fd:
	if (fd != FAN_NOFD) {
		put_unused_fd(fd);
		fput(f);
	}
	return ret;
}

/* intofiy userspace file descriptor functions */
static __poll_t fanotify_poll(struct file *file, poll_table *wait)
{
	struct fsnotify_group *group = file->private_data;
	__poll_t ret = 0;

	poll_wait(file, &group->notification_waitq, wait);
	spin_lock(&group->notification_lock);
	if (!fsnotify_notify_queue_is_empty(group))
		ret = EPOLLIN | EPOLLRDNORM;
	spin_unlock(&group->notification_lock);

	return ret;
}

static ssize_t fanotify_read(struct file *file, char __user *buf,
			     size_t count, loff_t *pos)
{
	struct fsnotify_group *group;
	struct fsnotify_event *kevent;
	char __user *start;
	int ret;
	DEFINE_WAIT_FUNC(wait, woken_wake_function);

	start = buf;
	group = file->private_data;

	pr_debug("%s: group=%p\n", __func__, group);

	add_wait_queue(&group->notification_waitq, &wait);
	while (1) {
		spin_lock(&group->notification_lock);
		kevent = get_one_event(group, count);
		spin_unlock(&group->notification_lock);

		if (IS_ERR(kevent)) {
			ret = PTR_ERR(kevent);
			break;
		}

		if (!kevent) {
			ret = -EAGAIN;
			if (file->f_flags & O_NONBLOCK)
				break;

			ret = -ERESTARTSYS;
			if (signal_pending(current))
				break;

			if (start != buf)
				break;

			wait_woken(&wait, TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT);
			continue;
		}

		ret = copy_event_to_user(group, kevent, buf);
		if (unlikely(ret == -EOPENSTALE)) {
			/*
			 * We cannot report events with stale fd so drop it.
			 * Setting ret to 0 will continue the event loop and
			 * do the right thing if there are no more events to
			 * read (i.e. return bytes read, -EAGAIN or wait).
			 */
			ret = 0;
		}

		/*
		 * Permission events get queued to wait for response.  Other
		 * events can be destroyed now.
		 */
		if (!fanotify_is_perm_event(kevent->mask)) {
			fsnotify_destroy_event(group, kevent);
		} else {
			if (ret <= 0) {
				FANOTIFY_PE(kevent)->response = FAN_DENY;
				wake_up(&group->fanotify_data.access_waitq);
			} else {
				spin_lock(&group->notification_lock);
				list_add_tail(&kevent->list,
					&group->fanotify_data.access_list);
				spin_unlock(&group->notification_lock);
			}
		}
		if (ret < 0)
			break;
		buf += ret;
		count -= ret;
	}
	remove_wait_queue(&group->notification_waitq, &wait);

	if (start != buf && ret != -EFAULT)
		ret = buf - start;
	return ret;
}

static ssize_t fanotify_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	struct fanotify_response response = { .fd = -1, .response = -1 };
	struct fsnotify_group *group;
	int ret;

	if (!IS_ENABLED(CONFIG_FANOTIFY_ACCESS_PERMISSIONS))
		return -EINVAL;

	group = file->private_data;

	if (count > sizeof(response))
		count = sizeof(response);

	pr_debug("%s: group=%p count=%zu\n", __func__, group, count);

	if (copy_from_user(&response, buf, count))
		return -EFAULT;

	ret = process_access_response(group, &response);
	if (ret < 0)
		count = ret;

	return count;
}

static int fanotify_release(struct inode *ignored, struct file *file)
{
	struct fsnotify_group *group = file->private_data;
	struct fanotify_perm_event_info *event, *next;
	struct fsnotify_event *fsn_event;

	/*
	 * Stop new events from arriving in the notification queue. since
	 * userspace cannot use fanotify fd anymore, no event can enter or
	 * leave access_list by now either.
	 */
	fsnotify_group_stop_queueing(group);

	/*
	 * Process all permission events on access_list and notification queue
	 * and simulate reply from userspace.
	 */
	spin_lock(&group->notification_lock);
	list_for_each_entry_safe(event, next, &group->fanotify_data.access_list,
				 fae.fse.list) {
		pr_debug("%s: found group=%p event=%p\n", __func__, group,
			 event);

		list_del_init(&event->fae.fse.list);
		event->response = FAN_ALLOW;
	}

	/*
	 * Destroy all non-permission events. For permission events just
	 * dequeue them and set the response. They will be freed once the
	 * response is consumed and fanotify_get_response() returns.
	 */
	while (!fsnotify_notify_queue_is_empty(group)) {
		fsn_event = fsnotify_remove_first_event(group);
		if (!(fsn_event->mask & FAN_ALL_PERM_EVENTS)) {
			spin_unlock(&group->notification_lock);
			fsnotify_destroy_event(group, fsn_event);
			spin_lock(&group->notification_lock);
		} else {
			FANOTIFY_PE(fsn_event)->response = FAN_ALLOW;
		}
	}
	spin_unlock(&group->notification_lock);

	/* Response for all permission events it set, wakeup waiters */
	wake_up(&group->fanotify_data.access_waitq);

	/* matches the fanotify_init->fsnotify_alloc_group */
	fsnotify_destroy_group(group);

	return 0;
}

static long fanotify_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct fsnotify_group *group;
	struct fsnotify_event *fsn_event;
	void __user *p;
	int ret = -ENOTTY;
	size_t send_len = 0;

	group = file->private_data;

	p = (void __user *) arg;

	switch (cmd) {
	case FIONREAD:
		spin_lock(&group->notification_lock);
		list_for_each_entry(fsn_event, &group->notification_list, list)
			send_len += FAN_EVENT_METADATA_LEN;
		spin_unlock(&group->notification_lock);
		ret = put_user(send_len, (int __user *) p);
		break;
	}

	return ret;
}

static const struct file_operations fanotify_fops = {
	.show_fdinfo	= fanotify_show_fdinfo,
	.poll		= fanotify_poll,
	.read		= fanotify_read,
	.write		= fanotify_write,
	.fasync		= NULL,
	.release	= fanotify_release,
	.unlocked_ioctl	= fanotify_ioctl,
	.compat_ioctl	= fanotify_ioctl,
	.llseek		= noop_llseek,
};

static int fanotify_find_path(int dfd, const char __user *filename,
			      struct path *path, unsigned int flags)
{
	int ret;

	pr_debug("%s: dfd=%d filename=%p flags=%x\n", __func__,
		 dfd, filename, flags);

	if (filename == NULL) {
		struct fd f = fdget(dfd);

		ret = -EBADF;
		if (!f.file)
			goto out;

		ret = -ENOTDIR;
		if ((flags & FAN_MARK_ONLYDIR) &&
		    !(S_ISDIR(file_inode(f.file)->i_mode))) {
			fdput(f);
			goto out;
		}

		*path = f.file->f_path;
		path_get(path);
		fdput(f);
	} else {
		unsigned int lookup_flags = 0;

		if (!(flags & FAN_MARK_DONT_FOLLOW))
			lookup_flags |= LOOKUP_FOLLOW;
		if (flags & FAN_MARK_ONLYDIR)
			lookup_flags |= LOOKUP_DIRECTORY;

		ret = user_path_at(dfd, filename, lookup_flags, path);
		if (ret)
			goto out;
	}

	/* you can only watch an inode if you have read permissions on it */
	ret = inode_permission(path->dentry->d_inode, MAY_READ);
	if (ret)
		path_put(path);
out:
	return ret;
}

static __u32 fanotify_mark_remove_from_mask(struct fsnotify_mark *fsn_mark,
					    __u32 mask,
					    unsigned int flags,
					    int *destroy)
{
	__u32 oldmask = 0;

	spin_lock(&fsn_mark->lock);
	if (!(flags & FAN_MARK_IGNORED_MASK)) {
		__u32 tmask = fsn_mark->mask & ~mask;

		if (flags & FAN_MARK_ONDIR)
			tmask &= ~FAN_ONDIR;

		oldmask = fsn_mark->mask;
		fsn_mark->mask = tmask;
	} else {
		__u32 tmask = fsn_mark->ignored_mask & ~mask;
		if (flags & FAN_MARK_ONDIR)
			tmask &= ~FAN_ONDIR;
		fsn_mark->ignored_mask = tmask;
	}
	*destroy = !(fsn_mark->mask | fsn_mark->ignored_mask);
	spin_unlock(&fsn_mark->lock);

	return mask & oldmask;
}

static int fanotify_remove_vfsmount_mark(struct fsnotify_group *group,
					 struct vfsmount *mnt, __u32 mask,
					 unsigned int flags)
{
	struct fsnotify_mark *fsn_mark = NULL;
	__u32 removed;
	int destroy_mark;

	mutex_lock(&group->mark_mutex);
	fsn_mark = fsnotify_find_mark(&real_mount(mnt)->mnt_fsnotify_marks,
				      group);
	if (!fsn_mark) {
		mutex_unlock(&group->mark_mutex);
		return -ENOENT;
	}

	removed = fanotify_mark_remove_from_mask(fsn_mark, mask, flags,
						 &destroy_mark);
	if (removed & real_mount(mnt)->mnt_fsnotify_mask)
		fsnotify_recalc_mask(real_mount(mnt)->mnt_fsnotify_marks);
	if (destroy_mark)
		fsnotify_detach_mark(fsn_mark);
	mutex_unlock(&group->mark_mutex);
	if (destroy_mark)
		fsnotify_free_mark(fsn_mark);

	fsnotify_put_mark(fsn_mark);
	return 0;
}

static int fanotify_remove_inode_mark(struct fsnotify_group *group,
				      struct inode *inode, __u32 mask,
				      unsigned int flags)
{
	struct fsnotify_mark *fsn_mark = NULL;
	__u32 removed;
	int destroy_mark;

	mutex_lock(&group->mark_mutex);
	fsn_mark = fsnotify_find_mark(&inode->i_fsnotify_marks, group);
	if (!fsn_mark) {
		mutex_unlock(&group->mark_mutex);
		return -ENOENT;
	}

	removed = fanotify_mark_remove_from_mask(fsn_mark, mask, flags,
						 &destroy_mark);
	if (removed & inode->i_fsnotify_mask)
		fsnotify_recalc_mask(inode->i_fsnotify_marks);
	if (destroy_mark)
		fsnotify_detach_mark(fsn_mark);
	mutex_unlock(&group->mark_mutex);
	if (destroy_mark)
		fsnotify_free_mark(fsn_mark);

	/* matches the fsnotify_find_mark() */
	fsnotify_put_mark(fsn_mark);

	return 0;
}

static __u32 fanotify_mark_add_to_mask(struct fsnotify_mark *fsn_mark,
				       __u32 mask,
				       unsigned int flags)
{
	__u32 oldmask = -1;

	spin_lock(&fsn_mark->lock);
	if (!(flags & FAN_MARK_IGNORED_MASK)) {
		__u32 tmask = fsn_mark->mask | mask;

		if (flags & FAN_MARK_ONDIR)
			tmask |= FAN_ONDIR;

		oldmask = fsn_mark->mask;
		fsn_mark->mask = tmask;
	} else {
		__u32 tmask = fsn_mark->ignored_mask | mask;
		if (flags & FAN_MARK_ONDIR)
			tmask |= FAN_ONDIR;

		fsn_mark->ignored_mask = tmask;
		if (flags & FAN_MARK_IGNORED_SURV_MODIFY)
			fsn_mark->flags |= FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY;
	}
	spin_unlock(&fsn_mark->lock);

	return mask & ~oldmask;
}

static struct fsnotify_mark *fanotify_add_new_mark(struct fsnotify_group *group,
						   struct inode *inode,
						   struct vfsmount *mnt)
{
	struct fsnotify_mark *mark;
	int ret;

	if (atomic_read(&group->num_marks) > group->fanotify_data.max_marks)
		return ERR_PTR(-ENOSPC);

	mark = kmem_cache_alloc(fanotify_mark_cache, GFP_KERNEL);
	if (!mark)
		return ERR_PTR(-ENOMEM);

	fsnotify_init_mark(mark, group);
	ret = fsnotify_add_mark_locked(mark, inode, mnt, 0);
	if (ret) {
		fsnotify_put_mark(mark);
		return ERR_PTR(ret);
	}

	return mark;
}


static int fanotify_add_vfsmount_mark(struct fsnotify_group *group,
				      struct vfsmount *mnt, __u32 mask,
				      unsigned int flags)
{
	struct fsnotify_mark *fsn_mark;
	__u32 added;

	mutex_lock(&group->mark_mutex);
	fsn_mark = fsnotify_find_mark(&real_mount(mnt)->mnt_fsnotify_marks,
				      group);
	if (!fsn_mark) {
		fsn_mark = fanotify_add_new_mark(group, NULL, mnt);
		if (IS_ERR(fsn_mark)) {
			mutex_unlock(&group->mark_mutex);
			return PTR_ERR(fsn_mark);
		}
	}
	added = fanotify_mark_add_to_mask(fsn_mark, mask, flags);
	if (added & ~real_mount(mnt)->mnt_fsnotify_mask)
		fsnotify_recalc_mask(real_mount(mnt)->mnt_fsnotify_marks);
	mutex_unlock(&group->mark_mutex);

	fsnotify_put_mark(fsn_mark);
	return 0;
}

static int fanotify_add_inode_mark(struct fsnotify_group *group,
				   struct inode *inode, __u32 mask,
				   unsigned int flags)
{
	struct fsnotify_mark *fsn_mark;
	__u32 added;

	pr_debug("%s: group=%p inode=%p\n", __func__, group, inode);

	/*
	 * If some other task has this inode open for write we should not add
	 * an ignored mark, unless that ignored mark is supposed to survive
	 * modification changes anyway.
	 */
	if ((flags & FAN_MARK_IGNORED_MASK) &&
	    !(flags & FAN_MARK_IGNORED_SURV_MODIFY) &&
	    (atomic_read(&inode->i_writecount) > 0))
		return 0;

	mutex_lock(&group->mark_mutex);
	fsn_mark = fsnotify_find_mark(&inode->i_fsnotify_marks, group);
	if (!fsn_mark) {
		fsn_mark = fanotify_add_new_mark(group, inode, NULL);
		if (IS_ERR(fsn_mark)) {
			mutex_unlock(&group->mark_mutex);
			return PTR_ERR(fsn_mark);
		}
	}
	added = fanotify_mark_add_to_mask(fsn_mark, mask, flags);
	if (added & ~inode->i_fsnotify_mask)
		fsnotify_recalc_mask(inode->i_fsnotify_marks);
	mutex_unlock(&group->mark_mutex);

	fsnotify_put_mark(fsn_mark);
	return 0;
}

/* fanotify syscalls */
SYSCALL_DEFINE2(fanotify_init, unsigned int, flags, unsigned int, event_f_flags)
{
	struct fsnotify_group *group;
	int f_flags, fd;
	struct user_struct *user;
	struct fanotify_event_info *oevent;

	pr_debug("%s: flags=%d event_f_flags=%d\n",
		__func__, flags, event_f_flags);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

#ifdef CONFIG_AUDITSYSCALL
	if (flags & ~(FAN_ALL_INIT_FLAGS | FAN_ENABLE_AUDIT))
#else
	if (flags & ~FAN_ALL_INIT_FLAGS)
#endif
		return -EINVAL;

	if (event_f_flags & ~FANOTIFY_INIT_ALL_EVENT_F_BITS)
		return -EINVAL;

	switch (event_f_flags & O_ACCMODE) {
	case O_RDONLY:
	case O_RDWR:
	case O_WRONLY:
		break;
	default:
		return -EINVAL;
	}

	user = get_current_user();
	if (atomic_read(&user->fanotify_listeners) > FANOTIFY_DEFAULT_MAX_LISTENERS) {
		free_uid(user);
		return -EMFILE;
	}

	f_flags = O_RDWR | FMODE_NONOTIFY;
	if (flags & FAN_CLOEXEC)
		f_flags |= O_CLOEXEC;
	if (flags & FAN_NONBLOCK)
		f_flags |= O_NONBLOCK;

	/* fsnotify_alloc_group takes a ref.  Dropped in fanotify_release */
	group = fsnotify_alloc_group(&fanotify_fsnotify_ops);
	if (IS_ERR(group)) {
		free_uid(user);
		return PTR_ERR(group);
	}

	group->fanotify_data.user = user;
	atomic_inc(&user->fanotify_listeners);

	oevent = fanotify_alloc_event(NULL, FS_Q_OVERFLOW, NULL);
	if (unlikely(!oevent)) {
		fd = -ENOMEM;
		goto out_destroy_group;
	}
	group->overflow_event = &oevent->fse;

	if (force_o_largefile())
		event_f_flags |= O_LARGEFILE;
	group->fanotify_data.f_flags = event_f_flags;
	init_waitqueue_head(&group->fanotify_data.access_waitq);
	INIT_LIST_HEAD(&group->fanotify_data.access_list);
	switch (flags & FAN_ALL_CLASS_BITS) {
	case FAN_CLASS_NOTIF:
		group->priority = FS_PRIO_0;
		break;
	case FAN_CLASS_CONTENT:
		group->priority = FS_PRIO_1;
		break;
	case FAN_CLASS_PRE_CONTENT:
		group->priority = FS_PRIO_2;
		break;
	default:
		fd = -EINVAL;
		goto out_destroy_group;
	}

	if (flags & FAN_UNLIMITED_QUEUE) {
		fd = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out_destroy_group;
		group->max_events = UINT_MAX;
	} else {
		group->max_events = FANOTIFY_DEFAULT_MAX_EVENTS;
	}

	if (flags & FAN_UNLIMITED_MARKS) {
		fd = -EPERM;
		if (!capable(CAP_SYS_ADMIN))
			goto out_destroy_group;
		group->fanotify_data.max_marks = UINT_MAX;
	} else {
		group->fanotify_data.max_marks = FANOTIFY_DEFAULT_MAX_MARKS;
	}

	if (flags & FAN_ENABLE_AUDIT) {
		fd = -EPERM;
		if (!capable(CAP_AUDIT_WRITE))
			goto out_destroy_group;
		group->fanotify_data.audit = true;
	}

	fd = anon_inode_getfd("[fanotify]", &fanotify_fops, group, f_flags);
	if (fd < 0)
		goto out_destroy_group;

	return fd;

out_destroy_group:
	fsnotify_destroy_group(group);
	return fd;
}

SYSCALL_DEFINE5(fanotify_mark, int, fanotify_fd, unsigned int, flags,
			      __u64, mask, int, dfd,
			      const char  __user *, pathname)
{
	struct inode *inode = NULL;
	struct vfsmount *mnt = NULL;
	struct fsnotify_group *group;
	struct fd f;
	struct path path;
	u32 valid_mask = FAN_ALL_EVENTS | FAN_EVENT_ON_CHILD;
	int ret;

	pr_debug("%s: fanotify_fd=%d flags=%x dfd=%d pathname=%p mask=%llx\n",
		 __func__, fanotify_fd, flags, dfd, pathname, mask);

	/* we only use the lower 32 bits as of right now. */
	if (mask & ((__u64)0xffffffff << 32))
		return -EINVAL;

	if (flags & ~FAN_ALL_MARK_FLAGS)
		return -EINVAL;
	switch (flags & (FAN_MARK_ADD | FAN_MARK_REMOVE | FAN_MARK_FLUSH)) {
	case FAN_MARK_ADD:		/* fallthrough */
	case FAN_MARK_REMOVE:
		if (!mask)
			return -EINVAL;
		break;
	case FAN_MARK_FLUSH:
		if (flags & ~(FAN_MARK_MOUNT | FAN_MARK_FLUSH))
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (mask & FAN_ONDIR) {
		flags |= FAN_MARK_ONDIR;
		mask &= ~FAN_ONDIR;
	}

	if (IS_ENABLED(CONFIG_FANOTIFY_ACCESS_PERMISSIONS))
		valid_mask |= FAN_ALL_PERM_EVENTS;

	if (mask & ~valid_mask)
		return -EINVAL;

	f = fdget(fanotify_fd);
	if (unlikely(!f.file))
		return -EBADF;

	/* verify that this is indeed an fanotify instance */
	ret = -EINVAL;
	if (unlikely(f.file->f_op != &fanotify_fops))
		goto fput_and_out;
	group = f.file->private_data;

	/*
	 * group->priority == FS_PRIO_0 == FAN_CLASS_NOTIF.  These are not
	 * allowed to set permissions events.
	 */
	ret = -EINVAL;
	if (mask & FAN_ALL_PERM_EVENTS &&
	    group->priority == FS_PRIO_0)
		goto fput_and_out;

	if (flags & FAN_MARK_FLUSH) {
		ret = 0;
		if (flags & FAN_MARK_MOUNT)
			fsnotify_clear_vfsmount_marks_by_group(group);
		else
			fsnotify_clear_inode_marks_by_group(group);
		goto fput_and_out;
	}

	ret = fanotify_find_path(dfd, pathname, &path, flags);
	if (ret)
		goto fput_and_out;

	/* inode held in place by reference to path; group by fget on fd */
	if (!(flags & FAN_MARK_MOUNT))
		inode = path.dentry->d_inode;
	else
		mnt = path.mnt;

	/* create/update an inode mark */
	switch (flags & (FAN_MARK_ADD | FAN_MARK_REMOVE)) {
	case FAN_MARK_ADD:
		if (flags & FAN_MARK_MOUNT)
			ret = fanotify_add_vfsmount_mark(group, mnt, mask, flags);
		else
			ret = fanotify_add_inode_mark(group, inode, mask, flags);
		break;
	case FAN_MARK_REMOVE:
		if (flags & FAN_MARK_MOUNT)
			ret = fanotify_remove_vfsmount_mark(group, mnt, mask, flags);
		else
			ret = fanotify_remove_inode_mark(group, inode, mask, flags);
		break;
	default:
		ret = -EINVAL;
	}

	path_put(&path);
fput_and_out:
	fdput(f);
	return ret;
}

#ifdef CONFIG_COMPAT
COMPAT_SYSCALL_DEFINE6(fanotify_mark,
				int, fanotify_fd, unsigned int, flags,
				__u32, mask0, __u32, mask1, int, dfd,
				const char  __user *, pathname)
{
	return sys_fanotify_mark(fanotify_fd, flags,
#ifdef __BIG_ENDIAN
				((__u64)mask0 << 32) | mask1,
#else
				((__u64)mask1 << 32) | mask0,
#endif
				 dfd, pathname);
}
#endif

/*
 * fanotify_user_setup - Our initialization function.  Note that we cannot return
 * error because we have compiled-in VFS hooks.  So an (unlikely) failure here
 * must result in panic().
 */
static int __init fanotify_user_setup(void)
{
	fanotify_mark_cache = KMEM_CACHE(fsnotify_mark, SLAB_PANIC);
	fanotify_event_cachep = KMEM_CACHE(fanotify_event_info, SLAB_PANIC);
	if (IS_ENABLED(CONFIG_FANOTIFY_ACCESS_PERMISSIONS)) {
		fanotify_perm_event_cachep =
			KMEM_CACHE(fanotify_perm_event_info, SLAB_PANIC);
	}

	return 0;
}
device_initcall(fanotify_user_setup);
