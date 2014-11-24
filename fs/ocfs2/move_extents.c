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
#include "refcounttree.h"
#include "move_extents.h"

struct ocfs2_move_extents_context {
	struct inode *inode;
	struct file *file;
	int auto_defrag;
	int partial;
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

	ret = ocfs2_duplicate_clusters_by_page(handle, inode, cpos,
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
	if (index == -1) {
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

	ocfs2_update_inode_fsync_trans(handle, inode, 0);
out:
	ocfs2_free_path(path);
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

	*credits += ocfs2_calc_extend_credits(osb->sb, et->et_root_el);

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

/*
 * Using one journal handle to guarantee the data consistency in case
 * crash happens anywhere.
 *
 *  XXX: defrag can end up with finishing partial extent as requested,
 * due to not enough contiguous clusters can be found in allocator.
 */
static int ocfs2_defrag_extent(struct ocfs2_move_extents_context *context,
			       u32 cpos, u32 phys_cpos, u32 *len, int ext_flags)
{
	int ret, credits = 0, extra_blocks = 0, partial = context->partial;
	handle_t *handle;
	struct inode *inode = context->inode;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct inode *tl_inode = osb->osb_tl_inode;
	struct ocfs2_refcount_tree *ref_tree = NULL;
	u32 new_phys_cpos, new_len;
	u64 phys_blkno = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);

	if ((ext_flags & OCFS2_EXT_REFCOUNTED) && *len) {

		BUG_ON(!(OCFS2_I(inode)->ip_dyn_features &
			 OCFS2_HAS_REFCOUNT_FL));

		BUG_ON(!context->refcount_loc);

		ret = ocfs2_lock_refcount_tree(osb, context->refcount_loc, 1,
					       &ref_tree, NULL);
		if (ret) {
			mlog_errno(ret);
			return ret;
		}

		ret = ocfs2_prepare_refcount_change_for_del(inode,
							context->refcount_loc,
							phys_blkno,
							*len,
							&credits,
							&extra_blocks);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	ret = ocfs2_lock_allocators_move_extents(inode, &context->et, *len, 1,
						 &context->meta_ac,
						 &context->data_ac,
						 extra_blocks, &credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * should be using allocation reservation strategy there?
	 *
	 * if (context->data_ac)
	 *	context->data_ac->ac_resv = &OCFS2_I(inode)->ip_la_data_resv;
	 */

	mutex_lock(&tl_inode->i_mutex);

	if (ocfs2_truncate_log_needs_flush(osb)) {
		ret = __ocfs2_flush_truncate_log(osb);
		if (ret < 0) {
			mlog_errno(ret);
			goto out_unlock_mutex;
		}
	}

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_unlock_mutex;
	}

	ret = __ocfs2_claim_clusters(handle, context->data_ac, 1, *len,
				     &new_phys_cpos, &new_len);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * allowing partial extent moving is kind of 'pros and cons', it makes
	 * whole defragmentation less likely to fail, on the contrary, the bad
	 * thing is it may make the fs even more fragmented after moving, let
	 * userspace make a good decision here.
	 */
	if (new_len != *len) {
		mlog(0, "len_claimed: %u, len: %u\n", new_len, *len);
		if (!partial) {
			context->range->me_flags &= ~OCFS2_MOVE_EXT_FL_COMPLETE;
			ret = -ENOSPC;
			goto out_commit;
		}
	}

	mlog(0, "cpos: %u, phys_cpos: %u, new_phys_cpos: %u\n", cpos,
	     phys_cpos, new_phys_cpos);

	ret = __ocfs2_move_extent(handle, context, cpos, new_len, phys_cpos,
				  new_phys_cpos, ext_flags);
	if (ret)
		mlog_errno(ret);

	if (partial && (new_len != *len))
		*len = new_len;

	/*
	 * Here we should write the new page out first if we are
	 * in write-back mode.
	 */
	ret = ocfs2_cow_sync_writeback(inode->i_sb, context->inode, cpos, *len);
	if (ret)
		mlog_errno(ret);

out_commit:
	ocfs2_commit_trans(osb, handle);

out_unlock_mutex:
	mutex_unlock(&tl_inode->i_mutex);

	if (context->data_ac) {
		ocfs2_free_alloc_context(context->data_ac);
		context->data_ac = NULL;
	}

	if (context->meta_ac) {
		ocfs2_free_alloc_context(context->meta_ac);
		context->meta_ac = NULL;
	}

out:
	if (ref_tree)
		ocfs2_unlock_refcount_tree(osb, ref_tree, 1);

	return ret;
}

/*
 * find the victim alloc group, where #blkno fits.
 */
static int ocfs2_find_victim_alloc_group(struct inode *inode,
					 u64 vict_blkno,
					 int type, int slot,
					 int *vict_bit,
					 struct buffer_head **ret_bh)
{
	int ret, i, bits_per_unit = 0;
	u64 blkno;
	char namebuf[40];

	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *ac_bh = NULL, *gd_bh = NULL;
	struct ocfs2_chain_list *cl;
	struct ocfs2_chain_rec *rec;
	struct ocfs2_dinode *ac_dinode;
	struct ocfs2_group_desc *bg;

	ocfs2_sprintf_system_inode_name(namebuf, sizeof(namebuf), type, slot);
	ret = ocfs2_lookup_ino_from_name(osb->sys_root_inode, namebuf,
					 strlen(namebuf), &blkno);
	if (ret) {
		ret = -ENOENT;
		goto out;
	}

	ret = ocfs2_read_blocks_sync(osb, blkno, 1, &ac_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ac_dinode = (struct ocfs2_dinode *)ac_bh->b_data;
	cl = &(ac_dinode->id2.i_chain);
	rec = &(cl->cl_recs[0]);

	if (type == GLOBAL_BITMAP_SYSTEM_INODE)
		bits_per_unit = osb->s_clustersize_bits -
					inode->i_sb->s_blocksize_bits;
	/*
	 * 'vict_blkno' was out of the valid range.
	 */
	if ((vict_blkno < le64_to_cpu(rec->c_blkno)) ||
	    (vict_blkno >= ((u64)le32_to_cpu(ac_dinode->id1.bitmap1.i_total) <<
				bits_per_unit))) {
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < le16_to_cpu(cl->cl_next_free_rec); i++) {

		rec = &(cl->cl_recs[i]);
		if (!rec)
			continue;

		bg = NULL;

		do {
			if (!bg)
				blkno = le64_to_cpu(rec->c_blkno);
			else
				blkno = le64_to_cpu(bg->bg_next_group);

			if (gd_bh) {
				brelse(gd_bh);
				gd_bh = NULL;
			}

			ret = ocfs2_read_blocks_sync(osb, blkno, 1, &gd_bh);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			bg = (struct ocfs2_group_desc *)gd_bh->b_data;

			if (vict_blkno < (le64_to_cpu(bg->bg_blkno) +
						le16_to_cpu(bg->bg_bits))) {

				*ret_bh = gd_bh;
				*vict_bit = (vict_blkno - blkno) >>
							bits_per_unit;
				mlog(0, "find the victim group: #%llu, "
				     "total_bits: %u, vict_bit: %u\n",
				     blkno, le16_to_cpu(bg->bg_bits),
				     *vict_bit);
				goto out;
			}

		} while (le64_to_cpu(bg->bg_next_group));
	}

	ret = -EINVAL;
out:
	brelse(ac_bh);

	/*
	 * caller has to release the gd_bh properly.
	 */
	return ret;
}

/*
 * XXX: helper to validate and adjust moving goal.
 */
static int ocfs2_validate_and_adjust_move_goal(struct inode *inode,
					       struct ocfs2_move_extents *range)
{
	int ret, goal_bit = 0;

	struct buffer_head *gd_bh = NULL;
	struct ocfs2_group_desc *bg;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	int c_to_b = 1 << (osb->s_clustersize_bits -
					inode->i_sb->s_blocksize_bits);

	/*
	 * make goal become cluster aligned.
	 */
	range->me_goal = ocfs2_block_to_cluster_start(inode->i_sb,
						      range->me_goal);
	/*
	 * validate goal sits within global_bitmap, and return the victim
	 * group desc
	 */
	ret = ocfs2_find_victim_alloc_group(inode, range->me_goal,
					    GLOBAL_BITMAP_SYSTEM_INODE,
					    OCFS2_INVALID_SLOT,
					    &goal_bit, &gd_bh);
	if (ret)
		goto out;

	bg = (struct ocfs2_group_desc *)gd_bh->b_data;

	/*
	 * moving goal is not allowd to start with a group desc blok(#0 blk)
	 * let's compromise to the latter cluster.
	 */
	if (range->me_goal == le64_to_cpu(bg->bg_blkno))
		range->me_goal += c_to_b;

	/*
	 * movement is not gonna cross two groups.
	 */
	if ((le16_to_cpu(bg->bg_bits) - goal_bit) * osb->s_clustersize <
								range->me_len) {
		ret = -EINVAL;
		goto out;
	}
	/*
	 * more exact validations/adjustments will be performed later during
	 * moving operation for each extent range.
	 */
	mlog(0, "extents get ready to be moved to #%llu block\n",
	     range->me_goal);

out:
	brelse(gd_bh);

	return ret;
}

static void ocfs2_probe_alloc_group(struct inode *inode, struct buffer_head *bh,
				    int *goal_bit, u32 move_len, u32 max_hop,
				    u32 *phys_cpos)
{
	int i, used, last_free_bits = 0, base_bit = *goal_bit;
	struct ocfs2_group_desc *gd = (struct ocfs2_group_desc *)bh->b_data;
	u32 base_cpos = ocfs2_blocks_to_clusters(inode->i_sb,
						 le64_to_cpu(gd->bg_blkno));

	for (i = base_bit; i < le16_to_cpu(gd->bg_bits); i++) {

		used = ocfs2_test_bit(i, (unsigned long *)gd->bg_bitmap);
		if (used) {
			/*
			 * we even tried searching the free chunk by jumping
			 * a 'max_hop' distance, but still failed.
			 */
			if ((i - base_bit) > max_hop) {
				*phys_cpos = 0;
				break;
			}

			if (last_free_bits)
				last_free_bits = 0;

			continue;
		} else
			last_free_bits++;

		if (last_free_bits == move_len) {
			*goal_bit = i;
			*phys_cpos = base_cpos + i;
			break;
		}
	}

	mlog(0, "found phys_cpos: %u to fit the wanted moving.\n", *phys_cpos);
}

static int ocfs2_move_extent(struct ocfs2_move_extents_context *context,
			     u32 cpos, u32 phys_cpos, u32 *new_phys_cpos,
			     u32 len, int ext_flags)
{
	int ret, credits = 0, extra_blocks = 0, goal_bit = 0;
	handle_t *handle;
	struct inode *inode = context->inode;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct inode *tl_inode = osb->osb_tl_inode;
	struct inode *gb_inode = NULL;
	struct buffer_head *gb_bh = NULL;
	struct buffer_head *gd_bh = NULL;
	struct ocfs2_group_desc *gd;
	struct ocfs2_refcount_tree *ref_tree = NULL;
	u32 move_max_hop = ocfs2_blocks_to_clusters(inode->i_sb,
						    context->range->me_threshold);
	u64 phys_blkno, new_phys_blkno;

	phys_blkno = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);

	if ((ext_flags & OCFS2_EXT_REFCOUNTED) && len) {

		BUG_ON(!(OCFS2_I(inode)->ip_dyn_features &
			 OCFS2_HAS_REFCOUNT_FL));

		BUG_ON(!context->refcount_loc);

		ret = ocfs2_lock_refcount_tree(osb, context->refcount_loc, 1,
					       &ref_tree, NULL);
		if (ret) {
			mlog_errno(ret);
			return ret;
		}

		ret = ocfs2_prepare_refcount_change_for_del(inode,
							context->refcount_loc,
							phys_blkno,
							len,
							&credits,
							&extra_blocks);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	ret = ocfs2_lock_allocators_move_extents(inode, &context->et, len, 1,
						 &context->meta_ac,
						 NULL, extra_blocks, &credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * need to count 2 extra credits for global_bitmap inode and
	 * group descriptor.
	 */
	credits += OCFS2_INODE_UPDATE_CREDITS + 1;

	/*
	 * ocfs2_move_extent() didn't reserve any clusters in lock_allocators()
	 * logic, while we still need to lock the global_bitmap.
	 */
	gb_inode = ocfs2_get_system_file_inode(osb, GLOBAL_BITMAP_SYSTEM_INODE,
					       OCFS2_INVALID_SLOT);
	if (!gb_inode) {
		mlog(ML_ERROR, "unable to get global_bitmap inode\n");
		ret = -EIO;
		goto out;
	}

	mutex_lock(&gb_inode->i_mutex);

	ret = ocfs2_inode_lock(gb_inode, &gb_bh, 1);
	if (ret) {
		mlog_errno(ret);
		goto out_unlock_gb_mutex;
	}

	mutex_lock(&tl_inode->i_mutex);

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_unlock_tl_inode;
	}

	new_phys_blkno = ocfs2_clusters_to_blocks(inode->i_sb, *new_phys_cpos);
	ret = ocfs2_find_victim_alloc_group(inode, new_phys_blkno,
					    GLOBAL_BITMAP_SYSTEM_INODE,
					    OCFS2_INVALID_SLOT,
					    &goal_bit, &gd_bh);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/*
	 * probe the victim cluster group to find a proper
	 * region to fit wanted movement, it even will perfrom
	 * a best-effort attempt by compromising to a threshold
	 * around the goal.
	 */
	ocfs2_probe_alloc_group(inode, gd_bh, &goal_bit, len, move_max_hop,
				new_phys_cpos);
	if (!*new_phys_cpos) {
		ret = -ENOSPC;
		goto out_commit;
	}

	ret = __ocfs2_move_extent(handle, context, cpos, len, phys_cpos,
				  *new_phys_cpos, ext_flags);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	gd = (struct ocfs2_group_desc *)gd_bh->b_data;
	ret = ocfs2_alloc_dinode_update_counts(gb_inode, handle, gb_bh, len,
					       le16_to_cpu(gd->bg_chain));
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_block_group_set_bits(handle, gb_inode, gd, gd_bh,
					 goal_bit, len);
	if (ret) {
		ocfs2_rollback_alloc_dinode_counts(gb_inode, gb_bh, len,
					       le16_to_cpu(gd->bg_chain));
		mlog_errno(ret);
	}

	/*
	 * Here we should write the new page out first if we are
	 * in write-back mode.
	 */
	ret = ocfs2_cow_sync_writeback(inode->i_sb, context->inode, cpos, len);
	if (ret)
		mlog_errno(ret);

out_commit:
	ocfs2_commit_trans(osb, handle);
	brelse(gd_bh);

out_unlock_tl_inode:
	mutex_unlock(&tl_inode->i_mutex);

	ocfs2_inode_unlock(gb_inode, 1);
out_unlock_gb_mutex:
	mutex_unlock(&gb_inode->i_mutex);
	brelse(gb_bh);
	iput(gb_inode);

out:
	if (context->meta_ac) {
		ocfs2_free_alloc_context(context->meta_ac);
		context->meta_ac = NULL;
	}

	if (ref_tree)
		ocfs2_unlock_refcount_tree(osb, ref_tree, 1);

	return ret;
}

/*
 * Helper to calculate the defraging length in one run according to threshold.
 */
static void ocfs2_calc_extent_defrag_len(u32 *alloc_size, u32 *len_defraged,
					 u32 threshold, int *skip)
{
	if ((*alloc_size + *len_defraged) < threshold) {
		/*
		 * proceed defragmentation until we meet the thresh
		 */
		*len_defraged += *alloc_size;
	} else if (*len_defraged == 0) {
		/*
		 * XXX: skip a large extent.
		 */
		*skip = 1;
	} else {
		/*
		 * split this extent to coalesce with former pieces as
		 * to reach the threshold.
		 *
		 * we're done here with one cycle of defragmentation
		 * in a size of 'thresh', resetting 'len_defraged'
		 * forces a new defragmentation.
		 */
		*alloc_size = threshold - *len_defraged;
		*len_defraged = 0;
	}
}

static int __ocfs2_move_extents_range(struct buffer_head *di_bh,
				struct ocfs2_move_extents_context *context)
{
	int ret = 0, flags, do_defrag, skip = 0;
	u32 cpos, phys_cpos, move_start, len_to_move, alloc_size;
	u32 len_defraged = 0, defrag_thresh = 0, new_phys_cpos = 0;

	struct inode *inode = context->inode;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_move_extents *range = context->range;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if ((i_size_read(inode) == 0) || (range->me_len == 0))
		return 0;

	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		return 0;

	context->refcount_loc = le64_to_cpu(di->i_refcount_loc);

	ocfs2_init_dinode_extent_tree(&context->et, INODE_CACHE(inode), di_bh);
	ocfs2_init_dealloc_ctxt(&context->dealloc);

	/*
	 * TO-DO XXX:
	 *
	 * - xattr extents.
	 */

	do_defrag = context->auto_defrag;

	/*
	 * extents moving happens in unit of clusters, for the sake
	 * of simplicity, we may ignore two clusters where 'byte_start'
	 * and 'byte_start + len' were within.
	 */
	move_start = ocfs2_clusters_for_bytes(osb->sb, range->me_start);
	len_to_move = (range->me_start + range->me_len) >>
						osb->s_clustersize_bits;
	if (len_to_move >= move_start)
		len_to_move -= move_start;
	else
		len_to_move = 0;

	if (do_defrag) {
		defrag_thresh = range->me_threshold >> osb->s_clustersize_bits;
		if (defrag_thresh <= 1)
			goto done;
	} else
		new_phys_cpos = ocfs2_blocks_to_clusters(inode->i_sb,
							 range->me_goal);

	mlog(0, "Inode: %llu, start: %llu, len: %llu, cstart: %u, clen: %u, "
	     "thresh: %u\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno,
	     (unsigned long long)range->me_start,
	     (unsigned long long)range->me_len,
	     move_start, len_to_move, defrag_thresh);

	cpos = move_start;
	while (len_to_move) {
		ret = ocfs2_get_clusters(inode, cpos, &phys_cpos, &alloc_size,
					 &flags);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (alloc_size > len_to_move)
			alloc_size = len_to_move;

		/*
		 * XXX: how to deal with a hole:
		 *
		 * - skip the hole of course
		 * - force a new defragmentation
		 */
		if (!phys_cpos) {
			if (do_defrag)
				len_defraged = 0;

			goto next;
		}

		if (do_defrag) {
			ocfs2_calc_extent_defrag_len(&alloc_size, &len_defraged,
						     defrag_thresh, &skip);
			/*
			 * skip large extents
			 */
			if (skip) {
				skip = 0;
				goto next;
			}

			mlog(0, "#Defrag: cpos: %u, phys_cpos: %u, "
			     "alloc_size: %u, len_defraged: %u\n",
			     cpos, phys_cpos, alloc_size, len_defraged);

			ret = ocfs2_defrag_extent(context, cpos, phys_cpos,
						  &alloc_size, flags);
		} else {
			ret = ocfs2_move_extent(context, cpos, phys_cpos,
						&new_phys_cpos, alloc_size,
						flags);

			new_phys_cpos += alloc_size;
		}

		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		context->clusters_moved += alloc_size;
next:
		cpos += alloc_size;
		len_to_move -= alloc_size;
	}

done:
	range->me_flags |= OCFS2_MOVE_EXT_FL_COMPLETE;

out:
	range->me_moved_len = ocfs2_clusters_to_bytes(osb->sb,
						      context->clusters_moved);
	range->me_new_offset = ocfs2_clusters_to_bytes(osb->sb,
						       context->new_phys_cpos);

	ocfs2_schedule_truncate_log_flush(osb, 1);
	ocfs2_run_deallocs(osb, &context->dealloc);

	return ret;
}

static int ocfs2_move_extents(struct ocfs2_move_extents_context *context)
{
	int status;
	handle_t *handle;
	struct inode *inode = context->inode;
	struct ocfs2_dinode *di;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (!inode)
		return -ENOENT;

	if (ocfs2_is_hard_readonly(osb) || ocfs2_is_soft_readonly(osb))
		return -EROFS;

	mutex_lock(&inode->i_mutex);

	/*
	 * This prevents concurrent writes from other nodes
	 */
	status = ocfs2_rw_lock(inode, 1);
	if (status) {
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_inode_lock(inode, &di_bh, 1);
	if (status) {
		mlog_errno(status);
		goto out_rw_unlock;
	}

	/*
	 * rememer ip_xattr_sem also needs to be held if necessary
	 */
	down_write(&OCFS2_I(inode)->ip_alloc_sem);

	status = __ocfs2_move_extents_range(di_bh, context);

	up_write(&OCFS2_I(inode)->ip_alloc_sem);
	if (status) {
		mlog_errno(status);
		goto out_inode_unlock;
	}

	/*
	 * We update ctime for these changes
	 */
	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out_inode_unlock;
	}

	status = ocfs2_journal_access_di(handle, INODE_CACHE(inode), di_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status) {
		mlog_errno(status);
		goto out_commit;
	}

	di = (struct ocfs2_dinode *)di_bh->b_data;
	inode->i_ctime = CURRENT_TIME;
	di->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
	di->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);
	ocfs2_update_inode_fsync_trans(handle, inode, 0);

	ocfs2_journal_dirty(handle, di_bh);

out_commit:
	ocfs2_commit_trans(osb, handle);

out_inode_unlock:
	brelse(di_bh);
	ocfs2_inode_unlock(inode, 1);
out_rw_unlock:
	ocfs2_rw_unlock(inode, 1);
out:
	mutex_unlock(&inode->i_mutex);

	return status;
}

int ocfs2_ioctl_move_extents(struct file *filp, void __user *argp)
{
	int status;

	struct inode *inode = file_inode(filp);
	struct ocfs2_move_extents range;
	struct ocfs2_move_extents_context *context;

	if (!argp)
		return -EINVAL;

	status = mnt_want_write_file(filp);
	if (status)
		return status;

	if ((!S_ISREG(inode->i_mode)) || !(filp->f_mode & FMODE_WRITE)) {
		status = -EPERM;
		goto out_drop;
	}

	if (inode->i_flags & (S_IMMUTABLE|S_APPEND)) {
		status = -EPERM;
		goto out_drop;
	}

	context = kzalloc(sizeof(struct ocfs2_move_extents_context), GFP_NOFS);
	if (!context) {
		status = -ENOMEM;
		mlog_errno(status);
		goto out_drop;
	}

	context->inode = inode;
	context->file = filp;

	if (copy_from_user(&range, argp, sizeof(range))) {
		status = -EFAULT;
		goto out_free;
	}

	if (range.me_start > i_size_read(inode)) {
		status = -EINVAL;
		goto out_free;
	}

	if (range.me_start + range.me_len > i_size_read(inode))
			range.me_len = i_size_read(inode) - range.me_start;

	context->range = &range;

	if (range.me_flags & OCFS2_MOVE_EXT_FL_AUTO_DEFRAG) {
		context->auto_defrag = 1;
		/*
		 * ok, the default theshold for the defragmentation
		 * is 1M, since our maximum clustersize was 1M also.
		 * any thought?
		 */
		if (!range.me_threshold)
			range.me_threshold = 1024 * 1024;

		if (range.me_threshold > i_size_read(inode))
			range.me_threshold = i_size_read(inode);

		if (range.me_flags & OCFS2_MOVE_EXT_FL_PART_DEFRAG)
			context->partial = 1;
	} else {
		/*
		 * first best-effort attempt to validate and adjust the goal
		 * (physical address in block), while it can't guarantee later
		 * operation can succeed all the time since global_bitmap may
		 * change a bit over time.
		 */

		status = ocfs2_validate_and_adjust_move_goal(inode, &range);
		if (status)
			goto out_copy;
	}

	status = ocfs2_move_extents(context);
	if (status)
		mlog_errno(status);
out_copy:
	/*
	 * movement/defragmentation may end up being partially completed,
	 * that's the reason why we need to return userspace the finished
	 * length and new_offset even if failure happens somewhere.
	 */
	if (copy_to_user(argp, &range, sizeof(range)))
		status = -EFAULT;

out_free:
	kfree(context);
out_drop:
	mnt_drop_write_file(filp);

	return status;
}
