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

static int __ocfs2_move_extent(handle_t *handle,
			       struct ocfs2_move_extents_context *context,
			       u32 cpos, u32 len, u32 p_cpos, u32 new_p_cpos,
			       int ext_flags)
{
	int ret = 0, index;
	struct inode *inode = context->inode;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_extent_rec *rec, replace_rec;
	struct ocfs2_path *path = NULL;
	struct ocfs2_extent_list *el;
	u64 ino = ocfs2_metadata_cache_owner(context->et.et_ci);
	u64 old_blkno = ocfs2_clusters_to_blocks(inode->i_sb, p_cpos);

	ret = ocfs2_duplicate_clusters_by_page(handle, context->file, cpos,
					       p_cpos, new_p_cpos, len);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	memset(&replace_rec, 0, sizeof(replace_rec));
	replace_rec.e_cpos = cpu_to_le32(cpos);
	replace_rec.e_leaf_clusters = cpu_to_le16(len);
	replace_rec.e_blkno = cpu_to_le64(ocfs2_clusters_to_blocks(inode->i_sb,
								   new_p_cpos));

	path = ocfs2_new_path_from_et(&context->et);
	if (!path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(INODE_CACHE(inode), path, cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	el = path_leaf_el(path);

	index = ocfs2_search_extent_list(el, cpos);
	if (index == -1 || index >= le16_to_cpu(el->l_next_free_rec)) {
		ocfs2_error(inode->i_sb,
			    "Inode %llu has an extent at cpos %u which can no "
			    "longer be found.\n",
			    (unsigned long long)ino, cpos);
		ret = -EROFS;
		goto out;
	}

	rec = &el->l_recs[index];

	BUG_ON(ext_flags != rec->e_flags);
	/*
	 * after moving/defraging to new location, the extent is not going
	 * to be refcounted anymore.
	 */
	replace_rec.e_flags = ext_flags & ~OCFS2_EXT_REFCOUNTED;

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode),
				      context->et.et_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_split_extent(handle, &context->et, path, index,
				 &replace_rec, context->meta_ac,
				 &context->dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_journal_dirty(handle, context->et.et_root_bh);

	context->new_phys_cpos = new_p_cpos;

	/*
	 * need I to append truncate log for old clusters?
	 */
	if (old_blkno) {
		if (ext_flags & OCFS2_EXT_REFCOUNTED)
			ret = ocfs2_decrease_refcount(inode, handle,
					ocfs2_blocks_to_clusters(osb->sb,
								 old_blkno),
					len, context->meta_ac,
					&context->dealloc, 1);
		else
			ret = ocfs2_truncate_log_append(osb, handle,
							old_blkno, len);
	}

out:
	return ret;
}

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
