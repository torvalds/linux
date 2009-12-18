#include <linux/fanotify.h>
#include <linux/fdtable.h>
#include <linux/fsnotify_backend.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h> /* UINT_MAX */
#include <linux/mount.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/wait.h>

static bool should_merge(struct fsnotify_event *old, struct fsnotify_event *new)
{
	pr_debug("%s: old=%p new=%p\n", __func__, old, new);

	if (old->to_tell == new->to_tell &&
	    old->data_type == new->data_type &&
	    old->tgid == new->tgid) {
		switch (old->data_type) {
		case (FSNOTIFY_EVENT_PATH):
			if ((old->path.mnt == new->path.mnt) &&
			    (old->path.dentry == new->path.dentry))
				return true;
		case (FSNOTIFY_EVENT_NONE):
			return true;
		default:
			BUG();
		};
	}
	return false;
}

/* Note, if we return an event in *arg that a reference is being held... */
static int fanotify_merge(struct list_head *list,
			  struct fsnotify_event *event,
			  void **arg)
{
	struct fsnotify_event_holder *test_holder;
	struct fsnotify_event *test_event;
	struct fsnotify_event *new_event;
	struct fsnotify_event **return_event = (struct fsnotify_event **)arg;
	int ret = 0;

	pr_debug("%s: list=%p event=%p\n", __func__, list, event);

	*return_event = NULL;

	/* and the list better be locked by something too! */

	list_for_each_entry_reverse(test_holder, list, event_list) {
		test_event = test_holder->event;
		if (should_merge(test_event, event)) {
			fsnotify_get_event(test_event);
			*return_event = test_event;

			ret = -EEXIST;
			/* if they are exactly the same we are done */
			if (test_event->mask == event->mask)
				goto out;

			/*
			 * if the refcnt == 1 this is the only queue
			 * for this event and so we can update the mask
			 * in place.
			 */
			if (atomic_read(&test_event->refcnt) == 1) {
				test_event->mask |= event->mask;
				goto out;
			}

			/* can't allocate memory, merge was no possible */
			new_event = fsnotify_clone_event(test_event);
			if (unlikely(!new_event)) {
				ret = 0;
				goto out;
			}

			/* we didn't return the test_event, so drop that ref */
			fsnotify_put_event(test_event);
			/* the reference we return on new_event is from clone */
			*return_event = new_event;

			/* build new event and replace it on the list */
			new_event->mask = (test_event->mask | event->mask);
			fsnotify_replace_event(test_holder, new_event);

			break;
		}
	}
out:
	return ret;
}

#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
static int fanotify_get_response_from_access(struct fsnotify_group *group,
					     struct fsnotify_event *event)
{
	int ret;

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	wait_event(group->fanotify_data.access_waitq, event->response);

	/* userspace responded, convert to something usable */
	spin_lock(&event->lock);
	switch (event->response) {
	case FAN_ALLOW:
		ret = 0;
		break;
	case FAN_DENY:
	default:
		ret = -EPERM;
	}
	event->response = 0;
	spin_unlock(&event->lock);

	return ret;
}
#endif

static int fanotify_handle_event(struct fsnotify_group *group, struct fsnotify_event *event)
{
	int ret;
	struct fsnotify_event *notify_event = NULL;

	BUILD_BUG_ON(FAN_ACCESS != FS_ACCESS);
	BUILD_BUG_ON(FAN_MODIFY != FS_MODIFY);
	BUILD_BUG_ON(FAN_CLOSE_NOWRITE != FS_CLOSE_NOWRITE);
	BUILD_BUG_ON(FAN_CLOSE_WRITE != FS_CLOSE_WRITE);
	BUILD_BUG_ON(FAN_OPEN != FS_OPEN);
	BUILD_BUG_ON(FAN_EVENT_ON_CHILD != FS_EVENT_ON_CHILD);
	BUILD_BUG_ON(FAN_Q_OVERFLOW != FS_Q_OVERFLOW);
	BUILD_BUG_ON(FAN_OPEN_PERM != FS_OPEN_PERM);
	BUILD_BUG_ON(FAN_ACCESS_PERM != FS_ACCESS_PERM);

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	ret = fsnotify_add_notify_event(group, event, NULL, fanotify_merge,
					(void **)&notify_event);
	/* -EEXIST means this event was merged with another, not that it was an error */
	if (ret == -EEXIST)
		ret = 0;
	if (ret)
		goto out;

#ifdef CONFIG_FANOTIFY_ACCESS_PERMISSIONS
	if (event->mask & FAN_ALL_PERM_EVENTS) {
		/* if we merged we need to wait on the new event */
		if (notify_event)
			event = notify_event;
		ret = fanotify_get_response_from_access(group, event);
	}
#endif

out:
	if (notify_event)
		fsnotify_put_event(notify_event);
	return ret;
}

static bool should_send_vfsmount_event(struct fsnotify_group *group, struct vfsmount *mnt,
				       struct inode *inode, __u32 mask)
{
	struct fsnotify_mark *mnt_mark;
	struct fsnotify_mark *inode_mark;

	pr_debug("%s: group=%p vfsmount=%p mask=%x\n",
		 __func__, group, mnt, mask);

	mnt_mark = fsnotify_find_vfsmount_mark(group, mnt);
	if (!mnt_mark)
		return false;

	mask &= mnt_mark->mask;
	mask &= ~mnt_mark->ignored_mask;

	if (mask) {
		inode_mark = fsnotify_find_inode_mark(group, inode);
		if (inode_mark) {
			mask &= ~inode_mark->ignored_mask;
			fsnotify_put_mark(inode_mark);
		}
	}

	/* find took a reference */
	fsnotify_put_mark(mnt_mark);

	return mask;
}

static bool should_send_inode_event(struct fsnotify_group *group, struct inode *inode,
				    __u32 mask)
{
	struct fsnotify_mark *fsn_mark;

	pr_debug("%s: group=%p inode=%p mask=%x\n",
		 __func__, group, inode, mask);

	fsn_mark = fsnotify_find_inode_mark(group, inode);
	if (!fsn_mark)
		return false;

	/* if the event is for a child and this inode doesn't care about
	 * events on the child, don't send it! */
	if ((mask & FS_EVENT_ON_CHILD) &&
	    !(fsn_mark->mask & FS_EVENT_ON_CHILD)) {
		mask = 0;
	} else {
		/*
		 * We care about children, but do we care about this particular
		 * type of event?
		 */
		mask &= ~FS_EVENT_ON_CHILD;
		mask &= fsn_mark->mask;
		mask &= ~fsn_mark->ignored_mask;
	}

	/* find took a reference */
	fsnotify_put_mark(fsn_mark);

	return mask;
}

static bool fanotify_should_send_event(struct fsnotify_group *group, struct inode *to_tell,
				       struct vfsmount *mnt, __u32 mask, void *data,
				       int data_type)
{
	pr_debug("%s: group=%p to_tell=%p mnt=%p mask=%x data=%p data_type=%d\n",
		 __func__, group, to_tell, mnt, mask, data, data_type);

	/* sorry, fanotify only gives a damn about files and dirs */
	if (!S_ISREG(to_tell->i_mode) &&
	    !S_ISDIR(to_tell->i_mode))
		return false;

	/* if we don't have enough info to send an event to userspace say no */
	if (data_type != FSNOTIFY_EVENT_PATH)
		return false;

	if (mnt)
		return should_send_vfsmount_event(group, mnt, to_tell, mask);
	else
		return should_send_inode_event(group, to_tell, mask);
}

const struct fsnotify_ops fanotify_fsnotify_ops = {
	.handle_event = fanotify_handle_event,
	.should_send_event = fanotify_should_send_event,
	.free_group_priv = NULL,
	.free_event_priv = NULL,
	.freeing_mark = NULL,
};
