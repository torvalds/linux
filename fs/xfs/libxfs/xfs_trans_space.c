// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs_platform.h"
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
	 * A parent pointer is recorded per dirent, so an inode with N links
	 * carries N of them and the attr fork can already be in leaf or node
	 * format when one is added.  That does not affect the reservation:
	 * XFS_DAENTER_SPACE_RES covers a split at every level of a
	 * maximum-depth attr dabtree, whatever format the fork is in now.
	 *
	 * The name is a dirent name and the value is a struct xfs_parent_rec,
	 * so the leaf entry is always local and never exceeds 272 bytes.
	 * Parent pointers require V5, hence a 1k minimum block size, so the
	 * entry always stays under half a block and this needs none of the
	 * double split allowance that xfs_attr_calc_size() makes.
	 *
	 * The second term hands a byte count to a macro whose parameter counts
	 * mappings, so it asks for more extent-add allowance than the single
	 * mapping a parent pointer adds - how much more depends on the block
	 * size.  It over-reserves either way, which is why it is left alone:
	 * correcting the unit would shrink a reservation that is only generous.
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
