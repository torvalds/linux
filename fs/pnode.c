/*
 *  linux/fs/pnode.c
 *
 * (C) Copyright IBM Corporation 2005.
 *	Released under GPL v2.
 *	Author : Ram Pai (linuxram@us.ibm.com)
 *
 */
#include <linux/namespace.h>
#include <linux/mount.h>
#include <linux/fs.h>
#include "pnode.h"

/* return the next shared peer mount of @p */
static inline struct vfsmount *next_peer(struct vfsmount *p)
{
	return list_entry(p->mnt_share.next, struct vfsmount, mnt_share);
}

void change_mnt_propagation(struct vfsmount *mnt, int type)
{
	if (type == MS_SHARED) {
		mnt->mnt_flags |= MNT_SHARED;
	} else {
		list_del_init(&mnt->mnt_share);
		mnt->mnt_flags &= ~MNT_PNODE_MASK;
	}
}
