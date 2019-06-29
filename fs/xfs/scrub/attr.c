// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2017 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/dabtree.h"


/* Set us up to scrub an inode's extended attributes. */
int
xchk_setup_xattr(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	size_t			sz;

	/*
	 * Allocate the buffer without the inode lock held.  We need enough
	 * space to read every xattr value in the file or enough space to
	 * hold three copies of the xattr free space bitmap.  (Not both at
	 * the same time.)
	 */
	sz = max_t(size_t, XATTR_SIZE_MAX, 3 * sizeof(long) *
			BITS_TO_LONGS(sc->mp->m_attr_geo->blksize));
	sc->buf = kmem_zalloc_large(sz, KM_SLEEP);
	if (!sc->buf)
		return -ENOMEM;

	return xchk_setup_inode_contents(sc, ip, 0);
}

/* Extended Attributes */

struct xchk_xattr {
	struct xfs_attr_list_context	context;
	struct xfs_scrub		*sc;
};

/*
 * Check that an extended attribute key can be looked up by hash.
 *
 * We use the XFS attribute list iterator (i.e. xfs_attr_list_int_ilocked)
 * to call this function for every attribute key in an inode.  Once
 * we're here, we load the attribute value to see if any errors happen,
 * or if we get more or less data than we expected.
 */
static void
xchk_xattr_listent(
	struct xfs_attr_list_context	*context,
	int				flags,
	unsigned char			*name,
	int				namelen,
	int				valuelen)
{
	struct xchk_xattr		*sx;
	struct xfs_da_args		args = { NULL };
	int				error = 0;

	sx = container_of(context, struct xchk_xattr, context);

	if (xchk_should_terminate(sx->sc, &error)) {
		context->seen_enough = 1;
		return;
	}

	if (flags & XFS_ATTR_INCOMPLETE) {
		/* Incomplete attr key, just mark the inode for preening. */
		xchk_ino_set_preen(sx->sc, context->dp->i_ino);
		return;
	}

	/* Does this name make sense? */
	if (!xfs_attr_namecheck(name, namelen)) {
		xchk_fblock_set_corrupt(sx->sc, XFS_ATTR_FORK, args.blkno);
		return;
	}

	args.flags = ATTR_KERNOTIME;
	if (flags & XFS_ATTR_ROOT)
		args.flags |= ATTR_ROOT;
	else if (flags & XFS_ATTR_SECURE)
		args.flags |= ATTR_SECURE;
	args.geo = context->dp->i_mount->m_attr_geo;
	args.whichfork = XFS_ATTR_FORK;
	args.dp = context->dp;
	args.name = name;
	args.namelen = namelen;
	args.hashval = xfs_da_hashname(args.name, args.namelen);
	args.trans = context->tp;
	args.value = sx->sc->buf;
	args.valuelen = XATTR_SIZE_MAX;

	error = xfs_attr_get_ilocked(context->dp, &args);
	if (error == -EEXIST)
		error = 0;
	if (!xchk_fblock_process_error(sx->sc, XFS_ATTR_FORK, args.blkno,
			&error))
		goto fail_xref;
	if (args.valuelen != valuelen)
		xchk_fblock_set_corrupt(sx->sc, XFS_ATTR_FORK,
					     args.blkno);
fail_xref:
	if (sx->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		context->seen_enough = 1;
	return;
}

/*
 * Mark a range [start, start+len) in this map.  Returns true if the
 * region was free, and false if there's a conflict or a problem.
 *
 * Within a char, the lowest bit of the char represents the byte with
 * the smallest address
 */
STATIC bool
xchk_xattr_set_map(
	struct xfs_scrub	*sc,
	unsigned long		*map,
	unsigned int		start,
	unsigned int		len)
{
	unsigned int		mapsize = sc->mp->m_attr_geo->blksize;
	bool			ret = true;

	if (start >= mapsize)
		return false;
	if (start + len > mapsize) {
		len = mapsize - start;
		ret = false;
	}

	if (find_next_bit(map, mapsize, start) < start + len)
		ret = false;
	bitmap_set(map, start, len);

	return ret;
}

/*
 * Check the leaf freemap from the usage bitmap.  Returns false if the
 * attr freemap has problems or points to used space.
 */
STATIC bool
xchk_xattr_check_freemap(
	struct xfs_scrub		*sc,
	unsigned long			*map,
	struct xfs_attr3_icleaf_hdr	*leafhdr)
{
	unsigned long			*freemap;
	unsigned long			*dstmap;
	unsigned int			mapsize = sc->mp->m_attr_geo->blksize;
	int				i;

	/* Construct bitmap of freemap contents. */
	freemap = (unsigned long *)sc->buf + BITS_TO_LONGS(mapsize);
	bitmap_zero(freemap, mapsize);
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
		if (!xchk_xattr_set_map(sc, freemap,
				leafhdr->freemap[i].base,
				leafhdr->freemap[i].size))
			return false;
	}

	/* Look for bits that are set in freemap and are marked in use. */
	dstmap = freemap + BITS_TO_LONGS(mapsize);
	return bitmap_and(dstmap, freemap, map, mapsize) == 0;
}

/*
 * Check this leaf entry's relations to everything else.
 * Returns the number of bytes used for the name/value data.
 */
STATIC void
xchk_xattr_entry(
	struct xchk_da_btree		*ds,
	int				level,
	char				*buf_end,
	struct xfs_attr_leafblock	*leaf,
	struct xfs_attr3_icleaf_hdr	*leafhdr,
	unsigned long			*usedmap,
	struct xfs_attr_leaf_entry	*ent,
	int				idx,
	unsigned int			*usedbytes,
	__u32				*last_hashval)
{
	struct xfs_mount		*mp = ds->state->mp;
	char				*name_end;
	struct xfs_attr_leaf_name_local	*lentry;
	struct xfs_attr_leaf_name_remote *rentry;
	unsigned int			nameidx;
	unsigned int			namesize;

	if (ent->pad2 != 0)
		xchk_da_set_corrupt(ds, level);

	/* Hash values in order? */
	if (be32_to_cpu(ent->hashval) < *last_hashval)
		xchk_da_set_corrupt(ds, level);
	*last_hashval = be32_to_cpu(ent->hashval);

	nameidx = be16_to_cpu(ent->nameidx);
	if (nameidx < leafhdr->firstused ||
	    nameidx >= mp->m_attr_geo->blksize) {
		xchk_da_set_corrupt(ds, level);
		return;
	}

	/* Check the name information. */
	if (ent->flags & XFS_ATTR_LOCAL) {
		lentry = xfs_attr3_leaf_name_local(leaf, idx);
		namesize = xfs_attr_leaf_entsize_local(lentry->namelen,
				be16_to_cpu(lentry->valuelen));
		name_end = (char *)lentry + namesize;
		if (lentry->namelen == 0)
			xchk_da_set_corrupt(ds, level);
	} else {
		rentry = xfs_attr3_leaf_name_remote(leaf, idx);
		namesize = xfs_attr_leaf_entsize_remote(rentry->namelen);
		name_end = (char *)rentry + namesize;
		if (rentry->namelen == 0 || rentry->valueblk == 0)
			xchk_da_set_corrupt(ds, level);
	}
	if (name_end > buf_end)
		xchk_da_set_corrupt(ds, level);

	if (!xchk_xattr_set_map(ds->sc, usedmap, nameidx, namesize))
		xchk_da_set_corrupt(ds, level);
	if (!(ds->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT))
		*usedbytes += namesize;
}

/* Scrub an attribute leaf. */
STATIC int
xchk_xattr_block(
	struct xchk_da_btree		*ds,
	int				level)
{
	struct xfs_attr3_icleaf_hdr	leafhdr;
	struct xfs_mount		*mp = ds->state->mp;
	struct xfs_da_state_blk		*blk = &ds->state->path.blk[level];
	struct xfs_buf			*bp = blk->bp;
	xfs_dablk_t			*last_checked = ds->private;
	struct xfs_attr_leafblock	*leaf = bp->b_addr;
	struct xfs_attr_leaf_entry	*ent;
	struct xfs_attr_leaf_entry	*entries;
	unsigned long			*usedmap = ds->sc->buf;
	char				*buf_end;
	size_t				off;
	__u32				last_hashval = 0;
	unsigned int			usedbytes = 0;
	unsigned int			hdrsize;
	int				i;

	if (*last_checked == blk->blkno)
		return 0;
	*last_checked = blk->blkno;
	bitmap_zero(usedmap, mp->m_attr_geo->blksize);

	/* Check all the padding. */
	if (xfs_sb_version_hascrc(&ds->sc->mp->m_sb)) {
		struct xfs_attr3_leafblock	*leaf = bp->b_addr;

		if (leaf->hdr.pad1 != 0 || leaf->hdr.pad2 != 0 ||
		    leaf->hdr.info.hdr.pad != 0)
			xchk_da_set_corrupt(ds, level);
	} else {
		if (leaf->hdr.pad1 != 0 || leaf->hdr.info.pad != 0)
			xchk_da_set_corrupt(ds, level);
	}

	/* Check the leaf header */
	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &leafhdr, leaf);
	hdrsize = xfs_attr3_leaf_hdr_size(leaf);

	if (leafhdr.usedbytes > mp->m_attr_geo->blksize)
		xchk_da_set_corrupt(ds, level);
	if (leafhdr.firstused > mp->m_attr_geo->blksize)
		xchk_da_set_corrupt(ds, level);
	if (leafhdr.firstused < hdrsize)
		xchk_da_set_corrupt(ds, level);
	if (!xchk_xattr_set_map(ds->sc, usedmap, 0, hdrsize))
		xchk_da_set_corrupt(ds, level);

	if (ds->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	entries = xfs_attr3_leaf_entryp(leaf);
	if ((char *)&entries[leafhdr.count] > (char *)leaf + leafhdr.firstused)
		xchk_da_set_corrupt(ds, level);

	buf_end = (char *)bp->b_addr + mp->m_attr_geo->blksize;
	for (i = 0, ent = entries; i < leafhdr.count; ent++, i++) {
		/* Mark the leaf entry itself. */
		off = (char *)ent - (char *)leaf;
		if (!xchk_xattr_set_map(ds->sc, usedmap, off,
				sizeof(xfs_attr_leaf_entry_t))) {
			xchk_da_set_corrupt(ds, level);
			goto out;
		}

		/* Check the entry and nameval. */
		xchk_xattr_entry(ds, level, buf_end, leaf, &leafhdr,
				usedmap, ent, i, &usedbytes, &last_hashval);

		if (ds->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			goto out;
	}

	if (!xchk_xattr_check_freemap(ds->sc, usedmap, &leafhdr))
		xchk_da_set_corrupt(ds, level);

	if (leafhdr.usedbytes != usedbytes)
		xchk_da_set_corrupt(ds, level);

out:
	return 0;
}

/* Scrub a attribute btree record. */
STATIC int
xchk_xattr_rec(
	struct xchk_da_btree		*ds,
	int				level,
	void				*rec)
{
	struct xfs_mount		*mp = ds->state->mp;
	struct xfs_attr_leaf_entry	*ent = rec;
	struct xfs_da_state_blk		*blk;
	struct xfs_attr_leaf_name_local	*lentry;
	struct xfs_attr_leaf_name_remote	*rentry;
	struct xfs_buf			*bp;
	xfs_dahash_t			calc_hash;
	xfs_dahash_t			hash;
	int				nameidx;
	int				hdrsize;
	unsigned int			badflags;
	int				error;

	blk = &ds->state->path.blk[level];

	/* Check the whole block, if necessary. */
	error = xchk_xattr_block(ds, level);
	if (error)
		goto out;
	if (ds->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Check the hash of the entry. */
	error = xchk_da_btree_hash(ds, level, &ent->hashval);
	if (error)
		goto out;

	/* Find the attr entry's location. */
	bp = blk->bp;
	hdrsize = xfs_attr3_leaf_hdr_size(bp->b_addr);
	nameidx = be16_to_cpu(ent->nameidx);
	if (nameidx < hdrsize || nameidx >= mp->m_attr_geo->blksize) {
		xchk_da_set_corrupt(ds, level);
		goto out;
	}

	/* Retrieve the entry and check it. */
	hash = be32_to_cpu(ent->hashval);
	badflags = ~(XFS_ATTR_LOCAL | XFS_ATTR_ROOT | XFS_ATTR_SECURE |
			XFS_ATTR_INCOMPLETE);
	if ((ent->flags & badflags) != 0)
		xchk_da_set_corrupt(ds, level);
	if (ent->flags & XFS_ATTR_LOCAL) {
		lentry = (struct xfs_attr_leaf_name_local *)
				(((char *)bp->b_addr) + nameidx);
		if (lentry->namelen <= 0) {
			xchk_da_set_corrupt(ds, level);
			goto out;
		}
		calc_hash = xfs_da_hashname(lentry->nameval, lentry->namelen);
	} else {
		rentry = (struct xfs_attr_leaf_name_remote *)
				(((char *)bp->b_addr) + nameidx);
		if (rentry->namelen <= 0) {
			xchk_da_set_corrupt(ds, level);
			goto out;
		}
		calc_hash = xfs_da_hashname(rentry->name, rentry->namelen);
	}
	if (calc_hash != hash)
		xchk_da_set_corrupt(ds, level);

out:
	return error;
}

/* Scrub the extended attribute metadata. */
int
xchk_xattr(
	struct xfs_scrub		*sc)
{
	struct xchk_xattr		sx;
	struct attrlist_cursor_kern	cursor = { 0 };
	xfs_dablk_t			last_checked = -1U;
	int				error = 0;

	if (!xfs_inode_hasattr(sc->ip))
		return -ENOENT;

	memset(&sx, 0, sizeof(sx));
	/* Check attribute tree structure */
	error = xchk_da_btree(sc, XFS_ATTR_FORK, xchk_xattr_rec,
			&last_checked);
	if (error)
		goto out;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	/* Check that every attr key can also be looked up by hash. */
	sx.context.dp = sc->ip;
	sx.context.cursor = &cursor;
	sx.context.resynch = 1;
	sx.context.put_listent = xchk_xattr_listent;
	sx.context.tp = sc->tp;
	sx.context.flags = ATTR_INCOMPLETE;
	sx.sc = sc;

	/*
	 * Look up every xattr in this file by name.
	 *
	 * Use the backend implementation of xfs_attr_list to call
	 * xchk_xattr_listent on every attribute key in this inode.
	 * In other words, we use the same iterator/callback mechanism
	 * that listattr uses to scrub extended attributes, though in our
	 * _listent function, we check the value of the attribute.
	 *
	 * The VFS only locks i_rwsem when modifying attrs, so keep all
	 * three locks held because that's the only way to ensure we're
	 * the only thread poking into the da btree.  We traverse the da
	 * btree while holding a leaf buffer locked for the xattr name
	 * iteration, which doesn't really follow the usual buffer
	 * locking order.
	 */
	error = xfs_attr_list_int_ilocked(&sx.context);
	if (!xchk_fblock_process_error(sc, XFS_ATTR_FORK, 0, &error))
		goto out;
out:
	return error;
}
