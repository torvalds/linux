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
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>

/*
 * Virtual device vector for the pool's root vdev.
 */

static uint64_t
vdev_root_core_tvds(vdev_t *vd)
{
	uint64_t tvds = 0;

	for (uint64_t c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (!cvd->vdev_ishole && !cvd->vdev_islog &&
		    cvd->vdev_ops != &vdev_indirect_ops) {
			tvds++;
		}
	}

	return (tvds);
}

/*
 * We should be able to tolerate one failure with absolutely no damage
 * to our metadata.  Two failures will take out space maps, a bunch of
 * indirect block trees, meta dnodes, dnodes, etc.  Probably not a happy
 * place to live.  When we get smarter, we can liberalize this policy.
 * e.g. If we haven't lost two consecutive top-level vdevs, then we are
 * probably fine.  Adding bean counters during alloc/free can make this
 * future guesswork more accurate.
 */
static boolean_t
too_many_errors(vdev_t *vd, uint64_t numerrors)
{
	uint64_t tvds;

	if (numerrors == 0)
		return (B_FALSE);

	tvds = vdev_root_core_tvds(vd);
	ASSERT3U(numerrors, <=, tvds);

	if (numerrors == tvds)
		return (B_TRUE);

	return (numerrors > spa_missing_tvds_allowed(vd->vdev_spa));
}

static int
vdev_root_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	spa_t *spa = vd->vdev_spa;
	int lasterror = 0;
	int numerrors = 0;

	if (vd->vdev_children == 0) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	vdev_open_children(vd);

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error && !cvd->vdev_islog) {
			lasterror = cvd->vdev_open_error;
			numerrors++;
		}
	}

	if (spa_load_state(spa) != SPA_LOAD_NONE)
		spa_set_missing_tvds(spa, numerrors);

	if (too_many_errors(vd, numerrors)) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	*asize = 0;
	*max_asize = 0;
	*logical_ashift = 0;
	*physical_ashift = 0;

	return (0);
}

static void
vdev_root_close(vdev_t *vd)
{
	for (int c = 0; c < vd->vdev_children; c++)
		vdev_close(vd->vdev_child[c]);
}

static void
vdev_root_state_change(vdev_t *vd, int faulted, int degraded)
{
	if (too_many_errors(vd, faulted)) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	} else if (degraded || faulted) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	} else {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
	}
}

vdev_ops_t vdev_root_ops = {
	vdev_root_open,
	vdev_root_close,
	vdev_default_asize,
	NULL,			/* io_start - not applicable to the root */
	NULL,			/* io_done - not applicable to the root */
	vdev_root_state_change,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	VDEV_TYPE_ROOT,		/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};
