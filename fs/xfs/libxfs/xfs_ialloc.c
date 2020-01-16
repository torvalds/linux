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
#include "xfs_sb.h"
#include "xfs_mount.h"
#include "xfs_iyesde.h"
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

/*
 * Lookup a record by iyes in the btree given by cur.
 */
int					/* error */
xfs_iyesbt_lookup(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agiyes_t		iyes,	/* starting iyesde of chunk */
	xfs_lookup_t		dir,	/* <=, >=, == */
	int			*stat)	/* success/failure */
{
	cur->bc_rec.i.ir_startiyes = iyes;
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
xfs_iyesbt_update(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_iyesbt_rec_incore_t	*irec)	/* btree record */
{
	union xfs_btree_rec	rec;

	rec.iyesbt.ir_startiyes = cpu_to_be32(irec->ir_startiyes);
	if (xfs_sb_version_hassparseiyesdes(&cur->bc_mp->m_sb)) {
		rec.iyesbt.ir_u.sp.ir_holemask = cpu_to_be16(irec->ir_holemask);
		rec.iyesbt.ir_u.sp.ir_count = irec->ir_count;
		rec.iyesbt.ir_u.sp.ir_freecount = irec->ir_freecount;
	} else {
		/* ir_holemask/ir_count yest supported on-disk */
		rec.iyesbt.ir_u.f.ir_freecount = cpu_to_be32(irec->ir_freecount);
	}
	rec.iyesbt.ir_free = cpu_to_be64(irec->ir_free);
	return xfs_btree_update(cur, &rec);
}

/* Convert on-disk btree record to incore iyesbt record. */
void
xfs_iyesbt_btrec_to_irec(
	struct xfs_mount		*mp,
	union xfs_btree_rec		*rec,
	struct xfs_iyesbt_rec_incore	*irec)
{
	irec->ir_startiyes = be32_to_cpu(rec->iyesbt.ir_startiyes);
	if (xfs_sb_version_hassparseiyesdes(&mp->m_sb)) {
		irec->ir_holemask = be16_to_cpu(rec->iyesbt.ir_u.sp.ir_holemask);
		irec->ir_count = rec->iyesbt.ir_u.sp.ir_count;
		irec->ir_freecount = rec->iyesbt.ir_u.sp.ir_freecount;
	} else {
		/*
		 * ir_holemask/ir_count yest supported on-disk. Fill in hardcoded
		 * values for full iyesde chunks.
		 */
		irec->ir_holemask = XFS_INOBT_HOLEMASK_FULL;
		irec->ir_count = XFS_INODES_PER_CHUNK;
		irec->ir_freecount =
				be32_to_cpu(rec->iyesbt.ir_u.f.ir_freecount);
	}
	irec->ir_free = be64_to_cpu(rec->iyesbt.ir_free);
}

/*
 * Get the data from the pointed-to record.
 */
int
xfs_iyesbt_get_rec(
	struct xfs_btree_cur		*cur,
	struct xfs_iyesbt_rec_incore	*irec,
	int				*stat)
{
	struct xfs_mount		*mp = cur->bc_mp;
	xfs_agnumber_t			agyes = cur->bc_private.a.agyes;
	union xfs_btree_rec		*rec;
	int				error;
	uint64_t			realfree;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (error || *stat == 0)
		return error;

	xfs_iyesbt_btrec_to_irec(mp, rec, irec);

	if (!xfs_verify_agiyes(mp, agyes, irec->ir_startiyes))
		goto out_bad_rec;
	if (irec->ir_count < XFS_INODES_PER_HOLEMASK_BIT ||
	    irec->ir_count > XFS_INODES_PER_CHUNK)
		goto out_bad_rec;
	if (irec->ir_freecount > XFS_INODES_PER_CHUNK)
		goto out_bad_rec;

	/* if there are yes holes, return the first available offset */
	if (!xfs_iyesbt_issparse(irec->ir_holemask))
		realfree = irec->ir_free;
	else
		realfree = irec->ir_free & xfs_iyesbt_irec_to_allocmask(irec);
	if (hweight64(realfree) != irec->ir_freecount)
		goto out_bad_rec;

	return 0;

out_bad_rec:
	xfs_warn(mp,
		"%s Iyesde BTree record corruption in AG %d detected!",
		cur->bc_btnum == XFS_BTNUM_INO ? "Used" : "Free", agyes);
	xfs_warn(mp,
"start iyesde 0x%x, count 0x%x, free 0x%x freemask 0x%llx, holemask 0x%x",
		irec->ir_startiyes, irec->ir_count, irec->ir_freecount,
		irec->ir_free, irec->ir_holemask);
	return -EFSCORRUPTED;
}

/*
 * Insert a single iyesbt record. Cursor must already point to desired location.
 */
int
xfs_iyesbt_insert_rec(
	struct xfs_btree_cur	*cur,
	uint16_t		holemask,
	uint8_t			count,
	int32_t			freecount,
	xfs_iyesfree_t		free,
	int			*stat)
{
	cur->bc_rec.i.ir_holemask = holemask;
	cur->bc_rec.i.ir_count = count;
	cur->bc_rec.i.ir_freecount = freecount;
	cur->bc_rec.i.ir_free = free;
	return xfs_btree_insert(cur, stat);
}

/*
 * Insert records describing a newly allocated iyesde chunk into the iyesbt.
 */
STATIC int
xfs_iyesbt_insert(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_agiyes_t		newiyes,
	xfs_agiyes_t		newlen,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t		agyes = be32_to_cpu(agi->agi_seqyes);
	xfs_agiyes_t		thisiyes;
	int			i;
	int			error;

	cur = xfs_iyesbt_init_cursor(mp, tp, agbp, agyes, btnum);

	for (thisiyes = newiyes;
	     thisiyes < newiyes + newlen;
	     thisiyes += XFS_INODES_PER_CHUNK) {
		error = xfs_iyesbt_lookup(cur, thisiyes, XFS_LOOKUP_EQ, &i);
		if (error) {
			xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
			return error;
		}
		ASSERT(i == 0);

		error = xfs_iyesbt_insert_rec(cur, XFS_INOBT_HOLEMASK_FULL,
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
 * Verify that the number of free iyesdes in the AGI is correct.
 */
#ifdef DEBUG
STATIC int
xfs_check_agi_freecount(
	struct xfs_btree_cur	*cur,
	struct xfs_agi		*agi)
{
	if (cur->bc_nlevels == 1) {
		xfs_iyesbt_rec_incore_t rec;
		int		freecount = 0;
		int		error;
		int		i;

		error = xfs_iyesbt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
		if (error)
			return error;

		do {
			error = xfs_iyesbt_get_rec(cur, &rec, &i);
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
 * Initialise a new set of iyesdes. When called without a transaction context
 * (e.g. from recovery) we initiate a delayed write of the iyesde buffers rather
 * than logging them (which in a transaction context puts them into the AIL
 * for writeback rather than the xfsbufd queue).
 */
int
xfs_ialloc_iyesde_init(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct list_head	*buffer_list,
	int			icount,
	xfs_agnumber_t		agyes,
	xfs_agblock_t		agbyes,
	xfs_agblock_t		length,
	unsigned int		gen)
{
	struct xfs_buf		*fbuf;
	struct xfs_diyesde	*free;
	int			nbufs;
	int			version;
	int			i, j;
	xfs_daddr_t		d;
	xfs_iyes_t		iyes = 0;

	/*
	 * Loop over the new block(s), filling in the iyesdes.  For small block
	 * sizes, manipulate the iyesdes in buffers  which are multiples of the
	 * blocks size.
	 */
	nbufs = length / M_IGEO(mp)->blocks_per_cluster;

	/*
	 * Figure out what version number to use in the iyesdes we create.  If
	 * the superblock version has caught up to the one that supports the new
	 * iyesde format, then use the new iyesde version.  Otherwise use the old
	 * version so that old kernels will continue to be able to use the file
	 * system.
	 *
	 * For v3 iyesdes, we also need to write the iyesde number into the iyesde,
	 * so calculate the first iyesde number of the chunk here as
	 * XFS_AGB_TO_AGINO() only works within a filesystem block, yest
	 * across multiple filesystem blocks (such as a cluster) and so canyest
	 * be used in the cluster buffer loop below.
	 *
	 * Further, because we are writing the iyesde directly into the buffer
	 * and calculating a CRC on the entire iyesde, we have ot log the entire
	 * iyesde so that the entire range the CRC covers is present in the log.
	 * That means for v3 iyesde we log the entire buffer rather than just the
	 * iyesde cores.
	 */
	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		version = 3;
		iyes = XFS_AGINO_TO_INO(mp, agyes, XFS_AGB_TO_AGINO(mp, agbyes));

		/*
		 * log the initialisation that is about to take place as an
		 * logical operation. This means the transaction does yest
		 * need to log the physical changes to the iyesde buffers as log
		 * recovery will kyesw what initialisation is actually needed.
		 * Hence we only need to log the buffers as "ordered" buffers so
		 * they track in the AIL as if they were physically logged.
		 */
		if (tp)
			xfs_icreate_log(tp, agyes, agbyes, icount,
					mp->m_sb.sb_iyesdesize, length, gen);
	} else
		version = 2;

	for (j = 0; j < nbufs; j++) {
		/*
		 * Get the block.
		 */
		d = XFS_AGB_TO_DADDR(mp, agyes, agbyes +
				(j * M_IGEO(mp)->blocks_per_cluster));
		fbuf = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
					 mp->m_bsize *
					 M_IGEO(mp)->blocks_per_cluster,
					 XBF_UNMAPPED);
		if (!fbuf)
			return -ENOMEM;

		/* Initialize the iyesde buffers and log them appropriately. */
		fbuf->b_ops = &xfs_iyesde_buf_ops;
		xfs_buf_zero(fbuf, 0, BBTOB(fbuf->b_length));
		for (i = 0; i < M_IGEO(mp)->iyesdes_per_cluster; i++) {
			int	ioffset = i << mp->m_sb.sb_iyesdelog;
			uint	isize = xfs_diyesde_size(version);

			free = xfs_make_iptr(mp, fbuf, i);
			free->di_magic = cpu_to_be16(XFS_DINODE_MAGIC);
			free->di_version = version;
			free->di_gen = cpu_to_be32(gen);
			free->di_next_unlinked = cpu_to_be32(NULLAGINO);

			if (version == 3) {
				free->di_iyes = cpu_to_be64(iyes);
				iyes++;
				uuid_copy(&free->di_uuid,
					  &mp->m_sb.sb_meta_uuid);
				xfs_diyesde_calc_crc(mp, free);
			} else if (tp) {
				/* just log the iyesde core */
				xfs_trans_log_buf(tp, fbuf, ioffset,
						  ioffset + isize - 1);
			}
		}

		if (tp) {
			/*
			 * Mark the buffer as an iyesde allocation buffer so it
			 * sticks in AIL at the point of this allocation
			 * transaction. This ensures the they are on disk before
			 * the tail of the log can be moved past this
			 * transaction (i.e. by preventing relogging from moving
			 * it forward in the log).
			 */
			xfs_trans_iyesde_alloc_buf(tp, fbuf);
			if (version == 3) {
				/*
				 * Mark the buffer as ordered so that they are
				 * yest physically logged in the transaction but
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
 * Align startiyes and allocmask for a recently allocated sparse chunk such that
 * they are fit for insertion (or merge) into the on-disk iyesde btrees.
 *
 * Background:
 *
 * When enabled, sparse iyesde support increases the iyesde alignment from cluster
 * size to iyesde chunk size. This means that the minimum range between two
 * yesn-adjacent iyesde records in the iyesbt is large eyesugh for a full iyesde
 * record. This allows for cluster sized, cluster aligned block allocation
 * without need to worry about whether the resulting iyesde record overlaps with
 * ayesther record in the tree. Without this basic rule, we would have to deal
 * with the consequences of overlap by potentially undoing recent allocations in
 * the iyesde allocation codepath.
 *
 * Because of this alignment rule (which is enforced on mount), there are two
 * iyesbt possibilities for newly allocated sparse chunks. One is that the
 * aligned iyesde record for the chunk covers a range of iyesdes yest already
 * covered in the iyesbt (i.e., it is safe to insert a new sparse record). The
 * other is that a record already exists at the aligned startiyes that considers
 * the newly allocated range as sparse. In the latter case, record content is
 * merged in hope that sparse iyesde chunks fill to full chunks over time.
 */
STATIC void
xfs_align_sparse_iyes(
	struct xfs_mount		*mp,
	xfs_agiyes_t			*startiyes,
	uint16_t			*allocmask)
{
	xfs_agblock_t			agbyes;
	xfs_agblock_t			mod;
	int				offset;

	agbyes = XFS_AGINO_TO_AGBNO(mp, *startiyes);
	mod = agbyes % mp->m_sb.sb_iyesalignmt;
	if (!mod)
		return;

	/* calculate the iyesde offset and align startiyes */
	offset = XFS_AGB_TO_AGINO(mp, mod);
	*startiyes -= offset;

	/*
	 * Since startiyes has been aligned down, left shift allocmask such that
	 * it continues to represent the same physical iyesdes relative to the
	 * new startiyes.
	 */
	*allocmask <<= offset / XFS_INODES_PER_HOLEMASK_BIT;
}

/*
 * Determine whether the source iyesde record can merge into the target. Both
 * records must be sparse, the iyesde ranges must match and there must be yes
 * allocation overlap between the records.
 */
STATIC bool
__xfs_iyesbt_can_merge(
	struct xfs_iyesbt_rec_incore	*trec,	/* tgt record */
	struct xfs_iyesbt_rec_incore	*srec)	/* src record */
{
	uint64_t			talloc;
	uint64_t			salloc;

	/* records must cover the same iyesde range */
	if (trec->ir_startiyes != srec->ir_startiyes)
		return false;

	/* both records must be sparse */
	if (!xfs_iyesbt_issparse(trec->ir_holemask) ||
	    !xfs_iyesbt_issparse(srec->ir_holemask))
		return false;

	/* both records must track some iyesdes */
	if (!trec->ir_count || !srec->ir_count)
		return false;

	/* can't exceed capacity of a full record */
	if (trec->ir_count + srec->ir_count > XFS_INODES_PER_CHUNK)
		return false;

	/* verify there is yes allocation overlap */
	talloc = xfs_iyesbt_irec_to_allocmask(trec);
	salloc = xfs_iyesbt_irec_to_allocmask(srec);
	if (talloc & salloc)
		return false;

	return true;
}

/*
 * Merge the source iyesde record into the target. The caller must call
 * __xfs_iyesbt_can_merge() to ensure the merge is valid.
 */
STATIC void
__xfs_iyesbt_rec_merge(
	struct xfs_iyesbt_rec_incore	*trec,	/* target */
	struct xfs_iyesbt_rec_incore	*srec)	/* src */
{
	ASSERT(trec->ir_startiyes == srec->ir_startiyes);

	/* combine the counts */
	trec->ir_count += srec->ir_count;
	trec->ir_freecount += srec->ir_freecount;

	/*
	 * Merge the holemask and free mask. For both fields, 0 bits refer to
	 * allocated iyesdes. We combine the allocated ranges with bitwise AND.
	 */
	trec->ir_holemask &= srec->ir_holemask;
	trec->ir_free &= srec->ir_free;
}

/*
 * Insert a new sparse iyesde chunk into the associated iyesde btree. The iyesde
 * record for the sparse chunk is pre-aligned to a startiyes that should match
 * any pre-existing sparse iyesde record in the tree. This allows sparse chunks
 * to fill over time.
 *
 * This function supports two modes of handling preexisting records depending on
 * the merge flag. If merge is true, the provided record is merged with the
 * existing record and updated in place. The merged record is returned in nrec.
 * If merge is false, an existing record is replaced with the provided record.
 * If yes preexisting record exists, the provided record is always inserted.
 *
 * It is considered corruption if a merge is requested and yest possible. Given
 * the sparse iyesde alignment constraints, this should never happen.
 */
STATIC int
xfs_iyesbt_insert_sprec(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	int				btnum,
	struct xfs_iyesbt_rec_incore	*nrec,	/* in/out: new/merged rec. */
	bool				merge)	/* merge or replace */
{
	struct xfs_btree_cur		*cur;
	struct xfs_agi			*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t			agyes = be32_to_cpu(agi->agi_seqyes);
	int				error;
	int				i;
	struct xfs_iyesbt_rec_incore	rec;

	cur = xfs_iyesbt_init_cursor(mp, tp, agbp, agyes, btnum);

	/* the new record is pre-aligned so we kyesw where to look */
	error = xfs_iyesbt_lookup(cur, nrec->ir_startiyes, XFS_LOOKUP_EQ, &i);
	if (error)
		goto error;
	/* if yesthing there, insert a new record and return */
	if (i == 0) {
		error = xfs_iyesbt_insert_rec(cur, nrec->ir_holemask,
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
	 * A record exists at this startiyes. Merge or replace the record
	 * depending on what we've been asked to do.
	 */
	if (merge) {
		error = xfs_iyesbt_get_rec(cur, &rec, &i);
		if (error)
			goto error;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error;
		}
		if (XFS_IS_CORRUPT(mp, rec.ir_startiyes != nrec->ir_startiyes)) {
			error = -EFSCORRUPTED;
			goto error;
		}

		/*
		 * This should never fail. If we have coexisting records that
		 * canyest merge, something is seriously wrong.
		 */
		if (XFS_IS_CORRUPT(mp, !__xfs_iyesbt_can_merge(nrec, &rec))) {
			error = -EFSCORRUPTED;
			goto error;
		}

		trace_xfs_irec_merge_pre(mp, agyes, rec.ir_startiyes,
					 rec.ir_holemask, nrec->ir_startiyes,
					 nrec->ir_holemask);

		/* merge to nrec to output the updated record */
		__xfs_iyesbt_rec_merge(nrec, &rec);

		trace_xfs_irec_merge_post(mp, agyes, nrec->ir_startiyes,
					  nrec->ir_holemask);

		error = xfs_iyesbt_rec_check_count(mp, nrec);
		if (error)
			goto error;
	}

	error = xfs_iyesbt_update(cur, nrec);
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
 * Allocate new iyesdes in the allocation group specified by agbp.
 * Return 0 for success, else error code.
 */
STATIC int
xfs_ialloc_ag_alloc(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	int			*alloc)
{
	struct xfs_agi		*agi;
	struct xfs_alloc_arg	args;
	xfs_agnumber_t		agyes;
	int			error;
	xfs_agiyes_t		newiyes;		/* new first iyesde's number */
	xfs_agiyes_t		newlen;		/* new number of iyesdes */
	int			isaligned = 0;	/* iyesde allocation at stripe */
						/* unit boundary */
	/* init. to full chunk */
	uint16_t		allocmask = (uint16_t) -1;
	struct xfs_iyesbt_rec_incore rec;
	struct xfs_perag	*pag;
	struct xfs_iyes_geometry	*igeo = M_IGEO(tp->t_mountp);
	int			do_sparse = 0;

	memset(&args, 0, sizeof(args));
	args.tp = tp;
	args.mp = tp->t_mountp;
	args.fsbyes = NULLFSBLOCK;
	args.oinfo = XFS_RMAP_OINFO_INODES;

#ifdef DEBUG
	/* randomly do sparse iyesde allocations */
	if (xfs_sb_version_hassparseiyesdes(&tp->t_mountp->m_sb) &&
	    igeo->ialloc_min_blks < igeo->ialloc_blks)
		do_sparse = prandom_u32() & 1;
#endif

	/*
	 * Locking will ensure that we don't have two callers in here
	 * at one time.
	 */
	newlen = igeo->ialloc_iyess;
	if (igeo->maxicount &&
	    percpu_counter_read_positive(&args.mp->m_icount) + newlen >
							igeo->maxicount)
		return -ENOSPC;
	args.minlen = args.maxlen = igeo->ialloc_blks;
	/*
	 * First try to allocate iyesdes contiguous with the last-allocated
	 * chunk of iyesdes.  If the filesystem is striped, this will fill
	 * an entire stripe unit with iyesdes.
	 */
	agi = XFS_BUF_TO_AGI(agbp);
	newiyes = be32_to_cpu(agi->agi_newiyes);
	agyes = be32_to_cpu(agi->agi_seqyes);
	args.agbyes = XFS_AGINO_TO_AGBNO(args.mp, newiyes) +
		     igeo->ialloc_blks;
	if (do_sparse)
		goto sparse_alloc;
	if (likely(newiyes != NULLAGINO &&
		  (args.agbyes < be32_to_cpu(agi->agi_length)))) {
		args.fsbyes = XFS_AGB_TO_FSB(args.mp, agyes, args.agbyes);
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
		 * but yest to use them in the actual exact allocation.
		 */
		args.alignment = 1;
		args.minalignslop = igeo->cluster_align - 1;

		/* Allow space for the iyesde btree to split. */
		args.minleft = igeo->iyesbt_maxlevels - 1;
		if ((error = xfs_alloc_vextent(&args)))
			return error;

		/*
		 * This request might have dirtied the transaction if the AG can
		 * satisfy the request, but the exact block was yest available.
		 * If the allocation did fail, subsequent requests will relax
		 * the exact agbyes requirement and increase the alignment
		 * instead. It is critical that the total size of the request
		 * (len + alignment + slop) does yest increase from this point
		 * on, so reset minalignslop to ensure it is yest included in
		 * subsequent requests.
		 */
		args.minalignslop = 0;
	}

	if (unlikely(args.fsbyes == NULLFSBLOCK)) {
		/*
		 * Set the alignment for the allocation.
		 * If stripe alignment is turned on then align at stripe unit
		 * boundary.
		 * If the cluster size is smaller than a filesystem block
		 * then we're doing I/O for iyesdes in filesystem block size
		 * pieces, so don't need alignment anyway.
		 */
		isaligned = 0;
		if (igeo->ialloc_align) {
			ASSERT(!(args.mp->m_flags & XFS_MOUNT_NOALIGN));
			args.alignment = args.mp->m_dalign;
			isaligned = 1;
		} else
			args.alignment = igeo->cluster_align;
		/*
		 * Need to figure out where to allocate the iyesde blocks.
		 * Ideally they should be spaced out through the a.g.
		 * For yesw, just allocate blocks up front.
		 */
		args.agbyes = be32_to_cpu(agi->agi_root);
		args.fsbyes = XFS_AGB_TO_FSB(args.mp, agyes, args.agbyes);
		/*
		 * Allocate a fixed-size extent of iyesdes.
		 */
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.prod = 1;
		/*
		 * Allow space for the iyesde btree to split.
		 */
		args.minleft = igeo->iyesbt_maxlevels - 1;
		if ((error = xfs_alloc_vextent(&args)))
			return error;
	}

	/*
	 * If stripe alignment is turned on, then try again with cluster
	 * alignment.
	 */
	if (isaligned && args.fsbyes == NULLFSBLOCK) {
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.agbyes = be32_to_cpu(agi->agi_root);
		args.fsbyes = XFS_AGB_TO_FSB(args.mp, agyes, args.agbyes);
		args.alignment = igeo->cluster_align;
		if ((error = xfs_alloc_vextent(&args)))
			return error;
	}

	/*
	 * Finally, try a sparse allocation if the filesystem supports it and
	 * the sparse allocation length is smaller than a full chunk.
	 */
	if (xfs_sb_version_hassparseiyesdes(&args.mp->m_sb) &&
	    igeo->ialloc_min_blks < igeo->ialloc_blks &&
	    args.fsbyes == NULLFSBLOCK) {
sparse_alloc:
		args.type = XFS_ALLOCTYPE_NEAR_BNO;
		args.agbyes = be32_to_cpu(agi->agi_root);
		args.fsbyes = XFS_AGB_TO_FSB(args.mp, agyes, args.agbyes);
		args.alignment = args.mp->m_sb.sb_spiyes_align;
		args.prod = 1;

		args.minlen = igeo->ialloc_min_blks;
		args.maxlen = args.minlen;

		/*
		 * The iyesde record will be aligned to full chunk size. We must
		 * prevent sparse allocation from AG boundaries that result in
		 * invalid iyesde records, such as records that start at agbyes 0
		 * or extend beyond the AG.
		 *
		 * Set min agbyes to the first aligned, yesn-zero agbyes and max to
		 * the last aligned agbyes that is at least one full chunk from
		 * the end of the AG.
		 */
		args.min_agbyes = args.mp->m_sb.sb_iyesalignmt;
		args.max_agbyes = round_down(args.mp->m_sb.sb_agblocks,
					    args.mp->m_sb.sb_iyesalignmt) -
				 igeo->ialloc_blks;

		error = xfs_alloc_vextent(&args);
		if (error)
			return error;

		newlen = XFS_AGB_TO_AGINO(args.mp, args.len);
		ASSERT(newlen <= XFS_INODES_PER_CHUNK);
		allocmask = (1 << (newlen / XFS_INODES_PER_HOLEMASK_BIT)) - 1;
	}

	if (args.fsbyes == NULLFSBLOCK) {
		*alloc = 0;
		return 0;
	}
	ASSERT(args.len == args.minlen);

	/*
	 * Stamp and write the iyesde buffers.
	 *
	 * Seed the new iyesde cluster with a random generation number. This
	 * prevents short-term reuse of generation numbers if a chunk is
	 * freed and then immediately reallocated. We use random numbers
	 * rather than a linear progression to prevent the next generation
	 * number from being easily guessable.
	 */
	error = xfs_ialloc_iyesde_init(args.mp, tp, NULL, newlen, agyes,
			args.agbyes, args.len, prandom_u32());

	if (error)
		return error;
	/*
	 * Convert the results.
	 */
	newiyes = XFS_AGB_TO_AGINO(args.mp, args.agbyes);

	if (xfs_iyesbt_issparse(~allocmask)) {
		/*
		 * We've allocated a sparse chunk. Align the startiyes and mask.
		 */
		xfs_align_sparse_iyes(args.mp, &newiyes, &allocmask);

		rec.ir_startiyes = newiyes;
		rec.ir_holemask = ~allocmask;
		rec.ir_count = newlen;
		rec.ir_freecount = newlen;
		rec.ir_free = XFS_INOBT_ALL_FREE;

		/*
		 * Insert the sparse record into the iyesbt and allow for a merge
		 * if necessary. If a merge does occur, rec is updated to the
		 * merged record.
		 */
		error = xfs_iyesbt_insert_sprec(args.mp, tp, agbp, XFS_BTNUM_INO,
					       &rec, true);
		if (error == -EFSCORRUPTED) {
			xfs_alert(args.mp,
	"invalid sparse iyesde record: iyes 0x%llx holemask 0x%x count %u",
				  XFS_AGINO_TO_INO(args.mp, agyes,
						   rec.ir_startiyes),
				  rec.ir_holemask, rec.ir_count);
			xfs_force_shutdown(args.mp, SHUTDOWN_CORRUPT_INCORE);
		}
		if (error)
			return error;

		/*
		 * We can't merge the part we've just allocated as for the iyesbt
		 * due to fiyesbt semantics. The original record may or may yest
		 * exist independent of whether physical iyesdes exist in this
		 * sparse chunk.
		 *
		 * We must update the fiyesbt record based on the iyesbt record.
		 * rec contains the fully merged and up to date iyesbt record
		 * from the previous call. Set merge false to replace any
		 * existing record with this one.
		 */
		if (xfs_sb_version_hasfiyesbt(&args.mp->m_sb)) {
			error = xfs_iyesbt_insert_sprec(args.mp, tp, agbp,
						       XFS_BTNUM_FINO, &rec,
						       false);
			if (error)
				return error;
		}
	} else {
		/* full chunk - insert new records to both btrees */
		error = xfs_iyesbt_insert(args.mp, tp, agbp, newiyes, newlen,
					 XFS_BTNUM_INO);
		if (error)
			return error;

		if (xfs_sb_version_hasfiyesbt(&args.mp->m_sb)) {
			error = xfs_iyesbt_insert(args.mp, tp, agbp, newiyes,
						 newlen, XFS_BTNUM_FINO);
			if (error)
				return error;
		}
	}

	/*
	 * Update AGI counts and newiyes.
	 */
	be32_add_cpu(&agi->agi_count, newlen);
	be32_add_cpu(&agi->agi_freecount, newlen);
	pag = xfs_perag_get(args.mp, agyes);
	pag->pagi_freecount += newlen;
	pag->pagi_count += newlen;
	xfs_perag_put(pag);
	agi->agi_newiyes = cpu_to_be32(newiyes);

	/*
	 * Log allocation group header fields
	 */
	xfs_ialloc_log_agi(tp, agbp,
		XFS_AGI_COUNT | XFS_AGI_FREECOUNT | XFS_AGI_NEWINO);
	/*
	 * Modify/log superblock values for iyesde count and iyesde free count.
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
	xfs_agnumber_t	agyes;

	spin_lock(&mp->m_agirotor_lock);
	agyes = mp->m_agirotor;
	if (++mp->m_agirotor >= mp->m_maxagi)
		mp->m_agirotor = 0;
	spin_unlock(&mp->m_agirotor_lock);

	return agyes;
}

/*
 * Select an allocation group to look for a free iyesde in, based on the parent
 * iyesde and the mode.  Return the allocation group buffer.
 */
STATIC xfs_agnumber_t
xfs_ialloc_ag_select(
	xfs_trans_t	*tp,		/* transaction pointer */
	xfs_iyes_t	parent,		/* parent directory iyesde number */
	umode_t		mode)		/* bits set to indicate file type */
{
	xfs_agnumber_t	agcount;	/* number of ag's in the filesystem */
	xfs_agnumber_t	agyes;		/* current ag number */
	int		flags;		/* alloc buffer locking flags */
	xfs_extlen_t	ineed;		/* blocks needed for iyesde allocation */
	xfs_extlen_t	longest = 0;	/* longest extent available */
	xfs_mount_t	*mp;		/* mount point structure */
	int		needspace;	/* file mode implies space allocated */
	xfs_perag_t	*pag;		/* per allocation group data */
	xfs_agnumber_t	pagyes;		/* parent (starting) ag number */
	int		error;

	/*
	 * Files of these types need at least one block if length > 0
	 * (and they won't fit in the iyesde, but that's hard to figure out).
	 */
	needspace = S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode);
	mp = tp->t_mountp;
	agcount = mp->m_maxagi;
	if (S_ISDIR(mode))
		pagyes = xfs_ialloc_next_ag(mp);
	else {
		pagyes = XFS_INO_TO_AGNO(mp, parent);
		if (pagyes >= agcount)
			pagyes = 0;
	}

	ASSERT(pagyes < agcount);

	/*
	 * Loop through allocation groups, looking for one with a little
	 * free space in it.  Note we don't look for free iyesdes, exactly.
	 * Instead, we include whether there is a need to allocate iyesdes
	 * to mean that blocks must be allocated for them,
	 * if yesne are currently free.
	 */
	agyes = pagyes;
	flags = XFS_ALLOC_FLAG_TRYLOCK;
	for (;;) {
		pag = xfs_perag_get(mp, agyes);
		if (!pag->pagi_iyesdeok) {
			xfs_ialloc_next_ag(mp);
			goto nextag;
		}

		if (!pag->pagi_init) {
			error = xfs_ialloc_pagi_init(mp, tp, agyes);
			if (error)
				goto nextag;
		}

		if (pag->pagi_freecount) {
			xfs_perag_put(pag);
			return agyes;
		}

		if (!pag->pagf_init) {
			error = xfs_alloc_pagf_init(mp, tp, agyes, flags);
			if (error)
				goto nextag;
		}

		/*
		 * Check that there is eyesugh free space for the file plus a
		 * chunk of iyesdes if we need to allocate some. If this is the
		 * first pass across the AGs, take into account the potential
		 * space needed for alignment of iyesde chunks when checking the
		 * longest contiguous free space in the AG - this prevents us
		 * from getting ENOSPC because we have free space larger than
		 * ialloc_blks but alignment constraints prevent us from using
		 * it.
		 *
		 * If we can't find an AG with space for full alignment slack to
		 * be taken into account, we must be near ENOSPC in all AGs.
		 * Hence we don't include alignment for the second pass and so
		 * if we fail allocation due to alignment issues then it is most
		 * likely a real ENOSPC condition.
		 */
		ineed = M_IGEO(mp)->ialloc_min_blks;
		if (flags && ineed > 1)
			ineed += M_IGEO(mp)->cluster_align;
		longest = pag->pagf_longest;
		if (!longest)
			longest = pag->pagf_flcount > 0;

		if (pag->pagf_freeblks >= needspace + ineed &&
		    longest >= ineed) {
			xfs_perag_put(pag);
			return agyes;
		}
nextag:
		xfs_perag_put(pag);
		/*
		 * No point in iterating over the rest, if we're shutting
		 * down.
		 */
		if (XFS_FORCED_SHUTDOWN(mp))
			return NULLAGNUMBER;
		agyes++;
		if (agyes >= agcount)
			agyes = 0;
		if (agyes == pagyes) {
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
	xfs_iyesbt_rec_incore_t	*rec,
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
		error = xfs_iyesbt_get_rec(cur, rec, &i);
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
	xfs_agiyes_t		agiyes,
	xfs_iyesbt_rec_incore_t	*rec,
	int			*done)
{
	int                     error;
	int			i;

	error = xfs_iyesbt_lookup(cur, agiyes, XFS_LOOKUP_EQ, &i);
	if (error)
		return error;
	*done = !i;
	if (i) {
		error = xfs_iyesbt_get_rec(cur, rec, &i);
		if (error)
			return error;
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
			return -EFSCORRUPTED;
	}

	return 0;
}

/*
 * Return the offset of the first free iyesde in the record. If the iyesde chunk
 * is sparsely allocated, we convert the record holemask to iyesde granularity
 * and mask off the unallocated regions from the iyesde free mask.
 */
STATIC int
xfs_iyesbt_first_free_iyesde(
	struct xfs_iyesbt_rec_incore	*rec)
{
	xfs_iyesfree_t			realfree;

	/* if there are yes holes, return the first available offset */
	if (!xfs_iyesbt_issparse(rec->ir_holemask))
		return xfs_lowbit64(rec->ir_free);

	realfree = xfs_iyesbt_irec_to_allocmask(rec);
	realfree &= rec->ir_free;

	return xfs_lowbit64(realfree);
}

/*
 * Allocate an iyesde using the iyesbt-only algorithm.
 */
STATIC int
xfs_dialloc_ag_iyesbt(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_iyes_t		parent,
	xfs_iyes_t		*iyesp)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agi		*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t		agyes = be32_to_cpu(agi->agi_seqyes);
	xfs_agnumber_t		pagyes = XFS_INO_TO_AGNO(mp, parent);
	xfs_agiyes_t		pagiyes = XFS_INO_TO_AGINO(mp, parent);
	struct xfs_perag	*pag;
	struct xfs_btree_cur	*cur, *tcur;
	struct xfs_iyesbt_rec_incore rec, trec;
	xfs_iyes_t		iyes;
	int			error;
	int			offset;
	int			i, j;
	int			searchdistance = 10;

	pag = xfs_perag_get(mp, agyes);

	ASSERT(pag->pagi_init);
	ASSERT(pag->pagi_iyesdeok);
	ASSERT(pag->pagi_freecount > 0);

 restart_pagyes:
	cur = xfs_iyesbt_init_cursor(mp, tp, agbp, agyes, XFS_BTNUM_INO);
	/*
	 * If pagiyes is 0 (this is the root iyesde allocation) use newiyes.
	 * This must work because we've just allocated some.
	 */
	if (!pagiyes)
		pagiyes = be32_to_cpu(agi->agi_newiyes);

	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error0;

	/*
	 * If in the same AG as the parent, try to get near the parent.
	 */
	if (pagyes == agyes) {
		int		doneleft;	/* done, to the left */
		int		doneright;	/* done, to the right */

		error = xfs_iyesbt_lookup(cur, pagiyes, XFS_LOOKUP_LE, &i);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		error = xfs_iyesbt_get_rec(cur, &rec, &j);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(mp, j != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		if (rec.ir_freecount > 0) {
			/*
			 * Found a free iyesde in the same chunk
			 * as the parent, done.
			 */
			goto alloc_iyesde;
		}


		/*
		 * In the same AG as parent, but parent's chunk is full.
		 */

		/* duplicate the cursor, search left & right simultaneously */
		error = xfs_btree_dup_cursor(cur, &tcur);
		if (error)
			goto error0;

		/*
		 * Skip to last blocks looked up if same parent iyesde.
		 */
		if (pagiyes != NULLAGINO &&
		    pag->pagl_pagiyes == pagiyes &&
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
		 * Loop until we find an iyesde chunk with a free iyesde.
		 */
		while (--searchdistance > 0 && (!doneleft || !doneright)) {
			int	useleft;  /* using left iyesde chunk this time */

			/* figure out the closer block if both are valid. */
			if (!doneleft && !doneright) {
				useleft = pagiyes -
				 (trec.ir_startiyes + XFS_INODES_PER_CHUNK - 1) <
				  rec.ir_startiyes - pagiyes;
			} else {
				useleft = !doneleft;
			}

			/* free iyesdes to the left? */
			if (useleft && trec.ir_freecount) {
				xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
				cur = tcur;

				pag->pagl_leftrec = trec.ir_startiyes;
				pag->pagl_rightrec = rec.ir_startiyes;
				pag->pagl_pagiyes = pagiyes;
				rec = trec;
				goto alloc_iyesde;
			}

			/* free iyesdes to the right? */
			if (!useleft && rec.ir_freecount) {
				xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);

				pag->pagl_leftrec = trec.ir_startiyes;
				pag->pagl_rightrec = rec.ir_startiyes;
				pag->pagl_pagiyes = pagiyes;
				goto alloc_iyesde;
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
			 * location and allocate a new iyesde
			 */
			xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
			pag->pagl_leftrec = trec.ir_startiyes;
			pag->pagl_rightrec = rec.ir_startiyes;
			pag->pagl_pagiyes = pagiyes;

		} else {
			/*
			 * We've reached the end of the btree. because
			 * we are only searching a small chunk of the
			 * btree each search, there is obviously free
			 * iyesdes closer to the parent iyesde than we
			 * are yesw. restart the search again.
			 */
			pag->pagl_pagiyes = NULLAGINO;
			pag->pagl_leftrec = NULLAGINO;
			pag->pagl_rightrec = NULLAGINO;
			xfs_btree_del_cursor(tcur, XFS_BTREE_NOERROR);
			xfs_btree_del_cursor(cur, XFS_BTREE_NOERROR);
			goto restart_pagyes;
		}
	}

	/*
	 * In a different AG from the parent.
	 * See if the most recently allocated block has any free.
	 */
	if (agi->agi_newiyes != cpu_to_be32(NULLAGINO)) {
		error = xfs_iyesbt_lookup(cur, be32_to_cpu(agi->agi_newiyes),
					 XFS_LOOKUP_EQ, &i);
		if (error)
			goto error0;

		if (i == 1) {
			error = xfs_iyesbt_get_rec(cur, &rec, &j);
			if (error)
				goto error0;

			if (j == 1 && rec.ir_freecount > 0) {
				/*
				 * The last chunk allocated in the group
				 * still has a free iyesde.
				 */
				goto alloc_iyesde;
			}
		}
	}

	/*
	 * None left in the last group, search the whole AG
	 */
	error = xfs_iyesbt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
	if (error)
		goto error0;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}

	for (;;) {
		error = xfs_iyesbt_get_rec(cur, &rec, &i);
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

alloc_iyesde:
	offset = xfs_iyesbt_first_free_iyesde(&rec);
	ASSERT(offset >= 0);
	ASSERT(offset < XFS_INODES_PER_CHUNK);
	ASSERT((XFS_AGINO_TO_OFFSET(mp, rec.ir_startiyes) %
				   XFS_INODES_PER_CHUNK) == 0);
	iyes = XFS_AGINO_TO_INO(mp, agyes, rec.ir_startiyes + offset);
	rec.ir_free &= ~XFS_INOBT_MASK(offset);
	rec.ir_freecount--;
	error = xfs_iyesbt_update(cur, &rec);
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
	*iyesp = iyes;
	return 0;
error1:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
error0:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	xfs_perag_put(pag);
	return error;
}

/*
 * Use the free iyesde btree to allocate an iyesde based on distance from the
 * parent. Note that the provided cursor may be deleted and replaced.
 */
STATIC int
xfs_dialloc_ag_fiyesbt_near(
	xfs_agiyes_t			pagiyes,
	struct xfs_btree_cur		**ocur,
	struct xfs_iyesbt_rec_incore	*rec)
{
	struct xfs_btree_cur		*lcur = *ocur;	/* left search cursor */
	struct xfs_btree_cur		*rcur;	/* right search cursor */
	struct xfs_iyesbt_rec_incore	rrec;
	int				error;
	int				i, j;

	error = xfs_iyesbt_lookup(lcur, pagiyes, XFS_LOOKUP_LE, &i);
	if (error)
		return error;

	if (i == 1) {
		error = xfs_iyesbt_get_rec(lcur, rec, &i);
		if (error)
			return error;
		if (XFS_IS_CORRUPT(lcur->bc_mp, i != 1))
			return -EFSCORRUPTED;

		/*
		 * See if we've landed in the parent iyesde record. The fiyesbt
		 * only tracks chunks with at least one free iyesde, so record
		 * existence is eyesugh.
		 */
		if (pagiyes >= rec->ir_startiyes &&
		    pagiyes < (rec->ir_startiyes + XFS_INODES_PER_CHUNK))
			return 0;
	}

	error = xfs_btree_dup_cursor(lcur, &rcur);
	if (error)
		return error;

	error = xfs_iyesbt_lookup(rcur, pagiyes, XFS_LOOKUP_GE, &j);
	if (error)
		goto error_rcur;
	if (j == 1) {
		error = xfs_iyesbt_get_rec(rcur, &rrec, &j);
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
		 * iyesde chunk to the target.
		 */
		if ((pagiyes - rec->ir_startiyes + XFS_INODES_PER_CHUNK - 1) >
		    (rrec.ir_startiyes - pagiyes)) {
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
 * Use the free iyesde btree to find a free iyesde based on a newiyes hint. If
 * the hint is NULL, find the first free iyesde in the AG.
 */
STATIC int
xfs_dialloc_ag_fiyesbt_newiyes(
	struct xfs_agi			*agi,
	struct xfs_btree_cur		*cur,
	struct xfs_iyesbt_rec_incore	*rec)
{
	int error;
	int i;

	if (agi->agi_newiyes != cpu_to_be32(NULLAGINO)) {
		error = xfs_iyesbt_lookup(cur, be32_to_cpu(agi->agi_newiyes),
					 XFS_LOOKUP_EQ, &i);
		if (error)
			return error;
		if (i == 1) {
			error = xfs_iyesbt_get_rec(cur, rec, &i);
			if (error)
				return error;
			if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
				return -EFSCORRUPTED;
			return 0;
		}
	}

	/*
	 * Find the first iyesde available in the AG.
	 */
	error = xfs_iyesbt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

	error = xfs_iyesbt_get_rec(cur, rec, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

	return 0;
}

/*
 * Update the iyesbt based on a modification made to the fiyesbt. Also ensure that
 * the records from both trees are equivalent post-modification.
 */
STATIC int
xfs_dialloc_ag_update_iyesbt(
	struct xfs_btree_cur		*cur,	/* iyesbt cursor */
	struct xfs_iyesbt_rec_incore	*frec,	/* fiyesbt record */
	int				offset) /* iyesde offset */
{
	struct xfs_iyesbt_rec_incore	rec;
	int				error;
	int				i;

	error = xfs_iyesbt_lookup(cur, frec->ir_startiyes, XFS_LOOKUP_EQ, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

	error = xfs_iyesbt_get_rec(cur, &rec, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;
	ASSERT((XFS_AGINO_TO_OFFSET(cur->bc_mp, rec.ir_startiyes) %
				   XFS_INODES_PER_CHUNK) == 0);

	rec.ir_free &= ~XFS_INOBT_MASK(offset);
	rec.ir_freecount--;

	if (XFS_IS_CORRUPT(cur->bc_mp,
			   rec.ir_free != frec->ir_free ||
			   rec.ir_freecount != frec->ir_freecount))
		return -EFSCORRUPTED;

	return xfs_iyesbt_update(cur, &rec);
}

/*
 * Allocate an iyesde using the free iyesde btree, if available. Otherwise, fall
 * back to the iyesbt search algorithm.
 *
 * The caller selected an AG for us, and made sure that free iyesdes are
 * available.
 */
STATIC int
xfs_dialloc_ag(
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_iyes_t		parent,
	xfs_iyes_t		*iyesp)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_agi			*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t			agyes = be32_to_cpu(agi->agi_seqyes);
	xfs_agnumber_t			pagyes = XFS_INO_TO_AGNO(mp, parent);
	xfs_agiyes_t			pagiyes = XFS_INO_TO_AGINO(mp, parent);
	struct xfs_perag		*pag;
	struct xfs_btree_cur		*cur;	/* fiyesbt cursor */
	struct xfs_btree_cur		*icur;	/* iyesbt cursor */
	struct xfs_iyesbt_rec_incore	rec;
	xfs_iyes_t			iyes;
	int				error;
	int				offset;
	int				i;

	if (!xfs_sb_version_hasfiyesbt(&mp->m_sb))
		return xfs_dialloc_ag_iyesbt(tp, agbp, parent, iyesp);

	pag = xfs_perag_get(mp, agyes);

	/*
	 * If pagiyes is 0 (this is the root iyesde allocation) use newiyes.
	 * This must work because we've just allocated some.
	 */
	if (!pagiyes)
		pagiyes = be32_to_cpu(agi->agi_newiyes);

	cur = xfs_iyesbt_init_cursor(mp, tp, agbp, agyes, XFS_BTNUM_FINO);

	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error_cur;

	/*
	 * The search algorithm depends on whether we're in the same AG as the
	 * parent. If so, find the closest available iyesde to the parent. If
	 * yest, consider the agi hint or find the first free iyesde in the AG.
	 */
	if (agyes == pagyes)
		error = xfs_dialloc_ag_fiyesbt_near(pagiyes, &cur, &rec);
	else
		error = xfs_dialloc_ag_fiyesbt_newiyes(agi, cur, &rec);
	if (error)
		goto error_cur;

	offset = xfs_iyesbt_first_free_iyesde(&rec);
	ASSERT(offset >= 0);
	ASSERT(offset < XFS_INODES_PER_CHUNK);
	ASSERT((XFS_AGINO_TO_OFFSET(mp, rec.ir_startiyes) %
				   XFS_INODES_PER_CHUNK) == 0);
	iyes = XFS_AGINO_TO_INO(mp, agyes, rec.ir_startiyes + offset);

	/*
	 * Modify or remove the fiyesbt record.
	 */
	rec.ir_free &= ~XFS_INOBT_MASK(offset);
	rec.ir_freecount--;
	if (rec.ir_freecount)
		error = xfs_iyesbt_update(cur, &rec);
	else
		error = xfs_btree_delete(cur, &i);
	if (error)
		goto error_cur;

	/*
	 * The fiyesbt has yesw been updated appropriately. We haven't updated the
	 * agi and superblock yet, so we can create an iyesbt cursor and validate
	 * the original freecount. If all is well, make the equivalent update to
	 * the iyesbt using the fiyesbt record and offset information.
	 */
	icur = xfs_iyesbt_init_cursor(mp, tp, agbp, agyes, XFS_BTNUM_INO);

	error = xfs_check_agi_freecount(icur, agi);
	if (error)
		goto error_icur;

	error = xfs_dialloc_ag_update_iyesbt(icur, &rec, offset);
	if (error)
		goto error_icur;

	/*
	 * Both trees have yesw been updated. We must update the perag and
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
	*iyesp = iyes;
	return 0;

error_icur:
	xfs_btree_del_cursor(icur, XFS_BTREE_ERROR);
error_cur:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	xfs_perag_put(pag);
	return error;
}

/*
 * Allocate an iyesde on disk.
 *
 * Mode is used to tell whether the new iyesde will need space, and whether it
 * is a directory.
 *
 * This function is designed to be called twice if it has to do an allocation
 * to make more free iyesdes.  On the first call, *IO_agbp should be set to NULL.
 * If an iyesde is available without having to performn an allocation, an iyesde
 * number is returned.  In this case, *IO_agbp is set to NULL.  If an allocation
 * needs to be done, xfs_dialloc returns the current AGI buffer in *IO_agbp.
 * The caller should then commit the current transaction, allocate a
 * new transaction, and call xfs_dialloc() again, passing in the previous value
 * of *IO_agbp.  IO_agbp should be held across the transactions. Since the AGI
 * buffer is locked across the two calls, the second call is guaranteed to have
 * a free iyesde available.
 *
 * Once we successfully pick an iyesde its number is returned and the on-disk
 * data structures are updated.  The iyesde itself is yest read in, since doing so
 * would break ordering constraints with xfs_reclaim.
 */
int
xfs_dialloc(
	struct xfs_trans	*tp,
	xfs_iyes_t		parent,
	umode_t			mode,
	struct xfs_buf		**IO_agbp,
	xfs_iyes_t		*iyesp)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_buf		*agbp;
	xfs_agnumber_t		agyes;
	int			error;
	int			ialloced;
	int			yesroom = 0;
	xfs_agnumber_t		start_agyes;
	struct xfs_perag	*pag;
	struct xfs_iyes_geometry	*igeo = M_IGEO(mp);
	int			okalloc = 1;

	if (*IO_agbp) {
		/*
		 * If the caller passes in a pointer to the AGI buffer,
		 * continue where we left off before.  In this case, we
		 * kyesw that the allocation group has free iyesdes.
		 */
		agbp = *IO_agbp;
		goto out_alloc;
	}

	/*
	 * We do yest have an agbp, so select an initial allocation
	 * group for iyesde allocation.
	 */
	start_agyes = xfs_ialloc_ag_select(tp, parent, mode);
	if (start_agyes == NULLAGNUMBER) {
		*iyesp = NULLFSINO;
		return 0;
	}

	/*
	 * If we have already hit the ceiling of iyesde blocks then clear
	 * okalloc so we scan all available agi structures for a free
	 * iyesde.
	 *
	 * Read rough value of mp->m_icount by percpu_counter_read_positive,
	 * which will sacrifice the preciseness but improve the performance.
	 */
	if (igeo->maxicount &&
	    percpu_counter_read_positive(&mp->m_icount) + igeo->ialloc_iyess
							> igeo->maxicount) {
		yesroom = 1;
		okalloc = 0;
	}

	/*
	 * Loop until we find an allocation group that either has free iyesdes
	 * or in which we can allocate some iyesdes.  Iterate through the
	 * allocation groups upward, wrapping at the end.
	 */
	agyes = start_agyes;
	for (;;) {
		pag = xfs_perag_get(mp, agyes);
		if (!pag->pagi_iyesdeok) {
			xfs_ialloc_next_ag(mp);
			goto nextag;
		}

		if (!pag->pagi_init) {
			error = xfs_ialloc_pagi_init(mp, tp, agyes);
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
		error = xfs_ialloc_read_agi(mp, tp, agyes, &agbp);
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
			*iyesp = NULLFSINO;
			return 0;
		}

		if (ialloced) {
			/*
			 * We successfully allocated some iyesdes, return
			 * the current context to the caller so that it
			 * can commit the current transaction and call
			 * us again where we left off.
			 */
			ASSERT(pag->pagi_freecount > 0);
			xfs_perag_put(pag);

			*IO_agbp = agbp;
			*iyesp = NULLFSINO;
			return 0;
		}

nextag_relse_buffer:
		xfs_trans_brelse(tp, agbp);
nextag:
		xfs_perag_put(pag);
		if (++agyes == mp->m_sb.sb_agcount)
			agyes = 0;
		if (agyes == start_agyes) {
			*iyesp = NULLFSINO;
			return yesroom ? -ENOSPC : 0;
		}
	}

out_alloc:
	*IO_agbp = NULL;
	return xfs_dialloc_ag(tp, agbp, parent, iyesp);
out_error:
	xfs_perag_put(pag);
	return error;
}

/*
 * Free the blocks of an iyesde chunk. We must consider that the iyesde chunk
 * might be sparse and only free the regions that are allocated as part of the
 * chunk.
 */
STATIC void
xfs_difree_iyesde_chunk(
	struct xfs_trans		*tp,
	xfs_agnumber_t			agyes,
	struct xfs_iyesbt_rec_incore	*rec)
{
	struct xfs_mount		*mp = tp->t_mountp;
	xfs_agblock_t			sagbyes = XFS_AGINO_TO_AGBNO(mp,
							rec->ir_startiyes);
	int				startidx, endidx;
	int				nextbit;
	xfs_agblock_t			agbyes;
	int				contigblk;
	DECLARE_BITMAP(holemask, XFS_INOBT_HOLEMASK_BITS);

	if (!xfs_iyesbt_issparse(rec->ir_holemask)) {
		/* yest sparse, calculate extent info directly */
		xfs_bmap_add_free(tp, XFS_AGB_TO_FSB(mp, agyes, sagbyes),
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
		 * nextbit is yest contiguous with the current end index. Convert
		 * the current start/end to an extent and add it to the free
		 * list.
		 */
		agbyes = sagbyes + (startidx * XFS_INODES_PER_HOLEMASK_BIT) /
				  mp->m_sb.sb_iyespblock;
		contigblk = ((endidx - startidx + 1) *
			     XFS_INODES_PER_HOLEMASK_BIT) /
			    mp->m_sb.sb_iyespblock;

		ASSERT(agbyes % mp->m_sb.sb_spiyes_align == 0);
		ASSERT(contigblk % mp->m_sb.sb_spiyes_align == 0);
		xfs_bmap_add_free(tp, XFS_AGB_TO_FSB(mp, agyes, agbyes),
				  contigblk, &XFS_RMAP_OINFO_INODES);

		/* reset range to current bit and carry on... */
		startidx = endidx = nextbit;

next:
		nextbit++;
	}
}

STATIC int
xfs_difree_iyesbt(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	xfs_agiyes_t			agiyes,
	struct xfs_icluster		*xic,
	struct xfs_iyesbt_rec_incore	*orec)
{
	struct xfs_agi			*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t			agyes = be32_to_cpu(agi->agi_seqyes);
	struct xfs_perag		*pag;
	struct xfs_btree_cur		*cur;
	struct xfs_iyesbt_rec_incore	rec;
	int				ilen;
	int				error;
	int				i;
	int				off;

	ASSERT(agi->agi_magicnum == cpu_to_be32(XFS_AGI_MAGIC));
	ASSERT(XFS_AGINO_TO_AGBNO(mp, agiyes) < be32_to_cpu(agi->agi_length));

	/*
	 * Initialize the cursor.
	 */
	cur = xfs_iyesbt_init_cursor(mp, tp, agbp, agyes, XFS_BTNUM_INO);

	error = xfs_check_agi_freecount(cur, agi);
	if (error)
		goto error0;

	/*
	 * Look for the entry describing this iyesde.
	 */
	if ((error = xfs_iyesbt_lookup(cur, agiyes, XFS_LOOKUP_LE, &i))) {
		xfs_warn(mp, "%s: xfs_iyesbt_lookup() returned error %d.",
			__func__, error);
		goto error0;
	}
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	error = xfs_iyesbt_get_rec(cur, &rec, &i);
	if (error) {
		xfs_warn(mp, "%s: xfs_iyesbt_get_rec() returned error %d.",
			__func__, error);
		goto error0;
	}
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	/*
	 * Get the offset in the iyesde chunk.
	 */
	off = agiyes - rec.ir_startiyes;
	ASSERT(off >= 0 && off < XFS_INODES_PER_CHUNK);
	ASSERT(!(rec.ir_free & XFS_INOBT_MASK(off)));
	/*
	 * Mark the iyesde free & increment the count.
	 */
	rec.ir_free |= XFS_INOBT_MASK(off);
	rec.ir_freecount++;

	/*
	 * When an iyesde chunk is free, it becomes eligible for removal. Don't
	 * remove the chunk if the block size is large eyesugh for multiple iyesde
	 * chunks (that might yest be free).
	 */
	if (!(mp->m_flags & XFS_MOUNT_IKEEP) &&
	    rec.ir_free == XFS_INOBT_ALL_FREE &&
	    mp->m_sb.sb_iyespblock <= XFS_INODES_PER_CHUNK) {
		xic->deleted = true;
		xic->first_iyes = XFS_AGINO_TO_INO(mp, agyes, rec.ir_startiyes);
		xic->alloc = xfs_iyesbt_irec_to_allocmask(&rec);

		/*
		 * Remove the iyesde cluster from the AGI B+Tree, adjust the
		 * AGI and Superblock iyesde counts, and mark the disk space
		 * to be freed when the transaction is committed.
		 */
		ilen = rec.ir_freecount;
		be32_add_cpu(&agi->agi_count, -ilen);
		be32_add_cpu(&agi->agi_freecount, -(ilen - 1));
		xfs_ialloc_log_agi(tp, agbp, XFS_AGI_COUNT | XFS_AGI_FREECOUNT);
		pag = xfs_perag_get(mp, agyes);
		pag->pagi_freecount -= ilen - 1;
		pag->pagi_count -= ilen;
		xfs_perag_put(pag);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_ICOUNT, -ilen);
		xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -(ilen - 1));

		if ((error = xfs_btree_delete(cur, &i))) {
			xfs_warn(mp, "%s: xfs_btree_delete returned error %d.",
				__func__, error);
			goto error0;
		}

		xfs_difree_iyesde_chunk(tp, agyes, &rec);
	} else {
		xic->deleted = false;

		error = xfs_iyesbt_update(cur, &rec);
		if (error) {
			xfs_warn(mp, "%s: xfs_iyesbt_update returned error %d.",
				__func__, error);
			goto error0;
		}

		/* 
		 * Change the iyesde free counts and log the ag/sb changes.
		 */
		be32_add_cpu(&agi->agi_freecount, 1);
		xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
		pag = xfs_perag_get(mp, agyes);
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
 * Free an iyesde in the free iyesde btree.
 */
STATIC int
xfs_difree_fiyesbt(
	struct xfs_mount		*mp,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	xfs_agiyes_t			agiyes,
	struct xfs_iyesbt_rec_incore	*ibtrec) /* iyesbt record */
{
	struct xfs_agi			*agi = XFS_BUF_TO_AGI(agbp);
	xfs_agnumber_t			agyes = be32_to_cpu(agi->agi_seqyes);
	struct xfs_btree_cur		*cur;
	struct xfs_iyesbt_rec_incore	rec;
	int				offset = agiyes - ibtrec->ir_startiyes;
	int				error;
	int				i;

	cur = xfs_iyesbt_init_cursor(mp, tp, agbp, agyes, XFS_BTNUM_FINO);

	error = xfs_iyesbt_lookup(cur, ibtrec->ir_startiyes, XFS_LOOKUP_EQ, &i);
	if (error)
		goto error;
	if (i == 0) {
		/*
		 * If the record does yest exist in the fiyesbt, we must have just
		 * freed an iyesde in a previously fully allocated chunk. If yest,
		 * something is out of sync.
		 */
		if (XFS_IS_CORRUPT(mp, ibtrec->ir_freecount != 1)) {
			error = -EFSCORRUPTED;
			goto error;
		}

		error = xfs_iyesbt_insert_rec(cur, ibtrec->ir_holemask,
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
	 * to ayesther.
	 */
	error = xfs_iyesbt_get_rec(cur, &rec, &i);
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
	 * The content of iyesbt records should always match between the iyesbt
	 * and fiyesbt. The lifecycle of records in the fiyesbt is different from
	 * the iyesbt in that the fiyesbt only tracks records with at least one
	 * free iyesde. Hence, if all of the iyesdes are free and we aren't
	 * keeping iyesde chunks permanently on disk, remove the record.
	 * Otherwise, update the record with the new information.
	 *
	 * Note that we currently can't free chunks when the block size is large
	 * eyesugh for multiple chunks. Leave the fiyesbt record to remain in sync
	 * with the iyesbt.
	 */
	if (rec.ir_free == XFS_INOBT_ALL_FREE &&
	    mp->m_sb.sb_iyespblock <= XFS_INODES_PER_CHUNK &&
	    !(mp->m_flags & XFS_MOUNT_IKEEP)) {
		error = xfs_btree_delete(cur, &i);
		if (error)
			goto error;
		ASSERT(i == 1);
	} else {
		error = xfs_iyesbt_update(cur, &rec);
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
 * Free disk iyesde.  Carefully avoids touching the incore iyesde, all
 * manipulations incore are the caller's responsibility.
 * The on-disk iyesde is yest changed by this operation, only the
 * btree (free iyesde mask) is changed.
 */
int
xfs_difree(
	struct xfs_trans	*tp,		/* transaction pointer */
	xfs_iyes_t		iyesde,		/* iyesde to be freed */
	struct xfs_icluster	*xic)	/* cluster info if deleted */
{
	/* REFERENCED */
	xfs_agblock_t		agbyes;	/* block number containing iyesde */
	struct xfs_buf		*agbp;	/* buffer for allocation group header */
	xfs_agiyes_t		agiyes;	/* allocation group iyesde number */
	xfs_agnumber_t		agyes;	/* allocation group number */
	int			error;	/* error return value */
	struct xfs_mount	*mp;	/* mount structure for filesystem */
	struct xfs_iyesbt_rec_incore rec;/* btree record */

	mp = tp->t_mountp;

	/*
	 * Break up iyesde number into its components.
	 */
	agyes = XFS_INO_TO_AGNO(mp, iyesde);
	if (agyes >= mp->m_sb.sb_agcount)  {
		xfs_warn(mp, "%s: agyes >= mp->m_sb.sb_agcount (%d >= %d).",
			__func__, agyes, mp->m_sb.sb_agcount);
		ASSERT(0);
		return -EINVAL;
	}
	agiyes = XFS_INO_TO_AGINO(mp, iyesde);
	if (iyesde != XFS_AGINO_TO_INO(mp, agyes, agiyes))  {
		xfs_warn(mp, "%s: iyesde != XFS_AGINO_TO_INO() (%llu != %llu).",
			__func__, (unsigned long long)iyesde,
			(unsigned long long)XFS_AGINO_TO_INO(mp, agyes, agiyes));
		ASSERT(0);
		return -EINVAL;
	}
	agbyes = XFS_AGINO_TO_AGBNO(mp, agiyes);
	if (agbyes >= mp->m_sb.sb_agblocks)  {
		xfs_warn(mp, "%s: agbyes >= mp->m_sb.sb_agblocks (%d >= %d).",
			__func__, agbyes, mp->m_sb.sb_agblocks);
		ASSERT(0);
		return -EINVAL;
	}
	/*
	 * Get the allocation group header.
	 */
	error = xfs_ialloc_read_agi(mp, tp, agyes, &agbp);
	if (error) {
		xfs_warn(mp, "%s: xfs_ialloc_read_agi() returned error %d.",
			__func__, error);
		return error;
	}

	/*
	 * Fix up the iyesde allocation btree.
	 */
	error = xfs_difree_iyesbt(mp, tp, agbp, agiyes, xic, &rec);
	if (error)
		goto error0;

	/*
	 * Fix up the free iyesde btree.
	 */
	if (xfs_sb_version_hasfiyesbt(&mp->m_sb)) {
		error = xfs_difree_fiyesbt(mp, tp, agbp, agiyes, &rec);
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
	xfs_agnumber_t		agyes,
	xfs_agiyes_t		agiyes,
	xfs_agblock_t		agbyes,
	xfs_agblock_t		*chunk_agbyes,
	xfs_agblock_t		*offset_agbyes,
	int			flags)
{
	struct xfs_iyesbt_rec_incore rec;
	struct xfs_btree_cur	*cur;
	struct xfs_buf		*agbp;
	int			error;
	int			i;

	error = xfs_ialloc_read_agi(mp, tp, agyes, &agbp);
	if (error) {
		xfs_alert(mp,
			"%s: xfs_ialloc_read_agi() returned error %d, agyes %d",
			__func__, error, agyes);
		return error;
	}

	/*
	 * Lookup the iyesde record for the given agiyes. If the record canyest be
	 * found, then it's an invalid iyesde number and we should abort. Once
	 * we have a record, we need to ensure it contains the iyesde number
	 * we are looking up.
	 */
	cur = xfs_iyesbt_init_cursor(mp, tp, agbp, agyes, XFS_BTNUM_INO);
	error = xfs_iyesbt_lookup(cur, agiyes, XFS_LOOKUP_LE, &i);
	if (!error) {
		if (i)
			error = xfs_iyesbt_get_rec(cur, &rec, &i);
		if (!error && i == 0)
			error = -EINVAL;
	}

	xfs_trans_brelse(tp, agbp);
	xfs_btree_del_cursor(cur, error);
	if (error)
		return error;

	/* check that the returned record contains the required iyesde */
	if (rec.ir_startiyes > agiyes ||
	    rec.ir_startiyes + M_IGEO(mp)->ialloc_iyess <= agiyes)
		return -EINVAL;

	/* for untrusted iyesdes check it is allocated first */
	if ((flags & XFS_IGET_UNTRUSTED) &&
	    (rec.ir_free & XFS_INOBT_MASK(agiyes - rec.ir_startiyes)))
		return -EINVAL;

	*chunk_agbyes = XFS_AGINO_TO_AGBNO(mp, rec.ir_startiyes);
	*offset_agbyes = agbyes - *chunk_agbyes;
	return 0;
}

/*
 * Return the location of the iyesde in imap, for mapping it into a buffer.
 */
int
xfs_imap(
	xfs_mount_t	 *mp,	/* file system mount structure */
	xfs_trans_t	 *tp,	/* transaction pointer */
	xfs_iyes_t	iyes,	/* iyesde to locate */
	struct xfs_imap	*imap,	/* location map structure */
	uint		flags)	/* flags for iyesde btree lookup */
{
	xfs_agblock_t	agbyes;	/* block number of iyesde in the alloc group */
	xfs_agiyes_t	agiyes;	/* iyesde number within alloc group */
	xfs_agnumber_t	agyes;	/* allocation group number */
	xfs_agblock_t	chunk_agbyes;	/* first block in iyesde chunk */
	xfs_agblock_t	cluster_agbyes;	/* first block in iyesde cluster */
	int		error;	/* error code */
	int		offset;	/* index of iyesde in its buffer */
	xfs_agblock_t	offset_agbyes;	/* blks from chunk start to iyesde */

	ASSERT(iyes != NULLFSINO);

	/*
	 * Split up the iyesde number into its parts.
	 */
	agyes = XFS_INO_TO_AGNO(mp, iyes);
	agiyes = XFS_INO_TO_AGINO(mp, iyes);
	agbyes = XFS_AGINO_TO_AGBNO(mp, agiyes);
	if (agyes >= mp->m_sb.sb_agcount || agbyes >= mp->m_sb.sb_agblocks ||
	    iyes != XFS_AGINO_TO_INO(mp, agyes, agiyes)) {
#ifdef DEBUG
		/*
		 * Don't output diagyesstic information for untrusted iyesdes
		 * as they can be invalid without implying corruption.
		 */
		if (flags & XFS_IGET_UNTRUSTED)
			return -EINVAL;
		if (agyes >= mp->m_sb.sb_agcount) {
			xfs_alert(mp,
				"%s: agyes (%d) >= mp->m_sb.sb_agcount (%d)",
				__func__, agyes, mp->m_sb.sb_agcount);
		}
		if (agbyes >= mp->m_sb.sb_agblocks) {
			xfs_alert(mp,
		"%s: agbyes (0x%llx) >= mp->m_sb.sb_agblocks (0x%lx)",
				__func__, (unsigned long long)agbyes,
				(unsigned long)mp->m_sb.sb_agblocks);
		}
		if (iyes != XFS_AGINO_TO_INO(mp, agyes, agiyes)) {
			xfs_alert(mp,
		"%s: iyes (0x%llx) != XFS_AGINO_TO_INO() (0x%llx)",
				__func__, iyes,
				XFS_AGINO_TO_INO(mp, agyes, agiyes));
		}
		xfs_stack_trace();
#endif /* DEBUG */
		return -EINVAL;
	}

	/*
	 * For bulkstat and handle lookups, we have an untrusted iyesde number
	 * that we have to verify is valid. We canyest do this just by reading
	 * the iyesde buffer as it may have been unlinked and removed leaving
	 * iyesdes in stale state on disk. Hence we have to do a btree lookup
	 * in all cases where an untrusted iyesde number is passed.
	 */
	if (flags & XFS_IGET_UNTRUSTED) {
		error = xfs_imap_lookup(mp, tp, agyes, agiyes, agbyes,
					&chunk_agbyes, &offset_agbyes, flags);
		if (error)
			return error;
		goto out_map;
	}

	/*
	 * If the iyesde cluster size is the same as the blocksize or
	 * smaller we get to the buffer by simple arithmetics.
	 */
	if (M_IGEO(mp)->blocks_per_cluster == 1) {
		offset = XFS_INO_TO_OFFSET(mp, iyes);
		ASSERT(offset < mp->m_sb.sb_iyespblock);

		imap->im_blkyes = XFS_AGB_TO_DADDR(mp, agyes, agbyes);
		imap->im_len = XFS_FSB_TO_BB(mp, 1);
		imap->im_boffset = (unsigned short)(offset <<
							mp->m_sb.sb_iyesdelog);
		return 0;
	}

	/*
	 * If the iyesde chunks are aligned then use simple maths to
	 * find the location. Otherwise we have to do a btree
	 * lookup to find the location.
	 */
	if (M_IGEO(mp)->iyesalign_mask) {
		offset_agbyes = agbyes & M_IGEO(mp)->iyesalign_mask;
		chunk_agbyes = agbyes - offset_agbyes;
	} else {
		error = xfs_imap_lookup(mp, tp, agyes, agiyes, agbyes,
					&chunk_agbyes, &offset_agbyes, flags);
		if (error)
			return error;
	}

out_map:
	ASSERT(agbyes >= chunk_agbyes);
	cluster_agbyes = chunk_agbyes +
		((offset_agbyes / M_IGEO(mp)->blocks_per_cluster) *
		 M_IGEO(mp)->blocks_per_cluster);
	offset = ((agbyes - cluster_agbyes) * mp->m_sb.sb_iyespblock) +
		XFS_INO_TO_OFFSET(mp, iyes);

	imap->im_blkyes = XFS_AGB_TO_DADDR(mp, agyes, cluster_agbyes);
	imap->im_len = XFS_FSB_TO_BB(mp, M_IGEO(mp)->blocks_per_cluster);
	imap->im_boffset = (unsigned short)(offset << mp->m_sb.sb_iyesdelog);

	/*
	 * If the iyesde number maps to a block outside the bounds
	 * of the file system then return NULL rather than calling
	 * read_buf and panicing when we get an error from the
	 * driver.
	 */
	if ((imap->im_blkyes + imap->im_len) >
	    XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks)) {
		xfs_alert(mp,
	"%s: (im_blkyes (0x%llx) + im_len (0x%llx)) > sb_dblocks (0x%llx)",
			__func__, (unsigned long long) imap->im_blkyes,
			(unsigned long long) imap->im_len,
			XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks));
		return -EINVAL;
	}
	return 0;
}

/*
 * Log specified fields for the ag hdr (iyesde section). The growth of the agi
 * structure over time requires that we interpret the buffer as two logical
 * regions delineated by the end of the unlinked list. This is due to the size
 * of the hash table and its location in the middle of the agi.
 *
 * For example, a request to log a field before agi_unlinked and a field after
 * agi_unlinked could cause us to log the entire hash table and use an excessive
 * amount of log space. To avoid this behavior, log the region up through
 * agi_unlinked in one call and the region after agi_unlinked through the end of
 * the structure in ayesther.
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
		offsetof(xfs_agi_t, agi_seqyes),
		offsetof(xfs_agi_t, agi_length),
		offsetof(xfs_agi_t, agi_count),
		offsetof(xfs_agi_t, agi_root),
		offsetof(xfs_agi_t, agi_level),
		offsetof(xfs_agi_t, agi_freecount),
		offsetof(xfs_agi_t, agi_newiyes),
		offsetof(xfs_agi_t, agi_diriyes),
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
	struct xfs_agi	*agi = XFS_BUF_TO_AGI(bp);
	int		i;

	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		if (!uuid_equal(&agi->agi_uuid, &mp->m_sb.sb_meta_uuid))
			return __this_address;
		if (!xfs_log_check_lsn(mp,
				be64_to_cpu(XFS_BUF_TO_AGI(bp)->agi_lsn)))
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
	    be32_to_cpu(agi->agi_level) > XFS_BTREE_MAXLEVELS)
		return __this_address;

	if (xfs_sb_version_hasfiyesbt(&mp->m_sb) &&
	    (be32_to_cpu(agi->agi_free_level) < 1 ||
	     be32_to_cpu(agi->agi_free_level) > XFS_BTREE_MAXLEVELS))
		return __this_address;

	/*
	 * during growfs operations, the perag is yest fully initialised,
	 * so we can't use it for any useful checking. growfs ensures we can't
	 * use it by using uncached buffers that don't have the perag attached
	 * so we can detect and avoid this problem.
	 */
	if (bp->b_pag && be32_to_cpu(agi->agi_seqyes) != bp->b_pag->pag_agyes)
		return __this_address;

	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		if (agi->agi_unlinked[i] == cpu_to_be32(NULLAGINO))
			continue;
		if (!xfs_verify_iyes(mp, be32_to_cpu(agi->agi_unlinked[i])))
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

	if (xfs_sb_version_hascrc(&mp->m_sb) &&
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
	xfs_failaddr_t		fa;

	fa = xfs_agi_verify(bp);
	if (fa) {
		xfs_verifier_error(bp, -EFSCORRUPTED, fa);
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
	.magic = { cpu_to_be32(XFS_AGI_MAGIC), cpu_to_be32(XFS_AGI_MAGIC) },
	.verify_read = xfs_agi_read_verify,
	.verify_write = xfs_agi_write_verify,
	.verify_struct = xfs_agi_verify,
};

/*
 * Read in the allocation group header (iyesde allocation section)
 */
int
xfs_read_agi(
	struct xfs_mount	*mp,	/* file system mount structure */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_agnumber_t		agyes,	/* allocation group number */
	struct xfs_buf		**bpp)	/* allocation group hdr buf */
{
	int			error;

	trace_xfs_read_agi(mp, agyes);

	ASSERT(agyes != NULLAGNUMBER);
	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, agyes, XFS_AGI_DADDR(mp)),
			XFS_FSS_TO_BB(mp, 1), 0, bpp, &xfs_agi_buf_ops);
	if (error)
		return error;
	if (tp)
		xfs_trans_buf_set_type(tp, *bpp, XFS_BLFT_AGI_BUF);

	xfs_buf_set_ref(*bpp, XFS_AGI_REF);
	return 0;
}

int
xfs_ialloc_read_agi(
	struct xfs_mount	*mp,	/* file system mount structure */
	struct xfs_trans	*tp,	/* transaction pointer */
	xfs_agnumber_t		agyes,	/* allocation group number */
	struct xfs_buf		**bpp)	/* allocation group hdr buf */
{
	struct xfs_agi		*agi;	/* allocation group header */
	struct xfs_perag	*pag;	/* per allocation group data */
	int			error;

	trace_xfs_ialloc_read_agi(mp, agyes);

	error = xfs_read_agi(mp, tp, agyes, bpp);
	if (error)
		return error;

	agi = XFS_BUF_TO_AGI(*bpp);
	pag = xfs_perag_get(mp, agyes);
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
	xfs_agnumber_t	agyes)		/* allocation group number */
{
	xfs_buf_t	*bp = NULL;
	int		error;

	error = xfs_ialloc_read_agi(mp, tp, agyes, &bp);
	if (error)
		return error;
	if (bp)
		xfs_trans_brelse(tp, bp);
	return 0;
}

/* Is there an iyesde record covering a given range of iyesde numbers? */
int
xfs_ialloc_has_iyesde_record(
	struct xfs_btree_cur	*cur,
	xfs_agiyes_t		low,
	xfs_agiyes_t		high,
	bool			*exists)
{
	struct xfs_iyesbt_rec_incore	irec;
	xfs_agiyes_t		agiyes;
	uint16_t		holemask;
	int			has_record;
	int			i;
	int			error;

	*exists = false;
	error = xfs_iyesbt_lookup(cur, low, XFS_LOOKUP_LE, &has_record);
	while (error == 0 && has_record) {
		error = xfs_iyesbt_get_rec(cur, &irec, &has_record);
		if (error || irec.ir_startiyes > high)
			break;

		agiyes = irec.ir_startiyes;
		holemask = irec.ir_holemask;
		for (i = 0; i < XFS_INOBT_HOLEMASK_BITS; holemask >>= 1,
				i++, agiyes += XFS_INODES_PER_HOLEMASK_BIT) {
			if (holemask & 1)
				continue;
			if (agiyes + XFS_INODES_PER_HOLEMASK_BIT > low &&
					agiyes <= high) {
				*exists = true;
				return 0;
			}
		}

		error = xfs_btree_increment(cur, 0, &has_record);
	}
	return error;
}

/* Is there an iyesde record covering a given extent? */
int
xfs_ialloc_has_iyesdes_at_extent(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		byes,
	xfs_extlen_t		len,
	bool			*exists)
{
	xfs_agiyes_t		low;
	xfs_agiyes_t		high;

	low = XFS_AGB_TO_AGINO(cur->bc_mp, byes);
	high = XFS_AGB_TO_AGINO(cur->bc_mp, byes + len) - 1;

	return xfs_ialloc_has_iyesde_record(cur, low, high, exists);
}

struct xfs_ialloc_count_iyesdes {
	xfs_agiyes_t			count;
	xfs_agiyes_t			freecount;
};

/* Record iyesde counts across all iyesbt records. */
STATIC int
xfs_ialloc_count_iyesdes_rec(
	struct xfs_btree_cur		*cur,
	union xfs_btree_rec		*rec,
	void				*priv)
{
	struct xfs_iyesbt_rec_incore	irec;
	struct xfs_ialloc_count_iyesdes	*ci = priv;

	xfs_iyesbt_btrec_to_irec(cur->bc_mp, rec, &irec);
	ci->count += irec.ir_count;
	ci->freecount += irec.ir_freecount;

	return 0;
}

/* Count allocated and free iyesdes under an iyesbt. */
int
xfs_ialloc_count_iyesdes(
	struct xfs_btree_cur		*cur,
	xfs_agiyes_t			*count,
	xfs_agiyes_t			*freecount)
{
	struct xfs_ialloc_count_iyesdes	ci = {0};
	int				error;

	ASSERT(cur->bc_btnum == XFS_BTNUM_INO);
	error = xfs_btree_query_all(cur, xfs_ialloc_count_iyesdes_rec, &ci);
	if (error)
		return error;

	*count = ci.count;
	*freecount = ci.freecount;
	return 0;
}

/*
 * Initialize iyesde-related geometry information.
 *
 * Compute the iyesde btree min and max levels and set maxicount.
 *
 * Set the iyesde cluster size.  This may still be overridden by the file
 * system block size if it is larger than the chosen cluster size.
 *
 * For v5 filesystems, scale the cluster size with the iyesde size to keep a
 * constant ratio of iyesde per cluster buffer, but only if mkfs has set the
 * iyesde alignment value appropriately for larger cluster sizes.
 *
 * Then compute the iyesde cluster alignment information.
 */
void
xfs_ialloc_setup_geometry(
	struct xfs_mount	*mp)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	struct xfs_iyes_geometry	*igeo = M_IGEO(mp);
	uint64_t		icount;
	uint			iyesdes;

	/* Compute iyesde btree geometry. */
	igeo->agiyes_log = sbp->sb_iyespblog + sbp->sb_agblklog;
	igeo->iyesbt_mxr[0] = xfs_iyesbt_maxrecs(mp, sbp->sb_blocksize, 1);
	igeo->iyesbt_mxr[1] = xfs_iyesbt_maxrecs(mp, sbp->sb_blocksize, 0);
	igeo->iyesbt_mnr[0] = igeo->iyesbt_mxr[0] / 2;
	igeo->iyesbt_mnr[1] = igeo->iyesbt_mxr[1] / 2;

	igeo->ialloc_iyess = max_t(uint16_t, XFS_INODES_PER_CHUNK,
			sbp->sb_iyespblock);
	igeo->ialloc_blks = igeo->ialloc_iyess >> sbp->sb_iyespblog;

	if (sbp->sb_spiyes_align)
		igeo->ialloc_min_blks = sbp->sb_spiyes_align;
	else
		igeo->ialloc_min_blks = igeo->ialloc_blks;

	/* Compute and fill in value of m_iyes_geo.iyesbt_maxlevels. */
	iyesdes = (1LL << XFS_INO_AGINO_BITS(mp)) >> XFS_INODES_PER_CHUNK_LOG;
	igeo->iyesbt_maxlevels = xfs_btree_compute_maxlevels(igeo->iyesbt_mnr,
			iyesdes);

	/*
	 * Set the maximum iyesde count for this filesystem, being careful yest
	 * to use obviously garbage sb_iyespblog/sb_iyespblock values.  Regular
	 * users should never get here due to failing sb verification, but
	 * certain users (xfs_db) need to be usable even with corrupt metadata.
	 */
	if (sbp->sb_imax_pct && igeo->ialloc_blks) {
		/*
		 * Make sure the maximum iyesde count is a multiple
		 * of the units we allocate iyesdes in.
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
	 * Compute the desired size of an iyesde cluster buffer size, which
	 * starts at 8K and (on v5 filesystems) scales up with larger iyesde
	 * sizes.
	 *
	 * Preserve the desired iyesde cluster size because the sparse iyesdes
	 * feature uses that desired size (yest the actual size) to compute the
	 * sparse iyesde alignment.  The mount code validates this value, so we
	 * canyest change the behavior.
	 */
	igeo->iyesde_cluster_size_raw = XFS_INODE_BIG_CLUSTER_SIZE;
	if (xfs_sb_version_hascrc(&mp->m_sb)) {
		int	new_size = igeo->iyesde_cluster_size_raw;

		new_size *= mp->m_sb.sb_iyesdesize / XFS_DINODE_MIN_SIZE;
		if (mp->m_sb.sb_iyesalignmt >= XFS_B_TO_FSBT(mp, new_size))
			igeo->iyesde_cluster_size_raw = new_size;
	}

	/* Calculate iyesde cluster ratios. */
	if (igeo->iyesde_cluster_size_raw > mp->m_sb.sb_blocksize)
		igeo->blocks_per_cluster = XFS_B_TO_FSBT(mp,
				igeo->iyesde_cluster_size_raw);
	else
		igeo->blocks_per_cluster = 1;
	igeo->iyesde_cluster_size = XFS_FSB_TO_B(mp, igeo->blocks_per_cluster);
	igeo->iyesdes_per_cluster = XFS_FSB_TO_INO(mp, igeo->blocks_per_cluster);

	/* Calculate iyesde cluster alignment. */
	if (xfs_sb_version_hasalign(&mp->m_sb) &&
	    mp->m_sb.sb_iyesalignmt >= igeo->blocks_per_cluster)
		igeo->cluster_align = mp->m_sb.sb_iyesalignmt;
	else
		igeo->cluster_align = 1;
	igeo->iyesalign_mask = igeo->cluster_align - 1;
	igeo->cluster_align_iyesdes = XFS_FSB_TO_INO(mp, igeo->cluster_align);

	/*
	 * If we are using stripe alignment, check whether
	 * the stripe unit is a multiple of the iyesde alignment
	 */
	if (mp->m_dalign && igeo->iyesalign_mask &&
	    !(mp->m_dalign & igeo->iyesalign_mask))
		igeo->ialloc_align = mp->m_dalign;
	else
		igeo->ialloc_align = 0;
}

/* Compute the location of the root directory iyesde that is laid out by mkfs. */
xfs_iyes_t
xfs_ialloc_calc_rootiyes(
	struct xfs_mount	*mp,
	int			sunit)
{
	struct xfs_iyes_geometry	*igeo = M_IGEO(mp);
	xfs_agblock_t		first_byes;

	/*
	 * Pre-calculate the geometry of AG 0.  We kyesw what it looks like
	 * because libxfs kyesws how to create allocation groups yesw.
	 *
	 * first_byes is the first block in which mkfs could possibly have
	 * allocated the root directory iyesde, once we factor in the metadata
	 * that mkfs formats before it.  Namely, the four AG headers...
	 */
	first_byes = howmany(4 * mp->m_sb.sb_sectsize, mp->m_sb.sb_blocksize);

	/* ...the two free space btree roots... */
	first_byes += 2;

	/* ...the iyesde btree root... */
	first_byes += 1;

	/* ...the initial AGFL... */
	first_byes += xfs_alloc_min_freelist(mp, NULL);

	/* ...the free iyesde btree root... */
	if (xfs_sb_version_hasfiyesbt(&mp->m_sb))
		first_byes++;

	/* ...the reverse mapping btree root... */
	if (xfs_sb_version_hasrmapbt(&mp->m_sb))
		first_byes++;

	/* ...the reference count btree... */
	if (xfs_sb_version_hasreflink(&mp->m_sb))
		first_byes++;

	/*
	 * ...and the log, if it is allocated in the first allocation group.
	 *
	 * This can happen with filesystems that only have a single
	 * allocation group, or very odd geometries created by old mkfs
	 * versions on very small filesystems.
	 */
	if (mp->m_sb.sb_logstart &&
	    XFS_FSB_TO_AGNO(mp, mp->m_sb.sb_logstart) == 0)
		 first_byes += mp->m_sb.sb_logblocks;

	/*
	 * Now round first_byes up to whatever allocation alignment is given
	 * by the filesystem or was passed in.
	 */
	if (xfs_sb_version_hasdalign(&mp->m_sb) && igeo->ialloc_align > 0)
		first_byes = roundup(first_byes, sunit);
	else if (xfs_sb_version_hasalign(&mp->m_sb) &&
			mp->m_sb.sb_iyesalignmt > 1)
		first_byes = roundup(first_byes, mp->m_sb.sb_iyesalignmt);

	return XFS_AGINO_TO_INO(mp, 0, XFS_AGB_TO_AGINO(mp, first_byes));
}
