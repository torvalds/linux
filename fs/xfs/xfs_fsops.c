/*
 * Copyright (c) 2000-2002 Silicon Graphics, Inc.  All Rights Reserved.
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

#include "xfs.h"
#include "xfs_macros.h"
#include "xfs_types.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_dir.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_ag.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_error.h"
#include "xfs_alloc.h"
#include "xfs_ialloc.h"
#include "xfs_fsops.h"
#include "xfs_itable.h"
#include "xfs_rw.h"
#include "xfs_refcache.h"
#include "xfs_trans_space.h"
#include "xfs_rtalloc.h"
#include "xfs_dir2.h"
#include "xfs_attr_sf.h"
#include "xfs_dir_sf.h"
#include "xfs_dir2_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"

/*
 * File system operations
 */

int
xfs_fs_geometry(
	xfs_mount_t		*mp,
	xfs_fsop_geom_t		*geo,
	int			new_version)
{
	geo->blocksize = mp->m_sb.sb_blocksize;
	geo->rtextsize = mp->m_sb.sb_rextsize;
	geo->agblocks = mp->m_sb.sb_agblocks;
	geo->agcount = mp->m_sb.sb_agcount;
	geo->logblocks = mp->m_sb.sb_logblocks;
	geo->sectsize = mp->m_sb.sb_sectsize;
	geo->inodesize = mp->m_sb.sb_inodesize;
	geo->imaxpct = mp->m_sb.sb_imax_pct;
	geo->datablocks = mp->m_sb.sb_dblocks;
	geo->rtblocks = mp->m_sb.sb_rblocks;
	geo->rtextents = mp->m_sb.sb_rextents;
	geo->logstart = mp->m_sb.sb_logstart;
	ASSERT(sizeof(geo->uuid)==sizeof(mp->m_sb.sb_uuid));
	memcpy(geo->uuid, &mp->m_sb.sb_uuid, sizeof(mp->m_sb.sb_uuid));
	if (new_version >= 2) {
		geo->sunit = mp->m_sb.sb_unit;
		geo->swidth = mp->m_sb.sb_width;
	}
	if (new_version >= 3) {
		geo->version = XFS_FSOP_GEOM_VERSION;
		geo->flags =
			(XFS_SB_VERSION_HASATTR(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_ATTR : 0) |
			(XFS_SB_VERSION_HASNLINK(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_NLINK : 0) |
			(XFS_SB_VERSION_HASQUOTA(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_QUOTA : 0) |
			(XFS_SB_VERSION_HASALIGN(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_IALIGN : 0) |
			(XFS_SB_VERSION_HASDALIGN(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_DALIGN : 0) |
			(XFS_SB_VERSION_HASSHARED(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_SHARED : 0) |
			(XFS_SB_VERSION_HASEXTFLGBIT(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_EXTFLG : 0) |
			(XFS_SB_VERSION_HASDIRV2(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_DIRV2 : 0) |
			(XFS_SB_VERSION_HASSECTOR(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_SECTOR : 0);
		geo->logsectsize = XFS_SB_VERSION_HASSECTOR(&mp->m_sb) ?
				mp->m_sb.sb_logsectsize : BBSIZE;
		geo->rtsectsize = mp->m_sb.sb_blocksize;
		geo->dirblocksize = mp->m_dirblksize;
	}
	if (new_version >= 4) {
		geo->flags |=
			(XFS_SB_VERSION_HASLOGV2(&mp->m_sb) ?
				XFS_FSOP_GEOM_FLAGS_LOGV2 : 0);
		geo->logsunit = mp->m_sb.sb_logsunit;
	}
	return 0;
}

static int
xfs_growfs_data_private(
	xfs_mount_t		*mp,		/* mount point for filesystem */
	xfs_growfs_data_t	*in)		/* growfs data input struct */
{
	xfs_agf_t		*agf;
	xfs_agi_t		*agi;
	xfs_agnumber_t		agno;
	xfs_extlen_t		agsize;
	xfs_extlen_t		tmpsize;
	xfs_alloc_rec_t		*arec;
	xfs_btree_sblock_t	*block;
	xfs_buf_t		*bp;
	int			bucket;
	int			dpct;
	int			error;
	xfs_agnumber_t		nagcount;
	xfs_agnumber_t		nagimax = 0;
	xfs_rfsblock_t		nb, nb_mod;
	xfs_rfsblock_t		new;
	xfs_rfsblock_t		nfree;
	xfs_agnumber_t		oagcount;
	int			pct;
	xfs_sb_t		*sbp;
	xfs_trans_t		*tp;

	nb = in->newblocks;
	pct = in->imaxpct;
	if (nb < mp->m_sb.sb_dblocks || pct < 0 || pct > 100)
		return XFS_ERROR(EINVAL);
	dpct = pct - mp->m_sb.sb_imax_pct;
	error = xfs_read_buf(mp, mp->m_ddev_targp,
			XFS_FSB_TO_BB(mp, nb) - XFS_FSS_TO_BB(mp, 1),
			XFS_FSS_TO_BB(mp, 1), 0, &bp);
	if (error)
		return error;
	ASSERT(bp);
	xfs_buf_relse(bp);

	new = nb;	/* use new as a temporary here */
	nb_mod = do_div(new, mp->m_sb.sb_agblocks);
	nagcount = new + (nb_mod != 0);
	if (nb_mod && nb_mod < XFS_MIN_AG_BLOCKS) {
		nagcount--;
		nb = nagcount * mp->m_sb.sb_agblocks;
		if (nb < mp->m_sb.sb_dblocks)
			return XFS_ERROR(EINVAL);
	}
	new = nb - mp->m_sb.sb_dblocks;
	oagcount = mp->m_sb.sb_agcount;
	if (nagcount > oagcount) {
		down_write(&mp->m_peraglock);
		mp->m_perag = kmem_realloc(mp->m_perag,
			sizeof(xfs_perag_t) * nagcount,
			sizeof(xfs_perag_t) * oagcount,
			KM_SLEEP);
		memset(&mp->m_perag[oagcount], 0,
			(nagcount - oagcount) * sizeof(xfs_perag_t));
		mp->m_flags |= XFS_MOUNT_32BITINODES;
		nagimax = xfs_initialize_perag(mp, nagcount);
		up_write(&mp->m_peraglock);
	}
	tp = xfs_trans_alloc(mp, XFS_TRANS_GROWFS);
	if ((error = xfs_trans_reserve(tp, XFS_GROWFS_SPACE_RES(mp),
			XFS_GROWDATA_LOG_RES(mp), 0, 0, 0))) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	nfree = 0;
	for (agno = nagcount - 1; agno >= oagcount; agno--, new -= agsize) {
		/*
		 * AG freelist header block
		 */
		bp = xfs_buf_get(mp->m_ddev_targp,
				  XFS_AG_DADDR(mp, agno, XFS_AGF_DADDR(mp)),
				  XFS_FSS_TO_BB(mp, 1), 0);
		agf = XFS_BUF_TO_AGF(bp);
		memset(agf, 0, mp->m_sb.sb_sectsize);
		INT_SET(agf->agf_magicnum, ARCH_CONVERT, XFS_AGF_MAGIC);
		INT_SET(agf->agf_versionnum, ARCH_CONVERT, XFS_AGF_VERSION);
		INT_SET(agf->agf_seqno, ARCH_CONVERT, agno);
		if (agno == nagcount - 1)
			agsize =
				nb -
				(agno * (xfs_rfsblock_t)mp->m_sb.sb_agblocks);
		else
			agsize = mp->m_sb.sb_agblocks;
		INT_SET(agf->agf_length, ARCH_CONVERT, agsize);
		INT_SET(agf->agf_roots[XFS_BTNUM_BNOi], ARCH_CONVERT,
			XFS_BNO_BLOCK(mp));
		INT_SET(agf->agf_roots[XFS_BTNUM_CNTi], ARCH_CONVERT,
			XFS_CNT_BLOCK(mp));
		INT_SET(agf->agf_levels[XFS_BTNUM_BNOi], ARCH_CONVERT, 1);
		INT_SET(agf->agf_levels[XFS_BTNUM_CNTi], ARCH_CONVERT, 1);
		agf->agf_flfirst = 0;
		INT_SET(agf->agf_fllast, ARCH_CONVERT, XFS_AGFL_SIZE(mp) - 1);
		agf->agf_flcount = 0;
		tmpsize = agsize - XFS_PREALLOC_BLOCKS(mp);
		INT_SET(agf->agf_freeblks, ARCH_CONVERT, tmpsize);
		INT_SET(agf->agf_longest, ARCH_CONVERT, tmpsize);
		error = xfs_bwrite(mp, bp);
		if (error) {
			goto error0;
		}
		/*
		 * AG inode header block
		 */
		bp = xfs_buf_get(mp->m_ddev_targp,
				  XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR(mp)),
				  XFS_FSS_TO_BB(mp, 1), 0);
		agi = XFS_BUF_TO_AGI(bp);
		memset(agi, 0, mp->m_sb.sb_sectsize);
		INT_SET(agi->agi_magicnum, ARCH_CONVERT, XFS_AGI_MAGIC);
		INT_SET(agi->agi_versionnum, ARCH_CONVERT, XFS_AGI_VERSION);
		INT_SET(agi->agi_seqno, ARCH_CONVERT, agno);
		INT_SET(agi->agi_length, ARCH_CONVERT, agsize);
		agi->agi_count = 0;
		INT_SET(agi->agi_root, ARCH_CONVERT, XFS_IBT_BLOCK(mp));
		INT_SET(agi->agi_level, ARCH_CONVERT, 1);
		agi->agi_freecount = 0;
		INT_SET(agi->agi_newino, ARCH_CONVERT, NULLAGINO);
		INT_SET(agi->agi_dirino, ARCH_CONVERT, NULLAGINO);
		for (bucket = 0; bucket < XFS_AGI_UNLINKED_BUCKETS; bucket++)
			INT_SET(agi->agi_unlinked[bucket], ARCH_CONVERT,
				NULLAGINO);
		error = xfs_bwrite(mp, bp);
		if (error) {
			goto error0;
		}
		/*
		 * BNO btree root block
		 */
		bp = xfs_buf_get(mp->m_ddev_targp,
			XFS_AGB_TO_DADDR(mp, agno, XFS_BNO_BLOCK(mp)),
			BTOBB(mp->m_sb.sb_blocksize), 0);
		block = XFS_BUF_TO_SBLOCK(bp);
		memset(block, 0, mp->m_sb.sb_blocksize);
		INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTB_MAGIC);
		block->bb_level = 0;
		INT_SET(block->bb_numrecs, ARCH_CONVERT, 1);
		INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
		INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);
		arec = XFS_BTREE_REC_ADDR(mp->m_sb.sb_blocksize, xfs_alloc,
			block, 1, mp->m_alloc_mxr[0]);
		INT_SET(arec->ar_startblock, ARCH_CONVERT,
			XFS_PREALLOC_BLOCKS(mp));
		INT_SET(arec->ar_blockcount, ARCH_CONVERT,
			agsize - INT_GET(arec->ar_startblock, ARCH_CONVERT));
		error = xfs_bwrite(mp, bp);
		if (error) {
			goto error0;
		}
		/*
		 * CNT btree root block
		 */
		bp = xfs_buf_get(mp->m_ddev_targp,
			XFS_AGB_TO_DADDR(mp, agno, XFS_CNT_BLOCK(mp)),
			BTOBB(mp->m_sb.sb_blocksize), 0);
		block = XFS_BUF_TO_SBLOCK(bp);
		memset(block, 0, mp->m_sb.sb_blocksize);
		INT_SET(block->bb_magic, ARCH_CONVERT, XFS_ABTC_MAGIC);
		block->bb_level = 0;
		INT_SET(block->bb_numrecs, ARCH_CONVERT, 1);
		INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
		INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);
		arec = XFS_BTREE_REC_ADDR(mp->m_sb.sb_blocksize, xfs_alloc,
			block, 1, mp->m_alloc_mxr[0]);
		INT_SET(arec->ar_startblock, ARCH_CONVERT,
			XFS_PREALLOC_BLOCKS(mp));
		INT_SET(arec->ar_blockcount, ARCH_CONVERT,
			agsize - INT_GET(arec->ar_startblock, ARCH_CONVERT));
		nfree += INT_GET(arec->ar_blockcount, ARCH_CONVERT);
		error = xfs_bwrite(mp, bp);
		if (error) {
			goto error0;
		}
		/*
		 * INO btree root block
		 */
		bp = xfs_buf_get(mp->m_ddev_targp,
			XFS_AGB_TO_DADDR(mp, agno, XFS_IBT_BLOCK(mp)),
			BTOBB(mp->m_sb.sb_blocksize), 0);
		block = XFS_BUF_TO_SBLOCK(bp);
		memset(block, 0, mp->m_sb.sb_blocksize);
		INT_SET(block->bb_magic, ARCH_CONVERT, XFS_IBT_MAGIC);
		block->bb_level = 0;
		block->bb_numrecs = 0;
		INT_SET(block->bb_leftsib, ARCH_CONVERT, NULLAGBLOCK);
		INT_SET(block->bb_rightsib, ARCH_CONVERT, NULLAGBLOCK);
		error = xfs_bwrite(mp, bp);
		if (error) {
			goto error0;
		}
	}
	xfs_trans_agblocks_delta(tp, nfree);
	/*
	 * There are new blocks in the old last a.g.
	 */
	if (new) {
		/*
		 * Change the agi length.
		 */
		error = xfs_ialloc_read_agi(mp, tp, agno, &bp);
		if (error) {
			goto error0;
		}
		ASSERT(bp);
		agi = XFS_BUF_TO_AGI(bp);
		INT_MOD(agi->agi_length, ARCH_CONVERT, new);
		ASSERT(nagcount == oagcount ||
		       INT_GET(agi->agi_length, ARCH_CONVERT) ==
				mp->m_sb.sb_agblocks);
		xfs_ialloc_log_agi(tp, bp, XFS_AGI_LENGTH);
		/*
		 * Change agf length.
		 */
		error = xfs_alloc_read_agf(mp, tp, agno, 0, &bp);
		if (error) {
			goto error0;
		}
		ASSERT(bp);
		agf = XFS_BUF_TO_AGF(bp);
		INT_MOD(agf->agf_length, ARCH_CONVERT, new);
		ASSERT(INT_GET(agf->agf_length, ARCH_CONVERT) ==
				INT_GET(agi->agi_length, ARCH_CONVERT));
		/*
		 * Free the new space.
		 */
		error = xfs_free_extent(tp, XFS_AGB_TO_FSB(mp, agno,
			INT_GET(agf->agf_length, ARCH_CONVERT) - new), new);
		if (error) {
			goto error0;
		}
	}
	if (nagcount > oagcount)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_AGCOUNT, nagcount - oagcount);
	if (nb > mp->m_sb.sb_dblocks)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_DBLOCKS,
				 nb - mp->m_sb.sb_dblocks);
	if (nfree)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_FDBLOCKS, nfree);
	if (dpct)
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_IMAXPCT, dpct);
	error = xfs_trans_commit(tp, 0, NULL);
	if (error) {
		return error;
	}
	/* New allocation groups fully initialized, so update mount struct */
	if (nagimax)
		mp->m_maxagi = nagimax;
	if (mp->m_sb.sb_imax_pct) {
		__uint64_t icount = mp->m_sb.sb_dblocks * mp->m_sb.sb_imax_pct;
		do_div(icount, 100);
		mp->m_maxicount = icount << mp->m_sb.sb_inopblog;
	} else
		mp->m_maxicount = 0;
	for (agno = 1; agno < nagcount; agno++) {
		error = xfs_read_buf(mp, mp->m_ddev_targp,
				  XFS_AGB_TO_DADDR(mp, agno, XFS_SB_BLOCK(mp)),
				  XFS_FSS_TO_BB(mp, 1), 0, &bp);
		if (error) {
			xfs_fs_cmn_err(CE_WARN, mp,
			"error %d reading secondary superblock for ag %d",
				error, agno);
			break;
		}
		sbp = XFS_BUF_TO_SBP(bp);
		xfs_xlatesb(sbp, &mp->m_sb, -1, XFS_SB_ALL_BITS);
		/*
		 * If we get an error writing out the alternate superblocks,
		 * just issue a warning and continue.  The real work is
		 * already done and committed.
		 */
		if (!(error = xfs_bwrite(mp, bp))) {
			continue;
		} else {
			xfs_fs_cmn_err(CE_WARN, mp,
		"write error %d updating secondary superblock for ag %d",
				error, agno);
			break; /* no point in continuing */
		}
	}
	return 0;

 error0:
	xfs_trans_cancel(tp, XFS_TRANS_ABORT);
	return error;
}

static int
xfs_growfs_log_private(
	xfs_mount_t		*mp,	/* mount point for filesystem */
	xfs_growfs_log_t	*in)	/* growfs log input struct */
{
	xfs_extlen_t		nb;

	nb = in->newblocks;
	if (nb < XFS_MIN_LOG_BLOCKS || nb < XFS_B_TO_FSB(mp, XFS_MIN_LOG_BYTES))
		return XFS_ERROR(EINVAL);
	if (nb == mp->m_sb.sb_logblocks &&
	    in->isint == (mp->m_sb.sb_logstart != 0))
		return XFS_ERROR(EINVAL);
	/*
	 * Moving the log is hard, need new interfaces to sync
	 * the log first, hold off all activity while moving it.
	 * Can have shorter or longer log in the same space,
	 * or transform internal to external log or vice versa.
	 */
	return XFS_ERROR(ENOSYS);
}

/*
 * protected versions of growfs function acquire and release locks on the mount
 * point - exported through ioctls: XFS_IOC_FSGROWFSDATA, XFS_IOC_FSGROWFSLOG,
 * XFS_IOC_FSGROWFSRT
 */


int
xfs_growfs_data(
	xfs_mount_t		*mp,
	xfs_growfs_data_t	*in)
{
	int error;
	if (!cpsema(&mp->m_growlock))
		return XFS_ERROR(EWOULDBLOCK);
	error = xfs_growfs_data_private(mp, in);
	vsema(&mp->m_growlock);
	return error;
}

int
xfs_growfs_log(
	xfs_mount_t		*mp,
	xfs_growfs_log_t	*in)
{
	int error;
	if (!cpsema(&mp->m_growlock))
		return XFS_ERROR(EWOULDBLOCK);
	error = xfs_growfs_log_private(mp, in);
	vsema(&mp->m_growlock);
	return error;
}

/*
 * exported through ioctl XFS_IOC_FSCOUNTS
 */

int
xfs_fs_counts(
	xfs_mount_t		*mp,
	xfs_fsop_counts_t	*cnt)
{
	unsigned long	s;

	s = XFS_SB_LOCK(mp);
	cnt->freedata = mp->m_sb.sb_fdblocks;
	cnt->freertx = mp->m_sb.sb_frextents;
	cnt->freeino = mp->m_sb.sb_ifree;
	cnt->allocino = mp->m_sb.sb_icount;
	XFS_SB_UNLOCK(mp, s);
	return 0;
}

/*
 * exported through ioctl XFS_IOC_SET_RESBLKS & XFS_IOC_GET_RESBLKS
 *
 * xfs_reserve_blocks is called to set m_resblks
 * in the in-core mount table. The number of unused reserved blocks
 * is kept in m_resbls_avail.
 *
 * Reserve the requested number of blocks if available. Otherwise return
 * as many as possible to satisfy the request. The actual number
 * reserved are returned in outval
 *
 * A null inval pointer indicates that only the current reserved blocks
 * available  should  be returned no settings are changed.
 */

int
xfs_reserve_blocks(
	xfs_mount_t             *mp,
	__uint64_t              *inval,
	xfs_fsop_resblks_t      *outval)
{
	__int64_t		lcounter, delta;
	__uint64_t		request;
	unsigned long		s;

	/* If inval is null, report current values and return */

	if (inval == (__uint64_t *)NULL) {
		outval->resblks = mp->m_resblks;
		outval->resblks_avail = mp->m_resblks_avail;
		return(0);
	}

	request = *inval;
	s = XFS_SB_LOCK(mp);

	/*
	 * If our previous reservation was larger than the current value,
	 * then move any unused blocks back to the free pool.
	 */

	if (mp->m_resblks > request) {
		lcounter = mp->m_resblks_avail - request;
		if (lcounter  > 0) {		/* release unused blocks */
			mp->m_sb.sb_fdblocks += lcounter;
			mp->m_resblks_avail -= lcounter;
		}
		mp->m_resblks = request;
	} else {
		delta = request - mp->m_resblks;
		lcounter = mp->m_sb.sb_fdblocks - delta;
		if (lcounter < 0) {
			/* We can't satisfy the request, just get what we can */
			mp->m_resblks += mp->m_sb.sb_fdblocks;
			mp->m_resblks_avail += mp->m_sb.sb_fdblocks;
			mp->m_sb.sb_fdblocks = 0;
		} else {
			mp->m_sb.sb_fdblocks = lcounter;
			mp->m_resblks = request;
			mp->m_resblks_avail += delta;
		}
	}

	outval->resblks = mp->m_resblks;
	outval->resblks_avail = mp->m_resblks_avail;
	XFS_SB_UNLOCK(mp, s);
	return(0);
}

int
xfs_fs_goingdown(
	xfs_mount_t	*mp,
	__uint32_t	inflags)
{
	switch (inflags) {
	case XFS_FSOP_GOING_FLAGS_DEFAULT: {
		struct vfs *vfsp = XFS_MTOVFS(mp);
		struct super_block *sb = freeze_bdev(vfsp->vfs_super->s_bdev);

		if (sb) {
			xfs_force_shutdown(mp, XFS_FORCE_UMOUNT);
			thaw_bdev(sb->s_bdev, sb);
		}
	
		break;
	}
	case XFS_FSOP_GOING_FLAGS_LOGFLUSH:
		xfs_force_shutdown(mp, XFS_FORCE_UMOUNT);
		break;
	case XFS_FSOP_GOING_FLAGS_NOLOGFLUSH:
		xfs_force_shutdown(mp, XFS_FORCE_UMOUNT|XFS_LOG_IO_ERROR);
		break;
	default:
		return XFS_ERROR(EINVAL);
	}

	return 0;
}
