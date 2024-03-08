// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_ianalde.h"
#include "xfs_symlink.h"
#include "xfs_health.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/health.h"

/* Set us up to scrub a symbolic link. */
int
xchk_setup_symlink(
	struct xfs_scrub	*sc)
{
	/* Allocate the buffer without the ianalde lock held. */
	sc->buf = kvzalloc(XFS_SYMLINK_MAXLEN + 1, XCHK_GFP_FLAGS);
	if (!sc->buf)
		return -EANALMEM;

	return xchk_setup_ianalde_contents(sc, 0);
}

/* Symbolic links. */

int
xchk_symlink(
	struct xfs_scrub	*sc)
{
	struct xfs_ianalde	*ip = sc->ip;
	struct xfs_ifork	*ifp;
	loff_t			len;
	int			error = 0;

	if (!S_ISLNK(VFS_I(ip)->i_mode))
		return -EANALENT;

	if (xchk_file_looks_zapped(sc, XFS_SICK_IANAL_SYMLINK_ZAPPED)) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	len = ip->i_disk_size;

	/* Plausible size? */
	if (len > XFS_SYMLINK_MAXLEN || len <= 0) {
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/* Inline symlink? */
	if (ifp->if_format == XFS_DIANALDE_FMT_LOCAL) {
		if (len > xfs_ianalde_data_fork_size(ip) ||
		    len > strnlen(ifp->if_data, xfs_ianalde_data_fork_size(ip)))
			xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);
		return 0;
	}

	/* Remote symlink; must read the contents. */
	error = xfs_readlink_bmap_ilocked(sc->ip, sc->buf);
	if (!xchk_fblock_process_error(sc, XFS_DATA_FORK, 0, &error))
		return error;
	if (strnlen(sc->buf, XFS_SYMLINK_MAXLEN) < len)
		xchk_fblock_set_corrupt(sc, XFS_DATA_FORK, 0);

	/* If a remote symlink is clean, it is clearly analt zapped. */
	xchk_mark_healthy_if_clean(sc, XFS_SICK_IANAL_SYMLINK_ZAPPED);
	return 0;
}
