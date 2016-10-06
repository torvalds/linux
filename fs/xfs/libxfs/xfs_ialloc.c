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
#include "xfs_bit.h"
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_rtalloc.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_cksum.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_icreate_item.h"
#include "xfs_icache.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_rmap.h"


/*
 * Allocation group level functions.
 */
static inline int
xfs_ialloc_cluster_alignment(
	struct xfs_mount	*mp)
{
	if (xfs_sb_version_hasalign(&mp->m_sb) &&
	    mp->m_sb.sb_inoalignmt >=
			XFS_B_TO_FSBT(mp, mp->m_inode_cluster_size))
		return mp->m_sb.sb_inoalignmt;
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
	cur->bc_rec.i.ir_holemask = 0;
	cur->bc_rec.i.ir_count = 0;
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
	if (xfs_sb_version_hassparseinodes(&cur->bc_mp->m_sb)) {
		rec.inobt.ir_u.sp.ir_holemask = cpu_to_be16(irec->ir_holemask);
		rec.inobt.ir_u.sp.ir_count = irec->ir_count;
		rec.inobt.ir_u.sp.ir_freecount = irec->ir_freecount;
	} else {
		/* ir_holemask/ir_count not supported on-disk */
		rec.inobt.ir_u.f.ir_freecount = cpu_to_be32(irec->ir_freecount);
	}
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
	if (error || *stat == 0)
		return error;

	irec->ir_startino = be32_to_cpu(rec->inobt.ir_startino);
	if (xfs_sb_version_hassparseinodes(&cur->bc_mp->m_sb)) {
		irec->ir_holemask = be16_to_cpu(rec->inobt.ir_u.sp.ir_holemask);
		irec->ir_count = rec->inobt.ir_u.sp.ir_count;
		irec->ir_freecount = rec->inobt.ir_u.sp.ir_freecount;
	} else {
		/*
		 * ir_holemask/ir_count not supported on-disk. Fill in hardcoded
		 * values for full inode chunks.
		 */
		irec->ir_holemask = XFS_INOBT_HOLEMASK_FULL;
		irec->ir_count = XFS_INODES_PER_CHUNK;
		irec->ir_freecount =
				be32_to_cpu(rec->inobt.ir_u.f.ir_freecount);
	}
	irec->ir_free = be64_to_cpu(rec->inobt.ir_free);

	return 0;
}

/*
 * Insert a single inobt record. Cursor must already point to desired location.
 */
STATIC int
xfs_inobt_insert_rec(
	struct xfs_btree_cur	*cur,
	__uint16_t		holemask,
	__uint8_t		count,
	__int32_t		freecount,
	xfs_inofree_t		free,
	int			*stat)
{
	cur->bc_rec.i.ir_holemask = holemask;
	cur->bc_rec.i.ir_count = count;
	cur->bc_rec.i.ir_freecount = freecount;
	cur->bc_rec.i.ir_free = free;
	return xfs_btree_insert(cur, stat);
}

/*
 * Insert records describing a newly allocated inode chunk into the inobt.
 */
STATIC int
xfs_inobt_insert(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_agino_t		newino,
	xfs_agino_t		newlen,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t		agno = be32_to_cpu(agi->agi_seqno);
	xfs_agino_t		thisino;
	int			i;
	int			error;

	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno, btnum);

	for (thisino = newino;
	     thisino < newino + newlen;
	     thisino += XFS_INODES_PER_CHUNK) {
		error = xfs_inobt_lookup(cur, thisino, XFS_LOOKUP_EQ, &i);
		if (error) {
			xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
			return error;
		}
		ASSERT(i == 0);

		error = xfs_inobt_insert_rec(cur, XFS_INOBT_HOLEMASK_FULL,
					     XFS_INODES_PER_CHUNK,
					     XFS_INODES_PER_CHUNK,
					     XFS_INOBT_ALL_FREE, &i);
		if (error) {
			xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
			return error;
		}
		ASSERT(i == 1);
	}

	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);

	return 0;
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
 * Initialise a new set of inodes. When called without a transaction context
 * (e.g. from recovery) we initiate a delayed write of the inode buffers rather
 * than logging them (which in a transaction context puts them into the AIL
 * for writeback rather than the xfsbufd queue).
 */
int
xfs_ialloc_inode_init(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct list_head	*buffer_list,
	int			icount,
	xfs_agnumber_t		agno,
	xfs_agblock_t		agbno,
	xfs_agblock_t		length,
	unsigned int		gen)
{
	struct xfs_buf		*fbuf;
	struct xfs_dinode	*free;
	int			nbufs, blks_per_cluster, inodes_per_cluster;
	int			version;
	int			i, j;
	xfs_daddr_t		d;
	xfs_ino_t		ino = 0;

	/*
	 * Loop over the new block(s), filling in the inodes.  For small block
	 * sizes, manipulate the inodes in buffers  which are multiples of the
	 * blocks size.
	 */
	blks_per_cluster = xfs_icluster_size_fsb(mp);
	inodes_per_cluster = blks_per_cluster << mp->m_sb.sb_inopblog;
	nbufs = length / blks_per_cluster;

	/*
	 * Figure out what version number to use in the inodes we create.  If
	 * the superblock version has caught up to the one that supports the new
	 * inode format, then use the new inode version.  Otherwise use the old
	 * version so that old kernels will continue to be able to use the file
	 * system.
	 *
	 * For v3 inodes, we also need to write the inode number into the inode,
	 * so calculate the first inode number of the chunk here as
	 * XFS_OFFBNO_TO_AGINO() only works within a filesystem block, not
	 * across multiple filesystem blocks (such as a cluster) and so cannot
	 * be used in the cluster buffer loop below.
	 *
	 * Further, because we are writing the inode directly into the buffer
	 * and calculating a CRC on the entire inode, we have ot log the entire
	 * inode so that the entire range the CRC covers is present in the log.
	 * That means for v3 inode we log the entire buffer rather than just the
	 * inode cores.
	 */
	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		version = 3;
		ino = XFS_AGINO_TO_INO(mp, agno,
				       XFS_OFFBNO_TO_AGINO(mp, agbno, 0));

		/*
		 * log the initialisation that is about to take place as an
		 * logical operation. This means the transaction does not
		 * need to log the physical changes to the inode buffers as log
		 * recovery will know what initialisation is actually needed.
		 * Hence we only need to log the buffers as "ordered" buffers so
		 * they track in the AIL as if they were physically logged.
		 */
		if (tp)
			xfs_icreate_log(tp, agno, agbno, icount,
					mp->m_sb.sb_inodesize, length, gen);
	} else
		version = 2;

	for (j = 0; j < nbufs; j++) {
		/*
		 * Get the block.
		 */
		d = XFS_AGB_TO_DADDR(mp, agno, agbno + (j * blks_per_cluster));
		fbuf = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
					 mp->m_bsize * blks_per_cluster,
					 XBF_UNMAPPED);
		if (!fbuf)
			return -ENOMEM;

		/* Initialize the inode buffers and log them appropriately. */
		fbuf->b_ops = &xfs_inode_buf_ops;
		xfs_buf_zero(fbuf, 0, BBTOB(fbuf->b_length));
		for (i = 0; i < inodes_per_cluster; i++) {
			int	ioffset = i << mp->m_sb.sb_inodelog;
			uint	isize = xfs_dinode_size(version);

			free = xfs_make_iptr(mp, fbuf, i);
			free->di_magic = cpu_to_be16(XFS_DINODE_MAGIC);
			free->di_version = version;
			free->di_gen = cpu_to_be32(gen);
			free->di_next_unlinked = cpu_to_be32(NULLAGINO);

			if (version == 3) {
				free->di_ino = cpu_to_be64(ino);
				ino++;
				uuid_copy(&free->di_uuid,
					  &mp->m_sb.sb_meta_uuid);
				xfs_dinode_calc_crc(mp, free);
			} else if (tp) {
				/* just log the inode core */
				xfs_trans_log_buf(tp, fbuf, ioffset,
						  ioffset + isize - 1);
			}
		}

		if (tp) {
			/*
			 * Mark the buffer as an inode allocation buffer so it
			 * sticks in AIL at the point of this allocation
			 * transaction. This ensures the they are on disk before
			 * the tail of the log can be moved past this
			 * transaction (i.e. by preventing relogging from moving
			 * it forward in the log).
			 */
			xfs_trans_inode_alloc_buf(tp, fbuf);
			if (version == 3) {
				/*
				 * Mark the buffer as ordered so that they are
				 * not physically logged in the transaction but
				 * still tracked in the AIL as part of the
				 * transaction and pin the log appropriately.
				 */
				xfs_trans_ordered_buf(tp, fbuf);
				xfs_trans_log_buf(tp, fbuf, 0,
						  BBTOB(fbuf->b_length) - 1);
			}
		} else {
			fbuf->b_flags |= XBF_DONE;
			xfs_buf_delwri_queue(fbuf, buffer_list);
			xfs_buf_relse(fbuf);
		}
	}
	return 0;
}

/*
 * Align startino and allocmask for a recently allocated sparse chunk such that
 * they are fit for insertion (or merge) into the on-disk inode btrees.
 *
 * Background:
 *
 * When enabled, sparse inode support increases the inode alignment from cluster
 * size to inode chunk size. This means that the minimum range between two
 * non-adjacent inode records in the inobt is large enough for a full inode
 * record. This allows for cluster sized, cluster aligned block allocation
 * without need to worry about whether the resulting inode record overlaps with
 * another record in the tree. Without this basic rule, we would have to deal
 * with the consequences of overlap by potentially undoing recent allocations in
 * the inode allocation codepath.
 *
 * Because of this alignment rule (which is enforced on mount), there are two
 * inobt possibilities for newly allocated sparse chunks. One is that the
 * aligned inode record for the chunk covers a range of inodes not already
 * covered in the inobt (i.e., it is safe to insert a new sparse record). The
 * other is that a record already exists at the aligned startino that considers
 * the newly allocated range as sparse. In the latter case, record content is
 * merged in hope that sparse inode chunks fill to full chunks over time.
 */
STATIC void
xfs_align_sparse_ino(
	struct xfs_mount		*mp,
	xfs_agino_t			*startino,
	uint16_t			*allocmask)
{
	xfs_agblock_t			agbno;
	xfs_agblock_t			mod;
	int				offset;

	agbno = XFS_AGINO_TO_AGBNO(mp, *startino);
	mod = agbno % mp->m_sb.sb_inoalignmt;
	if (!mod)
		return;

	/* calculate the inode offset and align startino */
	offset = mod << mp->m_sb.sb_inopblog;
	*startino -= offset;

	/*
	 * Since startino has been aligned down, left shift allocmask such that
	 * it continues to represent the same physical inodes relative to the
	 * new startino.
	 */
	*allocmask <<= offset / XFS_INODES_PER_HOLEMASK_BIT;
}

/*
 * Determine whether the source inode record can merge into the target. Both
 * records must be sparse, the inode ranges must match and there must be no
 * allocation overlap between the records.
 */
STATIC bool
__xfs_inobt_can_merge(
	struct xfs_inobt_rec_incore	*trec,	/* tgt record */
	struct xfs_inobt_rec_incore	*srec)	/* src record */
{
	uint64_t			talloc;
	uint64_t			salloc;

	/* records must cover the same inode range */
	if (trec->ir_startino != srec->ir_startino)
		return false;

	/* both records must be sparse */
	if (!xfs_inobt_issparse(trec->ir_holemask) ||
	    !xfs_inobt_issparse(srec->ir_holemask))
		return false;

	/* both records must track some inodes */
	if (!trec->ir_count || !srec->ir_count)
		return false;

	/* can't exceed capacity of a full record */
	if (trec->ir_count + srec->ir_count > XFS_INODES_PER_CHUNK)
		return false;

	/* verify there is no allocation overlap */
	talloc = xfs_inobt_irec_to_allocmask(trec);
	salloc = xfs_inobt_irec_to_allocmask(srec);
	if (talloc & salloc)
		return false;

	return true;
}

/*
 * Merge the source inode record into the target. The caller must call
 * __xfs_inobt_can_merge() to ensure the merge is valid.
 */
STATIC void
__xfs_inobt_rec_merge(
	struct xfs_inobt_rec_incore	*trec,	/* target */
	struct xfs_inobt_rec_incore	*srec)	/* src */
{
	ASSERT(trec->ir_startino == srec->ir_startino);

	/* combine the counts */
	trec->ir_count += srec->ir_count;
	trec->ir_freecount += srec->ir_freecount;

	/*
	 * Merge the holemask and free mask. For both fields, 0 bits refer to
	 * allocated inodes. We combine the allocated ranges with bitwise AND.
	 */
	trec->ir_holemask &= srec->ir_holemask;
	trec->ir_free &= srec->ir_free;
}

/*
 * Insert a new sparse inode chunk into the associated inode btree. The inode
 * record for the sparse chunk is pre-aligned to a startino that should match
 * any pre-existing sparse inode record in the tree. This allows sparse chunks
 * to fill over time.
 *
 * This function supports two modes of handling preexisting records depending on
 * the merge flag. If merge is true, the provided record is merged with the
 * existing record and updated in place. The merged record is returned in nrec.
 * If merge is false, an existing record is replaced with the provided record.
 * If no preexisting record exists, the provided record is always inserted.
 *
 * It is considered corruption if a merge is requested and not possible. Given
 * the sparse inode alignment constraints, this should never happen.
 */
STATIC int
xfs_inobt_insert_sprec(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	int				btnum,
	struct xfs_inobt_rec_incore	*nrec,	/* in/out: new/merged rec. */
	bool				merge)	/* merge or replace */
{
	struct xfs_btree_cur		*cur;
	struct xfs_agi			*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t			agno = be32_to_cpu(agi->agi_seqno);
	int				error;
	int				i;
	struct xfs_inobt_rec_incore	rec;

	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno, btnum);

	/* the new record is pre-aligned so we know where to look */
	error = xfs_inobt_lookup(cur, nrec->ir_startino, XFS_LOOKUP_EQ, &i);
	if (error)
		goto error;
	/* if nothing there, insert a new record and return */
	if (i == 0) {
		error = xfs_inobt_insert_rec(cur, nrec->ir_holemask,
					     nrec->ir_count, nrec->ir_freecount,
					     nrec->ir_free, &i);
		if (error)
			goto error;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error);

		goto out;
	}

	/*
	 * A record exists at this startino. Merge or replace the record
	 * depending on what we've been asked to do.
	 */
	if (merge) {
		error = xfs_inobt_get_rec(cur, &rec, &i);
		if (error)
			goto error;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error);
		XFS_WANT_CORRUPTED_GOTO(mp,
					rec.ir_startino == nrec->ir_startino,
					error);

		/*
		 * This should never fail. If we have coexisting records that
		 * cannot merge, something is seriously wrong.
		 */
		XFS_WANT_CORRUPTED_GOTO(mp, __xfs_inobt_can_merge(nrec, &rec),
					error);

		trace_xfs_irec_merge_pre(mp, agno, rec.ir_startino,
					 rec.ir_holemask, nrec->ir_startino,
					 nrec->ir_holemask);

		/* merge to nrec to output the updated record */
		__xfs_inobt_rec_merge(nrec, &rec);

		trace_xfs_irec_merge_post(mp, agno, nrec->ir_startino,
					  nrec->ir_holemask);

		error = xfs_inobt_rec_check_count(mp, nrec);
		if (error)
			goto error;
	}

	error = xfs_inobt_update(cur, nrec);
	if (error)
		goto error;

out:
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	return 0;
error:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
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
	xfs_agnumber_t	agno;
	int		error;
	xfs_agino_t	newino;		/* new first inode's number */
	xfs_agino_t	newlen;		/* new number of inodes */
	int		isaligned = 0;	/* inode allocation at stripe unit */
					/* boundary */
	uint16_t	allocmask = (uint16_t) -1; /* init. to full chunk */
	struct xfs_inobt_rec_incore rec;
	struct xfs_perag *pag;
	int		do_sparse = 0;

	memset(&args, 0, sizeof(args));
	args.tp = tp;
	args.mp = tp->t_mountp;
	args.fsbno = NULLFSBLOCK;
	xfs_rmap_ag_owner(&args.oinfo, XFS_RMAP_OWN_INODES);

#ifdef DEBUG
	/* randomly do sparse inode allocations */
	if (xfs_sb_version_hassparseinodes(&tp->t_mountp->m_sb) &&
	    args.mp->m_ialloc_min_blks < args.mp->m_ialloc_blks)
		do_sparse = prandom_u32() & 1;
#endif

	/*
	 * Locking will ensure that we don't have two callers in here
	 * at one time.
	 */
	newlen = args.mp->m_ialloc_inos;
	if (args.mp->m_maxicount &&
	    percpu_counter_read_positive(&args.mp->m_icount) + newlen >
							args.mp->m_maxicount)
		return -ENOSPC;
	args.minlen = args.maxlen = args.mp->m_ialloc_blks;
	/*
	 * First try to allocate inodes contiguous with the last-allocated
	 * chunk of inodes.  If the filesystem is striped, this will fill
	 * an entire stripe unit with inodes.
	 */
	agi = XFS_BUF_TO_AGI(agbp);
	newino = be32_to_cpu(agi->agi_newino);
	agno = be32_to_cpu(agi->agi_seqno);
	args.agbno = XFS_AGINO_TO_AGBNO(args.mp, newino) +
		     args.mp->m_ialloc_blks;
	if (do_sparse)
		goto sparse_alloc;
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
		args.minalignslop = xfs_ialloc_cluster_alignment(args.mp) - 1;

		/* Allow space for the inode btree to split. */
		args.minleft = args.mp->m_in_maxlevels - 1;
		if ((error = xfs_alloc_vextent(&args)))
			return error;

		/*
		 * This request might have dirtied the transaction if the AG can
		 * satisfy the request, but the exact block was not available.
		 * If the allocation did fail, subsequent requests will relax
		 * the exact agbno requirement and increase the alignment
		 * instead. It is critical that the total size of the request
		 * (len + alignment + slop) does not increase from this point
		 * on, so reset minalignslop to ensure it is not included in
		 * subsequent requests.
		 */
		args.minalignslop = 0;
	}

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
			args.alignment = xfs_ialloc_cluster_alignment(args.mp);
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
		args.alignment = xfs_ialloc_cluster_alignment(args.mp);
		if ((error = xfs_alloc_vextent(&args)))
			return error;
	}

	/*
	 * Finally, try a sparse allocation if the filesystem supports it and
	 * the sparse allocation length is smaller than a full chunk.
	 */
	if (xfs_sb_version_hassparseinodes(&args.mp->m_sb) &&
	    args.mp->m_ialloc_min_blks < args.mp->m_ialloc_blks &&
	    args.fsbno == NULLFSBLOCK) {
sparse_alloc:
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.agbno = be32_to_cpu(agi->agi_root);
		args.fsbno = XFS_AGB_TO_FSB(args.mp, agno, args.agbno);
		args.alignment = args.mp->m_sb.sb_spino_align;
		args.prod = 1;

		args.minlen = args.mp->m_ialloc_min_blks;
		args.maxlen = args.minlen;

		/*
		 * The inode record will be aligned to full chunk size. We must
		 * prevent sparse allocation from AG boundaries that result in
		 * invalid inode records, such as records that start at agbno 0
		 * or extend beyond the AG.
		 *
		 * Set min agbno to the first aligned, non-zero agbno and max to
		 * the last aligned agbno that is at least one full chunk from
		 * the end of the AG.
		 */
		args.min_agbno = args.mp->m_sb.sb_inoalignmt;
		args.max_agbno = round_down(args.mp->m_sb.sb_agblocks,
					    args.mp->m_sb.sb_inoalignmt) -
				 args.mp->m_ialloc_blks;

		error = xfs_alloc_vextent(&args);
		if (error)
			return error;

		newlen = args.len << args.mp->m_sb.sb_inopblog;
		ASSERT(newlen <= XFS_INODES_PER_CHUNK);
		allocmask = (1 << (newlen / XFS_INODES_PER_HOLEMASK_BIT)) - 1;
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
	error = xfs_ialloc_inode_init(args.mp, tp, NULL, newlen, agno,
			args.agbno, args.len, prandom_u32());

	if (error)
		return error;
	/*
	 * Convert the results.
	 */
	newino = XFS_OFFBNO_TO_AGINO(args.mp, args.agbno, 0);

	if (xfs_inobt_issparse(~allocmask)) {
		/*
		 * We've allocated a sparse chunk. Align the startino and mask.
		 */
		xfs_align_sparse_ino(args.mp, &newino, &allocmask);

		rec.ir_startino = newino;
		rec.ir_holemask = ~allocmask;
		rec.ir_count = newlen;
		rec.ir_freecount = newlen;
		rec.ir_free = XFS_INOBT_ALL_FREE;

		/*
		 * Insert the sparse record into the inobt and allow for a merge
		 * if necessary. If a merge does occur, rec is updated to the
		 * merged record.
		 */
		error = xfs_inobt_insert_sprec(args.mp, tp, agbp, XFS_BTNUM_INO,
					       &rec, true);
		if (error == -EFSCORRUPTED) {
			xfs_alert(args.mp,
	"invalid sparse inode record: ino 0x%llx holemask 0x%x count %u",
				  XFS_AGINO_TO_INO(args.mp, agno,
						   rec.ir_startino),
				  rec.ir_holemask, rec.ir_count);
			xfs_force_shutdown(args.mp, SHUTDOWN_CORRUPT_INCORE);
		}
		if (error)
			return error;

		/*
		 * We can't merge the part we've just allocated as for the inobt
		 * due to finobt semantics. The original record may or may not
		 * exist independent of whether physical inodes exist in this
		 * sparse chunk.
		 *
		 * We must update the finobt record based on the inobt record.
		 * rec contains the fully merged and up to date inobt record
		 * from the previous call. Set merge false to replace any
		 * existing record with this one.
		 */
		if (xfs_sb_version_hasfinobt(&args.mp->m_sb)) {
			error = xfs_inobt_insert_sprec(args.mp, tp, agbp,
						       XFS_BTNUM_FINO, &rec,
						       false);
			if (error)
				return error;
		}
	} else {
		/* full chunk - insert new records to both btrees */
		error = xfs_inobt_insert(args.mp, tp, agbp, newino, newlen,
					 XFS_BTNUM_INO);
		if (error)
			return error;

		if (xfs_sb_version_hasfinobt(&args.mp->m_sb)) {
			error = xfs_inobt_insert(args.mp, tp, agbp, newino,
						 newlen, XFS_BTNUM_FINO);
			if (error)
				return error;
		}
	}

	/*
	 * Update AGI counts and newino.
	 */
	be32_add_cpu(&agi->agi_count, newlen);
	be32_add_cpu(&agi->agi_freecount, newlen);
	pag = xfs_perag_get(args.mp, agno);
	pag->pagi_freecount += newlen;
	xfs_perag_put(pag);
	agi->agi_newino = cpu_to_be32(newino);

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
 * inode and the mode.  Return the allocation group buffer.
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
		 * Check that there is enough free space for the file plus a
		 * chunk of inodes if we need to allocate some. If this is the
		 * first pass across the AGs, take into account the potential
		 * space needed for alignment of inode chunks when checking the
		 * longest contiguous free space in the AG - this prevents us
		 * from getting ENOSPC because we have free space larger than
		 * m_ialloc_blks but alignment constraints prevent us from using
		 * it.
		 *
		 * If we can't find an AG with space for full alignment slack to
		 * be taken into account, we must be near ENOSPC in all AGs.
		 * Hence we don't include alignment for the second pass and so
		 * if we fail allocation due to alignment issues then it is most
		 * likely a real ENOSPC condition.
		 */
		ineed = mp->m_ialloc_min_blks;
		if (flags && ineed > 1)
			ineed += xfs_ialloc_cluster_alignment(mp);
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
		XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, i == 1);
	}

	return 0;
}

STATIC int
xfs_ialloc_get_rec(
	struct xfs_btree_cur	*cur,
	xfs_agino_t		agino,
	xfs_inobt_rec_incore_t	*rec,
	int			*done)
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
		XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, i == 1);
	}

	return 0;
}

/*
 * Return the offset of the first free inode in the record. If the inode chunk
 * is sparsely allocated, we convert the record holemask to inode granularity
 * and mask off the unallocated regions from the inode free mask.
 */
STATIC int
xfs_inobt_first_free_inode(
	struct xfs_inobt_rec_incore	*rec)
{
	xfs_inofree_t			realfree;

	/* if there are no holes, return the first available offset */
	if (!xfs_inobt_issparse(rec->ir_holemask))
		return xfs_lowbit64(rec->ir_free);

	realfree = xfs_inobt_irec_to_allocmask(rec);
	realfree &= rec->ir_free;

	return xfs_lowbit64(realfree);
}

/*
 * Allocate an inode using the inobt-only algorithm.
 */
STATIC int
xfs_dialloc_ag_inobt(
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
	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_INO);
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
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);

		error = xfs_inobt_get_rec(cur, &rec, &j);
		if (error)
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, j == 1, error0);

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
						   &trec, &doneleft);
			if (error)
				goto error1;

			error = xfs_ialloc_get_rec(cur, pag->pagl_rightrec,
						   &rec, &doneright);
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
	XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);

	for (;;) {
		error = xfs_inobt_get_rec(cur, &rec, &i);
		if (error)
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
		if (rec.ir_freecount > 0)
			break;
		error = xfs_btree_increment(cur, 0, &i);
		if (error)
			goto error0;
		XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
	}

alloc_inode:
	offset = xfs_inobt_first_free_inode(&rec);
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
 * Use the free inode btree to allocate an inode based on distance from the
 * parent. Note that the provided cursor may be deleted and replaced.
 */
STATIC int
xfs_dialloc_ag_finobt_near(
	xfs_agino_t			pagino,
	struct xfs_btree_cur		**ocur,
	struct xfs_inobt_rec_incore	*rec)
{
	struct xfs_btree_cur		*lcur = *ocur;	/* left search cursor */
	struct xfs_btree_cur		*rcur;	/* right search cursor */
	struct xfs_inobt_rec_incore	rrec;
	int				error;
	int				i, j;

	error = xfs_inobt_lookup(lcur, pagino, XFS_LOOKUP_LE, &i);
	if (error)
		return error;

	if (i == 1) {
		error = xfs_inobt_get_rec(lcur, rec, &i);
		if (error)
			return error;
		XFS_WANT_CORRUPTED_RETURN(lcur->bc_mp, i == 1);

		/*
		 * See if we've landed in the parent inode record. The finobt
		 * only tracks chunks with at least one free inode, so record
		 * existence is enough.
		 */
		if (pagino >= rec->ir_startino &&
		    pagino < (rec->ir_startino + XFS_INODES_PER_CHUNK))
			return 0;
	}

	error = xfs_btree_dup_cursor(lcur, &rcur);
	if (error)
		return error;

	error = xfs_inobt_lookup(rcur, pagino, XFS_LOOKUP_GE, &j);
	if (error)
		goto error_rcur;
	if (j == 1) {
		error = xfs_inobt_get_rec(rcur, &rrec, &j);
		if (error)
			goto error_rcur;
		XFS_WANT_CORRUPTED_GOTO(lcur->bc_mp, j == 1, error_rcur);
	}

	XFS_WANT_CORRUPTED_GOTO(lcur->bc_mp, i == 1 || j == 1, error_rcur);
	if (i == 1 && j == 1) {
		/*
		 * Both the left and right records are valid. Choose the closer
		 * inode chunk to the target.
		 */
		if ((pagino - rec->ir_startino + XFS_INODES_PER_CHUNK - 1) >
		    (rrec.ir_startino - pagino)) {
			*rec = rrec;
			xfs_btree_del_cursor(lcur, XFS_BTREE_NOERROR);
			*ocur = rcur;
		} else {
			xfs_btree_del_cursor(rcur, XFS_BTREE_NOERROR);
		}
	} else if (j == 1) {
		/* only the right record is valid */
		*rec = rrec;
		xfs_btree_del_cursor(lcur, XFS_BTREE_NOERROR);
		*ocur = rcur;
	} else if (i == 1) {
		/* only the left record is valid */
		xfs_btree_del_cursor(rcur, XFS_BTREE_NOERROR);
	}

	return 0;

error_rcur:
	xfs_btree_del_cursor(rcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Use the free inode btree to find a free inode based on a newino hint. If
 * the hint is NULL, find the first free inode in the AG.
 */
STATIC int
xfs_dialloc_ag_finobt_newino(
	struct xfs_agi			*agi,
	struct xfs_btree_cur		*cur,
	struct xfs_inobt_rec_incore	*rec)
{
	int error;
	int i;

	if (agi->agi_newino != cpu_to_be32(NULLAGINO)) {
		error = xfs_inobt_lookup(cur, be32_to_cpu(agi->agi_newino),
					 XFS_LOOKUP_EQ, &i);
		if (error)
			return error;
		if (i == 1) {
			error = xfs_inobt_get_rec(cur, rec, &i);
			if (error)
				return error;
			XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, i == 1);
			return 0;
		}
	}

	/*
	 * Find the first inode available in the AG.
	 */
	error = xfs_inobt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
	if (error)
		return error;
	XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, i == 1);

	error = xfs_inobt_get_rec(cur, rec, &i);
	if (error)
		return error;
	XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, i == 1);

	return 0;
}

/*
 * Update the inobt based on a modification made to the finobt. Also ensure that
 * the records from both trees are equivalent post-modification.
 */
STATIC int
xfs_dialloc_ag_update_inobt(
	struct xfs_btree_cur		*cur,	/* inobt cursor */
	struct xfs_inobt_rec_incore	*frec,	/* finobt record */
	int				offset) /* inode offset */
{
	struct xfs_inobt_rec_incore	rec;
	int				error;
	int				i;

	error = xfs_inobt_lookup(cur, frec->ir_startino, XFS_LOOKUP_EQ, &i);
	if (error)
		return error;
	XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, i == 1);

	error = xfs_inobt_get_rec(cur, &rec, &i);
	if (error)
		return error;
	XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, i == 1);
	ASSERT((XFS_AGINO_TO_OFFSET(cur->bc_mp, rec.ir_startino) %
				   XFS_INODES_PER_CHUNK) == 0);

	rec.ir_free &= ~XFS_INOBT_MASK(offset);
	rec.ir_freecount--;

	XFS_WANT_CORRUPTED_RETURN(cur->bc_mp, (rec.ir_free == frec->ir_free) &&
				  (rec.ir_freecount == frec->ir_freecount));

	return xfs_inobt_update(cur, &rec);
}

/*
 * Allocate an inode using the free inode btree, if available. Otherwise, fall
 * back to the inobt search algorithm.
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
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_agi			*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t			agno = be32_to_cpu(agi->agi_seqno);
	xfs_agnumber_t			pagno = XFS_INO_TO_AGNO(mp, parent);
	xfs_agino_t			pagino = XFS_INO_TO_AGINO(mp, parent);
	struct xfs_perag		*pag;
	struct xfs_btree_cur		*cur;	/* finobt cursor */
	struct xfs_btree_cur		*icur;	/* inobt cursor */
	struct xfs_inobt_rec_incore	rec;
	xfs_ino_t			ino;
	int				error;
	int				offset;
	int				i;

	if (!xfs_sb_version_hasfinobt(&mp->m_sb))
		return xfs_dialloc_ag_inobt(tp, agbp, parent, inop);

	pag = xfs_perag_get(mp, agno);

	/*
	 * If pagino is 0 (this is the root inode allocation) use newino.
	 * This must work because we've just allocated some.
	 */
	if (!pagino)
		pagino = be32_to_cpu(agi->agi_newino);

	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_FINO);

	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error_cur;

	/*
	 * The search algorithm depends on whether we're in the same AG as the
	 * parent. If so, find the closest available inode to the parent. If
	 * not, consider the agi hint or find the first free inode in the AG.
	 */
	if (agno == pagno)
		error = xfs_dialloc_ag_finobt_near(pagino, &cur, &rec);
	else
		error = xfs_dialloc_ag_finobt_newino(agi, cur, &rec);
	if (error)
		goto error_cur;

	offset = xfs_inobt_first_free_inode(&rec);
	ASSERT(offset >= 0);
	ASSERT(offset < XFS_INODES_PER_CHUNK);
	ASSERT((XFS_AGINO_TO_OFFSET(mp, rec.ir_startino) %
				   XFS_INODES_PER_CHUNK) == 0);
	ino = XFS_AGINO_TO_INO(mp, agno, rec.ir_startino + offset);

	/*
	 * Modify or remove the finobt record.
	 */
	rec.ir_free &= ~XFS_INOBT_MASK(offset);
	rec.ir_freecount--;
	if (rec.ir_freecount)
		error = xfs_inobt_update(cur, &rec);
	else
		error = xfs_btree_delete(cur, &i);
	if (error)
		goto error_cur;

	/*
	 * The finobt has now been updated appropriately. We haven't updated the
	 * agi and superblock yet, so we can create an inobt cursor and validate
	 * the original freecount. If all is well, make the equivalent update to
	 * the inobt using the finobt record and offset information.
	 */
	icur = xfs_inobt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_INO);

	error = xfs_check_agi_freecount(icur, agi);
	if (error)
		goto error_icur;

	error = xfs_dialloc_ag_update_inobt(icur, &rec, offset);
	if (error)
		goto error_icur;

	/*
	 * Both trees have now been updated. We must update the perag and
	 * superblock before we can check the freecount for each btree.
	 */
	be32_add_cpu(&agi->agi_freecount, -1);
	xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
	pag->pagi_freecount--;

	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -1);

	error = xfs_check_agi_freecount(icur, agi);
	if (error)
		goto error_icur;
	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error_icur;

	xfs_btree_del_cursor(icur, XFS_BTREE_NOERROR);
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	xfs_perag_put(pag);
	*inop = ino;
	return 0;

error_icur:
	xfs_btree_del_cursor(icur, XFS_BTREE_ERROR);
error_cur:
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
	 *
	 * Read rough value of mp->m_icount by percpu_counter_read_positive,
	 * which will sacrifice the preciseness but improve the performance.
	 */
	if (mp->m_maxicount &&
	    percpu_counter_read_positive(&mp->m_icount) + mp->m_ialloc_inos
							> mp->m_maxicount) {
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

			if (error != -ENOSPC)
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
			return noroom ? -ENOSPC : 0;
		}
	}

out_alloc:
	*IO_agbp = NULL;
	return xfs_dialloc_ag(tp, agbp, parent, inop);
out_error:
	xfs_perag_put(pag);
	return error;
}

/*
 * Free the blocks of an inode chunk. We must consider that the inode chunk
 * might be sparse and only free the regions that are allocated as part of the
 * chunk.
 */
STATIC void
xfs_difree_inode_chunk(
	struct xfs_mount		*mp,
	xfs_agnumber_t			agno,
	struct xfs_inobt_rec_incore	*rec,
	struct xfs_defer_ops		*dfops)
{
	xfs_agblock_t	sagbno = XFS_AGINO_TO_AGBNO(mp, rec->ir_startino);
	int		startidx, endidx;
	int		nextbit;
	xfs_agblock_t	agbno;
	int		contigblk;
	struct xfs_owner_info	oinfo;
	DECLARE_BITMAP(holemask, XFS_INOBT_HOLEMASK_BITS);
	xfs_rmap_ag_owner(&oinfo, XFS_RMAP_OWN_INODES);

	if (!xfs_inobt_issparse(rec->ir_holemask)) {
		/* not sparse, calculate extent info directly */
		xfs_bmap_add_free(mp, dfops, XFS_AGB_TO_FSB(mp, agno, sagbno),
				  mp->m_ialloc_blks, &oinfo);
		return;
	}

	/* holemask is only 16-bits (fits in an unsigned long) */
	ASSERT(sizeof(rec->ir_holemask) <= sizeof(holemask[0]));
	holemask[0] = rec->ir_holemask;

	/*
	 * Find contiguous ranges of zeroes (i.e., allocated regions) in the
	 * holemask and convert the start/end index of each range to an extent.
	 * We start with the start and end index both pointing at the first 0 in
	 * the mask.
	 */
	startidx = endidx = find_first_zero_bit(holemask,
						XFS_INOBT_HOLEMASK_BITS);
	nextbit = startidx + 1;
	while (startidx < XFS_INOBT_HOLEMASK_BITS) {
		nextbit = find_next_zero_bit(holemask, XFS_INOBT_HOLEMASK_BITS,
					     nextbit);
		/*
		 * If the next zero bit is contiguous, update the end index of
		 * the current range and continue.
		 */
		if (nextbit != XFS_INOBT_HOLEMASK_BITS &&
		    nextbit == endidx + 1) {
			endidx = nextbit;
			goto next;
		}

		/*
		 * nextbit is not contiguous with the current end index. Convert
		 * the current start/end to an extent and add it to the free
		 * list.
		 */
		agbno = sagbno + (startidx * XFS_INODES_PER_HOLEMASK_BIT) /
				  mp->m_sb.sb_inopblock;
		contigblk = ((endidx - startidx + 1) *
			     XFS_INODES_PER_HOLEMASK_BIT) /
			    mp->m_sb.sb_inopblock;

		ASSERT(agbno % mp->m_sb.sb_spino_align == 0);
		ASSERT(contigblk % mp->m_sb.sb_spino_align == 0);
		xfs_bmap_add_free(mp, dfops, XFS_AGB_TO_FSB(mp, agno, agbno),
				  contigblk, &oinfo);

		/* reset range to current bit and carry on... */
		startidx = endidx = nextbit;

next:
		nextbit++;
	}
}

STATIC int
xfs_difree_inobt(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	xfs_agino_t			agino,
	struct xfs_defer_ops		*dfops,
	struct xfs_icluster		*xic,
	struct xfs_inobt_rec_incore	*orec)
{
	struct xfs_agi			*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t			agno = be32_to_cpu(agi->agi_seqno);
	struct xfs_perag		*pag;
	struct xfs_btree_cur		*cur;
	struct xfs_inobt_rec_incore	rec;
	int				ilen;
	int				error;
	int				i;
	int				off;

	ASSERT(agi->agi_magicnum == cpu_to_be32(XFS_AGI_MAGIC));
	ASSERT(XFS_AGINO_TO_AGBNO(mp, agino) < be32_to_cpu(agi->agi_length));

	/*
	 * Initialize the cursor.
	 */
	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_INO);

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
	XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
	error = xfs_inobt_get_rec(cur, &rec, &i);
	if (error) {
		xfs_warn(mp, "%s: xfs_inobt_get_rec() returned error %d.",
			__func__, error);
		goto error0;
	}
	XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error0);
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
	 * When an inode chunk is free, it becomes eligible for removal. Don't
	 * remove the chunk if the block size is large enough for multiple inode
	 * chunks (that might not be free).
	 */
	if (!(mp->m_flags & XFS_MOUNT_IKEEP) &&
	    rec.ir_free == XFS_INOBT_ALL_FREE &&
	    mp->m_sb.sb_inopblock <= XFS_INODES_PER_CHUNK) {
		xic->deleted = 1;
		xic->first_ino = XFS_AGINO_TO_INO(mp, agno, rec.ir_startino);
		xic->alloc = xfs_inobt_irec_to_allocmask(&rec);

		/*
		 * Remove the inode cluster from the AGI B+Tree, adjust the
		 * AGI and Superblock inode counts, and mark the disk space
		 * to be freed when the transaction is committed.
		 */
		ilen = rec.ir_freecount;
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

		xfs_difree_inode_chunk(mp, agno, &rec, dfops);
	} else {
		xic->deleted = 0;

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

	*orec = rec;
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	return 0;

error0:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Free an inode in the free inode btree.
 */
STATIC int
xfs_difree_finobt(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	xfs_agino_t			agino,
	struct xfs_inobt_rec_incore	*ibtrec) /* inobt record */
{
	struct xfs_agi			*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t			agno = be32_to_cpu(agi->agi_seqno);
	struct xfs_btree_cur		*cur;
	struct xfs_inobt_rec_incore	rec;
	int				offset = agino - ibtrec->ir_startino;
	int				error;
	int				i;

	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_FINO);

	error = xfs_inobt_lookup(cur, ibtrec->ir_startino, XFS_LOOKUP_EQ, &i);
	if (error)
		goto error;
	if (i == 0) {
		/*
		 * If the record does not exist in the finobt, we must have just
		 * freed an inode in a previously fully allocated chunk. If not,
		 * something is out of sync.
		 */
		XFS_WANT_CORRUPTED_GOTO(mp, ibtrec->ir_freecount == 1, error);

		error = xfs_inobt_insert_rec(cur, ibtrec->ir_holemask,
					     ibtrec->ir_count,
					     ibtrec->ir_freecount,
					     ibtrec->ir_free, &i);
		if (error)
			goto error;
		ASSERT(i == 1);

		goto out;
	}

	/*
	 * Read and update the existing record. We could just copy the ibtrec
	 * across here, but that would defeat the purpose of having redundant
	 * metadata. By making the modifications independently, we can catch
	 * corruptions that we wouldn't see if we just copied from one record
	 * to another.
	 */
	error = xfs_inobt_get_rec(cur, &rec, &i);
	if (error)
		goto error;
	XFS_WANT_CORRUPTED_GOTO(mp, i == 1, error);

	rec.ir_free |= XFS_INOBT_MASK(offset);
	rec.ir_freecount++;

	XFS_WANT_CORRUPTED_GOTO(mp, (rec.ir_free == ibtrec->ir_free) &&
				(rec.ir_freecount == ibtrec->ir_freecount),
				error);

	/*
	 * The content of inobt records should always match between the inobt
	 * and finobt. The lifecycle of records in the finobt is different from
	 * the inobt in that the finobt only tracks records with at least one
	 * free inode. Hence, if all of the inodes are free and we aren't
	 * keeping inode chunks permanently on disk, remove the record.
	 * Otherwise, update the record with the new information.
	 *
	 * Note that we currently can't free chunks when the block size is large
	 * enough for multiple chunks. Leave the finobt record to remain in sync
	 * with the inobt.
	 */
	if (rec.ir_free == XFS_INOBT_ALL_FREE &&
	    mp->m_sb.sb_inopblock <= XFS_INODES_PER_CHUNK &&
	    !(mp->m_flags & XFS_MOUNT_IKEEP)) {
		error = xfs_btree_delete(cur, &i);
		if (error)
			goto error;
		ASSERT(i == 1);
	} else {
		error = xfs_inobt_update(cur, &rec);
		if (error)
			goto error;
	}

out:
	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error;

	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	return 0;

error:
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
	struct xfs_trans	*tp,		/* transaction pointer */
	xfs_ino_t		inode,		/* inode to be freed */
	struct xfs_defer_ops	*dfops,		/* extents to free */
	struct xfs_icluster	*xic)	/* cluster info if deleted */
{
	/* REFERENCED */
	xfs_agblock_t		agbno;	/* block number containing inode */
	struct xfs_buf		*agbp;	/* buffer for allocation group header */
	xfs_agino_t		agino;	/* allocation group inode number */
	xfs_agnumber_t		agno;	/* allocation group number */
	int			error;	/* error return value */
	struct xfs_mount	*mp;	/* mount structure for filesystem */
	struct xfs_inobt_rec_incore rec;/* btree record */

	mp = tp->t_mountp;

	/*
	 * Break up inode number into its components.
	 */
	agno = XFS_INO_TO_AGNO(mp, inode);
	if (agno >= mp->m_sb.sb_agcount)  {
		xfs_warn(mp, "%s: agno >= mp->m_sb.sb_agcount (%d >= %d).",
			__func__, agno, mp->m_sb.sb_agcount);
		ASSERT(0);
		return -EINVAL;
	}
	agino = XFS_INO_TO_AGINO(mp, inode);
	if (inode != XFS_AGINO_TO_INO(mp, agno, agino))  {
		xfs_warn(mp, "%s: inode != XFS_AGINO_TO_INO() (%llu != %llu).",
			__func__, (unsigned long long)inode,
			(unsigned long long)XFS_AGINO_TO_INO(mp, agno, agino));
		ASSERT(0);
		return -EINVAL;
	}
	agbno = XFS_AGINO_TO_AGBNO(mp, agino);
	if (agbno >= mp->m_sb.sb_agblocks)  {
		xfs_warn(mp, "%s: agbno >= mp->m_sb.sb_agblocks (%d >= %d).",
			__func__, agbno, mp->m_sb.sb_agblocks);
		ASSERT(0);
		return -EINVAL;
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

	/*
	 * Fix up the inode allocation btree.
	 */
	error = xfs_difree_inobt(mp, tp, agbp, agino, dfops, xic, &rec);
	if (error)
		goto error0;

	/*
	 * Fix up the free inode btree.
	 */
	if (xfs_sb_version_hasfinobt(&mp->m_sb)) {
		error = xfs_difree_finobt(mp, tp, agbp, agino, &rec);
		if (error)
			goto error0;
	}

	return 0;

error0:
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
	cur = xfs_inobt_init_cursor(mp, tp, agbp, agno, XFS_BTNUM_INO);
	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &i);
	if (!error) {
		if (i)
			error = xfs_inobt_get_rec(cur, &rec, &i);
		if (!error && i == 0)
			error = -EINVAL;
	}

	xfs_trans_brelse(tp, agbp);
	xfs_btree_del_cursor(cur, error ? XFS_BTREE_ERROR : XFS_BTREE_NOERROR);
	if (error)
		return error;

	/* check that the returned record contains the required inode */
	if (rec.ir_startino > agino ||
	    rec.ir_startino + mp->m_ialloc_inos <= agino)
		return -EINVAL;

	/* for untrusted inodes check it is allocated first */
	if ((flags & XFS_IGET_UNTRUSTED) &&
	    (rec.ir_free & XFS_INOBT_MASK(agino - rec.ir_startino)))
		return -EINVAL;

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
	xfs_agblock_t	offset_agbno;	/* blks from chunk start to inode */

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
			return -EINVAL;
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
		return -EINVAL;
	}

	blks_per_cluster = xfs_icluster_size_fsb(mp);

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
	if (blks_per_cluster == 1) {
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
		return -EINVAL;
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
	uint		inodes;

	inodes = (1LL << XFS_INO_AGINO_BITS(mp)) >> XFS_INODES_PER_CHUNK_LOG;
	mp->m_in_maxlevels = xfs_btree_compute_maxlevels(mp, mp->m_inobt_mnr,
							 inodes);
}

/*
 * Log specified fields for the ag hdr (inode section). The growth of the agi
 * structure over time requires that we interpret the buffer as two logical
 * regions delineated by the end of the unlinked list. This is due to the size
 * of the hash table and its location in the middle of the agi.
 *
 * For example, a request to log a field before agi_unlinked and a field after
 * agi_unlinked could cause us to log the entire hash table and use an excessive
 * amount of log space. To avoid this behavior, log the region up through
 * agi_unlinked in one call and the region after agi_unlinked through the end of
 * the structure in another.
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
		offsetof(xfs_agi_t, agi_free_root),
		offsetof(xfs_agi_t, agi_free_level),
		sizeof(xfs_agi_t)
	};
#ifdef DEBUG
	xfs_agi_t		*agi;	/* allocation group header */

	agi = XFS_BUF_TO_AGI(bp);
	ASSERT(agi->agi_magicnum == cpu_to_be32(XFS_AGI_MAGIC));
#endif

	xfs_trans_buf_set_type(tp, bp, XFS_BLFT_AGI_BUF);

	/*
	 * Compute byte offsets for the first and last fields in the first
	 * region and log the agi buffer. This only logs up through
	 * agi_unlinked.
	 */
	if (fields & XFS_AGI_ALL_BITS_R1) {
		xfs_btree_offsets(fields, offsets, XFS_AGI_NUM_BITS_R1,
				  &first, &last);
		xfs_trans_log_buf(tp, bp, first, last);
	}

	/*
	 * Mask off the bits in the first region and calculate the first and
	 * last field offsets for any bits in the second region.
	 */
	fields &= ~XFS_AGI_ALL_BITS_R1;
	if (fields) {
		xfs_btree_offsets(fields, offsets, XFS_AGI_NUM_BITS_R2,
				  &first, &last);
		xfs_trans_log_buf(tp, bp, first, last);
	}
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

static bool
xfs_agi_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_agi	*agi = XFS_BUF_TO_AGI(bp);

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		if (!uuid_equal(&agi->agi_uuid, &mp->m_sb.sb_meta_uuid))
			return false;
		if (!xfs_log_check_lsn(mp,
				be64_to_cpu(XFS_BUF_TO_AGI(bp)->agi_lsn)))
			return false;
	}

	/*
	 * Validate the magic number of the agi block.
	 */
	if (agi->agi_magicnum != cpu_to_be32(XFS_AGI_MAGIC))
		return false;
	if (!XFS_AGI_GOOD_VERSION(be32_to_cpu(agi->agi_versionnum)))
		return false;

	if (be32_to_cpu(agi->agi_level) > XFS_BTREE_MAXLEVELS)
		return false;
	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agi->agi_seqno) != bp->b_pag->pag_agno)
		return false;

	xfs_check_agi_unlinked(agi);
	return true;
}

static void
xfs_agi_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;

	if (xfs_sb_version_hascrc(&mp->m_sb) &&
	    !xfs_buf_verify_cksum(bp, XFS_AGI_CRC_OFF))
		xfs_buf_ioerror(bp, -EFSBADCRC);
	else if (XFS_TEST_ERROR(!xfs_agi_verify(bp), mp,
				XFS_ERRTAG_IALLOC_READ_AGI,
				XFS_RANDOM_IALLOC_READ_AGI))
		xfs_buf_ioerror(bp, -EFSCORRUPTED);

	if (bp->b_error)
		xfs_verifier_error(bp);
}

static void
xfs_agi_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_target->bt_mount;
	struct xfs_buf_log_item	*bip = bp->b_fspriv;

	if (!xfs_agi_verify(bp)) {
		xfs_buf_ioerror(bp, -EFSCORRUPTED);
		xfs_verifier_error(bp);
		return;
	}

	if (!xfs_sb_version_hascrc(&mp->m_sb))
		return;

	if (bip)
		XFS_BUF_TO_AGI(bp)->agi_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	xfs_buf_update_cksum(bp, XFS_AGI_CRC_OFF);
}

const struct xfs_buf_ops xfs_agi_buf_ops = {
	.name = "xfs_agi",
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

	trace_xfs_read_agi(mp, agno);

	ASSERT(agno != NULLAGNUMBER);
	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, agno, XFS_AGI_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, bpp, &xfs_agi_buf_ops);
	if (error)
		return error;

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

	trace_xfs_ialloc_read_agi(mp, agno);

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
