/*
 *  linux/fs/pnode.h
 *
 * (C) Copyright IBM Corporation 2005.
 *	Released under GPL v2.
 *
 */
#ifndef _LINUX_PNODE_H
#define _LINUX_PNODE_H

#include <linux/list.h>
#include "mount.h"

#define IS_MNT_SHARED(m) ((m)->mnt.mnt_flags & MNT_SHARED)
#define IS_MNT_SLAVE(m) ((m)->mnt_master)
#define IS_MNT_NEW(m)  (!(m)->mnt_ns)
#define CLEAR_MNT_SHARED(m) ((m)->mnt.mnt_flags &= ~MNT_SHARED)
#define IS_MNT_UNBINDABLE(m) ((m)->mnt.mnt_flags & MNT_UNBINDABLE)
#define IS_MNT_MARKED(m) ((m)->mnt.mnt_flags & MNT_MARKED)
#define SET_MNT_MARK(m) ((m)->mnt.mnt_flags |= MNT_MARKED)
#define CLEAR_MNT_MARK(m) ((m)->mnt.mnt_flags &= ~MNT_MARKED)
#define IS_MNT_LOCKED(m) ((m)->mnt.mnt_flags & MNT_LOCKED)

#define CL_EXPIRE    		0x01
#define CL_SLAVE     		0x02
#define CL_COPY_UNBINDABLE	0x04
#define CL_MAKE_SHARED 		0x08
#define CL_PRIVATE 		0x10
#define CL_SHARED_TO_SLAVE	0x20
#define CL_UNPRIVILEGED		0x40
#define CL_COPY_MNT_NS_FILE	0x80

#define CL_COPY_ALL		(CL_COPY_UNBINDABLE | CL_COPY_MNT_NS_FILE)

static inline void set_mnt_shared(struct mount *mnt)
{
	mnt->mnt.mnt_flags &= ~MNT_SHARED_MASK;
	mnt->mnt.mnt_flags |= MNT_SHARED;
}

void change_mnt_propagation(struct mount *, int);
int propagate_mnt(struct mount *, struct mountpoint *, struct mount *,
		struct hlist_head *);
int propagate_umount(struct list_head *);
int propagate_mount_busy(struct mount *, int);
void propagate_mount_unlock(struct mount *);
void mnt_release_group_id(struct mount *);
int get_dominating_id(struct mount *mnt, const struct path *root);
unsigned int mnt_get_count(struct mount *mnt);
void mnt_set_mountpoint(struct mount *, struct mountpoint *,
			struct mount *);
void mnt_change_mountpoint(struct mount *parent, struct mountpoint *mp,
			   struct mount *mnt);
struct mount *copy_tree(struct mount *, struct dentry *, int);
bool is_path_reachable(struct mount *, struct dentry *,
			 const struct path *root);
#endif /* _LINUX_PNODE_H */
