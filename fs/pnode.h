/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/fs/pnode.h
 *
 * (C) Copyright IBM Corporation 2005.
 */
#ifndef _LINUX_PNODE_H
#define _LINUX_PNODE_H

#include <linux/list.h>
#include "mount.h"

#define IS_MNT_SHARED(m) ((m)->mnt_t_flags & T_SHARED)
#define IS_MNT_SLAVE(m) ((m)->mnt_master)
#define IS_MNT_NEW(m) (!(m)->mnt_ns)
#define CLEAR_MNT_SHARED(m) ((m)->mnt_t_flags &= ~T_SHARED)
#define IS_MNT_UNBINDABLE(m) ((m)->mnt_t_flags & T_UNBINDABLE)
#define IS_MNT_MARKED(m) ((m)->mnt_t_flags & T_MARKED)
#define SET_MNT_MARK(m) ((m)->mnt_t_flags |= T_MARKED)
#define CLEAR_MNT_MARK(m) ((m)->mnt_t_flags &= ~T_MARKED)
#define IS_MNT_LOCKED(m) ((m)->mnt.mnt_flags & MNT_LOCKED)

#define CL_EXPIRE    		0x01
#define CL_SLAVE     		0x02
#define CL_COPY_UNBINDABLE	0x04
#define CL_MAKE_SHARED 		0x08
#define CL_PRIVATE 		0x10
#define CL_COPY_MNT_NS_FILE	0x40

/*
 * EXCL[namespace_sem]
 */
static inline void set_mnt_shared(struct mount *mnt)
{
	mnt->mnt_t_flags &= ~T_SHARED_MASK;
	mnt->mnt_t_flags |= T_SHARED;
}

static inline bool peers(const struct mount *m1, const struct mount *m2)
{
	return m1->mnt_group_id == m2->mnt_group_id && m1->mnt_group_id;
}

void change_mnt_propagation(struct mount *, int);
void bulk_make_private(struct list_head *);
int propagate_mnt(struct mount *, struct mountpoint *, struct mount *,
		struct hlist_head *);
void propagate_umount(struct list_head *);
int propagate_mount_busy(struct mount *, int);
void propagate_mount_unlock(struct mount *);
void mnt_release_group_id(struct mount *);
int get_dominating_id(struct mount *mnt, const struct path *root);
int mnt_get_count(struct mount *mnt);
void mnt_set_mountpoint(struct mount *, struct mountpoint *,
			struct mount *);
void mnt_change_mountpoint(struct mount *parent, struct mountpoint *mp,
			   struct mount *mnt);
struct mount *copy_tree(struct mount *, struct dentry *, int);
bool is_path_reachable(struct mount *, struct dentry *,
			 const struct path *root);
int count_mounts(struct mnt_namespace *ns, struct mount *mnt);
bool propagation_would_overmount(const struct mount *from,
				 const struct mount *to,
				 const struct mountpoint *mp);
#endif /* _LINUX_PNODE_H */
