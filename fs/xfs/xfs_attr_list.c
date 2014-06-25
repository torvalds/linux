/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2013 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_attr.h"
#include "xfs_attr_sf.h"
#include "xfs_attr_remote.h"
#include "xfs_attr_leaf.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_buf_item.h"
#include "xfs_cksum.h"
#include "xfs_dinode.h"
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
 * If the output buffer is not large enough to hold them all, then we
 * we have to calculate each entries' hashvalue and sort them before
 * we can begin returning them to the user.
 */
int
xfs_attr_shortform_list(xfs_attr_list_context_t *context)
{
	attrlist_cursor_kern_t *cursor;
	xfs_attr_sf_sort_t *sbuf, *sbp;
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	xfs_inode_t *dp;
	int sbsize, nsbuf, count, i;
	int error;

	ASSERT(context != NULL);
	dp = context->dp;
	ASSERT(dp != NULL);
	ASSERT(dp->i_afp != NULL);
	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	ASSERT(sf != NULL);
	if (!sf->hdr.count)
		return 0;
	cursor = context->cursor;
	ASSERT(cursor != NULL);

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
			error = context->put_listent(context,
					   sfe->flags,
					   sfe->nameval,
					   (int)sfe->namelen,
					   (int)sfe->valuelen,
					   &sfe->nameval[sfe->namelen]);

			/*
			 * Either search callback finished early or
			 * didn't fit it all in the buffer after all.
			 */
			if (context->seen_enough)
				break;

			if (error)
				return error;
			sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
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
	sbp = sbuf = kmem_alloc(sbsize, KM_SLEEP | KM_NOFS);

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
					     context->dp->i_mount, sfe);
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
		sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
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
	if (i == nsbuf) {
		kmem_free(sbuf);
		return 0;
	}

	/*
	 * Loop putting entries into the user buffer.
	 */
	for ( ; i < nsbuf; i++, sbp++) {
		if (cursor->hashval != sbp->hash) {
			cursor->hashval = sbp->hash;
			cursor->offset = 0;
		}
		error = context->put_listent(context,
					sbp->flags,
					sbp->name,
					sbp->namelen,
					sbp->valuelen,
					&sbp->name[sbp->namelen]);
		if (error)
			return error;
		if (context->seen_enough)
			break;
		cursor->offset++;
	}

	kmem_free(sbuf);
	return 0;
}

STATIC int
xfs_attr_node_list(xfs_attr_list_context_t *context)
{
	attrlist_cursor_kern_t *cursor;
	xfs_attr_leafblock_t *leaf;
	xfs_da_intnode_t *node;
	struct xfs_attr3_icleaf_hdr leafhdr;
	struct xfs_da3_icnode_hdr nodehdr;
	struct xfs_da_node_entry *btree;
	int error, i;
	struct xfs_buf *bp;
	struct xfs_inode	*dp = context->dp;

	trace_xfs_attr_node_list(context);

	cursor = context->cursor;
	cursor->initted = 1;

	/*
	 * Do all sorts of validation on the passed-in cursor structure.
	 * If anything is amiss, ignore the cursor and look up the hashval
	 * starting from the btree root.
	 */
	bp = NULL;
	if (cursor->blkno > 0) {
		error = xfs_da3_node_read(NULL, dp, cursor->blkno, -1,
					      &bp, XFS_ATTR_FORK);
		if ((error != 0) && (error != -EFSCORRUPTED))
			return error;
		if (bp) {
			struct xfs_attr_leaf_entry *entries;

			node = bp->b_addr;
			switch (be16_to_cpu(node->hdr.info.magic)) {
			case XFS_DA_NODE_MAGIC:
			case XFS_DA3_NODE_MAGIC:
				trace_xfs_attr_list_wrong_blk(context);
				xfs_trans_brelse(NULL, bp);
				bp = NULL;
				break;
			case XFS_ATTR_LEAF_MAGIC:
			case XFS_ATTR3_LEAF_MAGIC:
				leaf = bp->b_addr;
				xfs_attr3_leaf_hdr_from_disk(&leafhdr, leaf);
				entries = xfs_attr3_leaf_entryp(leaf);
				if (cursor->hashval > be32_to_cpu(
						entries[leafhdr.count - 1].hashval)) {
					trace_xfs_attr_list_wrong_blk(context);
					xfs_trans_brelse(NULL, bp);
					bp = NULL;
				} else if (cursor->hashval <= be32_to_cpu(
						entries[0].hashval)) {
					trace_xfs_attr_list_wrong_blk(context);
					xfs_trans_brelse(NULL, bp);
					bp = NULL;
				}
				break;
			default:
				trace_xfs_attr_list_wrong_blk(context);
				xfs_trans_brelse(NULL, bp);
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
		cursor->blkno = 0;
		for (;;) {
			__uint16_t magic;

			error = xfs_da3_node_read(NULL, dp,
						      cursor->blkno, -1, &bp,
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
				XFS_CORRUPTION_ERROR("xfs_attr_node_list(3)",
						     XFS_ERRLEVEL_LOW,
						     context->dp->i_mount,
						     node);
				xfs_trans_brelse(NULL, bp);
				return -EFSCORRUPTED;
			}

			dp->d_ops->node_hdr_from_disk(&nodehdr, node);
			btree = dp->d_ops->node_tree_p(node);
			for (i = 0; i < nodehdr.count; btree++, i++) {
				if (cursor->hashval
						<= be32_to_cpu(btree->hashval)) {
					cursor->blkno = be32_to_cpu(btree->before);
					trace_xfs_attr_list_node_descend(context,
									 btree);
					break;
				}
			}
			if (i == nodehdr.count) {
				xfs_trans_brelse(NULL, bp);
				return 0;
			}
			xfs_trans_brelse(NULL, bp);
		}
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
		if (error) {
			xfs_trans_brelse(NULL, bp);
			return error;
		}
		xfs_attr3_leaf_hdr_from_disk(&leafhdr, leaf);
		if (context->seen_enough || leafhdr.forw == 0)
			break;
		cursor->blkno = leafhdr.forw;
		xfs_trans_brelse(NULL, bp);
		error = xfs_attr3_leaf_read(NULL, dp, cursor->blkno, -1, &bp);
		if (error)
			return error;
	}
	xfs_trans_brelse(NULL, bp);
	return 0;
}

/*
 * Copy out attribute list entries for attr_list(), for leaf attribute lists.
 */
int
xfs_attr3_leaf_list_int(
	struct xfs_buf			*bp,
	struct xfs_attr_list_context	*context)
{
	struct attrlist_cursor_kern	*cursor;
	struct xfs_attr_leafblock	*leaf;
	struct xfs_attr3_icleaf_hdr	ichdr;
	struct xfs_attr_leaf_entry	*entries;
	struct xfs_attr_leaf_entry	*entry;
	int				retval;
	int				i;

	trace_xfs_attr_list_leaf(context);

	leaf = bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(&ichdr, leaf);
	entries = xfs_attr3_leaf_entryp(leaf);

	cursor = context->cursor;
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
	retval = 0;
	for (; i < ichdr.count; entry++, i++) {
		if (be32_to_cpu(entry->hashval) != cursor->hashval) {
			cursor->hashval = be32_to_cpu(entry->hashval);
			cursor->offset = 0;
		}

		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;		/* skip incomplete entries */

		if (entry->flags & XFS_ATTR_LOCAL) {
			xfs_attr_leaf_name_local_t *name_loc =
				xfs_attr3_leaf_name_local(leaf, i);

			retval = context->put_listent(context,
						entry->flags,
						name_loc->nameval,
						(int)name_loc->namelen,
						be16_to_cpu(name_loc->valuelen),
						&name_loc->nameval[name_loc->namelen]);
			if (retval)
				return retval;
		} else {
			xfs_attr_leaf_name_remote_t *name_rmt =
				xfs_attr3_leaf_name_remote(leaf, i);

			int valuelen = be32_to_cpu(name_rmt->valuelen);

			if (context->put_value) {
				xfs_da_args_t args;

				memset((char *)&args, 0, sizeof(args));
				args.geo = context->dp->i_mount->m_attr_geo;
				args.dp = context->dp;
				args.whichfork = XFS_ATTR_FORK;
				args.valuelen = valuelen;
				args.rmtvaluelen = valuelen;
				args.value = kmem_alloc(valuelen, KM_SLEEP | KM_NOFS);
				args.rmtblkno = be32_to_cpu(name_rmt->valueblk);
				args.rmtblkcnt = xfs_attr3_rmt_blocks(
							args.dp->i_mount, valuelen);
				retval = xfs_attr_rmtval_get(&args);
				if (retval)
					return retval;
				retval = context->put_listent(context,
						entry->flags,
						name_rmt->name,
						(int)name_rmt->namelen,
						valuelen,
						args.value);
				kmem_free(args.value);
			} else {
				retval = context->put_listent(context,
						entry->flags,
						name_rmt->name,
						(int)name_rmt->namelen,
						valuelen,
						NULL);
			}
			if (retval)
				return retval;
		}
		if (context->seen_enough)
			break;
		cursor->offset++;
	}
	trace_xfs_attr_list_leaf_end(context);
	return retval;
}

/*
 * Copy out attribute entries for attr_list(), for leaf attribute lists.
 */
STATIC int
xfs_attr_leaf_list(xfs_attr_list_context_t *context)
{
	int error;
	struct xfs_buf *bp;

	trace_xfs_attr_leaf_list(context);

	context->cursor->blkno = 0;
	error = xfs_attr3_leaf_read(NULL, context->dp, 0, -1, &bp);
	if (error)
		return error;

	error = xfs_attr3_leaf_list_int(bp, context);
	xfs_trans_brelse(NULL, bp);
	return error;
}

int
xfs_attr_list_int(
	xfs_attr_list_context_t *context)
{
	int error;
	xfs_inode_t *dp = context->dp;
	uint		lock_mode;

	XFS_STATS_INC(xs_attr_list);

	if (XFS_FORCED_SHUTDOWN(dp->i_mount))
		return -EIO;

	/*
	 * Decide on what work routines to call based on the inode size.
	 */
	lock_mode = xfs_ilock_attr_map_shared(dp);
	if (!xfs_inode_hasattr(dp)) {
		error = 0;
	} else if (dp->i_d.di_aformat == XFS_DINODE_FMT_LOCAL) {
		error = xfs_attr_shortform_list(context);
	} else if (xfs_bmap_one_block(dp, XFS_ATTR_FORK)) {
		error = xfs_attr_leaf_list(context);
	} else {
		error = xfs_attr_node_list(context);
	}
	xfs_iunlock(dp, lock_mode);
	return error;
}

#define	ATTR_ENTBASESIZE		/* minimum bytes used by an attr */ \
	(((struct attrlist_ent *) 0)->a_name - (char *) 0)
#define	ATTR_ENTSIZE(namelen)		/* actual bytes used by an attr */ \
	((ATTR_ENTBASESIZE + (namelen) + 1 + sizeof(u_int32_t)-1) \
	 & ~(sizeof(u_int32_t)-1))

/*
 * Format an attribute and copy it out to the user's buffer.
 * Take care to check values and protect against them changing later,
 * we may be reading them directly out of a user buffer.
 */
STATIC int
xfs_attr_put_listent(
	xfs_attr_list_context_t *context,
	int		flags,
	unsigned char	*name,
	int		namelen,
	int		valuelen,
	unsigned char	*value)
{
	struct attrlist *alist = (struct attrlist *)context->alist;
	attrlist_ent_t *aep;
	int arraytop;

	ASSERT(!(context->flags & ATTR_KERNOVAL));
	ASSERT(context->count >= 0);
	ASSERT(context->count < (ATTR_MAX_VALUELEN/8));
	ASSERT(context->firstu >= sizeof(*alist));
	ASSERT(context->firstu <= context->bufsize);

	/*
	 * Only list entries in the right namespace.
	 */
	if (((context->flags & ATTR_SECURE) == 0) !=
	    ((flags & XFS_ATTR_SECURE) == 0))
		return 0;
	if (((context->flags & ATTR_ROOT) == 0) !=
	    ((flags & XFS_ATTR_ROOT) == 0))
		return 0;

	arraytop = sizeof(*alist) +
			context->count * sizeof(alist->al_offset[0]);
	context->firstu -= ATTR_ENTSIZE(namelen);
	if (context->firstu < arraytop) {
		trace_xfs_attr_list_full(context);
		alist->al_more = 1;
		context->seen_enough = 1;
		return 1;
	}

	aep = (attrlist_ent_t *)&context->alist[context->firstu];
	aep->a_valuelen = valuelen;
	memcpy(aep->a_name, name, namelen);
	aep->a_name[namelen] = 0;
	alist->al_offset[context->count++] = context->firstu;
	alist->al_count = context->count;
	trace_xfs_attr_list_add(context);
	return 0;
}

/*
 * Generate a list of extended attribute names and optionally
 * also value lengths.  Positive return value follows the XFS
 * convention of being an error, zero or negative return code
 * is the length of the buffer returned (negated), indicating
 * success.
 */
int
xfs_attr_list(
	xfs_inode_t	*dp,
	char		*buffer,
	int		bufsize,
	int		flags,
	attrlist_cursor_kern_t *cursor)
{
	xfs_attr_list_context_t context;
	struct attrlist *alist;
	int error;

	/*
	 * Validate the cursor.
	 */
	if (cursor->pad1 || cursor->pad2)
		return -EINVAL;
	if ((cursor->initted == 0) &&
	    (cursor->hashval || cursor->blkno || cursor->offset))
		return -EINVAL;

	/*
	 * Check for a properly aligned buffer.
	 */
	if (((long)buffer) & (sizeof(int)-1))
		return -EFAULT;
	if (flags & ATTR_KERNOVAL)
		bufsize = 0;

	/*
	 * Initialize the output buffer.
	 */
	memset(&context, 0, sizeof(context));
	context.dp = dp;
	context.cursor = cursor;
	context.resynch = 1;
	context.flags = flags;
	context.alist = buffer;
	context.bufsize = (bufsize & ~(sizeof(int)-1));  /* align */
	context.firstu = context.bufsize;
	context.put_listent = xfs_attr_put_listent;

	alist = (struct attrlist *)context.alist;
	alist->al_count = 0;
	alist->al_more = 0;
	alist->al_offset[0] = context.bufsize;

	error = xfs_attr_list_int(&context);
	ASSERT(error <= 0);
	return error;
}
