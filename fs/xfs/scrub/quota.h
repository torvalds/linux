// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_QUOTA_H__
#define __XFS_SCRUB_QUOTA_H__

xfs_dqtype_t xchk_quota_to_dqtype(struct xfs_scrub *sc);

/* dquot iteration code */

struct xchk_dqiter {
	struct xfs_scrub	*sc;

	/* Quota file that we're walking. */
	struct xfs_inode	*quota_ip;

	/* Cached data fork mapping for the dquot. */
	struct xfs_bmbt_irec	bmap;

	/* The next dquot to scan. */
	uint64_t		id;

	/* Quota type (user/group/project). */
	xfs_dqtype_t		dqtype;

	/* Data fork sequence number to detect stale mappings. */
	unsigned int		if_seq;
};

void xchk_dqiter_init(struct xchk_dqiter *cursor, struct xfs_scrub *sc,
		xfs_dqtype_t dqtype);
int xchk_dquot_iter(struct xchk_dqiter *cursor, struct xfs_dquot **dqpp);

#endif /* __XFS_SCRUB_QUOTA_H__ */
