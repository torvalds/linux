/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * alloc.h
 *
 * Function prototypes
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

#ifndef OCFS2_ALLOC_H
#define OCFS2_ALLOC_H

struct ocfs2_alloc_context;
int ocfs2_insert_extent(struct ocfs2_super *osb,
			handle_t *handle,
			struct inode *inode,
			struct buffer_head *fe_bh,
			u32 cpos,
			u64 start_blk,
			u32 new_clusters,
			struct ocfs2_alloc_context *meta_ac);
int ocfs2_num_free_extents(struct ocfs2_super *osb,
			   struct inode *inode,
			   struct ocfs2_dinode *fe);
/* how many new metadata chunks would an allocation need at maximum? */
static inline int ocfs2_extend_meta_needed(struct ocfs2_dinode *fe)
{
	/*
	 * Rather than do all the work of determining how much we need
	 * (involves a ton of reads and locks), just ask for the
	 * maximal limit.  That's a tree depth shift.  So, one block for
	 * level of the tree (current l_tree_depth), one block for the
	 * new tree_depth==0 extent_block, and one block at the new
	 * top-of-the tree.
	 */
	return le16_to_cpu(fe->id2.i_list.l_tree_depth) + 2;
}

int ocfs2_truncate_log_init(struct ocfs2_super *osb);
void ocfs2_truncate_log_shutdown(struct ocfs2_super *osb);
void ocfs2_schedule_truncate_log_flush(struct ocfs2_super *osb,
				       int cancel);
int ocfs2_flush_truncate_log(struct ocfs2_super *osb);
int ocfs2_begin_truncate_log_recovery(struct ocfs2_super *osb,
				      int slot_num,
				      struct ocfs2_dinode **tl_copy);
int ocfs2_complete_truncate_log_recovery(struct ocfs2_super *osb,
					 struct ocfs2_dinode *tl_copy);

struct ocfs2_truncate_context {
	struct inode *tc_ext_alloc_inode;
	struct buffer_head *tc_ext_alloc_bh;
	int tc_ext_alloc_locked; /* is it cluster locked? */
	/* these get destroyed once it's passed to ocfs2_commit_truncate. */
	struct buffer_head *tc_last_eb_bh;
};

int ocfs2_zero_tail_for_truncate(struct inode *inode, handle_t *handle,
				 u64 new_i_size);
int ocfs2_prepare_truncate(struct ocfs2_super *osb,
			   struct inode *inode,
			   struct buffer_head *fe_bh,
			   struct ocfs2_truncate_context **tc);
int ocfs2_commit_truncate(struct ocfs2_super *osb,
			  struct inode *inode,
			  struct buffer_head *fe_bh,
			  struct ocfs2_truncate_context *tc);

int ocfs2_find_leaf(struct inode *inode, struct ocfs2_extent_list *root_el,
		    u32 cpos, struct buffer_head **leaf_bh);

/*
 * Helper function to look at the # of clusters in an extent record.
 */
static inline unsigned int ocfs2_rec_clusters(struct ocfs2_extent_list *el,
					      struct ocfs2_extent_rec *rec)
{
	/*
	 * Cluster count in extent records is slightly different
	 * between interior nodes and leaf nodes. This is to support
	 * unwritten extents which need a flags field in leaf node
	 * records, thus shrinking the available space for a clusters
	 * field.
	 */
	if (el->l_tree_depth)
		return le32_to_cpu(rec->e_int_clusters);
	else
		return le16_to_cpu(rec->e_leaf_clusters);
}

#endif /* OCFS2_ALLOC_H */
