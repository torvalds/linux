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
#include "xfs_dir2.h"
#include "xfs_dmapi.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_dir2_sf.h"
#include "xfs_attr_sf.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_bmap.h"

/*
 * Log specified fields for the inode given by bp and off.
 */
STATIC void
xfs_ialloc_log_di(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_buf_t	*bp,		/* inode buffer */
	int		off,		/* index of inode in buffer */
	int		fields)		/* bitmask of fields to log */
{
	int			first;		/* first byte number */
	int			ioffset;	/* off in bytes */
	int			last;		/* last byte number */
	xfs_mount_t		*mp;		/* mount point structure */
	static const short	offsets[] = {	/* field offsets */
						/* keep in sync with bits */
		offsetof(xfs_dinode_core_t, di_magic),
		offsetof(xfs_dinode_core_t, di_mode),
		offsetof(xfs_dinode_core_t, di_version),
		offsetof(xfs_dinode_core_t, di_format),
		offsetof(xfs_dinode_core_t, di_onlink),
		offsetof(xfs_dinode_core_t, di_uid),
		offsetof(xfs_dinode_core_t, di_gid),
		offsetof(xfs_dinode_core_t, di_nlink),
		offsetof(xfs_dinode_core_t, di_projid),
		offsetof(xfs_dinode_core_t, di_pad),
		offsetof(xfs_dinode_core_t, di_atime),
		offsetof(xfs_dinode_core_t, di_mtime),
		offsetof(xfs_dinode_core_t, di_ctime),
		offsetof(xfs_dinode_core_t, di_size),
		offsetof(xfs_dinode_core_t, di_nblocks),
		offsetof(xfs_dinode_core_t, di_extsize),
		offsetof(xfs_dinode_core_t, di_nextents),
		offsetof(xfs_dinode_core_t, di_anextents),
		offsetof(xfs_dinode_core_t, di_forkoff),
		offsetof(xfs_dinode_core_t, di_aformat),
		offsetof(xfs_dinode_core_t, di_dmevmask),
		offsetof(xfs_dinode_core_t, di_dmstate),
		offsetof(xfs_dinode_core_t, di_flags),
		offsetof(xfs_dinode_core_t, di_gen),
		offsetof(xfs_dinode_t, di_next_unlinked),
		offsetof(xfs_dinode_t, di_u),
		offsetof(xfs_dinode_t, di_a),
		sizeof(xfs_dinode_t)
	};


	ASSERT(offsetof(xfs_dinode_t, di_core) == 0);
	ASSERT((fields & (XFS_DI_U|XFS_DI_A)) == 0);
	mp = tp->t_mountp;
	/*
	 * Get the inode-relative first and last bytes for these fields
	 */
	xfs_btree_offsets(fields, offsets, XFS_DI_NUM_BITS, &first, &last);
	/*
	 * Convert to buffer offsets and log it.
	 */
	ioffset = off << mp->m_sb.sb_inodelog;
	first += ioffset;
	last += ioffset;
	xfs_trans_log_buf(tp, bp, first, last);
}

/*
 * Allocation group level functions.
 */

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
	int		blks_per_cluster;  /* fs blocks per inode cluster */
	xfs_btree_cur_t	*cur;		/* inode btree cursor */
	xfs_daddr_t	d;		/* disk addr of buffer */
	int		error;
	xfs_buf_t	*fbuf;		/* new free inodes' buffer */
	xfs_dinode_t	*free;		/* new free inode structure */
	int		i;		/* inode counter */
	int		j;		/* block counter */
	int		nbufs;		/* num bufs of new inodes */
	xfs_agino_t	newino;		/* new first inode's number */
	xfs_agino_t	newlen;		/* new number of inodes */
	int		ninodes;	/* num inodes per buf */
	xfs_agino_t	thisino;	/* current inode number, for loop */
	int		version;	/* inode version number to use */
	int		isaligned = 0;	/* inode allocation at stripe unit */
					/* boundary */

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
	args.agbno = XFS_AGINO_TO_AGBNO(args.mp, newino) +
			XFS_IALLOC_BLOCKS(args.mp);
	if (likely(newino != NULLAGINO &&
		  (args.agbno < be32_to_cpu(agi->agi_length)))) {
		args.fsbno = XFS_AGB_TO_FSB(args.mp,
				be32_to_cpu(agi->agi_seqno), args.agbno);
		args.type = XFS_ALLOCTYPE_THIS_BNO;
		args.mod = args.total = args.wasdel = args.isfl =
			args.userdata = args.minalignslop = 0;
		args.prod = 1;
		args.alignment = 1;
		/*
		 * Allow space for the inode btree to split.
		 */
		args.minleft = XFS_IN_MAXLEVELS(args.mp) - 1;
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
		} else if (XFS_SB_VERSION_HASALIGN(&args.mp->m_sb) &&
			   args.mp->m_sb.sb_inoalignmt >=
			   XFS_B_TO_FSBT(args.mp,
			  	XFS_INODE_CLUSTER_SIZE(args.mp)))
				args.alignment = args.mp->m_sb.sb_inoalignmt;
		else
			args.alignment = 1;
		/*
		 * Need to figure out where to allocate the inode blocks.
		 * Ideally they should be spaced out through the a.g.
		 * For now, just allocate blocks up front.
		 */
		args.agbno = be32_to_cpu(agi->agi_root);
		args.fsbno = XFS_AGB_TO_FSB(args.mp,
				be32_to_cpu(agi->agi_seqno), args.agbno);
		/*
		 * Allocate a fixed-size extent of inodes.
		 */
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.mod = args.total = args.wasdel = args.isfl =
			args.userdata = args.minalignslop = 0;
		args.prod = 1;
		/*
		 * Allow space for the inode btree to split.
		 */
		args.minleft = XFS_IN_MAXLEVELS(args.mp) - 1;
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
		args.fsbno = XFS_AGB_TO_FSB(args.mp,
				be32_to_cpu(agi->agi_seqno), args.agbno);
		if (XFS_SB_VERSION_HASALIGN(&args.mp->m_sb) &&
			args.mp->m_sb.sb_inoalignmt >=
			XFS_B_TO_FSBT(args.mp, XFS_INODE_CLUSTER_SIZE(args.mp)))
				args.alignment = args.mp->m_sb.sb_inoalignmt;
		else
			args.alignment = 1;
		if ((error = xfs_alloc_vextent(&args)))
			return error;
	}

	if (args.fsbno == NULLFSBLOCK) {
		*alloc = 0;
		return 0;
	}
	ASSERT(args.len == args.minlen);
	/*
	 * Convert the results.
	 */
	newino = XFS_OFFBNO_TO_AGINO(args.mp, args.agbno, 0);
	/*
	 * Loop over the new block(s), filling in the inodes.
	 * For small block sizes, manipulate the inodes in buffers
	 * which are multiples of the blocks size.
	 */
	if (args.mp->m_sb.sb_blocksize >= XFS_INODE_CLUSTER_SIZE(args.mp)) {
		blks_per_cluster = 1;
		nbufs = (int)args.len;
		ninodes = args.mp->m_sb.sb_inopblock;
	} else {
		blks_per_cluster = XFS_INODE_CLUSTER_SIZE(args.mp) /
				   args.mp->m_sb.sb_blocksize;
		nbufs = (int)args.len / blks_per_cluster;
		ninodes = blks_per_cluster * args.mp->m_sb.sb_inopblock;
	}
	/*
	 * Figure out what version number to use in the inodes we create.
	 * If the superblock version has caught up to the one that supports
	 * the new inode format, then use the new inode version.  Otherwise
	 * use the old version so that old kernels will continue to be
	 * able to use the file system.
	 */
	if (XFS_SB_VERSION_HASNLINK(&args.mp->m_sb))
		version = XFS_DINODE_VERSION_2;
	else
		version = XFS_DINODE_VERSION_1;

	for (j = 0; j < nbufs; j++) {
		/*
		 * Get the block.
		 */
		d = XFS_AGB_TO_DADDR(args.mp, be32_to_cpu(agi->agi_seqno),
				     args.agbno + (j * blks_per_cluster));
		fbuf = xfs_trans_get_buf(tp, args.mp->m_ddev_targp, d,
					 args.mp->m_bsize * blks_per_cluster,
					 XFS_BUF_LOCK);
		ASSERT(fbuf);
		ASSERT(!XFS_BUF_GETERROR(fbuf));
		/*
		 * Set initial values for the inodes in this buffer.
		 */
		xfs_biozero(fbuf, 0, ninodes << args.mp->m_sb.sb_inodelog);
		for (i = 0; i < ninodes; i++) {
			free = XFS_MAKE_IPTR(args.mp, fbuf, i);
			INT_SET(free->di_core.di_magic, ARCH_CONVERT, XFS_DINODE_MAGIC);
			INT_SET(free->di_core.di_version, ARCH_CONVERT, version);
			INT_SET(free->di_next_unlinked, ARCH_CONVERT, NULLAGINO);
			xfs_ialloc_log_di(tp, fbuf, i,
				XFS_DI_CORE_BITS | XFS_DI_NEXT_UNLINKED);
		}
		xfs_trans_inode_alloc_buf(tp, fbuf);
	}
	be32_add(&agi->agi_count, newlen);
	be32_add(&agi->agi_freecount, newlen);
	down_read(&args.mp->m_peraglock);
	args.mp->m_perag[be32_to_cpu(agi->agi_seqno)].pagi_freecount += newlen;
	up_read(&args.mp->m_peraglock);
	agi->agi_newino = cpu_to_be32(newino);
	/*
	 * Insert records describing the new inode chunk into the btree.
	 */
	cur = xfs_btree_init_cursor(args.mp, tp, agbp,
			be32_to_cpu(agi->agi_seqno),
			XFS_BTNUM_INO, (xfs_inode_t *)0, 0);
	for (thisino = newino;
	     thisino < newino + newlen;
	     thisino += XFS_INODES_PER_CHUNK) {
		if ((error = xfs_inobt_lookup_eq(cur, thisino,
				XFS_INODES_PER_CHUNK, XFS_INOBT_ALL_FREE, &i))) {
			xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
			return error;
		}
		ASSERT(i == 0);
		if ((error = xfs_inobt_insert(cur, &i))) {
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

STATIC __inline xfs_agnumber_t
xfs_ialloc_next_ag(
	xfs_mount_t	*mp)
{
	xfs_agnumber_t	agno;

	spin_lock(&mp->m_agirotor_lock);
	agno = mp->m_agirotor;
	if (++mp->m_agirotor == mp->m_maxagi)
		mp->m_agirotor = 0;
	spin_unlock(&mp->m_agirotor_lock);

	return agno;
}

/*
 * Select an allocation group to look for a free inode in, based on the parent
 * inode and then mode.  Return the allocation group buffer.
 */
STATIC xfs_buf_t *			/* allocation group buffer */
xfs_ialloc_ag_select(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent directory inode number */
	mode_t		mode,		/* bits set to indicate file type */
	int		okalloc)	/* ok to allocate more space */
{
	xfs_buf_t	*agbp;		/* allocation group header buffer */
	xfs_agnumber_t	agcount;	/* number of ag's in the filesystem */
	xfs_agnumber_t	agno;		/* current ag number */
	int		flags;		/* alloc buffer locking flags */
	xfs_extlen_t	ineed;		/* blocks needed for inode allocation */
	xfs_extlen_t	longest = 0;	/* longest extent available */
	xfs_mount_t	*mp;		/* mount point structure */
	int		needspace;	/* file mode implies space allocated */
	xfs_perag_t	*pag;		/* per allocation group data */
	xfs_agnumber_t	pagno;		/* parent (starting) ag number */

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
	down_read(&mp->m_peraglock);
	for (;;) {
		pag = &mp->m_perag[agno];
		if (!pag->pagi_init) {
			if (xfs_ialloc_read_agi(mp, tp, agno, &agbp)) {
				agbp = NULL;
				goto nextag;
			}
		} else
			agbp = NULL;

		if (!pag->pagi_inodeok) {
			xfs_ialloc_next_ag(mp);
			goto unlock_nextag;
		}

		/*
		 * Is there enough free space for the file plus a block
		 * of inodes (if we need to allocate some)?
		 */
		ineed = pag->pagi_freecount ? 0 : XFS_IALLOC_BLOCKS(mp);
		if (ineed && !pag->pagf_init) {
			if (agbp == NULL &&
			    xfs_ialloc_read_agi(mp, tp, agno, &agbp)) {
				agbp = NULL;
				goto nextag;
			}
			(void)xfs_alloc_pagf_init(mp, tp, agno, flags);
		}
		if (!ineed || pag->pagf_init) {
			if (ineed && !(longest = pag->pagf_longest))
				longest = pag->pagf_flcount > 0;
			if (!ineed ||
			    (pag->pagf_freeblks >= needspace + ineed &&
			     longest >= ineed &&
			     okalloc)) {
				if (agbp == NULL &&
				    xfs_ialloc_read_agi(mp, tp, agno, &agbp)) {
					agbp = NULL;
					goto nextag;
				}
				up_read(&mp->m_peraglock);
				return agbp;
			}
		}
unlock_nextag:
		if (agbp)
			xfs_trans_brelse(tp, agbp);
nextag:
		/*
		 * No point in iterating over the rest, if we're shutting
		 * down.
		 */
		if (XFS_FORCED_SHUTDOWN(mp)) {
			up_read(&mp->m_peraglock);
			return (xfs_buf_t *)0;
		}
		agno++;
		if (agno >= agcount)
			agno = 0;
		if (agno == pagno) {
			if (flags == 0) {
				up_read(&mp->m_peraglock);
				return (xfs_buf_t *)0;
			}
			flags = 0;
		}
	}
}

/*
 * Visible inode allocation functions.
 */

/*
 * Allocate an inode on disk.
 * Mode is used to tell whether the new inode will need space, and whether
 * it is a directory.
 *
 * The arguments IO_agbp and alloc_done are defined to work within
 * the constraint of one allocation per transaction.
 * xfs_dialloc() is designed to be called twice if it has to do an
 * allocation to make more free inodes.  On the first call,
 * IO_agbp should be set to NULL. If an inode is available,
 * i.e., xfs_dialloc() did not need to do an allocation, an inode
 * number is returned.  In this case, IO_agbp would be set to the
 * current ag_buf and alloc_done set to false.
 * If an allocation needed to be done, xfs_dialloc would return
 * the current ag_buf in IO_agbp and set alloc_done to true.
 * The caller should then commit the current transaction, allocate a new
 * transaction, and call xfs_dialloc() again, passing in the previous
 * value of IO_agbp.  IO_agbp should be held across the transactions.
 * Since the agbp is locked across the two calls, the second call is
 * guaranteed to have a free inode available.
 *
 * Once we successfully pick an inode its number is returned and the
 * on-disk data structures are updated.  The inode itself is not read
 * in, since doing so would break ordering constraints with xfs_reclaim.
 */
int
xfs_dialloc(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_ino_t	parent,		/* parent inode (directory) */
	mode_t		mode,		/* mode bits for new inode */
	int		okalloc,	/* ok to allocate more space */
	xfs_buf_t	**IO_agbp,	/* in/out ag header's buffer */
	boolean_t	*alloc_done,	/* true if we needed to replenish
					   inode freelist */
	xfs_ino_t	*inop)		/* inode number allocated */
{
	xfs_agnumber_t	agcount;	/* number of allocation groups */
	xfs_buf_t	*agbp;		/* allocation group header's buffer */
	xfs_agnumber_t	agno;		/* allocation group number */
	xfs_agi_t	*agi;		/* allocation group header structure */
	xfs_btree_cur_t	*cur;		/* inode allocation btree cursor */
	int		error;		/* error return value */
	int		i;		/* result code */
	int		ialloced;	/* inode allocation status */
	int		noroom = 0;	/* no space for inode blk allocation */
	xfs_ino_t	ino;		/* fs-relative inode to be returned */
	/* REFERENCED */
	int		j;		/* result code */
	xfs_mount_t	*mp;		/* file system mount structure */
	int		offset;		/* index of inode in chunk */
	xfs_agino_t	pagino;		/* parent's a.g. relative inode # */
	xfs_agnumber_t	pagno;		/* parent's allocation group number */
	xfs_inobt_rec_t	rec;		/* inode allocation record */
	xfs_agnumber_t	tagno;		/* testing allocation group number */
	xfs_btree_cur_t	*tcur;		/* temp cursor */
	xfs_inobt_rec_t	trec;		/* temp inode allocation record */


	if (*IO_agbp == NULL) {
		/*
		 * We do not have an agbp, so select an initial allocation
		 * group for inode allocation.
		 */
		agbp = xfs_ialloc_ag_select(tp, parent, mode, okalloc);
		/*
		 * Couldn't find an allocation group satisfying the
		 * criteria, give up.
		 */
		if (!agbp) {
			*inop = NULLFSINO;
			return 0;
		}
		agi = XFS_BUF_TO_AGI(agbp);
		ASSERT(be32_to_cpu(agi->agi_magicnum) == XFS_AGI_MAGIC);
	} else {
		/*
		 * Continue where we left off before.  In this case, we
		 * know that the allocation group has free inodes.
		 */
		agbp = *IO_agbp;
		agi = XFS_BUF_TO_AGI(agbp);
		ASSERT(be32_to_cpu(agi->agi_magicnum) == XFS_AGI_MAGIC);
		ASSERT(be32_to_cpu(agi->agi_freecount) > 0);
	}
	mp = tp->t_mountp;
	agcount = mp->m_sb.sb_agcount;
	agno = be32_to_cpu(agi->agi_seqno);
	tagno = agno;
	pagno = XFS_INO_TO_AGNO(mp, parent);
	pagino = XFS_INO_TO_AGINO(mp, parent);

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
	*alloc_done = B_FALSE;
	while (!agi->agi_freecount) {
		/*
		 * Don't do anything if we're not supposed to allocate
		 * any blocks, just go on to the next ag.
		 */
		if (okalloc) {
			/*
			 * Try to allocate some new inodes in the allocation
			 * group.
			 */
			if ((error = xfs_ialloc_ag_alloc(tp, agbp, &ialloced))) {
				xfs_trans_brelse(tp, agbp);
				if (error == ENOSPC) {
					*inop = NULLFSINO;
					return 0;
				} else
					return error;
			}
			if (ialloced) {
				/*
				 * We successfully allocated some inodes, return
				 * the current context to the caller so that it
				 * can commit the current transaction and call
				 * us again where we left off.
				 */
				ASSERT(be32_to_cpu(agi->agi_freecount) > 0);
				*alloc_done = B_TRUE;
				*IO_agbp = agbp;
				*inop = NULLFSINO;
				return 0;
			}
		}
		/*
		 * If it failed, give up on this ag.
		 */
		xfs_trans_brelse(tp, agbp);
		/*
		 * Go on to the next ag: get its ag header.
		 */
nextag:
		if (++tagno == agcount)
			tagno = 0;
		if (tagno == agno) {
			*inop = NULLFSINO;
			return noroom ? ENOSPC : 0;
		}
		down_read(&mp->m_peraglock);
		if (mp->m_perag[tagno].pagi_inodeok == 0) {
			up_read(&mp->m_peraglock);
			goto nextag;
		}
		error = xfs_ialloc_read_agi(mp, tp, tagno, &agbp);
		up_read(&mp->m_peraglock);
		if (error)
			goto nextag;
		agi = XFS_BUF_TO_AGI(agbp);
		ASSERT(be32_to_cpu(agi->agi_magicnum) == XFS_AGI_MAGIC);
	}
	/*
	 * Here with an allocation group that has a free inode.
	 * Reset agno since we may have chosen a new ag in the
	 * loop above.
	 */
	agno = tagno;
	*IO_agbp = NULL;
	cur = xfs_btree_init_cursor(mp, tp, agbp, be32_to_cpu(agi->agi_seqno),
				    XFS_BTNUM_INO, (xfs_inode_t *)0, 0);
	/*
	 * If pagino is 0 (this is the root inode allocation) use newino.
	 * This must work because we've just allocated some.
	 */
	if (!pagino)
		pagino = be32_to_cpu(agi->agi_newino);
#ifdef DEBUG
	if (cur->bc_nlevels == 1) {
		int	freecount = 0;

		if ((error = xfs_inobt_lookup_ge(cur, 0, 0, 0, &i)))
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
		do {
			if ((error = xfs_inobt_get_rec(cur, &rec.ir_startino,
					&rec.ir_freecount, &rec.ir_free, &i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			freecount += rec.ir_freecount;
			if ((error = xfs_inobt_increment(cur, 0, &i)))
				goto error0;
		} while (i == 1);

		ASSERT(freecount == be32_to_cpu(agi->agi_freecount) ||
		       XFS_FORCED_SHUTDOWN(mp));
	}
#endif
	/*
	 * If in the same a.g. as the parent, try to get near the parent.
	 */
	if (pagno == agno) {
		if ((error = xfs_inobt_lookup_le(cur, pagino, 0, 0, &i)))
			goto error0;
		if (i != 0 &&
		    (error = xfs_inobt_get_rec(cur, &rec.ir_startino,
			    &rec.ir_freecount, &rec.ir_free, &j)) == 0 &&
		    j == 1 &&
		    rec.ir_freecount > 0) {
			/*
			 * Found a free inode in the same chunk
			 * as parent, done.
			 */
		}
		/*
		 * In the same a.g. as parent, but parent's chunk is full.
		 */
		else {
			int	doneleft;	/* done, to the left */
			int	doneright;	/* done, to the right */

			if (error)
				goto error0;
			ASSERT(i == 1);
			ASSERT(j == 1);
			/*
			 * Duplicate the cursor, search left & right
			 * simultaneously.
			 */
			if ((error = xfs_btree_dup_cursor(cur, &tcur)))
				goto error0;
			/*
			 * Search left with tcur, back up 1 record.
			 */
			if ((error = xfs_inobt_decrement(tcur, 0, &i)))
				goto error1;
			doneleft = !i;
			if (!doneleft) {
				if ((error = xfs_inobt_get_rec(tcur,
						&trec.ir_startino,
						&trec.ir_freecount,
						&trec.ir_free, &i)))
					goto error1;
				XFS_WANT_CORRUPTED_GOTO(i == 1, error1);
			}
			/*
			 * Search right with cur, go forward 1 record.
			 */
			if ((error = xfs_inobt_increment(cur, 0, &i)))
				goto error1;
			doneright = !i;
			if (!doneright) {
				if ((error = xfs_inobt_get_rec(cur,
						&rec.ir_startino,
						&rec.ir_freecount,
						&rec.ir_free, &i)))
					goto error1;
				XFS_WANT_CORRUPTED_GOTO(i == 1, error1);
			}
			/*
			 * Loop until we find the closest inode chunk
			 * with a free one.
			 */
			while (!doneleft || !doneright) {
				int	useleft;  /* using left inode
						     chunk this time */

				/*
				 * Figure out which block is closer,
				 * if both are valid.
				 */
				if (!doneleft && !doneright)
					useleft =
						pagino -
						(trec.ir_startino +
						 XFS_INODES_PER_CHUNK - 1) <
						 rec.ir_startino - pagino;
				else
					useleft = !doneleft;
				/*
				 * If checking the left, does it have
				 * free inodes?
				 */
				if (useleft && trec.ir_freecount) {
					/*
					 * Yes, set it up as the chunk to use.
					 */
					rec = trec;
					xfs_btree_del_cursor(cur,
						XFS_BTREE_NOERROR);
					cur = tcur;
					break;
				}
				/*
				 * If checking the right, does it have
				 * free inodes?
				 */
				if (!useleft && rec.ir_freecount) {
					/*
					 * Yes, it's already set up.
					 */
					xfs_btree_del_cursor(tcur,
						XFS_BTREE_NOERROR);
					break;
				}
				/*
				 * If used the left, get another one
				 * further left.
				 */
				if (useleft) {
					if ((error = xfs_inobt_decrement(tcur, 0,
							&i)))
						goto error1;
					doneleft = !i;
					if (!doneleft) {
						if ((error = xfs_inobt_get_rec(
							    tcur,
							    &trec.ir_startino,
							    &trec.ir_freecount,
							    &trec.ir_free, &i)))
							goto error1;
						XFS_WANT_CORRUPTED_GOTO(i == 1,
							error1);
					}
				}
				/*
				 * If used the right, get another one
				 * further right.
				 */
				else {
					if ((error = xfs_inobt_increment(cur, 0,
							&i)))
						goto error1;
					doneright = !i;
					if (!doneright) {
						if ((error = xfs_inobt_get_rec(
							    cur,
							    &rec.ir_startino,
							    &rec.ir_freecount,
							    &rec.ir_free, &i)))
							goto error1;
						XFS_WANT_CORRUPTED_GOTO(i == 1,
							error1);
					}
				}
			}
			ASSERT(!doneleft || !doneright);
		}
	}
	/*
	 * In a different a.g. from the parent.
	 * See if the most recently allocated block has any free.
	 */
	else if (be32_to_cpu(agi->agi_newino) != NULLAGINO) {
		if ((error = xfs_inobt_lookup_eq(cur,
				be32_to_cpu(agi->agi_newino), 0, 0, &i)))
			goto error0;
		if (i == 1 &&
		    (error = xfs_inobt_get_rec(cur, &rec.ir_startino,
			    &rec.ir_freecount, &rec.ir_free, &j)) == 0 &&
		    j == 1 &&
		    rec.ir_freecount > 0) {
			/*
			 * The last chunk allocated in the group still has
			 * a free inode.
			 */
		}
		/*
		 * None left in the last group, search the whole a.g.
		 */
		else {
			if (error)
				goto error0;
			if ((error = xfs_inobt_lookup_ge(cur, 0, 0, 0, &i)))
				goto error0;
			ASSERT(i == 1);
			for (;;) {
				if ((error = xfs_inobt_get_rec(cur,
						&rec.ir_startino,
						&rec.ir_freecount, &rec.ir_free,
						&i)))
					goto error0;
				XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
				if (rec.ir_freecount > 0)
					break;
				if ((error = xfs_inobt_increment(cur, 0, &i)))
					goto error0;
				XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			}
		}
	}
	offset = XFS_IALLOC_FIND_FREE(&rec.ir_free);
	ASSERT(offset >= 0);
	ASSERT(offset < XFS_INODES_PER_CHUNK);
	ASSERT((XFS_AGINO_TO_OFFSET(mp, rec.ir_startino) %
				   XFS_INODES_PER_CHUNK) == 0);
	ino = XFS_AGINO_TO_INO(mp, agno, rec.ir_startino + offset);
	XFS_INOBT_CLR_FREE(&rec, offset);
	rec.ir_freecount--;
	if ((error = xfs_inobt_update(cur, rec.ir_startino, rec.ir_freecount,
			rec.ir_free)))
		goto error0;
	be32_add(&agi->agi_freecount, -1);
	xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
	down_read(&mp->m_peraglock);
	mp->m_perag[tagno].pagi_freecount--;
	up_read(&mp->m_peraglock);
#ifdef DEBUG
	if (cur->bc_nlevels == 1) {
		int	freecount = 0;

		if ((error = xfs_inobt_lookup_ge(cur, 0, 0, 0, &i)))
			goto error0;
		do {
			if ((error = xfs_inobt_get_rec(cur, &rec.ir_startino,
					&rec.ir_freecount, &rec.ir_free, &i)))
				goto error0;
			XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
			freecount += rec.ir_freecount;
			if ((error = xfs_inobt_increment(cur, 0, &i)))
				goto error0;
		} while (i == 1);
		ASSERT(freecount == be32_to_cpu(agi->agi_freecount) ||
		       XFS_FORCED_SHUTDOWN(mp));
	}
#endif
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -1);
	*inop = ino;
	return 0;
error1:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
error0:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
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
	xfs_inobt_rec_t	rec;	/* btree record */

	mp = tp->t_mountp;

	/*
	 * Break up inode number into its components.
	 */
	agno = XFS_INO_TO_AGNO(mp, inode);
	if (agno >= mp->m_sb.sb_agcount)  {
		cmn_err(CE_WARN,
			"xfs_difree: agno >= mp->m_sb.sb_agcount (%d >= %d) on %s.  Returning EINVAL.",
			agno, mp->m_sb.sb_agcount, mp->m_fsname);
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}
	agino = XFS_INO_TO_AGINO(mp, inode);
	if (inode != XFS_AGINO_TO_INO(mp, agno, agino))  {
		cmn_err(CE_WARN,
			"xfs_difree: inode != XFS_AGINO_TO_INO() "
			"(%llu != %llu) on %s.  Returning EINVAL.",
			(unsigned long long)inode,
			(unsigned long long)XFS_AGINO_TO_INO(mp, agno, agino),
			mp->m_fsname);
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}
	agbno = XFS_AGINO_TO_AGBNO(mp, agino);
	if (agbno >= mp->m_sb.sb_agblocks)  {
		cmn_err(CE_WARN,
			"xfs_difree: agbno >= mp->m_sb.sb_agblocks (%d >= %d) on %s.  Returning EINVAL.",
			agbno, mp->m_sb.sb_agblocks, mp->m_fsname);
		ASSERT(0);
		return XFS_ERROR(EINVAL);
	}
	/*
	 * Get the allocation group header.
	 */
	down_read(&mp->m_peraglock);
	error = xfs_ialloc_read_agi(mp, tp, agno, &agbp);
	up_read(&mp->m_peraglock);
	if (error) {
		cmn_err(CE_WARN,
			"xfs_difree: xfs_ialloc_read_agi() returned an error %d on %s.  Returning error.",
			error, mp->m_fsname);
		return error;
	}
	agi = XFS_BUF_TO_AGI(agbp);
	ASSERT(be32_to_cpu(agi->agi_magicnum) == XFS_AGI_MAGIC);
	ASSERT(agbno < be32_to_cpu(agi->agi_length));
	/*
	 * Initialize the cursor.
	 */
	cur = xfs_btree_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_INO,
		(xfs_inode_t *)0, 0);
#ifdef DEBUG
	if (cur->bc_nlevels == 1) {
		int freecount = 0;

		if ((error = xfs_inobt_lookup_ge(cur, 0, 0, 0, &i)))
			goto error0;
		do {
			if ((error = xfs_inobt_get_rec(cur, &rec.ir_startino,
					&rec.ir_freecount, &rec.ir_free, &i)))
				goto error0;
			if (i) {
				freecount += rec.ir_freecount;
				if ((error = xfs_inobt_increment(cur, 0, &i)))
					goto error0;
			}
		} while (i == 1);
		ASSERT(freecount == be32_to_cpu(agi->agi_freecount) ||
		       XFS_FORCED_SHUTDOWN(mp));
	}
#endif
	/*
	 * Look for the entry describing this inode.
	 */
	if ((error = xfs_inobt_lookup_le(cur, agino, 0, 0, &i))) {
		cmn_err(CE_WARN,
			"xfs_difree: xfs_inobt_lookup_le returned()  an error %d on %s.  Returning error.",
			error, mp->m_fsname);
		goto error0;
	}
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	if ((error = xfs_inobt_get_rec(cur, &rec.ir_startino, &rec.ir_freecount,
			&rec.ir_free, &i))) {
		cmn_err(CE_WARN,
			"xfs_difree: xfs_inobt_get_rec()  returned an error %d on %s.  Returning error.",
			error, mp->m_fsname);
		goto error0;
	}
	XFS_WANT_CORRUPTED_GOTO(i == 1, error0);
	/*
	 * Get the offset in the inode chunk.
	 */
	off = agino - rec.ir_startino;
	ASSERT(off >= 0 && off < XFS_INODES_PER_CHUNK);
	ASSERT(!XFS_INOBT_IS_FREE(&rec, off));
	/*
	 * Mark the inode free & increment the count.
	 */
	XFS_INOBT_SET_FREE(&rec, off);
	rec.ir_freecount++;

	/*
	 * When an inode cluster is free, it becomes eligible for removal
	 */
	if ((mp->m_flags & XFS_MOUNT_IDELETE) &&
	    (rec.ir_freecount == XFS_IALLOC_INODES(mp))) {

		*delete = 1;
		*first_ino = XFS_AGINO_TO_INO(mp, agno, rec.ir_startino);

		/*
		 * Remove the inode cluster from the AGI B+Tree, adjust the
		 * AGI and Superblock inode counts, and mark the disk space
		 * to be freed when the transaction is committed.
		 */
		ilen = XFS_IALLOC_INODES(mp);
		be32_add(&agi->agi_count, -ilen);
		be32_add(&agi->agi_freecount, -(ilen - 1));
		xfs_ialloc_log_agi(tp, agbp, XFS_AGI_COUNT | XFS_AGI_FREECOUNT);
		down_read(&mp->m_peraglock);
		mp->m_perag[agno].pagi_freecount -= ilen - 1;
		up_read(&mp->m_peraglock);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_ICOUNT, -ilen);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -(ilen - 1));

		if ((error = xfs_inobt_delete(cur, &i))) {
			cmn_err(CE_WARN, "xfs_difree: xfs_inobt_delete returned an error %d on %s.\n",
				error, mp->m_fsname);
			goto error0;
		}

		xfs_bmap_add_free(XFS_AGB_TO_FSB(mp,
				agno, XFS_INO_TO_AGBNO(mp,rec.ir_startino)),
				XFS_IALLOC_BLOCKS(mp), flist, mp);
	} else {
		*delete = 0;

		if ((error = xfs_inobt_update(cur, rec.ir_startino, rec.ir_freecount, rec.ir_free))) {
			cmn_err(CE_WARN,
				"xfs_difree: xfs_inobt_update()  returned an error %d on %s.  Returning error.",
				error, mp->m_fsname);
			goto error0;
		}
		/* 
		 * Change the inode free counts and log the ag/sb changes.
		 */
		be32_add(&agi->agi_freecount, 1);
		xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
		down_read(&mp->m_peraglock);
		mp->m_perag[agno].pagi_freecount++;
		up_read(&mp->m_peraglock);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, 1);
	}

#ifdef DEBUG
	if (cur->bc_nlevels == 1) {
		int freecount = 0;

		if ((error = xfs_inobt_lookup_ge(cur, 0, 0, 0, &i)))
			goto error0;
		do {
			if ((error = xfs_inobt_get_rec(cur,
					&rec.ir_startino,
					&rec.ir_freecount,
					&rec.ir_free, &i)))
				goto error0;
			if (i) {
				freecount += rec.ir_freecount;
				if ((error = xfs_inobt_increment(cur, 0, &i)))
					goto error0;
			}
		} while (i == 1);
		ASSERT(freecount == be32_to_cpu(agi->agi_freecount) ||
		       XFS_FORCED_SHUTDOWN(mp));
	}
#endif
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	return 0;

error0:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Return the location of the inode in bno/off, for mapping it into a buffer.
 */
/*ARGSUSED*/
int
xfs_dilocate(
	xfs_mount_t	*mp,	/* file system mount structure */
	xfs_trans_t	*tp,	/* transaction pointer */
	xfs_ino_t	ino,	/* inode to locate */
	xfs_fsblock_t	*bno,	/* output: block containing inode */
	int		*len,	/* output: num blocks in inode cluster */
	int		*off,	/* output: index in block of inode */
	uint		flags)	/* flags concerning inode lookup */
{
	xfs_agblock_t	agbno;	/* block number of inode in the alloc group */
	xfs_buf_t	*agbp;	/* agi buffer */
	xfs_agino_t	agino;	/* inode number within alloc group */
	xfs_agnumber_t	agno;	/* allocation group number */
	int		blks_per_cluster; /* num blocks per inode cluster */
	xfs_agblock_t	chunk_agbno;	/* first block in inode chunk */
	xfs_agino_t	chunk_agino;	/* first agino in inode chunk */
	__int32_t	chunk_cnt;	/* count of free inodes in chunk */
	xfs_inofree_t	chunk_free;	/* mask of free inodes in chunk */
	xfs_agblock_t	cluster_agbno;	/* first block in inode cluster */
	xfs_btree_cur_t	*cur;	/* inode btree cursor */
	int		error;	/* error code */
	int		i;	/* temp state */
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
		/* no diagnostics for bulkstat, ino comes from userspace */
		if (flags & XFS_IMAP_BULKSTAT)
			return XFS_ERROR(EINVAL);
		if (agno >= mp->m_sb.sb_agcount) {
			xfs_fs_cmn_err(CE_ALERT, mp,
					"xfs_dilocate: agno (%d) >= "
					"mp->m_sb.sb_agcount (%d)",
					agno,  mp->m_sb.sb_agcount);
		}
		if (agbno >= mp->m_sb.sb_agblocks) {
			xfs_fs_cmn_err(CE_ALERT, mp,
					"xfs_dilocate: agbno (0x%llx) >= "
					"mp->m_sb.sb_agblocks (0x%lx)",
					(unsigned long long) agbno,
					(unsigned long) mp->m_sb.sb_agblocks);
		}
		if (ino != XFS_AGINO_TO_INO(mp, agno, agino)) {
			xfs_fs_cmn_err(CE_ALERT, mp,
					"xfs_dilocate: ino (0x%llx) != "
					"XFS_AGINO_TO_INO(mp, agno, agino) "
					"(0x%llx)",
					ino, XFS_AGINO_TO_INO(mp, agno, agino));
		}
#endif /* DEBUG */
		return XFS_ERROR(EINVAL);
	}
	if ((mp->m_sb.sb_blocksize >= XFS_INODE_CLUSTER_SIZE(mp)) ||
	    !(flags & XFS_IMAP_LOOKUP)) {
		offset = XFS_INO_TO_OFFSET(mp, ino);
		ASSERT(offset < mp->m_sb.sb_inopblock);
		*bno = XFS_AGB_TO_FSB(mp, agno, agbno);
		*off = offset;
		*len = 1;
		return 0;
	}
	blks_per_cluster = XFS_INODE_CLUSTER_SIZE(mp) >> mp->m_sb.sb_blocklog;
	if (*bno != NULLFSBLOCK) {
		offset = XFS_INO_TO_OFFSET(mp, ino);
		ASSERT(offset < mp->m_sb.sb_inopblock);
		cluster_agbno = XFS_FSB_TO_AGBNO(mp, *bno);
		*off = ((agbno - cluster_agbno) * mp->m_sb.sb_inopblock) +
			offset;
		*len = blks_per_cluster;
		return 0;
	}
	if (mp->m_inoalign_mask) {
		offset_agbno = agbno & mp->m_inoalign_mask;
		chunk_agbno = agbno - offset_agbno;
	} else {
		down_read(&mp->m_peraglock);
		error = xfs_ialloc_read_agi(mp, tp, agno, &agbp);
		up_read(&mp->m_peraglock);
		if (error) {
#ifdef DEBUG
			xfs_fs_cmn_err(CE_ALERT, mp, "xfs_dilocate: "
					"xfs_ialloc_read_agi() returned "
					"error %d, agno %d",
					error, agno);
#endif /* DEBUG */
			return error;
		}
		cur = xfs_btree_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_INO,
			(xfs_inode_t *)0, 0);
		if ((error = xfs_inobt_lookup_le(cur, agino, 0, 0, &i))) {
#ifdef DEBUG
			xfs_fs_cmn_err(CE_ALERT, mp, "xfs_dilocate: "
					"xfs_inobt_lookup_le() failed");
#endif /* DEBUG */
			goto error0;
		}
		if ((error = xfs_inobt_get_rec(cur, &chunk_agino, &chunk_cnt,
				&chunk_free, &i))) {
#ifdef DEBUG
			xfs_fs_cmn_err(CE_ALERT, mp, "xfs_dilocate: "
					"xfs_inobt_get_rec() failed");
#endif /* DEBUG */
			goto error0;
		}
		if (i == 0) {
#ifdef DEBUG
			xfs_fs_cmn_err(CE_ALERT, mp, "xfs_dilocate: "
					"xfs_inobt_get_rec() failed");
#endif /* DEBUG */
			error = XFS_ERROR(EINVAL);
		}
		xfs_trans_brelse(tp, agbp);
		xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
		if (error)
			return error;
		chunk_agbno = XFS_AGINO_TO_AGBNO(mp, chunk_agino);
		offset_agbno = agbno - chunk_agbno;
	}
	ASSERT(agbno >= chunk_agbno);
	cluster_agbno = chunk_agbno +
		((offset_agbno / blks_per_cluster) * blks_per_cluster);
	offset = ((agbno - cluster_agbno) * mp->m_sb.sb_inopblock) +
		XFS_INO_TO_OFFSET(mp, ino);
	*bno = XFS_AGB_TO_FSB(mp, agno, cluster_agbno);
	*off = offset;
	*len = blks_per_cluster;
	return 0;
error0:
	xfs_trans_brelse(tp, agbp);
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
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
	ASSERT(be32_to_cpu(agi->agi_magicnum) == XFS_AGI_MAGIC);
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

/*
 * Read in the allocation group header (inode allocation section)
 */
int
xfs_ialloc_read_agi(
	xfs_mount_t	*mp,		/* file system mount structure */
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_agnumber_t	agno,		/* allocation group number */
	xfs_buf_t	**bpp)		/* allocation group hdr buf */
{
	xfs_agi_t	*agi;		/* allocation group header */
	int		agi_ok;		/* agi is consistent */
	xfs_buf_t	*bp;		/* allocation group hdr buf */
	xfs_perag_t	*pag;		/* per allocation group data */
	int		error;

	ASSERT(agno != NULLAGNUMBER);
	error = xfs_trans_read_buf(
			mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, &bp);
	if (error)
		return error;
	ASSERT(bp && !XFS_BUF_GETERROR(bp));

	/*
	 * Validate the magic number of the agi block.
	 */
	agi = XFS_BUF_TO_AGI(bp);
	agi_ok =
		be32_to_cpu(agi->agi_magicnum) == XFS_AGI_MAGIC &&
		XFS_AGI_GOOD_VERSION(be32_to_cpu(agi->agi_versionnum));
	if (unlikely(XFS_TEST_ERROR(!agi_ok, mp, XFS_ERRTAG_IALLOC_READ_AGI,
			XFS_RANDOM_IALLOC_READ_AGI))) {
		XFS_CORRUPTION_ERROR("xfs_ialloc_read_agi", XFS_ERRLEVEL_LOW,
				     mp, agi);
		xfs_trans_brelse(tp, bp);
		return XFS_ERROR(EFSCORRUPTED);
	}
	pag = &mp->m_perag[agno];
	if (!pag->pagi_init) {
		pag->pagi_freecount = be32_to_cpu(agi->agi_freecount);
		pag->pagi_init = 1;
	} else {
		/*
		 * It's possible for these to be out of sync if
		 * we are in the middle of a forced shutdown.
		 */
		ASSERT(pag->pagi_freecount == be32_to_cpu(agi->agi_freecount) ||
			XFS_FORCED_SHUTDOWN(mp));
	}

#ifdef DEBUG
	{
		int	i;

		for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++)
			ASSERT(agi->agi_unlinked[i]);
	}
#endif

	XFS_BUF_SET_VTYPE_REF(bp, B_FS_AGI, XFS_AGI_REF);
	*bpp = bp;
	return 0;
}
