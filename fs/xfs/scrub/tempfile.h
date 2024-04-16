// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2021-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_TEMPFILE_H__
#define __XFS_SCRUB_TEMPFILE_H__

#ifdef CONFIG_XFS_ONLINE_REPAIR
int xrep_tempfile_create(struct xfs_scrub *sc, uint16_t mode);
void xrep_tempfile_rele(struct xfs_scrub *sc);

bool xrep_tempfile_iolock_nowait(struct xfs_scrub *sc);
int xrep_tempfile_iolock_polled(struct xfs_scrub *sc);
void xrep_tempfile_iounlock(struct xfs_scrub *sc);

void xrep_tempfile_ilock(struct xfs_scrub *sc);
bool xrep_tempfile_ilock_nowait(struct xfs_scrub *sc);
void xrep_tempfile_iunlock(struct xfs_scrub *sc);
#else
static inline void xrep_tempfile_iolock_both(struct xfs_scrub *sc)
{
	xchk_ilock(sc, XFS_IOLOCK_EXCL);
}
# define xrep_tempfile_rele(sc)
#endif /* CONFIG_XFS_ONLINE_REPAIR */

#endif /* __XFS_SCRUB_TEMPFILE_H__ */
