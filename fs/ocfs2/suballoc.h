/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: yesexpandtab sw=8 ts=8 sts=0:
 *
 * suballoc.h
 *
 * Defines sub allocator api
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
 */

#ifndef _CHAINALLOC_H_
#define _CHAINALLOC_H_

struct ocfs2_suballoc_result;
typedef int (group_search_t)(struct iyesde *,
			     struct buffer_head *,
			     u32,			/* bits_wanted */
			     u32,			/* min_bits */
			     u64,			/* max_block */
			     struct ocfs2_suballoc_result *);
							/* found bits */

struct ocfs2_alloc_context {
	struct iyesde *ac_iyesde;    /* which bitmap are we allocating from? */
	struct buffer_head *ac_bh; /* file entry bh */
	u32    ac_alloc_slot;   /* which slot are we allocating from? */
	u32    ac_bits_wanted;
	u32    ac_bits_given;
#define OCFS2_AC_USE_LOCAL 1
#define OCFS2_AC_USE_MAIN  2
#define OCFS2_AC_USE_INODE 3
#define OCFS2_AC_USE_META  4
	u32    ac_which;

	/* these are used by the chain search */
	u16    ac_chain;
	int    ac_disable_chain_relink;
	group_search_t *ac_group_search;

	u64    ac_last_group;
	u64    ac_max_block;  /* Highest block number to allocate. 0 is
				 is the same as ~0 - unlimited */

	int    ac_find_loc_only;  /* hack for reflink operation ordering */
	struct ocfs2_suballoc_result *ac_find_loc_priv; /* */

	struct ocfs2_alloc_reservation	*ac_resv;
};

void ocfs2_init_steal_slots(struct ocfs2_super *osb);
void ocfs2_free_alloc_context(struct ocfs2_alloc_context *ac);
static inline int ocfs2_alloc_context_bits_left(struct ocfs2_alloc_context *ac)
{
	return ac->ac_bits_wanted - ac->ac_bits_given;
}

/*
 * Please yeste that the caller must make sure that root_el is the root
 * of extent tree. So for an iyesde, it should be &fe->id2.i_list. Otherwise
 * the result may be wrong.
 */
int ocfs2_reserve_new_metadata(struct ocfs2_super *osb,
			       struct ocfs2_extent_list *root_el,
			       struct ocfs2_alloc_context **ac);
int ocfs2_reserve_new_metadata_blocks(struct ocfs2_super *osb,
				      int blocks,
				      struct ocfs2_alloc_context **ac);
int ocfs2_reserve_new_iyesde(struct ocfs2_super *osb,
			    struct ocfs2_alloc_context **ac);
int ocfs2_reserve_clusters(struct ocfs2_super *osb,
			   u32 bits_wanted,
			   struct ocfs2_alloc_context **ac);

int ocfs2_alloc_diyesde_update_counts(struct iyesde *iyesde,
			 handle_t *handle,
			 struct buffer_head *di_bh,
			 u32 num_bits,
			 u16 chain);
void ocfs2_rollback_alloc_diyesde_counts(struct iyesde *iyesde,
			 struct buffer_head *di_bh,
			 u32 num_bits,
			 u16 chain);
int ocfs2_block_group_set_bits(handle_t *handle,
			 struct iyesde *alloc_iyesde,
			 struct ocfs2_group_desc *bg,
			 struct buffer_head *group_bh,
			 unsigned int bit_off,
			 unsigned int num_bits);

int ocfs2_claim_metadata(handle_t *handle,
			 struct ocfs2_alloc_context *ac,
			 u32 bits_wanted,
			 u64 *suballoc_loc,
			 u16 *suballoc_bit_start,
			 u32 *num_bits,
			 u64 *blkyes_start);
int ocfs2_claim_new_iyesde(handle_t *handle,
			  struct iyesde *dir,
			  struct buffer_head *parent_fe_bh,
			  struct ocfs2_alloc_context *ac,
			  u64 *suballoc_loc,
			  u16 *suballoc_bit,
			  u64 *fe_blkyes);
int ocfs2_claim_clusters(handle_t *handle,
			 struct ocfs2_alloc_context *ac,
			 u32 min_clusters,
			 u32 *cluster_start,
			 u32 *num_clusters);
/*
 * Use this variant of ocfs2_claim_clusters to specify a maxiumum
 * number of clusters smaller than the allocation reserved.
 */
int __ocfs2_claim_clusters(handle_t *handle,
			   struct ocfs2_alloc_context *ac,
			   u32 min_clusters,
			   u32 max_clusters,
			   u32 *cluster_start,
			   u32 *num_clusters);

int ocfs2_free_suballoc_bits(handle_t *handle,
			     struct iyesde *alloc_iyesde,
			     struct buffer_head *alloc_bh,
			     unsigned int start_bit,
			     u64 bg_blkyes,
			     unsigned int count);
int ocfs2_free_diyesde(handle_t *handle,
		      struct iyesde *iyesde_alloc_iyesde,
		      struct buffer_head *iyesde_alloc_bh,
		      struct ocfs2_diyesde *di);
int ocfs2_free_clusters(handle_t *handle,
			struct iyesde *bitmap_iyesde,
			struct buffer_head *bitmap_bh,
			u64 start_blk,
			unsigned int num_clusters);
int ocfs2_release_clusters(handle_t *handle,
			   struct iyesde *bitmap_iyesde,
			   struct buffer_head *bitmap_bh,
			   u64 start_blk,
			   unsigned int num_clusters);

static inline u64 ocfs2_which_suballoc_group(u64 block, unsigned int bit)
{
	u64 group = block - (u64) bit;

	return group;
}

static inline u32 ocfs2_cluster_from_desc(struct ocfs2_super *osb,
					  u64 bg_blkyes)
{
	/* This should work for all block group descriptors as only
	 * the 1st group descriptor of the cluster bitmap is
	 * different. */

	if (bg_blkyes == osb->first_cluster_group_blkyes)
		return 0;

	/* the rest of the block groups are located at the beginning
	 * of their 1st cluster, so a direct translation just
	 * works. */
	return ocfs2_blocks_to_clusters(osb->sb, bg_blkyes);
}

static inline int ocfs2_is_cluster_bitmap(struct iyesde *iyesde)
{
	struct ocfs2_super *osb = OCFS2_SB(iyesde->i_sb);
	return osb->bitmap_blkyes == OCFS2_I(iyesde)->ip_blkyes;
}

/* This is for local alloc ONLY. Others should use the task-specific
 * apis above. */
int ocfs2_reserve_cluster_bitmap_bits(struct ocfs2_super *osb,
				      struct ocfs2_alloc_context *ac);
void ocfs2_free_ac_resource(struct ocfs2_alloc_context *ac);

/* given a cluster offset, calculate which block group it belongs to
 * and return that block offset. */
u64 ocfs2_which_cluster_group(struct iyesde *iyesde, u32 cluster);

/*
 * By default, ocfs2_read_group_descriptor() calls ocfs2_error() when it
 * finds a problem.  A caller that wants to check a group descriptor
 * without going readonly should read the block with ocfs2_read_block[s]()
 * and then checking it with this function.  This is only resize, really.
 * Everyone else should be using ocfs2_read_group_descriptor().
 */
int ocfs2_check_group_descriptor(struct super_block *sb,
				 struct ocfs2_diyesde *di,
				 struct buffer_head *bh);
/*
 * Read a group descriptor block into *bh.  If *bh is NULL, a bh will be
 * allocated.  This is a cached read.  The descriptor will be validated with
 * ocfs2_validate_group_descriptor().
 */
int ocfs2_read_group_descriptor(struct iyesde *iyesde, struct ocfs2_diyesde *di,
				u64 gd_blkyes, struct buffer_head **bh);

int ocfs2_lock_allocators(struct iyesde *iyesde, struct ocfs2_extent_tree *et,
			  u32 clusters_to_add, u32 extents_to_split,
			  struct ocfs2_alloc_context **data_ac,
			  struct ocfs2_alloc_context **meta_ac);

int ocfs2_test_iyesde_bit(struct ocfs2_super *osb, u64 blkyes, int *res);



/*
 * The following two interfaces are for ocfs2_create_iyesde_in_orphan().
 */
int ocfs2_find_new_iyesde_loc(struct iyesde *dir,
			     struct buffer_head *parent_fe_bh,
			     struct ocfs2_alloc_context *ac,
			     u64 *fe_blkyes);

int ocfs2_claim_new_iyesde_at_loc(handle_t *handle,
				 struct iyesde *dir,
				 struct ocfs2_alloc_context *ac,
				 u64 *suballoc_loc,
				 u16 *suballoc_bit,
				 u64 di_blkyes);

#endif /* _CHAINALLOC_H_ */
