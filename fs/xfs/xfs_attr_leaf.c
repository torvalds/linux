/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
/*
 * xfs_attr_leaf.c
 *
 * GROT: figure out how to recover gracefully when bmap returns ENOSPC.
 */

#include "xfs.h"

#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode_item.h"
#include "xfs_inode.h"
#include "xfs_bmap.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_error.h"
#include "xfs_bit.h"

/*
 * xfs_attr_leaf.c
 *
 * Routines to implement leaf blocks of attributes as Btrees of hashed names.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
STATIC int xfs_attr_leaf_create(xfs_da_args_t *args, xfs_dablk_t which_block,
				    xfs_dabuf_t **bpp);
STATIC int xfs_attr_leaf_add_work(xfs_dabuf_t *leaf_buffer, xfs_da_args_t *args,
					      int freemap_index);
STATIC void xfs_attr_leaf_compact(xfs_trans_t *trans, xfs_dabuf_t *leaf_buffer);
STATIC void xfs_attr_leaf_rebalance(xfs_da_state_t *state,
						   xfs_da_state_blk_t *blk1,
						   xfs_da_state_blk_t *blk2);
STATIC int xfs_attr_leaf_figure_balance(xfs_da_state_t *state,
					   xfs_da_state_blk_t *leaf_blk_1,
					   xfs_da_state_blk_t *leaf_blk_2,
					   int *number_entries_in_blk1,
					   int *number_usedbytes_in_blk1);

/*
 * Routines used for shrinking the Btree.
 */
STATIC int xfs_attr_node_inactive(xfs_trans_t **trans, xfs_inode_t *dp,
				  xfs_dabuf_t *bp, int level);
STATIC int xfs_attr_leaf_inactive(xfs_trans_t **trans, xfs_inode_t *dp,
				  xfs_dabuf_t *bp);
STATIC int xfs_attr_leaf_freextent(xfs_trans_t **trans, xfs_inode_t *dp,
				   xfs_dablk_t blkno, int blkcnt);

/*
 * Utility routines.
 */
STATIC void xfs_attr_leaf_moveents(xfs_attr_leafblock_t *src_leaf,
					 int src_start,
					 xfs_attr_leafblock_t *dst_leaf,
					 int dst_start, int move_count,
					 xfs_mount_t *mp);
STATIC int xfs_attr_leaf_entsize(xfs_attr_leafblock_t *leaf, int index);
STATIC int xfs_attr_put_listent(xfs_attr_list_context_t *context,
			     attrnames_t *, char *name, int namelen,
			     int valuelen);


/*========================================================================
 * External routines when dirsize < XFS_LITINO(mp).
 *========================================================================*/

/*
 * Create the initial contents of a shortform attribute list.
 */
int
xfs_attr_shortform_create(xfs_da_args_t *args)
{
	xfs_attr_sf_hdr_t *hdr;
	xfs_inode_t *dp;
	xfs_ifork_t *ifp;

	dp = args->dp;
	ASSERT(dp != NULL);
	ifp = dp->i_afp;
	ASSERT(ifp != NULL);
	ASSERT(ifp->if_bytes == 0);
	if (dp->i_d.di_aformat == XFS_DINODE_FMT_EXTENTS) {
		ifp->if_flags &= ~XFS_IFEXTENTS;	/* just in case */
		dp->i_d.di_aformat = XFS_DINODE_FMT_LOCAL;
		ifp->if_flags |= XFS_IFINLINE;
	} else {
		ASSERT(ifp->if_flags & XFS_IFINLINE);
	}
	xfs_idata_realloc(dp, sizeof(*hdr), XFS_ATTR_FORK);
	hdr = (xfs_attr_sf_hdr_t *)ifp->if_u1.if_data;
	hdr->count = 0;
	INT_SET(hdr->totsize, ARCH_CONVERT, sizeof(*hdr));
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_ADATA);
	return(0);
}

/*
 * Add a name/value pair to the shortform attribute list.
 * Overflow from the inode has already been checked for.
 */
int
xfs_attr_shortform_add(xfs_da_args_t *args)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int i, offset, size;
	xfs_inode_t *dp;
	xfs_ifork_t *ifp;

	dp = args->dp;
	ifp = dp->i_afp;
	ASSERT(ifp->if_flags & XFS_IFINLINE);
	sf = (xfs_attr_shortform_t *)ifp->if_u1.if_data;
	sfe = &sf->list[0];
	for (i = 0; i < INT_GET(sf->hdr.count, ARCH_CONVERT);
				sfe = XFS_ATTR_SF_NEXTENTRY(sfe), i++) {
		if (sfe->namelen != args->namelen)
			continue;
		if (memcmp(args->name, sfe->nameval, args->namelen) != 0)
			continue;
		if (((args->flags & ATTR_SECURE) != 0) !=
		    ((sfe->flags & XFS_ATTR_SECURE) != 0))
			continue;
		if (((args->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0))
			continue;
		return(XFS_ERROR(EEXIST));
	}

	offset = (char *)sfe - (char *)sf;
	size = XFS_ATTR_SF_ENTSIZE_BYNAME(args->namelen, args->valuelen);
	xfs_idata_realloc(dp, size, XFS_ATTR_FORK);
	sf = (xfs_attr_shortform_t *)ifp->if_u1.if_data;
	sfe = (xfs_attr_sf_entry_t *)((char *)sf + offset);

	sfe->namelen = args->namelen;
	INT_SET(sfe->valuelen, ARCH_CONVERT, args->valuelen);
	sfe->flags = (args->flags & ATTR_SECURE) ? XFS_ATTR_SECURE :
			((args->flags & ATTR_ROOT) ? XFS_ATTR_ROOT : 0);
	memcpy(sfe->nameval, args->name, args->namelen);
	memcpy(&sfe->nameval[args->namelen], args->value, args->valuelen);
	INT_MOD(sf->hdr.count, ARCH_CONVERT, 1);
	INT_MOD(sf->hdr.totsize, ARCH_CONVERT, size);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_ADATA);

	return(0);
}

/*
 * Remove a name from the shortform attribute list structure.
 */
int
xfs_attr_shortform_remove(xfs_da_args_t *args)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int base, size=0, end, totsize, i;
	xfs_inode_t *dp;

	/*
	 * Remove the attribute.
	 */
	dp = args->dp;
	base = sizeof(xfs_attr_sf_hdr_t);
	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	sfe = &sf->list[0];
	for (i = 0; i < INT_GET(sf->hdr.count, ARCH_CONVERT);
				sfe = XFS_ATTR_SF_NEXTENTRY(sfe),
					base += size, i++) {
		size = XFS_ATTR_SF_ENTSIZE(sfe);
		if (sfe->namelen != args->namelen)
			continue;
		if (memcmp(sfe->nameval, args->name, args->namelen) != 0)
			continue;
		if (((args->flags & ATTR_SECURE) != 0) !=
		    ((sfe->flags & XFS_ATTR_SECURE) != 0))
			continue;
		if (((args->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0))
			continue;
		break;
	}
	if (i == INT_GET(sf->hdr.count, ARCH_CONVERT))
		return(XFS_ERROR(ENOATTR));

	end = base + size;
	totsize = INT_GET(sf->hdr.totsize, ARCH_CONVERT);
	if (end != totsize) {
		memmove(&((char *)sf)[base], &((char *)sf)[end],
							totsize - end);
	}
	INT_MOD(sf->hdr.count, ARCH_CONVERT, -1);
	INT_MOD(sf->hdr.totsize, ARCH_CONVERT, -size);
	xfs_idata_realloc(dp, -size, XFS_ATTR_FORK);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_ADATA);

	return(0);
}

/*
 * Look up a name in a shortform attribute list structure.
 */
/*ARGSUSED*/
int
xfs_attr_shortform_lookup(xfs_da_args_t *args)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int i;
	xfs_ifork_t *ifp;

	ifp = args->dp->i_afp;
	ASSERT(ifp->if_flags & XFS_IFINLINE);
	sf = (xfs_attr_shortform_t *)ifp->if_u1.if_data;
	sfe = &sf->list[0];
	for (i = 0; i < INT_GET(sf->hdr.count, ARCH_CONVERT);
				sfe = XFS_ATTR_SF_NEXTENTRY(sfe), i++) {
		if (sfe->namelen != args->namelen)
			continue;
		if (memcmp(args->name, sfe->nameval, args->namelen) != 0)
			continue;
		if (((args->flags & ATTR_SECURE) != 0) !=
		    ((sfe->flags & XFS_ATTR_SECURE) != 0))
			continue;
		if (((args->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0))
			continue;
		return(XFS_ERROR(EEXIST));
	}
	return(XFS_ERROR(ENOATTR));
}

/*
 * Look up a name in a shortform attribute list structure.
 */
/*ARGSUSED*/
int
xfs_attr_shortform_getvalue(xfs_da_args_t *args)
{
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	int i;

	ASSERT(args->dp->i_d.di_aformat == XFS_IFINLINE);
	sf = (xfs_attr_shortform_t *)args->dp->i_afp->if_u1.if_data;
	sfe = &sf->list[0];
	for (i = 0; i < INT_GET(sf->hdr.count, ARCH_CONVERT);
				sfe = XFS_ATTR_SF_NEXTENTRY(sfe), i++) {
		if (sfe->namelen != args->namelen)
			continue;
		if (memcmp(args->name, sfe->nameval, args->namelen) != 0)
			continue;
		if (((args->flags & ATTR_SECURE) != 0) !=
		    ((sfe->flags & XFS_ATTR_SECURE) != 0))
			continue;
		if (((args->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0))
			continue;
		if (args->flags & ATTR_KERNOVAL) {
			args->valuelen = INT_GET(sfe->valuelen, ARCH_CONVERT);
			return(XFS_ERROR(EEXIST));
		}
		if (args->valuelen < INT_GET(sfe->valuelen, ARCH_CONVERT)) {
			args->valuelen = INT_GET(sfe->valuelen, ARCH_CONVERT);
			return(XFS_ERROR(ERANGE));
		}
		args->valuelen = INT_GET(sfe->valuelen, ARCH_CONVERT);
		memcpy(args->value, &sfe->nameval[args->namelen],
						    args->valuelen);
		return(XFS_ERROR(EEXIST));
	}
	return(XFS_ERROR(ENOATTR));
}

/*
 * Convert from using the shortform to the leaf.
 */
int
xfs_attr_shortform_to_leaf(xfs_da_args_t *args)
{
	xfs_inode_t *dp;
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	xfs_da_args_t nargs;
	char *tmpbuffer;
	int error, i, size;
	xfs_dablk_t blkno;
	xfs_dabuf_t *bp;
	xfs_ifork_t *ifp;

	dp = args->dp;
	ifp = dp->i_afp;
	sf = (xfs_attr_shortform_t *)ifp->if_u1.if_data;
	size = INT_GET(sf->hdr.totsize, ARCH_CONVERT);
	tmpbuffer = kmem_alloc(size, KM_SLEEP);
	ASSERT(tmpbuffer != NULL);
	memcpy(tmpbuffer, ifp->if_u1.if_data, size);
	sf = (xfs_attr_shortform_t *)tmpbuffer;

	xfs_idata_realloc(dp, -size, XFS_ATTR_FORK);
	bp = NULL;
	error = xfs_da_grow_inode(args, &blkno);
	if (error) {
		/*
		 * If we hit an IO error middle of the transaction inside
		 * grow_inode(), we may have inconsistent data. Bail out.
		 */
		if (error == EIO)
			goto out;
		xfs_idata_realloc(dp, size, XFS_ATTR_FORK);	/* try to put */
		memcpy(ifp->if_u1.if_data, tmpbuffer, size);	/* it back */
		goto out;
	}

	ASSERT(blkno == 0);
	error = xfs_attr_leaf_create(args, blkno, &bp);
	if (error) {
		error = xfs_da_shrink_inode(args, 0, bp);
		bp = NULL;
		if (error)
			goto out;
		xfs_idata_realloc(dp, size, XFS_ATTR_FORK);	/* try to put */
		memcpy(ifp->if_u1.if_data, tmpbuffer, size);	/* it back */
		goto out;
	}

	memset((char *)&nargs, 0, sizeof(nargs));
	nargs.dp = dp;
	nargs.firstblock = args->firstblock;
	nargs.flist = args->flist;
	nargs.total = args->total;
	nargs.whichfork = XFS_ATTR_FORK;
	nargs.trans = args->trans;
	nargs.oknoent = 1;

	sfe = &sf->list[0];
	for (i = 0; i < INT_GET(sf->hdr.count, ARCH_CONVERT); i++) {
		nargs.name = (char *)sfe->nameval;
		nargs.namelen = sfe->namelen;
		nargs.value = (char *)&sfe->nameval[nargs.namelen];
		nargs.valuelen = INT_GET(sfe->valuelen, ARCH_CONVERT);
		nargs.hashval = xfs_da_hashname((char *)sfe->nameval,
						sfe->namelen);
		nargs.flags = (sfe->flags & XFS_ATTR_SECURE) ? ATTR_SECURE :
				((sfe->flags & XFS_ATTR_ROOT) ? ATTR_ROOT : 0);
		error = xfs_attr_leaf_lookup_int(bp, &nargs); /* set a->index */
		ASSERT(error == ENOATTR);
		error = xfs_attr_leaf_add(bp, &nargs);
		ASSERT(error != ENOSPC);
		if (error)
			goto out;
		sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
	}
	error = 0;

out:
	if(bp)
		xfs_da_buf_done(bp);
	kmem_free(tmpbuffer, size);
	return(error);
}

STATIC int
xfs_attr_shortform_compare(const void *a, const void *b)
{
	xfs_attr_sf_sort_t *sa, *sb;

	sa = (xfs_attr_sf_sort_t *)a;
	sb = (xfs_attr_sf_sort_t *)b;
	if (INT_GET(sa->hash, ARCH_CONVERT)
				< INT_GET(sb->hash, ARCH_CONVERT)) {
		return(-1);
	} else if (INT_GET(sa->hash, ARCH_CONVERT)
				> INT_GET(sb->hash, ARCH_CONVERT)) {
		return(1);
	} else {
		return(sa->entno - sb->entno);
	}
}

/*
 * Copy out entries of shortform attribute lists for attr_list().
 * Shortform atrtribute lists are not stored in hashval sorted order.
 * If the output buffer is not large enough to hold them all, then we
 * we have to calculate each entries' hashvalue and sort them before
 * we can begin returning them to the user.
 */
/*ARGSUSED*/
int
xfs_attr_shortform_list(xfs_attr_list_context_t *context)
{
	attrlist_cursor_kern_t *cursor;
	xfs_attr_sf_sort_t *sbuf, *sbp;
	xfs_attr_shortform_t *sf;
	xfs_attr_sf_entry_t *sfe;
	xfs_inode_t *dp;
	int sbsize, nsbuf, count, i;

	ASSERT(context != NULL);
	dp = context->dp;
	ASSERT(dp != NULL);
	ASSERT(dp->i_afp != NULL);
	sf = (xfs_attr_shortform_t *)dp->i_afp->if_u1.if_data;
	ASSERT(sf != NULL);
	if (!sf->hdr.count)
		return(0);
	cursor = context->cursor;
	ASSERT(cursor != NULL);

	xfs_attr_trace_l_c("sf start", context);

	/*
	 * If the buffer is large enough, do not bother with sorting.
	 * Note the generous fudge factor of 16 overhead bytes per entry.
	 */
	if ((dp->i_afp->if_bytes + INT_GET(sf->hdr.count, ARCH_CONVERT) * 16)
							< context->bufsize) {
		for (i = 0, sfe = &sf->list[0];
				i < INT_GET(sf->hdr.count, ARCH_CONVERT); i++) {
			attrnames_t	*namesp;

			if (((context->flags & ATTR_SECURE) != 0) !=
			    ((sfe->flags & XFS_ATTR_SECURE) != 0) &&
			    !(context->flags & ATTR_KERNORMALS)) {
				sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
				continue;
			}
			if (((context->flags & ATTR_ROOT) != 0) !=
			    ((sfe->flags & XFS_ATTR_ROOT) != 0) &&
			    !(context->flags & ATTR_KERNROOTLS)) {
				sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
				continue;
			}
			namesp = (sfe->flags & XFS_ATTR_SECURE) ? &attr_secure:
				((sfe->flags & XFS_ATTR_ROOT) ? &attr_trusted :
				  &attr_user);
			if (context->flags & ATTR_KERNOVAL) {
				ASSERT(context->flags & ATTR_KERNAMELS);
				context->count += namesp->attr_namelen +
					INT_GET(sfe->namelen, ARCH_CONVERT) + 1;
			}
			else {
				if (xfs_attr_put_listent(context, namesp,
						   (char *)sfe->nameval,
						   (int)sfe->namelen,
						   (int)INT_GET(sfe->valuelen,
								ARCH_CONVERT)))
					break;
			}
			sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
		}
		xfs_attr_trace_l_c("sf big-gulp", context);
		return(0);
	}

	/*
	 * It didn't all fit, so we have to sort everything on hashval.
	 */
	sbsize = INT_GET(sf->hdr.count, ARCH_CONVERT) * sizeof(*sbuf);
	sbp = sbuf = kmem_alloc(sbsize, KM_SLEEP);

	/*
	 * Scan the attribute list for the rest of the entries, storing
	 * the relevant info from only those that match into a buffer.
	 */
	nsbuf = 0;
	for (i = 0, sfe = &sf->list[0];
			i < INT_GET(sf->hdr.count, ARCH_CONVERT); i++) {
		if (unlikely(
		    ((char *)sfe < (char *)sf) ||
		    ((char *)sfe >= ((char *)sf + dp->i_afp->if_bytes)))) {
			XFS_CORRUPTION_ERROR("xfs_attr_shortform_list",
					     XFS_ERRLEVEL_LOW,
					     context->dp->i_mount, sfe);
			xfs_attr_trace_l_c("sf corrupted", context);
			kmem_free(sbuf, sbsize);
			return XFS_ERROR(EFSCORRUPTED);
		}
		if (((context->flags & ATTR_SECURE) != 0) !=
		    ((sfe->flags & XFS_ATTR_SECURE) != 0) &&
		    !(context->flags & ATTR_KERNORMALS)) {
			sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
			continue;
		}
		if (((context->flags & ATTR_ROOT) != 0) !=
		    ((sfe->flags & XFS_ATTR_ROOT) != 0) &&
		    !(context->flags & ATTR_KERNROOTLS)) {
			sfe = XFS_ATTR_SF_NEXTENTRY(sfe);
			continue;
		}
		sbp->entno = i;
		INT_SET(sbp->hash, ARCH_CONVERT,
			xfs_da_hashname((char *)sfe->nameval, sfe->namelen));
		sbp->name = (char *)sfe->nameval;
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
	qsort(sbuf, nsbuf, sizeof(*sbuf), xfs_attr_shortform_compare);

	/*
	 * Re-find our place IN THE SORTED LIST.
	 */
	count = 0;
	cursor->initted = 1;
	cursor->blkno = 0;
	for (sbp = sbuf, i = 0; i < nsbuf; i++, sbp++) {
		if (INT_GET(sbp->hash, ARCH_CONVERT) == cursor->hashval) {
			if (cursor->offset == count) {
				break;
			}
			count++;
		} else if (INT_GET(sbp->hash, ARCH_CONVERT) > cursor->hashval) {
			break;
		}
	}
	if (i == nsbuf) {
		kmem_free(sbuf, sbsize);
		xfs_attr_trace_l_c("blk end", context);
		return(0);
	}

	/*
	 * Loop putting entries into the user buffer.
	 */
	for ( ; i < nsbuf; i++, sbp++) {
		attrnames_t	*namesp;

		namesp = (sbp->flags & XFS_ATTR_SECURE) ? &attr_secure :
			((sbp->flags & XFS_ATTR_ROOT) ? &attr_trusted :
			  &attr_user);

		if (cursor->hashval != INT_GET(sbp->hash, ARCH_CONVERT)) {
			cursor->hashval = INT_GET(sbp->hash, ARCH_CONVERT);
			cursor->offset = 0;
		}
		if (context->flags & ATTR_KERNOVAL) {
			ASSERT(context->flags & ATTR_KERNAMELS);
			context->count += namesp->attr_namelen +
						sbp->namelen + 1;
		} else {
			if (xfs_attr_put_listent(context, namesp,
					sbp->name, sbp->namelen,
					INT_GET(sbp->valuelen, ARCH_CONVERT)))
				break;
		}
		cursor->offset++;
	}

	kmem_free(sbuf, sbsize);
	xfs_attr_trace_l_c("sf E-O-F", context);
	return(0);
}

/*
 * Check a leaf attribute block to see if all the entries would fit into
 * a shortform attribute list.
 */
int
xfs_attr_shortform_allfit(xfs_dabuf_t *bp, xfs_inode_t *dp)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	int bytes, i;

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);

	entry = &leaf->entries[0];
	bytes = sizeof(struct xfs_attr_sf_hdr);
	for (i = 0; i < INT_GET(leaf->hdr.count, ARCH_CONVERT); entry++, i++) {
		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;		/* don't copy partial entries */
		if (!(entry->flags & XFS_ATTR_LOCAL))
			return(0);
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, i);
		if (name_loc->namelen >= XFS_ATTR_SF_ENTSIZE_MAX)
			return(0);
		if (INT_GET(name_loc->valuelen, ARCH_CONVERT) >= XFS_ATTR_SF_ENTSIZE_MAX)
			return(0);
		bytes += sizeof(struct xfs_attr_sf_entry)-1
				+ name_loc->namelen
				+ INT_GET(name_loc->valuelen, ARCH_CONVERT);
	}
	return( bytes < XFS_IFORK_ASIZE(dp) );
}

/*
 * Convert a leaf attribute list to shortform attribute list
 */
int
xfs_attr_leaf_to_shortform(xfs_dabuf_t *bp, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_da_args_t nargs;
	xfs_inode_t *dp;
	char *tmpbuffer;
	int error, i;

	dp = args->dp;
	tmpbuffer = kmem_alloc(XFS_LBSIZE(dp->i_mount), KM_SLEEP);
	ASSERT(tmpbuffer != NULL);

	ASSERT(bp != NULL);
	memcpy(tmpbuffer, bp->data, XFS_LBSIZE(dp->i_mount));
	leaf = (xfs_attr_leafblock_t *)tmpbuffer;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	memset(bp->data, 0, XFS_LBSIZE(dp->i_mount));

	/*
	 * Clean out the prior contents of the attribute list.
	 */
	error = xfs_da_shrink_inode(args, 0, bp);
	if (error)
		goto out;
	error = xfs_attr_shortform_create(args);
	if (error)
		goto out;

	/*
	 * Copy the attributes
	 */
	memset((char *)&nargs, 0, sizeof(nargs));
	nargs.dp = dp;
	nargs.firstblock = args->firstblock;
	nargs.flist = args->flist;
	nargs.total = args->total;
	nargs.whichfork = XFS_ATTR_FORK;
	nargs.trans = args->trans;
	nargs.oknoent = 1;
	entry = &leaf->entries[0];
	for (i = 0; i < INT_GET(leaf->hdr.count, ARCH_CONVERT); entry++, i++) {
		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;	/* don't copy partial entries */
		if (!entry->nameidx)
			continue;
		ASSERT(entry->flags & XFS_ATTR_LOCAL);
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, i);
		nargs.name = (char *)name_loc->nameval;
		nargs.namelen = name_loc->namelen;
		nargs.value = (char *)&name_loc->nameval[nargs.namelen];
		nargs.valuelen = INT_GET(name_loc->valuelen, ARCH_CONVERT);
		nargs.hashval = INT_GET(entry->hashval, ARCH_CONVERT);
		nargs.flags = (entry->flags & XFS_ATTR_SECURE) ? ATTR_SECURE :
			      ((entry->flags & XFS_ATTR_ROOT) ? ATTR_ROOT : 0);
		xfs_attr_shortform_add(&nargs);
	}
	error = 0;

out:
	kmem_free(tmpbuffer, XFS_LBSIZE(dp->i_mount));
	return(error);
}

/*
 * Convert from using a single leaf to a root node and a leaf.
 */
int
xfs_attr_leaf_to_node(xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_da_intnode_t *node;
	xfs_inode_t *dp;
	xfs_dabuf_t *bp1, *bp2;
	xfs_dablk_t blkno;
	int error;

	dp = args->dp;
	bp1 = bp2 = NULL;
	error = xfs_da_grow_inode(args, &blkno);
	if (error)
		goto out;
	error = xfs_da_read_buf(args->trans, args->dp, 0, -1, &bp1,
					     XFS_ATTR_FORK);
	if (error)
		goto out;
	ASSERT(bp1 != NULL);
	bp2 = NULL;
	error = xfs_da_get_buf(args->trans, args->dp, blkno, -1, &bp2,
					    XFS_ATTR_FORK);
	if (error)
		goto out;
	ASSERT(bp2 != NULL);
	memcpy(bp2->data, bp1->data, XFS_LBSIZE(dp->i_mount));
	xfs_da_buf_done(bp1);
	bp1 = NULL;
	xfs_da_log_buf(args->trans, bp2, 0, XFS_LBSIZE(dp->i_mount) - 1);

	/*
	 * Set up the new root node.
	 */
	error = xfs_da_node_create(args, 0, 1, &bp1, XFS_ATTR_FORK);
	if (error)
		goto out;
	node = bp1->data;
	leaf = bp2->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	/* both on-disk, don't endian-flip twice */
	node->btree[0].hashval =
		leaf->entries[INT_GET(leaf->hdr.count, ARCH_CONVERT)-1 ].hashval;
	INT_SET(node->btree[0].before, ARCH_CONVERT, blkno);
	INT_SET(node->hdr.count, ARCH_CONVERT, 1);
	xfs_da_log_buf(args->trans, bp1, 0, XFS_LBSIZE(dp->i_mount) - 1);
	error = 0;
out:
	if (bp1)
		xfs_da_buf_done(bp1);
	if (bp2)
		xfs_da_buf_done(bp2);
	return(error);
}


/*========================================================================
 * Routines used for growing the Btree.
 *========================================================================*/

/*
 * Create the initial contents of a leaf attribute list
 * or a leaf in a node attribute list.
 */
STATIC int
xfs_attr_leaf_create(xfs_da_args_t *args, xfs_dablk_t blkno, xfs_dabuf_t **bpp)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_hdr_t *hdr;
	xfs_inode_t *dp;
	xfs_dabuf_t *bp;
	int error;

	dp = args->dp;
	ASSERT(dp != NULL);
	error = xfs_da_get_buf(args->trans, args->dp, blkno, -1, &bp,
					    XFS_ATTR_FORK);
	if (error)
		return(error);
	ASSERT(bp != NULL);
	leaf = bp->data;
	memset((char *)leaf, 0, XFS_LBSIZE(dp->i_mount));
	hdr = &leaf->hdr;
	INT_SET(hdr->info.magic, ARCH_CONVERT, XFS_ATTR_LEAF_MAGIC);
	INT_SET(hdr->firstused, ARCH_CONVERT, XFS_LBSIZE(dp->i_mount));
	if (!hdr->firstused) {
		INT_SET(hdr->firstused, ARCH_CONVERT,
			XFS_LBSIZE(dp->i_mount) - XFS_ATTR_LEAF_NAME_ALIGN);
	}

	INT_SET(hdr->freemap[0].base, ARCH_CONVERT,
						sizeof(xfs_attr_leaf_hdr_t));
	INT_SET(hdr->freemap[0].size, ARCH_CONVERT,
					  INT_GET(hdr->firstused, ARCH_CONVERT)
					- INT_GET(hdr->freemap[0].base,
								ARCH_CONVERT));

	xfs_da_log_buf(args->trans, bp, 0, XFS_LBSIZE(dp->i_mount) - 1);

	*bpp = bp;
	return(0);
}

/*
 * Split the leaf node, rebalance, then add the new entry.
 */
int
xfs_attr_leaf_split(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
				   xfs_da_state_blk_t *newblk)
{
	xfs_dablk_t blkno;
	int error;

	/*
	 * Allocate space for a new leaf node.
	 */
	ASSERT(oldblk->magic == XFS_ATTR_LEAF_MAGIC);
	error = xfs_da_grow_inode(state->args, &blkno);
	if (error)
		return(error);
	error = xfs_attr_leaf_create(state->args, blkno, &newblk->bp);
	if (error)
		return(error);
	newblk->blkno = blkno;
	newblk->magic = XFS_ATTR_LEAF_MAGIC;

	/*
	 * Rebalance the entries across the two leaves.
	 * NOTE: rebalance() currently depends on the 2nd block being empty.
	 */
	xfs_attr_leaf_rebalance(state, oldblk, newblk);
	error = xfs_da_blk_link(state, oldblk, newblk);
	if (error)
		return(error);

	/*
	 * Save info on "old" attribute for "atomic rename" ops, leaf_add()
	 * modifies the index/blkno/rmtblk/rmtblkcnt fields to show the
	 * "new" attrs info.  Will need the "old" info to remove it later.
	 *
	 * Insert the "new" entry in the correct block.
	 */
	if (state->inleaf)
		error = xfs_attr_leaf_add(oldblk->bp, state->args);
	else
		error = xfs_attr_leaf_add(newblk->bp, state->args);

	/*
	 * Update last hashval in each block since we added the name.
	 */
	oldblk->hashval = xfs_attr_leaf_lasthash(oldblk->bp, NULL);
	newblk->hashval = xfs_attr_leaf_lasthash(newblk->bp, NULL);
	return(error);
}

/*
 * Add a name to the leaf attribute list structure.
 */
int
xfs_attr_leaf_add(xfs_dabuf_t *bp, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_hdr_t *hdr;
	xfs_attr_leaf_map_t *map;
	int tablesize, entsize, sum, tmp, i;

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT((args->index >= 0)
		&& (args->index <= INT_GET(leaf->hdr.count, ARCH_CONVERT)));
	hdr = &leaf->hdr;
	entsize = xfs_attr_leaf_newentsize(args,
			   args->trans->t_mountp->m_sb.sb_blocksize, NULL);

	/*
	 * Search through freemap for first-fit on new name length.
	 * (may need to figure in size of entry struct too)
	 */
	tablesize = (INT_GET(hdr->count, ARCH_CONVERT) + 1)
					* sizeof(xfs_attr_leaf_entry_t)
					+ sizeof(xfs_attr_leaf_hdr_t);
	map = &hdr->freemap[XFS_ATTR_LEAF_MAPSIZE-1];
	for (sum = 0, i = XFS_ATTR_LEAF_MAPSIZE-1; i >= 0; map--, i--) {
		if (tablesize > INT_GET(hdr->firstused, ARCH_CONVERT)) {
			sum += INT_GET(map->size, ARCH_CONVERT);
			continue;
		}
		if (!map->size)
			continue;	/* no space in this map */
		tmp = entsize;
		if (INT_GET(map->base, ARCH_CONVERT)
				< INT_GET(hdr->firstused, ARCH_CONVERT))
			tmp += sizeof(xfs_attr_leaf_entry_t);
		if (INT_GET(map->size, ARCH_CONVERT) >= tmp) {
			tmp = xfs_attr_leaf_add_work(bp, args, i);
			return(tmp);
		}
		sum += INT_GET(map->size, ARCH_CONVERT);
	}

	/*
	 * If there are no holes in the address space of the block,
	 * and we don't have enough freespace, then compaction will do us
	 * no good and we should just give up.
	 */
	if (!hdr->holes && (sum < entsize))
		return(XFS_ERROR(ENOSPC));

	/*
	 * Compact the entries to coalesce free space.
	 * This may change the hdr->count via dropping INCOMPLETE entries.
	 */
	xfs_attr_leaf_compact(args->trans, bp);

	/*
	 * After compaction, the block is guaranteed to have only one
	 * free region, in freemap[0].  If it is not big enough, give up.
	 */
	if (INT_GET(hdr->freemap[0].size, ARCH_CONVERT)
				< (entsize + sizeof(xfs_attr_leaf_entry_t)))
		return(XFS_ERROR(ENOSPC));

	return(xfs_attr_leaf_add_work(bp, args, 0));
}

/*
 * Add a name to a leaf attribute list structure.
 */
STATIC int
xfs_attr_leaf_add_work(xfs_dabuf_t *bp, xfs_da_args_t *args, int mapindex)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_hdr_t *hdr;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	xfs_attr_leaf_map_t *map;
	xfs_mount_t *mp;
	int tmp, i;

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	ASSERT((mapindex >= 0) && (mapindex < XFS_ATTR_LEAF_MAPSIZE));
	ASSERT((args->index >= 0)
		&& (args->index <= INT_GET(hdr->count, ARCH_CONVERT)));

	/*
	 * Force open some space in the entry array and fill it in.
	 */
	entry = &leaf->entries[args->index];
	if (args->index < INT_GET(hdr->count, ARCH_CONVERT)) {
		tmp  = INT_GET(hdr->count, ARCH_CONVERT) - args->index;
		tmp *= sizeof(xfs_attr_leaf_entry_t);
		memmove((char *)(entry+1), (char *)entry, tmp);
		xfs_da_log_buf(args->trans, bp,
		    XFS_DA_LOGRANGE(leaf, entry, tmp + sizeof(*entry)));
	}
	INT_MOD(hdr->count, ARCH_CONVERT, 1);

	/*
	 * Allocate space for the new string (at the end of the run).
	 */
	map = &hdr->freemap[mapindex];
	mp = args->trans->t_mountp;
	ASSERT(INT_GET(map->base, ARCH_CONVERT) < XFS_LBSIZE(mp));
	ASSERT((INT_GET(map->base, ARCH_CONVERT) & 0x3) == 0);
	ASSERT(INT_GET(map->size, ARCH_CONVERT)
				>= xfs_attr_leaf_newentsize(args,
					     mp->m_sb.sb_blocksize, NULL));
	ASSERT(INT_GET(map->size, ARCH_CONVERT) < XFS_LBSIZE(mp));
	ASSERT((INT_GET(map->size, ARCH_CONVERT) & 0x3) == 0);
	INT_MOD(map->size, ARCH_CONVERT,
		-xfs_attr_leaf_newentsize(args, mp->m_sb.sb_blocksize, &tmp));
	INT_SET(entry->nameidx, ARCH_CONVERT,
					INT_GET(map->base, ARCH_CONVERT)
				      + INT_GET(map->size, ARCH_CONVERT));
	INT_SET(entry->hashval, ARCH_CONVERT, args->hashval);
	entry->flags = tmp ? XFS_ATTR_LOCAL : 0;
	entry->flags |= (args->flags & ATTR_SECURE) ? XFS_ATTR_SECURE :
			((args->flags & ATTR_ROOT) ? XFS_ATTR_ROOT : 0);
	if (args->rename) {
		entry->flags |= XFS_ATTR_INCOMPLETE;
		if ((args->blkno2 == args->blkno) &&
		    (args->index2 <= args->index)) {
			args->index2++;
		}
	}
	xfs_da_log_buf(args->trans, bp,
			  XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));
	ASSERT((args->index == 0) || (INT_GET(entry->hashval, ARCH_CONVERT)
						>= INT_GET((entry-1)->hashval,
							    ARCH_CONVERT)));
	ASSERT((args->index == INT_GET(hdr->count, ARCH_CONVERT)-1) ||
	       (INT_GET(entry->hashval, ARCH_CONVERT)
			    <= (INT_GET((entry+1)->hashval, ARCH_CONVERT))));

	/*
	 * Copy the attribute name and value into the new space.
	 *
	 * For "remote" attribute values, simply note that we need to
	 * allocate space for the "remote" value.  We can't actually
	 * allocate the extents in this transaction, and we can't decide
	 * which blocks they should be as we might allocate more blocks
	 * as part of this transaction (a split operation for example).
	 */
	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, args->index);
		name_loc->namelen = args->namelen;
		INT_SET(name_loc->valuelen, ARCH_CONVERT, args->valuelen);
		memcpy((char *)name_loc->nameval, args->name, args->namelen);
		memcpy((char *)&name_loc->nameval[args->namelen], args->value,
				   INT_GET(name_loc->valuelen, ARCH_CONVERT));
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		name_rmt->namelen = args->namelen;
		memcpy((char *)name_rmt->name, args->name, args->namelen);
		entry->flags |= XFS_ATTR_INCOMPLETE;
		/* just in case */
		name_rmt->valuelen = 0;
		name_rmt->valueblk = 0;
		args->rmtblkno = 1;
		args->rmtblkcnt = XFS_B_TO_FSB(mp, args->valuelen);
	}
	xfs_da_log_buf(args->trans, bp,
	     XFS_DA_LOGRANGE(leaf, XFS_ATTR_LEAF_NAME(leaf, args->index),
				   xfs_attr_leaf_entsize(leaf, args->index)));

	/*
	 * Update the control info for this leaf node
	 */
	if (INT_GET(entry->nameidx, ARCH_CONVERT)
				< INT_GET(hdr->firstused, ARCH_CONVERT)) {
		/* both on-disk, don't endian-flip twice */
		hdr->firstused = entry->nameidx;
	}
	ASSERT(INT_GET(hdr->firstused, ARCH_CONVERT)
				>= ((INT_GET(hdr->count, ARCH_CONVERT)
					* sizeof(*entry))+sizeof(*hdr)));
	tmp = (INT_GET(hdr->count, ARCH_CONVERT)-1)
					* sizeof(xfs_attr_leaf_entry_t)
					+ sizeof(xfs_attr_leaf_hdr_t);
	map = &hdr->freemap[0];
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; map++, i++) {
		if (INT_GET(map->base, ARCH_CONVERT) == tmp) {
			INT_MOD(map->base, ARCH_CONVERT,
					sizeof(xfs_attr_leaf_entry_t));
			INT_MOD(map->size, ARCH_CONVERT,
					-sizeof(xfs_attr_leaf_entry_t));
		}
	}
	INT_MOD(hdr->usedbytes, ARCH_CONVERT,
				xfs_attr_leaf_entsize(leaf, args->index));
	xfs_da_log_buf(args->trans, bp,
		XFS_DA_LOGRANGE(leaf, hdr, sizeof(*hdr)));
	return(0);
}

/*
 * Garbage collect a leaf attribute list block by copying it to a new buffer.
 */
STATIC void
xfs_attr_leaf_compact(xfs_trans_t *trans, xfs_dabuf_t *bp)
{
	xfs_attr_leafblock_t *leaf_s, *leaf_d;
	xfs_attr_leaf_hdr_t *hdr_s, *hdr_d;
	xfs_mount_t *mp;
	char *tmpbuffer;

	mp = trans->t_mountp;
	tmpbuffer = kmem_alloc(XFS_LBSIZE(mp), KM_SLEEP);
	ASSERT(tmpbuffer != NULL);
	memcpy(tmpbuffer, bp->data, XFS_LBSIZE(mp));
	memset(bp->data, 0, XFS_LBSIZE(mp));

	/*
	 * Copy basic information
	 */
	leaf_s = (xfs_attr_leafblock_t *)tmpbuffer;
	leaf_d = bp->data;
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	hdr_d->info = hdr_s->info;	/* struct copy */
	INT_SET(hdr_d->firstused, ARCH_CONVERT, XFS_LBSIZE(mp));
	/* handle truncation gracefully */
	if (!hdr_d->firstused) {
		INT_SET(hdr_d->firstused, ARCH_CONVERT,
				XFS_LBSIZE(mp) - XFS_ATTR_LEAF_NAME_ALIGN);
	}
	hdr_d->usedbytes = 0;
	hdr_d->count = 0;
	hdr_d->holes = 0;
	INT_SET(hdr_d->freemap[0].base, ARCH_CONVERT,
					sizeof(xfs_attr_leaf_hdr_t));
	INT_SET(hdr_d->freemap[0].size, ARCH_CONVERT,
				INT_GET(hdr_d->firstused, ARCH_CONVERT)
			      - INT_GET(hdr_d->freemap[0].base, ARCH_CONVERT));

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate name/value pairs packed and in sequence.
	 */
	xfs_attr_leaf_moveents(leaf_s, 0, leaf_d, 0,
				(int)INT_GET(hdr_s->count, ARCH_CONVERT), mp);

	xfs_da_log_buf(trans, bp, 0, XFS_LBSIZE(mp) - 1);

	kmem_free(tmpbuffer, XFS_LBSIZE(mp));
}

/*
 * Redistribute the attribute list entries between two leaf nodes,
 * taking into account the size of the new entry.
 *
 * NOTE: if new block is empty, then it will get the upper half of the
 * old block.  At present, all (one) callers pass in an empty second block.
 *
 * This code adjusts the args->index/blkno and args->index2/blkno2 fields
 * to match what it is doing in splitting the attribute leaf block.  Those
 * values are used in "atomic rename" operations on attributes.  Note that
 * the "new" and "old" values can end up in different blocks.
 */
STATIC void
xfs_attr_leaf_rebalance(xfs_da_state_t *state, xfs_da_state_blk_t *blk1,
				       xfs_da_state_blk_t *blk2)
{
	xfs_da_args_t *args;
	xfs_da_state_blk_t *tmp_blk;
	xfs_attr_leafblock_t *leaf1, *leaf2;
	xfs_attr_leaf_hdr_t *hdr1, *hdr2;
	int count, totallen, max, space, swap;

	/*
	 * Set up environment.
	 */
	ASSERT(blk1->magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(blk2->magic == XFS_ATTR_LEAF_MAGIC);
	leaf1 = blk1->bp->data;
	leaf2 = blk2->bp->data;
	ASSERT(INT_GET(leaf1->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(INT_GET(leaf2->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	args = state->args;

	/*
	 * Check ordering of blocks, reverse if it makes things simpler.
	 *
	 * NOTE: Given that all (current) callers pass in an empty
	 * second block, this code should never set "swap".
	 */
	swap = 0;
	if (xfs_attr_leaf_order(blk1->bp, blk2->bp)) {
		tmp_blk = blk1;
		blk1 = blk2;
		blk2 = tmp_blk;
		leaf1 = blk1->bp->data;
		leaf2 = blk2->bp->data;
		swap = 1;
	}
	hdr1 = &leaf1->hdr;
	hdr2 = &leaf2->hdr;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.  Then get
	 * the direction to copy and the number of elements to move.
	 *
	 * "inleaf" is true if the new entry should be inserted into blk1.
	 * If "swap" is also true, then reverse the sense of "inleaf".
	 */
	state->inleaf = xfs_attr_leaf_figure_balance(state, blk1, blk2,
							    &count, &totallen);
	if (swap)
		state->inleaf = !state->inleaf;

	/*
	 * Move any entries required from leaf to leaf:
	 */
	if (count < INT_GET(hdr1->count, ARCH_CONVERT)) {
		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		/* number entries being moved */
		count = INT_GET(hdr1->count, ARCH_CONVERT) - count;
		space  = INT_GET(hdr1->usedbytes, ARCH_CONVERT) - totallen;
		space += count * sizeof(xfs_attr_leaf_entry_t);

		/*
		 * leaf2 is the destination, compact it if it looks tight.
		 */
		max  = INT_GET(hdr2->firstused, ARCH_CONVERT)
						- sizeof(xfs_attr_leaf_hdr_t);
		max -= INT_GET(hdr2->count, ARCH_CONVERT)
					* sizeof(xfs_attr_leaf_entry_t);
		if (space > max) {
			xfs_attr_leaf_compact(args->trans, blk2->bp);
		}

		/*
		 * Move high entries from leaf1 to low end of leaf2.
		 */
		xfs_attr_leaf_moveents(leaf1,
				INT_GET(hdr1->count, ARCH_CONVERT)-count,
				leaf2, 0, count, state->mp);

		xfs_da_log_buf(args->trans, blk1->bp, 0, state->blocksize-1);
		xfs_da_log_buf(args->trans, blk2->bp, 0, state->blocksize-1);
	} else if (count > INT_GET(hdr1->count, ARCH_CONVERT)) {
		/*
		 * I assert that since all callers pass in an empty
		 * second buffer, this code should never execute.
		 */

		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		/* number entries being moved */
		count -= INT_GET(hdr1->count, ARCH_CONVERT);
		space  = totallen - INT_GET(hdr1->usedbytes, ARCH_CONVERT);
		space += count * sizeof(xfs_attr_leaf_entry_t);

		/*
		 * leaf1 is the destination, compact it if it looks tight.
		 */
		max  = INT_GET(hdr1->firstused, ARCH_CONVERT)
						- sizeof(xfs_attr_leaf_hdr_t);
		max -= INT_GET(hdr1->count, ARCH_CONVERT)
					* sizeof(xfs_attr_leaf_entry_t);
		if (space > max) {
			xfs_attr_leaf_compact(args->trans, blk1->bp);
		}

		/*
		 * Move low entries from leaf2 to high end of leaf1.
		 */
		xfs_attr_leaf_moveents(leaf2, 0, leaf1,
				(int)INT_GET(hdr1->count, ARCH_CONVERT), count,
				state->mp);

		xfs_da_log_buf(args->trans, blk1->bp, 0, state->blocksize-1);
		xfs_da_log_buf(args->trans, blk2->bp, 0, state->blocksize-1);
	}

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	blk1->hashval =
	    INT_GET(leaf1->entries[INT_GET(leaf1->hdr.count,
				    ARCH_CONVERT)-1].hashval, ARCH_CONVERT);
	blk2->hashval =
	    INT_GET(leaf2->entries[INT_GET(leaf2->hdr.count,
				    ARCH_CONVERT)-1].hashval, ARCH_CONVERT);

	/*
	 * Adjust the expected index for insertion.
	 * NOTE: this code depends on the (current) situation that the
	 * second block was originally empty.
	 *
	 * If the insertion point moved to the 2nd block, we must adjust
	 * the index.  We must also track the entry just following the
	 * new entry for use in an "atomic rename" operation, that entry
	 * is always the "old" entry and the "new" entry is what we are
	 * inserting.  The index/blkno fields refer to the "old" entry,
	 * while the index2/blkno2 fields refer to the "new" entry.
	 */
	if (blk1->index > INT_GET(leaf1->hdr.count, ARCH_CONVERT)) {
		ASSERT(state->inleaf == 0);
		blk2->index = blk1->index
				- INT_GET(leaf1->hdr.count, ARCH_CONVERT);
		args->index = args->index2 = blk2->index;
		args->blkno = args->blkno2 = blk2->blkno;
	} else if (blk1->index == INT_GET(leaf1->hdr.count, ARCH_CONVERT)) {
		if (state->inleaf) {
			args->index = blk1->index;
			args->blkno = blk1->blkno;
			args->index2 = 0;
			args->blkno2 = blk2->blkno;
		} else {
			blk2->index = blk1->index
				    - INT_GET(leaf1->hdr.count, ARCH_CONVERT);
			args->index = args->index2 = blk2->index;
			args->blkno = args->blkno2 = blk2->blkno;
		}
	} else {
		ASSERT(state->inleaf == 1);
		args->index = args->index2 = blk1->index;
		args->blkno = args->blkno2 = blk1->blkno;
	}
}

/*
 * Examine entries until we reduce the absolute difference in
 * byte usage between the two blocks to a minimum.
 * GROT: Is this really necessary?  With other than a 512 byte blocksize,
 * GROT: there will always be enough room in either block for a new entry.
 * GROT: Do a double-split for this case?
 */
STATIC int
xfs_attr_leaf_figure_balance(xfs_da_state_t *state,
				    xfs_da_state_blk_t *blk1,
				    xfs_da_state_blk_t *blk2,
				    int *countarg, int *usedbytesarg)
{
	xfs_attr_leafblock_t *leaf1, *leaf2;
	xfs_attr_leaf_hdr_t *hdr1, *hdr2;
	xfs_attr_leaf_entry_t *entry;
	int count, max, index, totallen, half;
	int lastdelta, foundit, tmp;

	/*
	 * Set up environment.
	 */
	leaf1 = blk1->bp->data;
	leaf2 = blk2->bp->data;
	hdr1 = &leaf1->hdr;
	hdr2 = &leaf2->hdr;
	foundit = 0;
	totallen = 0;

	/*
	 * Examine entries until we reduce the absolute difference in
	 * byte usage between the two blocks to a minimum.
	 */
	max = INT_GET(hdr1->count, ARCH_CONVERT)
			+ INT_GET(hdr2->count, ARCH_CONVERT);
	half  = (max+1) * sizeof(*entry);
	half += INT_GET(hdr1->usedbytes, ARCH_CONVERT)
				+ INT_GET(hdr2->usedbytes, ARCH_CONVERT)
				+ xfs_attr_leaf_newentsize(state->args,
						     state->blocksize, NULL);
	half /= 2;
	lastdelta = state->blocksize;
	entry = &leaf1->entries[0];
	for (count = index = 0; count < max; entry++, index++, count++) {

#define XFS_ATTR_ABS(A)	(((A) < 0) ? -(A) : (A))
		/*
		 * The new entry is in the first block, account for it.
		 */
		if (count == blk1->index) {
			tmp = totallen + sizeof(*entry) +
				xfs_attr_leaf_newentsize(state->args,
							 state->blocksize,
							 NULL);
			if (XFS_ATTR_ABS(half - tmp) > lastdelta)
				break;
			lastdelta = XFS_ATTR_ABS(half - tmp);
			totallen = tmp;
			foundit = 1;
		}

		/*
		 * Wrap around into the second block if necessary.
		 */
		if (count == INT_GET(hdr1->count, ARCH_CONVERT)) {
			leaf1 = leaf2;
			entry = &leaf1->entries[0];
			index = 0;
		}

		/*
		 * Figure out if next leaf entry would be too much.
		 */
		tmp = totallen + sizeof(*entry) + xfs_attr_leaf_entsize(leaf1,
									index);
		if (XFS_ATTR_ABS(half - tmp) > lastdelta)
			break;
		lastdelta = XFS_ATTR_ABS(half - tmp);
		totallen = tmp;
#undef XFS_ATTR_ABS
	}

	/*
	 * Calculate the number of usedbytes that will end up in lower block.
	 * If new entry not in lower block, fix up the count.
	 */
	totallen -= count * sizeof(*entry);
	if (foundit) {
		totallen -= sizeof(*entry) +
				xfs_attr_leaf_newentsize(state->args,
							 state->blocksize,
							 NULL);
	}

	*countarg = count;
	*usedbytesarg = totallen;
	return(foundit);
}

/*========================================================================
 * Routines used for shrinking the Btree.
 *========================================================================*/

/*
 * Check a leaf block and its neighbors to see if the block should be
 * collapsed into one or the other neighbor.  Always keep the block
 * with the smaller block number.
 * If the current block is over 50% full, don't try to join it, return 0.
 * If the block is empty, fill in the state structure and return 2.
 * If it can be collapsed, fill in the state structure and return 1.
 * If nothing can be done, return 0.
 *
 * GROT: allow for INCOMPLETE entries in calculation.
 */
int
xfs_attr_leaf_toosmall(xfs_da_state_t *state, int *action)
{
	xfs_attr_leafblock_t *leaf;
	xfs_da_state_blk_t *blk;
	xfs_da_blkinfo_t *info;
	int count, bytes, forward, error, retval, i;
	xfs_dablk_t blkno;
	xfs_dabuf_t *bp;

	/*
	 * Check for the degenerate case of the block being over 50% full.
	 * If so, it's not worth even looking to see if we might be able
	 * to coalesce with a sibling.
	 */
	blk = &state->path.blk[ state->path.active-1 ];
	info = blk->bp->data;
	ASSERT(INT_GET(info->magic, ARCH_CONVERT) == XFS_ATTR_LEAF_MAGIC);
	leaf = (xfs_attr_leafblock_t *)info;
	count = INT_GET(leaf->hdr.count, ARCH_CONVERT);
	bytes = sizeof(xfs_attr_leaf_hdr_t) +
		count * sizeof(xfs_attr_leaf_entry_t) +
		INT_GET(leaf->hdr.usedbytes, ARCH_CONVERT);
	if (bytes > (state->blocksize >> 1)) {
		*action = 0;	/* blk over 50%, don't try to join */
		return(0);
	}

	/*
	 * Check for the degenerate case of the block being empty.
	 * If the block is empty, we'll simply delete it, no need to
	 * coalesce it with a sibling block.  We choose (aribtrarily)
	 * to merge with the forward block unless it is NULL.
	 */
	if (count == 0) {
		/*
		 * Make altpath point to the block we want to keep and
		 * path point to the block we want to drop (this one).
		 */
		forward = info->forw;
		memcpy(&state->altpath, &state->path, sizeof(state->path));
		error = xfs_da_path_shift(state, &state->altpath, forward,
						 0, &retval);
		if (error)
			return(error);
		if (retval) {
			*action = 0;
		} else {
			*action = 2;
		}
		return(0);
	}

	/*
	 * Examine each sibling block to see if we can coalesce with
	 * at least 25% free space to spare.  We need to figure out
	 * whether to merge with the forward or the backward block.
	 * We prefer coalescing with the lower numbered sibling so as
	 * to shrink an attribute list over time.
	 */
	/* start with smaller blk num */
	forward = (INT_GET(info->forw, ARCH_CONVERT)
					< INT_GET(info->back, ARCH_CONVERT));
	for (i = 0; i < 2; forward = !forward, i++) {
		if (forward)
			blkno = INT_GET(info->forw, ARCH_CONVERT);
		else
			blkno = INT_GET(info->back, ARCH_CONVERT);
		if (blkno == 0)
			continue;
		error = xfs_da_read_buf(state->args->trans, state->args->dp,
					blkno, -1, &bp, XFS_ATTR_FORK);
		if (error)
			return(error);
		ASSERT(bp != NULL);

		leaf = (xfs_attr_leafblock_t *)info;
		count  = INT_GET(leaf->hdr.count, ARCH_CONVERT);
		bytes  = state->blocksize - (state->blocksize>>2);
		bytes -= INT_GET(leaf->hdr.usedbytes, ARCH_CONVERT);
		leaf = bp->data;
		ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
		count += INT_GET(leaf->hdr.count, ARCH_CONVERT);
		bytes -= INT_GET(leaf->hdr.usedbytes, ARCH_CONVERT);
		bytes -= count * sizeof(xfs_attr_leaf_entry_t);
		bytes -= sizeof(xfs_attr_leaf_hdr_t);
		xfs_da_brelse(state->args->trans, bp);
		if (bytes >= 0)
			break;	/* fits with at least 25% to spare */
	}
	if (i >= 2) {
		*action = 0;
		return(0);
	}

	/*
	 * Make altpath point to the block we want to keep (the lower
	 * numbered block) and path point to the block we want to drop.
	 */
	memcpy(&state->altpath, &state->path, sizeof(state->path));
	if (blkno < blk->blkno) {
		error = xfs_da_path_shift(state, &state->altpath, forward,
						 0, &retval);
	} else {
		error = xfs_da_path_shift(state, &state->path, forward,
						 0, &retval);
	}
	if (error)
		return(error);
	if (retval) {
		*action = 0;
	} else {
		*action = 1;
	}
	return(0);
}

/*
 * Remove a name from the leaf attribute list structure.
 *
 * Return 1 if leaf is less than 37% full, 0 if >= 37% full.
 * If two leaves are 37% full, when combined they will leave 25% free.
 */
int
xfs_attr_leaf_remove(xfs_dabuf_t *bp, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_hdr_t *hdr;
	xfs_attr_leaf_map_t *map;
	xfs_attr_leaf_entry_t *entry;
	int before, after, smallest, entsize;
	int tablesize, tmp, i;
	xfs_mount_t *mp;

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	mp = args->trans->t_mountp;
	ASSERT((INT_GET(hdr->count, ARCH_CONVERT) > 0)
		&& (INT_GET(hdr->count, ARCH_CONVERT) < (XFS_LBSIZE(mp)/8)));
	ASSERT((args->index >= 0)
		&& (args->index < INT_GET(hdr->count, ARCH_CONVERT)));
	ASSERT(INT_GET(hdr->firstused, ARCH_CONVERT)
				>= ((INT_GET(hdr->count, ARCH_CONVERT)
					* sizeof(*entry))+sizeof(*hdr)));
	entry = &leaf->entries[args->index];
	ASSERT(INT_GET(entry->nameidx, ARCH_CONVERT)
				>= INT_GET(hdr->firstused, ARCH_CONVERT));
	ASSERT(INT_GET(entry->nameidx, ARCH_CONVERT) < XFS_LBSIZE(mp));

	/*
	 * Scan through free region table:
	 *    check for adjacency of free'd entry with an existing one,
	 *    find smallest free region in case we need to replace it,
	 *    adjust any map that borders the entry table,
	 */
	tablesize = INT_GET(hdr->count, ARCH_CONVERT)
					* sizeof(xfs_attr_leaf_entry_t)
					+ sizeof(xfs_attr_leaf_hdr_t);
	map = &hdr->freemap[0];
	tmp = INT_GET(map->size, ARCH_CONVERT);
	before = after = -1;
	smallest = XFS_ATTR_LEAF_MAPSIZE - 1;
	entsize = xfs_attr_leaf_entsize(leaf, args->index);
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; map++, i++) {
		ASSERT(INT_GET(map->base, ARCH_CONVERT) < XFS_LBSIZE(mp));
		ASSERT(INT_GET(map->size, ARCH_CONVERT) < XFS_LBSIZE(mp));
		if (INT_GET(map->base, ARCH_CONVERT) == tablesize) {
			INT_MOD(map->base, ARCH_CONVERT,
					-sizeof(xfs_attr_leaf_entry_t));
			INT_MOD(map->size, ARCH_CONVERT,
					sizeof(xfs_attr_leaf_entry_t));
		}

		if ((INT_GET(map->base, ARCH_CONVERT)
					+ INT_GET(map->size, ARCH_CONVERT))
				== INT_GET(entry->nameidx, ARCH_CONVERT)) {
			before = i;
		} else if (INT_GET(map->base, ARCH_CONVERT)
			== (INT_GET(entry->nameidx, ARCH_CONVERT) + entsize)) {
			after = i;
		} else if (INT_GET(map->size, ARCH_CONVERT) < tmp) {
			tmp = INT_GET(map->size, ARCH_CONVERT);
			smallest = i;
		}
	}

	/*
	 * Coalesce adjacent freemap regions,
	 * or replace the smallest region.
	 */
	if ((before >= 0) || (after >= 0)) {
		if ((before >= 0) && (after >= 0)) {
			map = &hdr->freemap[before];
			INT_MOD(map->size, ARCH_CONVERT, entsize);
			INT_MOD(map->size, ARCH_CONVERT,
				INT_GET(hdr->freemap[after].size,
							ARCH_CONVERT));
			hdr->freemap[after].base = 0;
			hdr->freemap[after].size = 0;
		} else if (before >= 0) {
			map = &hdr->freemap[before];
			INT_MOD(map->size, ARCH_CONVERT, entsize);
		} else {
			map = &hdr->freemap[after];
			/* both on-disk, don't endian flip twice */
			map->base = entry->nameidx;
			INT_MOD(map->size, ARCH_CONVERT, entsize);
		}
	} else {
		/*
		 * Replace smallest region (if it is smaller than free'd entry)
		 */
		map = &hdr->freemap[smallest];
		if (INT_GET(map->size, ARCH_CONVERT) < entsize) {
			INT_SET(map->base, ARCH_CONVERT,
					INT_GET(entry->nameidx, ARCH_CONVERT));
			INT_SET(map->size, ARCH_CONVERT, entsize);
		}
	}

	/*
	 * Did we remove the first entry?
	 */
	if (INT_GET(entry->nameidx, ARCH_CONVERT)
				== INT_GET(hdr->firstused, ARCH_CONVERT))
		smallest = 1;
	else
		smallest = 0;

	/*
	 * Compress the remaining entries and zero out the removed stuff.
	 */
	memset(XFS_ATTR_LEAF_NAME(leaf, args->index), 0, entsize);
	INT_MOD(hdr->usedbytes, ARCH_CONVERT, -entsize);
	xfs_da_log_buf(args->trans, bp,
	     XFS_DA_LOGRANGE(leaf, XFS_ATTR_LEAF_NAME(leaf, args->index),
				   entsize));

	tmp = (INT_GET(hdr->count, ARCH_CONVERT) - args->index)
					* sizeof(xfs_attr_leaf_entry_t);
	memmove((char *)entry, (char *)(entry+1), tmp);
	INT_MOD(hdr->count, ARCH_CONVERT, -1);
	xfs_da_log_buf(args->trans, bp,
	    XFS_DA_LOGRANGE(leaf, entry, tmp + sizeof(*entry)));
	entry = &leaf->entries[INT_GET(hdr->count, ARCH_CONVERT)];
	memset((char *)entry, 0, sizeof(xfs_attr_leaf_entry_t));

	/*
	 * If we removed the first entry, re-find the first used byte
	 * in the name area.  Note that if the entry was the "firstused",
	 * then we don't have a "hole" in our block resulting from
	 * removing the name.
	 */
	if (smallest) {
		tmp = XFS_LBSIZE(mp);
		entry = &leaf->entries[0];
		for (i = INT_GET(hdr->count, ARCH_CONVERT)-1;
						i >= 0; entry++, i--) {
			ASSERT(INT_GET(entry->nameidx, ARCH_CONVERT)
				>= INT_GET(hdr->firstused, ARCH_CONVERT));
			ASSERT(INT_GET(entry->nameidx, ARCH_CONVERT)
							< XFS_LBSIZE(mp));
			if (INT_GET(entry->nameidx, ARCH_CONVERT) < tmp)
				tmp = INT_GET(entry->nameidx, ARCH_CONVERT);
		}
		INT_SET(hdr->firstused, ARCH_CONVERT, tmp);
		if (!hdr->firstused) {
			INT_SET(hdr->firstused, ARCH_CONVERT,
					tmp - XFS_ATTR_LEAF_NAME_ALIGN);
		}
	} else {
		hdr->holes = 1;		/* mark as needing compaction */
	}
	xfs_da_log_buf(args->trans, bp,
			  XFS_DA_LOGRANGE(leaf, hdr, sizeof(*hdr)));

	/*
	 * Check if leaf is less than 50% full, caller may want to
	 * "join" the leaf with a sibling if so.
	 */
	tmp  = sizeof(xfs_attr_leaf_hdr_t);
	tmp += INT_GET(leaf->hdr.count, ARCH_CONVERT)
					* sizeof(xfs_attr_leaf_entry_t);
	tmp += INT_GET(leaf->hdr.usedbytes, ARCH_CONVERT);
	return(tmp < mp->m_attr_magicpct); /* leaf is < 37% full */
}

/*
 * Move all the attribute list entries from drop_leaf into save_leaf.
 */
void
xfs_attr_leaf_unbalance(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk,
				       xfs_da_state_blk_t *save_blk)
{
	xfs_attr_leafblock_t *drop_leaf, *save_leaf, *tmp_leaf;
	xfs_attr_leaf_hdr_t *drop_hdr, *save_hdr, *tmp_hdr;
	xfs_mount_t *mp;
	char *tmpbuffer;

	/*
	 * Set up environment.
	 */
	mp = state->mp;
	ASSERT(drop_blk->magic == XFS_ATTR_LEAF_MAGIC);
	ASSERT(save_blk->magic == XFS_ATTR_LEAF_MAGIC);
	drop_leaf = drop_blk->bp->data;
	save_leaf = save_blk->bp->data;
	ASSERT(INT_GET(drop_leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(INT_GET(save_leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	drop_hdr = &drop_leaf->hdr;
	save_hdr = &save_leaf->hdr;

	/*
	 * Save last hashval from dying block for later Btree fixup.
	 */
	drop_blk->hashval =
		INT_GET(drop_leaf->entries[INT_GET(drop_leaf->hdr.count,
						ARCH_CONVERT)-1].hashval,
								ARCH_CONVERT);

	/*
	 * Check if we need a temp buffer, or can we do it in place.
	 * Note that we don't check "leaf" for holes because we will
	 * always be dropping it, toosmall() decided that for us already.
	 */
	if (save_hdr->holes == 0) {
		/*
		 * dest leaf has no holes, so we add there.  May need
		 * to make some room in the entry array.
		 */
		if (xfs_attr_leaf_order(save_blk->bp, drop_blk->bp)) {
			xfs_attr_leaf_moveents(drop_leaf, 0, save_leaf, 0,
			     (int)INT_GET(drop_hdr->count, ARCH_CONVERT), mp);
		} else {
			xfs_attr_leaf_moveents(drop_leaf, 0, save_leaf,
				  INT_GET(save_hdr->count, ARCH_CONVERT),
				  (int)INT_GET(drop_hdr->count, ARCH_CONVERT),
				  mp);
		}
	} else {
		/*
		 * Destination has holes, so we make a temporary copy
		 * of the leaf and add them both to that.
		 */
		tmpbuffer = kmem_alloc(state->blocksize, KM_SLEEP);
		ASSERT(tmpbuffer != NULL);
		memset(tmpbuffer, 0, state->blocksize);
		tmp_leaf = (xfs_attr_leafblock_t *)tmpbuffer;
		tmp_hdr = &tmp_leaf->hdr;
		tmp_hdr->info = save_hdr->info;	/* struct copy */
		tmp_hdr->count = 0;
		INT_SET(tmp_hdr->firstused, ARCH_CONVERT, state->blocksize);
		if (!tmp_hdr->firstused) {
			INT_SET(tmp_hdr->firstused, ARCH_CONVERT,
				state->blocksize - XFS_ATTR_LEAF_NAME_ALIGN);
		}
		tmp_hdr->usedbytes = 0;
		if (xfs_attr_leaf_order(save_blk->bp, drop_blk->bp)) {
			xfs_attr_leaf_moveents(drop_leaf, 0, tmp_leaf, 0,
				(int)INT_GET(drop_hdr->count, ARCH_CONVERT),
				mp);
			xfs_attr_leaf_moveents(save_leaf, 0, tmp_leaf,
				  INT_GET(tmp_leaf->hdr.count, ARCH_CONVERT),
				 (int)INT_GET(save_hdr->count, ARCH_CONVERT),
				 mp);
		} else {
			xfs_attr_leaf_moveents(save_leaf, 0, tmp_leaf, 0,
				(int)INT_GET(save_hdr->count, ARCH_CONVERT),
				mp);
			xfs_attr_leaf_moveents(drop_leaf, 0, tmp_leaf,
				INT_GET(tmp_leaf->hdr.count, ARCH_CONVERT),
				(int)INT_GET(drop_hdr->count, ARCH_CONVERT),
				mp);
		}
		memcpy((char *)save_leaf, (char *)tmp_leaf, state->blocksize);
		kmem_free(tmpbuffer, state->blocksize);
	}

	xfs_da_log_buf(state->args->trans, save_blk->bp, 0,
					   state->blocksize - 1);

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	save_blk->hashval =
		INT_GET(save_leaf->entries[INT_GET(save_leaf->hdr.count,
						ARCH_CONVERT)-1].hashval,
								ARCH_CONVERT);
}

/*========================================================================
 * Routines used for finding things in the Btree.
 *========================================================================*/

/*
 * Look up a name in a leaf attribute list structure.
 * This is the internal routine, it uses the caller's buffer.
 *
 * Note that duplicate keys are allowed, but only check within the
 * current leaf node.  The Btree code must check in adjacent leaf nodes.
 *
 * Return in args->index the index into the entry[] array of either
 * the found entry, or where the entry should have been (insert before
 * that entry).
 *
 * Don't change the args->value unless we find the attribute.
 */
int
xfs_attr_leaf_lookup_int(xfs_dabuf_t *bp, xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	int probe, span;
	xfs_dahash_t hashval;

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(INT_GET(leaf->hdr.count, ARCH_CONVERT)
					< (XFS_LBSIZE(args->dp->i_mount)/8));

	/*
	 * Binary search.  (note: small blocks will skip this loop)
	 */
	hashval = args->hashval;
	probe = span = INT_GET(leaf->hdr.count, ARCH_CONVERT) / 2;
	for (entry = &leaf->entries[probe]; span > 4;
		   entry = &leaf->entries[probe]) {
		span /= 2;
		if (INT_GET(entry->hashval, ARCH_CONVERT) < hashval)
			probe += span;
		else if (INT_GET(entry->hashval, ARCH_CONVERT) > hashval)
			probe -= span;
		else
			break;
	}
	ASSERT((probe >= 0) && 
	       (!leaf->hdr.count
	       || (probe < INT_GET(leaf->hdr.count, ARCH_CONVERT))));
	ASSERT((span <= 4) || (INT_GET(entry->hashval, ARCH_CONVERT)
							== hashval));

	/*
	 * Since we may have duplicate hashval's, find the first matching
	 * hashval in the leaf.
	 */
	while ((probe > 0) && (INT_GET(entry->hashval, ARCH_CONVERT)
							>= hashval)) {
		entry--;
		probe--;
	}
	while ((probe < INT_GET(leaf->hdr.count, ARCH_CONVERT))
		&& (INT_GET(entry->hashval, ARCH_CONVERT) < hashval)) {
		entry++;
		probe++;
	}
	if ((probe == INT_GET(leaf->hdr.count, ARCH_CONVERT))
		    || (INT_GET(entry->hashval, ARCH_CONVERT) != hashval)) {
		args->index = probe;
		return(XFS_ERROR(ENOATTR));
	}

	/*
	 * Duplicate keys may be present, so search all of them for a match.
	 */
	for (  ; (probe < INT_GET(leaf->hdr.count, ARCH_CONVERT))
			&& (INT_GET(entry->hashval, ARCH_CONVERT) == hashval);
			entry++, probe++) {
/*
 * GROT: Add code to remove incomplete entries.
 */
		/*
		 * If we are looking for INCOMPLETE entries, show only those.
		 * If we are looking for complete entries, show only those.
		 */
		if ((args->flags & XFS_ATTR_INCOMPLETE) !=
		    (entry->flags & XFS_ATTR_INCOMPLETE)) {
			continue;
		}
		if (entry->flags & XFS_ATTR_LOCAL) {
			name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, probe);
			if (name_loc->namelen != args->namelen)
				continue;
			if (memcmp(args->name, (char *)name_loc->nameval,
					     args->namelen) != 0)
				continue;
			if (((args->flags & ATTR_SECURE) != 0) !=
			    ((entry->flags & XFS_ATTR_SECURE) != 0))
				continue;
			if (((args->flags & ATTR_ROOT) != 0) !=
			    ((entry->flags & XFS_ATTR_ROOT) != 0))
				continue;
			args->index = probe;
			return(XFS_ERROR(EEXIST));
		} else {
			name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, probe);
			if (name_rmt->namelen != args->namelen)
				continue;
			if (memcmp(args->name, (char *)name_rmt->name,
					     args->namelen) != 0)
				continue;
			if (((args->flags & ATTR_SECURE) != 0) !=
			    ((entry->flags & XFS_ATTR_SECURE) != 0))
				continue;
			if (((args->flags & ATTR_ROOT) != 0) !=
			    ((entry->flags & XFS_ATTR_ROOT) != 0))
				continue;
			args->index = probe;
			args->rmtblkno
				  = INT_GET(name_rmt->valueblk, ARCH_CONVERT);
			args->rmtblkcnt = XFS_B_TO_FSB(args->dp->i_mount,
						   INT_GET(name_rmt->valuelen,
								ARCH_CONVERT));
			return(XFS_ERROR(EEXIST));
		}
	}
	args->index = probe;
	return(XFS_ERROR(ENOATTR));
}

/*
 * Get the value associated with an attribute name from a leaf attribute
 * list structure.
 */
int
xfs_attr_leaf_getvalue(xfs_dabuf_t *bp, xfs_da_args_t *args)
{
	int valuelen;
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(INT_GET(leaf->hdr.count, ARCH_CONVERT)
					< (XFS_LBSIZE(args->dp->i_mount)/8));
	ASSERT(args->index < ((int)INT_GET(leaf->hdr.count, ARCH_CONVERT)));

	entry = &leaf->entries[args->index];
	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, args->index);
		ASSERT(name_loc->namelen == args->namelen);
		ASSERT(memcmp(args->name, name_loc->nameval, args->namelen) == 0);
		valuelen = INT_GET(name_loc->valuelen, ARCH_CONVERT);
		if (args->flags & ATTR_KERNOVAL) {
			args->valuelen = valuelen;
			return(0);
		}
		if (args->valuelen < valuelen) {
			args->valuelen = valuelen;
			return(XFS_ERROR(ERANGE));
		}
		args->valuelen = valuelen;
		memcpy(args->value, &name_loc->nameval[args->namelen], valuelen);
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		ASSERT(name_rmt->namelen == args->namelen);
		ASSERT(memcmp(args->name, name_rmt->name, args->namelen) == 0);
		valuelen = INT_GET(name_rmt->valuelen, ARCH_CONVERT);
		args->rmtblkno = INT_GET(name_rmt->valueblk, ARCH_CONVERT);
		args->rmtblkcnt = XFS_B_TO_FSB(args->dp->i_mount, valuelen);
		if (args->flags & ATTR_KERNOVAL) {
			args->valuelen = valuelen;
			return(0);
		}
		if (args->valuelen < valuelen) {
			args->valuelen = valuelen;
			return(XFS_ERROR(ERANGE));
		}
		args->valuelen = valuelen;
	}
	return(0);
}

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Move the indicated entries from one leaf to another.
 * NOTE: this routine modifies both source and destination leaves.
 */
/*ARGSUSED*/
STATIC void
xfs_attr_leaf_moveents(xfs_attr_leafblock_t *leaf_s, int start_s,
			xfs_attr_leafblock_t *leaf_d, int start_d,
			int count, xfs_mount_t *mp)
{
	xfs_attr_leaf_hdr_t *hdr_s, *hdr_d;
	xfs_attr_leaf_entry_t *entry_s, *entry_d;
	int desti, tmp, i;

	/*
	 * Check for nothing to do.
	 */
	if (count == 0)
		return;

	/*
	 * Set up environment.
	 */
	ASSERT(INT_GET(leaf_s->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(INT_GET(leaf_d->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	ASSERT((INT_GET(hdr_s->count, ARCH_CONVERT) > 0)
				&& (INT_GET(hdr_s->count, ARCH_CONVERT)
						< (XFS_LBSIZE(mp)/8)));
	ASSERT(INT_GET(hdr_s->firstused, ARCH_CONVERT) >=
		((INT_GET(hdr_s->count, ARCH_CONVERT)
					* sizeof(*entry_s))+sizeof(*hdr_s)));
	ASSERT(INT_GET(hdr_d->count, ARCH_CONVERT) < (XFS_LBSIZE(mp)/8));
	ASSERT(INT_GET(hdr_d->firstused, ARCH_CONVERT) >=
		((INT_GET(hdr_d->count, ARCH_CONVERT)
					* sizeof(*entry_d))+sizeof(*hdr_d)));

	ASSERT(start_s < INT_GET(hdr_s->count, ARCH_CONVERT));
	ASSERT(start_d <= INT_GET(hdr_d->count, ARCH_CONVERT));
	ASSERT(count <= INT_GET(hdr_s->count, ARCH_CONVERT));

	/*
	 * Move the entries in the destination leaf up to make a hole?
	 */
	if (start_d < INT_GET(hdr_d->count, ARCH_CONVERT)) {
		tmp  = INT_GET(hdr_d->count, ARCH_CONVERT) - start_d;
		tmp *= sizeof(xfs_attr_leaf_entry_t);
		entry_s = &leaf_d->entries[start_d];
		entry_d = &leaf_d->entries[start_d + count];
		memmove((char *)entry_d, (char *)entry_s, tmp);
	}

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate attribute info packed and in sequence.
	 */
	entry_s = &leaf_s->entries[start_s];
	entry_d = &leaf_d->entries[start_d];
	desti = start_d;
	for (i = 0; i < count; entry_s++, entry_d++, desti++, i++) {
		ASSERT(INT_GET(entry_s->nameidx, ARCH_CONVERT)
				>= INT_GET(hdr_s->firstused, ARCH_CONVERT));
		tmp = xfs_attr_leaf_entsize(leaf_s, start_s + i);
#ifdef GROT
		/*
		 * Code to drop INCOMPLETE entries.  Difficult to use as we
		 * may also need to change the insertion index.  Code turned
		 * off for 6.2, should be revisited later.
		 */
		if (entry_s->flags & XFS_ATTR_INCOMPLETE) { /* skip partials? */
			memset(XFS_ATTR_LEAF_NAME(leaf_s, start_s + i), 0, tmp);
			INT_MOD(hdr_s->usedbytes, ARCH_CONVERT, -tmp);
			INT_MOD(hdr_s->count, ARCH_CONVERT, -1);
			entry_d--;	/* to compensate for ++ in loop hdr */
			desti--;
			if ((start_s + i) < offset)
				result++;	/* insertion index adjustment */
		} else {
#endif /* GROT */
			INT_MOD(hdr_d->firstused, ARCH_CONVERT, -tmp);
			/* both on-disk, don't endian flip twice */
			entry_d->hashval = entry_s->hashval;
			/* both on-disk, don't endian flip twice */
			entry_d->nameidx = hdr_d->firstused;
			entry_d->flags = entry_s->flags;
			ASSERT(INT_GET(entry_d->nameidx, ARCH_CONVERT) + tmp
							<= XFS_LBSIZE(mp));
			memmove(XFS_ATTR_LEAF_NAME(leaf_d, desti),
				XFS_ATTR_LEAF_NAME(leaf_s, start_s + i), tmp);
			ASSERT(INT_GET(entry_s->nameidx, ARCH_CONVERT) + tmp
							<= XFS_LBSIZE(mp));
			memset(XFS_ATTR_LEAF_NAME(leaf_s, start_s + i), 0, tmp);
			INT_MOD(hdr_s->usedbytes, ARCH_CONVERT, -tmp);
			INT_MOD(hdr_d->usedbytes, ARCH_CONVERT, tmp);
			INT_MOD(hdr_s->count, ARCH_CONVERT, -1);
			INT_MOD(hdr_d->count, ARCH_CONVERT, 1);
			tmp = INT_GET(hdr_d->count, ARCH_CONVERT)
						* sizeof(xfs_attr_leaf_entry_t)
						+ sizeof(xfs_attr_leaf_hdr_t);
			ASSERT(INT_GET(hdr_d->firstused, ARCH_CONVERT) >= tmp);
#ifdef GROT
		}
#endif /* GROT */
	}

	/*
	 * Zero out the entries we just copied.
	 */
	if (start_s == INT_GET(hdr_s->count, ARCH_CONVERT)) {
		tmp = count * sizeof(xfs_attr_leaf_entry_t);
		entry_s = &leaf_s->entries[start_s];
		ASSERT(((char *)entry_s + tmp) <=
		       ((char *)leaf_s + XFS_LBSIZE(mp)));
		memset((char *)entry_s, 0, tmp);
	} else {
		/*
		 * Move the remaining entries down to fill the hole,
		 * then zero the entries at the top.
		 */
		tmp  = INT_GET(hdr_s->count, ARCH_CONVERT) - count;
		tmp *= sizeof(xfs_attr_leaf_entry_t);
		entry_s = &leaf_s->entries[start_s + count];
		entry_d = &leaf_s->entries[start_s];
		memmove((char *)entry_d, (char *)entry_s, tmp);

		tmp = count * sizeof(xfs_attr_leaf_entry_t);
		entry_s = &leaf_s->entries[INT_GET(hdr_s->count,
							ARCH_CONVERT)];
		ASSERT(((char *)entry_s + tmp) <=
		       ((char *)leaf_s + XFS_LBSIZE(mp)));
		memset((char *)entry_s, 0, tmp);
	}

	/*
	 * Fill in the freemap information
	 */
	INT_SET(hdr_d->freemap[0].base, ARCH_CONVERT,
					sizeof(xfs_attr_leaf_hdr_t));
	INT_MOD(hdr_d->freemap[0].base, ARCH_CONVERT,
				INT_GET(hdr_d->count, ARCH_CONVERT)
					* sizeof(xfs_attr_leaf_entry_t));
	INT_SET(hdr_d->freemap[0].size, ARCH_CONVERT,
				INT_GET(hdr_d->firstused, ARCH_CONVERT)
			      - INT_GET(hdr_d->freemap[0].base, ARCH_CONVERT));
	hdr_d->freemap[1].base = 0;
	hdr_d->freemap[2].base = 0;
	hdr_d->freemap[1].size = 0;
	hdr_d->freemap[2].size = 0;
	hdr_s->holes = 1;	/* leaf may not be compact */
}

/*
 * Compare two leaf blocks "order".
 * Return 0 unless leaf2 should go before leaf1.
 */
int
xfs_attr_leaf_order(xfs_dabuf_t *leaf1_bp, xfs_dabuf_t *leaf2_bp)
{
	xfs_attr_leafblock_t *leaf1, *leaf2;

	leaf1 = leaf1_bp->data;
	leaf2 = leaf2_bp->data;
	ASSERT((INT_GET(leaf1->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC) &&
	       (INT_GET(leaf2->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC));
	if (   (INT_GET(leaf1->hdr.count, ARCH_CONVERT) > 0)
	    && (INT_GET(leaf2->hdr.count, ARCH_CONVERT) > 0)
	    && (   (INT_GET(leaf2->entries[ 0 ].hashval, ARCH_CONVERT) <
		      INT_GET(leaf1->entries[ 0 ].hashval, ARCH_CONVERT))
		|| (INT_GET(leaf2->entries[INT_GET(leaf2->hdr.count,
				ARCH_CONVERT)-1].hashval, ARCH_CONVERT) <
		      INT_GET(leaf1->entries[INT_GET(leaf1->hdr.count,
				ARCH_CONVERT)-1].hashval, ARCH_CONVERT))) ) {
		return(1);
	}
	return(0);
}

/*
 * Pick up the last hashvalue from a leaf block.
 */
xfs_dahash_t
xfs_attr_leaf_lasthash(xfs_dabuf_t *bp, int *count)
{
	xfs_attr_leafblock_t *leaf;

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	if (count)
		*count = INT_GET(leaf->hdr.count, ARCH_CONVERT);
	if (!leaf->hdr.count)
		return(0);
	return(INT_GET(leaf->entries[INT_GET(leaf->hdr.count,
				ARCH_CONVERT)-1].hashval, ARCH_CONVERT));
}

/*
 * Calculate the number of bytes used to store the indicated attribute
 * (whether local or remote only calculate bytes in this block).
 */
STATIC int
xfs_attr_leaf_entsize(xfs_attr_leafblock_t *leaf, int index)
{
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	int size;

	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	if (leaf->entries[index].flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, index);
		size = XFS_ATTR_LEAF_ENTSIZE_LOCAL(name_loc->namelen,
						   INT_GET(name_loc->valuelen,
								ARCH_CONVERT));
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, index);
		size = XFS_ATTR_LEAF_ENTSIZE_REMOTE(name_rmt->namelen);
	}
	return(size);
}

/*
 * Calculate the number of bytes that would be required to store the new
 * attribute (whether local or remote only calculate bytes in this block).
 * This routine decides as a side effect whether the attribute will be
 * a "local" or a "remote" attribute.
 */
int
xfs_attr_leaf_newentsize(xfs_da_args_t *args, int blocksize, int *local)
{
	int size;

	size = XFS_ATTR_LEAF_ENTSIZE_LOCAL(args->namelen, args->valuelen);
	if (size < XFS_ATTR_LEAF_ENTSIZE_LOCAL_MAX(blocksize)) {
		if (local) {
			*local = 1;
		}
	} else {
		size = XFS_ATTR_LEAF_ENTSIZE_REMOTE(args->namelen);
		if (local) {
			*local = 0;
		}
	}
	return(size);
}

/*
 * Copy out attribute list entries for attr_list(), for leaf attribute lists.
 */
int
xfs_attr_leaf_list_int(xfs_dabuf_t *bp, xfs_attr_list_context_t *context)
{
	attrlist_cursor_kern_t *cursor;
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_local_t *name_loc;
	xfs_attr_leaf_name_remote_t *name_rmt;
	int retval, i;

	ASSERT(bp != NULL);
	leaf = bp->data;
	cursor = context->cursor;
	cursor->initted = 1;

	xfs_attr_trace_l_cl("blk start", context, leaf);

	/*
	 * Re-find our place in the leaf block if this is a new syscall.
	 */
	if (context->resynch) {
		entry = &leaf->entries[0];
		for (i = 0; i < INT_GET(leaf->hdr.count, ARCH_CONVERT);
							entry++, i++) {
			if (INT_GET(entry->hashval, ARCH_CONVERT)
							== cursor->hashval) {
				if (cursor->offset == context->dupcnt) {
					context->dupcnt = 0;
					break;
				}
				context->dupcnt++;
			} else if (INT_GET(entry->hashval, ARCH_CONVERT)
							> cursor->hashval) {
				context->dupcnt = 0;
				break;
			}
		}
		if (i == INT_GET(leaf->hdr.count, ARCH_CONVERT)) {
			xfs_attr_trace_l_c("not found", context);
			return(0);
		}
	} else {
		entry = &leaf->entries[0];
		i = 0;
	}
	context->resynch = 0;

	/*
	 * We have found our place, start copying out the new attributes.
	 */
	retval = 0;
	for (  ; (i < INT_GET(leaf->hdr.count, ARCH_CONVERT))
	     && (retval == 0); entry++, i++) {
		attrnames_t	*namesp;

		if (INT_GET(entry->hashval, ARCH_CONVERT) != cursor->hashval) {
			cursor->hashval = INT_GET(entry->hashval, ARCH_CONVERT);
			cursor->offset = 0;
		}

		if (entry->flags & XFS_ATTR_INCOMPLETE)
			continue;		/* skip incomplete entries */
		if (((context->flags & ATTR_SECURE) != 0) !=
		    ((entry->flags & XFS_ATTR_SECURE) != 0) &&
		    !(context->flags & ATTR_KERNORMALS))
			continue;		/* skip non-matching entries */
		if (((context->flags & ATTR_ROOT) != 0) !=
		    ((entry->flags & XFS_ATTR_ROOT) != 0) &&
		    !(context->flags & ATTR_KERNROOTLS))
			continue;		/* skip non-matching entries */

		namesp = (entry->flags & XFS_ATTR_SECURE) ? &attr_secure :
			((entry->flags & XFS_ATTR_ROOT) ? &attr_trusted :
			  &attr_user);

		if (entry->flags & XFS_ATTR_LOCAL) {
			name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, i);
			if (context->flags & ATTR_KERNOVAL) {
				ASSERT(context->flags & ATTR_KERNAMELS);
				context->count += namesp->attr_namelen +
						(int)name_loc->namelen + 1;
			} else {
				retval = xfs_attr_put_listent(context, namesp,
					(char *)name_loc->nameval,
					(int)name_loc->namelen,
					(int)INT_GET(name_loc->valuelen,
								ARCH_CONVERT));
			}
		} else {
			name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, i);
			if (context->flags & ATTR_KERNOVAL) {
				ASSERT(context->flags & ATTR_KERNAMELS);
				context->count += namesp->attr_namelen +
						(int)name_rmt->namelen + 1;
			} else {
				retval = xfs_attr_put_listent(context, namesp,
					(char *)name_rmt->name,
					(int)name_rmt->namelen,
					(int)INT_GET(name_rmt->valuelen,
								ARCH_CONVERT));
			}
		}
		if (retval == 0) {
			cursor->offset++;
		}
	}
	xfs_attr_trace_l_cl("blk end", context, leaf);
	return(retval);
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
/*ARGSUSED*/
STATIC int
xfs_attr_put_listent(xfs_attr_list_context_t *context,
		     attrnames_t *namesp, char *name, int namelen, int valuelen)
{
	attrlist_ent_t *aep;
	int arraytop;

	ASSERT(!(context->flags & ATTR_KERNOVAL));
	if (context->flags & ATTR_KERNAMELS) {
		char *offset;

		ASSERT(context->count >= 0);

		arraytop = context->count + namesp->attr_namelen + namelen + 1;
		if (arraytop > context->firstu) {
			context->count = -1;	/* insufficient space */
			return(1);
		}
		offset = (char *)context->alist + context->count;
		strncpy(offset, namesp->attr_name, namesp->attr_namelen);
		offset += namesp->attr_namelen;
		strncpy(offset, name, namelen);			/* real name */
		offset += namelen;
		*offset = '\0';
		context->count += namesp->attr_namelen + namelen + 1;
		return(0);
	}

	ASSERT(context->count >= 0);
	ASSERT(context->count < (ATTR_MAX_VALUELEN/8));
	ASSERT(context->firstu >= sizeof(*context->alist));
	ASSERT(context->firstu <= context->bufsize);

	arraytop = sizeof(*context->alist) +
			context->count * sizeof(context->alist->al_offset[0]);
	context->firstu -= ATTR_ENTSIZE(namelen);
	if (context->firstu < arraytop) {
		xfs_attr_trace_l_c("buffer full", context);
		context->alist->al_more = 1;
		return(1);
	}

	aep = (attrlist_ent_t *)&(((char *)context->alist)[ context->firstu ]);
	aep->a_valuelen = valuelen;
	memcpy(aep->a_name, name, namelen);
	aep->a_name[ namelen ] = 0;
	context->alist->al_offset[ context->count++ ] = context->firstu;
	context->alist->al_count = context->count;
	xfs_attr_trace_l_c("add", context);
	return(0);
}

/*========================================================================
 * Manage the INCOMPLETE flag in a leaf entry
 *========================================================================*/

/*
 * Clear the INCOMPLETE flag on an entry in a leaf block.
 */
int
xfs_attr_leaf_clearflag(xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_remote_t *name_rmt;
	xfs_dabuf_t *bp;
	int error;
#ifdef DEBUG
	xfs_attr_leaf_name_local_t *name_loc;
	int namelen;
	char *name;
#endif /* DEBUG */

	/*
	 * Set up the operation.
	 */
	error = xfs_da_read_buf(args->trans, args->dp, args->blkno, -1, &bp,
					     XFS_ATTR_FORK);
	if (error) {
		return(error);
	}
	ASSERT(bp != NULL);

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(args->index < INT_GET(leaf->hdr.count, ARCH_CONVERT));
	ASSERT(args->index >= 0);
	entry = &leaf->entries[ args->index ];
	ASSERT(entry->flags & XFS_ATTR_INCOMPLETE);

#ifdef DEBUG
	if (entry->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf, args->index);
		namelen = name_loc->namelen;
		name = (char *)name_loc->nameval;
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		namelen = name_rmt->namelen;
		name = (char *)name_rmt->name;
	}
	ASSERT(INT_GET(entry->hashval, ARCH_CONVERT) == args->hashval);
	ASSERT(namelen == args->namelen);
	ASSERT(memcmp(name, args->name, namelen) == 0);
#endif /* DEBUG */

	entry->flags &= ~XFS_ATTR_INCOMPLETE;
	xfs_da_log_buf(args->trans, bp,
			 XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));

	if (args->rmtblkno) {
		ASSERT((entry->flags & XFS_ATTR_LOCAL) == 0);
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		INT_SET(name_rmt->valueblk, ARCH_CONVERT, args->rmtblkno);
		INT_SET(name_rmt->valuelen, ARCH_CONVERT, args->valuelen);
		xfs_da_log_buf(args->trans, bp,
			 XFS_DA_LOGRANGE(leaf, name_rmt, sizeof(*name_rmt)));
	}
	xfs_da_buf_done(bp);

	/*
	 * Commit the flag value change and start the next trans in series.
	 */
	error = xfs_attr_rolltrans(&args->trans, args->dp);

	return(error);
}

/*
 * Set the INCOMPLETE flag on an entry in a leaf block.
 */
int
xfs_attr_leaf_setflag(xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_remote_t *name_rmt;
	xfs_dabuf_t *bp;
	int error;

	/*
	 * Set up the operation.
	 */
	error = xfs_da_read_buf(args->trans, args->dp, args->blkno, -1, &bp,
					     XFS_ATTR_FORK);
	if (error) {
		return(error);
	}
	ASSERT(bp != NULL);

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(args->index < INT_GET(leaf->hdr.count, ARCH_CONVERT));
	ASSERT(args->index >= 0);
	entry = &leaf->entries[ args->index ];

	ASSERT((entry->flags & XFS_ATTR_INCOMPLETE) == 0);
	entry->flags |= XFS_ATTR_INCOMPLETE;
	xfs_da_log_buf(args->trans, bp,
			XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));
	if ((entry->flags & XFS_ATTR_LOCAL) == 0) {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, args->index);
		name_rmt->valueblk = 0;
		name_rmt->valuelen = 0;
		xfs_da_log_buf(args->trans, bp,
			 XFS_DA_LOGRANGE(leaf, name_rmt, sizeof(*name_rmt)));
	}
	xfs_da_buf_done(bp);

	/*
	 * Commit the flag value change and start the next trans in series.
	 */
	error = xfs_attr_rolltrans(&args->trans, args->dp);

	return(error);
}

/*
 * In a single transaction, clear the INCOMPLETE flag on the leaf entry
 * given by args->blkno/index and set the INCOMPLETE flag on the leaf
 * entry given by args->blkno2/index2.
 *
 * Note that they could be in different blocks, or in the same block.
 */
int
xfs_attr_leaf_flipflags(xfs_da_args_t *args)
{
	xfs_attr_leafblock_t *leaf1, *leaf2;
	xfs_attr_leaf_entry_t *entry1, *entry2;
	xfs_attr_leaf_name_remote_t *name_rmt;
	xfs_dabuf_t *bp1, *bp2;
	int error;
#ifdef DEBUG
	xfs_attr_leaf_name_local_t *name_loc;
	int namelen1, namelen2;
	char *name1, *name2;
#endif /* DEBUG */

	/*
	 * Read the block containing the "old" attr
	 */
	error = xfs_da_read_buf(args->trans, args->dp, args->blkno, -1, &bp1,
					     XFS_ATTR_FORK);
	if (error) {
		return(error);
	}
	ASSERT(bp1 != NULL);

	/*
	 * Read the block containing the "new" attr, if it is different
	 */
	if (args->blkno2 != args->blkno) {
		error = xfs_da_read_buf(args->trans, args->dp, args->blkno2,
					-1, &bp2, XFS_ATTR_FORK);
		if (error) {
			return(error);
		}
		ASSERT(bp2 != NULL);
	} else {
		bp2 = bp1;
	}

	leaf1 = bp1->data;
	ASSERT(INT_GET(leaf1->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(args->index < INT_GET(leaf1->hdr.count, ARCH_CONVERT));
	ASSERT(args->index >= 0);
	entry1 = &leaf1->entries[ args->index ];

	leaf2 = bp2->data;
	ASSERT(INT_GET(leaf2->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);
	ASSERT(args->index2 < INT_GET(leaf2->hdr.count, ARCH_CONVERT));
	ASSERT(args->index2 >= 0);
	entry2 = &leaf2->entries[ args->index2 ];

#ifdef DEBUG
	if (entry1->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf1, args->index);
		namelen1 = name_loc->namelen;
		name1 = (char *)name_loc->nameval;
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf1, args->index);
		namelen1 = name_rmt->namelen;
		name1 = (char *)name_rmt->name;
	}
	if (entry2->flags & XFS_ATTR_LOCAL) {
		name_loc = XFS_ATTR_LEAF_NAME_LOCAL(leaf2, args->index2);
		namelen2 = name_loc->namelen;
		name2 = (char *)name_loc->nameval;
	} else {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf2, args->index2);
		namelen2 = name_rmt->namelen;
		name2 = (char *)name_rmt->name;
	}
	ASSERT(INT_GET(entry1->hashval, ARCH_CONVERT) == INT_GET(entry2->hashval, ARCH_CONVERT));
	ASSERT(namelen1 == namelen2);
	ASSERT(memcmp(name1, name2, namelen1) == 0);
#endif /* DEBUG */

	ASSERT(entry1->flags & XFS_ATTR_INCOMPLETE);
	ASSERT((entry2->flags & XFS_ATTR_INCOMPLETE) == 0);

	entry1->flags &= ~XFS_ATTR_INCOMPLETE;
	xfs_da_log_buf(args->trans, bp1,
			  XFS_DA_LOGRANGE(leaf1, entry1, sizeof(*entry1)));
	if (args->rmtblkno) {
		ASSERT((entry1->flags & XFS_ATTR_LOCAL) == 0);
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf1, args->index);
		INT_SET(name_rmt->valueblk, ARCH_CONVERT, args->rmtblkno);
		INT_SET(name_rmt->valuelen, ARCH_CONVERT, args->valuelen);
		xfs_da_log_buf(args->trans, bp1,
			 XFS_DA_LOGRANGE(leaf1, name_rmt, sizeof(*name_rmt)));
	}

	entry2->flags |= XFS_ATTR_INCOMPLETE;
	xfs_da_log_buf(args->trans, bp2,
			  XFS_DA_LOGRANGE(leaf2, entry2, sizeof(*entry2)));
	if ((entry2->flags & XFS_ATTR_LOCAL) == 0) {
		name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf2, args->index2);
		name_rmt->valueblk = 0;
		name_rmt->valuelen = 0;
		xfs_da_log_buf(args->trans, bp2,
			 XFS_DA_LOGRANGE(leaf2, name_rmt, sizeof(*name_rmt)));
	}
	xfs_da_buf_done(bp1);
	if (bp1 != bp2)
		xfs_da_buf_done(bp2);

	/*
	 * Commit the flag value change and start the next trans in series.
	 */
	error = xfs_attr_rolltrans(&args->trans, args->dp);

	return(error);
}

/*========================================================================
 * Indiscriminately delete the entire attribute fork
 *========================================================================*/

/*
 * Recurse (gasp!) through the attribute nodes until we find leaves.
 * We're doing a depth-first traversal in order to invalidate everything.
 */
int
xfs_attr_root_inactive(xfs_trans_t **trans, xfs_inode_t *dp)
{
	xfs_da_blkinfo_t *info;
	xfs_daddr_t blkno;
	xfs_dabuf_t *bp;
	int error;

	/*
	 * Read block 0 to see what we have to work with.
	 * We only get here if we have extents, since we remove
	 * the extents in reverse order the extent containing
	 * block 0 must still be there.
	 */
	error = xfs_da_read_buf(*trans, dp, 0, -1, &bp, XFS_ATTR_FORK);
	if (error)
		return(error);
	blkno = xfs_da_blkno(bp);

	/*
	 * Invalidate the tree, even if the "tree" is only a single leaf block.
	 * This is a depth-first traversal!
	 */
	info = bp->data;
	if (INT_GET(info->magic, ARCH_CONVERT) == XFS_DA_NODE_MAGIC) {
		error = xfs_attr_node_inactive(trans, dp, bp, 1);
	} else if (INT_GET(info->magic, ARCH_CONVERT) == XFS_ATTR_LEAF_MAGIC) {
		error = xfs_attr_leaf_inactive(trans, dp, bp);
	} else {
		error = XFS_ERROR(EIO);
		xfs_da_brelse(*trans, bp);
	}
	if (error)
		return(error);

	/*
	 * Invalidate the incore copy of the root block.
	 */
	error = xfs_da_get_buf(*trans, dp, 0, blkno, &bp, XFS_ATTR_FORK);
	if (error)
		return(error);
	xfs_da_binval(*trans, bp);	/* remove from cache */
	/*
	 * Commit the invalidate and start the next transaction.
	 */
	error = xfs_attr_rolltrans(trans, dp);

	return (error);
}

/*
 * Recurse (gasp!) through the attribute nodes until we find leaves.
 * We're doing a depth-first traversal in order to invalidate everything.
 */
STATIC int
xfs_attr_node_inactive(xfs_trans_t **trans, xfs_inode_t *dp, xfs_dabuf_t *bp,
				   int level)
{
	xfs_da_blkinfo_t *info;
	xfs_da_intnode_t *node;
	xfs_dablk_t child_fsb;
	xfs_daddr_t parent_blkno, child_blkno;
	int error, count, i;
	xfs_dabuf_t *child_bp;

	/*
	 * Since this code is recursive (gasp!) we must protect ourselves.
	 */
	if (level > XFS_DA_NODE_MAXDEPTH) {
		xfs_da_brelse(*trans, bp);	/* no locks for later trans */
		return(XFS_ERROR(EIO));
	}

	node = bp->data;
	ASSERT(INT_GET(node->hdr.info.magic, ARCH_CONVERT)
						== XFS_DA_NODE_MAGIC);
	parent_blkno = xfs_da_blkno(bp);	/* save for re-read later */
	count = INT_GET(node->hdr.count, ARCH_CONVERT);
	if (!count) {
		xfs_da_brelse(*trans, bp);
		return(0);
	}
	child_fsb = INT_GET(node->btree[0].before, ARCH_CONVERT);
	xfs_da_brelse(*trans, bp);	/* no locks for later trans */

	/*
	 * If this is the node level just above the leaves, simply loop
	 * over the leaves removing all of them.  If this is higher up
	 * in the tree, recurse downward.
	 */
	for (i = 0; i < count; i++) {
		/*
		 * Read the subsidiary block to see what we have to work with.
		 * Don't do this in a transaction.  This is a depth-first
		 * traversal of the tree so we may deal with many blocks
		 * before we come back to this one.
		 */
		error = xfs_da_read_buf(*trans, dp, child_fsb, -2, &child_bp,
						XFS_ATTR_FORK);
		if (error)
			return(error);
		if (child_bp) {
						/* save for re-read later */
			child_blkno = xfs_da_blkno(child_bp);

			/*
			 * Invalidate the subtree, however we have to.
			 */
			info = child_bp->data;
			if (INT_GET(info->magic, ARCH_CONVERT)
							== XFS_DA_NODE_MAGIC) {
				error = xfs_attr_node_inactive(trans, dp,
						child_bp, level+1);
			} else if (INT_GET(info->magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC) {
				error = xfs_attr_leaf_inactive(trans, dp,
						child_bp);
			} else {
				error = XFS_ERROR(EIO);
				xfs_da_brelse(*trans, child_bp);
			}
			if (error)
				return(error);

			/*
			 * Remove the subsidiary block from the cache
			 * and from the log.
			 */
			error = xfs_da_get_buf(*trans, dp, 0, child_blkno,
				&child_bp, XFS_ATTR_FORK);
			if (error)
				return(error);
			xfs_da_binval(*trans, child_bp);
		}

		/*
		 * If we're not done, re-read the parent to get the next
		 * child block number.
		 */
		if ((i+1) < count) {
			error = xfs_da_read_buf(*trans, dp, 0, parent_blkno,
				&bp, XFS_ATTR_FORK);
			if (error)
				return(error);
			child_fsb = INT_GET(node->btree[i+1].before, ARCH_CONVERT);
			xfs_da_brelse(*trans, bp);
		}
		/*
		 * Atomically commit the whole invalidate stuff.
		 */
		if ((error = xfs_attr_rolltrans(trans, dp)))
			return (error);
	}

	return(0);
}

/*
 * Invalidate all of the "remote" value regions pointed to by a particular
 * leaf block.
 * Note that we must release the lock on the buffer so that we are not
 * caught holding something that the logging code wants to flush to disk.
 */
STATIC int
xfs_attr_leaf_inactive(xfs_trans_t **trans, xfs_inode_t *dp, xfs_dabuf_t *bp)
{
	xfs_attr_leafblock_t *leaf;
	xfs_attr_leaf_entry_t *entry;
	xfs_attr_leaf_name_remote_t *name_rmt;
	xfs_attr_inactive_list_t *list, *lp;
	int error, count, size, tmp, i;

	leaf = bp->data;
	ASSERT(INT_GET(leaf->hdr.info.magic, ARCH_CONVERT)
						== XFS_ATTR_LEAF_MAGIC);

	/*
	 * Count the number of "remote" value extents.
	 */
	count = 0;
	entry = &leaf->entries[0];
	for (i = 0; i < INT_GET(leaf->hdr.count, ARCH_CONVERT); entry++, i++) {
		if (   INT_GET(entry->nameidx, ARCH_CONVERT)
		    && ((entry->flags & XFS_ATTR_LOCAL) == 0)) {
			name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, i);
			if (name_rmt->valueblk)
				count++;
		}
	}

	/*
	 * If there are no "remote" values, we're done.
	 */
	if (count == 0) {
		xfs_da_brelse(*trans, bp);
		return(0);
	}

	/*
	 * Allocate storage for a list of all the "remote" value extents.
	 */
	size = count * sizeof(xfs_attr_inactive_list_t);
	list = (xfs_attr_inactive_list_t *)kmem_alloc(size, KM_SLEEP);

	/*
	 * Identify each of the "remote" value extents.
	 */
	lp = list;
	entry = &leaf->entries[0];
	for (i = 0; i < INT_GET(leaf->hdr.count, ARCH_CONVERT); entry++, i++) {
		if (   INT_GET(entry->nameidx, ARCH_CONVERT)
		    && ((entry->flags & XFS_ATTR_LOCAL) == 0)) {
			name_rmt = XFS_ATTR_LEAF_NAME_REMOTE(leaf, i);
			if (name_rmt->valueblk) {
				/* both on-disk, don't endian flip twice */
				lp->valueblk = name_rmt->valueblk;
				INT_SET(lp->valuelen, ARCH_CONVERT,
						XFS_B_TO_FSB(dp->i_mount,
						    INT_GET(name_rmt->valuelen,
							      ARCH_CONVERT)));
				lp++;
			}
		}
	}
	xfs_da_brelse(*trans, bp);	/* unlock for trans. in freextent() */

	/*
	 * Invalidate each of the "remote" value extents.
	 */
	error = 0;
	for (lp = list, i = 0; i < count; i++, lp++) {
		tmp = xfs_attr_leaf_freextent(trans, dp,
						     INT_GET(lp->valueblk,
								ARCH_CONVERT),
						     INT_GET(lp->valuelen,
								ARCH_CONVERT));
		if (error == 0)
			error = tmp;	/* save only the 1st errno */
	}

	kmem_free((xfs_caddr_t)list, size);
	return(error);
}

/*
 * Look at all the extents for this logical region,
 * invalidate any buffers that are incore/in transactions.
 */
STATIC int
xfs_attr_leaf_freextent(xfs_trans_t **trans, xfs_inode_t *dp,
				    xfs_dablk_t blkno, int blkcnt)
{
	xfs_bmbt_irec_t map;
	xfs_dablk_t tblkno;
	int tblkcnt, dblkcnt, nmap, error;
	xfs_daddr_t dblkno;
	xfs_buf_t *bp;

	/*
	 * Roll through the "value", invalidating the attribute value's
	 * blocks.
	 */
	tblkno = blkno;
	tblkcnt = blkcnt;
	while (tblkcnt > 0) {
		/*
		 * Try to remember where we decided to put the value.
		 */
		nmap = 1;
		error = xfs_bmapi(*trans, dp, (xfs_fileoff_t)tblkno, tblkcnt,
					XFS_BMAPI_ATTRFORK | XFS_BMAPI_METADATA,
					NULL, 0, &map, &nmap, NULL);
		if (error) {
			return(error);
		}
		ASSERT(nmap == 1);
		ASSERT(map.br_startblock != DELAYSTARTBLOCK);

		/*
		 * If it's a hole, these are already unmapped
		 * so there's nothing to invalidate.
		 */
		if (map.br_startblock != HOLESTARTBLOCK) {

			dblkno = XFS_FSB_TO_DADDR(dp->i_mount,
						  map.br_startblock);
			dblkcnt = XFS_FSB_TO_BB(dp->i_mount,
						map.br_blockcount);
			bp = xfs_trans_get_buf(*trans,
					dp->i_mount->m_ddev_targp,
					dblkno, dblkcnt, XFS_BUF_LOCK);
			xfs_trans_binval(*trans, bp);
			/*
			 * Roll to next transaction.
			 */
			if ((error = xfs_attr_rolltrans(trans, dp)))
				return (error);
		}

		tblkno += map.br_blockcount;
		tblkcnt -= map.br_blockcount;
	}

	return(0);
}


/*
 * Roll from one trans in the sequence of PERMANENT transactions to the next.
 */
int
xfs_attr_rolltrans(xfs_trans_t **transp, xfs_inode_t *dp)
{
	xfs_trans_t *trans;
	unsigned int logres, count;
	int	error;

	/*
	 * Ensure that the inode is always logged.
	 */
	trans = *transp;
	xfs_trans_log_inode(trans, dp, XFS_ILOG_CORE);

	/*
	 * Copy the critical parameters from one trans to the next.
	 */
	logres = trans->t_log_res;
	count = trans->t_log_count;
	*transp = xfs_trans_dup(trans);

	/*
	 * Commit the current transaction.
	 * If this commit failed, then it'd just unlock those items that
	 * are not marked ihold. That also means that a filesystem shutdown
	 * is in progress. The caller takes the responsibility to cancel
	 * the duplicate transaction that gets returned.
	 */
	if ((error = xfs_trans_commit(trans, 0, NULL)))
		return (error);

	trans = *transp;

	/*
	 * Reserve space in the log for th next transaction.
	 * This also pushes items in the "AIL", the list of logged items,
	 * out to disk if they are taking up space at the tail of the log
	 * that we want to use.  This requires that either nothing be locked
	 * across this call, or that anything that is locked be logged in
	 * the prior and the next transactions.
	 */
	error = xfs_trans_reserve(trans, 0, logres, 0,
				  XFS_TRANS_PERM_LOG_RES, count);
	/*
	 *  Ensure that the inode is in the new transaction and locked.
	 */
	if (!error) {
		xfs_trans_ijoin(trans, dp, XFS_ILOCK_EXCL);
		xfs_trans_ihold(trans, dp);
	}
	return (error);

}
