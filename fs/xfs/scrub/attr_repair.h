// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_ATTR_REPAIR_H__
#define __XFS_SCRUB_ATTR_REPAIR_H__

struct xrep_tempexch;

int xrep_xattr_swap(struct xfs_scrub *sc, struct xrep_tempexch *tx);
int xrep_xattr_reset_fork(struct xfs_scrub *sc);
int xrep_xattr_reset_tempfile_fork(struct xfs_scrub *sc);

#endif /* __XFS_SCRUB_ATTR_REPAIR_H__ */
