/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * Copyright (c) 2012 Red Hat, Inc.
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
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_da_format.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_trans.h"
#include "xfs_extfree_item.h"
#include "xfs_alloc.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_bmap_btree.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_quota.h"
#include "xfs_trans_space.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_log.h"
#include "xfs_dinode.h"

/* Kernel only BMAP related definitions and functions */

/*
 * Convert the given file system block to a disk block.  We have to treat it
 * differently based on whether the file is a real time file or not, because the
 * bmap code does.
 */
xfs_daddr_t
xfs_fsb_to_db(struct xfs_inode *ip, xfs_fsblock_t fsb)
{
	return (XFS_IS_REALTIME_INODE(ip) ? \
		 (xfs_daddr_t)XFS_FSB_TO_BB((ip)->i_mount, (fsb)) : \
		 XFS_FSB_TO_DADDR((ip)->i_mount, (fsb)));
}

/*
 * Routine to be called at transaction's end by xfs_bmapi, xfs_bunmapi
 * caller.  Frees all the extents that need freeing, which must be done
 * last due to locking considerations.  We never free any extents in
 * the first transaction.
 *
 * Return 1 if the given transaction was committed and a new one
 * started, and 0 otherwise in the committed parameter.
 */
int						/* error */
xfs_bmap_finish(
	xfs_trans_t		**tp,		/* transaction pointer addr */
	xfs_bmap_free_t		*flist,		/* i/o: list extents to free */
	int			*committed)	/* xact committed or not */
{
	xfs_efd_log_item_t	*efd;		/* extent free data */
	xfs_efi_log_item_t	*efi;		/* extent free intention */
	int			error;		/* error return value */
	xfs_bmap_free_item_t	*free;		/* free extent item */
	struct xfs_trans_res	tres;		/* new log reservation */
	xfs_mount_t		*mp;		/* filesystem mount structure */
	xfs_bmap_free_item_t	*next;		/* next item on free list */
	xfs_trans_t		*ntp;		/* new transaction pointer */

	ASSERT((*tp)->t_flags & XFS_TRANS_PERM_LOG_RES);
	if (flist->xbf_count == 0) {
		*committed = 0;
		return 0;
	}
	ntp = *tp;
	efi = xfs_trans_get_efi(ntp, flist->xbf_count);
	for (free = flist->xbf_first; free; free = free->xbfi_next)
		xfs_trans_log_efi_extent(ntp, efi, free->xbfi_startblock,
			free->xbfi_blockcount);

	tres.tr_logres = ntp->t_log_res;
	tres.tr_logcount = ntp->t_log_count;
	tres.tr_logflags = XFS_TRANS_PERM_LOG_RES;
	ntp = xfs_trans_dup(*tp);
	error = xfs_trans_commit(*tp, 0);
	*tp = ntp;
	*committed = 1;
	/*
	 * We have a new transaction, so we should return committed=1,
	 * even though we're returning an error.
	 */
	if (error)
		return error;

	/*
	 * transaction commit worked ok so we can drop the extra ticket
	 * reference that we gained in xfs_trans_dup()
	 */
	xfs_log_ticket_put(ntp->t_ticket);

	error = xfs_trans_reserve(ntp, &tres, 0, 0);
	if (error)
		return error;
	efd = xfs_trans_get_efd(ntp, efi, flist->xbf_count);
	for (free = flist->xbf_first; free != NULL; free = next) {
		next = free->xbfi_next;
		if ((error = xfs_free_extent(ntp, free->xbfi_startblock,
				free->xbfi_blockcount))) {
			/*
			 * The bmap free list will be cleaned up at a
			 * higher level.  The EFI will be canceled when
			 * this transaction is aborted.
			 * Need to force shutdown here to make sure it
			 * happens, since this transaction may not be
			 * dirty yet.
			 */
			mp = ntp->t_mountp;
			if (!XFS_FORCED_SHUTDOWN(mp))
				xfs_force_shutdown(mp,
						   (error == -EFSCORRUPTED) ?
						   SHUTDOWN_CORRUPT_INCORE :
						   SHUTDOWN_META_IO_ERROR);
			return error;
		}
		xfs_trans_log_efd_extent(ntp, efd, free->xbfi_startblock,
			free->xbfi_blockcount);
		xfs_bmap_del_free(flist, NULL, free);
	}
	return 0;
}

int
xfs_bmap_rtalloc(
	struct xfs_bmalloca	*ap)	/* bmap alloc argument struct */
{
	xfs_alloctype_t	atype = 0;	/* type for allocation routines */
	int		error;		/* error return value */
	xfs_mount_t	*mp;		/* mount point structure */
	xfs_extlen_t	prod = 0;	/* product factor for allocators */
	xfs_extlen_t	ralen = 0;	/* realtime allocation length */
	xfs_extlen_t	align;		/* minimum allocation alignment */
	xfs_rtblock_t	rtb;

	mp = ap->ip->i_mount;
	align = xfs_get_extsz_hint(ap->ip);
	prod = align / mp->m_sb.sb_rextsize;
	error = xfs_bmap_extsize_align(mp, &ap->got, &ap->prev,
					align, 1, ap->eof, 0,
					ap->conv, &ap->offset, &ap->length);
	if (error)
		return error;
	ASSERT(ap->length);
	ASSERT(ap->length % mp->m_sb.sb_rextsize == 0);

	/*
	 * If the offset & length are not perfectly aligned
	 * then kill prod, it will just get us in trouble.
	 */
	if (do_mod(ap->offset, align) || ap->length % align)
		prod = 1;
	/*
	 * Set ralen to be the actual requested length in rtextents.
	 */
	ralen = ap->length / mp->m_sb.sb_rextsize;
	/*
	 * If the old value was close enough to MAXEXTLEN that
	 * we rounded up to it, cut it back so it's valid again.
	 * Note that if it's a really large request (bigger than
	 * MAXEXTLEN), we don't hear about that number, and can't
	 * adjust the starting point to match it.
	 */
	if (ralen * mp->m_sb.sb_rextsize >= MAXEXTLEN)
		ralen = MAXEXTLEN / mp->m_sb.sb_rextsize;

	/*
	 * Lock out other modifications to the RT bitmap inode.
	 */
	xfs_ilock(mp->m_rbmip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(ap->tp, mp->m_rbmip, XFS_ILOCK_EXCL);

	/*
	 * If it's an allocation to an empty file at offset 0,
	 * pick an extent that will space things out in the rt area.
	 */
	if (ap->eof && ap->offset == 0) {
		xfs_rtblock_t uninitialized_var(rtx); /* realtime extent no */

		error = xfs_rtpick_extent(mp, ap->tp, ralen, &rtx);
		if (error)
			return error;
		ap->blkno = rtx * mp->m_sb.sb_rextsize;
	} else {
		ap->blkno = 0;
	}

	xfs_bmap_adjacent(ap);

	/*
	 * Realtime allocation, done through xfs_rtallocate_extent.
	 */
	atype = ap->blkno == 0 ?  XFS_ALLOCTYPE_ANY_AG : XFS_ALLOCTYPE_NEAR_BNO;
	do_div(ap->blkno, mp->m_sb.sb_rextsize);
	rtb = ap->blkno;
	ap->length = ralen;
	if ((error = xfs_rtallocate_extent(ap->tp, ap->blkno, 1, ap->length,
				&ralen, atype, ap->wasdel, prod, &rtb)))
		return error;
	if (rtb == NULLFSBLOCK && prod > 1 &&
	    (error = xfs_rtallocate_extent(ap->tp, ap->blkno, 1,
					   ap->length, &ralen, atype,
					   ap->wasdel, 1, &rtb)))
		return error;
	ap->blkno = rtb;
	if (ap->blkno != NULLFSBLOCK) {
		ap->blkno *= mp->m_sb.sb_rextsize;
		ralen *= mp->m_sb.sb_rextsize;
		ap->length = ralen;
		ap->ip->i_d.di_nblocks += ralen;
		xfs_trans_log_inode(ap->tp, ap->ip, XFS_ILOG_CORE);
		if (ap->wasdel)
			ap->ip->i_delayed_blks -= ralen;
		/*
		 * Adjust the disk quota also. This was reserved
		 * earlier.
		 */
		xfs_trans_mod_dquot_byino(ap->tp, ap->ip,
			ap->wasdel ? XFS_TRANS_DQ_DELRTBCOUNT :
					XFS_TRANS_DQ_RTBCOUNT, (long) ralen);
	} else {
		ap->length = 0;
	}
	return 0;
}

/*
 * Check if the endoff is outside the last extent. If so the caller will grow
 * the allocation to a stripe unit boundary.  All offsets are considered outside
 * the end of file for an empty fork, so 1 is returned in *eof in that case.
 */
int
xfs_bmap_eof(
	struct xfs_inode	*ip,
	xfs_fileoff_t		endoff,
	int			whichfork,
	int			*eof)
{
	struct xfs_bmbt_irec	rec;
	int			error;

	error = xfs_bmap_last_extent(NULL, ip, whichfork, &rec, eof);
	if (error || *eof)
		return error;

	*eof = endoff >= rec.br_startoff + rec.br_blockcount;
	return 0;
}

/*
 * Extent tree block counting routines.
 */

/*
 * Count leaf blocks given a range of extent records.
 */
STATIC void
xfs_bmap_count_leaves(
	xfs_ifork_t		*ifp,
	xfs_extnum_t		idx,
	int			numrecs,
	int			*count)
{
	int		b;

	for (b = 0; b < numrecs; b++) {
		xfs_bmbt_rec_host_t *frp = xfs_iext_get_ext(ifp, idx + b);
		*count += xfs_bmbt_get_blockcount(frp);
	}
}

/*
 * Count leaf blocks given a range of extent records originally
 * in btree format.
 */
STATIC void
xfs_bmap_disk_count_leaves(
	struct xfs_mount	*mp,
	struct xfs_btree_block	*block,
	int			numrecs,
	int			*count)
{
	int		b;
	xfs_bmbt_rec_t	*frp;

	for (b = 1; b <= numrecs; b++) {
		frp = XFS_BMBT_REC_ADDR(mp, block, b);
		*count += xfs_bmbt_disk_get_blockcount(frp);
	}
}

/*
 * Recursively walks each level of a btree
 * to count total fsblocks in use.
 */
STATIC int                                     /* error */
xfs_bmap_count_tree(
	xfs_mount_t     *mp,            /* file system mount point */
	xfs_trans_t     *tp,            /* transaction pointer */
	xfs_ifork_t	*ifp,		/* inode fork pointer */
	xfs_fsblock_t   blockno,	/* file system block number */
	int             levelin,	/* level in btree */
	int		*count)		/* Count of blocks */
{
	int			error;
	xfs_buf_t		*bp, *nbp;
	int			level = levelin;
	__be64			*pp;
	xfs_fsblock_t           bno = blockno;
	xfs_fsblock_t		nextbno;
	struct xfs_btree_block	*block, *nextblock;
	int			numrecs;

	error = xfs_btree_read_bufl(mp, tp, bno, 0, &bp, XFS_BMAP_BTREE_REF,
						&xfs_bmbt_buf_ops);
	if (error)
		return error;
	*count += 1;
	block = XFS_BUF_TO_BLOCK(bp);

	if (--level) {
		/* Not at node above leaves, count this level of nodes */
		nextbno = be64_to_cpu(block->bb_u.l.bb_rightsib);
		while (nextbno != NULLFSBLOCK) {
			error = xfs_btree_read_bufl(mp, tp, nextbno, 0, &nbp,
						XFS_BMAP_BTREE_REF,
						&xfs_bmbt_buf_ops);
			if (error)
				return error;
			*count += 1;
			nextblock = XFS_BUF_TO_BLOCK(nbp);
			nextbno = be64_to_cpu(nextblock->bb_u.l.bb_rightsib);
			xfs_trans_brelse(tp, nbp);
		}

		/* Dive to the next level */
		pp = XFS_BMBT_PTR_ADDR(mp, block, 1, mp->m_bmap_dmxr[1]);
		bno = be64_to_cpu(*pp);
		if (unlikely((error =
		     xfs_bmap_count_tree(mp, tp, ifp, bno, level, count)) < 0)) {
			xfs_trans_brelse(tp, bp);
			XFS_ERROR_REPORT("xfs_bmap_count_tree(1)",
					 XFS_ERRLEVEL_LOW, mp);
			return -EFSCORRUPTED;
		}
		xfs_trans_brelse(tp, bp);
	} else {
		/* count all level 1 nodes and their leaves */
		for (;;) {
			nextbno = be64_to_cpu(block->bb_u.l.bb_rightsib);
			numrecs = be16_to_cpu(block->bb_numrecs);
			xfs_bmap_disk_count_leaves(mp, block, numrecs, count);
			xfs_trans_brelse(tp, bp);
			if (nextbno == NULLFSBLOCK)
				break;
			bno = nextbno;
			error = xfs_btree_read_bufl(mp, tp, bno, 0, &bp,
						XFS_BMAP_BTREE_REF,
						&xfs_bmbt_buf_ops);
			if (error)
				return error;
			*count += 1;
			block = XFS_BUF_TO_BLOCK(bp);
		}
	}
	return 0;
}

/*
 * Count fsblocks of the given fork.
 */
int						/* error */
xfs_bmap_count_blocks(
	xfs_trans_t		*tp,		/* transaction pointer */
	xfs_inode_t		*ip,		/* incore inode */
	int			whichfork,	/* data or attr fork */
	int			*count)		/* out: count of blocks */
{
	struct xfs_btree_block	*block;	/* current btree block */
	xfs_fsblock_t		bno;	/* block # of "block" */
	xfs_ifork_t		*ifp;	/* fork structure */
	int			level;	/* btree level, for checking */
	xfs_mount_t		*mp;	/* file system mount structure */
	__be64			*pp;	/* pointer to block address */

	bno = NULLFSBLOCK;
	mp = ip->i_mount;
	ifp = XFS_IFORK_PTR(ip, whichfork);
	if ( XFS_IFORK_FORMAT(ip, whichfork) == XFS_DINODE_FMT_EXTENTS ) {
		xfs_bmap_count_leaves(ifp, 0,
			ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t),
			count);
		return 0;
	}

	/*
	 * Root level must use BMAP_BROOT_PTR_ADDR macro to get ptr out.
	 */
	block = ifp->if_broot;
	level = be16_to_cpu(block->bb_level);
	ASSERT(level > 0);
	pp = XFS_BMAP_BROOT_PTR_ADDR(mp, block, 1, ifp->if_broot_bytes);
	bno = be64_to_cpu(*pp);
	ASSERT(bno != NULLFSBLOCK);
	ASSERT(XFS_FSB_TO_AGNO(mp, bno) < mp->m_sb.sb_agcount);
	ASSERT(XFS_FSB_TO_AGBNO(mp, bno) < mp->m_sb.sb_agblocks);

	if (unlikely(xfs_bmap_count_tree(mp, tp, ifp, bno, level, count) < 0)) {
		XFS_ERROR_REPORT("xfs_bmap_count_blocks(2)", XFS_ERRLEVEL_LOW,
				 mp);
		return -EFSCORRUPTED;
	}

	return 0;
}

/*
 * returns 1 for success, 0 if we failed to map the extent.
 */
STATIC int
xfs_getbmapx_fix_eof_hole(
	xfs_inode_t		*ip,		/* xfs incore inode pointer */
	struct getbmapx		*out,		/* output structure */
	int			prealloced,	/* this is a file with
						 * preallocated data space */
	__int64_t		end,		/* last block requested */
	xfs_fsblock_t		startblock)
{
	__int64_t		fixlen;
	xfs_mount_t		*mp;		/* file system mount point */
	xfs_ifork_t		*ifp;		/* inode fork pointer */
	xfs_extnum_t		lastx;		/* last extent pointer */
	xfs_fileoff_t		fileblock;

	if (startblock == HOLESTARTBLOCK) {
		mp = ip->i_mount;
		out->bmv_block = -1;
		fixlen = XFS_FSB_TO_BB(mp, XFS_B_TO_FSB(mp, XFS_ISIZE(ip)));
		fixlen -= out->bmv_offset;
		if (prealloced && out->bmv_offset + out->bmv_length == end) {
			/* Came to hole at EOF. Trim it. */
			if (fixlen <= 0)
				return 0;
			out->bmv_length = fixlen;
		}
	} else {
		if (startblock == DELAYSTARTBLOCK)
			out->bmv_block = -2;
		else
			out->bmv_block = xfs_fsb_to_db(ip, startblock);
		fileblock = XFS_BB_TO_FSB(ip->i_mount, out->bmv_offset);
		ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
		if (xfs_iext_bno_to_ext(ifp, fileblock, &lastx) &&
		   (lastx == (ifp->if_bytes / (uint)sizeof(xfs_bmbt_rec_t))-1))
			out->bmv_oflags |= BMV_OF_LAST;
	}

	return 1;
}

/*
 * Get inode's extents as described in bmv, and format for output.
 * Calls formatter to fill the user's buffer until all extents
 * are mapped, until the passed-in bmv->bmv_count slots have
 * been filled, or until the formatter short-circuits the loop,
 * if it is tracking filled-in extents on its own.
 */
int						/* error code */
xfs_getbmap(
	xfs_inode_t		*ip,
	struct getbmapx		*bmv,		/* user bmap structure */
	xfs_bmap_format_t	formatter,	/* format to user */
	void			*arg)		/* formatter arg */
{
	__int64_t		bmvend;		/* last block requested */
	int			error = 0;	/* return value */
	__int64_t		fixlen;		/* length for -1 case */
	int			i;		/* extent number */
	int			lock;		/* lock state */
	xfs_bmbt_irec_t		*map;		/* buffer for user's data */
	xfs_mount_t		*mp;		/* file system mount point */
	int			nex;		/* # of user extents can do */
	int			nexleft;	/* # of user extents left */
	int			subnex;		/* # of bmapi's can do */
	int			nmap;		/* number of map entries */
	struct getbmapx		*out;		/* output structure */
	int			whichfork;	/* data or attr fork */
	int			prealloced;	/* this is a file with
						 * preallocated data space */
	int			iflags;		/* interface flags */
	int			bmapi_flags;	/* flags for xfs_bmapi */
	int			cur_ext = 0;

	mp = ip->i_mount;
	iflags = bmv->bmv_iflags;
	whichfork = iflags & BMV_IF_ATTRFORK ? XFS_ATTR_FORK : XFS_DATA_FORK;

	if (whichfork == XFS_ATTR_FORK) {
		if (XFS_IFORK_Q(ip)) {
			if (ip->i_d.di_aformat != XFS_DINODE_FMT_EXTENTS &&
			    ip->i_d.di_aformat != XFS_DINODE_FMT_BTREE &&
			    ip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)
				return -EINVAL;
		} else if (unlikely(
			   ip->i_d.di_aformat != 0 &&
			   ip->i_d.di_aformat != XFS_DINODE_FMT_EXTENTS)) {
			XFS_ERROR_REPORT("xfs_getbmap", XFS_ERRLEVEL_LOW,
					 ip->i_mount);
			return -EFSCORRUPTED;
		}

		prealloced = 0;
		fixlen = 1LL << 32;
	} else {
		if (ip->i_d.di_format != XFS_DINODE_FMT_EXTENTS &&
		    ip->i_d.di_format != XFS_DINODE_FMT_BTREE &&
		    ip->i_d.di_format != XFS_DINODE_FMT_LOCAL)
			return -EINVAL;

		if (xfs_get_extsz_hint(ip) ||
		    ip->i_d.di_flags & (XFS_DIFLAG_PREALLOC|XFS_DIFLAG_APPEND)){
			prealloced = 1;
			fixlen = mp->m_super->s_maxbytes;
		} else {
			prealloced = 0;
			fixlen = XFS_ISIZE(ip);
		}
	}

	if (bmv->bmv_length == -1) {
		fixlen = XFS_FSB_TO_BB(mp, XFS_B_TO_FSB(mp, fixlen));
		bmv->bmv_length =
			max_t(__int64_t, fixlen - bmv->bmv_offset, 0);
	} else if (bmv->bmv_length == 0) {
		bmv->bmv_entries = 0;
		return 0;
	} else if (bmv->bmv_length < 0) {
		return -EINVAL;
	}

	nex = bmv->bmv_count - 1;
	if (nex <= 0)
		return -EINVAL;
	bmvend = bmv->bmv_offset + bmv->bmv_length;


	if (bmv->bmv_count > ULONG_MAX / sizeof(struct getbmapx))
		return -ENOMEM;
	out = kmem_zalloc_large(bmv->bmv_count * sizeof(struct getbmapx), 0);
	if (!out)
		return -ENOMEM;

	xfs_ilock(ip, XFS_IOLOCK_SHARED);
	if (whichfork == XFS_DATA_FORK) {
		if (!(iflags & BMV_IF_DELALLOC) &&
		    (ip->i_delayed_blks || XFS_ISIZE(ip) > ip->i_d.di_size)) {
			error = filemap_write_and_wait(VFS_I(ip)->i_mapping);
			if (error)
				goto out_unlock_iolock;

			/*
			 * Even after flushing the inode, there can still be
			 * delalloc blocks on the inode beyond EOF due to
			 * speculative preallocation.  These are not removed
			 * until the release function is called or the inode
			 * is inactivated.  Hence we cannot assert here that
			 * ip->i_delayed_blks == 0.
			 */
		}

		lock = xfs_ilock_data_map_shared(ip);
	} else {
		lock = xfs_ilock_attr_map_shared(ip);
	}

	/*
	 * Don't let nex be bigger than the number of extents
	 * we can have assuming alternating holes and real extents.
	 */
	if (nex > XFS_IFORK_NEXTENTS(ip, whichfork) * 2 + 1)
		nex = XFS_IFORK_NEXTENTS(ip, whichfork) * 2 + 1;

	bmapi_flags = xfs_bmapi_aflag(whichfork);
	if (!(iflags & BMV_IF_PREALLOC))
		bmapi_flags |= XFS_BMAPI_IGSTATE;

	/*
	 * Allocate enough space to handle "subnex" maps at a time.
	 */
	error = -ENOMEM;
	subnex = 16;
	map = kmem_alloc(subnex * sizeof(*map), KM_MAYFAIL | KM_NOFS);
	if (!map)
		goto out_unlock_ilock;

	bmv->bmv_entries = 0;

	if (XFS_IFORK_NEXTENTS(ip, whichfork) == 0 &&
	    (whichfork == XFS_ATTR_FORK || !(iflags & BMV_IF_DELALLOC))) {
		error = 0;
		goto out_free_map;
	}

	nexleft = nex;

	do {
		nmap = (nexleft > subnex) ? subnex : nexleft;
		error = xfs_bmapi_read(ip, XFS_BB_TO_FSBT(mp, bmv->bmv_offset),
				       XFS_BB_TO_FSB(mp, bmv->bmv_length),
				       map, &nmap, bmapi_flags);
		if (error)
			goto out_free_map;
		ASSERT(nmap <= subnex);

		for (i = 0; i < nmap && nexleft && bmv->bmv_length; i++) {
			out[cur_ext].bmv_oflags = 0;
			if (map[i].br_state == XFS_EXT_UNWRITTEN)
				out[cur_ext].bmv_oflags |= BMV_OF_PREALLOC;
			else if (map[i].br_startblock == DELAYSTARTBLOCK)
				out[cur_ext].bmv_oflags |= BMV_OF_DELALLOC;
			out[cur_ext].bmv_offset =
				XFS_FSB_TO_BB(mp, map[i].br_startoff);
			out[cur_ext].bmv_length =
				XFS_FSB_TO_BB(mp, map[i].br_blockcount);
			out[cur_ext].bmv_unused1 = 0;
			out[cur_ext].bmv_unused2 = 0;

			/*
			 * delayed allocation extents that start beyond EOF can
			 * occur due to speculative EOF allocation when the
			 * delalloc extent is larger than the largest freespace
			 * extent at conversion time. These extents cannot be
			 * converted by data writeback, so can exist here even
			 * if we are not supposed to be finding delalloc
			 * extents.
			 */
			if (map[i].br_startblock == DELAYSTARTBLOCK &&
			    map[i].br_startoff <= XFS_B_TO_FSB(mp, XFS_ISIZE(ip)))
				ASSERT((iflags & BMV_IF_DELALLOC) != 0);

                        if (map[i].br_startblock == HOLESTARTBLOCK &&
			    whichfork == XFS_ATTR_FORK) {
				/* came to the end of attribute fork */
				out[cur_ext].bmv_oflags |= BMV_OF_LAST;
				goto out_free_map;
			}

			if (!xfs_getbmapx_fix_eof_hole(ip, &out[cur_ext],
					prealloced, bmvend,
					map[i].br_startblock))
				goto out_free_map;

			bmv->bmv_offset =
				out[cur_ext].bmv_offset +
				out[cur_ext].bmv_length;
			bmv->bmv_length =
				max_t(__int64_t, 0, bmvend - bmv->bmv_offset);

			/*
			 * In case we don't want to return the hole,
			 * don't increase cur_ext so that we can reuse
			 * it in the next loop.
			 */
			if ((iflags & BMV_IF_NO_HOLES) &&
			    map[i].br_startblock == HOLESTARTBLOCK) {
				memset(&out[cur_ext], 0, sizeof(out[cur_ext]));
				continue;
			}

			nexleft--;
			bmv->bmv_entries++;
			cur_ext++;
		}
	} while (nmap && nexleft && bmv->bmv_length);

 out_free_map:
	kmem_free(map);
 out_unlock_ilock:
	xfs_iunlock(ip, lock);
 out_unlock_iolock:
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);

	for (i = 0; i < cur_ext; i++) {
		int full = 0;	/* user array is full */

		/* format results & advance arg */
		error = formatter(&arg, &out[i], &full);
		if (error || full)
			break;
	}

	kmem_free(out);
	return error;
}

/*
 * dead simple method of punching delalyed allocation blocks from a range in
 * the inode. Walks a block at a time so will be slow, but is only executed in
 * rare error cases so the overhead is not critical. This will always punch out
 * both the start and end blocks, even if the ranges only partially overlap
 * them, so it is up to the caller to ensure that partial blocks are not
 * passed in.
 */
int
xfs_bmap_punch_delalloc_range(
	struct xfs_inode	*ip,
	xfs_fileoff_t		start_fsb,
	xfs_fileoff_t		length)
{
	xfs_fileoff_t		remaining = length;
	int			error = 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	do {
		int		done;
		xfs_bmbt_irec_t	imap;
		int		nimaps = 1;
		xfs_fsblock_t	firstblock;
		xfs_bmap_free_t flist;

		/*
		 * Map the range first and check that it is a delalloc extent
		 * before trying to unmap the range. Otherwise we will be
		 * trying to remove a real extent (which requires a
		 * transaction) or a hole, which is probably a bad idea...
		 */
		error = xfs_bmapi_read(ip, start_fsb, 1, &imap, &nimaps,
				       XFS_BMAPI_ENTIRE);

		if (error) {
			/* something screwed, just bail */
			if (!XFS_FORCED_SHUTDOWN(ip->i_mount)) {
				xfs_alert(ip->i_mount,
			"Failed delalloc mapping lookup ino %lld fsb %lld.",
						ip->i_ino, start_fsb);
			}
			break;
		}
		if (!nimaps) {
			/* nothing there */
			goto next_block;
		}
		if (imap.br_startblock != DELAYSTARTBLOCK) {
			/* been converted, ignore */
			goto next_block;
		}
		WARN_ON(imap.br_blockcount == 0);

		/*
		 * Note: while we initialise the firstblock/flist pair, they
		 * should never be used because blocks should never be
		 * allocated or freed for a delalloc extent and hence we need
		 * don't cancel or finish them after the xfs_bunmapi() call.
		 */
		xfs_bmap_init(&flist, &firstblock);
		error = xfs_bunmapi(NULL, ip, start_fsb, 1, 0, 1, &firstblock,
					&flist, &done);
		if (error)
			break;

		ASSERT(!flist.xbf_count && !flist.xbf_first);
next_block:
		start_fsb++;
		remaining--;
	} while(remaining > 0);

	return error;
}

/*
 * Test whether it is appropriate to check an inode for and free post EOF
 * blocks. The 'force' parameter determines whether we should also consider
 * regular files that are marked preallocated or append-only.
 */
bool
xfs_can_free_eofblocks(struct xfs_inode *ip, bool force)
{
	/* prealloc/delalloc exists only on regular files */
	if (!S_ISREG(ip->i_d.di_mode))
		return false;

	/*
	 * Zero sized files with no cached pages and delalloc blocks will not
	 * have speculative prealloc/delalloc blocks to remove.
	 */
	if (VFS_I(ip)->i_size == 0 &&
	    VFS_I(ip)->i_mapping->nrpages == 0 &&
	    ip->i_delayed_blks == 0)
		return false;

	/* If we haven't read in the extent list, then don't do it now. */
	if (!(ip->i_df.if_flags & XFS_IFEXTENTS))
		return false;

	/*
	 * Do not free real preallocated or append-only files unless the file
	 * has delalloc blocks and we are forced to remove them.
	 */
	if (ip->i_d.di_flags & (XFS_DIFLAG_PREALLOC | XFS_DIFLAG_APPEND))
		if (!force || ip->i_delayed_blks == 0)
			return false;

	return true;
}

/*
 * This is called by xfs_inactive to free any blocks beyond eof
 * when the link count isn't zero and by xfs_dm_punch_hole() when
 * punching a hole to EOF.
 */
int
xfs_free_eofblocks(
	xfs_mount_t	*mp,
	xfs_inode_t	*ip,
	bool		need_iolock)
{
	xfs_trans_t	*tp;
	int		error;
	xfs_fileoff_t	end_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	map_len;
	int		nimaps;
	xfs_bmbt_irec_t	imap;

	/*
	 * Figure out if there are any blocks beyond the end
	 * of the file.  If not, then there is nothing to do.
	 */
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)XFS_ISIZE(ip));
	last_fsb = XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes);
	if (last_fsb <= end_fsb)
		return 0;
	map_len = last_fsb - end_fsb;

	nimaps = 1;
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	error = xfs_bmapi_read(ip, end_fsb, map_len, &imap, &nimaps, 0);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (!error && (nimaps != 0) &&
	    (imap.br_startblock != HOLESTARTBLOCK ||
	     ip->i_delayed_blks)) {
		/*
		 * Attach the dquots to the inode up front.
		 */
		error = xfs_qm_dqattach(ip, 0);
		if (error)
			return error;

		/*
		 * There are blocks after the end of file.
		 * Free them up now by truncating the file to
		 * its current size.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_INACTIVE);

		if (need_iolock) {
			if (!xfs_ilock_nowait(ip, XFS_IOLOCK_EXCL)) {
				xfs_trans_cancel(tp, 0);
				return -EAGAIN;
			}
		}

		error = xfs_trans_reserve(tp, &M_RES(mp)->tr_itruncate, 0, 0);
		if (error) {
			ASSERT(XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			if (need_iolock)
				xfs_iunlock(ip, XFS_IOLOCK_EXCL);
			return error;
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, 0);

		/*
		 * Do not update the on-disk file size.  If we update the
		 * on-disk file size and then the system crashes before the
		 * contents of the file are flushed to disk then the files
		 * may be full of holes (ie NULL files bug).
		 */
		error = xfs_itruncate_extents(&tp, ip, XFS_DATA_FORK,
					      XFS_ISIZE(ip));
		if (error) {
			/*
			 * If we get an error at this point we simply don't
			 * bother truncating the file.
			 */
			xfs_trans_cancel(tp,
					 (XFS_TRANS_RELEASE_LOG_RES |
					  XFS_TRANS_ABORT));
		} else {
			error = xfs_trans_commit(tp,
						XFS_TRANS_RELEASE_LOG_RES);
			if (!error)
				xfs_inode_clear_eofblocks_tag(ip);
		}

		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (need_iolock)
			xfs_iunlock(ip, XFS_IOLOCK_EXCL);
	}
	return error;
}

int
xfs_alloc_file_space(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		len,
	int			alloc_type)
{
	xfs_mount_t		*mp = ip->i_mount;
	xfs_off_t		count;
	xfs_filblks_t		allocated_fsb;
	xfs_filblks_t		allocatesize_fsb;
	xfs_extlen_t		extsz, temp;
	xfs_fileoff_t		startoffset_fsb;
	xfs_fsblock_t		firstfsb;
	int			nimaps;
	int			quota_flag;
	int			rt;
	xfs_trans_t		*tp;
	xfs_bmbt_irec_t		imaps[1], *imapp;
	xfs_bmap_free_t		free_list;
	uint			qblocks, resblks, resrtextents;
	int			committed;
	int			error;

	trace_xfs_alloc_file_space(ip);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	if (len <= 0)
		return -EINVAL;

	rt = XFS_IS_REALTIME_INODE(ip);
	extsz = xfs_get_extsz_hint(ip);

	count = len;
	imapp = &imaps[0];
	nimaps = 1;
	startoffset_fsb	= XFS_B_TO_FSBT(mp, offset);
	allocatesize_fsb = XFS_B_TO_FSB(mp, count);

	/*
	 * Allocate file space until done or until there is an error
	 */
	while (allocatesize_fsb && !error) {
		xfs_fileoff_t	s, e;

		/*
		 * Determine space reservations for data/realtime.
		 */
		if (unlikely(extsz)) {
			s = startoffset_fsb;
			do_div(s, extsz);
			s *= extsz;
			e = startoffset_fsb + allocatesize_fsb;
			if ((temp = do_mod(startoffset_fsb, extsz)))
				e += temp;
			if ((temp = do_mod(e, extsz)))
				e += extsz - temp;
		} else {
			s = 0;
			e = allocatesize_fsb;
		}

		/*
		 * The transaction reservation is limited to a 32-bit block
		 * count, hence we need to limit the number of blocks we are
		 * trying to reserve to avoid an overflow. We can't allocate
		 * more than @nimaps extents, and an extent is limited on disk
		 * to MAXEXTLEN (21 bits), so use that to enforce the limit.
		 */
		resblks = min_t(xfs_fileoff_t, (e - s), (MAXEXTLEN * nimaps));
		if (unlikely(rt)) {
			resrtextents = qblocks = resblks;
			resrtextents /= mp->m_sb.sb_rextsize;
			resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
			quota_flag = XFS_QMOPT_RES_RTBLKS;
		} else {
			resrtextents = 0;
			resblks = qblocks = XFS_DIOSTRAT_SPACE_RES(mp, resblks);
			quota_flag = XFS_QMOPT_RES_REGBLKS;
		}

		/*
		 * Allocate and setup the transaction.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		error = xfs_trans_reserve(tp, &M_RES(mp)->tr_write,
					  resblks, resrtextents);
		/*
		 * Check for running out of space
		 */
		if (error) {
			/*
			 * Free the transaction structure.
			 */
			ASSERT(error == -ENOSPC || XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			break;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_trans_reserve_quota_nblks(tp, ip, qblocks,
						      0, quota_flag);
		if (error)
			goto error1;

		xfs_trans_ijoin(tp, ip, 0);

		xfs_bmap_init(&free_list, &firstfsb);
		error = xfs_bmapi_write(tp, ip, startoffset_fsb,
					allocatesize_fsb, alloc_type, &firstfsb,
					0, imapp, &nimaps, &free_list);
		if (error) {
			goto error0;
		}

		/*
		 * Complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error) {
			break;
		}

		allocated_fsb = imapp->br_blockcount;

		if (nimaps == 0) {
			error = -ENOSPC;
			break;
		}

		startoffset_fsb += allocated_fsb;
		allocatesize_fsb -= allocated_fsb;
	}

	return error;

error0:	/* Cancel bmap, unlock inode, unreserve quota blocks, cancel trans */
	xfs_bmap_cancel(&free_list);
	xfs_trans_unreserve_quota_nblks(tp, ip, (long)qblocks, 0, quota_flag);

error1:	/* Just cancel transaction */
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Zero file bytes between startoff and endoff inclusive.
 * The iolock is held exclusive and no blocks are buffered.
 *
 * This function is used by xfs_free_file_space() to zero
 * partial blocks when the range to free is not block aligned.
 * When unreserving space with boundaries that are not block
 * aligned we round up the start and round down the end
 * boundaries and then use this function to zero the parts of
 * the blocks that got dropped during the rounding.
 */
STATIC int
xfs_zero_remaining_bytes(
	xfs_inode_t		*ip,
	xfs_off_t		startoff,
	xfs_off_t		endoff)
{
	xfs_bmbt_irec_t		imap;
	xfs_fileoff_t		offset_fsb;
	xfs_off_t		lastoffset;
	xfs_off_t		offset;
	xfs_buf_t		*bp;
	xfs_mount_t		*mp = ip->i_mount;
	int			nimap;
	int			error = 0;

	/*
	 * Avoid doing I/O beyond eof - it's not necessary
	 * since nothing can read beyond eof.  The space will
	 * be zeroed when the file is extended anyway.
	 */
	if (startoff >= XFS_ISIZE(ip))
		return 0;

	if (endoff > XFS_ISIZE(ip))
		endoff = XFS_ISIZE(ip);

	bp = xfs_buf_get_uncached(XFS_IS_REALTIME_INODE(ip) ?
					mp->m_rtdev_targp : mp->m_ddev_targp,
				  BTOBB(mp->m_sb.sb_blocksize), 0);
	if (!bp)
		return -ENOMEM;

	xfs_buf_unlock(bp);

	for (offset = startoff; offset <= endoff; offset = lastoffset + 1) {
		uint lock_mode;

		offset_fsb = XFS_B_TO_FSBT(mp, offset);
		nimap = 1;

		lock_mode = xfs_ilock_data_map_shared(ip);
		error = xfs_bmapi_read(ip, offset_fsb, 1, &imap, &nimap, 0);
		xfs_iunlock(ip, lock_mode);

		if (error || nimap < 1)
			break;
		ASSERT(imap.br_blockcount >= 1);
		ASSERT(imap.br_startoff == offset_fsb);
		lastoffset = XFS_FSB_TO_B(mp, imap.br_startoff + 1) - 1;
		if (lastoffset > endoff)
			lastoffset = endoff;
		if (imap.br_startblock == HOLESTARTBLOCK)
			continue;
		ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
		if (imap.br_state == XFS_EXT_UNWRITTEN)
			continue;
		XFS_BUF_UNDONE(bp);
		XFS_BUF_UNWRITE(bp);
		XFS_BUF_READ(bp);
		XFS_BUF_SET_ADDR(bp, xfs_fsb_to_db(ip, imap.br_startblock));

		if (XFS_FORCED_SHUTDOWN(mp)) {
			error = -EIO;
			break;
		}
		xfs_buf_iorequest(bp);
		error = xfs_buf_iowait(bp);
		if (error) {
			xfs_buf_ioerror_alert(bp,
					"xfs_zero_remaining_bytes(read)");
			break;
		}
		memset(bp->b_addr +
			(offset - XFS_FSB_TO_B(mp, imap.br_startoff)),
		      0, lastoffset - offset + 1);
		XFS_BUF_UNDONE(bp);
		XFS_BUF_UNREAD(bp);
		XFS_BUF_WRITE(bp);

		if (XFS_FORCED_SHUTDOWN(mp)) {
			error = -EIO;
			break;
		}
		xfs_buf_iorequest(bp);
		error = xfs_buf_iowait(bp);
		if (error) {
			xfs_buf_ioerror_alert(bp,
					"xfs_zero_remaining_bytes(write)");
			break;
		}
	}
	xfs_buf_free(bp);
	return error;
}

int
xfs_free_file_space(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		len)
{
	int			committed;
	int			done;
	xfs_fileoff_t		endoffset_fsb;
	int			error;
	xfs_fsblock_t		firstfsb;
	xfs_bmap_free_t		free_list;
	xfs_bmbt_irec_t		imap;
	xfs_off_t		ioffset;
	xfs_extlen_t		mod=0;
	xfs_mount_t		*mp;
	int			nimap;
	uint			resblks;
	xfs_off_t		rounding;
	int			rt;
	xfs_fileoff_t		startoffset_fsb;
	xfs_trans_t		*tp;

	mp = ip->i_mount;

	trace_xfs_free_file_space(ip);

	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	error = 0;
	if (len <= 0)	/* if nothing being freed */
		return error;
	rt = XFS_IS_REALTIME_INODE(ip);
	startoffset_fsb	= XFS_B_TO_FSB(mp, offset);
	endoffset_fsb = XFS_B_TO_FSBT(mp, offset + len);

	/* wait for the completion of any pending DIOs */
	inode_dio_wait(VFS_I(ip));

	rounding = max_t(xfs_off_t, 1 << mp->m_sb.sb_blocklog, PAGE_CACHE_SIZE);
	ioffset = offset & ~(rounding - 1);
	error = filemap_write_and_wait_range(VFS_I(ip)->i_mapping,
					      ioffset, -1);
	if (error)
		goto out;
	truncate_pagecache_range(VFS_I(ip), ioffset, -1);

	/*
	 * Need to zero the stuff we're not freeing, on disk.
	 * If it's a realtime file & can't use unwritten extents then we
	 * actually need to zero the extent edges.  Otherwise xfs_bunmapi
	 * will take care of it for us.
	 */
	if (rt && !xfs_sb_version_hasextflgbit(&mp->m_sb)) {
		nimap = 1;
		error = xfs_bmapi_read(ip, startoffset_fsb, 1,
					&imap, &nimap, 0);
		if (error)
			goto out;
		ASSERT(nimap == 0 || nimap == 1);
		if (nimap && imap.br_startblock != HOLESTARTBLOCK) {
			xfs_daddr_t	block;

			ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
			block = imap.br_startblock;
			mod = do_div(block, mp->m_sb.sb_rextsize);
			if (mod)
				startoffset_fsb += mp->m_sb.sb_rextsize - mod;
		}
		nimap = 1;
		error = xfs_bmapi_read(ip, endoffset_fsb - 1, 1,
					&imap, &nimap, 0);
		if (error)
			goto out;
		ASSERT(nimap == 0 || nimap == 1);
		if (nimap && imap.br_startblock != HOLESTARTBLOCK) {
			ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
			mod++;
			if (mod && (mod != mp->m_sb.sb_rextsize))
				endoffset_fsb -= mod;
		}
	}
	if ((done = (endoffset_fsb <= startoffset_fsb)))
		/*
		 * One contiguous piece to clear
		 */
		error = xfs_zero_remaining_bytes(ip, offset, offset + len - 1);
	else {
		/*
		 * Some full blocks, possibly two pieces to clear
		 */
		if (offset < XFS_FSB_TO_B(mp, startoffset_fsb))
			error = xfs_zero_remaining_bytes(ip, offset,
				XFS_FSB_TO_B(mp, startoffset_fsb) - 1);
		if (!error &&
		    XFS_FSB_TO_B(mp, endoffset_fsb) < offset + len)
			error = xfs_zero_remaining_bytes(ip,
				XFS_FSB_TO_B(mp, endoffset_fsb),
				offset + len - 1);
	}

	/*
	 * free file space until done or until there is an error
	 */
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
	while (!error && !done) {

		/*
		 * allocate and setup the transaction. Allow this
		 * transaction to dip into the reserve blocks to ensure
		 * the freeing of the space succeeds at ENOSPC.
		 */
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		error = xfs_trans_reserve(tp, &M_RES(mp)->tr_write, resblks, 0);

		/*
		 * check for running out of space
		 */
		if (error) {
			/*
			 * Free the transaction structure.
			 */
			ASSERT(error == -ENOSPC || XFS_FORCED_SHUTDOWN(mp));
			xfs_trans_cancel(tp, 0);
			break;
		}
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_trans_reserve_quota(tp, mp,
				ip->i_udquot, ip->i_gdquot, ip->i_pdquot,
				resblks, 0, XFS_QMOPT_RES_REGBLKS);
		if (error)
			goto error1;

		xfs_trans_ijoin(tp, ip, 0);

		/*
		 * issue the bunmapi() call to free the blocks
		 */
		xfs_bmap_init(&free_list, &firstfsb);
		error = xfs_bunmapi(tp, ip, startoffset_fsb,
				  endoffset_fsb - startoffset_fsb,
				  0, 2, &firstfsb, &free_list, &done);
		if (error) {
			goto error0;
		}

		/*
		 * complete the transaction
		 */
		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (error) {
			goto error0;
		}

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

 out:
	return error;

 error0:
	xfs_bmap_cancel(&free_list);
 error1:
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	goto out;
}


int
xfs_zero_file_space(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		len)
{
	struct xfs_mount	*mp = ip->i_mount;
	uint			granularity;
	xfs_off_t		start_boundary;
	xfs_off_t		end_boundary;
	int			error;

	trace_xfs_zero_file_space(ip);

	granularity = max_t(uint, 1 << mp->m_sb.sb_blocklog, PAGE_CACHE_SIZE);

	/*
	 * Round the range of extents we are going to convert inwards.  If the
	 * offset is aligned, then it doesn't get changed so we zero from the
	 * start of the block offset points to.
	 */
	start_boundary = round_up(offset, granularity);
	end_boundary = round_down(offset + len, granularity);

	ASSERT(start_boundary >= offset);
	ASSERT(end_boundary <= offset + len);

	if (start_boundary < end_boundary - 1) {
		/*
		 * punch out delayed allocation blocks and the page cache over
		 * the conversion range
		 */
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_bmap_punch_delalloc_range(ip,
				XFS_B_TO_FSBT(mp, start_boundary),
				XFS_B_TO_FSB(mp, end_boundary - start_boundary));
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		truncate_pagecache_range(VFS_I(ip), start_boundary,
					 end_boundary - 1);

		/* convert the blocks */
		error = xfs_alloc_file_space(ip, start_boundary,
					end_boundary - start_boundary - 1,
					XFS_BMAPI_PREALLOC | XFS_BMAPI_CONVERT);
		if (error)
			goto out;

		/* We've handled the interior of the range, now for the edges */
		if (start_boundary != offset) {
			error = xfs_iozero(ip, offset, start_boundary - offset);
			if (error)
				goto out;
		}

		if (end_boundary != offset + len)
			error = xfs_iozero(ip, end_boundary,
					   offset + len - end_boundary);

	} else {
		/*
		 * It's either a sub-granularity range or the range spanned lies
		 * partially across two adjacent blocks.
		 */
		error = xfs_iozero(ip, offset, len);
	}

out:
	return error;

}

/*
 * xfs_collapse_file_space()
 *	This routine frees disk space and shift extent for the given file.
 *	The first thing we do is to free data blocks in the specified range
 *	by calling xfs_free_file_space(). It would also sync dirty data
 *	and invalidate page cache over the region on which collapse range
 *	is working. And Shift extent records to the left to cover a hole.
 * RETURNS:
 *	0 on success
 *	errno on error
 *
 */
int
xfs_collapse_file_space(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		len)
{
	int			done = 0;
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;
	xfs_extnum_t		current_ext = 0;
	struct xfs_bmap_free	free_list;
	xfs_fsblock_t		first_block;
	int			committed;
	xfs_fileoff_t		start_fsb;
	xfs_fileoff_t		shift_fsb;

	ASSERT(xfs_isilocked(ip, XFS_IOLOCK_EXCL));

	trace_xfs_collapse_file_space(ip);

	start_fsb = XFS_B_TO_FSB(mp, offset + len);
	shift_fsb = XFS_B_TO_FSB(mp, len);

	error = xfs_free_file_space(ip, offset, len);
	if (error)
		return error;

	while (!error && !done) {
		tp = xfs_trans_alloc(mp, XFS_TRANS_DIOSTRAT);
		/*
		 * We would need to reserve permanent block for transaction.
		 * This will come into picture when after shifting extent into
		 * hole we found that adjacent extents can be merged which
		 * may lead to freeing of a block during record update.
		 */
		error = xfs_trans_reserve(tp, &M_RES(mp)->tr_write,
				XFS_DIOSTRAT_SPACE_RES(mp, 0), 0);
		if (error) {
			xfs_trans_cancel(tp, 0);
			break;
		}

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_trans_reserve_quota(tp, mp, ip->i_udquot,
				ip->i_gdquot, ip->i_pdquot,
				XFS_DIOSTRAT_SPACE_RES(mp, 0), 0,
				XFS_QMOPT_RES_REGBLKS);
		if (error)
			goto out;

		xfs_trans_ijoin(tp, ip, 0);

		xfs_bmap_init(&free_list, &first_block);

		/*
		 * We are using the write transaction in which max 2 bmbt
		 * updates are allowed
		 */
		error = xfs_bmap_shift_extents(tp, ip, &done, start_fsb,
					       shift_fsb, &current_ext,
					       &first_block, &free_list,
					       XFS_BMAP_MAX_SHIFT_EXTENTS);
		if (error)
			goto out;

		error = xfs_bmap_finish(&tp, &free_list, &committed);
		if (error)
			goto out;

		error = xfs_trans_commit(tp, XFS_TRANS_RELEASE_LOG_RES);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}

	return error;

out:
	xfs_trans_cancel(tp, XFS_TRANS_RELEASE_LOG_RES | XFS_TRANS_ABORT);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * We need to check that the format of the data fork in the temporary inode is
 * valid for the target inode before doing the swap. This is not a problem with
 * attr1 because of the fixed fork offset, but attr2 has a dynamically sized
 * data fork depending on the space the attribute fork is taking so we can get
 * invalid formats on the target inode.
 *
 * E.g. target has space for 7 extents in extent format, temp inode only has
 * space for 6.  If we defragment down to 7 extents, then the tmp format is a
 * btree, but when swapped it needs to be in extent format. Hence we can't just
 * blindly swap data forks on attr2 filesystems.
 *
 * Note that we check the swap in both directions so that we don't end up with
 * a corrupt temporary inode, either.
 *
 * Note that fixing the way xfs_fsr sets up the attribute fork in the source
 * inode will prevent this situation from occurring, so all we do here is
 * reject and log the attempt. basically we are putting the responsibility on
 * userspace to get this right.
 */
static int
xfs_swap_extents_check_format(
	xfs_inode_t	*ip,	/* target inode */
	xfs_inode_t	*tip)	/* tmp inode */
{

	/* Should never get a local format */
	if (ip->i_d.di_format == XFS_DINODE_FMT_LOCAL ||
	    tip->i_d.di_format == XFS_DINODE_FMT_LOCAL)
		return -EINVAL;

	/*
	 * if the target inode has less extents that then temporary inode then
	 * why did userspace call us?
	 */
	if (ip->i_d.di_nextents < tip->i_d.di_nextents)
		return -EINVAL;

	/*
	 * if the target inode is in extent form and the temp inode is in btree
	 * form then we will end up with the target inode in the wrong format
	 * as we already know there are less extents in the temp inode.
	 */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    tip->i_d.di_format == XFS_DINODE_FMT_BTREE)
		return -EINVAL;

	/* Check temp in extent form to max in target */
	if (tip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    XFS_IFORK_NEXTENTS(tip, XFS_DATA_FORK) >
			XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK))
		return -EINVAL;

	/* Check target in extent form to max in temp */
	if (ip->i_d.di_format == XFS_DINODE_FMT_EXTENTS &&
	    XFS_IFORK_NEXTENTS(ip, XFS_DATA_FORK) >
			XFS_IFORK_MAXEXT(tip, XFS_DATA_FORK))
		return -EINVAL;

	/*
	 * If we are in a btree format, check that the temp root block will fit
	 * in the target and that it has enough extents to be in btree format
	 * in the target.
	 *
	 * Note that we have to be careful to allow btree->extent conversions
	 * (a common defrag case) which will occur when the temp inode is in
	 * extent format...
	 */
	if (tip->i_d.di_format == XFS_DINODE_FMT_BTREE) {
		if (XFS_IFORK_BOFF(ip) &&
		    XFS_BMAP_BMDR_SPACE(tip->i_df.if_broot) > XFS_IFORK_BOFF(ip))
			return -EINVAL;
		if (XFS_IFORK_NEXTENTS(tip, XFS_DATA_FORK) <=
		    XFS_IFORK_MAXEXT(ip, XFS_DATA_FORK))
			return -EINVAL;
	}

	/* Reciprocal target->temp btree format checks */
	if (ip->i_d.di_format == XFS_DINODE_FMT_BTREE) {
		if (XFS_IFORK_BOFF(tip) &&
		    XFS_BMAP_BMDR_SPACE(ip->i_df.if_broot) > XFS_IFORK_BOFF(tip))
			return -EINVAL;
		if (XFS_IFORK_NEXTENTS(ip, XFS_DATA_FORK) <=
		    XFS_IFORK_MAXEXT(tip, XFS_DATA_FORK))
			return -EINVAL;
	}

	return 0;
}

int
xfs_swap_extent_flush(
	struct xfs_inode	*ip)
{
	int	error;

	error = filemap_write_and_wait(VFS_I(ip)->i_mapping);
	if (error)
		return error;
	truncate_pagecache_range(VFS_I(ip), 0, -1);

	/* Verify O_DIRECT for ftmp */
	if (VFS_I(ip)->i_mapping->nrpages)
		return -EINVAL;

	/*
	 * Don't try to swap extents on mmap()d files because we can't lock
	 * out races against page faults safely.
	 */
	if (mapping_mapped(VFS_I(ip)->i_mapping))
		return -EBUSY;
	return 0;
}

int
xfs_swap_extents(
	xfs_inode_t	*ip,	/* target inode */
	xfs_inode_t	*tip,	/* tmp inode */
	xfs_swapext_t	*sxp)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_trans_t	*tp;
	xfs_bstat_t	*sbp = &sxp->sx_stat;
	xfs_ifork_t	*tempifp, *ifp, *tifp;
	int		src_log_flags, target_log_flags;
	int		error = 0;
	int		aforkblks = 0;
	int		taforkblks = 0;
	__uint64_t	tmp;
	int		lock_flags;

	tempifp = kmem_alloc(sizeof(xfs_ifork_t), KM_MAYFAIL);
	if (!tempifp) {
		error = -ENOMEM;
		goto out;
	}

	/*
	 * Lock up the inodes against other IO and truncate to begin with.
	 * Then we can ensure the inodes are flushed and have no page cache
	 * safely. Once we have done this we can take the ilocks and do the rest
	 * of the checks.
	 */
	lock_flags = XFS_IOLOCK_EXCL;
	xfs_lock_two_inodes(ip, tip, XFS_IOLOCK_EXCL);

	/* Verify that both files have the same format */
	if ((ip->i_d.di_mode & S_IFMT) != (tip->i_d.di_mode & S_IFMT)) {
		error = -EINVAL;
		goto out_unlock;
	}

	/* Verify both files are either real-time or non-realtime */
	if (XFS_IS_REALTIME_INODE(ip) != XFS_IS_REALTIME_INODE(tip)) {
		error = -EINVAL;
		goto out_unlock;
	}

	error = xfs_swap_extent_flush(ip);
	if (error)
		goto out_unlock;
	error = xfs_swap_extent_flush(tip);
	if (error)
		goto out_unlock;

	tp = xfs_trans_alloc(mp, XFS_TRANS_SWAPEXT);
	error = xfs_trans_reserve(tp, &M_RES(mp)->tr_ichange, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		goto out_unlock;
	}
	xfs_lock_two_inodes(ip, tip, XFS_ILOCK_EXCL);
	lock_flags |= XFS_ILOCK_EXCL;

	/* Verify all data are being swapped */
	if (sxp->sx_offset != 0 ||
	    sxp->sx_length != ip->i_d.di_size ||
	    sxp->sx_length != tip->i_d.di_size) {
		error = -EFAULT;
		goto out_trans_cancel;
	}

	trace_xfs_swap_extent_before(ip, 0);
	trace_xfs_swap_extent_before(tip, 1);

	/* check inode formats now that data is flushed */
	error = xfs_swap_extents_check_format(ip, tip);
	if (error) {
		xfs_notice(mp,
		    "%s: inode 0x%llx format is incompatible for exchanging.",
				__func__, ip->i_ino);
		goto out_trans_cancel;
	}

	/*
	 * Compare the current change & modify times with that
	 * passed in.  If they differ, we abort this swap.
	 * This is the mechanism used to ensure the calling
	 * process that the file was not changed out from
	 * under it.
	 */
	if ((sbp->bs_ctime.tv_sec != VFS_I(ip)->i_ctime.tv_sec) ||
	    (sbp->bs_ctime.tv_nsec != VFS_I(ip)->i_ctime.tv_nsec) ||
	    (sbp->bs_mtime.tv_sec != VFS_I(ip)->i_mtime.tv_sec) ||
	    (sbp->bs_mtime.tv_nsec != VFS_I(ip)->i_mtime.tv_nsec)) {
		error = -EBUSY;
		goto out_trans_cancel;
	}
	/*
	 * Count the number of extended attribute blocks
	 */
	if ( ((XFS_IFORK_Q(ip) != 0) && (ip->i_d.di_anextents > 0)) &&
	     (ip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)) {
		error = xfs_bmap_count_blocks(tp, ip, XFS_ATTR_FORK, &aforkblks);
		if (error)
			goto out_trans_cancel;
	}
	if ( ((XFS_IFORK_Q(tip) != 0) && (tip->i_d.di_anextents > 0)) &&
	     (tip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL)) {
		error = xfs_bmap_count_blocks(tp, tip, XFS_ATTR_FORK,
			&taforkblks);
		if (error)
			goto out_trans_cancel;
	}

	xfs_trans_ijoin(tp, ip, lock_flags);
	xfs_trans_ijoin(tp, tip, lock_flags);

	/*
	 * Before we've swapped the forks, lets set the owners of the forks
	 * appropriately. We have to do this as we are demand paging the btree
	 * buffers, and so the validation done on read will expect the owner
	 * field to be correctly set. Once we change the owners, we can swap the
	 * inode forks.
	 *
	 * Note the trickiness in setting the log flags - we set the owner log
	 * flag on the opposite inode (i.e. the inode we are setting the new
	 * owner to be) because once we swap the forks and log that, log
	 * recovery is going to see the fork as owned by the swapped inode,
	 * not the pre-swapped inodes.
	 */
	src_log_flags = XFS_ILOG_CORE;
	target_log_flags = XFS_ILOG_CORE;
	if (ip->i_d.di_version == 3 &&
	    ip->i_d.di_format == XFS_DINODE_FMT_BTREE) {
		target_log_flags |= XFS_ILOG_DOWNER;
		error = xfs_bmbt_change_owner(tp, ip, XFS_DATA_FORK,
					      tip->i_ino, NULL);
		if (error)
			goto out_trans_cancel;
	}

	if (tip->i_d.di_version == 3 &&
	    tip->i_d.di_format == XFS_DINODE_FMT_BTREE) {
		src_log_flags |= XFS_ILOG_DOWNER;
		error = xfs_bmbt_change_owner(tp, tip, XFS_DATA_FORK,
					      ip->i_ino, NULL);
		if (error)
			goto out_trans_cancel;
	}

	/*
	 * Swap the data forks of the inodes
	 */
	ifp = &ip->i_df;
	tifp = &tip->i_df;
	*tempifp = *ifp;	/* struct copy */
	*ifp = *tifp;		/* struct copy */
	*tifp = *tempifp;	/* struct copy */

	/*
	 * Fix the on-disk inode values
	 */
	tmp = (__uint64_t)ip->i_d.di_nblocks;
	ip->i_d.di_nblocks = tip->i_d.di_nblocks - taforkblks + aforkblks;
	tip->i_d.di_nblocks = tmp + taforkblks - aforkblks;

	tmp = (__uint64_t) ip->i_d.di_nextents;
	ip->i_d.di_nextents = tip->i_d.di_nextents;
	tip->i_d.di_nextents = tmp;

	tmp = (__uint64_t) ip->i_d.di_format;
	ip->i_d.di_format = tip->i_d.di_format;
	tip->i_d.di_format = tmp;

	/*
	 * The extents in the source inode could still contain speculative
	 * preallocation beyond EOF (e.g. the file is open but not modified
	 * while defrag is in progress). In that case, we need to copy over the
	 * number of delalloc blocks the data fork in the source inode is
	 * tracking beyond EOF so that when the fork is truncated away when the
	 * temporary inode is unlinked we don't underrun the i_delayed_blks
	 * counter on that inode.
	 */
	ASSERT(tip->i_delayed_blks == 0);
	tip->i_delayed_blks = ip->i_delayed_blks;
	ip->i_delayed_blks = 0;

	switch (ip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		/* If the extents fit in the inode, fix the
		 * pointer.  Otherwise it's already NULL or
		 * pointing to the extent.
		 */
		if (ip->i_d.di_nextents <= XFS_INLINE_EXTS) {
			ifp->if_u1.if_extents =
				ifp->if_u2.if_inline_ext;
		}
		src_log_flags |= XFS_ILOG_DEXT;
		break;
	case XFS_DINODE_FMT_BTREE:
		ASSERT(ip->i_d.di_version < 3 ||
		       (src_log_flags & XFS_ILOG_DOWNER));
		src_log_flags |= XFS_ILOG_DBROOT;
		break;
	}

	switch (tip->i_d.di_format) {
	case XFS_DINODE_FMT_EXTENTS:
		/* If the extents fit in the inode, fix the
		 * pointer.  Otherwise it's already NULL or
		 * pointing to the extent.
		 */
		if (tip->i_d.di_nextents <= XFS_INLINE_EXTS) {
			tifp->if_u1.if_extents =
				tifp->if_u2.if_inline_ext;
		}
		target_log_flags |= XFS_ILOG_DEXT;
		break;
	case XFS_DINODE_FMT_BTREE:
		target_log_flags |= XFS_ILOG_DBROOT;
		ASSERT(tip->i_d.di_version < 3 ||
		       (target_log_flags & XFS_ILOG_DOWNER));
		break;
	}

	xfs_trans_log_inode(tp, ip,  src_log_flags);
	xfs_trans_log_inode(tp, tip, target_log_flags);

	/*
	 * If this is a synchronous mount, make sure that the
	 * transaction goes to disk before returning to the user.
	 */
	if (mp->m_flags & XFS_MOUNT_WSYNC)
		xfs_trans_set_sync(tp);

	error = xfs_trans_commit(tp, 0);

	trace_xfs_swap_extent_after(ip, 0);
	trace_xfs_swap_extent_after(tip, 1);
out:
	kmem_free(tempifp);
	return error;

out_unlock:
	xfs_iunlock(ip, lock_flags);
	xfs_iunlock(tip, lock_flags);
	goto out;

out_trans_cancel:
	xfs_trans_cancel(tp, 0);
	goto out_unlock;
}
