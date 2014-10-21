#ifndef __FS_NOTIFY_FSNOTIFY_H_
#define __FS_NOTIFY_FSNOTIFY_H_

#include <linux/list.h>
#include <linux/fsnotify.h>
#include <linux/srcu.h>
#include <linux/types.h>

/* destroy all events sitting in this groups notification queue */
extern void fsnotify_flush_notify(struct fsnotify_group *group);

/* protects reads of inode and vfsmount marks list */
extern struct srcu_struct fsnotify_mark_srcu;

extern void fsnotify_set_inode_mark_mask_locked(struct fsnotify_mark *fsn_mark,
						__u32 mask);
/* add a mark to an inode */
extern int fsnotify_add_inode_mark(struct fsnotify_mark *mark,
				   struct fsnotify_group *group, struct inode *inode,
				   int allow_dups);
/* add a mark to a vfsmount */
extern int fsnotify_add_vfsmount_mark(struct fsnotify_mark *mark,
				      struct fsnotify_group *group, struct vfsmount *mnt,
				      int allow_dups);

/* vfsmount specific destruction of a mark */
extern void fsnotify_destroy_vfsmount_mark(struct fsnotify_mark *mark);
/* inode specific destruction of a mark */
extern void fsnotify_destroy_inode_mark(struct fsnotify_mark *mark);
/* run the list of all marks associated with inode and flag them to be freed */
extern void fsnotify_clear_marks_by_inode(struct inode *inode);
/* run the list of all marks associated with vfsmount and flag them to be freed */
extern void fsnotify_clear_marks_by_mount(struct vfsmount *mnt);
/*
 * update the dentry->d_flags of all of inode's children to indicate if inode cares
 * about events that happen to its children.
 */
extern void __fsnotify_update_child_dentry_flags(struct inode *inode);

/* allocate and destroy and event holder to attach events to notification/access queues */
extern struct fsnotify_event_holder *fsnotify_alloc_event_holder(void);
extern void fsnotify_destroy_event_holder(struct fsnotify_event_holder *holder);

#endif	/* __FS_NOTIFY_FSNOTIFY_H_ */
