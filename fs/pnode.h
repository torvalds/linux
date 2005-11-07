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
#include <linux/mount.h>

#define IS_MNT_SHARED(mnt) (mnt->mnt_flags & MNT_SHARED)
#define CLEAR_MNT_SHARED(mnt) (mnt->mnt_flags &= ~MNT_SHARED)

void change_mnt_propagation(struct vfsmount *, int);
#endif /* _LINUX_PNODE_H */
