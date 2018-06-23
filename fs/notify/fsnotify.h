/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_NOTIFY_FSNOTIFY_H_
#define __FS_NOTIFY_FSNOTIFY_H_

#include <linux/list.h>
#include <linux/fsnotify.h>
#include <linux/srcu.h>
#include <linux/types.h>

#include "../mount.h"

static inline struct inode *fsnotify_conn_inode(
				struct fsnotify_mark_connector *conn)
{
	return container_of(conn->obj, struct inode, i_fsnotify_marks);
}

static inline struct mount *fsnotify_conn_mount(
				struct fsnotify_mark_connector *conn)
{
	return container_of(conn->obj, struct mount, mnt_fsnotify_marks);
}

/* destroy all events sitting in this groups notification queue */
extern void fsnotify_flush_notify(struct fsnotify_group *group);

/* protects reads of inode and vfsmount marks list */
extern struct srcu_struct fsnotify_mark_srcu;

/* compare two groups for sorting of marks lists */
extern int fsnotify_compare_groups(struct fsnotify_group *a,
				   struct fsnotify_group *b);

/* Destroy all marks attached to an object via connector */
extern void fsnotify_destroy_marks(fsnotify_connp_t *connp);
/* run the list of all marks associated with inode and destroy them */
static inline void fsnotify_clear_marks_by_inode(struct inode *inode)
{
	fsnotify_destroy_marks(&inode->i_fsnotify_marks);
}
/* run the list of all marks associated with vfsmount and destroy them */
static inline void fsnotify_clear_marks_by_mount(struct vfsmount *mnt)
{
	fsnotify_destroy_marks(&real_mount(mnt)->mnt_fsnotify_marks);
}
/* Wait until all marks queued for destruction are destroyed */
extern void fsnotify_wait_marks_destroyed(void);

/*
 * update the dentry->d_flags of all of inode's children to indicate if inode cares
 * about events that happen to its children.
 */
extern void __fsnotify_update_child_dentry_flags(struct inode *inode);

/* allocate and destroy and event holder to attach events to notification/access queues */
extern struct fsnotify_event_holder *fsnotify_alloc_event_holder(void);
extern void fsnotify_destroy_event_holder(struct fsnotify_event_holder *holder);

#endif	/* __FS_NOTIFY_FSNOTIFY_H_ */
