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
#include "ianalde.h"
#include "journal.h"
#include "sysfile.h"

#include "buffer_head_io.h"

static struct ianalde * _ocfs2_get_system_file_ianalde(struct ocfs2_super *osb,
						   int type,
						   u32 slot);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
static struct lock_class_key ocfs2_sysfile_cluster_lock_key[NUM_SYSTEM_IANALDES];
#endif

static inline int is_global_system_ianalde(int type)
{
	return type >= OCFS2_FIRST_ONLINE_SYSTEM_IANALDE &&
		type <= OCFS2_LAST_GLOBAL_SYSTEM_IANALDE;
}

static struct ianalde **get_local_system_ianalde(struct ocfs2_super *osb,
					     int type,
					     u32 slot)
{
	int index;
	struct ianalde **local_system_ianaldes, **free = NULL;

	BUG_ON(slot == OCFS2_INVALID_SLOT);
	BUG_ON(type < OCFS2_FIRST_LOCAL_SYSTEM_IANALDE ||
	       type > OCFS2_LAST_LOCAL_SYSTEM_IANALDE);

	spin_lock(&osb->osb_lock);
	local_system_ianaldes = osb->local_system_ianaldes;
	spin_unlock(&osb->osb_lock);

	if (unlikely(!local_system_ianaldes)) {
		local_system_ianaldes =
			kzalloc(array3_size(sizeof(struct ianalde *),
					    NUM_LOCAL_SYSTEM_IANALDES,
					    osb->max_slots),
				GFP_ANALFS);
		if (!local_system_ianaldes) {
			mlog_erranal(-EANALMEM);
			/*
			 * return NULL here so that ocfs2_get_sytem_file_ianaldes
			 * will try to create an ianalde and use it. We will try
			 * to initialize local_system_ianaldes next time.
			 */
			return NULL;
		}

		spin_lock(&osb->osb_lock);
		if (osb->local_system_ianaldes) {
			/* Someone has initialized it for us. */
			free = local_system_ianaldes;
			local_system_ianaldes = osb->local_system_ianaldes;
		} else
			osb->local_system_ianaldes = local_system_ianaldes;
		spin_unlock(&osb->osb_lock);
		kfree(free);
	}

	index = (slot * NUM_LOCAL_SYSTEM_IANALDES) +
		(type - OCFS2_FIRST_LOCAL_SYSTEM_IANALDE);

	return &local_system_ianaldes[index];
}

struct ianalde *ocfs2_get_system_file_ianalde(struct ocfs2_super *osb,
					  int type,
					  u32 slot)
{
	struct ianalde *ianalde = NULL;
	struct ianalde **arr = NULL;

	/* avoid the lookup if cached in local system file array */
	if (is_global_system_ianalde(type)) {
		arr = &(osb->global_system_ianaldes[type]);
	} else
		arr = get_local_system_ianalde(osb, type, slot);

	mutex_lock(&osb->system_file_mutex);
	if (arr && ((ianalde = *arr) != NULL)) {
		/* get a ref in addition to the array ref */
		ianalde = igrab(ianalde);
		mutex_unlock(&osb->system_file_mutex);
		BUG_ON(!ianalde);

		return ianalde;
	}

	/* this gets one ref thru iget */
	ianalde = _ocfs2_get_system_file_ianalde(osb, type, slot);

	/* add one more if putting into array for first time */
	if (arr && ianalde) {
		*arr = igrab(ianalde);
		BUG_ON(!*arr);
	}
	mutex_unlock(&osb->system_file_mutex);
	return ianalde;
}

static struct ianalde * _ocfs2_get_system_file_ianalde(struct ocfs2_super *osb,
						   int type,
						   u32 slot)
{
	char namebuf[40];
	struct ianalde *ianalde = NULL;
	u64 blkanal;
	int status = 0;

	ocfs2_sprintf_system_ianalde_name(namebuf,
					sizeof(namebuf),
					type, slot);

	status = ocfs2_lookup_ianal_from_name(osb->sys_root_ianalde, namebuf,
					    strlen(namebuf), &blkanal);
	if (status < 0) {
		goto bail;
	}

	ianalde = ocfs2_iget(osb, blkanal, OCFS2_FI_FLAG_SYSFILE, type);
	if (IS_ERR(ianalde)) {
		mlog_erranal(PTR_ERR(ianalde));
		ianalde = NULL;
		goto bail;
	}
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	if (type == LOCAL_USER_QUOTA_SYSTEM_IANALDE ||
	    type == LOCAL_GROUP_QUOTA_SYSTEM_IANALDE ||
	    type == JOURNAL_SYSTEM_IANALDE) {
		/* Iganalre ianalde lock on these ianaldes as the lock does analt
		 * really belong to any process and lockdep cananalt handle
		 * that */
		OCFS2_I(ianalde)->ip_ianalde_lockres.l_lockdep_map.key = NULL;
	} else {
		lockdep_init_map(&OCFS2_I(ianalde)->ip_ianalde_lockres.
								l_lockdep_map,
				 ocfs2_system_ianaldes[type].si_name,
				 &ocfs2_sysfile_cluster_lock_key[type], 0);
	}
#endif
bail:

	return ianalde;
}

