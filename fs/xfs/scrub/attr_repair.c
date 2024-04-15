// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2018-2024 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_btree.h"
#include "xfs_bit.h"
#include "xfs_log_format.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_inode.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_dir2.h"
#include "xfs_attr.h"
#include "xfs_attr_leaf.h"
#include "xfs_attr_sf.h"
#include "xfs_attr_remote.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_exchmaps.h"
#include "xfs_exchrange.h"
#include "xfs_acl.h"
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
#include "scrub/tempfile.h"
#include "scrub/tempexch.h"
#include "scrub/xfile.h"
#include "scrub/xfarray.h"
#include "scrub/xfblob.h"
#include "scrub/attr.h"
#include "scrub/reap.h"
#include "scrub/attr_repair.h"

/*
 * Extended Attribute Repair
 * =========================
 *
 * We repair extended attributes by reading the attr leaf blocks looking for
 * attributes entries that look salvageable (name passes verifiers, value can
 * be retrieved, etc).  Each extended attribute worth salvaging is stashed in
 * memory, and the stashed entries are periodically replayed into a temporary
 * file to constrain memory use.  Batching the construction of the temporary
 * extended attribute structure in this fashion reduces lock cycling of the
 * file being repaired and the temporary file.
 *
 * When salvaging completes, the remaining stashed attributes are replayed to
 * the temporary file.  An atomic file contents exchange is used to commit the
 * new xattr blocks to the file being repaired.  This will disrupt attrmulti
 * cursors.
 */

struct xrep_xattr_key {
	/* Cookie for retrieval of the xattr name. */
	xfblob_cookie		name_cookie;

	/* Cookie for retrieval of the xattr value. */
	xfblob_cookie		value_cookie;

	/* XFS_ATTR_* flags */
	int			flags;

	/* Length of the value and name. */
	uint32_t		valuelen;
	uint16_t		namelen;
};

/*
 * Stash up to 8 pages of attrs in xattr_records/xattr_blobs before we write
 * them to the temp file.
 */
#define XREP_XATTR_MAX_STASH_BYTES	(PAGE_SIZE * 8)

struct xrep_xattr {
	struct xfs_scrub	*sc;

	/* Information for exchanging attr fork mappings at the end. */
	struct xrep_tempexch	tx;

	/* xattr keys */
	struct xfarray		*xattr_records;

	/* xattr values */
	struct xfblob		*xattr_blobs;

	/* Number of attributes that we are salvaging. */
	unsigned long long	attrs_found;
};

/* Set up to recreate the extended attributes. */
int
xrep_setup_xattr(
	struct xfs_scrub	*sc)
{
	return xrep_tempfile_create(sc, S_IFREG);
}

/*
 * Decide if we want to salvage this attribute.  We don't bother with
 * incomplete or oversized keys or values.  The @value parameter can be null
 * for remote attrs.
 */
STATIC int
xrep_xattr_want_salvage(
	struct xrep_xattr	*rx,
	unsigned int		attr_flags,
	const void		*name,
	int			namelen,
	const void		*value,
	int			valuelen)
{
	if (attr_flags & XFS_ATTR_INCOMPLETE)
		return false;
	if (namelen > XATTR_NAME_MAX || namelen <= 0)
		return false;
	if (!xfs_attr_namecheck(name, namelen))
		return false;
	if (valuelen > XATTR_SIZE_MAX || valuelen < 0)
		return false;
	if (hweight32(attr_flags & XFS_ATTR_NSP_ONDISK_MASK) > 1)
		return false;
	return true;
}

/* Allocate an in-core record to hold xattrs while we rebuild the xattr data. */
STATIC int
xrep_xattr_salvage_key(
	struct xrep_xattr	*rx,
	int			flags,
	unsigned char		*name,
	int			namelen,
	unsigned char		*value,
	int			valuelen)
{
	struct xrep_xattr_key	key = {
		.valuelen	= valuelen,
		.flags		= flags & XFS_ATTR_NSP_ONDISK_MASK,
	};
	unsigned int		i = 0;
	int			error = 0;

	if (xchk_should_terminate(rx->sc, &error))
		return error;

	/*
	 * Truncate the name to the first character that would trip namecheck.
	 * If we no longer have a name after that, ignore this attribute.
	 */
	while (i < namelen && name[i] != 0)
		i++;
	if (i == 0)
		return 0;
	key.namelen = i;

	trace_xrep_xattr_salvage_rec(rx->sc->ip, flags, name, key.namelen,
			valuelen);

	error = xfblob_store(rx->xattr_blobs, &key.name_cookie, name,
			key.namelen);
	if (error)
		return error;

	error = xfblob_store(rx->xattr_blobs, &key.value_cookie, value,
			key.valuelen);
	if (error)
		return error;

	error = xfarray_append(rx->xattr_records, &key);
	if (error)
		return error;

	rx->attrs_found++;
	return 0;
}

/*
 * Record a shortform extended attribute key & value for later reinsertion
 * into the inode.
 */
STATIC int
xrep_xattr_salvage_sf_attr(
	struct xrep_xattr		*rx,
	struct xfs_attr_sf_hdr		*hdr,
	struct xfs_attr_sf_entry	*sfe)
{
	struct xfs_scrub		*sc = rx->sc;
	struct xchk_xattr_buf		*ab = sc->buf;
	unsigned char			*name = sfe->nameval;
	unsigned char			*value = &sfe->nameval[sfe->namelen];

	if (!xchk_xattr_set_map(sc, ab->usedmap, (char *)name - (char *)hdr,
			sfe->namelen))
		return 0;

	if (!xchk_xattr_set_map(sc, ab->usedmap, (char *)value - (char *)hdr,
			sfe->valuelen))
		return 0;

	if (!xrep_xattr_want_salvage(rx, sfe->flags, sfe->nameval,
			sfe->namelen, value, sfe->valuelen))
		return 0;

	return xrep_xattr_salvage_key(rx, sfe->flags, sfe->nameval,
			sfe->namelen, value, sfe->valuelen);
}

/*
 * Record a local format extended attribute key & value for later reinsertion
 * into the inode.
 */
STATIC int
xrep_xattr_salvage_local_attr(
	struct xrep_xattr		*rx,
	struct xfs_attr_leaf_entry	*ent,
	unsigned int			nameidx,
	const char			*buf_end,
	struct xfs_attr_leaf_name_local	*lentry)
{
	struct xchk_xattr_buf		*ab = rx->sc->buf;
	unsigned char			*value;
	unsigned int			valuelen;
	unsigned int			namesize;

	/*
	 * Decode the leaf local entry format.  If something seems wrong, we
	 * junk the attribute.
	 */
	value = &lentry->nameval[lentry->namelen];
	valuelen = be16_to_cpu(lentry->valuelen);
	namesize = xfs_attr_leaf_entsize_local(lentry->namelen, valuelen);
	if ((char *)lentry + namesize > buf_end)
		return 0;
	if (!xrep_xattr_want_salvage(rx, ent->flags, lentry->nameval,
			lentry->namelen, value, valuelen))
		return 0;
	if (!xchk_xattr_set_map(rx->sc, ab->usedmap, nameidx, namesize))
		return 0;

	/* Try to save this attribute. */
	return xrep_xattr_salvage_key(rx, ent->flags, lentry->nameval,
			lentry->namelen, value, valuelen);
}

/*
 * Record a remote format extended attribute key & value for later reinsertion
 * into the inode.
 */
STATIC int
xrep_xattr_salvage_remote_attr(
	struct xrep_xattr		*rx,
	struct xfs_attr_leaf_entry	*ent,
	unsigned int			nameidx,
	const char			*buf_end,
	struct xfs_attr_leaf_name_remote *rentry,
	unsigned int			ent_idx,
	struct xfs_buf			*leaf_bp)
{
	struct xchk_xattr_buf		*ab = rx->sc->buf;
	struct xfs_da_args		args = {
		.trans			= rx->sc->tp,
		.dp			= rx->sc->ip,
		.index			= ent_idx,
		.geo			= rx->sc->mp->m_attr_geo,
		.owner			= rx->sc->ip->i_ino,
		.attr_filter		= ent->flags & XFS_ATTR_NSP_ONDISK_MASK,
		.namelen		= rentry->namelen,
		.name			= rentry->name,
		.value			= ab->value,
		.valuelen		= be32_to_cpu(rentry->valuelen),
	};
	unsigned int			namesize;
	int				error;

	/*
	 * Decode the leaf remote entry format.  If something seems wrong, we
	 * junk the attribute.  Note that we should never find a zero-length
	 * remote attribute value.
	 */
	namesize = xfs_attr_leaf_entsize_remote(rentry->namelen);
	if ((char *)rentry + namesize > buf_end)
		return 0;
	if (args.valuelen == 0 ||
	    !xrep_xattr_want_salvage(rx, ent->flags, rentry->name,
			rentry->namelen, NULL, args.valuelen))
		return 0;
	if (!xchk_xattr_set_map(rx->sc, ab->usedmap, nameidx, namesize))
		return 0;

	/*
	 * Enlarge the buffer (if needed) to hold the value that we're trying
	 * to salvage from the old extended attribute data.
	 */
	error = xchk_setup_xattr_buf(rx->sc, args.valuelen);
	if (error == -ENOMEM)
		error = -EDEADLOCK;
	if (error)
		return error;

	/* Look up the remote value and stash it for reconstruction. */
	error = xfs_attr3_leaf_getvalue(leaf_bp, &args);
	if (error || args.rmtblkno == 0)
		goto err_free;

	error = xfs_attr_rmtval_get(&args);
	if (error)
		goto err_free;

	/* Try to save this attribute. */
	error = xrep_xattr_salvage_key(rx, ent->flags, rentry->name,
			rentry->namelen, ab->value, args.valuelen);
err_free:
	/* remote value was garbage, junk it */
	if (error == -EFSBADCRC || error == -EFSCORRUPTED)
		error = 0;
	return error;
}

/* Extract every xattr key that we can from this attr fork block. */
STATIC int
xrep_xattr_recover_leaf(
	struct xrep_xattr		*rx,
	struct xfs_buf			*bp)
{
	struct xfs_attr3_icleaf_hdr	leafhdr;
	struct xfs_scrub		*sc = rx->sc;
	struct xfs_mount		*mp = sc->mp;
	struct xfs_attr_leafblock	*leaf;
	struct xfs_attr_leaf_name_local	*lentry;
	struct xfs_attr_leaf_name_remote *rentry;
	struct xfs_attr_leaf_entry	*ent;
	struct xfs_attr_leaf_entry	*entries;
	struct xchk_xattr_buf		*ab = rx->sc->buf;
	char				*buf_end;
	size_t				off;
	unsigned int			nameidx;
	unsigned int			hdrsize;
	int				i;
	int				error = 0;

	bitmap_zero(ab->usedmap, mp->m_attr_geo->blksize);

	/* Check the leaf header */
	leaf = bp->b_addr;
	xfs_attr3_leaf_hdr_from_disk(mp->m_attr_geo, &leafhdr, leaf);
	hdrsize = xfs_attr3_leaf_hdr_size(leaf);
	xchk_xattr_set_map(sc, ab->usedmap, 0, hdrsize);
	entries = xfs_attr3_leaf_entryp(leaf);

	buf_end = (char *)bp->b_addr + mp->m_attr_geo->blksize;
	for (i = 0, ent = entries; i < leafhdr.count; ent++, i++) {
		if (xchk_should_terminate(sc, &error))
			return error;

		/* Skip key if it conflicts with something else? */
		off = (char *)ent - (char *)leaf;
		if (!xchk_xattr_set_map(sc, ab->usedmap, off,
				sizeof(xfs_attr_leaf_entry_t)))
			continue;

		/* Check the name information. */
		nameidx = be16_to_cpu(ent->nameidx);
		if (nameidx < leafhdr.firstused ||
		    nameidx >= mp->m_attr_geo->blksize)
			continue;

		if (ent->flags & XFS_ATTR_LOCAL) {
			lentry = xfs_attr3_leaf_name_local(leaf, i);
			error = xrep_xattr_salvage_local_attr(rx, ent, nameidx,
					buf_end, lentry);
		} else {
			rentry = xfs_attr3_leaf_name_remote(leaf, i);
			error = xrep_xattr_salvage_remote_attr(rx, ent, nameidx,
					buf_end, rentry, i, bp);
		}
		if (error)
			return error;
	}

	return 0;
}

/* Try to recover shortform attrs. */
STATIC int
xrep_xattr_recover_sf(
	struct xrep_xattr		*rx)
{
	struct xfs_scrub		*sc = rx->sc;
	struct xchk_xattr_buf		*ab = sc->buf;
	struct xfs_attr_sf_hdr		*hdr;
	struct xfs_attr_sf_entry	*sfe;
	struct xfs_attr_sf_entry	*next;
	struct xfs_ifork		*ifp;
	unsigned char			*end;
	int				i;
	int				error = 0;

	ifp = xfs_ifork_ptr(rx->sc->ip, XFS_ATTR_FORK);
	hdr = ifp->if_data;

	bitmap_zero(ab->usedmap, ifp->if_bytes);
	end = (unsigned char *)ifp->if_data + ifp->if_bytes;
	xchk_xattr_set_map(sc, ab->usedmap, 0, sizeof(*hdr));

	sfe = xfs_attr_sf_firstentry(hdr);
	if ((unsigned char *)sfe > end)
		return 0;

	for (i = 0; i < hdr->count; i++) {
		if (xchk_should_terminate(sc, &error))
			return error;

		next = xfs_attr_sf_nextentry(sfe);
		if ((unsigned char *)next > end)
			break;

		if (xchk_xattr_set_map(sc, ab->usedmap,
				(char *)sfe - (char *)hdr,
				sizeof(struct xfs_attr_sf_entry))) {
			/*
			 * No conflicts with the sf entry; let's save this
			 * attribute.
			 */
			error = xrep_xattr_salvage_sf_attr(rx, hdr, sfe);
			if (error)
				return error;
		}

		sfe = next;
	}

	return 0;
}

/*
 * Try to return a buffer of xattr data for a given physical extent.
 *
 * Because the buffer cache get function complains if it finds a buffer
 * matching the block number but not matching the length, we must be careful to
 * look for incore buffers (up to the maximum length of a remote value) that
 * could be hiding anywhere in the physical range.  If we find an incore
 * buffer, we can pass that to the caller.  Optionally, read a single block and
 * pass that back.
 *
 * Note the subtlety that remote attr value blocks for which there is no incore
 * buffer will be passed to the callback one block at a time.  These buffers
 * will not have any ops attached and must be staled to prevent aliasing with
 * multiblock buffers once we drop the ILOCK.
 */
STATIC int
xrep_xattr_find_buf(
	struct xfs_mount	*mp,
	xfs_fsblock_t		fsbno,
	xfs_extlen_t		max_len,
	bool			can_read,
	struct xfs_buf		**bpp)
{
	struct xrep_bufscan	scan = {
		.daddr		= XFS_FSB_TO_DADDR(mp, fsbno),
		.max_sectors	= xrep_bufscan_max_sectors(mp, max_len),
		.daddr_step	= XFS_FSB_TO_BB(mp, 1),
	};
	struct xfs_buf		*bp;

	while ((bp = xrep_bufscan_advance(mp, &scan)) != NULL) {
		*bpp = bp;
		return 0;
	}

	if (!can_read) {
		*bpp = NULL;
		return 0;
	}

	return xfs_buf_read(mp->m_ddev_targp, scan.daddr, XFS_FSB_TO_BB(mp, 1),
			XBF_TRYLOCK, bpp, NULL);
}

/*
 * Deal with a buffer that we found during our walk of the attr fork.
 *
 * Attribute leaf and node blocks are simple -- they're a single block, so we
 * can walk them one at a time and we never have to worry about discontiguous
 * multiblock buffers like we do for directories.
 *
 * Unfortunately, remote attr blocks add a lot of complexity here.  Each disk
 * block is totally self contained, in the sense that the v5 header provides no
 * indication that there could be more data in the next block.  The incore
 * buffers can span multiple blocks, though they never cross extent records.
 * However, they don't necessarily start or end on an extent record boundary.
 * Therefore, we need a special buffer find function to walk the buffer cache
 * for us.
 *
 * The caller must hold the ILOCK on the file being repaired.  We use
 * XBF_TRYLOCK here to skip any locked buffer on the assumption that we don't
 * own the block and don't want to hang the system on a potentially garbage
 * buffer.
 */
STATIC int
xrep_xattr_recover_block(
	struct xrep_xattr	*rx,
	xfs_dablk_t		dabno,
	xfs_fsblock_t		fsbno,
	xfs_extlen_t		max_len,
	xfs_extlen_t		*actual_len)
{
	struct xfs_da_blkinfo	*info;
	struct xfs_buf		*bp;
	int			error;

	error = xrep_xattr_find_buf(rx->sc->mp, fsbno, max_len, true, &bp);
	if (error)
		return error;
	info = bp->b_addr;
	*actual_len = XFS_BB_TO_FSB(rx->sc->mp, bp->b_length);

	trace_xrep_xattr_recover_leafblock(rx->sc->ip, dabno,
			be16_to_cpu(info->magic));

	/*
	 * If the buffer has the right magic number for an attr leaf block and
	 * passes a structure check (we don't care about checksums), salvage
	 * as much as we can from the block. */
	if (info->magic == cpu_to_be16(XFS_ATTR3_LEAF_MAGIC) &&
	    xrep_buf_verify_struct(bp, &xfs_attr3_leaf_buf_ops) &&
	    xfs_attr3_leaf_header_check(bp, rx->sc->ip->i_ino) == NULL)
		error = xrep_xattr_recover_leaf(rx, bp);

	/*
	 * If the buffer didn't already have buffer ops set, it was read in by
	 * the _find_buf function and could very well be /part/ of a multiblock
	 * remote block.  Mark it stale so that it doesn't hang around in
	 * memory to cause problems.
	 */
	if (bp->b_ops == NULL)
		xfs_buf_stale(bp);

	xfs_buf_relse(bp);
	return error;
}

/* Insert one xattr key/value. */
STATIC int
xrep_xattr_insert_rec(
	struct xrep_xattr		*rx,
	const struct xrep_xattr_key	*key)
{
	struct xfs_da_args		args = {
		.dp			= rx->sc->tempip,
		.attr_filter		= key->flags,
		.attr_flags		= XATTR_CREATE,
		.namelen		= key->namelen,
		.valuelen		= key->valuelen,
		.owner			= rx->sc->ip->i_ino,
	};
	struct xchk_xattr_buf		*ab = rx->sc->buf;
	int				error;

	/*
	 * Grab pointers to the scrub buffer so that we can use them to insert
	 * attrs into the temp file.
	 */
	args.name = ab->name;
	args.value = ab->value;

	/*
	 * The attribute name is stored near the end of the in-core buffer,
	 * though we reserve one more byte to ensure null termination.
	 */
	ab->name[XATTR_NAME_MAX] = 0;

	error = xfblob_load(rx->xattr_blobs, key->name_cookie, ab->name,
			key->namelen);
	if (error)
		return error;

	error = xfblob_free(rx->xattr_blobs, key->name_cookie);
	if (error)
		return error;

	error = xfblob_load(rx->xattr_blobs, key->value_cookie, args.value,
			key->valuelen);
	if (error)
		return error;

	error = xfblob_free(rx->xattr_blobs, key->value_cookie);
	if (error)
		return error;

	ab->name[key->namelen] = 0;

	trace_xrep_xattr_insert_rec(rx->sc->tempip, key->flags, ab->name,
			key->namelen, key->valuelen);

	/*
	 * xfs_attr_set creates and commits its own transaction.  If the attr
	 * already exists, we'll just drop it during the rebuild.
	 */
	error = xfs_attr_set(&args);
	if (error == -EEXIST)
		error = 0;

	return error;
}

/*
 * Periodically flush salvaged attributes to the temporary file.  This is done
 * to reduce the memory requirements of the xattr rebuild because files can
 * contain millions of attributes.
 */
STATIC int
xrep_xattr_flush_stashed(
	struct xrep_xattr	*rx)
{
	xfarray_idx_t		array_cur;
	int			error;

	/*
	 * Entering this function, the scrub context has a reference to the
	 * inode being repaired, the temporary file, and a scrub transaction
	 * that we use during xattr salvaging to avoid livelocking if there
	 * are cycles in the xattr structures.  We hold ILOCK_EXCL on both
	 * the inode being repaired, though it is not ijoined to the scrub
	 * transaction.
	 *
	 * To constrain kernel memory use, we occasionally flush salvaged
	 * xattrs from the xfarray and xfblob structures into the temporary
	 * file in preparation for exchanging the xattr structures at the end.
	 * Updating the temporary file requires a transaction, so we commit the
	 * scrub transaction and drop the two ILOCKs so that xfs_attr_set can
	 * allocate whatever transaction it wants.
	 *
	 * We still hold IOLOCK_EXCL on the inode being repaired, which
	 * prevents anyone from modifying the damaged xattr data while we
	 * repair it.
	 */
	error = xrep_trans_commit(rx->sc);
	if (error)
		return error;
	xchk_iunlock(rx->sc, XFS_ILOCK_EXCL);

	/*
	 * Take the IOLOCK of the temporary file while we modify xattrs.  This
	 * isn't strictly required because the temporary file is never revealed
	 * to userspace, but we follow the same locking rules.  We still hold
	 * sc->ip's IOLOCK.
	 */
	error = xrep_tempfile_iolock_polled(rx->sc);
	if (error)
		return error;

	/* Add all the salvaged attrs to the temporary file. */
	foreach_xfarray_idx(rx->xattr_records, array_cur) {
		struct xrep_xattr_key	key;

		error = xfarray_load(rx->xattr_records, array_cur, &key);
		if (error)
			return error;

		error = xrep_xattr_insert_rec(rx, &key);
		if (error)
			return error;
	}

	/* Empty out both arrays now that we've added the entries. */
	xfarray_truncate(rx->xattr_records);
	xfblob_truncate(rx->xattr_blobs);

	xrep_tempfile_iounlock(rx->sc);

	/* Recreate the salvage transaction and relock the inode. */
	error = xchk_trans_alloc(rx->sc, 0);
	if (error)
		return error;
	xchk_ilock(rx->sc, XFS_ILOCK_EXCL);
	return 0;
}

/* Decide if we've stashed too much xattr data in memory. */
static inline bool
xrep_xattr_want_flush_stashed(
	struct xrep_xattr	*rx)
{
	unsigned long long	bytes;

	bytes = xfarray_bytes(rx->xattr_records) +
		xfblob_bytes(rx->xattr_blobs);
	return bytes > XREP_XATTR_MAX_STASH_BYTES;
}

/* Extract as many attribute keys and values as we can. */
STATIC int
xrep_xattr_recover(
	struct xrep_xattr	*rx)
{
	struct xfs_bmbt_irec	got;
	struct xfs_scrub	*sc = rx->sc;
	struct xfs_da_geometry	*geo = sc->mp->m_attr_geo;
	xfs_fileoff_t		offset;
	xfs_extlen_t		len;
	xfs_dablk_t		dabno;
	int			nmap;
	int			error;

	/*
	 * Iterate each xattr leaf block in the attr fork to scan them for any
	 * attributes that we might salvage.
	 */
	for (offset = 0;
	     offset < XFS_MAX_FILEOFF;
	     offset = got.br_startoff + got.br_blockcount) {
		nmap = 1;
		error = xfs_bmapi_read(sc->ip, offset, XFS_MAX_FILEOFF - offset,
				&got, &nmap, XFS_BMAPI_ATTRFORK);
		if (error)
			return error;
		if (nmap != 1)
			return -EFSCORRUPTED;
		if (!xfs_bmap_is_written_extent(&got))
			continue;

		for (dabno = round_up(got.br_startoff, geo->fsbcount);
		     dabno < got.br_startoff + got.br_blockcount;
		     dabno += len) {
			xfs_fileoff_t	curr_offset = dabno - got.br_startoff;
			xfs_extlen_t	maxlen;

			if (xchk_should_terminate(rx->sc, &error))
				return error;

			maxlen = min_t(xfs_filblks_t, INT_MAX,
					got.br_blockcount - curr_offset);
			error = xrep_xattr_recover_block(rx, dabno,
					curr_offset + got.br_startblock,
					maxlen, &len);
			if (error)
				return error;

			if (xrep_xattr_want_flush_stashed(rx)) {
				error = xrep_xattr_flush_stashed(rx);
				if (error)
					return error;
			}
		}
	}

	return 0;
}

/*
 * Reset the extended attribute fork to a state where we can start re-adding
 * the salvaged attributes.
 */
STATIC int
xrep_xattr_fork_remove(
	struct xfs_scrub	*sc,
	struct xfs_inode	*ip)
{
	struct xfs_attr_sf_hdr	*hdr;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_ATTR_FORK);

	/*
	 * If the data fork is in btree format, we can't change di_forkoff
	 * because we could run afoul of the rule that the data fork isn't
	 * supposed to be in btree format if there's enough space in the fork
	 * that it could have used extents format.  Instead, reinitialize the
	 * attr fork to have a shortform structure with zero attributes.
	 */
	if (ip->i_df.if_format == XFS_DINODE_FMT_BTREE) {
		ifp->if_format = XFS_DINODE_FMT_LOCAL;
		hdr = xfs_idata_realloc(ip, (int)sizeof(*hdr) - ifp->if_bytes,
				XFS_ATTR_FORK);
		hdr->count = 0;
		hdr->totsize = cpu_to_be16(sizeof(*hdr));
		xfs_trans_log_inode(sc->tp, ip,
				XFS_ILOG_CORE | XFS_ILOG_ADATA);
		return 0;
	}

	/* If we still have attr fork extents, something's wrong. */
	if (ifp->if_nextents != 0) {
		struct xfs_iext_cursor	icur;
		struct xfs_bmbt_irec	irec;
		unsigned int		i = 0;

		xfs_emerg(sc->mp,
	"inode 0x%llx attr fork still has %llu attr extents, format %d?!",
				ip->i_ino, ifp->if_nextents, ifp->if_format);
		for_each_xfs_iext(ifp, &icur, &irec) {
			xfs_err(sc->mp,
	"[%u]: startoff %llu startblock %llu blockcount %llu state %u",
					i++, irec.br_startoff,
					irec.br_startblock, irec.br_blockcount,
					irec.br_state);
		}
		ASSERT(0);
		return -EFSCORRUPTED;
	}

	xfs_attr_fork_remove(ip, sc->tp);
	return 0;
}

/*
 * Free all the attribute fork blocks of the file being repaired and delete the
 * fork.  The caller must ILOCK the scrub file and join it to the transaction.
 * This function returns with the inode joined to a clean transaction.
 */
int
xrep_xattr_reset_fork(
	struct xfs_scrub	*sc)
{
	int			error;

	trace_xrep_xattr_reset_fork(sc->ip, sc->ip);

	/* Unmap all the attr blocks. */
	if (xfs_ifork_has_extents(&sc->ip->i_af)) {
		error = xrep_reap_ifork(sc, sc->ip, XFS_ATTR_FORK);
		if (error)
			return error;
	}

	error = xrep_xattr_fork_remove(sc, sc->ip);
	if (error)
		return error;

	return xfs_trans_roll_inode(&sc->tp, sc->ip);
}

/*
 * Free all the attribute fork blocks of the temporary file and delete the attr
 * fork.  The caller must ILOCK the tempfile and join it to the transaction.
 * This function returns with the inode joined to a clean scrub transaction.
 */
STATIC int
xrep_xattr_reset_tempfile_fork(
	struct xfs_scrub	*sc)
{
	int			error;

	trace_xrep_xattr_reset_fork(sc->ip, sc->tempip);

	/*
	 * Wipe out the attr fork of the temp file so that regular inode
	 * inactivation won't trip over the corrupt attr fork.
	 */
	if (xfs_ifork_has_extents(&sc->tempip->i_af)) {
		error = xrep_reap_ifork(sc, sc->tempip, XFS_ATTR_FORK);
		if (error)
			return error;
	}

	return xrep_xattr_fork_remove(sc, sc->tempip);
}

/*
 * Find all the extended attributes for this inode by scraping them out of the
 * attribute key blocks by hand, and flushing them into the temp file.
 * When we're done, free the staging memory before exchanging the xattr
 * structures to reduce memory usage.
 */
STATIC int
xrep_xattr_salvage_attributes(
	struct xrep_xattr	*rx)
{
	struct xfs_inode	*ip = rx->sc->ip;
	int			error;

	/* Short format xattrs are easy! */
	if (rx->sc->ip->i_af.if_format == XFS_DINODE_FMT_LOCAL) {
		error = xrep_xattr_recover_sf(rx);
		if (error)
			return error;

		return xrep_xattr_flush_stashed(rx);
	}

	/*
	 * For non-inline xattr structures, the salvage function scans the
	 * buffer cache looking for potential attr leaf blocks.  The scan
	 * requires the ability to lock any buffer found and runs independently
	 * of any transaction <-> buffer item <-> buffer linkage.  Therefore,
	 * roll the transaction to ensure there are no buffers joined.  We hold
	 * the ILOCK independently of the transaction.
	 */
	error = xfs_trans_roll(&rx->sc->tp);
	if (error)
		return error;

	error = xfs_iread_extents(rx->sc->tp, ip, XFS_ATTR_FORK);
	if (error)
		return error;

	error = xrep_xattr_recover(rx);
	if (error)
		return error;

	return xrep_xattr_flush_stashed(rx);
}

/*
 * Prepare both inodes' attribute forks for an exchange.  Promote the tempfile
 * from short format to leaf format, and if the file being repaired has a short
 * format attr fork, turn it into an empty extent list.
 */
STATIC int
xrep_xattr_swap_prep(
	struct xfs_scrub	*sc,
	bool			temp_local,
	bool			ip_local)
{
	int			error;

	/*
	 * If the tempfile's attributes are in shortform format, convert that
	 * to a single leaf extent so that we can use the atomic mapping
	 * exchange.
	 */
	if (temp_local) {
		struct xfs_da_args	args = {
			.dp		= sc->tempip,
			.geo		= sc->mp->m_attr_geo,
			.whichfork	= XFS_ATTR_FORK,
			.trans		= sc->tp,
			.total		= 1,
			.owner		= sc->ip->i_ino,
		};

		error = xfs_attr_shortform_to_leaf(&args);
		if (error)
			return error;

		/*
		 * Roll the deferred log items to get us back to a clean
		 * transaction.
		 */
		error = xfs_defer_finish(&sc->tp);
		if (error)
			return error;
	}

	/*
	 * If the file being repaired had a shortform attribute fork, convert
	 * that to an empty extent list in preparation for the atomic mapping
	 * exchange.
	 */
	if (ip_local) {
		struct xfs_ifork	*ifp;

		ifp = xfs_ifork_ptr(sc->ip, XFS_ATTR_FORK);

		xfs_idestroy_fork(ifp);
		ifp->if_format = XFS_DINODE_FMT_EXTENTS;
		ifp->if_nextents = 0;
		ifp->if_bytes = 0;
		ifp->if_data = NULL;
		ifp->if_height = 0;

		xfs_trans_log_inode(sc->tp, sc->ip,
				XFS_ILOG_CORE | XFS_ILOG_ADATA);
	}

	return 0;
}

/* Exchange the temporary file's attribute fork with the one being repaired. */
STATIC int
xrep_xattr_swap(
	struct xfs_scrub	*sc,
	struct xrep_tempexch	*tx)
{
	bool			ip_local, temp_local;
	int			error = 0;

	ip_local = sc->ip->i_af.if_format == XFS_DINODE_FMT_LOCAL;
	temp_local = sc->tempip->i_af.if_format == XFS_DINODE_FMT_LOCAL;

	/*
	 * If the both files have a local format attr fork and the rebuilt
	 * xattr data would fit in the repaired file's attr fork, just copy
	 * the contents from the tempfile and declare ourselves done.
	 */
	if (ip_local && temp_local) {
		int	forkoff;
		int	newsize;

		newsize = xfs_attr_sf_totsize(sc->tempip);
		forkoff = xfs_attr_shortform_bytesfit(sc->ip, newsize);
		if (forkoff > 0) {
			sc->ip->i_forkoff = forkoff;
			xrep_tempfile_copyout_local(sc, XFS_ATTR_FORK);
			return 0;
		}
	}

	/* Otherwise, make sure both attr forks are in block-mapping mode. */
	error = xrep_xattr_swap_prep(sc, temp_local, ip_local);
	if (error)
		return error;

	return xrep_tempexch_contents(sc, tx);
}

/*
 * Exchange the new extended attribute data (which we created in the tempfile)
 * with the file being repaired.
 */
STATIC int
xrep_xattr_rebuild_tree(
	struct xrep_xattr	*rx)
{
	struct xfs_scrub	*sc = rx->sc;
	int			error;

	/*
	 * If we didn't find any attributes to salvage, repair the file by
	 * zapping its attr fork.
	 */
	if (rx->attrs_found == 0) {
		xfs_trans_ijoin(sc->tp, sc->ip, 0);
		error = xrep_xattr_reset_fork(sc);
		if (error)
			return error;

		goto forget_acls;
	}

	trace_xrep_xattr_rebuild_tree(sc->ip, sc->tempip);

	/*
	 * Commit the repair transaction and drop the ILOCKs so that we can use
	 * the atomic file content exchange helper functions to compute the
	 * correct resource reservations.
	 *
	 * We still hold IOLOCK_EXCL (aka i_rwsem) which will prevent xattr
	 * modifications, but there's nothing to prevent userspace from reading
	 * the attributes until we're ready for the exchange operation.  Reads
	 * will return -EIO without shutting down the fs, so we're ok with
	 * that.
	 */
	error = xrep_trans_commit(sc);
	if (error)
		return error;

	xchk_iunlock(sc, XFS_ILOCK_EXCL);

	/*
	 * Take the IOLOCK on the temporary file so that we can run xattr
	 * operations with the same locks held as we would for a normal file.
	 * We still hold sc->ip's IOLOCK.
	 */
	error = xrep_tempfile_iolock_polled(rx->sc);
	if (error)
		return error;

	/* Allocate exchange transaction and lock both inodes. */
	error = xrep_tempexch_trans_alloc(rx->sc, XFS_ATTR_FORK, &rx->tx);
	if (error)
		return error;

	/*
	 * Exchange the blocks mapped by the tempfile's attr fork with the file
	 * being repaired.  The old attr blocks will then be attached to the
	 * tempfile, so reap its attr fork.
	 */
	error = xrep_xattr_swap(sc, &rx->tx);
	if (error)
		return error;

	error = xrep_xattr_reset_tempfile_fork(sc);
	if (error)
		return error;

	/*
	 * Roll to get a transaction without any inodes joined to it.  Then we
	 * can drop the tempfile's ILOCK and IOLOCK before doing more work on
	 * the scrub target file.
	 */
	error = xfs_trans_roll(&sc->tp);
	if (error)
		return error;

	xrep_tempfile_iunlock(sc);
	xrep_tempfile_iounlock(sc);

forget_acls:
	/* Invalidate cached ACLs now that we've reloaded all the xattrs. */
	xfs_forget_acl(VFS_I(sc->ip), SGI_ACL_FILE);
	xfs_forget_acl(VFS_I(sc->ip), SGI_ACL_DEFAULT);
	return 0;
}

/* Tear down all the incore scan stuff we created. */
STATIC void
xrep_xattr_teardown(
	struct xrep_xattr	*rx)
{
	xfblob_destroy(rx->xattr_blobs);
	xfarray_destroy(rx->xattr_records);
	kfree(rx);
}

/* Set up the filesystem scan so we can regenerate extended attributes. */
STATIC int
xrep_xattr_setup_scan(
	struct xfs_scrub	*sc,
	struct xrep_xattr	**rxp)
{
	struct xrep_xattr	*rx;
	char			*descr;
	int			max_len;
	int			error;

	rx = kzalloc(sizeof(struct xrep_xattr), XCHK_GFP_FLAGS);
	if (!rx)
		return -ENOMEM;
	rx->sc = sc;

	/*
	 * Allocate enough memory to handle loading local attr values from the
	 * xfblob data while flushing stashed attrs to the temporary file.
	 * We only realloc the buffer when salvaging remote attr values.
	 */
	max_len = xfs_attr_leaf_entsize_local_max(sc->mp->m_attr_geo->blksize);
	error = xchk_setup_xattr_buf(rx->sc, max_len);
	if (error == -ENOMEM)
		error = -EDEADLOCK;
	if (error)
		goto out_rx;

	/* Set up some staging for salvaged attribute keys and values */
	descr = xchk_xfile_ino_descr(sc, "xattr keys");
	error = xfarray_create(descr, 0, sizeof(struct xrep_xattr_key),
			&rx->xattr_records);
	kfree(descr);
	if (error)
		goto out_rx;

	descr = xchk_xfile_ino_descr(sc, "xattr names");
	error = xfblob_create(descr, &rx->xattr_blobs);
	kfree(descr);
	if (error)
		goto out_keys;

	*rxp = rx;
	return 0;
out_keys:
	xfarray_destroy(rx->xattr_records);
out_rx:
	kfree(rx);
	return error;
}

/*
 * Repair the extended attribute metadata.
 *
 * XXX: Remote attribute value buffers encompass the entire (up to 64k) buffer.
 * The buffer cache in XFS can't handle aliased multiblock buffers, so this
 * might misbehave if the attr fork is crosslinked with other filesystem
 * metadata.
 */
int
xrep_xattr(
	struct xfs_scrub	*sc)
{
	struct xrep_xattr	*rx = NULL;
	int			error;

	if (!xfs_inode_hasattr(sc->ip))
		return -ENOENT;

	/* The rmapbt is required to reap the old attr fork. */
	if (!xfs_has_rmapbt(sc->mp))
		return -EOPNOTSUPP;

	error = xrep_xattr_setup_scan(sc, &rx);
	if (error)
		return error;

	ASSERT(sc->ilock_flags & XFS_ILOCK_EXCL);

	error = xrep_xattr_salvage_attributes(rx);
	if (error)
		goto out_scan;

	/* Last chance to abort before we start committing fixes. */
	if (xchk_should_terminate(sc, &error))
		goto out_scan;

	error = xrep_xattr_rebuild_tree(rx);
	if (error)
		goto out_scan;

out_scan:
	xrep_xattr_teardown(rx);
	return error;
}
