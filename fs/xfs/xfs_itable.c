/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#include "xfs_inum.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_itable.h"
#include "xfs_error.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_dinode.h"

STATIC int
xfs_internal_inum(
	xfs_mount_t	*mp,
	xfs_ino_t	ino)
{
	return (ino == mp->m_sb.sb_rbmino || ino == mp->m_sb.sb_rsumino ||
		(xfs_sb_version_hasquota(&mp->m_sb) &&
		 xfs_is_quota_inode(&mp->m_sb, ino)));
}

/*
 * Return stat information for one inode.
 * Return 0 if ok, else errno.
 */
int
xfs_bulkstat_one_int(
	struct xfs_mount	*mp,		/* mount point for filesystem */
	xfs_ino_t		ino,		/* inode to get data for */
	void __user		*buffer,	/* buffer to place output in */
	int			ubsize,		/* size of buffer */
	bulkstat_one_fmt_pf	formatter,	/* formatter, copy to user */
	int			*ubused,	/* bytes used by me */
	int			*stat)		/* BULKSTAT_RV_... */
{
	struct xfs_icdinode	*dic;		/* dinode core info pointer */
	struct xfs_inode	*ip;		/* incore inode pointer */
	struct xfs_bstat	*buf;		/* return buffer */
	int			error = 0;	/* error value */

	*stat = BULKSTAT_RV_NOTHING;

	if (!buffer || xfs_internal_inum(mp, ino))
		return -EINVAL;

	buf = kmem_alloc(sizeof(*buf), KM_SLEEP | KM_MAYFAIL);
	if (!buf)
		return -ENOMEM;

	error = xfs_iget(mp, NULL, ino,
			 (XFS_IGET_DONTCACHE | XFS_IGET_UNTRUSTED),
			 XFS_ILOCK_SHARED, &ip);
	if (error)
		goto out_free;

	ASSERT(ip != NULL);
	ASSERT(ip->i_imap.im_blkno != 0);

	dic = &ip->i_d;

	/* xfs_iget returns the following without needing
	 * further change.
	 */
	buf->bs_nlink = dic->di_nlink;
	buf->bs_projid_lo = dic->di_projid_lo;
	buf->bs_projid_hi = dic->di_projid_hi;
	buf->bs_ino = ino;
	buf->bs_mode = dic->di_mode;
	buf->bs_uid = dic->di_uid;
	buf->bs_gid = dic->di_gid;
	buf->bs_size = dic->di_size;
	buf->bs_atime.tv_sec = dic->di_atime.t_sec;
	buf->bs_atime.tv_nsec = dic->di_atime.t_nsec;
	buf->bs_mtime.tv_sec = dic->di_mtime.t_sec;
	buf->bs_mtime.tv_nsec = dic->di_mtime.t_nsec;
	buf->bs_ctime.tv_sec = dic->di_ctime.t_sec;
	buf->bs_ctime.tv_nsec = dic->di_ctime.t_nsec;
	buf->bs_xflags = xfs_ip2xflags(ip);
	buf->bs_extsize = dic->di_extsize << mp->m_sb.sb_blocklog;
	buf->bs_extents = dic->di_nextents;
	buf->bs_gen = dic->di_gen;
	memset(buf->bs_pad, 0, sizeof(buf->bs_pad));
	buf->bs_dmevmask = dic->di_dmevmask;
	buf->bs_dmstate = dic->di_dmstate;
	buf->bs_aextents = dic->di_anextents;
	buf->bs_forkoff = XFS_IFORK_BOFF(ip);

	switch (dic->di_format) {
	case XFS_DINODE_FMT_DEV:
		buf->bs_rdev = ip->i_df.if_u2.if_rdev;
		buf->bs_blksize = BLKDEV_IOSIZE;
		buf->bs_blocks = 0;
		break;
	case XFS_DINODE_FMT_LOCAL:
	case XFS_DINODE_FMT_UUID:
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
	IRELE(ip);

	error = formatter(buffer, ubsize, ubused, buf);
	if (!error)
		*stat = BULKSTAT_RV_DIDONE;

 out_free:
	kmem_free(buf);
	return error;
}

/* Return 0 on success or positive error */
STATIC int
xfs_bulkstat_one_fmt(
	void			__user *ubuffer,
	int			ubsize,
	int			*ubused,
	const xfs_bstat_t	*buffer)
{
	if (ubsize < sizeof(*buffer))
		return -ENOMEM;
	if (copy_to_user(ubuffer, buffer, sizeof(*buffer)))
		return -EFAULT;
	if (ubused)
		*ubused = sizeof(*buffer);
	return 0;
}

int
xfs_bulkstat_one(
	xfs_mount_t	*mp,		/* mount point for filesystem */
	xfs_ino_t	ino,		/* inode number to get data for */
	void		__user *buffer,	/* buffer to place output in */
	int		ubsize,		/* size of buffer */
	int		*ubused,	/* bytes used by me */
	int		*stat)		/* BULKSTAT_RV_... */
{
	return xfs_bulkstat_one_int(mp, ino, buffer, ubsize,
				    xfs_bulkstat_one_fmt, ubused, stat);
}

/*
 * Loop over all clusters in a chunk for a given incore inode allocation btree
 * record.  Do a readahead if there are any allocated inodes in that cluster.
 */
STATIC void
xfs_bulkstat_ichunk_ra(
	struct xfs_mount		*mp,
	xfs_agnumber_t			agno,
	struct xfs_inobt_rec_incore	*irec)
{
	xfs_agblock_t			agbno;
	struct blk_plug			plug;
	int				blks_per_cluster;
	int				inodes_per_cluster;
	int				i;	/* inode chunk index */

	agbno = XFS_AGINO_TO_AGBNO(mp, irec->ir_startino);
	blks_per_cluster = xfs_icluster_size_fsb(mp);
	inodes_per_cluster = blks_per_cluster << mp->m_sb.sb_inopblog;

	blk_start_plug(&plug);
	for (i = 0; i < XFS_INODES_PER_CHUNK;
	     i += inodes_per_cluster, agbno += blks_per_cluster) {
		if (xfs_inobt_maskn(i, inodes_per_cluster) & ~irec->ir_free) {
			xfs_btree_reada_bufs(mp, agno, agbno, blks_per_cluster,
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
STATIC int
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
	XFS_WANT_CORRUPTED_RETURN(stat == 1);

	/* Check if the record contains the inode in request */
	if (irec->ir_startino + XFS_INODES_PER_CHUNK <= agino)
		return -EINVAL;

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
		*icount = XFS_INODES_PER_CHUNK - irec->ir_freecount;
	}

	return 0;
}

#define XFS_BULKSTAT_UBLEFT(ubleft)	((ubleft) >= statstruct_size)

/*
 * Return stat information in bulk (by-inode) for the filesystem.
 */
int					/* error status */
xfs_bulkstat(
	xfs_mount_t		*mp,	/* mount point for filesystem */
	xfs_ino_t		*lastinop, /* last inode returned */
	int			*ubcountp, /* size of buffer/count returned */
	bulkstat_one_pf		formatter, /* func that'd fill a single buf */
	size_t			statstruct_size, /* sizeof struct filling */
	char			__user *ubuffer, /* buffer with inode stats */
	int			*done)	/* 1 if there are more stats to get */
{
	xfs_buf_t		*agbp;	/* agi header buffer */
	xfs_agi_t		*agi;	/* agi header data */
	xfs_agino_t		agino;	/* inode # in allocation group */
	xfs_agnumber_t		agno;	/* allocation group number */
	int			chunkidx; /* current index into inode chunk */
	int			clustidx; /* current index into inode cluster */
	xfs_btree_cur_t		*cur;	/* btree cursor for ialloc btree */
	int			end_of_ag; /* set if we've seen the ag end */
	int			error;	/* error code */
	int                     fmterror;/* bulkstat formatter result */
	int			i;	/* loop index */
	int			icount;	/* count of inodes good in irbuf */
	size_t			irbsize; /* size of irec buffer in bytes */
	xfs_ino_t		ino;	/* inode number (filesystem) */
	xfs_inobt_rec_incore_t	*irbp;	/* current irec buffer pointer */
	xfs_inobt_rec_incore_t	*irbuf;	/* start of irec buffer */
	xfs_inobt_rec_incore_t	*irbufend; /* end of good irec buffer entries */
	xfs_ino_t		lastino; /* last inode number returned */
	int			nirbuf;	/* size of irbuf */
	int			rval;	/* return value error code */
	int			tmp;	/* result value from btree calls */
	int			ubcount; /* size of user's buffer */
	int			ubleft;	/* bytes left in user's buffer */
	char			__user *ubufp;	/* pointer into user's buffer */
	int			ubelem;	/* spaces used in user's buffer */
	int			ubused;	/* bytes used by formatter */

	/*
	 * Get the last inode value, see if there's nothing to do.
	 */
	ino = (xfs_ino_t)*lastinop;
	lastino = ino;
	agno = XFS_INO_TO_AGNO(mp, ino);
	agino = XFS_INO_TO_AGINO(mp, ino);
	if (agno >= mp->m_sb.sb_agcount ||
	    ino != XFS_AGINO_TO_INO(mp, agno, agino)) {
		*done = 1;
		*ubcountp = 0;
		return 0;
	}

	ubcount = *ubcountp; /* statstruct's */
	ubleft = ubcount * statstruct_size; /* bytes */
	*ubcountp = ubelem = 0;
	*done = 0;
	fmterror = 0;
	ubufp = ubuffer;
	irbuf = kmem_zalloc_greedy(&irbsize, PAGE_SIZE, PAGE_SIZE * 4);
	if (!irbuf)
		return -ENOMEM;

	nirbuf = irbsize / sizeof(*irbuf);

	/*
	 * Loop over the allocation groups, starting from the last
	 * inode returned; 0 means start of the allocation group.
	 */
	rval = 0;
	while (XFS_BULKSTAT_UBLEFT(ubleft) && agno < mp->m_sb.sb_agcount) {
		cond_resched();
		error = xfs_ialloc_read_agi(mp, NULL, agno, &agbp);
		if (error)
			break;
		agi = XFS_BUF_TO_AGI(agbp);
		/*
		 * Allocate and initialize a btree cursor for ialloc btree.
		 */
		cur = xfs_inobt_init_cursor(mp, NULL, agbp, agno,
					    XFS_BTNUM_INO);
		irbp = irbuf;
		irbufend = irbuf + nirbuf;
		end_of_ag = 0;
		icount = 0;
		if (agino > 0) {
			/*
			 * In the middle of an allocation group, we need to get
			 * the remainder of the chunk we're in.
			 */
			struct xfs_inobt_rec_incore	r;

			error = xfs_bulkstat_grab_ichunk(cur, agino, &icount, &r);
			if (error)
				break;
			if (icount) {
				irbp->ir_startino = r.ir_startino;
				irbp->ir_freecount = r.ir_freecount;
				irbp->ir_free = r.ir_free;
				irbp++;
				agino = r.ir_startino + XFS_INODES_PER_CHUNK;
			}
			/* Increment to the next record */
			error = xfs_btree_increment(cur, 0, &tmp);
		} else {
			/* Start of ag.  Lookup the first inode chunk */
			error = xfs_inobt_lookup(cur, 0, XFS_LOOKUP_GE, &tmp);
		}
		if (error)
			break;

		/*
		 * Loop through inode btree records in this ag,
		 * until we run out of inodes or space in the buffer.
		 */
		while (irbp < irbufend && icount < ubcount) {
			struct xfs_inobt_rec_incore	r;

			error = xfs_inobt_get_rec(cur, &r, &i);
			if (error || i == 0) {
				end_of_ag = 1;
				break;
			}

			/*
			 * If this chunk has any allocated inodes, save it.
			 * Also start read-ahead now for this chunk.
			 */
			if (r.ir_freecount < XFS_INODES_PER_CHUNK) {
				xfs_bulkstat_ichunk_ra(mp, agno, &r);
				irbp->ir_startino = r.ir_startino;
				irbp->ir_freecount = r.ir_freecount;
				irbp->ir_free = r.ir_free;
				irbp++;
				icount += XFS_INODES_PER_CHUNK - r.ir_freecount;
			}
			/*
			 * Set agino to after this chunk and bump the cursor.
			 */
			agino = r.ir_startino + XFS_INODES_PER_CHUNK;
			error = xfs_btree_increment(cur, 0, &tmp);
			cond_resched();
		}
		/*
		 * Drop the btree buffers and the agi buffer.
		 * We can't hold any of the locks these represent
		 * when calling iget.
		 */
		xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
		xfs_buf_relse(agbp);
		/*
		 * Now format all the good inodes into the user's buffer.
		 */
		irbufend = irbp;
		for (irbp = irbuf;
		     irbp < irbufend && XFS_BULKSTAT_UBLEFT(ubleft); irbp++) {
			/*
			 * Now process this chunk of inodes.
			 */
			for (agino = irbp->ir_startino, chunkidx = clustidx = 0;
			     XFS_BULKSTAT_UBLEFT(ubleft) &&
				irbp->ir_freecount < XFS_INODES_PER_CHUNK;
			     chunkidx++, clustidx++, agino++) {
				ASSERT(chunkidx < XFS_INODES_PER_CHUNK);

				ino = XFS_AGINO_TO_INO(mp, agno, agino);
				/*
				 * Skip if this inode is free.
				 */
				if (XFS_INOBT_MASK(chunkidx) & irbp->ir_free) {
					lastino = ino;
					continue;
				}
				/*
				 * Count used inodes as free so we can tell
				 * when the chunk is used up.
				 */
				irbp->ir_freecount++;

				/*
				 * Get the inode and fill in a single buffer.
				 */
				ubused = statstruct_size;
				error = formatter(mp, ino, ubufp, ubleft,
						  &ubused, &fmterror);
				if (fmterror == BULKSTAT_RV_NOTHING) {
					if (error && error != -ENOENT &&
						error != -EINVAL) {
						ubleft = 0;
						rval = error;
						break;
					}
					lastino = ino;
					continue;
				}
				if (fmterror == BULKSTAT_RV_GIVEUP) {
					ubleft = 0;
					ASSERT(error);
					rval = error;
					break;
				}
				if (ubufp)
					ubufp += ubused;
				ubleft -= ubused;
				ubelem++;
				lastino = ino;
			}

			cond_resched();
		}
		/*
		 * Set up for the next loop iteration.
		 */
		if (XFS_BULKSTAT_UBLEFT(ubleft)) {
			if (end_of_ag) {
				agno++;
				agino = 0;
			} else
				agino = XFS_INO_TO_AGINO(mp, lastino);
		} else
			break;
	}
	/*
	 * Done, we're either out of filesystem or space to put the data.
	 */
	kmem_free(irbuf);
	*ubcountp = ubelem;
	/*
	 * Found some inodes, return them now and return the error next time.
	 */
	if (ubelem)
		rval = 0;
	if (agno >= mp->m_sb.sb_agcount) {
		/*
		 * If we ran out of filesystem, mark lastino as off
		 * the end of the filesystem, so the next call
		 * will return immediately.
		 */
		*lastinop = (xfs_ino_t)XFS_AGINO_TO_INO(mp, agno, 0);
		*done = 1;
	} else
		*lastinop = (xfs_ino_t)lastino;

	return rval;
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

	bcount = MIN(left, (int)(PAGE_SIZE / sizeof(*buffer)));
	buffer = kmem_alloc(bcount * sizeof(*buffer), KM_SLEEP);
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
		buffer[bufidx].xi_alloccount =
			XFS_INODES_PER_CHUNK - r.ir_freecount;
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
	} while (++agno < mp->m_sb.sb_agcount);

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
		xfs_btree_del_cursor(cur, (error ? XFS_BTREE_ERROR :
					   XFS_BTREE_NOERROR));
	if (agbp)
		xfs_buf_relse(agbp);

	return error;
}
