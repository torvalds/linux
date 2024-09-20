// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_TEMPEXCH_H__
#define __XFS_SCRUB_TEMPEXCH_H__

#ifdef CONFIG_XFS_ONLINE_REPAIR
struct xrep_tempexch {
	struct xfs_exchmaps_req	req;
};

int xrep_tempexch_trans_reserve(struct xfs_scrub *sc, int whichfork,
		struct xrep_tempexch *ti);
int xrep_tempexch_trans_alloc(struct xfs_scrub *sc, int whichfork,
		struct xrep_tempexch *ti);

int xrep_tempexch_contents(struct xfs_scrub *sc, struct xrep_tempexch *ti);
#endif /* CONFIG_XFS_ONLINE_REPAIR */

#endif /* __XFS_SCRUB_TEMPEXCH_H__ */
