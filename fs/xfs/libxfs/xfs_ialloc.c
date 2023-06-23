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
#include "xfs_bit.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_ialloc.h"
#include "xfs_ialloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_bmap.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"
#include "xfs_icreate_item.h"
#include "xfs_icache.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_rmap.h"
#include "xfs_ag.h"

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
	if (xfs_has_sparseinodes(cur->bc_mp)) {
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

/* Convert on-disk btree record to incore inobt record. */
void
xfs_inobt_btrec_to_irec(
	struct xfs_mount		*mp,
	const union xfs_btree_rec	*rec,
	struct xfs_inobt_rec_incore	*irec)
{
	irec->ir_startino = be32_to_cpu(rec->inobt.ir_startino);
	if (xfs_has_sparseinodes(mp)) {
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
}

/*
 * Get the data from the pointed-to record.
 */
int
xfs_inobt_get_rec(
	struct xfs_btree_cur		*cur,
	struct xfs_inobt_rec_incore	*irec,
	int				*stat)
{
	struct xfs_mount		*mp = cur->bc_mp;
	union xfs_btree_rec		*rec;
	int				error;
	uint64_t			realfree;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (error || *stat == 0)
		return error;

	xfs_inobt_btrec_to_irec(mp, rec, irec);

	if (!xfs_verify_agino(cur->bc_ag.pag, irec->ir_startino))
		goto out_bad_rec;
	if (irec->ir_count < XFS_INODES_PER_HOLEMASK_BIT ||
	    irec->ir_count > XFS_INODES_PER_CHUNK)
		goto out_bad_rec;
	if (irec->ir_freecount > XFS_INODES_PER_CHUNK)
		goto out_bad_rec;

	/* if there are no holes, return the first available offset */
	if (!xfs_inobt_issparse(irec->ir_holemask))
		realfree = irec->ir_free;
	else
		realfree = irec->ir_free & xfs_inobt_irec_to_allocmask(irec);
	if (hweight64(realfree) != irec->ir_freecount)
		goto out_bad_rec;

	return 0;

out_bad_rec:
	xfs_warn(mp,
		"%s Inode BTree record corruption in AG %d detected!",
		cur->bc_btnum == XFS_BTNUM_INO ? "Used" : "Free",
		cur->bc_ag.pag->pag_agno);
	xfs_warn(mp,
"start inode 0x%x, count 0x%x, free 0x%x freemask 0x%llx, holemask 0x%x",
		irec->ir_startino, irec->ir_count, irec->ir_freecount,
		irec->ir_free, irec->ir_holemask);
	return -EFSCORRUPTED;
}

/*
 * Insert a single inobt record. Cursor must already point to desired location.
 */
int
xfs_inobt_insert_rec(
	struct xfs_btree_cur	*cur,
	uint16_t		holemask,
	uint8_t			count,
	int32_t			freecount,
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
	struct xfs_perag	*pag,
	xfs_agino_t		newino,
	xfs_agino_t		newlen,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;
	xfs_agino_t		thisino;
	int			i;
	int			error;

	cur = xfs_inobt_init_cursor(mp, tp, agbp, pag, btnum);

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
static int
xfs_check_agi_freecount(
	struct xfs_btree_cur	*cur)
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

		if (!xfs_is_shutdown(cur->bc_mp))
			ASSERT(freecount == cur->bc_ag.pag->pagi_freecount);
	}
	return 0;
}
#else
#define xfs_check_agi_freecount(cur)	0
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
	int			nbufs;
	int			version;
	int			i, j;
	xfs_daddr_t		d;
	xfs_ino_t		ino = 0;
	int			error;

	/*
	 * Loop over the new block(s), filling in the inodes.  For small block
	 * sizes, manipulate the inodes in buffers  which are multiples of the
	 * blocks size.
	 */
	nbufs = length / M_IGEO(mp)->blocks_per_cluster;

	/*
	 * Figure out what version number to use in the inodes we create.  If
	 * the superblock version has caught up to the one that supports the new
	 * inode format, then use the new inode version.  Otherwise use the old
	 * version so that old kernels will continue to be able to use the file
	 * system.
	 *
	 * For v3 inodes, we also need to write the inode number into the inode,
	 * so calculate the first inode number of the chunk here as
	 * XFS_AGB_TO_AGINO() only works within a filesystem block, not
	 * across multiple filesystem blocks (such as a cluster) and so cannot
	 * be used in the cluster buffer loop below.
	 *
	 * Further, because we are writing the inode directly into the buffer
	 * and calculating a CRC on the entire inode, we have ot log the entire
	 * inode so that the entire range the CRC covers is present in the log.
	 * That means for v3 inode we log the entire buffer rather than just the
	 * inode cores.
	 */
	if (xfs_has_v3inodes(mp)) {
		version = 3;
		ino = XFS_AGINO_TO_INO(mp, agno, XFS_AGB_TO_AGINO(mp, agbno));

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
		d = XFS_AGB_TO_DADDR(mp, agno, agbno +
				(j * M_IGEO(mp)->blocks_per_cluster));
		error = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
				mp->m_bsize * M_IGEO(mp)->blocks_per_cluster,
				XBF_UNMAPPED, &fbuf);
		if (error)
			return error;

		/* Initialize the inode buffers and log them appropriately. */
		fbuf->b_ops = &xfs_inode_buf_ops;
		xfs_buf_zero(fbuf, 0, BBTOB(fbuf->b_length));
		for (i = 0; i < M_IGEO(mp)->inodes_per_cluster; i++) {
			int	ioffset = i << mp->m_sb.sb_inodelog;

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
					  ioffset + XFS_DINODE_SIZE(mp) - 1);
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
	offset = XFS_AGB_TO_AGINO(mp, mod);
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
	struct xfs_perag		*pag,
	int				btnum,
	struct xfs_inobt_rec_incore	*nrec,	/* in/out: new/merged rec. */
	bool				merge)	/* merge or replace */
{
	struct xfs_btree_cur		*cur;
	int				error;
	int				i;
	struct xfs_inobt_rec_incore	rec;

	cur = xfs_inobt_init_cursor(mp, tp, agbp, pag, btnum);

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
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error;
		}

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
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error;
		}
		if (XFS_IS_CORRUPT(mp, rec.ir_startino != nrec->ir_startino)) {
			error = -EFSCORRUPTED;
			goto error;
		}

		/*
		 * This should never fail. If we have coexisting records that
		 * cannot merge, something is seriously wrong.
		 */
		if (XFS_IS_CORRUPT(mp, !__xfs_inobt_can_merge(nrec, &rec))) {
			error = -EFSCORRUPTED;
			goto error;
		}

		trace_xfs_irec_merge_pre(mp, pag->pag_agno, rec.ir_startino,
					 rec.ir_holemask, nrec->ir_startino,
					 nrec->ir_holemask);

		/* merge to nrec to output the updated record */
		__xfs_inobt_rec_merge(nrec, &rec);

		trace_xfs_irec_merge_post(mp, pag->pag_agno, nrec->ir_startino,
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
 * Allocate new inodes in the allocation group specified by agbp.  Returns 0 if
 * inodes were allocated in this AG; -EAGAIN if there was no space in this AG so
 * the caller knows it can try another AG, a hard -ENOSPC when over the maximum
 * inode count threshold, or the usual negative error code for other errors.
 */
STATIC int
xfs_ialloc_ag_alloc(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_perag	*pag)
{
	struct xfs_agi		*agi;
	struct xfs_alloc_arg	args;
	int			error;
	xfs_agino_t		newino;		/* new first inode's number */
	xfs_agino_t		newlen;		/* new number of inodes */
	int			isaligned = 0;	/* inode allocation at stripe */
						/* unit boundary */
	/* init. to full chunk */
	struct xfs_inobt_rec_incore rec;
	struct xfs_ino_geometry	*igeo = M_IGEO(tp->t_mountp);
	uint16_t		allocmask = (uint16_t) -1;
	int			do_sparse = 0;

	memset(&args, 0, sizeof(args));
	args.tp = tp;
	args.mp = tp->t_mountp;
	args.fsbno = NULLFSBLOCK;
	args.oinfo = XFS_RMAP_OINFO_INODES;

#ifdef DEBUG
	/* randomly do sparse inode allocations */
	if (xfs_has_sparseinodes(tp->t_mountp) &&
	    igeo->ialloc_min_blks < igeo->ialloc_blks)
		do_sparse = prandom_u32() & 1;
#endif

	/*
	 * Locking will ensure that we don't have two callers in here
	 * at one time.
	 */
	newlen = igeo->ialloc_inos;
	if (igeo->maxicount &&
	    percpu_counter_read_positive(&args.mp->m_icount) + newlen >
							igeo->maxicount)
		return -ENOSPC;
	args.minlen = args.maxlen = igeo->ialloc_blks;
	/*
	 * First try to allocate inodes contiguous with the last-allocated
	 * chunk of inodes.  If the filesystem is striped, this will fill
	 * an entire stripe unit with inodes.
	 */
	agi = agbp->b_addr;
	newino = be32_to_cpu(agi->agi_newino);
	args.agbno = XFS_AGINO_TO_AGBNO(args.mp, newino) +
		     igeo->ialloc_blks;
	if (do_sparse)
		goto sparse_alloc;
	if (likely(newino != NULLAGINO &&
		  (args.agbno < be32_to_cpu(agi->agi_length)))) {
		args.fsbno = XFS_AGB_TO_FSB(args.mp, pag->pag_agno, args.agbno);
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
		args.minalignslop = igeo->cluster_align - 1;

		/* Allow space for the inode btree to split. */
		args.minleft = igeo->inobt_maxlevels;
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
		if (igeo->ialloc_align) {
			ASSERT(!xfs_has_noalign(args.mp));
			args.alignment = args.mp->m_dalign;
			isaligned = 1;
		} else
			args.alignment = igeo->cluster_align;
		/*
		 * Need to figure out where to allocate the inode blocks.
		 * Ideally they should be spaced out through the a.g.
		 * For now, just allocate blocks up front.
		 */
		args.agbno = be32_to_cpu(agi->agi_root);
		args.fsbno = XFS_AGB_TO_FSB(args.mp, pag->pag_agno, args.agbno);
		/*
		 * Allocate a fixed-size extent of inodes.
		 */
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.prod = 1;
		/*
		 * Allow space for the inode btree to split.
		 */
		args.minleft = igeo->inobt_maxlevels;
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
		args.fsbno = XFS_AGB_TO_FSB(args.mp, pag->pag_agno, args.agbno);
		args.alignment = igeo->cluster_align;
		if ((error = xfs_alloc_vextent(&args)))
			return error;
	}

	/*
	 * Finally, try a sparse allocation if the filesystem supports it and
	 * the sparse allocation length is smaller than a full chunk.
	 */
	if (xfs_has_sparseinodes(args.mp) &&
	    igeo->ialloc_min_blks < igeo->ialloc_blks &&
	    args.fsbno == NULLFSBLOCK) {
sparse_alloc:
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.agbno = be32_to_cpu(agi->agi_root);
		args.fsbno = XFS_AGB_TO_FSB(args.mp, pag->pag_agno, args.agbno);
		args.alignment = args.mp->m_sb.sb_spino_align;
		args.prod = 1;

		args.minlen = igeo->ialloc_min_blks;
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
				 igeo->ialloc_blks;

		error = xfs_alloc_vextent(&args);
		if (error)
			return error;

		newlen = XFS_AGB_TO_AGINO(args.mp, args.len);
		ASSERT(newlen <= XFS_INODES_PER_CHUNK);
		allocmask = (1 << (newlen / XFS_INODES_PER_HOLEMASK_BIT)) - 1;
	}

	if (args.fsbno == NULLFSBLOCK)
		return -EAGAIN;

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
	error = xfs_ialloc_inode_init(args.mp, tp, NULL, newlen, pag->pag_agno,
			args.agbno, args.len, prandom_u32());

	if (error)
		return error;
	/*
	 * Convert the results.
	 */
	newino = XFS_AGB_TO_AGINO(args.mp, args.agbno);

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
		error = xfs_inobt_insert_sprec(args.mp, tp, agbp, pag,
				XFS_BTNUM_INO, &rec, true);
		if (error == -EFSCORRUPTED) {
			xfs_alert(args.mp,
	"invalid sparse inode record: ino 0x%llx holemask 0x%x count %u",
				  XFS_AGINO_TO_INO(args.mp, pag->pag_agno,
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
		if (xfs_has_finobt(args.mp)) {
			error = xfs_inobt_insert_sprec(args.mp, tp, agbp, pag,
				       XFS_BTNUM_FINO, &rec, false);
			if (error)
				return error;
		}
	} else {
		/* full chunk - insert new records to both btrees */
		error = xfs_inobt_insert(args.mp, tp, agbp, pag, newino, newlen,
					 XFS_BTNUM_INO);
		if (error)
			return error;

		if (xfs_has_finobt(args.mp)) {
			error = xfs_inobt_insert(args.mp, tp, agbp, pag, newino,
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
	pag->pagi_freecount += newlen;
	pag->pagi_count += newlen;
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
	return 0;
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
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
			return -EFSCORRUPTED;
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
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
			return -EFSCORRUPTED;
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
	struct xfs_perag	*pag,
	xfs_ino_t		parent,
	xfs_ino_t		*inop)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agi		*agi = agbp->b_addr;
	xfs_agnumber_t		pagno = XFS_INO_TO_AGNO(mp, parent);
	xfs_agino_t		pagino = XFS_INO_TO_AGINO(mp, parent);
	struct xfs_btree_cur	*cur, *tcur;
	struct xfs_inobt_rec_incore rec, trec;
	xfs_ino_t		ino;
	int			error;
	int			offset;
	int			i, j;
	int			searchdistance = 10;

	ASSERT(pag->pagi_init);
	ASSERT(pag->pagi_inodeok);
	ASSERT(pag->pagi_freecount > 0);

 restart_pagno:
	cur = xfs_inobt_init_cursor(mp, tp, agbp, pag, XFS_BTNUM_INO);
	/*
	 * If pagino is 0 (this is the root inode allocation) use newino.
	 * This must work because we've just allocated some.
	 */
	if (!pagino)
		pagino = be32_to_cpu(agi->agi_newino);

	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error0;

	/*
	 * If in the same AG as the parent, try to get near the parent.
	 */
	if (pagno == pag->pag_agno) {
		int		doneleft;	/* done, to the left */
		int		doneright;	/* done, to the right */

		error = xfs_inobt_lookup(cur, pagino, XFS_LOOKUP_LE, &i);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		error = xfs_inobt_get_rec(cur, &rec, &j);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(mp, j != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

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
		while (--searchdistance > 0 && (!doneleft || !doneright)) {
			int	useleft;  /* using left inode chunk this time */

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
				xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
				cur = tcur;

				pag->pagl_leftrec = trec.ir_startino;
				pag->pagl_rightrec = rec.ir_startino;
				pag->pagl_pagino = pagino;
				rec = trec;
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

		if (searchdistance <= 0) {
			/*
			 * Not in range - save last search
			 * location and allocate a new inode
			 */
			xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
			pag->pagl_leftrec = trec.ir_startino;
			pag->pagl_rightrec = rec.ir_startino;
			pag->pagl_pagino = pagino;

		} else {
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
	}

	/*
	 * In a different AG from the parent.
	 * See if the most recently allocated block has any free.
	 */
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
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}

	for (;;) {
		error = xfs_inobt_get_rec(cur, &rec, &i);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
		if (rec.ir_freecount > 0)
			break;
		error = xfs_btree_increment(cur, 0, &i);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}
	}

alloc_inode:
	offset = xfs_inobt_first_free_inode(&rec);
	ASSERT(offset >= 0);
	ASSERT(offset < XFS_INODES_PER_CHUNK);
	ASSERT((XFS_AGINO_TO_OFFSET(mp, rec.ir_startino) %
				   XFS_INODES_PER_CHUNK) == 0);
	ino = XFS_AGINO_TO_INO(mp, pag->pag_agno, rec.ir_startino + offset);
	rec.ir_free &= ~XFS_INOBT_MASK(offset);
	rec.ir_freecount--;
	error = xfs_inobt_update(cur, &rec);
	if (error)
		goto error0;
	be32_add_cpu(&agi->agi_freecount, -1);
	xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
	pag->pagi_freecount--;

	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error0;

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
		if (XFS_IS_CORRUPT(lcur->bc_mp, i != 1))
			return -EFSCORRUPTED;

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
		if (XFS_IS_CORRUPT(lcur->bc_mp, j != 1)) {
			error = -EFSCORRUPTED;
			goto error_rcur;
		}
	}

	if (XFS_IS_CORRUPT(lcur->bc_mp, i != 1 && j != 1)) {
		error = -EFSCORRUPTED;
		goto error_rcur;
	}
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
			if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
				return -EFSCORRUPTED;
			return 0;
		}
	}

	/*
	 * Find the first inode available in the AG.
	 */
	error = xfs_inobt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

	error = xfs_inobt_get_rec(cur, rec, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

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
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

	error = xfs_inobt_get_rec(cur, &rec, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;
	ASSERT((XFS_AGINO_TO_OFFSET(cur->bc_mp, rec.ir_startino) %
				   XFS_INODES_PER_CHUNK) == 0);

	rec.ir_free &= ~XFS_INOBT_MASK(offset);
	rec.ir_freecount--;

	if (XFS_IS_CORRUPT(cur->bc_mp,
			   rec.ir_free != frec->ir_free ||
			   rec.ir_freecount != frec->ir_freecount))
		return -EFSCORRUPTED;

	return xfs_inobt_update(cur, &rec);
}

/*
 * Allocate an inode using the free inode btree, if available. Otherwise, fall
 * back to the inobt search algorithm.
 *
 * The caller selected an AG for us, and made sure that free inodes are
 * available.
 */
static int
xfs_dialloc_ag(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	struct xfs_perag	*pag,
	xfs_ino_t		parent,
	xfs_ino_t		*inop)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_agi			*agi = agbp->b_addr;
	xfs_agnumber_t			pagno = XFS_INO_TO_AGNO(mp, parent);
	xfs_agino_t			pagino = XFS_INO_TO_AGINO(mp, parent);
	struct xfs_btree_cur		*cur;	/* finobt cursor */
	struct xfs_btree_cur		*icur;	/* inobt cursor */
	struct xfs_inobt_rec_incore	rec;
	xfs_ino_t			ino;
	int				error;
	int				offset;
	int				i;

	if (!xfs_has_finobt(mp))
		return xfs_dialloc_ag_inobt(tp, agbp, pag, parent, inop);

	/*
	 * If pagino is 0 (this is the root inode allocation) use newino.
	 * This must work because we've just allocated some.
	 */
	if (!pagino)
		pagino = be32_to_cpu(agi->agi_newino);

	cur = xfs_inobt_init_cursor(mp, tp, agbp, pag, XFS_BTNUM_FINO);

	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error_cur;

	/*
	 * The search algorithm depends on whether we're in the same AG as the
	 * parent. If so, find the closest available inode to the parent. If
	 * not, consider the agi hint or find the first free inode in the AG.
	 */
	if (pag->pag_agno == pagno)
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
	ino = XFS_AGINO_TO_INO(mp, pag->pag_agno, rec.ir_startino + offset);

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
	icur = xfs_inobt_init_cursor(mp, tp, agbp, pag, XFS_BTNUM_INO);

	error = xfs_check_agi_freecount(icur);
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

	error = xfs_check_agi_freecount(icur);
	if (error)
		goto error_icur;
	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error_icur;

	xfs_btree_del_cursor(icur, XFS_BTREE_NOERROR);
	xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
	*inop = ino;
	return 0;

error_icur:
	xfs_btree_del_cursor(icur, XFS_BTREE_ERROR);
error_cur:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
}

static int
xfs_dialloc_roll(
	struct xfs_trans	**tpp,
	struct xfs_buf		*agibp)
{
	struct xfs_trans	*tp = *tpp;
	struct xfs_dquot_acct	*dqinfo;
	int			error;

	/*
	 * Hold to on to the agibp across the commit so no other allocation can
	 * come in and take the free inodes we just allocated for our caller.
	 */
	xfs_trans_bhold(tp, agibp);

	/*
	 * We want the quota changes to be associated with the next transaction,
	 * NOT this one. So, detach the dqinfo from this and attach it to the
	 * next transaction.
	 */
	dqinfo = tp->t_dqinfo;
	tp->t_dqinfo = NULL;

	error = xfs_trans_roll(&tp);

	/* Re-attach the quota info that we detached from prev trx. */
	tp->t_dqinfo = dqinfo;

	/*
	 * Join the buffer even on commit error so that the buffer is released
	 * when the caller cancels the transaction and doesn't have to handle
	 * this error case specially.
	 */
	xfs_trans_bjoin(tp, agibp);
	*tpp = tp;
	return error;
}

static xfs_agnumber_t
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

static bool
xfs_dialloc_good_ag(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	umode_t			mode,
	int			flags,
	bool			ok_alloc)
{
	struct xfs_mount	*mp = tp->t_mountp;
	xfs_extlen_t		ineed;
	xfs_extlen_t		longest = 0;
	int			needspace;
	int			error;

	if (!pag->pagi_inodeok)
		return false;

	if (!pag->pagi_init) {
		error = xfs_ialloc_read_agi(pag, tp, NULL);
		if (error)
			return false;
	}

	if (pag->pagi_freecount)
		return true;
	if (!ok_alloc)
		return false;

	if (!pag->pagf_init) {
		error = xfs_alloc_read_agf(pag, tp, flags, NULL);
		if (error)
			return false;
	}

	/*
	 * Check that there is enough free space for the file plus a chunk of
	 * inodes if we need to allocate some. If this is the first pass across
	 * the AGs, take into account the potential space needed for alignment
	 * of inode chunks when checking the longest contiguous free space in
	 * the AG - this prevents us from getting ENOSPC because we have free
	 * space larger than ialloc_blks but alignment constraints prevent us
	 * from using it.
	 *
	 * If we can't find an AG with space for full alignment slack to be
	 * taken into account, we must be near ENOSPC in all AGs.  Hence we
	 * don't include alignment for the second pass and so if we fail
	 * allocation due to alignment issues then it is most likely a real
	 * ENOSPC condition.
	 *
	 * XXX(dgc): this calculation is now bogus thanks to the per-ag
	 * reservations that xfs_alloc_fix_freelist() now does via
	 * xfs_alloc_space_available(). When the AG fills up, pagf_freeblks will
	 * be more than large enough for the check below to succeed, but
	 * xfs_alloc_space_available() will fail because of the non-zero
	 * metadata reservation and hence we won't actually be able to allocate
	 * more inodes in this AG. We do soooo much unnecessary work near ENOSPC
	 * because of this.
	 */
	ineed = M_IGEO(mp)->ialloc_min_blks;
	if (flags && ineed > 1)
		ineed += M_IGEO(mp)->cluster_align;
	longest = pag->pagf_longest;
	if (!longest)
		longest = pag->pagf_flcount > 0;
	needspace = S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);

	if (pag->pagf_freeblks < needspace + ineed || longest < ineed)
		return false;
	return true;
}

static int
xfs_dialloc_try_ag(
	struct xfs_trans	**tpp,
	struct xfs_perag	*pag,
	xfs_ino_t		parent,
	xfs_ino_t		*new_ino,
	bool			ok_alloc)
{
	struct xfs_buf		*agbp;
	xfs_ino_t		ino;
	int			error;

	/*
	 * Then read in the AGI buffer and recheck with the AGI buffer
	 * lock held.
	 */
	error = xfs_ialloc_read_agi(pag, *tpp, &agbp);
	if (error)
		return error;

	if (!pag->pagi_freecount) {
		if (!ok_alloc) {
			error = -EAGAIN;
			goto out_release;
		}

		error = xfs_ialloc_ag_alloc(*tpp, agbp, pag);
		if (error < 0)
			goto out_release;

		/*
		 * We successfully allocated space for an inode cluster in this
		 * AG.  Roll the transaction so that we can allocate one of the
		 * new inodes.
		 */
		ASSERT(pag->pagi_freecount > 0);
		error = xfs_dialloc_roll(tpp, agbp);
		if (error)
			goto out_release;
	}

	/* Allocate an inode in the found AG */
	error = xfs_dialloc_ag(*tpp, agbp, pag, parent, &ino);
	if (!error)
		*new_ino = ino;
	return error;

out_release:
	xfs_trans_brelse(*tpp, agbp);
	return error;
}

/*
 * Allocate an on-disk inode.
 *
 * Mode is used to tell whether the new inode is a directory and hence where to
 * locate it. The on-disk inode that is allocated will be returned in @new_ino
 * on success, otherwise an error will be set to indicate the failure (e.g.
 * -ENOSPC).
 */
int
xfs_dialloc(
	struct xfs_trans	**tpp,
	xfs_ino_t		parent,
	umode_t			mode,
	xfs_ino_t		*new_ino)
{
	struct xfs_mount	*mp = (*tpp)->t_mountp;
	xfs_agnumber_t		agno;
	int			error = 0;
	xfs_agnumber_t		start_agno;
	struct xfs_perag	*pag;
	struct xfs_ino_geometry	*igeo = M_IGEO(mp);
	bool			ok_alloc = true;
	int			flags;
	xfs_ino_t		ino;

	/*
	 * Directories, symlinks, and regular files frequently allocate at least
	 * one block, so factor that potential expansion when we examine whether
	 * an AG has enough space for file creation.
	 */
	if (S_ISDIR(mode))
		start_agno = xfs_ialloc_next_ag(mp);
	else {
		start_agno = XFS_INO_TO_AGNO(mp, parent);
		if (start_agno >= mp->m_maxagi)
			start_agno = 0;
	}

	/*
	 * If we have already hit the ceiling of inode blocks then clear
	 * ok_alloc so we scan all available agi structures for a free
	 * inode.
	 *
	 * Read rough value of mp->m_icount by percpu_counter_read_positive,
	 * which will sacrifice the preciseness but improve the performance.
	 */
	if (igeo->maxicount &&
	    percpu_counter_read_positive(&mp->m_icount) + igeo->ialloc_inos
							> igeo->maxicount) {
		ok_alloc = false;
	}

	/*
	 * Loop until we find an allocation group that either has free inodes
	 * or in which we can allocate some inodes.  Iterate through the
	 * allocation groups upward, wrapping at the end.
	 */
	agno = start_agno;
	flags = XFS_ALLOC_FLAG_TRYLOCK;
	for (;;) {
		pag = xfs_perag_get(mp, agno);
		if (xfs_dialloc_good_ag(*tpp, pag, mode, flags, ok_alloc)) {
			error = xfs_dialloc_try_ag(tpp, pag, parent,
					&ino, ok_alloc);
			if (error != -EAGAIN)
				break;
		}

		if (xfs_is_shutdown(mp)) {
			error = -EFSCORRUPTED;
			break;
		}
		if (++agno == mp->m_maxagi)
			agno = 0;
		if (agno == start_agno) {
			if (!flags) {
				error = -ENOSPC;
				break;
			}
			flags = 0;
		}
		xfs_perag_put(pag);
	}

	if (!error)
		*new_ino = ino;
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
	struct xfs_trans		*tp,
	xfs_agnumber_t			agno,
	struct xfs_inobt_rec_incore	*rec)
{
	struct xfs_mount		*mp = tp->t_mountp;
	xfs_agblock_t			sagbno = XFS_AGINO_TO_AGBNO(mp,
							rec->ir_startino);
	int				startidx, endidx;
	int				nextbit;
	xfs_agblock_t			agbno;
	int				contigblk;
	DECLARE_BITMAP(holemask, XFS_INOBT_HOLEMASK_BITS);

	if (!xfs_inobt_issparse(rec->ir_holemask)) {
		/* not sparse, calculate extent info directly */
		xfs_free_extent_later(tp, XFS_AGB_TO_FSB(mp, agno, sagbno),
				  M_IGEO(mp)->ialloc_blks,
				  &XFS_RMAP_OINFO_INODES);
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
		xfs_free_extent_later(tp, XFS_AGB_TO_FSB(mp, agno, agbno),
				  contigblk, &XFS_RMAP_OINFO_INODES);

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
	struct xfs_perag		*pag,
	xfs_agino_t			agino,
	struct xfs_icluster		*xic,
	struct xfs_inobt_rec_incore	*orec)
{
	struct xfs_agi			*agi = agbp->b_addr;
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
	cur = xfs_inobt_init_cursor(mp, tp, agbp, pag, XFS_BTNUM_INO);

	error = xfs_check_agi_freecount(cur);
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
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	error = xfs_inobt_get_rec(cur, &rec, &i);
	if (error) {
		xfs_warn(mp, "%s: xfs_inobt_get_rec() returned error %d.",
			__func__, error);
		goto error0;
	}
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
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
	if (!xfs_has_ikeep(mp) && rec.ir_free == XFS_INOBT_ALL_FREE &&
	    mp->m_sb.sb_inopblock <= XFS_INODES_PER_CHUNK) {
		struct xfs_perag	*pag = agbp->b_pag;

		xic->deleted = true;
		xic->first_ino = XFS_AGINO_TO_INO(mp, pag->pag_agno,
				rec.ir_startino);
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
		pag->pagi_freecount -= ilen - 1;
		pag->pagi_count -= ilen;
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_ICOUNT, -ilen);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -(ilen - 1));

		if ((error = xfs_btree_delete(cur, &i))) {
			xfs_warn(mp, "%s: xfs_btree_delete returned error %d.",
				__func__, error);
			goto error0;
		}

		xfs_difree_inode_chunk(tp, pag->pag_agno, &rec);
	} else {
		xic->deleted = false;

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
		pag->pagi_freecount++;
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, 1);
	}

	error = xfs_check_agi_freecount(cur);
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
	struct xfs_perag		*pag,
	xfs_agino_t			agino,
	struct xfs_inobt_rec_incore	*ibtrec) /* inobt record */
{
	struct xfs_btree_cur		*cur;
	struct xfs_inobt_rec_incore	rec;
	int				offset = agino - ibtrec->ir_startino;
	int				error;
	int				i;

	cur = xfs_inobt_init_cursor(mp, tp, agbp, pag, XFS_BTNUM_FINO);

	error = xfs_inobt_lookup(cur, ibtrec->ir_startino, XFS_LOOKUP_EQ, &i);
	if (error)
		goto error;
	if (i == 0) {
		/*
		 * If the record does not exist in the finobt, we must have just
		 * freed an inode in a previously fully allocated chunk. If not,
		 * something is out of sync.
		 */
		if (XFS_IS_CORRUPT(mp, ibtrec->ir_freecount != 1)) {
			error = -EFSCORRUPTED;
			goto error;
		}

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
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error;
	}

	rec.ir_free |= XFS_INOBT_MASK(offset);
	rec.ir_freecount++;

	if (XFS_IS_CORRUPT(mp,
			   rec.ir_free != ibtrec->ir_free ||
			   rec.ir_freecount != ibtrec->ir_freecount)) {
		error = -EFSCORRUPTED;
		goto error;
	}

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
	if (!xfs_has_ikeep(mp) && rec.ir_free == XFS_INOBT_ALL_FREE &&
	    mp->m_sb.sb_inopblock <= XFS_INODES_PER_CHUNK) {
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
	error = xfs_check_agi_freecount(cur);
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
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	xfs_ino_t		inode,
	struct xfs_icluster	*xic)
{
	/* REFERENCED */
	xfs_agblock_t		agbno;	/* block number containing inode */
	struct xfs_buf		*agbp;	/* buffer for allocation group header */
	xfs_agino_t		agino;	/* allocation group inode number */
	int			error;	/* error return value */
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_inobt_rec_incore rec;/* btree record */

	/*
	 * Break up inode number into its components.
	 */
	if (pag->pag_agno != XFS_INO_TO_AGNO(mp, inode)) {
		xfs_warn(mp, "%s: agno != pag->pag_agno (%d != %d).",
			__func__, XFS_INO_TO_AGNO(mp, inode), pag->pag_agno);
		ASSERT(0);
		return -EINVAL;
	}
	agino = XFS_INO_TO_AGINO(mp, inode);
	if (inode != XFS_AGINO_TO_INO(mp, pag->pag_agno, agino))  {
		xfs_warn(mp, "%s: inode != XFS_AGINO_TO_INO() (%llu != %llu).",
			__func__, (unsigned long long)inode,
			(unsigned long long)XFS_AGINO_TO_INO(mp, pag->pag_agno, agino));
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
	error = xfs_ialloc_read_agi(pag, tp, &agbp);
	if (error) {
		xfs_warn(mp, "%s: xfs_ialloc_read_agi() returned error %d.",
			__func__, error);
		return error;
	}

	/*
	 * Fix up the inode allocation btree.
	 */
	error = xfs_difree_inobt(mp, tp, agbp, pag, agino, xic, &rec);
	if (error)
		goto error0;

	/*
	 * Fix up the free inode btree.
	 */
	if (xfs_has_finobt(mp)) {
		error = xfs_difree_finobt(mp, tp, agbp, pag, agino, &rec);
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
	struct xfs_perag	*pag,
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

	error = xfs_ialloc_read_agi(pag, tp, &agbp);
	if (error) {
		xfs_alert(mp,
			"%s: xfs_ialloc_read_agi() returned error %d, agno %d",
			__func__, error, pag->pag_agno);
		return error;
	}

	/*
	 * Lookup the inode record for the given agino. If the record cannot be
	 * found, then it's an invalid inode number and we should abort. Once
	 * we have a record, we need to ensure it contains the inode number
	 * we are looking up.
	 */
	cur = xfs_inobt_init_cursor(mp, tp, agbp, pag, XFS_BTNUM_INO);
	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &i);
	if (!error) {
		if (i)
			error = xfs_inobt_get_rec(cur, &rec, &i);
		if (!error && i == 0)
			error = -EINVAL;
	}

	xfs_trans_brelse(tp, agbp);
	xfs_btree_del_cursor(cur, error);
	if (error)
		return error;

	/* check that the returned record contains the required inode */
	if (rec.ir_startino > agino ||
	    rec.ir_startino + M_IGEO(mp)->ialloc_inos <= agino)
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
	struct xfs_mount	 *mp,	/* file system mount structure */
	struct xfs_trans	 *tp,	/* transaction pointer */
	xfs_ino_t		ino,	/* inode to locate */
	struct xfs_imap		*imap,	/* location map structure */
	uint			flags)	/* flags for inode btree lookup */
{
	xfs_agblock_t		agbno;	/* block number of inode in the alloc group */
	xfs_agino_t		agino;	/* inode number within alloc group */
	xfs_agblock_t		chunk_agbno;	/* first block in inode chunk */
	xfs_agblock_t		cluster_agbno;	/* first block in inode cluster */
	int			error;	/* error code */
	int			offset;	/* index of inode in its buffer */
	xfs_agblock_t		offset_agbno;	/* blks from chunk start to inode */
	struct xfs_perag	*pag;

	ASSERT(ino != NULLFSINO);

	/*
	 * Split up the inode number into its parts.
	 */
	pag = xfs_perag_get(mp, XFS_INO_TO_AGNO(mp, ino));
	agino = XFS_INO_TO_AGINO(mp, ino);
	agbno = XFS_AGINO_TO_AGBNO(mp, agino);
	if (!pag || agbno >= mp->m_sb.sb_agblocks ||
	    ino != XFS_AGINO_TO_INO(mp, pag->pag_agno, agino)) {
		error = -EINVAL;
#ifdef DEBUG
		/*
		 * Don't output diagnostic information for untrusted inodes
		 * as they can be invalid without implying corruption.
		 */
		if (flags & XFS_IGET_UNTRUSTED)
			goto out_drop;
		if (!pag) {
			xfs_alert(mp,
				"%s: agno (%d) >= mp->m_sb.sb_agcount (%d)",
				__func__, XFS_INO_TO_AGNO(mp, ino),
				mp->m_sb.sb_agcount);
		}
		if (agbno >= mp->m_sb.sb_agblocks) {
			xfs_alert(mp,
		"%s: agbno (0x%llx) >= mp->m_sb.sb_agblocks (0x%lx)",
				__func__, (unsigned long long)agbno,
				(unsigned long)mp->m_sb.sb_agblocks);
		}
		if (pag && ino != XFS_AGINO_TO_INO(mp, pag->pag_agno, agino)) {
			xfs_alert(mp,
		"%s: ino (0x%llx) != XFS_AGINO_TO_INO() (0x%llx)",
				__func__, ino,
				XFS_AGINO_TO_INO(mp, pag->pag_agno, agino));
		}
		xfs_stack_trace();
#endif /* DEBUG */
		goto out_drop;
	}

	/*
	 * For bulkstat and handle lookups, we have an untrusted inode number
	 * that we have to verify is valid. We cannot do this just by reading
	 * the inode buffer as it may have been unlinked and removed leaving
	 * inodes in stale state on disk. Hence we have to do a btree lookup
	 * in all cases where an untrusted inode number is passed.
	 */
	if (flags & XFS_IGET_UNTRUSTED) {
		error = xfs_imap_lookup(mp, tp, pag, agino, agbno,
					&chunk_agbno, &offset_agbno, flags);
		if (error)
			goto out_drop;
		goto out_map;
	}

	/*
	 * If the inode cluster size is the same as the blocksize or
	 * smaller we get to the buffer by simple arithmetics.
	 */
	if (M_IGEO(mp)->blocks_per_cluster == 1) {
		offset = XFS_INO_TO_OFFSET(mp, ino);
		ASSERT(offset < mp->m_sb.sb_inopblock);

		imap->im_blkno = XFS_AGB_TO_DADDR(mp, pag->pag_agno, agbno);
		imap->im_len = XFS_FSB_TO_BB(mp, 1);
		imap->im_boffset = (unsigned short)(offset <<
							mp->m_sb.sb_inodelog);
		error = 0;
		goto out_drop;
	}

	/*
	 * If the inode chunks are aligned then use simple maths to
	 * find the location. Otherwise we have to do a btree
	 * lookup to find the location.
	 */
	if (M_IGEO(mp)->inoalign_mask) {
		offset_agbno = agbno & M_IGEO(mp)->inoalign_mask;
		chunk_agbno = agbno - offset_agbno;
	} else {
		error = xfs_imap_lookup(mp, tp, pag, agino, agbno,
					&chunk_agbno, &offset_agbno, flags);
		if (error)
			goto out_drop;
	}

out_map:
	ASSERT(agbno >= chunk_agbno);
	cluster_agbno = chunk_agbno +
		((offset_agbno / M_IGEO(mp)->blocks_per_cluster) *
		 M_IGEO(mp)->blocks_per_cluster);
	offset = ((agbno - cluster_agbno) * mp->m_sb.sb_inopblock) +
		XFS_INO_TO_OFFSET(mp, ino);

	imap->im_blkno = XFS_AGB_TO_DADDR(mp, pag->pag_agno, cluster_agbno);
	imap->im_len = XFS_FSB_TO_BB(mp, M_IGEO(mp)->blocks_per_cluster);
	imap->im_boffset = (unsigned short)(offset << mp->m_sb.sb_inodelog);

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
		error = -EINVAL;
		goto out_drop;
	}
	error = 0;
out_drop:
	if (pag)
		xfs_perag_put(pag);
	return error;
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
	struct xfs_trans	*tp,
	struct xfs_buf		*bp,
	uint32_t		fields)
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
		offsetof(xfs_agi_t, agi_iblocks),
		sizeof(xfs_agi_t)
	};
#ifdef DEBUG
	struct xfs_agi		*agi = bp->b_addr;

	ASSERT(agi->agi_magicnum == cpu_to_be32(XFS_AGI_MAGIC));
#endif

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

static xfs_failaddr_t
xfs_agi_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_mount;
	struct xfs_agi	*agi = bp->b_addr;
	int		i;

	if (xfs_has_crc(mp)) {
		if (!uuid_equal(&agi->agi_uuid, &mp->m_sb.sb_meta_uuid))
			return __this_address;
		if (!xfs_log_check_lsn(mp, be64_to_cpu(agi->agi_lsn)))
			return __this_address;
	}

	/*
	 * Validate the magic number of the agi block.
	 */
	if (!xfs_verify_magic(bp, agi->agi_magicnum))
		return __this_address;
	if (!XFS_AGI_GOOD_VERSION(be32_to_cpu(agi->agi_versionnum)))
		return __this_address;

	if (be32_to_cpu(agi->agi_level) < 1 ||
	    be32_to_cpu(agi->agi_level) > M_IGEO(mp)->inobt_maxlevels)
		return __this_address;

	if (xfs_has_finobt(mp) &&
	    (be32_to_cpu(agi->agi_free_level) < 1 ||
	     be32_to_cpu(agi->agi_free_level) > M_IGEO(mp)->inobt_maxlevels))
		return __this_address;

	/*
	 * during growfs operations, the perag is not fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agi->agi_seqno) != bp->b_pag->pag_agno)
		return __this_address;

	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		if (agi->agi_unlinked[i] == cpu_to_be32(NULLAGINO))
			continue;
		if (!xfs_verify_ino(mp, be32_to_cpu(agi->agi_unlinked[i])))
			return __this_address;
	}

	return NULL;
}

static void
xfs_agi_read_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount *mp = bp->b_mount;
	xfs_failaddr_t	fa;

	if (xfs_has_crc(mp) &&
	    !xfs_buf_verify_cksum(bp, XFS_AGI_CRC_OFF))
		xfs_verifier_error(bp, -EFSBADCRC, __this_address);
	else {
		fa = xfs_agi_verify(bp);
		if (XFS_TEST_ERROR(fa, mp, XFS_ERRTAG_IALLOC_READ_AGI))
			xfs_verifier_error(bp, -EFSCORRUPTED, fa);
	}
}

static void
xfs_agi_write_verify(
	struct xfs_buf	*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_buf_log_item	*bip = bp->b_log_item;
	struct xfs_agi		*agi = bp->b_addr;
	xfs_failaddr_t		fa;

	fa = xfs_agi_verify(bp);
	if (fa) {
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
		return;
	}

	if (!xfs_has_crc(mp))
		return;

	if (bip)
		agi->agi_lsn = cpu_to_be64(bip->bli_item.li_lsn);
	xfs_buf_update_cksum(bp, XFS_AGI_CRC_OFF);
}

const struct xfs_buf_ops xfs_agi_buf_ops = {
	.name = "xfs_agi",
	.magic = { cpu_to_be32(XFS_AGI_MAGIC), cpu_to_be32(XFS_AGI_MAGIC) },
	.verify_read = xfs_agi_read_verify,
	.verify_write = xfs_agi_write_verify,
	.verify_struct = xfs_agi_verify,
};

/*
 * Read in the allocation group header (inode allocation section)
 */
int
xfs_read_agi(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		**agibpp)
{
	struct xfs_mount	*mp = pag->pag_mount;
	int			error;

	trace_xfs_read_agi(pag->pag_mount, pag->pag_agno);

	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, pag->pag_agno, XFS_AGI_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, agibpp, &xfs_agi_buf_ops);
	if (error)
		return error;
	if (tp)
		xfs_trans_buf_set_type(tp, *agibpp, XFS_BLFT_AGI_BUF);

	xfs_buf_set_ref(*agibpp, XFS_AGI_REF);
	return 0;
}

/*
 * Read in the agi and initialise the per-ag data. If the caller supplies a
 * @agibpp, return the locked AGI buffer to them, otherwise release it.
 */
int
xfs_ialloc_read_agi(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		**agibpp)
{
	struct xfs_buf		*agibp;
	struct xfs_agi		*agi;
	int			error;

	trace_xfs_ialloc_read_agi(pag->pag_mount, pag->pag_agno);

	error = xfs_read_agi(pag, tp, &agibp);
	if (error)
		return error;

	agi = agibp->b_addr;
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
		xfs_is_shutdown(pag->pag_mount));
	if (agibpp)
		*agibpp = agibp;
	else
		xfs_trans_brelse(tp, agibp);
	return 0;
}

/* Is there an inode record covering a given range of inode numbers? */
int
xfs_ialloc_has_inode_record(
	struct xfs_btree_cur	*cur,
	xfs_agino_t		low,
	xfs_agino_t		high,
	bool			*exists)
{
	struct xfs_inobt_rec_incore	irec;
	xfs_agino_t		agino;
	uint16_t		holemask;
	int			has_record;
	int			i;
	int			error;

	*exists = false;
	error = xfs_inobt_lookup(cur, low, XFS_LOOKUP_LE, &has_record);
	while (error == 0 && has_record) {
		error = xfs_inobt_get_rec(cur, &irec, &has_record);
		if (error || irec.ir_startino > high)
			break;

		agino = irec.ir_startino;
		holemask = irec.ir_holemask;
		for (i = 0; i < XFS_INOBT_HOLEMASK_BITS; holemask >>= 1,
				i++, agino += XFS_INODES_PER_HOLEMASK_BIT) {
			if (holemask & 1)
				continue;
			if (agino + XFS_INODES_PER_HOLEMASK_BIT > low &&
					agino <= high) {
				*exists = true;
				return 0;
			}
		}

		error = xfs_btree_increment(cur, 0, &has_record);
	}
	return error;
}

/* Is there an inode record covering a given extent? */
int
xfs_ialloc_has_inodes_at_extent(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		bno,
	xfs_extlen_t		len,
	bool			*exists)
{
	xfs_agino_t		low;
	xfs_agino_t		high;

	low = XFS_AGB_TO_AGINO(cur->bc_mp, bno);
	high = XFS_AGB_TO_AGINO(cur->bc_mp, bno + len) - 1;

	return xfs_ialloc_has_inode_record(cur, low, high, exists);
}

struct xfs_ialloc_count_inodes {
	xfs_agino_t			count;
	xfs_agino_t			freecount;
};

/* Record inode counts across all inobt records. */
STATIC int
xfs_ialloc_count_inodes_rec(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	struct xfs_inobt_rec_incore	irec;
	struct xfs_ialloc_count_inodes	*ci = priv;

	xfs_inobt_btrec_to_irec(cur->bc_mp, rec, &irec);
	ci->count += irec.ir_count;
	ci->freecount += irec.ir_freecount;

	return 0;
}

/* Count allocated and free inodes under an inobt. */
int
xfs_ialloc_count_inodes(
	struct xfs_btree_cur		*cur,
	xfs_agino_t			*count,
	xfs_agino_t			*freecount)
{
	struct xfs_ialloc_count_inodes	ci = {0};
	int				error;

	ASSERT(cur->bc_btnum == XFS_BTNUM_INO);
	error = xfs_btree_query_all(cur, xfs_ialloc_count_inodes_rec, &ci);
	if (error)
		return error;

	*count = ci.count;
	*freecount = ci.freecount;
	return 0;
}

/*
 * Initialize inode-related geometry information.
 *
 * Compute the inode btree min and max levels and set maxicount.
 *
 * Set the inode cluster size.  This may still be overridden by the file
 * system block size if it is larger than the chosen cluster size.
 *
 * For v5 filesystems, scale the cluster size with the inode size to keep a
 * constant ratio of inode per cluster buffer, but only if mkfs has set the
 * inode alignment value appropriately for larger cluster sizes.
 *
 * Then compute the inode cluster alignment information.
 */
void
xfs_ialloc_setup_geometry(
	struct xfs_mount	*mp)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	struct xfs_ino_geometry	*igeo = M_IGEO(mp);
	uint64_t		icount;
	uint			inodes;

	igeo->new_diflags2 = 0;
	if (xfs_has_bigtime(mp))
		igeo->new_diflags2 |= XFS_DIFLAG2_BIGTIME;
	if (xfs_has_large_extent_counts(mp))
		igeo->new_diflags2 |= XFS_DIFLAG2_NREXT64;

	/* Compute inode btree geometry. */
	igeo->agino_log = sbp->sb_inopblog + sbp->sb_agblklog;
	igeo->inobt_mxr[0] = xfs_inobt_maxrecs(mp, sbp->sb_blocksize, 1);
	igeo->inobt_mxr[1] = xfs_inobt_maxrecs(mp, sbp->sb_blocksize, 0);
	igeo->inobt_mnr[0] = igeo->inobt_mxr[0] / 2;
	igeo->inobt_mnr[1] = igeo->inobt_mxr[1] / 2;

	igeo->ialloc_inos = max_t(uint16_t, XFS_INODES_PER_CHUNK,
			sbp->sb_inopblock);
	igeo->ialloc_blks = igeo->ialloc_inos >> sbp->sb_inopblog;

	if (sbp->sb_spino_align)
		igeo->ialloc_min_blks = sbp->sb_spino_align;
	else
		igeo->ialloc_min_blks = igeo->ialloc_blks;

	/* Compute and fill in value of m_ino_geo.inobt_maxlevels. */
	inodes = (1LL << XFS_INO_AGINO_BITS(mp)) >> XFS_INODES_PER_CHUNK_LOG;
	igeo->inobt_maxlevels = xfs_btree_compute_maxlevels(igeo->inobt_mnr,
			inodes);
	ASSERT(igeo->inobt_maxlevels <= xfs_iallocbt_maxlevels_ondisk());

	/*
	 * Set the maximum inode count for this filesystem, being careful not
	 * to use obviously garbage sb_inopblog/sb_inopblock values.  Regular
	 * users should never get here due to failing sb verification, but
	 * certain users (xfs_db) need to be usable even with corrupt metadata.
	 */
	if (sbp->sb_imax_pct && igeo->ialloc_blks) {
		/*
		 * Make sure the maximum inode count is a multiple
		 * of the units we allocate inodes in.
		 */
		icount = sbp->sb_dblocks * sbp->sb_imax_pct;
		do_div(icount, 100);
		do_div(icount, igeo->ialloc_blks);
		igeo->maxicount = XFS_FSB_TO_INO(mp,
				icount * igeo->ialloc_blks);
	} else {
		igeo->maxicount = 0;
	}

	/*
	 * Compute the desired size of an inode cluster buffer size, which
	 * starts at 8K and (on v5 filesystems) scales up with larger inode
	 * sizes.
	 *
	 * Preserve the desired inode cluster size because the sparse inodes
	 * feature uses that desired size (not the actual size) to compute the
	 * sparse inode alignment.  The mount code validates this value, so we
	 * cannot change the behavior.
	 */
	igeo->inode_cluster_size_raw = XFS_INODE_BIG_CLUSTER_SIZE;
	if (xfs_has_v3inodes(mp)) {
		int	new_size = igeo->inode_cluster_size_raw;

		new_size *= mp->m_sb.sb_inodesize / XFS_DINODE_MIN_SIZE;
		if (mp->m_sb.sb_inoalignmt >= XFS_B_TO_FSBT(mp, new_size))
			igeo->inode_cluster_size_raw = new_size;
	}

	/* Calculate inode cluster ratios. */
	if (igeo->inode_cluster_size_raw > mp->m_sb.sb_blocksize)
		igeo->blocks_per_cluster = XFS_B_TO_FSBT(mp,
				igeo->inode_cluster_size_raw);
	else
		igeo->blocks_per_cluster = 1;
	igeo->inode_cluster_size = XFS_FSB_TO_B(mp, igeo->blocks_per_cluster);
	igeo->inodes_per_cluster = XFS_FSB_TO_INO(mp, igeo->blocks_per_cluster);

	/* Calculate inode cluster alignment. */
	if (xfs_has_align(mp) &&
	    mp->m_sb.sb_inoalignmt >= igeo->blocks_per_cluster)
		igeo->cluster_align = mp->m_sb.sb_inoalignmt;
	else
		igeo->cluster_align = 1;
	igeo->inoalign_mask = igeo->cluster_align - 1;
	igeo->cluster_align_inodes = XFS_FSB_TO_INO(mp, igeo->cluster_align);

	/*
	 * If we are using stripe alignment, check whether
	 * the stripe unit is a multiple of the inode alignment
	 */
	if (mp->m_dalign && igeo->inoalign_mask &&
	    !(mp->m_dalign & igeo->inoalign_mask))
		igeo->ialloc_align = mp->m_dalign;
	else
		igeo->ialloc_align = 0;
}

/* Compute the location of the root directory inode that is laid out by mkfs. */
xfs_ino_t
xfs_ialloc_calc_rootino(
	struct xfs_mount	*mp,
	int			sunit)
{
	struct xfs_ino_geometry	*igeo = M_IGEO(mp);
	xfs_agblock_t		first_bno;

	/*
	 * Pre-calculate the geometry of AG 0.  We know what it looks like
	 * because libxfs knows how to create allocation groups now.
	 *
	 * first_bno is the first block in which mkfs could possibly have
	 * allocated the root directory inode, once we factor in the metadata
	 * that mkfs formats before it.  Namely, the four AG headers...
	 */
	first_bno = howmany(4 * mp->m_sb.sb_sectsize, mp->m_sb.sb_blocksize);

	/* ...the two free space btree roots... */
	first_bno += 2;

	/* ...the inode btree root... */
	first_bno += 1;

	/* ...the initial AGFL... */
	first_bno += xfs_alloc_min_freelist(mp, NULL);

	/* ...the free inode btree root... */
	if (xfs_has_finobt(mp))
		first_bno++;

	/* ...the reverse mapping btree root... */
	if (xfs_has_rmapbt(mp))
		first_bno++;

	/* ...the reference count btree... */
	if (xfs_has_reflink(mp))
		first_bno++;

	/*
	 * ...and the log, if it is allocated in the first allocation group.
	 *
	 * This can happen with filesystems that only have a single
	 * allocation group, or very odd geometries created by old mkfs
	 * versions on very small filesystems.
	 */
	if (xfs_ag_contains_log(mp, 0))
		 first_bno += mp->m_sb.sb_logblocks;

	/*
	 * Now round first_bno up to whatever allocation alignment is given
	 * by the filesystem or was passed in.
	 */
	if (xfs_has_dalign(mp) && igeo->ialloc_align > 0)
		first_bno = roundup(first_bno, sunit);
	else if (xfs_has_align(mp) &&
			mp->m_sb.sb_inoalignmt > 1)
		first_bno = roundup(first_bno, mp->m_sb.sb_inoalignmt);

	return XFS_AGINO_TO_INO(mp, 0, XFS_AGB_TO_AGINO(mp, first_bno));
}

/*
 * Ensure there are not sparse inode clusters that cross the new EOAG.
 *
 * This is a no-op for non-spinode filesystems since clusters are always fully
 * allocated and checking the bnobt suffices.  However, a spinode filesystem
 * could have a record where the upper inodes are free blocks.  If those blocks
 * were removed from the filesystem, the inode record would extend beyond EOAG,
 * which will be flagged as corruption.
 */
int
xfs_ialloc_check_shrink(
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	struct xfs_buf		*agibp,
	xfs_agblock_t		new_length)
{
	struct xfs_inobt_rec_incore rec;
	struct xfs_btree_cur	*cur;
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_perag	*pag;
	xfs_agino_t		agino = XFS_AGB_TO_AGINO(mp, new_length);
	int			has;
	int			error;

	if (!xfs_has_sparseinodes(mp))
		return 0;

	pag = xfs_perag_get(mp, agno);
	cur = xfs_inobt_init_cursor(mp, tp, agibp, pag, XFS_BTNUM_INO);

	/* Look up the inobt record that would correspond to the new EOFS. */
	error = xfs_inobt_lookup(cur, agino, XFS_LOOKUP_LE, &has);
	if (error || !has)
		goto out;

	error = xfs_inobt_get_rec(cur, &rec, &has);
	if (error)
		goto out;

	if (!has) {
		error = -EFSCORRUPTED;
		goto out;
	}

	/* If the record covers inodes that would be beyond EOFS, bail out. */
	if (rec.ir_startino + XFS_INODES_PER_CHUNK > agino) {
		error = -ENOSPC;
		goto out;
	}
out:
	xfs_btree_del_cursor(cur, error);
	xfs_perag_put(pag);
	return error;
}
