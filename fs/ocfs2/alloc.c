// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * alloc.c
 *
 * Extent allocs and frees
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/swap.h>
#include <linux/quotaops.h>
#include <linux/blkdev.h>
#include <linux/sched/signal.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "aops.h"
#include "blockcheck.h"
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
#include "xattr.h"
#include "refcounttree.h"
#include "ocfs2_trace.h"

#include "buffer_head_io.h"

enum ocfs2_contig_type {
	CONTIG_NONE = 0,
	CONTIG_LEFT,
	CONTIG_RIGHT,
	CONTIG_LEFTRIGHT,
};

static enum ocfs2_contig_type
	ocfs2_extent_rec_contig(struct super_block *sb,
				struct ocfs2_extent_rec *ext,
				struct ocfs2_extent_rec *insert_rec);
/*
 * Operations for a specific extent tree type.
 *
 * To implement an on-disk btree (extent tree) type in ocfs2, add
 * an ocfs2_extent_tree_operations structure and the matching
 * ocfs2_init_<thingy>_extent_tree() function.  That's pretty much it
 * for the allocation portion of the extent tree.
 */
struct ocfs2_extent_tree_operations {
	/*
	 * last_eb_blk is the block number of the right most leaf extent
	 * block.  Most on-disk structures containing an extent tree store
	 * this value for fast access.  The ->eo_set_last_eb_blk() and
	 * ->eo_get_last_eb_blk() operations access this value.  They are
	 *  both required.
	 */
	void (*eo_set_last_eb_blk)(struct ocfs2_extent_tree *et,
				   u64 blkno);
	u64 (*eo_get_last_eb_blk)(struct ocfs2_extent_tree *et);

	/*
	 * The on-disk structure usually keeps track of how many total
	 * clusters are stored in this extent tree.  This function updates
	 * that value.  new_clusters is the delta, and must be
	 * added to the total.  Required.
	 */
	void (*eo_update_clusters)(struct ocfs2_extent_tree *et,
				   u32 new_clusters);

	/*
	 * If this extent tree is supported by an extent map, insert
	 * a record into the map.
	 */
	void (*eo_extent_map_insert)(struct ocfs2_extent_tree *et,
				     struct ocfs2_extent_rec *rec);

	/*
	 * If this extent tree is supported by an extent map, truncate the
	 * map to clusters,
	 */
	void (*eo_extent_map_truncate)(struct ocfs2_extent_tree *et,
				       u32 clusters);

	/*
	 * If ->eo_insert_check() exists, it is called before rec is
	 * inserted into the extent tree.  It is optional.
	 */
	int (*eo_insert_check)(struct ocfs2_extent_tree *et,
			       struct ocfs2_extent_rec *rec);
	int (*eo_sanity_check)(struct ocfs2_extent_tree *et);

	/*
	 * --------------------------------------------------------------
	 * The remaining are internal to ocfs2_extent_tree and don't have
	 * accessor functions
	 */

	/*
	 * ->eo_fill_root_el() takes et->et_object and sets et->et_root_el.
	 * It is required.
	 */
	void (*eo_fill_root_el)(struct ocfs2_extent_tree *et);

	/*
	 * ->eo_fill_max_leaf_clusters sets et->et_max_leaf_clusters if
	 * it exists.  If it does not, et->et_max_leaf_clusters is set
	 * to 0 (unlimited).  Optional.
	 */
	void (*eo_fill_max_leaf_clusters)(struct ocfs2_extent_tree *et);

	/*
	 * ->eo_extent_contig test whether the 2 ocfs2_extent_rec
	 * are contiguous or not. Optional. Don't need to set it if use
	 * ocfs2_extent_rec as the tree leaf.
	 */
	enum ocfs2_contig_type
		(*eo_extent_contig)(struct ocfs2_extent_tree *et,
				    struct ocfs2_extent_rec *ext,
				    struct ocfs2_extent_rec *insert_rec);
};


/*
 * Pre-declare ocfs2_dinode_et_ops so we can use it as a sanity check
 * in the methods.
 */
static u64 ocfs2_dinode_get_last_eb_blk(struct ocfs2_extent_tree *et);
static void ocfs2_dinode_set_last_eb_blk(struct ocfs2_extent_tree *et,
					 u64 blkno);
static void ocfs2_dinode_update_clusters(struct ocfs2_extent_tree *et,
					 u32 clusters);
static void ocfs2_dinode_extent_map_insert(struct ocfs2_extent_tree *et,
					   struct ocfs2_extent_rec *rec);
static void ocfs2_dinode_extent_map_truncate(struct ocfs2_extent_tree *et,
					     u32 clusters);
static int ocfs2_dinode_insert_check(struct ocfs2_extent_tree *et,
				     struct ocfs2_extent_rec *rec);
static int ocfs2_dinode_sanity_check(struct ocfs2_extent_tree *et);
static void ocfs2_dinode_fill_root_el(struct ocfs2_extent_tree *et);

static int ocfs2_reuse_blk_from_dealloc(handle_t *handle,
					struct ocfs2_extent_tree *et,
					struct buffer_head **new_eb_bh,
					int blk_wanted, int *blk_given);
static int ocfs2_is_dealloc_empty(struct ocfs2_extent_tree *et);

static const struct ocfs2_extent_tree_operations ocfs2_dinode_et_ops = {
	.eo_set_last_eb_blk	= ocfs2_dinode_set_last_eb_blk,
	.eo_get_last_eb_blk	= ocfs2_dinode_get_last_eb_blk,
	.eo_update_clusters	= ocfs2_dinode_update_clusters,
	.eo_extent_map_insert	= ocfs2_dinode_extent_map_insert,
	.eo_extent_map_truncate	= ocfs2_dinode_extent_map_truncate,
	.eo_insert_check	= ocfs2_dinode_insert_check,
	.eo_sanity_check	= ocfs2_dinode_sanity_check,
	.eo_fill_root_el	= ocfs2_dinode_fill_root_el,
};

static void ocfs2_dinode_set_last_eb_blk(struct ocfs2_extent_tree *et,
					 u64 blkno)
{
	struct ocfs2_dinode *di = et->et_object;

	BUG_ON(et->et_ops != &ocfs2_dinode_et_ops);
	di->i_last_eb_blk = cpu_to_le64(blkno);
}

static u64 ocfs2_dinode_get_last_eb_blk(struct ocfs2_extent_tree *et)
{
	struct ocfs2_dinode *di = et->et_object;

	BUG_ON(et->et_ops != &ocfs2_dinode_et_ops);
	return le64_to_cpu(di->i_last_eb_blk);
}

static void ocfs2_dinode_update_clusters(struct ocfs2_extent_tree *et,
					 u32 clusters)
{
	struct ocfs2_inode_info *oi = cache_info_to_inode(et->et_ci);
	struct ocfs2_dinode *di = et->et_object;

	le32_add_cpu(&di->i_clusters, clusters);
	spin_lock(&oi->ip_lock);
	oi->ip_clusters = le32_to_cpu(di->i_clusters);
	spin_unlock(&oi->ip_lock);
}

static void ocfs2_dinode_extent_map_insert(struct ocfs2_extent_tree *et,
					   struct ocfs2_extent_rec *rec)
{
	struct inode *inode = &cache_info_to_inode(et->et_ci)->vfs_inode;

	ocfs2_extent_map_insert_rec(inode, rec);
}

static void ocfs2_dinode_extent_map_truncate(struct ocfs2_extent_tree *et,
					     u32 clusters)
{
	struct inode *inode = &cache_info_to_inode(et->et_ci)->vfs_inode;

	ocfs2_extent_map_trunc(inode, clusters);
}

static int ocfs2_dinode_insert_check(struct ocfs2_extent_tree *et,
				     struct ocfs2_extent_rec *rec)
{
	struct ocfs2_inode_info *oi = cache_info_to_inode(et->et_ci);
	struct ocfs2_super *osb = OCFS2_SB(oi->vfs_inode.i_sb);

	BUG_ON(oi->ip_dyn_features & OCFS2_INLINE_DATA_FL);
	mlog_bug_on_msg(!ocfs2_sparse_alloc(osb) &&
			(oi->ip_clusters != le32_to_cpu(rec->e_cpos)),
			"Device %s, asking for sparse allocation: inode %llu, "
			"cpos %u, clusters %u\n",
			osb->dev_str,
			(unsigned long long)oi->ip_blkno,
			rec->e_cpos, oi->ip_clusters);

	return 0;
}

static int ocfs2_dinode_sanity_check(struct ocfs2_extent_tree *et)
{
	struct ocfs2_dinode *di = et->et_object;

	BUG_ON(et->et_ops != &ocfs2_dinode_et_ops);
	BUG_ON(!OCFS2_IS_VALID_DINODE(di));

	return 0;
}

static void ocfs2_dinode_fill_root_el(struct ocfs2_extent_tree *et)
{
	struct ocfs2_dinode *di = et->et_object;

	et->et_root_el = &di->id2.i_list;
}


static void ocfs2_xattr_value_fill_root_el(struct ocfs2_extent_tree *et)
{
	struct ocfs2_xattr_value_buf *vb = et->et_object;

	et->et_root_el = &vb->vb_xv->xr_list;
}

static void ocfs2_xattr_value_set_last_eb_blk(struct ocfs2_extent_tree *et,
					      u64 blkno)
{
	struct ocfs2_xattr_value_buf *vb = et->et_object;

	vb->vb_xv->xr_last_eb_blk = cpu_to_le64(blkno);
}

static u64 ocfs2_xattr_value_get_last_eb_blk(struct ocfs2_extent_tree *et)
{
	struct ocfs2_xattr_value_buf *vb = et->et_object;

	return le64_to_cpu(vb->vb_xv->xr_last_eb_blk);
}

static void ocfs2_xattr_value_update_clusters(struct ocfs2_extent_tree *et,
					      u32 clusters)
{
	struct ocfs2_xattr_value_buf *vb = et->et_object;

	le32_add_cpu(&vb->vb_xv->xr_clusters, clusters);
}

static const struct ocfs2_extent_tree_operations ocfs2_xattr_value_et_ops = {
	.eo_set_last_eb_blk	= ocfs2_xattr_value_set_last_eb_blk,
	.eo_get_last_eb_blk	= ocfs2_xattr_value_get_last_eb_blk,
	.eo_update_clusters	= ocfs2_xattr_value_update_clusters,
	.eo_fill_root_el	= ocfs2_xattr_value_fill_root_el,
};

static void ocfs2_xattr_tree_fill_root_el(struct ocfs2_extent_tree *et)
{
	struct ocfs2_xattr_block *xb = et->et_object;

	et->et_root_el = &xb->xb_attrs.xb_root.xt_list;
}

static void ocfs2_xattr_tree_fill_max_leaf_clusters(struct ocfs2_extent_tree *et)
{
	struct super_block *sb = ocfs2_metadata_cache_get_super(et->et_ci);
	et->et_max_leaf_clusters =
		ocfs2_clusters_for_bytes(sb, OCFS2_MAX_XATTR_TREE_LEAF_SIZE);
}

static void ocfs2_xattr_tree_set_last_eb_blk(struct ocfs2_extent_tree *et,
					     u64 blkno)
{
	struct ocfs2_xattr_block *xb = et->et_object;
	struct ocfs2_xattr_tree_root *xt = &xb->xb_attrs.xb_root;

	xt->xt_last_eb_blk = cpu_to_le64(blkno);
}

static u64 ocfs2_xattr_tree_get_last_eb_blk(struct ocfs2_extent_tree *et)
{
	struct ocfs2_xattr_block *xb = et->et_object;
	struct ocfs2_xattr_tree_root *xt = &xb->xb_attrs.xb_root;

	return le64_to_cpu(xt->xt_last_eb_blk);
}

static void ocfs2_xattr_tree_update_clusters(struct ocfs2_extent_tree *et,
					     u32 clusters)
{
	struct ocfs2_xattr_block *xb = et->et_object;

	le32_add_cpu(&xb->xb_attrs.xb_root.xt_clusters, clusters);
}

static const struct ocfs2_extent_tree_operations ocfs2_xattr_tree_et_ops = {
	.eo_set_last_eb_blk	= ocfs2_xattr_tree_set_last_eb_blk,
	.eo_get_last_eb_blk	= ocfs2_xattr_tree_get_last_eb_blk,
	.eo_update_clusters	= ocfs2_xattr_tree_update_clusters,
	.eo_fill_root_el	= ocfs2_xattr_tree_fill_root_el,
	.eo_fill_max_leaf_clusters = ocfs2_xattr_tree_fill_max_leaf_clusters,
};

static void ocfs2_dx_root_set_last_eb_blk(struct ocfs2_extent_tree *et,
					  u64 blkno)
{
	struct ocfs2_dx_root_block *dx_root = et->et_object;

	dx_root->dr_last_eb_blk = cpu_to_le64(blkno);
}

static u64 ocfs2_dx_root_get_last_eb_blk(struct ocfs2_extent_tree *et)
{
	struct ocfs2_dx_root_block *dx_root = et->et_object;

	return le64_to_cpu(dx_root->dr_last_eb_blk);
}

static void ocfs2_dx_root_update_clusters(struct ocfs2_extent_tree *et,
					  u32 clusters)
{
	struct ocfs2_dx_root_block *dx_root = et->et_object;

	le32_add_cpu(&dx_root->dr_clusters, clusters);
}

static int ocfs2_dx_root_sanity_check(struct ocfs2_extent_tree *et)
{
	struct ocfs2_dx_root_block *dx_root = et->et_object;

	BUG_ON(!OCFS2_IS_VALID_DX_ROOT(dx_root));

	return 0;
}

static void ocfs2_dx_root_fill_root_el(struct ocfs2_extent_tree *et)
{
	struct ocfs2_dx_root_block *dx_root = et->et_object;

	et->et_root_el = &dx_root->dr_list;
}

static const struct ocfs2_extent_tree_operations ocfs2_dx_root_et_ops = {
	.eo_set_last_eb_blk	= ocfs2_dx_root_set_last_eb_blk,
	.eo_get_last_eb_blk	= ocfs2_dx_root_get_last_eb_blk,
	.eo_update_clusters	= ocfs2_dx_root_update_clusters,
	.eo_sanity_check	= ocfs2_dx_root_sanity_check,
	.eo_fill_root_el	= ocfs2_dx_root_fill_root_el,
};

static void ocfs2_refcount_tree_fill_root_el(struct ocfs2_extent_tree *et)
{
	struct ocfs2_refcount_block *rb = et->et_object;

	et->et_root_el = &rb->rf_list;
}

static void ocfs2_refcount_tree_set_last_eb_blk(struct ocfs2_extent_tree *et,
						u64 blkno)
{
	struct ocfs2_refcount_block *rb = et->et_object;

	rb->rf_last_eb_blk = cpu_to_le64(blkno);
}

static u64 ocfs2_refcount_tree_get_last_eb_blk(struct ocfs2_extent_tree *et)
{
	struct ocfs2_refcount_block *rb = et->et_object;

	return le64_to_cpu(rb->rf_last_eb_blk);
}

static void ocfs2_refcount_tree_update_clusters(struct ocfs2_extent_tree *et,
						u32 clusters)
{
	struct ocfs2_refcount_block *rb = et->et_object;

	le32_add_cpu(&rb->rf_clusters, clusters);
}

static enum ocfs2_contig_type
ocfs2_refcount_tree_extent_contig(struct ocfs2_extent_tree *et,
				  struct ocfs2_extent_rec *ext,
				  struct ocfs2_extent_rec *insert_rec)
{
	return CONTIG_NONE;
}

static const struct ocfs2_extent_tree_operations ocfs2_refcount_tree_et_ops = {
	.eo_set_last_eb_blk	= ocfs2_refcount_tree_set_last_eb_blk,
	.eo_get_last_eb_blk	= ocfs2_refcount_tree_get_last_eb_blk,
	.eo_update_clusters	= ocfs2_refcount_tree_update_clusters,
	.eo_fill_root_el	= ocfs2_refcount_tree_fill_root_el,
	.eo_extent_contig	= ocfs2_refcount_tree_extent_contig,
};

static void __ocfs2_init_extent_tree(struct ocfs2_extent_tree *et,
				     struct ocfs2_caching_info *ci,
				     struct buffer_head *bh,
				     ocfs2_journal_access_func access,
				     void *obj,
				     const struct ocfs2_extent_tree_operations *ops)
{
	et->et_ops = ops;
	et->et_root_bh = bh;
	et->et_ci = ci;
	et->et_root_journal_access = access;
	if (!obj)
		obj = (void *)bh->b_data;
	et->et_object = obj;
	et->et_dealloc = NULL;

	et->et_ops->eo_fill_root_el(et);
	if (!et->et_ops->eo_fill_max_leaf_clusters)
		et->et_max_leaf_clusters = 0;
	else
		et->et_ops->eo_fill_max_leaf_clusters(et);
}

void ocfs2_init_dinode_extent_tree(struct ocfs2_extent_tree *et,
				   struct ocfs2_caching_info *ci,
				   struct buffer_head *bh)
{
	__ocfs2_init_extent_tree(et, ci, bh, ocfs2_journal_access_di,
				 NULL, &ocfs2_dinode_et_ops);
}

void ocfs2_init_xattr_tree_extent_tree(struct ocfs2_extent_tree *et,
				       struct ocfs2_caching_info *ci,
				       struct buffer_head *bh)
{
	__ocfs2_init_extent_tree(et, ci, bh, ocfs2_journal_access_xb,
				 NULL, &ocfs2_xattr_tree_et_ops);
}

void ocfs2_init_xattr_value_extent_tree(struct ocfs2_extent_tree *et,
					struct ocfs2_caching_info *ci,
					struct ocfs2_xattr_value_buf *vb)
{
	__ocfs2_init_extent_tree(et, ci, vb->vb_bh, vb->vb_access, vb,
				 &ocfs2_xattr_value_et_ops);
}

void ocfs2_init_dx_root_extent_tree(struct ocfs2_extent_tree *et,
				    struct ocfs2_caching_info *ci,
				    struct buffer_head *bh)
{
	__ocfs2_init_extent_tree(et, ci, bh, ocfs2_journal_access_dr,
				 NULL, &ocfs2_dx_root_et_ops);
}

void ocfs2_init_refcount_extent_tree(struct ocfs2_extent_tree *et,
				     struct ocfs2_caching_info *ci,
				     struct buffer_head *bh)
{
	__ocfs2_init_extent_tree(et, ci, bh, ocfs2_journal_access_rb,
				 NULL, &ocfs2_refcount_tree_et_ops);
}

static inline void ocfs2_et_set_last_eb_blk(struct ocfs2_extent_tree *et,
					    u64 new_last_eb_blk)
{
	et->et_ops->eo_set_last_eb_blk(et, new_last_eb_blk);
}

static inline u64 ocfs2_et_get_last_eb_blk(struct ocfs2_extent_tree *et)
{
	return et->et_ops->eo_get_last_eb_blk(et);
}

static inline void ocfs2_et_update_clusters(struct ocfs2_extent_tree *et,
					    u32 clusters)
{
	et->et_ops->eo_update_clusters(et, clusters);
}

static inline void ocfs2_et_extent_map_insert(struct ocfs2_extent_tree *et,
					      struct ocfs2_extent_rec *rec)
{
	if (et->et_ops->eo_extent_map_insert)
		et->et_ops->eo_extent_map_insert(et, rec);
}

static inline void ocfs2_et_extent_map_truncate(struct ocfs2_extent_tree *et,
						u32 clusters)
{
	if (et->et_ops->eo_extent_map_truncate)
		et->et_ops->eo_extent_map_truncate(et, clusters);
}

static inline int ocfs2_et_root_journal_access(handle_t *handle,
					       struct ocfs2_extent_tree *et,
					       int type)
{
	return et->et_root_journal_access(handle, et->et_ci, et->et_root_bh,
					  type);
}

static inline enum ocfs2_contig_type
	ocfs2_et_extent_contig(struct ocfs2_extent_tree *et,
			       struct ocfs2_extent_rec *rec,
			       struct ocfs2_extent_rec *insert_rec)
{
	if (et->et_ops->eo_extent_contig)
		return et->et_ops->eo_extent_contig(et, rec, insert_rec);

	return ocfs2_extent_rec_contig(
				ocfs2_metadata_cache_get_super(et->et_ci),
				rec, insert_rec);
}

static inline int ocfs2_et_insert_check(struct ocfs2_extent_tree *et,
					struct ocfs2_extent_rec *rec)
{
	int ret = 0;

	if (et->et_ops->eo_insert_check)
		ret = et->et_ops->eo_insert_check(et, rec);
	return ret;
}

static inline int ocfs2_et_sanity_check(struct ocfs2_extent_tree *et)
{
	int ret = 0;

	if (et->et_ops->eo_sanity_check)
		ret = et->et_ops->eo_sanity_check(et);
	return ret;
}

static int ocfs2_cache_extent_block_free(struct ocfs2_cached_dealloc_ctxt *ctxt,
					 struct ocfs2_extent_block *eb);
static void ocfs2_adjust_rightmost_records(handle_t *handle,
					   struct ocfs2_extent_tree *et,
					   struct ocfs2_path *path,
					   struct ocfs2_extent_rec *insert_rec);
/*
 * Reset the actual path elements so that we can re-use the structure
 * to build another path. Generally, this involves freeing the buffer
 * heads.
 */
void ocfs2_reinit_path(struct ocfs2_path *path, int keep_root)
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
	else
		path_root_access(path) = NULL;

	path->p_tree_depth = depth;
}

void ocfs2_free_path(struct ocfs2_path *path)
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
	BUG_ON(path_root_access(dest) != path_root_access(src));

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
	BUG_ON(path_root_access(dest) != path_root_access(src));

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
					 struct ocfs2_extent_list *root_el,
					 ocfs2_journal_access_func access)
{
	struct ocfs2_path *path;

	BUG_ON(le16_to_cpu(root_el->l_tree_depth) >= OCFS2_MAX_PATH_DEPTH);

	path = kzalloc(sizeof(*path), GFP_NOFS);
	if (path) {
		path->p_tree_depth = le16_to_cpu(root_el->l_tree_depth);
		get_bh(root_bh);
		path_root_bh(path) = root_bh;
		path_root_el(path) = root_el;
		path_root_access(path) = access;
	}

	return path;
}

struct ocfs2_path *ocfs2_new_path_from_path(struct ocfs2_path *path)
{
	return ocfs2_new_path(path_root_bh(path), path_root_el(path),
			      path_root_access(path));
}

struct ocfs2_path *ocfs2_new_path_from_et(struct ocfs2_extent_tree *et)
{
	return ocfs2_new_path(et->et_root_bh, et->et_root_el,
			      et->et_root_journal_access);
}

/*
 * Journal the buffer at depth idx.  All idx>0 are extent_blocks,
 * otherwise it's the root_access function.
 *
 * I don't like the way this function's name looks next to
 * ocfs2_journal_access_path(), but I don't have a better one.
 */
int ocfs2_path_bh_journal_access(handle_t *handle,
				 struct ocfs2_caching_info *ci,
				 struct ocfs2_path *path,
				 int idx)
{
	ocfs2_journal_access_func access = path_root_access(path);

	if (!access)
		access = ocfs2_journal_access;

	if (idx)
		access = ocfs2_journal_access_eb;

	return access(handle, ci, path->p_node[idx].bh,
		      OCFS2_JOURNAL_ACCESS_WRITE);
}

/*
 * Convenience function to journal all components in a path.
 */
int ocfs2_journal_access_path(struct ocfs2_caching_info *ci,
			      handle_t *handle,
			      struct ocfs2_path *path)
{
	int i, ret = 0;

	if (!path)
		goto out;

	for(i = 0; i < path_num_items(path); i++) {
		ret = ocfs2_path_bh_journal_access(handle, ci, path, i);
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

/*
 * NOTE: ocfs2_block_extent_contig(), ocfs2_extents_adjacent() and
 * ocfs2_extent_rec_contig only work properly against leaf nodes!
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
	ocfs2_extent_rec_contig(struct super_block *sb,
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
	    ocfs2_block_extent_contig(sb, ext, blkno))
			return CONTIG_RIGHT;

	blkno = le64_to_cpu(ext->e_blkno);
	if (ocfs2_extents_adjacent(insert_rec, ext) &&
	    ocfs2_block_extent_contig(sb, insert_rec, blkno))
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
	int			ins_tree_depth;
};

struct ocfs2_merge_ctxt {
	enum ocfs2_contig_type	c_contig_type;
	int			c_has_empty_extent;
	int			c_split_covers_rec;
};

static int ocfs2_validate_extent_block(struct super_block *sb,
				       struct buffer_head *bh)
{
	int rc;
	struct ocfs2_extent_block *eb =
		(struct ocfs2_extent_block *)bh->b_data;

	trace_ocfs2_validate_extent_block((unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * If the ecc fails, we return the error but otherwise
	 * leave the filesystem running.  We know any error is
	 * local to this block.
	 */
	rc = ocfs2_validate_meta_ecc(sb, bh->b_data, &eb->h_check);
	if (rc) {
		mlog(ML_ERROR, "Checksum failed for extent block %llu\n",
		     (unsigned long long)bh->b_blocknr);
		return rc;
	}

	/*
	 * Errors after here are fatal.
	 */

	if (!OCFS2_IS_VALID_EXTENT_BLOCK(eb)) {
		rc = ocfs2_error(sb,
				 "Extent block #%llu has bad signature %.*s\n",
				 (unsigned long long)bh->b_blocknr, 7,
				 eb->h_signature);
		goto bail;
	}

	if (le64_to_cpu(eb->h_blkno) != bh->b_blocknr) {
		rc = ocfs2_error(sb,
				 "Extent block #%llu has an invalid h_blkno of %llu\n",
				 (unsigned long long)bh->b_blocknr,
				 (unsigned long long)le64_to_cpu(eb->h_blkno));
		goto bail;
	}

	if (le32_to_cpu(eb->h_fs_generation) != OCFS2_SB(sb)->fs_generation)
		rc = ocfs2_error(sb,
				 "Extent block #%llu has an invalid h_fs_generation of #%u\n",
				 (unsigned long long)bh->b_blocknr,
				 le32_to_cpu(eb->h_fs_generation));
bail:
	return rc;
}

int ocfs2_read_extent_block(struct ocfs2_caching_info *ci, u64 eb_blkno,
			    struct buffer_head **bh)
{
	int rc;
	struct buffer_head *tmp = *bh;

	rc = ocfs2_read_block(ci, eb_blkno, &tmp,
			      ocfs2_validate_extent_block);

	/* If ocfs2_read_block() got us a new bh, pass it up. */
	if (!rc && !*bh)
		*bh = tmp;

	return rc;
}


/*
 * How many free extents have we got before we need more meta data?
 */
int ocfs2_num_free_extents(struct ocfs2_extent_tree *et)
{
	int retval;
	struct ocfs2_extent_list *el = NULL;
	struct ocfs2_extent_block *eb;
	struct buffer_head *eb_bh = NULL;
	u64 last_eb_blk = 0;

	el = et->et_root_el;
	last_eb_blk = ocfs2_et_get_last_eb_blk(et);

	if (last_eb_blk) {
		retval = ocfs2_read_extent_block(et->et_ci, last_eb_blk,
						 &eb_bh);
		if (retval < 0) {
			mlog_errno(retval);
			goto bail;
		}
		eb = (struct ocfs2_extent_block *) eb_bh->b_data;
		el = &eb->h_list;
	}

	BUG_ON(el->l_tree_depth != 0);

	retval = le16_to_cpu(el->l_count) - le16_to_cpu(el->l_next_free_rec);
bail:
	brelse(eb_bh);

	trace_ocfs2_num_free_extents(retval);
	return retval;
}

/* expects array to already be allocated
 *
 * sets h_signature, h_blkno, h_suballoc_bit, h_suballoc_slot, and
 * l_count for you
 */
static int ocfs2_create_new_meta_bhs(handle_t *handle,
				     struct ocfs2_extent_tree *et,
				     int wanted,
				     struct ocfs2_alloc_context *meta_ac,
				     struct buffer_head *bhs[])
{
	int count, status, i;
	u16 suballoc_bit_start;
	u32 num_got;
	u64 suballoc_loc, first_blkno;
	struct ocfs2_super *osb =
		OCFS2_SB(ocfs2_metadata_cache_get_super(et->et_ci));
	struct ocfs2_extent_block *eb;

	count = 0;
	while (count < wanted) {
		status = ocfs2_claim_metadata(handle,
					      meta_ac,
					      wanted - count,
					      &suballoc_loc,
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
				status = -ENOMEM;
				mlog_errno(status);
				goto bail;
			}
			ocfs2_set_new_buffer_uptodate(et->et_ci, bhs[i]);

			status = ocfs2_journal_access_eb(handle, et->et_ci,
							 bhs[i],
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
			eb->h_suballoc_slot =
				cpu_to_le16(meta_ac->ac_alloc_slot);
			eb->h_suballoc_loc = cpu_to_le64(suballoc_loc);
			eb->h_suballoc_bit = cpu_to_le16(suballoc_bit_start);
			eb->h_list.l_count =
				cpu_to_le16(ocfs2_extent_recs_per_eb(osb->sb));

			suballoc_bit_start++;
			first_blkno++;

			/* We'll also be dirtied by the caller, so
			 * this isn't absolutely necessary. */
			ocfs2_journal_dirty(handle, bhs[i]);
		}

		count += num_got;
	}

	status = 0;
bail:
	if (status < 0) {
		for(i = 0; i < wanted; i++) {
			brelse(bhs[i]);
			bhs[i] = NULL;
		}
	}
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
 * Change range of the branches in the right most path according to the leaf
 * extent block's rightmost record.
 */
static int ocfs2_adjust_rightmost_branch(handle_t *handle,
					 struct ocfs2_extent_tree *et)
{
	int status;
	struct ocfs2_path *path = NULL;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;

	path = ocfs2_new_path_from_et(et);
	if (!path) {
		status = -ENOMEM;
		return status;
	}

	status = ocfs2_find_path(et->et_ci, path, UINT_MAX);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_extend_trans(handle, path_num_items(path));
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_journal_access_path(et->et_ci, handle, path);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	el = path_leaf_el(path);
	rec = &el->l_recs[le16_to_cpu(el->l_next_free_rec) - 1];

	ocfs2_adjust_rightmost_records(handle, et, path, rec);

out:
	ocfs2_free_path(path);
	return status;
}

/*
 * Add an entire tree branch to our inode. eb_bh is the extent block
 * to start at, if we don't want to start the branch at the root
 * structure.
 *
 * last_eb_bh is required as we have to update it's next_leaf pointer
 * for the new last extent block.
 *
 * the new branch will be 'empty' in the sense that every block will
 * contain a single record with cluster count == 0.
 */
static int ocfs2_add_branch(handle_t *handle,
			    struct ocfs2_extent_tree *et,
			    struct buffer_head *eb_bh,
			    struct buffer_head **last_eb_bh,
			    struct ocfs2_alloc_context *meta_ac)
{
	int status, new_blocks, i, block_given = 0;
	u64 next_blkno, new_last_eb_blk;
	struct buffer_head *bh;
	struct buffer_head **new_eb_bhs = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *eb_el;
	struct ocfs2_extent_list  *el;
	u32 new_cpos, root_end;

	BUG_ON(!last_eb_bh || !*last_eb_bh);

	if (eb_bh) {
		eb = (struct ocfs2_extent_block *) eb_bh->b_data;
		el = &eb->h_list;
	} else
		el = et->et_root_el;

	/* we never add a branch to a leaf. */
	BUG_ON(!el->l_tree_depth);

	new_blocks = le16_to_cpu(el->l_tree_depth);

	eb = (struct ocfs2_extent_block *)(*last_eb_bh)->b_data;
	new_cpos = ocfs2_sum_rightmost_rec(&eb->h_list);
	root_end = ocfs2_sum_rightmost_rec(et->et_root_el);

	/*
	 * If there is a gap before the root end and the real end
	 * of the righmost leaf block, we need to remove the gap
	 * between new_cpos and root_end first so that the tree
	 * is consistent after we add a new branch(it will start
	 * from new_cpos).
	 */
	if (root_end > new_cpos) {
		trace_ocfs2_adjust_rightmost_branch(
			(unsigned long long)
			ocfs2_metadata_cache_owner(et->et_ci),
			root_end, new_cpos);

		status = ocfs2_adjust_rightmost_branch(handle, et);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
	}

	/* allocate the number of new eb blocks we need */
	new_eb_bhs = kcalloc(new_blocks, sizeof(struct buffer_head *),
			     GFP_KERNEL);
	if (!new_eb_bhs) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	/* Firstyly, try to reuse dealloc since we have already estimated how
	 * many extent blocks we may use.
	 */
	if (!ocfs2_is_dealloc_empty(et)) {
		status = ocfs2_reuse_blk_from_dealloc(handle, et,
						      new_eb_bhs, new_blocks,
						      &block_given);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	BUG_ON(block_given > new_blocks);

	if (block_given < new_blocks) {
		BUG_ON(!meta_ac);
		status = ocfs2_create_new_meta_bhs(handle, et,
						   new_blocks - block_given,
						   meta_ac,
						   &new_eb_bhs[block_given]);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
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
		/* ocfs2_create_new_meta_bhs() should create it right! */
		BUG_ON(!OCFS2_IS_VALID_EXTENT_BLOCK(eb));
		eb_el = &eb->h_list;

		status = ocfs2_journal_access_eb(handle, et->et_ci, bh,
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

		ocfs2_journal_dirty(handle, bh);
		next_blkno = le64_to_cpu(eb->h_blkno);
	}

	/* This is a bit hairy. We want to update up to three blocks
	 * here without leaving any of them in an inconsistent state
	 * in case of error. We don't have to worry about
	 * journal_dirty erroring as it won't unless we've aborted the
	 * handle (in which case we would never be here) so reserving
	 * the write with journal_access is all we need to do. */
	status = ocfs2_journal_access_eb(handle, et->et_ci, *last_eb_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	status = ocfs2_et_root_journal_access(handle, et,
					      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	if (eb_bh) {
		status = ocfs2_journal_access_eb(handle, et->et_ci, eb_bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	/* Link the new branch into the rest of the tree (el will
	 * either be on the root_bh, or the extent block passed in. */
	i = le16_to_cpu(el->l_next_free_rec);
	el->l_recs[i].e_blkno = cpu_to_le64(next_blkno);
	el->l_recs[i].e_cpos = cpu_to_le32(new_cpos);
	el->l_recs[i].e_int_clusters = 0;
	le16_add_cpu(&el->l_next_free_rec, 1);

	/* fe needs a new last extent block pointer, as does the
	 * next_leaf on the previously last-extent-block. */
	ocfs2_et_set_last_eb_blk(et, new_last_eb_blk);

	eb = (struct ocfs2_extent_block *) (*last_eb_bh)->b_data;
	eb->h_next_leaf_blk = cpu_to_le64(new_last_eb_blk);

	ocfs2_journal_dirty(handle, *last_eb_bh);
	ocfs2_journal_dirty(handle, et->et_root_bh);
	if (eb_bh)
		ocfs2_journal_dirty(handle, eb_bh);

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
			brelse(new_eb_bhs[i]);
		kfree(new_eb_bhs);
	}

	return status;
}

/*
 * adds another level to the allocation tree.
 * returns back the new extent block so you can add a branch to it
 * after this call.
 */
static int ocfs2_shift_tree_depth(handle_t *handle,
				  struct ocfs2_extent_tree *et,
				  struct ocfs2_alloc_context *meta_ac,
				  struct buffer_head **ret_new_eb_bh)
{
	int status, i, block_given = 0;
	u32 new_clusters;
	struct buffer_head *new_eb_bh = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *root_el;
	struct ocfs2_extent_list  *eb_el;

	if (!ocfs2_is_dealloc_empty(et)) {
		status = ocfs2_reuse_blk_from_dealloc(handle, et,
						      &new_eb_bh, 1,
						      &block_given);
	} else if (meta_ac) {
		status = ocfs2_create_new_meta_bhs(handle, et, 1, meta_ac,
						   &new_eb_bh);

	} else {
		BUG();
	}

	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	eb = (struct ocfs2_extent_block *) new_eb_bh->b_data;
	/* ocfs2_create_new_meta_bhs() should create it right! */
	BUG_ON(!OCFS2_IS_VALID_EXTENT_BLOCK(eb));

	eb_el = &eb->h_list;
	root_el = et->et_root_el;

	status = ocfs2_journal_access_eb(handle, et->et_ci, new_eb_bh,
					 OCFS2_JOURNAL_ACCESS_CREATE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/* copy the root extent list data into the new extent block */
	eb_el->l_tree_depth = root_el->l_tree_depth;
	eb_el->l_next_free_rec = root_el->l_next_free_rec;
	for (i = 0; i < le16_to_cpu(root_el->l_next_free_rec); i++)
		eb_el->l_recs[i] = root_el->l_recs[i];

	ocfs2_journal_dirty(handle, new_eb_bh);

	status = ocfs2_et_root_journal_access(handle, et,
					      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	new_clusters = ocfs2_sum_rightmost_rec(eb_el);

	/* update root_bh now */
	le16_add_cpu(&root_el->l_tree_depth, 1);
	root_el->l_recs[0].e_cpos = 0;
	root_el->l_recs[0].e_blkno = eb->h_blkno;
	root_el->l_recs[0].e_int_clusters = cpu_to_le32(new_clusters);
	for (i = 1; i < le16_to_cpu(root_el->l_next_free_rec); i++)
		memset(&root_el->l_recs[i], 0, sizeof(struct ocfs2_extent_rec));
	root_el->l_next_free_rec = cpu_to_le16(1);

	/* If this is our 1st tree depth shift, then last_eb_blk
	 * becomes the allocated extent block */
	if (root_el->l_tree_depth == cpu_to_le16(1))
		ocfs2_et_set_last_eb_blk(et, le64_to_cpu(eb->h_blkno));

	ocfs2_journal_dirty(handle, et->et_root_bh);

	*ret_new_eb_bh = new_eb_bh;
	new_eb_bh = NULL;
	status = 0;
bail:
	brelse(new_eb_bh);

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
 * 2) the search fails to find anything, but the root_el has room. We
 *    pass NULL back in *lowest_eb_bh, but still return '0'
 *
 * 3) the search fails to find anything AND the root_el is full, in
 *    which case we return > 0
 *
 * return status < 0 indicates an error.
 */
static int ocfs2_find_branch_target(struct ocfs2_extent_tree *et,
				    struct buffer_head **target_bh)
{
	int status = 0, i;
	u64 blkno;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list  *el;
	struct buffer_head *bh = NULL;
	struct buffer_head *lowest_bh = NULL;

	*target_bh = NULL;

	el = et->et_root_el;

	while(le16_to_cpu(el->l_tree_depth) > 1) {
		if (le16_to_cpu(el->l_next_free_rec) == 0) {
			status = ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
					"Owner %llu has empty extent list (next_free_rec == 0)\n",
					(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci));
			goto bail;
		}
		i = le16_to_cpu(el->l_next_free_rec) - 1;
		blkno = le64_to_cpu(el->l_recs[i].e_blkno);
		if (!blkno) {
			status = ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
					"Owner %llu has extent list where extent # %d has no physical block start\n",
					(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci), i);
			goto bail;
		}

		brelse(bh);
		bh = NULL;

		status = ocfs2_read_extent_block(et->et_ci, blkno, &bh);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		eb = (struct ocfs2_extent_block *) bh->b_data;
		el = &eb->h_list;

		if (le16_to_cpu(el->l_next_free_rec) <
		    le16_to_cpu(el->l_count)) {
			brelse(lowest_bh);
			lowest_bh = bh;
			get_bh(lowest_bh);
		}
	}

	/* If we didn't find one and the fe doesn't have any room,
	 * then return '1' */
	el = et->et_root_el;
	if (!lowest_bh && (el->l_next_free_rec == el->l_count))
		status = 1;

	*target_bh = lowest_bh;
bail:
	brelse(bh);

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
static int ocfs2_grow_tree(handle_t *handle, struct ocfs2_extent_tree *et,
			   int *final_depth, struct buffer_head **last_eb_bh,
			   struct ocfs2_alloc_context *meta_ac)
{
	int ret, shift;
	struct ocfs2_extent_list *el = et->et_root_el;
	int depth = le16_to_cpu(el->l_tree_depth);
	struct buffer_head *bh = NULL;

	BUG_ON(meta_ac == NULL && ocfs2_is_dealloc_empty(et));

	shift = ocfs2_find_branch_target(et, &bh);
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
		trace_ocfs2_grow_tree(
			(unsigned long long)
			ocfs2_metadata_cache_owner(et->et_ci),
			depth);

		/* ocfs2_shift_tree_depth will return us a buffer with
		 * the new extent block (so we can pass that to
		 * ocfs2_add_branch). */
		ret = ocfs2_shift_tree_depth(handle, et, meta_ac, &bh);
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
	ret = ocfs2_add_branch(handle, et, bh, last_eb_bh,
			       meta_ac);
	if (ret < 0)
		mlog_errno(ret);

out:
	if (final_depth)
		*final_depth = depth;
	brelse(bh);
	return ret;
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
	BUG_ON(el->l_next_free_rec == el->l_count && !has_empty);

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

	trace_ocfs2_rotate_leaf(insert_cpos, insert_index,
				has_empty, next_free,
				le16_to_cpu(el->l_count));

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
int ocfs2_find_subtree_root(struct ocfs2_extent_tree *et,
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
				"Owner %llu, left depth %u, right depth %u\n"
				"left leaf blk %llu, right leaf blk %llu\n",
				(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
				left->p_tree_depth, right->p_tree_depth,
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
static int __ocfs2_find_path(struct ocfs2_caching_info *ci,
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

	el = root_el;
	while (el->l_tree_depth) {
		if (le16_to_cpu(el->l_next_free_rec) == 0) {
			ocfs2_error(ocfs2_metadata_cache_get_super(ci),
				    "Owner %llu has empty extent list at depth %u\n",
				    (unsigned long long)ocfs2_metadata_cache_owner(ci),
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
			ocfs2_error(ocfs2_metadata_cache_get_super(ci),
				    "Owner %llu has bad blkno in extent list at depth %u (index %d)\n",
				    (unsigned long long)ocfs2_metadata_cache_owner(ci),
				    le16_to_cpu(el->l_tree_depth), i);
			ret = -EROFS;
			goto out;
		}

		brelse(bh);
		bh = NULL;
		ret = ocfs2_read_extent_block(ci, blkno, &bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) bh->b_data;
		el = &eb->h_list;

		if (le16_to_cpu(el->l_next_free_rec) >
		    le16_to_cpu(el->l_count)) {
			ocfs2_error(ocfs2_metadata_cache_get_super(ci),
				    "Owner %llu has bad count in extent list at block %llu (next free=%u, count=%u)\n",
				    (unsigned long long)ocfs2_metadata_cache_owner(ci),
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
int ocfs2_find_path(struct ocfs2_caching_info *ci,
		    struct ocfs2_path *path, u32 cpos)
{
	struct find_path_data data;

	data.index = 1;
	data.path = path;
	return __ocfs2_find_path(ci, path_root_el(path), cpos,
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
int ocfs2_find_leaf(struct ocfs2_caching_info *ci,
		    struct ocfs2_extent_list *root_el, u32 cpos,
		    struct buffer_head **leaf_bh)
{
	int ret;
	struct buffer_head *bh = NULL;

	ret = __ocfs2_find_path(ci, root_el, cpos, find_leaf_ins, &bh);
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
 * right_rec: the record to the right of left_rec
 * right_child_el: is the child list pointed to by right_rec
 *
 * By definition, this only works on interior nodes.
 */
static void ocfs2_adjust_adjacent_records(struct ocfs2_extent_rec *left_rec,
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
	if (!ocfs2_rec_clusters(right_child_el, &right_child_el->l_recs[0])) {
		BUG_ON(right_child_el->l_tree_depth);
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

	ocfs2_adjust_adjacent_records(&root_el->l_recs[i],
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
 *   - When we've adjusted the last extent record in the left path leaf and the
 *     1st extent record in the right path leaf during cross extent block merge.
 */
static void ocfs2_complete_edge_insert(handle_t *handle,
				       struct ocfs2_path *left_path,
				       struct ocfs2_path *right_path,
				       int subtree_index)
{
	int i, idx;
	struct ocfs2_extent_list *el, *left_el, *right_el;
	struct ocfs2_extent_rec *left_rec, *right_rec;
	struct buffer_head *root_bh;

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
		trace_ocfs2_complete_edge_insert(i);

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

		ocfs2_adjust_adjacent_records(left_rec, right_rec, right_el);

		ocfs2_journal_dirty(handle, left_path->p_node[i].bh);
		ocfs2_journal_dirty(handle, right_path->p_node[i].bh);

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

	ocfs2_journal_dirty(handle, root_bh);
}

static int ocfs2_rotate_subtree_right(handle_t *handle,
				      struct ocfs2_extent_tree *et,
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
		ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
			    "Inode %llu has non-full interior leaf node %llu (next free = %u)\n",
			    (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
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

	ret = ocfs2_path_bh_journal_access(handle, et->et_ci, right_path,
					   subtree_index);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	for(i = subtree_index + 1; i < path_num_items(right_path); i++) {
		ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
						   right_path, i);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
						   left_path, i);
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
			(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
			(unsigned long long)right_leaf_bh->b_blocknr);

	ocfs2_create_empty_extent(right_el);

	ocfs2_journal_dirty(handle, right_leaf_bh);

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

	ocfs2_journal_dirty(handle, left_leaf_bh);

	ocfs2_complete_edge_insert(handle, left_path, right_path,
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
int ocfs2_find_cpos_for_left_leaf(struct super_block *sb,
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
		ocfs2_error(sb, "Invalid extent tree at extent block %llu\n",
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
	int ret = 0;
	int credits = (path->p_tree_depth - subtree_depth) * 2 + 1 + op_credits;

	if (jbd2_handle_buffer_credits(handle) < credits)
		ret = ocfs2_extend_trans(handle,
				credits - jbd2_handle_buffer_credits(handle));

	return ret;
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
 * Upon successful return from this function:
 *
 * - The 'right_path' array will contain a path to the leaf block
 *   whose range contains e_cpos.
 * - That leaf block will have a single empty extent in list index 0.
 * - In the case that the rotation requires a post-insert update,
 *   *ret_left_path will contain a valid path which can be passed to
 *   ocfs2_insert_path().
 */
static int ocfs2_rotate_tree_right(handle_t *handle,
				   struct ocfs2_extent_tree *et,
				   enum ocfs2_split_type split,
				   u32 insert_cpos,
				   struct ocfs2_path *right_path,
				   struct ocfs2_path **ret_left_path)
{
	int ret, start, orig_credits = jbd2_handle_buffer_credits(handle);
	u32 cpos;
	struct ocfs2_path *left_path = NULL;
	struct super_block *sb = ocfs2_metadata_cache_get_super(et->et_ci);

	*ret_left_path = NULL;

	left_path = ocfs2_new_path_from_path(right_path);
	if (!left_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_cpos_for_left_leaf(sb, right_path, &cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	trace_ocfs2_rotate_tree_right(
		(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
		insert_cpos, cpos);

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
		trace_ocfs2_rotate_tree_right(
			(unsigned long long)
			ocfs2_metadata_cache_owner(et->et_ci),
			insert_cpos, cpos);

		ret = ocfs2_find_path(et->et_ci, left_path, cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		mlog_bug_on_msg(path_leaf_bh(left_path) ==
				path_leaf_bh(right_path),
				"Owner %llu: error during insert of %u "
				"(left path cpos %u) results in two identical "
				"paths ending at %llu\n",
				(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
				insert_cpos, cpos,
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

		start = ocfs2_find_subtree_root(et, left_path, right_path);

		trace_ocfs2_rotate_subtree(start,
			(unsigned long long)
			right_path->p_node[start].bh->b_blocknr,
			right_path->p_tree_depth);

		ret = ocfs2_extend_rotate_transaction(handle, start,
						      orig_credits, right_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_rotate_subtree_right(handle, et, left_path,
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

		ret = ocfs2_find_cpos_for_left_leaf(sb, right_path, &cpos);
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

static int ocfs2_update_edge_lengths(handle_t *handle,
				     struct ocfs2_extent_tree *et,
				     struct ocfs2_path *path)
{
	int i, idx, ret;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_block *eb;
	u32 range;

	ret = ocfs2_journal_access_path(et->et_ci, handle, path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

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
out:
	return ret;
}

static void ocfs2_unlink_path(handle_t *handle,
			      struct ocfs2_extent_tree *et,
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
			     (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
			     (unsigned long long)le64_to_cpu(eb->h_blkno),
			     le16_to_cpu(el->l_next_free_rec));

			ocfs2_journal_dirty(handle, bh);
			ocfs2_remove_from_cache(et->et_ci, bh);
			continue;
		}

		el->l_next_free_rec = 0;
		memset(&el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));

		ocfs2_journal_dirty(handle, bh);

		ret = ocfs2_cache_extent_block_free(dealloc, eb);
		if (ret)
			mlog_errno(ret);

		ocfs2_remove_from_cache(et->et_ci, bh);
	}
}

static void ocfs2_unlink_subtree(handle_t *handle,
				 struct ocfs2_extent_tree *et,
				 struct ocfs2_path *left_path,
				 struct ocfs2_path *right_path,
				 int subtree_index,
				 struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int i;
	struct buffer_head *root_bh = left_path->p_node[subtree_index].bh;
	struct ocfs2_extent_list *root_el = left_path->p_node[subtree_index].el;
	struct ocfs2_extent_block *eb;

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

	ocfs2_unlink_path(handle, et, dealloc, right_path,
			  subtree_index + 1);
}

static int ocfs2_rotate_subtree_left(handle_t *handle,
				     struct ocfs2_extent_tree *et,
				     struct ocfs2_path *left_path,
				     struct ocfs2_path *right_path,
				     int subtree_index,
				     struct ocfs2_cached_dealloc_ctxt *dealloc,
				     int *deleted)
{
	int ret, i, del_right_subtree = 0, right_has_empty = 0;
	struct buffer_head *root_bh, *et_root_bh = path_root_bh(right_path);
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
			ret = ocfs2_journal_access_eb(handle, et->et_ci,
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
		ret = ocfs2_et_root_journal_access(handle, et,
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

	ret = ocfs2_path_bh_journal_access(handle, et->et_ci, right_path,
					   subtree_index);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	for(i = subtree_index + 1; i < path_num_items(right_path); i++) {
		ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
						   right_path, i);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
						   left_path, i);
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

	ocfs2_journal_dirty(handle, path_leaf_bh(left_path));
	ocfs2_journal_dirty(handle, path_leaf_bh(right_path));

	if (del_right_subtree) {
		ocfs2_unlink_subtree(handle, et, left_path, right_path,
				     subtree_index, dealloc);
		ret = ocfs2_update_edge_lengths(handle, et, left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *)path_leaf_bh(left_path)->b_data;
		ocfs2_et_set_last_eb_blk(et, le64_to_cpu(eb->h_blkno));

		/*
		 * Removal of the extent in the left leaf was skipped
		 * above so we could delete the right path
		 * 1st.
		 */
		if (right_has_empty)
			ocfs2_remove_empty_extent(left_leaf_el);

		ocfs2_journal_dirty(handle, et_root_bh);

		*deleted = 1;
	} else
		ocfs2_complete_edge_insert(handle, left_path, right_path,
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
int ocfs2_find_cpos_for_right_leaf(struct super_block *sb,
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
		ocfs2_error(sb, "Invalid extent tree at extent block %llu\n",
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

static int ocfs2_rotate_rightmost_leaf_left(handle_t *handle,
					    struct ocfs2_extent_tree *et,
					    struct ocfs2_path *path)
{
	int ret;
	struct buffer_head *bh = path_leaf_bh(path);
	struct ocfs2_extent_list *el = path_leaf_el(path);

	if (!ocfs2_is_empty_extent(&el->l_recs[0]))
		return 0;

	ret = ocfs2_path_bh_journal_access(handle, et->et_ci, path,
					   path_num_items(path) - 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_remove_empty_extent(el);
	ocfs2_journal_dirty(handle, bh);

out:
	return ret;
}

static int __ocfs2_rotate_tree_left(handle_t *handle,
				    struct ocfs2_extent_tree *et,
				    int orig_credits,
				    struct ocfs2_path *path,
				    struct ocfs2_cached_dealloc_ctxt *dealloc,
				    struct ocfs2_path **empty_extent_path)
{
	int ret, subtree_root, deleted;
	u32 right_cpos;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_path *right_path = NULL;
	struct super_block *sb = ocfs2_metadata_cache_get_super(et->et_ci);

	if (!ocfs2_is_empty_extent(&(path_leaf_el(path)->l_recs[0])))
		return 0;

	*empty_extent_path = NULL;

	ret = ocfs2_find_cpos_for_right_leaf(sb, path, &right_cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	left_path = ocfs2_new_path_from_path(path);
	if (!left_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ocfs2_cp_path(left_path, path);

	right_path = ocfs2_new_path_from_path(path);
	if (!right_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	while (right_cpos) {
		ret = ocfs2_find_path(et->et_ci, right_path, right_cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		subtree_root = ocfs2_find_subtree_root(et, left_path,
						       right_path);

		trace_ocfs2_rotate_subtree(subtree_root,
		     (unsigned long long)
		     right_path->p_node[subtree_root].bh->b_blocknr,
		     right_path->p_tree_depth);

		ret = ocfs2_extend_rotate_transaction(handle, 0,
						      orig_credits, left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * Caller might still want to make changes to the
		 * tree root, so re-add it to the journal here.
		 */
		ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
						   left_path, 0);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_rotate_subtree_left(handle, et, left_path,
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

		ret = ocfs2_find_cpos_for_right_leaf(sb, left_path,
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

static int ocfs2_remove_rightmost_path(handle_t *handle,
				struct ocfs2_extent_tree *et,
				struct ocfs2_path *path,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, subtree_index;
	u32 cpos;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;

	ret = ocfs2_et_sanity_check(et);
	if (ret)
		goto out;

	ret = ocfs2_journal_access_path(et->et_ci, handle, path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_cpos_for_left_leaf(ocfs2_metadata_cache_get_super(et->et_ci),
					    path, &cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (cpos) {
		/*
		 * We have a path to the left of this one - it needs
		 * an update too.
		 */
		left_path = ocfs2_new_path_from_path(path);
		if (!left_path) {
			ret = -ENOMEM;
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_find_path(et->et_ci, left_path, cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_journal_access_path(et->et_ci, handle, left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		subtree_index = ocfs2_find_subtree_root(et, left_path, path);

		ocfs2_unlink_subtree(handle, et, left_path, path,
				     subtree_index, dealloc);
		ret = ocfs2_update_edge_lengths(handle, et, left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *)path_leaf_bh(left_path)->b_data;
		ocfs2_et_set_last_eb_blk(et, le64_to_cpu(eb->h_blkno));
	} else {
		/*
		 * 'path' is also the leftmost path which
		 * means it must be the only one. This gets
		 * handled differently because we want to
		 * revert the root back to having extents
		 * in-line.
		 */
		ocfs2_unlink_path(handle, et, dealloc, path, 1);

		el = et->et_root_el;
		el->l_tree_depth = 0;
		el->l_next_free_rec = 0;
		memset(&el->l_recs[0], 0, sizeof(struct ocfs2_extent_rec));

		ocfs2_et_set_last_eb_blk(et, 0);
	}

	ocfs2_journal_dirty(handle, path_root_bh(path));

out:
	ocfs2_free_path(left_path);
	return ret;
}

static int ocfs2_remove_rightmost_empty_extent(struct ocfs2_super *osb,
				struct ocfs2_extent_tree *et,
				struct ocfs2_path *path,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	handle_t *handle;
	int ret;
	int credits = path->p_tree_depth * 2 + 1;

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		return ret;
	}

	ret = ocfs2_remove_rightmost_path(handle, et, path, dealloc);
	if (ret)
		mlog_errno(ret);

	ocfs2_commit_trans(osb, handle);
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
static int ocfs2_rotate_tree_left(handle_t *handle,
				  struct ocfs2_extent_tree *et,
				  struct ocfs2_path *path,
				  struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, orig_credits = jbd2_handle_buffer_credits(handle);
	struct ocfs2_path *tmp_path = NULL, *restart_path = NULL;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;

	el = path_leaf_el(path);
	if (!ocfs2_is_empty_extent(&el->l_recs[0]))
		return 0;

	if (path->p_tree_depth == 0) {
rightmost_no_delete:
		/*
		 * Inline extents. This is trivially handled, so do
		 * it up front.
		 */
		ret = ocfs2_rotate_rightmost_leaf_left(handle, et, path);
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
	 *  2b) we need to bring the root back to inline extents.
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
			ret = ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
					"Owner %llu has empty extent block at %llu\n",
					(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
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

		ret = ocfs2_remove_rightmost_path(handle, et, path,
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
	ret = __ocfs2_rotate_tree_left(handle, et, orig_credits, path,
				       dealloc, &restart_path);
	if (ret && ret != -EAGAIN) {
		mlog_errno(ret);
		goto out;
	}

	while (ret == -EAGAIN) {
		tmp_path = restart_path;
		restart_path = NULL;

		ret = __ocfs2_rotate_tree_left(handle, et, orig_credits,
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

static int ocfs2_get_right_path(struct ocfs2_extent_tree *et,
				struct ocfs2_path *left_path,
				struct ocfs2_path **ret_right_path)
{
	int ret;
	u32 right_cpos;
	struct ocfs2_path *right_path = NULL;
	struct ocfs2_extent_list *left_el;

	*ret_right_path = NULL;

	/* This function shouldn't be called for non-trees. */
	BUG_ON(left_path->p_tree_depth == 0);

	left_el = path_leaf_el(left_path);
	BUG_ON(left_el->l_next_free_rec != left_el->l_count);

	ret = ocfs2_find_cpos_for_right_leaf(ocfs2_metadata_cache_get_super(et->et_ci),
					     left_path, &right_cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* This function shouldn't be called for the rightmost leaf. */
	BUG_ON(right_cpos == 0);

	right_path = ocfs2_new_path_from_path(left_path);
	if (!right_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(et->et_ci, right_path, right_cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	*ret_right_path = right_path;
out:
	if (ret)
		ocfs2_free_path(right_path);
	return ret;
}

/*
 * Remove split_rec clusters from the record at index and merge them
 * onto the beginning of the record "next" to it.
 * For index < l_count - 1, the next means the extent rec at index + 1.
 * For index == l_count - 1, the "next" means the 1st extent rec of the
 * next extent block.
 */
static int ocfs2_merge_rec_right(struct ocfs2_path *left_path,
				 handle_t *handle,
				 struct ocfs2_extent_tree *et,
				 struct ocfs2_extent_rec *split_rec,
				 int index)
{
	int ret, next_free, i;
	unsigned int split_clusters = le16_to_cpu(split_rec->e_leaf_clusters);
	struct ocfs2_extent_rec *left_rec;
	struct ocfs2_extent_rec *right_rec;
	struct ocfs2_extent_list *right_el;
	struct ocfs2_path *right_path = NULL;
	int subtree_index = 0;
	struct ocfs2_extent_list *el = path_leaf_el(left_path);
	struct buffer_head *bh = path_leaf_bh(left_path);
	struct buffer_head *root_bh = NULL;

	BUG_ON(index >= le16_to_cpu(el->l_next_free_rec));
	left_rec = &el->l_recs[index];

	if (index == le16_to_cpu(el->l_next_free_rec) - 1 &&
	    le16_to_cpu(el->l_next_free_rec) == le16_to_cpu(el->l_count)) {
		/* we meet with a cross extent block merge. */
		ret = ocfs2_get_right_path(et, left_path, &right_path);
		if (ret) {
			mlog_errno(ret);
			return ret;
		}

		right_el = path_leaf_el(right_path);
		next_free = le16_to_cpu(right_el->l_next_free_rec);
		BUG_ON(next_free <= 0);
		right_rec = &right_el->l_recs[0];
		if (ocfs2_is_empty_extent(right_rec)) {
			BUG_ON(next_free <= 1);
			right_rec = &right_el->l_recs[1];
		}

		BUG_ON(le32_to_cpu(left_rec->e_cpos) +
		       le16_to_cpu(left_rec->e_leaf_clusters) !=
		       le32_to_cpu(right_rec->e_cpos));

		subtree_index = ocfs2_find_subtree_root(et, left_path,
							right_path);

		ret = ocfs2_extend_rotate_transaction(handle, subtree_index,
					jbd2_handle_buffer_credits(handle),
					right_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		root_bh = left_path->p_node[subtree_index].bh;
		BUG_ON(root_bh != right_path->p_node[subtree_index].bh);

		ret = ocfs2_path_bh_journal_access(handle, et->et_ci, right_path,
						   subtree_index);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		for (i = subtree_index + 1;
		     i < path_num_items(right_path); i++) {
			ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
							   right_path, i);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
							   left_path, i);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}

	} else {
		BUG_ON(index == le16_to_cpu(el->l_next_free_rec) - 1);
		right_rec = &el->l_recs[index + 1];
	}

	ret = ocfs2_path_bh_journal_access(handle, et->et_ci, left_path,
					   path_num_items(left_path) - 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	le16_add_cpu(&left_rec->e_leaf_clusters, -split_clusters);

	le32_add_cpu(&right_rec->e_cpos, -split_clusters);
	le64_add_cpu(&right_rec->e_blkno,
		     -ocfs2_clusters_to_blocks(ocfs2_metadata_cache_get_super(et->et_ci),
					       split_clusters));
	le16_add_cpu(&right_rec->e_leaf_clusters, split_clusters);

	ocfs2_cleanup_merge(el, index);

	ocfs2_journal_dirty(handle, bh);
	if (right_path) {
		ocfs2_journal_dirty(handle, path_leaf_bh(right_path));
		ocfs2_complete_edge_insert(handle, left_path, right_path,
					   subtree_index);
	}
out:
	ocfs2_free_path(right_path);
	return ret;
}

static int ocfs2_get_left_path(struct ocfs2_extent_tree *et,
			       struct ocfs2_path *right_path,
			       struct ocfs2_path **ret_left_path)
{
	int ret;
	u32 left_cpos;
	struct ocfs2_path *left_path = NULL;

	*ret_left_path = NULL;

	/* This function shouldn't be called for non-trees. */
	BUG_ON(right_path->p_tree_depth == 0);

	ret = ocfs2_find_cpos_for_left_leaf(ocfs2_metadata_cache_get_super(et->et_ci),
					    right_path, &left_cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* This function shouldn't be called for the leftmost leaf. */
	BUG_ON(left_cpos == 0);

	left_path = ocfs2_new_path_from_path(right_path);
	if (!left_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(et->et_ci, left_path, left_cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	*ret_left_path = left_path;
out:
	if (ret)
		ocfs2_free_path(left_path);
	return ret;
}

/*
 * Remove split_rec clusters from the record at index and merge them
 * onto the tail of the record "before" it.
 * For index > 0, the "before" means the extent rec at index - 1.
 *
 * For index == 0, the "before" means the last record of the previous
 * extent block. And there is also a situation that we may need to
 * remove the rightmost leaf extent block in the right_path and change
 * the right path to indicate the new rightmost path.
 */
static int ocfs2_merge_rec_left(struct ocfs2_path *right_path,
				handle_t *handle,
				struct ocfs2_extent_tree *et,
				struct ocfs2_extent_rec *split_rec,
				struct ocfs2_cached_dealloc_ctxt *dealloc,
				int index)
{
	int ret, i, subtree_index = 0, has_empty_extent = 0;
	unsigned int split_clusters = le16_to_cpu(split_rec->e_leaf_clusters);
	struct ocfs2_extent_rec *left_rec;
	struct ocfs2_extent_rec *right_rec;
	struct ocfs2_extent_list *el = path_leaf_el(right_path);
	struct buffer_head *bh = path_leaf_bh(right_path);
	struct buffer_head *root_bh = NULL;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_extent_list *left_el;

	BUG_ON(index < 0);

	right_rec = &el->l_recs[index];
	if (index == 0) {
		/* we meet with a cross extent block merge. */
		ret = ocfs2_get_left_path(et, right_path, &left_path);
		if (ret) {
			mlog_errno(ret);
			return ret;
		}

		left_el = path_leaf_el(left_path);
		BUG_ON(le16_to_cpu(left_el->l_next_free_rec) !=
		       le16_to_cpu(left_el->l_count));

		left_rec = &left_el->l_recs[
				le16_to_cpu(left_el->l_next_free_rec) - 1];
		BUG_ON(le32_to_cpu(left_rec->e_cpos) +
		       le16_to_cpu(left_rec->e_leaf_clusters) !=
		       le32_to_cpu(split_rec->e_cpos));

		subtree_index = ocfs2_find_subtree_root(et, left_path,
							right_path);

		ret = ocfs2_extend_rotate_transaction(handle, subtree_index,
					jbd2_handle_buffer_credits(handle),
					left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		root_bh = left_path->p_node[subtree_index].bh;
		BUG_ON(root_bh != right_path->p_node[subtree_index].bh);

		ret = ocfs2_path_bh_journal_access(handle, et->et_ci, right_path,
						   subtree_index);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		for (i = subtree_index + 1;
		     i < path_num_items(right_path); i++) {
			ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
							   right_path, i);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_path_bh_journal_access(handle, et->et_ci,
							   left_path, i);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}
	} else {
		left_rec = &el->l_recs[index - 1];
		if (ocfs2_is_empty_extent(&el->l_recs[0]))
			has_empty_extent = 1;
	}

	ret = ocfs2_path_bh_journal_access(handle, et->et_ci, right_path,
					   path_num_items(right_path) - 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (has_empty_extent && index == 1) {
		/*
		 * The easy case - we can just plop the record right in.
		 */
		*left_rec = *split_rec;
	} else
		le16_add_cpu(&left_rec->e_leaf_clusters, split_clusters);

	le32_add_cpu(&right_rec->e_cpos, split_clusters);
	le64_add_cpu(&right_rec->e_blkno,
		     ocfs2_clusters_to_blocks(ocfs2_metadata_cache_get_super(et->et_ci),
					      split_clusters));
	le16_add_cpu(&right_rec->e_leaf_clusters, -split_clusters);

	ocfs2_cleanup_merge(el, index);

	ocfs2_journal_dirty(handle, bh);
	if (left_path) {
		ocfs2_journal_dirty(handle, path_leaf_bh(left_path));

		/*
		 * In the situation that the right_rec is empty and the extent
		 * block is empty also,  ocfs2_complete_edge_insert can't handle
		 * it and we need to delete the right extent block.
		 */
		if (le16_to_cpu(right_rec->e_leaf_clusters) == 0 &&
		    le16_to_cpu(el->l_next_free_rec) == 1) {
			/* extend credit for ocfs2_remove_rightmost_path */
			ret = ocfs2_extend_rotate_transaction(handle, 0,
					jbd2_handle_buffer_credits(handle),
					right_path);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_remove_rightmost_path(handle, et,
							  right_path,
							  dealloc);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			/* Now the rightmost extent block has been deleted.
			 * So we use the new rightmost path.
			 */
			ocfs2_mv_path(right_path, left_path);
			left_path = NULL;
		} else
			ocfs2_complete_edge_insert(handle, left_path,
						   right_path, subtree_index);
	}
out:
	ocfs2_free_path(left_path);
	return ret;
}

static int ocfs2_try_to_merge_extent(handle_t *handle,
				     struct ocfs2_extent_tree *et,
				     struct ocfs2_path *path,
				     int split_index,
				     struct ocfs2_extent_rec *split_rec,
				     struct ocfs2_cached_dealloc_ctxt *dealloc,
				     struct ocfs2_merge_ctxt *ctxt)
{
	int ret = 0;
	struct ocfs2_extent_list *el = path_leaf_el(path);
	struct ocfs2_extent_rec *rec = &el->l_recs[split_index];

	BUG_ON(ctxt->c_contig_type == CONTIG_NONE);

	if (ctxt->c_split_covers_rec && ctxt->c_has_empty_extent) {
		/* extend credit for ocfs2_remove_rightmost_path */
		ret = ocfs2_extend_rotate_transaction(handle, 0,
				jbd2_handle_buffer_credits(handle),
				path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		/*
		 * The merge code will need to create an empty
		 * extent to take the place of the newly
		 * emptied slot. Remove any pre-existing empty
		 * extents - having more than one in a leaf is
		 * illegal.
		 */
		ret = ocfs2_rotate_tree_left(handle, et, path, dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		split_index--;
		rec = &el->l_recs[split_index];
	}

	if (ctxt->c_contig_type == CONTIG_LEFTRIGHT) {
		/*
		 * Left-right contig implies this.
		 */
		BUG_ON(!ctxt->c_split_covers_rec);

		/*
		 * Since the leftright insert always covers the entire
		 * extent, this call will delete the insert record
		 * entirely, resulting in an empty extent record added to
		 * the extent block.
		 *
		 * Since the adding of an empty extent shifts
		 * everything back to the right, there's no need to
		 * update split_index here.
		 *
		 * When the split_index is zero, we need to merge it to the
		 * prevoius extent block. It is more efficient and easier
		 * if we do merge_right first and merge_left later.
		 */
		ret = ocfs2_merge_rec_right(path, handle, et, split_rec,
					    split_index);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * We can only get this from logic error above.
		 */
		BUG_ON(!ocfs2_is_empty_extent(&el->l_recs[0]));

		/* extend credit for ocfs2_remove_rightmost_path */
		ret = ocfs2_extend_rotate_transaction(handle, 0,
					jbd2_handle_buffer_credits(handle),
					path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/* The merge left us with an empty extent, remove it. */
		ret = ocfs2_rotate_tree_left(handle, et, path, dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		rec = &el->l_recs[split_index];

		/*
		 * Note that we don't pass split_rec here on purpose -
		 * we've merged it into the rec already.
		 */
		ret = ocfs2_merge_rec_left(path, handle, et, rec,
					   dealloc, split_index);

		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/* extend credit for ocfs2_remove_rightmost_path */
		ret = ocfs2_extend_rotate_transaction(handle, 0,
				jbd2_handle_buffer_credits(handle),
				path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_rotate_tree_left(handle, et, path, dealloc);
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
			ret = ocfs2_merge_rec_left(path, handle, et,
						   split_rec, dealloc,
						   split_index);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		} else {
			ret = ocfs2_merge_rec_right(path, handle,
						    et, split_rec,
						    split_index);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}

		if (ctxt->c_split_covers_rec) {
			/* extend credit for ocfs2_remove_rightmost_path */
			ret = ocfs2_extend_rotate_transaction(handle, 0,
					jbd2_handle_buffer_credits(handle),
					path);
			if (ret) {
				mlog_errno(ret);
				ret = 0;
				goto out;
			}

			/*
			 * The merge may have left an empty extent in
			 * our leaf. Try to rotate it away.
			 */
			ret = ocfs2_rotate_tree_left(handle, et, path,
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
static void ocfs2_insert_at_leaf(struct ocfs2_extent_tree *et,
				 struct ocfs2_extent_rec *insert_rec,
				 struct ocfs2_extent_list *el,
				 struct ocfs2_insert_type *insert)
{
	int i = insert->ins_contig_index;
	unsigned int range;
	struct ocfs2_extent_rec *rec;

	BUG_ON(le16_to_cpu(el->l_tree_depth) != 0);

	if (insert->ins_split != SPLIT_NONE) {
		i = ocfs2_search_extent_list(el, le32_to_cpu(insert_rec->e_cpos));
		BUG_ON(i == -1);
		rec = &el->l_recs[i];
		ocfs2_subtract_from_rec(ocfs2_metadata_cache_get_super(et->et_ci),
					insert->ins_split, rec,
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
				"owner %llu, depth %u, count %u, next free %u, "
				"rec.cpos %u, rec.clusters %u, "
				"insert.cpos %u, insert.clusters %u\n",
				ocfs2_metadata_cache_owner(et->et_ci),
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
	 * extent record, or by virtue of an l_next_free_rec < l_count.
	 */
	ocfs2_rotate_leaf(el, insert_rec);
}

static void ocfs2_adjust_rightmost_records(handle_t *handle,
					   struct ocfs2_extent_tree *et,
					   struct ocfs2_path *path,
					   struct ocfs2_extent_rec *insert_rec)
{
	int i, next_free;
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
			ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
				    "Owner %llu has a bad extent list\n",
				    (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci));
			return;
		}

		rec = &el->l_recs[next_free - 1];

		rec->e_int_clusters = insert_rec->e_cpos;
		le32_add_cpu(&rec->e_int_clusters,
			     le16_to_cpu(insert_rec->e_leaf_clusters));
		le32_add_cpu(&rec->e_int_clusters,
			     -le32_to_cpu(rec->e_cpos));

		ocfs2_journal_dirty(handle, bh);
	}
}

static int ocfs2_append_rec_to_path(handle_t *handle,
				    struct ocfs2_extent_tree *et,
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

		ret = ocfs2_find_cpos_for_left_leaf(ocfs2_metadata_cache_get_super(et->et_ci),
						    right_path, &left_cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		trace_ocfs2_append_rec_to_path(
			(unsigned long long)
			ocfs2_metadata_cache_owner(et->et_ci),
			le32_to_cpu(insert_rec->e_cpos),
			left_cpos);

		/*
		 * No need to worry if the append is already in the
		 * leftmost leaf.
		 */
		if (left_cpos) {
			left_path = ocfs2_new_path_from_path(right_path);
			if (!left_path) {
				ret = -ENOMEM;
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_find_path(et->et_ci, left_path,
					      left_cpos);
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

	ret = ocfs2_journal_access_path(et->et_ci, handle, right_path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_adjust_rightmost_records(handle, et, right_path, insert_rec);

	*ret_left_path = left_path;
	ret = 0;
out:
	if (ret != 0)
		ocfs2_free_path(left_path);

	return ret;
}

static void ocfs2_split_record(struct ocfs2_extent_tree *et,
			       struct ocfs2_path *left_path,
			       struct ocfs2_path *right_path,
			       struct ocfs2_extent_rec *split_rec,
			       enum ocfs2_split_type split)
{
	int index;
	u32 cpos = le32_to_cpu(split_rec->e_cpos);
	struct ocfs2_extent_list *left_el = NULL, *right_el, *insert_el, *el;
	struct ocfs2_extent_rec *rec, *tmprec;

	right_el = path_leaf_el(right_path);
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
	ocfs2_subtract_from_rec(ocfs2_metadata_cache_get_super(et->et_ci),
				split, rec, split_rec);
	ocfs2_rotate_leaf(insert_el, split_rec);
}

/*
 * This function only does inserts on an allocation b-tree. For tree
 * depth = 0, ocfs2_insert_at_leaf() is called directly.
 *
 * right_path is the path we want to do the actual insert
 * in. left_path should only be passed in if we need to update that
 * portion of the tree after an edge insert.
 */
static int ocfs2_insert_path(handle_t *handle,
			     struct ocfs2_extent_tree *et,
			     struct ocfs2_path *left_path,
			     struct ocfs2_path *right_path,
			     struct ocfs2_extent_rec *insert_rec,
			     struct ocfs2_insert_type *insert)
{
	int ret, subtree_index;
	struct buffer_head *leaf_bh = path_leaf_bh(right_path);

	if (left_path) {
		/*
		 * There's a chance that left_path got passed back to
		 * us without being accounted for in the
		 * journal. Extend our transaction here to be sure we
		 * can change those blocks.
		 */
		ret = ocfs2_extend_trans(handle, left_path->p_tree_depth);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_journal_access_path(et->et_ci, handle, left_path);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	/*
	 * Pass both paths to the journal. The majority of inserts
	 * will be touching all components anyway.
	 */
	ret = ocfs2_journal_access_path(et->et_ci, handle, right_path);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	if (insert->ins_split != SPLIT_NONE) {
		/*
		 * We could call ocfs2_insert_at_leaf() for some types
		 * of splits, but it's easier to just let one separate
		 * function sort it all out.
		 */
		ocfs2_split_record(et, left_path, right_path,
				   insert_rec, insert->ins_split);

		/*
		 * Split might have modified either leaf and we don't
		 * have a guarantee that the later edge insert will
		 * dirty this for us.
		 */
		if (left_path)
			ocfs2_journal_dirty(handle,
					    path_leaf_bh(left_path));
	} else
		ocfs2_insert_at_leaf(et, insert_rec, path_leaf_el(right_path),
				     insert);

	ocfs2_journal_dirty(handle, leaf_bh);

	if (left_path) {
		/*
		 * The rotate code has indicated that we need to fix
		 * up portions of the tree after the insert.
		 *
		 * XXX: Should we extend the transaction here?
		 */
		subtree_index = ocfs2_find_subtree_root(et, left_path,
							right_path);
		ocfs2_complete_edge_insert(handle, left_path, right_path,
					   subtree_index);
	}

	ret = 0;
out:
	return ret;
}

static int ocfs2_do_insert_extent(handle_t *handle,
				  struct ocfs2_extent_tree *et,
				  struct ocfs2_extent_rec *insert_rec,
				  struct ocfs2_insert_type *type)
{
	int ret, rotate = 0;
	u32 cpos;
	struct ocfs2_path *right_path = NULL;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_extent_list *el;

	el = et->et_root_el;

	ret = ocfs2_et_root_journal_access(handle, et,
					   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (le16_to_cpu(el->l_tree_depth) == 0) {
		ocfs2_insert_at_leaf(et, insert_rec, el, type);
		goto out_update_clusters;
	}

	right_path = ocfs2_new_path_from_et(et);
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

	ret = ocfs2_find_path(et->et_ci, right_path, cpos);
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
		ret = ocfs2_rotate_tree_right(handle, et, type->ins_split,
					      le32_to_cpu(insert_rec->e_cpos),
					      right_path, &left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * ocfs2_rotate_tree_right() might have extended the
		 * transaction without re-journaling our tree root.
		 */
		ret = ocfs2_et_root_journal_access(handle, et,
						   OCFS2_JOURNAL_ACCESS_WRITE);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	} else if (type->ins_appending == APPEND_TAIL
		   && type->ins_contig != CONTIG_LEFT) {
		ret = ocfs2_append_rec_to_path(handle, et, insert_rec,
					       right_path, &left_path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	ret = ocfs2_insert_path(handle, et, left_path, right_path,
				insert_rec, type);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

out_update_clusters:
	if (type->ins_split == SPLIT_NONE)
		ocfs2_et_update_clusters(et,
					 le16_to_cpu(insert_rec->e_leaf_clusters));

	ocfs2_journal_dirty(handle, et->et_root_bh);

out:
	ocfs2_free_path(left_path);
	ocfs2_free_path(right_path);

	return ret;
}

static int ocfs2_figure_merge_contig_type(struct ocfs2_extent_tree *et,
			       struct ocfs2_path *path,
			       struct ocfs2_extent_list *el, int index,
			       struct ocfs2_extent_rec *split_rec,
			       struct ocfs2_merge_ctxt *ctxt)
{
	int status = 0;
	enum ocfs2_contig_type ret = CONTIG_NONE;
	u32 left_cpos, right_cpos;
	struct ocfs2_extent_rec *rec = NULL;
	struct ocfs2_extent_list *new_el;
	struct ocfs2_path *left_path = NULL, *right_path = NULL;
	struct buffer_head *bh;
	struct ocfs2_extent_block *eb;
	struct super_block *sb = ocfs2_metadata_cache_get_super(et->et_ci);

	if (index > 0) {
		rec = &el->l_recs[index - 1];
	} else if (path->p_tree_depth > 0) {
		status = ocfs2_find_cpos_for_left_leaf(sb, path, &left_cpos);
		if (status)
			goto exit;

		if (left_cpos != 0) {
			left_path = ocfs2_new_path_from_path(path);
			if (!left_path) {
				status = -ENOMEM;
				mlog_errno(status);
				goto exit;
			}

			status = ocfs2_find_path(et->et_ci, left_path,
						 left_cpos);
			if (status)
				goto free_left_path;

			new_el = path_leaf_el(left_path);

			if (le16_to_cpu(new_el->l_next_free_rec) !=
			    le16_to_cpu(new_el->l_count)) {
				bh = path_leaf_bh(left_path);
				eb = (struct ocfs2_extent_block *)bh->b_data;
				status = ocfs2_error(sb,
						"Extent block #%llu has an invalid l_next_free_rec of %d.  It should have matched the l_count of %d\n",
						(unsigned long long)le64_to_cpu(eb->h_blkno),
						le16_to_cpu(new_el->l_next_free_rec),
						le16_to_cpu(new_el->l_count));
				goto free_left_path;
			}
			rec = &new_el->l_recs[
				le16_to_cpu(new_el->l_next_free_rec) - 1];
		}
	}

	/*
	 * We're careful to check for an empty extent record here -
	 * the merge code will know what to do if it sees one.
	 */
	if (rec) {
		if (index == 1 && ocfs2_is_empty_extent(rec)) {
			if (split_rec->e_cpos == el->l_recs[index].e_cpos)
				ret = CONTIG_RIGHT;
		} else {
			ret = ocfs2_et_extent_contig(et, rec, split_rec);
		}
	}

	rec = NULL;
	if (index < (le16_to_cpu(el->l_next_free_rec) - 1))
		rec = &el->l_recs[index + 1];
	else if (le16_to_cpu(el->l_next_free_rec) == le16_to_cpu(el->l_count) &&
		 path->p_tree_depth > 0) {
		status = ocfs2_find_cpos_for_right_leaf(sb, path, &right_cpos);
		if (status)
			goto free_left_path;

		if (right_cpos == 0)
			goto free_left_path;

		right_path = ocfs2_new_path_from_path(path);
		if (!right_path) {
			status = -ENOMEM;
			mlog_errno(status);
			goto free_left_path;
		}

		status = ocfs2_find_path(et->et_ci, right_path, right_cpos);
		if (status)
			goto free_right_path;

		new_el = path_leaf_el(right_path);
		rec = &new_el->l_recs[0];
		if (ocfs2_is_empty_extent(rec)) {
			if (le16_to_cpu(new_el->l_next_free_rec) <= 1) {
				bh = path_leaf_bh(right_path);
				eb = (struct ocfs2_extent_block *)bh->b_data;
				status = ocfs2_error(sb,
						"Extent block #%llu has an invalid l_next_free_rec of %d\n",
						(unsigned long long)le64_to_cpu(eb->h_blkno),
						le16_to_cpu(new_el->l_next_free_rec));
				goto free_right_path;
			}
			rec = &new_el->l_recs[1];
		}
	}

	if (rec) {
		enum ocfs2_contig_type contig_type;

		contig_type = ocfs2_et_extent_contig(et, rec, split_rec);

		if (contig_type == CONTIG_LEFT && ret == CONTIG_RIGHT)
			ret = CONTIG_LEFTRIGHT;
		else if (ret == CONTIG_NONE)
			ret = contig_type;
	}

free_right_path:
	ocfs2_free_path(right_path);
free_left_path:
	ocfs2_free_path(left_path);
exit:
	if (status == 0)
		ctxt->c_contig_type = ret;

	return status;
}

static void ocfs2_figure_contig_type(struct ocfs2_extent_tree *et,
				     struct ocfs2_insert_type *insert,
				     struct ocfs2_extent_list *el,
				     struct ocfs2_extent_rec *insert_rec)
{
	int i;
	enum ocfs2_contig_type contig_type = CONTIG_NONE;

	BUG_ON(le16_to_cpu(el->l_tree_depth) != 0);

	for(i = 0; i < le16_to_cpu(el->l_next_free_rec); i++) {
		contig_type = ocfs2_et_extent_contig(et, &el->l_recs[i],
						     insert_rec);
		if (contig_type != CONTIG_NONE) {
			insert->ins_contig_index = i;
			break;
		}
	}
	insert->ins_contig = contig_type;

	if (insert->ins_contig != CONTIG_NONE) {
		struct ocfs2_extent_rec *rec =
				&el->l_recs[insert->ins_contig_index];
		unsigned int len = le16_to_cpu(rec->e_leaf_clusters) +
				   le16_to_cpu(insert_rec->e_leaf_clusters);

		/*
		 * Caller might want us to limit the size of extents, don't
		 * calculate contiguousness if we might exceed that limit.
		 */
		if (et->et_max_leaf_clusters &&
		    (len > et->et_max_leaf_clusters))
			insert->ins_contig = CONTIG_NONE;
	}
}

/*
 * This should only be called against the righmost leaf extent list.
 *
 * ocfs2_figure_appending_type() will figure out whether we'll have to
 * insert at the tail of the rightmost leaf.
 *
 * This should also work against the root extent list for tree's with 0
 * depth. If we consider the root extent list to be the rightmost leaf node
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
 * Helper function called at the beginning of an insert.
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
static int ocfs2_figure_insert_type(struct ocfs2_extent_tree *et,
				    struct buffer_head **last_eb_bh,
				    struct ocfs2_extent_rec *insert_rec,
				    int *free_records,
				    struct ocfs2_insert_type *insert)
{
	int ret;
	struct ocfs2_extent_block *eb;
	struct ocfs2_extent_list *el;
	struct ocfs2_path *path = NULL;
	struct buffer_head *bh = NULL;

	insert->ins_split = SPLIT_NONE;

	el = et->et_root_el;
	insert->ins_tree_depth = le16_to_cpu(el->l_tree_depth);

	if (el->l_tree_depth) {
		/*
		 * If we have tree depth, we read in the
		 * rightmost extent block ahead of time as
		 * ocfs2_figure_insert_type() and ocfs2_add_branch()
		 * may want it later.
		 */
		ret = ocfs2_read_extent_block(et->et_ci,
					      ocfs2_et_get_last_eb_blk(et),
					      &bh);
		if (ret) {
			mlog_errno(ret);
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
	*free_records = le16_to_cpu(el->l_count) -
		le16_to_cpu(el->l_next_free_rec);

	if (!insert->ins_tree_depth) {
		ocfs2_figure_contig_type(et, insert, el, insert_rec);
		ocfs2_figure_appending_type(insert, el, insert_rec);
		return 0;
	}

	path = ocfs2_new_path_from_et(et);
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
	ret = ocfs2_find_path(et->et_ci, path, le32_to_cpu(insert_rec->e_cpos));
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
	ocfs2_figure_contig_type(et, insert, el, insert_rec);

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
	if (ocfs2_et_get_last_eb_blk(et) ==
	    path_leaf_bh(path)->b_blocknr) {
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
 * Insert an extent into a btree.
 *
 * The caller needs to update the owning btree's cluster count.
 */
int ocfs2_insert_extent(handle_t *handle,
			struct ocfs2_extent_tree *et,
			u32 cpos,
			u64 start_blk,
			u32 new_clusters,
			u8 flags,
			struct ocfs2_alloc_context *meta_ac)
{
	int status;
	int free_records;
	struct buffer_head *last_eb_bh = NULL;
	struct ocfs2_insert_type insert = {0, };
	struct ocfs2_extent_rec rec;

	trace_ocfs2_insert_extent_start(
		(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
		cpos, new_clusters);

	memset(&rec, 0, sizeof(rec));
	rec.e_cpos = cpu_to_le32(cpos);
	rec.e_blkno = cpu_to_le64(start_blk);
	rec.e_leaf_clusters = cpu_to_le16(new_clusters);
	rec.e_flags = flags;
	status = ocfs2_et_insert_check(et, &rec);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_figure_insert_type(et, &last_eb_bh, &rec,
					  &free_records, &insert);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	trace_ocfs2_insert_extent(insert.ins_appending, insert.ins_contig,
				  insert.ins_contig_index, free_records,
				  insert.ins_tree_depth);

	if (insert.ins_contig == CONTIG_NONE && free_records == 0) {
		status = ocfs2_grow_tree(handle, et,
					 &insert.ins_tree_depth, &last_eb_bh,
					 meta_ac);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
	}

	/* Finally, we can add clusters. This might rotate the tree for us. */
	status = ocfs2_do_insert_extent(handle, et, &rec, &insert);
	if (status < 0)
		mlog_errno(status);
	else
		ocfs2_et_extent_map_insert(et, &rec);

bail:
	brelse(last_eb_bh);

	return status;
}

/*
 * Allcate and add clusters into the extent b-tree.
 * The new clusters(clusters_to_add) will be inserted at logical_offset.
 * The extent b-tree's root is specified by et, and
 * it is not limited to the file storage. Any extent tree can use this
 * function if it implements the proper ocfs2_extent_tree.
 */
int ocfs2_add_clusters_in_btree(handle_t *handle,
				struct ocfs2_extent_tree *et,
				u32 *logical_offset,
				u32 clusters_to_add,
				int mark_unwritten,
				struct ocfs2_alloc_context *data_ac,
				struct ocfs2_alloc_context *meta_ac,
				enum ocfs2_alloc_restarted *reason_ret)
{
	int status = 0, err = 0;
	int need_free = 0;
	int free_extents;
	enum ocfs2_alloc_restarted reason = RESTART_NONE;
	u32 bit_off, num_bits;
	u64 block;
	u8 flags = 0;
	struct ocfs2_super *osb =
		OCFS2_SB(ocfs2_metadata_cache_get_super(et->et_ci));

	BUG_ON(!clusters_to_add);

	if (mark_unwritten)
		flags = OCFS2_EXT_UNWRITTEN;

	free_extents = ocfs2_num_free_extents(et);
	if (free_extents < 0) {
		status = free_extents;
		mlog_errno(status);
		goto leave;
	}

	/* there are two cases which could cause us to EAGAIN in the
	 * we-need-more-metadata case:
	 * 1) we haven't reserved *any*
	 * 2) we are so fragmented, we've needed to add metadata too
	 *    many times. */
	if (!free_extents && !meta_ac) {
		err = -1;
		status = -EAGAIN;
		reason = RESTART_META;
		goto leave;
	} else if ((!free_extents)
		   && (ocfs2_alloc_context_bits_left(meta_ac)
		       < ocfs2_extend_meta_needed(et->et_root_el))) {
		err = -2;
		status = -EAGAIN;
		reason = RESTART_META;
		goto leave;
	}

	status = __ocfs2_claim_clusters(handle, data_ac, 1,
					clusters_to_add, &bit_off, &num_bits);
	if (status < 0) {
		if (status != -ENOSPC)
			mlog_errno(status);
		goto leave;
	}

	BUG_ON(num_bits > clusters_to_add);

	/* reserve our write early -- insert_extent may update the tree root */
	status = ocfs2_et_root_journal_access(handle, et,
					      OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		need_free = 1;
		goto bail;
	}

	block = ocfs2_clusters_to_blocks(osb->sb, bit_off);
	trace_ocfs2_add_clusters_in_btree(
	     (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
	     bit_off, num_bits);
	status = ocfs2_insert_extent(handle, et, *logical_offset, block,
				     num_bits, flags, meta_ac);
	if (status < 0) {
		mlog_errno(status);
		need_free = 1;
		goto bail;
	}

	ocfs2_journal_dirty(handle, et->et_root_bh);

	clusters_to_add -= num_bits;
	*logical_offset += num_bits;

	if (clusters_to_add) {
		err = clusters_to_add;
		status = -EAGAIN;
		reason = RESTART_TRANS;
	}

bail:
	if (need_free) {
		if (data_ac->ac_which == OCFS2_AC_USE_LOCAL)
			ocfs2_free_local_alloc_bits(osb, handle, data_ac,
					bit_off, num_bits);
		else
			ocfs2_free_clusters(handle,
					data_ac->ac_inode,
					data_ac->ac_bh,
					ocfs2_clusters_to_blocks(osb->sb, bit_off),
					num_bits);
	}

leave:
	if (reason_ret)
		*reason_ret = reason;
	trace_ocfs2_add_clusters_in_btree_ret(status, reason, err);
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

static int ocfs2_split_and_insert(handle_t *handle,
				  struct ocfs2_extent_tree *et,
				  struct ocfs2_path *path,
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

leftright:
	/*
	 * Store a copy of the record on the stack - it might move
	 * around as the tree is manipulated below.
	 */
	rec = path_leaf_el(path)->l_recs[split_index];

	rightmost_el = et->et_root_el;

	depth = le16_to_cpu(rightmost_el->l_tree_depth);
	if (depth) {
		BUG_ON(!(*last_eb_bh));
		eb = (struct ocfs2_extent_block *) (*last_eb_bh)->b_data;
		rightmost_el = &eb->h_list;
	}

	if (le16_to_cpu(rightmost_el->l_next_free_rec) ==
	    le16_to_cpu(rightmost_el->l_count)) {
		ret = ocfs2_grow_tree(handle, et,
				      &depth, last_eb_bh, meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	memset(&insert, 0, sizeof(struct ocfs2_insert_type));
	insert.ins_appending = APPEND_NONE;
	insert.ins_contig = CONTIG_NONE;
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

		ocfs2_make_right_split_rec(ocfs2_metadata_cache_get_super(et->et_ci),
					   &tmprec, insert_range, &rec);

		split_rec = tmprec;

		BUG_ON(do_leftright);
		do_leftright = 1;
	}

	ret = ocfs2_do_insert_extent(handle, et, &split_rec, &insert);
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
		ret = ocfs2_find_path(et->et_ci, path, cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		el = path_leaf_el(path);
		split_index = ocfs2_search_extent_list(el, cpos);
		if (split_index == -1) {
			ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
				    "Owner %llu has an extent at cpos %u which can no longer be found\n",
				    (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
				    cpos);
			ret = -EROFS;
			goto out;
		}
		goto leftright;
	}
out:

	return ret;
}

static int ocfs2_replace_extent_rec(handle_t *handle,
				    struct ocfs2_extent_tree *et,
				    struct ocfs2_path *path,
				    struct ocfs2_extent_list *el,
				    int split_index,
				    struct ocfs2_extent_rec *split_rec)
{
	int ret;

	ret = ocfs2_path_bh_journal_access(handle, et->et_ci, path,
					   path_num_items(path) - 1);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	el->l_recs[split_index] = *split_rec;

	ocfs2_journal_dirty(handle, path_leaf_bh(path));
out:
	return ret;
}

/*
 * Split part or all of the extent record at split_index in the leaf
 * pointed to by path. Merge with the contiguous extent record if needed.
 *
 * Care is taken to handle contiguousness so as to not grow the tree.
 *
 * meta_ac is not strictly necessary - we only truly need it if growth
 * of the tree is required. All other cases will degrade into a less
 * optimal tree layout.
 *
 * last_eb_bh should be the rightmost leaf block for any extent
 * btree. Since a split may grow the tree or a merge might shrink it,
 * the caller cannot trust the contents of that buffer after this call.
 *
 * This code is optimized for readability - several passes might be
 * made over certain portions of the tree. All of those blocks will
 * have been brought into cache (and pinned via the journal), so the
 * extra overhead is not expressed in terms of disk reads.
 */
int ocfs2_split_extent(handle_t *handle,
		       struct ocfs2_extent_tree *et,
		       struct ocfs2_path *path,
		       int split_index,
		       struct ocfs2_extent_rec *split_rec,
		       struct ocfs2_alloc_context *meta_ac,
		       struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret = 0;
	struct ocfs2_extent_list *el = path_leaf_el(path);
	struct buffer_head *last_eb_bh = NULL;
	struct ocfs2_extent_rec *rec = &el->l_recs[split_index];
	struct ocfs2_merge_ctxt ctxt;

	if (le32_to_cpu(rec->e_cpos) > le32_to_cpu(split_rec->e_cpos) ||
	    ((le32_to_cpu(rec->e_cpos) + le16_to_cpu(rec->e_leaf_clusters)) <
	     (le32_to_cpu(split_rec->e_cpos) + le16_to_cpu(split_rec->e_leaf_clusters)))) {
		ret = -EIO;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_figure_merge_contig_type(et, path, el,
					     split_index,
					     split_rec,
					     &ctxt);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * The core merge / split code wants to know how much room is
	 * left in this allocation tree, so we pass the
	 * rightmost extent list.
	 */
	if (path->p_tree_depth) {
		ret = ocfs2_read_extent_block(et->et_ci,
					      ocfs2_et_get_last_eb_blk(et),
					      &last_eb_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (rec->e_cpos == split_rec->e_cpos &&
	    rec->e_leaf_clusters == split_rec->e_leaf_clusters)
		ctxt.c_split_covers_rec = 1;
	else
		ctxt.c_split_covers_rec = 0;

	ctxt.c_has_empty_extent = ocfs2_is_empty_extent(&el->l_recs[0]);

	trace_ocfs2_split_extent(split_index, ctxt.c_contig_type,
				 ctxt.c_has_empty_extent,
				 ctxt.c_split_covers_rec);

	if (ctxt.c_contig_type == CONTIG_NONE) {
		if (ctxt.c_split_covers_rec)
			ret = ocfs2_replace_extent_rec(handle, et, path, el,
						       split_index, split_rec);
		else
			ret = ocfs2_split_and_insert(handle, et, path,
						     &last_eb_bh, split_index,
						     split_rec, meta_ac);
		if (ret)
			mlog_errno(ret);
	} else {
		ret = ocfs2_try_to_merge_extent(handle, et, path,
						split_index, split_rec,
						dealloc, &ctxt);
		if (ret)
			mlog_errno(ret);
	}

out:
	brelse(last_eb_bh);
	return ret;
}

/*
 * Change the flags of the already-existing extent at cpos for len clusters.
 *
 * new_flags: the flags we want to set.
 * clear_flags: the flags we want to clear.
 * phys: the new physical offset we want this new extent starts from.
 *
 * If the existing extent is larger than the request, initiate a
 * split. An attempt will be made at merging with adjacent extents.
 *
 * The caller is responsible for passing down meta_ac if we'll need it.
 */
int ocfs2_change_extent_flag(handle_t *handle,
			     struct ocfs2_extent_tree *et,
			     u32 cpos, u32 len, u32 phys,
			     struct ocfs2_alloc_context *meta_ac,
			     struct ocfs2_cached_dealloc_ctxt *dealloc,
			     int new_flags, int clear_flags)
{
	int ret, index;
	struct super_block *sb = ocfs2_metadata_cache_get_super(et->et_ci);
	u64 start_blkno = ocfs2_clusters_to_blocks(sb, phys);
	struct ocfs2_extent_rec split_rec;
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;

	left_path = ocfs2_new_path_from_et(et);
	if (!left_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(et->et_ci, left_path, cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	el = path_leaf_el(left_path);

	index = ocfs2_search_extent_list(el, cpos);
	if (index == -1) {
		ocfs2_error(sb,
			    "Owner %llu has an extent at cpos %u which can no longer be found\n",
			    (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
			    cpos);
		ret = -EROFS;
		goto out;
	}

	ret = -EIO;
	rec = &el->l_recs[index];
	if (new_flags && (rec->e_flags & new_flags)) {
		mlog(ML_ERROR, "Owner %llu tried to set %d flags on an "
		     "extent that already had them\n",
		     (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
		     new_flags);
		goto out;
	}

	if (clear_flags && !(rec->e_flags & clear_flags)) {
		mlog(ML_ERROR, "Owner %llu tried to clear %d flags on an "
		     "extent that didn't have them\n",
		     (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
		     clear_flags);
		goto out;
	}

	memset(&split_rec, 0, sizeof(struct ocfs2_extent_rec));
	split_rec.e_cpos = cpu_to_le32(cpos);
	split_rec.e_leaf_clusters = cpu_to_le16(len);
	split_rec.e_blkno = cpu_to_le64(start_blkno);
	split_rec.e_flags = rec->e_flags;
	if (new_flags)
		split_rec.e_flags |= new_flags;
	if (clear_flags)
		split_rec.e_flags &= ~clear_flags;

	ret = ocfs2_split_extent(handle, et, left_path,
				 index, &split_rec, meta_ac,
				 dealloc);
	if (ret)
		mlog_errno(ret);

out:
	ocfs2_free_path(left_path);
	return ret;

}

/*
 * Mark the already-existing extent at cpos as written for len clusters.
 * This removes the unwritten extent flag.
 *
 * If the existing extent is larger than the request, initiate a
 * split. An attempt will be made at merging with adjacent extents.
 *
 * The caller is responsible for passing down meta_ac if we'll need it.
 */
int ocfs2_mark_extent_written(struct inode *inode,
			      struct ocfs2_extent_tree *et,
			      handle_t *handle, u32 cpos, u32 len, u32 phys,
			      struct ocfs2_alloc_context *meta_ac,
			      struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret;

	trace_ocfs2_mark_extent_written(
		(unsigned long long)OCFS2_I(inode)->ip_blkno,
		cpos, len, phys);

	if (!ocfs2_writes_unwritten_extents(OCFS2_SB(inode->i_sb))) {
		ocfs2_error(inode->i_sb, "Inode %llu has unwritten extents that are being written to, but the feature bit is not set in the super block\n",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno);
		ret = -EROFS;
		goto out;
	}

	/*
	 * XXX: This should be fixed up so that we just re-insert the
	 * next extent records.
	 */
	ocfs2_et_extent_map_truncate(et, 0);

	ret = ocfs2_change_extent_flag(handle, et, cpos,
				       len, phys, meta_ac, dealloc,
				       0, OCFS2_EXT_UNWRITTEN);
	if (ret)
		mlog_errno(ret);

out:
	return ret;
}

static int ocfs2_split_tree(handle_t *handle, struct ocfs2_extent_tree *et,
			    struct ocfs2_path *path,
			    int index, u32 new_range,
			    struct ocfs2_alloc_context *meta_ac)
{
	int ret, depth, credits;
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
	ocfs2_make_right_split_rec(ocfs2_metadata_cache_get_super(et->et_ci),
				   &split_rec, new_range, rec);

	depth = path->p_tree_depth;
	if (depth > 0) {
		ret = ocfs2_read_extent_block(et->et_ci,
					      ocfs2_et_get_last_eb_blk(et),
					      &last_eb_bh);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) last_eb_bh->b_data;
		rightmost_el = &eb->h_list;
	} else
		rightmost_el = path_leaf_el(path);

	credits = path->p_tree_depth +
		  ocfs2_extend_meta_needed(et->et_root_el);
	ret = ocfs2_extend_trans(handle, credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (le16_to_cpu(rightmost_el->l_next_free_rec) ==
	    le16_to_cpu(rightmost_el->l_count)) {
		ret = ocfs2_grow_tree(handle, et, &depth, &last_eb_bh,
				      meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	memset(&insert, 0, sizeof(struct ocfs2_insert_type));
	insert.ins_appending = APPEND_NONE;
	insert.ins_contig = CONTIG_NONE;
	insert.ins_split = SPLIT_RIGHT;
	insert.ins_tree_depth = depth;

	ret = ocfs2_do_insert_extent(handle, et, &split_rec, &insert);
	if (ret)
		mlog_errno(ret);

out:
	brelse(last_eb_bh);
	return ret;
}

static int ocfs2_truncate_rec(handle_t *handle,
			      struct ocfs2_extent_tree *et,
			      struct ocfs2_path *path, int index,
			      struct ocfs2_cached_dealloc_ctxt *dealloc,
			      u32 cpos, u32 len)
{
	int ret;
	u32 left_cpos, rec_range, trunc_range;
	int is_rightmost_tree_rec = 0;
	struct super_block *sb = ocfs2_metadata_cache_get_super(et->et_ci);
	struct ocfs2_path *left_path = NULL;
	struct ocfs2_extent_list *el = path_leaf_el(path);
	struct ocfs2_extent_rec *rec;
	struct ocfs2_extent_block *eb;

	if (ocfs2_is_empty_extent(&el->l_recs[0]) && index > 0) {
		/* extend credit for ocfs2_remove_rightmost_path */
		ret = ocfs2_extend_rotate_transaction(handle, 0,
				jbd2_handle_buffer_credits(handle),
				path);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_rotate_tree_left(handle, et, path, dealloc);
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
		 *   1) Path is the leftmost one in our btree.
		 *   2) The leaf is rightmost and will be empty after
		 *      we remove the extent record - the rotate code
		 *      knows how to update the newly formed edge.
		 */

		ret = ocfs2_find_cpos_for_left_leaf(sb, path, &left_cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (left_cpos && le16_to_cpu(el->l_next_free_rec) > 1) {
			left_path = ocfs2_new_path_from_path(path);
			if (!left_path) {
				ret = -ENOMEM;
				mlog_errno(ret);
				goto out;
			}

			ret = ocfs2_find_path(et->et_ci, left_path,
					      left_cpos);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}
	}

	ret = ocfs2_extend_rotate_transaction(handle, 0,
					jbd2_handle_buffer_credits(handle),
					path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_path(et->et_ci, handle, path);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_path(et->et_ci, handle, left_path);
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

		next_free = le16_to_cpu(el->l_next_free_rec);
		if (is_rightmost_tree_rec && next_free > 1) {
			/*
			 * We skip the edge update if this path will
			 * be deleted by the rotate code.
			 */
			rec = &el->l_recs[next_free - 1];
			ocfs2_adjust_rightmost_records(handle, et, path,
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
			ocfs2_adjust_rightmost_records(handle, et, path, rec);
	} else {
		/* Caller should have trapped this. */
		mlog(ML_ERROR, "Owner %llu: Invalid record truncate: (%u, %u) "
		     "(%u, %u)\n",
		     (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
		     le32_to_cpu(rec->e_cpos),
		     le16_to_cpu(rec->e_leaf_clusters), cpos, len);
		BUG();
	}

	if (left_path) {
		int subtree_index;

		subtree_index = ocfs2_find_subtree_root(et, left_path, path);
		ocfs2_complete_edge_insert(handle, left_path, path,
					   subtree_index);
	}

	ocfs2_journal_dirty(handle, path_leaf_bh(path));

	ret = ocfs2_rotate_tree_left(handle, et, path, dealloc);
	if (ret)
		mlog_errno(ret);

out:
	ocfs2_free_path(left_path);
	return ret;
}

int ocfs2_remove_extent(handle_t *handle,
			struct ocfs2_extent_tree *et,
			u32 cpos, u32 len,
			struct ocfs2_alloc_context *meta_ac,
			struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, index;
	u32 rec_range, trunc_range;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_extent_list *el;
	struct ocfs2_path *path = NULL;

	/*
	 * XXX: Why are we truncating to 0 instead of wherever this
	 * affects us?
	 */
	ocfs2_et_extent_map_truncate(et, 0);

	path = ocfs2_new_path_from_et(et);
	if (!path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(et->et_ci, path, cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	el = path_leaf_el(path);
	index = ocfs2_search_extent_list(el, cpos);
	if (index == -1) {
		ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
			    "Owner %llu has an extent at cpos %u which can no longer be found\n",
			    (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
			    cpos);
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

	trace_ocfs2_remove_extent(
		(unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
		cpos, len, index, le32_to_cpu(rec->e_cpos),
		ocfs2_rec_clusters(el, rec));

	if (le32_to_cpu(rec->e_cpos) == cpos || rec_range == trunc_range) {
		ret = ocfs2_truncate_rec(handle, et, path, index, dealloc,
					 cpos, len);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	} else {
		ret = ocfs2_split_tree(handle, et, path, index,
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

		ret = ocfs2_find_path(et->et_ci, path, cpos);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		el = path_leaf_el(path);
		index = ocfs2_search_extent_list(el, cpos);
		if (index == -1) {
			ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
				    "Owner %llu: split at cpos %u lost record\n",
				    (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
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
			ocfs2_error(ocfs2_metadata_cache_get_super(et->et_ci),
				    "Owner %llu: error after split at cpos %u trunc len %u, existing record is (%u,%u)\n",
				    (unsigned long long)ocfs2_metadata_cache_owner(et->et_ci),
				    cpos, len, le32_to_cpu(rec->e_cpos),
				    ocfs2_rec_clusters(el, rec));
			ret = -EROFS;
			goto out;
		}

		ret = ocfs2_truncate_rec(handle, et, path, index, dealloc,
					 cpos, len);
		if (ret)
			mlog_errno(ret);
	}

out:
	ocfs2_free_path(path);
	return ret;
}

/*
 * ocfs2_reserve_blocks_for_rec_trunc() would look basically the
 * same as ocfs2_lock_alloctors(), except for it accepts a blocks
 * number to reserve some extra blocks, and it only handles meta
 * data allocations.
 *
 * Currently, only ocfs2_remove_btree_range() uses it for truncating
 * and punching holes.
 */
static int ocfs2_reserve_blocks_for_rec_trunc(struct inode *inode,
					      struct ocfs2_extent_tree *et,
					      u32 extents_to_split,
					      struct ocfs2_alloc_context **ac,
					      int extra_blocks)
{
	int ret = 0, num_free_extents;
	unsigned int max_recs_needed = 2 * extents_to_split;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	*ac = NULL;

	num_free_extents = ocfs2_num_free_extents(et);
	if (num_free_extents < 0) {
		ret = num_free_extents;
		mlog_errno(ret);
		goto out;
	}

	if (!num_free_extents ||
	    (ocfs2_sparse_alloc(osb) && num_free_extents < max_recs_needed))
		extra_blocks += ocfs2_extend_meta_needed(et->et_root_el);

	if (extra_blocks) {
		ret = ocfs2_reserve_new_metadata_blocks(osb, extra_blocks, ac);
		if (ret < 0) {
			if (ret != -ENOSPC)
				mlog_errno(ret);
		}
	}

out:
	if (ret) {
		if (*ac) {
			ocfs2_free_alloc_context(*ac);
			*ac = NULL;
		}
	}

	return ret;
}

int ocfs2_remove_btree_range(struct inode *inode,
			     struct ocfs2_extent_tree *et,
			     u32 cpos, u32 phys_cpos, u32 len, int flags,
			     struct ocfs2_cached_dealloc_ctxt *dealloc,
			     u64 refcount_loc, bool refcount_tree_locked)
{
	int ret, credits = 0, extra_blocks = 0;
	u64 phys_blkno = ocfs2_clusters_to_blocks(inode->i_sb, phys_cpos);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct inode *tl_inode = osb->osb_tl_inode;
	handle_t *handle;
	struct ocfs2_alloc_context *meta_ac = NULL;
	struct ocfs2_refcount_tree *ref_tree = NULL;

	if ((flags & OCFS2_EXT_REFCOUNTED) && len) {
		BUG_ON(!ocfs2_is_refcount_inode(inode));

		if (!refcount_tree_locked) {
			ret = ocfs2_lock_refcount_tree(osb, refcount_loc, 1,
						       &ref_tree, NULL);
			if (ret) {
				mlog_errno(ret);
				goto bail;
			}
		}

		ret = ocfs2_prepare_refcount_change_for_del(inode,
							    refcount_loc,
							    phys_blkno,
							    len,
							    &credits,
							    &extra_blocks);
		if (ret < 0) {
			mlog_errno(ret);
			goto bail;
		}
	}

	ret = ocfs2_reserve_blocks_for_rec_trunc(inode, et, 1, &meta_ac,
						 extra_blocks);
	if (ret) {
		mlog_errno(ret);
		goto bail;
	}

	inode_lock(tl_inode);

	if (ocfs2_truncate_log_needs_flush(osb)) {
		ret = __ocfs2_flush_truncate_log(osb);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}
	}

	handle = ocfs2_start_trans(osb,
			ocfs2_remove_extent_credits(osb->sb) + credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_et_root_journal_access(handle, et,
					   OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	dquot_free_space_nodirty(inode,
				  ocfs2_clusters_to_bytes(inode->i_sb, len));

	ret = ocfs2_remove_extent(handle, et, cpos, len, meta_ac, dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ocfs2_et_update_clusters(et, -len);
	ocfs2_update_inode_fsync_trans(handle, inode, 1);

	ocfs2_journal_dirty(handle, et->et_root_bh);

	if (phys_blkno) {
		if (flags & OCFS2_EXT_REFCOUNTED)
			ret = ocfs2_decrease_refcount(inode, handle,
					ocfs2_blocks_to_clusters(osb->sb,
								 phys_blkno),
					len, meta_ac,
					dealloc, 1);
		else
			ret = ocfs2_truncate_log_append(osb, handle,
							phys_blkno, len);
		if (ret)
			mlog_errno(ret);

	}

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	inode_unlock(tl_inode);
bail:
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	if (ref_tree)
		ocfs2_unlock_refcount_tree(osb, ref_tree, 1);

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

	BUG_ON(inode_trylock(tl_inode));

	start_cluster = ocfs2_blocks_to_clusters(osb->sb, start_blk);

	di = (struct ocfs2_dinode *) tl_bh->b_data;

	/* tl_bh is loaded from ocfs2_truncate_log_init().  It's validated
	 * by the underlying call to ocfs2_read_inode_block(), so any
	 * corruption is a code bug */
	BUG_ON(!OCFS2_IS_VALID_DINODE(di));

	tl = &di->id2.i_dealloc;
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

	status = ocfs2_journal_access_di(handle, INODE_CACHE(tl_inode), tl_bh,
					 OCFS2_JOURNAL_ACCESS_WRITE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	trace_ocfs2_truncate_log_append(
		(unsigned long long)OCFS2_I(tl_inode)->ip_blkno, index,
		start_cluster, num_clusters);
	if (ocfs2_truncate_log_can_coalesce(tl, start_cluster)) {
		/*
		 * Move index back to the record we are coalescing with.
		 * ocfs2_truncate_log_can_coalesce() guarantees nonzero
		 */
		index--;

		num_clusters += le32_to_cpu(tl->tl_recs[index].t_clusters);
		trace_ocfs2_truncate_log_append(
			(unsigned long long)OCFS2_I(tl_inode)->ip_blkno,
			index, le32_to_cpu(tl->tl_recs[index].t_start),
			num_clusters);
	} else {
		tl->tl_recs[index].t_start = cpu_to_le32(start_cluster);
		tl->tl_used = cpu_to_le16(index + 1);
	}
	tl->tl_recs[index].t_clusters = cpu_to_le32(num_clusters);

	ocfs2_journal_dirty(handle, tl_bh);

	osb->truncated_clusters += num_clusters;
bail:
	return status;
}

static int ocfs2_replay_truncate_records(struct ocfs2_super *osb,
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
	handle_t *handle;

	di = (struct ocfs2_dinode *) tl_bh->b_data;
	tl = &di->id2.i_dealloc;
	i = le16_to_cpu(tl->tl_used) - 1;
	while (i >= 0) {
		handle = ocfs2_start_trans(osb, OCFS2_TRUNCATE_LOG_FLUSH_ONE_REC);
		if (IS_ERR(handle)) {
			status = PTR_ERR(handle);
			mlog_errno(status);
			goto bail;
		}

		/* Caller has given us at least enough credits to
		 * update the truncate log dinode */
		status = ocfs2_journal_access_di(handle, INODE_CACHE(tl_inode), tl_bh,
						 OCFS2_JOURNAL_ACCESS_WRITE);
		if (status < 0) {
			ocfs2_commit_trans(osb, handle);
			mlog_errno(status);
			goto bail;
		}

		tl->tl_used = cpu_to_le16(i);

		ocfs2_journal_dirty(handle, tl_bh);

		rec = tl->tl_recs[i];
		start_blk = ocfs2_clusters_to_blocks(data_alloc_inode->i_sb,
						    le32_to_cpu(rec.t_start));
		num_clusters = le32_to_cpu(rec.t_clusters);

		/* if start_blk is not set, we ignore the record as
		 * invalid. */
		if (start_blk) {
			trace_ocfs2_replay_truncate_records(
				(unsigned long long)OCFS2_I(tl_inode)->ip_blkno,
				i, le32_to_cpu(rec.t_start), num_clusters);

			status = ocfs2_free_clusters(handle, data_alloc_inode,
						     data_alloc_bh, start_blk,
						     num_clusters);
			if (status < 0) {
				ocfs2_commit_trans(osb, handle);
				mlog_errno(status);
				goto bail;
			}
		}

		ocfs2_commit_trans(osb, handle);
		i--;
	}

	osb->truncated_clusters = 0;

bail:
	return status;
}

/* Expects you to already be holding tl_inode->i_rwsem */
int __ocfs2_flush_truncate_log(struct ocfs2_super *osb)
{
	int status;
	unsigned int num_to_flush;
	struct inode *tl_inode = osb->osb_tl_inode;
	struct inode *data_alloc_inode = NULL;
	struct buffer_head *tl_bh = osb->osb_tl_bh;
	struct buffer_head *data_alloc_bh = NULL;
	struct ocfs2_dinode *di;
	struct ocfs2_truncate_log *tl;
	struct ocfs2_journal *journal = osb->journal;

	BUG_ON(inode_trylock(tl_inode));

	di = (struct ocfs2_dinode *) tl_bh->b_data;

	/* tl_bh is loaded from ocfs2_truncate_log_init().  It's validated
	 * by the underlying call to ocfs2_read_inode_block(), so any
	 * corruption is a code bug */
	BUG_ON(!OCFS2_IS_VALID_DINODE(di));

	tl = &di->id2.i_dealloc;
	num_to_flush = le16_to_cpu(tl->tl_used);
	trace_ocfs2_flush_truncate_log(
		(unsigned long long)OCFS2_I(tl_inode)->ip_blkno,
		num_to_flush);
	if (!num_to_flush) {
		status = 0;
		goto out;
	}

	/* Appending truncate log(TA) and flushing truncate log(TF) are
	 * two separated transactions. They can be both committed but not
	 * checkpointed. If crash occurs then, both two transaction will be
	 * replayed with several already released to global bitmap clusters.
	 * Then truncate log will be replayed resulting in cluster double free.
	 */
	jbd2_journal_lock_updates(journal->j_journal);
	status = jbd2_journal_flush(journal->j_journal, 0);
	jbd2_journal_unlock_updates(journal->j_journal);
	if (status < 0) {
		mlog_errno(status);
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

	inode_lock(data_alloc_inode);

	status = ocfs2_inode_lock(data_alloc_inode, &data_alloc_bh, 1);
	if (status < 0) {
		mlog_errno(status);
		goto out_mutex;
	}

	status = ocfs2_replay_truncate_records(osb, data_alloc_inode,
					       data_alloc_bh);
	if (status < 0)
		mlog_errno(status);

	brelse(data_alloc_bh);
	ocfs2_inode_unlock(data_alloc_inode, 1);

out_mutex:
	inode_unlock(data_alloc_inode);
	iput(data_alloc_inode);

out:
	return status;
}

int ocfs2_flush_truncate_log(struct ocfs2_super *osb)
{
	int status;
	struct inode *tl_inode = osb->osb_tl_inode;

	inode_lock(tl_inode);
	status = __ocfs2_flush_truncate_log(osb);
	inode_unlock(tl_inode);

	return status;
}

static void ocfs2_truncate_log_worker(struct work_struct *work)
{
	int status;
	struct ocfs2_super *osb =
		container_of(work, struct ocfs2_super,
			     osb_truncate_log_wq.work);

	status = ocfs2_flush_truncate_log(osb);
	if (status < 0)
		mlog_errno(status);
	else
		ocfs2_init_steal_slots(osb);
}

#define OCFS2_TRUNCATE_LOG_FLUSH_INTERVAL (2 * HZ)
void ocfs2_schedule_truncate_log_flush(struct ocfs2_super *osb,
				       int cancel)
{
	if (osb->osb_tl_inode &&
			atomic_read(&osb->osb_tl_disable) == 0) {
		/* We want to push off log flushes while truncates are
		 * still running. */
		if (cancel)
			cancel_delayed_work(&osb->osb_truncate_log_wq);

		queue_delayed_work(osb->ocfs2_wq, &osb->osb_truncate_log_wq,
				   OCFS2_TRUNCATE_LOG_FLUSH_INTERVAL);
	}
}

/*
 * Try to flush truncate logs if we can free enough clusters from it.
 * As for return value, "< 0" means error, "0" no space and "1" means
 * we have freed enough spaces and let the caller try to allocate again.
 */
int ocfs2_try_to_free_truncate_log(struct ocfs2_super *osb,
					unsigned int needed)
{
	tid_t target;
	int ret = 0;
	unsigned int truncated_clusters;

	inode_lock(osb->osb_tl_inode);
	truncated_clusters = osb->truncated_clusters;
	inode_unlock(osb->osb_tl_inode);

	/*
	 * Check whether we can succeed in allocating if we free
	 * the truncate log.
	 */
	if (truncated_clusters < needed)
		goto out;

	ret = ocfs2_flush_truncate_log(osb);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (jbd2_journal_start_commit(osb->journal->j_journal, &target)) {
		jbd2_log_wait_commit(osb->journal->j_journal, target);
		ret = 1;
	}
out:
	return ret;
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

	status = ocfs2_read_inode_block(inode, &bh);
	if (status < 0) {
		iput(inode);
		mlog_errno(status);
		goto bail;
	}

	*tl_inode = inode;
	*tl_bh    = bh;
bail:
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

	trace_ocfs2_begin_truncate_log_recovery(slot_num);

	status = ocfs2_get_truncate_log_info(osb, slot_num, &tl_inode, &tl_bh);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	di = (struct ocfs2_dinode *) tl_bh->b_data;

	/* tl_bh is loaded from ocfs2_get_truncate_log_info().  It's
	 * validated by the underlying call to ocfs2_read_inode_block(),
	 * so any corruption is a code bug */
	BUG_ON(!OCFS2_IS_VALID_DINODE(di));

	tl = &di->id2.i_dealloc;
	if (le16_to_cpu(tl->tl_used)) {
		trace_ocfs2_truncate_log_recovery_num(le16_to_cpu(tl->tl_used));

		/*
		 * Assuming the write-out below goes well, this copy will be
		 * passed back to recovery for processing.
		 */
		*tl_copy = kmemdup(tl_bh->b_data, tl_bh->b_size, GFP_KERNEL);
		if (!(*tl_copy)) {
			status = -ENOMEM;
			mlog_errno(status);
			goto bail;
		}

		/* All we need to do to clear the truncate log is set
		 * tl_used. */
		tl->tl_used = 0;

		ocfs2_compute_meta_ecc(osb->sb, tl_bh->b_data, &di->i_check);
		status = ocfs2_write_block(osb, tl_bh, INODE_CACHE(tl_inode));
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

bail:
	iput(tl_inode);
	brelse(tl_bh);

	if (status < 0) {
		kfree(*tl_copy);
		*tl_copy = NULL;
		mlog_errno(status);
	}

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

	if (OCFS2_I(tl_inode)->ip_blkno == le64_to_cpu(tl_copy->i_blkno)) {
		mlog(ML_ERROR, "Asked to recover my own truncate log!\n");
		return -EINVAL;
	}

	tl = &tl_copy->id2.i_dealloc;
	num_recs = le16_to_cpu(tl->tl_used);
	trace_ocfs2_complete_truncate_log_recovery(
		(unsigned long long)le64_to_cpu(tl_copy->i_blkno),
		num_recs);

	inode_lock(tl_inode);
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
	inode_unlock(tl_inode);

	return status;
}

void ocfs2_truncate_log_shutdown(struct ocfs2_super *osb)
{
	int status;
	struct inode *tl_inode = osb->osb_tl_inode;

	atomic_set(&osb->osb_tl_disable, 1);

	if (tl_inode) {
		cancel_delayed_work(&osb->osb_truncate_log_wq);
		flush_workqueue(osb->ocfs2_wq);

		status = ocfs2_flush_truncate_log(osb);
		if (status < 0)
			mlog_errno(status);

		brelse(osb->osb_tl_bh);
		iput(osb->osb_tl_inode);
	}
}

int ocfs2_truncate_log_init(struct ocfs2_super *osb)
{
	int status;
	struct inode *tl_inode = NULL;
	struct buffer_head *tl_bh = NULL;

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
	atomic_set(&osb->osb_tl_disable, 0);
	osb->osb_tl_bh    = tl_bh;
	osb->osb_tl_inode = tl_inode;

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
 * Describe a single bit freed from a suballocator.  For the block
 * suballocators, it represents one block.  For the global cluster
 * allocator, it represents some clusters and free_bit indicates
 * clusters number.
 */
struct ocfs2_cached_block_free {
	struct ocfs2_cached_block_free		*free_next;
	u64					free_bg;
	u64					free_blk;
	unsigned int				free_bit;
};

struct ocfs2_per_slot_free_list {
	struct ocfs2_per_slot_free_list		*f_next_suballocator;
	int					f_inode_type;
	int					f_slot;
	struct ocfs2_cached_block_free		*f_first;
};

static int ocfs2_free_cached_blocks(struct ocfs2_super *osb,
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

	inode_lock(inode);

	ret = ocfs2_inode_lock(inode, &di_bh, 1);
	if (ret) {
		mlog_errno(ret);
		goto out_mutex;
	}

	while (head) {
		if (head->free_bg)
			bg_blkno = head->free_bg;
		else
			bg_blkno = ocfs2_which_suballoc_group(head->free_blk,
							      head->free_bit);
		handle = ocfs2_start_trans(osb, OCFS2_SUBALLOC_FREE);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			mlog_errno(ret);
			goto out_unlock;
		}

		trace_ocfs2_free_cached_blocks(
		     (unsigned long long)head->free_blk, head->free_bit);

		ret = ocfs2_free_suballoc_bits(handle, inode, di_bh,
					       head->free_bit, bg_blkno, 1);
		if (ret)
			mlog_errno(ret);

		ocfs2_commit_trans(osb, handle);

		tmp = head;
		head = head->free_next;
		kfree(tmp);
	}

out_unlock:
	ocfs2_inode_unlock(inode, 1);
	brelse(di_bh);
out_mutex:
	inode_unlock(inode);
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

int ocfs2_cache_cluster_dealloc(struct ocfs2_cached_dealloc_ctxt *ctxt,
				u64 blkno, unsigned int bit)
{
	int ret = 0;
	struct ocfs2_cached_block_free *item;

	item = kzalloc(sizeof(*item), GFP_NOFS);
	if (item == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		return ret;
	}

	trace_ocfs2_cache_cluster_dealloc((unsigned long long)blkno, bit);

	item->free_blk = blkno;
	item->free_bit = bit;
	item->free_next = ctxt->c_global_allocator;

	ctxt->c_global_allocator = item;
	return ret;
}

static int ocfs2_free_cached_clusters(struct ocfs2_super *osb,
				      struct ocfs2_cached_block_free *head)
{
	struct ocfs2_cached_block_free *tmp;
	struct inode *tl_inode = osb->osb_tl_inode;
	handle_t *handle;
	int ret = 0;

	inode_lock(tl_inode);

	while (head) {
		if (ocfs2_truncate_log_needs_flush(osb)) {
			ret = __ocfs2_flush_truncate_log(osb);
			if (ret < 0) {
				mlog_errno(ret);
				break;
			}
		}

		handle = ocfs2_start_trans(osb, OCFS2_TRUNCATE_LOG_UPDATE);
		if (IS_ERR(handle)) {
			ret = PTR_ERR(handle);
			mlog_errno(ret);
			break;
		}

		ret = ocfs2_truncate_log_append(osb, handle, head->free_blk,
						head->free_bit);

		ocfs2_commit_trans(osb, handle);
		tmp = head;
		head = head->free_next;
		kfree(tmp);

		if (ret < 0) {
			mlog_errno(ret);
			break;
		}
	}

	inode_unlock(tl_inode);

	while (head) {
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
			trace_ocfs2_run_deallocs(fl->f_inode_type,
						 fl->f_slot);
			ret2 = ocfs2_free_cached_blocks(osb,
							fl->f_inode_type,
							fl->f_slot,
							fl->f_first);
			if (ret2)
				mlog_errno(ret2);
			if (!ret)
				ret = ret2;
		}

		ctxt->c_first_suballocator = fl->f_next_suballocator;
		kfree(fl);
	}

	if (ctxt->c_global_allocator) {
		ret2 = ocfs2_free_cached_clusters(osb,
						  ctxt->c_global_allocator);
		if (ret2)
			mlog_errno(ret2);
		if (!ret)
			ret = ret2;

		ctxt->c_global_allocator = NULL;
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

static struct ocfs2_per_slot_free_list *
ocfs2_find_preferred_free_list(int type,
			       int preferred_slot,
			       int *real_slot,
			       struct ocfs2_cached_dealloc_ctxt *ctxt)
{
	struct ocfs2_per_slot_free_list *fl = ctxt->c_first_suballocator;

	while (fl) {
		if (fl->f_inode_type == type && fl->f_slot == preferred_slot) {
			*real_slot = fl->f_slot;
			return fl;
		}

		fl = fl->f_next_suballocator;
	}

	/* If we can't find any free list matching preferred slot, just use
	 * the first one.
	 */
	fl = ctxt->c_first_suballocator;
	*real_slot = fl->f_slot;

	return fl;
}

/* Return Value 1 indicates empty */
static int ocfs2_is_dealloc_empty(struct ocfs2_extent_tree *et)
{
	struct ocfs2_per_slot_free_list *fl = NULL;

	if (!et->et_dealloc)
		return 1;

	fl = et->et_dealloc->c_first_suballocator;
	if (!fl)
		return 1;

	if (!fl->f_first)
		return 1;

	return 0;
}

/* If extent was deleted from tree due to extent rotation and merging, and
 * no metadata is reserved ahead of time. Try to reuse some extents
 * just deleted. This is only used to reuse extent blocks.
 * It is supposed to find enough extent blocks in dealloc if our estimation
 * on metadata is accurate.
 */
static int ocfs2_reuse_blk_from_dealloc(handle_t *handle,
					struct ocfs2_extent_tree *et,
					struct buffer_head **new_eb_bh,
					int blk_wanted, int *blk_given)
{
	int i, status = 0, real_slot;
	struct ocfs2_cached_dealloc_ctxt *dealloc;
	struct ocfs2_per_slot_free_list *fl;
	struct ocfs2_cached_block_free *bf;
	struct ocfs2_extent_block *eb;
	struct ocfs2_super *osb =
		OCFS2_SB(ocfs2_metadata_cache_get_super(et->et_ci));

	*blk_given = 0;

	/* If extent tree doesn't have a dealloc, this is not faulty. Just
	 * tell upper caller dealloc can't provide any block and it should
	 * ask for alloc to claim more space.
	 */
	dealloc = et->et_dealloc;
	if (!dealloc)
		goto bail;

	for (i = 0; i < blk_wanted; i++) {
		/* Prefer to use local slot */
		fl = ocfs2_find_preferred_free_list(EXTENT_ALLOC_SYSTEM_INODE,
						    osb->slot_num, &real_slot,
						    dealloc);
		/* If no more block can be reused, we should claim more
		 * from alloc. Just return here normally.
		 */
		if (!fl) {
			status = 0;
			break;
		}

		bf = fl->f_first;
		fl->f_first = bf->free_next;

		new_eb_bh[i] = sb_getblk(osb->sb, bf->free_blk);
		if (new_eb_bh[i] == NULL) {
			status = -ENOMEM;
			mlog_errno(status);
			goto bail;
		}

		mlog(0, "Reusing block(%llu) from "
		     "dealloc(local slot:%d, real slot:%d)\n",
		     bf->free_blk, osb->slot_num, real_slot);

		ocfs2_set_new_buffer_uptodate(et->et_ci, new_eb_bh[i]);

		status = ocfs2_journal_access_eb(handle, et->et_ci,
						 new_eb_bh[i],
						 OCFS2_JOURNAL_ACCESS_CREATE);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		memset(new_eb_bh[i]->b_data, 0, osb->sb->s_blocksize);
		eb = (struct ocfs2_extent_block *) new_eb_bh[i]->b_data;

		/* We can't guarantee that buffer head is still cached, so
		 * polutlate the extent block again.
		 */
		strcpy(eb->h_signature, OCFS2_EXTENT_BLOCK_SIGNATURE);
		eb->h_blkno = cpu_to_le64(bf->free_blk);
		eb->h_fs_generation = cpu_to_le32(osb->fs_generation);
		eb->h_suballoc_slot = cpu_to_le16(real_slot);
		eb->h_suballoc_loc = cpu_to_le64(bf->free_bg);
		eb->h_suballoc_bit = cpu_to_le16(bf->free_bit);
		eb->h_list.l_count =
			cpu_to_le16(ocfs2_extent_recs_per_eb(osb->sb));

		/* We'll also be dirtied by the caller, so
		 * this isn't absolutely necessary.
		 */
		ocfs2_journal_dirty(handle, new_eb_bh[i]);

		if (!fl->f_first) {
			dealloc->c_first_suballocator = fl->f_next_suballocator;
			kfree(fl);
		}
		kfree(bf);
	}

	*blk_given = i;

bail:
	if (unlikely(status < 0)) {
		for (i = 0; i < blk_wanted; i++)
			brelse(new_eb_bh[i]);
	}

	return status;
}

int ocfs2_cache_block_dealloc(struct ocfs2_cached_dealloc_ctxt *ctxt,
			      int type, int slot, u64 suballoc,
			      u64 blkno, unsigned int bit)
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

	item = kzalloc(sizeof(*item), GFP_NOFS);
	if (item == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	trace_ocfs2_cache_block_dealloc(type, slot,
					(unsigned long long)suballoc,
					(unsigned long long)blkno, bit);

	item->free_bg = suballoc;
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
					 le64_to_cpu(eb->h_suballoc_loc),
					 le64_to_cpu(eb->h_blkno),
					 le16_to_cpu(eb->h_suballoc_bit));
}

static int ocfs2_zero_func(handle_t *handle, struct buffer_head *bh)
{
	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	return 0;
}

void ocfs2_map_and_dirty_page(struct inode *inode, handle_t *handle,
			      unsigned int from, unsigned int to,
			      struct page *page, int zero, u64 *phys)
{
	int ret, partial = 0;
	loff_t start_byte = ((loff_t)page->index << PAGE_SHIFT) + from;
	loff_t length = to - from;

	ret = ocfs2_map_page_blocks(page, phys, inode, from, to, 0);
	if (ret)
		mlog_errno(ret);

	if (zero)
		zero_user_segment(page, from, to);

	/*
	 * Need to set the buffers we zero'd into uptodate
	 * here if they aren't - ocfs2_map_page_blocks()
	 * might've skipped some
	 */
	ret = walk_page_buffers(handle, page_buffers(page),
				from, to, &partial,
				ocfs2_zero_func);
	if (ret < 0)
		mlog_errno(ret);
	else if (ocfs2_should_order_data(inode)) {
		ret = ocfs2_jbd2_inode_add_write(handle, inode,
						 start_byte, length);
		if (ret < 0)
			mlog_errno(ret);
	}

	if (!partial)
		SetPageUptodate(page);

	flush_dcache_page(page);
}

static void ocfs2_zero_cluster_pages(struct inode *inode, loff_t start,
				     loff_t end, struct page **pages,
				     int numpages, u64 phys, handle_t *handle)
{
	int i;
	struct page *page;
	unsigned int from, to = PAGE_SIZE;
	struct super_block *sb = inode->i_sb;

	BUG_ON(!ocfs2_sparse_alloc(OCFS2_SB(sb)));

	if (numpages == 0)
		goto out;

	to = PAGE_SIZE;
	for(i = 0; i < numpages; i++) {
		page = pages[i];

		from = start & (PAGE_SIZE - 1);
		if ((end >> PAGE_SHIFT) == page->index)
			to = end & (PAGE_SIZE - 1);

		BUG_ON(from > PAGE_SIZE);
		BUG_ON(to > PAGE_SIZE);

		ocfs2_map_and_dirty_page(inode, handle, from, to, page, 1,
					 &phys);

		start = (page->index + 1) << PAGE_SHIFT;
	}
out:
	if (pages)
		ocfs2_unlock_and_free_pages(pages, numpages);
}

int ocfs2_grab_pages(struct inode *inode, loff_t start, loff_t end,
		     struct page **pages, int *num)
{
	int numpages, ret = 0;
	struct address_space *mapping = inode->i_mapping;
	unsigned long index;
	loff_t last_page_bytes;

	BUG_ON(start > end);

	numpages = 0;
	last_page_bytes = PAGE_ALIGN(end);
	index = start >> PAGE_SHIFT;
	do {
		pages[numpages] = find_or_create_page(mapping, index, GFP_NOFS);
		if (!pages[numpages]) {
			ret = -ENOMEM;
			mlog_errno(ret);
			goto out;
		}

		numpages++;
		index++;
	} while (index < (last_page_bytes >> PAGE_SHIFT));

out:
	if (ret != 0) {
		if (pages)
			ocfs2_unlock_and_free_pages(pages, numpages);
		numpages = 0;
	}

	*num = numpages;

	return ret;
}

static int ocfs2_grab_eof_pages(struct inode *inode, loff_t start, loff_t end,
				struct page **pages, int *num)
{
	struct super_block *sb = inode->i_sb;

	BUG_ON(start >> OCFS2_SB(sb)->s_clustersize_bits !=
	       (end - 1) >> OCFS2_SB(sb)->s_clustersize_bits);

	return ocfs2_grab_pages(inode, start, end, pages, num);
}

/*
 * Zero partial cluster for a hole punch or truncate. This avoids exposing
 * nonzero data on subsequent file extends.
 *
 * We need to call this before i_size is updated on the inode because
 * otherwise block_write_full_page() will skip writeout of pages past
 * i_size.
 */
int ocfs2_zero_range_for_truncate(struct inode *inode, handle_t *handle,
				  u64 range_start, u64 range_end)
{
	int ret = 0, numpages;
	struct page **pages = NULL;
	u64 phys;
	unsigned int ext_flags;
	struct super_block *sb = inode->i_sb;

	/*
	 * File systems which don't support sparse files zero on every
	 * extend.
	 */
	if (!ocfs2_sparse_alloc(OCFS2_SB(sb)))
		return 0;

	/*
	 * Avoid zeroing pages fully beyond current i_size. It is pointless as
	 * underlying blocks of those pages should be already zeroed out and
	 * page writeback will skip them anyway.
	 */
	range_end = min_t(u64, range_end, i_size_read(inode));
	if (range_start >= range_end)
		return 0;

	pages = kcalloc(ocfs2_pages_per_cluster(sb),
			sizeof(struct page *), GFP_NOFS);
	if (pages == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_extent_map_get_blocks(inode,
					  range_start >> sb->s_blocksize_bits,
					  &phys, NULL, &ext_flags);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Tail is a hole, or is marked unwritten. In either case, we
	 * can count on read and write to return/push zero's.
	 */
	if (phys == 0 || ext_flags & OCFS2_EXT_UNWRITTEN)
		goto out;

	ret = ocfs2_grab_eof_pages(inode, range_start, range_end, pages,
				   &numpages);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_zero_cluster_pages(inode, range_start, range_end, pages,
				 numpages, phys, handle);

	/*
	 * Initiate writeout of the pages we zero'd here. We don't
	 * wait on them - the truncate_inode_pages() call later will
	 * do that for us.
	 */
	ret = filemap_fdatawrite_range(inode->i_mapping, range_start,
				       range_end - 1);
	if (ret)
		mlog_errno(ret);

out:
	kfree(pages);

	return ret;
}

static void ocfs2_zero_dinode_id2_with_xattr(struct inode *inode,
					     struct ocfs2_dinode *di)
{
	unsigned int blocksize = 1 << inode->i_sb->s_blocksize_bits;
	unsigned int xattrsize = le16_to_cpu(di->i_xattr_inline_size);

	if (le16_to_cpu(di->i_dyn_features) & OCFS2_INLINE_XATTR_FL)
		memset(&di->id2, 0, blocksize -
				    offsetof(struct ocfs2_dinode, id2) -
				    xattrsize);
	else
		memset(&di->id2, 0, blocksize -
				    offsetof(struct ocfs2_dinode, id2));
}

void ocfs2_dinode_new_extent_list(struct inode *inode,
				  struct ocfs2_dinode *di)
{
	ocfs2_zero_dinode_id2_with_xattr(inode, di);
	di->id2.i_list.l_tree_depth = 0;
	di->id2.i_list.l_next_free_rec = 0;
	di->id2.i_list.l_count = cpu_to_le16(
		ocfs2_extent_recs_per_inode_with_xattr(inode->i_sb, di));
}

void ocfs2_set_inode_data_inline(struct inode *inode, struct ocfs2_dinode *di)
{
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_inline_data *idata = &di->id2.i_data;

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features |= OCFS2_INLINE_DATA_FL;
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	spin_unlock(&oi->ip_lock);

	/*
	 * We clear the entire i_data structure here so that all
	 * fields can be properly initialized.
	 */
	ocfs2_zero_dinode_id2_with_xattr(inode, di);

	idata->id_count = cpu_to_le16(
			ocfs2_max_inline_data_with_xattr(inode->i_sb, di));
}

int ocfs2_convert_inline_data_to_extents(struct inode *inode,
					 struct buffer_head *di_bh)
{
	int ret, has_data, num_pages = 0;
	int need_free = 0;
	u32 bit_off, num;
	handle_t *handle;
	u64 block;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_alloc_context *data_ac = NULL;
	struct page *page = NULL;
	struct ocfs2_extent_tree et;
	int did_quota = 0;

	has_data = i_size_read(inode) ? 1 : 0;

	if (has_data) {
		ret = ocfs2_reserve_clusters(osb, 1, &data_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	handle = ocfs2_start_trans(osb,
				   ocfs2_inline_to_extents_credits(osb->sb));
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	if (has_data) {
		unsigned int page_end = min_t(unsigned, PAGE_SIZE,
							osb->s_clustersize);
		u64 phys;

		ret = dquot_alloc_space_nodirty(inode,
				       ocfs2_clusters_to_bytes(osb->sb, 1));
		if (ret)
			goto out_commit;
		did_quota = 1;

		data_ac->ac_resv = &oi->ip_la_data_resv;

		ret = ocfs2_claim_clusters(handle, data_ac, 1, &bit_off,
					   &num);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}

		/*
		 * Save two copies, one for insert, and one that can
		 * be changed by ocfs2_map_and_dirty_page() below.
		 */
		block = phys = ocfs2_clusters_to_blocks(inode->i_sb, bit_off);

		ret = ocfs2_grab_eof_pages(inode, 0, page_end, &page,
					   &num_pages);
		if (ret) {
			mlog_errno(ret);
			need_free = 1;
			goto out_commit;
		}

		/*
		 * This should populate the 1st page for us and mark
		 * it up to date.
		 */
		ret = ocfs2_read_inline_data(inode, page, di_bh);
		if (ret) {
			mlog_errno(ret);
			need_free = 1;
			goto out_unlock;
		}

		ocfs2_map_and_dirty_page(inode, handle, 0, page_end, page, 0,
					 &phys);
	}

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features &= ~OCFS2_INLINE_DATA_FL;
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	spin_unlock(&oi->ip_lock);

	ocfs2_update_inode_fsync_trans(handle, inode, 1);
	ocfs2_dinode_new_extent_list(inode, di);

	ocfs2_journal_dirty(handle, di_bh);

	if (has_data) {
		/*
		 * An error at this point should be extremely rare. If
		 * this proves to be false, we could always re-build
		 * the in-inode data from our pages.
		 */
		ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(inode), di_bh);
		ret = ocfs2_insert_extent(handle, &et, 0, block, 1, 0, NULL);
		if (ret) {
			mlog_errno(ret);
			need_free = 1;
			goto out_unlock;
		}

		inode->i_blocks = ocfs2_inode_sector_count(inode);
	}

out_unlock:
	if (page)
		ocfs2_unlock_and_free_pages(&page, num_pages);

out_commit:
	if (ret < 0 && did_quota)
		dquot_free_space_nodirty(inode,
					  ocfs2_clusters_to_bytes(osb->sb, 1));

	if (need_free) {
		if (data_ac->ac_which == OCFS2_AC_USE_LOCAL)
			ocfs2_free_local_alloc_bits(osb, handle, data_ac,
					bit_off, num);
		else
			ocfs2_free_clusters(handle,
					data_ac->ac_inode,
					data_ac->ac_bh,
					ocfs2_clusters_to_blocks(osb->sb, bit_off),
					num);
	}

	ocfs2_commit_trans(osb, handle);

out:
	if (data_ac)
		ocfs2_free_alloc_context(data_ac);
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
			  struct buffer_head *di_bh)
{
	int status = 0, i, flags = 0;
	u32 new_highest_cpos, range, trunc_cpos, trunc_len, phys_cpos, coff;
	u64 blkno = 0;
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec;
	struct ocfs2_path *path = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_extent_list *root_el = &(di->id2.i_list);
	u64 refcount_loc = le64_to_cpu(di->i_refcount_loc);
	struct ocfs2_extent_tree et;
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct ocfs2_refcount_tree *ref_tree = NULL;

	ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(inode), di_bh);
	ocfs2_init_dealloc_ctxt(&dealloc);

	new_highest_cpos = ocfs2_clusters_for_bytes(osb->sb,
						     i_size_read(inode));

	path = ocfs2_new_path(di_bh, &di->id2.i_list,
			      ocfs2_journal_access_di);
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
	status = ocfs2_find_path(INODE_CACHE(inode), path, UINT_MAX);
	if (status) {
		mlog_errno(status);
		goto bail;
	}

	trace_ocfs2_commit_truncate(
		(unsigned long long)OCFS2_I(inode)->ip_blkno,
		new_highest_cpos,
		OCFS2_I(inode)->ip_clusters,
		path->p_tree_depth);

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
	rec = &el->l_recs[i];
	flags = rec->e_flags;
	range = le32_to_cpu(rec->e_cpos) + ocfs2_rec_clusters(el, rec);

	if (i == 0 && ocfs2_is_empty_extent(rec)) {
		/*
		 * Lower levels depend on this never happening, but it's best
		 * to check it up here before changing the tree.
		*/
		if (root_el->l_tree_depth && rec->e_int_clusters == 0) {
			mlog(ML_ERROR, "Inode %lu has an empty "
				    "extent record, depth %u\n", inode->i_ino,
				    le16_to_cpu(root_el->l_tree_depth));
			status = ocfs2_remove_rightmost_empty_extent(osb,
					&et, path, &dealloc);
			if (status) {
				mlog_errno(status);
				goto bail;
			}

			ocfs2_reinit_path(path, 1);
			goto start;
		} else {
			trunc_cpos = le32_to_cpu(rec->e_cpos);
			trunc_len = 0;
			blkno = 0;
		}
	} else if (le32_to_cpu(rec->e_cpos) >= new_highest_cpos) {
		/*
		 * Truncate entire record.
		 */
		trunc_cpos = le32_to_cpu(rec->e_cpos);
		trunc_len = ocfs2_rec_clusters(el, rec);
		blkno = le64_to_cpu(rec->e_blkno);
	} else if (range > new_highest_cpos) {
		/*
		 * Partial truncate. it also should be
		 * the last truncate we're doing.
		 */
		trunc_cpos = new_highest_cpos;
		trunc_len = range - new_highest_cpos;
		coff = new_highest_cpos - le32_to_cpu(rec->e_cpos);
		blkno = le64_to_cpu(rec->e_blkno) +
				ocfs2_clusters_to_blocks(inode->i_sb, coff);
	} else {
		/*
		 * Truncate completed, leave happily.
		 */
		status = 0;
		goto bail;
	}

	phys_cpos = ocfs2_blocks_to_clusters(inode->i_sb, blkno);

	if ((flags & OCFS2_EXT_REFCOUNTED) && trunc_len && !ref_tree) {
		status = ocfs2_lock_refcount_tree(osb, refcount_loc, 1,
				&ref_tree, NULL);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
	}

	status = ocfs2_remove_btree_range(inode, &et, trunc_cpos,
					  phys_cpos, trunc_len, flags, &dealloc,
					  refcount_loc, true);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	ocfs2_reinit_path(path, 1);

	/*
	 * The check above will catch the case where we've truncated
	 * away all allocation.
	 */
	goto start;

bail:
	if (ref_tree)
		ocfs2_unlock_refcount_tree(osb, ref_tree, 1);

	ocfs2_schedule_truncate_log_flush(osb, 1);

	ocfs2_run_deallocs(osb, &dealloc);

	ocfs2_free_path(path);

	return status;
}

/*
 * 'start' is inclusive, 'end' is not.
 */
int ocfs2_truncate_inline(struct inode *inode, struct buffer_head *di_bh,
			  unsigned int start, unsigned int end, int trunc)
{
	int ret;
	unsigned int numbytes;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_inline_data *idata = &di->id2.i_data;

	/* No need to punch hole beyond i_size. */
	if (start >= i_size_read(inode))
		return 0;

	if (end > i_size_read(inode))
		end = i_size_read(inode);

	BUG_ON(start > end);

	if (!(OCFS2_I(inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL) ||
	    !(le16_to_cpu(di->i_dyn_features) & OCFS2_INLINE_DATA_FL) ||
	    !ocfs2_supports_inline_data(osb)) {
		ocfs2_error(inode->i_sb,
			    "Inline data flags for inode %llu don't agree! Disk: 0x%x, Memory: 0x%x, Superblock: 0x%x\n",
			    (unsigned long long)OCFS2_I(inode)->ip_blkno,
			    le16_to_cpu(di->i_dyn_features),
			    OCFS2_I(inode)->ip_dyn_features,
			    osb->s_feature_incompat);
		ret = -EROFS;
		goto out;
	}

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	numbytes = end - start;
	memset(idata->id_data + start, 0, numbytes);

	/*
	 * No need to worry about the data page here - it's been
	 * truncated already and inline data doesn't need it for
	 * pushing zero's to disk, so we'll let read_folio pick it up
	 * later.
	 */
	if (trunc) {
		i_size_write(inode, start);
		di->i_size = cpu_to_le64(start);
	}

	inode->i_blocks = ocfs2_inode_sector_count(inode);
	inode->i_mtime = inode_set_ctime_current(inode);

	di->i_ctime = di->i_mtime = cpu_to_le64(inode_get_ctime(inode).tv_sec);
	di->i_ctime_nsec = di->i_mtime_nsec = cpu_to_le32(inode_get_ctime(inode).tv_nsec);

	ocfs2_update_inode_fsync_trans(handle, inode, 1);
	ocfs2_journal_dirty(handle, di_bh);

out_commit:
	ocfs2_commit_trans(osb, handle);

out:
	return ret;
}

static int ocfs2_trim_extent(struct super_block *sb,
			     struct ocfs2_group_desc *gd,
			     u64 group, u32 start, u32 count)
{
	u64 discard, bcount;
	struct ocfs2_super *osb = OCFS2_SB(sb);

	bcount = ocfs2_clusters_to_blocks(sb, count);
	discard = ocfs2_clusters_to_blocks(sb, start);

	/*
	 * For the first cluster group, the gd->bg_blkno is not at the start
	 * of the group, but at an offset from the start. If we add it while
	 * calculating discard for first group, we will wrongly start fstrim a
	 * few blocks after the desried start block and the range can cross
	 * over into the next cluster group. So, add it only if this is not
	 * the first cluster group.
	 */
	if (group != osb->first_cluster_group_blkno)
		discard += le64_to_cpu(gd->bg_blkno);

	trace_ocfs2_trim_extent(sb, (unsigned long long)discard, bcount);

	return sb_issue_discard(sb, discard, bcount, GFP_NOFS, 0);
}

static int ocfs2_trim_group(struct super_block *sb,
			    struct ocfs2_group_desc *gd, u64 group,
			    u32 start, u32 max, u32 minbits)
{
	int ret = 0, count = 0, next;
	void *bitmap = gd->bg_bitmap;

	if (le16_to_cpu(gd->bg_free_bits_count) < minbits)
		return 0;

	trace_ocfs2_trim_group((unsigned long long)le64_to_cpu(gd->bg_blkno),
			       start, max, minbits);

	while (start < max) {
		start = ocfs2_find_next_zero_bit(bitmap, max, start);
		if (start >= max)
			break;
		next = ocfs2_find_next_bit(bitmap, max, start);

		if ((next - start) >= minbits) {
			ret = ocfs2_trim_extent(sb, gd, group,
						start, next - start);
			if (ret < 0) {
				mlog_errno(ret);
				break;
			}
			count += next - start;
		}
		start = next + 1;

		if (fatal_signal_pending(current)) {
			count = -ERESTARTSYS;
			break;
		}

		if ((le16_to_cpu(gd->bg_free_bits_count) - count) < minbits)
			break;
	}

	if (ret < 0)
		count = ret;

	return count;
}

static
int ocfs2_trim_mainbm(struct super_block *sb, struct fstrim_range *range)
{
	struct ocfs2_super *osb = OCFS2_SB(sb);
	u64 start, len, trimmed = 0, first_group, last_group = 0, group = 0;
	int ret, cnt;
	u32 first_bit, last_bit, minlen;
	struct buffer_head *main_bm_bh = NULL;
	struct inode *main_bm_inode = NULL;
	struct buffer_head *gd_bh = NULL;
	struct ocfs2_dinode *main_bm;
	struct ocfs2_group_desc *gd = NULL;

	start = range->start >> osb->s_clustersize_bits;
	len = range->len >> osb->s_clustersize_bits;
	minlen = range->minlen >> osb->s_clustersize_bits;

	if (minlen >= osb->bitmap_cpg || range->len < sb->s_blocksize)
		return -EINVAL;

	trace_ocfs2_trim_mainbm(start, len, minlen);

next_group:
	main_bm_inode = ocfs2_get_system_file_inode(osb,
						    GLOBAL_BITMAP_SYSTEM_INODE,
						    OCFS2_INVALID_SLOT);
	if (!main_bm_inode) {
		ret = -EIO;
		mlog_errno(ret);
		goto out;
	}

	inode_lock(main_bm_inode);

	ret = ocfs2_inode_lock(main_bm_inode, &main_bm_bh, 0);
	if (ret < 0) {
		mlog_errno(ret);
		goto out_mutex;
	}
	main_bm = (struct ocfs2_dinode *)main_bm_bh->b_data;

	/*
	 * Do some check before trim the first group.
	 */
	if (!group) {
		if (start >= le32_to_cpu(main_bm->i_clusters)) {
			ret = -EINVAL;
			goto out_unlock;
		}

		if (start + len > le32_to_cpu(main_bm->i_clusters))
			len = le32_to_cpu(main_bm->i_clusters) - start;

		/*
		 * Determine first and last group to examine based on
		 * start and len
		 */
		first_group = ocfs2_which_cluster_group(main_bm_inode, start);
		if (first_group == osb->first_cluster_group_blkno)
			first_bit = start;
		else
			first_bit = start - ocfs2_blocks_to_clusters(sb,
								first_group);
		last_group = ocfs2_which_cluster_group(main_bm_inode,
						       start + len - 1);
		group = first_group;
	}

	do {
		if (first_bit + len >= osb->bitmap_cpg)
			last_bit = osb->bitmap_cpg;
		else
			last_bit = first_bit + len;

		ret = ocfs2_read_group_descriptor(main_bm_inode,
						  main_bm, group,
						  &gd_bh);
		if (ret < 0) {
			mlog_errno(ret);
			break;
		}

		gd = (struct ocfs2_group_desc *)gd_bh->b_data;
		cnt = ocfs2_trim_group(sb, gd, group,
				       first_bit, last_bit, minlen);
		brelse(gd_bh);
		gd_bh = NULL;
		if (cnt < 0) {
			ret = cnt;
			mlog_errno(ret);
			break;
		}

		trimmed += cnt;
		len -= osb->bitmap_cpg - first_bit;
		first_bit = 0;
		if (group == osb->first_cluster_group_blkno)
			group = ocfs2_clusters_to_blocks(sb, osb->bitmap_cpg);
		else
			group += ocfs2_clusters_to_blocks(sb, osb->bitmap_cpg);
	} while (0);

out_unlock:
	ocfs2_inode_unlock(main_bm_inode, 0);
	brelse(main_bm_bh);
	main_bm_bh = NULL;
out_mutex:
	inode_unlock(main_bm_inode);
	iput(main_bm_inode);

	/*
	 * If all the groups trim are not done or failed, but we should release
	 * main_bm related locks for avoiding the current IO starve, then go to
	 * trim the next group
	 */
	if (ret >= 0 && group <= last_group) {
		cond_resched();
		goto next_group;
	}
out:
	range->len = trimmed * osb->s_clustersize;
	return ret;
}

int ocfs2_trim_fs(struct super_block *sb, struct fstrim_range *range)
{
	int ret;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	struct ocfs2_trim_fs_info info, *pinfo = NULL;

	ocfs2_trim_fs_lock_res_init(osb);

	trace_ocfs2_trim_fs(range->start, range->len, range->minlen);

	ret = ocfs2_trim_fs_lock(osb, NULL, 1);
	if (ret < 0) {
		if (ret != -EAGAIN) {
			mlog_errno(ret);
			ocfs2_trim_fs_lock_res_uninit(osb);
			return ret;
		}

		mlog(ML_NOTICE, "Wait for trim on device (%s) to "
		     "finish, which is running from another node.\n",
		     osb->dev_str);
		ret = ocfs2_trim_fs_lock(osb, &info, 0);
		if (ret < 0) {
			mlog_errno(ret);
			ocfs2_trim_fs_lock_res_uninit(osb);
			return ret;
		}

		if (info.tf_valid && info.tf_success &&
		    info.tf_start == range->start &&
		    info.tf_len == range->len &&
		    info.tf_minlen == range->minlen) {
			/* Avoid sending duplicated trim to a shared device */
			mlog(ML_NOTICE, "The same trim on device (%s) was "
			     "just done from node (%u), return.\n",
			     osb->dev_str, info.tf_nodenum);
			range->len = info.tf_trimlen;
			goto out;
		}
	}

	info.tf_nodenum = osb->node_num;
	info.tf_start = range->start;
	info.tf_len = range->len;
	info.tf_minlen = range->minlen;

	ret = ocfs2_trim_mainbm(sb, range);

	info.tf_trimlen = range->len;
	info.tf_success = (ret < 0 ? 0 : 1);
	pinfo = &info;
out:
	ocfs2_trim_fs_unlock(osb, pinfo);
	ocfs2_trim_fs_lock_res_uninit(osb);
	return ret;
}
