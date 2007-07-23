/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * extent_map.c
 *
 * Block/Cluster mapping functions
 *
 * Copyright (C) 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License, version 2,  as published by the Free Software Foundation.
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
#include <linux/init.h>
#include <linux/types.h>

#define MLOG_MASK_PREFIX ML_EXTENT_MAP
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "extent_map.h"
#include "inode.h"
#include "super.h"

#include "buffer_head_io.h"

/*
 * The extent caching implementation is intentionally trivial.
 *
 * We only cache a small number of extents stored directly on the
 * inode, so linear order operations are acceptable. If we ever want
 * to increase the size of the extent map, then these algorithms must
 * get smarter.
 */

void ocfs2_extent_map_init(struct inode *inode)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);

	oi->ip_extent_map.em_num_items = 0;
	INIT_LIST_HEAD(&oi->ip_extent_map.em_list);
}

static void __ocfs2_extent_map_lookup(struct ocfs2_extent_map *em,
				      unsigned int cpos,
				      struct ocfs2_extent_map_item **ret_emi)
{
	unsigned int range;
	struct ocfs2_extent_map_item *emi;

	*ret_emi = NULL;

	list_for_each_entry(emi, &em->em_list, ei_list) {
		range = emi->ei_cpos + emi->ei_clusters;

		if (cpos >= emi->ei_cpos && cpos < range) {
			list_move(&emi->ei_list, &em->em_list);

			*ret_emi = emi;
			break;
		}
	}
}

static int ocfs2_extent_map_lookup(struct inode *inode, unsigned int cpos,
				   unsigned int *phys, unsigned int *len,
				   unsigned int *flags)
{
	unsigned int coff;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_extent_map_item *emi;

	spin_lock(&oi->ip_lock);

	__ocfs2_extent_map_lookup(&oi->ip_extent_map, cpos, &emi);
	if (emi) {
		coff = cpos - emi->ei_cpos;
		*phys = emi->ei_phys + coff;
		if (len)
			*len = emi->ei_clusters - coff;
		if (flags)
			*flags = emi->ei_flags;
	}

	spin_unlock(&oi->ip_lock);

	if (emi == NULL)
		return -ENOENT;

	return 0;
}

/*
 * Forget about all clusters equal to or greater than cpos.
 */
void ocfs2_extent_map_trunc(struct inode *inode, unsigned int cpos)
{
	struct ocfs2_extent_map_item *emi, *n;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_extent_map *em = &oi->ip_extent_map;
	LIST_HEAD(tmp_list);
	unsigned int range;

	spin_lock(&oi->ip_lock);
	list_for_each_entry_safe(emi, n, &em->em_list, ei_list) {
		if (emi->ei_cpos >= cpos) {
			/* Full truncate of this record. */
			list_move(&emi->ei_list, &tmp_list);
			BUG_ON(em->em_num_items == 0);
			em->em_num_items--;
			continue;
		}

		range = emi->ei_cpos + emi->ei_clusters;
		if (range > cpos) {
			/* Partial truncate */
			emi->ei_clusters = cpos - emi->ei_cpos;
		}
	}
	spin_unlock(&oi->ip_lock);

	list_for_each_entry_safe(emi, n, &tmp_list, ei_list) {
		list_del(&emi->ei_list);
		kfree(emi);
	}
}

/*
 * Is any part of emi2 contained within emi1
 */
static int ocfs2_ei_is_contained(struct ocfs2_extent_map_item *emi1,
				 struct ocfs2_extent_map_item *emi2)
{
	unsigned int range1, range2;

	/*
	 * Check if logical start of emi2 is inside emi1
	 */
	range1 = emi1->ei_cpos + emi1->ei_clusters;
	if (emi2->ei_cpos >= emi1->ei_cpos && emi2->ei_cpos < range1)
		return 1;

	/*
	 * Check if logical end of emi2 is inside emi1
	 */
	range2 = emi2->ei_cpos + emi2->ei_clusters;
	if (range2 > emi1->ei_cpos && range2 <= range1)
		return 1;

	return 0;
}

static void ocfs2_copy_emi_fields(struct ocfs2_extent_map_item *dest,
				  struct ocfs2_extent_map_item *src)
{
	dest->ei_cpos = src->ei_cpos;
	dest->ei_phys = src->ei_phys;
	dest->ei_clusters = src->ei_clusters;
	dest->ei_flags = src->ei_flags;
}

/*
 * Try to merge emi with ins. Returns 1 if merge succeeds, zero
 * otherwise.
 */
static int ocfs2_try_to_merge_extent_map(struct ocfs2_extent_map_item *emi,
					 struct ocfs2_extent_map_item *ins)
{
	/*
	 * Handle contiguousness
	 */
	if (ins->ei_phys == (emi->ei_phys + emi->ei_clusters) &&
	    ins->ei_cpos == (emi->ei_cpos + emi->ei_clusters) &&
	    ins->ei_flags == emi->ei_flags) {
		emi->ei_clusters += ins->ei_clusters;
		return 1;
	} else if ((ins->ei_phys + ins->ei_clusters) == emi->ei_phys &&
		   (ins->ei_cpos + ins->ei_clusters) == emi->ei_phys &&
		   ins->ei_flags == emi->ei_flags) {
		emi->ei_phys = ins->ei_phys;
		emi->ei_cpos = ins->ei_cpos;
		emi->ei_clusters += ins->ei_clusters;
		return 1;
	}

	/*
	 * Overlapping extents - this shouldn't happen unless we've
	 * split an extent to change it's flags. That is exceedingly
	 * rare, so there's no sense in trying to optimize it yet.
	 */
	if (ocfs2_ei_is_contained(emi, ins) ||
	    ocfs2_ei_is_contained(ins, emi)) {
		ocfs2_copy_emi_fields(emi, ins);
		return 1;
	}

	/* No merge was possible. */
	return 0;
}

/*
 * In order to reduce complexity on the caller, this insert function
 * is intentionally liberal in what it will accept.
 *
 * The only rule is that the truncate call *must* be used whenever
 * records have been deleted. This avoids inserting overlapping
 * records with different physical mappings.
 */
void ocfs2_extent_map_insert_rec(struct inode *inode,
				 struct ocfs2_extent_rec *rec)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_extent_map *em = &oi->ip_extent_map;
	struct ocfs2_extent_map_item *emi, *new_emi = NULL;
	struct ocfs2_extent_map_item ins;

	ins.ei_cpos = le32_to_cpu(rec->e_cpos);
	ins.ei_phys = ocfs2_blocks_to_clusters(inode->i_sb,
					       le64_to_cpu(rec->e_blkno));
	ins.ei_clusters = le16_to_cpu(rec->e_leaf_clusters);
	ins.ei_flags = rec->e_flags;

search:
	spin_lock(&oi->ip_lock);

	list_for_each_entry(emi, &em->em_list, ei_list) {
		if (ocfs2_try_to_merge_extent_map(emi, &ins)) {
			list_move(&emi->ei_list, &em->em_list);
			spin_unlock(&oi->ip_lock);
			goto out;
		}
	}

	/*
	 * No item could be merged.
	 *
	 * Either allocate and add a new item, or overwrite the last recently
	 * inserted.
	 */

	if (em->em_num_items < OCFS2_MAX_EXTENT_MAP_ITEMS) {
		if (new_emi == NULL) {
			spin_unlock(&oi->ip_lock);

			new_emi = kmalloc(sizeof(*new_emi), GFP_NOFS);
			if (new_emi == NULL)
				goto out;

			goto search;
		}

		ocfs2_copy_emi_fields(new_emi, &ins);
		list_add(&new_emi->ei_list, &em->em_list);
		em->em_num_items++;
		new_emi = NULL;
	} else {
		BUG_ON(list_empty(&em->em_list) || em->em_num_items == 0);
		emi = list_entry(em->em_list.prev,
				 struct ocfs2_extent_map_item, ei_list);
		list_move(&emi->ei_list, &em->em_list);
		ocfs2_copy_emi_fields(emi, &ins);
	}

	spin_unlock(&oi->ip_lock);

out:
	if (new_emi)
		kfree(new_emi);
}

/*
 * Return the 1st index within el which contains an extent start
 * larger than v_cluster.
 */
static int ocfs2_search_for_hole_index(struct ocfs2_extent_list *el,
				       u32 v_cluster)
{
	int i;
	struct ocfs2_extent_rec *rec;

	for(i = 0; i < le16_to_cpu(el->l_next_free_rec); i++) {
		rec = &el->l_recs[i];

		if (v_cluster < le32_to_cpu(rec->e_cpos))
			break;
	}

	return i;
}

/*
 * Figure out the size of a hole which starts at v_cluster within the given
 * extent list.
 *
 * If there is no more allocation past v_cluster, we return the maximum
 * cluster size minus v_cluster.
 *
 * If we have in-inode extents, then el points to the dinode list and
 * eb_bh is NULL. Otherwise, eb_bh should point to the extent block
 * containing el.
 */
static int ocfs2_figure_hole_clusters(struct inode *inode,
				      struct ocfs2_extent_list *el,
				      struct buffer_head *eb_bh,
				      u32 v_cluster,
				      u32 *num_clusters)
{
	int ret, i;
	struct buffer_head *next_eb_bh = NULL;
	struct ocfs2_extent_block *eb, *next_eb;

	i = ocfs2_search_for_hole_index(el, v_cluster);

	if (i == le16_to_cpu(el->l_next_free_rec) && eb_bh) {
		eb = (struct ocfs2_extent_block *)eb_bh->b_data;

		/*
		 * Check the next leaf for any extents.
		 */

		if (le64_to_cpu(eb->h_next_leaf_blk) == 0ULL)
			goto no_more_extents;

		ret = ocfs2_read_block(OCFS2_SB(inode->i_sb),
				       le64_to_cpu(eb->h_next_leaf_blk),
				       &next_eb_bh, OCFS2_BH_CACHED, inode);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		next_eb = (struct ocfs2_extent_block *)next_eb_bh->b_data;

		if (!OCFS2_IS_VALID_EXTENT_BLOCK(next_eb)) {
			ret = -EROFS;
			OCFS2_RO_ON_INVALID_EXTENT_BLOCK(inode->i_sb, next_eb);
			goto out;
		}

		el = &next_eb->h_list;

		i = ocfs2_search_for_hole_index(el, v_cluster);
	}

no_more_extents:
	if (i == le16_to_cpu(el->l_next_free_rec)) {
		/*
		 * We're at the end of our existing allocation. Just
		 * return the maximum number of clusters we could
		 * possibly allocate.
		 */
		*num_clusters = UINT_MAX - v_cluster;
	} else {
		*num_clusters = le32_to_cpu(el->l_recs[i].e_cpos) - v_cluster;
	}

	ret = 0;
out:
	brelse(next_eb_bh);
	return ret;
}

int ocfs2_get_clusters(struct inode *inode, u32 v_cluster,
		       u32 *p_cluster, u32 *num_clusters,
		       unsigned int *extent_flags)
{
	int ret, i;
	unsigned int flags = 0;
	struct buffer_head *di_bh = NULL;
	struct buffer_head *eb_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;
	u32 coff;

	ret = ocfs2_extent_map_lookup(inode, v_cluster, p_cluster,
				      num_clusters, extent_flags);
	if (ret == 0)
		goto out;

	ret = ocfs2_read_block(OCFS2_SB(inode->i_sb), OCFS2_I(inode)->ip_blkno,
			       &di_bh, OCFS2_BH_CACHED, inode);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	di = (struct ocfs2_dinode *) di_bh->b_data;
	el = &di->id2.i_list;

	if (el->l_tree_depth) {
		ret = ocfs2_find_leaf(inode, el, v_cluster, &eb_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) eb_bh->b_data;
		el = &eb->h_list;

		if (el->l_tree_depth) {
			ocfs2_error(inode->i_sb,
				    "Inode %lu has non zero tree depth in "
				    "leaf block %llu\n", inode->i_ino,
				    (unsigned long long)eb_bh->b_blocknr);
			ret = -EROFS;
			goto out;
		}
	}

	i = ocfs2_search_extent_list(el, v_cluster);
	if (i == -1) {
		/*
		 * A hole was found. Return some canned values that
		 * callers can key on. If asked for, num_clusters will
		 * be populated with the size of the hole.
		 */
		*p_cluster = 0;
		if (num_clusters) {
			ret = ocfs2_figure_hole_clusters(inode, el, eb_bh,
							 v_cluster,
							 num_clusters);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}
	} else {
		rec = &el->l_recs[i];

		BUG_ON(v_cluster < le32_to_cpu(rec->e_cpos));

		if (!rec->e_blkno) {
			ocfs2_error(inode->i_sb, "Inode %lu has bad extent "
				    "record (%u, %u, 0)", inode->i_ino,
				    le32_to_cpu(rec->e_cpos),
				    ocfs2_rec_clusters(el, rec));
			ret = -EROFS;
			goto out;
		}

		coff = v_cluster - le32_to_cpu(rec->e_cpos);

		*p_cluster = ocfs2_blocks_to_clusters(inode->i_sb,
						    le64_to_cpu(rec->e_blkno));
		*p_cluster = *p_cluster + coff;

		if (num_clusters)
			*num_clusters = ocfs2_rec_clusters(el, rec) - coff;

		flags = rec->e_flags;

		ocfs2_extent_map_insert_rec(inode, rec);
	}

	if (extent_flags)
		*extent_flags = flags;

out:
	brelse(di_bh);
	brelse(eb_bh);
	return ret;
}

/*
 * This expects alloc_sem to be held. The allocation cannot change at
 * all while the map is in the process of being updated.
 */
int ocfs2_extent_map_get_blocks(struct inode *inode, u64 v_blkno, u64 *p_blkno,
				u64 *ret_count, unsigned int *extent_flags)
{
	int ret;
	int bpc = ocfs2_clusters_to_blocks(inode->i_sb, 1);
	u32 cpos, num_clusters, p_cluster;
	u64 boff = 0;

	cpos = ocfs2_blocks_to_clusters(inode->i_sb, v_blkno);

	ret = ocfs2_get_clusters(inode, cpos, &p_cluster, &num_clusters,
				 extent_flags);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * p_cluster == 0 indicates a hole.
	 */
	if (p_cluster) {
		boff = ocfs2_clusters_to_blocks(inode->i_sb, p_cluster);
		boff += (v_blkno & (u64)(bpc - 1));
	}

	*p_blkno = boff;

	if (ret_count) {
		*ret_count = ocfs2_clusters_to_blocks(inode->i_sb, num_clusters);
		*ret_count -= v_blkno & (u64)(bpc - 1);
	}

out:
	return ret;
}
