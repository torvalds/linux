/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_NOTIFY_FSNOTIFY_H_
#define __FS_NOTIFY_FSNOTIFY_H_

#include <linux/list.h>
#include <linux/fsyestify.h>
#include <linux/srcu.h>
#include <linux/types.h>

#include "../mount.h"

static inline struct iyesde *fsyestify_conn_iyesde(
				struct fsyestify_mark_connector *conn)
{
	return container_of(conn->obj, struct iyesde, i_fsyestify_marks);
}

static inline struct mount *fsyestify_conn_mount(
				struct fsyestify_mark_connector *conn)
{
	return container_of(conn->obj, struct mount, mnt_fsyestify_marks);
}

static inline struct super_block *fsyestify_conn_sb(
				struct fsyestify_mark_connector *conn)
{
	return container_of(conn->obj, struct super_block, s_fsyestify_marks);
}

/* destroy all events sitting in this groups yestification queue */
extern void fsyestify_flush_yestify(struct fsyestify_group *group);

/* protects reads of iyesde and vfsmount marks list */
extern struct srcu_struct fsyestify_mark_srcu;

/* compare two groups for sorting of marks lists */
extern int fsyestify_compare_groups(struct fsyestify_group *a,
				   struct fsyestify_group *b);

/* Destroy all marks attached to an object via connector */
extern void fsyestify_destroy_marks(fsyestify_connp_t *connp);
/* run the list of all marks associated with iyesde and destroy them */
static inline void fsyestify_clear_marks_by_iyesde(struct iyesde *iyesde)
{
	fsyestify_destroy_marks(&iyesde->i_fsyestify_marks);
}
/* run the list of all marks associated with vfsmount and destroy them */
static inline void fsyestify_clear_marks_by_mount(struct vfsmount *mnt)
{
	fsyestify_destroy_marks(&real_mount(mnt)->mnt_fsyestify_marks);
}
/* run the list of all marks associated with sb and destroy them */
static inline void fsyestify_clear_marks_by_sb(struct super_block *sb)
{
	fsyestify_destroy_marks(&sb->s_fsyestify_marks);
}

/*
 * update the dentry->d_flags of all of iyesde's children to indicate if iyesde cares
 * about events that happen to its children.
 */
extern void __fsyestify_update_child_dentry_flags(struct iyesde *iyesde);

/* allocate and destroy and event holder to attach events to yestification/access queues */
extern struct fsyestify_event_holder *fsyestify_alloc_event_holder(void);
extern void fsyestify_destroy_event_holder(struct fsyestify_event_holder *holder);

extern struct kmem_cache *fsyestify_mark_connector_cachep;

#endif	/* __FS_NOTIFY_FSNOTIFY_H_ */
