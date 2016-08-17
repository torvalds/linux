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
 * Portions Copyright 2011 Martin Matuska
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/txg_impl.h>
#include <sys/dmu_impl.h>
#include <sys/spa_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>
#include <sys/zil.h>
#include <sys/callb.h>
#include <sys/trace_txg.h>

/*
 * ZFS Transaction Groups
 * ----------------------
 *
 * ZFS transaction groups are, as the name implies, groups of transactions
 * that act on persistent state. ZFS asserts consistency at the granularity of
 * these transaction groups. Each successive transaction group (txg) is
 * assigned a 64-bit consecutive identifier. There are three active
 * transaction group states: open, quiescing, or syncing. At any given time,
 * there may be an active txg associated with each state; each active txg may
 * either be processing, or blocked waiting to enter the next state. There may
 * be up to three active txgs, and there is always a txg in the open state
 * (though it may be blocked waiting to enter the quiescing state). In broad
 * strokes, transactions -- operations that change in-memory structures -- are
 * accepted into the txg in the open state, and are completed while the txg is
 * in the open or quiescing states. The accumulated changes are written to
 * disk in the syncing state.
 *
 * Open
 *
 * When a new txg becomes active, it first enters the open state. New
 * transactions -- updates to in-memory structures -- are assigned to the
 * currently open txg. There is always a txg in the open state so that ZFS can
 * accept new changes (though the txg may refuse new changes if it has hit
 * some limit). ZFS advances the open txg to the next state for a variety of
 * reasons such as it hitting a time or size threshold, or the execution of an
 * administrative action that must be completed in the syncing state.
 *
 * Quiescing
 *
 * After a txg exits the open state, it enters the quiescing state. The
 * quiescing state is intended to provide a buffer between accepting new
 * transactions in the open state and writing them out to stable storage in
 * the syncing state. While quiescing, transactions can continue their
 * operation without delaying either of the other states. Typically, a txg is
 * in the quiescing state very briefly since the operations are bounded by
 * software latencies rather than, say, slower I/O latencies. After all
 * transactions complete, the txg is ready to enter the next state.
 *
 * Syncing
 *
 * In the syncing state, the in-memory state built up during the open and (to
 * a lesser degree) the quiescing states is written to stable storage. The
 * process of writing out modified data can, in turn modify more data. For
 * example when we write new blocks, we need to allocate space for them; those
 * allocations modify metadata (space maps)... which themselves must be
 * written to stable storage. During the sync state, ZFS iterates, writing out
 * data until it converges and all in-memory changes have been written out.
 * The first such pass is the largest as it encompasses all the modified user
 * data (as opposed to filesystem metadata). Subsequent passes typically have
 * far less data to write as they consist exclusively of filesystem metadata.
 *
 * To ensure convergence, after a certain number of passes ZFS begins
 * overwriting locations on stable storage that had been allocated earlier in
 * the syncing state (and subsequently freed). ZFS usually allocates new
 * blocks to optimize for large, continuous, writes. For the syncing state to
 * converge however it must complete a pass where no new blocks are allocated
 * since each allocation requires a modification of persistent metadata.
 * Further, to hasten convergence, after a prescribed number of passes, ZFS
 * also defers frees, and stops compressing.
 *
 * In addition to writing out user data, we must also execute synctasks during
 * the syncing context. A synctask is the mechanism by which some
 * administrative activities work such as creating and destroying snapshots or
 * datasets. Note that when a synctask is initiated it enters the open txg,
 * and ZFS then pushes that txg as quickly as possible to completion of the
 * syncing state in order to reduce the latency of the administrative
 * activity. To complete the syncing state, ZFS writes out a new uberblock,
 * the root of the tree of blocks that comprise all state stored on the ZFS
 * pool. Finally, if there is a quiesced txg waiting, we signal that it can
 * now transition to the syncing state.
 */

static void txg_sync_thread(dsl_pool_t *dp);
static void txg_quiesce_thread(dsl_pool_t *dp);

int zfs_txg_timeout = 5;	/* max seconds worth of delta per txg */

/*
 * Prepare the txg subsystem.
 */
void
txg_init(dsl_pool_t *dp, uint64_t txg)
{
	tx_state_t *tx = &dp->dp_tx;
	int c;
	bzero(tx, sizeof (tx_state_t));

	tx->tx_cpu = vmem_zalloc(max_ncpus * sizeof (tx_cpu_t), KM_SLEEP);

	for (c = 0; c < max_ncpus; c++) {
		int i;

		mutex_init(&tx->tx_cpu[c].tc_lock, NULL, MUTEX_DEFAULT, NULL);
		mutex_init(&tx->tx_cpu[c].tc_open_lock, NULL, MUTEX_NOLOCKDEP,
		    NULL);
		for (i = 0; i < TXG_SIZE; i++) {
			cv_init(&tx->tx_cpu[c].tc_cv[i], NULL, CV_DEFAULT,
			    NULL);
			list_create(&tx->tx_cpu[c].tc_callbacks[i],
			    sizeof (dmu_tx_callback_t),
			    offsetof(dmu_tx_callback_t, dcb_node));
		}
	}

	mutex_init(&tx->tx_sync_lock, NULL, MUTEX_DEFAULT, NULL);

	cv_init(&tx->tx_sync_more_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&tx->tx_sync_done_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&tx->tx_quiesce_more_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&tx->tx_quiesce_done_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&tx->tx_exit_cv, NULL, CV_DEFAULT, NULL);

	tx->tx_open_txg = txg;
}

/*
 * Close down the txg subsystem.
 */
void
txg_fini(dsl_pool_t *dp)
{
	tx_state_t *tx = &dp->dp_tx;
	int c;

	ASSERT(tx->tx_threads == 0);

	mutex_destroy(&tx->tx_sync_lock);

	cv_destroy(&tx->tx_sync_more_cv);
	cv_destroy(&tx->tx_sync_done_cv);
	cv_destroy(&tx->tx_quiesce_more_cv);
	cv_destroy(&tx->tx_quiesce_done_cv);
	cv_destroy(&tx->tx_exit_cv);

	for (c = 0; c < max_ncpus; c++) {
		int i;

		mutex_destroy(&tx->tx_cpu[c].tc_open_lock);
		mutex_destroy(&tx->tx_cpu[c].tc_lock);
		for (i = 0; i < TXG_SIZE; i++) {
			cv_destroy(&tx->tx_cpu[c].tc_cv[i]);
			list_destroy(&tx->tx_cpu[c].tc_callbacks[i]);
		}
	}

	if (tx->tx_commit_cb_taskq != NULL)
		taskq_destroy(tx->tx_commit_cb_taskq);

	vmem_free(tx->tx_cpu, max_ncpus * sizeof (tx_cpu_t));

	bzero(tx, sizeof (tx_state_t));
}

/*
 * Start syncing transaction groups.
 */
void
txg_sync_start(dsl_pool_t *dp)
{
	tx_state_t *tx = &dp->dp_tx;

	mutex_enter(&tx->tx_sync_lock);

	dprintf("pool %p\n", dp);

	ASSERT(tx->tx_threads == 0);

	tx->tx_threads = 2;

	tx->tx_quiesce_thread = thread_create(NULL, 0, txg_quiesce_thread,
	    dp, 0, &p0, TS_RUN, defclsyspri);

	/*
	 * The sync thread can need a larger-than-default stack size on
	 * 32-bit x86.  This is due in part to nested pools and
	 * scrub_visitbp() recursion.
	 */
	tx->tx_sync_thread = thread_create(NULL, 0, txg_sync_thread,
	    dp, 0, &p0, TS_RUN, defclsyspri);

	mutex_exit(&tx->tx_sync_lock);
}

static void
txg_thread_enter(tx_state_t *tx, callb_cpr_t *cpr)
{
	CALLB_CPR_INIT(cpr, &tx->tx_sync_lock, callb_generic_cpr, FTAG);
	mutex_enter(&tx->tx_sync_lock);
}

static void
txg_thread_exit(tx_state_t *tx, callb_cpr_t *cpr, kthread_t **tpp)
{
	ASSERT(*tpp != NULL);
	*tpp = NULL;
	tx->tx_threads--;
	cv_broadcast(&tx->tx_exit_cv);
	CALLB_CPR_EXIT(cpr);		/* drops &tx->tx_sync_lock */
	thread_exit();
}

static void
txg_thread_wait(tx_state_t *tx, callb_cpr_t *cpr, kcondvar_t *cv, clock_t time)
{
	CALLB_CPR_SAFE_BEGIN(cpr);

	if (time)
		(void) cv_timedwait_sig(cv, &tx->tx_sync_lock,
		    ddi_get_lbolt() + time);
	else
		cv_wait_sig(cv, &tx->tx_sync_lock);

	CALLB_CPR_SAFE_END(cpr, &tx->tx_sync_lock);
}

/*
 * Stop syncing transaction groups.
 */
void
txg_sync_stop(dsl_pool_t *dp)
{
	tx_state_t *tx = &dp->dp_tx;

	dprintf("pool %p\n", dp);
	/*
	 * Finish off any work in progress.
	 */
	ASSERT(tx->tx_threads == 2);

	/*
	 * We need to ensure that we've vacated the deferred space_maps.
	 */
	txg_wait_synced(dp, tx->tx_open_txg + TXG_DEFER_SIZE);

	/*
	 * Wake all sync threads and wait for them to die.
	 */
	mutex_enter(&tx->tx_sync_lock);

	ASSERT(tx->tx_threads == 2);

	tx->tx_exiting = 1;

	cv_broadcast(&tx->tx_quiesce_more_cv);
	cv_broadcast(&tx->tx_quiesce_done_cv);
	cv_broadcast(&tx->tx_sync_more_cv);

	while (tx->tx_threads != 0)
		cv_wait(&tx->tx_exit_cv, &tx->tx_sync_lock);

	tx->tx_exiting = 0;

	mutex_exit(&tx->tx_sync_lock);
}

uint64_t
txg_hold_open(dsl_pool_t *dp, txg_handle_t *th)
{
	tx_state_t *tx = &dp->dp_tx;
	tx_cpu_t *tc;
	uint64_t txg;

	/*
	 * It appears the processor id is simply used as a "random"
	 * number to index into the array, and there isn't any other
	 * significance to the chosen tx_cpu. Because.. Why not use
	 * the current cpu to index into the array?
	 */
	kpreempt_disable();
	tc = &tx->tx_cpu[CPU_SEQID];
	kpreempt_enable();

	mutex_enter(&tc->tc_open_lock);
	txg = tx->tx_open_txg;

	mutex_enter(&tc->tc_lock);
	tc->tc_count[txg & TXG_MASK]++;
	mutex_exit(&tc->tc_lock);

	th->th_cpu = tc;
	th->th_txg = txg;

	return (txg);
}

void
txg_rele_to_quiesce(txg_handle_t *th)
{
	tx_cpu_t *tc = th->th_cpu;

	ASSERT(!MUTEX_HELD(&tc->tc_lock));
	mutex_exit(&tc->tc_open_lock);
}

void
txg_register_callbacks(txg_handle_t *th, list_t *tx_callbacks)
{
	tx_cpu_t *tc = th->th_cpu;
	int g = th->th_txg & TXG_MASK;

	mutex_enter(&tc->tc_lock);
	list_move_tail(&tc->tc_callbacks[g], tx_callbacks);
	mutex_exit(&tc->tc_lock);
}

void
txg_rele_to_sync(txg_handle_t *th)
{
	tx_cpu_t *tc = th->th_cpu;
	int g = th->th_txg & TXG_MASK;

	mutex_enter(&tc->tc_lock);
	ASSERT(tc->tc_count[g] != 0);
	if (--tc->tc_count[g] == 0)
		cv_broadcast(&tc->tc_cv[g]);
	mutex_exit(&tc->tc_lock);

	th->th_cpu = NULL;	/* defensive */
}

/*
 * Blocks until all transactions in the group are committed.
 *
 * On return, the transaction group has reached a stable state in which it can
 * then be passed off to the syncing context.
 */
static void
txg_quiesce(dsl_pool_t *dp, uint64_t txg)
{
	tx_state_t *tx = &dp->dp_tx;
	uint64_t tx_open_time;
	int g = txg & TXG_MASK;
	int c;

	/*
	 * Grab all tc_open_locks so nobody else can get into this txg.
	 */
	for (c = 0; c < max_ncpus; c++)
		mutex_enter(&tx->tx_cpu[c].tc_open_lock);

	ASSERT(txg == tx->tx_open_txg);
	tx->tx_open_txg++;
	tx->tx_open_time = tx_open_time = gethrtime();

	DTRACE_PROBE2(txg__quiescing, dsl_pool_t *, dp, uint64_t, txg);
	DTRACE_PROBE2(txg__opened, dsl_pool_t *, dp, uint64_t, tx->tx_open_txg);

	/*
	 * Now that we've incremented tx_open_txg, we can let threads
	 * enter the next transaction group.
	 */
	for (c = 0; c < max_ncpus; c++)
		mutex_exit(&tx->tx_cpu[c].tc_open_lock);

	spa_txg_history_set(dp->dp_spa, txg, TXG_STATE_OPEN, tx_open_time);
	spa_txg_history_add(dp->dp_spa, txg + 1, tx_open_time);

	/*
	 * Quiesce the transaction group by waiting for everyone to txg_exit().
	 */
	for (c = 0; c < max_ncpus; c++) {
		tx_cpu_t *tc = &tx->tx_cpu[c];
		mutex_enter(&tc->tc_lock);
		while (tc->tc_count[g] != 0)
			cv_wait(&tc->tc_cv[g], &tc->tc_lock);
		mutex_exit(&tc->tc_lock);
	}

	spa_txg_history_set(dp->dp_spa, txg, TXG_STATE_QUIESCED, gethrtime());
}

static void
txg_do_callbacks(list_t *cb_list)
{
	dmu_tx_do_callbacks(cb_list, 0);

	list_destroy(cb_list);

	kmem_free(cb_list, sizeof (list_t));
}

/*
 * Dispatch the commit callbacks registered on this txg to worker threads.
 *
 * If no callbacks are registered for a given TXG, nothing happens.
 * This function creates a taskq for the associated pool, if needed.
 */
static void
txg_dispatch_callbacks(dsl_pool_t *dp, uint64_t txg)
{
	int c;
	tx_state_t *tx = &dp->dp_tx;
	list_t *cb_list;

	for (c = 0; c < max_ncpus; c++) {
		tx_cpu_t *tc = &tx->tx_cpu[c];
		/*
		 * No need to lock tx_cpu_t at this point, since this can
		 * only be called once a txg has been synced.
		 */

		int g = txg & TXG_MASK;

		if (list_is_empty(&tc->tc_callbacks[g]))
			continue;

		if (tx->tx_commit_cb_taskq == NULL) {
			/*
			 * Commit callback taskq hasn't been created yet.
			 */
			tx->tx_commit_cb_taskq = taskq_create("tx_commit_cb",
			    max_ncpus, defclsyspri, max_ncpus, max_ncpus * 2,
			    TASKQ_PREPOPULATE | TASKQ_DYNAMIC);
		}

		cb_list = kmem_alloc(sizeof (list_t), KM_SLEEP);
		list_create(cb_list, sizeof (dmu_tx_callback_t),
		    offsetof(dmu_tx_callback_t, dcb_node));

		list_move_tail(cb_list, &tc->tc_callbacks[g]);

		(void) taskq_dispatch(tx->tx_commit_cb_taskq, (task_func_t *)
		    txg_do_callbacks, cb_list, TQ_SLEEP);
	}
}

/*
 * Wait for pending commit callbacks of already-synced transactions to finish
 * processing.
 * Calling this function from within a commit callback will deadlock.
 */
void
txg_wait_callbacks(dsl_pool_t *dp)
{
	tx_state_t *tx = &dp->dp_tx;

	if (tx->tx_commit_cb_taskq != NULL)
		taskq_wait_outstanding(tx->tx_commit_cb_taskq, 0);
}

static void
txg_sync_thread(dsl_pool_t *dp)
{
	spa_t *spa = dp->dp_spa;
	tx_state_t *tx = &dp->dp_tx;
	callb_cpr_t cpr;
	clock_t start, delta;

	(void) spl_fstrans_mark();
	txg_thread_enter(tx, &cpr);

	start = delta = 0;
	for (;;) {
		clock_t timeout = zfs_txg_timeout * hz;
		clock_t timer;
		uint64_t txg;
		txg_stat_t *ts;

		/*
		 * We sync when we're scanning, there's someone waiting
		 * on us, or the quiesce thread has handed off a txg to
		 * us, or we have reached our timeout.
		 */
		timer = (delta >= timeout ? 0 : timeout - delta);
		while (!dsl_scan_active(dp->dp_scan) &&
		    !tx->tx_exiting && timer > 0 &&
		    tx->tx_synced_txg >= tx->tx_sync_txg_waiting &&
		    tx->tx_quiesced_txg == 0 &&
		    dp->dp_dirty_total < zfs_dirty_data_sync) {
			dprintf("waiting; tx_synced=%llu waiting=%llu dp=%p\n",
			    tx->tx_synced_txg, tx->tx_sync_txg_waiting, dp);
			txg_thread_wait(tx, &cpr, &tx->tx_sync_more_cv, timer);
			delta = ddi_get_lbolt() - start;
			timer = (delta > timeout ? 0 : timeout - delta);
		}

		/*
		 * Wait until the quiesce thread hands off a txg to us,
		 * prompting it to do so if necessary.
		 */
		while (!tx->tx_exiting && tx->tx_quiesced_txg == 0) {
			if (tx->tx_quiesce_txg_waiting < tx->tx_open_txg+1)
				tx->tx_quiesce_txg_waiting = tx->tx_open_txg+1;
			cv_broadcast(&tx->tx_quiesce_more_cv);
			txg_thread_wait(tx, &cpr, &tx->tx_quiesce_done_cv, 0);
		}

		if (tx->tx_exiting)
			txg_thread_exit(tx, &cpr, &tx->tx_sync_thread);

		/*
		 * Consume the quiesced txg which has been handed off to
		 * us.  This may cause the quiescing thread to now be
		 * able to quiesce another txg, so we must signal it.
		 */
		txg = tx->tx_quiesced_txg;
		tx->tx_quiesced_txg = 0;
		tx->tx_syncing_txg = txg;
		DTRACE_PROBE2(txg__syncing, dsl_pool_t *, dp, uint64_t, txg);
		ts = spa_txg_history_init_io(spa, txg, dp);
		cv_broadcast(&tx->tx_quiesce_more_cv);

		dprintf("txg=%llu quiesce_txg=%llu sync_txg=%llu\n",
		    txg, tx->tx_quiesce_txg_waiting, tx->tx_sync_txg_waiting);
		mutex_exit(&tx->tx_sync_lock);

		start = ddi_get_lbolt();
		spa_sync(spa, txg);
		delta = ddi_get_lbolt() - start;

		mutex_enter(&tx->tx_sync_lock);
		tx->tx_synced_txg = txg;
		tx->tx_syncing_txg = 0;
		DTRACE_PROBE2(txg__synced, dsl_pool_t *, dp, uint64_t, txg);
		spa_txg_history_fini_io(spa, ts);
		cv_broadcast(&tx->tx_sync_done_cv);

		/*
		 * Dispatch commit callbacks to worker threads.
		 */
		txg_dispatch_callbacks(dp, txg);
	}
}

static void
txg_quiesce_thread(dsl_pool_t *dp)
{
	tx_state_t *tx = &dp->dp_tx;
	callb_cpr_t cpr;

	txg_thread_enter(tx, &cpr);

	for (;;) {
		uint64_t txg;

		/*
		 * We quiesce when there's someone waiting on us.
		 * However, we can only have one txg in "quiescing" or
		 * "quiesced, waiting to sync" state.  So we wait until
		 * the "quiesced, waiting to sync" txg has been consumed
		 * by the sync thread.
		 */
		while (!tx->tx_exiting &&
		    (tx->tx_open_txg >= tx->tx_quiesce_txg_waiting ||
		    tx->tx_quiesced_txg != 0))
			txg_thread_wait(tx, &cpr, &tx->tx_quiesce_more_cv, 0);

		if (tx->tx_exiting)
			txg_thread_exit(tx, &cpr, &tx->tx_quiesce_thread);

		txg = tx->tx_open_txg;
		dprintf("txg=%llu quiesce_txg=%llu sync_txg=%llu\n",
		    txg, tx->tx_quiesce_txg_waiting,
		    tx->tx_sync_txg_waiting);
		mutex_exit(&tx->tx_sync_lock);
		txg_quiesce(dp, txg);
		mutex_enter(&tx->tx_sync_lock);

		/*
		 * Hand this txg off to the sync thread.
		 */
		dprintf("quiesce done, handing off txg %llu\n", txg);
		tx->tx_quiesced_txg = txg;
		DTRACE_PROBE2(txg__quiesced, dsl_pool_t *, dp, uint64_t, txg);
		cv_broadcast(&tx->tx_sync_more_cv);
		cv_broadcast(&tx->tx_quiesce_done_cv);
	}
}

/*
 * Delay this thread by delay nanoseconds if we are still in the open
 * transaction group and there is already a waiting txg quiesing or quiesced.
 * Abort the delay if this txg stalls or enters the quiesing state.
 */
void
txg_delay(dsl_pool_t *dp, uint64_t txg, hrtime_t delay, hrtime_t resolution)
{
	tx_state_t *tx = &dp->dp_tx;
	hrtime_t start = gethrtime();

	/* don't delay if this txg could transition to quiescing immediately */
	if (tx->tx_open_txg > txg ||
	    tx->tx_syncing_txg == txg-1 || tx->tx_synced_txg == txg-1)
		return;

	mutex_enter(&tx->tx_sync_lock);
	if (tx->tx_open_txg > txg || tx->tx_synced_txg == txg-1) {
		mutex_exit(&tx->tx_sync_lock);
		return;
	}

	while (gethrtime() - start < delay &&
	    tx->tx_syncing_txg < txg-1 && !txg_stalled(dp)) {
		(void) cv_timedwait_hires(&tx->tx_quiesce_more_cv,
		    &tx->tx_sync_lock, delay, resolution, 0);
	}

	DMU_TX_STAT_BUMP(dmu_tx_delay);

	mutex_exit(&tx->tx_sync_lock);
}

void
txg_wait_synced(dsl_pool_t *dp, uint64_t txg)
{
	tx_state_t *tx = &dp->dp_tx;

	ASSERT(!dsl_pool_config_held(dp));

	mutex_enter(&tx->tx_sync_lock);
	ASSERT(tx->tx_threads == 2);
	if (txg == 0)
		txg = tx->tx_open_txg + TXG_DEFER_SIZE;
	if (tx->tx_sync_txg_waiting < txg)
		tx->tx_sync_txg_waiting = txg;
	dprintf("txg=%llu quiesce_txg=%llu sync_txg=%llu\n",
	    txg, tx->tx_quiesce_txg_waiting, tx->tx_sync_txg_waiting);
	while (tx->tx_synced_txg < txg) {
		dprintf("broadcasting sync more "
		    "tx_synced=%llu waiting=%llu dp=%p\n",
		    tx->tx_synced_txg, tx->tx_sync_txg_waiting, dp);
		cv_broadcast(&tx->tx_sync_more_cv);
		cv_wait(&tx->tx_sync_done_cv, &tx->tx_sync_lock);
	}
	mutex_exit(&tx->tx_sync_lock);
}

void
txg_wait_open(dsl_pool_t *dp, uint64_t txg)
{
	tx_state_t *tx = &dp->dp_tx;

	ASSERT(!dsl_pool_config_held(dp));

	mutex_enter(&tx->tx_sync_lock);
	ASSERT(tx->tx_threads == 2);
	if (txg == 0)
		txg = tx->tx_open_txg + 1;
	if (tx->tx_quiesce_txg_waiting < txg)
		tx->tx_quiesce_txg_waiting = txg;
	dprintf("txg=%llu quiesce_txg=%llu sync_txg=%llu\n",
	    txg, tx->tx_quiesce_txg_waiting, tx->tx_sync_txg_waiting);
	while (tx->tx_open_txg < txg) {
		cv_broadcast(&tx->tx_quiesce_more_cv);
		cv_wait(&tx->tx_quiesce_done_cv, &tx->tx_sync_lock);
	}
	mutex_exit(&tx->tx_sync_lock);
}

/*
 * If there isn't a txg syncing or in the pipeline, push another txg through
 * the pipeline by queiscing the open txg.
 */
void
txg_kick(dsl_pool_t *dp)
{
	tx_state_t *tx = &dp->dp_tx;

	ASSERT(!dsl_pool_config_held(dp));

	mutex_enter(&tx->tx_sync_lock);
	if (tx->tx_syncing_txg == 0 &&
	    tx->tx_quiesce_txg_waiting <= tx->tx_open_txg &&
	    tx->tx_sync_txg_waiting <= tx->tx_synced_txg &&
	    tx->tx_quiesced_txg <= tx->tx_synced_txg) {
		tx->tx_quiesce_txg_waiting = tx->tx_open_txg + 1;
		cv_broadcast(&tx->tx_quiesce_more_cv);
	}
	mutex_exit(&tx->tx_sync_lock);
}

boolean_t
txg_stalled(dsl_pool_t *dp)
{
	tx_state_t *tx = &dp->dp_tx;
	return (tx->tx_quiesce_txg_waiting > tx->tx_open_txg);
}

boolean_t
txg_sync_waiting(dsl_pool_t *dp)
{
	tx_state_t *tx = &dp->dp_tx;

	return (tx->tx_syncing_txg <= tx->tx_sync_txg_waiting ||
	    tx->tx_quiesced_txg != 0);
}

/*
 * Verify that this txg is active (open, quiescing, syncing).  Non-active
 * txg's should not be manipulated.
 */
void
txg_verify(spa_t *spa, uint64_t txg)
{
	ASSERTV(dsl_pool_t *dp = spa_get_dsl(spa));
	if (txg <= TXG_INITIAL || txg == ZILTEST_TXG)
		return;
	ASSERT3U(txg, <=, dp->dp_tx.tx_open_txg);
	ASSERT3U(txg, >=, dp->dp_tx.tx_synced_txg);
	ASSERT3U(txg, >=, dp->dp_tx.tx_open_txg - TXG_CONCURRENT_STATES);
}

/*
 * Per-txg object lists.
 */
void
txg_list_create(txg_list_t *tl, spa_t *spa, size_t offset)
{
	int t;

	mutex_init(&tl->tl_lock, NULL, MUTEX_DEFAULT, NULL);

	tl->tl_offset = offset;
	tl->tl_spa = spa;

	for (t = 0; t < TXG_SIZE; t++)
		tl->tl_head[t] = NULL;
}

void
txg_list_destroy(txg_list_t *tl)
{
	int t;

	for (t = 0; t < TXG_SIZE; t++)
		ASSERT(txg_list_empty(tl, t));

	mutex_destroy(&tl->tl_lock);
}

boolean_t
txg_list_empty(txg_list_t *tl, uint64_t txg)
{
	txg_verify(tl->tl_spa, txg);
	return (tl->tl_head[txg & TXG_MASK] == NULL);
}

/*
 * Returns true if all txg lists are empty.
 *
 * Warning: this is inherently racy (an item could be added immediately
 * after this function returns). We don't bother with the lock because
 * it wouldn't change the semantics.
 */
boolean_t
txg_all_lists_empty(txg_list_t *tl)
{
	int i;

	for (i = 0; i < TXG_SIZE; i++) {
		if (!txg_list_empty(tl, i)) {
			return (B_FALSE);
		}
	}
	return (B_TRUE);
}

/*
 * Add an entry to the list (unless it's already on the list).
 * Returns B_TRUE if it was actually added.
 */
boolean_t
txg_list_add(txg_list_t *tl, void *p, uint64_t txg)
{
	int t = txg & TXG_MASK;
	txg_node_t *tn = (txg_node_t *)((char *)p + tl->tl_offset);
	boolean_t add;

	txg_verify(tl->tl_spa, txg);
	mutex_enter(&tl->tl_lock);
	add = (tn->tn_member[t] == 0);
	if (add) {
		tn->tn_member[t] = 1;
		tn->tn_next[t] = tl->tl_head[t];
		tl->tl_head[t] = tn;
	}
	mutex_exit(&tl->tl_lock);

	return (add);
}

/*
 * Add an entry to the end of the list, unless it's already on the list.
 * (walks list to find end)
 * Returns B_TRUE if it was actually added.
 */
boolean_t
txg_list_add_tail(txg_list_t *tl, void *p, uint64_t txg)
{
	int t = txg & TXG_MASK;
	txg_node_t *tn = (txg_node_t *)((char *)p + tl->tl_offset);
	boolean_t add;

	txg_verify(tl->tl_spa, txg);
	mutex_enter(&tl->tl_lock);
	add = (tn->tn_member[t] == 0);
	if (add) {
		txg_node_t **tp;

		for (tp = &tl->tl_head[t]; *tp != NULL; tp = &(*tp)->tn_next[t])
			continue;

		tn->tn_member[t] = 1;
		tn->tn_next[t] = NULL;
		*tp = tn;
	}
	mutex_exit(&tl->tl_lock);

	return (add);
}

/*
 * Remove the head of the list and return it.
 */
void *
txg_list_remove(txg_list_t *tl, uint64_t txg)
{
	int t = txg & TXG_MASK;
	txg_node_t *tn;
	void *p = NULL;

	txg_verify(tl->tl_spa, txg);
	mutex_enter(&tl->tl_lock);
	if ((tn = tl->tl_head[t]) != NULL) {
		p = (char *)tn - tl->tl_offset;
		tl->tl_head[t] = tn->tn_next[t];
		tn->tn_next[t] = NULL;
		tn->tn_member[t] = 0;
	}
	mutex_exit(&tl->tl_lock);

	return (p);
}

/*
 * Remove a specific item from the list and return it.
 */
void *
txg_list_remove_this(txg_list_t *tl, void *p, uint64_t txg)
{
	int t = txg & TXG_MASK;
	txg_node_t *tn, **tp;

	txg_verify(tl->tl_spa, txg);
	mutex_enter(&tl->tl_lock);

	for (tp = &tl->tl_head[t]; (tn = *tp) != NULL; tp = &tn->tn_next[t]) {
		if ((char *)tn - tl->tl_offset == p) {
			*tp = tn->tn_next[t];
			tn->tn_next[t] = NULL;
			tn->tn_member[t] = 0;
			mutex_exit(&tl->tl_lock);
			return (p);
		}
	}

	mutex_exit(&tl->tl_lock);

	return (NULL);
}

boolean_t
txg_list_member(txg_list_t *tl, void *p, uint64_t txg)
{
	int t = txg & TXG_MASK;
	txg_node_t *tn = (txg_node_t *)((char *)p + tl->tl_offset);

	txg_verify(tl->tl_spa, txg);
	return (tn->tn_member[t] != 0);
}

/*
 * Walk a txg list -- only safe if you know it's not changing.
 */
void *
txg_list_head(txg_list_t *tl, uint64_t txg)
{
	int t = txg & TXG_MASK;
	txg_node_t *tn = tl->tl_head[t];

	txg_verify(tl->tl_spa, txg);
	return (tn == NULL ? NULL : (char *)tn - tl->tl_offset);
}

void *
txg_list_next(txg_list_t *tl, void *p, uint64_t txg)
{
	int t = txg & TXG_MASK;
	txg_node_t *tn = (txg_node_t *)((char *)p + tl->tl_offset);

	txg_verify(tl->tl_spa, txg);
	tn = tn->tn_next[t];

	return (tn == NULL ? NULL : (char *)tn - tl->tl_offset);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(txg_init);
EXPORT_SYMBOL(txg_fini);
EXPORT_SYMBOL(txg_sync_start);
EXPORT_SYMBOL(txg_sync_stop);
EXPORT_SYMBOL(txg_hold_open);
EXPORT_SYMBOL(txg_rele_to_quiesce);
EXPORT_SYMBOL(txg_rele_to_sync);
EXPORT_SYMBOL(txg_register_callbacks);
EXPORT_SYMBOL(txg_delay);
EXPORT_SYMBOL(txg_wait_synced);
EXPORT_SYMBOL(txg_wait_open);
EXPORT_SYMBOL(txg_wait_callbacks);
EXPORT_SYMBOL(txg_stalled);
EXPORT_SYMBOL(txg_sync_waiting);

module_param(zfs_txg_timeout, int, 0644);
MODULE_PARM_DESC(zfs_txg_timeout, "Max seconds worth of delta per txg");
#endif
