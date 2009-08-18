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
