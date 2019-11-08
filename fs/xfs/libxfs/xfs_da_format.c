// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2002,2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"

/*
 * Directory data block operations
 */

static uint8_t
xfs_dir2_data_get_ftype(
	struct xfs_dir2_data_entry *dep)
{
	return XFS_DIR3_FT_UNKNOWN;
}

static void
xfs_dir2_data_put_ftype(
	struct xfs_dir2_data_entry *dep,
	uint8_t			ftype)
{
	ASSERT(ftype < XFS_DIR3_FT_MAX);
}

static uint8_t
xfs_dir3_data_get_ftype(
	struct xfs_dir2_data_entry *dep)
{
	uint8_t		ftype = dep->name[dep->namelen];

	if (ftype >= XFS_DIR3_FT_MAX)
		return XFS_DIR3_FT_UNKNOWN;
	return ftype;
}

static void
xfs_dir3_data_put_ftype(
	struct xfs_dir2_data_entry *dep,
	uint8_t			type)
{
	ASSERT(type < XFS_DIR3_FT_MAX);
	ASSERT(dep->namelen != 0);

	dep->name[dep->namelen] = type;
}

static struct xfs_dir2_data_free *
xfs_dir2_data_bestfree_p(struct xfs_dir2_data_hdr *hdr)
{
	return hdr->bestfree;
}

static struct xfs_dir2_data_free *
xfs_dir3_data_bestfree_p(struct xfs_dir2_data_hdr *hdr)
{
	return ((struct xfs_dir3_data_hdr *)hdr)->best_free;
}

static const struct xfs_dir_ops xfs_dir2_ops = {
	.data_get_ftype = xfs_dir2_data_get_ftype,
	.data_put_ftype = xfs_dir2_data_put_ftype,
	.data_bestfree_p = xfs_dir2_data_bestfree_p,
};

static const struct xfs_dir_ops xfs_dir2_ftype_ops = {
	.data_get_ftype = xfs_dir3_data_get_ftype,
	.data_put_ftype = xfs_dir3_data_put_ftype,
	.data_bestfree_p = xfs_dir2_data_bestfree_p,
};

static const struct xfs_dir_ops xfs_dir3_ops = {
	.data_get_ftype = xfs_dir3_data_get_ftype,
	.data_put_ftype = xfs_dir3_data_put_ftype,
	.data_bestfree_p = xfs_dir3_data_bestfree_p,
};

/*
 * Return the ops structure according to the current config.  If we are passed
 * an inode, then that overrides the default config we use which is based on
 * feature bits.
 */
const struct xfs_dir_ops *
xfs_dir_get_ops(
	struct xfs_mount	*mp,
	struct xfs_inode	*dp)
{
	if (dp)
		return dp->d_ops;
	if (mp->m_dir_inode_ops)
		return mp->m_dir_inode_ops;
	if (xfs_sb_version_hascrc(&mp->m_sb))
		return &xfs_dir3_ops;
	if (xfs_sb_version_hasftype(&mp->m_sb))
		return &xfs_dir2_ftype_ops;
	return &xfs_dir2_ops;
}
