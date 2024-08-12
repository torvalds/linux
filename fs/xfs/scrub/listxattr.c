// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2022-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_attr_sf.h"
#include "xfs_trans.h"
#include "scrub/scrub.h"
#include "scrub/bitmap.h"
#include "scrub/dab_bitmap.h"
#include "scrub/listxattr.h"

/* Call a function for every entry in a shortform xattr structure. */
STATIC int
xchk_xattr_walk_sf(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	xchk_xattr_fn			attr_fn,
	void				*priv)
{
	struct xfs_attr_sf_hdr		*hdr = ip->i_af.if_data;
	struct xfs_attr_sf_entry	*sfe;
	unsigned int			i;
	int				error;

	sfe = xfs_attr_sf_firstentry(hdr);
	for (i = 0; i < hdr->count; i++) {
		error = attr_fn(sc, ip, sfe->flags, sfe->nameval, sfe->namelen,
				&sfe->nameval[sfe->namelen], sfe->valuelen,
				priv);
		if (error)
			return error;

		sfe = xfs_attr_sf_nextentry(sfe);
	}

	return 0;
}

/* Call a function for every entry in this xattr leaf block. */
STATIC int
xchk_xattr_walk_leaf_entries(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	xchk_xattr_fn			attr_fn,
	struct xfs_buf			*bp,
	void				*priv)
{
	struct xfs_attr3_icleaf_hdr	ichdr;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_attr_leafblock	*leaf = bp->b_addr;
	struct xfs_attr_leaf_entry	*entry;
	unsigned int			i;
	int				error;

	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &ichdr, leaf);
	entry = xfs_attr3_leaf_entryp(leaf);

	for (i = 0; i < ichdr.count; entry++, i++) {
		void			*value;
		unsigned char		*name;
		unsigned int		namelen, valuelen;

		if (entry->flags & XFS_ATTR_LOCAL) {
			struct xfs_attr_leaf_name_local		*name_loc;

			name_loc = xfs_attr3_leaf_name_local(leaf, i);
			name = name_loc->nameval;
			namelen = name_loc->namelen;
			value = &name_loc->nameval[name_loc->namelen];
			valuelen = be16_to_cpu(name_loc->valuelen);
		} else {
			struct xfs_attr_leaf_name_remote	*name_rmt;

			name_rmt = xfs_attr3_leaf_name_remote(leaf, i);
			name = name_rmt->name;
			namelen = name_rmt->namelen;
			value = NULL;
			valuelen = be32_to_cpu(name_rmt->valuelen);
		}

		error = attr_fn(sc, ip, entry->flags, name, namelen, value,
				valuelen, priv);
		if (error)
			return error;

	}

	return 0;
}

/*
 * Call a function for every entry in a leaf-format xattr structure.  Avoid
 * memory allocations for the loop detector since there's only one block.
 */
STATIC int
xchk_xattr_walk_leaf(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	xchk_xattr_fn			attr_fn,
	void				*priv)
{
	struct xfs_buf			*leaf_bp;
	int				error;

	error = xfs_attr3_leaf_read(sc->tp, ip, ip->i_ino, 0, &leaf_bp);
	if (error)
		return error;

	error = xchk_xattr_walk_leaf_entries(sc, ip, attr_fn, leaf_bp, priv);
	xfs_trans_brelse(sc->tp, leaf_bp);
	return error;
}

/* Find the leftmost leaf in the xattr dabtree. */
STATIC int
xchk_xattr_find_leftmost_leaf(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	struct xdab_bitmap		*seen_dablks,
	struct xfs_buf			**leaf_bpp)
{
	struct xfs_da3_icnode_hdr	nodehdr;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_trans		*tp = sc->tp;
	struct xfs_da_intnode		*node;
	struct xfs_da_node_entry	*btree;
	struct xfs_buf			*bp;
	xfs_failaddr_t			fa;
	xfs_dablk_t			blkno = 0;
	unsigned int			expected_level = 0;
	int				error;

	for (;;) {
		xfs_extlen_t		len = 1;
		uint16_t		magic;

		/* Make sure we haven't seen this new block already. */
		if (xdab_bitmap_test(seen_dablks, blkno, &len))
			return -EFSCORRUPTED;

		error = xfs_da3_node_read(tp, ip, blkno, &bp, XFS_ATTR_FORK);
		if (error)
			return error;

		node = bp->b_addr;
		magic = be16_to_cpu(node->hdr.info.magic);
		if (magic == XFS_ATTR_LEAF_MAGIC ||
		    magic == XFS_ATTR3_LEAF_MAGIC)
			break;

		error = -EFSCORRUPTED;
		if (magic != XFS_DA_NODE_MAGIC &&
		    magic != XFS_DA3_NODE_MAGIC)
			goto out_buf;

		fa = xfs_da3_node_header_check(bp, ip->i_ino);
		if (fa)
			goto out_buf;

		xfs_da3_node_hdr_from_disk(mp, &nodehdr, node);

		if (nodehdr.count == 0 || nodehdr.level >= XFS_DA_NODE_MAXDEPTH)
			goto out_buf;

		/* Check the level from the root node. */
		if (blkno == 0)
			expected_level = nodehdr.level - 1;
		else if (expected_level != nodehdr.level)
			goto out_buf;
		else
			expected_level--;

		/* Remember that we've seen this node. */
		error = xdab_bitmap_set(seen_dablks, blkno, 1);
		if (error)
			goto out_buf;

		/* Find the next level towards the leaves of the dabtree. */
		btree = nodehdr.btree;
		blkno = be32_to_cpu(btree->before);
		xfs_trans_brelse(tp, bp);
	}

	error = -EFSCORRUPTED;
	fa = xfs_attr3_leaf_header_check(bp, ip->i_ino);
	if (fa)
		goto out_buf;

	if (expected_level != 0)
		goto out_buf;

	/* Remember that we've seen this leaf. */
	error = xdab_bitmap_set(seen_dablks, blkno, 1);
	if (error)
		goto out_buf;

	*leaf_bpp = bp;
	return 0;

out_buf:
	xfs_trans_brelse(tp, bp);
	return error;
}

/* Call a function for every entry in a node-format xattr structure. */
STATIC int
xchk_xattr_walk_node(
	struct xfs_scrub		*sc,
	struct xfs_inode		*ip,
	xchk_xattr_fn			attr_fn,
	xchk_xattrleaf_fn		leaf_fn,
	void				*priv)
{
	struct xfs_attr3_icleaf_hdr	leafhdr;
	struct xdab_bitmap		seen_dablks;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_attr_leafblock	*leaf;
	struct xfs_buf			*leaf_bp;
	int				error;

	xdab_bitmap_init(&seen_dablks);

	error = xchk_xattr_find_leftmost_leaf(sc, ip, &seen_dablks, &leaf_bp);
	if (error)
		goto out_bitmap;

	for (;;) {
		xfs_extlen_t	len;

		error = xchk_xattr_walk_leaf_entries(sc, ip, attr_fn, leaf_bp,
				priv);
		if (error)
			goto out_leaf;

		/* Find the right sibling of this leaf block. */
		leaf = leaf_bp->b_addr;
		xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &leafhdr, leaf);
		if (leafhdr.forw == 0)
			goto out_leaf;

		xfs_trans_brelse(sc->tp, leaf_bp);

		if (leaf_fn) {
			error = leaf_fn(sc, priv);
			if (error)
				goto out_bitmap;
		}

		/* Make sure we haven't seen this new leaf already. */
		len = 1;
		if (xdab_bitmap_test(&seen_dablks, leafhdr.forw, &len)) {
			error = -EFSCORRUPTED;
			goto out_bitmap;
		}

		error = xfs_attr3_leaf_read(sc->tp, ip, ip->i_ino,
				leafhdr.forw, &leaf_bp);
		if (error)
			goto out_bitmap;

		/* Remember that we've seen this new leaf. */
		error = xdab_bitmap_set(&seen_dablks, leafhdr.forw, 1);
		if (error)
			goto out_leaf;
	}

out_leaf:
	xfs_trans_brelse(sc->tp, leaf_bp);
out_bitmap:
	xdab_bitmap_destroy(&seen_dablks);
	return error;
}

/*
 * Call a function for every extended attribute in a file.
 *
 * Callers must hold the ILOCK.  No validation or cursor restarts allowed.
 * Returns -EFSCORRUPTED on any problem, including loops in the dabtree.
 */
int
xchk_xattr_walk(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	xchk_xattr_fn		attr_fn,
	xchk_xattrleaf_fn	leaf_fn,
	void			*priv)
{
	int			error;

	xfs_assert_ilocked(ip, XFS_ILOCK_SHARED | XFS_ILOCK_EXCL);

	if (!xfs_inode_hasattr(ip))
		return 0;

	if (ip->i_af.if_format == XFS_DINODE_FMT_LOCAL)
		return xchk_xattr_walk_sf(sc, ip, attr_fn, priv);

	/* attr functions require that the attr fork is loaded */
	error = xfs_iread_extents(sc->tp, ip, XFS_ATTR_FORK);
	if (error)
		return error;

	if (xfs_attr_is_leaf(ip))
		return xchk_xattr_walk_leaf(sc, ip, attr_fn, priv);

	return xchk_xattr_walk_node(sc, ip, attr_fn, leaf_fn, priv);
}
