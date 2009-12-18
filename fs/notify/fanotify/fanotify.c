#include <linux/fdtable.h>
#include <linux/fsnotify_backend.h>
#include <linux/init.h>
#include <linux/kernel.h> /* UINT_MAX */
#include <linux/types.h>

#include "fanotify.h"

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

	ret = fsnotify_add_notify_event(group, event, NULL, NULL);

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
