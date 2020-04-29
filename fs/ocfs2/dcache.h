/* SPDX-License-Identifier: GPL-2.0-or-later */
/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * dcache.h
 *
 * Function prototypes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 */

#ifndef OCFS2_DCACHE_H
#define OCFS2_DCACHE_H

extern const struct dentry_operations ocfs2_dentry_ops;

struct ocfs2_dentry_lock {
	unsigned int		dl_count;
	u64			dl_parent_blkno;

	/*
	 * The ocfs2_dentry_lock keeps an inode reference until
	 * dl_lockres has been destroyed. This is usually done in
	 * ->d_iput() anyway, so there should be minimal impact.
	 */
	struct inode		*dl_inode;
	struct ocfs2_lock_res	dl_lockres;
};

int ocfs2_dentry_attach_lock(struct dentry *dentry, struct inode *inode,
			     u64 parent_blkno);

void ocfs2_dentry_lock_put(struct ocfs2_super *osb,
			   struct ocfs2_dentry_lock *dl);

struct dentry *ocfs2_find_local_alias(struct inode *inode, u64 parent_blkno,
				      int skip_unhashed);

void ocfs2_dentry_move(struct dentry *dentry, struct dentry *target,
		       struct inode *old_dir, struct inode *new_dir);

extern spinlock_t dentry_attach_lock;
void ocfs2_dentry_attach_gen(struct dentry *dentry);

#endif /* OCFS2_DCACHE_H */
