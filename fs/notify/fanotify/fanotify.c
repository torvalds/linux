#include <linux/fdtable.h>
#include <linux/fsnotify_backend.h>
#include <linux/init.h>
#include <linux/kernel.h> /* UINT_MAX */
#include <linux/types.h>

#include "fanotify.h"

static bool should_merge(struct fsnotify_event *old, struct fsnotify_event *new)
{
	pr_debug("%s: old=%p new=%p\n", __func__, old, new);

	if ((old->mask == new->mask) &&
	    (old->to_tell == new->to_tell) &&
	    (old->data_type == new->data_type)) {
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

static int fanotify_merge(struct list_head *list, struct fsnotify_event *event)
{
	struct fsnotify_event_holder *holder;
	struct fsnotify_event *test_event;

	pr_debug("%s: list=%p event=%p\n", __func__, list, event);

	/* and the list better be locked by something too! */

	list_for_each_entry_reverse(holder, list, event_list) {
		test_event = holder->event;
		if (should_merge(test_event, event))
			return -EEXIST;
	}

	return 0;
}

static int fanotify_handle_event(struct fsnotify_group *group, struct fsnotify_event *event)
{
	int ret;


	BUILD_BUG_ON(FAN_ACCESS != FS_ACCESS);
	BUILD_BUG_ON(FAN_MODIFY != FS_MODIFY);
	BUILD_BUG_ON(FAN_CLOSE_NOWRITE != FS_CLOSE_NOWRITE);
	BUILD_BUG_ON(FAN_CLOSE_WRITE != FS_CLOSE_WRITE);
	BUILD_BUG_ON(FAN_OPEN != FS_OPEN);
	BUILD_BUG_ON(FAN_EVENT_ON_CHILD != FS_EVENT_ON_CHILD);
	BUILD_BUG_ON(FAN_Q_OVERFLOW != FS_Q_OVERFLOW);

	pr_debug("%s: group=%p event=%p\n", __func__, group, event);

	ret = fsnotify_add_notify_event(group, event, NULL, fanotify_merge);
	/* -EEXIST means this event was merged with another, not that it was an error */
	if (ret == -EEXIST)
		ret = 0;
	return ret;
}

static bool fanotify_should_send_event(struct fsnotify_group *group, struct inode *inode,
				       struct vfsmount *mnt, __u32 mask, void *data,
				       int data_type)
{
	struct fsnotify_mark *fsn_mark;
	bool send;

	pr_debug("%s: group=%p inode=%p mask=%x data=%p data_type=%d\n",
		 __func__, group, inode, mask, data, data_type);

	/* sorry, fanotify only gives a damn about files and dirs */
	if (!S_ISREG(inode->i_mode) &&
	    !S_ISDIR(inode->i_mode))
		return false;

	/* if we don't have enough info to send an event to userspace say no */
	if (data_type != FSNOTIFY_EVENT_PATH)
		return false;

	fsn_mark = fsnotify_find_mark(group, inode);
	if (!fsn_mark)
		return false;

	/* if the event is for a child and this inode doesn't care about
	 * events on the child, don't send it! */
	if ((mask & FS_EVENT_ON_CHILD) &&
	    !(fsn_mark->mask & FS_EVENT_ON_CHILD)) {
		send = false;
	} else {
		/*
		 * We care about children, but do we care about this particular
		 * type of event?
		 */
		mask = (mask & ~FS_EVENT_ON_CHILD);
		send = (fsn_mark->mask & mask);
	}

	/* find took a reference */
	fsnotify_put_mark(fsn_mark);

	return send;
}

const struct fsnotify_ops fanotify_fsnotify_ops = {
	.handle_event = fanotify_handle_event,
	.should_send_event = fanotify_should_send_event,
	.free_group_priv = NULL,
	.free_event_priv = NULL,
	.freeing_mark = NULL,
};
