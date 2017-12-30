/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_inode_fork.h"
#include "xfs_symlink.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"

/* Set us up to scrub a symbolic link. */
int
xfs_scrub_setup_symlink(
	struct xfs_scrub_context	*sc,
	struct xfs_inode		*ip)
{
	/* Allocate the buffer without the inode lock held. */
	sc->buf = kmem_zalloc_large(XFS_SYMLINK_MAXLEN + 1, KM_SLEEP);
	if (!sc->buf)
		return -ENOMEM;

	return xfs_scrub_setup_inode_contents(sc, ip, 0);
}

/* Symbolic links. */

int
xfs_scrub_symlink(
	struct xfs_scrub_context	*sc)
{
	struct xfs_inode		*ip = sc->ip;
	struct xfs_ifork		*ifp;
	loff_t				len;
	int				error = 0;

	if (!S_ISLNK(VFS_I(ip)->i_mode))
		return -ENOENT;
	ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
	len = ip->i_d.di_size;

	/* Plausible size? */
	if (len > XFS_SYMLINK_MAXLEN || len <= 0) {
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out;
	}

	/* Inline symlink? */
	if (ifp->if_flags & XFS_IFINLINE) {
		if (len > XFS_IFORK_DSIZE(ip) ||
		    len > strnlen(ifp->if_u1.if_data, XFS_IFORK_DSIZE(ip)))
			xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		goto out;
	}

	/* Remote symlink; must read the contents. */
	error = xfs_readlink_bmap_ilocked(sc->ip, sc->buf);
	if (!xfs_scrub_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		goto out;
	if (strnlen(sc->buf, XFS_SYMLINK_MAXLEN) < len)
		xfs_scrub_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
out:
	return error;
}
