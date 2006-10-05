/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * alloc.c
 *
 * Extent allocs and frees
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
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

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>

#define MLOG_MASK_PREFIX ML_DISK_ALLOC
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "inode.h"
#include "journal.h"
#include "localalloc.h"
#include "suballoc.h"
#include "sysfile.h"
#include "file.h"
#include "super.h"
#include "uptodate.h"

#include "buffer_head_io.h"

static int ocfs2_extent_contig(struct inode *inode,
			       struct ocfs2_extent_rec *ext,
			       u64 blkno);

static int ocfs2_create_new_meta_bhs(struct ocfs2_super *osb,
				     struct ocfs2_journal_handle *handle,
				     struct inode *inode,
				     int wanted,
				     struct ocfs2_alloc_context *meta_ac,
				     struct buffer_head *bhs[]);

static int ocfs2_add_branch(struct ocfs2_super *osb,
			    struct ocfs2_journal_handle *handle,
			    struct inode *inode,
			    struct buffer_head *fe_bh,
			    struct buffer_head *eb_bh,
			    struct buffer_head *last_eb_bh,
			    struct ocfs2_alloc_context *meta_ac);

static int ocfs2_shift_tree_depth(struct ocfs2_super *osb,
				  struct ocfs2_journal_handle *handle,
				  struct inode *inode,
				  struct buffer_head *fe_bh,
				  struct ocfs2_alloc_context *meta_ac,
				  struct buffer_head **ret_new_eb_bh);

static int ocfs2_do_insert_extent(struct ocfs2_super *osb,
				  struct ocfs2_journal_handle *handle,
				  struct inode *inode,
				  struct buffer_head *fe_bh,
				  u64 blkno,
				  u32 new_clusters);

static int ocfs2_find_branch_target(struct ocfs2_super *osb,
				    struct inode *inode,
				    struct buffer_head *fe_bh,
				    struct buffer_head **target_bh);

static int ocfs2_find_new_last_ext_blk(struct ocfs2_super *osb,
				       struct inode *inode,
				       struct ocfs2_dinode *fe,
				       unsigned int new_i_clusters,
				       struct buffer_head *old_last_eb,
				       struct buffer_head **new_last_eb);

static void ocfs2_free_truncate_context(struct ocfs2_truncate_context *tc);

static int ocfs2_extent_contig(struct inode *inode,
			       struct ocfs2_extent_rec *ext,
			       u64 blkno)
{
	return blkno == (le64_to_cpu(ext->e_blkno) +
			 ocfs2_clusters_to_blocks(inode->i_sb,
						  le32_to_cpu(ext->e_clusters)));
}

/*
 * How many free extents have we got before we need more meta data?
 */
int ocfs2_num_free_extents(struct ocfs2_super *osb,
			   struct inode *inode,
			   struct ocfs2_dinode *fe)
{
	int retval;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_block *eb;
	struct buffer_head *eb_bh = NULL;

	mlog_entry_void();

	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, fe);
		retval = -EIO;
		goto bail;
	}

	if (fe->i_last_eb_blk) {
		retval = ocfs2_read_block(osb, le64_to_cpu(fe->i_last_eb_blk),
					  &eb_bh, OCFS2_BH_CACHED, inode);
		if (retval < 0) {
			mlog_errno(retval);
			goto bail;
		}
		eb = (struct ocfs2_extent_block *) eb_bh->b_data;
		el = &eb->h_list;
	} else
		el = &fe->id2.i_list;

	BUG_ON(el->l_tree_depth != 0);

	retval = le16_to_cpu(el->l_count) - le16_to_cpu(el->l_next_free_rec);
bail:
	if (eb_bh)
		brelse(eb_bh);

	mlog_exit(retval);
	return retval;
}

/* expects array to already be allocated
 *
 * sets h_signature, h_blkno, h_suballoc_bit, h_suballoc_slot, and
 * l_count for you
 */
static int ocfs2_create_new_meta_bhs(struct ocfs2_super *osb,
				     struct ocfs2_journal_handle *handle,
				     struct inode *inode,
				     int wanted,
				     struct ocfs2_alloc_context *meta_ac,
				     struct buffer_head *bhs[])
{
	int count, status, i;
	u16 suballoc_bit_start;
	u32 num_got;
	u64 first_blkno;
	struct ocfs2_extent_block *eb;

	mlog_entry_void();

	count = 0;
	while (count < wanted) {
		status = ocfs2_claim_metadata(osb,
					      handle,
					      meta_ac,
					      wanted - count,
					      &suballoc_bit_start,
					      &num_got,
					      &first_blkno);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		for(i = count;  i < (num_got + count); i++) {
			bhs[i] = sb_getblk(osb->sb, first_blkno);
			if (bhs[i] == NULL) {
				status = -EIO;
				mlog_errno(status);
				goto bail;
			}
			ocfs2_set_new_buffer_uptodate(inode, bhs[i]);

			status = ocfs2_journal_access(handle, inode, bhs[i],
						      OCFS2_JOURNAL_ACCESS_CREATE);
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}

			memset(bhs[i]->b_data, 0, osb->sb->s_blocksize);
			eb = (struct ocfs2_extent_block *) bhs[i]->b_data;
			/* Ok, setup the minimal stuff here. */
			strcpy(eb->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE);
			eb->h_blkno = cpu_to_le64(first_blkno);
			eb->h_fs_generation = cpu_to_le32(osb->fs_generation);

#ifndef OCFS2_USE_ALL_METADATA_SUBALLOCATORS
			/* we always use slot zero's suballocator */
			eb->h_suballoc_slot = 0;
#else
			eb->h_suballoc_slot = cpu_to_le16(osb->slot_num);
#endif
			eb->h_suballoc_bit = cpu_to_le16(suballoc_bit_start);
			eb->h_list.l_count =
				cpu_to_le16(ocfs2_extent_recs_per_eb(osb->sb));

			suballoc_bit_start++;
			first_blkno++;

			/* We'll also be dirtied by the caller, so
			 * this isn't absolutely necessary. */
			status = ocfs2_journal_dirty(handle, bhs[i]);
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}
		}

		count += num_got;
	}

	status = 0;
bail:
	if (status < 0) {
		for(i = 0; i < wanted; i++) {
			if (bhs[i])
				brelse(bhs[i]);
			bhs[i] = NULL;
		}
	}
	mlog_exit(status);
	return status;
}

/*
 * Add an entire tree branch to our inode. eb_bh is the extent block
 * to start at, if we don't want to start the branch at the dinode
 * structure.
 *
 * last_eb_bh is required as we have to update it's next_leaf pointer
 * for the new last extent block.
 *
 * the new branch will be 'empty' in the sense that every block will
 * contain a single record with e_clusters == 0.
 */
static int ocfs2_add_branch(struct ocfs2_super *osb,
			    struct ocfs2_journal_handle *handle,
			    struct inode *inode,
			    struct buffer_head *fe_bh,
			    struct buffer_head *eb_bh,
			    struct buffer_head *last_eb_bh,
			    struct ocfs2_alloc_context *meta_ac)
{
	int status, new_blocks, i;
	u64 next_blkno, new_last_eb_blk;
	struct buffer_head *bh;
	struct buffer_head **new_eb_bhs = NULL;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *eb_el;
	struct ocfs2_extent_list  *el;

	mlog_entry_void();

	BUG_ON(!last_eb_bh);

	fe = (struct ocfs2_dinode *) fe_bh->b_data;

	if (eb_bh) {
		eb = (struct ocfs2_extent_block *) eb_bh->b_data;
		el = &eb->h_list;
	} else
		el = &fe->id2.i_list;

	/* we never add a branch to a leaf. */
	BUG_ON(!el->l_tree_depth);

	new_blocks = le16_to_cpu(el->l_tree_depth);

	/* allocate the number of new eb blocks we need */
	new_eb_bhs = kcalloc(new_blocks, sizeof(struct buffer_head *),
			     GFP_KERNEL);
	if (!new_eb_bhs) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_create_new_meta_bhs(osb, handle, inode, new_blocks,
					   meta_ac, new_eb_bhs);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* Note: new_eb_bhs[new_blocks - 1] is the guy which will be
	 * linked with the rest of the tree.
	 * conversly, new_eb_bhs[0] is the new bottommost leaf.
	 *
	 * when we leave the loop, new_last_eb_blk will point to the
	 * newest leaf, and next_blkno will point to the topmost extent
	 * block. */
	next_blkno = new_last_eb_blk = 0;
	for(i = 0; i < new_blocks; i++) {
		bh = new_eb_bhs[i];
		eb = (struct ocfs2_extent_block *) bh->b_data;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			status = -EIO;
			goto bail;
		}
		eb_el = &eb->h_list;

		status = ocfs2_journal_access(handle, inode, bh,
					      OCFS2_JOURNAL_ACCESS_CREATE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		eb->h_next_leaf_blk = 0;
		eb_el->l_tree_depth = cpu_to_le16(i);
		eb_el->l_next_free_rec = cpu_to_le16(1);
		eb_el->l_recs[0].e_cpos = fe->i_clusters;
		eb_el->l_recs[0].e_blkno = cpu_to_le64(next_blkno);
		eb_el->l_recs[0].e_clusters = cpu_to_le32(0);
		if (!eb_el->l_tree_depth)
			new_last_eb_blk = le64_to_cpu(eb->h_blkno);

		status = ocfs2_journal_dirty(handle, bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		next_blkno = le64_to_cpu(eb->h_blkno);
	}

	/* This is a bit hairy. We want to update up to three blocks
	 * here without leaving any of them in an inconsistent state
	 * in case of error. We don't have to worry about
	 * journal_dirty erroring as it won't unless we've aborted the
	 * handle (in which case we would never be here) so reserving
	 * the write with journal_access is all we need to do. */
	status = ocfs2_journal_access(handle, inode, last_eb_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	status = ocfs2_journal_access(handle, inode, fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	if (eb_bh) {
		status = ocfs2_journal_access(handle, inode, eb_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	/* Link the new branch into the rest of the tree (el will
	 * either be on the fe, or the extent block passed in. */
	i = le16_to_cpu(el->l_next_free_rec);
	el->l_recs[i].e_blkno = cpu_to_le64(next_blkno);
	el->l_recs[i].e_cpos = fe->i_clusters;
	el->l_recs[i].e_clusters = 0;
	le16_add_cpu(&el->l_next_free_rec, 1);

	/* fe needs a new last extent block pointer, as does the
	 * next_leaf on the previously last-extent-block. */
	fe->i_last_eb_blk = cpu_to_le64(new_last_eb_blk);

	eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
	eb->h_next_leaf_blk = cpu_to_le64(new_last_eb_blk);

	status = ocfs2_journal_dirty(handle, last_eb_bh);
	if (status < 0)
		mlog_errno(status);
	status = ocfs2_journal_dirty(handle, fe_bh);
	if (status < 0)
		mlog_errno(status);
	if (eb_bh) {
		status = ocfs2_journal_dirty(handle, eb_bh);
		if (status < 0)
			mlog_errno(status);
	}

	status = 0;
bail:
	if (new_eb_bhs) {
		for (i = 0; i < new_blocks; i++)
			if (new_eb_bhs[i])
				brelse(new_eb_bhs[i]);
		kfree(new_eb_bhs);
	}

	mlog_exit(status);
	return status;
}

/*
 * adds another level to the allocation tree.
 * returns back the new extent block so you can add a branch to it
 * after this call.
 */
static int ocfs2_shift_tree_depth(struct ocfs2_super *osb,
				  struct ocfs2_journal_handle *handle,
				  struct inode *inode,
				  struct buffer_head *fe_bh,
				  struct ocfs2_alloc_context *meta_ac,
				  struct buffer_head **ret_new_eb_bh)
{
	int status, i;
	struct buffer_head *new_eb_bh = NULL;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *fe_el;
	struct ocfs2_extent_list  *eb_el;

	mlog_entry_void();

	status = ocfs2_create_new_meta_bhs(osb, handle, inode, 1, meta_ac,
					   &new_eb_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	eb = (struct ocfs2_extent_block *) new_eb_bh->b_data;
	if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
		OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
		status = -EIO;
		goto bail;
	}

	eb_el = &eb->h_list;
	fe = (struct ocfs2_dinode *) fe_bh->b_data;
	fe_el = &fe->id2.i_list;

	status = ocfs2_journal_access(handle, inode, new_eb_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* copy the fe data into the new extent block */
	eb_el->l_tree_depth = fe_el->l_tree_depth;
	eb_el->l_next_free_rec = fe_el->l_next_free_rec;
	for(i = 0; i < le16_to_cpu(fe_el->l_next_free_rec); i++) {
		eb_el->l_recs[i].e_cpos = fe_el->l_recs[i].e_cpos;
		eb_el->l_recs[i].e_clusters = fe_el->l_recs[i].e_clusters;
		eb_el->l_recs[i].e_blkno = fe_el->l_recs[i].e_blkno;
	}

	status = ocfs2_journal_dirty(handle, new_eb_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_journal_access(handle, inode, fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* update fe now */
	le16_add_cpu(&fe_el->l_tree_depth, 1);
	fe_el->l_recs[0].e_cpos = 0;
	fe_el->l_recs[0].e_blkno = eb->h_blkno;
	fe_el->l_recs[0].e_clusters = fe->i_clusters;
	for(i = 1; i < le16_to_cpu(fe_el->l_next_free_rec); i++) {
		fe_el->l_recs[i].e_cpos = 0;
		fe_el->l_recs[i].e_clusters = 0;
		fe_el->l_recs[i].e_blkno = 0;
	}
	fe_el->l_next_free_rec = cpu_to_le16(1);

	/* If this is our 1st tree depth shift, then last_eb_blk
	 * becomes the allocated extent block */
	if (fe_el->l_tree_depth == cpu_to_le16(1))
		fe->i_last_eb_blk = eb->h_blkno;

	status = ocfs2_journal_dirty(handle, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	*ret_new_eb_bh = new_eb_bh;
	new_eb_bh = NULL;
	status = 0;
bail:
	if (new_eb_bh)
		brelse(new_eb_bh);

	mlog_exit(status);
	return status;
}

/*
 * Expects the tree to already have room in the rightmost leaf for the
 * extent.  Updates all the extent blocks (and the dinode) on the way
 * down.
 */
static int ocfs2_do_insert_extent(struct ocfs2_super *osb,
				  struct ocfs2_journal_handle *handle,
				  struct inode *inode,
				  struct buffer_head *fe_bh,
				  u64 start_blk,
				  u32 new_clusters)
{
	int status, i, num_bhs = 0;
	u64 next_blkno;
	u16 next_free;
	struct buffer_head **eb_bhs = NULL;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *el;

	mlog_entry_void();

	status = ocfs2_journal_access(handle, inode, fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	fe = (struct ocfs2_dinode *) fe_bh->b_data;
	el = &fe->id2.i_list;
	if (el->l_tree_depth) {
		/* This is another operation where we want to be
		 * careful about our tree updates. An error here means
		 * none of the previous changes we made should roll
		 * forward. As a result, we have to record the buffers
		 * for this part of the tree in an array and reserve a
		 * journal write to them before making any changes. */
		num_bhs = le16_to_cpu(fe->id2.i_list.l_tree_depth);
		eb_bhs = kcalloc(num_bhs, sizeof(struct buffer_head *),
				 GFP_KERNEL);
		if (!eb_bhs) {
			status = -ENOMEM;
			mlog_errno(status);
			goto bail;
		}

		i = 0;
		while(el->l_tree_depth) {
			next_free = le16_to_cpu(el->l_next_free_rec);
			if (next_free == 0) {
				ocfs2_error(inode->i_sb,
					    "Dinode %llu has a bad extent list",
					    (unsigned long long)OCFS2_I(inode)->ip_blkno);
				status = -EIO;
				goto bail;
			}
			next_blkno = le64_to_cpu(el->l_recs[next_free - 1].e_blkno);

			BUG_ON(i >= num_bhs);
			status = ocfs2_read_block(osb, next_blkno, &eb_bhs[i],
						  OCFS2_BH_CACHED, inode);
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}
			eb = (struct ocfs2_extent_block *) eb_bhs[i]->b_data;
			if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
				OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb,
								 eb);
				status = -EIO;
				goto bail;
			}

			status = ocfs2_journal_access(handle, inode, eb_bhs[i],
						      OCFS2_JOURNAL_ACCESS_WRITE);
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}

			el = &eb->h_list;
			i++;
			/* When we leave this loop, eb_bhs[num_bhs - 1] will
			 * hold the bottom-most leaf extent block. */
		}
		BUG_ON(el->l_tree_depth);

		el = &fe->id2.i_list;
		/* If we have tree depth, then the fe update is
		 * trivial, and we want to switch el out for the
		 * bottom-most leaf in order to update it with the
		 * actual extent data below. */
		next_free = le16_to_cpu(el->l_next_free_rec);
		if (next_free == 0) {
			ocfs2_error(inode->i_sb,
				    "Dinode %llu has a bad extent list",
				    (unsigned long long)OCFS2_I(inode)->ip_blkno);
			status = -EIO;
			goto bail;
		}
		le32_add_cpu(&el->l_recs[next_free - 1].e_clusters,
			     new_clusters);
		/* (num_bhs - 1) to avoid the leaf */
		for(i = 0; i < (num_bhs - 1); i++) {
			eb = (struct ocfs2_extent_block *) eb_bhs[i]->b_data;
			el = &eb->h_list;

			/* finally, make our actual change to the
			 * intermediate extent blocks. */
			next_free = le16_to_cpu(el->l_next_free_rec);
			le32_add_cpu(&el->l_recs[next_free - 1].e_clusters,
				     new_clusters);

			status = ocfs2_journal_dirty(handle, eb_bhs[i]);
			if (status < 0)
				mlog_errno(status);
		}
		BUG_ON(i != (num_bhs - 1));
		/* note that the leaf block wasn't touched in
		 * the loop above */
		eb = (struct ocfs2_extent_block *) eb_bhs[num_bhs - 1]->b_data;
		el = &eb->h_list;
		BUG_ON(el->l_tree_depth);
	}

	/* yay, we can finally add the actual extent now! */
	i = le16_to_cpu(el->l_next_free_rec) - 1;
	if (le16_to_cpu(el->l_next_free_rec) &&
	    ocfs2_extent_contig(inode, &el->l_recs[i], start_blk)) {
		le32_add_cpu(&el->l_recs[i].e_clusters, new_clusters);
	} else if (le16_to_cpu(el->l_next_free_rec) &&
		   (le32_to_cpu(el->l_recs[i].e_clusters) == 0)) {
		/* having an empty extent at eof is legal. */
		if (el->l_recs[i].e_cpos != fe->i_clusters) {
			ocfs2_error(inode->i_sb,
				    "Dinode %llu trailing extent is bad: "
				    "cpos (%u) != number of clusters (%u)",
				    (unsigned long long)OCFS2_I(inode)->ip_blkno,
				    le32_to_cpu(el->l_recs[i].e_cpos),
				    le32_to_cpu(fe->i_clusters));
			status = -EIO;
			goto bail;
		}
		el->l_recs[i].e_blkno = cpu_to_le64(start_blk);
		el->l_recs[i].e_clusters = cpu_to_le32(new_clusters);
	} else {
		/* No contiguous record, or no empty record at eof, so
		 * we add a new one. */

		BUG_ON(le16_to_cpu(el->l_next_free_rec) >=
		       le16_to_cpu(el->l_count));
		i = le16_to_cpu(el->l_next_free_rec);

		el->l_recs[i].e_blkno = cpu_to_le64(start_blk);
		el->l_recs[i].e_clusters = cpu_to_le32(new_clusters);
		el->l_recs[i].e_cpos = fe->i_clusters;
		le16_add_cpu(&el->l_next_free_rec, 1);
	}

	/*
	 * extent_map errors are not fatal, so they are ignored outside
	 * of flushing the thing.
	 */
	status = ocfs2_extent_map_append(inode, &el->l_recs[i],
					 new_clusters);
	if (status) {
		mlog_errno(status);
		ocfs2_extent_map_drop(inode, le32_to_cpu(fe->i_clusters));
	}

	status = ocfs2_journal_dirty(handle, fe_bh);
	if (status < 0)
		mlog_errno(status);
	if (fe->id2.i_list.l_tree_depth) {
		status = ocfs2_journal_dirty(handle, eb_bhs[num_bhs - 1]);
		if (status < 0)
			mlog_errno(status);
	}

	status = 0;
bail:
	if (eb_bhs) {
		for (i = 0; i < num_bhs; i++)
			if (eb_bhs[i])
				brelse(eb_bhs[i]);
		kfree(eb_bhs);
	}

	mlog_exit(status);
	return status;
}

/*
 * Should only be called when there is no space left in any of the
 * leaf nodes. What we want to do is find the lowest tree depth
 * non-leaf extent block with room for new records. There are three
 * valid results of this search:
 *
 * 1) a lowest extent block is found, then we pass it back in
 *    *lowest_eb_bh and return '0'
 *
 * 2) the search fails to find anything, but the dinode has room. We
 *    pass NULL back in *lowest_eb_bh, but still return '0'
 *
 * 3) the search fails to find anything AND the dinode is full, in
 *    which case we return > 0
 *
 * return status < 0 indicates an error.
 */
static int ocfs2_find_branch_target(struct ocfs2_super *osb,
				    struct inode *inode,
				    struct buffer_head *fe_bh,
				    struct buffer_head **target_bh)
{
	int status = 0, i;
	u64 blkno;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *el;
	struct buffer_head *bh = NULL;
	struct buffer_head *lowest_bh = NULL;

	mlog_entry_void();

	*target_bh = NULL;

	fe = (struct ocfs2_dinode *) fe_bh->b_data;
	el = &fe->id2.i_list;

	while(le16_to_cpu(el->l_tree_depth) > 1) {
		if (le16_to_cpu(el->l_next_free_rec) == 0) {
			ocfs2_error(inode->i_sb, "Dinode %llu has empty "
				    "extent list (next_free_rec == 0)",
				    (unsigned long long)OCFS2_I(inode)->ip_blkno);
			status = -EIO;
			goto bail;
		}
		i = le16_to_cpu(el->l_next_free_rec) - 1;
		blkno = le64_to_cpu(el->l_recs[i].e_blkno);
		if (!blkno) {
			ocfs2_error(inode->i_sb, "Dinode %llu has extent "
				    "list where extent # %d has no physical "
				    "block start",
				    (unsigned long long)OCFS2_I(inode)->ip_blkno, i);
			status = -EIO;
			goto bail;
		}

		if (bh) {
			brelse(bh);
			bh = NULL;
		}

		status = ocfs2_read_block(osb, blkno, &bh, OCFS2_BH_CACHED,
					  inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		eb = (struct ocfs2_extent_block *) bh->b_data;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			status = -EIO;
			goto bail;
		}
		el = &eb->h_list;

		if (le16_to_cpu(el->l_next_free_rec) <
		    le16_to_cpu(el->l_count)) {
			if (lowest_bh)
				brelse(lowest_bh);
			lowest_bh = bh;
			get_bh(lowest_bh);
		}
	}

	/* If we didn't find one and the fe doesn't have any room,
	 * then return '1' */
	if (!lowest_bh
	    && (fe->id2.i_list.l_next_free_rec == fe->id2.i_list.l_count))
		status = 1;

	*target_bh = lowest_bh;
bail:
	if (bh)
		brelse(bh);

	mlog_exit(status);
	return status;
}

/* the caller needs to update fe->i_clusters */
int ocfs2_insert_extent(struct ocfs2_super *osb,
			struct ocfs2_journal_handle *handle,
			struct inode *inode,
			struct buffer_head *fe_bh,
			u64 start_blk,
			u32 new_clusters,
			struct ocfs2_alloc_context *meta_ac)
{
	int status, i, shift;
	struct buffer_head *last_eb_bh = NULL;
	struct buffer_head *bh = NULL;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *el;

	mlog_entry_void();

	mlog(0, "add %u clusters starting at block %llu to inode %llu\n",
	     new_clusters, (unsigned long long)start_blk,
	     (unsigned long long)OCFS2_I(inode)->ip_blkno);

	fe = (struct ocfs2_dinode *) fe_bh->b_data;
	el = &fe->id2.i_list;

	if (el->l_tree_depth) {
		/* jump to end of tree */
		status = ocfs2_read_block(osb, le64_to_cpu(fe->i_last_eb_blk),
					  &last_eb_bh, OCFS2_BH_CACHED, inode);
		if (status < 0) {
			mlog_exit(status);
			goto bail;
		}
		eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
		el = &eb->h_list;
	}

	/* Can we allocate without adding/shifting tree bits? */
	i = le16_to_cpu(el->l_next_free_rec) - 1;
	if (le16_to_cpu(el->l_next_free_rec) == 0
	    || (le16_to_cpu(el->l_next_free_rec) < le16_to_cpu(el->l_count))
	    || le32_to_cpu(el->l_recs[i].e_clusters) == 0
	    || ocfs2_extent_contig(inode, &el->l_recs[i], start_blk))
		goto out_add;

	mlog(0, "ocfs2_allocate_extent: couldn't do a simple add, traversing "
	     "tree now.\n");

	shift = ocfs2_find_branch_target(osb, inode, fe_bh, &bh);
	if (shift < 0) {
		status = shift;
		mlog_errno(status);
		goto bail;
	}

	/* We traveled all the way to the bottom of the allocation tree
	 * and didn't find room for any more extents - we need to add
	 * another tree level */
	if (shift) {
		/* if we hit a leaf, we'd better be empty :) */
		BUG_ON(le16_to_cpu(el->l_next_free_rec) !=
		       le16_to_cpu(el->l_count));
		BUG_ON(bh);
		mlog(0, "ocfs2_allocate_extent: need to shift tree depth "
		     "(current = %u)\n",
		     le16_to_cpu(fe->id2.i_list.l_tree_depth));

		/* ocfs2_shift_tree_depth will return us a buffer with
		 * the new extent block (so we can pass that to
		 * ocfs2_add_branch). */
		status = ocfs2_shift_tree_depth(osb, handle, inode, fe_bh,
						meta_ac, &bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		/* Special case: we have room now if we shifted from
		 * tree_depth 0 */
		if (fe->id2.i_list.l_tree_depth == cpu_to_le16(1))
			goto out_add;
	}

	/* call ocfs2_add_branch to add the final part of the tree with
	 * the new data. */
	mlog(0, "ocfs2_allocate_extent: add branch. bh = %p\n", bh);
	status = ocfs2_add_branch(osb, handle, inode, fe_bh, bh, last_eb_bh,
				  meta_ac);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

out_add:
	/* Finally, we can add clusters. */
	status = ocfs2_do_insert_extent(osb, handle, inode, fe_bh,
					start_blk, new_clusters);
	if (status < 0)
		mlog_errno(status);

bail:
	if (bh)
		brelse(bh);

	if (last_eb_bh)
		brelse(last_eb_bh);

	mlog_exit(status);
	return status;
}

static inline int ocfs2_truncate_log_needs_flush(struct ocfs2_super *osb)
{
	struct buffer_head *tl_bh = osb->osb_tl_bh;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;

	di = (struct ocfs2_dinode *) tl_bh->b_data;
	tl = &di->id2.i_dealloc;

	mlog_bug_on_msg(le16_to_cpu(tl->tl_used) > le16_to_cpu(tl->tl_count),
			"slot %d, invalid truncate log parameters: used = "
			"%u, count = %u\n", osb->slot_num,
			le16_to_cpu(tl->tl_used), le16_to_cpu(tl->tl_count));
	return le16_to_cpu(tl->tl_used) == le16_to_cpu(tl->tl_count);
}

static int ocfs2_truncate_log_can_coalesce(struct ocfs2_truncate_log *tl,
					   unsigned int new_start)
{
	unsigned int tail_index;
	unsigned int current_tail;

	/* No records, nothing to coalesce */
	if (!le16_to_cpu(tl->tl_used))
		return 0;

	tail_index = le16_to_cpu(tl->tl_used) - 1;
	current_tail = le32_to_cpu(tl->tl_recs[tail_index].t_start);
	current_tail += le32_to_cpu(tl->tl_recs[tail_index].t_clusters);

	return current_tail == new_start;
}

static int ocfs2_truncate_log_append(struct ocfs2_super *osb,
				     struct ocfs2_journal_handle *handle,
				     u64 start_blk,
				     unsigned int num_clusters)
{
	int status, index;
	unsigned int start_cluster, tl_count;
	struct inode *tl_inode = osb->osb_tl_inode;
	struct buffer_head *tl_bh = osb->osb_tl_bh;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;

	mlog_entry("start_blk = %llu, num_clusters = %u\n",
		   (unsigned long long)start_blk, num_clusters);

	BUG_ON(mutex_trylock(&tl_inode->i_mutex));

	start_cluster = ocfs2_blocks_to_clusters(osb->sb, start_blk);

	di = (struct ocfs2_dinode *) tl_bh->b_data;
	tl = &di->id2.i_dealloc;
	if (!OCFS2_IS_VALID_DINODE(di)) {
		OCFS2_RO_ON_INVALID_DINODE(osb->sb, di);
		status = -EIO;
		goto bail;
	}

	tl_count = le16_to_cpu(tl->tl_count);
	mlog_bug_on_msg(tl_count > ocfs2_truncate_recs_per_inode(osb->sb) ||
			tl_count == 0,
			"Truncate record count on #%llu invalid "
			"wanted %u, actual %u\n",
			(unsigned long long)OCFS2_I(tl_inode)->ip_blkno,
			ocfs2_truncate_recs_per_inode(osb->sb),
			le16_to_cpu(tl->tl_count));

	/* Caller should have known to flush before calling us. */
	index = le16_to_cpu(tl->tl_used);
	if (index >= tl_count) {
		status = -ENOSPC;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_journal_access(handle, tl_inode, tl_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	mlog(0, "Log truncate of %u clusters starting at cluster %u to "
	     "%llu (index = %d)\n", num_clusters, start_cluster,
	     (unsigned long long)OCFS2_I(tl_inode)->ip_blkno, index);

	if (ocfs2_truncate_log_can_coalesce(tl, start_cluster)) {
		/*
		 * Move index back to the record we are coalescing with.
		 * ocfs2_truncate_log_can_coalesce() guarantees nonzero
		 */
		index--;

		num_clusters += le32_to_cpu(tl->tl_recs[index].t_clusters);
		mlog(0, "Coalesce with index %u (start = %u, clusters = %u)\n",
		     index, le32_to_cpu(tl->tl_recs[index].t_start),
		     num_clusters);
	} else {
		tl->tl_recs[index].t_start = cpu_to_le32(start_cluster);
		tl->tl_used = cpu_to_le16(index + 1);
	}
	tl->tl_recs[index].t_clusters = cpu_to_le32(num_clusters);

	status = ocfs2_journal_dirty(handle, tl_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_replay_truncate_records(struct ocfs2_super *osb,
					 struct ocfs2_journal_handle *handle,
					 struct inode *data_alloc_inode,
					 struct buffer_head *data_alloc_bh)
{
	int status = 0;
	int i;
	unsigned int num_clusters;
	u64 start_blk;
	struct ocfs2_truncate_rec rec;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;
	struct inode *tl_inode = osb->osb_tl_inode;
	struct buffer_head *tl_bh = osb->osb_tl_bh;

	mlog_entry_void();

	di = (struct ocfs2_dinode *) tl_bh->b_data;
	tl = &di->id2.i_dealloc;
	i = le16_to_cpu(tl->tl_used) - 1;
	while (i >= 0) {
		/* Caller has given us at least enough credits to
		 * update the truncate log dinode */
		status = ocfs2_journal_access(handle, tl_inode, tl_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		tl->tl_used = cpu_to_le16(i);

		status = ocfs2_journal_dirty(handle, tl_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		/* TODO: Perhaps we can calculate the bulk of the
		 * credits up front rather than extending like
		 * this. */
		status = ocfs2_extend_trans(handle->k_handle,
					    OCFS2_TRUNCATE_LOG_FLUSH_ONE_REC);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		rec = tl->tl_recs[i];
		start_blk = ocfs2_clusters_to_blocks(data_alloc_inode->i_sb,
						    le32_to_cpu(rec.t_start));
		num_clusters = le32_to_cpu(rec.t_clusters);

		/* if start_blk is not set, we ignore the record as
		 * invalid. */
		if (start_blk) {
			mlog(0, "free record %d, start = %u, clusters = %u\n",
			     i, le32_to_cpu(rec.t_start), num_clusters);

			status = ocfs2_free_clusters(handle, data_alloc_inode,
						     data_alloc_bh, start_blk,
						     num_clusters);
			if (status < 0) {
				mlog_errno(status);
				goto bail;
			}
		}
		i--;
	}

bail:
	mlog_exit(status);
	return status;
}

/* Expects you to already be holding tl_inode->i_mutex */
static int __ocfs2_flush_truncate_log(struct ocfs2_super *osb)
{
	int status;
	unsigned int num_to_flush;
	struct ocfs2_journal_handle *handle = NULL;
	struct inode *tl_inode = osb->osb_tl_inode;
	struct inode *data_alloc_inode = NULL;
	struct buffer_head *tl_bh = osb->osb_tl_bh;
	struct buffer_head *data_alloc_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;

	mlog_entry_void();

	BUG_ON(mutex_trylock(&tl_inode->i_mutex));

	di = (struct ocfs2_dinode *) tl_bh->b_data;
	tl = &di->id2.i_dealloc;
	if (!OCFS2_IS_VALID_DINODE(di)) {
		OCFS2_RO_ON_INVALID_DINODE(osb->sb, di);
		status = -EIO;
		goto bail;
	}

	num_to_flush = le16_to_cpu(tl->tl_used);
	mlog(0, "Flush %u records from truncate log #%llu\n",
	     num_to_flush, (unsigned long long)OCFS2_I(tl_inode)->ip_blkno);
	if (!num_to_flush) {
		status = 0;
		goto bail;
	}

	handle = ocfs2_alloc_handle(osb);
	if (!handle) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	data_alloc_inode = ocfs2_get_system_file_inode(osb,
						       GLOBAL_BITMAP_SYSTEM_INODE,
						       OCFS2_INVALID_SLOT);
	if (!data_alloc_inode) {
		status = -EINVAL;
		mlog(ML_ERROR, "Could not get bitmap inode!\n");
		goto bail;
	}

	ocfs2_handle_add_inode(handle, data_alloc_inode);
	status = ocfs2_meta_lock(data_alloc_inode, handle, &data_alloc_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	handle = ocfs2_start_trans(osb, handle, OCFS2_TRUNCATE_LOG_UPDATE);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_replay_truncate_records(osb, handle, data_alloc_inode,
					       data_alloc_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

bail:
	if (handle)
		ocfs2_commit_trans(handle);

	if (data_alloc_inode)
		iput(data_alloc_inode);

	if (data_alloc_bh)
		brelse(data_alloc_bh);

	mlog_exit(status);
	return status;
}

int ocfs2_flush_truncate_log(struct ocfs2_super *osb)
{
	int status;
	struct inode *tl_inode = osb->osb_tl_inode;

	mutex_lock(&tl_inode->i_mutex);
	status = __ocfs2_flush_truncate_log(osb);
	mutex_unlock(&tl_inode->i_mutex);

	return status;
}

static void ocfs2_truncate_log_worker(void *data)
{
	int status;
	struct ocfs2_super *osb = data;

	mlog_entry_void();

	status = ocfs2_flush_truncate_log(osb);
	if (status < 0)
		mlog_errno(status);

	mlog_exit(status);
}

#define OCFS2_TRUNCATE_LOG_FLUSH_INTERVAL (2 * HZ)
void ocfs2_schedule_truncate_log_flush(struct ocfs2_super *osb,
				       int cancel)
{
	if (osb->osb_tl_inode) {
		/* We want to push off log flushes while truncates are
		 * still running. */
		if (cancel)
			cancel_delayed_work(&osb->osb_truncate_log_wq);

		queue_delayed_work(ocfs2_wq, &osb->osb_truncate_log_wq,
				   OCFS2_TRUNCATE_LOG_FLUSH_INTERVAL);
	}
}

static int ocfs2_get_truncate_log_info(struct ocfs2_super *osb,
				       int slot_num,
				       struct inode **tl_inode,
				       struct buffer_head **tl_bh)
{
	int status;
	struct inode *inode = NULL;
	struct buffer_head *bh = NULL;

	inode = ocfs2_get_system_file_inode(osb,
					   TRUNCATE_LOG_SYSTEM_INODE,
					   slot_num);
	if (!inode) {
		status = -EINVAL;
		mlog(ML_ERROR, "Could not get load truncate log inode!\n");
		goto bail;
	}

	status = ocfs2_read_block(osb, OCFS2_I(inode)->ip_blkno, &bh,
				  OCFS2_BH_CACHED, inode);
	if (status < 0) {
		iput(inode);
		mlog_errno(status);
		goto bail;
	}

	*tl_inode = inode;
	*tl_bh    = bh;
bail:
	mlog_exit(status);
	return status;
}

/* called during the 1st stage of node recovery. we stamp a clean
 * truncate log and pass back a copy for processing later. if the
 * truncate log does not require processing, a *tl_copy is set to
 * NULL. */
int ocfs2_begin_truncate_log_recovery(struct ocfs2_super *osb,
				      int slot_num,
				      struct ocfs2_dinode **tl_copy)
{
	int status;
	struct inode *tl_inode = NULL;
	struct buffer_head *tl_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;

	*tl_copy = NULL;

	mlog(0, "recover truncate log from slot %d\n", slot_num);

	status = ocfs2_get_truncate_log_info(osb, slot_num, &tl_inode, &tl_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	di = (struct ocfs2_dinode *) tl_bh->b_data;
	tl = &di->id2.i_dealloc;
	if (!OCFS2_IS_VALID_DINODE(di)) {
		OCFS2_RO_ON_INVALID_DINODE(tl_inode->i_sb, di);
		status = -EIO;
		goto bail;
	}

	if (le16_to_cpu(tl->tl_used)) {
		mlog(0, "We'll have %u logs to recover\n",
		     le16_to_cpu(tl->tl_used));

		*tl_copy = kmalloc(tl_bh->b_size, GFP_KERNEL);
		if (!(*tl_copy)) {
			status = -ENOMEM;
			mlog_errno(status);
			goto bail;
		}

		/* Assuming the write-out below goes well, this copy
		 * will be passed back to recovery for processing. */
		memcpy(*tl_copy, tl_bh->b_data, tl_bh->b_size);

		/* All we need to do to clear the truncate log is set
		 * tl_used. */
		tl->tl_used = 0;

		status = ocfs2_write_block(osb, tl_bh, tl_inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

bail:
	if (tl_inode)
		iput(tl_inode);
	if (tl_bh)
		brelse(tl_bh);

	if (status < 0 && (*tl_copy)) {
		kfree(*tl_copy);
		*tl_copy = NULL;
	}

	mlog_exit(status);
	return status;
}

int ocfs2_complete_truncate_log_recovery(struct ocfs2_super *osb,
					 struct ocfs2_dinode *tl_copy)
{
	int status = 0;
	int i;
	unsigned int clusters, num_recs, start_cluster;
	u64 start_blk;
	struct ocfs2_journal_handle *handle;
	struct inode *tl_inode = osb->osb_tl_inode;
	struct ocfs2_truncate_log *tl;

	mlog_entry_void();

	if (OCFS2_I(tl_inode)->ip_blkno == le64_to_cpu(tl_copy->i_blkno)) {
		mlog(ML_ERROR, "Asked to recover my own truncate log!\n");
		return -EINVAL;
	}

	tl = &tl_copy->id2.i_dealloc;
	num_recs = le16_to_cpu(tl->tl_used);
	mlog(0, "cleanup %u records from %llu\n", num_recs,
	     (unsigned long long)tl_copy->i_blkno);

	mutex_lock(&tl_inode->i_mutex);
	for(i = 0; i < num_recs; i++) {
		if (ocfs2_truncate_log_needs_flush(osb)) {
			status = __ocfs2_flush_truncate_log(osb);
			if (status < 0) {
				mlog_errno(status);
				goto bail_up;
			}
		}

		handle = ocfs2_start_trans(osb, NULL,
					   OCFS2_TRUNCATE_LOG_UPDATE);
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			mlog_errno(status);
			goto bail_up;
		}

		clusters = le32_to_cpu(tl->tl_recs[i].t_clusters);
		start_cluster = le32_to_cpu(tl->tl_recs[i].t_start);
		start_blk = ocfs2_clusters_to_blocks(osb->sb, start_cluster);

		status = ocfs2_truncate_log_append(osb, handle,
						   start_blk, clusters);
		ocfs2_commit_trans(handle);
		if (status < 0) {
			mlog_errno(status);
			goto bail_up;
		}
	}

bail_up:
	mutex_unlock(&tl_inode->i_mutex);

	mlog_exit(status);
	return status;
}

void ocfs2_truncate_log_shutdown(struct ocfs2_super *osb)
{
	int status;
	struct inode *tl_inode = osb->osb_tl_inode;

	mlog_entry_void();

	if (tl_inode) {
		cancel_delayed_work(&osb->osb_truncate_log_wq);
		flush_workqueue(ocfs2_wq);

		status = ocfs2_flush_truncate_log(osb);
		if (status < 0)
			mlog_errno(status);

		brelse(osb->osb_tl_bh);
		iput(osb->osb_tl_inode);
	}

	mlog_exit_void();
}

int ocfs2_truncate_log_init(struct ocfs2_super *osb)
{
	int status;
	struct inode *tl_inode = NULL;
	struct buffer_head *tl_bh = NULL;

	mlog_entry_void();

	status = ocfs2_get_truncate_log_info(osb,
					     osb->slot_num,
					     &tl_inode,
					     &tl_bh);
	if (status < 0)
		mlog_errno(status);

	/* ocfs2_truncate_log_shutdown keys on the existence of
	 * osb->osb_tl_inode so we don't set any of the osb variables
	 * until we're sure all is well. */
	INIT_WORK(&osb->osb_truncate_log_wq, ocfs2_truncate_log_worker, osb);
	osb->osb_tl_bh    = tl_bh;
	osb->osb_tl_inode = tl_inode;

	mlog_exit(status);
	return status;
}

/* This function will figure out whether the currently last extent
 * block will be deleted, and if it will, what the new last extent
 * block will be so we can update his h_next_leaf_blk field, as well
 * as the dinodes i_last_eb_blk */
static int ocfs2_find_new_last_ext_blk(struct ocfs2_super *osb,
				       struct inode *inode,
				       struct ocfs2_dinode *fe,
				       u32 new_i_clusters,
				       struct buffer_head *old_last_eb,
				       struct buffer_head **new_last_eb)
{
	int i, status = 0;
	u64 block = 0;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct buffer_head *bh = NULL;

	*new_last_eb = NULL;

	if (!OCFS2_IS_VALID_DINODE(fe)) {
		OCFS2_RO_ON_INVALID_DINODE(inode->i_sb, fe);
		status = -EIO;
		goto bail;
	}

	/* we have no tree, so of course, no last_eb. */
	if (!fe->id2.i_list.l_tree_depth)
		goto bail;

	/* trunc to zero special case - this makes tree_depth = 0
	 * regardless of what it is.  */
	if (!new_i_clusters)
		goto bail;

	eb = (struct ocfs2_extent_block *) old_last_eb->b_data;
	el = &(eb->h_list);
	BUG_ON(!el->l_next_free_rec);

	/* Make sure that this guy will actually be empty after we
	 * clear away the data. */
	if (le32_to_cpu(el->l_recs[0].e_cpos) < new_i_clusters)
		goto bail;

	/* Ok, at this point, we know that last_eb will definitely
	 * change, so lets traverse the tree and find the second to
	 * last extent block. */
	el = &(fe->id2.i_list);
	/* go down the tree, */
	do {
		for(i = (le16_to_cpu(el->l_next_free_rec) - 1); i >= 0; i--) {
			if (le32_to_cpu(el->l_recs[i].e_cpos) <
			    new_i_clusters) {
				block = le64_to_cpu(el->l_recs[i].e_blkno);
				break;
			}
		}
		BUG_ON(i < 0);

		if (bh) {
			brelse(bh);
			bh = NULL;
		}

		status = ocfs2_read_block(osb, block, &bh, OCFS2_BH_CACHED,
					 inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		eb = (struct ocfs2_extent_block *) bh->b_data;
		el = &eb->h_list;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			status = -EIO;
			goto bail;
		}
	} while (el->l_tree_depth);

	*new_last_eb = bh;
	get_bh(*new_last_eb);
	mlog(0, "returning block %llu\n",
	     (unsigned long long)le64_to_cpu(eb->h_blkno));
bail:
	if (bh)
		brelse(bh);

	return status;
}

static int ocfs2_do_truncate(struct ocfs2_super *osb,
			     unsigned int clusters_to_del,
			     struct inode *inode,
			     struct buffer_head *fe_bh,
			     struct buffer_head *old_last_eb_bh,
			     struct ocfs2_journal_handle *handle,
			     struct ocfs2_truncate_context *tc)
{
	int status, i, depth;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_block *last_eb = NULL;
	struct ocfs2_extent_list *el;
	struct buffer_head *eb_bh = NULL;
	struct buffer_head *last_eb_bh = NULL;
	u64 next_eb = 0;
	u64 delete_blk = 0;

	fe = (struct ocfs2_dinode *) fe_bh->b_data;

	status = ocfs2_find_new_last_ext_blk(osb,
					     inode,
					     fe,
					     le32_to_cpu(fe->i_clusters) -
					     		clusters_to_del,
					     old_last_eb_bh,
					     &last_eb_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	if (last_eb_bh)
		last_eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;

	status = ocfs2_journal_access(handle, inode, fe_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	el = &(fe->id2.i_list);

	spin_lock(&OCFS2_I(inode)->ip_lock);
	OCFS2_I(inode)->ip_clusters = le32_to_cpu(fe->i_clusters) -
				      clusters_to_del;
	spin_unlock(&OCFS2_I(inode)->ip_lock);
	le32_add_cpu(&fe->i_clusters, -clusters_to_del);
	fe->i_mtime = cpu_to_le64(CURRENT_TIME.tv_sec);
	fe->i_mtime_nsec = cpu_to_le32(CURRENT_TIME.tv_nsec);

	i = le16_to_cpu(el->l_next_free_rec) - 1;

	BUG_ON(le32_to_cpu(el->l_recs[i].e_clusters) < clusters_to_del);
	le32_add_cpu(&el->l_recs[i].e_clusters, -clusters_to_del);
	/* tree depth zero, we can just delete the clusters, otherwise
	 * we need to record the offset of the next level extent block
	 * as we may overwrite it. */
	if (!el->l_tree_depth)
		delete_blk = le64_to_cpu(el->l_recs[i].e_blkno)
			+ ocfs2_clusters_to_blocks(osb->sb,
					le32_to_cpu(el->l_recs[i].e_clusters));
	else
		next_eb = le64_to_cpu(el->l_recs[i].e_blkno);

	if (!el->l_recs[i].e_clusters) {
		/* if we deleted the whole extent record, then clear
		 * out the other fields and update the extent
		 * list. For depth > 0 trees, we've already recorded
		 * the extent block in 'next_eb' */
		el->l_recs[i].e_cpos = 0;
		el->l_recs[i].e_blkno = 0;
		BUG_ON(!el->l_next_free_rec);
		le16_add_cpu(&el->l_next_free_rec, -1);
	}

	depth = le16_to_cpu(el->l_tree_depth);
	if (!fe->i_clusters) {
		/* trunc to zero is a special case. */
		el->l_tree_depth = 0;
		fe->i_last_eb_blk = 0;
	} else if (last_eb)
		fe->i_last_eb_blk = last_eb->h_blkno;

	status = ocfs2_journal_dirty(handle, fe_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	if (last_eb) {
		/* If there will be a new last extent block, then by
		 * definition, there cannot be any leaves to the right of
		 * him. */
		status = ocfs2_journal_access(handle, inode, last_eb_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		last_eb->h_next_leaf_blk = 0;
		status = ocfs2_journal_dirty(handle, last_eb_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	/* if our tree depth > 0, update all the tree blocks below us. */
	while (depth) {
		mlog(0, "traveling tree (depth = %d, next_eb = %llu)\n",
		     depth,  (unsigned long long)next_eb);
		status = ocfs2_read_block(osb, next_eb, &eb_bh,
					  OCFS2_BH_CACHED, inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		eb = (struct ocfs2_extent_block *)eb_bh->b_data;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			status = -EIO;
			goto bail;
		}
		el = &(eb->h_list);

		status = ocfs2_journal_access(handle, inode, eb_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		BUG_ON(le16_to_cpu(el->l_next_free_rec) == 0);
		BUG_ON(depth != (le16_to_cpu(el->l_tree_depth) + 1));

		i = le16_to_cpu(el->l_next_free_rec) - 1;

		mlog(0, "extent block %llu, before: record %d: "
		     "(%u, %u, %llu), next = %u\n",
		     (unsigned long long)le64_to_cpu(eb->h_blkno), i,
		     le32_to_cpu(el->l_recs[i].e_cpos),
		     le32_to_cpu(el->l_recs[i].e_clusters),
		     (unsigned long long)le64_to_cpu(el->l_recs[i].e_blkno),
		     le16_to_cpu(el->l_next_free_rec));

		BUG_ON(le32_to_cpu(el->l_recs[i].e_clusters) < clusters_to_del);
		le32_add_cpu(&el->l_recs[i].e_clusters, -clusters_to_del);

		next_eb = le64_to_cpu(el->l_recs[i].e_blkno);
		/* bottom-most block requires us to delete data.*/
		if (!el->l_tree_depth)
			delete_blk = le64_to_cpu(el->l_recs[i].e_blkno)
				+ ocfs2_clusters_to_blocks(osb->sb,
					le32_to_cpu(el->l_recs[i].e_clusters));
		if (!el->l_recs[i].e_clusters) {
			el->l_recs[i].e_cpos = 0;
			el->l_recs[i].e_blkno = 0;
			BUG_ON(!el->l_next_free_rec);
			le16_add_cpu(&el->l_next_free_rec, -1);
		}
		mlog(0, "extent block %llu, after: record %d: "
		     "(%u, %u, %llu), next = %u\n",
		     (unsigned long long)le64_to_cpu(eb->h_blkno), i,
		     le32_to_cpu(el->l_recs[i].e_cpos),
		     le32_to_cpu(el->l_recs[i].e_clusters),
		     (unsigned long long)le64_to_cpu(el->l_recs[i].e_blkno),
		     le16_to_cpu(el->l_next_free_rec));

		status = ocfs2_journal_dirty(handle, eb_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		if (!el->l_next_free_rec) {
			mlog(0, "deleting this extent block.\n");

			ocfs2_remove_from_cache(inode, eb_bh);

			BUG_ON(el->l_recs[0].e_clusters);
			BUG_ON(el->l_recs[0].e_cpos);
			BUG_ON(el->l_recs[0].e_blkno);
			if (eb->h_suballoc_slot == 0) {
				/*
				 * This code only understands how to
				 * lock the suballocator in slot 0,
				 * which is fine because allocation is
				 * only ever done out of that
				 * suballocator too. A future version
				 * might change that however, so avoid
				 * a free if we don't know how to
				 * handle it. This way an fs incompat
				 * bit will not be necessary.
				 */
				status = ocfs2_free_extent_block(handle,
								 tc->tc_ext_alloc_inode,
								 tc->tc_ext_alloc_bh,
								 eb);
				if (status < 0) {
					mlog_errno(status);
					goto bail;
				}
			}
		}
		brelse(eb_bh);
		eb_bh = NULL;
		depth--;
	}

	BUG_ON(!delete_blk);
	status = ocfs2_truncate_log_append(osb, handle, delete_blk,
					   clusters_to_del);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	status = 0;
bail:
	if (!status)
		ocfs2_extent_map_trunc(inode, le32_to_cpu(fe->i_clusters));
	else
		ocfs2_extent_map_drop(inode, 0);
	mlog_exit(status);
	return status;
}

/*
 * It is expected, that by the time you call this function,
 * inode->i_size and fe->i_size have been adjusted.
 *
 * WARNING: This will kfree the truncate context
 */
int ocfs2_commit_truncate(struct ocfs2_super *osb,
			  struct inode *inode,
			  struct buffer_head *fe_bh,
			  struct ocfs2_truncate_context *tc)
{
	int status, i, credits, tl_sem = 0;
	u32 clusters_to_del, target_i_clusters;
	u64 last_eb = 0;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct buffer_head *last_eb_bh;
	struct ocfs2_journal_handle *handle = NULL;
	struct inode *tl_inode = osb->osb_tl_inode;

	mlog_entry_void();

	down_write(&OCFS2_I(inode)->ip_alloc_sem);

	target_i_clusters = ocfs2_clusters_for_bytes(osb->sb,
						     i_size_read(inode));

	last_eb_bh = tc->tc_last_eb_bh;
	tc->tc_last_eb_bh = NULL;

	fe = (struct ocfs2_dinode *) fe_bh->b_data;

	if (fe->id2.i_list.l_tree_depth) {
		eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
		el = &eb->h_list;
	} else
		el = &fe->id2.i_list;
	last_eb = le64_to_cpu(fe->i_last_eb_blk);
start:
	mlog(0, "ocfs2_commit_truncate: fe->i_clusters = %u, "
	     "last_eb = %llu, fe->i_last_eb_blk = %llu, "
	     "fe->id2.i_list.l_tree_depth = %u last_eb_bh = %p\n",
	     le32_to_cpu(fe->i_clusters), (unsigned long long)last_eb,
	     (unsigned long long)le64_to_cpu(fe->i_last_eb_blk),
	     le16_to_cpu(fe->id2.i_list.l_tree_depth), last_eb_bh);

	if (last_eb != le64_to_cpu(fe->i_last_eb_blk)) {
		mlog(0, "last_eb changed!\n");
		BUG_ON(!fe->id2.i_list.l_tree_depth);
		last_eb = le64_to_cpu(fe->i_last_eb_blk);
		/* i_last_eb_blk may have changed, read it if
		 * necessary. We don't have to worry about the
		 * truncate to zero case here (where there becomes no
		 * last_eb) because we never loop back after our work
		 * is done. */
		if (last_eb_bh) {
			brelse(last_eb_bh);
			last_eb_bh = NULL;
		}

		status = ocfs2_read_block(osb, last_eb,
					  &last_eb_bh, OCFS2_BH_CACHED,
					  inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			status = -EIO;
			goto bail;
		}
		el = &(eb->h_list);
	}

	/* by now, el will point to the extent list on the bottom most
	 * portion of this tree. */
	i = le16_to_cpu(el->l_next_free_rec) - 1;
	if (le32_to_cpu(el->l_recs[i].e_cpos) >= target_i_clusters)
		clusters_to_del = le32_to_cpu(el->l_recs[i].e_clusters);
	else
		clusters_to_del = (le32_to_cpu(el->l_recs[i].e_clusters) +
				   le32_to_cpu(el->l_recs[i].e_cpos)) -
				  target_i_clusters;

	mlog(0, "clusters_to_del = %u in this pass\n", clusters_to_del);

	mutex_lock(&tl_inode->i_mutex);
	tl_sem = 1;
	/* ocfs2_truncate_log_needs_flush guarantees us at least one
	 * record is free for use. If there isn't any, we flush to get
	 * an empty truncate log.  */
	if (ocfs2_truncate_log_needs_flush(osb)) {
		status = __ocfs2_flush_truncate_log(osb);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	credits = ocfs2_calc_tree_trunc_credits(osb->sb, clusters_to_del,
						fe, el);
	handle = ocfs2_start_trans(osb, NULL, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	inode->i_ctime = inode->i_mtime = CURRENT_TIME;
	status = ocfs2_mark_inode_dirty(handle, inode, fe_bh);
	if (status < 0)
		mlog_errno(status);

	status = ocfs2_do_truncate(osb, clusters_to_del, inode, fe_bh,
				   last_eb_bh, handle, tc);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	mutex_unlock(&tl_inode->i_mutex);
	tl_sem = 0;

	ocfs2_commit_trans(handle);
	handle = NULL;

	BUG_ON(le32_to_cpu(fe->i_clusters) < target_i_clusters);
	if (le32_to_cpu(fe->i_clusters) > target_i_clusters)
		goto start;
bail:
	up_write(&OCFS2_I(inode)->ip_alloc_sem);

	ocfs2_schedule_truncate_log_flush(osb, 1);

	if (tl_sem)
		mutex_unlock(&tl_inode->i_mutex);

	if (handle)
		ocfs2_commit_trans(handle);

	if (last_eb_bh)
		brelse(last_eb_bh);

	/* This will drop the ext_alloc cluster lock for us */
	ocfs2_free_truncate_context(tc);

	mlog_exit(status);
	return status;
}


/*
 * Expects the inode to already be locked. This will figure out which
 * inodes need to be locked and will put them on the returned truncate
 * context.
 */
int ocfs2_prepare_truncate(struct ocfs2_super *osb,
			   struct inode *inode,
			   struct buffer_head *fe_bh,
			   struct ocfs2_truncate_context **tc)
{
	int status, metadata_delete;
	unsigned int new_i_clusters;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct buffer_head *last_eb_bh = NULL;
	struct inode *ext_alloc_inode = NULL;
	struct buffer_head *ext_alloc_bh = NULL;

	mlog_entry_void();

	*tc = NULL;

	new_i_clusters = ocfs2_clusters_for_bytes(osb->sb,
						  i_size_read(inode));
	fe = (struct ocfs2_dinode *) fe_bh->b_data;

	mlog(0, "fe->i_clusters = %u, new_i_clusters = %u, fe->i_size ="
	     "%llu\n", fe->i_clusters, new_i_clusters,
	     (unsigned long long)fe->i_size);

	if (le32_to_cpu(fe->i_clusters) <= new_i_clusters) {
		ocfs2_error(inode->i_sb, "Dinode %llu has cluster count "
			    "%u and size %llu whereas struct inode has "
			    "cluster count %u and size %llu which caused an "
			    "invalid truncate to %u clusters.",
			    (unsigned long long)le64_to_cpu(fe->i_blkno),
			    le32_to_cpu(fe->i_clusters),
			    (unsigned long long)le64_to_cpu(fe->i_size),
			    OCFS2_I(inode)->ip_clusters, i_size_read(inode),
			    new_i_clusters);
		mlog_meta_lvb(ML_ERROR, &OCFS2_I(inode)->ip_meta_lockres);
		status = -EIO;
		goto bail;
	}

	*tc = kcalloc(1, sizeof(struct ocfs2_truncate_context), GFP_KERNEL);
	if (!(*tc)) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	metadata_delete = 0;
	if (fe->id2.i_list.l_tree_depth) {
		/* If we have a tree, then the truncate may result in
		 * metadata deletes. Figure this out from the
		 * rightmost leaf block.*/
		status = ocfs2_read_block(osb, le64_to_cpu(fe->i_last_eb_blk),
					  &last_eb_bh, OCFS2_BH_CACHED, inode);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);

			brelse(last_eb_bh);
			status = -EIO;
			goto bail;
		}
		el = &(eb->h_list);
		if (le32_to_cpu(el->l_recs[0].e_cpos) >= new_i_clusters)
			metadata_delete = 1;
	}

	(*tc)->tc_last_eb_bh = last_eb_bh;

	if (metadata_delete) {
		mlog(0, "Will have to delete metadata for this trunc. "
		     "locking allocator.\n");
		ext_alloc_inode = ocfs2_get_system_file_inode(osb, EXTENT_ALLOC_SYSTEM_INODE, 0);
		if (!ext_alloc_inode) {
			status = -ENOMEM;
			mlog_errno(status);
			goto bail;
		}

		mutex_lock(&ext_alloc_inode->i_mutex);
		(*tc)->tc_ext_alloc_inode = ext_alloc_inode;

		status = ocfs2_meta_lock(ext_alloc_inode,
					 NULL,
					 &ext_alloc_bh,
					 1);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
		(*tc)->tc_ext_alloc_bh = ext_alloc_bh;
		(*tc)->tc_ext_alloc_locked = 1;
	}

	status = 0;
bail:
	if (status < 0) {
		if (*tc)
			ocfs2_free_truncate_context(*tc);
		*tc = NULL;
	}
	mlog_exit_void();
	return status;
}

static void ocfs2_free_truncate_context(struct ocfs2_truncate_context *tc)
{
	if (tc->tc_ext_alloc_inode) {
		if (tc->tc_ext_alloc_locked)
			ocfs2_meta_unlock(tc->tc_ext_alloc_inode, 1);

		mutex_unlock(&tc->tc_ext_alloc_inode->i_mutex);
		iput(tc->tc_ext_alloc_inode);
	}

	if (tc->tc_ext_alloc_bh)
		brelse(tc->tc_ext_alloc_bh);

	if (tc->tc_last_eb_bh)
		brelse(tc->tc_last_eb_bh);

	kfree(tc);
}
