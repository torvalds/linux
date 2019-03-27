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
 */

#ifndef	_SYS_BPLIST_H
#define	_SYS_BPLIST_H

#include <sys/zfs_context.h>
#include <sys/spa.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct bplist_entry {
	blkptr_t	bpe_blk;
	list_node_t	bpe_node;
} bplist_entry_t;

typedef struct bplist {
	kmutex_t	bpl_lock;
	list_t		bpl_list;
} bplist_t;

typedef int bplist_itor_t(void *arg, const blkptr_t *bp, dmu_tx_t *tx);

void bplist_create(bplist_t *bpl);
void bplist_destroy(bplist_t *bpl);
void bplist_append(bplist_t *bpl, const blkptr_t *bp);
void bplist_iterate(bplist_t *bpl, bplist_itor_t *func,
    void *arg, dmu_tx_t *tx);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_BPLIST_H */
