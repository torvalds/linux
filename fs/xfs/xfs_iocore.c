/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir2.h"
#include "xfs_dfrag.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_itable.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_bmap.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_quota.h"
#include "xfs_trans_space.h"
#include "xfs_iomap.h"

void
xfs_iocore_inode_reinit(
	xfs_inode_t	*ip)
{
	xfs_iocore_t	*io = &ip->i_iocore;

	io->io_flags = 0;
	if (ip->i_d.di_flags & XFS_DIFLAG_REALTIME)
		io->io_flags |= XFS_IOCORE_RT;
	io->io_dmevmask = ip->i_d.di_dmevmask;
	io->io_dmstate = ip->i_d.di_dmstate;
}

void
xfs_iocore_inode_init(
	xfs_inode_t	*ip)
{
	xfs_iocore_t	*io = &ip->i_iocore;
	xfs_mount_t	*mp = ip->i_mount;

	io->io_mount = mp;
#ifdef DEBUG
	io->io_lock = &ip->i_lock;
	io->io_iolock = &ip->i_iolock;
#endif

	io->io_obj = (void *)ip;

	xfs_iocore_inode_reinit(ip);
}
