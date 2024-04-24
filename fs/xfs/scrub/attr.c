// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2017-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_attr_sf.h"
#include "xfs_parent.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/dabtree.h"
#include "scrub/attr.h"
#include "scrub/listxattr.h"
#include "scrub/repair.h"

/* Free the buffers linked from the xattr buffer. */
static void
xchk_xattr_buf_cleanup(
	void			*priv)
{
	struct xchk_xattr_buf	*ab = priv;

	kvfree(ab->freemap);
	ab->freemap = NULL;
	kvfree(ab->usedmap);
	ab->usedmap = NULL;
	kvfree(ab->value);
	ab->value = NULL;
	ab->value_sz = 0;
	kvfree(ab->name);
	ab->name = NULL;
}

/*
 * Allocate the free space bitmap if we're trying harder; there are leaf blocks
 * in the attr fork; or we can't tell if there are leaf blocks.
 */
static inline bool
xchk_xattr_want_freemap(
	struct xfs_scrub	*sc)
{
	struct xfs_ifork	*ifp;

	if (sc->flags & XCHK_TRY_HARDER)
		return true;

	if (!sc->ip)
		return true;

	ifp = xfs_ifork_ptr(sc->ip, XFS_ATTR_FORK);
	if (!ifp)
		return false;

	return xfs_ifork_has_extents(ifp);
}

/*
 * Allocate enough memory to hold an attr value and attr block bitmaps,
 * reallocating the buffer if necessary.  Buffer contents are not preserved
 * across a reallocation.
 */
int
xchk_setup_xattr_buf(
	struct xfs_scrub	*sc,
	size_t			value_size)
{
	size_t			bmp_sz;
	struct xchk_xattr_buf	*ab = sc->buf;
	void			*new_val;

	bmp_sz = sizeof(long) * BITS_TO_LONGS(sc->mp->m_attr_geo->blksize);

	if (ab)
		goto resize_value;

	ab = kvzalloc(sizeof(struct xchk_xattr_buf), XCHK_GFP_FLAGS);
	if (!ab)
		return -ENOMEM;
	sc->buf = ab;
	sc->buf_cleanup = xchk_xattr_buf_cleanup;

	ab->usedmap = kvmalloc(bmp_sz, XCHK_GFP_FLAGS);
	if (!ab->usedmap)
		return -ENOMEM;

	if (xchk_xattr_want_freemap(sc)) {
		ab->freemap = kvmalloc(bmp_sz, XCHK_GFP_FLAGS);
		if (!ab->freemap)
			return -ENOMEM;
	}

	if (xchk_could_repair(sc)) {
		ab->name = kvmalloc(XATTR_NAME_MAX + 1, XCHK_GFP_FLAGS);
		if (!ab->name)
			return -ENOMEM;
	}

resize_value:
	if (ab->value_sz >= value_size)
		return 0;

	if (ab->value) {
		kvfree(ab->value);
		ab->value = NULL;
		ab->value_sz = 0;
	}

	new_val = kvmalloc(value_size, XCHK_GFP_FLAGS);
	if (!new_val)
		return -ENOMEM;

	ab->value = new_val;
	ab->value_sz = value_size;
	return 0;
}

/* Set us up to scrub an inode's extended attributes. */
int
xchk_setup_xattr(
	struct xfs_scrub	*sc)
{
	int			error;

	if (xchk_could_repair(sc)) {
		error = xrep_setup_xattr(sc);
		if (error)
			return error;
	}

	/*
	 * We failed to get memory while checking attrs, so this time try to
	 * get all the memory we're ever going to need.  Allocate the buffer
	 * without the inode lock held, which means we can sleep.
	 */
	if (sc->flags & XCHK_TRY_HARDER) {
		error = xchk_setup_xattr_buf(sc, XATTR_SIZE_MAX);
		if (error)
			return error;
	}

	return xchk_setup_inode_contents(sc, 0);
}

/* Extended Attributes */

/*
 * Check that an extended attribute key can be looked up by hash.
 *
 * We use the extended attribute walk helper to call this function for every
 * attribute key in an inode.  Once we're here, we load the attribute value to
 * see if any errors happen, or if we get more or less data than we expected.
 */
static int
xchk_xattr_actor(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip,
	unsigned int		attr_flags,
	const unsigned char	*name,
	unsigned int		namelen,
	const void		*value,
	unsigned int		valuelen,
	void			*priv)
{
	struct xfs_da_args		args = {
		.attr_filter		= attr_flags & XFS_ATTR_NSP_ONDISK_MASK,
		.geo			= sc->mp->m_attr_geo,
		.whichfork		= XFS_ATTR_FORK,
		.dp			= ip,
		.name			= name,
		.namelen		= namelen,
		.trans			= sc->tp,
		.valuelen		= valuelen,
		.owner			= ip->i_ino,
	};
	struct xchk_xattr_buf		*ab;
	int				error = 0;

	ab = sc->buf;

	if (xchk_should_terminate(sc, &error))
		return error;

	if (attr_flags & ~XFS_ATTR_ONDISK_MASK) {
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, args.blkno);
		return -ECANCELED;
	}

	if (attr_flags & XFS_ATTR_INCOMPLETE) {
		/* Incomplete attr key, just mark the inode for preening. */
		xchk_ino_set_preen(sc, ip->i_ino);
		return 0;
	}

	/* Does this name make sense? */
	if (!xfs_attr_namecheck(attr_flags, name, namelen)) {
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, args.blkno);
		return -ECANCELED;
	}

	/* Check parent pointer record. */
	if ((attr_flags & XFS_ATTR_PARENT) &&
	    !xfs_parent_valuecheck(sc->mp, value, valuelen)) {
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, args.blkno);
		return -ECANCELED;
	}

	/*
	 * Try to allocate enough memory to extract the attr value.  If that
	 * doesn't work, return -EDEADLOCK as a signal to try again with a
	 * maximally sized buffer.
	 */
	error = xchk_setup_xattr_buf(sc, valuelen);
	if (error == -ENOMEM)
		error = -EDEADLOCK;
	if (error)
		return error;

	/*
	 * Parent pointers are matched on attr name and value, so we must
	 * supply the xfs_parent_rec here when confirming that the dabtree
	 * indexing works correctly.
	 */
	if (attr_flags & XFS_ATTR_PARENT)
		memcpy(ab->value, value, valuelen);

	args.value = ab->value;

	/*
	 * Get the attr value to ensure that lookup can find this attribute
	 * through the dabtree indexing and that remote value retrieval also
	 * works correctly.
	 */
	xfs_attr_sethash(&args);
	error = xfs_attr_get_ilocked(&args);
	/* ENODATA means the hash lookup failed and the attr is bad */
	if (error == -ENODATA)
		error = -EFSCORRUPTED;
	if (!xchk_fblock_process_error(sc, XFS_ATTR_FORK, args.blkno,
			&error))
		return error;
	if (args.valuelen != valuelen)
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, args.blkno);

	return 0;
}

/*
 * Mark a range [start, start+len) in this map.  Returns true if the
 * region was free, and false if there's a conflict or a problem.
 *
 * Within a char, the lowest bit of the char represents the byte with
 * the smallest address
 */
bool
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
	struct xfs_attr3_icleaf_hdr	*leafhdr)
{
	struct xchk_xattr_buf		*ab = sc->buf;
	unsigned int			mapsize = sc->mp->m_attr_geo->blksize;
	int				i;

	/* Construct bitmap of freemap contents. */
	bitmap_zero(ab->freemap, mapsize);
	for (i = 0; i < XFS_ATTR_LEAF_MAPSIZE; i++) {
		if (!xchk_xattr_set_map(sc, ab->freemap,
				leafhdr->freemap[i].base,
				leafhdr->freemap[i].size))
			return false;
	}

	/* Look for bits that are set in freemap and are marked in use. */
	return !bitmap_intersects(ab->freemap, ab->usedmap, mapsize);
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
	struct xfs_attr_leaf_entry	*ent,
	int				idx,
	unsigned int			*usedbytes,
	__u32				*last_hashval)
{
	struct xfs_mount		*mp = ds->state->mp;
	struct xchk_xattr_buf		*ab = ds->sc->buf;
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

	if (!xchk_xattr_set_map(ds->sc, ab->usedmap, nameidx, namesize))
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
	struct xchk_xattr_buf		*ab = ds->sc->buf;
	char				*buf_end;
	size_t				off;
	__u32				last_hashval = 0;
	unsigned int			usedbytes = 0;
	unsigned int			hdrsize;
	int				i;

	if (*last_checked == blk->blkno)
		return 0;

	*last_checked = blk->blkno;
	bitmap_zero(ab->usedmap, mp->m_attr_geo->blksize);

	/* Check all the padding. */
	if (xfs_has_crc(ds->sc->mp)) {
		struct xfs_attr3_leafblock	*leaf3 = bp->b_addr;

		if (leaf3->hdr.pad1 != 0 || leaf3->hdr.pad2 != 0 ||
		    leaf3->hdr.info.hdr.pad != 0)
			xchk_da_set_corrupt(ds, level);
	} else {
		if (leaf->hdr.pad1 != 0 || leaf->hdr.info.pad != 0)
			xchk_da_set_corrupt(ds, level);
	}

	/* Check the leaf header */
	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &leafhdr, leaf);
	hdrsize = xfs_attr3_leaf_hdr_size(leaf);

	/*
	 * Empty xattr leaf blocks mapped at block 0 are probably a byproduct
	 * of a race between setxattr and a log shutdown.  Anywhere else in the
	 * attr fork is a corruption.
	 */
	if (leafhdr.count == 0) {
		if (blk->blkno == 0)
			xchk_da_set_preen(ds, level);
		else
			xchk_da_set_corrupt(ds, level);
	}
	if (leafhdr.usedbytes > mp->m_attr_geo->blksize)
		xchk_da_set_corrupt(ds, level);
	if (leafhdr.firstused > mp->m_attr_geo->blksize)
		xchk_da_set_corrupt(ds, level);
	if (leafhdr.firstused < hdrsize)
		xchk_da_set_corrupt(ds, level);
	if (!xchk_xattr_set_map(ds->sc, ab->usedmap, 0, hdrsize))
		xchk_da_set_corrupt(ds, level);
	if (leafhdr.holes)
		xchk_da_set_preen(ds, level);

	if (ds->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		goto out;

	entries = xfs_attr3_leaf_entryp(leaf);
	if ((char *)&entries[leafhdr.count] > (char *)leaf + leafhdr.firstused)
		xchk_da_set_corrupt(ds, level);

	buf_end = (char *)bp->b_addr + mp->m_attr_geo->blksize;
	for (i = 0, ent = entries; i < leafhdr.count; ent++, i++) {
		/* Mark the leaf entry itself. */
		off = (char *)ent - (char *)leaf;
		if (!xchk_xattr_set_map(ds->sc, ab->usedmap, off,
				sizeof(xfs_attr_leaf_entry_t))) {
			xchk_da_set_corrupt(ds, level);
			goto out;
		}

		/* Check the entry and nameval. */
		xchk_xattr_entry(ds, level, buf_end, leaf, &leafhdr,
				ent, i, &usedbytes, &last_hashval);

		if (ds->sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
			goto out;
	}

	if (!xchk_xattr_check_freemap(ds->sc, &leafhdr))
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
	int				level)
{
	struct xfs_mount		*mp = ds->state->mp;
	struct xfs_da_state_blk		*blk = &ds->state->path.blk[level];
	struct xfs_attr_leaf_name_local	*lentry;
	struct xfs_attr_leaf_name_remote	*rentry;
	struct xfs_buf			*bp;
	struct xfs_attr_leaf_entry	*ent;
	xfs_dahash_t			calc_hash;
	xfs_dahash_t			hash;
	int				nameidx;
	int				hdrsize;
	int				error;

	ASSERT(blk->magic == XFS_ATTR_LEAF_MAGIC);

	ent = xfs_attr3_leaf_entryp(blk->bp->b_addr) + blk->index;

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
	if (ent->flags & ~XFS_ATTR_ONDISK_MASK) {
		xchk_da_set_corrupt(ds, level);
		return 0;
	}
	if (!xfs_attr_check_namespace(ent->flags)) {
		xchk_da_set_corrupt(ds, level);
		return 0;
	}

	if (ent->flags & XFS_ATTR_LOCAL) {
		lentry = (struct xfs_attr_leaf_name_local *)
				(((char *)bp->b_addr) + nameidx);
		if (lentry->namelen <= 0) {
			xchk_da_set_corrupt(ds, level);
			goto out;
		}
		calc_hash = xfs_attr_hashval(mp, ent->flags, lentry->nameval,
					     lentry->namelen,
					     lentry->nameval + lentry->namelen,
					     be16_to_cpu(lentry->valuelen));
	} else {
		rentry = (struct xfs_attr_leaf_name_remote *)
				(((char *)bp->b_addr) + nameidx);
		if (rentry->namelen <= 0) {
			xchk_da_set_corrupt(ds, level);
			goto out;
		}
		if (ent->flags & XFS_ATTR_PARENT) {
			xchk_da_set_corrupt(ds, level);
			goto out;
		}
		calc_hash = xfs_attr_hashval(mp, ent->flags, rentry->name,
					     rentry->namelen, NULL,
					     be32_to_cpu(rentry->valuelen));
	}
	if (calc_hash != hash)
		xchk_da_set_corrupt(ds, level);

out:
	return error;
}

/* Check space usage of shortform attrs. */
STATIC int
xchk_xattr_check_sf(
	struct xfs_scrub		*sc)
{
	struct xchk_xattr_buf		*ab = sc->buf;
	struct xfs_ifork		*ifp = &sc->ip->i_af;
	struct xfs_attr_sf_hdr		*sf = ifp->if_data;
	struct xfs_attr_sf_entry	*sfe = xfs_attr_sf_firstentry(sf);
	struct xfs_attr_sf_entry	*next;
	unsigned char			*end = ifp->if_data + ifp->if_bytes;
	int				i;
	int				error = 0;

	bitmap_zero(ab->usedmap, ifp->if_bytes);
	xchk_xattr_set_map(sc, ab->usedmap, 0, sizeof(*sf));

	if ((unsigned char *)sfe > end) {
		xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
		return 0;
	}

	for (i = 0; i < sf->count; i++) {
		unsigned char		*name = sfe->nameval;
		unsigned char		*value = &sfe->nameval[sfe->namelen];

		if (xchk_should_terminate(sc, &error))
			return error;

		next = xfs_attr_sf_nextentry(sfe);
		if ((unsigned char *)next > end) {
			xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
			break;
		}

		/*
		 * Shortform entries do not set LOCAL or INCOMPLETE, so the
		 * only valid flag bits here are for namespaces.
		 */
		if (sfe->flags & ~XFS_ATTR_NSP_ONDISK_MASK) {
			xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
			break;
		}

		if (!xchk_xattr_set_map(sc, ab->usedmap,
				(char *)sfe - (char *)sf,
				sizeof(struct xfs_attr_sf_entry))) {
			xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
			break;
		}

		if (!xchk_xattr_set_map(sc, ab->usedmap,
				(char *)name - (char *)sf,
				sfe->namelen)) {
			xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
			break;
		}

		if (!xchk_xattr_set_map(sc, ab->usedmap,
				(char *)value - (char *)sf,
				sfe->valuelen)) {
			xchk_fblock_set_corrupt(sc, XFS_ATTR_FORK, 0);
			break;
		}

		sfe = next;
	}

	return 0;
}

/* Scrub the extended attribute metadata. */
int
xchk_xattr(
	struct xfs_scrub		*sc)
{
	xfs_dablk_t			last_checked = -1U;
	int				error = 0;

	if (!xfs_inode_hasattr(sc->ip))
		return -ENOENT;

	/* Allocate memory for xattr checking. */
	error = xchk_setup_xattr_buf(sc, 0);
	if (error == -ENOMEM)
		return -EDEADLOCK;
	if (error)
		return error;

	/* Check the physical structure of the xattr. */
	if (sc->ip->i_af.if_format == XFS_DINODE_FMT_LOCAL)
		error = xchk_xattr_check_sf(sc);
	else
		error = xchk_da_btree(sc, XFS_ATTR_FORK, xchk_xattr_rec,
				&last_checked);
	if (error)
		return error;

	if (sc->sm->sm_flags & XFS_SCRUB_OFLAG_CORRUPT)
		return 0;

	/*
	 * Look up every xattr in this file by name and hash.
	 *
	 * The VFS only locks i_rwsem when modifying attrs, so keep all
	 * three locks held because that's the only way to ensure we're
	 * the only thread poking into the da btree.  We traverse the da
	 * btree while holding a leaf buffer locked for the xattr name
	 * iteration, which doesn't really follow the usual buffer
	 * locking order.
	 */
	error = xchk_xattr_walk(sc, sc->ip, xchk_xattr_actor, NULL, NULL);
	if (!xchk_fblock_process_error(sc, XFS_ATTR_FORK, 0, &error))
		return error;

	return 0;
}
