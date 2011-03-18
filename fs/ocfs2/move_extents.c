/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * move_extents.c
 *
 * Copyright (C) 2011 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/mount.h>
#include <linux/swap.h>

#include <cluster/masklog.h>

#include "ocfs2.h"
#include "ocfs2_ioctl.h"

#include "alloc.h"
#include "aops.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "inode.h"
#include "journal.h"
#include "suballoc.h"
#include "uptodate.h"
#include "super.h"
#include "dir.h"
#include "buffer_head_io.h"
#include "sysfile.h"
#include "suballoc.h"
#include "refcounttree.h"
#include "move_extents.h"

struct ocfs2_move_extents_context {
	struct inode *inode;
	struct file *file;
	int auto_defrag;
	int credits;
	u32 new_phys_cpos;
	u32 clusters_moved;
	u64 refcount_loc;
	struct ocfs2_move_extents *range;
	struct ocfs2_extent_tree et;
	struct ocfs2_alloc_context *meta_ac;
	struct ocfs2_alloc_context *data_ac;
	struct ocfs2_cached_dealloc_ctxt dealloc;
};

/*
 * lock allocators, and reserving appropriate number of bits for
 * meta blocks and data clusters.
 *
 * in some cases, we don't need to reserve clusters, just let data_ac
 * be NULL.
 */
static int ocfs2_lock_allocators_move_extents(struct inode *inode,
					struct ocfs2_extent_tree *et,
					u32 clusters_to_move,
					u32 extents_to_split,
					struct ocfs2_alloc_context **meta_ac,
					struct ocfs2_alloc_context **data_ac,
					int extra_blocks,
					int *credits)
{
	int ret, num_free_extents;
	unsigned int max_recs_needed = 2 * extents_to_split + clusters_to_move;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	num_free_extents = ocfs2_num_free_extents(osb, et);
	if (num_free_extents < 0) {
		ret = num_free_extents;
		mlog_errno(ret);
		goto out;
	}

	if (!num_free_extents ||
	    (ocfs2_sparse_alloc(osb) && num_free_extents < max_recs_needed))
		extra_blocks += ocfs2_extend_meta_needed(et->et_root_el);

	ret = ocfs2_reserve_new_metadata_blocks(osb, extra_blocks, meta_ac);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (data_ac) {
		ret = ocfs2_reserve_clusters(osb, clusters_to_move, data_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	*credits += ocfs2_calc_extend_credits(osb->sb, et->et_root_el,
					      clusters_to_move + 2);

	mlog(0, "reserve metadata_blocks: %d, data_clusters: %u, credits: %d\n",
	     extra_blocks, clusters_to_move, *credits);
out:
	if (ret) {
		if (*meta_ac) {
			ocfs2_free_alloc_context(*meta_ac);
			*meta_ac = NULL;
		}
	}

	return ret;
}
