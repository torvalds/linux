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

void change_mnt_propagation(struct vfsmount *mnt, int type)
{
	mnt->mnt_flags &= ~MNT_PNODE_MASK;
}
