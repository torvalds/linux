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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>

/*
 * Virtual device vector for mirroring.
 */

typedef struct mirror_child {
	vdev_t		*mc_vd;
	uint64_t	mc_offset;
	int		mc_error;
	int		mc_pending;
	uint8_t		mc_tried;
	uint8_t		mc_skipped;
	uint8_t		mc_speculative;
} mirror_child_t;

typedef struct mirror_map {
	int		mm_children;
	int		mm_replacing;
	int		mm_preferred;
	int		mm_root;
	mirror_child_t	mm_child[1];
} mirror_map_t;

/*
 * When the children are equally busy queue incoming requests to a single
 * child for N microseconds.  This is done to maximize the likelihood that
 * the Linux elevator will be able to merge requests while it is plugged.
 * Otherwise, requests are queued to the least busy device.
 *
 * For rotational disks the Linux elevator will plug for 10ms which is
 * why zfs_vdev_mirror_switch_us is set to 10ms by default.  For non-
 * rotational disks the elevator will not plug, but 10ms is still a small
 * enough value that the requests will get spread over all the children.
 *
 * For fast SSDs it may make sense to decrease zfs_vdev_mirror_switch_us
 * significantly to bound the worst case latencies.  It would probably be
 * ideal to calculate a decaying average of the last observed latencies and
 * use that to dynamically adjust the zfs_vdev_mirror_switch_us time.
 */
int zfs_vdev_mirror_switch_us = 10000;

static void
vdev_mirror_map_free(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;

	kmem_free(mm, offsetof(mirror_map_t, mm_child[mm->mm_children]));
}

static const zio_vsd_ops_t vdev_mirror_vsd_ops = {
	vdev_mirror_map_free,
	zio_vsd_default_cksum_report
};

static int
vdev_mirror_pending(vdev_t *vd)
{
	return (avl_numnodes(&vd->vdev_queue.vq_active_tree));
}

/*
 * Avoid inlining the function to keep vdev_mirror_io_start(), which
 * is this functions only caller, as small as possible on the stack.
 */
noinline static mirror_map_t *
vdev_mirror_map_alloc(zio_t *zio)
{
	mirror_map_t *mm = NULL;
	mirror_child_t *mc;
	vdev_t *vd = zio->io_vd;
	int c, d;

	if (vd == NULL) {
		dva_t *dva = zio->io_bp->blk_dva;
		spa_t *spa = zio->io_spa;

		c = BP_GET_NDVAS(zio->io_bp);

		mm = kmem_zalloc(offsetof(mirror_map_t, mm_child[c]),
		    KM_SLEEP);
		mm->mm_children = c;
		mm->mm_replacing = B_FALSE;
		mm->mm_preferred = spa_get_random(c);
		mm->mm_root = B_TRUE;

		/*
		 * Check the other, lower-index DVAs to see if they're on
		 * the same vdev as the child we picked.  If they are, use
		 * them since they are likely to have been allocated from
		 * the primary metaslab in use at the time, and hence are
		 * more likely to have locality with single-copy data.
		 */
		for (c = mm->mm_preferred, d = c - 1; d >= 0; d--) {
			if (DVA_GET_VDEV(&dva[d]) == DVA_GET_VDEV(&dva[c]))
				mm->mm_preferred = d;
		}

		for (c = 0; c < mm->mm_children; c++) {
			mc = &mm->mm_child[c];

			mc->mc_vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[c]));
			mc->mc_offset = DVA_GET_OFFSET(&dva[c]);
		}
	} else {
		int lowest_pending = INT_MAX;
		int lowest_nr = 1;

		c = vd->vdev_children;

		mm = kmem_zalloc(offsetof(mirror_map_t, mm_child[c]),
		    KM_SLEEP);
		mm->mm_children = c;
		mm->mm_replacing = (vd->vdev_ops == &vdev_replacing_ops ||
		    vd->vdev_ops == &vdev_spare_ops);
		mm->mm_preferred = 0;
		mm->mm_root = B_FALSE;

		for (c = 0; c < mm->mm_children; c++) {
			mc = &mm->mm_child[c];
			mc->mc_vd = vd->vdev_child[c];
			mc->mc_offset = zio->io_offset;

			if (mm->mm_replacing)
				continue;

			if (!vdev_readable(mc->mc_vd)) {
				mc->mc_error = SET_ERROR(ENXIO);
				mc->mc_tried = 1;
				mc->mc_skipped = 1;
				mc->mc_pending = INT_MAX;
				continue;
			}

			mc->mc_pending = vdev_mirror_pending(mc->mc_vd);
			if (mc->mc_pending < lowest_pending) {
				lowest_pending = mc->mc_pending;
				lowest_nr = 1;
			} else if (mc->mc_pending == lowest_pending) {
				lowest_nr++;
			}
		}

		d = gethrtime() / (NSEC_PER_USEC * zfs_vdev_mirror_switch_us);
		d = (d % lowest_nr) + 1;

		for (c = 0; c < mm->mm_children; c++) {
			mc = &mm->mm_child[c];

			if (mm->mm_child[c].mc_pending == lowest_pending) {
				if (--d == 0) {
					mm->mm_preferred = c;
					break;
				}
			}
		}
	}

	zio->io_vsd = mm;
	zio->io_vsd_ops = &vdev_mirror_vsd_ops;
	return (mm);
}

static int
vdev_mirror_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *ashift)
{
	int numerrors = 0;
	int lasterror = 0;
	int c;

	if (vd->vdev_children == 0) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	vdev_open_children(vd);

	for (c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error) {
			lasterror = cvd->vdev_open_error;
			numerrors++;
			continue;
		}

		*asize = MIN(*asize - 1, cvd->vdev_asize - 1) + 1;
		*max_asize = MIN(*max_asize - 1, cvd->vdev_max_asize - 1) + 1;
		*ashift = MAX(*ashift, cvd->vdev_ashift);
	}

	if (numerrors == vd->vdev_children) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	return (0);
}

static void
vdev_mirror_close(vdev_t *vd)
{
	int c;

	for (c = 0; c < vd->vdev_children; c++)
		vdev_close(vd->vdev_child[c]);
}

static void
vdev_mirror_child_done(zio_t *zio)
{
	mirror_child_t *mc = zio->io_private;

	mc->mc_error = zio->io_error;
	mc->mc_tried = 1;
	mc->mc_skipped = 0;
}

static void
vdev_mirror_scrub_done(zio_t *zio)
{
	mirror_child_t *mc = zio->io_private;

	if (zio->io_error == 0) {
		zio_t *pio;

		mutex_enter(&zio->io_lock);
		while ((pio = zio_walk_parents(zio)) != NULL) {
			mutex_enter(&pio->io_lock);
			ASSERT3U(zio->io_size, >=, pio->io_size);
			bcopy(zio->io_data, pio->io_data, pio->io_size);
			mutex_exit(&pio->io_lock);
		}
		mutex_exit(&zio->io_lock);
	}

	zio_buf_free(zio->io_data, zio->io_size);

	mc->mc_error = zio->io_error;
	mc->mc_tried = 1;
	mc->mc_skipped = 0;
}

/*
 * Try to find a child whose DTL doesn't contain the block we want to read.
 * If we can't, try the read on any vdev we haven't already tried.
 */
static int
vdev_mirror_child_select(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;
	mirror_child_t *mc;
	uint64_t txg = zio->io_txg;
	int i, c;

	ASSERT(zio->io_bp == NULL || BP_PHYSICAL_BIRTH(zio->io_bp) == txg);

	/*
	 * Try to find a child whose DTL doesn't contain the block to read.
	 * If a child is known to be completely inaccessible (indicated by
	 * vdev_readable() returning B_FALSE), don't even try.
	 */
	for (i = 0, c = mm->mm_preferred; i < mm->mm_children; i++, c++) {
		if (c >= mm->mm_children)
			c = 0;
		mc = &mm->mm_child[c];
		if (mc->mc_tried || mc->mc_skipped)
			continue;
		if (mc->mc_vd == NULL || !vdev_readable(mc->mc_vd)) {
			mc->mc_error = SET_ERROR(ENXIO);
			mc->mc_tried = 1;	/* don't even try */
			mc->mc_skipped = 1;
			continue;
		}
		if (!vdev_dtl_contains(mc->mc_vd, DTL_MISSING, txg, 1))
			return (c);
		mc->mc_error = SET_ERROR(ESTALE);
		mc->mc_skipped = 1;
		mc->mc_speculative = 1;
	}

	/*
	 * Every device is either missing or has this txg in its DTL.
	 * Look for any child we haven't already tried before giving up.
	 */
	for (c = 0; c < mm->mm_children; c++)
		if (!mm->mm_child[c].mc_tried)
			return (c);

	/*
	 * Every child failed.  There's no place left to look.
	 */
	return (-1);
}

static void
vdev_mirror_io_start(zio_t *zio)
{
	mirror_map_t *mm;
	mirror_child_t *mc;
	int c, children;

	mm = vdev_mirror_map_alloc(zio);

	if (zio->io_type == ZIO_TYPE_READ) {
		if ((zio->io_flags & ZIO_FLAG_SCRUB) && !mm->mm_replacing) {
			/*
			 * For scrubbing reads we need to allocate a read
			 * buffer for each child and issue reads to all
			 * children.  If any child succeeds, it will copy its
			 * data into zio->io_data in vdev_mirror_scrub_done.
			 */
			for (c = 0; c < mm->mm_children; c++) {
				mc = &mm->mm_child[c];
				zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
				    mc->mc_vd, mc->mc_offset,
				    zio_buf_alloc(zio->io_size), zio->io_size,
				    zio->io_type, zio->io_priority, 0,
				    vdev_mirror_scrub_done, mc));
			}
			zio_execute(zio);
			return;
		}
		/*
		 * For normal reads just pick one child.
		 */
		c = vdev_mirror_child_select(zio);
		children = (c >= 0);
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_WRITE);

		/*
		 * Writes go to all children.
		 */
		c = 0;
		children = mm->mm_children;
	}

	while (children--) {
		mc = &mm->mm_child[c];
		zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
		    mc->mc_vd, mc->mc_offset, zio->io_data, zio->io_size,
		    zio->io_type, zio->io_priority, 0,
		    vdev_mirror_child_done, mc));
		c++;
	}

	zio_execute(zio);
}

static int
vdev_mirror_worst_error(mirror_map_t *mm)
{
	int c, error[2] = { 0, 0 };

	for (c = 0; c < mm->mm_children; c++) {
		mirror_child_t *mc = &mm->mm_child[c];
		int s = mc->mc_speculative;
		error[s] = zio_worst_error(error[s], mc->mc_error);
	}

	return (error[0] ? error[0] : error[1]);
}

static void
vdev_mirror_io_done(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;
	mirror_child_t *mc;
	int c;
	int good_copies = 0;
	int unexpected_errors = 0;

	for (c = 0; c < mm->mm_children; c++) {
		mc = &mm->mm_child[c];

		if (mc->mc_error) {
			if (!mc->mc_skipped)
				unexpected_errors++;
		} else if (mc->mc_tried) {
			good_copies++;
		}
	}

	if (zio->io_type == ZIO_TYPE_WRITE) {
		/*
		 * XXX -- for now, treat partial writes as success.
		 *
		 * Now that we support write reallocation, it would be better
		 * to treat partial failure as real failure unless there are
		 * no non-degraded top-level vdevs left, and not update DTLs
		 * if we intend to reallocate.
		 */
		/* XXPOLICY */
		if (good_copies != mm->mm_children) {
			/*
			 * Always require at least one good copy.
			 *
			 * For ditto blocks (io_vd == NULL), require
			 * all copies to be good.
			 *
			 * XXX -- for replacing vdevs, there's no great answer.
			 * If the old device is really dead, we may not even
			 * be able to access it -- so we only want to
			 * require good writes to the new device.  But if
			 * the new device turns out to be flaky, we want
			 * to be able to detach it -- which requires all
			 * writes to the old device to have succeeded.
			 */
			if (good_copies == 0 || zio->io_vd == NULL)
				zio->io_error = vdev_mirror_worst_error(mm);
		}
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ);

	/*
	 * If we don't have a good copy yet, keep trying other children.
	 */
	/* XXPOLICY */
	if (good_copies == 0 && (c = vdev_mirror_child_select(zio)) != -1) {
		ASSERT(c >= 0 && c < mm->mm_children);
		mc = &mm->mm_child[c];
		zio_vdev_io_redone(zio);
		zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
		    mc->mc_vd, mc->mc_offset, zio->io_data, zio->io_size,
		    ZIO_TYPE_READ, zio->io_priority, 0,
		    vdev_mirror_child_done, mc));
		return;
	}

	/* XXPOLICY */
	if (good_copies == 0) {
		zio->io_error = vdev_mirror_worst_error(mm);
		ASSERT(zio->io_error != 0);
	}

	if (good_copies && spa_writeable(zio->io_spa) &&
	    (unexpected_errors ||
	    (zio->io_flags & ZIO_FLAG_RESILVER) ||
	    ((zio->io_flags & ZIO_FLAG_SCRUB) && mm->mm_replacing))) {
		/*
		 * Use the good data we have in hand to repair damaged children.
		 */
		for (c = 0; c < mm->mm_children; c++) {
			/*
			 * Don't rewrite known good children.
			 * Not only is it unnecessary, it could
			 * actually be harmful: if the system lost
			 * power while rewriting the only good copy,
			 * there would be no good copies left!
			 */
			mc = &mm->mm_child[c];

			if (mc->mc_error == 0) {
				if (mc->mc_tried)
					continue;
				if (!(zio->io_flags & ZIO_FLAG_SCRUB) &&
				    !vdev_dtl_contains(mc->mc_vd, DTL_PARTIAL,
				    zio->io_txg, 1))
					continue;
				mc->mc_error = SET_ERROR(ESTALE);
			}

			zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
			    mc->mc_vd, mc->mc_offset,
			    zio->io_data, zio->io_size,
			    ZIO_TYPE_WRITE, ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_IO_REPAIR | (unexpected_errors ?
			    ZIO_FLAG_SELF_HEAL : 0), NULL, NULL));
		}
	}
}

static void
vdev_mirror_state_change(vdev_t *vd, int faulted, int degraded)
{
	if (faulted == vd->vdev_children)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	else if (degraded + faulted != 0)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	else
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
}

vdev_ops_t vdev_mirror_ops = {
	vdev_mirror_open,
	vdev_mirror_close,
	vdev_default_asize,
	vdev_mirror_io_start,
	vdev_mirror_io_done,
	vdev_mirror_state_change,
	NULL,
	NULL,
	VDEV_TYPE_MIRROR,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};

vdev_ops_t vdev_replacing_ops = {
	vdev_mirror_open,
	vdev_mirror_close,
	vdev_default_asize,
	vdev_mirror_io_start,
	vdev_mirror_io_done,
	vdev_mirror_state_change,
	NULL,
	NULL,
	VDEV_TYPE_REPLACING,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};

vdev_ops_t vdev_spare_ops = {
	vdev_mirror_open,
	vdev_mirror_close,
	vdev_default_asize,
	vdev_mirror_io_start,
	vdev_mirror_io_done,
	vdev_mirror_state_change,
	NULL,
	NULL,
	VDEV_TYPE_SPARE,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(zfs_vdev_mirror_switch_us, int, 0644);
MODULE_PARM_DESC(zfs_vdev_mirror_switch_us, "Switch mirrors every N usecs");
#endif
