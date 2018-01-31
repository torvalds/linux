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
#ifndef __XFS_SCRUB_BTREE_H__
#define __XFS_SCRUB_BTREE_H__

/* btree scrub */

/* Check for btree operation errors. */
bool xfs_scrub_btree_process_error(struct xfs_scrub_context *sc,
		struct xfs_btree_cur *cur, int level, int *error);

/* Check for btree xref operation errors. */
bool xfs_scrub_btree_xref_process_error(struct xfs_scrub_context *sc,
				struct xfs_btree_cur *cur, int level,
				int *error);

/* Check for btree corruption. */
void xfs_scrub_btree_set_corrupt(struct xfs_scrub_context *sc,
		struct xfs_btree_cur *cur, int level);

/* Check for btree xref discrepancies. */
void xfs_scrub_btree_xref_set_corrupt(struct xfs_scrub_context *sc,
		struct xfs_btree_cur *cur, int level);

struct xfs_scrub_btree;
typedef int (*xfs_scrub_btree_rec_fn)(
	struct xfs_scrub_btree	*bs,
	union xfs_btree_rec	*rec);

struct xfs_scrub_btree {
	/* caller-provided scrub state */
	struct xfs_scrub_context	*sc;
	struct xfs_btree_cur		*cur;
	xfs_scrub_btree_rec_fn		scrub_rec;
	struct xfs_owner_info		*oinfo;
	void				*private;

	/* internal scrub state */
	union xfs_btree_rec		lastrec;
	bool				firstrec;
	union xfs_btree_key		lastkey[XFS_BTREE_MAXLEVELS];
	bool				firstkey[XFS_BTREE_MAXLEVELS];
	struct list_head		to_check;
};
int xfs_scrub_btree(struct xfs_scrub_context *sc, struct xfs_btree_cur *cur,
		    xfs_scrub_btree_rec_fn scrub_fn,
		    struct xfs_owner_info *oinfo, void *private);

#endif /* __XFS_SCRUB_BTREE_H__ */
