// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_attr_sf.h"
#include "xfs_attr_leaf.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_dir2.h"

STATIC int
xfs_attr_shortform_compare(const void *a, const void *b)
{
	xfs_attr_sf_sort_t *sa, *sb;

	sa = (xfs_attr_sf_sort_t *)a;
	sb = (xfs_attr_sf_sort_t *)b;
	if (sa->hash < sb->hash) {
		return -1;
	} else if (sa->hash > sb->hash) {
		return 1;
	} else {
		return sa->entno - sb->entno;
	}
}

#define XFS_ISRESET_CURSOR(cursor) \
	(!((cursor)->initted) && !((cursor)->hashval) && \
	 !((cursor)->blkno) && !((cursor)->offset))
/*
 * Copy out entries of shortform attribute lists for attr_list().
 * Shortform attribute lists are not stored in hashval sorted order.
 * If the output buffer is not large enough to hold them all, then
 * we have to calculate each entries' hashvalue and sort them before
 * we can begin returning them to the user.
 */
static int
xfs_attr_shortform_list(
	struct xfs_attr_list_context	*context)
{
	struct xfs_attrlist_cursor_kern	*cursor = &context->cursor;
	struct xfs_inode		*dp = context->dp;
	struct xfs_attr_sf_sort		*sbuf, *sbp;
	struct xfs_attr_shortform	*sf;
	struct xfs_attr_sf_entry	*sfe;
	int				sbsize, nsbuf, count, i;
	int				error = 0;

	ASSERT(dp->i_afp != NULL);
	sf = (struct xfs_attr_shortform *)dp->i_afp->if_u1.if_data;
	ASSERT(sf != NULL);
	if (!sf->hdr.count)
		return 0;

	trace_xfs_attr_list_sf(context);

	/*
	 * If the buffer is large enough and the cursor is at the start,
	 * do not bother with sorting since we will return everything in
	 * one buffer and another call using the cursor won't need to be
	 * made.
	 * Note the generous fudge factor of 16 overhead bytes per entry.
	 * If bufsize is zero then put_listent must be a search function
	 * and can just scan through what we have.
	 */
	if (context->bufsize == 0 ||
	    (XFS_ISRESET_CURSOR(cursor) &&
	     (dp->i_afp->if_bytes + sf->hdr.count * 16) < context->bufsize)) {
		for (i = 0, sfe = &sf->list[0]; i < sf->hdr.count; i++) {
			if (XFS_IS_CORRUPT(context->dp->i_mount,
					   !xfs_attr_namecheck(sfe->nameval,
							       sfe->namelen)))
				return -EFSCORRUPTED;
			context->put_listent(context,
					     sfe->flags,
					     sfe->nameval,
					     (int)sfe->namelen,
					     (int)sfe->valuelen);
			/*
			 * Either search callback finished early or
			 * didn't fit it all in the buffer after all.
			 */
			if (context->seen_enough)
				break;
			sfe = xfs_attr_sf_nextentry(sfe);
		}
		trace_xfs_attr_list_sf_all(context);
		return 0;
	}

	/* do no more for a search callback */
	if (context->bufsize == 0)
		return 0;

	/*
	 * It didn't all fit, so we have to sort everything on hashval.
	 */
	sbsize = sf->hdr.count * sizeof(*sbuf);
	sbp = sbuf = kmem_alloc(sbsize, KM_NOFS);

	/*
	 * Scan the attribute list for the rest of the entries, storing
	 * the relevant info from only those that match into a buffer.
	 */
	nsbuf = 0;
	for (i = 0, sfe = &sf->list[0]; i < sf->hdr.count; i++) {
		if (unlikely(
		    ((char *)sfe < (char *)sf) ||
		    ((char *)sfe >= ((char *)sf + dp->i_afp->if_bytes)))) {
			XFS_CORRUPTION_ERROR("xfs_attr_shortform_list",
					     XFS_ERRLEVEL_LOW,
					     context->dp->i_mount, sfe,
					     sizeof(*sfe));
			kmem_free(sbuf);
			return -EFSCORRUPTED;
		}

		sbp->entno = i;
		sbp->hash = xfs_da_hashname(sfe->nameval, sfe->namelen);
		sbp->name = sfe->nameval;
		sbp->namelen = sfe->namelen;
		/* These are bytes, and both on-disk, don't endian-flip */
		sbp->valuelen = sfe->valuelen;
		sbp->flags = sfe->flags;
		sfe = xfs_attr_sf_nextentry(sfe);
		sbp++;
		nsbuf++;
	}

	/*
	 * Sort the entries on hash then entno.
	 */
	xfs_sort(sbuf, nsbuf, sizeof(*sbuf), xfs_attr_shortform_compare);

	/*
	 * Re-find our place IN THE SORTED LIST.
	 */
	count = 0;
	cursor->initted = 1;
	cursor->blkno = 0;
	for (sbp = sbuf, i = 0; i < nsbuf; i++, sbp++) {
		if (sbp->hash == cursor->hashval) {
			if (cursor->offset == count) {
				break;
			}
			count++;
		} else if (sbp->hash > cursor->hashval) {
			break;
		}
	}
	if (i == nsbuf)
		goto out;

	/*
	 * Loop putting entries into the user buffer.
	 */
	for ( ; i < nsbuf; i++, sbp++) {
		if (cursor->hashval != sbp->hash) {
			cursor->hashval = sbp->hash;
			cursor->offset = 0;
		}
		if (XFS_IS_CORRUPT(context->dp->i_mount,
				   !xfs_attr_namecheck(sbp->name,
						       sbp->namelen))) {
			error = -EFSCORRUPTED;
			goto out;
		}
		context->put_listent(context,
				     sbp->flags,
				     sbp->name,
				     sbp->namelen,
				     sbp->valuelen);
		if (context->seen_enough)
			break;
		cursor->offset++;
	}
out:
	kmem_free(sbuf);
	return error;
}

/*
 * We didn't find the block & hash mentioned in the cursor state, so
 * walk down the attr btree looking for the hash.
 */
STATIC int
xfs_attr_node_list_lookup(
	struct xfs_attr_list_context	*context,
	struct xfs_attrlist_cursor_kern	*cursor,
	struct xfs_buf			**pbp)
{
	struct xfs_da3_icnode_hdr	nodehdr;
	struct xfs_da_intnode		*node;
	struct xfs_da_node_entry	*btree;
	struct xfs_inode		*dp = context->dp;
	struct xfs_mount		*mp = dp->i_mount;
	struct xfs_trans		*tp = context->tp;
	struct xfs_buf			*bp;
	int				i;
	int				error = 0;
	unsigned int			expected_level = 0;
	uint16_t			magic;

	ASSERT(*pbp == NULL);
	cursor->blkno = 0;
	for (;;) {
		error = xfs_da3_node_read(tp, dp, cursor->blkno, &bp,
				XFS_ATTR_FORK);
		if (error)
			return error;
		node = bp->b_addr;
		magic = be16_to_cpu(node->hdr.info.magic);
		if (magic == XFS_ATTR_LEAF_MAGIC ||
		    magic == XFS_ATTR3_LEAF_MAGIC)
			break;
		if (magic != XFS_DA_NODE_MAGIC &&
		    magic != XFS_DA3_NODE_MAGIC) {
			XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp,
					node, sizeof(*node));
			goto out_corruptbuf;
		}

		xfs_da3_node_hdr_from_disk(mp, &nodehdr, node);

		/* Tree taller than we can handle; bail out! */
		if (nodehdr.level >= XFS_DA_NODE_MAXDEPTH)
			goto out_corruptbuf;

		/* Check the level from the root node. */
		if (cursor->blkno == 0)
			expected_level = nodehdr.level - 1;
		else if (expected_level != nodehdr.level)
			goto out_corruptbuf;
		else
			expected_level--;

		btree = nodehdr.btree;
		for (i = 0; i < nodehdr.count; btree++, i++) {
			if (cursor->hashval <= be32_to_cpu(btree->hashval)) {
				cursor->blkno = be32_to_cpu(btree->before);
				trace_xfs_attr_list_node_descend(context,
						btree);
				break;
			}
		}
		xfs_trans_brelse(tp, bp);

		if (i == nodehdr.count)
			return 0;

		/* We can't point back to the root. */
		if (XFS_IS_CORRUPT(mp, cursor->blkno == 0))
			return -EFSCORRUPTED;
	}

	if (expected_level != 0)
		goto out_corruptbuf;

	*pbp = bp;
	return 0;

out_corruptbuf:
	xfs_buf_mark_corrupt(bp);
	xfs_trans_brelse(tp, bp);
	return -EFSCORRUPTED;
}

STATIC int
xfs_attr_node_list(
	struct xfs_attr_list_context	*context)
{
	struct xfs_attrlist_cursor_kern	*cursor = &context->cursor;
	struct xfs_attr3_icleaf_hdr	leafhdr;
	struct xfs_attr_leafblock	*leaf;
	struct xfs_da_intnode		*node;
	struct xfs_buf			*bp;
	struct xfs_inode		*dp = context->dp;
	struct xfs_mount		*mp = dp->i_mount;
	int				error = 0;

	trace_xfs_attr_node_list(context);

	cursor->initted = 1;

	/*
	 * Do all sorts of validation on the passed-in cursor structure.
	 * If anything is amiss, ignore the cursor and look up the hashval
	 * starting from the btree root.
	 */
	bp = NULL;
	if (cursor->blkno > 0) {
		error = xfs_da3_node_read(context->tp, dp, cursor->blkno, &bp,
				XFS_ATTR_FORK);
		if ((error != 0) && (error != -EFSCORRUPTED))
			return error;
		if (bp) {
			struct xfs_attr_leaf_entry *entries;

			node = bp->b_addr;
			switch (be16_to_cpu(node->hdr.info.magic)) {
			case XFS_DA_NODE_MAGIC:
			case XFS_DA3_NODE_MAGIC:
				trace_xfs_attr_list_wrong_blk(context);
				xfs_trans_brelse(context->tp, bp);
				bp = NULL;
				break;
			case XFS_ATTR_LEAF_MAGIC:
			case XFS_ATTR3_LEAF_MAGIC:
				leaf = bp->b_addr;
				xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo,
							     &leafhdr, leaf);
				entries = xfs_attr3_leaf_entryp(leaf);
				if (cursor->hashval > be32_to_cpu(
						entries[leafhdr.count - 1].hashval)) {
					trace_xfs_attr_list_wrong_blk(context);
					xfs_trans_brelse(context->tp, bp);
					bp = NULL;
				} else if (cursor->hashval <= be32_to_cpu(
						entries[0].hashval)) {
					trace_xfs_attr_list_wrong_blk(context);
					xfs_trans_brelse(context->tp, bp);
					bp = NULL;
				}
				break;
			default:
				trace_xfs_attr_list_wrong_blk(context);
				xfs_trans_brelse(context->tp, bp);
				bp = NULL;
			}
		}
	}

	/*
	 * We did not find what we expected given the cursor's contents,
	 * so we start from the top and work down based on the hash value.
	 * Note that start of node block is same as start of leaf block.
	 */
	if (bp == NULL) {
		error = xfs_attr_node_list_lookup(context, cursor, &bp);
		if (error || !bp)
			return error;
	}
	ASSERT(bp != NULL);

	/*
	 * Roll upward through the blocks, processing each leaf block in
	 * order.  As long as there is space in the result buffer, keep
	 * adding the information.
	 */
	for (;;) {
		leaf = bp->b_addr;
		error = xfs_attr3_leaf_list_int(bp, context);
		if (error)
			break;
		xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &leafhdr, leaf);
		if (context->seen_enough || leafhdr.forw == 0)
			break;
		cursor->blkno = leafhdr.forw;
		xfs_trans_brelse(context->tp, bp);
		error = xfs_attr3_leaf_read(context->tp, dp, cursor->blkno,
					    &bp);
		if (error)
			return error;
	}
	xfs_trans_brelse(context->tp, bp);
	return error;
}

/*
 * Copy out attribute list entries for attr_list(), for leaf attribute lists.
 */
int
xfs_attr3_leaf_list_int(
	struct xfs_buf			*bp,
	struct xfs_attr_list_context	*context)
{
	struct xfs_attrlist_cursor_kern	*cursor = &context->cursor;
	struct xfs_attr_leafblock	*leaf;
	struct xfs_attr3_icleaf_hdr	ichdr;
	struct xfs_attr_leaf_entry	*entries;
	struct xfs_attr_leaf_entry	*entry;
	int				i;
	struct xfs_mount		*mp = context->dp->i_mount;

	trace_xfs_attr_list_leaf(context);

	leaf = bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &ichdr, leaf);
	entries = xfs_attr3_leaf_entryp(leaf);

	cursor->initted = 1;

	/*
	 * Re-find our place in the leaf block if this is a new syscall.
	 */
	if (context->resynch) {
		entry = &entries[0];
		for (i = 0; i < ichdr.count; entry++, i++) {
			if (be32_to_cpu(entry->hashval) == cursor->hashval) {
				if (cursor->offset == context->dupcnt) {
					context->dupcnt = 0;
					break;
				}
				context->dupcnt++;
			} else if (be32_to_cpu(entry->hashval) >
					cursor->hashval) {
				context->dupcnt = 0;
				break;
			}
		}
		if (i == ichdr.count) {
			trace_xfs_attr_list_notfound(context);
			return 0;
		}
	} else {
		entry = &entries[0];
		i = 0;
	}
	context->resynch = 0;

	/*
	 * We have found our place, start copying out the new attributes.
	 */
	for (; i < ichdr.count; entry++, i++) {
		char *name;
		int namelen, valuelen;

		if (be32_to_cpu(entry->hashval) != cursor->hashval) {
			cursor->hashval = be32_to_cpu(entry->hashval);
			cursor->offset = 0;
		}

		if ((entry->flags & XFS_ATTR_INCOMPLETE) &&
		    !context->allow_incomplete)
			continue;

		if (entry->flags & XFS_ATTR_LOCAL) {
			xfs_attr_leaf_name_local_t *name_loc;

			name_loc = xfs_attr3_leaf_name_local(leaf, i);
			name = name_loc->nameval;
			namelen = name_loc->namelen;
			valuelen = be16_to_cpu(name_loc->valuelen);
		} else {
			xfs_attr_leaf_name_remote_t *name_rmt;

			name_rmt = xfs_attr3_leaf_name_remote(leaf, i);
			name = name_rmt->name;
			namelen = name_rmt->namelen;
			valuelen = be32_to_cpu(name_rmt->valuelen);
		}

		if (XFS_IS_CORRUPT(context->dp->i_mount,
				   !xfs_attr_namecheck(name, namelen)))
			return -EFSCORRUPTED;
		context->put_listent(context, entry->flags,
					      name, namelen, valuelen);
		if (context->seen_enough)
			break;
		cursor->offset++;
	}
	trace_xfs_attr_list_leaf_end(context);
	return 0;
}

/*
 * Copy out attribute entries for attr_list(), for leaf attribute lists.
 */
STATIC int
xfs_attr_leaf_list(
	struct xfs_attr_list_context	*context)
{
	struct xfs_buf			*bp;
	int				error;

	trace_xfs_attr_leaf_list(context);

	context->cursor.blkno = 0;
	error = xfs_attr3_leaf_read(context->tp, context->dp, 0, &bp);
	if (error)
		return error;

	error = xfs_attr3_leaf_list_int(bp, context);
	xfs_trans_brelse(context->tp, bp);
	return error;
}

int
xfs_attr_list_ilocked(
	struct xfs_attr_list_context	*context)
{
	struct xfs_inode		*dp = context->dp;

	ASSERT(xfs_isilocked(dp, XFS_ILOCK_SHARED | XFS_ILOCK_EXCL));

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	if (!xfs_inode_hasattr(dp))
		return 0;
	if (dp->i_afp->if_format == XFS_DINODE_FMT_LOCAL)
		return xfs_attr_shortform_list(context);
	if (xfs_bmap_one_block(dp, XFS_ATTR_FORK))
		return xfs_attr_leaf_list(context);
	return xfs_attr_node_list(context);
}

int
xfs_attr_list(
	struct xfs_attr_list_context	*context)
{
	struct xfs_inode		*dp = context->dp;
	uint				lock_mode;
	int				error;

	XFS_STATS_INC(dp->i_mount, xs_attr_list);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return -EIO;

	lock_mode = xfs_ilock_attr_map_shared(dp);
	error = xfs_attr_list_ilocked(context);
	xfs_iunlock(dp, lock_mode);
	return error;
}
