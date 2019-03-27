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

#ifndef	_SYS_BPOBJ_H
#define	_SYS_BPOBJ_H

#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct bpobj_phys {
	/*
	 * This is the bonus buffer for the dead lists.  The object's
	 * contents is an array of bpo_entries blkptr_t's, representing
	 * a total of bpo_bytes physical space.
	 */
	uint64_t	bpo_num_blkptrs;
	uint64_t	bpo_bytes;
	uint64_t	bpo_comp;
	uint64_t	bpo_uncomp;
	uint64_t	bpo_subobjs;
	uint64_t	bpo_num_subobjs;
} bpobj_phys_t;

#define	BPOBJ_SIZE_V0	(2 * sizeof (uint64_t))
#define	BPOBJ_SIZE_V1	(4 * sizeof (uint64_t))

typedef struct bpobj {
	kmutex_t	bpo_lock;
	objset_t	*bpo_os;
	uint64_t	bpo_object;
	int		bpo_epb;
	uint8_t		bpo_havecomp;
	uint8_t		bpo_havesubobj;
	bpobj_phys_t	*bpo_phys;
	dmu_buf_t	*bpo_dbuf;
	dmu_buf_t	*bpo_cached_dbuf;
} bpobj_t;

typedef int bpobj_itor_t(void *arg, const blkptr_t *bp, dmu_tx_t *tx);

uint64_t bpobj_alloc(objset_t *mos, int blocksize, dmu_tx_t *tx);
uint64_t bpobj_alloc_empty(objset_t *os, int blocksize, dmu_tx_t *tx);
void bpobj_free(objset_t *os, uint64_t obj, dmu_tx_t *tx);
void bpobj_decr_empty(objset_t *os, dmu_tx_t *tx);

int bpobj_open(bpobj_t *bpo, objset_t *mos, uint64_t object);
void bpobj_close(bpobj_t *bpo);
boolean_t bpobj_is_open(const bpobj_t *bpo);

int bpobj_iterate(bpobj_t *bpo, bpobj_itor_t func, void *arg, dmu_tx_t *tx);
int bpobj_iterate_nofree(bpobj_t *bpo, bpobj_itor_t func, void *, dmu_tx_t *);

void bpobj_enqueue_subobj(bpobj_t *bpo, uint64_t subobj, dmu_tx_t *tx);
void bpobj_enqueue(bpobj_t *bpo, const blkptr_t *bp, dmu_tx_t *tx);

int bpobj_space(bpobj_t *bpo,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
int bpobj_space_range(bpobj_t *bpo, uint64_t mintxg, uint64_t maxtxg,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
boolean_t bpobj_is_empty(bpobj_t *bpo);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_BPOBJ_H */
