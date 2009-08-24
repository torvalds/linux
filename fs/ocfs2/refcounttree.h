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
	struct rw_semaphore rf_sem;
	struct ocfs2_lock_res rf_lockres;
	struct kref rf_getcnt;
	int rf_removed;

	/* the following 4 fields are used by caching_info. */
	struct ocfs2_caching_info rf_ci;
	spinlock_t rf_lock;
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
#endif /* OCFS2_REFCOUNTTREE_H */
