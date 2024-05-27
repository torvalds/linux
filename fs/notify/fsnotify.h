/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_NOTIFY_FSNOTIFY_H_
#define __FS_NOTIFY_FSNOTIFY_H_

#include <linux/list.h>
#include <linux/fsnotify.h>
#include <linux/srcu.h>
#include <linux/types.h>

#include "../mount.h"

/*
 * fsnotify_connp_t is what we embed in objects which connector can be attached
 * to.
 */
typedef struct fsnotify_mark_connector __rcu *fsnotify_connp_t;

static inline struct inode *fsnotify_conn_inode(
				struct fsnotify_mark_connector *conn)
{
	return conn->obj;
}

static inline struct mount *fsnotify_conn_mount(
				struct fsnotify_mark_connector *conn)
{
	return real_mount(conn->obj);
}

static inline struct super_block *fsnotify_conn_sb(
				struct fsnotify_mark_connector *conn)
{
	return conn->obj;
}

static inline struct super_block *fsnotify_object_sb(void *obj,
			enum fsnotify_obj_type obj_type)
{
	switch (obj_type) {
	case FSNOTIFY_OBJ_TYPE_INODE:
		return ((struct inode *)obj)->i_sb;
	case FSNOTIFY_OBJ_TYPE_VFSMOUNT:
		return ((struct vfsmount *)obj)->mnt_sb;
	case FSNOTIFY_OBJ_TYPE_SB:
		return (struct super_block *)obj;
	default:
		return NULL;
	}
}

static inline struct super_block *fsnotify_connector_sb(
				struct fsnotify_mark_connector *conn)
{
	return fsnotify_object_sb(conn->obj, conn->type);
}

static inline fsnotify_connp_t *fsnotify_sb_marks(struct super_block *sb)
{
	struct fsnotify_sb_info *sbinfo = fsnotify_sb_info(sb);

	return sbinfo ? &sbinfo->sb_marks : NULL;
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
/* run the list of all marks associated with sb and destroy them */
static inline void fsnotify_clear_marks_by_sb(struct super_block *sb)
{
	fsnotify_destroy_marks(fsnotify_sb_marks(sb));
}

/*
 * update the dentry->d_flags of all of inode's children to indicate if inode cares
 * about events that happen to its children.
 */
extern void __fsnotify_update_child_dentry_flags(struct inode *inode);

extern struct kmem_cache *fsnotify_mark_connector_cachep;

#endif	/* __FS_NOTIFY_FSNOTIFY_H_ */
