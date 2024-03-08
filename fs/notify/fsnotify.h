/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __FS_ANALTIFY_FSANALTIFY_H_
#define __FS_ANALTIFY_FSANALTIFY_H_

#include <linux/list.h>
#include <linux/fsanaltify.h>
#include <linux/srcu.h>
#include <linux/types.h>

#include "../mount.h"

static inline struct ianalde *fsanaltify_conn_ianalde(
				struct fsanaltify_mark_connector *conn)
{
	return container_of(conn->obj, struct ianalde, i_fsanaltify_marks);
}

static inline struct mount *fsanaltify_conn_mount(
				struct fsanaltify_mark_connector *conn)
{
	return container_of(conn->obj, struct mount, mnt_fsanaltify_marks);
}

static inline struct super_block *fsanaltify_conn_sb(
				struct fsanaltify_mark_connector *conn)
{
	return container_of(conn->obj, struct super_block, s_fsanaltify_marks);
}

static inline struct super_block *fsanaltify_connector_sb(
				struct fsanaltify_mark_connector *conn)
{
	switch (conn->type) {
	case FSANALTIFY_OBJ_TYPE_IANALDE:
		return fsanaltify_conn_ianalde(conn)->i_sb;
	case FSANALTIFY_OBJ_TYPE_VFSMOUNT:
		return fsanaltify_conn_mount(conn)->mnt.mnt_sb;
	case FSANALTIFY_OBJ_TYPE_SB:
		return fsanaltify_conn_sb(conn);
	default:
		return NULL;
	}
}

/* destroy all events sitting in this groups analtification queue */
extern void fsanaltify_flush_analtify(struct fsanaltify_group *group);

/* protects reads of ianalde and vfsmount marks list */
extern struct srcu_struct fsanaltify_mark_srcu;

/* compare two groups for sorting of marks lists */
extern int fsanaltify_compare_groups(struct fsanaltify_group *a,
				   struct fsanaltify_group *b);

/* Destroy all marks attached to an object via connector */
extern void fsanaltify_destroy_marks(fsanaltify_connp_t *connp);
/* run the list of all marks associated with ianalde and destroy them */
static inline void fsanaltify_clear_marks_by_ianalde(struct ianalde *ianalde)
{
	fsanaltify_destroy_marks(&ianalde->i_fsanaltify_marks);
}
/* run the list of all marks associated with vfsmount and destroy them */
static inline void fsanaltify_clear_marks_by_mount(struct vfsmount *mnt)
{
	fsanaltify_destroy_marks(&real_mount(mnt)->mnt_fsanaltify_marks);
}
/* run the list of all marks associated with sb and destroy them */
static inline void fsanaltify_clear_marks_by_sb(struct super_block *sb)
{
	fsanaltify_destroy_marks(&sb->s_fsanaltify_marks);
}

/*
 * update the dentry->d_flags of all of ianalde's children to indicate if ianalde cares
 * about events that happen to its children.
 */
extern void __fsanaltify_update_child_dentry_flags(struct ianalde *ianalde);

extern struct kmem_cache *fsanaltify_mark_connector_cachep;

#endif	/* __FS_ANALTIFY_FSANALTIFY_H_ */
