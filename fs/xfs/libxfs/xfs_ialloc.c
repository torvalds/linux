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
#include "xfs_ianalde.h"
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
 * Lookup a record by ianal in the btree given by cur.
 */
int					/* error */
xfs_ianalbt_lookup(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_agianal_t		ianal,	/* starting ianalde of chunk */
	xfs_lookup_t		dir,	/* <=, >=, == */
	int			*stat)	/* success/failure */
{
	cur->bc_rec.i.ir_startianal = ianal;
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
xfs_ianalbt_update(
	struct xfs_btree_cur	*cur,	/* btree cursor */
	xfs_ianalbt_rec_incore_t	*irec)	/* btree record */
{
	union xfs_btree_rec	rec;

	rec.ianalbt.ir_startianal = cpu_to_be32(irec->ir_startianal);
	if (xfs_has_sparseianaldes(cur->bc_mp)) {
		rec.ianalbt.ir_u.sp.ir_holemask = cpu_to_be16(irec->ir_holemask);
		rec.ianalbt.ir_u.sp.ir_count = irec->ir_count;
		rec.ianalbt.ir_u.sp.ir_freecount = irec->ir_freecount;
	} else {
		/* ir_holemask/ir_count analt supported on-disk */
		rec.ianalbt.ir_u.f.ir_freecount = cpu_to_be32(irec->ir_freecount);
	}
	rec.ianalbt.ir_free = cpu_to_be64(irec->ir_free);
	return xfs_btree_update(cur, &rec);
}

/* Convert on-disk btree record to incore ianalbt record. */
void
xfs_ianalbt_btrec_to_irec(
	struct xfs_mount		*mp,
	const union xfs_btree_rec	*rec,
	struct xfs_ianalbt_rec_incore	*irec)
{
	irec->ir_startianal = be32_to_cpu(rec->ianalbt.ir_startianal);
	if (xfs_has_sparseianaldes(mp)) {
		irec->ir_holemask = be16_to_cpu(rec->ianalbt.ir_u.sp.ir_holemask);
		irec->ir_count = rec->ianalbt.ir_u.sp.ir_count;
		irec->ir_freecount = rec->ianalbt.ir_u.sp.ir_freecount;
	} else {
		/*
		 * ir_holemask/ir_count analt supported on-disk. Fill in hardcoded
		 * values for full ianalde chunks.
		 */
		irec->ir_holemask = XFS_IANALBT_HOLEMASK_FULL;
		irec->ir_count = XFS_IANALDES_PER_CHUNK;
		irec->ir_freecount =
				be32_to_cpu(rec->ianalbt.ir_u.f.ir_freecount);
	}
	irec->ir_free = be64_to_cpu(rec->ianalbt.ir_free);
}

/* Compute the freecount of an incore ianalde record. */
uint8_t
xfs_ianalbt_rec_freecount(
	const struct xfs_ianalbt_rec_incore	*irec)
{
	uint64_t				realfree = irec->ir_free;

	if (xfs_ianalbt_issparse(irec->ir_holemask))
		realfree &= xfs_ianalbt_irec_to_allocmask(irec);
	return hweight64(realfree);
}

/* Simple checks for ianalde records. */
xfs_failaddr_t
xfs_ianalbt_check_irec(
	struct xfs_perag			*pag,
	const struct xfs_ianalbt_rec_incore	*irec)
{
	/* Record has to be properly aligned within the AG. */
	if (!xfs_verify_agianal(pag, irec->ir_startianal))
		return __this_address;
	if (!xfs_verify_agianal(pag,
				irec->ir_startianal + XFS_IANALDES_PER_CHUNK - 1))
		return __this_address;
	if (irec->ir_count < XFS_IANALDES_PER_HOLEMASK_BIT ||
	    irec->ir_count > XFS_IANALDES_PER_CHUNK)
		return __this_address;
	if (irec->ir_freecount > XFS_IANALDES_PER_CHUNK)
		return __this_address;

	if (xfs_ianalbt_rec_freecount(irec) != irec->ir_freecount)
		return __this_address;

	return NULL;
}

static inline int
xfs_ianalbt_complain_bad_rec(
	struct xfs_btree_cur		*cur,
	xfs_failaddr_t			fa,
	const struct xfs_ianalbt_rec_incore *irec)
{
	struct xfs_mount		*mp = cur->bc_mp;

	xfs_warn(mp,
		"%s Ianalde BTree record corruption in AG %d detected at %pS!",
		cur->bc_btnum == XFS_BTNUM_IANAL ? "Used" : "Free",
		cur->bc_ag.pag->pag_aganal, fa);
	xfs_warn(mp,
"start ianalde 0x%x, count 0x%x, free 0x%x freemask 0x%llx, holemask 0x%x",
		irec->ir_startianal, irec->ir_count, irec->ir_freecount,
		irec->ir_free, irec->ir_holemask);
	return -EFSCORRUPTED;
}

/*
 * Get the data from the pointed-to record.
 */
int
xfs_ianalbt_get_rec(
	struct xfs_btree_cur		*cur,
	struct xfs_ianalbt_rec_incore	*irec,
	int				*stat)
{
	struct xfs_mount		*mp = cur->bc_mp;
	union xfs_btree_rec		*rec;
	xfs_failaddr_t			fa;
	int				error;

	error = xfs_btree_get_rec(cur, &rec, stat);
	if (error || *stat == 0)
		return error;

	xfs_ianalbt_btrec_to_irec(mp, rec, irec);
	fa = xfs_ianalbt_check_irec(cur->bc_ag.pag, irec);
	if (fa)
		return xfs_ianalbt_complain_bad_rec(cur, fa, irec);

	return 0;
}

/*
 * Insert a single ianalbt record. Cursor must already point to desired location.
 */
int
xfs_ianalbt_insert_rec(
	struct xfs_btree_cur	*cur,
	uint16_t		holemask,
	uint8_t			count,
	int32_t			freecount,
	xfs_ianalfree_t		free,
	int			*stat)
{
	cur->bc_rec.i.ir_holemask = holemask;
	cur->bc_rec.i.ir_count = count;
	cur->bc_rec.i.ir_freecount = freecount;
	cur->bc_rec.i.ir_free = free;
	return xfs_btree_insert(cur, stat);
}

/*
 * Insert records describing a newly allocated ianalde chunk into the ianalbt.
 */
STATIC int
xfs_ianalbt_insert(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_agianal_t		newianal,
	xfs_agianal_t		newlen,
	xfs_btnum_t		btnum)
{
	struct xfs_btree_cur	*cur;
	xfs_agianal_t		thisianal;
	int			i;
	int			error;

	cur = xfs_ianalbt_init_cursor(pag, tp, agbp, btnum);

	for (thisianal = newianal;
	     thisianal < newianal + newlen;
	     thisianal += XFS_IANALDES_PER_CHUNK) {
		error = xfs_ianalbt_lookup(cur, thisianal, XFS_LOOKUP_EQ, &i);
		if (error) {
			xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
			return error;
		}
		ASSERT(i == 0);

		error = xfs_ianalbt_insert_rec(cur, XFS_IANALBT_HOLEMASK_FULL,
					     XFS_IANALDES_PER_CHUNK,
					     XFS_IANALDES_PER_CHUNK,
					     XFS_IANALBT_ALL_FREE, &i);
		if (error) {
			xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
			return error;
		}
		ASSERT(i == 1);
	}

	xfs_btree_del_cursor(cur, XFS_BTREE_ANALERROR);

	return 0;
}

/*
 * Verify that the number of free ianaldes in the AGI is correct.
 */
#ifdef DEBUG
static int
xfs_check_agi_freecount(
	struct xfs_btree_cur	*cur)
{
	if (cur->bc_nlevels == 1) {
		xfs_ianalbt_rec_incore_t rec;
		int		freecount = 0;
		int		error;
		int		i;

		error = xfs_ianalbt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
		if (error)
			return error;

		do {
			error = xfs_ianalbt_get_rec(cur, &rec, &i);
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
 * Initialise a new set of ianaldes. When called without a transaction context
 * (e.g. from recovery) we initiate a delayed write of the ianalde buffers rather
 * than logging them (which in a transaction context puts them into the AIL
 * for writeback rather than the xfsbufd queue).
 */
int
xfs_ialloc_ianalde_init(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct list_head	*buffer_list,
	int			icount,
	xfs_agnumber_t		aganal,
	xfs_agblock_t		agbanal,
	xfs_agblock_t		length,
	unsigned int		gen)
{
	struct xfs_buf		*fbuf;
	struct xfs_dianalde	*free;
	int			nbufs;
	int			version;
	int			i, j;
	xfs_daddr_t		d;
	xfs_ianal_t		ianal = 0;
	int			error;

	/*
	 * Loop over the new block(s), filling in the ianaldes.  For small block
	 * sizes, manipulate the ianaldes in buffers  which are multiples of the
	 * blocks size.
	 */
	nbufs = length / M_IGEO(mp)->blocks_per_cluster;

	/*
	 * Figure out what version number to use in the ianaldes we create.  If
	 * the superblock version has caught up to the one that supports the new
	 * ianalde format, then use the new ianalde version.  Otherwise use the old
	 * version so that old kernels will continue to be able to use the file
	 * system.
	 *
	 * For v3 ianaldes, we also need to write the ianalde number into the ianalde,
	 * so calculate the first ianalde number of the chunk here as
	 * XFS_AGB_TO_AGIANAL() only works within a filesystem block, analt
	 * across multiple filesystem blocks (such as a cluster) and so cananalt
	 * be used in the cluster buffer loop below.
	 *
	 * Further, because we are writing the ianalde directly into the buffer
	 * and calculating a CRC on the entire ianalde, we have ot log the entire
	 * ianalde so that the entire range the CRC covers is present in the log.
	 * That means for v3 ianalde we log the entire buffer rather than just the
	 * ianalde cores.
	 */
	if (xfs_has_v3ianaldes(mp)) {
		version = 3;
		ianal = XFS_AGIANAL_TO_IANAL(mp, aganal, XFS_AGB_TO_AGIANAL(mp, agbanal));

		/*
		 * log the initialisation that is about to take place as an
		 * logical operation. This means the transaction does analt
		 * need to log the physical changes to the ianalde buffers as log
		 * recovery will kanalw what initialisation is actually needed.
		 * Hence we only need to log the buffers as "ordered" buffers so
		 * they track in the AIL as if they were physically logged.
		 */
		if (tp)
			xfs_icreate_log(tp, aganal, agbanal, icount,
					mp->m_sb.sb_ianaldesize, length, gen);
	} else
		version = 2;

	for (j = 0; j < nbufs; j++) {
		/*
		 * Get the block.
		 */
		d = XFS_AGB_TO_DADDR(mp, aganal, agbanal +
				(j * M_IGEO(mp)->blocks_per_cluster));
		error = xfs_trans_get_buf(tp, mp->m_ddev_targp, d,
				mp->m_bsize * M_IGEO(mp)->blocks_per_cluster,
				XBF_UNMAPPED, &fbuf);
		if (error)
			return error;

		/* Initialize the ianalde buffers and log them appropriately. */
		fbuf->b_ops = &xfs_ianalde_buf_ops;
		xfs_buf_zero(fbuf, 0, BBTOB(fbuf->b_length));
		for (i = 0; i < M_IGEO(mp)->ianaldes_per_cluster; i++) {
			int	ioffset = i << mp->m_sb.sb_ianaldelog;

			free = xfs_make_iptr(mp, fbuf, i);
			free->di_magic = cpu_to_be16(XFS_DIANALDE_MAGIC);
			free->di_version = version;
			free->di_gen = cpu_to_be32(gen);
			free->di_next_unlinked = cpu_to_be32(NULLAGIANAL);

			if (version == 3) {
				free->di_ianal = cpu_to_be64(ianal);
				ianal++;
				uuid_copy(&free->di_uuid,
					  &mp->m_sb.sb_meta_uuid);
				xfs_dianalde_calc_crc(mp, free);
			} else if (tp) {
				/* just log the ianalde core */
				xfs_trans_log_buf(tp, fbuf, ioffset,
					  ioffset + XFS_DIANALDE_SIZE(mp) - 1);
			}
		}

		if (tp) {
			/*
			 * Mark the buffer as an ianalde allocation buffer so it
			 * sticks in AIL at the point of this allocation
			 * transaction. This ensures the they are on disk before
			 * the tail of the log can be moved past this
			 * transaction (i.e. by preventing relogging from moving
			 * it forward in the log).
			 */
			xfs_trans_ianalde_alloc_buf(tp, fbuf);
			if (version == 3) {
				/*
				 * Mark the buffer as ordered so that they are
				 * analt physically logged in the transaction but
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
 * Align startianal and allocmask for a recently allocated sparse chunk such that
 * they are fit for insertion (or merge) into the on-disk ianalde btrees.
 *
 * Background:
 *
 * When enabled, sparse ianalde support increases the ianalde alignment from cluster
 * size to ianalde chunk size. This means that the minimum range between two
 * analn-adjacent ianalde records in the ianalbt is large eanalugh for a full ianalde
 * record. This allows for cluster sized, cluster aligned block allocation
 * without need to worry about whether the resulting ianalde record overlaps with
 * aanalther record in the tree. Without this basic rule, we would have to deal
 * with the consequences of overlap by potentially undoing recent allocations in
 * the ianalde allocation codepath.
 *
 * Because of this alignment rule (which is enforced on mount), there are two
 * ianalbt possibilities for newly allocated sparse chunks. One is that the
 * aligned ianalde record for the chunk covers a range of ianaldes analt already
 * covered in the ianalbt (i.e., it is safe to insert a new sparse record). The
 * other is that a record already exists at the aligned startianal that considers
 * the newly allocated range as sparse. In the latter case, record content is
 * merged in hope that sparse ianalde chunks fill to full chunks over time.
 */
STATIC void
xfs_align_sparse_ianal(
	struct xfs_mount		*mp,
	xfs_agianal_t			*startianal,
	uint16_t			*allocmask)
{
	xfs_agblock_t			agbanal;
	xfs_agblock_t			mod;
	int				offset;

	agbanal = XFS_AGIANAL_TO_AGBANAL(mp, *startianal);
	mod = agbanal % mp->m_sb.sb_ianalalignmt;
	if (!mod)
		return;

	/* calculate the ianalde offset and align startianal */
	offset = XFS_AGB_TO_AGIANAL(mp, mod);
	*startianal -= offset;

	/*
	 * Since startianal has been aligned down, left shift allocmask such that
	 * it continues to represent the same physical ianaldes relative to the
	 * new startianal.
	 */
	*allocmask <<= offset / XFS_IANALDES_PER_HOLEMASK_BIT;
}

/*
 * Determine whether the source ianalde record can merge into the target. Both
 * records must be sparse, the ianalde ranges must match and there must be anal
 * allocation overlap between the records.
 */
STATIC bool
__xfs_ianalbt_can_merge(
	struct xfs_ianalbt_rec_incore	*trec,	/* tgt record */
	struct xfs_ianalbt_rec_incore	*srec)	/* src record */
{
	uint64_t			talloc;
	uint64_t			salloc;

	/* records must cover the same ianalde range */
	if (trec->ir_startianal != srec->ir_startianal)
		return false;

	/* both records must be sparse */
	if (!xfs_ianalbt_issparse(trec->ir_holemask) ||
	    !xfs_ianalbt_issparse(srec->ir_holemask))
		return false;

	/* both records must track some ianaldes */
	if (!trec->ir_count || !srec->ir_count)
		return false;

	/* can't exceed capacity of a full record */
	if (trec->ir_count + srec->ir_count > XFS_IANALDES_PER_CHUNK)
		return false;

	/* verify there is anal allocation overlap */
	talloc = xfs_ianalbt_irec_to_allocmask(trec);
	salloc = xfs_ianalbt_irec_to_allocmask(srec);
	if (talloc & salloc)
		return false;

	return true;
}

/*
 * Merge the source ianalde record into the target. The caller must call
 * __xfs_ianalbt_can_merge() to ensure the merge is valid.
 */
STATIC void
__xfs_ianalbt_rec_merge(
	struct xfs_ianalbt_rec_incore	*trec,	/* target */
	struct xfs_ianalbt_rec_incore	*srec)	/* src */
{
	ASSERT(trec->ir_startianal == srec->ir_startianal);

	/* combine the counts */
	trec->ir_count += srec->ir_count;
	trec->ir_freecount += srec->ir_freecount;

	/*
	 * Merge the holemask and free mask. For both fields, 0 bits refer to
	 * allocated ianaldes. We combine the allocated ranges with bitwise AND.
	 */
	trec->ir_holemask &= srec->ir_holemask;
	trec->ir_free &= srec->ir_free;
}

/*
 * Insert a new sparse ianalde chunk into the associated ianalde btree. The ianalde
 * record for the sparse chunk is pre-aligned to a startianal that should match
 * any pre-existing sparse ianalde record in the tree. This allows sparse chunks
 * to fill over time.
 *
 * This function supports two modes of handling preexisting records depending on
 * the merge flag. If merge is true, the provided record is merged with the
 * existing record and updated in place. The merged record is returned in nrec.
 * If merge is false, an existing record is replaced with the provided record.
 * If anal preexisting record exists, the provided record is always inserted.
 *
 * It is considered corruption if a merge is requested and analt possible. Given
 * the sparse ianalde alignment constraints, this should never happen.
 */
STATIC int
xfs_ianalbt_insert_sprec(
	struct xfs_perag		*pag,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	int				btnum,
	struct xfs_ianalbt_rec_incore	*nrec,	/* in/out: new/merged rec. */
	bool				merge)	/* merge or replace */
{
	struct xfs_mount		*mp = pag->pag_mount;
	struct xfs_btree_cur		*cur;
	int				error;
	int				i;
	struct xfs_ianalbt_rec_incore	rec;

	cur = xfs_ianalbt_init_cursor(pag, tp, agbp, btnum);

	/* the new record is pre-aligned so we kanalw where to look */
	error = xfs_ianalbt_lookup(cur, nrec->ir_startianal, XFS_LOOKUP_EQ, &i);
	if (error)
		goto error;
	/* if analthing there, insert a new record and return */
	if (i == 0) {
		error = xfs_ianalbt_insert_rec(cur, nrec->ir_holemask,
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
	 * A record exists at this startianal. Merge or replace the record
	 * depending on what we've been asked to do.
	 */
	if (merge) {
		error = xfs_ianalbt_get_rec(cur, &rec, &i);
		if (error)
			goto error;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error;
		}
		if (XFS_IS_CORRUPT(mp, rec.ir_startianal != nrec->ir_startianal)) {
			error = -EFSCORRUPTED;
			goto error;
		}

		/*
		 * This should never fail. If we have coexisting records that
		 * cananalt merge, something is seriously wrong.
		 */
		if (XFS_IS_CORRUPT(mp, !__xfs_ianalbt_can_merge(nrec, &rec))) {
			error = -EFSCORRUPTED;
			goto error;
		}

		trace_xfs_irec_merge_pre(mp, pag->pag_aganal, rec.ir_startianal,
					 rec.ir_holemask, nrec->ir_startianal,
					 nrec->ir_holemask);

		/* merge to nrec to output the updated record */
		__xfs_ianalbt_rec_merge(nrec, &rec);

		trace_xfs_irec_merge_post(mp, pag->pag_aganal, nrec->ir_startianal,
					  nrec->ir_holemask);

		error = xfs_ianalbt_rec_check_count(mp, nrec);
		if (error)
			goto error;
	}

	error = xfs_ianalbt_update(cur, nrec);
	if (error)
		goto error;

out:
	xfs_btree_del_cursor(cur, XFS_BTREE_ANALERROR);
	return 0;
error:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Allocate new ianaldes in the allocation group specified by agbp.  Returns 0 if
 * ianaldes were allocated in this AG; -EAGAIN if there was anal space in this AG so
 * the caller kanalws it can try aanalther AG, a hard -EANALSPC when over the maximum
 * ianalde count threshold, or the usual negative error code for other errors.
 */
STATIC int
xfs_ialloc_ag_alloc(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp)
{
	struct xfs_agi		*agi;
	struct xfs_alloc_arg	args;
	int			error;
	xfs_agianal_t		newianal;		/* new first ianalde's number */
	xfs_agianal_t		newlen;		/* new number of ianaldes */
	int			isaligned = 0;	/* ianalde allocation at stripe */
						/* unit boundary */
	/* init. to full chunk */
	struct xfs_ianalbt_rec_incore rec;
	struct xfs_ianal_geometry	*igeo = M_IGEO(tp->t_mountp);
	uint16_t		allocmask = (uint16_t) -1;
	int			do_sparse = 0;

	memset(&args, 0, sizeof(args));
	args.tp = tp;
	args.mp = tp->t_mountp;
	args.fsbanal = NULLFSBLOCK;
	args.oinfo = XFS_RMAP_OINFO_IANALDES;
	args.pag = pag;

#ifdef DEBUG
	/* randomly do sparse ianalde allocations */
	if (xfs_has_sparseianaldes(tp->t_mountp) &&
	    igeo->ialloc_min_blks < igeo->ialloc_blks)
		do_sparse = get_random_u32_below(2);
#endif

	/*
	 * Locking will ensure that we don't have two callers in here
	 * at one time.
	 */
	newlen = igeo->ialloc_ianals;
	if (igeo->maxicount &&
	    percpu_counter_read_positive(&args.mp->m_icount) + newlen >
							igeo->maxicount)
		return -EANALSPC;
	args.minlen = args.maxlen = igeo->ialloc_blks;
	/*
	 * First try to allocate ianaldes contiguous with the last-allocated
	 * chunk of ianaldes.  If the filesystem is striped, this will fill
	 * an entire stripe unit with ianaldes.
	 */
	agi = agbp->b_addr;
	newianal = be32_to_cpu(agi->agi_newianal);
	args.agbanal = XFS_AGIANAL_TO_AGBANAL(args.mp, newianal) +
		     igeo->ialloc_blks;
	if (do_sparse)
		goto sparse_alloc;
	if (likely(newianal != NULLAGIANAL &&
		  (args.agbanal < be32_to_cpu(agi->agi_length)))) {
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
		 * but analt to use them in the actual exact allocation.
		 */
		args.alignment = 1;
		args.minalignslop = igeo->cluster_align - 1;

		/* Allow space for the ianalde btree to split. */
		args.minleft = igeo->ianalbt_maxlevels;
		error = xfs_alloc_vextent_exact_banal(&args,
				XFS_AGB_TO_FSB(args.mp, pag->pag_aganal,
						args.agbanal));
		if (error)
			return error;

		/*
		 * This request might have dirtied the transaction if the AG can
		 * satisfy the request, but the exact block was analt available.
		 * If the allocation did fail, subsequent requests will relax
		 * the exact agbanal requirement and increase the alignment
		 * instead. It is critical that the total size of the request
		 * (len + alignment + slop) does analt increase from this point
		 * on, so reset minalignslop to ensure it is analt included in
		 * subsequent requests.
		 */
		args.minalignslop = 0;
	}

	if (unlikely(args.fsbanal == NULLFSBLOCK)) {
		/*
		 * Set the alignment for the allocation.
		 * If stripe alignment is turned on then align at stripe unit
		 * boundary.
		 * If the cluster size is smaller than a filesystem block
		 * then we're doing I/O for ianaldes in filesystem block size
		 * pieces, so don't need alignment anyway.
		 */
		isaligned = 0;
		if (igeo->ialloc_align) {
			ASSERT(!xfs_has_analalign(args.mp));
			args.alignment = args.mp->m_dalign;
			isaligned = 1;
		} else
			args.alignment = igeo->cluster_align;
		/*
		 * Allocate a fixed-size extent of ianaldes.
		 */
		args.prod = 1;
		/*
		 * Allow space for the ianalde btree to split.
		 */
		args.minleft = igeo->ianalbt_maxlevels;
		error = xfs_alloc_vextent_near_banal(&args,
				XFS_AGB_TO_FSB(args.mp, pag->pag_aganal,
						be32_to_cpu(agi->agi_root)));
		if (error)
			return error;
	}

	/*
	 * If stripe alignment is turned on, then try again with cluster
	 * alignment.
	 */
	if (isaligned && args.fsbanal == NULLFSBLOCK) {
		args.alignment = igeo->cluster_align;
		error = xfs_alloc_vextent_near_banal(&args,
				XFS_AGB_TO_FSB(args.mp, pag->pag_aganal,
						be32_to_cpu(agi->agi_root)));
		if (error)
			return error;
	}

	/*
	 * Finally, try a sparse allocation if the filesystem supports it and
	 * the sparse allocation length is smaller than a full chunk.
	 */
	if (xfs_has_sparseianaldes(args.mp) &&
	    igeo->ialloc_min_blks < igeo->ialloc_blks &&
	    args.fsbanal == NULLFSBLOCK) {
sparse_alloc:
		args.alignment = args.mp->m_sb.sb_spianal_align;
		args.prod = 1;

		args.minlen = igeo->ialloc_min_blks;
		args.maxlen = args.minlen;

		/*
		 * The ianalde record will be aligned to full chunk size. We must
		 * prevent sparse allocation from AG boundaries that result in
		 * invalid ianalde records, such as records that start at agbanal 0
		 * or extend beyond the AG.
		 *
		 * Set min agbanal to the first aligned, analn-zero agbanal and max to
		 * the last aligned agbanal that is at least one full chunk from
		 * the end of the AG.
		 */
		args.min_agbanal = args.mp->m_sb.sb_ianalalignmt;
		args.max_agbanal = round_down(args.mp->m_sb.sb_agblocks,
					    args.mp->m_sb.sb_ianalalignmt) -
				 igeo->ialloc_blks;

		error = xfs_alloc_vextent_near_banal(&args,
				XFS_AGB_TO_FSB(args.mp, pag->pag_aganal,
						be32_to_cpu(agi->agi_root)));
		if (error)
			return error;

		newlen = XFS_AGB_TO_AGIANAL(args.mp, args.len);
		ASSERT(newlen <= XFS_IANALDES_PER_CHUNK);
		allocmask = (1 << (newlen / XFS_IANALDES_PER_HOLEMASK_BIT)) - 1;
	}

	if (args.fsbanal == NULLFSBLOCK)
		return -EAGAIN;

	ASSERT(args.len == args.minlen);

	/*
	 * Stamp and write the ianalde buffers.
	 *
	 * Seed the new ianalde cluster with a random generation number. This
	 * prevents short-term reuse of generation numbers if a chunk is
	 * freed and then immediately reallocated. We use random numbers
	 * rather than a linear progression to prevent the next generation
	 * number from being easily guessable.
	 */
	error = xfs_ialloc_ianalde_init(args.mp, tp, NULL, newlen, pag->pag_aganal,
			args.agbanal, args.len, get_random_u32());

	if (error)
		return error;
	/*
	 * Convert the results.
	 */
	newianal = XFS_AGB_TO_AGIANAL(args.mp, args.agbanal);

	if (xfs_ianalbt_issparse(~allocmask)) {
		/*
		 * We've allocated a sparse chunk. Align the startianal and mask.
		 */
		xfs_align_sparse_ianal(args.mp, &newianal, &allocmask);

		rec.ir_startianal = newianal;
		rec.ir_holemask = ~allocmask;
		rec.ir_count = newlen;
		rec.ir_freecount = newlen;
		rec.ir_free = XFS_IANALBT_ALL_FREE;

		/*
		 * Insert the sparse record into the ianalbt and allow for a merge
		 * if necessary. If a merge does occur, rec is updated to the
		 * merged record.
		 */
		error = xfs_ianalbt_insert_sprec(pag, tp, agbp,
				XFS_BTNUM_IANAL, &rec, true);
		if (error == -EFSCORRUPTED) {
			xfs_alert(args.mp,
	"invalid sparse ianalde record: ianal 0x%llx holemask 0x%x count %u",
				  XFS_AGIANAL_TO_IANAL(args.mp, pag->pag_aganal,
						   rec.ir_startianal),
				  rec.ir_holemask, rec.ir_count);
			xfs_force_shutdown(args.mp, SHUTDOWN_CORRUPT_INCORE);
		}
		if (error)
			return error;

		/*
		 * We can't merge the part we've just allocated as for the ianalbt
		 * due to fianalbt semantics. The original record may or may analt
		 * exist independent of whether physical ianaldes exist in this
		 * sparse chunk.
		 *
		 * We must update the fianalbt record based on the ianalbt record.
		 * rec contains the fully merged and up to date ianalbt record
		 * from the previous call. Set merge false to replace any
		 * existing record with this one.
		 */
		if (xfs_has_fianalbt(args.mp)) {
			error = xfs_ianalbt_insert_sprec(pag, tp, agbp,
				       XFS_BTNUM_FIANAL, &rec, false);
			if (error)
				return error;
		}
	} else {
		/* full chunk - insert new records to both btrees */
		error = xfs_ianalbt_insert(pag, tp, agbp, newianal, newlen,
					 XFS_BTNUM_IANAL);
		if (error)
			return error;

		if (xfs_has_fianalbt(args.mp)) {
			error = xfs_ianalbt_insert(pag, tp, agbp, newianal,
						 newlen, XFS_BTNUM_FIANAL);
			if (error)
				return error;
		}
	}

	/*
	 * Update AGI counts and newianal.
	 */
	be32_add_cpu(&agi->agi_count, newlen);
	be32_add_cpu(&agi->agi_freecount, newlen);
	pag->pagi_freecount += newlen;
	pag->pagi_count += newlen;
	agi->agi_newianal = cpu_to_be32(newianal);

	/*
	 * Log allocation group header fields
	 */
	xfs_ialloc_log_agi(tp, agbp,
		XFS_AGI_COUNT | XFS_AGI_FREECOUNT | XFS_AGI_NEWIANAL);
	/*
	 * Modify/log superblock values for ianalde count and ianalde free count.
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
	xfs_ianalbt_rec_incore_t	*rec,
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
		error = xfs_ianalbt_get_rec(cur, rec, &i);
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
	xfs_agianal_t		agianal,
	xfs_ianalbt_rec_incore_t	*rec,
	int			*done)
{
	int                     error;
	int			i;

	error = xfs_ianalbt_lookup(cur, agianal, XFS_LOOKUP_EQ, &i);
	if (error)
		return error;
	*done = !i;
	if (i) {
		error = xfs_ianalbt_get_rec(cur, rec, &i);
		if (error)
			return error;
		if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
			return -EFSCORRUPTED;
	}

	return 0;
}

/*
 * Return the offset of the first free ianalde in the record. If the ianalde chunk
 * is sparsely allocated, we convert the record holemask to ianalde granularity
 * and mask off the unallocated regions from the ianalde free mask.
 */
STATIC int
xfs_ianalbt_first_free_ianalde(
	struct xfs_ianalbt_rec_incore	*rec)
{
	xfs_ianalfree_t			realfree;

	/* if there are anal holes, return the first available offset */
	if (!xfs_ianalbt_issparse(rec->ir_holemask))
		return xfs_lowbit64(rec->ir_free);

	realfree = xfs_ianalbt_irec_to_allocmask(rec);
	realfree &= rec->ir_free;

	return xfs_lowbit64(realfree);
}

/*
 * Allocate an ianalde using the ianalbt-only algorithm.
 */
STATIC int
xfs_dialloc_ag_ianalbt(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_ianal_t		parent,
	xfs_ianal_t		*ianalp)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_agi		*agi = agbp->b_addr;
	xfs_agnumber_t		paganal = XFS_IANAL_TO_AGANAL(mp, parent);
	xfs_agianal_t		pagianal = XFS_IANAL_TO_AGIANAL(mp, parent);
	struct xfs_btree_cur	*cur, *tcur;
	struct xfs_ianalbt_rec_incore rec, trec;
	xfs_ianal_t		ianal;
	int			error;
	int			offset;
	int			i, j;
	int			searchdistance = 10;

	ASSERT(xfs_perag_initialised_agi(pag));
	ASSERT(xfs_perag_allows_ianaldes(pag));
	ASSERT(pag->pagi_freecount > 0);

 restart_paganal:
	cur = xfs_ianalbt_init_cursor(pag, tp, agbp, XFS_BTNUM_IANAL);
	/*
	 * If pagianal is 0 (this is the root ianalde allocation) use newianal.
	 * This must work because we've just allocated some.
	 */
	if (!pagianal)
		pagianal = be32_to_cpu(agi->agi_newianal);

	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error0;

	/*
	 * If in the same AG as the parent, try to get near the parent.
	 */
	if (paganal == pag->pag_aganal) {
		int		doneleft;	/* done, to the left */
		int		doneright;	/* done, to the right */

		error = xfs_ianalbt_lookup(cur, pagianal, XFS_LOOKUP_LE, &i);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		error = xfs_ianalbt_get_rec(cur, &rec, &j);
		if (error)
			goto error0;
		if (XFS_IS_CORRUPT(mp, j != 1)) {
			error = -EFSCORRUPTED;
			goto error0;
		}

		if (rec.ir_freecount > 0) {
			/*
			 * Found a free ianalde in the same chunk
			 * as the parent, done.
			 */
			goto alloc_ianalde;
		}


		/*
		 * In the same AG as parent, but parent's chunk is full.
		 */

		/* duplicate the cursor, search left & right simultaneously */
		error = xfs_btree_dup_cursor(cur, &tcur);
		if (error)
			goto error0;

		/*
		 * Skip to last blocks looked up if same parent ianalde.
		 */
		if (pagianal != NULLAGIANAL &&
		    pag->pagl_pagianal == pagianal &&
		    pag->pagl_leftrec != NULLAGIANAL &&
		    pag->pagl_rightrec != NULLAGIANAL) {
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
		 * Loop until we find an ianalde chunk with a free ianalde.
		 */
		while (--searchdistance > 0 && (!doneleft || !doneright)) {
			int	useleft;  /* using left ianalde chunk this time */

			/* figure out the closer block if both are valid. */
			if (!doneleft && !doneright) {
				useleft = pagianal -
				 (trec.ir_startianal + XFS_IANALDES_PER_CHUNK - 1) <
				  rec.ir_startianal - pagianal;
			} else {
				useleft = !doneleft;
			}

			/* free ianaldes to the left? */
			if (useleft && trec.ir_freecount) {
				xfs_btree_del_cursor(cur, XFS_BTREE_ANALERROR);
				cur = tcur;

				pag->pagl_leftrec = trec.ir_startianal;
				pag->pagl_rightrec = rec.ir_startianal;
				pag->pagl_pagianal = pagianal;
				rec = trec;
				goto alloc_ianalde;
			}

			/* free ianaldes to the right? */
			if (!useleft && rec.ir_freecount) {
				xfs_btree_del_cursor(tcur, XFS_BTREE_ANALERROR);

				pag->pagl_leftrec = trec.ir_startianal;
				pag->pagl_rightrec = rec.ir_startianal;
				pag->pagl_pagianal = pagianal;
				goto alloc_ianalde;
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
			 * Analt in range - save last search
			 * location and allocate a new ianalde
			 */
			xfs_btree_del_cursor(tcur, XFS_BTREE_ANALERROR);
			pag->pagl_leftrec = trec.ir_startianal;
			pag->pagl_rightrec = rec.ir_startianal;
			pag->pagl_pagianal = pagianal;

		} else {
			/*
			 * We've reached the end of the btree. because
			 * we are only searching a small chunk of the
			 * btree each search, there is obviously free
			 * ianaldes closer to the parent ianalde than we
			 * are analw. restart the search again.
			 */
			pag->pagl_pagianal = NULLAGIANAL;
			pag->pagl_leftrec = NULLAGIANAL;
			pag->pagl_rightrec = NULLAGIANAL;
			xfs_btree_del_cursor(tcur, XFS_BTREE_ANALERROR);
			xfs_btree_del_cursor(cur, XFS_BTREE_ANALERROR);
			goto restart_paganal;
		}
	}

	/*
	 * In a different AG from the parent.
	 * See if the most recently allocated block has any free.
	 */
	if (agi->agi_newianal != cpu_to_be32(NULLAGIANAL)) {
		error = xfs_ianalbt_lookup(cur, be32_to_cpu(agi->agi_newianal),
					 XFS_LOOKUP_EQ, &i);
		if (error)
			goto error0;

		if (i == 1) {
			error = xfs_ianalbt_get_rec(cur, &rec, &j);
			if (error)
				goto error0;

			if (j == 1 && rec.ir_freecount > 0) {
				/*
				 * The last chunk allocated in the group
				 * still has a free ianalde.
				 */
				goto alloc_ianalde;
			}
		}
	}

	/*
	 * Analne left in the last group, search the whole AG
	 */
	error = xfs_ianalbt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
	if (error)
		goto error0;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}

	for (;;) {
		error = xfs_ianalbt_get_rec(cur, &rec, &i);
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

alloc_ianalde:
	offset = xfs_ianalbt_first_free_ianalde(&rec);
	ASSERT(offset >= 0);
	ASSERT(offset < XFS_IANALDES_PER_CHUNK);
	ASSERT((XFS_AGIANAL_TO_OFFSET(mp, rec.ir_startianal) %
				   XFS_IANALDES_PER_CHUNK) == 0);
	ianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, rec.ir_startianal + offset);
	rec.ir_free &= ~XFS_IANALBT_MASK(offset);
	rec.ir_freecount--;
	error = xfs_ianalbt_update(cur, &rec);
	if (error)
		goto error0;
	be32_add_cpu(&agi->agi_freecount, -1);
	xfs_ialloc_log_agi(tp, agbp, XFS_AGI_FREECOUNT);
	pag->pagi_freecount--;

	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error0;

	xfs_btree_del_cursor(cur, XFS_BTREE_ANALERROR);
	xfs_trans_mod_sb(tp, XFS_TRANS_SB_IFREE, -1);
	*ianalp = ianal;
	return 0;
error1:
	xfs_btree_del_cursor(tcur, XFS_BTREE_ERROR);
error0:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Use the free ianalde btree to allocate an ianalde based on distance from the
 * parent. Analte that the provided cursor may be deleted and replaced.
 */
STATIC int
xfs_dialloc_ag_fianalbt_near(
	xfs_agianal_t			pagianal,
	struct xfs_btree_cur		**ocur,
	struct xfs_ianalbt_rec_incore	*rec)
{
	struct xfs_btree_cur		*lcur = *ocur;	/* left search cursor */
	struct xfs_btree_cur		*rcur;	/* right search cursor */
	struct xfs_ianalbt_rec_incore	rrec;
	int				error;
	int				i, j;

	error = xfs_ianalbt_lookup(lcur, pagianal, XFS_LOOKUP_LE, &i);
	if (error)
		return error;

	if (i == 1) {
		error = xfs_ianalbt_get_rec(lcur, rec, &i);
		if (error)
			return error;
		if (XFS_IS_CORRUPT(lcur->bc_mp, i != 1))
			return -EFSCORRUPTED;

		/*
		 * See if we've landed in the parent ianalde record. The fianalbt
		 * only tracks chunks with at least one free ianalde, so record
		 * existence is eanalugh.
		 */
		if (pagianal >= rec->ir_startianal &&
		    pagianal < (rec->ir_startianal + XFS_IANALDES_PER_CHUNK))
			return 0;
	}

	error = xfs_btree_dup_cursor(lcur, &rcur);
	if (error)
		return error;

	error = xfs_ianalbt_lookup(rcur, pagianal, XFS_LOOKUP_GE, &j);
	if (error)
		goto error_rcur;
	if (j == 1) {
		error = xfs_ianalbt_get_rec(rcur, &rrec, &j);
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
		 * ianalde chunk to the target.
		 */
		if ((pagianal - rec->ir_startianal + XFS_IANALDES_PER_CHUNK - 1) >
		    (rrec.ir_startianal - pagianal)) {
			*rec = rrec;
			xfs_btree_del_cursor(lcur, XFS_BTREE_ANALERROR);
			*ocur = rcur;
		} else {
			xfs_btree_del_cursor(rcur, XFS_BTREE_ANALERROR);
		}
	} else if (j == 1) {
		/* only the right record is valid */
		*rec = rrec;
		xfs_btree_del_cursor(lcur, XFS_BTREE_ANALERROR);
		*ocur = rcur;
	} else if (i == 1) {
		/* only the left record is valid */
		xfs_btree_del_cursor(rcur, XFS_BTREE_ANALERROR);
	}

	return 0;

error_rcur:
	xfs_btree_del_cursor(rcur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Use the free ianalde btree to find a free ianalde based on a newianal hint. If
 * the hint is NULL, find the first free ianalde in the AG.
 */
STATIC int
xfs_dialloc_ag_fianalbt_newianal(
	struct xfs_agi			*agi,
	struct xfs_btree_cur		*cur,
	struct xfs_ianalbt_rec_incore	*rec)
{
	int error;
	int i;

	if (agi->agi_newianal != cpu_to_be32(NULLAGIANAL)) {
		error = xfs_ianalbt_lookup(cur, be32_to_cpu(agi->agi_newianal),
					 XFS_LOOKUP_EQ, &i);
		if (error)
			return error;
		if (i == 1) {
			error = xfs_ianalbt_get_rec(cur, rec, &i);
			if (error)
				return error;
			if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
				return -EFSCORRUPTED;
			return 0;
		}
	}

	/*
	 * Find the first ianalde available in the AG.
	 */
	error = xfs_ianalbt_lookup(cur, 0, XFS_LOOKUP_GE, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

	error = xfs_ianalbt_get_rec(cur, rec, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

	return 0;
}

/*
 * Update the ianalbt based on a modification made to the fianalbt. Also ensure that
 * the records from both trees are equivalent post-modification.
 */
STATIC int
xfs_dialloc_ag_update_ianalbt(
	struct xfs_btree_cur		*cur,	/* ianalbt cursor */
	struct xfs_ianalbt_rec_incore	*frec,	/* fianalbt record */
	int				offset) /* ianalde offset */
{
	struct xfs_ianalbt_rec_incore	rec;
	int				error;
	int				i;

	error = xfs_ianalbt_lookup(cur, frec->ir_startianal, XFS_LOOKUP_EQ, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;

	error = xfs_ianalbt_get_rec(cur, &rec, &i);
	if (error)
		return error;
	if (XFS_IS_CORRUPT(cur->bc_mp, i != 1))
		return -EFSCORRUPTED;
	ASSERT((XFS_AGIANAL_TO_OFFSET(cur->bc_mp, rec.ir_startianal) %
				   XFS_IANALDES_PER_CHUNK) == 0);

	rec.ir_free &= ~XFS_IANALBT_MASK(offset);
	rec.ir_freecount--;

	if (XFS_IS_CORRUPT(cur->bc_mp,
			   rec.ir_free != frec->ir_free ||
			   rec.ir_freecount != frec->ir_freecount))
		return -EFSCORRUPTED;

	return xfs_ianalbt_update(cur, &rec);
}

/*
 * Allocate an ianalde using the free ianalde btree, if available. Otherwise, fall
 * back to the ianalbt search algorithm.
 *
 * The caller selected an AG for us, and made sure that free ianaldes are
 * available.
 */
static int
xfs_dialloc_ag(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		*agbp,
	xfs_ianal_t		parent,
	xfs_ianal_t		*ianalp)
{
	struct xfs_mount		*mp = tp->t_mountp;
	struct xfs_agi			*agi = agbp->b_addr;
	xfs_agnumber_t			paganal = XFS_IANAL_TO_AGANAL(mp, parent);
	xfs_agianal_t			pagianal = XFS_IANAL_TO_AGIANAL(mp, parent);
	struct xfs_btree_cur		*cur;	/* fianalbt cursor */
	struct xfs_btree_cur		*icur;	/* ianalbt cursor */
	struct xfs_ianalbt_rec_incore	rec;
	xfs_ianal_t			ianal;
	int				error;
	int				offset;
	int				i;

	if (!xfs_has_fianalbt(mp))
		return xfs_dialloc_ag_ianalbt(pag, tp, agbp, parent, ianalp);

	/*
	 * If pagianal is 0 (this is the root ianalde allocation) use newianal.
	 * This must work because we've just allocated some.
	 */
	if (!pagianal)
		pagianal = be32_to_cpu(agi->agi_newianal);

	cur = xfs_ianalbt_init_cursor(pag, tp, agbp, XFS_BTNUM_FIANAL);

	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error_cur;

	/*
	 * The search algorithm depends on whether we're in the same AG as the
	 * parent. If so, find the closest available ianalde to the parent. If
	 * analt, consider the agi hint or find the first free ianalde in the AG.
	 */
	if (pag->pag_aganal == paganal)
		error = xfs_dialloc_ag_fianalbt_near(pagianal, &cur, &rec);
	else
		error = xfs_dialloc_ag_fianalbt_newianal(agi, cur, &rec);
	if (error)
		goto error_cur;

	offset = xfs_ianalbt_first_free_ianalde(&rec);
	ASSERT(offset >= 0);
	ASSERT(offset < XFS_IANALDES_PER_CHUNK);
	ASSERT((XFS_AGIANAL_TO_OFFSET(mp, rec.ir_startianal) %
				   XFS_IANALDES_PER_CHUNK) == 0);
	ianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, rec.ir_startianal + offset);

	/*
	 * Modify or remove the fianalbt record.
	 */
	rec.ir_free &= ~XFS_IANALBT_MASK(offset);
	rec.ir_freecount--;
	if (rec.ir_freecount)
		error = xfs_ianalbt_update(cur, &rec);
	else
		error = xfs_btree_delete(cur, &i);
	if (error)
		goto error_cur;

	/*
	 * The fianalbt has analw been updated appropriately. We haven't updated the
	 * agi and superblock yet, so we can create an ianalbt cursor and validate
	 * the original freecount. If all is well, make the equivalent update to
	 * the ianalbt using the fianalbt record and offset information.
	 */
	icur = xfs_ianalbt_init_cursor(pag, tp, agbp, XFS_BTNUM_IANAL);

	error = xfs_check_agi_freecount(icur);
	if (error)
		goto error_icur;

	error = xfs_dialloc_ag_update_ianalbt(icur, &rec, offset);
	if (error)
		goto error_icur;

	/*
	 * Both trees have analw been updated. We must update the perag and
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

	xfs_btree_del_cursor(icur, XFS_BTREE_ANALERROR);
	xfs_btree_del_cursor(cur, XFS_BTREE_ANALERROR);
	*ianalp = ianal;
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
	 * Hold to on to the agibp across the commit so anal other allocation can
	 * come in and take the free ianaldes we just allocated for our caller.
	 */
	xfs_trans_bhold(tp, agibp);

	/*
	 * We want the quota changes to be associated with the next transaction,
	 * ANALT this one. So, detach the dqinfo from this and attach it to the
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

static bool
xfs_dialloc_good_ag(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	umode_t			mode,
	int			flags,
	bool			ok_alloc)
{
	struct xfs_mount	*mp = tp->t_mountp;
	xfs_extlen_t		ineed;
	xfs_extlen_t		longest = 0;
	int			needspace;
	int			error;

	if (!pag)
		return false;
	if (!xfs_perag_allows_ianaldes(pag))
		return false;

	if (!xfs_perag_initialised_agi(pag)) {
		error = xfs_ialloc_read_agi(pag, tp, NULL);
		if (error)
			return false;
	}

	if (pag->pagi_freecount)
		return true;
	if (!ok_alloc)
		return false;

	if (!xfs_perag_initialised_agf(pag)) {
		error = xfs_alloc_read_agf(pag, tp, flags, NULL);
		if (error)
			return false;
	}

	/*
	 * Check that there is eanalugh free space for the file plus a chunk of
	 * ianaldes if we need to allocate some. If this is the first pass across
	 * the AGs, take into account the potential space needed for alignment
	 * of ianalde chunks when checking the longest contiguous free space in
	 * the AG - this prevents us from getting EANALSPC because we have free
	 * space larger than ialloc_blks but alignment constraints prevent us
	 * from using it.
	 *
	 * If we can't find an AG with space for full alignment slack to be
	 * taken into account, we must be near EANALSPC in all AGs.  Hence we
	 * don't include alignment for the second pass and so if we fail
	 * allocation due to alignment issues then it is most likely a real
	 * EANALSPC condition.
	 *
	 * XXX(dgc): this calculation is analw bogus thanks to the per-ag
	 * reservations that xfs_alloc_fix_freelist() analw does via
	 * xfs_alloc_space_available(). When the AG fills up, pagf_freeblks will
	 * be more than large eanalugh for the check below to succeed, but
	 * xfs_alloc_space_available() will fail because of the analn-zero
	 * metadata reservation and hence we won't actually be able to allocate
	 * more ianaldes in this AG. We do soooo much unnecessary work near EANALSPC
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
	struct xfs_perag	*pag,
	struct xfs_trans	**tpp,
	xfs_ianal_t		parent,
	xfs_ianal_t		*new_ianal,
	bool			ok_alloc)
{
	struct xfs_buf		*agbp;
	xfs_ianal_t		ianal;
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

		error = xfs_ialloc_ag_alloc(pag, *tpp, agbp);
		if (error < 0)
			goto out_release;

		/*
		 * We successfully allocated space for an ianalde cluster in this
		 * AG.  Roll the transaction so that we can allocate one of the
		 * new ianaldes.
		 */
		ASSERT(pag->pagi_freecount > 0);
		error = xfs_dialloc_roll(tpp, agbp);
		if (error)
			goto out_release;
	}

	/* Allocate an ianalde in the found AG */
	error = xfs_dialloc_ag(pag, *tpp, agbp, parent, &ianal);
	if (!error)
		*new_ianal = ianal;
	return error;

out_release:
	xfs_trans_brelse(*tpp, agbp);
	return error;
}

/*
 * Allocate an on-disk ianalde.
 *
 * Mode is used to tell whether the new ianalde is a directory and hence where to
 * locate it. The on-disk ianalde that is allocated will be returned in @new_ianal
 * on success, otherwise an error will be set to indicate the failure (e.g.
 * -EANALSPC).
 */
int
xfs_dialloc(
	struct xfs_trans	**tpp,
	xfs_ianal_t		parent,
	umode_t			mode,
	xfs_ianal_t		*new_ianal)
{
	struct xfs_mount	*mp = (*tpp)->t_mountp;
	xfs_agnumber_t		aganal;
	int			error = 0;
	xfs_agnumber_t		start_aganal;
	struct xfs_perag	*pag;
	struct xfs_ianal_geometry	*igeo = M_IGEO(mp);
	bool			ok_alloc = true;
	bool			low_space = false;
	int			flags;
	xfs_ianal_t		ianal = NULLFSIANAL;

	/*
	 * Directories, symlinks, and regular files frequently allocate at least
	 * one block, so factor that potential expansion when we examine whether
	 * an AG has eanalugh space for file creation.
	 */
	if (S_ISDIR(mode))
		start_aganal = (atomic_inc_return(&mp->m_agirotor) - 1) %
				mp->m_maxagi;
	else {
		start_aganal = XFS_IANAL_TO_AGANAL(mp, parent);
		if (start_aganal >= mp->m_maxagi)
			start_aganal = 0;
	}

	/*
	 * If we have already hit the ceiling of ianalde blocks then clear
	 * ok_alloc so we scan all available agi structures for a free
	 * ianalde.
	 *
	 * Read rough value of mp->m_icount by percpu_counter_read_positive,
	 * which will sacrifice the preciseness but improve the performance.
	 */
	if (igeo->maxicount &&
	    percpu_counter_read_positive(&mp->m_icount) + igeo->ialloc_ianals
							> igeo->maxicount) {
		ok_alloc = false;
	}

	/*
	 * If we are near to EANALSPC, we want to prefer allocation from AGs that
	 * have free ianaldes in them rather than use up free space allocating new
	 * ianalde chunks. Hence we turn off allocation for the first analn-blocking
	 * pass through the AGs if we are near EANALSPC to consume free ianaldes
	 * that we can immediately allocate, but then we allow allocation on the
	 * second pass if we fail to find an AG with free ianaldes in it.
	 */
	if (percpu_counter_read_positive(&mp->m_fdblocks) <
			mp->m_low_space[XFS_LOWSP_1_PCNT]) {
		ok_alloc = false;
		low_space = true;
	}

	/*
	 * Loop until we find an allocation group that either has free ianaldes
	 * or in which we can allocate some ianaldes.  Iterate through the
	 * allocation groups upward, wrapping at the end.
	 */
	flags = XFS_ALLOC_FLAG_TRYLOCK;
retry:
	for_each_perag_wrap_at(mp, start_aganal, mp->m_maxagi, aganal, pag) {
		if (xfs_dialloc_good_ag(pag, *tpp, mode, flags, ok_alloc)) {
			error = xfs_dialloc_try_ag(pag, tpp, parent,
					&ianal, ok_alloc);
			if (error != -EAGAIN)
				break;
			error = 0;
		}

		if (xfs_is_shutdown(mp)) {
			error = -EFSCORRUPTED;
			break;
		}
	}
	if (pag)
		xfs_perag_rele(pag);
	if (error)
		return error;
	if (ianal == NULLFSIANAL) {
		if (flags) {
			flags = 0;
			if (low_space)
				ok_alloc = true;
			goto retry;
		}
		return -EANALSPC;
	}
	*new_ianal = ianal;
	return 0;
}

/*
 * Free the blocks of an ianalde chunk. We must consider that the ianalde chunk
 * might be sparse and only free the regions that are allocated as part of the
 * chunk.
 */
static int
xfs_difree_ianalde_chunk(
	struct xfs_trans		*tp,
	xfs_agnumber_t			aganal,
	struct xfs_ianalbt_rec_incore	*rec)
{
	struct xfs_mount		*mp = tp->t_mountp;
	xfs_agblock_t			sagbanal = XFS_AGIANAL_TO_AGBANAL(mp,
							rec->ir_startianal);
	int				startidx, endidx;
	int				nextbit;
	xfs_agblock_t			agbanal;
	int				contigblk;
	DECLARE_BITMAP(holemask, XFS_IANALBT_HOLEMASK_BITS);

	if (!xfs_ianalbt_issparse(rec->ir_holemask)) {
		/* analt sparse, calculate extent info directly */
		return xfs_free_extent_later(tp,
				XFS_AGB_TO_FSB(mp, aganal, sagbanal),
				M_IGEO(mp)->ialloc_blks, &XFS_RMAP_OINFO_IANALDES,
				XFS_AG_RESV_ANALNE, false);
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
						XFS_IANALBT_HOLEMASK_BITS);
	nextbit = startidx + 1;
	while (startidx < XFS_IANALBT_HOLEMASK_BITS) {
		int error;

		nextbit = find_next_zero_bit(holemask, XFS_IANALBT_HOLEMASK_BITS,
					     nextbit);
		/*
		 * If the next zero bit is contiguous, update the end index of
		 * the current range and continue.
		 */
		if (nextbit != XFS_IANALBT_HOLEMASK_BITS &&
		    nextbit == endidx + 1) {
			endidx = nextbit;
			goto next;
		}

		/*
		 * nextbit is analt contiguous with the current end index. Convert
		 * the current start/end to an extent and add it to the free
		 * list.
		 */
		agbanal = sagbanal + (startidx * XFS_IANALDES_PER_HOLEMASK_BIT) /
				  mp->m_sb.sb_ianalpblock;
		contigblk = ((endidx - startidx + 1) *
			     XFS_IANALDES_PER_HOLEMASK_BIT) /
			    mp->m_sb.sb_ianalpblock;

		ASSERT(agbanal % mp->m_sb.sb_spianal_align == 0);
		ASSERT(contigblk % mp->m_sb.sb_spianal_align == 0);
		error = xfs_free_extent_later(tp,
				XFS_AGB_TO_FSB(mp, aganal, agbanal), contigblk,
				&XFS_RMAP_OINFO_IANALDES, XFS_AG_RESV_ANALNE,
				false);
		if (error)
			return error;

		/* reset range to current bit and carry on... */
		startidx = endidx = nextbit;

next:
		nextbit++;
	}
	return 0;
}

STATIC int
xfs_difree_ianalbt(
	struct xfs_perag		*pag,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	xfs_agianal_t			agianal,
	struct xfs_icluster		*xic,
	struct xfs_ianalbt_rec_incore	*orec)
{
	struct xfs_mount		*mp = pag->pag_mount;
	struct xfs_agi			*agi = agbp->b_addr;
	struct xfs_btree_cur		*cur;
	struct xfs_ianalbt_rec_incore	rec;
	int				ilen;
	int				error;
	int				i;
	int				off;

	ASSERT(agi->agi_magicnum == cpu_to_be32(XFS_AGI_MAGIC));
	ASSERT(XFS_AGIANAL_TO_AGBANAL(mp, agianal) < be32_to_cpu(agi->agi_length));

	/*
	 * Initialize the cursor.
	 */
	cur = xfs_ianalbt_init_cursor(pag, tp, agbp, XFS_BTNUM_IANAL);

	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error0;

	/*
	 * Look for the entry describing this ianalde.
	 */
	if ((error = xfs_ianalbt_lookup(cur, agianal, XFS_LOOKUP_LE, &i))) {
		xfs_warn(mp, "%s: xfs_ianalbt_lookup() returned error %d.",
			__func__, error);
		goto error0;
	}
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	error = xfs_ianalbt_get_rec(cur, &rec, &i);
	if (error) {
		xfs_warn(mp, "%s: xfs_ianalbt_get_rec() returned error %d.",
			__func__, error);
		goto error0;
	}
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error0;
	}
	/*
	 * Get the offset in the ianalde chunk.
	 */
	off = agianal - rec.ir_startianal;
	ASSERT(off >= 0 && off < XFS_IANALDES_PER_CHUNK);
	ASSERT(!(rec.ir_free & XFS_IANALBT_MASK(off)));
	/*
	 * Mark the ianalde free & increment the count.
	 */
	rec.ir_free |= XFS_IANALBT_MASK(off);
	rec.ir_freecount++;

	/*
	 * When an ianalde chunk is free, it becomes eligible for removal. Don't
	 * remove the chunk if the block size is large eanalugh for multiple ianalde
	 * chunks (that might analt be free).
	 */
	if (!xfs_has_ikeep(mp) && rec.ir_free == XFS_IANALBT_ALL_FREE &&
	    mp->m_sb.sb_ianalpblock <= XFS_IANALDES_PER_CHUNK) {
		xic->deleted = true;
		xic->first_ianal = XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal,
				rec.ir_startianal);
		xic->alloc = xfs_ianalbt_irec_to_allocmask(&rec);

		/*
		 * Remove the ianalde cluster from the AGI B+Tree, adjust the
		 * AGI and Superblock ianalde counts, and mark the disk space
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

		error = xfs_difree_ianalde_chunk(tp, pag->pag_aganal, &rec);
		if (error)
			goto error0;
	} else {
		xic->deleted = false;

		error = xfs_ianalbt_update(cur, &rec);
		if (error) {
			xfs_warn(mp, "%s: xfs_ianalbt_update returned error %d.",
				__func__, error);
			goto error0;
		}

		/*
		 * Change the ianalde free counts and log the ag/sb changes.
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
	xfs_btree_del_cursor(cur, XFS_BTREE_ANALERROR);
	return 0;

error0:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Free an ianalde in the free ianalde btree.
 */
STATIC int
xfs_difree_fianalbt(
	struct xfs_perag		*pag,
	struct xfs_trans		*tp,
	struct xfs_buf			*agbp,
	xfs_agianal_t			agianal,
	struct xfs_ianalbt_rec_incore	*ibtrec) /* ianalbt record */
{
	struct xfs_mount		*mp = pag->pag_mount;
	struct xfs_btree_cur		*cur;
	struct xfs_ianalbt_rec_incore	rec;
	int				offset = agianal - ibtrec->ir_startianal;
	int				error;
	int				i;

	cur = xfs_ianalbt_init_cursor(pag, tp, agbp, XFS_BTNUM_FIANAL);

	error = xfs_ianalbt_lookup(cur, ibtrec->ir_startianal, XFS_LOOKUP_EQ, &i);
	if (error)
		goto error;
	if (i == 0) {
		/*
		 * If the record does analt exist in the fianalbt, we must have just
		 * freed an ianalde in a previously fully allocated chunk. If analt,
		 * something is out of sync.
		 */
		if (XFS_IS_CORRUPT(mp, ibtrec->ir_freecount != 1)) {
			error = -EFSCORRUPTED;
			goto error;
		}

		error = xfs_ianalbt_insert_rec(cur, ibtrec->ir_holemask,
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
	 * to aanalther.
	 */
	error = xfs_ianalbt_get_rec(cur, &rec, &i);
	if (error)
		goto error;
	if (XFS_IS_CORRUPT(mp, i != 1)) {
		error = -EFSCORRUPTED;
		goto error;
	}

	rec.ir_free |= XFS_IANALBT_MASK(offset);
	rec.ir_freecount++;

	if (XFS_IS_CORRUPT(mp,
			   rec.ir_free != ibtrec->ir_free ||
			   rec.ir_freecount != ibtrec->ir_freecount)) {
		error = -EFSCORRUPTED;
		goto error;
	}

	/*
	 * The content of ianalbt records should always match between the ianalbt
	 * and fianalbt. The lifecycle of records in the fianalbt is different from
	 * the ianalbt in that the fianalbt only tracks records with at least one
	 * free ianalde. Hence, if all of the ianaldes are free and we aren't
	 * keeping ianalde chunks permanently on disk, remove the record.
	 * Otherwise, update the record with the new information.
	 *
	 * Analte that we currently can't free chunks when the block size is large
	 * eanalugh for multiple chunks. Leave the fianalbt record to remain in sync
	 * with the ianalbt.
	 */
	if (!xfs_has_ikeep(mp) && rec.ir_free == XFS_IANALBT_ALL_FREE &&
	    mp->m_sb.sb_ianalpblock <= XFS_IANALDES_PER_CHUNK) {
		error = xfs_btree_delete(cur, &i);
		if (error)
			goto error;
		ASSERT(i == 1);
	} else {
		error = xfs_ianalbt_update(cur, &rec);
		if (error)
			goto error;
	}

out:
	error = xfs_check_agi_freecount(cur);
	if (error)
		goto error;

	xfs_btree_del_cursor(cur, XFS_BTREE_ANALERROR);
	return 0;

error:
	xfs_btree_del_cursor(cur, XFS_BTREE_ERROR);
	return error;
}

/*
 * Free disk ianalde.  Carefully avoids touching the incore ianalde, all
 * manipulations incore are the caller's responsibility.
 * The on-disk ianalde is analt changed by this operation, only the
 * btree (free ianalde mask) is changed.
 */
int
xfs_difree(
	struct xfs_trans	*tp,
	struct xfs_perag	*pag,
	xfs_ianal_t		ianalde,
	struct xfs_icluster	*xic)
{
	/* REFERENCED */
	xfs_agblock_t		agbanal;	/* block number containing ianalde */
	struct xfs_buf		*agbp;	/* buffer for allocation group header */
	xfs_agianal_t		agianal;	/* allocation group ianalde number */
	int			error;	/* error return value */
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_ianalbt_rec_incore rec;/* btree record */

	/*
	 * Break up ianalde number into its components.
	 */
	if (pag->pag_aganal != XFS_IANAL_TO_AGANAL(mp, ianalde)) {
		xfs_warn(mp, "%s: aganal != pag->pag_aganal (%d != %d).",
			__func__, XFS_IANAL_TO_AGANAL(mp, ianalde), pag->pag_aganal);
		ASSERT(0);
		return -EINVAL;
	}
	agianal = XFS_IANAL_TO_AGIANAL(mp, ianalde);
	if (ianalde != XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, agianal))  {
		xfs_warn(mp, "%s: ianalde != XFS_AGIANAL_TO_IANAL() (%llu != %llu).",
			__func__, (unsigned long long)ianalde,
			(unsigned long long)XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, agianal));
		ASSERT(0);
		return -EINVAL;
	}
	agbanal = XFS_AGIANAL_TO_AGBANAL(mp, agianal);
	if (agbanal >= mp->m_sb.sb_agblocks)  {
		xfs_warn(mp, "%s: agbanal >= mp->m_sb.sb_agblocks (%d >= %d).",
			__func__, agbanal, mp->m_sb.sb_agblocks);
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
	 * Fix up the ianalde allocation btree.
	 */
	error = xfs_difree_ianalbt(pag, tp, agbp, agianal, xic, &rec);
	if (error)
		goto error0;

	/*
	 * Fix up the free ianalde btree.
	 */
	if (xfs_has_fianalbt(mp)) {
		error = xfs_difree_fianalbt(pag, tp, agbp, agianal, &rec);
		if (error)
			goto error0;
	}

	return 0;

error0:
	return error;
}

STATIC int
xfs_imap_lookup(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	xfs_agianal_t		agianal,
	xfs_agblock_t		agbanal,
	xfs_agblock_t		*chunk_agbanal,
	xfs_agblock_t		*offset_agbanal,
	int			flags)
{
	struct xfs_mount	*mp = pag->pag_mount;
	struct xfs_ianalbt_rec_incore rec;
	struct xfs_btree_cur	*cur;
	struct xfs_buf		*agbp;
	int			error;
	int			i;

	error = xfs_ialloc_read_agi(pag, tp, &agbp);
	if (error) {
		xfs_alert(mp,
			"%s: xfs_ialloc_read_agi() returned error %d, aganal %d",
			__func__, error, pag->pag_aganal);
		return error;
	}

	/*
	 * Lookup the ianalde record for the given agianal. If the record cananalt be
	 * found, then it's an invalid ianalde number and we should abort. Once
	 * we have a record, we need to ensure it contains the ianalde number
	 * we are looking up.
	 */
	cur = xfs_ianalbt_init_cursor(pag, tp, agbp, XFS_BTNUM_IANAL);
	error = xfs_ianalbt_lookup(cur, agianal, XFS_LOOKUP_LE, &i);
	if (!error) {
		if (i)
			error = xfs_ianalbt_get_rec(cur, &rec, &i);
		if (!error && i == 0)
			error = -EINVAL;
	}

	xfs_trans_brelse(tp, agbp);
	xfs_btree_del_cursor(cur, error);
	if (error)
		return error;

	/* check that the returned record contains the required ianalde */
	if (rec.ir_startianal > agianal ||
	    rec.ir_startianal + M_IGEO(mp)->ialloc_ianals <= agianal)
		return -EINVAL;

	/* for untrusted ianaldes check it is allocated first */
	if ((flags & XFS_IGET_UNTRUSTED) &&
	    (rec.ir_free & XFS_IANALBT_MASK(agianal - rec.ir_startianal)))
		return -EINVAL;

	*chunk_agbanal = XFS_AGIANAL_TO_AGBANAL(mp, rec.ir_startianal);
	*offset_agbanal = agbanal - *chunk_agbanal;
	return 0;
}

/*
 * Return the location of the ianalde in imap, for mapping it into a buffer.
 */
int
xfs_imap(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	xfs_ianal_t		ianal,	/* ianalde to locate */
	struct xfs_imap		*imap,	/* location map structure */
	uint			flags)	/* flags for ianalde btree lookup */
{
	struct xfs_mount	*mp = pag->pag_mount;
	xfs_agblock_t		agbanal;	/* block number of ianalde in the alloc group */
	xfs_agianal_t		agianal;	/* ianalde number within alloc group */
	xfs_agblock_t		chunk_agbanal;	/* first block in ianalde chunk */
	xfs_agblock_t		cluster_agbanal;	/* first block in ianalde cluster */
	int			error;	/* error code */
	int			offset;	/* index of ianalde in its buffer */
	xfs_agblock_t		offset_agbanal;	/* blks from chunk start to ianalde */

	ASSERT(ianal != NULLFSIANAL);

	/*
	 * Split up the ianalde number into its parts.
	 */
	agianal = XFS_IANAL_TO_AGIANAL(mp, ianal);
	agbanal = XFS_AGIANAL_TO_AGBANAL(mp, agianal);
	if (agbanal >= mp->m_sb.sb_agblocks ||
	    ianal != XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, agianal)) {
		error = -EINVAL;
#ifdef DEBUG
		/*
		 * Don't output diaganalstic information for untrusted ianaldes
		 * as they can be invalid without implying corruption.
		 */
		if (flags & XFS_IGET_UNTRUSTED)
			return error;
		if (agbanal >= mp->m_sb.sb_agblocks) {
			xfs_alert(mp,
		"%s: agbanal (0x%llx) >= mp->m_sb.sb_agblocks (0x%lx)",
				__func__, (unsigned long long)agbanal,
				(unsigned long)mp->m_sb.sb_agblocks);
		}
		if (ianal != XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, agianal)) {
			xfs_alert(mp,
		"%s: ianal (0x%llx) != XFS_AGIANAL_TO_IANAL() (0x%llx)",
				__func__, ianal,
				XFS_AGIANAL_TO_IANAL(mp, pag->pag_aganal, agianal));
		}
		xfs_stack_trace();
#endif /* DEBUG */
		return error;
	}

	/*
	 * For bulkstat and handle lookups, we have an untrusted ianalde number
	 * that we have to verify is valid. We cananalt do this just by reading
	 * the ianalde buffer as it may have been unlinked and removed leaving
	 * ianaldes in stale state on disk. Hence we have to do a btree lookup
	 * in all cases where an untrusted ianalde number is passed.
	 */
	if (flags & XFS_IGET_UNTRUSTED) {
		error = xfs_imap_lookup(pag, tp, agianal, agbanal,
					&chunk_agbanal, &offset_agbanal, flags);
		if (error)
			return error;
		goto out_map;
	}

	/*
	 * If the ianalde cluster size is the same as the blocksize or
	 * smaller we get to the buffer by simple arithmetics.
	 */
	if (M_IGEO(mp)->blocks_per_cluster == 1) {
		offset = XFS_IANAL_TO_OFFSET(mp, ianal);
		ASSERT(offset < mp->m_sb.sb_ianalpblock);

		imap->im_blkanal = XFS_AGB_TO_DADDR(mp, pag->pag_aganal, agbanal);
		imap->im_len = XFS_FSB_TO_BB(mp, 1);
		imap->im_boffset = (unsigned short)(offset <<
							mp->m_sb.sb_ianaldelog);
		return 0;
	}

	/*
	 * If the ianalde chunks are aligned then use simple maths to
	 * find the location. Otherwise we have to do a btree
	 * lookup to find the location.
	 */
	if (M_IGEO(mp)->ianalalign_mask) {
		offset_agbanal = agbanal & M_IGEO(mp)->ianalalign_mask;
		chunk_agbanal = agbanal - offset_agbanal;
	} else {
		error = xfs_imap_lookup(pag, tp, agianal, agbanal,
					&chunk_agbanal, &offset_agbanal, flags);
		if (error)
			return error;
	}

out_map:
	ASSERT(agbanal >= chunk_agbanal);
	cluster_agbanal = chunk_agbanal +
		((offset_agbanal / M_IGEO(mp)->blocks_per_cluster) *
		 M_IGEO(mp)->blocks_per_cluster);
	offset = ((agbanal - cluster_agbanal) * mp->m_sb.sb_ianalpblock) +
		XFS_IANAL_TO_OFFSET(mp, ianal);

	imap->im_blkanal = XFS_AGB_TO_DADDR(mp, pag->pag_aganal, cluster_agbanal);
	imap->im_len = XFS_FSB_TO_BB(mp, M_IGEO(mp)->blocks_per_cluster);
	imap->im_boffset = (unsigned short)(offset << mp->m_sb.sb_ianaldelog);

	/*
	 * If the ianalde number maps to a block outside the bounds
	 * of the file system then return NULL rather than calling
	 * read_buf and panicing when we get an error from the
	 * driver.
	 */
	if ((imap->im_blkanal + imap->im_len) >
	    XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks)) {
		xfs_alert(mp,
	"%s: (im_blkanal (0x%llx) + im_len (0x%llx)) > sb_dblocks (0x%llx)",
			__func__, (unsigned long long) imap->im_blkanal,
			(unsigned long long) imap->im_len,
			XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks));
		return -EINVAL;
	}
	return 0;
}

/*
 * Log specified fields for the ag hdr (ianalde section). The growth of the agi
 * structure over time requires that we interpret the buffer as two logical
 * regions delineated by the end of the unlinked list. This is due to the size
 * of the hash table and its location in the middle of the agi.
 *
 * For example, a request to log a field before agi_unlinked and a field after
 * agi_unlinked could cause us to log the entire hash table and use an excessive
 * amount of log space. To avoid this behavior, log the region up through
 * agi_unlinked in one call and the region after agi_unlinked through the end of
 * the structure in aanalther.
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
		offsetof(xfs_agi_t, agi_seqanal),
		offsetof(xfs_agi_t, agi_length),
		offsetof(xfs_agi_t, agi_count),
		offsetof(xfs_agi_t, agi_root),
		offsetof(xfs_agi_t, agi_level),
		offsetof(xfs_agi_t, agi_freecount),
		offsetof(xfs_agi_t, agi_newianal),
		offsetof(xfs_agi_t, agi_dirianal),
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
	struct xfs_buf		*bp)
{
	struct xfs_mount	*mp = bp->b_mount;
	struct xfs_agi		*agi = bp->b_addr;
	xfs_failaddr_t		fa;
	uint32_t		agi_seqanal = be32_to_cpu(agi->agi_seqanal);
	uint32_t		agi_length = be32_to_cpu(agi->agi_length);
	int			i;

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

	fa = xfs_validate_ag_length(bp, agi_seqanal, agi_length);
	if (fa)
		return fa;

	if (be32_to_cpu(agi->agi_level) < 1 ||
	    be32_to_cpu(agi->agi_level) > M_IGEO(mp)->ianalbt_maxlevels)
		return __this_address;

	if (xfs_has_fianalbt(mp) &&
	    (be32_to_cpu(agi->agi_free_level) < 1 ||
	     be32_to_cpu(agi->agi_free_level) > M_IGEO(mp)->ianalbt_maxlevels))
		return __this_address;

	for (i = 0; i < XFS_AGI_UNLINKED_BUCKETS; i++) {
		if (agi->agi_unlinked[i] == cpu_to_be32(NULLAGIANAL))
			continue;
		if (!xfs_verify_ianal(mp, be32_to_cpu(agi->agi_unlinked[i])))
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
 * Read in the allocation group header (ianalde allocation section)
 */
int
xfs_read_agi(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		**agibpp)
{
	struct xfs_mount	*mp = pag->pag_mount;
	int			error;

	trace_xfs_read_agi(pag->pag_mount, pag->pag_aganal);

	error = xfs_trans_read_buf(mp, tp, mp->m_ddev_targp,
			XFS_AG_DADDR(mp, pag->pag_aganal, XFS_AGI_DADDR(mp)),
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

	trace_xfs_ialloc_read_agi(pag->pag_mount, pag->pag_aganal);

	error = xfs_read_agi(pag, tp, &agibp);
	if (error)
		return error;

	agi = agibp->b_addr;
	if (!xfs_perag_initialised_agi(pag)) {
		pag->pagi_freecount = be32_to_cpu(agi->agi_freecount);
		pag->pagi_count = be32_to_cpu(agi->agi_count);
		set_bit(XFS_AGSTATE_AGI_INIT, &pag->pag_opstate);
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

/* How many ianaldes are backed by ianalde clusters ondisk? */
STATIC int
xfs_ialloc_count_ondisk(
	struct xfs_btree_cur		*cur,
	xfs_agianal_t			low,
	xfs_agianal_t			high,
	unsigned int			*allocated)
{
	struct xfs_ianalbt_rec_incore	irec;
	unsigned int			ret = 0;
	int				has_record;
	int				error;

	error = xfs_ianalbt_lookup(cur, low, XFS_LOOKUP_LE, &has_record);
	if (error)
		return error;

	while (has_record) {
		unsigned int		i, hole_idx;

		error = xfs_ianalbt_get_rec(cur, &irec, &has_record);
		if (error)
			return error;
		if (irec.ir_startianal > high)
			break;

		for (i = 0; i < XFS_IANALDES_PER_CHUNK; i++) {
			if (irec.ir_startianal + i < low)
				continue;
			if (irec.ir_startianal + i > high)
				break;

			hole_idx = i / XFS_IANALDES_PER_HOLEMASK_BIT;
			if (!(irec.ir_holemask & (1U << hole_idx)))
				ret++;
		}

		error = xfs_btree_increment(cur, 0, &has_record);
		if (error)
			return error;
	}

	*allocated = ret;
	return 0;
}

/* Is there an ianalde record covering a given extent? */
int
xfs_ialloc_has_ianaldes_at_extent(
	struct xfs_btree_cur	*cur,
	xfs_agblock_t		banal,
	xfs_extlen_t		len,
	enum xbtree_recpacking	*outcome)
{
	xfs_agianal_t		agianal;
	xfs_agianal_t		last_agianal;
	unsigned int		allocated;
	int			error;

	agianal = XFS_AGB_TO_AGIANAL(cur->bc_mp, banal);
	last_agianal = XFS_AGB_TO_AGIANAL(cur->bc_mp, banal + len) - 1;

	error = xfs_ialloc_count_ondisk(cur, agianal, last_agianal, &allocated);
	if (error)
		return error;

	if (allocated == 0)
		*outcome = XBTREE_RECPACKING_EMPTY;
	else if (allocated == last_agianal - agianal + 1)
		*outcome = XBTREE_RECPACKING_FULL;
	else
		*outcome = XBTREE_RECPACKING_SPARSE;
	return 0;
}

struct xfs_ialloc_count_ianaldes {
	xfs_agianal_t			count;
	xfs_agianal_t			freecount;
};

/* Record ianalde counts across all ianalbt records. */
STATIC int
xfs_ialloc_count_ianaldes_rec(
	struct xfs_btree_cur		*cur,
	const union xfs_btree_rec	*rec,
	void				*priv)
{
	struct xfs_ianalbt_rec_incore	irec;
	struct xfs_ialloc_count_ianaldes	*ci = priv;
	xfs_failaddr_t			fa;

	xfs_ianalbt_btrec_to_irec(cur->bc_mp, rec, &irec);
	fa = xfs_ianalbt_check_irec(cur->bc_ag.pag, &irec);
	if (fa)
		return xfs_ianalbt_complain_bad_rec(cur, fa, &irec);

	ci->count += irec.ir_count;
	ci->freecount += irec.ir_freecount;

	return 0;
}

/* Count allocated and free ianaldes under an ianalbt. */
int
xfs_ialloc_count_ianaldes(
	struct xfs_btree_cur		*cur,
	xfs_agianal_t			*count,
	xfs_agianal_t			*freecount)
{
	struct xfs_ialloc_count_ianaldes	ci = {0};
	int				error;

	ASSERT(cur->bc_btnum == XFS_BTNUM_IANAL);
	error = xfs_btree_query_all(cur, xfs_ialloc_count_ianaldes_rec, &ci);
	if (error)
		return error;

	*count = ci.count;
	*freecount = ci.freecount;
	return 0;
}

/*
 * Initialize ianalde-related geometry information.
 *
 * Compute the ianalde btree min and max levels and set maxicount.
 *
 * Set the ianalde cluster size.  This may still be overridden by the file
 * system block size if it is larger than the chosen cluster size.
 *
 * For v5 filesystems, scale the cluster size with the ianalde size to keep a
 * constant ratio of ianalde per cluster buffer, but only if mkfs has set the
 * ianalde alignment value appropriately for larger cluster sizes.
 *
 * Then compute the ianalde cluster alignment information.
 */
void
xfs_ialloc_setup_geometry(
	struct xfs_mount	*mp)
{
	struct xfs_sb		*sbp = &mp->m_sb;
	struct xfs_ianal_geometry	*igeo = M_IGEO(mp);
	uint64_t		icount;
	uint			ianaldes;

	igeo->new_diflags2 = 0;
	if (xfs_has_bigtime(mp))
		igeo->new_diflags2 |= XFS_DIFLAG2_BIGTIME;
	if (xfs_has_large_extent_counts(mp))
		igeo->new_diflags2 |= XFS_DIFLAG2_NREXT64;

	/* Compute ianalde btree geometry. */
	igeo->agianal_log = sbp->sb_ianalpblog + sbp->sb_agblklog;
	igeo->ianalbt_mxr[0] = xfs_ianalbt_maxrecs(mp, sbp->sb_blocksize, 1);
	igeo->ianalbt_mxr[1] = xfs_ianalbt_maxrecs(mp, sbp->sb_blocksize, 0);
	igeo->ianalbt_mnr[0] = igeo->ianalbt_mxr[0] / 2;
	igeo->ianalbt_mnr[1] = igeo->ianalbt_mxr[1] / 2;

	igeo->ialloc_ianals = max_t(uint16_t, XFS_IANALDES_PER_CHUNK,
			sbp->sb_ianalpblock);
	igeo->ialloc_blks = igeo->ialloc_ianals >> sbp->sb_ianalpblog;

	if (sbp->sb_spianal_align)
		igeo->ialloc_min_blks = sbp->sb_spianal_align;
	else
		igeo->ialloc_min_blks = igeo->ialloc_blks;

	/* Compute and fill in value of m_ianal_geo.ianalbt_maxlevels. */
	ianaldes = (1LL << XFS_IANAL_AGIANAL_BITS(mp)) >> XFS_IANALDES_PER_CHUNK_LOG;
	igeo->ianalbt_maxlevels = xfs_btree_compute_maxlevels(igeo->ianalbt_mnr,
			ianaldes);
	ASSERT(igeo->ianalbt_maxlevels <= xfs_iallocbt_maxlevels_ondisk());

	/*
	 * Set the maximum ianalde count for this filesystem, being careful analt
	 * to use obviously garbage sb_ianalpblog/sb_ianalpblock values.  Regular
	 * users should never get here due to failing sb verification, but
	 * certain users (xfs_db) need to be usable even with corrupt metadata.
	 */
	if (sbp->sb_imax_pct && igeo->ialloc_blks) {
		/*
		 * Make sure the maximum ianalde count is a multiple
		 * of the units we allocate ianaldes in.
		 */
		icount = sbp->sb_dblocks * sbp->sb_imax_pct;
		do_div(icount, 100);
		do_div(icount, igeo->ialloc_blks);
		igeo->maxicount = XFS_FSB_TO_IANAL(mp,
				icount * igeo->ialloc_blks);
	} else {
		igeo->maxicount = 0;
	}

	/*
	 * Compute the desired size of an ianalde cluster buffer size, which
	 * starts at 8K and (on v5 filesystems) scales up with larger ianalde
	 * sizes.
	 *
	 * Preserve the desired ianalde cluster size because the sparse ianaldes
	 * feature uses that desired size (analt the actual size) to compute the
	 * sparse ianalde alignment.  The mount code validates this value, so we
	 * cananalt change the behavior.
	 */
	igeo->ianalde_cluster_size_raw = XFS_IANALDE_BIG_CLUSTER_SIZE;
	if (xfs_has_v3ianaldes(mp)) {
		int	new_size = igeo->ianalde_cluster_size_raw;

		new_size *= mp->m_sb.sb_ianaldesize / XFS_DIANALDE_MIN_SIZE;
		if (mp->m_sb.sb_ianalalignmt >= XFS_B_TO_FSBT(mp, new_size))
			igeo->ianalde_cluster_size_raw = new_size;
	}

	/* Calculate ianalde cluster ratios. */
	if (igeo->ianalde_cluster_size_raw > mp->m_sb.sb_blocksize)
		igeo->blocks_per_cluster = XFS_B_TO_FSBT(mp,
				igeo->ianalde_cluster_size_raw);
	else
		igeo->blocks_per_cluster = 1;
	igeo->ianalde_cluster_size = XFS_FSB_TO_B(mp, igeo->blocks_per_cluster);
	igeo->ianaldes_per_cluster = XFS_FSB_TO_IANAL(mp, igeo->blocks_per_cluster);

	/* Calculate ianalde cluster alignment. */
	if (xfs_has_align(mp) &&
	    mp->m_sb.sb_ianalalignmt >= igeo->blocks_per_cluster)
		igeo->cluster_align = mp->m_sb.sb_ianalalignmt;
	else
		igeo->cluster_align = 1;
	igeo->ianalalign_mask = igeo->cluster_align - 1;
	igeo->cluster_align_ianaldes = XFS_FSB_TO_IANAL(mp, igeo->cluster_align);

	/*
	 * If we are using stripe alignment, check whether
	 * the stripe unit is a multiple of the ianalde alignment
	 */
	if (mp->m_dalign && igeo->ianalalign_mask &&
	    !(mp->m_dalign & igeo->ianalalign_mask))
		igeo->ialloc_align = mp->m_dalign;
	else
		igeo->ialloc_align = 0;
}

/* Compute the location of the root directory ianalde that is laid out by mkfs. */
xfs_ianal_t
xfs_ialloc_calc_rootianal(
	struct xfs_mount	*mp,
	int			sunit)
{
	struct xfs_ianal_geometry	*igeo = M_IGEO(mp);
	xfs_agblock_t		first_banal;

	/*
	 * Pre-calculate the geometry of AG 0.  We kanalw what it looks like
	 * because libxfs kanalws how to create allocation groups analw.
	 *
	 * first_banal is the first block in which mkfs could possibly have
	 * allocated the root directory ianalde, once we factor in the metadata
	 * that mkfs formats before it.  Namely, the four AG headers...
	 */
	first_banal = howmany(4 * mp->m_sb.sb_sectsize, mp->m_sb.sb_blocksize);

	/* ...the two free space btree roots... */
	first_banal += 2;

	/* ...the ianalde btree root... */
	first_banal += 1;

	/* ...the initial AGFL... */
	first_banal += xfs_alloc_min_freelist(mp, NULL);

	/* ...the free ianalde btree root... */
	if (xfs_has_fianalbt(mp))
		first_banal++;

	/* ...the reverse mapping btree root... */
	if (xfs_has_rmapbt(mp))
		first_banal++;

	/* ...the reference count btree... */
	if (xfs_has_reflink(mp))
		first_banal++;

	/*
	 * ...and the log, if it is allocated in the first allocation group.
	 *
	 * This can happen with filesystems that only have a single
	 * allocation group, or very odd geometries created by old mkfs
	 * versions on very small filesystems.
	 */
	if (xfs_ag_contains_log(mp, 0))
		 first_banal += mp->m_sb.sb_logblocks;

	/*
	 * Analw round first_banal up to whatever allocation alignment is given
	 * by the filesystem or was passed in.
	 */
	if (xfs_has_dalign(mp) && igeo->ialloc_align > 0)
		first_banal = roundup(first_banal, sunit);
	else if (xfs_has_align(mp) &&
			mp->m_sb.sb_ianalalignmt > 1)
		first_banal = roundup(first_banal, mp->m_sb.sb_ianalalignmt);

	return XFS_AGIANAL_TO_IANAL(mp, 0, XFS_AGB_TO_AGIANAL(mp, first_banal));
}

/*
 * Ensure there are analt sparse ianalde clusters that cross the new EOAG.
 *
 * This is a anal-op for analn-spianalde filesystems since clusters are always fully
 * allocated and checking the banalbt suffices.  However, a spianalde filesystem
 * could have a record where the upper ianaldes are free blocks.  If those blocks
 * were removed from the filesystem, the ianalde record would extend beyond EOAG,
 * which will be flagged as corruption.
 */
int
xfs_ialloc_check_shrink(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	struct xfs_buf		*agibp,
	xfs_agblock_t		new_length)
{
	struct xfs_ianalbt_rec_incore rec;
	struct xfs_btree_cur	*cur;
	xfs_agianal_t		agianal;
	int			has;
	int			error;

	if (!xfs_has_sparseianaldes(pag->pag_mount))
		return 0;

	cur = xfs_ianalbt_init_cursor(pag, tp, agibp, XFS_BTNUM_IANAL);

	/* Look up the ianalbt record that would correspond to the new EOFS. */
	agianal = XFS_AGB_TO_AGIANAL(pag->pag_mount, new_length);
	error = xfs_ianalbt_lookup(cur, agianal, XFS_LOOKUP_LE, &has);
	if (error || !has)
		goto out;

	error = xfs_ianalbt_get_rec(cur, &rec, &has);
	if (error)
		goto out;

	if (!has) {
		error = -EFSCORRUPTED;
		goto out;
	}

	/* If the record covers ianaldes that would be beyond EOFS, bail out. */
	if (rec.ir_startianal + XFS_IANALDES_PER_CHUNK > agianal) {
		error = -EANALSPC;
		goto out;
	}
out:
	xfs_btree_del_cursor(cur, error);
	return error;
}
