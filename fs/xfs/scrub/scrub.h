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
#ifndef __XFS_SCRUB_SCRUB_H__
#define __XFS_SCRUB_SCRUB_H__

struct xfs_scrub_context;

struct xfs_scrub_meta_ops {
	/* Acquire whatever resources are needed for the operation. */
	int		(*setup)(struct xfs_scrub_context *,
				 struct xfs_inode *);

	/* Examine metadata for errors. */
	int		(*scrub)(struct xfs_scrub_context *);

	/* Decide if we even have this piece of metadata. */
	bool		(*has)(struct xfs_sb *);
};

struct xfs_scrub_context {
	/* General scrub state. */
	struct xfs_mount		*mp;
	struct xfs_scrub_metadata	*sm;
	const struct xfs_scrub_meta_ops	*ops;
	struct xfs_trans		*tp;
	struct xfs_inode		*ip;
	bool				try_harder;
};

/* Metadata scrubbers */

#endif	/* __XFS_SCRUB_SCRUB_H__ */
