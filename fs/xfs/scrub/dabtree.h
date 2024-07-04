// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#ifndef __XFS_SCRUB_DABTREE_H__
#define __XFS_SCRUB_DABTREE_H__

/* dir/attr btree */

struct xchk_da_btree {
	struct xfs_da_args	dargs;
	xfs_dahash_t		hashes[XFS_DA_NODE_MAXDEPTH];
	int			maxrecs[XFS_DA_NODE_MAXDEPTH];
	struct xfs_da_state	*state;
	struct xfs_scrub	*sc;
	void			*private;

	/*
	 * Lowest and highest directory block address in which we expect
	 * to find dir/attr btree node blocks.  For a directory this
	 * (presumably) means between LEAF_OFFSET and FREE_OFFSET; for
	 * attributes there is no limit.
	 */
	xfs_dablk_t		lowest;
	xfs_dablk_t		highest;

	int			tree_level;
};

typedef int (*xchk_da_btree_rec_fn)(struct xchk_da_btree *ds, int level);

/* Check for da btree operation errors. */
bool xchk_da_process_error(struct xchk_da_btree *ds, int level, int *error);

/* Check for da btree corruption. */
void xchk_da_set_corrupt(struct xchk_da_btree *ds, int level);
void xchk_da_set_preen(struct xchk_da_btree *ds, int level);

void xchk_da_set_preen(struct xchk_da_btree *ds, int level);

int xchk_da_btree_hash(struct xchk_da_btree *ds, int level, __be32 *hashp);
int xchk_da_btree(struct xfs_scrub *sc, int whichfork,
		xchk_da_btree_rec_fn scrub_fn, void *private);

#endif /* __XFS_SCRUB_DABTREE_H__ */
