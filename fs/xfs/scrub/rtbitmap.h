// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_RTBITMAP_H__
#define __XFS_SCRUB_RTBITMAP_H__

struct xchk_rtbitmap {
	uint64_t		rextents;
	uint64_t		rbmblocks;
	unsigned int		rextslog;
	unsigned int		resblks;
};

#ifdef CONFIG_XFS_ONLINE_REPAIR
int xrep_setup_rtbitmap(struct xfs_scrub *sc, struct xchk_rtbitmap *rtb);
#else
# define xrep_setup_rtbitmap(sc, rtb)	(0)
#endif /* CONFIG_XFS_ONLINE_REPAIR */

#endif /* __XFS_SCRUB_RTBITMAP_H__ */
