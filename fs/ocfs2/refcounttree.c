/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * refcounttree.c
 *
 * Copyright (C) 2009 Oracle.  All rights reserved.
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

#include <linux/sort.h>
#include <cluster/masklog.h>
#include "ocfs2.h"
#include "inode.h"
#include "alloc.h"
#include "suballoc.h"
#include "journal.h"
#include "uptodate.h"
#include "super.h"
#include "buffer_head_io.h"
#include "blockcheck.h"
#include "refcounttree.h"
#include "sysfile.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "aops.h"
#include "xattr.h"
#include "namei.h"
#include "ocfs2_trace.h"

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/swap.h>
#include <linux/security.h>
#include <linux/fsnotify.h>
#include <linux/quotaops.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/posix_acl.h>

struct ocfs2_cow_context {
	struct inode *inode;
	u32 cow_start;
	u32 cow_len;
	struct ocfs2_extent_tree data_et;
	struct ocfs2_refcount_tree *ref_tree;
	struct buffer_head *ref_root_bh;
	struct ocfs2_alloc_context *meta_ac;
	struct ocfs2_alloc_context *data_ac;
	struct ocfs2_cached_dealloc_ctxt dealloc;
	void *cow_object;
	struct ocfs2_post_refcount *post_refcount;
	int extra_credits;
	int (*get_clusters)(struct ocfs2_cow_context *context,
			    u32 v_cluster, u32 *p_cluster,
			    u32 *num_clusters,
			    unsigned int *extent_flags);
	int (*cow_duplicate_clusters)(handle_t *handle,
				      struct inode *inode,
				      u32 cpos, u32 old_cluster,
				      u32 new_cluster, u32 new_len);
};

static inline struct ocfs2_refcount_tree *
cache_info_to_refcount(struct ocfs2_caching_info *ci)
{
	return container_of(ci, struct ocfs2_refcount_tree, rf_ci);
}

static int ocfs2_validate_refcount_block(struct super_block *sb,
					 struct buffer_head *bh)
{
	int rc;
	struct ocfs2_refcount_block *rb =
		(struct ocfs2_refcount_block *)bh->b_data;

	trace_ocfs2_validate_refcount_block((unsigned long long)bh->b_blocknr);

	BUG_ON(!buffer_uptodate(bh));

	/*
	 * If the ecc fails, we return the error but otherwise
	 * leave the filesystem running.  We know any error is
	 * local to this block.
	 */
	rc = ocfs2_validate_meta_ecc(sb, bh->b_data, &rb->rf_check);
	if (rc) {
		mlog(ML_ERROR, "Checksum failed for refcount block %llu\n",
		     (unsigned long long)bh->b_blocknr);
		return rc;
	}


	if (!OCFS2_IS_VALID_REFCOUNT_BLOCK(rb)) {
		ocfs2_error(sb,
			    "Refcount block #%llu has bad signature %.*s",
			    (unsigned long long)bh->b_blocknr, 7,
			    rb->rf_signature);
		return -EINVAL;
	}

	if (le64_to_cpu(rb->rf_blkno) != bh->b_blocknr) {
		ocfs2_error(sb,
			    "Refcount block #%llu has an invalid rf_blkno "
			    "of %llu",
			    (unsigned long long)bh->b_blocknr,
			    (unsigned long long)le64_to_cpu(rb->rf_blkno));
		return -EINVAL;
	}

	if (le32_to_cpu(rb->rf_fs_generation) != OCFS2_SB(sb)->fs_generation) {
		ocfs2_error(sb,
			    "Refcount block #%llu has an invalid "
			    "rf_fs_generation of #%u",
			    (unsigned long long)bh->b_blocknr,
			    le32_to_cpu(rb->rf_fs_generation));
		return -EINVAL;
	}

	return 0;
}

static int ocfs2_read_refcount_block(struct ocfs2_caching_info *ci,
				     u64 rb_blkno,
				     struct buffer_head **bh)
{
	int rc;
	struct buffer_head *tmp = *bh;

	rc = ocfs2_read_block(ci, rb_blkno, &tmp,
			      ocfs2_validate_refcount_block);

	/* If ocfs2_read_block() got us a new bh, pass it up. */
	if (!rc && !*bh)
		*bh = tmp;

	return rc;
}

static u64 ocfs2_refcount_cache_owner(struct ocfs2_caching_info *ci)
{
	struct ocfs2_refcount_tree *rf = cache_info_to_refcount(ci);

	return rf->rf_blkno;
}

static struct super_block *
ocfs2_refcount_cache_get_super(struct ocfs2_caching_info *ci)
{
	struct ocfs2_refcount_tree *rf = cache_info_to_refcount(ci);

	return rf->rf_sb;
}

static void ocfs2_refcount_cache_lock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_refcount_tree *rf = cache_info_to_refcount(ci);

	spin_lock(&rf->rf_lock);
}

static void ocfs2_refcount_cache_unlock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_refcount_tree *rf = cache_info_to_refcount(ci);

	spin_unlock(&rf->rf_lock);
}

static void ocfs2_refcount_cache_io_lock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_refcount_tree *rf = cache_info_to_refcount(ci);

	mutex_lock(&rf->rf_io_mutex);
}

static void ocfs2_refcount_cache_io_unlock(struct ocfs2_caching_info *ci)
{
	struct ocfs2_refcount_tree *rf = cache_info_to_refcount(ci);

	mutex_unlock(&rf->rf_io_mutex);
}

static const struct ocfs2_caching_operations ocfs2_refcount_caching_ops = {
	.co_owner		= ocfs2_refcount_cache_owner,
	.co_get_super		= ocfs2_refcount_cache_get_super,
	.co_cache_lock		= ocfs2_refcount_cache_lock,
	.co_cache_unlock	= ocfs2_refcount_cache_unlock,
	.co_io_lock		= ocfs2_refcount_cache_io_lock,
	.co_io_unlock		= ocfs2_refcount_cache_io_unlock,
};

static struct ocfs2_refcount_tree *
ocfs2_find_refcount_tree(struct ocfs2_super *osb, u64 blkno)
{
	struct rb_node *n = osb->osb_rf_lock_tree.rb_node;
	struct ocfs2_refcount_tree *tree = NULL;

	while (n) {
		tree = rb_entry(n, struct ocfs2_refcount_tree, rf_node);

		if (blkno < tree->rf_blkno)
			n = n->rb_left;
		else if (blkno > tree->rf_blkno)
			n = n->rb_right;
		else
			return tree;
	}

	return NULL;
}

/* osb_lock is already locked. */
static void ocfs2_insert_refcount_tree(struct ocfs2_super *osb,
				       struct ocfs2_refcount_tree *new)
{
	u64 rf_blkno = new->rf_blkno;
	struct rb_node *parent = NULL;
	struct rb_node **p = &osb->osb_rf_lock_tree.rb_node;
	struct ocfs2_refcount_tree *tmp;

	while (*p) {
		parent = *p;

		tmp = rb_entry(parent, struct ocfs2_refcount_tree,
			       rf_node);

		if (rf_blkno < tmp->rf_blkno)
			p = &(*p)->rb_left;
		else if (rf_blkno > tmp->rf_blkno)
			p = &(*p)->rb_right;
		else {
			/* This should never happen! */
			mlog(ML_ERROR, "Duplicate refcount block %llu found!\n",
			     (unsigned long long)rf_blkno);
			BUG();
		}
	}

	rb_link_node(&new->rf_node, parent, p);
	rb_insert_color(&new->rf_node, &osb->osb_rf_lock_tree);
}

static void ocfs2_free_refcount_tree(struct ocfs2_refcount_tree *tree)
{
	ocfs2_metadata_cache_exit(&tree->rf_ci);
	ocfs2_simple_drop_lockres(OCFS2_SB(tree->rf_sb), &tree->rf_lockres);
	ocfs2_lock_res_free(&tree->rf_lockres);
	kfree(tree);
}

static inline void
ocfs2_erase_refcount_tree_from_list_no_lock(struct ocfs2_super *osb,
					struct ocfs2_refcount_tree *tree)
{
	rb_erase(&tree->rf_node, &osb->osb_rf_lock_tree);
	if (osb->osb_ref_tree_lru && osb->osb_ref_tree_lru == tree)
		osb->osb_ref_tree_lru = NULL;
}

static void ocfs2_erase_refcount_tree_from_list(struct ocfs2_super *osb,
					struct ocfs2_refcount_tree *tree)
{
	spin_lock(&osb->osb_lock);
	ocfs2_erase_refcount_tree_from_list_no_lock(osb, tree);
	spin_unlock(&osb->osb_lock);
}

static void ocfs2_kref_remove_refcount_tree(struct kref *kref)
{
	struct ocfs2_refcount_tree *tree =
		container_of(kref, struct ocfs2_refcount_tree, rf_getcnt);

	ocfs2_free_refcount_tree(tree);
}

static inline void
ocfs2_refcount_tree_get(struct ocfs2_refcount_tree *tree)
{
	kref_get(&tree->rf_getcnt);
}

static inline void
ocfs2_refcount_tree_put(struct ocfs2_refcount_tree *tree)
{
	kref_put(&tree->rf_getcnt, ocfs2_kref_remove_refcount_tree);
}

static inline void ocfs2_init_refcount_tree_ci(struct ocfs2_refcount_tree *new,
					       struct super_block *sb)
{
	ocfs2_metadata_cache_init(&new->rf_ci, &ocfs2_refcount_caching_ops);
	mutex_init(&new->rf_io_mutex);
	new->rf_sb = sb;
	spin_lock_init(&new->rf_lock);
}

static inline void ocfs2_init_refcount_tree_lock(struct ocfs2_super *osb,
					struct ocfs2_refcount_tree *new,
					u64 rf_blkno, u32 generation)
{
	init_rwsem(&new->rf_sem);
	ocfs2_refcount_lock_res_init(&new->rf_lockres, osb,
				     rf_blkno, generation);
}

static struct ocfs2_refcount_tree*
ocfs2_allocate_refcount_tree(struct ocfs2_super *osb, u64 rf_blkno)
{
	struct ocfs2_refcount_tree *new;

	new = kzalloc(sizeof(struct ocfs2_refcount_tree), GFP_NOFS);
	if (!new)
		return NULL;

	new->rf_blkno = rf_blkno;
	kref_init(&new->rf_getcnt);
	ocfs2_init_refcount_tree_ci(new, osb->sb);

	return new;
}

static int ocfs2_get_refcount_tree(struct ocfs2_super *osb, u64 rf_blkno,
				   struct ocfs2_refcount_tree **ret_tree)
{
	int ret = 0;
	struct ocfs2_refcount_tree *tree, *new = NULL;
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_refcount_block *ref_rb;

	spin_lock(&osb->osb_lock);
	if (osb->osb_ref_tree_lru &&
	    osb->osb_ref_tree_lru->rf_blkno == rf_blkno)
		tree = osb->osb_ref_tree_lru;
	else
		tree = ocfs2_find_refcount_tree(osb, rf_blkno);
	if (tree)
		goto out;

	spin_unlock(&osb->osb_lock);

	new = ocfs2_allocate_refcount_tree(osb, rf_blkno);
	if (!new) {
		ret = -ENOMEM;
		mlog_errno(ret);
		return ret;
	}
	/*
	 * We need the generation to create the refcount tree lock and since
	 * it isn't changed during the tree modification, we are safe here to
	 * read without protection.
	 * We also have to purge the cache after we create the lock since the
	 * refcount block may have the stale data. It can only be trusted when
	 * we hold the refcount lock.
	 */
	ret = ocfs2_read_refcount_block(&new->rf_ci, rf_blkno, &ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		ocfs2_metadata_cache_exit(&new->rf_ci);
		kfree(new);
		return ret;
	}

	ref_rb = (struct ocfs2_refcount_block *)ref_root_bh->b_data;
	new->rf_generation = le32_to_cpu(ref_rb->rf_generation);
	ocfs2_init_refcount_tree_lock(osb, new, rf_blkno,
				      new->rf_generation);
	ocfs2_metadata_cache_purge(&new->rf_ci);

	spin_lock(&osb->osb_lock);
	tree = ocfs2_find_refcount_tree(osb, rf_blkno);
	if (tree)
		goto out;

	ocfs2_insert_refcount_tree(osb, new);

	tree = new;
	new = NULL;

out:
	*ret_tree = tree;

	osb->osb_ref_tree_lru = tree;

	spin_unlock(&osb->osb_lock);

	if (new)
		ocfs2_free_refcount_tree(new);

	brelse(ref_root_bh);
	return ret;
}

static int ocfs2_get_refcount_block(struct inode *inode, u64 *ref_blkno)
{
	int ret;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;

	ret = ocfs2_read_inode_block(inode, &di_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	BUG_ON(!(OCFS2_I(inode)->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL));

	di = (struct ocfs2_dinode *)di_bh->b_data;
	*ref_blkno = le64_to_cpu(di->i_refcount_loc);
	brelse(di_bh);
out:
	return ret;
}

static int __ocfs2_lock_refcount_tree(struct ocfs2_super *osb,
				      struct ocfs2_refcount_tree *tree, int rw)
{
	int ret;

	ret = ocfs2_refcount_lock(tree, rw);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (rw)
		down_write(&tree->rf_sem);
	else
		down_read(&tree->rf_sem);

out:
	return ret;
}

/*
 * Lock the refcount tree pointed by ref_blkno and return the tree.
 * In most case, we lock the tree and read the refcount block.
 * So read it here if the caller really needs it.
 *
 * If the tree has been re-created by other node, it will free the
 * old one and re-create it.
 */
int ocfs2_lock_refcount_tree(struct ocfs2_super *osb,
			     u64 ref_blkno, int rw,
			     struct ocfs2_refcount_tree **ret_tree,
			     struct buffer_head **ref_bh)
{
	int ret, delete_tree = 0;
	struct ocfs2_refcount_tree *tree = NULL;
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_refcount_block *rb;

again:
	ret = ocfs2_get_refcount_tree(osb, ref_blkno, &tree);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	ocfs2_refcount_tree_get(tree);

	ret = __ocfs2_lock_refcount_tree(osb, tree, rw);
	if (ret) {
		mlog_errno(ret);
		ocfs2_refcount_tree_put(tree);
		goto out;
	}

	ret = ocfs2_read_refcount_block(&tree->rf_ci, tree->rf_blkno,
					&ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		ocfs2_unlock_refcount_tree(osb, tree, rw);
		ocfs2_refcount_tree_put(tree);
		goto out;
	}

	rb = (struct ocfs2_refcount_block *)ref_root_bh->b_data;
	/*
	 * If the refcount block has been freed and re-created, we may need
	 * to recreate the refcount tree also.
	 *
	 * Here we just remove the tree from the rb-tree, and the last
	 * kref holder will unlock and delete this refcount_tree.
	 * Then we goto "again" and ocfs2_get_refcount_tree will create
	 * the new refcount tree for us.
	 */
	if (tree->rf_generation != le32_to_cpu(rb->rf_generation)) {
		if (!tree->rf_removed) {
			ocfs2_erase_refcount_tree_from_list(osb, tree);
			tree->rf_removed = 1;
			delete_tree = 1;
		}

		ocfs2_unlock_refcount_tree(osb, tree, rw);
		/*
		 * We get an extra reference when we create the refcount
		 * tree, so another put will destroy it.
		 */
		if (delete_tree)
			ocfs2_refcount_tree_put(tree);
		brelse(ref_root_bh);
		ref_root_bh = NULL;
		goto again;
	}

	*ret_tree = tree;
	if (ref_bh) {
		*ref_bh = ref_root_bh;
		ref_root_bh = NULL;
	}
out:
	brelse(ref_root_bh);
	return ret;
}

void ocfs2_unlock_refcount_tree(struct ocfs2_super *osb,
				struct ocfs2_refcount_tree *tree, int rw)
{
	if (rw)
		up_write(&tree->rf_sem);
	else
		up_read(&tree->rf_sem);

	ocfs2_refcount_unlock(tree, rw);
	ocfs2_refcount_tree_put(tree);
}

void ocfs2_purge_refcount_trees(struct ocfs2_super *osb)
{
	struct rb_node *node;
	struct ocfs2_refcount_tree *tree;
	struct rb_root *root = &osb->osb_rf_lock_tree;

	while ((node = rb_last(root)) != NULL) {
		tree = rb_entry(node, struct ocfs2_refcount_tree, rf_node);

		trace_ocfs2_purge_refcount_trees(
				(unsigned long long) tree->rf_blkno);

		rb_erase(&tree->rf_node, root);
		ocfs2_free_refcount_tree(tree);
	}
}

/*
 * Create a refcount tree for an inode.
 * We take for granted that the inode is already locked.
 */
static int ocfs2_create_refcount_tree(struct inode *inode,
				      struct buffer_head *di_bh)
{
	int ret;
	handle_t *handle = NULL;
	struct ocfs2_alloc_context *meta_ac = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *new_bh = NULL;
	struct ocfs2_refcount_block *rb;
	struct ocfs2_refcount_tree *new_tree = NULL, *tree = NULL;
	u16 suballoc_bit_start;
	u32 num_got;
	u64 suballoc_loc, first_blkno;

	BUG_ON(oi->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL);

	trace_ocfs2_create_refcount_tree(
		(unsigned long long)OCFS2_I(inode)->ip_blkno);

	ret = ocfs2_reserve_new_metadata_blocks(osb, 1, &meta_ac);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	handle = ocfs2_start_trans(osb, OCFS2_REFCOUNT_TREE_CREATE_CREDITS);
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

	ret = ocfs2_claim_metadata(handle, meta_ac, 1, &suballoc_loc,
				   &suballoc_bit_start, &num_got,
				   &first_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	new_tree = ocfs2_allocate_refcount_tree(osb, first_blkno);
	if (!new_tree) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out_commit;
	}

	new_bh = sb_getblk(inode->i_sb, first_blkno);
	if (!new_bh) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out_commit;
	}
	ocfs2_set_new_buffer_uptodate(&new_tree->rf_ci, new_bh);

	ret = ocfs2_journal_access_rb(handle, &new_tree->rf_ci, new_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	/* Initialize ocfs2_refcount_block. */
	rb = (struct ocfs2_refcount_block *)new_bh->b_data;
	memset(rb, 0, inode->i_sb->s_blocksize);
	strcpy((void *)rb, OCFS2_REFCOUNT_BLOCK_SIGNATURE);
	rb->rf_suballoc_slot = cpu_to_le16(meta_ac->ac_alloc_slot);
	rb->rf_suballoc_loc = cpu_to_le64(suballoc_loc);
	rb->rf_suballoc_bit = cpu_to_le16(suballoc_bit_start);
	rb->rf_fs_generation = cpu_to_le32(osb->fs_generation);
	rb->rf_blkno = cpu_to_le64(first_blkno);
	rb->rf_count = cpu_to_le32(1);
	rb->rf_records.rl_count =
			cpu_to_le16(ocfs2_refcount_recs_per_rb(osb->sb));
	spin_lock(&osb->osb_lock);
	rb->rf_generation = osb->s_next_generation++;
	spin_unlock(&osb->osb_lock);

	ocfs2_journal_dirty(handle, new_bh);

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features |= OCFS2_HAS_REFCOUNT_FL;
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	di->i_refcount_loc = cpu_to_le64(first_blkno);
	spin_unlock(&oi->ip_lock);

	trace_ocfs2_create_refcount_tree_blkno((unsigned long long)first_blkno);

	ocfs2_journal_dirty(handle, di_bh);

	/*
	 * We have to init the tree lock here since it will use
	 * the generation number to create it.
	 */
	new_tree->rf_generation = le32_to_cpu(rb->rf_generation);
	ocfs2_init_refcount_tree_lock(osb, new_tree, first_blkno,
				      new_tree->rf_generation);

	spin_lock(&osb->osb_lock);
	tree = ocfs2_find_refcount_tree(osb, first_blkno);

	/*
	 * We've just created a new refcount tree in this block.  If
	 * we found a refcount tree on the ocfs2_super, it must be
	 * one we just deleted.  We free the old tree before
	 * inserting the new tree.
	 */
	BUG_ON(tree && tree->rf_generation == new_tree->rf_generation);
	if (tree)
		ocfs2_erase_refcount_tree_from_list_no_lock(osb, tree);
	ocfs2_insert_refcount_tree(osb, new_tree);
	spin_unlock(&osb->osb_lock);
	new_tree = NULL;
	if (tree)
		ocfs2_refcount_tree_put(tree);

out_commit:
	ocfs2_commit_trans(osb, handle);

out:
	if (new_tree) {
		ocfs2_metadata_cache_exit(&new_tree->rf_ci);
		kfree(new_tree);
	}

	brelse(new_bh);
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);

	return ret;
}

static int ocfs2_set_refcount_tree(struct inode *inode,
				   struct buffer_head *di_bh,
				   u64 refcount_loc)
{
	int ret;
	handle_t *handle = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_refcount_block *rb;
	struct ocfs2_refcount_tree *ref_tree;

	BUG_ON(oi->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL);

	ret = ocfs2_lock_refcount_tree(osb, refcount_loc, 1,
				       &ref_tree, &ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	handle = ocfs2_start_trans(osb, OCFS2_REFCOUNT_TREE_SET_CREDITS);
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

	ret = ocfs2_journal_access_rb(handle, &ref_tree->rf_ci, ref_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	rb = (struct ocfs2_refcount_block *)ref_root_bh->b_data;
	le32_add_cpu(&rb->rf_count, 1);

	ocfs2_journal_dirty(handle, ref_root_bh);

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features |= OCFS2_HAS_REFCOUNT_FL;
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	di->i_refcount_loc = cpu_to_le64(refcount_loc);
	spin_unlock(&oi->ip_lock);
	ocfs2_journal_dirty(handle, di_bh);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	ocfs2_unlock_refcount_tree(osb, ref_tree, 1);
	brelse(ref_root_bh);

	return ret;
}

int ocfs2_remove_refcount_tree(struct inode *inode, struct buffer_head *di_bh)
{
	int ret, delete_tree = 0;
	handle_t *handle = NULL;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_refcount_block *rb;
	struct inode *alloc_inode = NULL;
	struct buffer_head *alloc_bh = NULL;
	struct buffer_head *blk_bh = NULL;
	struct ocfs2_refcount_tree *ref_tree;
	int credits = OCFS2_REFCOUNT_TREE_REMOVE_CREDITS;
	u64 blk = 0, bg_blkno = 0, ref_blkno = le64_to_cpu(di->i_refcount_loc);
	u16 bit = 0;

	if (!(oi->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL))
		return 0;

	BUG_ON(!ref_blkno);
	ret = ocfs2_lock_refcount_tree(osb, ref_blkno, 1, &ref_tree, &blk_bh);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	rb = (struct ocfs2_refcount_block *)blk_bh->b_data;

	/*
	 * If we are the last user, we need to free the block.
	 * So lock the allocator ahead.
	 */
	if (le32_to_cpu(rb->rf_count) == 1) {
		blk = le64_to_cpu(rb->rf_blkno);
		bit = le16_to_cpu(rb->rf_suballoc_bit);
		if (rb->rf_suballoc_loc)
			bg_blkno = le64_to_cpu(rb->rf_suballoc_loc);
		else
			bg_blkno = ocfs2_which_suballoc_group(blk, bit);

		alloc_inode = ocfs2_get_system_file_inode(osb,
					EXTENT_ALLOC_SYSTEM_INODE,
					le16_to_cpu(rb->rf_suballoc_slot));
		if (!alloc_inode) {
			ret = -ENOMEM;
			mlog_errno(ret);
			goto out;
		}
		mutex_lock(&alloc_inode->i_mutex);

		ret = ocfs2_inode_lock(alloc_inode, &alloc_bh, 1);
		if (ret) {
			mlog_errno(ret);
			goto out_mutex;
		}

		credits += OCFS2_SUBALLOC_FREE;
	}

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out_unlock;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(inode), di_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_journal_access_rb(handle, &ref_tree->rf_ci, blk_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	spin_lock(&oi->ip_lock);
	oi->ip_dyn_features &= ~OCFS2_HAS_REFCOUNT_FL;
	di->i_dyn_features = cpu_to_le16(oi->ip_dyn_features);
	di->i_refcount_loc = 0;
	spin_unlock(&oi->ip_lock);
	ocfs2_journal_dirty(handle, di_bh);

	le32_add_cpu(&rb->rf_count , -1);
	ocfs2_journal_dirty(handle, blk_bh);

	if (!rb->rf_count) {
		delete_tree = 1;
		ocfs2_erase_refcount_tree_from_list(osb, ref_tree);
		ret = ocfs2_free_suballoc_bits(handle, alloc_inode,
					       alloc_bh, bit, bg_blkno, 1);
		if (ret)
			mlog_errno(ret);
	}

out_commit:
	ocfs2_commit_trans(osb, handle);
out_unlock:
	if (alloc_inode) {
		ocfs2_inode_unlock(alloc_inode, 1);
		brelse(alloc_bh);
	}
out_mutex:
	if (alloc_inode) {
		mutex_unlock(&alloc_inode->i_mutex);
		iput(alloc_inode);
	}
out:
	ocfs2_unlock_refcount_tree(osb, ref_tree, 1);
	if (delete_tree)
		ocfs2_refcount_tree_put(ref_tree);
	brelse(blk_bh);

	return ret;
}

static void ocfs2_find_refcount_rec_in_rl(struct ocfs2_caching_info *ci,
					  struct buffer_head *ref_leaf_bh,
					  u64 cpos, unsigned int len,
					  struct ocfs2_refcount_rec *ret_rec,
					  int *index)
{
	int i = 0;
	struct ocfs2_refcount_block *rb =
		(struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
	struct ocfs2_refcount_rec *rec = NULL;

	for (; i < le16_to_cpu(rb->rf_records.rl_used); i++) {
		rec = &rb->rf_records.rl_recs[i];

		if (le64_to_cpu(rec->r_cpos) +
		    le32_to_cpu(rec->r_clusters) <= cpos)
			continue;
		else if (le64_to_cpu(rec->r_cpos) > cpos)
			break;

		/* ok, cpos fail in this rec. Just return. */
		if (ret_rec)
			*ret_rec = *rec;
		goto out;
	}

	if (ret_rec) {
		/* We meet with a hole here, so fake the rec. */
		ret_rec->r_cpos = cpu_to_le64(cpos);
		ret_rec->r_refcount = 0;
		if (i < le16_to_cpu(rb->rf_records.rl_used) &&
		    le64_to_cpu(rec->r_cpos) < cpos + len)
			ret_rec->r_clusters =
				cpu_to_le32(le64_to_cpu(rec->r_cpos) - cpos);
		else
			ret_rec->r_clusters = cpu_to_le32(len);
	}

out:
	*index = i;
}

/*
 * Try to remove refcount tree. The mechanism is:
 * 1) Check whether i_clusters == 0, if no, exit.
 * 2) check whether we have i_xattr_loc in dinode. if yes, exit.
 * 3) Check whether we have inline xattr stored outside, if yes, exit.
 * 4) Remove the tree.
 */
int ocfs2_try_remove_refcount_tree(struct inode *inode,
				   struct buffer_head *di_bh)
{
	int ret;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;

	down_write(&oi->ip_xattr_sem);
	down_write(&oi->ip_alloc_sem);

	if (oi->ip_clusters)
		goto out;

	if ((oi->ip_dyn_features & OCFS2_HAS_XATTR_FL) && di->i_xattr_loc)
		goto out;

	if (oi->ip_dyn_features & OCFS2_INLINE_XATTR_FL &&
	    ocfs2_has_inline_xattr_value_outside(inode, di))
		goto out;

	ret = ocfs2_remove_refcount_tree(inode, di_bh);
	if (ret)
		mlog_errno(ret);
out:
	up_write(&oi->ip_alloc_sem);
	up_write(&oi->ip_xattr_sem);
	return 0;
}

/*
 * Find the end range for a leaf refcount block indicated by
 * el->l_recs[index].e_blkno.
 */
static int ocfs2_get_refcount_cpos_end(struct ocfs2_caching_info *ci,
				       struct buffer_head *ref_root_bh,
				       struct ocfs2_extent_block *eb,
				       struct ocfs2_extent_list *el,
				       int index,  u32 *cpos_end)
{
	int ret, i, subtree_root;
	u32 cpos;
	u64 blkno;
	struct super_block *sb = ocfs2_metadata_cache_get_super(ci);
	struct ocfs2_path *left_path = NULL, *right_path = NULL;
	struct ocfs2_extent_tree et;
	struct ocfs2_extent_list *tmp_el;

	if (index < le16_to_cpu(el->l_next_free_rec) - 1) {
		/*
		 * We have a extent rec after index, so just use the e_cpos
		 * of the next extent rec.
		 */
		*cpos_end = le32_to_cpu(el->l_recs[index+1].e_cpos);
		return 0;
	}

	if (!eb || (eb && !eb->h_next_leaf_blk)) {
		/*
		 * We are the last extent rec, so any high cpos should
		 * be stored in this leaf refcount block.
		 */
		*cpos_end = UINT_MAX;
		return 0;
	}

	/*
	 * If the extent block isn't the last one, we have to find
	 * the subtree root between this extent block and the next
	 * leaf extent block and get the corresponding e_cpos from
	 * the subroot. Otherwise we may corrupt the b-tree.
	 */
	ocfs2_init_refcount_extent_tree(&et, ci, ref_root_bh);

	left_path = ocfs2_new_path_from_et(&et);
	if (!left_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	cpos = le32_to_cpu(eb->h_list.l_recs[index].e_cpos);
	ret = ocfs2_find_path(ci, left_path, cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	right_path = ocfs2_new_path_from_path(left_path);
	if (!right_path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_cpos_for_right_leaf(sb, left_path, &cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(ci, right_path, cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	subtree_root = ocfs2_find_subtree_root(&et, left_path,
					       right_path);

	tmp_el = left_path->p_node[subtree_root].el;
	blkno = left_path->p_node[subtree_root+1].bh->b_blocknr;
	for (i = 0; i < le16_to_cpu(tmp_el->l_next_free_rec); i++) {
		if (le64_to_cpu(tmp_el->l_recs[i].e_blkno) == blkno) {
			*cpos_end = le32_to_cpu(tmp_el->l_recs[i+1].e_cpos);
			break;
		}
	}

	BUG_ON(i == le16_to_cpu(tmp_el->l_next_free_rec));

out:
	ocfs2_free_path(left_path);
	ocfs2_free_path(right_path);
	return ret;
}

/*
 * Given a cpos and len, try to find the refcount record which contains cpos.
 * 1. If cpos can be found in one refcount record, return the record.
 * 2. If cpos can't be found, return a fake record which start from cpos
 *    and end at a small value between cpos+len and start of the next record.
 *    This fake record has r_refcount = 0.
 */
static int ocfs2_get_refcount_rec(struct ocfs2_caching_info *ci,
				  struct buffer_head *ref_root_bh,
				  u64 cpos, unsigned int len,
				  struct ocfs2_refcount_rec *ret_rec,
				  int *index,
				  struct buffer_head **ret_bh)
{
	int ret = 0, i, found;
	u32 low_cpos, uninitialized_var(cpos_end);
	struct ocfs2_extent_list *el;
	struct ocfs2_extent_rec *rec = NULL;
	struct ocfs2_extent_block *eb = NULL;
	struct buffer_head *eb_bh = NULL, *ref_leaf_bh = NULL;
	struct super_block *sb = ocfs2_metadata_cache_get_super(ci);
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_root_bh->b_data;

	if (!(le32_to_cpu(rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL)) {
		ocfs2_find_refcount_rec_in_rl(ci, ref_root_bh, cpos, len,
					      ret_rec, index);
		*ret_bh = ref_root_bh;
		get_bh(ref_root_bh);
		return 0;
	}

	el = &rb->rf_list;
	low_cpos = cpos & OCFS2_32BIT_POS_MASK;

	if (el->l_tree_depth) {
		ret = ocfs2_find_leaf(ci, el, low_cpos, &eb_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		eb = (struct ocfs2_extent_block *) eb_bh->b_data;
		el = &eb->h_list;

		if (el->l_tree_depth) {
			ocfs2_error(sb,
			"refcount tree %llu has non zero tree "
			"depth in leaf btree tree block %llu\n",
			(unsigned long long)ocfs2_metadata_cache_owner(ci),
			(unsigned long long)eb_bh->b_blocknr);
			ret = -EROFS;
			goto out;
		}
	}

	found = 0;
	for (i = le16_to_cpu(el->l_next_free_rec) - 1; i >= 0; i--) {
		rec = &el->l_recs[i];

		if (le32_to_cpu(rec->e_cpos) <= low_cpos) {
			found = 1;
			break;
		}
	}

	if (found) {
		ret = ocfs2_get_refcount_cpos_end(ci, ref_root_bh,
						  eb, el, i, &cpos_end);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (cpos_end < low_cpos + len)
			len = cpos_end - low_cpos;
	}

	ret = ocfs2_read_refcount_block(ci, le64_to_cpu(rec->e_blkno),
					&ref_leaf_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_find_refcount_rec_in_rl(ci, ref_leaf_bh, cpos, len,
				      ret_rec, index);
	*ret_bh = ref_leaf_bh;
out:
	brelse(eb_bh);
	return ret;
}

enum ocfs2_ref_rec_contig {
	REF_CONTIG_NONE = 0,
	REF_CONTIG_LEFT,
	REF_CONTIG_RIGHT,
	REF_CONTIG_LEFTRIGHT,
};

static enum ocfs2_ref_rec_contig
	ocfs2_refcount_rec_adjacent(struct ocfs2_refcount_block *rb,
				    int index)
{
	if ((rb->rf_records.rl_recs[index].r_refcount ==
	    rb->rf_records.rl_recs[index + 1].r_refcount) &&
	    (le64_to_cpu(rb->rf_records.rl_recs[index].r_cpos) +
	    le32_to_cpu(rb->rf_records.rl_recs[index].r_clusters) ==
	    le64_to_cpu(rb->rf_records.rl_recs[index + 1].r_cpos)))
		return REF_CONTIG_RIGHT;

	return REF_CONTIG_NONE;
}

static enum ocfs2_ref_rec_contig
	ocfs2_refcount_rec_contig(struct ocfs2_refcount_block *rb,
				  int index)
{
	enum ocfs2_ref_rec_contig ret = REF_CONTIG_NONE;

	if (index < le16_to_cpu(rb->rf_records.rl_used) - 1)
		ret = ocfs2_refcount_rec_adjacent(rb, index);

	if (index > 0) {
		enum ocfs2_ref_rec_contig tmp;

		tmp = ocfs2_refcount_rec_adjacent(rb, index - 1);

		if (tmp == REF_CONTIG_RIGHT) {
			if (ret == REF_CONTIG_RIGHT)
				ret = REF_CONTIG_LEFTRIGHT;
			else
				ret = REF_CONTIG_LEFT;
		}
	}

	return ret;
}

static void ocfs2_rotate_refcount_rec_left(struct ocfs2_refcount_block *rb,
					   int index)
{
	BUG_ON(rb->rf_records.rl_recs[index].r_refcount !=
	       rb->rf_records.rl_recs[index+1].r_refcount);

	le32_add_cpu(&rb->rf_records.rl_recs[index].r_clusters,
		     le32_to_cpu(rb->rf_records.rl_recs[index+1].r_clusters));

	if (index < le16_to_cpu(rb->rf_records.rl_used) - 2)
		memmove(&rb->rf_records.rl_recs[index + 1],
			&rb->rf_records.rl_recs[index + 2],
			sizeof(struct ocfs2_refcount_rec) *
			(le16_to_cpu(rb->rf_records.rl_used) - index - 2));

	memset(&rb->rf_records.rl_recs[le16_to_cpu(rb->rf_records.rl_used) - 1],
	       0, sizeof(struct ocfs2_refcount_rec));
	le16_add_cpu(&rb->rf_records.rl_used, -1);
}

/*
 * Merge the refcount rec if we are contiguous with the adjacent recs.
 */
static void ocfs2_refcount_rec_merge(struct ocfs2_refcount_block *rb,
				     int index)
{
	enum ocfs2_ref_rec_contig contig =
				ocfs2_refcount_rec_contig(rb, index);

	if (contig == REF_CONTIG_NONE)
		return;

	if (contig == REF_CONTIG_LEFT || contig == REF_CONTIG_LEFTRIGHT) {
		BUG_ON(index == 0);
		index--;
	}

	ocfs2_rotate_refcount_rec_left(rb, index);

	if (contig == REF_CONTIG_LEFTRIGHT)
		ocfs2_rotate_refcount_rec_left(rb, index);
}

/*
 * Change the refcount indexed by "index" in ref_bh.
 * If refcount reaches 0, remove it.
 */
static int ocfs2_change_refcount_rec(handle_t *handle,
				     struct ocfs2_caching_info *ci,
				     struct buffer_head *ref_leaf_bh,
				     int index, int merge, int change)
{
	int ret;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
	struct ocfs2_refcount_list *rl = &rb->rf_records;
	struct ocfs2_refcount_rec *rec = &rl->rl_recs[index];

	ret = ocfs2_journal_access_rb(handle, ci, ref_leaf_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	trace_ocfs2_change_refcount_rec(
		(unsigned long long)ocfs2_metadata_cache_owner(ci),
		index, le32_to_cpu(rec->r_refcount), change);
	le32_add_cpu(&rec->r_refcount, change);

	if (!rec->r_refcount) {
		if (index != le16_to_cpu(rl->rl_used) - 1) {
			memmove(rec, rec + 1,
				(le16_to_cpu(rl->rl_used) - index - 1) *
				sizeof(struct ocfs2_refcount_rec));
			memset(&rl->rl_recs[le16_to_cpu(rl->rl_used) - 1],
			       0, sizeof(struct ocfs2_refcount_rec));
		}

		le16_add_cpu(&rl->rl_used, -1);
	} else if (merge)
		ocfs2_refcount_rec_merge(rb, index);

	ocfs2_journal_dirty(handle, ref_leaf_bh);
out:
	return ret;
}

static int ocfs2_expand_inline_ref_root(handle_t *handle,
					struct ocfs2_caching_info *ci,
					struct buffer_head *ref_root_bh,
					struct buffer_head **ref_leaf_bh,
					struct ocfs2_alloc_context *meta_ac)
{
	int ret;
	u16 suballoc_bit_start;
	u32 num_got;
	u64 suballoc_loc, blkno;
	struct super_block *sb = ocfs2_metadata_cache_get_super(ci);
	struct buffer_head *new_bh = NULL;
	struct ocfs2_refcount_block *new_rb;
	struct ocfs2_refcount_block *root_rb =
			(struct ocfs2_refcount_block *)ref_root_bh->b_data;

	ret = ocfs2_journal_access_rb(handle, ci, ref_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_claim_metadata(handle, meta_ac, 1, &suballoc_loc,
				   &suballoc_bit_start, &num_got,
				   &blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	new_bh = sb_getblk(sb, blkno);
	if (new_bh == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}
	ocfs2_set_new_buffer_uptodate(ci, new_bh);

	ret = ocfs2_journal_access_rb(handle, ci, new_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * Initialize ocfs2_refcount_block.
	 * It should contain the same information as the old root.
	 * so just memcpy it and change the corresponding field.
	 */
	memcpy(new_bh->b_data, ref_root_bh->b_data, sb->s_blocksize);

	new_rb = (struct ocfs2_refcount_block *)new_bh->b_data;
	new_rb->rf_suballoc_slot = cpu_to_le16(meta_ac->ac_alloc_slot);
	new_rb->rf_suballoc_loc = cpu_to_le64(suballoc_loc);
	new_rb->rf_suballoc_bit = cpu_to_le16(suballoc_bit_start);
	new_rb->rf_blkno = cpu_to_le64(blkno);
	new_rb->rf_cpos = cpu_to_le32(0);
	new_rb->rf_parent = cpu_to_le64(ref_root_bh->b_blocknr);
	new_rb->rf_flags = cpu_to_le32(OCFS2_REFCOUNT_LEAF_FL);
	ocfs2_journal_dirty(handle, new_bh);

	/* Now change the root. */
	memset(&root_rb->rf_list, 0, sb->s_blocksize -
	       offsetof(struct ocfs2_refcount_block, rf_list));
	root_rb->rf_list.l_count = cpu_to_le16(ocfs2_extent_recs_per_rb(sb));
	root_rb->rf_clusters = cpu_to_le32(1);
	root_rb->rf_list.l_next_free_rec = cpu_to_le16(1);
	root_rb->rf_list.l_recs[0].e_blkno = cpu_to_le64(blkno);
	root_rb->rf_list.l_recs[0].e_leaf_clusters = cpu_to_le16(1);
	root_rb->rf_flags = cpu_to_le32(OCFS2_REFCOUNT_TREE_FL);

	ocfs2_journal_dirty(handle, ref_root_bh);

	trace_ocfs2_expand_inline_ref_root((unsigned long long)blkno,
		le16_to_cpu(new_rb->rf_records.rl_used));

	*ref_leaf_bh = new_bh;
	new_bh = NULL;
out:
	brelse(new_bh);
	return ret;
}

static int ocfs2_refcount_rec_no_intersect(struct ocfs2_refcount_rec *prev,
					   struct ocfs2_refcount_rec *next)
{
	if (ocfs2_get_ref_rec_low_cpos(prev) + le32_to_cpu(prev->r_clusters) <=
		ocfs2_get_ref_rec_low_cpos(next))
		return 1;

	return 0;
}

static int cmp_refcount_rec_by_low_cpos(const void *a, const void *b)
{
	const struct ocfs2_refcount_rec *l = a, *r = b;
	u32 l_cpos = ocfs2_get_ref_rec_low_cpos(l);
	u32 r_cpos = ocfs2_get_ref_rec_low_cpos(r);

	if (l_cpos > r_cpos)
		return 1;
	if (l_cpos < r_cpos)
		return -1;
	return 0;
}

static int cmp_refcount_rec_by_cpos(const void *a, const void *b)
{
	const struct ocfs2_refcount_rec *l = a, *r = b;
	u64 l_cpos = le64_to_cpu(l->r_cpos);
	u64 r_cpos = le64_to_cpu(r->r_cpos);

	if (l_cpos > r_cpos)
		return 1;
	if (l_cpos < r_cpos)
		return -1;
	return 0;
}

static void swap_refcount_rec(void *a, void *b, int size)
{
	struct ocfs2_refcount_rec *l = a, *r = b, tmp;

	tmp = *l;
	*l = *r;
	*r = tmp;
}

/*
 * The refcount cpos are ordered by their 64bit cpos,
 * But we will use the low 32 bit to be the e_cpos in the b-tree.
 * So we need to make sure that this pos isn't intersected with others.
 *
 * Note: The refcount block is already sorted by their low 32 bit cpos,
 *       So just try the middle pos first, and we will exit when we find
 *       the good position.
 */
static int ocfs2_find_refcount_split_pos(struct ocfs2_refcount_list *rl,
					 u32 *split_pos, int *split_index)
{
	int num_used = le16_to_cpu(rl->rl_used);
	int delta, middle = num_used / 2;

	for (delta = 0; delta < middle; delta++) {
		/* Let's check delta earlier than middle */
		if (ocfs2_refcount_rec_no_intersect(
					&rl->rl_recs[middle - delta - 1],
					&rl->rl_recs[middle - delta])) {
			*split_index = middle - delta;
			break;
		}

		/* For even counts, don't walk off the end */
		if ((middle + delta + 1) == num_used)
			continue;

		/* Now try delta past middle */
		if (ocfs2_refcount_rec_no_intersect(
					&rl->rl_recs[middle + delta],
					&rl->rl_recs[middle + delta + 1])) {
			*split_index = middle + delta + 1;
			break;
		}
	}

	if (delta >= middle)
		return -ENOSPC;

	*split_pos = ocfs2_get_ref_rec_low_cpos(&rl->rl_recs[*split_index]);
	return 0;
}

static int ocfs2_divide_leaf_refcount_block(struct buffer_head *ref_leaf_bh,
					    struct buffer_head *new_bh,
					    u32 *split_cpos)
{
	int split_index = 0, num_moved, ret;
	u32 cpos = 0;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
	struct ocfs2_refcount_list *rl = &rb->rf_records;
	struct ocfs2_refcount_block *new_rb =
			(struct ocfs2_refcount_block *)new_bh->b_data;
	struct ocfs2_refcount_list *new_rl = &new_rb->rf_records;

	trace_ocfs2_divide_leaf_refcount_block(
		(unsigned long long)ref_leaf_bh->b_blocknr,
		le16_to_cpu(rl->rl_count), le16_to_cpu(rl->rl_used));

	/*
	 * XXX: Improvement later.
	 * If we know all the high 32 bit cpos is the same, no need to sort.
	 *
	 * In order to make the whole process safe, we do:
	 * 1. sort the entries by their low 32 bit cpos first so that we can
	 *    find the split cpos easily.
	 * 2. call ocfs2_insert_extent to insert the new refcount block.
	 * 3. move the refcount rec to the new block.
	 * 4. sort the entries by their 64 bit cpos.
	 * 5. dirty the new_rb and rb.
	 */
	sort(&rl->rl_recs, le16_to_cpu(rl->rl_used),
	     sizeof(struct ocfs2_refcount_rec),
	     cmp_refcount_rec_by_low_cpos, swap_refcount_rec);

	ret = ocfs2_find_refcount_split_pos(rl, &cpos, &split_index);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	new_rb->rf_cpos = cpu_to_le32(cpos);

	/* move refcount records starting from split_index to the new block. */
	num_moved = le16_to_cpu(rl->rl_used) - split_index;
	memcpy(new_rl->rl_recs, &rl->rl_recs[split_index],
	       num_moved * sizeof(struct ocfs2_refcount_rec));

	/*ok, remove the entries we just moved over to the other block. */
	memset(&rl->rl_recs[split_index], 0,
	       num_moved * sizeof(struct ocfs2_refcount_rec));

	/* change old and new rl_used accordingly. */
	le16_add_cpu(&rl->rl_used, -num_moved);
	new_rl->rl_used = cpu_to_le16(num_moved);

	sort(&rl->rl_recs, le16_to_cpu(rl->rl_used),
	     sizeof(struct ocfs2_refcount_rec),
	     cmp_refcount_rec_by_cpos, swap_refcount_rec);

	sort(&new_rl->rl_recs, le16_to_cpu(new_rl->rl_used),
	     sizeof(struct ocfs2_refcount_rec),
	     cmp_refcount_rec_by_cpos, swap_refcount_rec);

	*split_cpos = cpos;
	return 0;
}

static int ocfs2_new_leaf_refcount_block(handle_t *handle,
					 struct ocfs2_caching_info *ci,
					 struct buffer_head *ref_root_bh,
					 struct buffer_head *ref_leaf_bh,
					 struct ocfs2_alloc_context *meta_ac)
{
	int ret;
	u16 suballoc_bit_start;
	u32 num_got, new_cpos;
	u64 suballoc_loc, blkno;
	struct super_block *sb = ocfs2_metadata_cache_get_super(ci);
	struct ocfs2_refcount_block *root_rb =
			(struct ocfs2_refcount_block *)ref_root_bh->b_data;
	struct buffer_head *new_bh = NULL;
	struct ocfs2_refcount_block *new_rb;
	struct ocfs2_extent_tree ref_et;

	BUG_ON(!(le32_to_cpu(root_rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL));

	ret = ocfs2_journal_access_rb(handle, ci, ref_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_rb(handle, ci, ref_leaf_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_claim_metadata(handle, meta_ac, 1, &suballoc_loc,
				   &suballoc_bit_start, &num_got,
				   &blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	new_bh = sb_getblk(sb, blkno);
	if (new_bh == NULL) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}
	ocfs2_set_new_buffer_uptodate(ci, new_bh);

	ret = ocfs2_journal_access_rb(handle, ci, new_bh,
				      OCFS2_JOURNAL_ACCESS_CREATE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* Initialize ocfs2_refcount_block. */
	new_rb = (struct ocfs2_refcount_block *)new_bh->b_data;
	memset(new_rb, 0, sb->s_blocksize);
	strcpy((void *)new_rb, OCFS2_REFCOUNT_BLOCK_SIGNATURE);
	new_rb->rf_suballoc_slot = cpu_to_le16(meta_ac->ac_alloc_slot);
	new_rb->rf_suballoc_loc = cpu_to_le64(suballoc_loc);
	new_rb->rf_suballoc_bit = cpu_to_le16(suballoc_bit_start);
	new_rb->rf_fs_generation = cpu_to_le32(OCFS2_SB(sb)->fs_generation);
	new_rb->rf_blkno = cpu_to_le64(blkno);
	new_rb->rf_parent = cpu_to_le64(ref_root_bh->b_blocknr);
	new_rb->rf_flags = cpu_to_le32(OCFS2_REFCOUNT_LEAF_FL);
	new_rb->rf_records.rl_count =
				cpu_to_le16(ocfs2_refcount_recs_per_rb(sb));
	new_rb->rf_generation = root_rb->rf_generation;

	ret = ocfs2_divide_leaf_refcount_block(ref_leaf_bh, new_bh, &new_cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_journal_dirty(handle, ref_leaf_bh);
	ocfs2_journal_dirty(handle, new_bh);

	ocfs2_init_refcount_extent_tree(&ref_et, ci, ref_root_bh);

	trace_ocfs2_new_leaf_refcount_block(
			(unsigned long long)new_bh->b_blocknr, new_cpos);

	/* Insert the new leaf block with the specific offset cpos. */
	ret = ocfs2_insert_extent(handle, &ref_et, new_cpos, new_bh->b_blocknr,
				  1, 0, meta_ac);
	if (ret)
		mlog_errno(ret);

out:
	brelse(new_bh);
	return ret;
}

static int ocfs2_expand_refcount_tree(handle_t *handle,
				      struct ocfs2_caching_info *ci,
				      struct buffer_head *ref_root_bh,
				      struct buffer_head *ref_leaf_bh,
				      struct ocfs2_alloc_context *meta_ac)
{
	int ret;
	struct buffer_head *expand_bh = NULL;

	if (ref_root_bh == ref_leaf_bh) {
		/*
		 * the old root bh hasn't been expanded to a b-tree,
		 * so expand it first.
		 */
		ret = ocfs2_expand_inline_ref_root(handle, ci, ref_root_bh,
						   &expand_bh, meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	} else {
		expand_bh = ref_leaf_bh;
		get_bh(expand_bh);
	}


	/* Now add a new refcount block into the tree.*/
	ret = ocfs2_new_leaf_refcount_block(handle, ci, ref_root_bh,
					    expand_bh, meta_ac);
	if (ret)
		mlog_errno(ret);
out:
	brelse(expand_bh);
	return ret;
}

/*
 * Adjust the extent rec in b-tree representing ref_leaf_bh.
 *
 * Only called when we have inserted a new refcount rec at index 0
 * which means ocfs2_extent_rec.e_cpos may need some change.
 */
static int ocfs2_adjust_refcount_rec(handle_t *handle,
				     struct ocfs2_caching_info *ci,
				     struct buffer_head *ref_root_bh,
				     struct buffer_head *ref_leaf_bh,
				     struct ocfs2_refcount_rec *rec)
{
	int ret = 0, i;
	u32 new_cpos, old_cpos;
	struct ocfs2_path *path = NULL;
	struct ocfs2_extent_tree et;
	struct ocfs2_refcount_block *rb =
		(struct ocfs2_refcount_block *)ref_root_bh->b_data;
	struct ocfs2_extent_list *el;

	if (!(le32_to_cpu(rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL))
		goto out;

	rb = (struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
	old_cpos = le32_to_cpu(rb->rf_cpos);
	new_cpos = le64_to_cpu(rec->r_cpos) & OCFS2_32BIT_POS_MASK;
	if (old_cpos <= new_cpos)
		goto out;

	ocfs2_init_refcount_extent_tree(&et, ci, ref_root_bh);

	path = ocfs2_new_path_from_et(&et);
	if (!path) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_find_path(ci, path, old_cpos);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * 2 more credits, one for the leaf refcount block, one for
	 * the extent block contains the extent rec.
	 */
	ret = ocfs2_extend_trans(handle, 2);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_rb(handle, ci, ref_leaf_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_eb(handle, ci, path_leaf_bh(path),
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret < 0) {
		mlog_errno(ret);
		goto out;
	}

	/* change the leaf extent block first. */
	el = path_leaf_el(path);

	for (i = 0; i < le16_to_cpu(el->l_next_free_rec); i++)
		if (le32_to_cpu(el->l_recs[i].e_cpos) == old_cpos)
			break;

	BUG_ON(i == le16_to_cpu(el->l_next_free_rec));

	el->l_recs[i].e_cpos = cpu_to_le32(new_cpos);

	/* change the r_cpos in the leaf block. */
	rb->rf_cpos = cpu_to_le32(new_cpos);

	ocfs2_journal_dirty(handle, path_leaf_bh(path));
	ocfs2_journal_dirty(handle, ref_leaf_bh);

out:
	ocfs2_free_path(path);
	return ret;
}

static int ocfs2_insert_refcount_rec(handle_t *handle,
				     struct ocfs2_caching_info *ci,
				     struct buffer_head *ref_root_bh,
				     struct buffer_head *ref_leaf_bh,
				     struct ocfs2_refcount_rec *rec,
				     int index, int merge,
				     struct ocfs2_alloc_context *meta_ac)
{
	int ret;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
	struct ocfs2_refcount_list *rf_list = &rb->rf_records;
	struct buffer_head *new_bh = NULL;

	BUG_ON(le32_to_cpu(rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL);

	if (rf_list->rl_used == rf_list->rl_count) {
		u64 cpos = le64_to_cpu(rec->r_cpos);
		u32 len = le32_to_cpu(rec->r_clusters);

		ret = ocfs2_expand_refcount_tree(handle, ci, ref_root_bh,
						 ref_leaf_bh, meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ret = ocfs2_get_refcount_rec(ci, ref_root_bh,
					     cpos, len, NULL, &index,
					     &new_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ref_leaf_bh = new_bh;
		rb = (struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
		rf_list = &rb->rf_records;
	}

	ret = ocfs2_journal_access_rb(handle, ci, ref_leaf_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (index < le16_to_cpu(rf_list->rl_used))
		memmove(&rf_list->rl_recs[index + 1],
			&rf_list->rl_recs[index],
			(le16_to_cpu(rf_list->rl_used) - index) *
			 sizeof(struct ocfs2_refcount_rec));

	trace_ocfs2_insert_refcount_rec(
		(unsigned long long)ref_leaf_bh->b_blocknr, index,
		(unsigned long long)le64_to_cpu(rec->r_cpos),
		le32_to_cpu(rec->r_clusters), le32_to_cpu(rec->r_refcount));

	rf_list->rl_recs[index] = *rec;

	le16_add_cpu(&rf_list->rl_used, 1);

	if (merge)
		ocfs2_refcount_rec_merge(rb, index);

	ocfs2_journal_dirty(handle, ref_leaf_bh);

	if (index == 0) {
		ret = ocfs2_adjust_refcount_rec(handle, ci,
						ref_root_bh,
						ref_leaf_bh, rec);
		if (ret)
			mlog_errno(ret);
	}
out:
	brelse(new_bh);
	return ret;
}

/*
 * Split the refcount_rec indexed by "index" in ref_leaf_bh.
 * This is much simple than our b-tree code.
 * split_rec is the new refcount rec we want to insert.
 * If split_rec->r_refcount > 0, we are changing the refcount(in case we
 * increase refcount or decrease a refcount to non-zero).
 * If split_rec->r_refcount == 0, we are punching a hole in current refcount
 * rec( in case we decrease a refcount to zero).
 */
static int ocfs2_split_refcount_rec(handle_t *handle,
				    struct ocfs2_caching_info *ci,
				    struct buffer_head *ref_root_bh,
				    struct buffer_head *ref_leaf_bh,
				    struct ocfs2_refcount_rec *split_rec,
				    int index, int merge,
				    struct ocfs2_alloc_context *meta_ac,
				    struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, recs_need;
	u32 len;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
	struct ocfs2_refcount_list *rf_list = &rb->rf_records;
	struct ocfs2_refcount_rec *orig_rec = &rf_list->rl_recs[index];
	struct ocfs2_refcount_rec *tail_rec = NULL;
	struct buffer_head *new_bh = NULL;

	BUG_ON(le32_to_cpu(rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL);

	trace_ocfs2_split_refcount_rec(le64_to_cpu(orig_rec->r_cpos),
		le32_to_cpu(orig_rec->r_clusters),
		le32_to_cpu(orig_rec->r_refcount),
		le64_to_cpu(split_rec->r_cpos),
		le32_to_cpu(split_rec->r_clusters),
		le32_to_cpu(split_rec->r_refcount));

	/*
	 * If we just need to split the header or tail clusters,
	 * no more recs are needed, just split is OK.
	 * Otherwise we at least need one new recs.
	 */
	if (!split_rec->r_refcount &&
	    (split_rec->r_cpos == orig_rec->r_cpos ||
	     le64_to_cpu(split_rec->r_cpos) +
	     le32_to_cpu(split_rec->r_clusters) ==
	     le64_to_cpu(orig_rec->r_cpos) + le32_to_cpu(orig_rec->r_clusters)))
		recs_need = 0;
	else
		recs_need = 1;

	/*
	 * We need one more rec if we split in the middle and the new rec have
	 * some refcount in it.
	 */
	if (split_rec->r_refcount &&
	    (split_rec->r_cpos != orig_rec->r_cpos &&
	     le64_to_cpu(split_rec->r_cpos) +
	     le32_to_cpu(split_rec->r_clusters) !=
	     le64_to_cpu(orig_rec->r_cpos) + le32_to_cpu(orig_rec->r_clusters)))
		recs_need++;

	/* If the leaf block don't have enough record, expand it. */
	if (le16_to_cpu(rf_list->rl_used) + recs_need >
					 le16_to_cpu(rf_list->rl_count)) {
		struct ocfs2_refcount_rec tmp_rec;
		u64 cpos = le64_to_cpu(orig_rec->r_cpos);
		len = le32_to_cpu(orig_rec->r_clusters);
		ret = ocfs2_expand_refcount_tree(handle, ci, ref_root_bh,
						 ref_leaf_bh, meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		/*
		 * We have to re-get it since now cpos may be moved to
		 * another leaf block.
		 */
		ret = ocfs2_get_refcount_rec(ci, ref_root_bh,
					     cpos, len, &tmp_rec, &index,
					     &new_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		ref_leaf_bh = new_bh;
		rb = (struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
		rf_list = &rb->rf_records;
		orig_rec = &rf_list->rl_recs[index];
	}

	ret = ocfs2_journal_access_rb(handle, ci, ref_leaf_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/*
	 * We have calculated out how many new records we need and store
	 * in recs_need, so spare enough space first by moving the records
	 * after "index" to the end.
	 */
	if (index != le16_to_cpu(rf_list->rl_used) - 1)
		memmove(&rf_list->rl_recs[index + 1 + recs_need],
			&rf_list->rl_recs[index + 1],
			(le16_to_cpu(rf_list->rl_used) - index - 1) *
			 sizeof(struct ocfs2_refcount_rec));

	len = (le64_to_cpu(orig_rec->r_cpos) +
	      le32_to_cpu(orig_rec->r_clusters)) -
	      (le64_to_cpu(split_rec->r_cpos) +
	      le32_to_cpu(split_rec->r_clusters));

	/*
	 * If we have "len", the we will split in the tail and move it
	 * to the end of the space we have just spared.
	 */
	if (len) {
		tail_rec = &rf_list->rl_recs[index + recs_need];

		memcpy(tail_rec, orig_rec, sizeof(struct ocfs2_refcount_rec));
		le64_add_cpu(&tail_rec->r_cpos,
			     le32_to_cpu(tail_rec->r_clusters) - len);
		tail_rec->r_clusters = cpu_to_le32(len);
	}

	/*
	 * If the split pos isn't the same as the original one, we need to
	 * split in the head.
	 *
	 * Note: We have the chance that split_rec.r_refcount = 0,
	 * recs_need = 0 and len > 0, which means we just cut the head from
	 * the orig_rec and in that case we have done some modification in
	 * orig_rec above, so the check for r_cpos is faked.
	 */
	if (split_rec->r_cpos != orig_rec->r_cpos && tail_rec != orig_rec) {
		len = le64_to_cpu(split_rec->r_cpos) -
		      le64_to_cpu(orig_rec->r_cpos);
		orig_rec->r_clusters = cpu_to_le32(len);
		index++;
	}

	le16_add_cpu(&rf_list->rl_used, recs_need);

	if (split_rec->r_refcount) {
		rf_list->rl_recs[index] = *split_rec;
		trace_ocfs2_split_refcount_rec_insert(
			(unsigned long long)ref_leaf_bh->b_blocknr, index,
			(unsigned long long)le64_to_cpu(split_rec->r_cpos),
			le32_to_cpu(split_rec->r_clusters),
			le32_to_cpu(split_rec->r_refcount));

		if (merge)
			ocfs2_refcount_rec_merge(rb, index);
	}

	ocfs2_journal_dirty(handle, ref_leaf_bh);

out:
	brelse(new_bh);
	return ret;
}

static int __ocfs2_increase_refcount(handle_t *handle,
				     struct ocfs2_caching_info *ci,
				     struct buffer_head *ref_root_bh,
				     u64 cpos, u32 len, int merge,
				     struct ocfs2_alloc_context *meta_ac,
				     struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret = 0, index;
	struct buffer_head *ref_leaf_bh = NULL;
	struct ocfs2_refcount_rec rec;
	unsigned int set_len = 0;

	trace_ocfs2_increase_refcount_begin(
	     (unsigned long long)ocfs2_metadata_cache_owner(ci),
	     (unsigned long long)cpos, len);

	while (len) {
		ret = ocfs2_get_refcount_rec(ci, ref_root_bh,
					     cpos, len, &rec, &index,
					     &ref_leaf_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		set_len = le32_to_cpu(rec.r_clusters);

		/*
		 * Here we may meet with 3 situations:
		 *
		 * 1. If we find an already existing record, and the length
		 *    is the same, cool, we just need to increase the r_refcount
		 *    and it is OK.
		 * 2. If we find a hole, just insert it with r_refcount = 1.
		 * 3. If we are in the middle of one extent record, split
		 *    it.
		 */
		if (rec.r_refcount && le64_to_cpu(rec.r_cpos) == cpos &&
		    set_len <= len) {
			trace_ocfs2_increase_refcount_change(
				(unsigned long long)cpos, set_len,
				le32_to_cpu(rec.r_refcount));
			ret = ocfs2_change_refcount_rec(handle, ci,
							ref_leaf_bh, index,
							merge, 1);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		} else if (!rec.r_refcount) {
			rec.r_refcount = cpu_to_le32(1);

			trace_ocfs2_increase_refcount_insert(
			     (unsigned long long)le64_to_cpu(rec.r_cpos),
			     set_len);
			ret = ocfs2_insert_refcount_rec(handle, ci, ref_root_bh,
							ref_leaf_bh,
							&rec, index,
							merge, meta_ac);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		} else  {
			set_len = min((u64)(cpos + len),
				      le64_to_cpu(rec.r_cpos) + set_len) - cpos;
			rec.r_cpos = cpu_to_le64(cpos);
			rec.r_clusters = cpu_to_le32(set_len);
			le32_add_cpu(&rec.r_refcount, 1);

			trace_ocfs2_increase_refcount_split(
			     (unsigned long long)le64_to_cpu(rec.r_cpos),
			     set_len, le32_to_cpu(rec.r_refcount));
			ret = ocfs2_split_refcount_rec(handle, ci,
						       ref_root_bh, ref_leaf_bh,
						       &rec, index, merge,
						       meta_ac, dealloc);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}

		cpos += set_len;
		len -= set_len;
		brelse(ref_leaf_bh);
		ref_leaf_bh = NULL;
	}

out:
	brelse(ref_leaf_bh);
	return ret;
}

static int ocfs2_remove_refcount_extent(handle_t *handle,
				struct ocfs2_caching_info *ci,
				struct buffer_head *ref_root_bh,
				struct buffer_head *ref_leaf_bh,
				struct ocfs2_alloc_context *meta_ac,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret;
	struct super_block *sb = ocfs2_metadata_cache_get_super(ci);
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
	struct ocfs2_extent_tree et;

	BUG_ON(rb->rf_records.rl_used);

	trace_ocfs2_remove_refcount_extent(
		(unsigned long long)ocfs2_metadata_cache_owner(ci),
		(unsigned long long)ref_leaf_bh->b_blocknr,
		le32_to_cpu(rb->rf_cpos));

	ocfs2_init_refcount_extent_tree(&et, ci, ref_root_bh);
	ret = ocfs2_remove_extent(handle, &et, le32_to_cpu(rb->rf_cpos),
				  1, meta_ac, dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ocfs2_remove_from_cache(ci, ref_leaf_bh);

	/*
	 * add the freed block to the dealloc so that it will be freed
	 * when we run dealloc.
	 */
	ret = ocfs2_cache_block_dealloc(dealloc, EXTENT_ALLOC_SYSTEM_INODE,
					le16_to_cpu(rb->rf_suballoc_slot),
					le64_to_cpu(rb->rf_suballoc_loc),
					le64_to_cpu(rb->rf_blkno),
					le16_to_cpu(rb->rf_suballoc_bit));
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_rb(handle, ci, ref_root_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	rb = (struct ocfs2_refcount_block *)ref_root_bh->b_data;

	le32_add_cpu(&rb->rf_clusters, -1);

	/*
	 * check whether we need to restore the root refcount block if
	 * there is no leaf extent block at atll.
	 */
	if (!rb->rf_list.l_next_free_rec) {
		BUG_ON(rb->rf_clusters);

		trace_ocfs2_restore_refcount_block(
		     (unsigned long long)ref_root_bh->b_blocknr);

		rb->rf_flags = 0;
		rb->rf_parent = 0;
		rb->rf_cpos = 0;
		memset(&rb->rf_records, 0, sb->s_blocksize -
		       offsetof(struct ocfs2_refcount_block, rf_records));
		rb->rf_records.rl_count =
				cpu_to_le16(ocfs2_refcount_recs_per_rb(sb));
	}

	ocfs2_journal_dirty(handle, ref_root_bh);

out:
	return ret;
}

int ocfs2_increase_refcount(handle_t *handle,
			    struct ocfs2_caching_info *ci,
			    struct buffer_head *ref_root_bh,
			    u64 cpos, u32 len,
			    struct ocfs2_alloc_context *meta_ac,
			    struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	return __ocfs2_increase_refcount(handle, ci, ref_root_bh,
					 cpos, len, 1,
					 meta_ac, dealloc);
}

static int ocfs2_decrease_refcount_rec(handle_t *handle,
				struct ocfs2_caching_info *ci,
				struct buffer_head *ref_root_bh,
				struct buffer_head *ref_leaf_bh,
				int index, u64 cpos, unsigned int len,
				struct ocfs2_alloc_context *meta_ac,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret;
	struct ocfs2_refcount_block *rb =
			(struct ocfs2_refcount_block *)ref_leaf_bh->b_data;
	struct ocfs2_refcount_rec *rec = &rb->rf_records.rl_recs[index];

	BUG_ON(cpos < le64_to_cpu(rec->r_cpos));
	BUG_ON(cpos + len >
	       le64_to_cpu(rec->r_cpos) + le32_to_cpu(rec->r_clusters));

	trace_ocfs2_decrease_refcount_rec(
		(unsigned long long)ocfs2_metadata_cache_owner(ci),
		(unsigned long long)cpos, len);

	if (cpos == le64_to_cpu(rec->r_cpos) &&
	    len == le32_to_cpu(rec->r_clusters))
		ret = ocfs2_change_refcount_rec(handle, ci,
						ref_leaf_bh, index, 1, -1);
	else {
		struct ocfs2_refcount_rec split = *rec;
		split.r_cpos = cpu_to_le64(cpos);
		split.r_clusters = cpu_to_le32(len);

		le32_add_cpu(&split.r_refcount, -1);

		ret = ocfs2_split_refcount_rec(handle, ci,
					       ref_root_bh, ref_leaf_bh,
					       &split, index, 1,
					       meta_ac, dealloc);
	}

	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	/* Remove the leaf refcount block if it contains no refcount record. */
	if (!rb->rf_records.rl_used && ref_leaf_bh != ref_root_bh) {
		ret = ocfs2_remove_refcount_extent(handle, ci, ref_root_bh,
						   ref_leaf_bh, meta_ac,
						   dealloc);
		if (ret)
			mlog_errno(ret);
	}

out:
	return ret;
}

static int __ocfs2_decrease_refcount(handle_t *handle,
				     struct ocfs2_caching_info *ci,
				     struct buffer_head *ref_root_bh,
				     u64 cpos, u32 len,
				     struct ocfs2_alloc_context *meta_ac,
				     struct ocfs2_cached_dealloc_ctxt *dealloc,
				     int delete)
{
	int ret = 0, index = 0;
	struct ocfs2_refcount_rec rec;
	unsigned int r_count = 0, r_len;
	struct super_block *sb = ocfs2_metadata_cache_get_super(ci);
	struct buffer_head *ref_leaf_bh = NULL;

	trace_ocfs2_decrease_refcount(
		(unsigned long long)ocfs2_metadata_cache_owner(ci),
		(unsigned long long)cpos, len, delete);

	while (len) {
		ret = ocfs2_get_refcount_rec(ci, ref_root_bh,
					     cpos, len, &rec, &index,
					     &ref_leaf_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		r_count = le32_to_cpu(rec.r_refcount);
		BUG_ON(r_count == 0);
		if (!delete)
			BUG_ON(r_count > 1);

		r_len = min((u64)(cpos + len), le64_to_cpu(rec.r_cpos) +
			      le32_to_cpu(rec.r_clusters)) - cpos;

		ret = ocfs2_decrease_refcount_rec(handle, ci, ref_root_bh,
						  ref_leaf_bh, index,
						  cpos, r_len,
						  meta_ac, dealloc);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (le32_to_cpu(rec.r_refcount) == 1 && delete) {
			ret = ocfs2_cache_cluster_dealloc(dealloc,
					  ocfs2_clusters_to_blocks(sb, cpos),
							  r_len);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}

		cpos += r_len;
		len -= r_len;
		brelse(ref_leaf_bh);
		ref_leaf_bh = NULL;
	}

out:
	brelse(ref_leaf_bh);
	return ret;
}

/* Caller must hold refcount tree lock. */
int ocfs2_decrease_refcount(struct inode *inode,
			    handle_t *handle, u32 cpos, u32 len,
			    struct ocfs2_alloc_context *meta_ac,
			    struct ocfs2_cached_dealloc_ctxt *dealloc,
			    int delete)
{
	int ret;
	u64 ref_blkno;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_refcount_tree *tree;

	BUG_ON(!(oi->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL));

	ret = ocfs2_get_refcount_block(inode, &ref_blkno);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_get_refcount_tree(OCFS2_SB(inode->i_sb), ref_blkno, &tree);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_refcount_block(&tree->rf_ci, tree->rf_blkno,
					&ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = __ocfs2_decrease_refcount(handle, &tree->rf_ci, ref_root_bh,
					cpos, len, meta_ac, dealloc, delete);
	if (ret)
		mlog_errno(ret);
out:
	brelse(ref_root_bh);
	return ret;
}

/*
 * Mark the already-existing extent at cpos as refcounted for len clusters.
 * This adds the refcount extent flag.
 *
 * If the existing extent is larger than the request, initiate a
 * split. An attempt will be made at merging with adjacent extents.
 *
 * The caller is responsible for passing down meta_ac if we'll need it.
 */
static int ocfs2_mark_extent_refcounted(struct inode *inode,
				struct ocfs2_extent_tree *et,
				handle_t *handle, u32 cpos,
				u32 len, u32 phys,
				struct ocfs2_alloc_context *meta_ac,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret;

	trace_ocfs2_mark_extent_refcounted(OCFS2_I(inode)->ip_blkno,
					   cpos, len, phys);

	if (!ocfs2_refcount_tree(OCFS2_SB(inode->i_sb))) {
		ocfs2_error(inode->i_sb, "Inode %lu want to use refcount "
			    "tree, but the feature bit is not set in the "
			    "super block.", inode->i_ino);
		ret = -EROFS;
		goto out;
	}

	ret = ocfs2_change_extent_flag(handle, et, cpos,
				       len, phys, meta_ac, dealloc,
				       OCFS2_EXT_REFCOUNTED, 0);
	if (ret)
		mlog_errno(ret);

out:
	return ret;
}

/*
 * Given some contiguous physical clusters, calculate what we need
 * for modifying their refcount.
 */
static int ocfs2_calc_refcount_meta_credits(struct super_block *sb,
					    struct ocfs2_caching_info *ci,
					    struct buffer_head *ref_root_bh,
					    u64 start_cpos,
					    u32 clusters,
					    int *meta_add,
					    int *credits)
{
	int ret = 0, index, ref_blocks = 0, recs_add = 0;
	u64 cpos = start_cpos;
	struct ocfs2_refcount_block *rb;
	struct ocfs2_refcount_rec rec;
	struct buffer_head *ref_leaf_bh = NULL, *prev_bh = NULL;
	u32 len;

	while (clusters) {
		ret = ocfs2_get_refcount_rec(ci, ref_root_bh,
					     cpos, clusters, &rec,
					     &index, &ref_leaf_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		if (ref_leaf_bh != prev_bh) {
			/*
			 * Now we encounter a new leaf block, so calculate
			 * whether we need to extend the old leaf.
			 */
			if (prev_bh) {
				rb = (struct ocfs2_refcount_block *)
							prev_bh->b_data;

				if (le16_to_cpu(rb->rf_records.rl_used) +
				    recs_add >
				    le16_to_cpu(rb->rf_records.rl_count))
					ref_blocks++;
			}

			recs_add = 0;
			*credits += 1;
			brelse(prev_bh);
			prev_bh = ref_leaf_bh;
			get_bh(prev_bh);
		}

		rb = (struct ocfs2_refcount_block *)ref_leaf_bh->b_data;

		trace_ocfs2_calc_refcount_meta_credits_iterate(
				recs_add, (unsigned long long)cpos, clusters,
				(unsigned long long)le64_to_cpu(rec.r_cpos),
				le32_to_cpu(rec.r_clusters),
				le32_to_cpu(rec.r_refcount), index);

		len = min((u64)cpos + clusters, le64_to_cpu(rec.r_cpos) +
			  le32_to_cpu(rec.r_clusters)) - cpos;
		/*
		 * We record all the records which will be inserted to the
		 * same refcount block, so that we can tell exactly whether
		 * we need a new refcount block or not.
		 *
		 * If we will insert a new one, this is easy and only happens
		 * during adding refcounted flag to the extent, so we don't
		 * have a chance of spliting. We just need one record.
		 *
		 * If the refcount rec already exists, that would be a little
		 * complicated. we may have to:
		 * 1) split at the beginning if the start pos isn't aligned.
		 *    we need 1 more record in this case.
		 * 2) split int the end if the end pos isn't aligned.
		 *    we need 1 more record in this case.
		 * 3) split in the middle because of file system fragmentation.
		 *    we need 2 more records in this case(we can't detect this
		 *    beforehand, so always think of the worst case).
		 */
		if (rec.r_refcount) {
			recs_add += 2;
			/* Check whether we need a split at the beginning. */
			if (cpos == start_cpos &&
			    cpos != le64_to_cpu(rec.r_cpos))
				recs_add++;

			/* Check whether we need a split in the end. */
			if (cpos + clusters < le64_to_cpu(rec.r_cpos) +
			    le32_to_cpu(rec.r_clusters))
				recs_add++;
		} else
			recs_add++;

		brelse(ref_leaf_bh);
		ref_leaf_bh = NULL;
		clusters -= len;
		cpos += len;
	}

	if (prev_bh) {
		rb = (struct ocfs2_refcount_block *)prev_bh->b_data;

		if (le16_to_cpu(rb->rf_records.rl_used) + recs_add >
		    le16_to_cpu(rb->rf_records.rl_count))
			ref_blocks++;

		*credits += 1;
	}

	if (!ref_blocks)
		goto out;

	*meta_add += ref_blocks;
	*credits += ref_blocks;

	/*
	 * So we may need ref_blocks to insert into the tree.
	 * That also means we need to change the b-tree and add that number
	 * of records since we never merge them.
	 * We need one more block for expansion since the new created leaf
	 * block is also full and needs split.
	 */
	rb = (struct ocfs2_refcount_block *)ref_root_bh->b_data;
	if (le32_to_cpu(rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL) {
		struct ocfs2_extent_tree et;

		ocfs2_init_refcount_extent_tree(&et, ci, ref_root_bh);
		*meta_add += ocfs2_extend_meta_needed(et.et_root_el);
		*credits += ocfs2_calc_extend_credits(sb,
						      et.et_root_el);
	} else {
		*credits += OCFS2_EXPAND_REFCOUNT_TREE_CREDITS;
		*meta_add += 1;
	}

out:

	trace_ocfs2_calc_refcount_meta_credits(
		(unsigned long long)start_cpos, clusters,
		*meta_add, *credits);
	brelse(ref_leaf_bh);
	brelse(prev_bh);
	return ret;
}

/*
 * For refcount tree, we will decrease some contiguous clusters
 * refcount count, so just go through it to see how many blocks
 * we gonna touch and whether we need to create new blocks.
 *
 * Normally the refcount blocks store these refcount should be
 * contiguous also, so that we can get the number easily.
 * We will at most add split 2 refcount records and 2 more
 * refcount blocks, so just check it in a rough way.
 *
 * Caller must hold refcount tree lock.
 */
int ocfs2_prepare_refcount_change_for_del(struct inode *inode,
					  u64 refcount_loc,
					  u64 phys_blkno,
					  u32 clusters,
					  int *credits,
					  int *ref_blocks)
{
	int ret;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_refcount_tree *tree;
	u64 start_cpos = ocfs2_blocks_to_clusters(inode->i_sb, phys_blkno);

	if (!ocfs2_refcount_tree(OCFS2_SB(inode->i_sb))) {
		ocfs2_error(inode->i_sb, "Inode %lu want to use refcount "
			    "tree, but the feature bit is not set in the "
			    "super block.", inode->i_ino);
		ret = -EROFS;
		goto out;
	}

	BUG_ON(!(oi->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL));

	ret = ocfs2_get_refcount_tree(OCFS2_SB(inode->i_sb),
				      refcount_loc, &tree);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_read_refcount_block(&tree->rf_ci, refcount_loc,
					&ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_calc_refcount_meta_credits(inode->i_sb,
					       &tree->rf_ci,
					       ref_root_bh,
					       start_cpos, clusters,
					       ref_blocks, credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	trace_ocfs2_prepare_refcount_change_for_del(*ref_blocks, *credits);

out:
	brelse(ref_root_bh);
	return ret;
}

#define	MAX_CONTIG_BYTES	1048576

static inline unsigned int ocfs2_cow_contig_clusters(struct super_block *sb)
{
	return ocfs2_clusters_for_bytes(sb, MAX_CONTIG_BYTES);
}

static inline unsigned int ocfs2_cow_contig_mask(struct super_block *sb)
{
	return ~(ocfs2_cow_contig_clusters(sb) - 1);
}

/*
 * Given an extent that starts at 'start' and an I/O that starts at 'cpos',
 * find an offset (start + (n * contig_clusters)) that is closest to cpos
 * while still being less than or equal to it.
 *
 * The goal is to break the extent at a multiple of contig_clusters.
 */
static inline unsigned int ocfs2_cow_align_start(struct super_block *sb,
						 unsigned int start,
						 unsigned int cpos)
{
	BUG_ON(start > cpos);

	return start + ((cpos - start) & ocfs2_cow_contig_mask(sb));
}

/*
 * Given a cluster count of len, pad it out so that it is a multiple
 * of contig_clusters.
 */
static inline unsigned int ocfs2_cow_align_length(struct super_block *sb,
						  unsigned int len)
{
	unsigned int padded =
		(len + (ocfs2_cow_contig_clusters(sb) - 1)) &
		ocfs2_cow_contig_mask(sb);

	/* Did we wrap? */
	if (padded < len)
		padded = UINT_MAX;

	return padded;
}

/*
 * Calculate out the start and number of virtual clusters we need to to CoW.
 *
 * cpos is vitual start cluster position we want to do CoW in a
 * file and write_len is the cluster length.
 * max_cpos is the place where we want to stop CoW intentionally.
 *
 * Normal we will start CoW from the beginning of extent record cotaining cpos.
 * We try to break up extents on boundaries of MAX_CONTIG_BYTES so that we
 * get good I/O from the resulting extent tree.
 */
static int ocfs2_refcount_cal_cow_clusters(struct inode *inode,
					   struct ocfs2_extent_list *el,
					   u32 cpos,
					   u32 write_len,
					   u32 max_cpos,
					   u32 *cow_start,
					   u32 *cow_len)
{
	int ret = 0;
	int tree_height = le16_to_cpu(el->l_tree_depth), i;
	struct buffer_head *eb_bh = NULL;
	struct ocfs2_extent_block *eb = NULL;
	struct ocfs2_extent_rec *rec;
	unsigned int want_clusters, rec_end = 0;
	int contig_clusters = ocfs2_cow_contig_clusters(inode->i_sb);
	int leaf_clusters;

	BUG_ON(cpos + write_len > max_cpos);

	if (tree_height > 0) {
		ret = ocfs2_find_leaf(INODE_CACHE(inode), el, cpos, &eb_bh);
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

	*cow_len = 0;
	for (i = 0; i < le16_to_cpu(el->l_next_free_rec); i++) {
		rec = &el->l_recs[i];

		if (ocfs2_is_empty_extent(rec)) {
			mlog_bug_on_msg(i != 0, "Inode %lu has empty record in "
					"index %d\n", inode->i_ino, i);
			continue;
		}

		if (le32_to_cpu(rec->e_cpos) +
		    le16_to_cpu(rec->e_leaf_clusters) <= cpos)
			continue;

		if (*cow_len == 0) {
			/*
			 * We should find a refcounted record in the
			 * first pass.
			 */
			BUG_ON(!(rec->e_flags & OCFS2_EXT_REFCOUNTED));
			*cow_start = le32_to_cpu(rec->e_cpos);
		}

		/*
		 * If we encounter a hole, a non-refcounted record or
		 * pass the max_cpos, stop the search.
		 */
		if ((!(rec->e_flags & OCFS2_EXT_REFCOUNTED)) ||
		    (*cow_len && rec_end != le32_to_cpu(rec->e_cpos)) ||
		    (max_cpos <= le32_to_cpu(rec->e_cpos)))
			break;

		leaf_clusters = le16_to_cpu(rec->e_leaf_clusters);
		rec_end = le32_to_cpu(rec->e_cpos) + leaf_clusters;
		if (rec_end > max_cpos) {
			rec_end = max_cpos;
			leaf_clusters = rec_end - le32_to_cpu(rec->e_cpos);
		}

		/*
		 * How many clusters do we actually need from
		 * this extent?  First we see how many we actually
		 * need to complete the write.  If that's smaller
		 * than contig_clusters, we try for contig_clusters.
		 */
		if (!*cow_len)
			want_clusters = write_len;
		else
			want_clusters = (cpos + write_len) -
				(*cow_start + *cow_len);
		if (want_clusters < contig_clusters)
			want_clusters = contig_clusters;

		/*
		 * If the write does not cover the whole extent, we
		 * need to calculate how we're going to split the extent.
		 * We try to do it on contig_clusters boundaries.
		 *
		 * Any extent smaller than contig_clusters will be
		 * CoWed in its entirety.
		 */
		if (leaf_clusters <= contig_clusters)
			*cow_len += leaf_clusters;
		else if (*cow_len || (*cow_start == cpos)) {
			/*
			 * This extent needs to be CoW'd from its
			 * beginning, so all we have to do is compute
			 * how many clusters to grab.  We align
			 * want_clusters to the edge of contig_clusters
			 * to get better I/O.
			 */
			want_clusters = ocfs2_cow_align_length(inode->i_sb,
							       want_clusters);

			if (leaf_clusters < want_clusters)
				*cow_len += leaf_clusters;
			else
				*cow_len += want_clusters;
		} else if ((*cow_start + contig_clusters) >=
			   (cpos + write_len)) {
			/*
			 * Breaking off contig_clusters at the front
			 * of the extent will cover our write.  That's
			 * easy.
			 */
			*cow_len = contig_clusters;
		} else if ((rec_end - cpos) <= contig_clusters) {
			/*
			 * Breaking off contig_clusters at the tail of
			 * this extent will cover cpos.
			 */
			*cow_start = rec_end - contig_clusters;
			*cow_len = contig_clusters;
		} else if ((rec_end - cpos) <= want_clusters) {
			/*
			 * While we can't fit the entire write in this
			 * extent, we know that the write goes from cpos
			 * to the end of the extent.  Break that off.
			 * We try to break it at some multiple of
			 * contig_clusters from the front of the extent.
			 * Failing that (ie, cpos is within
			 * contig_clusters of the front), we'll CoW the
			 * entire extent.
			 */
			*cow_start = ocfs2_cow_align_start(inode->i_sb,
							   *cow_start, cpos);
			*cow_len = rec_end - *cow_start;
		} else {
			/*
			 * Ok, the entire write lives in the middle of
			 * this extent.  Let's try to slice the extent up
			 * nicely.  Optimally, our CoW region starts at
			 * m*contig_clusters from the beginning of the
			 * extent and goes for n*contig_clusters,
			 * covering the entire write.
			 */
			*cow_start = ocfs2_cow_align_start(inode->i_sb,
							   *cow_start, cpos);

			want_clusters = (cpos + write_len) - *cow_start;
			want_clusters = ocfs2_cow_align_length(inode->i_sb,
							       want_clusters);
			if (*cow_start + want_clusters <= rec_end)
				*cow_len = want_clusters;
			else
				*cow_len = rec_end - *cow_start;
		}

		/* Have we covered our entire write yet? */
		if ((*cow_start + *cow_len) >= (cpos + write_len))
			break;

		/*
		 * If we reach the end of the extent block and don't get enough
		 * clusters, continue with the next extent block if possible.
		 */
		if (i + 1 == le16_to_cpu(el->l_next_free_rec) &&
		    eb && eb->h_next_leaf_blk) {
			brelse(eb_bh);
			eb_bh = NULL;

			ret = ocfs2_read_extent_block(INODE_CACHE(inode),
					       le64_to_cpu(eb->h_next_leaf_blk),
					       &eb_bh);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			eb = (struct ocfs2_extent_block *) eb_bh->b_data;
			el = &eb->h_list;
			i = -1;
		}
	}

out:
	brelse(eb_bh);
	return ret;
}

/*
 * Prepare meta_ac, data_ac and calculate credits when we want to add some
 * num_clusters in data_tree "et" and change the refcount for the old
 * clusters(starting form p_cluster) in the refcount tree.
 *
 * Note:
 * 1. since we may split the old tree, so we at most will need num_clusters + 2
 *    more new leaf records.
 * 2. In some case, we may not need to reserve new clusters(e.g, reflink), so
 *    just give data_ac = NULL.
 */
static int ocfs2_lock_refcount_allocators(struct super_block *sb,
					u32 p_cluster, u32 num_clusters,
					struct ocfs2_extent_tree *et,
					struct ocfs2_caching_info *ref_ci,
					struct buffer_head *ref_root_bh,
					struct ocfs2_alloc_context **meta_ac,
					struct ocfs2_alloc_context **data_ac,
					int *credits)
{
	int ret = 0, meta_add = 0;
	int num_free_extents = ocfs2_num_free_extents(OCFS2_SB(sb), et);

	if (num_free_extents < 0) {
		ret = num_free_extents;
		mlog_errno(ret);
		goto out;
	}

	if (num_free_extents < num_clusters + 2)
		meta_add =
			ocfs2_extend_meta_needed(et->et_root_el);

	*credits += ocfs2_calc_extend_credits(sb, et->et_root_el);

	ret = ocfs2_calc_refcount_meta_credits(sb, ref_ci, ref_root_bh,
					       p_cluster, num_clusters,
					       &meta_add, credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	trace_ocfs2_lock_refcount_allocators(meta_add, *credits);
	ret = ocfs2_reserve_new_metadata_blocks(OCFS2_SB(sb), meta_add,
						meta_ac);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (data_ac) {
		ret = ocfs2_reserve_clusters(OCFS2_SB(sb), num_clusters,
					     data_ac);
		if (ret)
			mlog_errno(ret);
	}

out:
	if (ret) {
		if (*meta_ac) {
			ocfs2_free_alloc_context(*meta_ac);
			*meta_ac = NULL;
		}
	}

	return ret;
}

static int ocfs2_clear_cow_buffer(handle_t *handle, struct buffer_head *bh)
{
	BUG_ON(buffer_dirty(bh));

	clear_buffer_mapped(bh);

	return 0;
}

int ocfs2_duplicate_clusters_by_page(handle_t *handle,
				     struct inode *inode,
				     u32 cpos, u32 old_cluster,
				     u32 new_cluster, u32 new_len)
{
	int ret = 0, partial;
	struct super_block *sb = inode->i_sb;
	u64 new_block = ocfs2_clusters_to_blocks(sb, new_cluster);
	struct page *page;
	pgoff_t page_index;
	unsigned int from, to, readahead_pages;
	loff_t offset, end, map_end;
	struct address_space *mapping = inode->i_mapping;

	trace_ocfs2_duplicate_clusters_by_page(cpos, old_cluster,
					       new_cluster, new_len);

	readahead_pages =
		(ocfs2_cow_contig_clusters(sb) <<
		 OCFS2_SB(sb)->s_clustersize_bits) >> PAGE_CACHE_SHIFT;
	offset = ((loff_t)cpos) << OCFS2_SB(sb)->s_clustersize_bits;
	end = offset + (new_len << OCFS2_SB(sb)->s_clustersize_bits);
	/*
	 * We only duplicate pages until we reach the page contains i_size - 1.
	 * So trim 'end' to i_size.
	 */
	if (end > i_size_read(inode))
		end = i_size_read(inode);

	while (offset < end) {
		page_index = offset >> PAGE_CACHE_SHIFT;
		map_end = ((loff_t)page_index + 1) << PAGE_CACHE_SHIFT;
		if (map_end > end)
			map_end = end;

		/* from, to is the offset within the page. */
		from = offset & (PAGE_CACHE_SIZE - 1);
		to = PAGE_CACHE_SIZE;
		if (map_end & (PAGE_CACHE_SIZE - 1))
			to = map_end & (PAGE_CACHE_SIZE - 1);

		page = find_or_create_page(mapping, page_index, GFP_NOFS);
		if (!page) {
			ret = -ENOMEM;
			mlog_errno(ret);
			break;
		}

		/*
		 * In case PAGE_CACHE_SIZE <= CLUSTER_SIZE, This page
		 * can't be dirtied before we CoW it out.
		 */
		if (PAGE_CACHE_SIZE <= OCFS2_SB(sb)->s_clustersize)
			BUG_ON(PageDirty(page));

		if (!PageUptodate(page)) {
			ret = block_read_full_page(page, ocfs2_get_block);
			if (ret) {
				mlog_errno(ret);
				goto unlock;
			}
			lock_page(page);
		}

		if (page_has_buffers(page)) {
			ret = walk_page_buffers(handle, page_buffers(page),
						from, to, &partial,
						ocfs2_clear_cow_buffer);
			if (ret) {
				mlog_errno(ret);
				goto unlock;
			}
		}

		ocfs2_map_and_dirty_page(inode,
					 handle, from, to,
					 page, 0, &new_block);
		mark_page_accessed(page);
unlock:
		unlock_page(page);
		page_cache_release(page);
		page = NULL;
		offset = map_end;
		if (ret)
			break;
	}

	return ret;
}

int ocfs2_duplicate_clusters_by_jbd(handle_t *handle,
				    struct inode *inode,
				    u32 cpos, u32 old_cluster,
				    u32 new_cluster, u32 new_len)
{
	int ret = 0;
	struct super_block *sb = inode->i_sb;
	struct ocfs2_caching_info *ci = INODE_CACHE(inode);
	int i, blocks = ocfs2_clusters_to_blocks(sb, new_len);
	u64 old_block = ocfs2_clusters_to_blocks(sb, old_cluster);
	u64 new_block = ocfs2_clusters_to_blocks(sb, new_cluster);
	struct ocfs2_super *osb = OCFS2_SB(sb);
	struct buffer_head *old_bh = NULL;
	struct buffer_head *new_bh = NULL;

	trace_ocfs2_duplicate_clusters_by_page(cpos, old_cluster,
					       new_cluster, new_len);

	for (i = 0; i < blocks; i++, old_block++, new_block++) {
		new_bh = sb_getblk(osb->sb, new_block);
		if (new_bh == NULL) {
			ret = -ENOMEM;
			mlog_errno(ret);
			break;
		}

		ocfs2_set_new_buffer_uptodate(ci, new_bh);

		ret = ocfs2_read_block(ci, old_block, &old_bh, NULL);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		ret = ocfs2_journal_access(handle, ci, new_bh,
					   OCFS2_JOURNAL_ACCESS_CREATE);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		memcpy(new_bh->b_data, old_bh->b_data, sb->s_blocksize);
		ocfs2_journal_dirty(handle, new_bh);

		brelse(new_bh);
		brelse(old_bh);
		new_bh = NULL;
		old_bh = NULL;
	}

	brelse(new_bh);
	brelse(old_bh);
	return ret;
}

static int ocfs2_clear_ext_refcount(handle_t *handle,
				    struct ocfs2_extent_tree *et,
				    u32 cpos, u32 p_cluster, u32 len,
				    unsigned int ext_flags,
				    struct ocfs2_alloc_context *meta_ac,
				    struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret, index;
	struct ocfs2_extent_rec replace_rec;
	struct ocfs2_path *path = NULL;
	struct ocfs2_extent_list *el;
	struct super_block *sb = ocfs2_metadata_cache_get_super(et->et_ci);
	u64 ino = ocfs2_metadata_cache_owner(et->et_ci);

	trace_ocfs2_clear_ext_refcount((unsigned long long)ino,
				       cpos, len, p_cluster, ext_flags);

	memset(&replace_rec, 0, sizeof(replace_rec));
	replace_rec.e_cpos = cpu_to_le32(cpos);
	replace_rec.e_leaf_clusters = cpu_to_le16(len);
	replace_rec.e_blkno = cpu_to_le64(ocfs2_clusters_to_blocks(sb,
								   p_cluster));
	replace_rec.e_flags = ext_flags;
	replace_rec.e_flags &= ~OCFS2_EXT_REFCOUNTED;

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
	if (index == -1 || index >= le16_to_cpu(el->l_next_free_rec)) {
		ocfs2_error(sb,
			    "Inode %llu has an extent at cpos %u which can no "
			    "longer be found.\n",
			    (unsigned long long)ino, cpos);
		ret = -EROFS;
		goto out;
	}

	ret = ocfs2_split_extent(handle, et, path, index,
				 &replace_rec, meta_ac, dealloc);
	if (ret)
		mlog_errno(ret);

out:
	ocfs2_free_path(path);
	return ret;
}

static int ocfs2_replace_clusters(handle_t *handle,
				  struct ocfs2_cow_context *context,
				  u32 cpos, u32 old,
				  u32 new, u32 len,
				  unsigned int ext_flags)
{
	int ret;
	struct ocfs2_caching_info *ci = context->data_et.et_ci;
	u64 ino = ocfs2_metadata_cache_owner(ci);

	trace_ocfs2_replace_clusters((unsigned long long)ino,
				     cpos, old, new, len, ext_flags);

	/*If the old clusters is unwritten, no need to duplicate. */
	if (!(ext_flags & OCFS2_EXT_UNWRITTEN)) {
		ret = context->cow_duplicate_clusters(handle, context->inode,
						      cpos, old, new, len);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	ret = ocfs2_clear_ext_refcount(handle, &context->data_et,
				       cpos, new, len, ext_flags,
				       context->meta_ac, &context->dealloc);
	if (ret)
		mlog_errno(ret);
out:
	return ret;
}

int ocfs2_cow_sync_writeback(struct super_block *sb,
			     struct inode *inode,
			     u32 cpos, u32 num_clusters)
{
	int ret = 0;
	loff_t offset, end, map_end;
	pgoff_t page_index;
	struct page *page;

	if (ocfs2_should_order_data(inode))
		return 0;

	offset = ((loff_t)cpos) << OCFS2_SB(sb)->s_clustersize_bits;
	end = offset + (num_clusters << OCFS2_SB(sb)->s_clustersize_bits);

	ret = filemap_fdatawrite_range(inode->i_mapping,
				       offset, end - 1);
	if (ret < 0) {
		mlog_errno(ret);
		return ret;
	}

	while (offset < end) {
		page_index = offset >> PAGE_CACHE_SHIFT;
		map_end = ((loff_t)page_index + 1) << PAGE_CACHE_SHIFT;
		if (map_end > end)
			map_end = end;

		page = find_or_create_page(inode->i_mapping,
					   page_index, GFP_NOFS);
		BUG_ON(!page);

		wait_on_page_writeback(page);
		if (PageError(page)) {
			ret = -EIO;
			mlog_errno(ret);
		} else
			mark_page_accessed(page);

		unlock_page(page);
		page_cache_release(page);
		page = NULL;
		offset = map_end;
		if (ret)
			break;
	}

	return ret;
}

static int ocfs2_di_get_clusters(struct ocfs2_cow_context *context,
				 u32 v_cluster, u32 *p_cluster,
				 u32 *num_clusters,
				 unsigned int *extent_flags)
{
	return ocfs2_get_clusters(context->inode, v_cluster, p_cluster,
				  num_clusters, extent_flags);
}

static int ocfs2_make_clusters_writable(struct super_block *sb,
					struct ocfs2_cow_context *context,
					u32 cpos, u32 p_cluster,
					u32 num_clusters, unsigned int e_flags)
{
	int ret, delete, index, credits =  0;
	u32 new_bit, new_len, orig_num_clusters;
	unsigned int set_len;
	struct ocfs2_super *osb = OCFS2_SB(sb);
	handle_t *handle;
	struct buffer_head *ref_leaf_bh = NULL;
	struct ocfs2_caching_info *ref_ci = &context->ref_tree->rf_ci;
	struct ocfs2_refcount_rec rec;

	trace_ocfs2_make_clusters_writable(cpos, p_cluster,
					   num_clusters, e_flags);

	ret = ocfs2_lock_refcount_allocators(sb, p_cluster, num_clusters,
					     &context->data_et,
					     ref_ci,
					     context->ref_root_bh,
					     &context->meta_ac,
					     &context->data_ac, &credits);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	if (context->post_refcount)
		credits += context->post_refcount->credits;

	credits += context->extra_credits;
	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	orig_num_clusters = num_clusters;

	while (num_clusters) {
		ret = ocfs2_get_refcount_rec(ref_ci, context->ref_root_bh,
					     p_cluster, num_clusters,
					     &rec, &index, &ref_leaf_bh);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}

		BUG_ON(!rec.r_refcount);
		set_len = min((u64)p_cluster + num_clusters,
			      le64_to_cpu(rec.r_cpos) +
			      le32_to_cpu(rec.r_clusters)) - p_cluster;

		/*
		 * There are many different situation here.
		 * 1. If refcount == 1, remove the flag and don't COW.
		 * 2. If refcount > 1, allocate clusters.
		 *    Here we may not allocate r_len once at a time, so continue
		 *    until we reach num_clusters.
		 */
		if (le32_to_cpu(rec.r_refcount) == 1) {
			delete = 0;
			ret = ocfs2_clear_ext_refcount(handle,
						       &context->data_et,
						       cpos, p_cluster,
						       set_len, e_flags,
						       context->meta_ac,
						       &context->dealloc);
			if (ret) {
				mlog_errno(ret);
				goto out_commit;
			}
		} else {
			delete = 1;

			ret = __ocfs2_claim_clusters(handle,
						     context->data_ac,
						     1, set_len,
						     &new_bit, &new_len);
			if (ret) {
				mlog_errno(ret);
				goto out_commit;
			}

			ret = ocfs2_replace_clusters(handle, context,
						     cpos, p_cluster, new_bit,
						     new_len, e_flags);
			if (ret) {
				mlog_errno(ret);
				goto out_commit;
			}
			set_len = new_len;
		}

		ret = __ocfs2_decrease_refcount(handle, ref_ci,
						context->ref_root_bh,
						p_cluster, set_len,
						context->meta_ac,
						&context->dealloc, delete);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}

		cpos += set_len;
		p_cluster += set_len;
		num_clusters -= set_len;
		brelse(ref_leaf_bh);
		ref_leaf_bh = NULL;
	}

	/* handle any post_cow action. */
	if (context->post_refcount && context->post_refcount->func) {
		ret = context->post_refcount->func(context->inode, handle,
						context->post_refcount->para);
		if (ret) {
			mlog_errno(ret);
			goto out_commit;
		}
	}

	/*
	 * Here we should write the new page out first if we are
	 * in write-back mode.
	 */
	if (context->get_clusters == ocfs2_di_get_clusters) {
		ret = ocfs2_cow_sync_writeback(sb, context->inode, cpos,
					       orig_num_clusters);
		if (ret)
			mlog_errno(ret);
	}

out_commit:
	ocfs2_commit_trans(osb, handle);

out:
	if (context->data_ac) {
		ocfs2_free_alloc_context(context->data_ac);
		context->data_ac = NULL;
	}
	if (context->meta_ac) {
		ocfs2_free_alloc_context(context->meta_ac);
		context->meta_ac = NULL;
	}
	brelse(ref_leaf_bh);

	return ret;
}

static int ocfs2_replace_cow(struct ocfs2_cow_context *context)
{
	int ret = 0;
	struct inode *inode = context->inode;
	u32 cow_start = context->cow_start, cow_len = context->cow_len;
	u32 p_cluster, num_clusters;
	unsigned int ext_flags;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);

	if (!ocfs2_refcount_tree(OCFS2_SB(inode->i_sb))) {
		ocfs2_error(inode->i_sb, "Inode %lu want to use refcount "
			    "tree, but the feature bit is not set in the "
			    "super block.", inode->i_ino);
		return -EROFS;
	}

	ocfs2_init_dealloc_ctxt(&context->dealloc);

	while (cow_len) {
		ret = context->get_clusters(context, cow_start, &p_cluster,
					    &num_clusters, &ext_flags);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		BUG_ON(!(ext_flags & OCFS2_EXT_REFCOUNTED));

		if (cow_len < num_clusters)
			num_clusters = cow_len;

		ret = ocfs2_make_clusters_writable(inode->i_sb, context,
						   cow_start, p_cluster,
						   num_clusters, ext_flags);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		cow_len -= num_clusters;
		cow_start += num_clusters;
	}

	if (ocfs2_dealloc_has_cluster(&context->dealloc)) {
		ocfs2_schedule_truncate_log_flush(osb, 1);
		ocfs2_run_deallocs(osb, &context->dealloc);
	}

	return ret;
}

/*
 * Starting at cpos, try to CoW write_len clusters.  Don't CoW
 * past max_cpos.  This will stop when it runs into a hole or an
 * unrefcounted extent.
 */
static int ocfs2_refcount_cow_hunk(struct inode *inode,
				   struct buffer_head *di_bh,
				   u32 cpos, u32 write_len, u32 max_cpos)
{
	int ret;
	u32 cow_start = 0, cow_len = 0;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_refcount_tree *ref_tree;
	struct ocfs2_cow_context *context = NULL;

	BUG_ON(!(oi->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL));

	ret = ocfs2_refcount_cal_cow_clusters(inode, &di->id2.i_list,
					      cpos, write_len, max_cpos,
					      &cow_start, &cow_len);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	trace_ocfs2_refcount_cow_hunk(OCFS2_I(inode)->ip_blkno,
				      cpos, write_len, max_cpos,
				      cow_start, cow_len);

	BUG_ON(cow_len == 0);

	context = kzalloc(sizeof(struct ocfs2_cow_context), GFP_NOFS);
	if (!context) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_lock_refcount_tree(osb, le64_to_cpu(di->i_refcount_loc),
				       1, &ref_tree, &ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	context->inode = inode;
	context->cow_start = cow_start;
	context->cow_len = cow_len;
	context->ref_tree = ref_tree;
	context->ref_root_bh = ref_root_bh;
	context->cow_duplicate_clusters = ocfs2_duplicate_clusters_by_page;
	context->get_clusters = ocfs2_di_get_clusters;

	ocfs2_init_dinode_extent_tree(&context->data_et,
				      INODE_CACHE(inode), di_bh);

	ret = ocfs2_replace_cow(context);
	if (ret)
		mlog_errno(ret);

	/*
	 * truncate the extent map here since no matter whether we meet with
	 * any error during the action, we shouldn't trust cached extent map
	 * any more.
	 */
	ocfs2_extent_map_trunc(inode, cow_start);

	ocfs2_unlock_refcount_tree(osb, ref_tree, 1);
	brelse(ref_root_bh);
out:
	kfree(context);
	return ret;
}

/*
 * CoW any and all clusters between cpos and cpos+write_len.
 * Don't CoW past max_cpos.  If this returns successfully, all
 * clusters between cpos and cpos+write_len are safe to modify.
 */
int ocfs2_refcount_cow(struct inode *inode,
		       struct buffer_head *di_bh,
		       u32 cpos, u32 write_len, u32 max_cpos)
{
	int ret = 0;
	u32 p_cluster, num_clusters;
	unsigned int ext_flags;

	while (write_len) {
		ret = ocfs2_get_clusters(inode, cpos, &p_cluster,
					 &num_clusters, &ext_flags);
		if (ret) {
			mlog_errno(ret);
			break;
		}

		if (write_len < num_clusters)
			num_clusters = write_len;

		if (ext_flags & OCFS2_EXT_REFCOUNTED) {
			ret = ocfs2_refcount_cow_hunk(inode, di_bh, cpos,
						      num_clusters, max_cpos);
			if (ret) {
				mlog_errno(ret);
				break;
			}
		}

		write_len -= num_clusters;
		cpos += num_clusters;
	}

	return ret;
}

static int ocfs2_xattr_value_get_clusters(struct ocfs2_cow_context *context,
					  u32 v_cluster, u32 *p_cluster,
					  u32 *num_clusters,
					  unsigned int *extent_flags)
{
	struct inode *inode = context->inode;
	struct ocfs2_xattr_value_root *xv = context->cow_object;

	return ocfs2_xattr_get_clusters(inode, v_cluster, p_cluster,
					num_clusters, &xv->xr_list,
					extent_flags);
}

/*
 * Given a xattr value root, calculate the most meta/credits we need for
 * refcount tree change if we truncate it to 0.
 */
int ocfs2_refcounted_xattr_delete_need(struct inode *inode,
				       struct ocfs2_caching_info *ref_ci,
				       struct buffer_head *ref_root_bh,
				       struct ocfs2_xattr_value_root *xv,
				       int *meta_add, int *credits)
{
	int ret = 0, index, ref_blocks = 0;
	u32 p_cluster, num_clusters;
	u32 cpos = 0, clusters = le32_to_cpu(xv->xr_clusters);
	struct ocfs2_refcount_block *rb;
	struct ocfs2_refcount_rec rec;
	struct buffer_head *ref_leaf_bh = NULL;

	while (cpos < clusters) {
		ret = ocfs2_xattr_get_clusters(inode, cpos, &p_cluster,
					       &num_clusters, &xv->xr_list,
					       NULL);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		cpos += num_clusters;

		while (num_clusters) {
			ret = ocfs2_get_refcount_rec(ref_ci, ref_root_bh,
						     p_cluster, num_clusters,
						     &rec, &index,
						     &ref_leaf_bh);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}

			BUG_ON(!rec.r_refcount);

			rb = (struct ocfs2_refcount_block *)ref_leaf_bh->b_data;

			/*
			 * We really don't know whether the other clusters is in
			 * this refcount block or not, so just take the worst
			 * case that all the clusters are in this block and each
			 * one will split a refcount rec, so totally we need
			 * clusters * 2 new refcount rec.
			 */
			if (le16_to_cpu(rb->rf_records.rl_used) + clusters * 2 >
			    le16_to_cpu(rb->rf_records.rl_count))
				ref_blocks++;

			*credits += 1;
			brelse(ref_leaf_bh);
			ref_leaf_bh = NULL;

			if (num_clusters <= le32_to_cpu(rec.r_clusters))
				break;
			else
				num_clusters -= le32_to_cpu(rec.r_clusters);
			p_cluster += num_clusters;
		}
	}

	*meta_add += ref_blocks;
	if (!ref_blocks)
		goto out;

	rb = (struct ocfs2_refcount_block *)ref_root_bh->b_data;
	if (le32_to_cpu(rb->rf_flags) & OCFS2_REFCOUNT_TREE_FL)
		*credits += OCFS2_EXPAND_REFCOUNT_TREE_CREDITS;
	else {
		struct ocfs2_extent_tree et;

		ocfs2_init_refcount_extent_tree(&et, ref_ci, ref_root_bh);
		*credits += ocfs2_calc_extend_credits(inode->i_sb,
						      et.et_root_el);
	}

out:
	brelse(ref_leaf_bh);
	return ret;
}

/*
 * Do CoW for xattr.
 */
int ocfs2_refcount_cow_xattr(struct inode *inode,
			     struct ocfs2_dinode *di,
			     struct ocfs2_xattr_value_buf *vb,
			     struct ocfs2_refcount_tree *ref_tree,
			     struct buffer_head *ref_root_bh,
			     u32 cpos, u32 write_len,
			     struct ocfs2_post_refcount *post)
{
	int ret;
	struct ocfs2_xattr_value_root *xv = vb->vb_xv;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_cow_context *context = NULL;
	u32 cow_start, cow_len;

	BUG_ON(!(oi->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL));

	ret = ocfs2_refcount_cal_cow_clusters(inode, &xv->xr_list,
					      cpos, write_len, UINT_MAX,
					      &cow_start, &cow_len);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	BUG_ON(cow_len == 0);

	context = kzalloc(sizeof(struct ocfs2_cow_context), GFP_NOFS);
	if (!context) {
		ret = -ENOMEM;
		mlog_errno(ret);
		goto out;
	}

	context->inode = inode;
	context->cow_start = cow_start;
	context->cow_len = cow_len;
	context->ref_tree = ref_tree;
	context->ref_root_bh = ref_root_bh;
	context->cow_object = xv;

	context->cow_duplicate_clusters = ocfs2_duplicate_clusters_by_jbd;
	/* We need the extra credits for duplicate_clusters by jbd. */
	context->extra_credits =
		ocfs2_clusters_to_blocks(inode->i_sb, 1) * cow_len;
	context->get_clusters = ocfs2_xattr_value_get_clusters;
	context->post_refcount = post;

	ocfs2_init_xattr_value_extent_tree(&context->data_et,
					   INODE_CACHE(inode), vb);

	ret = ocfs2_replace_cow(context);
	if (ret)
		mlog_errno(ret);

out:
	kfree(context);
	return ret;
}

/*
 * Insert a new extent into refcount tree and mark a extent rec
 * as refcounted in the dinode tree.
 */
int ocfs2_add_refcount_flag(struct inode *inode,
			    struct ocfs2_extent_tree *data_et,
			    struct ocfs2_caching_info *ref_ci,
			    struct buffer_head *ref_root_bh,
			    u32 cpos, u32 p_cluster, u32 num_clusters,
			    struct ocfs2_cached_dealloc_ctxt *dealloc,
			    struct ocfs2_post_refcount *post)
{
	int ret;
	handle_t *handle;
	int credits = 1, ref_blocks = 0;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_alloc_context *meta_ac = NULL;

	ret = ocfs2_calc_refcount_meta_credits(inode->i_sb,
					       ref_ci, ref_root_bh,
					       p_cluster, num_clusters,
					       &ref_blocks, &credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	trace_ocfs2_add_refcount_flag(ref_blocks, credits);

	if (ref_blocks) {
		ret = ocfs2_reserve_new_metadata_blocks(OCFS2_SB(inode->i_sb),
							ref_blocks, &meta_ac);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	if (post)
		credits += post->credits;

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_mark_extent_refcounted(inode, data_et, handle,
					   cpos, num_clusters, p_cluster,
					   meta_ac, dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = __ocfs2_increase_refcount(handle, ref_ci, ref_root_bh,
					p_cluster, num_clusters, 0,
					meta_ac, dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	if (post && post->func) {
		ret = post->func(inode, handle, post->para);
		if (ret)
			mlog_errno(ret);
	}

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);
	return ret;
}

static int ocfs2_change_ctime(struct inode *inode,
			      struct buffer_head *di_bh)
{
	int ret;
	handle_t *handle;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;

	handle = ocfs2_start_trans(OCFS2_SB(inode->i_sb),
				   OCFS2_INODE_UPDATE_CREDITS);
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

	inode->i_ctime = CURRENT_TIME;
	di->i_ctime = cpu_to_le64(inode->i_ctime.tv_sec);
	di->i_ctime_nsec = cpu_to_le32(inode->i_ctime.tv_nsec);

	ocfs2_journal_dirty(handle, di_bh);

out_commit:
	ocfs2_commit_trans(OCFS2_SB(inode->i_sb), handle);
out:
	return ret;
}

static int ocfs2_attach_refcount_tree(struct inode *inode,
				      struct buffer_head *di_bh)
{
	int ret, data_changed = 0;
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_inode_info *oi = OCFS2_I(inode);
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)di_bh->b_data;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_refcount_tree *ref_tree;
	unsigned int ext_flags;
	loff_t size;
	u32 cpos, num_clusters, clusters, p_cluster;
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct ocfs2_extent_tree di_et;

	ocfs2_init_dealloc_ctxt(&dealloc);

	if (!(oi->ip_dyn_features & OCFS2_HAS_REFCOUNT_FL)) {
		ret = ocfs2_create_refcount_tree(inode, di_bh);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
	}

	BUG_ON(!di->i_refcount_loc);
	ret = ocfs2_lock_refcount_tree(osb,
				       le64_to_cpu(di->i_refcount_loc), 1,
				       &ref_tree, &ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (oi->ip_dyn_features & OCFS2_INLINE_DATA_FL)
		goto attach_xattr;

	ocfs2_init_dinode_extent_tree(&di_et, INODE_CACHE(inode), di_bh);

	size = i_size_read(inode);
	clusters = ocfs2_clusters_for_bytes(inode->i_sb, size);

	cpos = 0;
	while (cpos < clusters) {
		ret = ocfs2_get_clusters(inode, cpos, &p_cluster,
					 &num_clusters, &ext_flags);
		if (ret) {
			mlog_errno(ret);
			goto unlock;
		}
		if (p_cluster && !(ext_flags & OCFS2_EXT_REFCOUNTED)) {
			ret = ocfs2_add_refcount_flag(inode, &di_et,
						      &ref_tree->rf_ci,
						      ref_root_bh, cpos,
						      p_cluster, num_clusters,
						      &dealloc, NULL);
			if (ret) {
				mlog_errno(ret);
				goto unlock;
			}

			data_changed = 1;
		}
		cpos += num_clusters;
	}

attach_xattr:
	if (oi->ip_dyn_features & OCFS2_HAS_XATTR_FL) {
		ret = ocfs2_xattr_attach_refcount_tree(inode, di_bh,
						       &ref_tree->rf_ci,
						       ref_root_bh,
						       &dealloc);
		if (ret) {
			mlog_errno(ret);
			goto unlock;
		}
	}

	if (data_changed) {
		ret = ocfs2_change_ctime(inode, di_bh);
		if (ret)
			mlog_errno(ret);
	}

unlock:
	ocfs2_unlock_refcount_tree(osb, ref_tree, 1);
	brelse(ref_root_bh);

	if (!ret && ocfs2_dealloc_has_cluster(&dealloc)) {
		ocfs2_schedule_truncate_log_flush(osb, 1);
		ocfs2_run_deallocs(osb, &dealloc);
	}
out:
	/*
	 * Empty the extent map so that we may get the right extent
	 * record from the disk.
	 */
	ocfs2_extent_map_trunc(inode, 0);

	return ret;
}

static int ocfs2_add_refcounted_extent(struct inode *inode,
				   struct ocfs2_extent_tree *et,
				   struct ocfs2_caching_info *ref_ci,
				   struct buffer_head *ref_root_bh,
				   u32 cpos, u32 p_cluster, u32 num_clusters,
				   unsigned int ext_flags,
				   struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret;
	handle_t *handle;
	int credits = 0;
	struct ocfs2_super *osb = OCFS2_SB(inode->i_sb);
	struct ocfs2_alloc_context *meta_ac = NULL;

	ret = ocfs2_lock_refcount_allocators(inode->i_sb,
					     p_cluster, num_clusters,
					     et, ref_ci,
					     ref_root_bh, &meta_ac,
					     NULL, &credits);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	handle = ocfs2_start_trans(osb, credits);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_insert_extent(handle, et, cpos,
			ocfs2_clusters_to_blocks(inode->i_sb, p_cluster),
			num_clusters, ext_flags, meta_ac);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	ret = ocfs2_increase_refcount(handle, ref_ci, ref_root_bh,
				      p_cluster, num_clusters,
				      meta_ac, dealloc);
	if (ret)
		mlog_errno(ret);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	if (meta_ac)
		ocfs2_free_alloc_context(meta_ac);
	return ret;
}

static int ocfs2_duplicate_inline_data(struct inode *s_inode,
				       struct buffer_head *s_bh,
				       struct inode *t_inode,
				       struct buffer_head *t_bh)
{
	int ret;
	handle_t *handle;
	struct ocfs2_super *osb = OCFS2_SB(s_inode->i_sb);
	struct ocfs2_dinode *s_di = (struct ocfs2_dinode *)s_bh->b_data;
	struct ocfs2_dinode *t_di = (struct ocfs2_dinode *)t_bh->b_data;

	BUG_ON(!(OCFS2_I(s_inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL));

	handle = ocfs2_start_trans(osb, OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(t_inode), t_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	t_di->id2.i_data.id_count = s_di->id2.i_data.id_count;
	memcpy(t_di->id2.i_data.id_data, s_di->id2.i_data.id_data,
	       le16_to_cpu(s_di->id2.i_data.id_count));
	spin_lock(&OCFS2_I(t_inode)->ip_lock);
	OCFS2_I(t_inode)->ip_dyn_features |= OCFS2_INLINE_DATA_FL;
	t_di->i_dyn_features = cpu_to_le16(OCFS2_I(t_inode)->ip_dyn_features);
	spin_unlock(&OCFS2_I(t_inode)->ip_lock);

	ocfs2_journal_dirty(handle, t_bh);

out_commit:
	ocfs2_commit_trans(osb, handle);
out:
	return ret;
}

static int ocfs2_duplicate_extent_list(struct inode *s_inode,
				struct inode *t_inode,
				struct buffer_head *t_bh,
				struct ocfs2_caching_info *ref_ci,
				struct buffer_head *ref_root_bh,
				struct ocfs2_cached_dealloc_ctxt *dealloc)
{
	int ret = 0;
	u32 p_cluster, num_clusters, clusters, cpos;
	loff_t size;
	unsigned int ext_flags;
	struct ocfs2_extent_tree et;

	ocfs2_init_dinode_extent_tree(&et, INODE_CACHE(t_inode), t_bh);

	size = i_size_read(s_inode);
	clusters = ocfs2_clusters_for_bytes(s_inode->i_sb, size);

	cpos = 0;
	while (cpos < clusters) {
		ret = ocfs2_get_clusters(s_inode, cpos, &p_cluster,
					 &num_clusters, &ext_flags);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}
		if (p_cluster) {
			ret = ocfs2_add_refcounted_extent(t_inode, &et,
							  ref_ci, ref_root_bh,
							  cpos, p_cluster,
							  num_clusters,
							  ext_flags,
							  dealloc);
			if (ret) {
				mlog_errno(ret);
				goto out;
			}
		}

		cpos += num_clusters;
	}

out:
	return ret;
}

/*
 * change the new file's attributes to the src.
 *
 * reflink creates a snapshot of a file, that means the attributes
 * must be identical except for three exceptions - nlink, ino, and ctime.
 */
static int ocfs2_complete_reflink(struct inode *s_inode,
				  struct buffer_head *s_bh,
				  struct inode *t_inode,
				  struct buffer_head *t_bh,
				  bool preserve)
{
	int ret;
	handle_t *handle;
	struct ocfs2_dinode *s_di = (struct ocfs2_dinode *)s_bh->b_data;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)t_bh->b_data;
	loff_t size = i_size_read(s_inode);

	handle = ocfs2_start_trans(OCFS2_SB(t_inode->i_sb),
				   OCFS2_INODE_UPDATE_CREDITS);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		mlog_errno(ret);
		return ret;
	}

	ret = ocfs2_journal_access_di(handle, INODE_CACHE(t_inode), t_bh,
				      OCFS2_JOURNAL_ACCESS_WRITE);
	if (ret) {
		mlog_errno(ret);
		goto out_commit;
	}

	spin_lock(&OCFS2_I(t_inode)->ip_lock);
	OCFS2_I(t_inode)->ip_clusters = OCFS2_I(s_inode)->ip_clusters;
	OCFS2_I(t_inode)->ip_attr = OCFS2_I(s_inode)->ip_attr;
	OCFS2_I(t_inode)->ip_dyn_features = OCFS2_I(s_inode)->ip_dyn_features;
	spin_unlock(&OCFS2_I(t_inode)->ip_lock);
	i_size_write(t_inode, size);
	t_inode->i_blocks = s_inode->i_blocks;

	di->i_xattr_inline_size = s_di->i_xattr_inline_size;
	di->i_clusters = s_di->i_clusters;
	di->i_size = s_di->i_size;
	di->i_dyn_features = s_di->i_dyn_features;
	di->i_attr = s_di->i_attr;

	if (preserve) {
		t_inode->i_uid = s_inode->i_uid;
		t_inode->i_gid = s_inode->i_gid;
		t_inode->i_mode = s_inode->i_mode;
		di->i_uid = s_di->i_uid;
		di->i_gid = s_di->i_gid;
		di->i_mode = s_di->i_mode;

		/*
		 * update time.
		 * we want mtime to appear identical to the source and
		 * update ctime.
		 */
		t_inode->i_ctime = CURRENT_TIME;

		di->i_ctime = cpu_to_le64(t_inode->i_ctime.tv_sec);
		di->i_ctime_nsec = cpu_to_le32(t_inode->i_ctime.tv_nsec);

		t_inode->i_mtime = s_inode->i_mtime;
		di->i_mtime = s_di->i_mtime;
		di->i_mtime_nsec = s_di->i_mtime_nsec;
	}

	ocfs2_journal_dirty(handle, t_bh);

out_commit:
	ocfs2_commit_trans(OCFS2_SB(t_inode->i_sb), handle);
	return ret;
}

static int ocfs2_create_reflink_node(struct inode *s_inode,
				     struct buffer_head *s_bh,
				     struct inode *t_inode,
				     struct buffer_head *t_bh,
				     bool preserve)
{
	int ret;
	struct buffer_head *ref_root_bh = NULL;
	struct ocfs2_cached_dealloc_ctxt dealloc;
	struct ocfs2_super *osb = OCFS2_SB(s_inode->i_sb);
	struct ocfs2_refcount_block *rb;
	struct ocfs2_dinode *di = (struct ocfs2_dinode *)s_bh->b_data;
	struct ocfs2_refcount_tree *ref_tree;

	ocfs2_init_dealloc_ctxt(&dealloc);

	ret = ocfs2_set_refcount_tree(t_inode, t_bh,
				      le64_to_cpu(di->i_refcount_loc));
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	if (OCFS2_I(s_inode)->ip_dyn_features & OCFS2_INLINE_DATA_FL) {
		ret = ocfs2_duplicate_inline_data(s_inode, s_bh,
						  t_inode, t_bh);
		if (ret)
			mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_lock_refcount_tree(osb, le64_to_cpu(di->i_refcount_loc),
				       1, &ref_tree, &ref_root_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}
	rb = (struct ocfs2_refcount_block *)ref_root_bh->b_data;

	ret = ocfs2_duplicate_extent_list(s_inode, t_inode, t_bh,
					  &ref_tree->rf_ci, ref_root_bh,
					  &dealloc);
	if (ret) {
		mlog_errno(ret);
		goto out_unlock_refcount;
	}

out_unlock_refcount:
	ocfs2_unlock_refcount_tree(osb, ref_tree, 1);
	brelse(ref_root_bh);
out:
	if (ocfs2_dealloc_has_cluster(&dealloc)) {
		ocfs2_schedule_truncate_log_flush(osb, 1);
		ocfs2_run_deallocs(osb, &dealloc);
	}

	return ret;
}

static int __ocfs2_reflink(struct dentry *old_dentry,
			   struct buffer_head *old_bh,
			   struct inode *new_inode,
			   bool preserve)
{
	int ret;
	struct inode *inode = old_dentry->d_inode;
	struct buffer_head *new_bh = NULL;

	if (OCFS2_I(inode)->ip_flags & OCFS2_INODE_SYSTEM_FILE) {
		ret = -EINVAL;
		mlog_errno(ret);
		goto out;
	}

	ret = filemap_fdatawrite(inode->i_mapping);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	ret = ocfs2_attach_refcount_tree(inode, old_bh);
	if (ret) {
		mlog_errno(ret);
		goto out;
	}

	mutex_lock_nested(&new_inode->i_mutex, I_MUTEX_CHILD);
	ret = ocfs2_inode_lock_nested(new_inode, &new_bh, 1,
				      OI_LS_REFLINK_TARGET);
	if (ret) {
		mlog_errno(ret);
		goto out_unlock;
	}

	ret = ocfs2_create_reflink_node(inode, old_bh,
					new_inode, new_bh, preserve);
	if (ret) {
		mlog_errno(ret);
		goto inode_unlock;
	}

	if (OCFS2_I(inode)->ip_dyn_features & OCFS2_HAS_XATTR_FL) {
		ret = ocfs2_reflink_xattrs(inode, old_bh,
					   new_inode, new_bh,
					   preserve);
		if (ret) {
			mlog_errno(ret);
			goto inode_unlock;
		}
	}

	ret = ocfs2_complete_reflink(inode, old_bh,
				     new_inode, new_bh, preserve);
	if (ret)
		mlog_errno(ret);

inode_unlock:
	ocfs2_inode_unlock(new_inode, 1);
	brelse(new_bh);
out_unlock:
	mutex_unlock(&new_inode->i_mutex);
out:
	if (!ret) {
		ret = filemap_fdatawait(inode->i_mapping);
		if (ret)
			mlog_errno(ret);
	}
	return ret;
}

static int ocfs2_reflink(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry, bool preserve)
{
	int error;
	struct inode *inode = old_dentry->d_inode;
	struct buffer_head *old_bh = NULL;
	struct inode *new_orphan_inode = NULL;
	struct posix_acl *default_acl, *acl;
	umode_t mode;

	if (!ocfs2_refcount_tree(OCFS2_SB(inode->i_sb)))
		return -EOPNOTSUPP;

	mode = inode->i_mode;
	error = posix_acl_create(dir, &mode, &default_acl, &acl);
	if (error) {
		mlog_errno(error);
		goto out;
	}

	error = ocfs2_create_inode_in_orphan(dir, mode,
					     &new_orphan_inode);
	if (error) {
		mlog_errno(error);
		goto out;
	}

	error = ocfs2_rw_lock(inode, 1);
	if (error) {
		mlog_errno(error);
		goto out;
	}

	error = ocfs2_inode_lock(inode, &old_bh, 1);
	if (error) {
		mlog_errno(error);
		ocfs2_rw_unlock(inode, 1);
		goto out;
	}

	down_write(&OCFS2_I(inode)->ip_xattr_sem);
	down_write(&OCFS2_I(inode)->ip_alloc_sem);
	error = __ocfs2_reflink(old_dentry, old_bh,
				new_orphan_inode, preserve);
	up_write(&OCFS2_I(inode)->ip_alloc_sem);
	up_write(&OCFS2_I(inode)->ip_xattr_sem);

	ocfs2_inode_unlock(inode, 1);
	ocfs2_rw_unlock(inode, 1);
	brelse(old_bh);

	if (error) {
		mlog_errno(error);
		goto out;
	}

	/* If the security isn't preserved, we need to re-initialize them. */
	if (!preserve) {
		error = ocfs2_init_security_and_acl(dir, new_orphan_inode,
						    &new_dentry->d_name,
						    default_acl, acl);
		if (error)
			mlog_errno(error);
	}
out:
	if (default_acl)
		posix_acl_release(default_acl);
	if (acl)
		posix_acl_release(acl);
	if (!error) {
		error = ocfs2_mv_orphaned_inode_to_new(dir, new_orphan_inode,
						       new_dentry);
		if (error)
			mlog_errno(error);
	}

	if (new_orphan_inode) {
		/*
		 * We need to open_unlock the inode no matter whether we
		 * succeed or not, so that other nodes can delete it later.
		 */
		ocfs2_open_unlock(new_orphan_inode);
		if (error)
			iput(new_orphan_inode);
	}

	return error;
}

/*
 * Below here are the bits used by OCFS2_IOC_REFLINK() to fake
 * sys_reflink().  This will go away when vfs_reflink() exists in
 * fs/namei.c.
 */

/* copied from may_create in VFS. */
static inline int ocfs2_may_create(struct inode *dir, struct dentry *child)
{
	if (child->d_inode)
		return -EEXIST;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	return inode_permission(dir, MAY_WRITE | MAY_EXEC);
}

/**
 * ocfs2_vfs_reflink - Create a reference-counted link
 *
 * @old_dentry:        source dentry + inode
 * @dir:       directory to create the target
 * @new_dentry:        target dentry
 * @preserve:  if true, preserve all file attributes
 */
static int ocfs2_vfs_reflink(struct dentry *old_dentry, struct inode *dir,
			     struct dentry *new_dentry, bool preserve)
{
	struct inode *inode = old_dentry->d_inode;
	int error;

	if (!inode)
		return -ENOENT;

	error = ocfs2_may_create(dir, new_dentry);
	if (error)
		return error;

	if (dir->i_sb != inode->i_sb)
		return -EXDEV;

	/*
	 * A reflink to an append-only or immutable file cannot be created.
	 */
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		return -EPERM;

	/* Only regular files can be reflinked. */
	if (!S_ISREG(inode->i_mode))
		return -EPERM;

	/*
	 * If the caller wants to preserve ownership, they require the
	 * rights to do so.
	 */
	if (preserve) {
		if (!uid_eq(current_fsuid(), inode->i_uid) && !capable(CAP_CHOWN))
			return -EPERM;
		if (!in_group_p(inode->i_gid) && !capable(CAP_CHOWN))
			return -EPERM;
	}

	/*
	 * If the caller is modifying any aspect of the attributes, they
	 * are not creating a snapshot.  They need read permission on the
	 * file.
	 */
	if (!preserve) {
		error = inode_permission(inode, MAY_READ);
		if (error)
			return error;
	}

	mutex_lock(&inode->i_mutex);
	dquot_initialize(dir);
	error = ocfs2_reflink(old_dentry, dir, new_dentry, preserve);
	mutex_unlock(&inode->i_mutex);
	if (!error)
		fsnotify_create(dir, new_dentry);
	return error;
}
/*
 * Most codes are copied from sys_linkat.
 */
int ocfs2_reflink_ioctl(struct inode *inode,
			const char __user *oldname,
			const char __user *newname,
			bool preserve)
{
	struct dentry *new_dentry;
	struct path old_path, new_path;
	int error;

	if (!ocfs2_refcount_tree(OCFS2_SB(inode->i_sb)))
		return -EOPNOTSUPP;

	error = user_path_at(AT_FDCWD, oldname, 0, &old_path);
	if (error) {
		mlog_errno(error);
		return error;
	}

	new_dentry = user_path_create(AT_FDCWD, newname, &new_path, 0);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry)) {
		mlog_errno(error);
		goto out;
	}

	error = -EXDEV;
	if (old_path.mnt != new_path.mnt) {
		mlog_errno(error);
		goto out_dput;
	}

	error = ocfs2_vfs_reflink(old_path.dentry,
				  new_path.dentry->d_inode,
				  new_dentry, preserve);
out_dput:
	done_path_create(&new_path, new_dentry);
out:
	path_put(&old_path);

	return error;
}
