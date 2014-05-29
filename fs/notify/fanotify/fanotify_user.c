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

#include <asm/ioctls.h>

#include "../../mount.h"
#include "../fdinfo.h"
#include "fanotify.h"

#define FANOTIFY_DEFAULT_MAX_EVENTS	16384
#define FANOTIFY_DEFAULT_MAX_MARKS	8192
#define FANOTIFY_DEFAULT_MAX_LISTENERS	128

extern const struct fsnotify_ops fanotify_fsnotify_ops;

static struct kmem_cache *fanotify_mark_cache __read_mostly;
struct kmem_cache *fanotify_event_cachep __read_mostly;
struct kmem_cache *fanotify_perm_event_cachep __read_mostly;

/*
 * Get an fsnotify notification event if one exists and is small
 * enough to fit in "count". Return an error pointer if the count
 * is not large enough.
 *
 * Called with the group->notification_mutex held.
 */
static struct fsnotify_event *get_one_event(struct fsnotify_group *group,
					    size_t count)
{
	BUG_ON(!mutex_is_locked(&group->notification_mutex));

	pr_debug("%s: group=%p count=%zd\n", __func__, group, count);

	if (fsnotify_notify_queue_is_empty(group))
		return NULL;

	if (FAN_EVENT_METADATA_LEN > count)
		return ERR_PTR(-EINVAL);

	/* held the notification_mutex the whole time, so this is the
	 * same event we peeked above */
	return fsnotify_remove_notify_event(group);
}

static int create_fd(struct fsnotify_group *group,
		     struct fanotify_event_info *event,
		     struct file **file)
{
	int client_fd;
	struct file *new_file;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	client_fd = get_unused_fd();
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

#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
static struct fanotify_perm_event_info *dequeue_event(
				struct fsnotify_group *group, int fd)
{
	struct fanotify_perm_event_info *event, *return_e = NULL;

	spin_lock(&group->fanotify_data.access_lock);
	list_for_each_entry(event, &group->fanotify_data.access_list,
			    fae.fse.list) {
		if (event->fd != fd)
			continue;

		list_del_init(&event->fae.fse.list);
		return_e = event;
		break;
	}
	spin_unlock(&group->fanotify_data.access_lock);

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
	switch (response) {
	case FAN_ALLOW:
	case FAN_DENY:
		break;
	default:
		return -EINVAL;
	}

	if (fd < 0)
		return -EINVAL;

	event = dequeue_event(group, fd);
	if (!event)
		return -ENOENT;

	event->response = response;
	wake_up(&group->fanotify_data.access_waitq);

	return 0;
}
#endif

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

#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
	if (event->mask & FAN_ALL_PERM_EVENTS)
		FANOTIFY_PE(event)->fd = fd;
#endif

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
static unsigned int fanotify_poll(struct file *file, poll_table *wait)
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

static ssize_t fanotify_read(struct file *file, char __user *buf,
			     size_t count, loff_t *pos)
{
	struct fsnotify_group *group;
	struct fsnotify_event *kevent;
	char __user *start;
	int ret;
	DEFINE_WAIT(wait);

	start = buf;
	group = file->private_data;

	pr_debug("%s: group=%p\n", __func__, group);

	while (1) {
		prepare_to_wait(&group->notification_waitq, &wait, TASK_INTERRUPTIBLE);

		mutex_lock(&group->notification_mutex);
		kevent = get_one_event(group, count);
		mutex_unlock(&group->notification_mutex);

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
			schedule();
			continue;
		}

		ret = copy_event_to_user(group, kevent, buf);
		/*
		 * Permission events get queued to wait for response.  Other
		 * events can be destroyed now.
		 */
		if (!(kevent->mask & FAN_ALL_PERM_EVENTS)) {
			fsnotify_destroy_event(group, kevent);
			if (ret < 0)
				break;
		} else {
#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
			if (ret < 0) {
				FANOTIFY_PE(kevent)->response = FAN_DENY;
				wake_up(&group->fanotify_data.access_waitq);
				break;
			}
			spin_lock(&group->fanotify_data.access_lock);
			list_add_tail(&kevent->list,
				      &group->fanotify_data.access_list);
			spin_unlock(&group->fanotify_data.access_lock);
#endif
		}
		buf += ret;
		count -= ret;
	}

	finish_wait(&group->notification_waitq, &wait);
	if (start != buf && ret != -EFAULT)
		ret = buf - start;
	return ret;
}

static ssize_t fanotify_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
	struct fanotify_response response = { .fd = -1, .response = -1 };
	struct fsnotify_group *group;
	int ret;

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
#else
	return -EINVAL;
#endif
}

static int fanotify_release(struct inode *ignored, struct file *file)
{
	struct fsnotify_group *group = file->private_data;

#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
	struct fanotify_perm_event_info *event, *next;

	spin_lock(&group->fanotify_data.access_lock);

	atomic_inc(&group->fanotify_data.bypass_perm);

	list_for_each_entry_safe(event, next, &group->fanotify_data.access_list,
				 fae.fse.list) {
		pr_debug("%s: found group=%p event=%p\n", __func__, group,
			 event);

		list_del_init(&event->fae.fse.list);
		event->response = FAN_ALLOW;
	}
	spin_unlock(&group->fanotify_data.access_lock);

	wake_up(&group->fanotify_data.access_waitq);
#endif

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
		mutex_lock(&group->notification_mutex);
		list_for_each_entry(fsn_event, &group->notification_list, list)
			send_len += FAN_EVENT_METADATA_LEN;
		mutex_unlock(&group->notification_mutex);
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

static void fanotify_free_mark(struct fsnotify_mark *fsn_mark)
{
	kmem_cache_free(fanotify_mark_cache, fsn_mark);
}

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
	__u32 oldmask;

	spin_lock(&fsn_mark->lock);
	if (!(flags & FAN_MARK_IGNORED_MASK)) {
		oldmask = fsn_mark->mask;
		fsnotify_set_mark_mask_locked(fsn_mark, (oldmask & ~mask));
	} else {
		oldmask = fsn_mark->ignored_mask;
		fsnotify_set_mark_ignored_mask_locked(fsn_mark, (oldmask & ~mask));
	}
	spin_unlock(&fsn_mark->lock);

	*destroy = !(oldmask & ~mask);

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
	fsn_mark = fsnotify_find_vfsmount_mark(group, mnt);
	if (!fsn_mark) {
		mutex_unlock(&group->mark_mutex);
		return -ENOENT;
	}

	removed = fanotify_mark_remove_from_mask(fsn_mark, mask, flags,
						 &destroy_mark);
	if (destroy_mark)
		fsnotify_destroy_mark_locked(fsn_mark, group);
	mutex_unlock(&group->mark_mutex);

	fsnotify_put_mark(fsn_mark);
	if (removed & real_mount(mnt)->mnt_fsnotify_mask)
		fsnotify_recalc_vfsmount_mask(mnt);

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
	fsn_mark = fsnotify_find_inode_mark(group, inode);
	if (!fsn_mark) {
		mutex_unlock(&group->mark_mutex);
		return -ENOENT;
	}

	removed = fanotify_mark_remove_from_mask(fsn_mark, mask, flags,
						 &destroy_mark);
	if (destroy_mark)
		fsnotify_destroy_mark_locked(fsn_mark, group);
	mutex_unlock(&group->mark_mutex);

	/* matches the fsnotify_find_inode_mark() */
	fsnotify_put_mark(fsn_mark);
	if (removed & inode->i_fsnotify_mask)
		fsnotify_recalc_inode_mask(inode);

	return 0;
}

static __u32 fanotify_mark_add_to_mask(struct fsnotify_mark *fsn_mark,
				       __u32 mask,
				       unsigned int flags)
{
	__u32 oldmask = -1;

	spin_lock(&fsn_mark->lock);
	if (!(flags & FAN_MARK_IGNORED_MASK)) {
		oldmask = fsn_mark->mask;
		fsnotify_set_mark_mask_locked(fsn_mark, (oldmask | mask));
	} else {
		__u32 tmask = fsn_mark->ignored_mask | mask;
		fsnotify_set_mark_ignored_mask_locked(fsn_mark, tmask);
		if (flags & FAN_MARK_IGNORED_SURV_MODIFY)
			fsn_mark->flags |= FSNOTIFY_MARK_FLAG_IGNORED_SURV_MODIFY;
	}

	if (!(flags & FAN_MARK_ONDIR)) {
		__u32 tmask = fsn_mark->ignored_mask | FAN_ONDIR;
		fsnotify_set_mark_ignored_mask_locked(fsn_mark, tmask);
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

	fsnotify_init_mark(mark, fanotify_free_mark);
	ret = fsnotify_add_mark_locked(mark, group, inode, mnt, 0);
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
	fsn_mark = fsnotify_find_vfsmount_mark(group, mnt);
	if (!fsn_mark) {
		fsn_mark = fanotify_add_new_mark(group, NULL, mnt);
		if (IS_ERR(fsn_mark)) {
			mutex_unlock(&group->mark_mutex);
			return PTR_ERR(fsn_mark);
		}
	}
	added = fanotify_mark_add_to_mask(fsn_mark, mask, flags);
	mutex_unlock(&group->mark_mutex);

	if (added & ~real_mount(mnt)->mnt_fsnotify_mask)
		fsnotify_recalc_vfsmount_mask(mnt);

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
	fsn_mark = fsnotify_find_inode_mark(group, inode);
	if (!fsn_mark) {
		fsn_mark = fanotify_add_new_mark(group, inode, NULL);
		if (IS_ERR(fsn_mark)) {
			mutex_unlock(&group->mark_mutex);
			return PTR_ERR(fsn_mark);
		}
	}
	added = fanotify_mark_add_to_mask(fsn_mark, mask, flags);
	mutex_unlock(&group->mark_mutex);

	if (added & ~inode->i_fsnotify_mask)
		fsnotify_recalc_inode_mask(inode);

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

	if (flags & ~FAN_ALL_INIT_FLAGS)
		return -EINVAL;

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
#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
	spin_lock_init(&group->fanotify_data.access_lock);
	init_waitqueue_head(&group->fanotify_data.access_waitq);
	INIT_LIST_HEAD(&group->fanotify_data.access_list);
	atomic_set(&group->fanotify_data.bypass_perm, 0);
#endif
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
	case FAN_MARK_FLUSH:
		break;
	default:
		return -EINVAL;
	}

	if (mask & FAN_ONDIR) {
		flags |= FAN_MARK_ONDIR;
		mask &= ~FAN_ONDIR;
	}

#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
	if (mask & ~(FAN_ALL_EVENTS | FAN_ALL_PERM_EVENTS | FAN_EVENT_ON_CHILD))
#else
	if (mask & ~(FAN_ALL_EVENTS | FAN_EVENT_ON_CHILD))
#endif
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

	ret = fanotify_find_path(dfd, pathname, &path, flags);
	if (ret)
		goto fput_and_out;

	/* inode held in place by reference to path; group by fget on fd */
	if (!(flags & FAN_MARK_MOUNT))
		inode = path.dentry->d_inode;
	else
		mnt = path.mnt;

	/* create/update an inode mark */
	switch (flags & (FAN_MARK_ADD | FAN_MARK_REMOVE | FAN_MARK_FLUSH)) {
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
	case FAN_MARK_FLUSH:
		if (flags & FAN_MARK_MOUNT)
			fsnotify_clear_vfsmount_marks_by_group(group);
		else
			fsnotify_clear_inode_marks_by_group(group);
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
#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
	fanotify_perm_event_cachep = KMEM_CACHE(fanotify_perm_event_info,
						SLAB_PANIC);
#endif

	return 0;
}
device_initcall(fanotify_user_setup);
