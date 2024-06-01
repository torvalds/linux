// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_da_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"

/* Calculate the disk space required to add a parent pointer. */
unsigned int
xfs_parent_calc_space_res(
	struct xfs_mount	*mp,
	unsigned int		namelen)
{
	/*
	 * Parent pointers are always the first attr in an attr tree, and never
	 * larger than a block
	 */
	return XFS_DAENTER_SPACE_RES(mp, XFS_ATTR_FORK) +
	       XFS_NEXTENTADD_SPACE_RES(mp, namelen, XFS_ATTR_FORK);
}

unsigned int
xfs_create_space_res(
	struct xfs_mount	*mp,
	unsigned int		namelen)
{
	unsigned int		ret;

	ret = XFS_IALLOC_SPACE_RES(mp) + XFS_DIRENTER_SPACE_RES(mp, namelen);
	if (xfs_has_parent(mp))
		ret += xfs_parent_calc_space_res(mp, namelen);

	return ret;
}

unsigned int
xfs_mkdir_space_res(
	struct xfs_mount	*mp,
	unsigned int		namelen)
{
	return xfs_create_space_res(mp, namelen);
}

unsigned int
xfs_link_space_res(
	struct xfs_mount	*mp,
	unsigned int		namelen)
{
	unsigned int		ret;

	ret = XFS_DIRENTER_SPACE_RES(mp, namelen);
	if (xfs_has_parent(mp))
		ret += xfs_parent_calc_space_res(mp, namelen);

	return ret;
}

unsigned int
xfs_symlink_space_res(
	struct xfs_mount	*mp,
	unsigned int		namelen,
	unsigned int		fsblocks)
{
	unsigned int		ret;

	ret = XFS_IALLOC_SPACE_RES(mp) + XFS_DIRENTER_SPACE_RES(mp, namelen) +
			fsblocks;

	if (xfs_has_parent(mp))
		ret += xfs_parent_calc_space_res(mp, namelen);

	return ret;
}

unsigned int
xfs_remove_space_res(
	struct xfs_mount	*mp,
	unsigned int		namelen)
{
	unsigned int		ret = XFS_DIRREMOVE_SPACE_RES(mp);

	if (xfs_has_parent(mp))
		ret += xfs_parent_calc_space_res(mp, namelen);

	return ret;
}

unsigned int
xfs_rename_space_res(
	struct xfs_mount	*mp,
	unsigned int		src_namelen,
	bool			target_exists,
	unsigned int		target_namelen,
	bool			has_whiteout)
{
	unsigned int		ret;

	ret = XFS_DIRREMOVE_SPACE_RES(mp) +
			XFS_DIRENTER_SPACE_RES(mp, target_namelen);

	if (xfs_has_parent(mp)) {
		if (has_whiteout)
			ret += xfs_parent_calc_space_res(mp, src_namelen);
		ret += 2 * xfs_parent_calc_space_res(mp, target_namelen);
	}

	if (target_exists)
		ret += xfs_parent_calc_space_res(mp, target_namelen);

	return ret;
}
