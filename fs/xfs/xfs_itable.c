// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_iwalk.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_icache.h"
#include "xfs_health.h"

/*
 * Bulk Stat
 * =========
 *
 * Use the inode walking functions to fill out struct xfs_bstat for every
 * allocated inode, then pass the stat information to some externally provided
 * iteration function.
 */

struct xfs_bstat_chunk {
	bulkstat_one_fmt_pf	formatter;
	struct xfs_ibulk	*breq;
	struct xfs_bstat	*buf;
};

/*
 * Fill out the bulkstat info for a single inode and report it somewhere.
 *
 * bc->breq->lastino is effectively the inode cursor as we walk through the
 * filesystem.  Therefore, we update it any time we need to move the cursor
 * forward, regardless of whether or not we're sending any bstat information
 * back to userspace.  If the inode is internal metadata or, has been freed
 * out from under us, we just simply keep going.
 *
 * However, if any other type of error happens we want to stop right where we
 * are so that userspace will call back with exact number of the bad inode and
 * we can send back an error code.
 *
 * Note that if the formatter tells us there's no space left in the buffer we
 * move the cursor forward and abort the walk.
 */
STATIC int
xfs_bulkstat_one_int(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_ino_t		ino,
	struct xfs_bstat_chunk	*bc)
{
	struct xfs_icdinode	*dic;		/* dinode core info pointer */
	struct xfs_inode	*ip;		/* incore inode pointer */
	struct inode		*inode;
	struct xfs_bstat	*buf = bc->buf;
	int			error = -EINVAL;

	if (xfs_internal_inum(mp, ino))
		goto out_advance;

	error = xfs_iget(mp, tp, ino,
			 (XFS_IGET_DONTCACHE | XFS_IGET_UNTRUSTED),
			 XFS_ILOCK_SHARED, &ip);
	if (error == -ENOENT || error == -EINVAL)
		goto out_advance;
	if (error)
		goto out;

	ASSERT(ip != NULL);
	ASSERT(ip->i_imap.im_blkno != 0);
	inode = VFS_I(ip);

	dic = &ip->i_d;

	/* xfs_iget returns the following without needing
	 * further change.
	 */
	buf->bs_projid_lo = dic->di_projid_lo;
	buf->bs_projid_hi = dic->di_projid_hi;
	buf->bs_ino = ino;
	buf->bs_uid = dic->di_uid;
	buf->bs_gid = dic->di_gid;
	buf->bs_size = dic->di_size;

	buf->bs_nlink = inode->i_nlink;
	buf->bs_atime.tv_sec = inode->i_atime.tv_sec;
	buf->bs_atime.tv_nsec = inode->i_atime.tv_nsec;
	buf->bs_mtime.tv_sec = inode->i_mtime.tv_sec;
	buf->bs_mtime.tv_nsec = inode->i_mtime.tv_nsec;
	buf->bs_ctime.tv_sec = inode->i_ctime.tv_sec;
	buf->bs_ctime.tv_nsec = inode->i_ctime.tv_nsec;
	buf->bs_gen = inode->i_generation;
	buf->bs_mode = inode->i_mode;

	buf->bs_xflags = xfs_ip2xflags(ip);
	buf->bs_extsize = dic->di_extsize << mp->m_sb.sb_blocklog;
	buf->bs_extents = dic->di_nextents;
	memset(buf->bs_pad, 0, sizeof(buf->bs_pad));
	xfs_bulkstat_health(ip, buf);
	buf->bs_dmevmask = dic->di_dmevmask;
	buf->bs_dmstate = dic->di_dmstate;
	buf->bs_aextents = dic->di_anextents;
	buf->bs_forkoff = XFS_IFORK_BOFF(ip);

	if (dic->di_version == 3) {
		if (dic->di_flags2 & XFS_DIFLAG2_COWEXTSIZE)
			buf->bs_cowextsize = dic->di_cowextsize <<
					mp->m_sb.sb_blocklog;
	}

	switch (dic->di_format) {
	case XFS_DINODE_FMT_DEV:
		buf->bs_rdev = sysv_encode_dev(inode->i_rdev);
		buf->bs_blksize = BLKDEV_IOSIZE;
		buf->bs_blocks = 0;
		break;
	case XFS_DINODE_FMT_LOCAL:
		buf->bs_rdev = 0;
		buf->bs_blksize = mp->m_sb.sb_blocksize;
		buf->bs_blocks = 0;
		break;
	case XFS_DINODE_FMT_EXTENTS:
	case XFS_DINODE_FMT_BTREE:
		buf->bs_rdev = 0;
		buf->bs_blksize = mp->m_sb.sb_blocksize;
		buf->bs_blocks = dic->di_nblocks + ip->i_delayed_blks;
		break;
	}
	xfs_iunlock(ip, XFS_ILOCK_SHARED);
	xfs_irele(ip);

	error = bc->formatter(bc->breq, buf);
	if (error == XFS_IBULK_ABORT)
		goto out_advance;
	if (error)
		goto out;

out_advance:
	/*
	 * Advance the cursor to the inode that comes after the one we just
	 * looked at.  We want the caller to move along if the bulkstat
	 * information was copied successfully; if we tried to grab the inode
	 * but it's no longer allocated; or if it's internal metadata.
	 */
	bc->breq->startino = ino + 1;
out:
	return error;
}

/* Bulkstat a single inode. */
int
xfs_bulkstat_one(
	struct xfs_ibulk	*breq,
	bulkstat_one_fmt_pf	formatter)
{
	struct xfs_bstat_chunk	bc = {
		.formatter	= formatter,
		.breq		= breq,
	};
	int			error;

	ASSERT(breq->icount == 1);

	bc.buf = kmem_zalloc(sizeof(struct xfs_bstat), KM_SLEEP | KM_MAYFAIL);
	if (!bc.buf)
		return -ENOMEM;

	error = xfs_bulkstat_one_int(breq->mp, NULL, breq->startino, &bc);

	kmem_free(bc.buf);

	/*
	 * If we reported one inode to userspace then we abort because we hit
	 * the end of the buffer.  Don't leak that back to userspace.
	 */
	if (error == XFS_IWALK_ABORT)
		error = 0;

	return error;
}

/*
 * Loop over all clusters in a chunk for a given incore inode allocation btree
 * record.  Do a readahead if there are any allocated inodes in that cluster.
 */
void
xfs_bulkstat_ichunk_ra(
	struct xfs_mount		*mp,
	xfs_agnumber_t			agno,
	struct xfs_inobt_rec_incore	*irec)
{
	struct xfs_ino_geometry		*igeo = M_IGEO(mp);
	xfs_agblock_t			agbno;
	struct blk_plug			plug;
	int				i;	/* inode chunk index */

	agbno = XFS_AGINO_TO_AGBNO(mp, irec->ir_startino);

	blk_start_plug(&plug);
	for (i = 0;
	     i < XFS_INODES_PER_CHUNK;
	     i += igeo->inodes_per_cluster,
			agbno += igeo->blocks_per_cluster) {
		if (xfs_inobt_maskn(i, igeo->inodes_per_cluster) &
		    ~irec->ir_free) {
			xfs_btree_reada_bufs(mp, agno, agbno,
					igeo->blocks_per_cluster,
					&xfs_inode_buf_ops);
		}
	}
	blk_finish_plug(&plug);
}

/*
 * Lookup the inode chunk that the given inode lives in and then get the record
 * if we found the chunk.  If the inode was not the last in the chunk and there
 * are some left allocated, update the data for the pointed-to record as well as
 * return the count of grabbed inodes.
 */
int
xfs_bulkstat_grab_ichunk(
	struct xfs_btree_cur		*cur,	/* btree cursor */
	xfs_agino_t			agino,	/* starting inode of chunk */
	int				*icount,/* return # of inodes grabbed */
	struct xfs_inobt_rec_incore	*irec)	/* btree record */
{
	int				idx;	/* index into inode chunk */
	int				stat;
	int				error = 0;

	/* Lookup the inode chunk that this inode lives in */
	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &stat);
	if (error)
		return error;
	if (!stat) {
		*icount = 0;
		return error;
	}

	/* Get the record, should always work */
	error = xfs_inobt_get_rec(cur, irec, &stat);
	if (error)
		return error;
	XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, stat == 1);

	/* Check if the record contains the inode in request */
	if (irec->ir_startino + XFS_INODES_PER_CHUNK <= agino) {
		*icount = 0;
		return 0;
	}

	idx = agino - irec->ir_startino + 1;
	if (idx < XFS_INODES_PER_CHUNK &&
	    (xfs_inobt_maskn(idx, XFS_INODES_PER_CHUNK - idx) & ~irec->ir_free)) {
		int	i;

		/* We got a right chunk with some left inodes allocated at it.
		 * Grab the chunk record.  Mark all the uninteresting inodes
		 * free -- because they're before our start point.
		 */
		for (i = 0; i < idx; i++) {
			if (XFS_INOBT_MASK(i) & ~irec->ir_free)
				irec->ir_freecount++;
		}

		irec->ir_free |= xfs_inobt_maskn(0, idx);
		*icount = irec->ir_count - irec->ir_freecount;
	}

	return 0;
}

#define XFS_BULKSTAT_UBLEFT(ubleft)	((ubleft) >= statstruct_size)

static int
xfs_bulkstat_iwalk(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_ino_t		ino,
	void			*data)
{
	int			error;

	error = xfs_bulkstat_one_int(mp, tp, ino, data);
	/* bulkstat just skips over missing inodes */
	if (error == -ENOENT || error == -EINVAL)
		return 0;
	return error;
}

/*
 * Check the incoming lastino parameter.
 *
 * We allow any inode value that could map to physical space inside the
 * filesystem because if there are no inodes there, bulkstat moves on to the
 * next chunk.  In other words, the magic agino value of zero takes us to the
 * first chunk in the AG, and an agino value past the end of the AG takes us to
 * the first chunk in the next AG.
 *
 * Therefore we can end early if the requested inode is beyond the end of the
 * filesystem or doesn't map properly.
 */
static inline bool
xfs_bulkstat_already_done(
	struct xfs_mount	*mp,
	xfs_ino_t		startino)
{
	xfs_agnumber_t		agno = XFS_INO_TO_AGNO(mp, startino);
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, startino);

	return agno >= mp->m_sb.sb_agcount ||
	       startino != XFS_AGINO_TO_INO(mp, agno, agino);
}

/* Return stat information in bulk (by-inode) for the filesystem. */
int
xfs_bulkstat(
	struct xfs_ibulk	*breq,
	bulkstat_one_fmt_pf	formatter)
{
	struct xfs_bstat_chunk	bc = {
		.formatter	= formatter,
		.breq		= breq,
	};
	int			error;

	if (xfs_bulkstat_already_done(breq->mp, breq->startino))
		return 0;

	bc.buf = kmem_zalloc(sizeof(struct xfs_bstat), KM_SLEEP | KM_MAYFAIL);
	if (!bc.buf)
		return -ENOMEM;

	error = xfs_iwalk(breq->mp, NULL, breq->startino, xfs_bulkstat_iwalk,
			breq->icount, &bc);

	kmem_free(bc.buf);

	/*
	 * We found some inodes, so clear the error status and return them.
	 * The lastino pointer will point directly at the inode that triggered
	 * any error that occurred, so on the next call the error will be
	 * triggered again and propagated to userspace as there will be no
	 * formatted inodes in the buffer.
	 */
	if (breq->ocount > 0)
		error = 0;

	return error;
}

int
xfs_inumbers_fmt(
	void			__user *ubuffer, /* buffer to write to */
	const struct xfs_inogrp	*buffer,	/* buffer to read from */
	long			count,		/* # of elements to read */
	long			*written)	/* # of bytes written */
{
	if (copy_to_user(ubuffer, buffer, count * sizeof(*buffer)))
		return -EFAULT;
	*written = count * sizeof(*buffer);
	return 0;
}

/*
 * Return inode number table for the filesystem.
 */
int					/* error status */
xfs_inumbers(
	struct xfs_mount	*mp,/* mount point for filesystem */
	xfs_ino_t		*lastino,/* last inode returned */
	int			*count,/* size of buffer/count returned */
	void			__user *ubuffer,/* buffer with inode descriptions */
	inumbers_fmt_pf		formatter)
{
	xfs_agnumber_t		agno = XFS_INO_TO_AGNO(mp, *lastino);
	xfs_agino_t		agino = XFS_INO_TO_AGINO(mp, *lastino);
	struct xfs_btree_cur	*cur = NULL;
	struct xfs_buf		*agbp = NULL;
	struct xfs_inogrp	*buffer;
	int			bcount;
	int			left = *count;
	int			bufidx = 0;
	int			error = 0;

	*count = 0;
	if (agno >= mp->m_sb.sb_agcount ||
	    *lastino != XFS_AGINO_TO_INO(mp, agno, agino))
		return error;

	bcount = min(left, (int)(PAGE_SIZE / sizeof(*buffer)));
	buffer = kmem_zalloc(bcount * sizeof(*buffer), KM_SLEEP);
	do {
		struct xfs_inobt_rec_incore	r;
		int				stat;

		if (!agbp) {
			error = xfs_ialloc_read_agi(mp, NULL, agno, &agbp);
			if (error)
				break;

			cur = xfs_inobt_init_cursor(mp, NULL, agbp, agno,
						    XFS_BTNUM_INO);
			error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_GE,
						 &stat);
			if (error)
				break;
			if (!stat)
				goto next_ag;
		}

		error = xfs_inobt_get_rec(cur, &r, &stat);
		if (error)
			break;
		if (!stat)
			goto next_ag;

		agino = r.ir_startino + XFS_INODES_PER_CHUNK - 1;
		buffer[bufidx].xi_startino =
			XFS_AGINO_TO_INO(mp, agno, r.ir_startino);
		buffer[bufidx].xi_alloccount = r.ir_count - r.ir_freecount;
		buffer[bufidx].xi_allocmask = ~r.ir_free;
		if (++bufidx == bcount) {
			long	written;

			error = formatter(ubuffer, buffer, bufidx, &written);
			if (error)
				break;
			ubuffer += written;
			*count += bufidx;
			bufidx = 0;
		}
		if (!--left)
			break;

		error = xfs_btree_increment(cur, 0, &stat);
		if (error)
			break;
		if (stat)
			continue;

next_ag:
		xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
		cur = NULL;
		xfs_buf_relse(agbp);
		agbp = NULL;
		agino = 0;
		agno++;
	} while (agno < mp->m_sb.sb_agcount);

	if (!error) {
		if (bufidx) {
			long	written;

			error = formatter(ubuffer, buffer, bufidx, &written);
			if (!error)
				*count += bufidx;
		}
		*lastino = XFS_AGINO_TO_INO(mp, agno, agino);
	}

	kmem_free(buffer);
	if (cur)
		xfs_btree_del_cursor(cur, error);
	if (agbp)
		xfs_buf_relse(agbp);

	return error;
}
