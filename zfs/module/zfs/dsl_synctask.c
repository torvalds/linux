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

#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
#include <sys/metaslab.h>

#define	DST_AVG_BLKSHIFT 14

/* ARGSUSED */
static int
dsl_null_checkfunc(void *arg, dmu_tx_t *tx)
{
	return (0);
}

/*
 * Called from open context to perform a callback in syncing context.  Waits
 * for the operation to complete.
 *
 * The checkfunc will be called from open context as a preliminary check
 * which can quickly fail.  If it succeeds, it will be called again from
 * syncing context.  The checkfunc should generally be designed to work
 * properly in either context, but if necessary it can check
 * dmu_tx_is_syncing(tx).
 *
 * The synctask infrastructure enforces proper locking strategy with respect
 * to the dp_config_rwlock -- the lock will always be held when the callbacks
 * are called.  It will be held for read during the open-context (preliminary)
 * call to the checkfunc, and then held for write from syncing context during
 * the calls to the check and sync funcs.
 *
 * A dataset or pool name can be passed as the first argument.  Typically,
 * the check func will hold, check the return value of the hold, and then
 * release the dataset.  The sync func will VERIFYO(hold()) the dataset.
 * This is safe because no changes can be made between the check and sync funcs,
 * and the sync func will only be called if the check func successfully opened
 * the dataset.
 */
int
dsl_sync_task(const char *pool, dsl_checkfunc_t *checkfunc,
    dsl_syncfunc_t *syncfunc, void *arg,
    int blocks_modified, zfs_space_check_t space_check)
{
	spa_t *spa;
	dmu_tx_t *tx;
	int err;
	dsl_sync_task_t dst = { { { NULL } } };
	dsl_pool_t *dp;

	err = spa_open(pool, &spa, FTAG);
	if (err != 0)
		return (err);
	dp = spa_get_dsl(spa);

top:
	tx = dmu_tx_create_dd(dp->dp_mos_dir);
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));

	dst.dst_pool = dp;
	dst.dst_txg = dmu_tx_get_txg(tx);
	dst.dst_space = blocks_modified << DST_AVG_BLKSHIFT;
	dst.dst_space_check = space_check;
	dst.dst_checkfunc = checkfunc != NULL ? checkfunc : dsl_null_checkfunc;
	dst.dst_syncfunc = syncfunc;
	dst.dst_arg = arg;
	dst.dst_error = 0;
	dst.dst_nowaiter = B_FALSE;

	dsl_pool_config_enter(dp, FTAG);
	err = dst.dst_checkfunc(arg, tx);
	dsl_pool_config_exit(dp, FTAG);

	if (err != 0) {
		dmu_tx_commit(tx);
		spa_close(spa, FTAG);
		return (err);
	}

	VERIFY(txg_list_add_tail(&dp->dp_sync_tasks, &dst, dst.dst_txg));

	dmu_tx_commit(tx);

	txg_wait_synced(dp, dst.dst_txg);

	if (dst.dst_error == EAGAIN) {
		txg_wait_synced(dp, dst.dst_txg + TXG_DEFER_SIZE);
		goto top;
	}

	spa_close(spa, FTAG);
	return (dst.dst_error);
}

void
dsl_sync_task_nowait(dsl_pool_t *dp, dsl_syncfunc_t *syncfunc, void *arg,
    int blocks_modified, zfs_space_check_t space_check, dmu_tx_t *tx)
{
	dsl_sync_task_t *dst = kmem_zalloc(sizeof (*dst), KM_SLEEP);

	dst->dst_pool = dp;
	dst->dst_txg = dmu_tx_get_txg(tx);
	dst->dst_space = blocks_modified << DST_AVG_BLKSHIFT;
	dst->dst_space_check = space_check;
	dst->dst_checkfunc = dsl_null_checkfunc;
	dst->dst_syncfunc = syncfunc;
	dst->dst_arg = arg;
	dst->dst_error = 0;
	dst->dst_nowaiter = B_TRUE;

	VERIFY(txg_list_add_tail(&dp->dp_sync_tasks, dst, dst->dst_txg));
}

/*
 * Called in syncing context to execute the synctask.
 */
void
dsl_sync_task_sync(dsl_sync_task_t *dst, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dst->dst_pool;

	ASSERT0(dst->dst_error);

	/*
	 * Check for sufficient space.
	 *
	 * When the sync task was created, the caller specified the
	 * type of space checking required.  See the comment in
	 * zfs_space_check_t for details on the semantics of each
	 * type of space checking.
	 *
	 * We just check against what's on-disk; we don't want any
	 * in-flight accounting to get in our way, because open context
	 * may have already used up various in-core limits
	 * (arc_tempreserve, dsl_pool_tempreserve).
	 */
	if (dst->dst_space_check != ZFS_SPACE_CHECK_NONE) {
		uint64_t quota = dsl_pool_adjustedsize(dp,
		    dst->dst_space_check == ZFS_SPACE_CHECK_RESERVED) -
		    metaslab_class_get_deferred(spa_normal_class(dp->dp_spa));
		uint64_t used = dsl_dir_phys(dp->dp_root_dir)->dd_used_bytes;
		/* MOS space is triple-dittoed, so we multiply by 3. */
		if (dst->dst_space > 0 && used + dst->dst_space * 3 > quota) {
			dst->dst_error = SET_ERROR(ENOSPC);
			if (dst->dst_nowaiter)
				kmem_free(dst, sizeof (*dst));
			return;
		}
	}

	/*
	 * Check for errors by calling checkfunc.
	 */
	rrw_enter(&dp->dp_config_rwlock, RW_WRITER, FTAG);
	dst->dst_error = dst->dst_checkfunc(dst->dst_arg, tx);
	if (dst->dst_error == 0)
		dst->dst_syncfunc(dst->dst_arg, tx);
	rrw_exit(&dp->dp_config_rwlock, FTAG);
	if (dst->dst_nowaiter)
		kmem_free(dst, sizeof (*dst));
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(dsl_sync_task);
EXPORT_SYMBOL(dsl_sync_task_nowait);
#endif
