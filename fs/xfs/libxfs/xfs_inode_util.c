// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_inode_util.h"

uint16_t
xfs_flags2diflags(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	/* can't set PREALLOC this way, just preserve it */
	uint16_t		di_flags =
		(ip->i_diflags & XFS_DIFLAG_PREALLOC);

	if (xflags & FS_XFLAG_IMMUTABLE)
		di_flags |= XFS_DIFLAG_IMMUTABLE;
	if (xflags & FS_XFLAG_APPEND)
		di_flags |= XFS_DIFLAG_APPEND;
	if (xflags & FS_XFLAG_SYNC)
		di_flags |= XFS_DIFLAG_SYNC;
	if (xflags & FS_XFLAG_NOATIME)
		di_flags |= XFS_DIFLAG_NOATIME;
	if (xflags & FS_XFLAG_NODUMP)
		di_flags |= XFS_DIFLAG_NODUMP;
	if (xflags & FS_XFLAG_NODEFRAG)
		di_flags |= XFS_DIFLAG_NODEFRAG;
	if (xflags & FS_XFLAG_FILESTREAM)
		di_flags |= XFS_DIFLAG_FILESTREAM;
	if (S_ISDIR(VFS_I(ip)->i_mode)) {
		if (xflags & FS_XFLAG_RTINHERIT)
			di_flags |= XFS_DIFLAG_RTINHERIT;
		if (xflags & FS_XFLAG_NOSYMLINKS)
			di_flags |= XFS_DIFLAG_NOSYMLINKS;
		if (xflags & FS_XFLAG_EXTSZINHERIT)
			di_flags |= XFS_DIFLAG_EXTSZINHERIT;
		if (xflags & FS_XFLAG_PROJINHERIT)
			di_flags |= XFS_DIFLAG_PROJINHERIT;
	} else if (S_ISREG(VFS_I(ip)->i_mode)) {
		if (xflags & FS_XFLAG_REALTIME)
			di_flags |= XFS_DIFLAG_REALTIME;
		if (xflags & FS_XFLAG_EXTSIZE)
			di_flags |= XFS_DIFLAG_EXTSIZE;
	}

	return di_flags;
}

uint64_t
xfs_flags2diflags2(
	struct xfs_inode	*ip,
	unsigned int		xflags)
{
	uint64_t		di_flags2 =
		(ip->i_diflags2 & (XFS_DIFLAG2_REFLINK |
				   XFS_DIFLAG2_BIGTIME |
				   XFS_DIFLAG2_NREXT64));

	if (xflags & FS_XFLAG_DAX)
		di_flags2 |= XFS_DIFLAG2_DAX;
	if (xflags & FS_XFLAG_COWEXTSIZE)
		di_flags2 |= XFS_DIFLAG2_COWEXTSIZE;

	return di_flags2;
}

uint32_t
xfs_ip2xflags(
	struct xfs_inode	*ip)
{
	uint32_t		flags = 0;

	if (ip->i_diflags & XFS_DIFLAG_ANY) {
		if (ip->i_diflags & XFS_DIFLAG_REALTIME)
			flags |= FS_XFLAG_REALTIME;
		if (ip->i_diflags & XFS_DIFLAG_PREALLOC)
			flags |= FS_XFLAG_PREALLOC;
		if (ip->i_diflags & XFS_DIFLAG_IMMUTABLE)
			flags |= FS_XFLAG_IMMUTABLE;
		if (ip->i_diflags & XFS_DIFLAG_APPEND)
			flags |= FS_XFLAG_APPEND;
		if (ip->i_diflags & XFS_DIFLAG_SYNC)
			flags |= FS_XFLAG_SYNC;
		if (ip->i_diflags & XFS_DIFLAG_NOATIME)
			flags |= FS_XFLAG_NOATIME;
		if (ip->i_diflags & XFS_DIFLAG_NODUMP)
			flags |= FS_XFLAG_NODUMP;
		if (ip->i_diflags & XFS_DIFLAG_RTINHERIT)
			flags |= FS_XFLAG_RTINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_PROJINHERIT)
			flags |= FS_XFLAG_PROJINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_NOSYMLINKS)
			flags |= FS_XFLAG_NOSYMLINKS;
		if (ip->i_diflags & XFS_DIFLAG_EXTSIZE)
			flags |= FS_XFLAG_EXTSIZE;
		if (ip->i_diflags & XFS_DIFLAG_EXTSZINHERIT)
			flags |= FS_XFLAG_EXTSZINHERIT;
		if (ip->i_diflags & XFS_DIFLAG_NODEFRAG)
			flags |= FS_XFLAG_NODEFRAG;
		if (ip->i_diflags & XFS_DIFLAG_FILESTREAM)
			flags |= FS_XFLAG_FILESTREAM;
	}

	if (ip->i_diflags2 & XFS_DIFLAG2_ANY) {
		if (ip->i_diflags2 & XFS_DIFLAG2_DAX)
			flags |= FS_XFLAG_DAX;
		if (ip->i_diflags2 & XFS_DIFLAG2_COWEXTSIZE)
			flags |= FS_XFLAG_COWEXTSIZE;
	}

	if (xfs_inode_has_attr_fork(ip))
		flags |= FS_XFLAG_HASATTR;
	return flags;
}

prid_t
xfs_get_initial_prid(struct xfs_inode *dp)
{
	if (dp->i_diflags & XFS_DIFLAG_PROJINHERIT)
		return dp->i_projid;

	/* Assign to the root project by default. */
	return 0;
}
