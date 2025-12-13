// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sysfile.c
 *
 * Initialize, read, write, etc. system files.
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/highmem.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dir.h"
#include "inode.h"
#include "journal.h"
#include "sysfile.h"

#include "buffer_head_io.h"

static struct inode * _ocfs2_get_system_file_inode(struct ocfs2_super *osb,
						   int type,
						   u32 slot);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key ocfs2_sysfile_cluster_lock_key[NUM_SYSTEM_INODES];
#endif

static inline int is_global_system_inode(int type)
{
	return type >= OCFS2_FIRST_ONLINE_SYSTEM_INODE &&
		type <= OCFS2_LAST_GLOBAL_SYSTEM_INODE;
}

static struct inode **get_local_system_inode(struct ocfs2_super *osb,
					     int type,
					     u32 slot)
{
	int index;
	struct inode **local_system_inodes, **free = NULL;

	BUG_ON(slot == OCFS2_INVALID_SLOT);
	BUG_ON(type < OCFS2_FIRST_LOCAL_SYSTEM_INODE ||
	       type > OCFS2_LAST_LOCAL_SYSTEM_INODE);

	spin_lock(&osb->osb_lock);
	local_system_inodes = osb->local_system_inodes;
	spin_unlock(&osb->osb_lock);

	if (unlikely(!local_system_inodes)) {
		local_system_inodes =
			kzalloc(array3_size(sizeof(struct inode *),
					    NUM_LOCAL_SYSTEM_INODES,
					    osb->max_slots),
				GFP_NOFS);
		if (!local_system_inodes) {
			mlog_errno(-ENOMEM);
			/*
			 * return NULL here so that ocfs2_get_sytem_file_inodes
			 * will try to create an inode and use it. We will try
			 * to initialize local_system_inodes next time.
			 */
			return NULL;
		}

		spin_lock(&osb->osb_lock);
		if (osb->local_system_inodes) {
			/* Someone has initialized it for us. */
			free = local_system_inodes;
			local_system_inodes = osb->local_system_inodes;
		} else
			osb->local_system_inodes = local_system_inodes;
		spin_unlock(&osb->osb_lock);
		kfree(free);
	}

	index = (slot * NUM_LOCAL_SYSTEM_INODES) +
		(type - OCFS2_FIRST_LOCAL_SYSTEM_INODE);

	return &local_system_inodes[index];
}

struct inode *ocfs2_get_system_file_inode(struct ocfs2_super *osb,
					  int type,
					  u32 slot)
{
	struct inode *inode = NULL;
	struct inode **arr = NULL;

	/* avoid the lookup if cached in local system file array */
	if (is_global_system_inode(type)) {
		arr = &(osb->global_system_inodes[type]);
	} else
		arr = get_local_system_inode(osb, type, slot);

	mutex_lock(&osb->system_file_mutex);
	if (arr && ((inode = *arr) != NULL)) {
		/* get a ref in addition to the array ref */
		inode = igrab(inode);
		mutex_unlock(&osb->system_file_mutex);
		BUG_ON(!inode);

		return inode;
	}

	/* this gets one ref thru iget */
	inode = _ocfs2_get_system_file_inode(osb, type, slot);

	/* add one more if putting into array for first time */
	if (arr && inode) {
		*arr = igrab(inode);
		BUG_ON(!*arr);
	}
	mutex_unlock(&osb->system_file_mutex);
	return inode;
}

static struct inode * _ocfs2_get_system_file_inode(struct ocfs2_super *osb,
						   int type,
						   u32 slot)
{
	char namebuf[40];
	struct inode *inode = NULL;
	u64 blkno;
	int len, status = 0;

	len = ocfs2_sprintf_system_inode_name(namebuf,
					      sizeof(namebuf),
					      type, slot);

	status = ocfs2_lookup_ino_from_name(osb->sys_root_inode,
					    namebuf, len, &blkno);
	if (status < 0) {
		goto bail;
	}

	inode = ocfs2_iget(osb, blkno, OCFS2_FI_FLAG_SYSFILE, type);
	if (IS_ERR(inode)) {
		mlog_errno(PTR_ERR(inode));
		inode = NULL;
		goto bail;
	}
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (type == LOCAL_USER_QUOTA_SYSTEM_INODE ||
	    type == LOCAL_GROUP_QUOTA_SYSTEM_INODE ||
	    type == JOURNAL_SYSTEM_INODE) {
		/* Ignore inode lock on these inodes as the lock does not
		 * really belong to any process and lockdep cannot handle
		 * that */
		OCFS2_I(inode)->ip_inode_lockres.l_lockdep_map.key = NULL;
	} else {
		lockdep_init_map(&OCFS2_I(inode)->ip_inode_lockres.
								l_lockdep_map,
				 ocfs2_system_inodes[type].si_name,
				 &ocfs2_sysfile_cluster_lock_key[type], 0);
	}
#endif
bail:

	return inode;
}

