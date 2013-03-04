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
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_bmap.h"


/*
 * Allocation group level functions.
 */
static inline int
xfs_ialloc_cluster_alignment(
	xfs_alloc_arg_t	*args)
{
	if (xfs_sb_version_hasalign(&args->mp->m_sb) &&
	    args->mp->m_sb.sb_inoalignmt >=
	     XFS_B_TO_FSBT(args->mp, XFS_INODE_CLUSTER_SIZE(args->mp)))
		return args->mp->m_sb.sb_inoalignmt;
	return 1;
}

/*
 * Lookup a record by ino in the btree given by cur.
 */
int					/* error */
xfs_inobt_lookup(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agino_t		ino,	/* starting inode of chunk */
	xfs_lookup_t		dir,	/* <=, >=, == */
	int			*stat)	/* success/failure */
{
	cur->bc_rec.i.ir_startino = ino;
	cur->bc_rec.i.ir_freecount = 0;
	cur->bc_rec.i.ir_free = 0;
	return xfs_btree_lookup(cur, dir, stat);
}

/*
 * Update the record referred to by cur to the value given.
 * This either works (return 0) or gets an EFSCORRUPTED error.
 */
STATIC int				/* error */
xfs_inobt_update(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_inobt_rec_incore_t	*irec)	/* btree record */
{
	union xfs_btree_rec	rec;

	rec.inobt.ir_startino = cpu_to_be32(irec->ir_startino);
	rec.inobt.ir_freecount = cpu_to_be32(irec->ir_freecount);
	rec.inobt.ir_free = cpu_to_be64(irec->ir_free);
	return xfs_btree_update(cur, &rec);
}

/*
 * Get the data from the pointed-to record.
 */
int					/* error */
xfs_inobt_get_rec(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_inobt_rec_incore_t	*irec,	/* btree record */
	int			*stat)	/* output: success/failure */
{
	union xfs_btree_rec	*rec;
	int			error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (!error && *stat == 1) {
		irec->ir_startino = be32_to_cpu(rec->inobt.ir_startino);
		irec->ir_freecount = be32_to_cpu(rec->inobt.ir_freecount);
		irec->ir_free = be64_to_cpu(rec->inobt.ir_free);
	}
	return error;
}

/*
 * Verify that the number of free inodes in the AGI is correct.
 */
#ifdef DEBUG
STATIC int
xfs_check_agi_freecount(
	struct xfs_btree_cur	*cur,
	struct xfs_agi		*agi)
{
	if (cur->bc_nlevels == 1) {
		xfs_inobt_rec_incore_t rec;
		int		freecount = 0;
		int		error;
		int		i;

		error = xfs_inobt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
		if (error)
			return error;

		do {
			error = xfs_inobt_get_rec(cur, &rec, &i);
			if (error)
				return error;

			if (i) {
				freecount += rec.ir_freecount;
				error = xfs_btree_increment(cur, 0, &i);
				if (error)
					return error;
			}
		} while (i == 1);

		if (!XFS_FORCED_SHUTDOWN(cur->bc_mp))
			ASSERT(freecount == be32_to_cpu(agi->agi_freecount));
	}
	return 0;
}
#else
#define xfs_check_agi_freecount(cur, agi)	0
#endif

/*
 * Initialise a new set of inodes.
 */
STATIC int
xfs_ialloc_inode_init(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		agbno,
	xfs_agblock_t		length,
	unsigned int		gen)
{
	struct xfs_buf		*fbuf;
	struct xfs_dinode	*free;
	int			blks_per_cluster, nbufs, ninodes;
	int			version;
	int			i, j;
	xfs_daddr_t		d;

	/*
	 * Loop over the new block(s), filling in the inodes.
	 * For small block sizes, manipulate the inodes in buffers
	 * which are multiples of the blocks size.
	 */
	if (mp->m_sb.sb_blocksize >= XFS_INODE_CLUSTER_SIZE(mp)) {
		blks_per_cluster = 1;
		nbufs = length;
		ninodes = mp->m_sb.sb_inopblock;
	} else {
		blks_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) /
				   mp->m_sb.sb_blocksize;
		nbufs = length / blks_per_cluster;
		ninodes = blks_per_cluster * mp->m_sb.sb_inopblock;
	}

	/*
	 * Figure out what version number to use in the inodes we create.
	 * If the superblock version has caught up to the one that supports
	 * the new inode format, then use the new inode version.  Otherwise
	 * use the old version so that old kernels will continue to be
	 * able to use the file system.
	 */
	if (xfs_sb_version_hasnlink(&mp->m_sb))
		version = 2;
	else
		version = 1;

	for (j = 0; j < nbufs; j++) {
		/*
		 * Get the block.
		 */
		d = XFS_AGB_TO_DADDR(mp, agno, agbno + (j * blks_per_cluster));
		fbuf = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
					 mp->m_bsize * blks_per_cluster,
					 XBF_UNMAPPED);
		if (!fbuf)
			return ENOMEM;
		/*
		 * Initialize all inodes in this buffer and then log them.
		 *
		 * XXX: It would be much better if we had just one transaction
		 *	to log a whole cluster of inodes instead of all the
		 *	individual transactions causing a lot of log traffic.
		 */
		fbuf->b_ops = &xfs_inode_buf_ops;
		xfs_buf_zero(fbuf, 0, ninodes << mp->m_sb.sb_inodelog);
		for (i = 0; i < ninodes; i++) {
			int	ioffset = i << mp->m_sb.sb_inodelog;
			uint	isize = sizeof(struct xfs_dinode);

			free = xfs_make_iptr(mp, fbuf, i);
			free->di_magic = cpu_to_be16(XFS_DINODE_MAGIC);
			free->di_version = version;
			free->di_gen = cpu_to_be32(gen);
			free->di_next_unlinked = cpu_to_be32(NULLAGINO);
			xfs_trans_log_buf(tp, fbuf, ioffset, ioffset + isize - 1);
		}
		xfs_trans_inode_alloc_buf(tp, fbuf);
	}
	return 0;
}

/*
 * Allocate new inodes in the allocation group specified by agbp.
 * Return 0 for success, else error code.
 */
STATIC int				/* error code or 0 */
xfs_ialloc_ag_alloc(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_buf_t	*agbp,		/* alloc group buffer */
	int		*alloc)
{
	xfs_agi_t	*agi;		/* allocation group header */
	xfs_alloc_arg_t	args;		/* allocation argument structure */
	xfs_btree_cur_t	*cur;		/* inode btree cursor */
	xfs_agnumber_t	agno;
	int		error;
	int		i;
	xfs_agino_t	newino;		/* new first inode's number */
	xfs_agino_t	newlen;		/* new number of inodes */
	xfs_agino_t	thisino;	/* current inode number, for loop */
	int		isaligned = 0;	/* inode allocation at stripe unit */
					/* boundary */
	struct xfs_perag *pag;

	memset(&args, 0, sizeof(args));
	args.tp = tp;
	args.mp = tp->t_mountp;

	/*
	 * Locking will ensure that we don't have two callers in here
	 * at one time.
	 */
	newlen = XFS_IALLOC_INODES(args.mp);
	if (args.mp->m_maxicount &&
	    args.mp->m_sb.sb_icount + newlen > args.mp->m_maxicount)
		return XFS_ERROR(ENOSPC);
	args.minlen = args.maxlen = XFS_IALLOC_BLOCKS(args.mp);
	/*
	 * First try to allocate inodes contiguous with the last-allocated
	 * chunk of inodes.  If the filesystem is striped, this will fill
	 * an entire stripe unit with inodes.
 	 */
	agi = XFS_BUF_TO_AGI(agbp);
	newino = be32_to_cpu(agi->agi_newino);
	agno = be32_to_cpu(agi->agi_seqno);
	args.agbno = XFS_AGINO_TO_AGBNO(args.mp, newino) +
			XFS_IALLOC_BLOCKS(args.mp);
	if (likely(newino != NULLAGINO &&
		  (args.agbno < be32_to_cpu(agi->agi_length)))) {
		args.fsbno = XFS_AGB_TO_FSB(args.mp, agno, args.agbno);
		args.type = XFS_ALLOCTYPE_THIS_BNO;
		args.prod = 1;

		/*
		 * We need to take into account alignment here to ensure that
		 * we don't modify the free list if we fail to have an exact
		 * block. If we don't have an exact match, and every oher
		 * attempt allocation attempt fails, we'll end up cancelling
		 * a dirty transaction and shutting down.
		 *
		 * For an exact allocation, alignment must be 1,
		 * however we need to take cluster alignment into account when
		 * fixing up the freelist. Use the minalignslop field to
		 * indicate that extra blocks might be required for alignment,
		 * but not to use them in the actual exact allocation.
		 */
		args.alignment = 1;
		args.minalignslop = xfs_ialloc_cluster_alignment(&args) - 1;

		/* Allow space for the inode btree to split. */
		args.minleft = args.mp->m_in_maxlevels - 1;
		if ((error = xfs_alloc_vextent(&args)))
			return error;
	} else
		args.fsbno = NULLFSBLOCK;

	if (unlikely(args.fsbno == NULLFSBLOCK)) {
		/*
		 * Set the alignment for the allocation.
		 * If stripe alignment is turned on then align at stripe unit
		 * boundary.
		 * If the cluster size is smaller than a filesystem block
		 * then we're doing I/O for inodes in filesystem block size
		 * pieces, so don't need alignment anyway.
		 */
		isaligned = 0;
		if (args.mp->m_sinoalign) {
			ASSERT(!(args.mp->m_flags & XFS_MOUNT_NOALIGN));
			args.alignment = args.mp->m_dalign;
			isaligned = 1;
		} else
			args.alignment = xfs_ialloc_cluster_alignment(&args);
		/*
		 * Need to figure out where to allocate the inode blocks.
		 * Ideally they should be spaced out through the a.g.
		 * For now, just allocate blocks up front.
		 */
		args.agbno = be32_to_cpu(agi->agi_root);
		args.fsbno = XFS_AGB_TO_FSB(args.mp, agno, args.agbno);
		/*
		 * Allocate a fixed-size extent of inodes.
		 */
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.prod = 1;
		/*
		 * Allow space for the inode btree to split.
		 */
		args.minleft = args.mp->m_in_maxlevels - 1;
		if ((error = xfs_alloc_vextent(&args)))
			return error;
	}

	/*
	 * If stripe alignment is turned on, then try again with cluster
	 * alignment.
	 */
	if (isaligned && args.fsbno == NULLFSBLOCK) {
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.agbno = be32_to_cpu(agi->agi_root);
		args.fsbno = XFS_AGB_TO_FSB(args.mp, agno, args.agbno);
		args.alignment = xfs_ialloc_cluster_alignment(&args);
		if ((error = xfs_alloc_vextent(&args)))
			return error;
	}

	if (args.fsbno == NULLFSBLOCK) {
		*alloc = 0;
		return 0;
	}
	ASSERT(args.len == args.minlen);

	/*
	 * Stamp and write the inode buffers.
	 *
	 * Seed the new inode cluster with a random generation number. This
	 * prevents short-term reuse of generation numbers if a chunk is
	 * freed and then immediately reallocated. We use random numbers
	 * rather than a linear progression to prevent the next generation
	 * number from being easily guessable.
	 */
	error = xfs_ialloc_inode_init(args.mp, tp, agno, args.agbno,
			args.len, prandom_u32());

	if (error)
		return error;
	/*
	 * Convert the results.
	 */
	newino = XFS_OFFBNO_TO_AGINO(args.mp, args.agbno, 0);
	be32_add_cpu(&agi->agi_count, newlen);
	be32_add_cpu(&agi->agi_freecount, newlen);
	pag = xfs_perag_get(args.mp, agno);
	pag->pagi_freecount += newlen;
	xfs_perag_put(pag);
	agi->agi_newino = cpu_to_be32(newino);

	/*
	 * Insert records describing the new inode chunk into the btree.
	 */
	cur = xfs_inobt_init_cursor(args.mp, tp, agbp, agno);
	for (thisino = newino;
	     thisino < newino + newlen;
	     thisino += XFS_INODES_PER_CHUNK) {
		cur->bc_rec.i.ir_startino = thisino;
		cur->bc_rec.i.ir_freecount = XFS_INODES_PER_CHUNK;
		cur->bc_rec.i.ir_free = XFS_INOBT_ALL_FREE;
		error = xfs_btree_lookup(cur, XFS_LOOKUP_EQ, &i);
		if (error) {
			xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
			return error;
		}
		ASSERT(i == 0);
		error = xfs_btree_insert(cur, &i);
		if (error) {
			xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
			return error;
		}
		ASSERT(i == 1);
	}
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	/*
	 * Log allocation group header fields
	 */
	xfs_ialloc_log_agi(tp, agbp,
		XFS_AGI_COUNT | XFS_AGI_FREECOUNT | XFS_AGI_NEWINO);
	/*
	 * Modify/log superblock values for inode count and inode free count.
	 */
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_ICOUNT, (long)newlen);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, (long)newlen);
	*alloc = 1;
	return 0;
}

STATIC xfs_agnumber_t
xfs_ialloc_next_ag(
	xfs_mount_t	*mp)
{
	xfs_agnumber_t	agno;

	spin_lock(&mp->m_agirotor_lock);
	agno = mp->m_agirotor;
	if (++mp->m_agirotor >= mp->m_maxagi)
		mp->m_agirotor = 0;
	spin_unlock(&mp->m_agirotor_lock);

	return agno;
}

/*
 * Select an allocation group to look for a free inode in, based on the parent
 * inode and then mode.  Return the allocation group buffer.
 */
STATIC xfs_agnumber_t
xfs_ialloc_ag_select(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent directory inode number */
	umode_t		mode,		/* bits set to indicate file type */
	int		okalloc)	/* ok to allocate more space */
{
	xfs_agnumber_t	agcount;	/* number of ag's in the filesystem */
	xfs_agnumber_t	agno;		/* current ag number */
	int		flags;		/* alloc buffer locking flags */
	xfs_extlen_t	ineed;		/* blocks needed for inode allocation */
	xfs_extlen_t	longest = 0;	/* longest extent available */
	xfs_mount_t	*mp;		/* mount point structure */
	int		needspace;	/* file mode implies space allocated */
	xfs_perag_t	*pag;		/* per allocation group data */
	xfs_agnumber_t	pagno;		/* parent (starting) ag number */
	int		error;

	/*
	 * Files of these types need at least one block if length > 0
	 * (and they won't fit in the inode, but that's hard to figure out).
	 */
	needspace = S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);
	mp = tp->t_mountp;
	agcount = mp->m_maxagi;
	if (S_ISDIR(mode))
		pagno = xfs_ialloc_next_ag(mp);
	else {
		pagno = XFS_INO_TO_AGNO(mp, parent);
		if (pagno >= agcount)
			pagno = 0;
	}

	ASSERT(pagno < agcount);

	/*
	 * Loop through allocation groups, looking for one with a little
	 * free space in it.  Note we don't look for free inodes, exactly.
	 * Instead, we include whether there is a need to allocate inodes
	 * to mean that blocks must be allocated for them,
	 * if none are currently free.
	 */
	agno = pagno;
	flags = XFS_ALLOC_FLAG_TRYLOCK;
	for (;;) {
		pag = xfs_perag_get(mp, agno);
		if (!pag->pagi_inodeok) {
			xfs_ialloc_next_ag(mp);
			goto nextag;
		}

		if (!pag->pagi_init) {
			error = xfs_ialloc_pagi_init(mp, tp, agno);
			if (error)
				goto nextag;
		}

		if (pag->pagi_freecount) {
			xfs_perag_put(pag);
			return agno;
		}

		if (!okalloc)
			goto nextag;

		if (!pag->pagf_init) {
			error = xfs_alloc_pagf_init(mp, tp, agno, flags);
			if (error)
				goto nextag;
		}

		/*
		 * Is there enough free space for the file plus a block of
		 * inodes? (if we need to allocate some)?
		 */
		ineed = XFS_IALLOC_BLOCKS(mp);
		longest = pag->pagf_longest;
		if (!longest)
			longest = pag->pagf_flcount > 0;

		if (pag->pagf_freeblks >= needspace + ineed &&
		    longest >= ineed) {
			xfs_perag_put(pag);
			return agno;
		}
nextag:
		xfs_perag_put(pag);
		/*
		 * No point in iterating over the rest, if we're shutting
		 * down.
		 */
		if (XFS_FORCED_SHUTDOWN(mp))
			return NULLAGNUMBER;
		agno++;
		if (agno >= agcount)
			agno = 0;
		if (agno == pagno) {
			if (flags == 0)
				return NULLAGNUMBER;
			flags = 0;
		}
	}
}

/*
 * Try to retrieve the next record to the left/right from the current one.
 */
STATIC int
xfs_ialloc_next_rec(
	struct xfs_btree_cur	*cur,
	xfs_inobt_rec_incore_t	*rec,
	int			*done,
	int			left)
{
	int                     error;
	int			i;

	if (left)
		error = xfs_btree_decrement(cur, 0, &i);
	else
		error = xfs_btree_increment(cur, 0, &i);

	if (error)
		return error;
	*done = !i;
	if (i) {
		error = xfs_inobt_get_rec(cur, rec, &i);
		if (error)
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 1);
	}

	return 0;
}

STATIC int
xfs_ialloc_get_rec(
	struct xfs_btree_cur	*cur,
	xfs_agino_t		agino,
	xfs_inobt_rec_incore_t	*rec,
	int			*done,
	int			left)
{
	int                     error;
	int			i;

	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_EQ, &i);
	if (error)
		return error;
	*done = !i;
	if (i) {
		error = xfs_inobt_get_rec(cur, rec, &i);
		if (error)
			return error;
		XFS_WANT_CORRUPTED_RETURN(i == 1);
	}

	return 0;
}

/*
 * Allocate an inode.
 *
 * The caller selected an AG for us, and made sure that free inodes are
 * available.
 */
STATIC int
xfs_dialloc_ag(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_ino_t		parent,
	xfs_ino_t		*inop)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t		agno = be32_to_cpu(agi->agi_seqno);
	xfs_agnumber_t		pagno = XFS_INO_TO_AGNO(mp, parent);
	xfs_agino_t		pagino = XFS_INO_TO_AGINO(mp, parent);
	struct xfs_perag	*pag;
	struct xfs_btree_cur	*cur, *tcur;
	struct xfs_inobt_rec_incore rec, trec;
	xfs_ino_t		ino;
	int			error;
	int			offset;
	int			i, j;

	pag = xfs_perag_get(mp, agno);

	ASSERT(pag->pagi_init);
	ASSERT(pag->pagi_inodeok);
	ASSERT(pag->pagi_freecount > 0);

 restart_pagno:
	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno);
	/*
	 * If pagino is 0 (this is the root inode allocation) use newino.
	 * This must work because we've just allocated some.
	 */
	if (!pagino)
		pagino = be32_to_cpu(agi->agi_newino);

	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error0;

	/*
	 * If in the same AG as the parent, try to get near the parent.
	 */
	if (pagno == agno) {
		int		doneleft;	/* done, to the left */
		int		doneright;	/* done, to the right */
		int		searchdistance = 10;

		error = xfs_inobt_lookup(cur, pagino, XFS_LOOKUP_LE, &i);
		if (error)
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);

		error = xfs_inobt_get_rec(cur, &rec, &j);
		if (error)
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);

		if (rec.ir_freecount > 0) {
			/*
			 * Found a free inode in the same chunk
			 * as the parent, done.
			 */
			goto alloc_inode;
		}


		/*
		 * In the same AG as parent, but parent's chunk is full.
		 */

		/* duplicate the cursor, search left & right simultaneously */
		error = xfs_btree_dup_cursor(cur, &tcur);
		if (error)
			goto error0;

		/*
		 * Skip to last blocks looked up if same parent inode.
		 */
		if (pagino != NULLAGINO &&
		    pag->pagl_pagino == pagino &&
		    pag->pagl_leftrec != NULLAGINO &&
		    pag->pagl_rightrec != NULLAGINO) {
			error = xfs_ialloc_get_rec(tcur, pag->pagl_leftrec,
						   &trec, &doneleft, 1);
			if (error)
				goto error1;

			error = xfs_ialloc_get_rec(cur, pag->pagl_rightrec,
						   &rec, &doneright, 0);
			if (error)
				goto error1;
		} else {
			/* search left with tcur, back up 1 record */
			error = xfs_ialloc_next_rec(tcur, &trec, &doneleft, 1);
			if (error)
				goto error1;

			/* search right with cur, go forward 1 record. */
			error = xfs_ialloc_next_rec(cur, &rec, &doneright, 0);
			if (error)
				goto error1;
		}

		/*
		 * Loop until we find an inode chunk with a free inode.
		 */
		while (!doneleft || !doneright) {
			int	useleft;  /* using left inode chunk this time */

			if (!--searchdistance) {
				/*
				 * Not in range - save last search
				 * location and allocate a new inode
				 */
				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
				pag->pagl_leftrec = trec.ir_startino;
				pag->pagl_rightrec = rec.ir_startino;
				pag->pagl_pagino = pagino;
				goto newino;
			}

			/* figure out the closer block if both are valid. */
			if (!doneleft && !doneright) {
				useleft = pagino -
				 (trec.ir_startino + XFS_INODES_PER_CHUNK - 1) <
				  rec.ir_startino - pagino;
			} else {
				useleft = !doneleft;
			}

			/* free inodes to the left? */
			if (useleft && trec.ir_freecount) {
				rec = trec;
				xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
				cur = tcur;

				pag->pagl_leftrec = trec.ir_startino;
				pag->pagl_rightrec = rec.ir_startino;
				pag->pagl_pagino = pagino;
				goto alloc_inode;
			}

			/* free inodes to the right? */
			if (!useleft && rec.ir_freecount) {
				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);

				pag->pagl_leftrec = trec.ir_startino;
				pag->pagl_rightrec = rec.ir_startino;
				pag->pagl_pagino = pagino;
				goto alloc_inode;
			}

			/* get next record to check */
			if (useleft) {
				error = xfs_ialloc_next_rec(tcur, &trec,
								 &doneleft, 1);
			} else {
				error = xfs_ialloc_next_rec(cur, &rec,
								 &doneright, 0);
			}
			if (error)
				goto error1;
		}

		/*
		 * We've reached the end of the btree. because
		 * we are only searching a small chunk of the
		 * btree each search, there is obviously free
		 * inodes closer to the parent inode than we
		 * are now. restart the search again.
		 */
		pag->pagl_pagino = NULLAGINO;
		pag->pagl_leftrec = NULLAGINO;
		pag->pagl_rightrec = NULLAGINO;
		xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
		xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
		goto restart_pagno;
	}

	/*
	 * In a different AG from the parent.
	 * See if the most recently allocated block has any free.
	 */
newino:
	if (agi->agi_newino != cpu_to_be32(NULLAGINO)) {
		error = xfs_inobt_lookup(cur, be32_to_cpu(agi->agi_newino),
					 XFS_LOOKUP_EQ, &i);
		if (error)
			goto error0;

		if (i == 1) {
			error = xfs_inobt_get_rec(cur, &rec, &j);
			if (error)
				goto error0;

			if (j == 1 && rec.ir_freecount > 0) {
				/*
				 * The last chunk allocated in the group
				 * still has a free inode.
				 */
				goto alloc_inode;
			}
		}
	}

	/*
	 * None left in the last group, search the whole AG
	 */
	error = xfs_inobt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
	if (error)
		goto error0;
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);

	for (;;) {
		error = xfs_inobt_get_rec(cur, &rec, &i);
		if (error)
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		if (rec.ir_freecount > 0)
			break;
		error = xfs_btree_increment(cur, 0, &i);
		if (error)
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	}

alloc_inode:
	offset = xfs_lowbit64(rec.ir_free);
	ASSERT(offset >= 0);
	ASSERT(offset < XFS_INODES_PER_CHUNK);
	ASSERT((XFS_AGINO_TO_OFFSET(mp, rec.ir_startino) %
				   XFS_INODES_PER_CHUNK) == 0);
	ino = XFS_AGINO_TO_INO(mp, agno, rec.ir_startino + offset);
	rec.ir_free &= ~XFS_INOBT_MASK(offset);
	rec.ir_freecount--;
	error = xfs_inobt_update(cur, &rec);
	if (error)
		goto error0;
	be32_add_cpu(&agi->agi_freecount, -1);
	xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
	pag->pagi_freecount--;

	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error0;

	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -1);
	xfs_perag_put(pag);
	*inop = ino;
	return 0;
error1:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
error0:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	xfs_perag_put(pag);
	return error;
}

/*
 * Allocate an inode on disk.
 *
 * Mode is used to tell whether the new inode will need space, and whether it
 * is a directory.
 *
 * This function is designed to be called twice if it has to do an allocation
 * to make more free inodes.  On the first call, *IO_agbp should be set to NULL.
 * If an inode is available without having to performn an allocation, an inode
 * number is returned.  In this case, *IO_agbp is set to NULL.  If an allocation
 * needs to be done, xfs_dialloc returns the current AGI buffer in *IO_agbp.
 * The caller should then commit the current transaction, allocate a
 * new transaction, and call xfs_dialloc() again, passing in the previous value
 * of *IO_agbp.  IO_agbp should be held across the transactions. Since the AGI
 * buffer is locked across the two calls, the second call is guaranteed to have
 * a free inode available.
 *
 * Once we successfully pick an inode its number is returned and the on-disk
 * data structures are updated.  The inode itself is not read in, since doing so
 * would break ordering constraints with xfs_reclaim.
 */
int
xfs_dialloc(
	struct xfs_trans	*tp,
	xfs_ino_t		parent,
	umode_t			mode,
	int			okalloc,
	struct xfs_buf		**IO_agbp,
	xfs_ino_t		*inop)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_buf		*agbp;
	xfs_agnumber_t		agno;
	int			error;
	int			ialloced;
	int			noroom = 0;
	xfs_agnumber_t		start_agno;
	struct xfs_perag	*pag;

	if (*IO_agbp) {
		/*
		 * If the caller passes in a pointer to the AGI buffer,
		 * continue where we left off before.  In this case, we
		 * know that the allocation group has free inodes.
		 */
		agbp = *IO_agbp;
		goto out_alloc;
	}

	/*
	 * We do not have an agbp, so select an initial allocation
	 * group for inode allocation.
	 */
	start_agno = xfs_ialloc_ag_select(tp, parent, mode, okalloc);
	if (start_agno == NULLAGNUMBER) {
		*inop = NULLFSINO;
		return 0;
	}

	/*
	 * If we have already hit the ceiling of inode blocks then clear
	 * okalloc so we scan all available agi structures for a free
	 * inode.
	 */
	if (mp->m_maxicount &&
	    mp->m_sb.sb_icount + XFS_IALLOC_INODES(mp) > mp->m_maxicount) {
		noroom = 1;
		okalloc = 0;
	}

	/*
	 * Loop until we find an allocation group that either has free inodes
	 * or in which we can allocate some inodes.  Iterate through the
	 * allocation groups upward, wrapping at the end.
	 */
	agno = start_agno;
	for (;;) {
		pag = xfs_perag_get(mp, agno);
		if (!pag->pagi_inodeok) {
			xfs_ialloc_next_ag(mp);
			goto nextag;
		}

		if (!pag->pagi_init) {
			error = xfs_ialloc_pagi_init(mp, tp, agno);
			if (error)
				goto out_error;
		}

		/*
		 * Do a first racy fast path check if this AG is usable.
		 */
		if (!pag->pagi_freecount && !okalloc)
			goto nextag;

		/*
		 * Then read in the AGI buffer and recheck with the AGI buffer
		 * lock held.
		 */
		error = xfs_ialloc_read_agi(mp, tp, agno, &agbp);
		if (error)
			goto out_error;

		if (pag->pagi_freecount) {
			xfs_perag_put(pag);
			goto out_alloc;
		}

		if (!okalloc)
			goto nextag_relse_buffer;


		error = xfs_ialloc_ag_alloc(tp, agbp, &ialloced);
		if (error) {
			xfs_trans_brelse(tp, agbp);

			if (error != ENOSPC)
				goto out_error;

			xfs_perag_put(pag);
			*inop = NULLFSINO;
			return 0;
		}

		if (ialloced) {
			/*
			 * We successfully allocated some inodes, return
			 * the current context to the caller so that it
			 * can commit the current transaction and call
			 * us again where we left off.
			 */
			ASSERT(pag->pagi_freecount > 0);
			xfs_perag_put(pag);

			*IO_agbp = agbp;
			*inop = NULLFSINO;
			return 0;
		}

nextag_relse_buffer:
		xfs_trans_brelse(tp, agbp);
nextag:
		xfs_perag_put(pag);
		if (++agno == mp->m_sb.sb_agcount)
			agno = 0;
		if (agno == start_agno) {
			*inop = NULLFSINO;
			return noroom ? ENOSPC : 0;
		}
	}

out_alloc:
	*IO_agbp = NULL;
	return xfs_dialloc_ag(tp, agbp, parent, inop);
out_error:
	xfs_perag_put(pag);
	return XFS_ERROR(error);
}

/*
 * Free disk inode.  Carefully avoids touching the incore inode, all
 * manipulations incore are the caller's responsibility.
 * The on-disk inode is not changed by this operation, only the
 * btree (free inode mask) is changed.
 */
int
xfs_difree(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	inode,		/* inode to be freed */
	xfs_bmap_free_t	*flist,		/* extents to free */
	int		*delete,	/* set if inode cluster was deleted */
	xfs_ino_t	*first_ino)	/* first inode in deleted cluster */
{
	/* REFERENCED */
	xfs_agblock_t	agbno;	/* block number containing inode */
	xfs_buf_t	*agbp;	/* buffer containing allocation group header */
	xfs_agino_t	agino;	/* inode number relative to allocation group */
	xfs_agnumber_t	agno;	/* allocation group number */
	xfs_agi_t	*agi;	/* allocation group header */
	xfs_btree_cur_t	*cur;	/* inode btree cursor */
	int		error;	/* error return value */
	int		i;	/* result code */
	int		ilen;	/* inodes in an inode cluster */
	xfs_mount_t	*mp;	/* mount structure for filesystem */
	int		off;	/* offset of inode in inode chunk */
	xfs_inobt_rec_incore_t rec;	/* btree record */
	struct xfs_perag *pag;

	mp = tp->t_mountp;

	/*
	 * Break up inode number into its components.
	 */
	agno = XFS_INO_TO_AGNO(mp, inode);
	if (agno >= mp->m_sb.sb_agcount)  {
		xfs_warn(mp, "%s: agno >= mp->m_sb.sb_agcount (%d >= %d).",
			__func__, agno, mp->m_sb.sb_agcount);
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}
	agino = XFS_INO_TO_AGINO(mp, inode);
	if (inode != XFS_AGINO_TO_INO(mp, agno, agino))  {
		xfs_warn(mp, "%s: inode != XFS_AGINO_TO_INO() (%llu != %llu).",
			__func__, (unsigned long long)inode,
			(unsigned long long)XFS_AGINO_TO_INO(mp, agno, agino));
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}
	agbno = XFS_AGINO_TO_AGBNO(mp, agino);
	if (agbno >= mp->m_sb.sb_agblocks)  {
		xfs_warn(mp, "%s: agbno >= mp->m_sb.sb_agblocks (%d >= %d).",
			__func__, agbno, mp->m_sb.sb_agblocks);
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}
	/*
	 * Get the allocation group header.
	 */
	error = xfs_ialloc_read_agi(mp, tp, agno, &agbp);
	if (error) {
		xfs_warn(mp, "%s: xfs_ialloc_read_agi() returned error %d.",
			__func__, error);
		return error;
	}
	agi = XFS_BUF_TO_AGI(agbp);
	ASSERT(agi->agi_magicnum == cpu_to_be32(XFS_AGI_MAGIC));
	ASSERT(agbno < be32_to_cpu(agi->agi_length));
	/*
	 * Initialize the cursor.
	 */
	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno);

	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error0;

	/*
	 * Look for the entry describing this inode.
	 */
	if ((error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &i))) {
		xfs_warn(mp, "%s: xfs_inobt_lookup() returned error %d.",
			__func__, error);
		goto error0;
	}
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	error = xfs_inobt_get_rec(cur, &rec, &i);
	if (error) {
		xfs_warn(mp, "%s: xfs_inobt_get_rec() returned error %d.",
			__func__, error);
		goto error0;
	}
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	/*
	 * Get the offset in the inode chunk.
	 */
	off = agino - rec.ir_startino;
	ASSERT(off >= 0 && off < XFS_INODES_PER_CHUNK);
	ASSERT(!(rec.ir_free & XFS_INOBT_MASK(off)));
	/*
	 * Mark the inode free & increment the count.
	 */
	rec.ir_free |= XFS_INOBT_MASK(off);
	rec.ir_freecount++;

	/*
	 * When an inode cluster is free, it becomes eligible for removal
	 */
	if (!(mp->m_flags & XFS_MOUNT_IKEEP) &&
	    (rec.ir_freecount == XFS_IALLOC_INODES(mp))) {

		*delete = 1;
		*first_ino = XFS_AGINO_TO_INO(mp, agno, rec.ir_startino);

		/*
		 * Remove the inode cluster from the AGI B+Tree, adjust the
		 * AGI and Superblock inode counts, and mark the disk space
		 * to be freed when the transaction is committed.
		 */
		ilen = XFS_IALLOC_INODES(mp);
		be32_add_cpu(&agi->agi_count, -ilen);
		be32_add_cpu(&agi->agi_freecount, -(ilen - 1));
		xfs_ialloc_log_agi(tp, agbp, XFS_AGI_COUNT | XFS_AGI_FREECOUNT);
		pag = xfs_perag_get(mp, agno);
		pag->pagi_freecount -= ilen - 1;
		xfs_perag_put(pag);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_ICOUNT, -ilen);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -(ilen - 1));

		if ((error = xfs_btree_delete(cur, &i))) {
			xfs_warn(mp, "%s: xfs_btree_delete returned error %d.",
				__func__, error);
			goto error0;
		}

		xfs_bmap_add_free(XFS_AGB_TO_FSB(mp,
				agno, XFS_INO_TO_AGBNO(mp,rec.ir_startino)),
				XFS_IALLOC_BLOCKS(mp), flist, mp);
	} else {
		*delete = 0;

		error = xfs_inobt_update(cur, &rec);
		if (error) {
			xfs_warn(mp, "%s: xfs_inobt_update returned error %d.",
				__func__, error);
			goto error0;
		}

		/* 
		 * Change the inode free counts and log the ag/sb changes.
		 */
		be32_add_cpu(&agi->agi_freecount, 1);
		xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
		pag = xfs_perag_get(mp, agno);
		pag->pagi_freecount++;
		xfs_perag_put(pag);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, 1);
	}

	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error0;

	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	return 0;

error0:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
}

STATIC int
xfs_imap_lookup(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_agino_t		agino,
	xfs_agblock_t		agbno,
	xfs_agblock_t		*chunk_agbno,
	xfs_agblock_t		*offset_agbno,
	int			flags)
{
	struct xfs_inobt_rec_incore rec;
	struct xfs_btree_cur	*cur;
	struct xfs_buf		*agbp;
	int			error;
	int			i;

	error = xfs_ialloc_read_agi(mp, tp, agno, &agbp);
	if (error) {
		xfs_alert(mp,
			"%s: xfs_ialloc_read_agi() returned error %d, agno %d",
			__func__, error, agno);
		return error;
	}

	/*
	 * Lookup the inode record for the given agino. If the record cannot be
	 * found, then it's an invalid inode number and we should abort. Once
	 * we have a record, we need to ensure it contains the inode number
	 * we are looking up.
	 */
	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno);
	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &i);
	if (!error) {
		if (i)
			error = xfs_inobt_get_rec(cur, &rec, &i);
		if (!error && i == 0)
			error = EINVAL;
	}

	xfs_trans_brelse(tp, agbp);
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	if (error)
		return error;

	/* check that the returned record contains the required inode */
	if (rec.ir_startino > agino ||
	    rec.ir_startino + XFS_IALLOC_INODES(mp) <= agino)
		return EINVAL;

	/* for untrusted inodes check it is allocated first */
	if ((flags & XFS_IGET_UNTRUSTED) &&
	    (rec.ir_free & XFS_INOBT_MASK(agino - rec.ir_startino)))
		return EINVAL;

	*chunk_agbno = XFS_AGINO_TO_AGBNO(mp, rec.ir_startino);
	*offset_agbno = agbno - *chunk_agbno;
	return 0;
}

/*
 * Return the location of the inode in imap, for mapping it into a buffer.
 */
int
xfs_imap(
	xfs_mount_t	 *mp,	/* file system mount structure */
	xfs_trans_t	 *tp,	/* transaction pointer */
	xfs_ino_t	ino,	/* inode to locate */
	struct xfs_imap	*imap,	/* location map structure */
	uint		flags)	/* flags for inode btree lookup */
{
	xfs_agblock_t	agbno;	/* block number of inode in the alloc group */
	xfs_agino_t	agino;	/* inode number within alloc group */
	xfs_agnumber_t	agno;	/* allocation group number */
	int		blks_per_cluster; /* num blocks per inode cluster */
	xfs_agblock_t	chunk_agbno;	/* first block in inode chunk */
	xfs_agblock_t	cluster_agbno;	/* first block in inode cluster */
	int		error;	/* error code */
	int		offset;	/* index of inode in its buffer */
	int		offset_agbno;	/* blks from chunk start to inode */

	ASSERT(ino != NULLFSINO);

	/*
	 * Split up the inode number into its parts.
	 */
	agno = XFS_INO_TO_AGNO(mp, ino);
	agino = XFS_INO_TO_AGINO(mp, ino);
	agbno = XFS_AGINO_TO_AGBNO(mp, agino);
	if (agno >= mp->m_sb.sb_agcount || agbno >= mp->m_sb.sb_agblocks ||
	    ino != XFS_AGINO_TO_INO(mp, agno, agino)) {
#ifdef DEBUG
		/*
		 * Don't output diagnostic information for untrusted inodes
		 * as they can be invalid without implying corruption.
		 */
		if (flags & XFS_IGET_UNTRUSTED)
			return XFS_ERROR(EINVAL);
		if (agno >= mp->m_sb.sb_agcount) {
			xfs_alert(mp,
				"%s: agno (%d) >= mp->m_sb.sb_agcount (%d)",
				__func__, agno, mp->m_sb.sb_agcount);
		}
		if (agbno >= mp->m_sb.sb_agblocks) {
			xfs_alert(mp,
		"%s: agbno (0x%llx) >= mp->m_sb.sb_agblocks (0x%lx)",
				__func__, (unsigned long long)agbno,
				(unsigned long)mp->m_sb.sb_agblocks);
		}
		if (ino != XFS_AGINO_TO_INO(mp, agno, agino)) {
			xfs_alert(mp,
		"%s: ino (0x%llx) != XFS_AGINO_TO_INO() (0x%llx)",
				__func__, ino,
				XFS_AGINO_TO_INO(mp, agno, agino));
		}
		xfs_stack_trace();
#endif /* DEBUG */
		return XFS_ERROR(EINVAL);
	}

	blks_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_blocklog;

	/*
	 * For bulkstat and handle lookups, we have an untrusted inode number
	 * that we have to verify is valid. We cannot do this just by reading
	 * the inode buffer as it may have been unlinked and removed leaving
	 * inodes in stale state on disk. Hence we have to do a btree lookup
	 * in all cases where an untrusted inode number is passed.
	 */
	if (flags & XFS_IGET_UNTRUSTED) {
		error = xfs_imap_lookup(mp, tp, agno, agino, agbno,
					&chunk_agbno, &offset_agbno, flags);
		if (error)
			return error;
		goto out_map;
	}

	/*
	 * If the inode cluster size is the same as the blocksize or
	 * smaller we get to the buffer by simple arithmetics.
	 */
	if (XFS_INODE_CLUSTER_SIZE(mp) <= mp->m_sb.sb_blocksize) {
		offset = XFS_INO_TO_OFFSET(mp, ino);
		ASSERT(offset < mp->m_sb.sb_inopblock);

		imap->im_blkno = XFS_AGB_TO_DADDR(mp, agno, agbno);
		imap->im_len = XFS_FSB_TO_BB(mp, 1);
		imap->im_boffset = (ushort)(offset << mp->m_sb.sb_inodelog);
		return 0;
	}

	/*
	 * If the inode chunks are aligned then use simple maths to
	 * find the location. Otherwise we have to do a btree
	 * lookup to find the location.
	 */
	if (mp->m_inoalign_mask) {
		offset_agbno = agbno & mp->m_inoalign_mask;
		chunk_agbno = agbno - offset_agbno;
	} else {
		error = xfs_imap_lookup(mp, tp, agno, agino, agbno,
					&chunk_agbno, &offset_agbno, flags);
		if (error)
			return error;
	}

out_map:
	ASSERT(agbno >= chunk_agbno);
	cluster_agbno = chunk_agbno +
		((offset_agbno / blks_per_cluster) * blks_per_cluster);
	offset = ((agbno - cluster_agbno) * mp->m_sb.sb_inopblock) +
		XFS_INO_TO_OFFSET(mp, ino);

	imap->im_blkno = XFS_AGB_TO_DADDR(mp, agno, cluster_agbno);
	imap->im_len = XFS_FSB_TO_BB(mp, blks_per_cluster);
	imap->im_boffset = (ushort)(offset << mp->m_sb.sb_inodelog);

	/*
	 * If the inode number maps to a block outside the bounds
	 * of the file system then return NULL rather than calling
	 * read_buf and panicing when we get an error from the
	 * driver.
	 */
	if ((imap->im_blkno + imap->im_len) >
	    XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks)) {
		xfs_alert(mp,
	"%s: (im_blkno (0x%llx) + im_len (0x%llx)) > sb_dblocks (0x%llx)",
			__func__, (unsigned long long) imap->im_blkno,
			(unsigned long long) imap->im_len,
			XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks));
		return XFS_ERROR(EINVAL);
	}
	return 0;
}

/*
 * Compute and fill in value of m_in_maxlevels.
 */
void
xfs_ialloc_compute_maxlevels(
	xfs_mount_t	*mp)		/* file system mount structure */
{
	int		level;
	uint		maxblocks;
	uint		maxleafents;
	int		minleafrecs;
	int		minnoderecs;

	maxleafents = (1LL << XFS_INO_AGINO_BITS(mp)) >>
		XFS_INODES_PER_CHUNK_LOG;
	minleafrecs = mp->m_alloc_mnr[0];
	minnoderecs = mp->m_alloc_mnr[1];
	maxblocks = (maxleafents + minleafrecs - 1) / minleafrecs;
	for (level = 1; maxblocks > 1; level++)
		maxblocks = (maxblocks + minnoderecs - 1) / minnoderecs;
	mp->m_in_maxlevels = level;
}

/*
 * Log specified fields for the ag hdr (inode section)
 */
void
xfs_ialloc_log_agi(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_buf_t	*bp,		/* allocation group header buffer */
	int		fields)		/* bitmask of fields to log */
{
	int			first;		/* first byte number */
	int			last;		/* last byte number */
	static const short	offsets[] = {	/* field starting offsets */
					/* keep in sync with bit definitions */
		offsetof(xfs_agi_t, agi_magicnum),
		offsetof(xfs_agi_t, agi_versionnum),
		offsetof(xfs_agi_t, agi_seqno),
		offsetof(xfs_agi_t, agi_length),
		offsetof(xfs_agi_t, agi_count),
		offsetof(xfs_agi_t, agi_root),
		offsetof(xfs_agi_t, agi_level),
		offsetof(xfs_agi_t, agi_freecount),
		offsetof(xfs_agi_t, agi_newino),
		offsetof(xfs_agi_t, agi_dirino),
		offsetof(xfs_agi_t, agi_unlinked),
		sizeof(xfs_agi_t)
	};
#ifdef DEBUG
	xfs_agi_t		*agi;	/* allocation group header */

	agi = XFS_BUF_TO_AGI(bp);
	ASSERT(agi->agi_magicnum == cpu_to_be32(XFS_AGI_MAGIC));
#endif
	/*
	 * Compute byte offsets for the first and last fields.
	 */
	xfs_btree_offsets(fields, offsets, XFS_AGI_NUM_BITS, &first, &last);
	/*
	 * Log the allocation group inode header buffer.
	 */
	xfs_trans_log_buf(tp, bp, first, last);
}

#ifdef DEBUG
STATIC void
xfs_check_agi_unlinked(
	struct xfs_agi		*agi)
{
	int			i;

	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++)
		ASSERT(agi->agi_unlinked[i]);
}
#else
#define xfs_check_agi_unlinked(agi)
#endif

static void
xfs_agi_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_agi	*agi = XFS_BUF_TO_AGI(bp);
	int		agi_ok;

	/*
	 * Validate the magic number of the agi block.
	 */
	agi_ok = agi->agi_magicnum == cpu_to_be32(XFS_AGI_MAGIC) &&
		XFS_AGI_GOOD_VERSION(be32_to_cpu(agi->agi_versionnum));

	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag)
		agi_ok = agi_ok && be32_to_cpu(agi->agi_seqno) ==
						bp->b_pag->pag_agno;

	if (unlikely(XFS_TEST_ERROR(!agi_ok, mp, XFS_ERRTAG_IALLOC_READ_AGI,
			XFS_RANDOM_IALLOC_READ_AGI))) {
		XFS_CORRUPTION_ERROR(__func__, XFS_ERRLEVEL_LOW, mp, agi);
		xfs_buf_ioerror(bp, EFSCORRUPTED);
	}
	xfs_check_agi_unlinked(agi);
}

static void
xfs_agi_read_verify(
	struct xfs_buf	*bp)
{
	xfs_agi_verify(bp);
}

static void
xfs_agi_write_verify(
	struct xfs_buf	*bp)
{
	xfs_agi_verify(bp);
}

const struct xfs_buf_ops xfs_agi_buf_ops = {
	.verify_read = xfs_agi_read_verify,
	.verify_write = xfs_agi_write_verify,
};

/*
 * Read in the allocation group header (inode allocation section)
 */
int
xfs_read_agi(
	struct xfs_mount	*mp,	/* file system mount structure */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	struct xfs_buf		**bpp)	/* allocation group hdr buf */
{
	int			error;

	ASSERT(agno != NULLAGNUMBER);

	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, bpp, &xfs_agi_buf_ops);
	if (error)
		return error;

	ASSERT(!xfs_buf_geterror(*bpp));
	xfs_buf_set_ref(*bpp, XFS_AGI_REF);
	return 0;
}

int
xfs_ialloc_read_agi(
	struct xfs_mount	*mp,	/* file system mount structure */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_agnumber_t		agno,	/* allocation group number */
	struct xfs_buf		**bpp)	/* allocation group hdr buf */
{
	struct xfs_agi		*agi;	/* allocation group header */
	struct xfs_perag	*pag;	/* per allocation group data */
	int			error;

	error = xfs_read_agi(mp, tp, agno, bpp);
	if (error)
		return error;

	agi = XFS_BUF_TO_AGI(*bpp);
	pag = xfs_perag_get(mp, agno);
	if (!pag->pagi_init) {
		pag->pagi_freecount = be32_to_cpu(agi->agi_freecount);
		pag->pagi_count = be32_to_cpu(agi->agi_count);
		pag->pagi_init = 1;
	}

	/*
	 * It's possible for these to be out of sync if
	 * we are in the middle of a forced shutdown.
	 */
	ASSERT(pag->pagi_freecount == be32_to_cpu(agi->agi_freecount) ||
		XFS_FORCED_SHUTDOWN(mp));
	xfs_perag_put(pag);
	return 0;
}

/*
 * Read in the agi to initialise the per-ag data in the mount structure
 */
int
xfs_ialloc_pagi_init(
	xfs_mount_t	*mp,		/* file system mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno)		/* allocation group number */
{
	xfs_buf_t	*bp = NULL;
	int		error;

	error = xfs_ialloc_read_agi(mp, tp, agno, &bp);
	if (error)
		return error;
	if (bp)
		xfs_trans_brelse(tp, bp);
	return 0;
}
