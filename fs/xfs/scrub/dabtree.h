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
#ifndef __XFS_SCRUB_DABTREE_H__
#define __XFS_SCRUB_DABTREE_H__

/* dir/attr btree */

struct xfs_scrub_da_btree {
	struct xfs_da_args		dargs;
	xfs_dahash_t			hashes[XFS_DA_NODE_MAXDEPTH];
	int				maxrecs[XFS_DA_NODE_MAXDEPTH];
	struct xfs_da_state		*state;
	struct xfs_scrub_context	*sc;
	void				*private;

	/*
	 * Lowest and highest directory block address in which we expect
	 * to find dir/attr btree node blocks.  For a directory this
	 * (presumably) means between LEAF_OFFSET and FREE_OFFSET; for
	 * attributes there is no limit.
	 */
	xfs_dablk_t			lowest;
	xfs_dablk_t			highest;

	int				tree_level;
};

typedef int (*xfs_scrub_da_btree_rec_fn)(struct xfs_scrub_da_btree *ds,
		int level, void *rec);

/* Check for da btree operation errors. */
bool xfs_scrub_da_process_error(struct xfs_scrub_da_btree *ds, int level, int *error);

/* Check for da btree corruption. */
void xfs_scrub_da_set_corrupt(struct xfs_scrub_da_btree *ds, int level);

int xfs_scrub_da_btree_hash(struct xfs_scrub_da_btree *ds, int level,
			    __be32 *hashp);
int xfs_scrub_da_btree(struct xfs_scrub_context *sc, int whichfork,
		       xfs_scrub_da_btree_rec_fn scrub_fn, void *private);

#endif /* __XFS_SCRUB_DABTREE_H__ */
