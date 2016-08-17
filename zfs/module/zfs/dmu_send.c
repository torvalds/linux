/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 by Delphix. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2014, Joyent, Inc. All rights reserved.
 * Copyright (c) 2011, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/spa_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>
#include <zfs_fletcher.h>
#include <sys/avl.h>
#include <sys/ddt.h>
#include <sys/zfs_onexit.h>
#include <sys/dmu_send.h>
#include <sys/dsl_destroy.h>
#include <sys/blkptr.h>
#include <sys/dsl_bookmark.h>
#include <sys/zfeature.h>
#include <sys/zvol.h>

/* Set this tunable to TRUE to replace corrupt data with 0x2f5baddb10c */
int zfs_send_corrupt_data = B_FALSE;

static char *dmu_recv_tag = "dmu_recv_tag";
static const char *recv_clone_name = "%recv";

typedef struct dump_bytes_io {
	dmu_sendarg_t	*dbi_dsp;
	void		*dbi_buf;
	int		dbi_len;
} dump_bytes_io_t;

static void
dump_bytes_cb(void *arg)
{
	dump_bytes_io_t *dbi = (dump_bytes_io_t *)arg;
	dmu_sendarg_t *dsp = dbi->dbi_dsp;
	dsl_dataset_t *ds = dsp->dsa_os->os_dsl_dataset;
	ssize_t resid; /* have to get resid to get detailed errno */
	ASSERT0(dbi->dbi_len % 8);

	fletcher_4_incremental_native(dbi->dbi_buf, dbi->dbi_len, &dsp->dsa_zc);
	dsp->dsa_err = vn_rdwr(UIO_WRITE, dsp->dsa_vp,
	    (caddr_t)dbi->dbi_buf, dbi->dbi_len,
	    0, UIO_SYSSPACE, FAPPEND, RLIM64_INFINITY, CRED(), &resid);

	mutex_enter(&ds->ds_sendstream_lock);
	*dsp->dsa_off += dbi->dbi_len;
	mutex_exit(&ds->ds_sendstream_lock);
}

static int
dump_bytes(dmu_sendarg_t *dsp, void *buf, int len)
{
	dump_bytes_io_t dbi;

	dbi.dbi_dsp = dsp;
	dbi.dbi_buf = buf;
	dbi.dbi_len = len;

#if defined(HAVE_LARGE_STACKS)
	dump_bytes_cb(&dbi);
#else
	/*
	 * The vn_rdwr() call is performed in a taskq to ensure that there is
	 * always enough stack space to write safely to the target filesystem.
	 * The ZIO_TYPE_FREE threads are used because there can be a lot of
	 * them and they are used in vdev_file.c for a similar purpose.
	 */
	spa_taskq_dispatch_sync(dmu_objset_spa(dsp->dsa_os), ZIO_TYPE_FREE,
	    ZIO_TASKQ_ISSUE, dump_bytes_cb, &dbi, TQ_SLEEP);
#endif /* HAVE_LARGE_STACKS */

	return (dsp->dsa_err);
}

static int
dump_free(dmu_sendarg_t *dsp, uint64_t object, uint64_t offset,
    uint64_t length)
{
	struct drr_free *drrf = &(dsp->dsa_drr->drr_u.drr_free);

	/*
	 * When we receive a free record, dbuf_free_range() assumes
	 * that the receiving system doesn't have any dbufs in the range
	 * being freed.  This is always true because there is a one-record
	 * constraint: we only send one WRITE record for any given
	 * object+offset.  We know that the one-record constraint is
	 * true because we always send data in increasing order by
	 * object,offset.
	 *
	 * If the increasing-order constraint ever changes, we should find
	 * another way to assert that the one-record constraint is still
	 * satisfied.
	 */
	ASSERT(object > dsp->dsa_last_data_object ||
	    (object == dsp->dsa_last_data_object &&
	    offset > dsp->dsa_last_data_offset));

	/*
	 * If we are doing a non-incremental send, then there can't
	 * be any data in the dataset we're receiving into.  Therefore
	 * a free record would simply be a no-op.  Save space by not
	 * sending it to begin with.
	 */
	if (!dsp->dsa_incremental)
		return (0);

	if (length != -1ULL && offset + length < offset)
		length = -1ULL;

	/*
	 * If there is a pending op, but it's not PENDING_FREE, push it out,
	 * since free block aggregation can only be done for blocks of the
	 * same type (i.e., DRR_FREE records can only be aggregated with
	 * other DRR_FREE records.  DRR_FREEOBJECTS records can only be
	 * aggregated with other DRR_FREEOBJECTS records.
	 */
	if (dsp->dsa_pending_op != PENDING_NONE &&
	    dsp->dsa_pending_op != PENDING_FREE) {
		if (dump_bytes(dsp, dsp->dsa_drr,
		    sizeof (dmu_replay_record_t)) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}

	if (dsp->dsa_pending_op == PENDING_FREE) {
		/*
		 * There should never be a PENDING_FREE if length is -1
		 * (because dump_dnode is the only place where this
		 * function is called with a -1, and only after flushing
		 * any pending record).
		 */
		ASSERT(length != -1ULL);
		/*
		 * Check to see whether this free block can be aggregated
		 * with pending one.
		 */
		if (drrf->drr_object == object && drrf->drr_offset +
		    drrf->drr_length == offset) {
			drrf->drr_length += length;
			return (0);
		} else {
			/* not a continuation.  Push out pending record */
			if (dump_bytes(dsp, dsp->dsa_drr,
			    sizeof (dmu_replay_record_t)) != 0)
				return (SET_ERROR(EINTR));
			dsp->dsa_pending_op = PENDING_NONE;
		}
	}
	/* create a FREE record and make it pending */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_FREE;
	drrf->drr_object = object;
	drrf->drr_offset = offset;
	drrf->drr_length = length;
	drrf->drr_toguid = dsp->dsa_toguid;
	if (length == -1ULL) {
		if (dump_bytes(dsp, dsp->dsa_drr,
		    sizeof (dmu_replay_record_t)) != 0)
			return (SET_ERROR(EINTR));
	} else {
		dsp->dsa_pending_op = PENDING_FREE;
	}

	return (0);
}

static int
dump_write(dmu_sendarg_t *dsp, dmu_object_type_t type,
    uint64_t object, uint64_t offset, int blksz, const blkptr_t *bp, void *data)
{
	struct drr_write *drrw = &(dsp->dsa_drr->drr_u.drr_write);

	/*
	 * We send data in increasing object, offset order.
	 * See comment in dump_free() for details.
	 */
	ASSERT(object > dsp->dsa_last_data_object ||
	    (object == dsp->dsa_last_data_object &&
	    offset > dsp->dsa_last_data_offset));
	dsp->dsa_last_data_object = object;
	dsp->dsa_last_data_offset = offset + blksz - 1;

	/*
	 * If there is any kind of pending aggregation (currently either
	 * a grouping of free objects or free blocks), push it out to
	 * the stream, since aggregation can't be done across operations
	 * of different types.
	 */
	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_bytes(dsp, dsp->dsa_drr,
		    sizeof (dmu_replay_record_t)) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}
	/* write a DATA record */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_WRITE;
	drrw->drr_object = object;
	drrw->drr_type = type;
	drrw->drr_offset = offset;
	drrw->drr_length = blksz;
	drrw->drr_toguid = dsp->dsa_toguid;
	if (bp == NULL || BP_IS_EMBEDDED(bp)) {
		/*
		 * There's no pre-computed checksum for partial-block
		 * writes or embedded BP's, so (like
		 * fletcher4-checkummed blocks) userland will have to
		 * compute a dedup-capable checksum itself.
		 */
		drrw->drr_checksumtype = ZIO_CHECKSUM_OFF;
	} else {
		drrw->drr_checksumtype = BP_GET_CHECKSUM(bp);
		if (zio_checksum_table[drrw->drr_checksumtype].ci_dedup)
			drrw->drr_checksumflags |= DRR_CHECKSUM_DEDUP;
		DDK_SET_LSIZE(&drrw->drr_key, BP_GET_LSIZE(bp));
		DDK_SET_PSIZE(&drrw->drr_key, BP_GET_PSIZE(bp));
		DDK_SET_COMPRESS(&drrw->drr_key, BP_GET_COMPRESS(bp));
		drrw->drr_key.ddk_cksum = bp->blk_cksum;
	}

	if (dump_bytes(dsp, dsp->dsa_drr, sizeof (dmu_replay_record_t)) != 0)
		return (SET_ERROR(EINTR));
	if (dump_bytes(dsp, data, blksz) != 0)
		return (SET_ERROR(EINTR));
	return (0);
}

static int
dump_write_embedded(dmu_sendarg_t *dsp, uint64_t object, uint64_t offset,
    int blksz, const blkptr_t *bp)
{
	char buf[BPE_PAYLOAD_SIZE];
	struct drr_write_embedded *drrw =
	    &(dsp->dsa_drr->drr_u.drr_write_embedded);

	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_bytes(dsp, dsp->dsa_drr,
		    sizeof (dmu_replay_record_t)) != 0)
			return (EINTR);
		dsp->dsa_pending_op = PENDING_NONE;
	}

	ASSERT(BP_IS_EMBEDDED(bp));

	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_WRITE_EMBEDDED;
	drrw->drr_object = object;
	drrw->drr_offset = offset;
	drrw->drr_length = blksz;
	drrw->drr_toguid = dsp->dsa_toguid;
	drrw->drr_compression = BP_GET_COMPRESS(bp);
	drrw->drr_etype = BPE_GET_ETYPE(bp);
	drrw->drr_lsize = BPE_GET_LSIZE(bp);
	drrw->drr_psize = BPE_GET_PSIZE(bp);

	decode_embedded_bp_compressed(bp, buf);

	if (dump_bytes(dsp, dsp->dsa_drr, sizeof (dmu_replay_record_t)) != 0)
		return (EINTR);
	if (dump_bytes(dsp, buf, P2ROUNDUP(drrw->drr_psize, 8)) != 0)
		return (EINTR);
	return (0);
}

static int
dump_spill(dmu_sendarg_t *dsp, uint64_t object, int blksz, void *data)
{
	struct drr_spill *drrs = &(dsp->dsa_drr->drr_u.drr_spill);

	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_bytes(dsp, dsp->dsa_drr,
		    sizeof (dmu_replay_record_t)) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}

	/* write a SPILL record */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_SPILL;
	drrs->drr_object = object;
	drrs->drr_length = blksz;
	drrs->drr_toguid = dsp->dsa_toguid;

	if (dump_bytes(dsp, dsp->dsa_drr, sizeof (dmu_replay_record_t)))
		return (SET_ERROR(EINTR));
	if (dump_bytes(dsp, data, blksz))
		return (SET_ERROR(EINTR));
	return (0);
}

static int
dump_freeobjects(dmu_sendarg_t *dsp, uint64_t firstobj, uint64_t numobjs)
{
	struct drr_freeobjects *drrfo = &(dsp->dsa_drr->drr_u.drr_freeobjects);

	/* See comment in dump_free(). */
	if (!dsp->dsa_incremental)
		return (0);

	/*
	 * If there is a pending op, but it's not PENDING_FREEOBJECTS,
	 * push it out, since free block aggregation can only be done for
	 * blocks of the same type (i.e., DRR_FREE records can only be
	 * aggregated with other DRR_FREE records.  DRR_FREEOBJECTS records
	 * can only be aggregated with other DRR_FREEOBJECTS records.
	 */
	if (dsp->dsa_pending_op != PENDING_NONE &&
	    dsp->dsa_pending_op != PENDING_FREEOBJECTS) {
		if (dump_bytes(dsp, dsp->dsa_drr,
		    sizeof (dmu_replay_record_t)) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}
	if (dsp->dsa_pending_op == PENDING_FREEOBJECTS) {
		/*
		 * See whether this free object array can be aggregated
		 * with pending one
		 */
		if (drrfo->drr_firstobj + drrfo->drr_numobjs == firstobj) {
			drrfo->drr_numobjs += numobjs;
			return (0);
		} else {
			/* can't be aggregated.  Push out pending record */
			if (dump_bytes(dsp, dsp->dsa_drr,
			    sizeof (dmu_replay_record_t)) != 0)
				return (SET_ERROR(EINTR));
			dsp->dsa_pending_op = PENDING_NONE;
		}
	}

	/* write a FREEOBJECTS record */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_FREEOBJECTS;
	drrfo->drr_firstobj = firstobj;
	drrfo->drr_numobjs = numobjs;
	drrfo->drr_toguid = dsp->dsa_toguid;

	dsp->dsa_pending_op = PENDING_FREEOBJECTS;

	return (0);
}

static int
dump_dnode(dmu_sendarg_t *dsp, uint64_t object, dnode_phys_t *dnp)
{
	struct drr_object *drro = &(dsp->dsa_drr->drr_u.drr_object);

	if (dnp == NULL || dnp->dn_type == DMU_OT_NONE)
		return (dump_freeobjects(dsp, object, 1));

	if (dsp->dsa_pending_op != PENDING_NONE) {
		if (dump_bytes(dsp, dsp->dsa_drr,
		    sizeof (dmu_replay_record_t)) != 0)
			return (SET_ERROR(EINTR));
		dsp->dsa_pending_op = PENDING_NONE;
	}

	/* write an OBJECT record */
	bzero(dsp->dsa_drr, sizeof (dmu_replay_record_t));
	dsp->dsa_drr->drr_type = DRR_OBJECT;
	drro->drr_object = object;
	drro->drr_type = dnp->dn_type;
	drro->drr_bonustype = dnp->dn_bonustype;
	drro->drr_blksz = dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT;
	drro->drr_bonuslen = dnp->dn_bonuslen;
	drro->drr_checksumtype = dnp->dn_checksum;
	drro->drr_compress = dnp->dn_compress;
	drro->drr_toguid = dsp->dsa_toguid;

	if (!(dsp->dsa_featureflags & DMU_BACKUP_FEATURE_LARGE_BLOCKS) &&
	    drro->drr_blksz > SPA_OLD_MAXBLOCKSIZE)
		drro->drr_blksz = SPA_OLD_MAXBLOCKSIZE;

	if (dump_bytes(dsp, dsp->dsa_drr, sizeof (dmu_replay_record_t)) != 0)
		return (SET_ERROR(EINTR));

	if (dump_bytes(dsp, DN_BONUS(dnp), P2ROUNDUP(dnp->dn_bonuslen, 8)) != 0)
		return (SET_ERROR(EINTR));

	/* Free anything past the end of the file. */
	if (dump_free(dsp, object, (dnp->dn_maxblkid + 1) *
	    (dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT), -1ULL) != 0)
		return (SET_ERROR(EINTR));
	if (dsp->dsa_err != 0)
		return (SET_ERROR(EINTR));
	return (0);
}

static boolean_t
backup_do_embed(dmu_sendarg_t *dsp, const blkptr_t *bp)
{
	if (!BP_IS_EMBEDDED(bp))
		return (B_FALSE);

	/*
	 * Compression function must be legacy, or explicitly enabled.
	 */
	if ((BP_GET_COMPRESS(bp) >= ZIO_COMPRESS_LEGACY_FUNCTIONS &&
	    !(dsp->dsa_featureflags & DMU_BACKUP_FEATURE_EMBED_DATA_LZ4)))
		return (B_FALSE);

	/*
	 * Embed type must be explicitly enabled.
	 */
	switch (BPE_GET_ETYPE(bp)) {
	case BP_EMBEDDED_TYPE_DATA:
		if (dsp->dsa_featureflags & DMU_BACKUP_FEATURE_EMBED_DATA)
			return (B_TRUE);
		break;
	default:
		return (B_FALSE);
	}
	return (B_FALSE);
}

#define	BP_SPAN(dnp, level) \
	(((uint64_t)dnp->dn_datablkszsec) << (SPA_MINBLOCKSHIFT + \
	(level) * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT)))

/* ARGSUSED */
static int
backup_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	dmu_sendarg_t *dsp = arg;
	dmu_object_type_t type = bp ? BP_GET_TYPE(bp) : DMU_OT_NONE;
	int err = 0;

	if (issig(JUSTLOOKING) && issig(FORREAL))
		return (SET_ERROR(EINTR));

	if (zb->zb_object != DMU_META_DNODE_OBJECT &&
	    DMU_OBJECT_IS_SPECIAL(zb->zb_object)) {
		return (0);
	} else if (zb->zb_level == ZB_ZIL_LEVEL) {
		/*
		 * If we are sending a non-snapshot (which is allowed on
		 * read-only pools), it may have a ZIL, which must be ignored.
		 */
		return (0);
	} else if (BP_IS_HOLE(bp) &&
	    zb->zb_object == DMU_META_DNODE_OBJECT) {
		uint64_t span = BP_SPAN(dnp, zb->zb_level);
		uint64_t dnobj = (zb->zb_blkid * span) >> DNODE_SHIFT;
		err = dump_freeobjects(dsp, dnobj, span >> DNODE_SHIFT);
	} else if (BP_IS_HOLE(bp)) {
		uint64_t span = BP_SPAN(dnp, zb->zb_level);
		err = dump_free(dsp, zb->zb_object, zb->zb_blkid * span, span);
	} else if (zb->zb_level > 0 || type == DMU_OT_OBJSET) {
		return (0);
	} else if (type == DMU_OT_DNODE) {
		dnode_phys_t *blk;
		int i;
		int blksz = BP_GET_LSIZE(bp);
		arc_flags_t aflags = ARC_FLAG_WAIT;
		arc_buf_t *abuf;

		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL,
		    &aflags, zb) != 0)
			return (SET_ERROR(EIO));

		blk = abuf->b_data;
		for (i = 0; i < blksz >> DNODE_SHIFT; i++) {
			uint64_t dnobj = (zb->zb_blkid <<
			    (DNODE_BLOCK_SHIFT - DNODE_SHIFT)) + i;
			err = dump_dnode(dsp, dnobj, blk+i);
			if (err != 0)
				break;
		}
		(void) arc_buf_remove_ref(abuf, &abuf);
	} else if (type == DMU_OT_SA) {
		arc_flags_t aflags = ARC_FLAG_WAIT;
		arc_buf_t *abuf;
		int blksz = BP_GET_LSIZE(bp);

		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL,
		    &aflags, zb) != 0)
			return (SET_ERROR(EIO));

		err = dump_spill(dsp, zb->zb_object, blksz, abuf->b_data);
		(void) arc_buf_remove_ref(abuf, &abuf);
	} else if (backup_do_embed(dsp, bp)) {
		/* it's an embedded level-0 block of a regular object */
		int blksz = dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT;
		err = dump_write_embedded(dsp, zb->zb_object,
		    zb->zb_blkid * blksz, blksz, bp);
	} else { /* it's a level-0 block of a regular object */
		uint64_t offset;
		arc_flags_t aflags = ARC_FLAG_WAIT;
		arc_buf_t *abuf;
		int blksz = BP_GET_LSIZE(bp);

		ASSERT3U(blksz, ==, dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT);
		ASSERT0(zb->zb_level);
		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL,
		    &aflags, zb) != 0) {
			if (zfs_send_corrupt_data) {
				uint64_t *ptr;
				/* Send a block filled with 0x"zfs badd bloc" */
				abuf = arc_buf_alloc(spa, blksz, &abuf,
				    ARC_BUFC_DATA);
				for (ptr = abuf->b_data;
				    (char *)ptr < (char *)abuf->b_data + blksz;
				    ptr++)
					*ptr = 0x2f5baddb10cULL;
			} else {
				return (SET_ERROR(EIO));
			}
		}

		offset = zb->zb_blkid * blksz;

		if (!(dsp->dsa_featureflags &
		    DMU_BACKUP_FEATURE_LARGE_BLOCKS) &&
		    blksz > SPA_OLD_MAXBLOCKSIZE) {
			char *buf = abuf->b_data;
			while (blksz > 0 && err == 0) {
				int n = MIN(blksz, SPA_OLD_MAXBLOCKSIZE);
				err = dump_write(dsp, type, zb->zb_object,
				    offset, n, NULL, buf);
				offset += n;
				buf += n;
				blksz -= n;
			}
		} else {
			err = dump_write(dsp, type, zb->zb_object,
			    offset, blksz, bp, abuf->b_data);
		}
		(void) arc_buf_remove_ref(abuf, &abuf);
	}

	ASSERT(err == 0 || err == EINTR);
	return (err);
}

/*
 * Releases dp using the specified tag.
 */
static int
dmu_send_impl(void *tag, dsl_pool_t *dp, dsl_dataset_t *ds,
    zfs_bookmark_phys_t *fromzb, boolean_t is_clone, boolean_t embedok,
    boolean_t large_block_ok, int outfd, vnode_t *vp, offset_t *off)
{
	objset_t *os;
	dmu_replay_record_t *drr;
	dmu_sendarg_t *dsp;
	int err;
	uint64_t fromtxg = 0;
	uint64_t featureflags = 0;

	err = dmu_objset_from_ds(ds, &os);
	if (err != 0) {
		dsl_pool_rele(dp, tag);
		return (err);
	}

	drr = kmem_zalloc(sizeof (dmu_replay_record_t), KM_SLEEP);
	drr->drr_type = DRR_BEGIN;
	drr->drr_u.drr_begin.drr_magic = DMU_BACKUP_MAGIC;
	DMU_SET_STREAM_HDRTYPE(drr->drr_u.drr_begin.drr_versioninfo,
	    DMU_SUBSTREAM);

#ifdef _KERNEL
	if (dmu_objset_type(os) == DMU_OST_ZFS) {
		uint64_t version;
		if (zfs_get_zplprop(os, ZFS_PROP_VERSION, &version) != 0) {
			kmem_free(drr, sizeof (dmu_replay_record_t));
			dsl_pool_rele(dp, tag);
			return (SET_ERROR(EINVAL));
		}
		if (version >= ZPL_VERSION_SA) {
			featureflags |= DMU_BACKUP_FEATURE_SA_SPILL;
		}
	}
#endif

	if (large_block_ok && ds->ds_large_blocks)
		featureflags |= DMU_BACKUP_FEATURE_LARGE_BLOCKS;
	if (embedok &&
	    spa_feature_is_active(dp->dp_spa, SPA_FEATURE_EMBEDDED_DATA)) {
		featureflags |= DMU_BACKUP_FEATURE_EMBED_DATA;
		if (spa_feature_is_active(dp->dp_spa, SPA_FEATURE_LZ4_COMPRESS))
			featureflags |= DMU_BACKUP_FEATURE_EMBED_DATA_LZ4;
	} else {
		embedok = B_FALSE;
	}

	DMU_SET_FEATUREFLAGS(drr->drr_u.drr_begin.drr_versioninfo,
	    featureflags);

	drr->drr_u.drr_begin.drr_creation_time =
	    dsl_dataset_phys(ds)->ds_creation_time;
	drr->drr_u.drr_begin.drr_type = dmu_objset_type(os);
	if (is_clone)
		drr->drr_u.drr_begin.drr_flags |= DRR_FLAG_CLONE;
	drr->drr_u.drr_begin.drr_toguid = dsl_dataset_phys(ds)->ds_guid;
	if (dsl_dataset_phys(ds)->ds_flags & DS_FLAG_CI_DATASET)
		drr->drr_u.drr_begin.drr_flags |= DRR_FLAG_CI_DATA;

	if (fromzb != NULL) {
		drr->drr_u.drr_begin.drr_fromguid = fromzb->zbm_guid;
		fromtxg = fromzb->zbm_creation_txg;
	}
	dsl_dataset_name(ds, drr->drr_u.drr_begin.drr_toname);
	if (!ds->ds_is_snapshot) {
		(void) strlcat(drr->drr_u.drr_begin.drr_toname, "@--head--",
		    sizeof (drr->drr_u.drr_begin.drr_toname));
	}

	dsp = kmem_zalloc(sizeof (dmu_sendarg_t), KM_SLEEP);

	dsp->dsa_drr = drr;
	dsp->dsa_vp = vp;
	dsp->dsa_outfd = outfd;
	dsp->dsa_proc = curproc;
	dsp->dsa_os = os;
	dsp->dsa_off = off;
	dsp->dsa_toguid = dsl_dataset_phys(ds)->ds_guid;
	ZIO_SET_CHECKSUM(&dsp->dsa_zc, 0, 0, 0, 0);
	dsp->dsa_pending_op = PENDING_NONE;
	dsp->dsa_incremental = (fromzb != NULL);
	dsp->dsa_featureflags = featureflags;

	mutex_enter(&ds->ds_sendstream_lock);
	list_insert_head(&ds->ds_sendstreams, dsp);
	mutex_exit(&ds->ds_sendstream_lock);

	dsl_dataset_long_hold(ds, FTAG);
	dsl_pool_rele(dp, tag);

	if (dump_bytes(dsp, drr, sizeof (dmu_replay_record_t)) != 0) {
		err = dsp->dsa_err;
		goto out;
	}

	err = traverse_dataset(ds, fromtxg, TRAVERSE_PRE | TRAVERSE_PREFETCH,
	    backup_cb, dsp);

	if (dsp->dsa_pending_op != PENDING_NONE)
		if (dump_bytes(dsp, drr, sizeof (dmu_replay_record_t)) != 0)
			err = SET_ERROR(EINTR);

	if (err != 0) {
		if (err == EINTR && dsp->dsa_err != 0)
			err = dsp->dsa_err;
		goto out;
	}

	bzero(drr, sizeof (dmu_replay_record_t));
	drr->drr_type = DRR_END;
	drr->drr_u.drr_end.drr_checksum = dsp->dsa_zc;
	drr->drr_u.drr_end.drr_toguid = dsp->dsa_toguid;

	if (dump_bytes(dsp, drr, sizeof (dmu_replay_record_t)) != 0) {
		err = dsp->dsa_err;
		goto out;
	}

out:
	mutex_enter(&ds->ds_sendstream_lock);
	list_remove(&ds->ds_sendstreams, dsp);
	mutex_exit(&ds->ds_sendstream_lock);

	kmem_free(drr, sizeof (dmu_replay_record_t));
	kmem_free(dsp, sizeof (dmu_sendarg_t));

	dsl_dataset_long_rele(ds, FTAG);

	return (err);
}

int
dmu_send_obj(const char *pool, uint64_t tosnap, uint64_t fromsnap,
    boolean_t embedok, boolean_t large_block_ok,
    int outfd, vnode_t *vp, offset_t *off)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	dsl_dataset_t *fromds = NULL;
	int err;

	err = dsl_pool_hold(pool, FTAG, &dp);
	if (err != 0)
		return (err);

	err = dsl_dataset_hold_obj(dp, tosnap, FTAG, &ds);
	if (err != 0) {
		dsl_pool_rele(dp, FTAG);
		return (err);
	}

	if (fromsnap != 0) {
		zfs_bookmark_phys_t zb;
		boolean_t is_clone;

		err = dsl_dataset_hold_obj(dp, fromsnap, FTAG, &fromds);
		if (err != 0) {
			dsl_dataset_rele(ds, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (err);
		}
		if (!dsl_dataset_is_before(ds, fromds, 0))
			err = SET_ERROR(EXDEV);
		zb.zbm_creation_time =
		    dsl_dataset_phys(fromds)->ds_creation_time;
		zb.zbm_creation_txg = dsl_dataset_phys(fromds)->ds_creation_txg;
		zb.zbm_guid = dsl_dataset_phys(fromds)->ds_guid;
		is_clone = (fromds->ds_dir != ds->ds_dir);
		dsl_dataset_rele(fromds, FTAG);
		err = dmu_send_impl(FTAG, dp, ds, &zb, is_clone,
		    embedok, large_block_ok, outfd, vp, off);
	} else {
		err = dmu_send_impl(FTAG, dp, ds, NULL, B_FALSE,
		    embedok, large_block_ok, outfd, vp, off);
	}
	dsl_dataset_rele(ds, FTAG);
	return (err);
}

int
dmu_send(const char *tosnap, const char *fromsnap,
    boolean_t embedok, boolean_t large_block_ok,
    int outfd, vnode_t *vp, offset_t *off)
{
	dsl_pool_t *dp;
	dsl_dataset_t *ds;
	int err;
	boolean_t owned = B_FALSE;

	if (fromsnap != NULL && strpbrk(fromsnap, "@#") == NULL)
		return (SET_ERROR(EINVAL));

	err = dsl_pool_hold(tosnap, FTAG, &dp);
	if (err != 0)
		return (err);

	if (strchr(tosnap, '@') == NULL && spa_writeable(dp->dp_spa)) {
		/*
		 * We are sending a filesystem or volume.  Ensure
		 * that it doesn't change by owning the dataset.
		 */
		err = dsl_dataset_own(dp, tosnap, FTAG, &ds);
		owned = B_TRUE;
	} else {
		err = dsl_dataset_hold(dp, tosnap, FTAG, &ds);
	}
	if (err != 0) {
		dsl_pool_rele(dp, FTAG);
		return (err);
	}

	if (fromsnap != NULL) {
		zfs_bookmark_phys_t zb;
		boolean_t is_clone = B_FALSE;
		int fsnamelen = strchr(tosnap, '@') - tosnap;

		/*
		 * If the fromsnap is in a different filesystem, then
		 * mark the send stream as a clone.
		 */
		if (strncmp(tosnap, fromsnap, fsnamelen) != 0 ||
		    (fromsnap[fsnamelen] != '@' &&
		    fromsnap[fsnamelen] != '#')) {
			is_clone = B_TRUE;
		}

		if (strchr(fromsnap, '@')) {
			dsl_dataset_t *fromds;
			err = dsl_dataset_hold(dp, fromsnap, FTAG, &fromds);
			if (err == 0) {
				if (!dsl_dataset_is_before(ds, fromds, 0))
					err = SET_ERROR(EXDEV);
				zb.zbm_creation_time =
				    dsl_dataset_phys(fromds)->ds_creation_time;
				zb.zbm_creation_txg =
				    dsl_dataset_phys(fromds)->ds_creation_txg;
				zb.zbm_guid = dsl_dataset_phys(fromds)->ds_guid;
				is_clone = (ds->ds_dir != fromds->ds_dir);
				dsl_dataset_rele(fromds, FTAG);
			}
		} else {
			err = dsl_bookmark_lookup(dp, fromsnap, ds, &zb);
		}
		if (err != 0) {
			dsl_dataset_rele(ds, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (err);
		}
		err = dmu_send_impl(FTAG, dp, ds, &zb, is_clone,
		    embedok, large_block_ok, outfd, vp, off);
	} else {
		err = dmu_send_impl(FTAG, dp, ds, NULL, B_FALSE,
		    embedok, large_block_ok, outfd, vp, off);
	}
	if (owned)
		dsl_dataset_disown(ds, FTAG);
	else
		dsl_dataset_rele(ds, FTAG);
	return (err);
}

static int
dmu_adjust_send_estimate_for_indirects(dsl_dataset_t *ds, uint64_t size,
    uint64_t *sizep)
{
	int err;
	/*
	 * Assume that space (both on-disk and in-stream) is dominated by
	 * data.  We will adjust for indirect blocks and the copies property,
	 * but ignore per-object space used (eg, dnodes and DRR_OBJECT records).
	 */

	/*
	 * Subtract out approximate space used by indirect blocks.
	 * Assume most space is used by data blocks (non-indirect, non-dnode).
	 * Assume all blocks are recordsize.  Assume ditto blocks and
	 * internal fragmentation counter out compression.
	 *
	 * Therefore, space used by indirect blocks is sizeof(blkptr_t) per
	 * block, which we observe in practice.
	 */
	uint64_t recordsize;
	err = dsl_prop_get_int_ds(ds, "recordsize", &recordsize);
	if (err != 0)
		return (err);
	size -= size / recordsize * sizeof (blkptr_t);

	/* Add in the space for the record associated with each block. */
	size += size / recordsize * sizeof (dmu_replay_record_t);

	*sizep = size;

	return (0);
}

int
dmu_send_estimate(dsl_dataset_t *ds, dsl_dataset_t *fromds, uint64_t *sizep)
{
	int err;
	uint64_t size;

	ASSERT(dsl_pool_config_held(ds->ds_dir->dd_pool));

	/* tosnap must be a snapshot */
	if (!ds->ds_is_snapshot)
		return (SET_ERROR(EINVAL));

	/* fromsnap, if provided, must be a snapshot */
	if (fromds != NULL && !fromds->ds_is_snapshot)
		return (SET_ERROR(EINVAL));

	/*
	 * fromsnap must be an earlier snapshot from the same fs as tosnap,
	 * or the origin's fs.
	 */
	if (fromds != NULL && !dsl_dataset_is_before(ds, fromds, 0))
		return (SET_ERROR(EXDEV));

	/* Get uncompressed size estimate of changed data. */
	if (fromds == NULL) {
		size = dsl_dataset_phys(ds)->ds_uncompressed_bytes;
	} else {
		uint64_t used, comp;
		err = dsl_dataset_space_written(fromds, ds,
		    &used, &comp, &size);
		if (err != 0)
			return (err);
	}

	err = dmu_adjust_send_estimate_for_indirects(ds, size, sizep);
	return (err);
}

/*
 * Simple callback used to traverse the blocks of a snapshot and sum their
 * uncompressed size
 */
/* ARGSUSED */
static int
dmu_calculate_send_traversal(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	uint64_t *spaceptr = arg;
	if (bp != NULL && !BP_IS_HOLE(bp)) {
		*spaceptr += BP_GET_UCSIZE(bp);
	}
	return (0);
}

/*
 * Given a desination snapshot and a TXG, calculate the approximate size of a
 * send stream sent from that TXG. from_txg may be zero, indicating that the
 * whole snapshot will be sent.
 */
int
dmu_send_estimate_from_txg(dsl_dataset_t *ds, uint64_t from_txg,
    uint64_t *sizep)
{
	int err;
	uint64_t size = 0;

	ASSERT(dsl_pool_config_held(ds->ds_dir->dd_pool));

	/* tosnap must be a snapshot */
	if (!dsl_dataset_is_snapshot(ds))
		return (SET_ERROR(EINVAL));

	/* verify that from_txg is before the provided snapshot was taken */
	if (from_txg >= dsl_dataset_phys(ds)->ds_creation_txg) {
		return (SET_ERROR(EXDEV));
	}
	/*
	 * traverse the blocks of the snapshot with birth times after
	 * from_txg, summing their uncompressed size
	 */
	err = traverse_dataset(ds, from_txg, TRAVERSE_POST,
	    dmu_calculate_send_traversal, &size);
	if (err)
		return (err);

	err = dmu_adjust_send_estimate_for_indirects(ds, size, sizep);
	return (err);
}

typedef struct dmu_recv_begin_arg {
	const char *drba_origin;
	dmu_recv_cookie_t *drba_cookie;
	cred_t *drba_cred;
	uint64_t drba_snapobj;
} dmu_recv_begin_arg_t;

static int
recv_begin_check_existing_impl(dmu_recv_begin_arg_t *drba, dsl_dataset_t *ds,
    uint64_t fromguid)
{
	uint64_t val;
	int error;
	dsl_pool_t *dp = ds->ds_dir->dd_pool;

	/* temporary clone name must not exist */
	error = zap_lookup(dp->dp_meta_objset,
	    dsl_dir_phys(ds->ds_dir)->dd_child_dir_zapobj, recv_clone_name,
	    8, 1, &val);
	if (error != ENOENT)
		return (error == 0 ? EBUSY : error);

	/* new snapshot name must not exist */
	error = zap_lookup(dp->dp_meta_objset,
	    dsl_dataset_phys(ds)->ds_snapnames_zapobj,
	    drba->drba_cookie->drc_tosnap, 8, 1, &val);
	if (error != ENOENT)
		return (error == 0 ? EEXIST : error);

	/*
	 * Check snapshot limit before receiving. We'll recheck again at the
	 * end, but might as well abort before receiving if we're already over
	 * the limit.
	 *
	 * Note that we do not check the file system limit with
	 * dsl_dir_fscount_check because the temporary %clones don't count
	 * against that limit.
	 */
	error = dsl_fs_ss_limit_check(ds->ds_dir, 1, ZFS_PROP_SNAPSHOT_LIMIT,
	    NULL, drba->drba_cred);
	if (error != 0)
		return (error);

	if (fromguid != 0) {
		dsl_dataset_t *snap;
		uint64_t obj = dsl_dataset_phys(ds)->ds_prev_snap_obj;

		/* Find snapshot in this dir that matches fromguid. */
		while (obj != 0) {
			error = dsl_dataset_hold_obj(dp, obj, FTAG,
			    &snap);
			if (error != 0)
				return (SET_ERROR(ENODEV));
			if (snap->ds_dir != ds->ds_dir) {
				dsl_dataset_rele(snap, FTAG);
				return (SET_ERROR(ENODEV));
			}
			if (dsl_dataset_phys(snap)->ds_guid == fromguid)
				break;
			obj = dsl_dataset_phys(snap)->ds_prev_snap_obj;
			dsl_dataset_rele(snap, FTAG);
		}
		if (obj == 0)
			return (SET_ERROR(ENODEV));

		if (drba->drba_cookie->drc_force) {
			drba->drba_snapobj = obj;
		} else {
			/*
			 * If we are not forcing, there must be no
			 * changes since fromsnap.
			 */
			if (dsl_dataset_modified_since_snap(ds, snap)) {
				dsl_dataset_rele(snap, FTAG);
				return (SET_ERROR(ETXTBSY));
			}
			drba->drba_snapobj = ds->ds_prev->ds_object;
		}

		dsl_dataset_rele(snap, FTAG);
	} else {
		/* if full, then must be forced */
		if (!drba->drba_cookie->drc_force)
			return (SET_ERROR(EEXIST));
		/* start from $ORIGIN@$ORIGIN, if supported */
		drba->drba_snapobj = dp->dp_origin_snap != NULL ?
		    dp->dp_origin_snap->ds_object : 0;
	}

	return (0);

}

static int
dmu_recv_begin_check(void *arg, dmu_tx_t *tx)
{
	dmu_recv_begin_arg_t *drba = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	struct drr_begin *drrb = drba->drba_cookie->drc_drrb;
	uint64_t fromguid = drrb->drr_fromguid;
	int flags = drrb->drr_flags;
	int error;
	uint64_t featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
	dsl_dataset_t *ds;
	const char *tofs = drba->drba_cookie->drc_tofs;

	/* already checked */
	ASSERT3U(drrb->drr_magic, ==, DMU_BACKUP_MAGIC);

	if (DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
	    DMU_COMPOUNDSTREAM ||
	    drrb->drr_type >= DMU_OST_NUMTYPES ||
	    ((flags & DRR_FLAG_CLONE) && drba->drba_origin == NULL))
		return (SET_ERROR(EINVAL));

	/* Verify pool version supports SA if SA_SPILL feature set */
	if ((featureflags & DMU_BACKUP_FEATURE_SA_SPILL) &&
	    spa_version(dp->dp_spa) < SPA_VERSION_SA)
		return (SET_ERROR(ENOTSUP));

	/*
	 * The receiving code doesn't know how to translate a WRITE_EMBEDDED
	 * record to a plan WRITE record, so the pool must have the
	 * EMBEDDED_DATA feature enabled if the stream has WRITE_EMBEDDED
	 * records.  Same with WRITE_EMBEDDED records that use LZ4 compression.
	 */
	if ((featureflags & DMU_BACKUP_FEATURE_EMBED_DATA) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_EMBEDDED_DATA))
		return (SET_ERROR(ENOTSUP));
	if ((featureflags & DMU_BACKUP_FEATURE_EMBED_DATA_LZ4) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_LZ4_COMPRESS))
		return (SET_ERROR(ENOTSUP));

	/*
	 * The receiving code doesn't know how to translate large blocks
	 * to smaller ones, so the pool must have the LARGE_BLOCKS
	 * feature enabled if the stream has LARGE_BLOCKS.
	 */
	if ((featureflags & DMU_BACKUP_FEATURE_LARGE_BLOCKS) &&
	    !spa_feature_is_enabled(dp->dp_spa, SPA_FEATURE_LARGE_BLOCKS))
		return (SET_ERROR(ENOTSUP));

	error = dsl_dataset_hold(dp, tofs, FTAG, &ds);
	if (error == 0) {
		/* target fs already exists; recv into temp clone */

		/* Can't recv a clone into an existing fs */
		if (flags & DRR_FLAG_CLONE) {
			dsl_dataset_rele(ds, FTAG);
			return (SET_ERROR(EINVAL));
		}

		error = recv_begin_check_existing_impl(drba, ds, fromguid);
		dsl_dataset_rele(ds, FTAG);
	} else if (error == ENOENT) {
		/* target fs does not exist; must be a full backup or clone */
		char buf[MAXNAMELEN];

		/*
		 * If it's a non-clone incremental, we are missing the
		 * target fs, so fail the recv.
		 */
		if (fromguid != 0 && !(flags & DRR_FLAG_CLONE))
			return (SET_ERROR(ENOENT));

		/* Open the parent of tofs */
		ASSERT3U(strlen(tofs), <, MAXNAMELEN);
		(void) strlcpy(buf, tofs, strrchr(tofs, '/') - tofs + 1);
		error = dsl_dataset_hold(dp, buf, FTAG, &ds);
		if (error != 0)
			return (error);

		/*
		 * Check filesystem and snapshot limits before receiving. We'll
		 * recheck snapshot limits again at the end (we create the
		 * filesystems and increment those counts during begin_sync).
		 */
		error = dsl_fs_ss_limit_check(ds->ds_dir, 1,
		    ZFS_PROP_FILESYSTEM_LIMIT, NULL, drba->drba_cred);
		if (error != 0) {
			dsl_dataset_rele(ds, FTAG);
			return (error);
		}

		error = dsl_fs_ss_limit_check(ds->ds_dir, 1,
		    ZFS_PROP_SNAPSHOT_LIMIT, NULL, drba->drba_cred);
		if (error != 0) {
			dsl_dataset_rele(ds, FTAG);
			return (error);
		}

		if (drba->drba_origin != NULL) {
			dsl_dataset_t *origin;
			error = dsl_dataset_hold(dp, drba->drba_origin,
			    FTAG, &origin);
			if (error != 0) {
				dsl_dataset_rele(ds, FTAG);
				return (error);
			}
			if (!origin->ds_is_snapshot) {
				dsl_dataset_rele(origin, FTAG);
				dsl_dataset_rele(ds, FTAG);
				return (SET_ERROR(EINVAL));
			}
			if (dsl_dataset_phys(origin)->ds_guid != fromguid) {
				dsl_dataset_rele(origin, FTAG);
				dsl_dataset_rele(ds, FTAG);
				return (SET_ERROR(ENODEV));
			}
			dsl_dataset_rele(origin, FTAG);
		}
		dsl_dataset_rele(ds, FTAG);
		error = 0;
	}
	return (error);
}

static void
dmu_recv_begin_sync(void *arg, dmu_tx_t *tx)
{
	dmu_recv_begin_arg_t *drba = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	struct drr_begin *drrb = drba->drba_cookie->drc_drrb;
	const char *tofs = drba->drba_cookie->drc_tofs;
	dsl_dataset_t *ds, *newds;
	uint64_t dsobj;
	int error;
	uint64_t crflags;

	crflags = (drrb->drr_flags & DRR_FLAG_CI_DATA) ?
	    DS_FLAG_CI_DATASET : 0;

	error = dsl_dataset_hold(dp, tofs, FTAG, &ds);
	if (error == 0) {
		/* create temporary clone */
		dsl_dataset_t *snap = NULL;
		if (drba->drba_snapobj != 0) {
			VERIFY0(dsl_dataset_hold_obj(dp,
			    drba->drba_snapobj, FTAG, &snap));
		}
		dsobj = dsl_dataset_create_sync(ds->ds_dir, recv_clone_name,
		    snap, crflags, drba->drba_cred, tx);
		dsl_dataset_rele(snap, FTAG);
		dsl_dataset_rele(ds, FTAG);
	} else {
		dsl_dir_t *dd;
		const char *tail;
		dsl_dataset_t *origin = NULL;

		VERIFY0(dsl_dir_hold(dp, tofs, FTAG, &dd, &tail));

		if (drba->drba_origin != NULL) {
			VERIFY0(dsl_dataset_hold(dp, drba->drba_origin,
			    FTAG, &origin));
		}

		/* Create new dataset. */
		dsobj = dsl_dataset_create_sync(dd,
		    strrchr(tofs, '/') + 1,
		    origin, crflags, drba->drba_cred, tx);
		if (origin != NULL)
			dsl_dataset_rele(origin, FTAG);
		dsl_dir_rele(dd, FTAG);
		drba->drba_cookie->drc_newfs = B_TRUE;
	}
	VERIFY0(dsl_dataset_own_obj(dp, dsobj, dmu_recv_tag, &newds));

	if ((DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo) &
	    DMU_BACKUP_FEATURE_LARGE_BLOCKS) &&
	    !newds->ds_large_blocks) {
		dsl_dataset_activate_large_blocks_sync_impl(dsobj, tx);
		newds->ds_large_blocks = B_TRUE;
	}

	dmu_buf_will_dirty(newds->ds_dbuf, tx);
	dsl_dataset_phys(newds)->ds_flags |= DS_FLAG_INCONSISTENT;

	/*
	 * If we actually created a non-clone, we need to create the
	 * objset in our new dataset.
	 */
	if (BP_IS_HOLE(dsl_dataset_get_blkptr(newds))) {
		(void) dmu_objset_create_impl(dp->dp_spa,
		    newds, dsl_dataset_get_blkptr(newds), drrb->drr_type, tx);
	}

	drba->drba_cookie->drc_ds = newds;

	spa_history_log_internal_ds(newds, "receive", tx, "");
}

/*
 * NB: callers *MUST* call dmu_recv_stream() if dmu_recv_begin()
 * succeeds; otherwise we will leak the holds on the datasets.
 */
int
dmu_recv_begin(char *tofs, char *tosnap, struct drr_begin *drrb,
    boolean_t force, char *origin, dmu_recv_cookie_t *drc)
{
	dmu_recv_begin_arg_t drba = { 0 };
	dmu_replay_record_t *drr;

	bzero(drc, sizeof (dmu_recv_cookie_t));
	drc->drc_drrb = drrb;
	drc->drc_tosnap = tosnap;
	drc->drc_tofs = tofs;
	drc->drc_force = force;
	drc->drc_cred = CRED();

	if (drrb->drr_magic == BSWAP_64(DMU_BACKUP_MAGIC))
		drc->drc_byteswap = B_TRUE;
	else if (drrb->drr_magic != DMU_BACKUP_MAGIC)
		return (SET_ERROR(EINVAL));

	drr = kmem_zalloc(sizeof (dmu_replay_record_t), KM_SLEEP);
	drr->drr_type = DRR_BEGIN;
	drr->drr_u.drr_begin = *drc->drc_drrb;
	if (drc->drc_byteswap) {
		fletcher_4_incremental_byteswap(drr,
		    sizeof (dmu_replay_record_t), &drc->drc_cksum);
	} else {
		fletcher_4_incremental_native(drr,
		    sizeof (dmu_replay_record_t), &drc->drc_cksum);
	}
	kmem_free(drr, sizeof (dmu_replay_record_t));

	if (drc->drc_byteswap) {
		drrb->drr_magic = BSWAP_64(drrb->drr_magic);
		drrb->drr_versioninfo = BSWAP_64(drrb->drr_versioninfo);
		drrb->drr_creation_time = BSWAP_64(drrb->drr_creation_time);
		drrb->drr_type = BSWAP_32(drrb->drr_type);
		drrb->drr_toguid = BSWAP_64(drrb->drr_toguid);
		drrb->drr_fromguid = BSWAP_64(drrb->drr_fromguid);
	}

	drba.drba_origin = origin;
	drba.drba_cookie = drc;
	drba.drba_cred = CRED();

	return (dsl_sync_task(tofs, dmu_recv_begin_check, dmu_recv_begin_sync,
	    &drba, 5, ZFS_SPACE_CHECK_NORMAL));
}

struct restorearg {
	int err;
	boolean_t byteswap;
	vnode_t *vp;
	char *buf;
	uint64_t voff;
	int bufsize; /* amount of memory allocated for buf */
	zio_cksum_t cksum;
	avl_tree_t *guid_to_ds_map;
};

typedef struct guid_map_entry {
	uint64_t	guid;
	dsl_dataset_t	*gme_ds;
	avl_node_t	avlnode;
} guid_map_entry_t;

static int
guid_compare(const void *arg1, const void *arg2)
{
	const guid_map_entry_t *gmep1 = arg1;
	const guid_map_entry_t *gmep2 = arg2;

	if (gmep1->guid < gmep2->guid)
		return (-1);
	else if (gmep1->guid > gmep2->guid)
		return (1);
	return (0);
}

static void
free_guid_map_onexit(void *arg)
{
	avl_tree_t *ca = arg;
	void *cookie = NULL;
	guid_map_entry_t *gmep;

	while ((gmep = avl_destroy_nodes(ca, &cookie)) != NULL) {
		dsl_dataset_long_rele(gmep->gme_ds, gmep);
		dsl_dataset_rele(gmep->gme_ds, gmep);
		kmem_free(gmep, sizeof (guid_map_entry_t));
	}
	avl_destroy(ca);
	kmem_free(ca, sizeof (avl_tree_t));
}

static void *
restore_read(struct restorearg *ra, int len, char *buf)
{
	int done = 0;

	if (buf == NULL)
		buf = ra->buf;

	/* some things will require 8-byte alignment, so everything must */
	ASSERT0(len % 8);
	ASSERT3U(len, <=, ra->bufsize);

	while (done < len) {
		ssize_t resid;

		ra->err = vn_rdwr(UIO_READ, ra->vp,
		    buf + done, len - done,
		    ra->voff, UIO_SYSSPACE, FAPPEND,
		    RLIM64_INFINITY, CRED(), &resid);

		if (resid == len - done)
			ra->err = SET_ERROR(EINVAL);
		ra->voff += len - done - resid;
		done = len - resid;
		if (ra->err != 0)
			return (NULL);
	}

	ASSERT3U(done, ==, len);
	if (ra->byteswap)
		fletcher_4_incremental_byteswap(buf, len, &ra->cksum);
	else
		fletcher_4_incremental_native(buf, len, &ra->cksum);
	return (buf);
}

noinline static void
backup_byteswap(dmu_replay_record_t *drr)
{
#define	DO64(X) (drr->drr_u.X = BSWAP_64(drr->drr_u.X))
#define	DO32(X) (drr->drr_u.X = BSWAP_32(drr->drr_u.X))
	drr->drr_type = BSWAP_32(drr->drr_type);
	drr->drr_payloadlen = BSWAP_32(drr->drr_payloadlen);
	switch (drr->drr_type) {
	case DRR_BEGIN:
		DO64(drr_begin.drr_magic);
		DO64(drr_begin.drr_versioninfo);
		DO64(drr_begin.drr_creation_time);
		DO32(drr_begin.drr_type);
		DO32(drr_begin.drr_flags);
		DO64(drr_begin.drr_toguid);
		DO64(drr_begin.drr_fromguid);
		break;
	case DRR_OBJECT:
		DO64(drr_object.drr_object);
		DO32(drr_object.drr_type);
		DO32(drr_object.drr_bonustype);
		DO32(drr_object.drr_blksz);
		DO32(drr_object.drr_bonuslen);
		DO64(drr_object.drr_toguid);
		break;
	case DRR_FREEOBJECTS:
		DO64(drr_freeobjects.drr_firstobj);
		DO64(drr_freeobjects.drr_numobjs);
		DO64(drr_freeobjects.drr_toguid);
		break;
	case DRR_WRITE:
		DO64(drr_write.drr_object);
		DO32(drr_write.drr_type);
		DO64(drr_write.drr_offset);
		DO64(drr_write.drr_length);
		DO64(drr_write.drr_toguid);
		DO64(drr_write.drr_key.ddk_cksum.zc_word[0]);
		DO64(drr_write.drr_key.ddk_cksum.zc_word[1]);
		DO64(drr_write.drr_key.ddk_cksum.zc_word[2]);
		DO64(drr_write.drr_key.ddk_cksum.zc_word[3]);
		DO64(drr_write.drr_key.ddk_prop);
		break;
	case DRR_WRITE_BYREF:
		DO64(drr_write_byref.drr_object);
		DO64(drr_write_byref.drr_offset);
		DO64(drr_write_byref.drr_length);
		DO64(drr_write_byref.drr_toguid);
		DO64(drr_write_byref.drr_refguid);
		DO64(drr_write_byref.drr_refobject);
		DO64(drr_write_byref.drr_refoffset);
		DO64(drr_write_byref.drr_key.ddk_cksum.zc_word[0]);
		DO64(drr_write_byref.drr_key.ddk_cksum.zc_word[1]);
		DO64(drr_write_byref.drr_key.ddk_cksum.zc_word[2]);
		DO64(drr_write_byref.drr_key.ddk_cksum.zc_word[3]);
		DO64(drr_write_byref.drr_key.ddk_prop);
		break;
	case DRR_WRITE_EMBEDDED:
		DO64(drr_write_embedded.drr_object);
		DO64(drr_write_embedded.drr_offset);
		DO64(drr_write_embedded.drr_length);
		DO64(drr_write_embedded.drr_toguid);
		DO32(drr_write_embedded.drr_lsize);
		DO32(drr_write_embedded.drr_psize);
		break;
	case DRR_FREE:
		DO64(drr_free.drr_object);
		DO64(drr_free.drr_offset);
		DO64(drr_free.drr_length);
		DO64(drr_free.drr_toguid);
		break;
	case DRR_SPILL:
		DO64(drr_spill.drr_object);
		DO64(drr_spill.drr_length);
		DO64(drr_spill.drr_toguid);
		break;
	case DRR_END:
		DO64(drr_end.drr_checksum.zc_word[0]);
		DO64(drr_end.drr_checksum.zc_word[1]);
		DO64(drr_end.drr_checksum.zc_word[2]);
		DO64(drr_end.drr_checksum.zc_word[3]);
		DO64(drr_end.drr_toguid);
		break;
	default:
		break;
	}
#undef DO64
#undef DO32
}

static inline uint8_t
deduce_nblkptr(dmu_object_type_t bonus_type, uint64_t bonus_size)
{
	if (bonus_type == DMU_OT_SA) {
		return (1);
	} else {
		return (1 +
		    ((DN_MAX_BONUSLEN - bonus_size) >> SPA_BLKPTRSHIFT));
	}
}

noinline static int
restore_object(struct restorearg *ra, objset_t *os, struct drr_object *drro)
{
	dmu_object_info_t doi;
	dmu_tx_t *tx;
	void *data = NULL;
	uint64_t object;
	int err;

	if (drro->drr_type == DMU_OT_NONE ||
	    !DMU_OT_IS_VALID(drro->drr_type) ||
	    !DMU_OT_IS_VALID(drro->drr_bonustype) ||
	    drro->drr_checksumtype >= ZIO_CHECKSUM_FUNCTIONS ||
	    drro->drr_compress >= ZIO_COMPRESS_FUNCTIONS ||
	    P2PHASE(drro->drr_blksz, SPA_MINBLOCKSIZE) ||
	    drro->drr_blksz < SPA_MINBLOCKSIZE ||
	    drro->drr_blksz > spa_maxblocksize(dmu_objset_spa(os)) ||
	    drro->drr_bonuslen > DN_MAX_BONUSLEN) {
		return (SET_ERROR(EINVAL));
	}

	err = dmu_object_info(os, drro->drr_object, &doi);

	if (err != 0 && err != ENOENT)
		return (SET_ERROR(EINVAL));
	object = err == 0 ? drro->drr_object : DMU_NEW_OBJECT;

	if (drro->drr_bonuslen) {
		data = restore_read(ra, P2ROUNDUP(drro->drr_bonuslen, 8), NULL);
		if (ra->err != 0)
			return (ra->err);
	}

	/*
	 * If we are losing blkptrs or changing the block size this must
	 * be a new file instance.  We must clear out the previous file
	 * contents before we can change this type of metadata in the dnode.
	 */
	if (err == 0) {
		int nblkptr;

		nblkptr = deduce_nblkptr(drro->drr_bonustype,
		    drro->drr_bonuslen);

		if (drro->drr_blksz != doi.doi_data_block_size ||
		    nblkptr < doi.doi_nblkptr) {
			err = dmu_free_long_range(os, drro->drr_object,
			    0, DMU_OBJECT_END);
			if (err != 0)
				return (SET_ERROR(EINVAL));
		}
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, object);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	if (object == DMU_NEW_OBJECT) {
		/* currently free, want to be allocated */
		err = dmu_object_claim(os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen, tx);
	} else if (drro->drr_type != doi.doi_type ||
	    drro->drr_blksz != doi.doi_data_block_size ||
	    drro->drr_bonustype != doi.doi_bonus_type ||
	    drro->drr_bonuslen != doi.doi_bonus_size) {
		/* currently allocated, but with different properties */
		err = dmu_object_reclaim(os, drro->drr_object,
		    drro->drr_type, drro->drr_blksz,
		    drro->drr_bonustype, drro->drr_bonuslen, tx);
	}
	if (err != 0) {
		dmu_tx_commit(tx);
		return (SET_ERROR(EINVAL));
	}

	dmu_object_set_checksum(os, drro->drr_object, drro->drr_checksumtype,
	    tx);
	dmu_object_set_compress(os, drro->drr_object, drro->drr_compress, tx);

	if (data != NULL) {
		dmu_buf_t *db;

		VERIFY(0 == dmu_bonus_hold(os, drro->drr_object, FTAG, &db));
		dmu_buf_will_dirty(db, tx);

		ASSERT3U(db->db_size, >=, drro->drr_bonuslen);
		bcopy(data, db->db_data, drro->drr_bonuslen);
		if (ra->byteswap) {
			dmu_object_byteswap_t byteswap =
			    DMU_OT_BYTESWAP(drro->drr_bonustype);
			dmu_ot_byteswap[byteswap].ob_func(db->db_data,
			    drro->drr_bonuslen);
		}
		dmu_buf_rele(db, FTAG);
	}
	dmu_tx_commit(tx);
	return (0);
}

/* ARGSUSED */
noinline static int
restore_freeobjects(struct restorearg *ra, objset_t *os,
    struct drr_freeobjects *drrfo)
{
	uint64_t obj;

	if (drrfo->drr_firstobj + drrfo->drr_numobjs < drrfo->drr_firstobj)
		return (SET_ERROR(EINVAL));

	for (obj = drrfo->drr_firstobj;
	    obj < drrfo->drr_firstobj + drrfo->drr_numobjs;
	    (void) dmu_object_next(os, &obj, FALSE, 0)) {
		int err;

		if (dmu_object_info(os, obj, NULL) != 0)
			continue;

		err = dmu_free_long_object(os, obj);
		if (err != 0)
			return (err);
	}
	return (0);
}

noinline static int
restore_write(struct restorearg *ra, objset_t *os,
    struct drr_write *drrw)
{
	dmu_tx_t *tx;
	dmu_buf_t *bonus;
	arc_buf_t *abuf;
	void *data;
	int err;

	if (drrw->drr_offset + drrw->drr_length < drrw->drr_offset ||
	    !DMU_OT_IS_VALID(drrw->drr_type))
		return (SET_ERROR(EINVAL));

	if (dmu_object_info(os, drrw->drr_object, NULL) != 0)
		return (SET_ERROR(EINVAL));

	if (dmu_bonus_hold(os, drrw->drr_object, FTAG, &bonus) != 0)
		return (SET_ERROR(EINVAL));

	abuf = dmu_request_arcbuf(bonus, drrw->drr_length);

	data = restore_read(ra, drrw->drr_length, abuf->b_data);
	if (data == NULL) {
		dmu_return_arcbuf(abuf);
		dmu_buf_rele(bonus, FTAG);
		return (ra->err);
	}

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, drrw->drr_object,
	    drrw->drr_offset, drrw->drr_length);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_return_arcbuf(abuf);
		dmu_buf_rele(bonus, FTAG);
		dmu_tx_abort(tx);
		return (err);
	}
	if (ra->byteswap) {
		dmu_object_byteswap_t byteswap =
		    DMU_OT_BYTESWAP(drrw->drr_type);
		dmu_ot_byteswap[byteswap].ob_func(data, drrw->drr_length);
	}
	dmu_assign_arcbuf(bonus, drrw->drr_offset, abuf, tx);
	dmu_tx_commit(tx);
	dmu_buf_rele(bonus, FTAG);
	return (0);
}

/*
 * Handle a DRR_WRITE_BYREF record.  This record is used in dedup'ed
 * streams to refer to a copy of the data that is already on the
 * system because it came in earlier in the stream.  This function
 * finds the earlier copy of the data, and uses that copy instead of
 * data from the stream to fulfill this write.
 */
static int
restore_write_byref(struct restorearg *ra, objset_t *os,
    struct drr_write_byref *drrwbr)
{
	dmu_tx_t *tx;
	int err;
	guid_map_entry_t gmesrch;
	guid_map_entry_t *gmep;
	avl_index_t where;
	objset_t *ref_os = NULL;
	dmu_buf_t *dbp;

	if (drrwbr->drr_offset + drrwbr->drr_length < drrwbr->drr_offset)
		return (SET_ERROR(EINVAL));

	/*
	 * If the GUID of the referenced dataset is different from the
	 * GUID of the target dataset, find the referenced dataset.
	 */
	if (drrwbr->drr_toguid != drrwbr->drr_refguid) {
		gmesrch.guid = drrwbr->drr_refguid;
		if ((gmep = avl_find(ra->guid_to_ds_map, &gmesrch,
		    &where)) == NULL) {
			return (SET_ERROR(EINVAL));
		}
		if (dmu_objset_from_ds(gmep->gme_ds, &ref_os))
			return (SET_ERROR(EINVAL));
	} else {
		ref_os = os;
	}

	err = dmu_buf_hold(ref_os, drrwbr->drr_refobject,
	    drrwbr->drr_refoffset, FTAG, &dbp, DMU_READ_PREFETCH);
	if (err != 0)
		return (err);

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, drrwbr->drr_object,
	    drrwbr->drr_offset, drrwbr->drr_length);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}
	dmu_write(os, drrwbr->drr_object,
	    drrwbr->drr_offset, drrwbr->drr_length, dbp->db_data, tx);
	dmu_buf_rele(dbp, FTAG);
	dmu_tx_commit(tx);
	return (0);
}

static int
restore_write_embedded(struct restorearg *ra, objset_t *os,
    struct drr_write_embedded *drrwnp)
{
	dmu_tx_t *tx;
	int err;
	void *data;

	if (drrwnp->drr_offset + drrwnp->drr_length < drrwnp->drr_offset)
		return (EINVAL);

	if (drrwnp->drr_psize > BPE_PAYLOAD_SIZE)
		return (EINVAL);

	if (drrwnp->drr_etype >= NUM_BP_EMBEDDED_TYPES)
		return (EINVAL);
	if (drrwnp->drr_compression >= ZIO_COMPRESS_FUNCTIONS)
		return (EINVAL);

	data = restore_read(ra, P2ROUNDUP(drrwnp->drr_psize, 8), NULL);
	if (data == NULL)
		return (ra->err);

	tx = dmu_tx_create(os);

	dmu_tx_hold_write(tx, drrwnp->drr_object,
	    drrwnp->drr_offset, drrwnp->drr_length);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	dmu_write_embedded(os, drrwnp->drr_object,
	    drrwnp->drr_offset, data, drrwnp->drr_etype,
	    drrwnp->drr_compression, drrwnp->drr_lsize, drrwnp->drr_psize,
	    ra->byteswap ^ ZFS_HOST_BYTEORDER, tx);

	dmu_tx_commit(tx);
	return (0);
}

static int
restore_spill(struct restorearg *ra, objset_t *os, struct drr_spill *drrs)
{
	dmu_tx_t *tx;
	void *data;
	dmu_buf_t *db, *db_spill;
	int err;

	if (drrs->drr_length < SPA_MINBLOCKSIZE ||
	    drrs->drr_length > spa_maxblocksize(dmu_objset_spa(os)))
		return (SET_ERROR(EINVAL));

	data = restore_read(ra, drrs->drr_length, NULL);
	if (data == NULL)
		return (ra->err);

	if (dmu_object_info(os, drrs->drr_object, NULL) != 0)
		return (SET_ERROR(EINVAL));

	VERIFY(0 == dmu_bonus_hold(os, drrs->drr_object, FTAG, &db));
	if ((err = dmu_spill_hold_by_bonus(db, FTAG, &db_spill)) != 0) {
		dmu_buf_rele(db, FTAG);
		return (err);
	}

	tx = dmu_tx_create(os);

	dmu_tx_hold_spill(tx, db->db_object);

	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err != 0) {
		dmu_buf_rele(db, FTAG);
		dmu_buf_rele(db_spill, FTAG);
		dmu_tx_abort(tx);
		return (err);
	}
	dmu_buf_will_dirty(db_spill, tx);

	if (db_spill->db_size < drrs->drr_length)
		VERIFY(0 == dbuf_spill_set_blksz(db_spill,
		    drrs->drr_length, tx));
	bcopy(data, db_spill->db_data, drrs->drr_length);

	dmu_buf_rele(db, FTAG);
	dmu_buf_rele(db_spill, FTAG);

	dmu_tx_commit(tx);
	return (0);
}

/* ARGSUSED */
noinline static int
restore_free(struct restorearg *ra, objset_t *os,
    struct drr_free *drrf)
{
	int err;

	if (drrf->drr_length != -1ULL &&
	    drrf->drr_offset + drrf->drr_length < drrf->drr_offset)
		return (SET_ERROR(EINVAL));

	if (dmu_object_info(os, drrf->drr_object, NULL) != 0)
		return (SET_ERROR(EINVAL));

	err = dmu_free_long_range(os, drrf->drr_object,
	    drrf->drr_offset, drrf->drr_length);
	return (err);
}

/* used to destroy the drc_ds on error */
static void
dmu_recv_cleanup_ds(dmu_recv_cookie_t *drc)
{
	char name[MAXNAMELEN];
	dsl_dataset_name(drc->drc_ds, name);
	dsl_dataset_disown(drc->drc_ds, dmu_recv_tag);
	(void) dsl_destroy_head(name);
}

/*
 * NB: callers *must* call dmu_recv_end() if this succeeds.
 */
int
dmu_recv_stream(dmu_recv_cookie_t *drc, vnode_t *vp, offset_t *voffp,
    int cleanup_fd, uint64_t *action_handlep)
{
	struct restorearg ra = { 0 };
	dmu_replay_record_t *drr;
	objset_t *os;
	zio_cksum_t pcksum;
	int featureflags;

	ra.byteswap = drc->drc_byteswap;
	ra.cksum = drc->drc_cksum;
	ra.vp = vp;
	ra.voff = *voffp;
	ra.bufsize = SPA_MAXBLOCKSIZE;
	ra.buf = vmem_alloc(ra.bufsize, KM_SLEEP);

	/* these were verified in dmu_recv_begin */
	ASSERT3U(DMU_GET_STREAM_HDRTYPE(drc->drc_drrb->drr_versioninfo), ==,
	    DMU_SUBSTREAM);
	ASSERT3U(drc->drc_drrb->drr_type, <, DMU_OST_NUMTYPES);

	/*
	 * Open the objset we are modifying.
	 */
	VERIFY0(dmu_objset_from_ds(drc->drc_ds, &os));

	ASSERT(dsl_dataset_phys(drc->drc_ds)->ds_flags & DS_FLAG_INCONSISTENT);

	featureflags = DMU_GET_FEATUREFLAGS(drc->drc_drrb->drr_versioninfo);

	/* if this stream is dedup'ed, set up the avl tree for guid mapping */
	if (featureflags & DMU_BACKUP_FEATURE_DEDUP) {
		minor_t minor;

		if (cleanup_fd == -1) {
			ra.err = SET_ERROR(EBADF);
			goto out;
		}
		ra.err = zfs_onexit_fd_hold(cleanup_fd, &minor);
		if (ra.err != 0) {
			cleanup_fd = -1;
			goto out;
		}

		if (*action_handlep == 0) {
			ra.guid_to_ds_map =
			    kmem_alloc(sizeof (avl_tree_t), KM_SLEEP);
			avl_create(ra.guid_to_ds_map, guid_compare,
			    sizeof (guid_map_entry_t),
			    offsetof(guid_map_entry_t, avlnode));
			ra.err = zfs_onexit_add_cb(minor,
			    free_guid_map_onexit, ra.guid_to_ds_map,
			    action_handlep);
			if (ra.err != 0)
				goto out;
		} else {
			ra.err = zfs_onexit_cb_data(minor, *action_handlep,
			    (void **)&ra.guid_to_ds_map);
			if (ra.err != 0)
				goto out;
		}

		drc->drc_guid_to_ds_map = ra.guid_to_ds_map;
	}

	/*
	 * Read records and process them.
	 */
	pcksum = ra.cksum;
	while (ra.err == 0 &&
	    NULL != (drr = restore_read(&ra, sizeof (*drr), NULL))) {
		if (issig(JUSTLOOKING) && issig(FORREAL)) {
			ra.err = SET_ERROR(EINTR);
			goto out;
		}

		if (ra.byteswap)
			backup_byteswap(drr);

		switch (drr->drr_type) {
		case DRR_OBJECT:
		{
			/*
			 * We need to make a copy of the record header,
			 * because restore_{object,write} may need to
			 * restore_read(), which will invalidate drr.
			 */
			struct drr_object drro = drr->drr_u.drr_object;
			ra.err = restore_object(&ra, os, &drro);
			break;
		}
		case DRR_FREEOBJECTS:
		{
			struct drr_freeobjects drrfo =
			    drr->drr_u.drr_freeobjects;
			ra.err = restore_freeobjects(&ra, os, &drrfo);
			break;
		}
		case DRR_WRITE:
		{
			struct drr_write drrw = drr->drr_u.drr_write;
			ra.err = restore_write(&ra, os, &drrw);
			break;
		}
		case DRR_WRITE_BYREF:
		{
			struct drr_write_byref drrwbr =
			    drr->drr_u.drr_write_byref;
			ra.err = restore_write_byref(&ra, os, &drrwbr);
			break;
		}
		case DRR_WRITE_EMBEDDED:
		{
			struct drr_write_embedded drrwe =
			    drr->drr_u.drr_write_embedded;
			ra.err = restore_write_embedded(&ra, os, &drrwe);
			break;
		}
		case DRR_FREE:
		{
			struct drr_free drrf = drr->drr_u.drr_free;
			ra.err = restore_free(&ra, os, &drrf);
			break;
		}
		case DRR_END:
		{
			struct drr_end drre = drr->drr_u.drr_end;
			/*
			 * We compare against the *previous* checksum
			 * value, because the stored checksum is of
			 * everything before the DRR_END record.
			 */
			if (!ZIO_CHECKSUM_EQUAL(drre.drr_checksum, pcksum))
				ra.err = SET_ERROR(ECKSUM);
			goto out;
		}
		case DRR_SPILL:
		{
			struct drr_spill drrs = drr->drr_u.drr_spill;
			ra.err = restore_spill(&ra, os, &drrs);
			break;
		}
		default:
			ra.err = SET_ERROR(EINVAL);
			goto out;
		}
		pcksum = ra.cksum;
	}
	ASSERT(ra.err != 0);

out:
	if ((featureflags & DMU_BACKUP_FEATURE_DEDUP) && (cleanup_fd != -1))
		zfs_onexit_fd_rele(cleanup_fd);

	if (ra.err != 0) {
		/*
		 * destroy what we created, so we don't leave it in the
		 * inconsistent restoring state.
		 */
		dmu_recv_cleanup_ds(drc);
	}

	vmem_free(ra.buf, ra.bufsize);
	*voffp = ra.voff;
	return (ra.err);
}

static int
dmu_recv_end_check(void *arg, dmu_tx_t *tx)
{
	dmu_recv_cookie_t *drc = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);
	int error;

	ASSERT3P(drc->drc_ds->ds_owner, ==, dmu_recv_tag);

	if (!drc->drc_newfs) {
		dsl_dataset_t *origin_head;

		error = dsl_dataset_hold(dp, drc->drc_tofs, FTAG, &origin_head);
		if (error != 0)
			return (error);
		if (drc->drc_force) {
			/*
			 * We will destroy any snapshots in tofs (i.e. before
			 * origin_head) that are after the origin (which is
			 * the snap before drc_ds, because drc_ds can not
			 * have any snaps of its own).
			 */
			uint64_t obj;

			obj = dsl_dataset_phys(origin_head)->ds_prev_snap_obj;
			while (obj !=
			    dsl_dataset_phys(drc->drc_ds)->ds_prev_snap_obj) {
				dsl_dataset_t *snap;
				error = dsl_dataset_hold_obj(dp, obj, FTAG,
				    &snap);
				if (error != 0)
					break;
				if (snap->ds_dir != origin_head->ds_dir)
					error = SET_ERROR(EINVAL);
				if (error == 0)  {
					error = dsl_destroy_snapshot_check_impl(
					    snap, B_FALSE);
				}
				obj = dsl_dataset_phys(snap)->ds_prev_snap_obj;
				dsl_dataset_rele(snap, FTAG);
				if (error != 0)
					break;
			}
			if (error != 0) {
				dsl_dataset_rele(origin_head, FTAG);
				return (error);
			}
		}
		error = dsl_dataset_clone_swap_check_impl(drc->drc_ds,
		    origin_head, drc->drc_force, drc->drc_owner, tx);
		if (error != 0) {
			dsl_dataset_rele(origin_head, FTAG);
			return (error);
		}
		error = dsl_dataset_snapshot_check_impl(origin_head,
		    drc->drc_tosnap, tx, B_TRUE, 1, drc->drc_cred);
		dsl_dataset_rele(origin_head, FTAG);
		if (error != 0)
			return (error);

		error = dsl_destroy_head_check_impl(drc->drc_ds, 1);
	} else {
		error = dsl_dataset_snapshot_check_impl(drc->drc_ds,
		    drc->drc_tosnap, tx, B_TRUE, 1, drc->drc_cred);
	}
	return (error);
}

static void
dmu_recv_end_sync(void *arg, dmu_tx_t *tx)
{
	dmu_recv_cookie_t *drc = arg;
	dsl_pool_t *dp = dmu_tx_pool(tx);

	spa_history_log_internal_ds(drc->drc_ds, "finish receiving",
	    tx, "snap=%s", drc->drc_tosnap);

	if (!drc->drc_newfs) {
		dsl_dataset_t *origin_head;

		VERIFY0(dsl_dataset_hold(dp, drc->drc_tofs, FTAG,
		    &origin_head));

		if (drc->drc_force) {
			/*
			 * Destroy any snapshots of drc_tofs (origin_head)
			 * after the origin (the snap before drc_ds).
			 */
			uint64_t obj;

			obj = dsl_dataset_phys(origin_head)->ds_prev_snap_obj;
			while (obj !=
			    dsl_dataset_phys(drc->drc_ds)->ds_prev_snap_obj) {
				dsl_dataset_t *snap;
				VERIFY0(dsl_dataset_hold_obj(dp, obj, FTAG,
				    &snap));
				ASSERT3P(snap->ds_dir, ==, origin_head->ds_dir);
				obj = dsl_dataset_phys(snap)->ds_prev_snap_obj;
				dsl_destroy_snapshot_sync_impl(snap,
				    B_FALSE, tx);
				dsl_dataset_rele(snap, FTAG);
			}
		}
		VERIFY3P(drc->drc_ds->ds_prev, ==,
		    origin_head->ds_prev);

		dsl_dataset_clone_swap_sync_impl(drc->drc_ds,
		    origin_head, tx);
		dsl_dataset_snapshot_sync_impl(origin_head,
		    drc->drc_tosnap, tx);

		/* set snapshot's creation time and guid */
		dmu_buf_will_dirty(origin_head->ds_prev->ds_dbuf, tx);
		dsl_dataset_phys(origin_head->ds_prev)->ds_creation_time =
		    drc->drc_drrb->drr_creation_time;
		dsl_dataset_phys(origin_head->ds_prev)->ds_guid =
		    drc->drc_drrb->drr_toguid;
		dsl_dataset_phys(origin_head->ds_prev)->ds_flags &=
		    ~DS_FLAG_INCONSISTENT;

		dmu_buf_will_dirty(origin_head->ds_dbuf, tx);
		dsl_dataset_phys(origin_head)->ds_flags &=
		    ~DS_FLAG_INCONSISTENT;

		dsl_dataset_rele(origin_head, FTAG);
		dsl_destroy_head_sync_impl(drc->drc_ds, tx);

		if (drc->drc_owner != NULL)
			VERIFY3P(origin_head->ds_owner, ==, drc->drc_owner);
	} else {
		dsl_dataset_t *ds = drc->drc_ds;

		dsl_dataset_snapshot_sync_impl(ds, drc->drc_tosnap, tx);

		/* set snapshot's creation time and guid */
		dmu_buf_will_dirty(ds->ds_prev->ds_dbuf, tx);
		dsl_dataset_phys(ds->ds_prev)->ds_creation_time =
		    drc->drc_drrb->drr_creation_time;
		dsl_dataset_phys(ds->ds_prev)->ds_guid =
		    drc->drc_drrb->drr_toguid;
		dsl_dataset_phys(ds->ds_prev)->ds_flags &=
		    ~DS_FLAG_INCONSISTENT;

		dmu_buf_will_dirty(ds->ds_dbuf, tx);
		dsl_dataset_phys(ds)->ds_flags &= ~DS_FLAG_INCONSISTENT;
	}
	drc->drc_newsnapobj = dsl_dataset_phys(drc->drc_ds)->ds_prev_snap_obj;
	zvol_create_minors(dp->dp_spa, drc->drc_tofs, B_TRUE);
	/*
	 * Release the hold from dmu_recv_begin.  This must be done before
	 * we return to open context, so that when we free the dataset's dnode,
	 * we can evict its bonus buffer.
	 */
	dsl_dataset_disown(drc->drc_ds, dmu_recv_tag);
	drc->drc_ds = NULL;
}

static int
add_ds_to_guidmap(const char *name, avl_tree_t *guid_map, uint64_t snapobj)
{
	dsl_pool_t *dp;
	dsl_dataset_t *snapds;
	guid_map_entry_t *gmep;
	int err;

	ASSERT(guid_map != NULL);

	err = dsl_pool_hold(name, FTAG, &dp);
	if (err != 0)
		return (err);
	gmep = kmem_alloc(sizeof (*gmep), KM_SLEEP);
	err = dsl_dataset_hold_obj(dp, snapobj, gmep, &snapds);
	if (err == 0) {
		gmep->guid = dsl_dataset_phys(snapds)->ds_guid;
		gmep->gme_ds = snapds;
		avl_add(guid_map, gmep);
		dsl_dataset_long_hold(snapds, gmep);
	} else {
		kmem_free(gmep, sizeof (*gmep));
	}

	dsl_pool_rele(dp, FTAG);
	return (err);
}

static int dmu_recv_end_modified_blocks = 3;

static int
dmu_recv_existing_end(dmu_recv_cookie_t *drc)
{
	int error;

#ifdef _KERNEL
	char *name;

	/*
	 * We will be destroying the ds; make sure its origin is unmounted if
	 * necessary.
	 */
	name = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	dsl_dataset_name(drc->drc_ds, name);
	zfs_destroy_unmount_origin(name);
	kmem_free(name, MAXNAMELEN);
#endif

	error = dsl_sync_task(drc->drc_tofs,
	    dmu_recv_end_check, dmu_recv_end_sync, drc,
	    dmu_recv_end_modified_blocks, ZFS_SPACE_CHECK_NORMAL);

	if (error != 0)
		dmu_recv_cleanup_ds(drc);
	return (error);
}

static int
dmu_recv_new_end(dmu_recv_cookie_t *drc)
{
	int error;

	error = dsl_sync_task(drc->drc_tofs,
	    dmu_recv_end_check, dmu_recv_end_sync, drc,
	    dmu_recv_end_modified_blocks, ZFS_SPACE_CHECK_NORMAL);

	if (error != 0) {
		dmu_recv_cleanup_ds(drc);
	} else if (drc->drc_guid_to_ds_map != NULL) {
		(void) add_ds_to_guidmap(drc->drc_tofs,
		    drc->drc_guid_to_ds_map,
		    drc->drc_newsnapobj);
	}
	return (error);
}

int
dmu_recv_end(dmu_recv_cookie_t *drc, void *owner)
{
	drc->drc_owner = owner;

	if (drc->drc_newfs)
		return (dmu_recv_new_end(drc));
	else
		return (dmu_recv_existing_end(drc));
}

/*
 * Return TRUE if this objset is currently being received into.
 */
boolean_t
dmu_objset_is_receiving(objset_t *os)
{
	return (os->os_dsl_dataset != NULL &&
	    os->os_dsl_dataset->ds_owner == dmu_recv_tag);
}

#if defined(_KERNEL)
module_param(zfs_send_corrupt_data, int, 0644);
MODULE_PARM_DESC(zfs_send_corrupt_data, "Allow sending corrupt data");
#endif
