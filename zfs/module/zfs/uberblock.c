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
 * Copyright (c) 2013, 2014 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/uberblock_impl.h>
#include <sys/vdev_impl.h>

int
uberblock_verify(uberblock_t *ub)
{
	if (ub->ub_magic == BSWAP_64((uint64_t)UBERBLOCK_MAGIC))
		byteswap_uint64_array(ub, sizeof (uberblock_t));

	if (ub->ub_magic != UBERBLOCK_MAGIC)
		return (SET_ERROR(EINVAL));

	return (0);
}

/*
 * Update the uberblock and return TRUE if anything changed in this
 * transaction group.
 */
boolean_t
uberblock_update(uberblock_t *ub, vdev_t *rvd, uint64_t txg)
{
	ASSERT(ub->ub_txg < txg);

	/*
	 * We explicitly do not set ub_version here, so that older versions
	 * continue to be written with the previous uberblock version.
	 */
	ub->ub_magic = UBERBLOCK_MAGIC;
	ub->ub_txg = txg;
	ub->ub_guid_sum = rvd->vdev_guid_sum;
	ub->ub_timestamp = gethrestime_sec();
	ub->ub_software_version = SPA_VERSION;

	return (ub->ub_rootbp.blk_birth == txg);
}
