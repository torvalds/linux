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
 * Copyright (c) 2015 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DSL_DEADLIST_H
#define	_SYS_DSL_DEADLIST_H

#include <sys/bpobj.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dmu_buf;
struct dsl_dataset;

typedef struct dsl_deadlist_phys {
	uint64_t dl_used;
	uint64_t dl_comp;
	uint64_t dl_uncomp;
	uint64_t dl_pad[37]; /* pad out to 320b for future expansion */
} dsl_deadlist_phys_t;

typedef struct dsl_deadlist {
	objset_t *dl_os;
	uint64_t dl_object;
	avl_tree_t dl_tree;
	boolean_t dl_havetree;
	struct dmu_buf *dl_dbuf;
	dsl_deadlist_phys_t *dl_phys;
	kmutex_t dl_lock;

	/* if it's the old on-disk format: */
	bpobj_t dl_bpobj;
	boolean_t dl_oldfmt;
} dsl_deadlist_t;

typedef struct dsl_deadlist_entry {
	avl_node_t dle_node;
	uint64_t dle_mintxg;
	bpobj_t dle_bpobj;
} dsl_deadlist_entry_t;

void dsl_deadlist_open(dsl_deadlist_t *dl, objset_t *os, uint64_t object);
void dsl_deadlist_close(dsl_deadlist_t *dl);
uint64_t dsl_deadlist_alloc(objset_t *os, dmu_tx_t *tx);
void dsl_deadlist_free(objset_t *os, uint64_t dlobj, dmu_tx_t *tx);
void dsl_deadlist_insert(dsl_deadlist_t *dl, const blkptr_t *bp, dmu_tx_t *tx);
void dsl_deadlist_add_key(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx);
void dsl_deadlist_remove_key(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx);
uint64_t dsl_deadlist_clone(dsl_deadlist_t *dl, uint64_t maxtxg,
    uint64_t mrs_obj, dmu_tx_t *tx);
void dsl_deadlist_space(dsl_deadlist_t *dl,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
void dsl_deadlist_space_range(dsl_deadlist_t *dl,
    uint64_t mintxg, uint64_t maxtxg,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
void dsl_deadlist_merge(dsl_deadlist_t *dl, uint64_t obj, dmu_tx_t *tx);
void dsl_deadlist_move_bpobj(dsl_deadlist_t *dl, bpobj_t *bpo, uint64_t mintxg,
    dmu_tx_t *tx);
boolean_t dsl_deadlist_is_open(dsl_deadlist_t *dl);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_DEADLIST_H */
