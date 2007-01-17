/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * suballoc.h
 *
 * Defines sub allocator api
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
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

#ifndef _CHAINALLOC_H_
#define _CHAINALLOC_H_

typedef int (group_search_t)(struct inode *,
			     struct buffer_head *,
			     u32,
			     u32,
			     u16 *,
			     u16 *);

struct ocfs2_alloc_context {
	struct inode *ac_inode;    /* which bitmap are we allocating from? */
	struct buffer_head *ac_bh; /* file entry bh */
	u32    ac_bits_wanted;
	u32    ac_bits_given;
#define OCFS2_AC_USE_LOCAL 1
#define OCFS2_AC_USE_MAIN  2
#define OCFS2_AC_USE_INODE 3
#define OCFS2_AC_USE_META  4
	u32    ac_which;

	/* these are used by the chain search */
	u16    ac_chain;
	int    ac_allow_chain_relink;
	group_search_t *ac_group_search;

	u64    ac_last_group;
};

void ocfs2_free_alloc_context(struct ocfs2_alloc_context *ac);
static inline int ocfs2_alloc_context_bits_left(struct ocfs2_alloc_context *ac)
{
	return ac->ac_bits_wanted - ac->ac_bits_given;
}

int ocfs2_reserve_new_metadata(struct ocfs2_super *osb,
			       struct ocfs2_dinode *fe,
			       struct ocfs2_alloc_context **ac);
int ocfs2_reserve_new_inode(struct ocfs2_super *osb,
			    struct ocfs2_alloc_context **ac);
int ocfs2_reserve_clusters(struct ocfs2_super *osb,
			   u32 bits_wanted,
			   struct ocfs2_alloc_context **ac);

int ocfs2_claim_metadata(struct ocfs2_super *osb,
			 handle_t *handle,
			 struct ocfs2_alloc_context *ac,
			 u32 bits_wanted,
			 u16 *suballoc_bit_start,
			 u32 *num_bits,
			 u64 *blkno_start);
int ocfs2_claim_new_inode(struct ocfs2_super *osb,
			  handle_t *handle,
			  struct ocfs2_alloc_context *ac,
			  u16 *suballoc_bit,
			  u64 *fe_blkno);
int ocfs2_claim_clusters(struct ocfs2_super *osb,
			 handle_t *handle,
			 struct ocfs2_alloc_context *ac,
			 u32 min_clusters,
			 u32 *cluster_start,
			 u32 *num_clusters);

int ocfs2_free_dinode(handle_t *handle,
		      struct inode *inode_alloc_inode,
		      struct buffer_head *inode_alloc_bh,
		      struct ocfs2_dinode *di);
int ocfs2_free_extent_block(handle_t *handle,
			    struct inode *eb_alloc_inode,
			    struct buffer_head *eb_alloc_bh,
			    struct ocfs2_extent_block *eb);
int ocfs2_free_clusters(handle_t *handle,
			struct inode *bitmap_inode,
			struct buffer_head *bitmap_bh,
			u64 start_blk,
			unsigned int num_clusters);

static inline u32 ocfs2_cluster_from_desc(struct ocfs2_super *osb,
					  u64 bg_blkno)
{
	/* This should work for all block group descriptors as only
	 * the 1st group descriptor of the cluster bitmap is
	 * different. */

	if (bg_blkno == osb->first_cluster_group_blkno)
		return 0;

	/* the rest of the block groups are located at the beginning
	 * of their 1st cluster, so a direct translation just
	 * works. */
	return ocfs2_blocks_to_clusters(osb->sb, bg_blkno);
}

static inline int ocfs2_is_cluster_bitmap(struct inode *inode)
{
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	return osb->bitmap_blkno == OCFS2_I(inode)->ip_blkno;
}

/* This is for local alloc ONLY. Others should use the task-specific
 * apis above. */
int ocfs2_reserve_cluster_bitmap_bits(struct ocfs2_super *osb,
				      struct ocfs2_alloc_context *ac);

#endif /* _CHAINALLOC_H_ */
