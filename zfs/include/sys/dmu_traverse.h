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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DMU_TRAVERSE_H
#define	_SYS_DMU_TRAVERSE_H

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zio.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dnode_phys;
struct dsl_dataset;
struct zilog;
struct arc_buf;

typedef int (blkptr_cb_t)(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const struct dnode_phys *dnp, void *arg);

#define	TRAVERSE_PRE			(1<<0)
#define	TRAVERSE_POST			(1<<1)
#define	TRAVERSE_PREFETCH_METADATA	(1<<2)
#define	TRAVERSE_PREFETCH_DATA		(1<<3)
#define	TRAVERSE_PREFETCH (TRAVERSE_PREFETCH_METADATA | TRAVERSE_PREFETCH_DATA)
#define	TRAVERSE_HARD			(1<<4)

/* Special traverse error return value to indicate skipping of children */
#define	TRAVERSE_VISIT_NO_CHILDREN	-1

int traverse_dataset(struct dsl_dataset *ds,
    uint64_t txg_start, int flags, blkptr_cb_t func, void *arg);
int traverse_dataset_resume(struct dsl_dataset *ds, uint64_t txg_start,
    zbookmark_phys_t *resume, int flags, blkptr_cb_t func, void *arg);
int traverse_dataset_destroyed(spa_t *spa, blkptr_t *blkptr,
    uint64_t txg_start, zbookmark_phys_t *resume, int flags,
    blkptr_cb_t func, void *arg);
int traverse_pool(spa_t *spa,
    uint64_t txg_start, int flags, blkptr_cb_t func, void *arg);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DMU_TRAVERSE_H */
