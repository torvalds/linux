/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * xattr.c
 *
 * Copyright (C) 2008 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#define MLOG_MASK_PREFIX ML_XATTR
#include <cluster/masklog.h>

#include "ocfs2.h"
#include "alloc.h"
#include "dlmglue.h"
#include "file.h"
#include "inode.h"
#include "journal.h"
#include "ocfs2_fs.h"
#include "suballoc.h"
#include "uptodate.h"
#include "buffer_head_io.h"

static int ocfs2_xattr_extend_allocation(struct inode *inode,
					 u32 clusters_to_add,
					 struct buffer_head *xattr_bh,
					 struct ocfs2_xattr_value_root *xv)
{
	int status = 0;
	int restart_func = 0;
	int credits = 0;
	handle_t *handle = NULL;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	enum ocfs2_alloc_restarted why;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_extent_list *root_el = &xv->xr_list;
	u32 prev_clusters, logical_start = le32_to_cpu(xv->xr_clusters);

	mlog(0, "(clusters_to_add for xattr= %u)\n", clusters_to_add);

restart_all:

	status = ocfs2_lock_allocators(inode, xattr_bh, root_el,
				       clusters_to_add, 0, &data_ac,
				       &meta_ac, OCFS2_XATTR_VALUE_EXTENT, xv);
	if (status) {
		mlog_errno(status);
		goto leave;
	}

	credits = ocfs2_calc_extend_credits(osb->sb, root_el, clusters_to_add);
	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto leave;
	}

restarted_transaction:
	status = ocfs2_journal_access(handle, inode, xattr_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	prev_clusters = le32_to_cpu(xv->xr_clusters);
	status = ocfs2_add_clusters_in_btree(osb,
					     inode,
					     &logical_start,
					     clusters_to_add,
					     0,
					     xattr_bh,
					     root_el,
					     handle,
					     data_ac,
					     meta_ac,
					     &why,
					     OCFS2_XATTR_VALUE_EXTENT,
					     xv);
	if ((status < 0) && (status != -EAGAIN)) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	status = ocfs2_journal_dirty(handle, xattr_bh);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	clusters_to_add -= le32_to_cpu(xv->xr_clusters) - prev_clusters;

	if (why != RESTART_NONE && clusters_to_add) {
		if (why == RESTART_META) {
			mlog(0, "restarting function.\n");
			restart_func = 1;
		} else {
			BUG_ON(why != RESTART_TRANS);

			mlog(0, "restarting transaction.\n");
			/* TODO: This can be more intelligent. */
			credits = ocfs2_calc_extend_credits(osb->sb,
							    root_el,
							    clusters_to_add);
			status = ocfs2_extend_trans(handle, credits);
			if (status < 0) {
				/* handle still has to be committed at
				 * this point. */
				status = -ENOMEM;
				mlog_errno(status);
				goto leave;
			}
			goto restarted_transaction;
		}
	}

leave:
	if (handle) {
		ocfs2_commit_trans(osb, handle);
		handle = NULL;
	}
	if (data_ac) {
		ocfs2_free_alloc_context(data_ac);
		data_ac = NULL;
	}
	if (meta_ac) {
		ocfs2_free_alloc_context(meta_ac);
		meta_ac = NULL;
	}
	if ((!status) && restart_func) {
		restart_func = 0;
		goto restart_all;
	}

	return status;
}

static int __ocfs2_remove_xattr_range(struct inode *inode,
				      struct buffer_head *root_bh,
				      struct ocfs2_xattr_value_root *xv,
				      u32 cpos, u32 phys_cpos, u32 len,
				      struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret;
	u64 phys_blkno = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct inode *tl_inode = osb->osb_tl_inode;
	handle_t *handle;
	struct ocfs2_alloc_context *meta_ac = NULL;

	ret = ocfs2_lock_allocators(inode, root_bh, &xv->xr_list,
				    0, 1, NULL, &meta_ac,
				    OCFS2_XATTR_VALUE_EXTENT, xv);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	mutex_lock(&tl_inode->i_mutex);

	if (ocfs2_truncate_log_needs_flush(osb)) {
		ret = __ocfs2_flush_truncate_log(osb);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	handle = ocfs2_start_trans(osb, OCFS2_REMOVE_EXTENT_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access(handle, inode, root_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_remove_extent(inode, root_bh, cpos, len, handle, meta_ac,
				  dealloc, OCFS2_XATTR_VALUE_EXTENT, xv);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	le32_add_cpu(&xv->xr_clusters, -len);

	ret = ocfs2_journal_dirty(handle, root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_truncate_log_append(osb, handle, phys_blkno, len);
	if (ret)
		mlog_errno(ret);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	mutex_unlock(&tl_inode->i_mutex);

	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	return ret;
}

static int ocfs2_xattr_shrink_size(struct inode *inode,
				   u32 old_clusters,
				   u32 new_clusters,
				   struct buffer_head *root_bh,
				   struct ocfs2_xattr_value_root *xv)
{
	int ret = 0;
	u32 trunc_len, cpos, phys_cpos, alloc_size;
	u64 block;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_cached_dealloc_ctxt dealloc;

	ocfs2_init_dealloc_ctxt(&dealloc);

	if (old_clusters <= new_clusters)
		return 0;

	cpos = new_clusters;
	trunc_len = old_clusters - new_clusters;
	while (trunc_len) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &phys_cpos,
					       &alloc_size, &xv->xr_list);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (alloc_size > trunc_len)
			alloc_size = trunc_len;

		ret = __ocfs2_remove_xattr_range(inode, root_bh, xv, cpos,
						 phys_cpos, alloc_size,
						 &dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		block = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);
		ocfs2_remove_xattr_clusters_from_cache(inode, block,
						       alloc_size);
		cpos += alloc_size;
		trunc_len -= alloc_size;
	}

out:
	ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &dealloc);

	return ret;
}

static int ocfs2_xattr_value_truncate(struct inode *inode,
				      struct buffer_head *root_bh,
				      struct ocfs2_xattr_value_root *xv,
				      int len)
{
	int ret;
	u32 new_clusters = ocfs2_clusters_for_bytes(inode->i_sb, len);
	u32 old_clusters = le32_to_cpu(xv->xr_clusters);

	if (new_clusters == old_clusters)
		return 0;

	if (new_clusters > old_clusters)
		ret = ocfs2_xattr_extend_allocation(inode,
						    new_clusters - old_clusters,
						    root_bh, xv);
	else
		ret = ocfs2_xattr_shrink_size(inode,
					      old_clusters, new_clusters,
					      root_bh, xv);

	return ret;
}
