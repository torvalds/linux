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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
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
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>

struct diffarg {
	struct vnode *da_vp;		/* file to which we are reporting */
	offset_t *da_offp;
	int da_err;			/* error that stopped diff search */
	dmu_diff_record_t da_ddr;
};

static int
write_record(struct diffarg *da)
{
	ssize_t resid; /* have to get resid to get detailed errno */

	if (da->da_ddr.ddr_type == DDR_NONE) {
		da->da_err = 0;
		return (0);
	}

	da->da_err = vn_rdwr(UIO_WRITE, da->da_vp, (caddr_t)&da->da_ddr,
	    sizeof (da->da_ddr), 0, UIO_SYSSPACE, FAPPEND,
	    RLIM64_INFINITY, CRED(), &resid);
	*da->da_offp += sizeof (da->da_ddr);
	return (da->da_err);
}

static int
report_free_dnode_range(struct diffarg *da, uint64_t first, uint64_t last)
{
	ASSERT(first <= last);
	if (da->da_ddr.ddr_type != DDR_FREE ||
	    first != da->da_ddr.ddr_last + 1) {
		if (write_record(da) != 0)
			return (da->da_err);
		da->da_ddr.ddr_type = DDR_FREE;
		da->da_ddr.ddr_first = first;
		da->da_ddr.ddr_last = last;
		return (0);
	}
	da->da_ddr.ddr_last = last;
	return (0);
}

static int
report_dnode(struct diffarg *da, uint64_t object, dnode_phys_t *dnp)
{
	ASSERT(dnp != NULL);
	if (dnp->dn_type == DMU_OT_NONE)
		return (report_free_dnode_range(da, object, object));

	if (da->da_ddr.ddr_type != DDR_INUSE ||
	    object != da->da_ddr.ddr_last + 1) {
		if (write_record(da) != 0)
			return (da->da_err);
		da->da_ddr.ddr_type = DDR_INUSE;
		da->da_ddr.ddr_first = da->da_ddr.ddr_last = object;
		return (0);
	}
	da->da_ddr.ddr_last = object;
	return (0);
}

#define	DBP_SPAN(dnp, level)				  \
	(((uint64_t)dnp->dn_datablkszsec) << (SPA_MINBLOCKSHIFT + \
	(level) * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT)))

/* ARGSUSED */
static int
diff_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	struct diffarg *da = arg;
	int err = 0;

	if (issig(JUSTLOOKING) && issig(FORREAL))
		return (SET_ERROR(EINTR));

	if (bp == NULL || zb->zb_object != DMU_META_DNODE_OBJECT)
		return (0);

	if (BP_IS_HOLE(bp)) {
		uint64_t span = DBP_SPAN(dnp, zb->zb_level);
		uint64_t dnobj = (zb->zb_blkid * span) >> DNODE_SHIFT;

		err = report_free_dnode_range(da, dnobj,
		    dnobj + (span >> DNODE_SHIFT) - 1);
		if (err)
			return (err);
	} else if (zb->zb_level == 0) {
		dnode_phys_t *blk;
		arc_buf_t *abuf;
		arc_flags_t aflags = ARC_FLAG_WAIT;
		int blksz = BP_GET_LSIZE(bp);
		int i;

		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL,
		    &aflags, zb) != 0)
			return (SET_ERROR(EIO));

		blk = abuf->b_data;
		for (i = 0; i < blksz >> DNODE_SHIFT; i++) {
			uint64_t dnobj = (zb->zb_blkid <<
			    (DNODE_BLOCK_SHIFT - DNODE_SHIFT)) + i;
			err = report_dnode(da, dnobj, blk+i);
			if (err)
				break;
		}
		arc_buf_destroy(abuf, &abuf);
		if (err)
			return (err);
		/* Don't care about the data blocks */
		return (TRAVERSE_VISIT_NO_CHILDREN);
	}
	return (0);
}

int
dmu_diff(const char *tosnap_name, const char *fromsnap_name,
    struct vnode *vp, offset_t *offp)
{
	struct diffarg da;
	dsl_dataset_t *fromsnap;
	dsl_dataset_t *tosnap;
	dsl_pool_t *dp;
	int error;
	uint64_t fromtxg;

	if (strchr(tosnap_name, '@') == NULL ||
	    strchr(fromsnap_name, '@') == NULL)
		return (SET_ERROR(EINVAL));

	error = dsl_pool_hold(tosnap_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, tosnap_name, FTAG, &tosnap);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	error = dsl_dataset_hold(dp, fromsnap_name, FTAG, &fromsnap);
	if (error != 0) {
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	if (!dsl_dataset_is_before(tosnap, fromsnap, 0)) {
		dsl_dataset_rele(fromsnap, FTAG);
		dsl_dataset_rele(tosnap, FTAG);
		dsl_pool_rele(dp, FTAG);
		return (SET_ERROR(EXDEV));
	}

	fromtxg = dsl_dataset_phys(fromsnap)->ds_creation_txg;
	dsl_dataset_rele(fromsnap, FTAG);

	dsl_dataset_long_hold(tosnap, FTAG);
	dsl_pool_rele(dp, FTAG);

	da.da_vp = vp;
	da.da_offp = offp;
	da.da_ddr.ddr_type = DDR_NONE;
	da.da_ddr.ddr_first = da.da_ddr.ddr_last = 0;
	da.da_err = 0;

	error = traverse_dataset(tosnap, fromtxg,
	    TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA, diff_cb, &da);

	if (error != 0) {
		da.da_err = error;
	} else {
		/* we set the da.da_err we return as side-effect */
		(void) write_record(&da);
	}

	dsl_dataset_long_rele(tosnap, FTAG);
	dsl_dataset_rele(tosnap, FTAG);

	return (da.da_err);
}
