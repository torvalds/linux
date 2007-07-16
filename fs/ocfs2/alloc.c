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
#include <linux/swap.h>

#define MLOG_MASK_PREFIX ML_DISK_ALLOC
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "aops.h"
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

static void ocfs2_free_truncate_context(struct ocfs2_truncate_context *tc);
static int ocfs2_cache_extent_block_free(struct ocfs2_cached_dealloc_ctxt *ctxt,
					 struct ocfs2_extent_block *eb);

/*
 * Structures which describe a path through a btree, and functions to
 * manipulate them.
 *
 * The idea here is to be as generic as possible with the tree
 * manipulation code.
 */
struct ocfs2_path_item {
	struct buffer_head		*bh;
	struct ocfs2_extent_list	*el;
};

#define OCFS2_MAX_PATH_DEPTH	5

struct ocfs2_path {
	int			p_tree_depth;
	struct ocfs2_path_item	p_node[OCFS2_MAX_PATH_DEPTH];
};

#define path_root_bh(_path) ((_path)->p_node[0].bh)
#define path_root_el(_path) ((_path)->p_node[0].el)
#define path_leaf_bh(_path) ((_path)->p_node[(_path)->p_tree_depth].bh)
#define path_leaf_el(_path) ((_path)->p_node[(_path)->p_tree_depth].el)
#define path_num_items(_path) ((_path)->p_tree_depth + 1)

/*
 * Reset the actual path elements so that we can re-use the structure
 * to build another path. Generally, this involves freeing the buffer
 * heads.
 */
static void ocfs2_reinit_path(struct ocfs2_path *path, int keep_root)
{
	int i, start = 0, depth = 0;
	struct ocfs2_path_item *node;

	if (keep_root)
		start = 1;

	for(i = start; i < path_num_items(path); i++) {
		node = &path->p_node[i];

		brelse(node->bh);
		node->bh = NULL;
		node->el = NULL;
	}

	/*
	 * Tree depth may change during truncate, or insert. If we're
	 * keeping the root extent list, then make sure that our path
	 * structure reflects the proper depth.
	 */
	if (keep_root)
		depth = le16_to_cpu(path_root_el(path)->l_tree_depth);

	path->p_tree_depth = depth;
}

static void ocfs2_free_path(struct ocfs2_path *path)
{
	if (path) {
		ocfs2_reinit_path(path, 0);
		kfree(path);
	}
}

/*
 * All the elements of src into dest. After this call, src could be freed
 * without affecting dest.
 *
 * Both paths should have the same root. Any non-root elements of dest
 * will be freed.
 */
static void ocfs2_cp_path(struct ocfs2_path *dest, struct ocfs2_path *src)
{
	int i;

	BUG_ON(path_root_bh(dest) != path_root_bh(src));
	BUG_ON(path_root_el(dest) != path_root_el(src));

	ocfs2_reinit_path(dest, 1);

	for(i = 1; i < OCFS2_MAX_PATH_DEPTH; i++) {
		dest->p_node[i].bh = src->p_node[i].bh;
		dest->p_node[i].el = src->p_node[i].el;

		if (dest->p_node[i].bh)
			get_bh(dest->p_node[i].bh);
	}
}

/*
 * Make the *dest path the same as src and re-initialize src path to
 * have a root only.
 */
static void ocfs2_mv_path(struct ocfs2_path *dest, struct ocfs2_path *src)
{
	int i;

	BUG_ON(path_root_bh(dest) != path_root_bh(src));

	for(i = 1; i < OCFS2_MAX_PATH_DEPTH; i++) {
		brelse(dest->p_node[i].bh);

		dest->p_node[i].bh = src->p_node[i].bh;
		dest->p_node[i].el = src->p_node[i].el;

		src->p_node[i].bh = NULL;
		src->p_node[i].el = NULL;
	}
}

/*
 * Insert an extent block at given index.
 *
 * This will not take an additional reference on eb_bh.
 */
static inline void ocfs2_path_insert_eb(struct ocfs2_path *path, int index,
					struct buffer_head *eb_bh)
{
	struct ocfs2_extent_block *eb = (struct ocfs2_extent_block *)eb_bh->b_data;

	/*
	 * Right now, no root bh is an extent block, so this helps
	 * catch code errors with dinode trees. The assertion can be
	 * safely removed if we ever need to insert extent block
	 * structures at the root.
	 */
	BUG_ON(index == 0);

	path->p_node[index].bh = eb_bh;
	path->p_node[index].el = &eb->h_list;
}

static struct ocfs2_path *ocfs2_new_path(struct buffer_head *root_bh,
					 struct ocfs2_extent_list *root_el)
{
	struct ocfs2_path *path;

	BUG_ON(le16_to_cpu(root_el->l_tree_depth) >= OCFS2_MAX_PATH_DEPTH);

	path = kzalloc(sizeof(*path), GFP_NOFS);
	if (path) {
		path->p_tree_depth = le16_to_cpu(root_el->l_tree_depth);
		get_bh(root_bh);
		path_root_bh(path) = root_bh;
		path_root_el(path) = root_el;
	}

	return path;
}

/*
 * Allocate and initialize a new path based on a disk inode tree.
 */
static struct ocfs2_path *ocfs2_new_inode_path(struct buffer_head *di_bh)
{
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_extent_list *el = &di->id2.i_list;

	return ocfs2_new_path(di_bh, el);
}

/*
 * Convenience function to journal all components in a path.
 */
static int ocfs2_journal_access_path(struct inode *inode, handle_t *handle,
				     struct ocfs2_path *path)
{
	int i, ret = 0;

	if (!path)
		goto out;

	for(i = 0; i < path_num_items(path); i++) {
		ret = ocfs2_journal_access(handle, inode, path->p_node[i].bh,
					   OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

out:
	return ret;
}

/*
 * Return the index of the extent record which contains cluster #v_cluster.
 * -1 is returned if it was not found.
 *
 * Should work fine on interior and exterior nodes.
 */
int ocfs2_search_extent_list(struct ocfs2_extent_list *el, u32 v_cluster)
{
	int ret = -1;
	int i;
	struct ocfs2_extent_rec *rec;
	u32 rec_end, rec_start, clusters;

	for(i = 0; i < le16_to_cpu(el->l_next_free_rec); i++) {
		rec = &el->l_recs[i];

		rec_start = le32_to_cpu(rec->e_cpos);
		clusters = ocfs2_rec_clusters(el, rec);

		rec_end = rec_start + clusters;

		if (v_cluster >= rec_start && v_cluster < rec_end) {
			ret = i;
			break;
		}
	}

	return ret;
}

enum ocfs2_contig_type {
	CONTIG_NONE = 0,
	CONTIG_LEFT,
	CONTIG_RIGHT,
	CONTIG_LEFTRIGHT,
};


/*
 * NOTE: ocfs2_block_extent_contig(), ocfs2_extents_adjacent() and
 * ocfs2_extent_contig only work properly against leaf nodes!
 */
static int ocfs2_block_extent_contig(struct super_block *sb,
				     struct ocfs2_extent_rec *ext,
				     u64 blkno)
{
	u64 blk_end = le64_to_cpu(ext->e_blkno);

	blk_end += ocfs2_clusters_to_blocks(sb,
				    le16_to_cpu(ext->e_leaf_clusters));

	return blkno == blk_end;
}

static int ocfs2_extents_adjacent(struct ocfs2_extent_rec *left,
				  struct ocfs2_extent_rec *right)
{
	u32 left_range;

	left_range = le32_to_cpu(left->e_cpos) +
		le16_to_cpu(left->e_leaf_clusters);

	return (left_range == le32_to_cpu(right->e_cpos));
}

static enum ocfs2_contig_type
	ocfs2_extent_contig(struct inode *inode,
			    struct ocfs2_extent_rec *ext,
			    struct ocfs2_extent_rec *insert_rec)
{
	u64 blkno = le64_to_cpu(insert_rec->e_blkno);

	/*
	 * Refuse to coalesce extent records with different flag
	 * fields - we don't want to mix unwritten extents with user
	 * data.
	 */
	if (ext->e_flags != insert_rec->e_flags)
		return CONTIG_NONE;

	if (ocfs2_extents_adjacent(ext, insert_rec) &&
	    ocfs2_block_extent_contig(inode->i_sb, ext, blkno))
			return CONTIG_RIGHT;

	blkno = le64_to_cpu(ext->e_blkno);
	if (ocfs2_extents_adjacent(insert_rec, ext) &&
	    ocfs2_block_extent_contig(inode->i_sb, insert_rec, blkno))
		return CONTIG_LEFT;

	return CONTIG_NONE;
}

/*
 * NOTE: We can have pretty much any combination of contiguousness and
 * appending.
 *
 * The usefulness of APPEND_TAIL is more in that it lets us know that
 * we'll have to update the path to that leaf.
 */
enum ocfs2_append_type {
	APPEND_NONE = 0,
	APPEND_TAIL,
};

enum ocfs2_split_type {
	SPLIT_NONE = 0,
	SPLIT_LEFT,
	SPLIT_RIGHT,
};

struct ocfs2_insert_type {
	enum ocfs2_split_type	ins_split;
	enum ocfs2_append_type	ins_appending;
	enum ocfs2_contig_type	ins_contig;
	int			ins_contig_index;
	int			ins_free_records;
	int			ins_tree_depth;
};

struct ocfs2_merge_ctxt {
	enum ocfs2_contig_type	c_contig_type;
	int			c_has_empty_extent;
	int			c_split_covers_rec;
	int			c_used_tail_recs;
};

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
				     handle_t *handle,
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
			eb->h_suballoc_slot = cpu_to_le16(osb->slot_num);
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
 * Helper function for ocfs2_add_branch() and ocfs2_shift_tree_depth().
 *
 * Returns the sum of the rightmost extent rec logical offset and
 * cluster count.
 *
 * ocfs2_add_branch() uses this to determine what logical cluster
 * value should be populated into the leftmost new branch records.
 *
 * ocfs2_shift_tree_depth() uses this to determine the # clusters
 * value for the new topmost tree record.
 */
static inline u32 ocfs2_sum_rightmost_rec(struct ocfs2_extent_list  *el)
{
	int i;

	i = le16_to_cpu(el->l_next_free_rec) - 1;

	return le32_to_cpu(el->l_recs[i].e_cpos) +
		ocfs2_rec_clusters(el, &el->l_recs[i]);
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
 * contain a single record with cluster count == 0.
 */
static int ocfs2_add_branch(struct ocfs2_super *osb,
			    handle_t *handle,
			    struct inode *inode,
			    struct buffer_head *fe_bh,
			    struct buffer_head *eb_bh,
			    struct buffer_head **last_eb_bh,
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
	u32 new_cpos;

	mlog_entry_void();

	BUG_ON(!last_eb_bh || !*last_eb_bh);

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

	eb = (struct ocfs2_extent_block *)(*last_eb_bh)->b_data;
	new_cpos = ocfs2_sum_rightmost_rec(&eb->h_list);

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
		/*
		 * This actually counts as an empty extent as
		 * c_clusters == 0
		 */
		eb_el->l_recs[0].e_cpos = cpu_to_le32(new_cpos);
		eb_el->l_recs[0].e_blkno = cpu_to_le64(next_blkno);
		/*
		 * eb_el isn't always an interior node, but even leaf
		 * nodes want a zero'd flags and reserved field so
		 * this gets the whole 32 bits regardless of use.
		 */
		eb_el->l_recs[0].e_int_clusters = cpu_to_le32(0);
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
	status = ocfs2_journal_access(handle, inode, *last_eb_bh,
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
	el->l_recs[i].e_cpos = cpu_to_le32(new_cpos);
	el->l_recs[i].e_int_clusters = 0;
	le16_add_cpu(&el->l_next_free_rec, 1);

	/* fe needs a new last extent block pointer, as does the
	 * next_leaf on the previously last-extent-block. */
	fe->i_last_eb_blk = cpu_to_le64(new_last_eb_blk);

	eb = (struct ocfs2_extent_block *) (*last_eb_bh)->b_data;
	eb->h_next_leaf_blk = cpu_to_le64(new_last_eb_blk);

	status = ocfs2_journal_dirty(handle, *last_eb_bh);
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

	/*
	 * Some callers want to track the rightmost leaf so pass it
	 * back here.
	 */
	brelse(*last_eb_bh);
	get_bh(new_eb_bhs[0]);
	*last_eb_bh = new_eb_bhs[0];

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
				  handle_t *handle,
				  struct inode *inode,
				  struct buffer_head *fe_bh,
				  struct ocfs2_alloc_context *meta_ac,
				  struct buffer_head **ret_new_eb_bh)
{
	int status, i;
	u32 new_clusters;
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
	for(i = 0; i < le16_to_cpu(fe_el->l_next_free_rec); i++)
		eb_el->l_recs[i] = fe_el->l_recs[i];

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

	new_clusters = ocfs2_sum_rightmost_rec(eb_el);

	/* update fe now */
	le16_add_cpu(&fe_el->l_tree_depth, 1);
	fe_el->l_recs[0].e_cpos = 0;
	fe_el->l_recs[0].e_blkno = eb->h_blkno;
	fe_el->l_recs[0].e_int_clusters = cpu_to_le32(new_clusters);
	for(i = 1; i < le16_to_cpu(fe_el->l_next_free_rec); i++)
		memset(&fe_el->l_recs[i], 0, sizeof(struct ocfs2_extent_rec));
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

/*
 * Grow a b-tree so that it has more records.
 *
 * We might shift the tree depth in which case existing paths should
 * be considered invalid.
 *
 * Tree depth after the grow is returned via *final_depth.
 *
 * *last_eb_bh will be updated by ocfs2_add_branch().
 */
static int ocfs2_grow_tree(struct inode *inode, handle_t *handle,
			   struct buffer_head *di_bh, int *final_depth,
			   struct buffer_head **last_eb_bh,
			   struct ocfs2_alloc_context *meta_ac)
{
	int ret, shift;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	int depth = le16_to_cpu(di->id2.i_list.l_tree_depth);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *bh = NULL;

	BUG_ON(meta_ac == NULL);

	shift = ocfs2_find_branch_target(osb, inode, di_bh, &bh);
	if (shift < 0) {
		ret = shift;
		mlog_errno(ret);
		goto out;
	}

	/* We traveled all the way to the bottom of the allocation tree
	 * and didn't find room for any more extents - we need to add
	 * another tree level */
	if (shift) {
		BUG_ON(bh);
		mlog(0, "need to shift tree depth (current = %d)\n", depth);

		/* ocfs2_shift_tree_depth will return us a buffer with
		 * the new extent block (so we can pass that to
		 * ocfs2_add_branch). */
		ret = ocfs2_shift_tree_depth(osb, handle, inode, di_bh,
					     meta_ac, &bh);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
		depth++;
		if (depth == 1) {
			/*
			 * Special case: we have room now if we shifted from
			 * tree_depth 0, so no more work needs to be done.
			 *
			 * We won't be calling add_branch, so pass
			 * back *last_eb_bh as the new leaf. At depth
			 * zero, it should always be null so there's
			 * no reason to brelse.
			 */
			BUG_ON(*last_eb_bh);
			get_bh(bh);
			*last_eb_bh = bh;
			goto out;
		}
	}

	/* call ocfs2_add_branch to add the final part of the tree with
	 * the new data. */
	mlog(0, "add branch. bh = %p\n", bh);
	ret = ocfs2_add_branch(osb, handle, inode, di_bh, bh, last_eb_bh,
			       meta_ac);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

out:
	if (final_depth)
		*final_depth = depth;
	brelse(bh);
	return ret;
}

/*
 * This is only valid for leaf nodes, which are the only ones that can
 * have empty extents anyway.
 */
static inline int ocfs2_is_empty_extent(struct ocfs2_extent_rec *rec)
{
	return !rec->e_leaf_clusters;
}

/*
 * This function will discard the rightmost extent record.
 */
static void ocfs2_shift_records_right(struct ocfs2_extent_list *el)
{
	int next_free = le16_to_cpu(el->l_next_free_rec);
	int count = le16_to_cpu(el->l_count);
	unsigned int num_bytes;

	BUG_ON(!next_free);
	/* This will cause us to go off the end of our extent list. */
	BUG_ON(next_free >= count);

	num_bytes = sizeof(struct ocfs2_extent_rec) * next_free;

	memmove(&el->l_recs[1], &el->l_recs[0], num_bytes);
}

static void ocfs2_rotate_leaf(struct ocfs2_extent_list *el,
			      struct ocfs2_extent_rec *insert_rec)
{
	int i, insert_index, next_free, has_empty, num_bytes;
	u32 insert_cpos = le32_to_cpu(insert_rec->e_cpos);
	struct ocfs2_extent_rec *rec;

	next_free = le16_to_cpu(el->l_next_free_rec);
	has_empty = ocfs2_is_empty_extent(&el->l_recs[0]);

	BUG_ON(!next_free);

	/* The tree code before us didn't allow enough room in the leaf. */
	if (el->l_next_free_rec == el->l_count && !has_empty)
		BUG();

	/*
	 * The easiest way to approach this is to just remove the
	 * empty extent and temporarily decrement next_free.
	 */
	if (has_empty) {
		/*
		 * If next_free was 1 (only an empty extent), this
		 * loop won't execute, which is fine. We still want
		 * the decrement above to happen.
		 */
		for(i = 0; i < (next_free - 1); i++)
			el->l_recs[i] = el->l_recs[i+1];

		next_free--;
	}

	/*
	 * Figure out what the new record index should be.
	 */
	for(i = 0; i < next_free; i++) {
		rec = &el->l_recs[i];

		if (insert_cpos < le32_to_cpu(rec->e_cpos))
			break;
	}
	insert_index = i;

	mlog(0, "ins %u: index %d, has_empty %d, next_free %d, count %d\n",
	     insert_cpos, insert_index, has_empty, next_free, le16_to_cpu(el->l_count));

	BUG_ON(insert_index < 0);
	BUG_ON(insert_index >= le16_to_cpu(el->l_count));
	BUG_ON(insert_index > next_free);

	/*
	 * No need to memmove if we're just adding to the tail.
	 */
	if (insert_index != next_free) {
		BUG_ON(next_free >= le16_to_cpu(el->l_count));

		num_bytes = next_free - insert_index;
		num_bytes *= sizeof(struct ocfs2_extent_rec);
		memmove(&el->l_recs[insert_index + 1],
			&el->l_recs[insert_index],
			num_bytes);
	}

	/*
	 * Either we had an empty extent, and need to re-increment or
	 * there was no empty extent on a non full rightmost leaf node,
	 * in which case we still need to increment.
	 */
	next_free++;
	el->l_next_free_rec = cpu_to_le16(next_free);
	/*
	 * Make sure none of the math above just messed up our tree.
	 */
	BUG_ON(le16_to_cpu(el->l_next_free_rec) > le16_to_cpu(el->l_count));

	el->l_recs[insert_index] = *insert_rec;

}

static void ocfs2_remove_empty_extent(struct ocfs2_extent_list *el)
{
	int size, num_recs = le16_to_cpu(el->l_next_free_rec);

	BUG_ON(num_recs == 0);

	if (ocfs2_is_empty_extent(&el->l_recs[0])) {
		num_recs--;
		size = num_recs * sizeof(struct ocfs2_extent_rec);
		memmove(&el->l_recs[0], &el->l_recs[1], size);
		memset(&el->l_recs[num_recs], 0,
		       sizeof(struct ocfs2_extent_rec));
		el->l_next_free_rec = cpu_to_le16(num_recs);
	}
}

/*
 * Create an empty extent record .
 *
 * l_next_free_rec may be updated.
 *
 * If an empty extent already exists do nothing.
 */
static void ocfs2_create_empty_extent(struct ocfs2_extent_list *el)
{
	int next_free = le16_to_cpu(el->l_next_free_rec);

	BUG_ON(le16_to_cpu(el->l_tree_depth) != 0);

	if (next_free == 0)
		goto set_and_inc;

	if (ocfs2_is_empty_extent(&el->l_recs[0]))
		return;

	mlog_bug_on_msg(el->l_count == el->l_next_free_rec,
			"Asked to create an empty extent in a full list:\n"
			"count = %u, tree depth = %u",
			le16_to_cpu(el->l_count),
			le16_to_cpu(el->l_tree_depth));

	ocfs2_shift_records_right(el);

set_and_inc:
	le16_add_cpu(&el->l_next_free_rec, 1);
	memset(&el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));
}

/*
 * For a rotation which involves two leaf nodes, the "root node" is
 * the lowest level tree node which contains a path to both leafs. This
 * resulting set of information can be used to form a complete "subtree"
 *
 * This function is passed two full paths from the dinode down to a
 * pair of adjacent leaves. It's task is to figure out which path
 * index contains the subtree root - this can be the root index itself
 * in a worst-case rotation.
 *
 * The array index of the subtree root is passed back.
 */
static int ocfs2_find_subtree_root(struct inode *inode,
				   struct ocfs2_path *left,
				   struct ocfs2_path *right)
{
	int i = 0;

	/*
	 * Check that the caller passed in two paths from the same tree.
	 */
	BUG_ON(path_root_bh(left) != path_root_bh(right));

	do {
		i++;

		/*
		 * The caller didn't pass two adjacent paths.
		 */
		mlog_bug_on_msg(i > left->p_tree_depth,
				"Inode %lu, left depth %u, right depth %u\n"
				"left leaf blk %llu, right leaf blk %llu\n",
				inode->i_ino, left->p_tree_depth,
				right->p_tree_depth,
				(unsigned long long)path_leaf_bh(left)->b_blocknr,
				(unsigned long long)path_leaf_bh(right)->b_blocknr);
	} while (left->p_node[i].bh->b_blocknr ==
		 right->p_node[i].bh->b_blocknr);

	return i - 1;
}

typedef void (path_insert_t)(void *, struct buffer_head *);

/*
 * Traverse a btree path in search of cpos, starting at root_el.
 *
 * This code can be called with a cpos larger than the tree, in which
 * case it will return the rightmost path.
 */
static int __ocfs2_find_path(struct inode *inode,
			     struct ocfs2_extent_list *root_el, u32 cpos,
			     path_insert_t *func, void *data)
{
	int i, ret = 0;
	u32 range;
	u64 blkno;
	struct buffer_head *bh = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	el = root_el;
	while (el->l_tree_depth) {
		if (le16_to_cpu(el->l_next_free_rec) == 0) {
			ocfs2_error(inode->i_sb,
				    "Inode %llu has empty extent list at "
				    "depth %u\n",
				    (unsigned long long)oi->ip_blkno,
				    le16_to_cpu(el->l_tree_depth));
			ret = -EROFS;
			goto out;

		}

		for(i = 0; i < le16_to_cpu(el->l_next_free_rec) - 1; i++) {
			rec = &el->l_recs[i];

			/*
			 * In the case that cpos is off the allocation
			 * tree, this should just wind up returning the
			 * rightmost record.
			 */
			range = le32_to_cpu(rec->e_cpos) +
				ocfs2_rec_clusters(el, rec);
			if (cpos >= le32_to_cpu(rec->e_cpos) && cpos < range)
			    break;
		}

		blkno = le64_to_cpu(el->l_recs[i].e_blkno);
		if (blkno == 0) {
			ocfs2_error(inode->i_sb,
				    "Inode %llu has bad blkno in extent list "
				    "at depth %u (index %d)\n",
				    (unsigned long long)oi->ip_blkno,
				    le16_to_cpu(el->l_tree_depth), i);
			ret = -EROFS;
			goto out;
		}

		brelse(bh);
		bh = NULL;
		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb), blkno,
				       &bh, OCFS2_BH_CACHED, inode);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) bh->b_data;
		el = &eb->h_list;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			ret = -EIO;
			goto out;
		}

		if (le16_to_cpu(el->l_next_free_rec) >
		    le16_to_cpu(el->l_count)) {
			ocfs2_error(inode->i_sb,
				    "Inode %llu has bad count in extent list "
				    "at block %llu (next free=%u, count=%u)\n",
				    (unsigned long long)oi->ip_blkno,
				    (unsigned long long)bh->b_blocknr,
				    le16_to_cpu(el->l_next_free_rec),
				    le16_to_cpu(el->l_count));
			ret = -EROFS;
			goto out;
		}

		if (func)
			func(data, bh);
	}

out:
	/*
	 * Catch any trailing bh that the loop didn't handle.
	 */
	brelse(bh);

	return ret;
}

/*
 * Given an initialized path (that is, it has a valid root extent
 * list), this function will traverse the btree in search of the path
 * which would contain cpos.
 *
 * The path traveled is recorded in the path structure.
 *
 * Note that this will not do any comparisons on leaf node extent
 * records, so it will work fine in the case that we just added a tree
 * branch.
 */
struct find_path_data {
	int index;
	struct ocfs2_path *path;
};
static void find_path_ins(void *data, struct buffer_head *bh)
{
	struct find_path_data *fp = data;

	get_bh(bh);
	ocfs2_path_insert_eb(fp->path, fp->index, bh);
	fp->index++;
}
static int ocfs2_find_path(struct inode *inode, struct ocfs2_path *path,
			   u32 cpos)
{
	struct find_path_data data;

	data.index = 1;
	data.path = path;
	return __ocfs2_find_path(inode, path_root_el(path), cpos,
				 find_path_ins, &data);
}

static void find_leaf_ins(void *data, struct buffer_head *bh)
{
	struct ocfs2_extent_block *eb =(struct ocfs2_extent_block *)bh->b_data;
	struct ocfs2_extent_list *el = &eb->h_list;
	struct buffer_head **ret = data;

	/* We want to retain only the leaf block. */
	if (le16_to_cpu(el->l_tree_depth) == 0) {
		get_bh(bh);
		*ret = bh;
	}
}
/*
 * Find the leaf block in the tree which would contain cpos. No
 * checking of the actual leaf is done.
 *
 * Some paths want to call this instead of allocating a path structure
 * and calling ocfs2_find_path().
 *
 * This function doesn't handle non btree extent lists.
 */
int ocfs2_find_leaf(struct inode *inode, struct ocfs2_extent_list *root_el,
		    u32 cpos, struct buffer_head **leaf_bh)
{
	int ret;
	struct buffer_head *bh = NULL;

	ret = __ocfs2_find_path(inode, root_el, cpos, find_leaf_ins, &bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	*leaf_bh = bh;
out:
	return ret;
}

/*
 * Adjust the adjacent records (left_rec, right_rec) involved in a rotation.
 *
 * Basically, we've moved stuff around at the bottom of the tree and
 * we need to fix up the extent records above the changes to reflect
 * the new changes.
 *
 * left_rec: the record on the left.
 * left_child_el: is the child list pointed to by left_rec
 * right_rec: the record to the right of left_rec
 * right_child_el: is the child list pointed to by right_rec
 *
 * By definition, this only works on interior nodes.
 */
static void ocfs2_adjust_adjacent_records(struct ocfs2_extent_rec *left_rec,
				  struct ocfs2_extent_list *left_child_el,
				  struct ocfs2_extent_rec *right_rec,
				  struct ocfs2_extent_list *right_child_el)
{
	u32 left_clusters, right_end;

	/*
	 * Interior nodes never have holes. Their cpos is the cpos of
	 * the leftmost record in their child list. Their cluster
	 * count covers the full theoretical range of their child list
	 * - the range between their cpos and the cpos of the record
	 * immediately to their right.
	 */
	left_clusters = le32_to_cpu(right_child_el->l_recs[0].e_cpos);
	if (ocfs2_is_empty_extent(&right_child_el->l_recs[0])) {
		BUG_ON(le16_to_cpu(right_child_el->l_next_free_rec) <= 1);
		left_clusters = le32_to_cpu(right_child_el->l_recs[1].e_cpos);
	}
	left_clusters -= le32_to_cpu(left_rec->e_cpos);
	left_rec->e_int_clusters = cpu_to_le32(left_clusters);

	/*
	 * Calculate the rightmost cluster count boundary before
	 * moving cpos - we will need to adjust clusters after
	 * updating e_cpos to keep the same highest cluster count.
	 */
	right_end = le32_to_cpu(right_rec->e_cpos);
	right_end += le32_to_cpu(right_rec->e_int_clusters);

	right_rec->e_cpos = left_rec->e_cpos;
	le32_add_cpu(&right_rec->e_cpos, left_clusters);

	right_end -= le32_to_cpu(right_rec->e_cpos);
	right_rec->e_int_clusters = cpu_to_le32(right_end);
}

/*
 * Adjust the adjacent root node records involved in a
 * rotation. left_el_blkno is passed in as a key so that we can easily
 * find it's index in the root list.
 */
static void ocfs2_adjust_root_records(struct ocfs2_extent_list *root_el,
				      struct ocfs2_extent_list *left_el,
				      struct ocfs2_extent_list *right_el,
				      u64 left_el_blkno)
{
	int i;

	BUG_ON(le16_to_cpu(root_el->l_tree_depth) <=
	       le16_to_cpu(left_el->l_tree_depth));

	for(i = 0; i < le16_to_cpu(root_el->l_next_free_rec) - 1; i++) {
		if (le64_to_cpu(root_el->l_recs[i].e_blkno) == left_el_blkno)
			break;
	}

	/*
	 * The path walking code should have never returned a root and
	 * two paths which are not adjacent.
	 */
	BUG_ON(i >= (le16_to_cpu(root_el->l_next_free_rec) - 1));

	ocfs2_adjust_adjacent_records(&root_el->l_recs[i], left_el,
				      &root_el->l_recs[i + 1], right_el);
}

/*
 * We've changed a leaf block (in right_path) and need to reflect that
 * change back up the subtree.
 *
 * This happens in multiple places:
 *   - When we've moved an extent record from the left path leaf to the right
 *     path leaf to make room for an empty extent in the left path leaf.
 *   - When our insert into the right path leaf is at the leftmost edge
 *     and requires an update of the path immediately to it's left. This
 *     can occur at the end of some types of rotation and appending inserts.
 */
static void ocfs2_complete_edge_insert(struct inode *inode, handle_t *handle,
				       struct ocfs2_path *left_path,
				       struct ocfs2_path *right_path,
				       int subtree_index)
{
	int ret, i, idx;
	struct ocfs2_extent_list *el, *left_el, *right_el;
	struct ocfs2_extent_rec *left_rec, *right_rec;
	struct buffer_head *root_bh = left_path->p_node[subtree_index].bh;

	/*
	 * Update the counts and position values within all the
	 * interior nodes to reflect the leaf rotation we just did.
	 *
	 * The root node is handled below the loop.
	 *
	 * We begin the loop with right_el and left_el pointing to the
	 * leaf lists and work our way up.
	 *
	 * NOTE: within this loop, left_el and right_el always refer
	 * to the *child* lists.
	 */
	left_el = path_leaf_el(left_path);
	right_el = path_leaf_el(right_path);
	for(i = left_path->p_tree_depth - 1; i > subtree_index; i--) {
		mlog(0, "Adjust records at index %u\n", i);

		/*
		 * One nice property of knowing that all of these
		 * nodes are below the root is that we only deal with
		 * the leftmost right node record and the rightmost
		 * left node record.
		 */
		el = left_path->p_node[i].el;
		idx = le16_to_cpu(left_el->l_next_free_rec) - 1;
		left_rec = &el->l_recs[idx];

		el = right_path->p_node[i].el;
		right_rec = &el->l_recs[0];

		ocfs2_adjust_adjacent_records(left_rec, left_el, right_rec,
					      right_el);

		ret = ocfs2_journal_dirty(handle, left_path->p_node[i].bh);
		if (ret)
			mlog_errno(ret);

		ret = ocfs2_journal_dirty(handle, right_path->p_node[i].bh);
		if (ret)
			mlog_errno(ret);

		/*
		 * Setup our list pointers now so that the current
		 * parents become children in the next iteration.
		 */
		left_el = left_path->p_node[i].el;
		right_el = right_path->p_node[i].el;
	}

	/*
	 * At the root node, adjust the two adjacent records which
	 * begin our path to the leaves.
	 */

	el = left_path->p_node[subtree_index].el;
	left_el = left_path->p_node[subtree_index + 1].el;
	right_el = right_path->p_node[subtree_index + 1].el;

	ocfs2_adjust_root_records(el, left_el, right_el,
				  left_path->p_node[subtree_index + 1].bh->b_blocknr);

	root_bh = left_path->p_node[subtree_index].bh;

	ret = ocfs2_journal_dirty(handle, root_bh);
	if (ret)
		mlog_errno(ret);
}

static int ocfs2_rotate_subtree_right(struct inode *inode,
				      handle_t *handle,
				      struct ocfs2_path *left_path,
				      struct ocfs2_path *right_path,
				      int subtree_index)
{
	int ret, i;
	struct buffer_head *right_leaf_bh;
	struct buffer_head *left_leaf_bh = NULL;
	struct buffer_head *root_bh;
	struct ocfs2_extent_list *right_el, *left_el;
	struct ocfs2_extent_rec move_rec;

	left_leaf_bh = path_leaf_bh(left_path);
	left_el = path_leaf_el(left_path);

	if (left_el->l_next_free_rec != left_el->l_count) {
		ocfs2_error(inode->i_sb,
			    "Inode %llu has non-full interior leaf node %llu"
			    "(next free = %u)",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno,
			    (unsigned long long)left_leaf_bh->b_blocknr,
			    le16_to_cpu(left_el->l_next_free_rec));
		return -EROFS;
	}

	/*
	 * This extent block may already have an empty record, so we
	 * return early if so.
	 */
	if (ocfs2_is_empty_extent(&left_el->l_recs[0]))
		return 0;

	root_bh = left_path->p_node[subtree_index].bh;
	BUG_ON(root_bh != right_path->p_node[subtree_index].bh);

	ret = ocfs2_journal_access(handle, inode, root_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	for(i = subtree_index + 1; i < path_num_items(right_path); i++) {
		ret = ocfs2_journal_access(handle, inode,
					   right_path->p_node[i].bh,
					   OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_journal_access(handle, inode,
					   left_path->p_node[i].bh,
					   OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	right_leaf_bh = path_leaf_bh(right_path);
	right_el = path_leaf_el(right_path);

	/* This is a code error, not a disk corruption. */
	mlog_bug_on_msg(!right_el->l_next_free_rec, "Inode %llu: Rotate fails "
			"because rightmost leaf block %llu is empty\n",
			(unsigned long long)OCFS2_I(inode)->ip_blkno,
			(unsigned long long)right_leaf_bh->b_blocknr);

	ocfs2_create_empty_extent(right_el);

	ret = ocfs2_journal_dirty(handle, right_leaf_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* Do the copy now. */
	i = le16_to_cpu(left_el->l_next_free_rec) - 1;
	move_rec = left_el->l_recs[i];
	right_el->l_recs[0] = move_rec;

	/*
	 * Clear out the record we just copied and shift everything
	 * over, leaving an empty extent in the left leaf.
	 *
	 * We temporarily subtract from next_free_rec so that the
	 * shift will lose the tail record (which is now defunct).
	 */
	le16_add_cpu(&left_el->l_next_free_rec, -1);
	ocfs2_shift_records_right(left_el);
	memset(&left_el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));
	le16_add_cpu(&left_el->l_next_free_rec, 1);

	ret = ocfs2_journal_dirty(handle, left_leaf_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_complete_edge_insert(inode, handle, left_path, right_path,
				subtree_index);

out:
	return ret;
}

/*
 * Given a full path, determine what cpos value would return us a path
 * containing the leaf immediately to the left of the current one.
 *
 * Will return zero if the path passed in is already the leftmost path.
 */
static int ocfs2_find_cpos_for_left_leaf(struct super_block *sb,
					 struct ocfs2_path *path, u32 *cpos)
{
	int i, j, ret = 0;
	u64 blkno;
	struct ocfs2_extent_list *el;

	BUG_ON(path->p_tree_depth == 0);

	*cpos = 0;

	blkno = path_leaf_bh(path)->b_blocknr;

	/* Start at the tree node just above the leaf and work our way up. */
	i = path->p_tree_depth - 1;
	while (i >= 0) {
		el = path->p_node[i].el;

		/*
		 * Find the extent record just before the one in our
		 * path.
		 */
		for(j = 0; j < le16_to_cpu(el->l_next_free_rec); j++) {
			if (le64_to_cpu(el->l_recs[j].e_blkno) == blkno) {
				if (j == 0) {
					if (i == 0) {
						/*
						 * We've determined that the
						 * path specified is already
						 * the leftmost one - return a
						 * cpos of zero.
						 */
						goto out;
					}
					/*
					 * The leftmost record points to our
					 * leaf - we need to travel up the
					 * tree one level.
					 */
					goto next_node;
				}

				*cpos = le32_to_cpu(el->l_recs[j - 1].e_cpos);
				*cpos = *cpos + ocfs2_rec_clusters(el,
							   &el->l_recs[j - 1]);
				*cpos = *cpos - 1;
				goto out;
			}
		}

		/*
		 * If we got here, we never found a valid node where
		 * the tree indicated one should be.
		 */
		ocfs2_error(sb,
			    "Invalid extent tree at extent block %llu\n",
			    (unsigned long long)blkno);
		ret = -EROFS;
		goto out;

next_node:
		blkno = path->p_node[i].bh->b_blocknr;
		i--;
	}

out:
	return ret;
}

/*
 * Extend the transaction by enough credits to complete the rotation,
 * and still leave at least the original number of credits allocated
 * to this transaction.
 */
static int ocfs2_extend_rotate_transaction(handle_t *handle, int subtree_depth,
					   int op_credits,
					   struct ocfs2_path *path)
{
	int credits = (path->p_tree_depth - subtree_depth) * 2 + 1 + op_credits;

	if (handle->h_buffer_credits < credits)
		return ocfs2_extend_trans(handle, credits);

	return 0;
}

/*
 * Trap the case where we're inserting into the theoretical range past
 * the _actual_ left leaf range. Otherwise, we'll rotate a record
 * whose cpos is less than ours into the right leaf.
 *
 * It's only necessary to look at the rightmost record of the left
 * leaf because the logic that calls us should ensure that the
 * theoretical ranges in the path components above the leaves are
 * correct.
 */
static int ocfs2_rotate_requires_path_adjustment(struct ocfs2_path *left_path,
						 u32 insert_cpos)
{
	struct ocfs2_extent_list *left_el;
	struct ocfs2_extent_rec *rec;
	int next_free;

	left_el = path_leaf_el(left_path);
	next_free = le16_to_cpu(left_el->l_next_free_rec);
	rec = &left_el->l_recs[next_free - 1];

	if (insert_cpos > le32_to_cpu(rec->e_cpos))
		return 1;
	return 0;
}

static int ocfs2_leftmost_rec_contains(struct ocfs2_extent_list *el, u32 cpos)
{
	int next_free = le16_to_cpu(el->l_next_free_rec);
	unsigned int range;
	struct ocfs2_extent_rec *rec;

	if (next_free == 0)
		return 0;

	rec = &el->l_recs[0];
	if (ocfs2_is_empty_extent(rec)) {
		/* Empty list. */
		if (next_free == 1)
			return 0;
		rec = &el->l_recs[1];
	}

	range = le32_to_cpu(rec->e_cpos) + ocfs2_rec_clusters(el, rec);
	if (cpos >= le32_to_cpu(rec->e_cpos) && cpos < range)
		return 1;
	return 0;
}

/*
 * Rotate all the records in a btree right one record, starting at insert_cpos.
 *
 * The path to the rightmost leaf should be passed in.
 *
 * The array is assumed to be large enough to hold an entire path (tree depth).
 *
 * Upon succesful return from this function:
 *
 * - The 'right_path' array will contain a path to the leaf block
 *   whose range contains e_cpos.
 * - That leaf block will have a single empty extent in list index 0.
 * - In the case that the rotation requires a post-insert update,
 *   *ret_left_path will contain a valid path which can be passed to
 *   ocfs2_insert_path().
 */
static int ocfs2_rotate_tree_right(struct inode *inode,
				   handle_t *handle,
				   enum ocfs2_split_type split,
				   u32 insert_cpos,
				   struct ocfs2_path *right_path,
				   struct ocfs2_path **ret_left_path)
{
	int ret, start, orig_credits = handle->h_buffer_credits;
	u32 cpos;
	struct ocfs2_path *left_path = NULL;

	*ret_left_path = NULL;

	left_path = ocfs2_new_path(path_root_bh(right_path),
				   path_root_el(right_path));
	if (!left_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_cpos_for_left_leaf(inode->i_sb, right_path, &cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	mlog(0, "Insert: %u, first left path cpos: %u\n", insert_cpos, cpos);

	/*
	 * What we want to do here is:
	 *
	 * 1) Start with the rightmost path.
	 *
	 * 2) Determine a path to the leaf block directly to the left
	 *    of that leaf.
	 *
	 * 3) Determine the 'subtree root' - the lowest level tree node
	 *    which contains a path to both leaves.
	 *
	 * 4) Rotate the subtree.
	 *
	 * 5) Find the next subtree by considering the left path to be
	 *    the new right path.
	 *
	 * The check at the top of this while loop also accepts
	 * insert_cpos == cpos because cpos is only a _theoretical_
	 * value to get us the left path - insert_cpos might very well
	 * be filling that hole.
	 *
	 * Stop at a cpos of '0' because we either started at the
	 * leftmost branch (i.e., a tree with one branch and a
	 * rotation inside of it), or we've gone as far as we can in
	 * rotating subtrees.
	 */
	while (cpos && insert_cpos <= cpos) {
		mlog(0, "Rotating a tree: ins. cpos: %u, left path cpos: %u\n",
		     insert_cpos, cpos);

		ret = ocfs2_find_path(inode, left_path, cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		mlog_bug_on_msg(path_leaf_bh(left_path) ==
				path_leaf_bh(right_path),
				"Inode %lu: error during insert of %u "
				"(left path cpos %u) results in two identical "
				"paths ending at %llu\n",
				inode->i_ino, insert_cpos, cpos,
				(unsigned long long)
				path_leaf_bh(left_path)->b_blocknr);

		if (split == SPLIT_NONE &&
		    ocfs2_rotate_requires_path_adjustment(left_path,
							  insert_cpos)) {

			/*
			 * We've rotated the tree as much as we
			 * should. The rest is up to
			 * ocfs2_insert_path() to complete, after the
			 * record insertion. We indicate this
			 * situation by returning the left path.
			 *
			 * The reason we don't adjust the records here
			 * before the record insert is that an error
			 * later might break the rule where a parent
			 * record e_cpos will reflect the actual
			 * e_cpos of the 1st nonempty record of the
			 * child list.
			 */
			*ret_left_path = left_path;
			goto out_ret_path;
		}

		start = ocfs2_find_subtree_root(inode, left_path, right_path);

		mlog(0, "Subtree root at index %d (blk %llu, depth %d)\n",
		     start,
		     (unsigned long long) right_path->p_node[start].bh->b_blocknr,
		     right_path->p_tree_depth);

		ret = ocfs2_extend_rotate_transaction(handle, start,
						      orig_credits, right_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_rotate_subtree_right(inode, handle, left_path,
						 right_path, start);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (split != SPLIT_NONE &&
		    ocfs2_leftmost_rec_contains(path_leaf_el(right_path),
						insert_cpos)) {
			/*
			 * A rotate moves the rightmost left leaf
			 * record over to the leftmost right leaf
			 * slot. If we're doing an extent split
			 * instead of a real insert, then we have to
			 * check that the extent to be split wasn't
			 * just moved over. If it was, then we can
			 * exit here, passing left_path back -
			 * ocfs2_split_extent() is smart enough to
			 * search both leaves.
			 */
			*ret_left_path = left_path;
			goto out_ret_path;
		}

		/*
		 * There is no need to re-read the next right path
		 * as we know that it'll be our current left
		 * path. Optimize by copying values instead.
		 */
		ocfs2_mv_path(right_path, left_path);

		ret = ocfs2_find_cpos_for_left_leaf(inode->i_sb, right_path,
						    &cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

out:
	ocfs2_free_path(left_path);

out_ret_path:
	return ret;
}

static void ocfs2_update_edge_lengths(struct inode *inode, handle_t *handle,
				      struct ocfs2_path *path)
{
	int i, idx;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_block *eb;
	u32 range;

	/* Path should always be rightmost. */
	eb = (struct ocfs2_extent_block *)path_leaf_bh(path)->b_data;
	BUG_ON(eb->h_next_leaf_blk != 0ULL);

	el = &eb->h_list;
	BUG_ON(le16_to_cpu(el->l_next_free_rec) == 0);
	idx = le16_to_cpu(el->l_next_free_rec) - 1;
	rec = &el->l_recs[idx];
	range = le32_to_cpu(rec->e_cpos) + ocfs2_rec_clusters(el, rec);

	for (i = 0; i < path->p_tree_depth; i++) {
		el = path->p_node[i].el;
		idx = le16_to_cpu(el->l_next_free_rec) - 1;
		rec = &el->l_recs[idx];

		rec->e_int_clusters = cpu_to_le32(range);
		le32_add_cpu(&rec->e_int_clusters, -le32_to_cpu(rec->e_cpos));

		ocfs2_journal_dirty(handle, path->p_node[i].bh);
	}
}

static void ocfs2_unlink_path(struct inode *inode, handle_t *handle,
			      struct ocfs2_cached_dealloc_ctxt *dealloc,
			      struct ocfs2_path *path, int unlink_start)
{
	int ret, i;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct buffer_head *bh;

	for(i = unlink_start; i < path_num_items(path); i++) {
		bh = path->p_node[i].bh;

		eb = (struct ocfs2_extent_block *)bh->b_data;
		/*
		 * Not all nodes might have had their final count
		 * decremented by the caller - handle this here.
		 */
		el = &eb->h_list;
		if (le16_to_cpu(el->l_next_free_rec) > 1) {
			mlog(ML_ERROR,
			     "Inode %llu, attempted to remove extent block "
			     "%llu with %u records\n",
			     (unsigned long long)OCFS2_I(inode)->ip_blkno,
			     (unsigned long long)le64_to_cpu(eb->h_blkno),
			     le16_to_cpu(el->l_next_free_rec));

			ocfs2_journal_dirty(handle, bh);
			ocfs2_remove_from_cache(inode, bh);
			continue;
		}

		el->l_next_free_rec = 0;
		memset(&el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));

		ocfs2_journal_dirty(handle, bh);

		ret = ocfs2_cache_extent_block_free(dealloc, eb);
		if (ret)
			mlog_errno(ret);

		ocfs2_remove_from_cache(inode, bh);
	}
}

static void ocfs2_unlink_subtree(struct inode *inode, handle_t *handle,
				 struct ocfs2_path *left_path,
				 struct ocfs2_path *right_path,
				 int subtree_index,
				 struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int i;
	struct buffer_head *root_bh = left_path->p_node[subtree_index].bh;
	struct ocfs2_extent_list *root_el = left_path->p_node[subtree_index].el;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_block *eb;

	el = path_leaf_el(left_path);

	eb = (struct ocfs2_extent_block *)right_path->p_node[subtree_index + 1].bh->b_data;

	for(i = 1; i < le16_to_cpu(root_el->l_next_free_rec); i++)
		if (root_el->l_recs[i].e_blkno == eb->h_blkno)
			break;

	BUG_ON(i >= le16_to_cpu(root_el->l_next_free_rec));

	memset(&root_el->l_recs[i], 0, sizeof(struct ocfs2_extent_rec));
	le16_add_cpu(&root_el->l_next_free_rec, -1);

	eb = (struct ocfs2_extent_block *)path_leaf_bh(left_path)->b_data;
	eb->h_next_leaf_blk = 0;

	ocfs2_journal_dirty(handle, root_bh);
	ocfs2_journal_dirty(handle, path_leaf_bh(left_path));

	ocfs2_unlink_path(inode, handle, dealloc, right_path,
			  subtree_index + 1);
}

static int ocfs2_rotate_subtree_left(struct inode *inode, handle_t *handle,
				     struct ocfs2_path *left_path,
				     struct ocfs2_path *right_path,
				     int subtree_index,
				     struct ocfs2_cached_dealloc_ctxt *dealloc,
				     int *deleted)
{
	int ret, i, del_right_subtree = 0, right_has_empty = 0;
	struct buffer_head *root_bh, *di_bh = path_root_bh(right_path);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_extent_list *right_leaf_el, *left_leaf_el;
	struct ocfs2_extent_block *eb;

	*deleted = 0;

	right_leaf_el = path_leaf_el(right_path);
	left_leaf_el = path_leaf_el(left_path);
	root_bh = left_path->p_node[subtree_index].bh;
	BUG_ON(root_bh != right_path->p_node[subtree_index].bh);

	if (!ocfs2_is_empty_extent(&left_leaf_el->l_recs[0]))
		return 0;

	eb = (struct ocfs2_extent_block *)path_leaf_bh(right_path)->b_data;
	if (ocfs2_is_empty_extent(&right_leaf_el->l_recs[0])) {
		/*
		 * It's legal for us to proceed if the right leaf is
		 * the rightmost one and it has an empty extent. There
		 * are two cases to handle - whether the leaf will be
		 * empty after removal or not. If the leaf isn't empty
		 * then just remove the empty extent up front. The
		 * next block will handle empty leaves by flagging
		 * them for unlink.
		 *
		 * Non rightmost leaves will throw -EAGAIN and the
		 * caller can manually move the subtree and retry.
		 */

		if (eb->h_next_leaf_blk != 0ULL)
			return -EAGAIN;

		if (le16_to_cpu(right_leaf_el->l_next_free_rec) > 1) {
			ret = ocfs2_journal_access(handle, inode,
						   path_leaf_bh(right_path),
						   OCFS2_JOURNAL_ACCESS_WRITE);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			ocfs2_remove_empty_extent(right_leaf_el);
		} else
			right_has_empty = 1;
	}

	if (eb->h_next_leaf_blk == 0ULL &&
	    le16_to_cpu(right_leaf_el->l_next_free_rec) == 1) {
		/*
		 * We have to update i_last_eb_blk during the meta
		 * data delete.
		 */
		ret = ocfs2_journal_access(handle, inode, di_bh,
					   OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		del_right_subtree = 1;
	}

	/*
	 * Getting here with an empty extent in the right path implies
	 * that it's the rightmost path and will be deleted.
	 */
	BUG_ON(right_has_empty && !del_right_subtree);

	ret = ocfs2_journal_access(handle, inode, root_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	for(i = subtree_index + 1; i < path_num_items(right_path); i++) {
		ret = ocfs2_journal_access(handle, inode,
					   right_path->p_node[i].bh,
					   OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_journal_access(handle, inode,
					   left_path->p_node[i].bh,
					   OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (!right_has_empty) {
		/*
		 * Only do this if we're moving a real
		 * record. Otherwise, the action is delayed until
		 * after removal of the right path in which case we
		 * can do a simple shift to remove the empty extent.
		 */
		ocfs2_rotate_leaf(left_leaf_el, &right_leaf_el->l_recs[0]);
		memset(&right_leaf_el->l_recs[0], 0,
		       sizeof(struct ocfs2_extent_rec));
	}
	if (eb->h_next_leaf_blk == 0ULL) {
		/*
		 * Move recs over to get rid of empty extent, decrease
		 * next_free. This is allowed to remove the last
		 * extent in our leaf (setting l_next_free_rec to
		 * zero) - the delete code below won't care.
		 */
		ocfs2_remove_empty_extent(right_leaf_el);
	}

	ret = ocfs2_journal_dirty(handle, path_leaf_bh(left_path));
	if (ret)
		mlog_errno(ret);
	ret = ocfs2_journal_dirty(handle, path_leaf_bh(right_path));
	if (ret)
		mlog_errno(ret);

	if (del_right_subtree) {
		ocfs2_unlink_subtree(inode, handle, left_path, right_path,
				     subtree_index, dealloc);
		ocfs2_update_edge_lengths(inode, handle, left_path);

		eb = (struct ocfs2_extent_block *)path_leaf_bh(left_path)->b_data;
		di->i_last_eb_blk = eb->h_blkno;

		/*
		 * Removal of the extent in the left leaf was skipped
		 * above so we could delete the right path
		 * 1st.
		 */
		if (right_has_empty)
			ocfs2_remove_empty_extent(left_leaf_el);

		ret = ocfs2_journal_dirty(handle, di_bh);
		if (ret)
			mlog_errno(ret);

		*deleted = 1;
	} else
		ocfs2_complete_edge_insert(inode, handle, left_path, right_path,
					   subtree_index);

out:
	return ret;
}

/*
 * Given a full path, determine what cpos value would return us a path
 * containing the leaf immediately to the right of the current one.
 *
 * Will return zero if the path passed in is already the rightmost path.
 *
 * This looks similar, but is subtly different to
 * ocfs2_find_cpos_for_left_leaf().
 */
static int ocfs2_find_cpos_for_right_leaf(struct super_block *sb,
					  struct ocfs2_path *path, u32 *cpos)
{
	int i, j, ret = 0;
	u64 blkno;
	struct ocfs2_extent_list *el;

	*cpos = 0;

	if (path->p_tree_depth == 0)
		return 0;

	blkno = path_leaf_bh(path)->b_blocknr;

	/* Start at the tree node just above the leaf and work our way up. */
	i = path->p_tree_depth - 1;
	while (i >= 0) {
		int next_free;

		el = path->p_node[i].el;

		/*
		 * Find the extent record just after the one in our
		 * path.
		 */
		next_free = le16_to_cpu(el->l_next_free_rec);
		for(j = 0; j < le16_to_cpu(el->l_next_free_rec); j++) {
			if (le64_to_cpu(el->l_recs[j].e_blkno) == blkno) {
				if (j == (next_free - 1)) {
					if (i == 0) {
						/*
						 * We've determined that the
						 * path specified is already
						 * the rightmost one - return a
						 * cpos of zero.
						 */
						goto out;
					}
					/*
					 * The rightmost record points to our
					 * leaf - we need to travel up the
					 * tree one level.
					 */
					goto next_node;
				}

				*cpos = le32_to_cpu(el->l_recs[j + 1].e_cpos);
				goto out;
			}
		}

		/*
		 * If we got here, we never found a valid node where
		 * the tree indicated one should be.
		 */
		ocfs2_error(sb,
			    "Invalid extent tree at extent block %llu\n",
			    (unsigned long long)blkno);
		ret = -EROFS;
		goto out;

next_node:
		blkno = path->p_node[i].bh->b_blocknr;
		i--;
	}

out:
	return ret;
}

static int ocfs2_rotate_rightmost_leaf_left(struct inode *inode,
					    handle_t *handle,
					    struct buffer_head *bh,
					    struct ocfs2_extent_list *el)
{
	int ret;

	if (!ocfs2_is_empty_extent(&el->l_recs[0]))
		return 0;

	ret = ocfs2_journal_access(handle, inode, bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_remove_empty_extent(el);

	ret = ocfs2_journal_dirty(handle, bh);
	if (ret)
		mlog_errno(ret);

out:
	return ret;
}

static int __ocfs2_rotate_tree_left(struct inode *inode,
				    handle_t *handle, int orig_credits,
				    struct ocfs2_path *path,
				    struct ocfs2_cached_dealloc_ctxt *dealloc,
				    struct ocfs2_path **empty_extent_path)
{
	int ret, subtree_root, deleted;
	u32 right_cpos;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_path *right_path = NULL;

	BUG_ON(!ocfs2_is_empty_extent(&(path_leaf_el(path)->l_recs[0])));

	*empty_extent_path = NULL;

	ret = ocfs2_find_cpos_for_right_leaf(inode->i_sb, path,
					     &right_cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	left_path = ocfs2_new_path(path_root_bh(path),
				   path_root_el(path));
	if (!left_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ocfs2_cp_path(left_path, path);

	right_path = ocfs2_new_path(path_root_bh(path),
				    path_root_el(path));
	if (!right_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	while (right_cpos) {
		ret = ocfs2_find_path(inode, right_path, right_cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		subtree_root = ocfs2_find_subtree_root(inode, left_path,
						       right_path);

		mlog(0, "Subtree root at index %d (blk %llu, depth %d)\n",
		     subtree_root,
		     (unsigned long long)
		     right_path->p_node[subtree_root].bh->b_blocknr,
		     right_path->p_tree_depth);

		ret = ocfs2_extend_rotate_transaction(handle, subtree_root,
						      orig_credits, left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_rotate_subtree_left(inode, handle, left_path,
						right_path, subtree_root,
						dealloc, &deleted);
		if (ret == -EAGAIN) {
			/*
			 * The rotation has to temporarily stop due to
			 * the right subtree having an empty
			 * extent. Pass it back to the caller for a
			 * fixup.
			 */
			*empty_extent_path = right_path;
			right_path = NULL;
			goto out;
		}
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * The subtree rotate might have removed records on
		 * the rightmost edge. If so, then rotation is
		 * complete.
		 */
		if (deleted)
			break;

		ocfs2_mv_path(left_path, right_path);

		ret = ocfs2_find_cpos_for_right_leaf(inode->i_sb, left_path,
						     &right_cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

out:
	ocfs2_free_path(right_path);
	ocfs2_free_path(left_path);

	return ret;
}

static int ocfs2_remove_rightmost_path(struct inode *inode, handle_t *handle,
				       struct ocfs2_path *path,
				       struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, subtree_index;
	u32 cpos;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;

	/*
	 * XXX: This code assumes that the root is an inode, which is
	 * true for now but may change as tree code gets generic.
	 */
	di = (struct ocfs2_dinode *)path_root_bh(path)->b_data;
	if (!OCFS2_IS_VALID_DINODE(di)) {
		ret = -EIO;
		ocfs2_error(inode->i_sb,
			    "Inode %llu has invalid path root",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno);
		goto out;
	}

	/*
	 * There's two ways we handle this depending on
	 * whether path is the only existing one.
	 */
	ret = ocfs2_extend_rotate_transaction(handle, 0,
					      handle->h_buffer_credits,
					      path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_path(inode, handle, path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_cpos_for_left_leaf(inode->i_sb, path, &cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (cpos) {
		/*
		 * We have a path to the left of this one - it needs
		 * an update too.
		 */
		left_path = ocfs2_new_path(path_root_bh(path),
					   path_root_el(path));
		if (!left_path) {
			ret = -ENOMEM;
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_find_path(inode, left_path, cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_journal_access_path(inode, handle, left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		subtree_index = ocfs2_find_subtree_root(inode, left_path, path);

		ocfs2_unlink_subtree(inode, handle, left_path, path,
				     subtree_index, dealloc);
		ocfs2_update_edge_lengths(inode, handle, left_path);

		eb = (struct ocfs2_extent_block *)path_leaf_bh(left_path)->b_data;
		di->i_last_eb_blk = eb->h_blkno;
	} else {
		/*
		 * 'path' is also the leftmost path which
		 * means it must be the only one. This gets
		 * handled differently because we want to
		 * revert the inode back to having extents
		 * in-line.
		 */
		ocfs2_unlink_path(inode, handle, dealloc, path, 1);

		el = &di->id2.i_list;
		el->l_tree_depth = 0;
		el->l_next_free_rec = 0;
		memset(&el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));

		di->i_last_eb_blk = 0;
	}

	ocfs2_journal_dirty(handle, path_root_bh(path));

out:
	ocfs2_free_path(left_path);
	return ret;
}

/*
 * Left rotation of btree records.
 *
 * In many ways, this is (unsurprisingly) the opposite of right
 * rotation. We start at some non-rightmost path containing an empty
 * extent in the leaf block. The code works its way to the rightmost
 * path by rotating records to the left in every subtree.
 *
 * This is used by any code which reduces the number of extent records
 * in a leaf. After removal, an empty record should be placed in the
 * leftmost list position.
 *
 * This won't handle a length update of the rightmost path records if
 * the rightmost tree leaf record is removed so the caller is
 * responsible for detecting and correcting that.
 */
static int ocfs2_rotate_tree_left(struct inode *inode, handle_t *handle,
				  struct ocfs2_path *path,
				  struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, orig_credits = handle->h_buffer_credits;
	struct ocfs2_path *tmp_path = NULL, *restart_path = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;

	el = path_leaf_el(path);
	if (!ocfs2_is_empty_extent(&el->l_recs[0]))
		return 0;

	if (path->p_tree_depth == 0) {
rightmost_no_delete:
		/*
		 * In-inode extents. This is trivially handled, so do
		 * it up front.
		 */
		ret = ocfs2_rotate_rightmost_leaf_left(inode, handle,
						       path_leaf_bh(path),
						       path_leaf_el(path));
		if (ret)
			mlog_errno(ret);
		goto out;
	}

	/*
	 * Handle rightmost branch now. There's several cases:
	 *  1) simple rotation leaving records in there. That's trivial.
	 *  2) rotation requiring a branch delete - there's no more
	 *     records left. Two cases of this:
	 *     a) There are branches to the left.
	 *     b) This is also the leftmost (the only) branch.
	 *
	 *  1) is handled via ocfs2_rotate_rightmost_leaf_left()
	 *  2a) we need the left branch so that we can update it with the unlink
	 *  2b) we need to bring the inode back to inline extents.
	 */

	eb = (struct ocfs2_extent_block *)path_leaf_bh(path)->b_data;
	el = &eb->h_list;
	if (eb->h_next_leaf_blk == 0) {
		/*
		 * This gets a bit tricky if we're going to delete the
		 * rightmost path. Get the other cases out of the way
		 * 1st.
		 */
		if (le16_to_cpu(el->l_next_free_rec) > 1)
			goto rightmost_no_delete;

		if (le16_to_cpu(el->l_next_free_rec) == 0) {
			ret = -EIO;
			ocfs2_error(inode->i_sb,
				    "Inode %llu has empty extent block at %llu",
				    (unsigned long long)OCFS2_I(inode)->ip_blkno,
				    (unsigned long long)le64_to_cpu(eb->h_blkno));
			goto out;
		}

		/*
		 * XXX: The caller can not trust "path" any more after
		 * this as it will have been deleted. What do we do?
		 *
		 * In theory the rotate-for-merge code will never get
		 * here because it'll always ask for a rotate in a
		 * nonempty list.
		 */

		ret = ocfs2_remove_rightmost_path(inode, handle, path,
						  dealloc);
		if (ret)
			mlog_errno(ret);
		goto out;
	}

	/*
	 * Now we can loop, remembering the path we get from -EAGAIN
	 * and restarting from there.
	 */
try_rotate:
	ret = __ocfs2_rotate_tree_left(inode, handle, orig_credits, path,
				       dealloc, &restart_path);
	if (ret && ret != -EAGAIN) {
		mlog_errno(ret);
		goto out;
	}

	while (ret == -EAGAIN) {
		tmp_path = restart_path;
		restart_path = NULL;

		ret = __ocfs2_rotate_tree_left(inode, handle, orig_credits,
					       tmp_path, dealloc,
					       &restart_path);
		if (ret && ret != -EAGAIN) {
			mlog_errno(ret);
			goto out;
		}

		ocfs2_free_path(tmp_path);
		tmp_path = NULL;

		if (ret == 0)
			goto try_rotate;
	}

out:
	ocfs2_free_path(tmp_path);
	ocfs2_free_path(restart_path);
	return ret;
}

static void ocfs2_cleanup_merge(struct ocfs2_extent_list *el,
				int index)
{
	struct ocfs2_extent_rec *rec = &el->l_recs[index];
	unsigned int size;

	if (rec->e_leaf_clusters == 0) {
		/*
		 * We consumed all of the merged-from record. An empty
		 * extent cannot exist anywhere but the 1st array
		 * position, so move things over if the merged-from
		 * record doesn't occupy that position.
		 *
		 * This creates a new empty extent so the caller
		 * should be smart enough to have removed any existing
		 * ones.
		 */
		if (index > 0) {
			BUG_ON(ocfs2_is_empty_extent(&el->l_recs[0]));
			size = index * sizeof(struct ocfs2_extent_rec);
			memmove(&el->l_recs[1], &el->l_recs[0], size);
		}

		/*
		 * Always memset - the caller doesn't check whether it
		 * created an empty extent, so there could be junk in
		 * the other fields.
		 */
		memset(&el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));
	}
}

/*
 * Remove split_rec clusters from the record at index and merge them
 * onto the beginning of the record at index + 1.
 */
static int ocfs2_merge_rec_right(struct inode *inode, struct buffer_head *bh,
				handle_t *handle,
				struct ocfs2_extent_rec *split_rec,
				struct ocfs2_extent_list *el, int index)
{
	int ret;
	unsigned int split_clusters = le16_to_cpu(split_rec->e_leaf_clusters);
	struct ocfs2_extent_rec *left_rec;
	struct ocfs2_extent_rec *right_rec;

	BUG_ON(index >= le16_to_cpu(el->l_next_free_rec));

	left_rec = &el->l_recs[index];
	right_rec = &el->l_recs[index + 1];

	ret = ocfs2_journal_access(handle, inode, bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	le16_add_cpu(&left_rec->e_leaf_clusters, -split_clusters);

	le32_add_cpu(&right_rec->e_cpos, -split_clusters);
	le64_add_cpu(&right_rec->e_blkno,
		     -ocfs2_clusters_to_blocks(inode->i_sb, split_clusters));
	le16_add_cpu(&right_rec->e_leaf_clusters, split_clusters);

	ocfs2_cleanup_merge(el, index);

	ret = ocfs2_journal_dirty(handle, bh);
	if (ret)
		mlog_errno(ret);

out:
	return ret;
}

/*
 * Remove split_rec clusters from the record at index and merge them
 * onto the tail of the record at index - 1.
 */
static int ocfs2_merge_rec_left(struct inode *inode, struct buffer_head *bh,
				handle_t *handle,
				struct ocfs2_extent_rec *split_rec,
				struct ocfs2_extent_list *el, int index)
{
	int ret, has_empty_extent = 0;
	unsigned int split_clusters = le16_to_cpu(split_rec->e_leaf_clusters);
	struct ocfs2_extent_rec *left_rec;
	struct ocfs2_extent_rec *right_rec;

	BUG_ON(index <= 0);

	left_rec = &el->l_recs[index - 1];
	right_rec = &el->l_recs[index];
	if (ocfs2_is_empty_extent(&el->l_recs[0]))
		has_empty_extent = 1;

	ret = ocfs2_journal_access(handle, inode, bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (has_empty_extent && index == 1) {
		/*
		 * The easy case - we can just plop the record right in.
		 */
		*left_rec = *split_rec;

		has_empty_extent = 0;
	} else {
		le16_add_cpu(&left_rec->e_leaf_clusters, split_clusters);
	}

	le32_add_cpu(&right_rec->e_cpos, split_clusters);
	le64_add_cpu(&right_rec->e_blkno,
		     ocfs2_clusters_to_blocks(inode->i_sb, split_clusters));
	le16_add_cpu(&right_rec->e_leaf_clusters, -split_clusters);

	ocfs2_cleanup_merge(el, index);

	ret = ocfs2_journal_dirty(handle, bh);
	if (ret)
		mlog_errno(ret);

out:
	return ret;
}

static int ocfs2_try_to_merge_extent(struct inode *inode,
				     handle_t *handle,
				     struct ocfs2_path *left_path,
				     int split_index,
				     struct ocfs2_extent_rec *split_rec,
				     struct ocfs2_cached_dealloc_ctxt *dealloc,
				     struct ocfs2_merge_ctxt *ctxt)

{
	int ret = 0, delete_tail_recs = 0;
	struct ocfs2_extent_list *el = path_leaf_el(left_path);
	struct ocfs2_extent_rec *rec = &el->l_recs[split_index];

	BUG_ON(ctxt->c_contig_type == CONTIG_NONE);

	if (ctxt->c_split_covers_rec) {
		delete_tail_recs++;

		if (ctxt->c_contig_type == CONTIG_LEFTRIGHT ||
		    ctxt->c_has_empty_extent)
			delete_tail_recs++;

		if (ctxt->c_has_empty_extent) {
			/*
			 * The merge code will need to create an empty
			 * extent to take the place of the newly
			 * emptied slot. Remove any pre-existing empty
			 * extents - having more than one in a leaf is
			 * illegal.
			 */
			ret = ocfs2_rotate_tree_left(inode, handle, left_path,
						     dealloc);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
			split_index--;
			rec = &el->l_recs[split_index];
		}
	}

	if (ctxt->c_contig_type == CONTIG_LEFTRIGHT) {
		/*
		 * Left-right contig implies this.
		 */
		BUG_ON(!ctxt->c_split_covers_rec);
		BUG_ON(split_index == 0);

		/*
		 * Since the leftright insert always covers the entire
		 * extent, this call will delete the insert record
		 * entirely, resulting in an empty extent record added to
		 * the extent block.
		 *
		 * Since the adding of an empty extent shifts
		 * everything back to the right, there's no need to
		 * update split_index here.
		 */
		ret = ocfs2_merge_rec_left(inode, path_leaf_bh(left_path),
					   handle, split_rec, el, split_index);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * We can only get this from logic error above.
		 */
		BUG_ON(!ocfs2_is_empty_extent(&el->l_recs[0]));

		/*
		 * The left merge left us with an empty extent, remove
		 * it.
		 */
		ret = ocfs2_rotate_tree_left(inode, handle, left_path, dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		split_index--;
		rec = &el->l_recs[split_index];

		/*
		 * Note that we don't pass split_rec here on purpose -
		 * we've merged it into the left side.
		 */
		ret = ocfs2_merge_rec_right(inode, path_leaf_bh(left_path),
					    handle, rec, el, split_index);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		BUG_ON(!ocfs2_is_empty_extent(&el->l_recs[0]));

		ret = ocfs2_rotate_tree_left(inode, handle, left_path,
					     dealloc);
		/*
		 * Error from this last rotate is not critical, so
		 * print but don't bubble it up.
		 */
		if (ret)
			mlog_errno(ret);
		ret = 0;
	} else {
		/*
		 * Merge a record to the left or right.
		 *
		 * 'contig_type' is relative to the existing record,
		 * so for example, if we're "right contig", it's to
		 * the record on the left (hence the left merge).
		 */
		if (ctxt->c_contig_type == CONTIG_RIGHT) {
			ret = ocfs2_merge_rec_left(inode,
						   path_leaf_bh(left_path),
						   handle, split_rec, el,
						   split_index);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		} else {
			ret = ocfs2_merge_rec_right(inode,
						    path_leaf_bh(left_path),
						    handle, split_rec, el,
						    split_index);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}

		if (ctxt->c_split_covers_rec) {
			/*
			 * The merge may have left an empty extent in
			 * our leaf. Try to rotate it away.
			 */
			ret = ocfs2_rotate_tree_left(inode, handle, left_path,
						     dealloc);
			if (ret)
				mlog_errno(ret);
			ret = 0;
		}
	}

out:
	return ret;
}

static void ocfs2_subtract_from_rec(struct super_block *sb,
				    enum ocfs2_split_type split,
				    struct ocfs2_extent_rec *rec,
				    struct ocfs2_extent_rec *split_rec)
{
	u64 len_blocks;

	len_blocks = ocfs2_clusters_to_blocks(sb,
				le16_to_cpu(split_rec->e_leaf_clusters));

	if (split == SPLIT_LEFT) {
		/*
		 * Region is on the left edge of the existing
		 * record.
		 */
		le32_add_cpu(&rec->e_cpos,
			     le16_to_cpu(split_rec->e_leaf_clusters));
		le64_add_cpu(&rec->e_blkno, len_blocks);
		le16_add_cpu(&rec->e_leaf_clusters,
			     -le16_to_cpu(split_rec->e_leaf_clusters));
	} else {
		/*
		 * Region is on the right edge of the existing
		 * record.
		 */
		le16_add_cpu(&rec->e_leaf_clusters,
			     -le16_to_cpu(split_rec->e_leaf_clusters));
	}
}

/*
 * Do the final bits of extent record insertion at the target leaf
 * list. If this leaf is part of an allocation tree, it is assumed
 * that the tree above has been prepared.
 */
static void ocfs2_insert_at_leaf(struct ocfs2_extent_rec *insert_rec,
				 struct ocfs2_extent_list *el,
				 struct ocfs2_insert_type *insert,
				 struct inode *inode)
{
	int i = insert->ins_contig_index;
	unsigned int range;
	struct ocfs2_extent_rec *rec;

	BUG_ON(le16_to_cpu(el->l_tree_depth) != 0);

	if (insert->ins_split != SPLIT_NONE) {
		i = ocfs2_search_extent_list(el, le32_to_cpu(insert_rec->e_cpos));
		BUG_ON(i == -1);
		rec = &el->l_recs[i];
		ocfs2_subtract_from_rec(inode->i_sb, insert->ins_split, rec,
					insert_rec);
		goto rotate;
	}

	/*
	 * Contiguous insert - either left or right.
	 */
	if (insert->ins_contig != CONTIG_NONE) {
		rec = &el->l_recs[i];
		if (insert->ins_contig == CONTIG_LEFT) {
			rec->e_blkno = insert_rec->e_blkno;
			rec->e_cpos = insert_rec->e_cpos;
		}
		le16_add_cpu(&rec->e_leaf_clusters,
			     le16_to_cpu(insert_rec->e_leaf_clusters));
		return;
	}

	/*
	 * Handle insert into an empty leaf.
	 */
	if (le16_to_cpu(el->l_next_free_rec) == 0 ||
	    ((le16_to_cpu(el->l_next_free_rec) == 1) &&
	     ocfs2_is_empty_extent(&el->l_recs[0]))) {
		el->l_recs[0] = *insert_rec;
		el->l_next_free_rec = cpu_to_le16(1);
		return;
	}

	/*
	 * Appending insert.
	 */
	if (insert->ins_appending == APPEND_TAIL) {
		i = le16_to_cpu(el->l_next_free_rec) - 1;
		rec = &el->l_recs[i];
		range = le32_to_cpu(rec->e_cpos)
			+ le16_to_cpu(rec->e_leaf_clusters);
		BUG_ON(le32_to_cpu(insert_rec->e_cpos) < range);

		mlog_bug_on_msg(le16_to_cpu(el->l_next_free_rec) >=
				le16_to_cpu(el->l_count),
				"inode %lu, depth %u, count %u, next free %u, "
				"rec.cpos %u, rec.clusters %u, "
				"insert.cpos %u, insert.clusters %u\n",
				inode->i_ino,
				le16_to_cpu(el->l_tree_depth),
				le16_to_cpu(el->l_count),
				le16_to_cpu(el->l_next_free_rec),
				le32_to_cpu(el->l_recs[i].e_cpos),
				le16_to_cpu(el->l_recs[i].e_leaf_clusters),
				le32_to_cpu(insert_rec->e_cpos),
				le16_to_cpu(insert_rec->e_leaf_clusters));
		i++;
		el->l_recs[i] = *insert_rec;
		le16_add_cpu(&el->l_next_free_rec, 1);
		return;
	}

rotate:
	/*
	 * Ok, we have to rotate.
	 *
	 * At this point, it is safe to assume that inserting into an
	 * empty leaf and appending to a leaf have both been handled
	 * above.
	 *
	 * This leaf needs to have space, either by the empty 1st
	 * extent record, or by virtue of an l_next_rec < l_count.
	 */
	ocfs2_rotate_leaf(el, insert_rec);
}

static inline void ocfs2_update_dinode_clusters(struct inode *inode,
						struct ocfs2_dinode *di,
						u32 clusters)
{
	le32_add_cpu(&di->i_clusters, clusters);
	spin_lock(&OCFS2_I(inode)->ip_lock);
	OCFS2_I(inode)->ip_clusters = le32_to_cpu(di->i_clusters);
	spin_unlock(&OCFS2_I(inode)->ip_lock);
}

static void ocfs2_adjust_rightmost_records(struct inode *inode,
					   handle_t *handle,
					   struct ocfs2_path *path,
					   struct ocfs2_extent_rec *insert_rec)
{
	int ret, i, next_free;
	struct buffer_head *bh;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;

	/*
	 * Update everything except the leaf block.
	 */
	for (i = 0; i < path->p_tree_depth; i++) {
		bh = path->p_node[i].bh;
		el = path->p_node[i].el;

		next_free = le16_to_cpu(el->l_next_free_rec);
		if (next_free == 0) {
			ocfs2_error(inode->i_sb,
				    "Dinode %llu has a bad extent list",
				    (unsigned long long)OCFS2_I(inode)->ip_blkno);
			ret = -EIO;
			return;
		}

		rec = &el->l_recs[next_free - 1];

		rec->e_int_clusters = insert_rec->e_cpos;
		le32_add_cpu(&rec->e_int_clusters,
			     le16_to_cpu(insert_rec->e_leaf_clusters));
		le32_add_cpu(&rec->e_int_clusters,
			     -le32_to_cpu(rec->e_cpos));

		ret = ocfs2_journal_dirty(handle, bh);
		if (ret)
			mlog_errno(ret);

	}
}

static int ocfs2_append_rec_to_path(struct inode *inode, handle_t *handle,
				    struct ocfs2_extent_rec *insert_rec,
				    struct ocfs2_path *right_path,
				    struct ocfs2_path **ret_left_path)
{
	int ret, next_free;
	struct ocfs2_extent_list *el;
	struct ocfs2_path *left_path = NULL;

	*ret_left_path = NULL;

	/*
	 * This shouldn't happen for non-trees. The extent rec cluster
	 * count manipulation below only works for interior nodes.
	 */
	BUG_ON(right_path->p_tree_depth == 0);

	/*
	 * If our appending insert is at the leftmost edge of a leaf,
	 * then we might need to update the rightmost records of the
	 * neighboring path.
	 */
	el = path_leaf_el(right_path);
	next_free = le16_to_cpu(el->l_next_free_rec);
	if (next_free == 0 ||
	    (next_free == 1 && ocfs2_is_empty_extent(&el->l_recs[0]))) {
		u32 left_cpos;

		ret = ocfs2_find_cpos_for_left_leaf(inode->i_sb, right_path,
						    &left_cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		mlog(0, "Append may need a left path update. cpos: %u, "
		     "left_cpos: %u\n", le32_to_cpu(insert_rec->e_cpos),
		     left_cpos);

		/*
		 * No need to worry if the append is already in the
		 * leftmost leaf.
		 */
		if (left_cpos) {
			left_path = ocfs2_new_path(path_root_bh(right_path),
						   path_root_el(right_path));
			if (!left_path) {
				ret = -ENOMEM;
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_find_path(inode, left_path, left_cpos);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			/*
			 * ocfs2_insert_path() will pass the left_path to the
			 * journal for us.
			 */
		}
	}

	ret = ocfs2_journal_access_path(inode, handle, right_path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_adjust_rightmost_records(inode, handle, right_path, insert_rec);

	*ret_left_path = left_path;
	ret = 0;
out:
	if (ret != 0)
		ocfs2_free_path(left_path);

	return ret;
}

static void ocfs2_split_record(struct inode *inode,
			       struct ocfs2_path *left_path,
			       struct ocfs2_path *right_path,
			       struct ocfs2_extent_rec *split_rec,
			       enum ocfs2_split_type split)
{
	int index;
	u32 cpos = le32_to_cpu(split_rec->e_cpos);
	struct ocfs2_extent_list *left_el = NULL, *right_el, *insert_el, *el;
	struct ocfs2_extent_rec *rec, *tmprec;

	right_el = path_leaf_el(right_path);;
	if (left_path)
		left_el = path_leaf_el(left_path);

	el = right_el;
	insert_el = right_el;
	index = ocfs2_search_extent_list(el, cpos);
	if (index != -1) {
		if (index == 0 && left_path) {
			BUG_ON(ocfs2_is_empty_extent(&el->l_recs[0]));

			/*
			 * This typically means that the record
			 * started in the left path but moved to the
			 * right as a result of rotation. We either
			 * move the existing record to the left, or we
			 * do the later insert there.
			 *
			 * In this case, the left path should always
			 * exist as the rotate code will have passed
			 * it back for a post-insert update.
			 */

			if (split == SPLIT_LEFT) {
				/*
				 * It's a left split. Since we know
				 * that the rotate code gave us an
				 * empty extent in the left path, we
				 * can just do the insert there.
				 */
				insert_el = left_el;
			} else {
				/*
				 * Right split - we have to move the
				 * existing record over to the left
				 * leaf. The insert will be into the
				 * newly created empty extent in the
				 * right leaf.
				 */
				tmprec = &right_el->l_recs[index];
				ocfs2_rotate_leaf(left_el, tmprec);
				el = left_el;

				memset(tmprec, 0, sizeof(*tmprec));
				index = ocfs2_search_extent_list(left_el, cpos);
				BUG_ON(index == -1);
			}
		}
	} else {
		BUG_ON(!left_path);
		BUG_ON(!ocfs2_is_empty_extent(&left_el->l_recs[0]));
		/*
		 * Left path is easy - we can just allow the insert to
		 * happen.
		 */
		el = left_el;
		insert_el = left_el;
		index = ocfs2_search_extent_list(el, cpos);
		BUG_ON(index == -1);
	}

	rec = &el->l_recs[index];
	ocfs2_subtract_from_rec(inode->i_sb, split, rec, split_rec);
	ocfs2_rotate_leaf(insert_el, split_rec);
}

/*
 * This function only does inserts on an allocation b-tree. For dinode
 * lists, ocfs2_insert_at_leaf() is called directly.
 *
 * right_path is the path we want to do the actual insert
 * in. left_path should only be passed in if we need to update that
 * portion of the tree after an edge insert.
 */
static int ocfs2_insert_path(struct inode *inode,
			     handle_t *handle,
			     struct ocfs2_path *left_path,
			     struct ocfs2_path *right_path,
			     struct ocfs2_extent_rec *insert_rec,
			     struct ocfs2_insert_type *insert)
{
	int ret, subtree_index;
	struct buffer_head *leaf_bh = path_leaf_bh(right_path);

	/*
	 * Pass both paths to the journal. The majority of inserts
	 * will be touching all components anyway.
	 */
	ret = ocfs2_journal_access_path(inode, handle, right_path);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	if (left_path) {
		int credits = handle->h_buffer_credits;

		/*
		 * There's a chance that left_path got passed back to
		 * us without being accounted for in the
		 * journal. Extend our transaction here to be sure we
		 * can change those blocks.
		 */
		credits += left_path->p_tree_depth;

		ret = ocfs2_extend_trans(handle, credits);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_journal_access_path(inode, handle, left_path);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (insert->ins_split != SPLIT_NONE) {
		/*
		 * We could call ocfs2_insert_at_leaf() for some types
		 * of splits, but it's easier to just let one seperate
		 * function sort it all out.
		 */
		ocfs2_split_record(inode, left_path, right_path,
				   insert_rec, insert->ins_split);
	} else
		ocfs2_insert_at_leaf(insert_rec, path_leaf_el(right_path),
				     insert, inode);

	ret = ocfs2_journal_dirty(handle, leaf_bh);
	if (ret)
		mlog_errno(ret);

	if (left_path) {
		/*
		 * The rotate code has indicated that we need to fix
		 * up portions of the tree after the insert.
		 *
		 * XXX: Should we extend the transaction here?
		 */
		subtree_index = ocfs2_find_subtree_root(inode, left_path,
							right_path);
		ocfs2_complete_edge_insert(inode, handle, left_path,
					   right_path, subtree_index);
	}

	ret = 0;
out:
	return ret;
}

static int ocfs2_do_insert_extent(struct inode *inode,
				  handle_t *handle,
				  struct buffer_head *di_bh,
				  struct ocfs2_extent_rec *insert_rec,
				  struct ocfs2_insert_type *type)
{
	int ret, rotate = 0;
	u32 cpos;
	struct ocfs2_path *right_path = NULL;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_list *el;

	di = (struct ocfs2_dinode *) di_bh->b_data;
	el = &di->id2.i_list;

	ret = ocfs2_journal_access(handle, inode, di_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (le16_to_cpu(el->l_tree_depth) == 0) {
		ocfs2_insert_at_leaf(insert_rec, el, type, inode);
		goto out_update_clusters;
	}

	right_path = ocfs2_new_inode_path(di_bh);
	if (!right_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Determine the path to start with. Rotations need the
	 * rightmost path, everything else can go directly to the
	 * target leaf.
	 */
	cpos = le32_to_cpu(insert_rec->e_cpos);
	if (type->ins_appending == APPEND_NONE &&
	    type->ins_contig == CONTIG_NONE) {
		rotate = 1;
		cpos = UINT_MAX;
	}

	ret = ocfs2_find_path(inode, right_path, cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Rotations and appends need special treatment - they modify
	 * parts of the tree's above them.
	 *
	 * Both might pass back a path immediate to the left of the
	 * one being inserted to. This will be cause
	 * ocfs2_insert_path() to modify the rightmost records of
	 * left_path to account for an edge insert.
	 *
	 * XXX: When modifying this code, keep in mind that an insert
	 * can wind up skipping both of these two special cases...
	 */
	if (rotate) {
		ret = ocfs2_rotate_tree_right(inode, handle, type->ins_split,
					      le32_to_cpu(insert_rec->e_cpos),
					      right_path, &left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	} else if (type->ins_appending == APPEND_TAIL
		   && type->ins_contig != CONTIG_LEFT) {
		ret = ocfs2_append_rec_to_path(inode, handle, insert_rec,
					       right_path, &left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	ret = ocfs2_insert_path(inode, handle, left_path, right_path,
				insert_rec, type);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

out_update_clusters:
	if (type->ins_split == SPLIT_NONE)
		ocfs2_update_dinode_clusters(inode, di,
					     le16_to_cpu(insert_rec->e_leaf_clusters));

	ret = ocfs2_journal_dirty(handle, di_bh);
	if (ret)
		mlog_errno(ret);

out:
	ocfs2_free_path(left_path);
	ocfs2_free_path(right_path);

	return ret;
}

static enum ocfs2_contig_type
ocfs2_figure_merge_contig_type(struct inode *inode,
			       struct ocfs2_extent_list *el, int index,
			       struct ocfs2_extent_rec *split_rec)
{
	struct ocfs2_extent_rec *rec;
	enum ocfs2_contig_type ret = CONTIG_NONE;

	/*
	 * We're careful to check for an empty extent record here -
	 * the merge code will know what to do if it sees one.
	 */

	if (index > 0) {
		rec = &el->l_recs[index - 1];
		if (index == 1 && ocfs2_is_empty_extent(rec)) {
			if (split_rec->e_cpos == el->l_recs[index].e_cpos)
				ret = CONTIG_RIGHT;
		} else {
			ret = ocfs2_extent_contig(inode, rec, split_rec);
		}
	}

	if (index < (le16_to_cpu(el->l_next_free_rec) - 1)) {
		enum ocfs2_contig_type contig_type;

		rec = &el->l_recs[index + 1];
		contig_type = ocfs2_extent_contig(inode, rec, split_rec);

		if (contig_type == CONTIG_LEFT && ret == CONTIG_RIGHT)
			ret = CONTIG_LEFTRIGHT;
		else if (ret == CONTIG_NONE)
			ret = contig_type;
	}

	return ret;
}

static void ocfs2_figure_contig_type(struct inode *inode,
				     struct ocfs2_insert_type *insert,
				     struct ocfs2_extent_list *el,
				     struct ocfs2_extent_rec *insert_rec)
{
	int i;
	enum ocfs2_contig_type contig_type = CONTIG_NONE;

	BUG_ON(le16_to_cpu(el->l_tree_depth) != 0);

	for(i = 0; i < le16_to_cpu(el->l_next_free_rec); i++) {
		contig_type = ocfs2_extent_contig(inode, &el->l_recs[i],
						  insert_rec);
		if (contig_type != CONTIG_NONE) {
			insert->ins_contig_index = i;
			break;
		}
	}
	insert->ins_contig = contig_type;
}

/*
 * This should only be called against the righmost leaf extent list.
 *
 * ocfs2_figure_appending_type() will figure out whether we'll have to
 * insert at the tail of the rightmost leaf.
 *
 * This should also work against the dinode list for tree's with 0
 * depth. If we consider the dinode list to be the rightmost leaf node
 * then the logic here makes sense.
 */
static void ocfs2_figure_appending_type(struct ocfs2_insert_type *insert,
					struct ocfs2_extent_list *el,
					struct ocfs2_extent_rec *insert_rec)
{
	int i;
	u32 cpos = le32_to_cpu(insert_rec->e_cpos);
	struct ocfs2_extent_rec *rec;

	insert->ins_appending = APPEND_NONE;

	BUG_ON(le16_to_cpu(el->l_tree_depth) != 0);

	if (!el->l_next_free_rec)
		goto set_tail_append;

	if (ocfs2_is_empty_extent(&el->l_recs[0])) {
		/* Were all records empty? */
		if (le16_to_cpu(el->l_next_free_rec) == 1)
			goto set_tail_append;
	}

	i = le16_to_cpu(el->l_next_free_rec) - 1;
	rec = &el->l_recs[i];

	if (cpos >=
	    (le32_to_cpu(rec->e_cpos) + le16_to_cpu(rec->e_leaf_clusters)))
		goto set_tail_append;

	return;

set_tail_append:
	insert->ins_appending = APPEND_TAIL;
}

/*
 * Helper function called at the begining of an insert.
 *
 * This computes a few things that are commonly used in the process of
 * inserting into the btree:
 *   - Whether the new extent is contiguous with an existing one.
 *   - The current tree depth.
 *   - Whether the insert is an appending one.
 *   - The total # of free records in the tree.
 *
 * All of the information is stored on the ocfs2_insert_type
 * structure.
 */
static int ocfs2_figure_insert_type(struct inode *inode,
				    struct buffer_head *di_bh,
				    struct buffer_head **last_eb_bh,
				    struct ocfs2_extent_rec *insert_rec,
				    struct ocfs2_insert_type *insert)
{
	int ret;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct ocfs2_path *path = NULL;
	struct buffer_head *bh = NULL;

	insert->ins_split = SPLIT_NONE;

	el = &di->id2.i_list;
	insert->ins_tree_depth = le16_to_cpu(el->l_tree_depth);

	if (el->l_tree_depth) {
		/*
		 * If we have tree depth, we read in the
		 * rightmost extent block ahead of time as
		 * ocfs2_figure_insert_type() and ocfs2_add_branch()
		 * may want it later.
		 */
		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				       le64_to_cpu(di->i_last_eb_blk), &bh,
				       OCFS2_BH_CACHED, inode);
		if (ret) {
			mlog_exit(ret);
			goto out;
		}
		eb = (struct ocfs2_extent_block *) bh->b_data;
		el = &eb->h_list;
	}

	/*
	 * Unless we have a contiguous insert, we'll need to know if
	 * there is room left in our allocation tree for another
	 * extent record.
	 *
	 * XXX: This test is simplistic, we can search for empty
	 * extent records too.
	 */
	insert->ins_free_records = le16_to_cpu(el->l_count) -
		le16_to_cpu(el->l_next_free_rec);

	if (!insert->ins_tree_depth) {
		ocfs2_figure_contig_type(inode, insert, el, insert_rec);
		ocfs2_figure_appending_type(insert, el, insert_rec);
		return 0;
	}

	path = ocfs2_new_inode_path(di_bh);
	if (!path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	/*
	 * In the case that we're inserting past what the tree
	 * currently accounts for, ocfs2_find_path() will return for
	 * us the rightmost tree path. This is accounted for below in
	 * the appending code.
	 */
	ret = ocfs2_find_path(inode, path, le32_to_cpu(insert_rec->e_cpos));
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	el = path_leaf_el(path);

	/*
	 * Now that we have the path, there's two things we want to determine:
	 * 1) Contiguousness (also set contig_index if this is so)
	 *
	 * 2) Are we doing an append? We can trivially break this up
         *     into two types of appends: simple record append, or a
         *     rotate inside the tail leaf.
	 */
	ocfs2_figure_contig_type(inode, insert, el, insert_rec);

	/*
	 * The insert code isn't quite ready to deal with all cases of
	 * left contiguousness. Specifically, if it's an insert into
	 * the 1st record in a leaf, it will require the adjustment of
	 * cluster count on the last record of the path directly to it's
	 * left. For now, just catch that case and fool the layers
	 * above us. This works just fine for tree_depth == 0, which
	 * is why we allow that above.
	 */
	if (insert->ins_contig == CONTIG_LEFT &&
	    insert->ins_contig_index == 0)
		insert->ins_contig = CONTIG_NONE;

	/*
	 * Ok, so we can simply compare against last_eb to figure out
	 * whether the path doesn't exist. This will only happen in
	 * the case that we're doing a tail append, so maybe we can
	 * take advantage of that information somehow.
	 */
	if (le64_to_cpu(di->i_last_eb_blk) == path_leaf_bh(path)->b_blocknr) {
		/*
		 * Ok, ocfs2_find_path() returned us the rightmost
		 * tree path. This might be an appending insert. There are
		 * two cases:
		 *    1) We're doing a true append at the tail:
		 *	-This might even be off the end of the leaf
		 *    2) We're "appending" by rotating in the tail
		 */
		ocfs2_figure_appending_type(insert, el, insert_rec);
	}

out:
	ocfs2_free_path(path);

	if (ret == 0)
		*last_eb_bh = bh;
	else
		brelse(bh);
	return ret;
}

/*
 * Insert an extent into an inode btree.
 *
 * The caller needs to update fe->i_clusters
 */
int ocfs2_insert_extent(struct ocfs2_super *osb,
			handle_t *handle,
			struct inode *inode,
			struct buffer_head *fe_bh,
			u32 cpos,
			u64 start_blk,
			u32 new_clusters,
			u8 flags,
			struct ocfs2_alloc_context *meta_ac)
{
	int status;
	struct buffer_head *last_eb_bh = NULL;
	struct buffer_head *bh = NULL;
	struct ocfs2_insert_type insert = {0, };
	struct ocfs2_extent_rec rec;

	mlog(0, "add %u clusters at position %u to inode %llu\n",
	     new_clusters, cpos, (unsigned long long)OCFS2_I(inode)->ip_blkno);

	mlog_bug_on_msg(!ocfs2_sparse_alloc(osb) &&
			(OCFS2_I(inode)->ip_clusters != cpos),
			"Device %s, asking for sparse allocation: inode %llu, "
			"cpos %u, clusters %u\n",
			osb->dev_str,
			(unsigned long long)OCFS2_I(inode)->ip_blkno, cpos,
			OCFS2_I(inode)->ip_clusters);

	memset(&rec, 0, sizeof(rec));
	rec.e_cpos = cpu_to_le32(cpos);
	rec.e_blkno = cpu_to_le64(start_blk);
	rec.e_leaf_clusters = cpu_to_le16(new_clusters);
	rec.e_flags = flags;

	status = ocfs2_figure_insert_type(inode, fe_bh, &last_eb_bh, &rec,
					  &insert);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	mlog(0, "Insert.appending: %u, Insert.Contig: %u, "
	     "Insert.contig_index: %d, Insert.free_records: %d, "
	     "Insert.tree_depth: %d\n",
	     insert.ins_appending, insert.ins_contig, insert.ins_contig_index,
	     insert.ins_free_records, insert.ins_tree_depth);

	if (insert.ins_contig == CONTIG_NONE && insert.ins_free_records == 0) {
		status = ocfs2_grow_tree(inode, handle, fe_bh,
					 &insert.ins_tree_depth, &last_eb_bh,
					 meta_ac);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
	}

	/* Finally, we can add clusters. This might rotate the tree for us. */
	status = ocfs2_do_insert_extent(inode, handle, fe_bh, &rec, &insert);
	if (status < 0)
		mlog_errno(status);
	else
		ocfs2_extent_map_insert_rec(inode, &rec);

bail:
	if (bh)
		brelse(bh);

	if (last_eb_bh)
		brelse(last_eb_bh);

	mlog_exit(status);
	return status;
}

static void ocfs2_make_right_split_rec(struct super_block *sb,
				       struct ocfs2_extent_rec *split_rec,
				       u32 cpos,
				       struct ocfs2_extent_rec *rec)
{
	u32 rec_cpos = le32_to_cpu(rec->e_cpos);
	u32 rec_range = rec_cpos + le16_to_cpu(rec->e_leaf_clusters);

	memset(split_rec, 0, sizeof(struct ocfs2_extent_rec));

	split_rec->e_cpos = cpu_to_le32(cpos);
	split_rec->e_leaf_clusters = cpu_to_le16(rec_range - cpos);

	split_rec->e_blkno = rec->e_blkno;
	le64_add_cpu(&split_rec->e_blkno,
		     ocfs2_clusters_to_blocks(sb, cpos - rec_cpos));

	split_rec->e_flags = rec->e_flags;
}

static int ocfs2_split_and_insert(struct inode *inode,
				  handle_t *handle,
				  struct ocfs2_path *path,
				  struct buffer_head *di_bh,
				  struct buffer_head **last_eb_bh,
				  int split_index,
				  struct ocfs2_extent_rec *orig_split_rec,
				  struct ocfs2_alloc_context *meta_ac)
{
	int ret = 0, depth;
	unsigned int insert_range, rec_range, do_leftright = 0;
	struct ocfs2_extent_rec tmprec;
	struct ocfs2_extent_list *rightmost_el;
	struct ocfs2_extent_rec rec;
	struct ocfs2_extent_rec split_rec = *orig_split_rec;
	struct ocfs2_insert_type insert;
	struct ocfs2_extent_block *eb;
	struct ocfs2_dinode *di;

leftright:
	/*
	 * Store a copy of the record on the stack - it might move
	 * around as the tree is manipulated below.
	 */
	rec = path_leaf_el(path)->l_recs[split_index];

	di = (struct ocfs2_dinode *)di_bh->b_data;
	rightmost_el = &di->id2.i_list;

	depth = le16_to_cpu(rightmost_el->l_tree_depth);
	if (depth) {
		BUG_ON(!(*last_eb_bh));
		eb = (struct ocfs2_extent_block *) (*last_eb_bh)->b_data;
		rightmost_el = &eb->h_list;
	}

	if (le16_to_cpu(rightmost_el->l_next_free_rec) ==
	    le16_to_cpu(rightmost_el->l_count)) {
		int old_depth = depth;

		ret = ocfs2_grow_tree(inode, handle, di_bh, &depth, last_eb_bh,
				      meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (old_depth != depth) {
			eb = (struct ocfs2_extent_block *)(*last_eb_bh)->b_data;
			rightmost_el = &eb->h_list;
		}
	}

	memset(&insert, 0, sizeof(struct ocfs2_insert_type));
	insert.ins_appending = APPEND_NONE;
	insert.ins_contig = CONTIG_NONE;
	insert.ins_free_records = le16_to_cpu(rightmost_el->l_count)
		- le16_to_cpu(rightmost_el->l_next_free_rec);
	insert.ins_tree_depth = depth;

	insert_range = le32_to_cpu(split_rec.e_cpos) +
		le16_to_cpu(split_rec.e_leaf_clusters);
	rec_range = le32_to_cpu(rec.e_cpos) +
		le16_to_cpu(rec.e_leaf_clusters);

	if (split_rec.e_cpos == rec.e_cpos) {
		insert.ins_split = SPLIT_LEFT;
	} else if (insert_range == rec_range) {
		insert.ins_split = SPLIT_RIGHT;
	} else {
		/*
		 * Left/right split. We fake this as a right split
		 * first and then make a second pass as a left split.
		 */
		insert.ins_split = SPLIT_RIGHT;

		ocfs2_make_right_split_rec(inode->i_sb, &tmprec, insert_range,
					   &rec);

		split_rec = tmprec;

		BUG_ON(do_leftright);
		do_leftright = 1;
	}

	ret = ocfs2_do_insert_extent(inode, handle, di_bh, &split_rec,
				     &insert);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (do_leftright == 1) {
		u32 cpos;
		struct ocfs2_extent_list *el;

		do_leftright++;
		split_rec = *orig_split_rec;

		ocfs2_reinit_path(path, 1);

		cpos = le32_to_cpu(split_rec.e_cpos);
		ret = ocfs2_find_path(inode, path, cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		el = path_leaf_el(path);
		split_index = ocfs2_search_extent_list(el, cpos);
		goto leftright;
	}
out:

	return ret;
}

/*
 * Mark part or all of the extent record at split_index in the leaf
 * pointed to by path as written. This removes the unwritten
 * extent flag.
 *
 * Care is taken to handle contiguousness so as to not grow the tree.
 *
 * meta_ac is not strictly necessary - we only truly need it if growth
 * of the tree is required. All other cases will degrade into a less
 * optimal tree layout.
 *
 * last_eb_bh should be the rightmost leaf block for any inode with a
 * btree. Since a split may grow the tree or a merge might shrink it, the caller cannot trust the contents of that buffer after this call.
 *
 * This code is optimized for readability - several passes might be
 * made over certain portions of the tree. All of those blocks will
 * have been brought into cache (and pinned via the journal), so the
 * extra overhead is not expressed in terms of disk reads.
 */
static int __ocfs2_mark_extent_written(struct inode *inode,
				       struct buffer_head *di_bh,
				       handle_t *handle,
				       struct ocfs2_path *path,
				       int split_index,
				       struct ocfs2_extent_rec *split_rec,
				       struct ocfs2_alloc_context *meta_ac,
				       struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret = 0;
	struct ocfs2_extent_list *el = path_leaf_el(path);
	struct buffer_head *eb_bh, *last_eb_bh = NULL;
	struct ocfs2_extent_rec *rec = &el->l_recs[split_index];
	struct ocfs2_merge_ctxt ctxt;
	struct ocfs2_extent_list *rightmost_el;

	if (!rec->e_flags & OCFS2_EXT_UNWRITTEN) {
		ret = -EIO;
		mlog_errno(ret);
		goto out;
	}

	if (le32_to_cpu(rec->e_cpos) > le32_to_cpu(split_rec->e_cpos) ||
	    ((le32_to_cpu(rec->e_cpos) + le16_to_cpu(rec->e_leaf_clusters)) <
	     (le32_to_cpu(split_rec->e_cpos) + le16_to_cpu(split_rec->e_leaf_clusters)))) {
		ret = -EIO;
		mlog_errno(ret);
		goto out;
	}

	eb_bh = path_leaf_bh(path);
	ret = ocfs2_journal_access(handle, inode, eb_bh,
				   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ctxt.c_contig_type = ocfs2_figure_merge_contig_type(inode, el,
							    split_index,
							    split_rec);

	/*
	 * The core merge / split code wants to know how much room is
	 * left in this inodes allocation tree, so we pass the
	 * rightmost extent list.
	 */
	if (path->p_tree_depth) {
		struct ocfs2_extent_block *eb;
		struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;

		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				       le64_to_cpu(di->i_last_eb_blk),
				       &last_eb_bh, OCFS2_BH_CACHED, inode);
		if (ret) {
			mlog_exit(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
		if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
			ret = -EROFS;
			goto out;
		}

		rightmost_el = &eb->h_list;
	} else
		rightmost_el = path_root_el(path);

	ctxt.c_used_tail_recs = le16_to_cpu(rightmost_el->l_next_free_rec);
	if (ctxt.c_used_tail_recs > 0 &&
	    ocfs2_is_empty_extent(&rightmost_el->l_recs[0]))
		ctxt.c_used_tail_recs--;

	if (rec->e_cpos == split_rec->e_cpos &&
	    rec->e_leaf_clusters == split_rec->e_leaf_clusters)
		ctxt.c_split_covers_rec = 1;
	else
		ctxt.c_split_covers_rec = 0;

	ctxt.c_has_empty_extent = ocfs2_is_empty_extent(&el->l_recs[0]);

	mlog(0, "index: %d, contig: %u, used_tail_recs: %u, "
	     "has_empty: %u, split_covers: %u\n", split_index,
	     ctxt.c_contig_type, ctxt.c_used_tail_recs,
	     ctxt.c_has_empty_extent, ctxt.c_split_covers_rec);

	if (ctxt.c_contig_type == CONTIG_NONE) {
		if (ctxt.c_split_covers_rec)
			el->l_recs[split_index] = *split_rec;
		else
			ret = ocfs2_split_and_insert(inode, handle, path, di_bh,
						     &last_eb_bh, split_index,
						     split_rec, meta_ac);
		if (ret)
			mlog_errno(ret);
	} else {
		ret = ocfs2_try_to_merge_extent(inode, handle, path,
						split_index, split_rec,
						dealloc, &ctxt);
		if (ret)
			mlog_errno(ret);
	}

	ocfs2_journal_dirty(handle, eb_bh);

out:
	brelse(last_eb_bh);
	return ret;
}

/*
 * Mark the already-existing extent at cpos as written for len clusters.
 *
 * If the existing extent is larger than the request, initiate a
 * split. An attempt will be made at merging with adjacent extents.
 *
 * The caller is responsible for passing down meta_ac if we'll need it.
 */
int ocfs2_mark_extent_written(struct inode *inode, struct buffer_head *di_bh,
			      handle_t *handle, u32 cpos, u32 len, u32 phys,
			      struct ocfs2_alloc_context *meta_ac,
			      struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, index;
	u64 start_blkno = ocfs2_clusters_to_blocks(inode->i_sb, phys);
	struct ocfs2_extent_rec split_rec;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_extent_list *el;

	mlog(0, "Inode %lu cpos %u, len %u, phys %u (%llu)\n",
	     inode->i_ino, cpos, len, phys, (unsigned long long)start_blkno);

	if (!ocfs2_writes_unwritten_extents(OCFS2_SB(inode->i_sb))) {
		ocfs2_error(inode->i_sb, "Inode %llu has unwritten extents "
			    "that are being written to, but the feature bit "
			    "is not set in the super block.",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno);
		ret = -EROFS;
		goto out;
	}

	/*
	 * XXX: This should be fixed up so that we just re-insert the
	 * next extent records.
	 */
	ocfs2_extent_map_trunc(inode, 0);

	left_path = ocfs2_new_inode_path(di_bh);
	if (!left_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(inode, left_path, cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	el = path_leaf_el(left_path);

	index = ocfs2_search_extent_list(el, cpos);
	if (index == -1 || index >= le16_to_cpu(el->l_next_free_rec)) {
		ocfs2_error(inode->i_sb,
			    "Inode %llu has an extent at cpos %u which can no "
			    "longer be found.\n",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno, cpos);
		ret = -EROFS;
		goto out;
	}

	memset(&split_rec, 0, sizeof(struct ocfs2_extent_rec));
	split_rec.e_cpos = cpu_to_le32(cpos);
	split_rec.e_leaf_clusters = cpu_to_le16(len);
	split_rec.e_blkno = cpu_to_le64(start_blkno);
	split_rec.e_flags = path_leaf_el(left_path)->l_recs[index].e_flags;
	split_rec.e_flags &= ~OCFS2_EXT_UNWRITTEN;

	ret = __ocfs2_mark_extent_written(inode, di_bh, handle, left_path,
					  index, &split_rec, meta_ac, dealloc);
	if (ret)
		mlog_errno(ret);

out:
	ocfs2_free_path(left_path);
	return ret;
}

static int ocfs2_split_tree(struct inode *inode, struct buffer_head *di_bh,
			    handle_t *handle, struct ocfs2_path *path,
			    int index, u32 new_range,
			    struct ocfs2_alloc_context *meta_ac)
{
	int ret, depth, credits = handle->h_buffer_credits;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct buffer_head *last_eb_bh = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *rightmost_el, *el;
	struct ocfs2_extent_rec split_rec;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_insert_type insert;

	/*
	 * Setup the record to split before we grow the tree.
	 */
	el = path_leaf_el(path);
	rec = &el->l_recs[index];
	ocfs2_make_right_split_rec(inode->i_sb, &split_rec, new_range, rec);

	depth = path->p_tree_depth;
	if (depth > 0) {
		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				       le64_to_cpu(di->i_last_eb_blk),
				       &last_eb_bh, OCFS2_BH_CACHED, inode);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
		rightmost_el = &eb->h_list;
	} else
		rightmost_el = path_leaf_el(path);

	credits += path->p_tree_depth + ocfs2_extend_meta_needed(di);
	ret = ocfs2_extend_trans(handle, credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (le16_to_cpu(rightmost_el->l_next_free_rec) ==
	    le16_to_cpu(rightmost_el->l_count)) {
		int old_depth = depth;

		ret = ocfs2_grow_tree(inode, handle, di_bh, &depth, &last_eb_bh,
				      meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (old_depth != depth) {
			eb = (struct ocfs2_extent_block *)last_eb_bh->b_data;
			rightmost_el = &eb->h_list;
		}
	}

	memset(&insert, 0, sizeof(struct ocfs2_insert_type));
	insert.ins_appending = APPEND_NONE;
	insert.ins_contig = CONTIG_NONE;
	insert.ins_split = SPLIT_RIGHT;
	insert.ins_free_records = le16_to_cpu(rightmost_el->l_count)
		- le16_to_cpu(rightmost_el->l_next_free_rec);
	insert.ins_tree_depth = depth;

	ret = ocfs2_do_insert_extent(inode, handle, di_bh, &split_rec, &insert);
	if (ret)
		mlog_errno(ret);

out:
	brelse(last_eb_bh);
	return ret;
}

static int ocfs2_truncate_rec(struct inode *inode, handle_t *handle,
			      struct ocfs2_path *path, int index,
			      struct ocfs2_cached_dealloc_ctxt *dealloc,
			      u32 cpos, u32 len)
{
	int ret;
	u32 left_cpos, rec_range, trunc_range;
	int wants_rotate = 0, is_rightmost_tree_rec = 0;
	struct super_block *sb = inode->i_sb;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_extent_list *el = path_leaf_el(path);
	struct ocfs2_extent_rec *rec;
	struct ocfs2_extent_block *eb;

	if (ocfs2_is_empty_extent(&el->l_recs[0]) && index > 0) {
		ret = ocfs2_rotate_tree_left(inode, handle, path, dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		index--;
	}

	if (index == (le16_to_cpu(el->l_next_free_rec) - 1) &&
	    path->p_tree_depth) {
		/*
		 * Check whether this is the rightmost tree record. If
		 * we remove all of this record or part of its right
		 * edge then an update of the record lengths above it
		 * will be required.
		 */
		eb = (struct ocfs2_extent_block *)path_leaf_bh(path)->b_data;
		if (eb->h_next_leaf_blk == 0)
			is_rightmost_tree_rec = 1;
	}

	rec = &el->l_recs[index];
	if (index == 0 && path->p_tree_depth &&
	    le32_to_cpu(rec->e_cpos) == cpos) {
		/*
		 * Changing the leftmost offset (via partial or whole
		 * record truncate) of an interior (or rightmost) path
		 * means we have to update the subtree that is formed
		 * by this leaf and the one to it's left.
		 *
		 * There are two cases we can skip:
		 *   1) Path is the leftmost one in our inode tree.
		 *   2) The leaf is rightmost and will be empty after
		 *      we remove the extent record - the rotate code
		 *      knows how to update the newly formed edge.
		 */

		ret = ocfs2_find_cpos_for_left_leaf(inode->i_sb, path,
						    &left_cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (left_cpos && le16_to_cpu(el->l_next_free_rec) > 1) {
			left_path = ocfs2_new_path(path_root_bh(path),
						   path_root_el(path));
			if (!left_path) {
				ret = -ENOMEM;
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_find_path(inode, left_path, left_cpos);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}
	}

	ret = ocfs2_extend_rotate_transaction(handle, 0,
					      handle->h_buffer_credits,
					      path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_path(inode, handle, path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_path(inode, handle, left_path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	rec_range = le32_to_cpu(rec->e_cpos) + ocfs2_rec_clusters(el, rec);
	trunc_range = cpos + len;

	if (le32_to_cpu(rec->e_cpos) == cpos && rec_range == trunc_range) {
		int next_free;

		memset(rec, 0, sizeof(*rec));
		ocfs2_cleanup_merge(el, index);
		wants_rotate = 1;

		next_free = le16_to_cpu(el->l_next_free_rec);
		if (is_rightmost_tree_rec && next_free > 1) {
			/*
			 * We skip the edge update if this path will
			 * be deleted by the rotate code.
			 */
			rec = &el->l_recs[next_free - 1];
			ocfs2_adjust_rightmost_records(inode, handle, path,
						       rec);
		}
	} else if (le32_to_cpu(rec->e_cpos) == cpos) {
		/* Remove leftmost portion of the record. */
		le32_add_cpu(&rec->e_cpos, len);
		le64_add_cpu(&rec->e_blkno, ocfs2_clusters_to_blocks(sb, len));
		le16_add_cpu(&rec->e_leaf_clusters, -len);
	} else if (rec_range == trunc_range) {
		/* Remove rightmost portion of the record */
		le16_add_cpu(&rec->e_leaf_clusters, -len);
		if (is_rightmost_tree_rec)
			ocfs2_adjust_rightmost_records(inode, handle, path, rec);
	} else {
		/* Caller should have trapped this. */
		mlog(ML_ERROR, "Inode %llu: Invalid record truncate: (%u, %u) "
		     "(%u, %u)\n", (unsigned long long)OCFS2_I(inode)->ip_blkno,
		     le32_to_cpu(rec->e_cpos),
		     le16_to_cpu(rec->e_leaf_clusters), cpos, len);
		BUG();
	}

	if (left_path) {
		int subtree_index;

		subtree_index = ocfs2_find_subtree_root(inode, left_path, path);
		ocfs2_complete_edge_insert(inode, handle, left_path, path,
					   subtree_index);
	}

	ocfs2_journal_dirty(handle, path_leaf_bh(path));

	ret = ocfs2_rotate_tree_left(inode, handle, path, dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

out:
	ocfs2_free_path(left_path);
	return ret;
}

int ocfs2_remove_extent(struct inode *inode, struct buffer_head *di_bh,
			u32 cpos, u32 len, handle_t *handle,
			struct ocfs2_alloc_context *meta_ac,
			struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, index;
	u32 rec_range, trunc_range;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_extent_list *el;
	struct ocfs2_path *path;

	ocfs2_extent_map_trunc(inode, 0);

	path = ocfs2_new_inode_path(di_bh);
	if (!path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(inode, path, cpos);
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
			    (unsigned long long)OCFS2_I(inode)->ip_blkno, cpos);
		ret = -EROFS;
		goto out;
	}

	/*
	 * We have 3 cases of extent removal:
	 *   1) Range covers the entire extent rec
	 *   2) Range begins or ends on one edge of the extent rec
	 *   3) Range is in the middle of the extent rec (no shared edges)
	 *
	 * For case 1 we remove the extent rec and left rotate to
	 * fill the hole.
	 *
	 * For case 2 we just shrink the existing extent rec, with a
	 * tree update if the shrinking edge is also the edge of an
	 * extent block.
	 *
	 * For case 3 we do a right split to turn the extent rec into
	 * something case 2 can handle.
	 */
	rec = &el->l_recs[index];
	rec_range = le32_to_cpu(rec->e_cpos) + ocfs2_rec_clusters(el, rec);
	trunc_range = cpos + len;

	BUG_ON(cpos < le32_to_cpu(rec->e_cpos) || trunc_range > rec_range);

	mlog(0, "Inode %llu, remove (cpos %u, len %u). Existing index %d "
	     "(cpos %u, len %u)\n",
	     (unsigned long long)OCFS2_I(inode)->ip_blkno, cpos, len, index,
	     le32_to_cpu(rec->e_cpos), ocfs2_rec_clusters(el, rec));

	if (le32_to_cpu(rec->e_cpos) == cpos || rec_range == trunc_range) {
		ret = ocfs2_truncate_rec(inode, handle, path, index, dealloc,
					 cpos, len);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	} else {
		ret = ocfs2_split_tree(inode, di_bh, handle, path, index,
				       trunc_range, meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * The split could have manipulated the tree enough to
		 * move the record location, so we have to look for it again.
		 */
		ocfs2_reinit_path(path, 1);

		ret = ocfs2_find_path(inode, path, cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		el = path_leaf_el(path);
		index = ocfs2_search_extent_list(el, cpos);
		if (index == -1 || index >= le16_to_cpu(el->l_next_free_rec)) {
			ocfs2_error(inode->i_sb,
				    "Inode %llu: split at cpos %u lost record.",
				    (unsigned long long)OCFS2_I(inode)->ip_blkno,
				    cpos);
			ret = -EROFS;
			goto out;
		}

		/*
		 * Double check our values here. If anything is fishy,
		 * it's easier to catch it at the top level.
		 */
		rec = &el->l_recs[index];
		rec_range = le32_to_cpu(rec->e_cpos) +
			ocfs2_rec_clusters(el, rec);
		if (rec_range != trunc_range) {
			ocfs2_error(inode->i_sb,
				    "Inode %llu: error after split at cpos %u"
				    "trunc len %u, existing record is (%u,%u)",
				    (unsigned long long)OCFS2_I(inode)->ip_blkno,
				    cpos, len, le32_to_cpu(rec->e_cpos),
				    ocfs2_rec_clusters(el, rec));
			ret = -EROFS;
			goto out;
		}

		ret = ocfs2_truncate_rec(inode, handle, path, index, dealloc,
					 cpos, len);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

out:
	ocfs2_free_path(path);
	return ret;
}

int ocfs2_truncate_log_needs_flush(struct ocfs2_super *osb)
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

int ocfs2_truncate_log_append(struct ocfs2_super *osb,
			      handle_t *handle,
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
					 handle_t *handle,
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
		status = ocfs2_extend_trans(handle,
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
int __ocfs2_flush_truncate_log(struct ocfs2_super *osb)
{
	int status;
	unsigned int num_to_flush;
	handle_t *handle;
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
		goto out;
	}

	num_to_flush = le16_to_cpu(tl->tl_used);
	mlog(0, "Flush %u records from truncate log #%llu\n",
	     num_to_flush, (unsigned long long)OCFS2_I(tl_inode)->ip_blkno);
	if (!num_to_flush) {
		status = 0;
		goto out;
	}

	data_alloc_inode = ocfs2_get_system_file_inode(osb,
						       GLOBAL_BITMAP_SYSTEM_INODE,
						       OCFS2_INVALID_SLOT);
	if (!data_alloc_inode) {
		status = -EINVAL;
		mlog(ML_ERROR, "Could not get bitmap inode!\n");
		goto out;
	}

	mutex_lock(&data_alloc_inode->i_mutex);

	status = ocfs2_meta_lock(data_alloc_inode, &data_alloc_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto out_mutex;
	}

	handle = ocfs2_start_trans(osb, OCFS2_TRUNCATE_LOG_UPDATE);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		mlog_errno(status);
		goto out_unlock;
	}

	status = ocfs2_replay_truncate_records(osb, handle, data_alloc_inode,
					       data_alloc_bh);
	if (status < 0)
		mlog_errno(status);

	ocfs2_commit_trans(osb, handle);

out_unlock:
	brelse(data_alloc_bh);
	ocfs2_meta_unlock(data_alloc_inode, 1);

out_mutex:
	mutex_unlock(&data_alloc_inode->i_mutex);
	iput(data_alloc_inode);

out:
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

static void ocfs2_truncate_log_worker(struct work_struct *work)
{
	int status;
	struct ocfs2_super *osb =
		container_of(work, struct ocfs2_super,
			     osb_truncate_log_wq.work);

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
	handle_t *handle;
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
	     (unsigned long long)le64_to_cpu(tl_copy->i_blkno));

	mutex_lock(&tl_inode->i_mutex);
	for(i = 0; i < num_recs; i++) {
		if (ocfs2_truncate_log_needs_flush(osb)) {
			status = __ocfs2_flush_truncate_log(osb);
			if (status < 0) {
				mlog_errno(status);
				goto bail_up;
			}
		}

		handle = ocfs2_start_trans(osb, OCFS2_TRUNCATE_LOG_UPDATE);
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
		ocfs2_commit_trans(osb, handle);
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
	INIT_DELAYED_WORK(&osb->osb_truncate_log_wq,
			  ocfs2_truncate_log_worker);
	osb->osb_tl_bh    = tl_bh;
	osb->osb_tl_inode = tl_inode;

	mlog_exit(status);
	return status;
}

/*
 * Delayed de-allocation of suballocator blocks.
 *
 * Some sets of block de-allocations might involve multiple suballocator inodes.
 *
 * The locking for this can get extremely complicated, especially when
 * the suballocator inodes to delete from aren't known until deep
 * within an unrelated codepath.
 *
 * ocfs2_extent_block structures are a good example of this - an inode
 * btree could have been grown by any number of nodes each allocating
 * out of their own suballoc inode.
 *
 * These structures allow the delay of block de-allocation until a
 * later time, when locking of multiple cluster inodes won't cause
 * deadlock.
 */

/*
 * Describes a single block free from a suballocator
 */
struct ocfs2_cached_block_free {
	struct ocfs2_cached_block_free		*free_next;
	u64					free_blk;
	unsigned int				free_bit;
};

struct ocfs2_per_slot_free_list {
	struct ocfs2_per_slot_free_list		*f_next_suballocator;
	int					f_inode_type;
	int					f_slot;
	struct ocfs2_cached_block_free		*f_first;
};

static int ocfs2_free_cached_items(struct ocfs2_super *osb,
				   int sysfile_type,
				   int slot,
				   struct ocfs2_cached_block_free *head)
{
	int ret;
	u64 bg_blkno;
	handle_t *handle;
	struct inode *inode;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_cached_block_free *tmp;

	inode = ocfs2_get_system_file_inode(osb, sysfile_type, slot);
	if (!inode) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto out;
	}

	mutex_lock(&inode->i_mutex);

	ret = ocfs2_meta_lock(inode, &di_bh, 1);
	if (ret) {
		mlog_errno(ret);
		goto out_mutex;
	}

	handle = ocfs2_start_trans(osb, OCFS2_SUBALLOC_FREE);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_unlock;
	}

	while (head) {
		bg_blkno = ocfs2_which_suballoc_group(head->free_blk,
						      head->free_bit);
		mlog(0, "Free bit: (bit %u, blkno %llu)\n",
		     head->free_bit, (unsigned long long)head->free_blk);

		ret = ocfs2_free_suballoc_bits(handle, inode, di_bh,
					       head->free_bit, bg_blkno, 1);
		if (ret) {
			mlog_errno(ret);
			goto out_journal;
		}

		ret = ocfs2_extend_trans(handle, OCFS2_SUBALLOC_FREE);
		if (ret) {
			mlog_errno(ret);
			goto out_journal;
		}

		tmp = head;
		head = head->free_next;
		kfree(tmp);
	}

out_journal:
	ocfs2_commit_trans(osb, handle);

out_unlock:
	ocfs2_meta_unlock(inode, 1);
	brelse(di_bh);
out_mutex:
	mutex_unlock(&inode->i_mutex);
	iput(inode);
out:
	while(head) {
		/* Premature exit may have left some dangling items. */
		tmp = head;
		head = head->free_next;
		kfree(tmp);
	}

	return ret;
}

int ocfs2_run_deallocs(struct ocfs2_super *osb,
		       struct ocfs2_cached_dealloc_ctxt *ctxt)
{
	int ret = 0, ret2;
	struct ocfs2_per_slot_free_list *fl;

	if (!ctxt)
		return 0;

	while (ctxt->c_first_suballocator) {
		fl = ctxt->c_first_suballocator;

		if (fl->f_first) {
			mlog(0, "Free items: (type %u, slot %d)\n",
			     fl->f_inode_type, fl->f_slot);
			ret2 = ocfs2_free_cached_items(osb, fl->f_inode_type,
						       fl->f_slot, fl->f_first);
			if (ret2)
				mlog_errno(ret2);
			if (!ret)
				ret = ret2;
		}

		ctxt->c_first_suballocator = fl->f_next_suballocator;
		kfree(fl);
	}

	return ret;
}

static struct ocfs2_per_slot_free_list *
ocfs2_find_per_slot_free_list(int type,
			      int slot,
			      struct ocfs2_cached_dealloc_ctxt *ctxt)
{
	struct ocfs2_per_slot_free_list *fl = ctxt->c_first_suballocator;

	while (fl) {
		if (fl->f_inode_type == type && fl->f_slot == slot)
			return fl;

		fl = fl->f_next_suballocator;
	}

	fl = kmalloc(sizeof(*fl), GFP_NOFS);
	if (fl) {
		fl->f_inode_type = type;
		fl->f_slot = slot;
		fl->f_first = NULL;
		fl->f_next_suballocator = ctxt->c_first_suballocator;

		ctxt->c_first_suballocator = fl;
	}
	return fl;
}

static int ocfs2_cache_block_dealloc(struct ocfs2_cached_dealloc_ctxt *ctxt,
				     int type, int slot, u64 blkno,
				     unsigned int bit)
{
	int ret;
	struct ocfs2_per_slot_free_list *fl;
	struct ocfs2_cached_block_free *item;

	fl = ocfs2_find_per_slot_free_list(type, slot, ctxt);
	if (fl == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	item = kmalloc(sizeof(*item), GFP_NOFS);
	if (item == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	mlog(0, "Insert: (type %d, slot %u, bit %u, blk %llu)\n",
	     type, slot, bit, (unsigned long long)blkno);

	item->free_blk = blkno;
	item->free_bit = bit;
	item->free_next = fl->f_first;

	fl->f_first = item;

	ret = 0;
out:
	return ret;
}

static int ocfs2_cache_extent_block_free(struct ocfs2_cached_dealloc_ctxt *ctxt,
					 struct ocfs2_extent_block *eb)
{
	return ocfs2_cache_block_dealloc(ctxt, EXTENT_ALLOC_SYSTEM_INODE,
					 le16_to_cpu(eb->h_suballoc_slot),
					 le64_to_cpu(eb->h_blkno),
					 le16_to_cpu(eb->h_suballoc_bit));
}

/* This function will figure out whether the currently last extent
 * block will be deleted, and if it will, what the new last extent
 * block will be so we can update his h_next_leaf_blk field, as well
 * as the dinodes i_last_eb_blk */
static int ocfs2_find_new_last_ext_blk(struct inode *inode,
				       unsigned int clusters_to_del,
				       struct ocfs2_path *path,
				       struct buffer_head **new_last_eb)
{
	int next_free, ret = 0;
	u32 cpos;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct buffer_head *bh = NULL;

	*new_last_eb = NULL;

	/* we have no tree, so of course, no last_eb. */
	if (!path->p_tree_depth)
		goto out;

	/* trunc to zero special case - this makes tree_depth = 0
	 * regardless of what it is.  */
	if (OCFS2_I(inode)->ip_clusters == clusters_to_del)
		goto out;

	el = path_leaf_el(path);
	BUG_ON(!el->l_next_free_rec);

	/*
	 * Make sure that this extent list will actually be empty
	 * after we clear away the data. We can shortcut out if
	 * there's more than one non-empty extent in the
	 * list. Otherwise, a check of the remaining extent is
	 * necessary.
	 */
	next_free = le16_to_cpu(el->l_next_free_rec);
	rec = NULL;
	if (ocfs2_is_empty_extent(&el->l_recs[0])) {
		if (next_free > 2)
			goto out;

		/* We may have a valid extent in index 1, check it. */
		if (next_free == 2)
			rec = &el->l_recs[1];

		/*
		 * Fall through - no more nonempty extents, so we want
		 * to delete this leaf.
		 */
	} else {
		if (next_free > 1)
			goto out;

		rec = &el->l_recs[0];
	}

	if (rec) {
		/*
		 * Check it we'll only be trimming off the end of this
		 * cluster.
		 */
		if (le16_to_cpu(rec->e_leaf_clusters) > clusters_to_del)
			goto out;
	}

	ret = ocfs2_find_cpos_for_left_leaf(inode->i_sb, path, &cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_leaf(inode, path_root_el(path), cpos, &bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	eb = (struct ocfs2_extent_block *) bh->b_data;
	el = &eb->h_list;
	if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
		OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, eb);
		ret = -EROFS;
		goto out;
	}

	*new_last_eb = bh;
	get_bh(*new_last_eb);
	mlog(0, "returning block %llu, (cpos: %u)\n",
	     (unsigned long long)le64_to_cpu(eb->h_blkno), cpos);
out:
	brelse(bh);

	return ret;
}

/*
 * Trim some clusters off the rightmost edge of a tree. Only called
 * during truncate.
 *
 * The caller needs to:
 *   - start journaling of each path component.
 *   - compute and fully set up any new last ext block
 */
static int ocfs2_trim_tree(struct inode *inode, struct ocfs2_path *path,
			   handle_t *handle, struct ocfs2_truncate_context *tc,
			   u32 clusters_to_del, u64 *delete_start)
{
	int ret, i, index = path->p_tree_depth;
	u32 new_edge = 0;
	u64 deleted_eb = 0;
	struct buffer_head *bh;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;

	*delete_start = 0;

	while (index >= 0) {
		bh = path->p_node[index].bh;
		el = path->p_node[index].el;

		mlog(0, "traveling tree (index = %d, block = %llu)\n",
		     index,  (unsigned long long)bh->b_blocknr);

		BUG_ON(le16_to_cpu(el->l_next_free_rec) == 0);

		if (index !=
		    (path->p_tree_depth - le16_to_cpu(el->l_tree_depth))) {
			ocfs2_error(inode->i_sb,
				    "Inode %lu has invalid ext. block %llu",
				    inode->i_ino,
				    (unsigned long long)bh->b_blocknr);
			ret = -EROFS;
			goto out;
		}

find_tail_record:
		i = le16_to_cpu(el->l_next_free_rec) - 1;
		rec = &el->l_recs[i];

		mlog(0, "Extent list before: record %d: (%u, %u, %llu), "
		     "next = %u\n", i, le32_to_cpu(rec->e_cpos),
		     ocfs2_rec_clusters(el, rec),
		     (unsigned long long)le64_to_cpu(rec->e_blkno),
		     le16_to_cpu(el->l_next_free_rec));

		BUG_ON(ocfs2_rec_clusters(el, rec) < clusters_to_del);

		if (le16_to_cpu(el->l_tree_depth) == 0) {
			/*
			 * If the leaf block contains a single empty
			 * extent and no records, we can just remove
			 * the block.
			 */
			if (i == 0 && ocfs2_is_empty_extent(rec)) {
				memset(rec, 0,
				       sizeof(struct ocfs2_extent_rec));
				el->l_next_free_rec = cpu_to_le16(0);

				goto delete;
			}

			/*
			 * Remove any empty extents by shifting things
			 * left. That should make life much easier on
			 * the code below. This condition is rare
			 * enough that we shouldn't see a performance
			 * hit.
			 */
			if (ocfs2_is_empty_extent(&el->l_recs[0])) {
				le16_add_cpu(&el->l_next_free_rec, -1);

				for(i = 0;
				    i < le16_to_cpu(el->l_next_free_rec); i++)
					el->l_recs[i] = el->l_recs[i + 1];

				memset(&el->l_recs[i], 0,
				       sizeof(struct ocfs2_extent_rec));

				/*
				 * We've modified our extent list. The
				 * simplest way to handle this change
				 * is to being the search from the
				 * start again.
				 */
				goto find_tail_record;
			}

			le16_add_cpu(&rec->e_leaf_clusters, -clusters_to_del);

			/*
			 * We'll use "new_edge" on our way back up the
			 * tree to know what our rightmost cpos is.
			 */
			new_edge = le16_to_cpu(rec->e_leaf_clusters);
			new_edge += le32_to_cpu(rec->e_cpos);

			/*
			 * The caller will use this to delete data blocks.
			 */
			*delete_start = le64_to_cpu(rec->e_blkno)
				+ ocfs2_clusters_to_blocks(inode->i_sb,
					le16_to_cpu(rec->e_leaf_clusters));

			/*
			 * If it's now empty, remove this record.
			 */
			if (le16_to_cpu(rec->e_leaf_clusters) == 0) {
				memset(rec, 0,
				       sizeof(struct ocfs2_extent_rec));
				le16_add_cpu(&el->l_next_free_rec, -1);
			}
		} else {
			if (le64_to_cpu(rec->e_blkno) == deleted_eb) {
				memset(rec, 0,
				       sizeof(struct ocfs2_extent_rec));
				le16_add_cpu(&el->l_next_free_rec, -1);

				goto delete;
			}

			/* Can this actually happen? */
			if (le16_to_cpu(el->l_next_free_rec) == 0)
				goto delete;

			/*
			 * We never actually deleted any clusters
			 * because our leaf was empty. There's no
			 * reason to adjust the rightmost edge then.
			 */
			if (new_edge == 0)
				goto delete;

			rec->e_int_clusters = cpu_to_le32(new_edge);
			le32_add_cpu(&rec->e_int_clusters,
				     -le32_to_cpu(rec->e_cpos));

			 /*
			  * A deleted child record should have been
			  * caught above.
			  */
			 BUG_ON(le32_to_cpu(rec->e_int_clusters) == 0);
		}

delete:
		ret = ocfs2_journal_dirty(handle, bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		mlog(0, "extent list container %llu, after: record %d: "
		     "(%u, %u, %llu), next = %u.\n",
		     (unsigned long long)bh->b_blocknr, i,
		     le32_to_cpu(rec->e_cpos), ocfs2_rec_clusters(el, rec),
		     (unsigned long long)le64_to_cpu(rec->e_blkno),
		     le16_to_cpu(el->l_next_free_rec));

		/*
		 * We must be careful to only attempt delete of an
		 * extent block (and not the root inode block).
		 */
		if (index > 0 && le16_to_cpu(el->l_next_free_rec) == 0) {
			struct ocfs2_extent_block *eb =
				(struct ocfs2_extent_block *)bh->b_data;

			/*
			 * Save this for use when processing the
			 * parent block.
			 */
			deleted_eb = le64_to_cpu(eb->h_blkno);

			mlog(0, "deleting this extent block.\n");

			ocfs2_remove_from_cache(inode, bh);

			BUG_ON(ocfs2_rec_clusters(el, &el->l_recs[0]));
			BUG_ON(le32_to_cpu(el->l_recs[0].e_cpos));
			BUG_ON(le64_to_cpu(el->l_recs[0].e_blkno));

			ret = ocfs2_cache_extent_block_free(&tc->tc_dealloc, eb);
			/* An error here is not fatal. */
			if (ret < 0)
				mlog_errno(ret);
		} else {
			deleted_eb = 0;
		}

		index--;
	}

	ret = 0;
out:
	return ret;
}

static int ocfs2_do_truncate(struct ocfs2_super *osb,
			     unsigned int clusters_to_del,
			     struct inode *inode,
			     struct buffer_head *fe_bh,
			     handle_t *handle,
			     struct ocfs2_truncate_context *tc,
			     struct ocfs2_path *path)
{
	int status;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *last_eb = NULL;
	struct ocfs2_extent_list *el;
	struct buffer_head *last_eb_bh = NULL;
	u64 delete_blk = 0;

	fe = (struct ocfs2_dinode *) fe_bh->b_data;

	status = ocfs2_find_new_last_ext_blk(inode, clusters_to_del,
					     path, &last_eb_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/*
	 * Each component will be touched, so we might as well journal
	 * here to avoid having to handle errors later.
	 */
	status = ocfs2_journal_access_path(inode, handle, path);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	if (last_eb_bh) {
		status = ocfs2_journal_access(handle, inode, last_eb_bh,
					      OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		last_eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
	}

	el = &(fe->id2.i_list);

	/*
	 * Lower levels depend on this never happening, but it's best
	 * to check it up here before changing the tree.
	 */
	if (el->l_tree_depth && el->l_recs[0].e_int_clusters == 0) {
		ocfs2_error(inode->i_sb,
			    "Inode %lu has an empty extent record, depth %u\n",
			    inode->i_ino, le16_to_cpu(el->l_tree_depth));
		status = -EROFS;
		goto bail;
	}

	spin_lock(&OCFS2_I(inode)->ip_lock);
	OCFS2_I(inode)->ip_clusters = le32_to_cpu(fe->i_clusters) -
				      clusters_to_del;
	spin_unlock(&OCFS2_I(inode)->ip_lock);
	le32_add_cpu(&fe->i_clusters, -clusters_to_del);

	status = ocfs2_trim_tree(inode, path, handle, tc,
				 clusters_to_del, &delete_blk);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	if (le32_to_cpu(fe->i_clusters) == 0) {
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
		last_eb->h_next_leaf_blk = 0;
		status = ocfs2_journal_dirty(handle, last_eb_bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	if (delete_blk) {
		status = ocfs2_truncate_log_append(osb, handle, delete_blk,
						   clusters_to_del);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}
	status = 0;
bail:

	mlog_exit(status);
	return status;
}

static int ocfs2_writeback_zero_func(handle_t *handle, struct buffer_head *bh)
{
	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	return 0;
}

static int ocfs2_ordered_zero_func(handle_t *handle, struct buffer_head *bh)
{
	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	return ocfs2_journal_dirty_data(handle, bh);
}

static void ocfs2_zero_cluster_pages(struct inode *inode, loff_t start,
				     loff_t end, struct page **pages,
				     int numpages, u64 phys, handle_t *handle)
{
	int i, ret, partial = 0;
	void *kaddr;
	struct page *page;
	unsigned int from, to = PAGE_CACHE_SIZE;
	struct super_block *sb = inode->i_sb;

	BUG_ON(!ocfs2_sparse_alloc(OCFS2_SB(sb)));

	if (numpages == 0)
		goto out;

	to = PAGE_CACHE_SIZE;
	for(i = 0; i < numpages; i++) {
		page = pages[i];

		from = start & (PAGE_CACHE_SIZE - 1);
		if ((end >> PAGE_CACHE_SHIFT) == page->index)
			to = end & (PAGE_CACHE_SIZE - 1);

		BUG_ON(from > PAGE_CACHE_SIZE);
		BUG_ON(to > PAGE_CACHE_SIZE);

		ret = ocfs2_map_page_blocks(page, &phys, inode, from, to, 0);
		if (ret)
			mlog_errno(ret);

		kaddr = kmap_atomic(page, KM_USER0);
		memset(kaddr + from, 0, to - from);
		kunmap_atomic(kaddr, KM_USER0);

		/*
		 * Need to set the buffers we zero'd into uptodate
		 * here if they aren't - ocfs2_map_page_blocks()
		 * might've skipped some
		 */
		if (ocfs2_should_order_data(inode)) {
			ret = walk_page_buffers(handle,
						page_buffers(page),
						from, to, &partial,
						ocfs2_ordered_zero_func);
			if (ret < 0)
				mlog_errno(ret);
		} else {
			ret = walk_page_buffers(handle, page_buffers(page),
						from, to, &partial,
						ocfs2_writeback_zero_func);
			if (ret < 0)
				mlog_errno(ret);
		}

		if (!partial)
			SetPageUptodate(page);

		flush_dcache_page(page);

		start = (page->index + 1) << PAGE_CACHE_SHIFT;
	}
out:
	if (pages) {
		for (i = 0; i < numpages; i++) {
			page = pages[i];
			unlock_page(page);
			mark_page_accessed(page);
			page_cache_release(page);
		}
	}
}

static int ocfs2_grab_eof_pages(struct inode *inode, loff_t start, loff_t end,
				struct page **pages, int *num, u64 *phys)
{
	int i, numpages = 0, ret = 0;
	unsigned int ext_flags;
	struct super_block *sb = inode->i_sb;
	struct address_space *mapping = inode->i_mapping;
	unsigned long index;
	loff_t last_page_bytes;

	BUG_ON(!ocfs2_sparse_alloc(OCFS2_SB(sb)));
	BUG_ON(start > end);

	if (start == end)
		goto out;

	BUG_ON(start >> OCFS2_SB(sb)->s_clustersize_bits !=
	       (end - 1) >> OCFS2_SB(sb)->s_clustersize_bits);

	ret = ocfs2_extent_map_get_blocks(inode, start >> sb->s_blocksize_bits,
					  phys, NULL, &ext_flags);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* Tail is a hole. */
	if (*phys == 0)
		goto out;

	/* Tail is marked as unwritten, we can count on write to zero
	 * in that case. */
	if (ext_flags & OCFS2_EXT_UNWRITTEN)
		goto out;

	last_page_bytes = PAGE_ALIGN(end);
	index = start >> PAGE_CACHE_SHIFT;
	do {
		pages[numpages] = grab_cache_page(mapping, index);
		if (!pages[numpages]) {
			ret = -ENOMEM;
			mlog_errno(ret);
			goto out;
		}

		numpages++;
		index++;
	} while (index < (last_page_bytes >> PAGE_CACHE_SHIFT));

out:
	if (ret != 0) {
		if (pages) {
			for (i = 0; i < numpages; i++) {
				if (pages[i]) {
					unlock_page(pages[i]);
					page_cache_release(pages[i]);
				}
			}
		}
		numpages = 0;
	}

	*num = numpages;

	return ret;
}

/*
 * Zero the area past i_size but still within an allocated
 * cluster. This avoids exposing nonzero data on subsequent file
 * extends.
 *
 * We need to call this before i_size is updated on the inode because
 * otherwise block_write_full_page() will skip writeout of pages past
 * i_size. The new_i_size parameter is passed for this reason.
 */
int ocfs2_zero_range_for_truncate(struct inode *inode, handle_t *handle,
				  u64 range_start, u64 range_end)
{
	int ret, numpages;
	struct page **pages = NULL;
	u64 phys;

	/*
	 * File systems which don't support sparse files zero on every
	 * extend.
	 */
	if (!ocfs2_sparse_alloc(OCFS2_SB(inode->i_sb)))
		return 0;

	pages = kcalloc(ocfs2_pages_per_cluster(inode->i_sb),
			sizeof(struct page *), GFP_NOFS);
	if (pages == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_grab_eof_pages(inode, range_start, range_end, pages,
				   &numpages, &phys);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (numpages == 0)
		goto out;

	ocfs2_zero_cluster_pages(inode, range_start, range_end, pages,
				 numpages, phys, handle);

	/*
	 * Initiate writeout of the pages we zero'd here. We don't
	 * wait on them - the truncate_inode_pages() call later will
	 * do that for us.
	 */
	ret = do_sync_mapping_range(inode->i_mapping, range_start,
				    range_end - 1, SYNC_FILE_RANGE_WRITE);
	if (ret)
		mlog_errno(ret);

out:
	if (pages)
		kfree(pages);

	return ret;
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
	u32 clusters_to_del, new_highest_cpos, range;
	struct ocfs2_extent_list *el;
	handle_t *handle = NULL;
	struct inode *tl_inode = osb->osb_tl_inode;
	struct ocfs2_path *path = NULL;

	mlog_entry_void();

	new_highest_cpos = ocfs2_clusters_for_bytes(osb->sb,
						     i_size_read(inode));

	path = ocfs2_new_inode_path(fe_bh);
	if (!path) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	ocfs2_extent_map_trunc(inode, new_highest_cpos);

start:
	/*
	 * Check that we still have allocation to delete.
	 */
	if (OCFS2_I(inode)->ip_clusters == 0) {
		status = 0;
		goto bail;
	}

	/*
	 * Truncate always works against the rightmost tree branch.
	 */
	status = ocfs2_find_path(inode, path, UINT_MAX);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	mlog(0, "inode->ip_clusters = %u, tree_depth = %u\n",
	     OCFS2_I(inode)->ip_clusters, path->p_tree_depth);

	/*
	 * By now, el will point to the extent list on the bottom most
	 * portion of this tree. Only the tail record is considered in
	 * each pass.
	 *
	 * We handle the following cases, in order:
	 * - empty extent: delete the remaining branch
	 * - remove the entire record
	 * - remove a partial record
	 * - no record needs to be removed (truncate has completed)
	 */
	el = path_leaf_el(path);
	if (le16_to_cpu(el->l_next_free_rec) == 0) {
		ocfs2_error(inode->i_sb,
			    "Inode %llu has empty extent block at %llu\n",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno,
			    (unsigned long long)path_leaf_bh(path)->b_blocknr);
		status = -EROFS;
		goto bail;
	}

	i = le16_to_cpu(el->l_next_free_rec) - 1;
	range = le32_to_cpu(el->l_recs[i].e_cpos) +
		ocfs2_rec_clusters(el, &el->l_recs[i]);
	if (i == 0 && ocfs2_is_empty_extent(&el->l_recs[i])) {
		clusters_to_del = 0;
	} else if (le32_to_cpu(el->l_recs[i].e_cpos) >= new_highest_cpos) {
		clusters_to_del = ocfs2_rec_clusters(el, &el->l_recs[i]);
	} else if (range > new_highest_cpos) {
		clusters_to_del = (ocfs2_rec_clusters(el, &el->l_recs[i]) +
				   le32_to_cpu(el->l_recs[i].e_cpos)) -
				  new_highest_cpos;
	} else {
		status = 0;
		goto bail;
	}

	mlog(0, "clusters_to_del = %u in this pass, tail blk=%llu\n",
	     clusters_to_del, (unsigned long long)path_leaf_bh(path)->b_blocknr);

	BUG_ON(clusters_to_del == 0);

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
						(struct ocfs2_dinode *)fe_bh->b_data,
						el);
	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		status = PTR_ERR(handle);
		handle = NULL;
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_do_truncate(osb, clusters_to_del, inode, fe_bh, handle,
				   tc, path);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	mutex_unlock(&tl_inode->i_mutex);
	tl_sem = 0;

	ocfs2_commit_trans(osb, handle);
	handle = NULL;

	ocfs2_reinit_path(path, 1);

	/*
	 * The check above will catch the case where we've truncated
	 * away all allocation.
	 */
	goto start;

bail:

	ocfs2_schedule_truncate_log_flush(osb, 1);

	if (tl_sem)
		mutex_unlock(&tl_inode->i_mutex);

	if (handle)
		ocfs2_commit_trans(osb, handle);

	ocfs2_run_deallocs(osb, &tc->tc_dealloc);

	ocfs2_free_path(path);

	/* This will drop the ext_alloc cluster lock for us */
	ocfs2_free_truncate_context(tc);

	mlog_exit(status);
	return status;
}

/*
 * Expects the inode to already be locked.
 */
int ocfs2_prepare_truncate(struct ocfs2_super *osb,
			   struct inode *inode,
			   struct buffer_head *fe_bh,
			   struct ocfs2_truncate_context **tc)
{
	int status;
	unsigned int new_i_clusters;
	struct ocfs2_dinode *fe;
	struct ocfs2_extent_block *eb;
	struct buffer_head *last_eb_bh = NULL;

	mlog_entry_void();

	*tc = NULL;

	new_i_clusters = ocfs2_clusters_for_bytes(osb->sb,
						  i_size_read(inode));
	fe = (struct ocfs2_dinode *) fe_bh->b_data;

	mlog(0, "fe->i_clusters = %u, new_i_clusters = %u, fe->i_size ="
	     "%llu\n", le32_to_cpu(fe->i_clusters), new_i_clusters,
	     (unsigned long long)le64_to_cpu(fe->i_size));

	*tc = kzalloc(sizeof(struct ocfs2_truncate_context), GFP_KERNEL);
	if (!(*tc)) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}
	ocfs2_init_dealloc_ctxt(&(*tc)->tc_dealloc);

	if (fe->id2.i_list.l_tree_depth) {
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
	}

	(*tc)->tc_last_eb_bh = last_eb_bh;

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
	/*
	 * The caller is responsible for completing deallocation
	 * before freeing the context.
	 */
	if (tc->tc_dealloc.c_first_suballocator != NULL)
		mlog(ML_NOTICE,
		     "Truncate completion has non-empty dealloc context\n");

	if (tc->tc_last_eb_bh)
		brelse(tc->tc_last_eb_bh);

	kfree(tc);
}
