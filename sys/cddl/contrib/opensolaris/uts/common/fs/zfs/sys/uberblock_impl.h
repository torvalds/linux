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
 * Copyright (c) 2016, 2017 by Delphix. All rights reserved.
 */

#ifndef _SYS_UBERBLOCK_IMPL_H
#define	_SYS_UBERBLOCK_IMPL_H

#include <sys/uberblock.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The uberblock version is incremented whenever an incompatible on-disk
 * format change is made to the SPA, DMU, or ZAP.
 *
 * Note: the first two fields should never be moved.  When a storage pool
 * is opened, the uberblock must be read off the disk before the version
 * can be checked.  If the ub_version field is moved, we may not detect
 * version mismatch.  If the ub_magic field is moved, applications that
 * expect the magic number in the first word won't work.
 */
#define	UBERBLOCK_MAGIC		0x00bab10c		/* oo-ba-bloc!	*/
#define	UBERBLOCK_SHIFT		10			/* up to 1K	*/

struct uberblock {
	uint64_t	ub_magic;	/* UBERBLOCK_MAGIC		*/
	uint64_t	ub_version;	/* SPA_VERSION			*/
	uint64_t	ub_txg;		/* txg of last sync		*/
	uint64_t	ub_guid_sum;	/* sum of all vdev guids	*/
	uint64_t	ub_timestamp;	/* UTC time of last sync	*/
	blkptr_t	ub_rootbp;	/* MOS objset_phys_t		*/

	/* highest SPA_VERSION supported by software that wrote this txg */
	uint64_t	ub_software_version;

	/* These fields are reserved for features that are under development: */
	uint64_t	ub_mmp_magic;
	uint64_t	ub_mmp_delay;
	uint64_t	ub_mmp_seq;

	/*
	 * ub_checkpoint_txg indicates two things about the current uberblock:
	 *
	 * 1] If it is not zero then this uberblock is a checkpoint. If it is
	 *    zero, then this uberblock is not a checkpoint.
	 *
	 * 2] On checkpointed uberblocks, the value of ub_checkpoint_txg is
	 *    the ub_txg that the uberblock had at the time we moved it to
	 *    the MOS config.
	 *
	 * The field is set when we checkpoint the uberblock and continues to
	 * hold that value even after we've rewound (unlike the ub_txg that
	 * is reset to a higher value).
	 *
	 * Besides checks used to determine whether we are reopening the
	 * pool from a checkpointed uberblock [see spa_ld_select_uberblock()],
	 * the value of the field is used to determine which ZIL blocks have
	 * been allocated according to the ms_sm when we are rewinding to a
	 * checkpoint. Specifically, if blk_birth > ub_checkpoint_txg, then
	 * the ZIL block is not allocated [see uses of spa_min_claim_txg()].
	 */
	uint64_t	ub_checkpoint_txg;
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UBERBLOCK_IMPL_H */
