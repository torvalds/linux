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
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DSL_SYNCTASK_H
#define	_SYS_DSL_SYNCTASK_H

#include <sys/txg.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_pool;

typedef int (dsl_checkfunc_t)(void *, dmu_tx_t *);
typedef void (dsl_syncfunc_t)(void *, dmu_tx_t *);

typedef enum zfs_space_check {
	/*
	 * Normal space check: if there is less than 3.2% free space,
	 * the operation will fail.  Operations which are logically
	 * creating things should use this (e.g. "zfs create", "zfs snapshot").
	 * User writes (via the ZPL / ZVOL) also fail at this point.
	 */
	ZFS_SPACE_CHECK_NORMAL,

	/*
	 * Space check allows use of half the slop space.  If there
	 * is less than 1.6% free space, the operation will fail.  Most
	 * operations should use this (e.g. "zfs set", "zfs rename"),
	 * because we want them to succeed even after user writes are failing,
	 * so that they can be used as part of the space recovery process.
	 */
	ZFS_SPACE_CHECK_RESERVED,

	/*
	 * Space check allows use of three quarters of the slop space.
	 * If there is less than 0.8% free space, the operation will
	 * fail.
	 */
	ZFS_SPACE_CHECK_EXTRA_RESERVED,

	/*
	 * In all cases "zfs destroy" is expected to result in an net
	 * reduction of space, except one. When the pool has a
	 * checkpoint, space freed by "zfs destroy" will not actually
	 * free anything internally. Thus, it starts failing after
	 * three quarters of the slop space is exceeded.
	 */
	ZFS_SPACE_CHECK_DESTROY = ZFS_SPACE_CHECK_EXTRA_RESERVED,

	/*
	 * A channel program can run a "zfs destroy" as part of its
	 * script and therefore has the same space_check policy when
	 * being evaluated.
	 */
	ZFS_SPACE_CHECK_ZCP_EVAL = ZFS_SPACE_CHECK_DESTROY,

	/*
	 * No space check is performed. This level of space check should
	 * be used cautiously as operations that use it can even run when
	 * 0.8% capacity is left for use. In this scenario, if there is a
	 * checkpoint, async destroys are suspended and any kind of freeing
	 * can potentially add space instead of freeing it.
	 *
	 * See also the comments above spa_slop_shift.
	 */
	ZFS_SPACE_CHECK_NONE,

	ZFS_SPACE_CHECK_DISCARD_CHECKPOINT = ZFS_SPACE_CHECK_NONE,

} zfs_space_check_t;

typedef struct dsl_sync_task {
	txg_node_t dst_node;
	struct dsl_pool *dst_pool;
	uint64_t dst_txg;
	int dst_space;
	zfs_space_check_t dst_space_check;
	dsl_checkfunc_t *dst_checkfunc;
	dsl_syncfunc_t *dst_syncfunc;
	void *dst_arg;
	int dst_error;
	boolean_t dst_nowaiter;
} dsl_sync_task_t;

void dsl_sync_task_sync(dsl_sync_task_t *, dmu_tx_t *);
int dsl_sync_task(const char *, dsl_checkfunc_t *,
    dsl_syncfunc_t *, void *, int, zfs_space_check_t);
void dsl_sync_task_nowait(struct dsl_pool *, dsl_syncfunc_t *,
    void *, int, zfs_space_check_t, dmu_tx_t *);
int dsl_early_sync_task(const char *, dsl_checkfunc_t *,
    dsl_syncfunc_t *, void *, int, zfs_space_check_t);
void dsl_early_sync_task_nowait(struct dsl_pool *, dsl_syncfunc_t *,
    void *, int, zfs_space_check_t, dmu_tx_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_SYNCTASK_H */
