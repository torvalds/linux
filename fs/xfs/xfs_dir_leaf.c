/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
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
#include "xfs_types.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_da_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_alloc.h"
#include "xfs_btree.h"
#include "xfs_bmap.h"
#include "xfs_dir_leaf.h"
#include "xfs_error.h"

/*
 * xfs_dir_leaf.c
 *
 * Routines to implement leaf blocks of directories as Btrees of hashed names.
 */

/*========================================================================
 * Function prototypes for the kernel.
 *========================================================================*/

/*
 * Routines used for growing the Btree.
 */
STATIC void xfs_dir_leaf_add_work(xfs_dabuf_t *leaf_buffer, xfs_da_args_t *args,
					      int insertion_index,
					      int freemap_index);
STATIC int xfs_dir_leaf_compact(xfs_trans_t *trans, xfs_dabuf_t *leaf_buffer,
					    int musthave, int justcheck);
STATIC void xfs_dir_leaf_rebalance(xfs_da_state_t *state,
						  xfs_da_state_blk_t *blk1,
						  xfs_da_state_blk_t *blk2);
STATIC int xfs_dir_leaf_figure_balance(xfs_da_state_t *state,
					  xfs_da_state_blk_t *leaf_blk_1,
					  xfs_da_state_blk_t *leaf_blk_2,
					  int *number_entries_in_blk1,
					  int *number_namebytes_in_blk1);

STATIC int xfs_dir_leaf_create(struct xfs_da_args *args,
				xfs_dablk_t which_block,
				struct xfs_dabuf **bpp);

/*
 * Utility routines.
 */
STATIC void xfs_dir_leaf_moveents(xfs_dir_leafblock_t *src_leaf,
					      int src_start,
					      xfs_dir_leafblock_t *dst_leaf,
					      int dst_start, int move_count,
					      xfs_mount_t *mp);


/*========================================================================
 * External routines when dirsize < XFS_IFORK_DSIZE(dp).
 *========================================================================*/


/*
 * Validate a given inode number.
 */
int
xfs_dir_ino_validate(xfs_mount_t *mp, xfs_ino_t ino)
{
	xfs_agblock_t	agblkno;
	xfs_agino_t	agino;
	xfs_agnumber_t	agno;
	int		ino_ok;
	int		ioff;

	agno = XFS_INO_TO_AGNO(mp, ino);
	agblkno = XFS_INO_TO_AGBNO(mp, ino);
	ioff = XFS_INO_TO_OFFSET(mp, ino);
	agino = XFS_OFFBNO_TO_AGINO(mp, agblkno, ioff);
	ino_ok =
		agno < mp->m_sb.sb_agcount &&
		agblkno < mp->m_sb.sb_agblocks &&
		agblkno != 0 &&
		ioff < (1 << mp->m_sb.sb_inopblog) &&
		XFS_AGINO_TO_INO(mp, agno, agino) == ino;
	if (unlikely(XFS_TEST_ERROR(!ino_ok, mp, XFS_ERRTAG_DIR_INO_VALIDATE,
			XFS_RANDOM_DIR_INO_VALIDATE))) {
		xfs_fs_cmn_err(CE_WARN, mp, "Invalid inode number 0x%Lx",
				(unsigned long long) ino);
		XFS_ERROR_REPORT("xfs_dir_ino_validate", XFS_ERRLEVEL_LOW, mp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	return 0;
}

/*
 * Create the initial contents of a shortform directory.
 */
int
xfs_dir_shortform_create(xfs_da_args_t *args, xfs_ino_t parent)
{
	xfs_dir_sf_hdr_t *hdr;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp != NULL);
	ASSERT(dp->i_d.di_size == 0);
	if (dp->i_d.di_format == XFS_DINODE_FMT_EXTENTS) {
		dp->i_df.if_flags &= ~XFS_IFEXTENTS;	/* just in case */
		dp->i_d.di_format = XFS_DINODE_FMT_LOCAL;
		xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE);
		dp->i_df.if_flags |= XFS_IFINLINE;
	}
	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	ASSERT(dp->i_df.if_bytes == 0);
	xfs_idata_realloc(dp, sizeof(*hdr), XFS_DATA_FORK);
	hdr = (xfs_dir_sf_hdr_t *)dp->i_df.if_u1.if_data;
	XFS_DIR_SF_PUT_DIRINO(&parent, &hdr->parent);

	hdr->count = 0;
	dp->i_d.di_size = sizeof(*hdr);
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);
	return 0;
}

/*
 * Add a name to the shortform directory structure.
 * Overflow from the inode has already been checked for.
 */
int
xfs_dir_shortform_addname(xfs_da_args_t *args)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int i, offset, size;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Catch the case where the conversion from shortform to leaf
	 * failed part way through.
	 */
	if (dp->i_d.di_size < sizeof(xfs_dir_sf_hdr_t)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sf = (xfs_dir_shortform_t *)dp->i_df.if_u1.if_data;
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if (sfe->namelen == args->namelen &&
		    args->name[0] == sfe->name[0] &&
		    memcmp(args->name, sfe->name, args->namelen) == 0)
			return XFS_ERROR(EEXIST);
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}

	offset = (int)((char *)sfe - (char *)sf);
	size = XFS_DIR_SF_ENTSIZE_BYNAME(args->namelen);
	xfs_idata_realloc(dp, size, XFS_DATA_FORK);
	sf = (xfs_dir_shortform_t *)dp->i_df.if_u1.if_data;
	sfe = (xfs_dir_sf_entry_t *)((char *)sf + offset);

	XFS_DIR_SF_PUT_DIRINO(&args->inumber, &sfe->inumber);
	sfe->namelen = args->namelen;
	memcpy(sfe->name, args->name, sfe->namelen);
	sf->hdr.count++;

	dp->i_d.di_size += size;
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);

	return 0;
}

/*
 * Remove a name from the shortform directory structure.
 */
int
xfs_dir_shortform_removename(xfs_da_args_t *args)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int base, size = 0, i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Catch the case where the conversion from shortform to leaf
	 * failed part way through.
	 */
	if (dp->i_d.di_size < sizeof(xfs_dir_sf_hdr_t)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	base = sizeof(xfs_dir_sf_hdr_t);
	sf = (xfs_dir_shortform_t *)dp->i_df.if_u1.if_data;
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		size = XFS_DIR_SF_ENTSIZE_BYENTRY(sfe);
		if (sfe->namelen == args->namelen &&
		    sfe->name[0] == args->name[0] &&
		    memcmp(sfe->name, args->name, args->namelen) == 0)
			break;
		base += size;
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	if (i < 0) {
		ASSERT(args->oknoent);
		return XFS_ERROR(ENOENT);
	}

	if ((base + size) != dp->i_d.di_size) {
		memmove(&((char *)sf)[base], &((char *)sf)[base+size],
					      dp->i_d.di_size - (base+size));
	}
	sf->hdr.count--;

	xfs_idata_realloc(dp, -size, XFS_DATA_FORK);
	dp->i_d.di_size -= size;
	xfs_trans_log_inode(args->trans, dp, XFS_ILOG_CORE | XFS_ILOG_DDATA);

	return 0;
}

/*
 * Look up a name in a shortform directory structure.
 */
int
xfs_dir_shortform_lookup(xfs_da_args_t *args)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int i;
	xfs_inode_t *dp;

	dp = args->dp;
	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Catch the case where the conversion from shortform to leaf
	 * failed part way through.
	 */
	if (dp->i_d.di_size < sizeof(xfs_dir_sf_hdr_t)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sf = (xfs_dir_shortform_t *)dp->i_df.if_u1.if_data;
	if (args->namelen == 2 &&
	    args->name[0] == '.' && args->name[1] == '.') {
		XFS_DIR_SF_GET_DIRINO(&sf->hdr.parent, &args->inumber);
		return(XFS_ERROR(EEXIST));
	}
	if (args->namelen == 1 && args->name[0] == '.') {
		args->inumber = dp->i_ino;
		return(XFS_ERROR(EEXIST));
	}
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if (sfe->namelen == args->namelen &&
		    sfe->name[0] == args->name[0] &&
		    memcmp(args->name, sfe->name, args->namelen) == 0) {
			XFS_DIR_SF_GET_DIRINO(&sfe->inumber, &args->inumber);
			return(XFS_ERROR(EEXIST));
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	ASSERT(args->oknoent);
	return(XFS_ERROR(ENOENT));
}

/*
 * Convert from using the shortform to the leaf.
 */
int
xfs_dir_shortform_to_leaf(xfs_da_args_t *iargs)
{
	xfs_inode_t *dp;
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	xfs_da_args_t args;
	xfs_ino_t inumber;
	char *tmpbuffer;
	int retval, i, size;
	xfs_dablk_t blkno;
	xfs_dabuf_t *bp;

	dp = iargs->dp;
	/*
	 * Catch the case where the conversion from shortform to leaf
	 * failed part way through.
	 */
	if (dp->i_d.di_size < sizeof(xfs_dir_sf_hdr_t)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	size = dp->i_df.if_bytes;
	tmpbuffer = kmem_alloc(size, KM_SLEEP);
	ASSERT(tmpbuffer != NULL);

	memcpy(tmpbuffer, dp->i_df.if_u1.if_data, size);

	sf = (xfs_dir_shortform_t *)tmpbuffer;
	XFS_DIR_SF_GET_DIRINO(&sf->hdr.parent, &inumber);

	xfs_idata_realloc(dp, -size, XFS_DATA_FORK);
	dp->i_d.di_size = 0;
	xfs_trans_log_inode(iargs->trans, dp, XFS_ILOG_CORE);
	retval = xfs_da_grow_inode(iargs, &blkno);
	if (retval)
		goto out;

	ASSERT(blkno == 0);
	retval = xfs_dir_leaf_create(iargs, blkno, &bp);
	if (retval)
		goto out;
	xfs_da_buf_done(bp);

	args.name = ".";
	args.namelen = 1;
	args.hashval = xfs_dir_hash_dot;
	args.inumber = dp->i_ino;
	args.dp = dp;
	args.firstblock = iargs->firstblock;
	args.flist = iargs->flist;
	args.total = iargs->total;
	args.whichfork = XFS_DATA_FORK;
	args.trans = iargs->trans;
	args.justcheck = 0;
	args.addname = args.oknoent = 1;
	retval = xfs_dir_leaf_addname(&args);
	if (retval)
		goto out;

	args.name = "..";
	args.namelen = 2;
	args.hashval = xfs_dir_hash_dotdot;
	args.inumber = inumber;
	retval = xfs_dir_leaf_addname(&args);
	if (retval)
		goto out;

	sfe = &sf->list[0];
	for (i = 0; i < sf->hdr.count; i++) {
		args.name = (char *)(sfe->name);
		args.namelen = sfe->namelen;
		args.hashval = xfs_da_hashname((char *)(sfe->name),
					       sfe->namelen);
		XFS_DIR_SF_GET_DIRINO(&sfe->inumber, &args.inumber);
		retval = xfs_dir_leaf_addname(&args);
		if (retval)
			goto out;
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	retval = 0;

out:
	kmem_free(tmpbuffer, size);
	return retval;
}

STATIC int
xfs_dir_shortform_compare(const void *a, const void *b)
{
	xfs_dir_sf_sort_t *sa, *sb;

	sa = (xfs_dir_sf_sort_t *)a;
	sb = (xfs_dir_sf_sort_t *)b;
	if (sa->hash < sb->hash)
		return -1;
	else if (sa->hash > sb->hash)
		return 1;
	else
		return sa->entno - sb->entno;
}

/*
 * Copy out directory entries for getdents(), for shortform directories.
 */
/*ARGSUSED*/
int
xfs_dir_shortform_getdents(xfs_inode_t *dp, uio_t *uio, int *eofp,
				       xfs_dirent_t *dbp, xfs_dir_put_t put)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	int retval, i, sbsize, nsbuf, lastresid=0, want_entno;
	xfs_mount_t *mp;
	xfs_dahash_t cookhash, hash;
	xfs_dir_put_args_t p;
	xfs_dir_sf_sort_t *sbuf, *sbp;

	mp = dp->i_mount;
	sf = (xfs_dir_shortform_t *)dp->i_df.if_u1.if_data;
	cookhash = XFS_DA_COOKIE_HASH(mp, uio->uio_offset);
	want_entno = XFS_DA_COOKIE_ENTRY(mp, uio->uio_offset);
	nsbuf = sf->hdr.count + 2;
	sbsize = (nsbuf + 1) * sizeof(*sbuf);
	sbp = sbuf = kmem_alloc(sbsize, KM_SLEEP);

	xfs_dir_trace_g_du("sf: start", dp, uio);

	/*
	 * Collect all the entries into the buffer.
	 * Entry 0 is .
	 */
	sbp->entno = 0;
	sbp->seqno = 0;
	sbp->hash = xfs_dir_hash_dot;
	sbp->ino = dp->i_ino;
	sbp->name = ".";
	sbp->namelen = 1;
	sbp++;

	/*
	 * Entry 1 is ..
	 */
	sbp->entno = 1;
	sbp->seqno = 0;
	sbp->hash = xfs_dir_hash_dotdot;
	sbp->ino = XFS_GET_DIR_INO8(sf->hdr.parent);
	sbp->name = "..";
	sbp->namelen = 2;
	sbp++;

	/*
	 * Scan the directory data for the rest of the entries.
	 */
	for (i = 0, sfe = &sf->list[0]; i < sf->hdr.count; i++) {

		if (unlikely(
		    ((char *)sfe < (char *)sf) ||
		    ((char *)sfe >= ((char *)sf + dp->i_df.if_bytes)))) {
			xfs_dir_trace_g_du("sf: corrupted", dp, uio);
			XFS_CORRUPTION_ERROR("xfs_dir_shortform_getdents",
					     XFS_ERRLEVEL_LOW, mp, sfe);
			kmem_free(sbuf, sbsize);
			return XFS_ERROR(EFSCORRUPTED);
		}

		sbp->entno = i + 2;
		sbp->seqno = 0;
		sbp->hash = xfs_da_hashname((char *)sfe->name, sfe->namelen);
		sbp->ino = XFS_GET_DIR_INO8(sfe->inumber);
		sbp->name = (char *)sfe->name;
		sbp->namelen = sfe->namelen;
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
		sbp++;
	}

	/*
	 * Sort the entries on hash then entno.
	 */
	xfs_sort(sbuf, nsbuf, sizeof(*sbuf), xfs_dir_shortform_compare);
	/*
	 * Stuff in last entry.
	 */
	sbp->entno = nsbuf;
	sbp->hash = XFS_DA_MAXHASH;
	sbp->seqno = 0;
	/*
	 * Figure out the sequence numbers in case there's a hash duplicate.
	 */
	for (hash = sbuf->hash, sbp = sbuf + 1;
				sbp < &sbuf[nsbuf + 1]; sbp++) {
		if (sbp->hash == hash)
			sbp->seqno = sbp[-1].seqno + 1;
		else
			hash = sbp->hash;
	}

	/*
	 * Set up put routine.
	 */
	p.dbp = dbp;
	p.put = put;
	p.uio = uio;

	/*
	 * Find our place.
	 */
	for (sbp = sbuf; sbp < &sbuf[nsbuf + 1]; sbp++) {
		if (sbp->hash > cookhash ||
		    (sbp->hash == cookhash && sbp->seqno >= want_entno))
			break;
	}

	/*
	 * Did we fail to find anything?  We stop at the last entry,
	 * the one we put maxhash into.
	 */
	if (sbp == &sbuf[nsbuf]) {
		kmem_free(sbuf, sbsize);
		xfs_dir_trace_g_du("sf: hash beyond end", dp, uio);
		uio->uio_offset = XFS_DA_MAKE_COOKIE(mp, 0, 0, XFS_DA_MAXHASH);
		*eofp = 1;
		return 0;
	}

	/*
	 * Loop putting entries into the user buffer.
	 */
	while (sbp < &sbuf[nsbuf]) {
		/*
		 * Save the first resid in a run of equal-hashval entries
		 * so that we can back them out if they don't all fit.
		 */
		if (sbp->seqno == 0 || sbp == sbuf)
			lastresid = uio->uio_resid;
		XFS_PUT_COOKIE(p.cook, mp, 0, sbp[1].seqno, sbp[1].hash);
		p.ino = sbp->ino;
#if XFS_BIG_INUMS
		p.ino += mp->m_inoadd;
#endif
		p.name = sbp->name;
		p.namelen = sbp->namelen;
		retval = p.put(&p);
		if (!p.done) {
			uio->uio_offset =
				XFS_DA_MAKE_COOKIE(mp, 0, 0, sbp->hash);
			kmem_free(sbuf, sbsize);
			uio->uio_resid = lastresid;
			xfs_dir_trace_g_du("sf: E-O-B", dp, uio);
			return retval;
		}
		sbp++;
	}
	kmem_free(sbuf, sbsize);
	uio->uio_offset = p.cook.o;
	*eofp = 1;
	xfs_dir_trace_g_du("sf: E-O-F", dp, uio);
	return 0;
}

/*
 * Look up a name in a shortform directory structure, replace the inode number.
 */
int
xfs_dir_shortform_replace(xfs_da_args_t *args)
{
	xfs_dir_shortform_t *sf;
	xfs_dir_sf_entry_t *sfe;
	xfs_inode_t *dp;
	int i;

	dp = args->dp;
	ASSERT(dp->i_df.if_flags & XFS_IFINLINE);
	/*
	 * Catch the case where the conversion from shortform to leaf
	 * failed part way through.
	 */
	if (dp->i_d.di_size < sizeof(xfs_dir_sf_hdr_t)) {
		ASSERT(XFS_FORCED_SHUTDOWN(dp->i_mount));
		return XFS_ERROR(EIO);
	}
	ASSERT(dp->i_df.if_bytes == dp->i_d.di_size);
	ASSERT(dp->i_df.if_u1.if_data != NULL);
	sf = (xfs_dir_shortform_t *)dp->i_df.if_u1.if_data;
	if (args->namelen == 2 &&
	    args->name[0] == '.' && args->name[1] == '.') {
		/* XXX - replace assert? */
		XFS_DIR_SF_PUT_DIRINO(&args->inumber, &sf->hdr.parent);
		xfs_trans_log_inode(args->trans, dp, XFS_ILOG_DDATA);
		return 0;
	}
	ASSERT(args->namelen != 1 || args->name[0] != '.');
	sfe = &sf->list[0];
	for (i = sf->hdr.count-1; i >= 0; i--) {
		if (sfe->namelen == args->namelen &&
		    sfe->name[0] == args->name[0] &&
		    memcmp(args->name, sfe->name, args->namelen) == 0) {
			ASSERT(memcmp((char *)&args->inumber,
				(char *)&sfe->inumber, sizeof(xfs_ino_t)));
			XFS_DIR_SF_PUT_DIRINO(&args->inumber, &sfe->inumber);
			xfs_trans_log_inode(args->trans, dp, XFS_ILOG_DDATA);
			return 0;
		}
		sfe = XFS_DIR_SF_NEXTENTRY(sfe);
	}
	ASSERT(args->oknoent);
	return XFS_ERROR(ENOENT);
}

/*
 * Convert a leaf directory to shortform structure
 */
int
xfs_dir_leaf_to_shortform(xfs_da_args_t *iargs)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	xfs_da_args_t args;
	xfs_inode_t *dp;
	xfs_ino_t parent = 0;
	char *tmpbuffer;
	int retval, i;
	xfs_dabuf_t *bp;

	dp = iargs->dp;
	tmpbuffer = kmem_alloc(XFS_LBSIZE(dp->i_mount), KM_SLEEP);
	ASSERT(tmpbuffer != NULL);

	retval = xfs_da_read_buf(iargs->trans, iargs->dp, 0, -1, &bp,
					       XFS_DATA_FORK);
	if (retval)
		goto out;
	ASSERT(bp != NULL);
	memcpy(tmpbuffer, bp->data, XFS_LBSIZE(dp->i_mount));
	leaf = (xfs_dir_leafblock_t *)tmpbuffer;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	memset(bp->data, 0, XFS_LBSIZE(dp->i_mount));

	/*
	 * Find and special case the parent inode number
	 */
	hdr = &leaf->hdr;
	entry = &leaf->entries[0];
	for (i = be16_to_cpu(hdr->count)-1; i >= 0; entry++, i--) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, be16_to_cpu(entry->nameidx));
		if ((entry->namelen == 2) &&
		    (namest->name[0] == '.') &&
		    (namest->name[1] == '.')) {
			XFS_DIR_SF_GET_DIRINO(&namest->inumber, &parent);
			entry->nameidx = 0;
		} else if ((entry->namelen == 1) && (namest->name[0] == '.')) {
			entry->nameidx = 0;
		}
	}
	retval = xfs_da_shrink_inode(iargs, 0, bp);
	if (retval)
		goto out;
	retval = xfs_dir_shortform_create(iargs, parent);
	if (retval)
		goto out;

	/*
	 * Copy the rest of the filenames
	 */
	entry = &leaf->entries[0];
	args.dp = dp;
	args.firstblock = iargs->firstblock;
	args.flist = iargs->flist;
	args.total = iargs->total;
	args.whichfork = XFS_DATA_FORK;
	args.trans = iargs->trans;
	args.justcheck = 0;
	args.addname = args.oknoent = 1;
	for (i = 0; i < be16_to_cpu(hdr->count); entry++, i++) {
		if (!entry->nameidx)
			continue;
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, be16_to_cpu(entry->nameidx));
		args.name = (char *)(namest->name);
		args.namelen = entry->namelen;
		args.hashval = be32_to_cpu(entry->hashval);
		XFS_DIR_SF_GET_DIRINO(&namest->inumber, &args.inumber);
		xfs_dir_shortform_addname(&args);
	}

out:
	kmem_free(tmpbuffer, XFS_LBSIZE(dp->i_mount));
	return retval;
}

/*
 * Convert from using a single leaf to a root node and a leaf.
 */
int
xfs_dir_leaf_to_node(xfs_da_args_t *args)
{
	xfs_dir_leafblock_t *leaf;
	xfs_da_intnode_t *node;
	xfs_inode_t *dp;
	xfs_dabuf_t *bp1, *bp2;
	xfs_dablk_t blkno;
	int retval;

	dp = args->dp;
	retval = xfs_da_grow_inode(args, &blkno);
	ASSERT(blkno == 1);
	if (retval)
		return retval;
	retval = xfs_da_read_buf(args->trans, args->dp, 0, -1, &bp1,
					      XFS_DATA_FORK);
	if (retval)
		return retval;
	ASSERT(bp1 != NULL);
	retval = xfs_da_get_buf(args->trans, args->dp, 1, -1, &bp2,
					     XFS_DATA_FORK);
	if (retval) {
		xfs_da_buf_done(bp1);
		return retval;
	}
	ASSERT(bp2 != NULL);
	memcpy(bp2->data, bp1->data, XFS_LBSIZE(dp->i_mount));
	xfs_da_buf_done(bp1);
	xfs_da_log_buf(args->trans, bp2, 0, XFS_LBSIZE(dp->i_mount) - 1);

	/*
	 * Set up the new root node.
	 */
	retval = xfs_da_node_create(args, 0, 1, &bp1, XFS_DATA_FORK);
	if (retval) {
		xfs_da_buf_done(bp2);
		return retval;
	}
	node = bp1->data;
	leaf = bp2->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	node->btree[0].hashval = leaf->entries[be16_to_cpu(leaf->hdr.count)-1].hashval;
	xfs_da_buf_done(bp2);
	node->btree[0].before = cpu_to_be32(blkno);
	node->hdr.count = cpu_to_be16(1);
	xfs_da_log_buf(args->trans, bp1,
		XFS_DA_LOGRANGE(node, &node->btree[0], sizeof(node->btree[0])));
	xfs_da_buf_done(bp1);

	return retval;
}


/*========================================================================
 * Routines used for growing the Btree.
 *========================================================================*/

/*
 * Create the initial contents of a leaf directory
 * or a leaf in a node directory.
 */
STATIC int
xfs_dir_leaf_create(xfs_da_args_t *args, xfs_dablk_t blkno, xfs_dabuf_t **bpp)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_inode_t *dp;
	xfs_dabuf_t *bp;
	int retval;

	dp = args->dp;
	ASSERT(dp != NULL);
	retval = xfs_da_get_buf(args->trans, dp, blkno, -1, &bp, XFS_DATA_FORK);
	if (retval)
		return retval;
	ASSERT(bp != NULL);
	leaf = bp->data;
	memset((char *)leaf, 0, XFS_LBSIZE(dp->i_mount));
	hdr = &leaf->hdr;
	hdr->info.magic = cpu_to_be16(XFS_DIR_LEAF_MAGIC);
	hdr->firstused = cpu_to_be16(XFS_LBSIZE(dp->i_mount));
	if (!hdr->firstused)
		hdr->firstused = cpu_to_be16(XFS_LBSIZE(dp->i_mount) - 1);
	hdr->freemap[0].base = cpu_to_be16(sizeof(xfs_dir_leaf_hdr_t));
	hdr->freemap[0].size = cpu_to_be16(be16_to_cpu(hdr->firstused) -
					   be16_to_cpu(hdr->freemap[0].base));

	xfs_da_log_buf(args->trans, bp, 0, XFS_LBSIZE(dp->i_mount) - 1);

	*bpp = bp;
	return 0;
}

/*
 * Split the leaf node, rebalance, then add the new entry.
 */
int
xfs_dir_leaf_split(xfs_da_state_t *state, xfs_da_state_blk_t *oldblk,
				  xfs_da_state_blk_t *newblk)
{
	xfs_dablk_t blkno;
	xfs_da_args_t *args;
	int error;

	/*
	 * Allocate space for a new leaf node.
	 */
	args = state->args;
	ASSERT(args != NULL);
	ASSERT(oldblk->magic == XFS_DIR_LEAF_MAGIC);
	error = xfs_da_grow_inode(args, &blkno);
	if (error)
		return error;
	error = xfs_dir_leaf_create(args, blkno, &newblk->bp);
	if (error)
		return error;
	newblk->blkno = blkno;
	newblk->magic = XFS_DIR_LEAF_MAGIC;

	/*
	 * Rebalance the entries across the two leaves.
	 */
	xfs_dir_leaf_rebalance(state, oldblk, newblk);
	error = xfs_da_blk_link(state, oldblk, newblk);
	if (error)
		return error;

	/*
	 * Insert the new entry in the correct block.
	 */
	if (state->inleaf) {
		error = xfs_dir_leaf_add(oldblk->bp, args, oldblk->index);
	} else {
		error = xfs_dir_leaf_add(newblk->bp, args, newblk->index);
	}

	/*
	 * Update last hashval in each block since we added the name.
	 */
	oldblk->hashval = xfs_dir_leaf_lasthash(oldblk->bp, NULL);
	newblk->hashval = xfs_dir_leaf_lasthash(newblk->bp, NULL);
	return error;
}

/*
 * Add a name to the leaf directory structure.
 *
 * Must take into account fragmented leaves and leaves where spacemap has
 * lost some freespace information (ie: holes).
 */
int
xfs_dir_leaf_add(xfs_dabuf_t *bp, xfs_da_args_t *args, int index)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_dir_leaf_map_t *map;
	int tablesize, entsize, sum, i, tmp, error;

	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	ASSERT((index >= 0) && (index <= be16_to_cpu(leaf->hdr.count)));
	hdr = &leaf->hdr;
	entsize = XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen);

	/*
	 * Search through freemap for first-fit on new name length.
	 * (may need to figure in size of entry struct too)
	 */
	tablesize = (be16_to_cpu(hdr->count) + 1) *
		sizeof(xfs_dir_leaf_entry_t) + sizeof(xfs_dir_leaf_hdr_t);
	map = &hdr->freemap[XFS_DIR_LEAF_MAPSIZE-1];
	for (sum = 0, i = XFS_DIR_LEAF_MAPSIZE-1; i >= 0; map--, i--) {
		if (tablesize > be16_to_cpu(hdr->firstused)) {
			sum += be16_to_cpu(map->size);
			continue;
		}
		if (!map->size)
			continue;	/* no space in this map */
		tmp = entsize;
		if (be16_to_cpu(map->base) < be16_to_cpu(hdr->firstused))
			tmp += (uint)sizeof(xfs_dir_leaf_entry_t);
		if (be16_to_cpu(map->size) >= tmp) {
			if (!args->justcheck)
				xfs_dir_leaf_add_work(bp, args, index, i);
			return 0;
		}
		sum += be16_to_cpu(map->size);
	}

	/*
	 * If there are no holes in the address space of the block,
	 * and we don't have enough freespace, then compaction will do us
	 * no good and we should just give up.
	 */
	if (!hdr->holes && (sum < entsize))
		return XFS_ERROR(ENOSPC);

	/*
	 * Compact the entries to coalesce free space.
	 * Pass the justcheck flag so the checking pass can return
	 * an error, without changing anything, if it won't fit.
	 */
	error = xfs_dir_leaf_compact(args->trans, bp,
			args->total == 0 ?
				entsize +
				(uint)sizeof(xfs_dir_leaf_entry_t) : 0,
			args->justcheck);
	if (error)
		return error;
	/*
	 * After compaction, the block is guaranteed to have only one
	 * free region, in freemap[0].  If it is not big enough, give up.
	 */
	if (be16_to_cpu(hdr->freemap[0].size) <
	    (entsize + (uint)sizeof(xfs_dir_leaf_entry_t)))
		return XFS_ERROR(ENOSPC);

	if (!args->justcheck)
		xfs_dir_leaf_add_work(bp, args, index, 0);
	return 0;
}

/*
 * Add a name to a leaf directory structure.
 */
STATIC void
xfs_dir_leaf_add_work(xfs_dabuf_t *bp, xfs_da_args_t *args, int index,
		      int mapindex)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	xfs_dir_leaf_map_t *map;
	/* REFERENCED */
	xfs_mount_t *mp;
	int tmp, i;

	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	ASSERT((mapindex >= 0) && (mapindex < XFS_DIR_LEAF_MAPSIZE));
	ASSERT((index >= 0) && (index <= be16_to_cpu(hdr->count)));

	/*
	 * Force open some space in the entry array and fill it in.
	 */
	entry = &leaf->entries[index];
	if (index < be16_to_cpu(hdr->count)) {
		tmp  = be16_to_cpu(hdr->count) - index;
		tmp *= (uint)sizeof(xfs_dir_leaf_entry_t);
		memmove(entry + 1, entry, tmp);
		xfs_da_log_buf(args->trans, bp,
		    XFS_DA_LOGRANGE(leaf, entry, tmp + (uint)sizeof(*entry)));
	}
	be16_add(&hdr->count, 1);

	/*
	 * Allocate space for the new string (at the end of the run).
	 */
	map = &hdr->freemap[mapindex];
	mp = args->trans->t_mountp;
	ASSERT(be16_to_cpu(map->base) < XFS_LBSIZE(mp));
	ASSERT(be16_to_cpu(map->size) >= XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen));
	ASSERT(be16_to_cpu(map->size) < XFS_LBSIZE(mp));

	be16_add(&map->size, -(XFS_DIR_LEAF_ENTSIZE_BYNAME(args->namelen)));
	entry->nameidx = cpu_to_be16(be16_to_cpu(map->base) +
				     be16_to_cpu(map->size));
	entry->hashval = cpu_to_be32(args->hashval);
	entry->namelen = args->namelen;
	xfs_da_log_buf(args->trans, bp,
	    XFS_DA_LOGRANGE(leaf, entry, sizeof(*entry)));

	/*
	 * Copy the string and inode number into the new space.
	 */
	namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, be16_to_cpu(entry->nameidx));
	XFS_DIR_SF_PUT_DIRINO(&args->inumber, &namest->inumber);
	memcpy(namest->name, args->name, args->namelen);
	xfs_da_log_buf(args->trans, bp,
	    XFS_DA_LOGRANGE(leaf, namest, XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry)));

	/*
	 * Update the control info for this leaf node
	 */
	if (be16_to_cpu(entry->nameidx) < be16_to_cpu(hdr->firstused))
		hdr->firstused = entry->nameidx;
	ASSERT(be16_to_cpu(hdr->firstused) >=
	       ((be16_to_cpu(hdr->count)*sizeof(*entry))+sizeof(*hdr)));
	tmp = (be16_to_cpu(hdr->count)-1) * (uint)sizeof(xfs_dir_leaf_entry_t)
			+ (uint)sizeof(xfs_dir_leaf_hdr_t);
	map = &hdr->freemap[0];
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		if (be16_to_cpu(map->base) == tmp) {
			int entry_size = sizeof(xfs_dir_leaf_entry_t);
			be16_add(&map->base, entry_size);
			be16_add(&map->size, -entry_size);
		}
	}
	be16_add(&hdr->namebytes, args->namelen);
	xfs_da_log_buf(args->trans, bp,
		XFS_DA_LOGRANGE(leaf, hdr, sizeof(*hdr)));
}

/*
 * Garbage collect a leaf directory block by copying it to a new buffer.
 */
STATIC int
xfs_dir_leaf_compact(xfs_trans_t *trans, xfs_dabuf_t *bp, int musthave,
		     int justcheck)
{
	xfs_dir_leafblock_t *leaf_s, *leaf_d;
	xfs_dir_leaf_hdr_t *hdr_s, *hdr_d;
	xfs_mount_t *mp;
	char *tmpbuffer;
	char *tmpbuffer2=NULL;
	int rval;
	int lbsize;

	mp = trans->t_mountp;
	lbsize = XFS_LBSIZE(mp);
	tmpbuffer = kmem_alloc(lbsize, KM_SLEEP);
	ASSERT(tmpbuffer != NULL);
	memcpy(tmpbuffer, bp->data, lbsize);

	/*
	 * Make a second copy in case xfs_dir_leaf_moveents()
	 * below destroys the original.
	 */
	if (musthave || justcheck) {
		tmpbuffer2 = kmem_alloc(lbsize, KM_SLEEP);
		memcpy(tmpbuffer2, bp->data, lbsize);
	}
	memset(bp->data, 0, lbsize);

	/*
	 * Copy basic information
	 */
	leaf_s = (xfs_dir_leafblock_t *)tmpbuffer;
	leaf_d = bp->data;
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	hdr_d->info = hdr_s->info;	/* struct copy */
	hdr_d->firstused = cpu_to_be16(lbsize);
	if (!hdr_d->firstused)
		hdr_d->firstused = cpu_to_be16(lbsize - 1);
	hdr_d->namebytes = 0;
	hdr_d->count = 0;
	hdr_d->holes = 0;
	hdr_d->freemap[0].base = cpu_to_be16(sizeof(xfs_dir_leaf_hdr_t));
	hdr_d->freemap[0].size = cpu_to_be16(be16_to_cpu(hdr_d->firstused) -
			                     be16_to_cpu(hdr_d->freemap[0].base));

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate filenames packed and in sequence.
	 * This changes the source (leaf_s) as well.
	 */
	xfs_dir_leaf_moveents(leaf_s, 0, leaf_d, 0, be16_to_cpu(hdr_s->count), mp);

	if (musthave && be16_to_cpu(hdr_d->freemap[0].size) < musthave)
		rval = XFS_ERROR(ENOSPC);
	else
		rval = 0;

	if (justcheck || rval == ENOSPC) {
		ASSERT(tmpbuffer2);
		memcpy(bp->data, tmpbuffer2, lbsize);
	} else {
		xfs_da_log_buf(trans, bp, 0, lbsize - 1);
	}

	kmem_free(tmpbuffer, lbsize);
	if (musthave || justcheck)
		kmem_free(tmpbuffer2, lbsize);
	return rval;
}

/*
 * Redistribute the directory entries between two leaf nodes,
 * taking into account the size of the new entry.
 *
 * NOTE: if new block is empty, then it will get the upper half of old block.
 */
STATIC void
xfs_dir_leaf_rebalance(xfs_da_state_t *state, xfs_da_state_blk_t *blk1,
				      xfs_da_state_blk_t *blk2)
{
	xfs_da_state_blk_t *tmp_blk;
	xfs_dir_leafblock_t *leaf1, *leaf2;
	xfs_dir_leaf_hdr_t *hdr1, *hdr2;
	int count, totallen, max, space, swap;

	/*
	 * Set up environment.
	 */
	ASSERT(blk1->magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(blk2->magic == XFS_DIR_LEAF_MAGIC);
	leaf1 = blk1->bp->data;
	leaf2 = blk2->bp->data;
	ASSERT(be16_to_cpu(leaf1->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	ASSERT(be16_to_cpu(leaf2->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);

	/*
	 * Check ordering of blocks, reverse if it makes things simpler.
	 */
	swap = 0;
	if (xfs_dir_leaf_order(blk1->bp, blk2->bp)) {
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
	 */
	state->inleaf = xfs_dir_leaf_figure_balance(state, blk1, blk2,
							   &count, &totallen);
	if (swap)
		state->inleaf = !state->inleaf;

	/*
	 * Move any entries required from leaf to leaf:
	 */
	if (count < be16_to_cpu(hdr1->count)) {
		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		count = be16_to_cpu(hdr1->count) - count;	/* number entries being moved */
		space = be16_to_cpu(hdr1->namebytes) - totallen;
		space += count * ((uint)sizeof(xfs_dir_leaf_name_t)-1);
		space += count * (uint)sizeof(xfs_dir_leaf_entry_t);

		/*
		 * leaf2 is the destination, compact it if it looks tight.
		 */
		max  = be16_to_cpu(hdr2->firstused) - (uint)sizeof(xfs_dir_leaf_hdr_t);
		max -= be16_to_cpu(hdr2->count) * (uint)sizeof(xfs_dir_leaf_entry_t);
		if (space > max) {
			xfs_dir_leaf_compact(state->args->trans, blk2->bp,
								 0, 0);
		}

		/*
		 * Move high entries from leaf1 to low end of leaf2.
		 */
		xfs_dir_leaf_moveents(leaf1, be16_to_cpu(hdr1->count) - count,
					     leaf2, 0, count, state->mp);

		xfs_da_log_buf(state->args->trans, blk1->bp, 0,
						   state->blocksize-1);
		xfs_da_log_buf(state->args->trans, blk2->bp, 0,
						   state->blocksize-1);

	} else if (count > be16_to_cpu(hdr1->count)) {
		/*
		 * Figure the total bytes to be added to the destination leaf.
		 */
		count -= be16_to_cpu(hdr1->count);		/* number entries being moved */
		space  = totallen - be16_to_cpu(hdr1->namebytes);
		space += count * ((uint)sizeof(xfs_dir_leaf_name_t)-1);
		space += count * (uint)sizeof(xfs_dir_leaf_entry_t);

		/*
		 * leaf1 is the destination, compact it if it looks tight.
		 */
		max  = be16_to_cpu(hdr1->firstused) - (uint)sizeof(xfs_dir_leaf_hdr_t);
		max -= be16_to_cpu(hdr1->count) * (uint)sizeof(xfs_dir_leaf_entry_t);
		if (space > max) {
			xfs_dir_leaf_compact(state->args->trans, blk1->bp,
								 0, 0);
		}

		/*
		 * Move low entries from leaf2 to high end of leaf1.
		 */
		xfs_dir_leaf_moveents(leaf2, 0, leaf1, be16_to_cpu(hdr1->count),
					     count, state->mp);

		xfs_da_log_buf(state->args->trans, blk1->bp, 0,
						   state->blocksize-1);
		xfs_da_log_buf(state->args->trans, blk2->bp, 0,
						   state->blocksize-1);
	}

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	blk1->hashval = be32_to_cpu(leaf1->entries[
			be16_to_cpu(leaf1->hdr.count)-1].hashval);
	blk2->hashval = be32_to_cpu(leaf2->entries[
			be16_to_cpu(leaf2->hdr.count)-1].hashval);

	/*
	 * Adjust the expected index for insertion.
	 * GROT: this doesn't work unless blk2 was originally empty.
	 */
	if (!state->inleaf) {
		blk2->index = blk1->index - be16_to_cpu(leaf1->hdr.count);
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
xfs_dir_leaf_figure_balance(xfs_da_state_t *state,
					   xfs_da_state_blk_t *blk1,
					   xfs_da_state_blk_t *blk2,
					   int *countarg, int *namebytesarg)
{
	xfs_dir_leafblock_t *leaf1, *leaf2;
	xfs_dir_leaf_hdr_t *hdr1, *hdr2;
	xfs_dir_leaf_entry_t *entry;
	int count, max, totallen, half;
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
	max = be16_to_cpu(hdr1->count) + be16_to_cpu(hdr2->count);
	half  = (max+1) * (uint)(sizeof(*entry)+sizeof(xfs_dir_leaf_entry_t)-1);
	half += be16_to_cpu(hdr1->namebytes) + be16_to_cpu(hdr2->namebytes) +
		state->args->namelen;
	half /= 2;
	lastdelta = state->blocksize;
	entry = &leaf1->entries[0];
	for (count = 0; count < max; entry++, count++) {

#define XFS_DIR_ABS(A)	(((A) < 0) ? -(A) : (A))
		/*
		 * The new entry is in the first block, account for it.
		 */
		if (count == blk1->index) {
			tmp = totallen + (uint)sizeof(*entry)
				+ XFS_DIR_LEAF_ENTSIZE_BYNAME(state->args->namelen);
			if (XFS_DIR_ABS(half - tmp) > lastdelta)
				break;
			lastdelta = XFS_DIR_ABS(half - tmp);
			totallen = tmp;
			foundit = 1;
		}

		/*
		 * Wrap around into the second block if necessary.
		 */
		if (count == be16_to_cpu(hdr1->count)) {
			leaf1 = leaf2;
			entry = &leaf1->entries[0];
		}

		/*
		 * Figure out if next leaf entry would be too much.
		 */
		tmp = totallen + (uint)sizeof(*entry)
				+ XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry);
		if (XFS_DIR_ABS(half - tmp) > lastdelta)
			break;
		lastdelta = XFS_DIR_ABS(half - tmp);
		totallen = tmp;
#undef XFS_DIR_ABS
	}

	/*
	 * Calculate the number of namebytes that will end up in lower block.
	 * If new entry not in lower block, fix up the count.
	 */
	totallen -=
		count * (uint)(sizeof(*entry)+sizeof(xfs_dir_leaf_entry_t)-1);
	if (foundit) {
		totallen -= (sizeof(*entry)+sizeof(xfs_dir_leaf_entry_t)-1) +
			    state->args->namelen;
	}

	*countarg = count;
	*namebytesarg = totallen;
	return foundit;
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
 */
int
xfs_dir_leaf_toosmall(xfs_da_state_t *state, int *action)
{
	xfs_dir_leafblock_t *leaf;
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
	ASSERT(be16_to_cpu(info->magic) == XFS_DIR_LEAF_MAGIC);
	leaf = (xfs_dir_leafblock_t *)info;
	count = be16_to_cpu(leaf->hdr.count);
	bytes = (uint)sizeof(xfs_dir_leaf_hdr_t) +
		count * (uint)sizeof(xfs_dir_leaf_entry_t) +
		count * ((uint)sizeof(xfs_dir_leaf_name_t)-1) +
		be16_to_cpu(leaf->hdr.namebytes);
	if (bytes > (state->blocksize >> 1)) {
		*action = 0;	/* blk over 50%, don't try to join */
		return 0;
	}

	/*
	 * Check for the degenerate case of the block being empty.
	 * If the block is empty, we'll simply delete it, no need to
	 * coalesce it with a sibling block.  We choose (arbitrarily)
	 * to merge with the forward block unless it is NULL.
	 */
	if (count == 0) {
		/*
		 * Make altpath point to the block we want to keep and
		 * path point to the block we want to drop (this one).
		 */
		forward = (info->forw != 0);
		memcpy(&state->altpath, &state->path, sizeof(state->path));
		error = xfs_da_path_shift(state, &state->altpath, forward,
						 0, &retval);
		if (error)
			return error;
		if (retval) {
			*action = 0;
		} else {
			*action = 2;
		}
		return 0;
	}

	/*
	 * Examine each sibling block to see if we can coalesce with
	 * at least 25% free space to spare.  We need to figure out
	 * whether to merge with the forward or the backward block.
	 * We prefer coalescing with the lower numbered sibling so as
	 * to shrink a directory over time.
	 */
	forward = (be32_to_cpu(info->forw) < be32_to_cpu(info->back));	/* start with smaller blk num */
	for (i = 0; i < 2; forward = !forward, i++) {
		if (forward)
			blkno = be32_to_cpu(info->forw);
		else
			blkno = be32_to_cpu(info->back);
		if (blkno == 0)
			continue;
		error = xfs_da_read_buf(state->args->trans, state->args->dp,
							    blkno, -1, &bp,
							    XFS_DATA_FORK);
		if (error)
			return error;
		ASSERT(bp != NULL);

		leaf = (xfs_dir_leafblock_t *)info;
		count  = be16_to_cpu(leaf->hdr.count);
		bytes  = state->blocksize - (state->blocksize>>2);
		bytes -= be16_to_cpu(leaf->hdr.namebytes);
		leaf = bp->data;
		ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
		count += be16_to_cpu(leaf->hdr.count);
		bytes -= be16_to_cpu(leaf->hdr.namebytes);
		bytes -= count * ((uint)sizeof(xfs_dir_leaf_name_t) - 1);
		bytes -= count * (uint)sizeof(xfs_dir_leaf_entry_t);
		bytes -= (uint)sizeof(xfs_dir_leaf_hdr_t);
		if (bytes >= 0)
			break;	/* fits with at least 25% to spare */

		xfs_da_brelse(state->args->trans, bp);
	}
	if (i >= 2) {
		*action = 0;
		return 0;
	}
	xfs_da_buf_done(bp);

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
		return error;
	if (retval) {
		*action = 0;
	} else {
		*action = 1;
	}
	return 0;
}

/*
 * Remove a name from the leaf directory structure.
 *
 * Return 1 if leaf is less than 37% full, 0 if >= 37% full.
 * If two leaves are 37% full, when combined they will leave 25% free.
 */
int
xfs_dir_leaf_remove(xfs_trans_t *trans, xfs_dabuf_t *bp, int index)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_hdr_t *hdr;
	xfs_dir_leaf_map_t *map;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	int before, after, smallest, entsize;
	int tablesize, tmp, i;
	xfs_mount_t *mp;

	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	hdr = &leaf->hdr;
	mp = trans->t_mountp;
	ASSERT(hdr->count && (be16_to_cpu(hdr->count) < (XFS_LBSIZE(mp)/8)));
	ASSERT((index >= 0) && (index < be16_to_cpu(hdr->count)));
	ASSERT(be16_to_cpu(hdr->firstused) >=
	       ((be16_to_cpu(hdr->count)*sizeof(*entry))+sizeof(*hdr)));
	entry = &leaf->entries[index];
	ASSERT(be16_to_cpu(entry->nameidx) >= be16_to_cpu(hdr->firstused));
	ASSERT(be16_to_cpu(entry->nameidx) < XFS_LBSIZE(mp));

	/*
	 * Scan through free region table:
	 *    check for adjacency of free'd entry with an existing one,
	 *    find smallest free region in case we need to replace it,
	 *    adjust any map that borders the entry table,
	 */
	tablesize = be16_to_cpu(hdr->count) * (uint)sizeof(xfs_dir_leaf_entry_t)
			+ (uint)sizeof(xfs_dir_leaf_hdr_t);
	map = &hdr->freemap[0];
	tmp = be16_to_cpu(map->size);
	before = after = -1;
	smallest = XFS_DIR_LEAF_MAPSIZE - 1;
	entsize = XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry);
	for (i = 0; i < XFS_DIR_LEAF_MAPSIZE; map++, i++) {
		ASSERT(be16_to_cpu(map->base) < XFS_LBSIZE(mp));
		ASSERT(be16_to_cpu(map->size) < XFS_LBSIZE(mp));
		if (be16_to_cpu(map->base) == tablesize) {
			int entry_size = sizeof(xfs_dir_leaf_entry_t);
			be16_add(&map->base, -entry_size);
			be16_add(&map->size, entry_size);
		}

		if ((be16_to_cpu(map->base) + be16_to_cpu(map->size)) ==
				be16_to_cpu(entry->nameidx)) {
			before = i;
		} else if (be16_to_cpu(map->base) ==
				(be16_to_cpu(entry->nameidx) + entsize)) {
			after = i;
		} else if (be16_to_cpu(map->size) < tmp) {
			tmp = be16_to_cpu(map->size);
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
			be16_add(&map->size, entsize);
			be16_add(&map->size, be16_to_cpu(hdr->freemap[after].size));
			hdr->freemap[after].base = 0;
			hdr->freemap[after].size = 0;
		} else if (before >= 0) {
			map = &hdr->freemap[before];
			be16_add(&map->size, entsize);
		} else {
			map = &hdr->freemap[after];
			map->base = entry->nameidx;
			be16_add(&map->size, entsize);
		}
	} else {
		/*
		 * Replace smallest region (if it is smaller than free'd entry)
		 */
		map = &hdr->freemap[smallest];
		if (be16_to_cpu(map->size) < entsize) {
			map->base = entry->nameidx;
			map->size = cpu_to_be16(entsize);
		}
	}

	/*
	 * Did we remove the first entry?
	 */
	if (be16_to_cpu(entry->nameidx) == be16_to_cpu(hdr->firstused))
		smallest = 1;
	else
		smallest = 0;

	/*
	 * Compress the remaining entries and zero out the removed stuff.
	 */
	namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, be16_to_cpu(entry->nameidx));
	memset((char *)namest, 0, entsize);
	xfs_da_log_buf(trans, bp, XFS_DA_LOGRANGE(leaf, namest, entsize));

	be16_add(&hdr->namebytes, -(entry->namelen));
	tmp = (be16_to_cpu(hdr->count) - index) * (uint)sizeof(xfs_dir_leaf_entry_t);
	memmove(entry, entry + 1, tmp);
	be16_add(&hdr->count, -1);
	xfs_da_log_buf(trans, bp,
	    XFS_DA_LOGRANGE(leaf, entry, tmp + (uint)sizeof(*entry)));
	entry = &leaf->entries[be16_to_cpu(hdr->count)];
	memset((char *)entry, 0, sizeof(xfs_dir_leaf_entry_t));

	/*
	 * If we removed the first entry, re-find the first used byte
	 * in the name area.  Note that if the entry was the "firstused",
	 * then we don't have a "hole" in our block resulting from
	 * removing the name.
	 */
	if (smallest) {
		tmp = XFS_LBSIZE(mp);
		entry = &leaf->entries[0];
		for (i = be16_to_cpu(hdr->count)-1; i >= 0; entry++, i--) {
			ASSERT(be16_to_cpu(entry->nameidx) >=
			       be16_to_cpu(hdr->firstused));
			ASSERT(be16_to_cpu(entry->nameidx) < XFS_LBSIZE(mp));
			if (be16_to_cpu(entry->nameidx) < tmp)
				tmp = be16_to_cpu(entry->nameidx);
		}
		hdr->firstused = cpu_to_be16(tmp);
		if (!hdr->firstused)
			hdr->firstused = cpu_to_be16(tmp - 1);
	} else {
		hdr->holes = 1;		/* mark as needing compaction */
	}

	xfs_da_log_buf(trans, bp, XFS_DA_LOGRANGE(leaf, hdr, sizeof(*hdr)));

	/*
	 * Check if leaf is less than 50% full, caller may want to
	 * "join" the leaf with a sibling if so.
	 */
	tmp  = (uint)sizeof(xfs_dir_leaf_hdr_t);
	tmp += be16_to_cpu(leaf->hdr.count) * (uint)sizeof(xfs_dir_leaf_entry_t);
	tmp += be16_to_cpu(leaf->hdr.count) * ((uint)sizeof(xfs_dir_leaf_name_t) - 1);
	tmp += be16_to_cpu(leaf->hdr.namebytes);
	if (tmp < mp->m_dir_magicpct)
		return 1;			/* leaf is < 37% full */
	return 0;
}

/*
 * Move all the directory entries from drop_leaf into save_leaf.
 */
void
xfs_dir_leaf_unbalance(xfs_da_state_t *state, xfs_da_state_blk_t *drop_blk,
				      xfs_da_state_blk_t *save_blk)
{
	xfs_dir_leafblock_t *drop_leaf, *save_leaf, *tmp_leaf;
	xfs_dir_leaf_hdr_t *drop_hdr, *save_hdr, *tmp_hdr;
	xfs_mount_t *mp;
	char *tmpbuffer;

	/*
	 * Set up environment.
	 */
	mp = state->mp;
	ASSERT(drop_blk->magic == XFS_DIR_LEAF_MAGIC);
	ASSERT(save_blk->magic == XFS_DIR_LEAF_MAGIC);
	drop_leaf = drop_blk->bp->data;
	save_leaf = save_blk->bp->data;
	ASSERT(be16_to_cpu(drop_leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	ASSERT(be16_to_cpu(save_leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	drop_hdr = &drop_leaf->hdr;
	save_hdr = &save_leaf->hdr;

	/*
	 * Save last hashval from dying block for later Btree fixup.
	 */
	drop_blk->hashval = be32_to_cpu(drop_leaf->entries[
			be16_to_cpu(drop_leaf->hdr.count)-1].hashval);

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
		if (xfs_dir_leaf_order(save_blk->bp, drop_blk->bp)) {
			xfs_dir_leaf_moveents(drop_leaf, 0, save_leaf, 0,
					be16_to_cpu(drop_hdr->count), mp);
		} else {
			xfs_dir_leaf_moveents(drop_leaf, 0,
					save_leaf, be16_to_cpu(save_hdr->count),
					be16_to_cpu(drop_hdr->count), mp);
		}
	} else {
		/*
		 * Destination has holes, so we make a temporary copy
		 * of the leaf and add them both to that.
		 */
		tmpbuffer = kmem_alloc(state->blocksize, KM_SLEEP);
		ASSERT(tmpbuffer != NULL);
		memset(tmpbuffer, 0, state->blocksize);
		tmp_leaf = (xfs_dir_leafblock_t *)tmpbuffer;
		tmp_hdr = &tmp_leaf->hdr;
		tmp_hdr->info = save_hdr->info;	/* struct copy */
		tmp_hdr->count = 0;
		tmp_hdr->firstused = cpu_to_be16(state->blocksize);
		if (!tmp_hdr->firstused)
			tmp_hdr->firstused = cpu_to_be16(state->blocksize - 1);
		tmp_hdr->namebytes = 0;
		if (xfs_dir_leaf_order(save_blk->bp, drop_blk->bp)) {
			xfs_dir_leaf_moveents(drop_leaf, 0, tmp_leaf, 0,
					be16_to_cpu(drop_hdr->count), mp);
			xfs_dir_leaf_moveents(save_leaf, 0,
					tmp_leaf, be16_to_cpu(tmp_leaf->hdr.count),
					be16_to_cpu(save_hdr->count), mp);
		} else {
			xfs_dir_leaf_moveents(save_leaf, 0, tmp_leaf, 0,
						 be16_to_cpu(save_hdr->count), mp);
			xfs_dir_leaf_moveents(drop_leaf, 0,
					      tmp_leaf, be16_to_cpu(tmp_leaf->hdr.count),
					      be16_to_cpu(drop_hdr->count), mp);
		}
		memcpy(save_leaf, tmp_leaf, state->blocksize);
		kmem_free(tmpbuffer, state->blocksize);
	}

	xfs_da_log_buf(state->args->trans, save_blk->bp, 0,
					   state->blocksize - 1);

	/*
	 * Copy out last hashval in each block for B-tree code.
	 */
	save_blk->hashval = be32_to_cpu(save_leaf->entries[
			be16_to_cpu(save_leaf->hdr.count)-1].hashval);
}

/*========================================================================
 * Routines used for finding things in the Btree.
 *========================================================================*/

/*
 * Look up a name in a leaf directory structure.
 * This is the internal routine, it uses the caller's buffer.
 *
 * Note that duplicate keys are allowed, but only check within the
 * current leaf node.  The Btree code must check in adjacent leaf nodes.
 *
 * Return in *index the index into the entry[] array of either the found
 * entry, or where the entry should have been (insert before that entry).
 *
 * Don't change the args->inumber unless we find the filename.
 */
int
xfs_dir_leaf_lookup_int(xfs_dabuf_t *bp, xfs_da_args_t *args, int *index)
{
	xfs_dir_leafblock_t *leaf;
	xfs_dir_leaf_entry_t *entry;
	xfs_dir_leaf_name_t *namest;
	int probe, span;
	xfs_dahash_t hashval;

	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	ASSERT(be16_to_cpu(leaf->hdr.count) < (XFS_LBSIZE(args->dp->i_mount)/8));

	/*
	 * Binary search.  (note: small blocks will skip this loop)
	 */
	hashval = args->hashval;
	probe = span = be16_to_cpu(leaf->hdr.count) / 2;
	for (entry = &leaf->entries[probe]; span > 4;
		   entry = &leaf->entries[probe]) {
		span /= 2;
		if (be32_to_cpu(entry->hashval) < hashval)
			probe += span;
		else if (be32_to_cpu(entry->hashval) > hashval)
			probe -= span;
		else
			break;
	}
	ASSERT((probe >= 0) && \
	       ((!leaf->hdr.count) || (probe < be16_to_cpu(leaf->hdr.count))));
	ASSERT((span <= 4) || (be32_to_cpu(entry->hashval) == hashval));

	/*
	 * Since we may have duplicate hashval's, find the first matching
	 * hashval in the leaf.
	 */
	while ((probe > 0) && (be32_to_cpu(entry->hashval) >= hashval)) {
		entry--;
		probe--;
	}
	while ((probe < be16_to_cpu(leaf->hdr.count)) &&
	       (be32_to_cpu(entry->hashval) < hashval)) {
		entry++;
		probe++;
	}
	if ((probe == be16_to_cpu(leaf->hdr.count)) ||
	    (be32_to_cpu(entry->hashval) != hashval)) {
		*index = probe;
		ASSERT(args->oknoent);
		return XFS_ERROR(ENOENT);
	}

	/*
	 * Duplicate keys may be present, so search all of them for a match.
	 */
	while ((probe < be16_to_cpu(leaf->hdr.count)) &&
	       (be32_to_cpu(entry->hashval) == hashval)) {
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf, be16_to_cpu(entry->nameidx));
		if (entry->namelen == args->namelen &&
		    namest->name[0] == args->name[0] &&
		    memcmp(args->name, namest->name, args->namelen) == 0) {
			XFS_DIR_SF_GET_DIRINO(&namest->inumber, &args->inumber);
			*index = probe;
			return XFS_ERROR(EEXIST);
		}
		entry++;
		probe++;
	}
	*index = probe;
	ASSERT(probe == be16_to_cpu(leaf->hdr.count) || args->oknoent);
	return XFS_ERROR(ENOENT);
}

/*========================================================================
 * Utility routines.
 *========================================================================*/

/*
 * Move the indicated entries from one leaf to another.
 * NOTE: this routine modifies both source and destination leaves.
 */
/* ARGSUSED */
STATIC void
xfs_dir_leaf_moveents(xfs_dir_leafblock_t *leaf_s, int start_s,
		      xfs_dir_leafblock_t *leaf_d, int start_d,
		      int count, xfs_mount_t *mp)
{
	xfs_dir_leaf_hdr_t *hdr_s, *hdr_d;
	xfs_dir_leaf_entry_t *entry_s, *entry_d;
	int tmp, i;

	/*
	 * Check for nothing to do.
	 */
	if (count == 0)
		return;

	/*
	 * Set up environment.
	 */
	ASSERT(be16_to_cpu(leaf_s->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	ASSERT(be16_to_cpu(leaf_d->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	hdr_s = &leaf_s->hdr;
	hdr_d = &leaf_d->hdr;
	ASSERT(hdr_s->count && (be16_to_cpu(hdr_s->count) < (XFS_LBSIZE(mp)/8)));
	ASSERT(be16_to_cpu(hdr_s->firstused) >=
		((be16_to_cpu(hdr_s->count)*sizeof(*entry_s))+sizeof(*hdr_s)));
	ASSERT(be16_to_cpu(hdr_d->count) < (XFS_LBSIZE(mp)/8));
	ASSERT(be16_to_cpu(hdr_d->firstused) >=
		((be16_to_cpu(hdr_d->count)*sizeof(*entry_d))+sizeof(*hdr_d)));

	ASSERT(start_s < be16_to_cpu(hdr_s->count));
	ASSERT(start_d <= be16_to_cpu(hdr_d->count));
	ASSERT(count <= be16_to_cpu(hdr_s->count));

	/*
	 * Move the entries in the destination leaf up to make a hole?
	 */
	if (start_d < be16_to_cpu(hdr_d->count)) {
		tmp  = be16_to_cpu(hdr_d->count) - start_d;
		tmp *= (uint)sizeof(xfs_dir_leaf_entry_t);
		entry_s = &leaf_d->entries[start_d];
		entry_d = &leaf_d->entries[start_d + count];
		memcpy(entry_d, entry_s, tmp);
	}

	/*
	 * Copy all entry's in the same (sorted) order,
	 * but allocate filenames packed and in sequence.
	 */
	entry_s = &leaf_s->entries[start_s];
	entry_d = &leaf_d->entries[start_d];
	for (i = 0; i < count; entry_s++, entry_d++, i++) {
		ASSERT(be16_to_cpu(entry_s->nameidx) >=
		       be16_to_cpu(hdr_s->firstused));
		tmp = XFS_DIR_LEAF_ENTSIZE_BYENTRY(entry_s);
		be16_add(&hdr_d->firstused, -(tmp));
		entry_d->hashval = entry_s->hashval;
		entry_d->nameidx = hdr_d->firstused;
		entry_d->namelen = entry_s->namelen;
		ASSERT(be16_to_cpu(entry_d->nameidx) + tmp <= XFS_LBSIZE(mp));
		memcpy(XFS_DIR_LEAF_NAMESTRUCT(leaf_d, be16_to_cpu(entry_d->nameidx)),
		       XFS_DIR_LEAF_NAMESTRUCT(leaf_s, be16_to_cpu(entry_s->nameidx)), tmp);
		ASSERT(be16_to_cpu(entry_s->nameidx) + tmp <= XFS_LBSIZE(mp));
		memset((char *)XFS_DIR_LEAF_NAMESTRUCT(leaf_s,
					be16_to_cpu(entry_s->nameidx)), 0, tmp);
		be16_add(&hdr_s->namebytes, -(entry_d->namelen));
		be16_add(&hdr_d->namebytes, entry_d->namelen);
		be16_add(&hdr_s->count, -1);
		be16_add(&hdr_d->count, +1);
		tmp = be16_to_cpu(hdr_d->count) * (uint)sizeof(xfs_dir_leaf_entry_t)
				+ (uint)sizeof(xfs_dir_leaf_hdr_t);
		ASSERT(be16_to_cpu(hdr_d->firstused) >= tmp);

	}

	/*
	 * Zero out the entries we just copied.
	 */
	if (start_s == be16_to_cpu(hdr_s->count)) {
		tmp = count * (uint)sizeof(xfs_dir_leaf_entry_t);
		entry_s = &leaf_s->entries[start_s];
		ASSERT((char *)entry_s + tmp <= (char *)leaf_s + XFS_LBSIZE(mp));
		memset((char *)entry_s, 0, tmp);
	} else {
		/*
		 * Move the remaining entries down to fill the hole,
		 * then zero the entries at the top.
		 */
		tmp  = be16_to_cpu(hdr_s->count) - count;
		tmp *= (uint)sizeof(xfs_dir_leaf_entry_t);
		entry_s = &leaf_s->entries[start_s + count];
		entry_d = &leaf_s->entries[start_s];
		memcpy(entry_d, entry_s, tmp);

		tmp = count * (uint)sizeof(xfs_dir_leaf_entry_t);
		entry_s = &leaf_s->entries[be16_to_cpu(hdr_s->count)];
		ASSERT((char *)entry_s + tmp <= (char *)leaf_s + XFS_LBSIZE(mp));
		memset((char *)entry_s, 0, tmp);
	}

	/*
	 * Fill in the freemap information
	 */
	hdr_d->freemap[0].base = cpu_to_be16(sizeof(xfs_dir_leaf_hdr_t) +
			be16_to_cpu(hdr_d->count) * sizeof(xfs_dir_leaf_entry_t));
	hdr_d->freemap[0].size = cpu_to_be16(be16_to_cpu(hdr_d->firstused) -
			be16_to_cpu(hdr_d->freemap[0].base));
	hdr_d->freemap[1].base = 0;
	hdr_d->freemap[1].size = 0;
	hdr_d->freemap[2].base = 0;
	hdr_d->freemap[2].size = 0;
	hdr_s->holes = 1;	/* leaf may not be compact */
}

/*
 * Compare two leaf blocks "order".
 */
int
xfs_dir_leaf_order(xfs_dabuf_t *leaf1_bp, xfs_dabuf_t *leaf2_bp)
{
	xfs_dir_leafblock_t *leaf1, *leaf2;

	leaf1 = leaf1_bp->data;
	leaf2 = leaf2_bp->data;
	ASSERT((be16_to_cpu(leaf1->hdr.info.magic) == XFS_DIR_LEAF_MAGIC) &&
	       (be16_to_cpu(leaf2->hdr.info.magic) == XFS_DIR_LEAF_MAGIC));
	if (leaf1->hdr.count && leaf2->hdr.count &&
	    ((be32_to_cpu(leaf2->entries[0].hashval) <
	      be32_to_cpu(leaf1->entries[0 ].hashval)) ||
	     (be32_to_cpu(leaf2->entries[
			  be16_to_cpu(leaf2->hdr.count)-1].hashval) <
	      be32_to_cpu(leaf1->entries[
			  be16_to_cpu(leaf1->hdr.count)-1].hashval)))) {
		return 1;
	}
	return 0;
}

/*
 * Pick up the last hashvalue from a leaf block.
 */
xfs_dahash_t
xfs_dir_leaf_lasthash(xfs_dabuf_t *bp, int *count)
{
	xfs_dir_leafblock_t *leaf;

	leaf = bp->data;
	ASSERT(be16_to_cpu(leaf->hdr.info.magic) == XFS_DIR_LEAF_MAGIC);
	if (count)
		*count = be16_to_cpu(leaf->hdr.count);
	if (!leaf->hdr.count)
		return(0);
	return be32_to_cpu(leaf->entries[be16_to_cpu(leaf->hdr.count)-1].hashval);
}

/*
 * Copy out directory entries for getdents(), for leaf directories.
 */
int
xfs_dir_leaf_getdents_int(
	xfs_dabuf_t	*bp,
	xfs_inode_t	*dp,
	xfs_dablk_t	bno,
	uio_t		*uio,
	int		*eobp,
	xfs_dirent_t	*dbp,
	xfs_dir_put_t	put,
	xfs_daddr_t		nextda)
{
	xfs_dir_leafblock_t	*leaf;
	xfs_dir_leaf_entry_t	*entry;
	xfs_dir_leaf_name_t	*namest;
	int			entno, want_entno, i, nextentno;
	xfs_mount_t		*mp;
	xfs_dahash_t		cookhash;
	xfs_dahash_t		nexthash = 0;
#if (BITS_PER_LONG == 32)
	xfs_dahash_t		lasthash = XFS_DA_MAXHASH;
#endif
	xfs_dir_put_args_t	p;

	mp = dp->i_mount;
	leaf = bp->data;
	if (be16_to_cpu(leaf->hdr.info.magic) != XFS_DIR_LEAF_MAGIC) {
		*eobp = 1;
		return XFS_ERROR(ENOENT);	/* XXX wrong code */
	}

	want_entno = XFS_DA_COOKIE_ENTRY(mp, uio->uio_offset);

	cookhash = XFS_DA_COOKIE_HASH(mp, uio->uio_offset);

	xfs_dir_trace_g_dul("leaf: start", dp, uio, leaf);

	/*
	 * Re-find our place.
	 */
	for (i = entno = 0, entry = &leaf->entries[0];
		     i < be16_to_cpu(leaf->hdr.count); entry++, i++) {

		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf,
				    be16_to_cpu(entry->nameidx));

		if (unlikely(
		    ((char *)namest < (char *)leaf) ||
		    ((char *)namest >= (char *)leaf + XFS_LBSIZE(mp)))) {
			XFS_CORRUPTION_ERROR("xfs_dir_leaf_getdents_int(1)",
					     XFS_ERRLEVEL_LOW, mp, leaf);
			xfs_dir_trace_g_du("leaf: corrupted", dp, uio);
			return XFS_ERROR(EFSCORRUPTED);
		}
		if (be32_to_cpu(entry->hashval) >= cookhash) {
			if (entno < want_entno &&
			    be32_to_cpu(entry->hashval) == cookhash) {
				/*
				 * Trying to get to a particular offset in a
				 * run of equal-hashval entries.
				 */
				entno++;
			} else if (want_entno > 0 && entno == want_entno &&
				   be32_to_cpu(entry->hashval) == cookhash) {
				break;
			} else {
				entno = 0;
				break;
			}
		}
	}

	if (i == be16_to_cpu(leaf->hdr.count)) {
		xfs_dir_trace_g_du("leaf: hash not found", dp, uio);
		if (!leaf->hdr.info.forw)
			uio->uio_offset =
				XFS_DA_MAKE_COOKIE(mp, 0, 0, XFS_DA_MAXHASH);
		/*
		 * Don't set uio_offset if there's another block:
		 * the node code will be setting uio_offset anyway.
		 */
		*eobp = 0;
		return 0;
	}
	xfs_dir_trace_g_due("leaf: hash found", dp, uio, entry);

	p.dbp = dbp;
	p.put = put;
	p.uio = uio;

	/*
	 * We're synchronized, start copying entries out to the user.
	 */
	for (; entno >= 0 && i < be16_to_cpu(leaf->hdr.count);
			     entry++, i++, (entno = nextentno)) {
		int lastresid=0, retval;
		xfs_dircook_t lastoffset;
		xfs_dahash_t thishash;

		/*
		 * Check for a damaged directory leaf block and pick up
		 * the inode number from this entry.
		 */
		namest = XFS_DIR_LEAF_NAMESTRUCT(leaf,
				    be16_to_cpu(entry->nameidx));

		if (unlikely(
		    ((char *)namest < (char *)leaf) ||
		    ((char *)namest >= (char *)leaf + XFS_LBSIZE(mp)))) {
			XFS_CORRUPTION_ERROR("xfs_dir_leaf_getdents_int(2)",
					     XFS_ERRLEVEL_LOW, mp, leaf);
			xfs_dir_trace_g_du("leaf: corrupted", dp, uio);
			return XFS_ERROR(EFSCORRUPTED);
		}

		xfs_dir_trace_g_duc("leaf: middle cookie  ",
						   dp, uio, p.cook.o);

		if (i < (be16_to_cpu(leaf->hdr.count) - 1)) {
			nexthash = be32_to_cpu(entry[1].hashval);

			if (nexthash == be32_to_cpu(entry->hashval))
				nextentno = entno + 1;
			else
				nextentno = 0;
			XFS_PUT_COOKIE(p.cook, mp, bno, nextentno, nexthash);
			xfs_dir_trace_g_duc("leaf: middle cookie  ",
						   dp, uio, p.cook.o);

		} else if ((thishash = be32_to_cpu(leaf->hdr.info.forw))) {
			xfs_dabuf_t *bp2;
			xfs_dir_leafblock_t *leaf2;

			ASSERT(nextda != -1);

			retval = xfs_da_read_buf(dp->i_transp, dp, thishash,
						 nextda, &bp2, XFS_DATA_FORK);
			if (retval)
				return retval;

			ASSERT(bp2 != NULL);

			leaf2 = bp2->data;

			if (unlikely(
			       (be16_to_cpu(leaf2->hdr.info.magic)
						!= XFS_DIR_LEAF_MAGIC)
			    || (be32_to_cpu(leaf2->hdr.info.back)
						!= bno))) {	/* GROT */
				XFS_CORRUPTION_ERROR("xfs_dir_leaf_getdents_int(3)",
						     XFS_ERRLEVEL_LOW, mp,
						     leaf2);
				xfs_da_brelse(dp->i_transp, bp2);

				return XFS_ERROR(EFSCORRUPTED);
			}

			nexthash = be32_to_cpu(leaf2->entries[0].hashval);
			nextentno = -1;
			XFS_PUT_COOKIE(p.cook, mp, thishash, 0, nexthash);
			xfs_da_brelse(dp->i_transp, bp2);
			xfs_dir_trace_g_duc("leaf: next blk cookie",
						   dp, uio, p.cook.o);
		} else {
			nextentno = -1;
			XFS_PUT_COOKIE(p.cook, mp, 0, 0, XFS_DA_MAXHASH);
		}

		/*
		 * Save off the cookie so we can fall back should the
		 * 'put' into the outgoing buffer fails.  To handle a run
		 * of equal-hashvals, the off_t structure on 64bit
		 * builds has entno built into the cookie to ID the
		 * entry.  On 32bit builds, we only have space for the
		 * hashval so we can't ID specific entries within a group
		 * of same hashval entries.   For this, lastoffset is set
		 * to the first in the run of equal hashvals so we don't
		 * include any entries unless we can include all entries
		 * that share the same hashval.  Hopefully the buffer
		 * provided is big enough to handle it (see pv763517).
		 */
		thishash = be32_to_cpu(entry->hashval);
#if (BITS_PER_LONG == 32)
		if (thishash != lasthash) {
			XFS_PUT_COOKIE(lastoffset, mp, bno, entno, thishash);
			lastresid = uio->uio_resid;
			lasthash = thishash;
		} else {
			xfs_dir_trace_g_duc("leaf: DUP COOKIES, skipped",
						   dp, uio, p.cook.o);
		}
#else
		XFS_PUT_COOKIE(lastoffset, mp, bno, entno, thishash);
		lastresid = uio->uio_resid;
#endif /* BITS_PER_LONG == 32 */

		/*
		 * Put the current entry into the outgoing buffer.  If we fail
		 * then restore the UIO to the first entry in the current
		 * run of equal-hashval entries (probably one 1 entry long).
		 */
		p.ino = XFS_GET_DIR_INO8(namest->inumber);
#if XFS_BIG_INUMS
		p.ino += mp->m_inoadd;
#endif
		p.name = (char *)namest->name;
		p.namelen = entry->namelen;

		retval = p.put(&p);

		if (!p.done) {
			uio->uio_offset = lastoffset.o;
			uio->uio_resid = lastresid;

			*eobp = 1;

			xfs_dir_trace_g_du("leaf: E-O-B", dp, uio);

			return retval;
		}
	}

	uio->uio_offset = p.cook.o;

	*eobp = 0;

	xfs_dir_trace_g_du("leaf: E-O-F", dp, uio);

	return 0;
}

/*
 * Format a dirent64 structure and copy it out the the user's buffer.
 */
int
xfs_dir_put_dirent64_direct(xfs_dir_put_args_t *pa)
{
	iovec_t *iovp;
	int reclen, namelen;
	xfs_dirent_t *idbp;
	uio_t *uio;

	namelen = pa->namelen;
	reclen = DIRENTSIZE(namelen);
	uio = pa->uio;
	if (reclen > uio->uio_resid) {
		pa->done = 0;
		return 0;
	}
	iovp = uio->uio_iov;
	idbp = (xfs_dirent_t *)iovp->iov_base;
	iovp->iov_base = (char *)idbp + reclen;
	iovp->iov_len -= reclen;
	uio->uio_resid -= reclen;
	idbp->d_reclen = reclen;
	idbp->d_ino = pa->ino;
	idbp->d_off = pa->cook.o;
	idbp->d_name[namelen] = '\0';
	pa->done = 1;
	memcpy(idbp->d_name, pa->name, namelen);
	return 0;
}

/*
 * Format a dirent64 structure and copy it out the the user's buffer.
 */
int
xfs_dir_put_dirent64_uio(xfs_dir_put_args_t *pa)
{
	int		retval, reclen, namelen;
	xfs_dirent_t	*idbp;
	uio_t		*uio;

	namelen = pa->namelen;
	reclen = DIRENTSIZE(namelen);
	uio = pa->uio;
	if (reclen > uio->uio_resid) {
		pa->done = 0;
		return 0;
	}
	idbp = pa->dbp;
	idbp->d_reclen = reclen;
	idbp->d_ino = pa->ino;
	idbp->d_off = pa->cook.o;
	idbp->d_name[namelen] = '\0';
	memcpy(idbp->d_name, pa->name, namelen);
	retval = uio_read((caddr_t)idbp, reclen, uio);
	pa->done = (retval == 0);
	return retval;
}
