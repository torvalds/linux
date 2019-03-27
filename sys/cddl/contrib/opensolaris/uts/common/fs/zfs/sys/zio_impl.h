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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 */

#ifndef _ZIO_IMPL_H
#define	_ZIO_IMPL_H

#include <sys/zfs_context.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * XXX -- Describe ZFS I/O pipeline here. Fill in as needed.
 *
 * The ZFS I/O pipeline is comprised of various stages which are defined
 * in the zio_stage enum below. The individual stages are used to construct
 * these basic I/O operations: Read, Write, Free, Claim, and Ioctl.
 *
 * I/O operations: (XXX - provide detail for each of the operations)
 *
 * Read:
 * Write:
 * Free:
 * Claim:
 * Ioctl:
 *
 * Although the most common pipeline are used by the basic I/O operations
 * above, there are some helper pipelines (one could consider them
 * sub-pipelines) which are used internally by the ZIO module and are
 * explained below:
 *
 * Interlock Pipeline:
 * The interlock pipeline is the most basic pipeline and is used by all
 * of the I/O operations. The interlock pipeline does not perform any I/O
 * and is used to coordinate the dependencies between I/Os that are being
 * issued (i.e. the parent/child relationship).
 *
 * Vdev child Pipeline:
 * The vdev child pipeline is responsible for performing the physical I/O.
 * It is in this pipeline where the I/O are queued and possibly cached.
 *
 * In addition to performing I/O, the pipeline is also responsible for
 * data transformations. The transformations performed are based on the
 * specific properties that user may have selected and modify the
 * behavior of the pipeline. Examples of supported transformations are
 * compression, dedup, and nop writes. Transformations will either modify
 * the data or the pipeline. This list below further describes each of
 * the supported transformations:
 *
 * Compression:
 * ZFS supports three different flavors of compression -- gzip, lzjb, and
 * zle. Compression occurs as part of the write pipeline and is performed
 * in the ZIO_STAGE_WRITE_BP_INIT stage.
 *
 * Dedup:
 * Dedup reads are handled by the ZIO_STAGE_DDT_READ_START and
 * ZIO_STAGE_DDT_READ_DONE stages. These stages are added to an existing
 * read pipeline if the dedup bit is set on the block pointer.
 * Writing a dedup block is performed by the ZIO_STAGE_DDT_WRITE stage
 * and added to a write pipeline if a user has enabled dedup on that
 * particular dataset.
 *
 * NOP Write:
 * The NOP write feature is performed by the ZIO_STAGE_NOP_WRITE stage
 * and is added to an existing write pipeline if a crypographically
 * secure checksum (i.e. SHA256) is enabled and compression is turned on.
 * The NOP write stage will compare the checksums of the current data
 * on-disk (level-0 blocks only) and the data that is currently being written.
 * If the checksum values are identical then the pipeline is converted to
 * an interlock pipeline skipping block allocation and bypassing the
 * physical I/O.  The nop write feature can handle writes in either
 * syncing or open context (i.e. zil writes) and as a result is mutually
 * exclusive with dedup.
 */

/*
 * zio pipeline stage definitions
 */
enum zio_stage {
	ZIO_STAGE_OPEN			= 1 << 0,	/* RWFCI */

	ZIO_STAGE_READ_BP_INIT		= 1 << 1,	/* R---- */
	ZIO_STAGE_WRITE_BP_INIT		= 1 << 2,	/* -W--- */
	ZIO_STAGE_FREE_BP_INIT		= 1 << 3,	/* --F-- */
	ZIO_STAGE_ISSUE_ASYNC		= 1 << 4,	/* RWF-- */
	ZIO_STAGE_WRITE_COMPRESS	= 1 << 5,	/* -W--- */

	ZIO_STAGE_CHECKSUM_GENERATE	= 1 << 6,	/* -W--- */

	ZIO_STAGE_NOP_WRITE		= 1 << 7,	/* -W--- */

	ZIO_STAGE_DDT_READ_START	= 1 << 8,	/* R---- */
	ZIO_STAGE_DDT_READ_DONE		= 1 << 9,	/* R---- */
	ZIO_STAGE_DDT_WRITE		= 1 << 10,	/* -W--- */
	ZIO_STAGE_DDT_FREE		= 1 << 11,	/* --F-- */

	ZIO_STAGE_GANG_ASSEMBLE		= 1 << 12,	/* RWFC- */
	ZIO_STAGE_GANG_ISSUE		= 1 << 13,	/* RWFC- */

	ZIO_STAGE_DVA_THROTTLE		= 1 << 14,	/* -W--- */
	ZIO_STAGE_DVA_ALLOCATE		= 1 << 15,	/* -W--- */
	ZIO_STAGE_DVA_FREE		= 1 << 16,	/* --F-- */
	ZIO_STAGE_DVA_CLAIM		= 1 << 17,	/* ---C- */

	ZIO_STAGE_READY			= 1 << 18,	/* RWFCI */

	ZIO_STAGE_VDEV_IO_START		= 1 << 19,	/* RWF-I */
	ZIO_STAGE_VDEV_IO_DONE		= 1 << 20,	/* RWF-I */
	ZIO_STAGE_VDEV_IO_ASSESS	= 1 << 21,	/* RWF-I */

	ZIO_STAGE_CHECKSUM_VERIFY	= 1 << 22,	/* R---- */

	ZIO_STAGE_DONE			= 1 << 23	/* RWFCI */
};

#define	ZIO_INTERLOCK_STAGES			\
	(ZIO_STAGE_READY |			\
	ZIO_STAGE_DONE)

#define	ZIO_INTERLOCK_PIPELINE			\
	ZIO_INTERLOCK_STAGES

#define	ZIO_VDEV_IO_STAGES			\
	(ZIO_STAGE_VDEV_IO_START |		\
	ZIO_STAGE_VDEV_IO_DONE |		\
	ZIO_STAGE_VDEV_IO_ASSESS)

#define	ZIO_VDEV_CHILD_PIPELINE			\
	(ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_DONE)

#define	ZIO_READ_COMMON_STAGES			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_CHECKSUM_VERIFY)

#define	ZIO_READ_PHYS_PIPELINE			\
	ZIO_READ_COMMON_STAGES

#define	ZIO_READ_PIPELINE			\
	(ZIO_READ_COMMON_STAGES |		\
	ZIO_STAGE_READ_BP_INIT)

#define	ZIO_DDT_CHILD_READ_PIPELINE		\
	ZIO_READ_COMMON_STAGES

#define	ZIO_DDT_READ_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_READ_BP_INIT |		\
	ZIO_STAGE_DDT_READ_START |		\
	ZIO_STAGE_DDT_READ_DONE)

#define	ZIO_WRITE_COMMON_STAGES			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_ISSUE_ASYNC |			\
	ZIO_STAGE_CHECKSUM_GENERATE)

#define	ZIO_WRITE_PHYS_PIPELINE			\
	ZIO_WRITE_COMMON_STAGES

#define	ZIO_REWRITE_PIPELINE			\
	(ZIO_WRITE_COMMON_STAGES |		\
	ZIO_STAGE_WRITE_COMPRESS |		\
	ZIO_STAGE_WRITE_BP_INIT)

#define	ZIO_WRITE_PIPELINE			\
	(ZIO_WRITE_COMMON_STAGES |		\
	ZIO_STAGE_WRITE_BP_INIT |		\
	ZIO_STAGE_WRITE_COMPRESS |		\
	ZIO_STAGE_DVA_THROTTLE |		\
	ZIO_STAGE_DVA_ALLOCATE)

#define	ZIO_DDT_CHILD_WRITE_PIPELINE		\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_VDEV_IO_STAGES |			\
	ZIO_STAGE_DVA_THROTTLE |		\
	ZIO_STAGE_DVA_ALLOCATE)

#define	ZIO_DDT_WRITE_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_WRITE_BP_INIT |		\
	ZIO_STAGE_ISSUE_ASYNC |			\
	ZIO_STAGE_WRITE_COMPRESS |		\
	ZIO_STAGE_CHECKSUM_GENERATE |		\
	ZIO_STAGE_DDT_WRITE)

#define	ZIO_GANG_STAGES				\
	(ZIO_STAGE_GANG_ASSEMBLE |		\
	ZIO_STAGE_GANG_ISSUE)

#define	ZIO_FREE_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_FREE_BP_INIT |		\
	ZIO_STAGE_DVA_FREE)

#define	ZIO_FREE_PHYS_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_VDEV_IO_STAGES)

#define	ZIO_DDT_FREE_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_FREE_BP_INIT |		\
	ZIO_STAGE_ISSUE_ASYNC |			\
	ZIO_STAGE_DDT_FREE)

#define	ZIO_CLAIM_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_DVA_CLAIM)

#define	ZIO_IOCTL_PIPELINE			\
	(ZIO_INTERLOCK_STAGES |			\
	ZIO_STAGE_VDEV_IO_START |		\
	ZIO_STAGE_VDEV_IO_ASSESS)

#define	ZIO_BLOCKING_STAGES			\
	(ZIO_STAGE_DVA_ALLOCATE |		\
	ZIO_STAGE_DVA_CLAIM |			\
	ZIO_STAGE_VDEV_IO_START)

extern void zio_inject_init(void);
extern void zio_inject_fini(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZIO_IMPL_H */
