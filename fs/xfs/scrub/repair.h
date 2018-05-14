/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
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
#ifndef __XFS_SCRUB_REPAIR_H__
#define __XFS_SCRUB_REPAIR_H__

static inline int xfs_repair_notsupported(struct xfs_scrub_context *sc)
{
	return -EOPNOTSUPP;
}

#ifdef CONFIG_XFS_ONLINE_REPAIR

/* Repair helpers */

int xfs_repair_attempt(struct xfs_inode *ip, struct xfs_scrub_context *sc,
		bool *fixed);
void xfs_repair_failure(struct xfs_mount *mp);

/* Metadata repairers */

int xfs_repair_probe(struct xfs_scrub_context *sc);

#else

static inline int xfs_repair_attempt(
	struct xfs_inode		*ip,
	struct xfs_scrub_context	*sc,
	bool				*fixed)
{
	return -EOPNOTSUPP;
}

static inline void xfs_repair_failure(struct xfs_mount *mp) {}

#define xfs_repair_probe		xfs_repair_notsupported

#endif /* CONFIG_XFS_ONLINE_REPAIR */

#endif	/* __XFS_SCRUB_REPAIR_H__ */
