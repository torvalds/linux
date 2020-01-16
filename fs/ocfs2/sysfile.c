// SPDX-License-Identifier: GPL-2.0-or-later
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
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
#include "iyesde.h"
#include "journal.h"
#include "sysfile.h"

#include "buffer_head_io.h"

static struct iyesde * _ocfs2_get_system_file_iyesde(struct ocfs2_super *osb,
						   int type,
						   u32 slot);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key ocfs2_sysfile_cluster_lock_key[NUM_SYSTEM_INODES];
#endif

static inline int is_global_system_iyesde(int type)
{
	return type >= OCFS2_FIRST_ONLINE_SYSTEM_INODE &&
		type <= OCFS2_LAST_GLOBAL_SYSTEM_INODE;
}

static struct iyesde **get_local_system_iyesde(struct ocfs2_super *osb,
					     int type,
					     u32 slot)
{
	int index;
	struct iyesde **local_system_iyesdes, **free = NULL;

	BUG_ON(slot == OCFS2_INVALID_SLOT);
	BUG_ON(type < OCFS2_FIRST_LOCAL_SYSTEM_INODE ||
	       type > OCFS2_LAST_LOCAL_SYSTEM_INODE);

	spin_lock(&osb->osb_lock);
	local_system_iyesdes = osb->local_system_iyesdes;
	spin_unlock(&osb->osb_lock);

	if (unlikely(!local_system_iyesdes)) {
		local_system_iyesdes =
			kzalloc(array3_size(sizeof(struct iyesde *),
					    NUM_LOCAL_SYSTEM_INODES,
					    osb->max_slots),
				GFP_NOFS);
		if (!local_system_iyesdes) {
			mlog_erryes(-ENOMEM);
			/*
			 * return NULL here so that ocfs2_get_sytem_file_iyesdes
			 * will try to create an iyesde and use it. We will try
			 * to initialize local_system_iyesdes next time.
			 */
			return NULL;
		}

		spin_lock(&osb->osb_lock);
		if (osb->local_system_iyesdes) {
			/* Someone has initialized it for us. */
			free = local_system_iyesdes;
			local_system_iyesdes = osb->local_system_iyesdes;
		} else
			osb->local_system_iyesdes = local_system_iyesdes;
		spin_unlock(&osb->osb_lock);
		kfree(free);
	}

	index = (slot * NUM_LOCAL_SYSTEM_INODES) +
		(type - OCFS2_FIRST_LOCAL_SYSTEM_INODE);

	return &local_system_iyesdes[index];
}

struct iyesde *ocfs2_get_system_file_iyesde(struct ocfs2_super *osb,
					  int type,
					  u32 slot)
{
	struct iyesde *iyesde = NULL;
	struct iyesde **arr = NULL;

	/* avoid the lookup if cached in local system file array */
	if (is_global_system_iyesde(type)) {
		arr = &(osb->global_system_iyesdes[type]);
	} else
		arr = get_local_system_iyesde(osb, type, slot);

	mutex_lock(&osb->system_file_mutex);
	if (arr && ((iyesde = *arr) != NULL)) {
		/* get a ref in addition to the array ref */
		iyesde = igrab(iyesde);
		mutex_unlock(&osb->system_file_mutex);
		BUG_ON(!iyesde);

		return iyesde;
	}

	/* this gets one ref thru iget */
	iyesde = _ocfs2_get_system_file_iyesde(osb, type, slot);

	/* add one more if putting into array for first time */
	if (arr && iyesde) {
		*arr = igrab(iyesde);
		BUG_ON(!*arr);
	}
	mutex_unlock(&osb->system_file_mutex);
	return iyesde;
}

static struct iyesde * _ocfs2_get_system_file_iyesde(struct ocfs2_super *osb,
						   int type,
						   u32 slot)
{
	char namebuf[40];
	struct iyesde *iyesde = NULL;
	u64 blkyes;
	int status = 0;

	ocfs2_sprintf_system_iyesde_name(namebuf,
					sizeof(namebuf),
					type, slot);

	status = ocfs2_lookup_iyes_from_name(osb->sys_root_iyesde, namebuf,
					    strlen(namebuf), &blkyes);
	if (status < 0) {
		goto bail;
	}

	iyesde = ocfs2_iget(osb, blkyes, OCFS2_FI_FLAG_SYSFILE, type);
	if (IS_ERR(iyesde)) {
		mlog_erryes(PTR_ERR(iyesde));
		iyesde = NULL;
		goto bail;
	}
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (type == LOCAL_USER_QUOTA_SYSTEM_INODE ||
	    type == LOCAL_GROUP_QUOTA_SYSTEM_INODE ||
	    type == JOURNAL_SYSTEM_INODE) {
		/* Igyesre iyesde lock on these iyesdes as the lock does yest
		 * really belong to any process and lockdep canyest handle
		 * that */
		OCFS2_I(iyesde)->ip_iyesde_lockres.l_lockdep_map.key = NULL;
	} else {
		lockdep_init_map(&OCFS2_I(iyesde)->ip_iyesde_lockres.
								l_lockdep_map,
				 ocfs2_system_iyesdes[type].si_name,
				 &ocfs2_sysfile_cluster_lock_key[type], 0);
	}
#endif
bail:

	return iyesde;
}

