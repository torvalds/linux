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

#define MLOG_MASK_PREFIX ML_REFCOUNT
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
#include "dlmglue.h"

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

	mlog(0, "Validating refcount block %llu\n",
	     (unsigned long long)bh->b_blocknr);

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

void ocfs2_kref_remove_refcount_tree(struct kref *kref)
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

	new = kzalloc(sizeof(struct ocfs2_refcount_tree), GFP_NOFS);
	if (!new) {
		ret = -ENOMEM;
		return ret;
	}

	new->rf_blkno = rf_blkno;
	kref_init(&new->rf_getcnt);
	ocfs2_init_refcount_tree_ci(new, osb->sb);

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

int ocfs2_lock_refcount_tree_by_inode(struct inode *inode, int rw,
				      struct ocfs2_refcount_tree **ret_tree,
				      struct buffer_head **ref_bh)
{
	int ret;
	u64 ref_blkno;

	ret = ocfs2_get_refcount_block(inode, &ref_blkno);
	if (ret) {
		mlog_errno(ret);
		return ret;
	}

	return ocfs2_lock_refcount_tree(OCFS2_SB(inode->i_sb), ref_blkno,
					rw, ret_tree, ref_bh);
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

		mlog(0, "Purge tree %llu\n",
		     (unsigned long long) tree->rf_blkno);

		rb_erase(&tree->rf_node, root);
		ocfs2_free_refcount_tree(tree);
	}
}
