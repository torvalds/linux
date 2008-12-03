/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_btree.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_quota.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_rw.h"
#include "xfs_buf_item.h"
#include "xfs_log_priv.h"
#include "xfs_dir2_trace.h"
#include "xfs_extfree_item.h"
#include "xfs_acl.h"
#include "xfs_attr.h"
#include "xfs_mru_cache.h"
#include "xfs_filestream.h"
#include "xfs_fsops.h"
#include "xfs_vnodeops.h"
#include "xfs_utils.h"
#include "xfs_sync.h"


/*
 * xfs_unmount_flush implements a set of flush operation on special
 * inodes, which are needed as a separate set of operations so that
 * they can be called as part of relocation process.
 */
int
xfs_unmount_flush(
	xfs_mount_t	*mp,		/* Mount structure we are getting
					   rid of. */
	int             relocation)	/* Called from vfs relocation. */
{
	/*
	 * Release dquot that rootinode, rbmino and rsumino might be holding,
	 * flush and purge the quota inodes.
	 */
	XFS_QM_UNMOUNT(mp);

	if (mp->m_rbmip)
		IRELE(mp->m_rbmip);
	if (mp->m_rsumip)
		IRELE(mp->m_rsumip);

	return 0;
}
