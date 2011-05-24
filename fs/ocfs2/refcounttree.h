/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * refcounttree.h
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
#ifndef OCFS2_REFCOUNTTREE_H
#define OCFS2_REFCOUNTTREE_H

struct ocfs2_refcount_tree {
	struct rb_node rf_node;
	u64 rf_blkno;
	u32 rf_generation;
	struct kref rf_getcnt;
	struct rw_semaphore rf_sem;
	struct ocfs2_lock_res rf_lockres;
	int rf_removed;

	/* the following 4 fields are used by caching_info. */
	spinlock_t rf_lock;
	struct ocfs2_caching_info rf_ci;
	struct mutex rf_io_mutex;
	struct super_block *rf_sb;
};

void ocfs2_purge_refcount_trees(struct ocfs2_super *osb);
int ocfs2_lock_refcount_tree(struct ocfs2_super *osb, u64 ref_blkno, int rw,
			     struct ocfs2_refcount_tree **tree,
			     struct buffer_head **ref_bh);
void ocfs2_unlock_refcount_tree(struct ocfs2_super *osb,
				struct ocfs2_refcount_tree *tree,
				int rw);

int ocfs2_decrease_refcount(struct inode *inode,
			    handle_t *handle, u32 cpos, u32 len,
			    struct ocfs2_alloc_context *meta_ac,
			    struct ocfs2_cached_dealloc_ctxt *dealloc,
			    int delete);
int ocfs2_prepare_refcount_change_for_del(struct inode *inode,
					  u64 refcount_loc,
					  u64 phys_blkno,
					  u32 clusters,
					  int *credits,
					  int *ref_blocks);
int ocfs2_refcount_cow(struct inode *inode,
		       struct file *filep, struct buffer_head *di_bh,
		       u32 cpos, u32 write_len, u32 max_cpos);

typedef int (ocfs2_post_refcount_func)(struct inode *inode,
				       handle_t *handle,
				       void *para);
/*
 * Some refcount caller need to do more work after we modify the data b-tree
 * during refcount operation(including CoW and add refcount flag), and make the
 * transaction complete. So it must give us this structure so that we can do it
 * within our transaction.
 *
 */
struct ocfs2_post_refcount {
	int credits;			/* credits it need for journal. */
	ocfs2_post_refcount_func *func;	/* real function. */
	void *para;
};

int ocfs2_refcounted_xattr_delete_need(struct inode *inode,
				       struct ocfs2_caching_info *ref_ci,
				       struct buffer_head *ref_root_bh,
				       struct ocfs2_xattr_value_root *xv,
				       int *meta_add, int *credits);
int ocfs2_refcount_cow_xattr(struct inode *inode,
			     struct ocfs2_dinode *di,
			     struct ocfs2_xattr_value_buf *vb,
			     struct ocfs2_refcount_tree *ref_tree,
			     struct buffer_head *ref_root_bh,
			     u32 cpos, u32 write_len,
			     struct ocfs2_post_refcount *post);
int ocfs2_duplicate_clusters_by_page(handle_t *handle,
				     struct file *file,
				     u32 cpos, u32 old_cluster,
				     u32 new_cluster, u32 new_len);
int ocfs2_duplicate_clusters_by_jbd(handle_t *handle,
				    struct file *file,
				    u32 cpos, u32 old_cluster,
				    u32 new_cluster, u32 new_len);
int ocfs2_cow_sync_writeback(struct super_block *sb,
			     struct inode *inode,
			     u32 cpos, u32 num_clusters);
int ocfs2_add_refcount_flag(struct inode *inode,
			    struct ocfs2_extent_tree *data_et,
			    struct ocfs2_caching_info *ref_ci,
			    struct buffer_head *ref_root_bh,
			    u32 cpos, u32 p_cluster, u32 num_clusters,
			    struct ocfs2_cached_dealloc_ctxt *dealloc,
			    struct ocfs2_post_refcount *post);
int ocfs2_remove_refcount_tree(struct inode *inode, struct buffer_head *di_bh);
int ocfs2_try_remove_refcount_tree(struct inode *inode,
				   struct buffer_head *di_bh);
int ocfs2_increase_refcount(handle_t *handle,
			    struct ocfs2_caching_info *ci,
			    struct buffer_head *ref_root_bh,
			    u64 cpos, u32 len,
			    struct ocfs2_alloc_context *meta_ac,
			    struct ocfs2_cached_dealloc_ctxt *dealloc);
int ocfs2_reflink_ioctl(struct inode *inode,
			const char __user *oldname,
			const char __user *newname,
			bool preserve);
#endif /* OCFS2_REFCOUNTTREE_H */
