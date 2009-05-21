#ifndef __FS_NOTIFY_FSNOTIFY_H_
#define __FS_NOTIFY_FSNOTIFY_H_

#include <linux/list.h>
#include <linux/fsnotify.h>
#include <linux/srcu.h>
#include <linux/types.h>

/* protects reads of fsnotify_groups */
extern struct srcu_struct fsnotify_grp_srcu;
/* all groups which receive fsnotify events */
extern struct list_head fsnotify_groups;
/* all bitwise OR of all event types (FS_*) for all fsnotify_groups */
extern __u32 fsnotify_mask;

/* destroy all events sitting in this groups notification queue */
extern void fsnotify_flush_notify(struct fsnotify_group *group);

/* final kfree of a group */
extern void fsnotify_final_destroy_group(struct fsnotify_group *group);

/* run the list of all marks associated with inode and flag them to be freed */
extern void fsnotify_clear_marks_by_inode(struct inode *inode);
/*
 * update the dentry->d_flags of all of inode's children to indicate if inode cares
 * about events that happen to its children.
 */
extern void __fsnotify_update_child_dentry_flags(struct inode *inode);

/* allocate and destroy and event holder to attach events to notification/access queues */
extern struct fsnotify_event_holder *fsnotify_alloc_event_holder(void);
extern void fsnotify_destroy_event_holder(struct fsnotify_event_holder *holder);

#endif	/* __FS_NOTIFY_FSNOTIFY_H_ */
