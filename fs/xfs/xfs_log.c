// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_log_recover.h"
#include "xfs_inode.h"
#include "xfs_trace.h"
#include "xfs_fsops.h"
#include "xfs_cksum.h"
#include "xfs_sysfs.h"
#include "xfs_sb.h"
#include "xfs_health.h"

kmem_zone_t	*xfs_log_ticket_zone;

/* Local miscellaneous function prototypes */
STATIC int
xlog_commit_record(
	struct xlog		*log,
	struct xlog_ticket	*ticket,
	struct xlog_in_core	**iclog,
	xfs_lsn_t		*commitlsnp);

STATIC struct xlog *
xlog_alloc_log(
	struct xfs_mount	*mp,
	struct xfs_buftarg	*log_target,
	xfs_daddr_t		blk_offset,
	int			num_bblks);
STATIC int
xlog_space_left(
	struct xlog		*log,
	atomic64_t		*head);
STATIC void
xlog_dealloc_log(
	struct xlog		*log);

/* local state machine functions */
STATIC void xlog_state_done_syncing(xlog_in_core_t *iclog, int);
STATIC void
xlog_state_do_callback(
	struct xlog		*log,
	int			aborted,
	struct xlog_in_core	*iclog);
STATIC int
xlog_state_get_iclog_space(
	struct xlog		*log,
	int			len,
	struct xlog_in_core	**iclog,
	struct xlog_ticket	*ticket,
	int			*continued_write,
	int			*logoffsetp);
STATIC int
xlog_state_release_iclog(
	struct xlog		*log,
	struct xlog_in_core	*iclog);
STATIC void
xlog_state_switch_iclogs(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	int			eventual_size);
STATIC void
xlog_state_want_sync(
	struct xlog		*log,
	struct xlog_in_core	*iclog);

STATIC void
xlog_grant_push_ail(
	struct xlog		*log,
	int			need_bytes);
STATIC void
xlog_regrant_reserve_log_space(
	struct xlog		*log,
	struct xlog_ticket	*ticket);
STATIC void
xlog_ungrant_log_space(
	struct xlog		*log,
	struct xlog_ticket	*ticket);

#if defined(DEBUG)
STATIC void
xlog_verify_dest_ptr(
	struct xlog		*log,
	void			*ptr);
STATIC void
xlog_verify_grant_tail(
	struct xlog *log);
STATIC void
xlog_verify_iclog(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	int			count,
	bool                    syncing);
STATIC void
xlog_verify_tail_lsn(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	xfs_lsn_t		tail_lsn);
#else
#define xlog_verify_dest_ptr(a,b)
#define xlog_verify_grant_tail(a)
#define xlog_verify_iclog(a,b,c,d)
#define xlog_verify_tail_lsn(a,b,c)
#endif

STATIC int
xlog_iclogs_empty(
	struct xlog		*log);

static void
xlog_grant_sub_space(
	struct xlog		*log,
	atomic64_t		*head,
	int			bytes)
{
	int64_t	head_val = atomic64_read(head);
	int64_t new, old;

	do {
		int	cycle, space;

		xlog_crack_grant_head_val(head_val, &cycle, &space);

		space -= bytes;
		if (space < 0) {
			space += log->l_logsize;
			cycle--;
		}

		old = head_val;
		new = xlog_assign_grant_head_val(cycle, space);
		head_val = atomic64_cmpxchg(head, old, new);
	} while (head_val != old);
}

static void
xlog_grant_add_space(
	struct xlog		*log,
	atomic64_t		*head,
	int			bytes)
{
	int64_t	head_val = atomic64_read(head);
	int64_t new, old;

	do {
		int		tmp;
		int		cycle, space;

		xlog_crack_grant_head_val(head_val, &cycle, &space);

		tmp = log->l_logsize - space;
		if (tmp > bytes)
			space += bytes;
		else {
			space = bytes - tmp;
			cycle++;
		}

		old = head_val;
		new = xlog_assign_grant_head_val(cycle, space);
		head_val = atomic64_cmpxchg(head, old, new);
	} while (head_val != old);
}

STATIC void
xlog_grant_head_init(
	struct xlog_grant_head	*head)
{
	xlog_assign_grant_head(&head->grant, 1, 0);
	INIT_LIST_HEAD(&head->waiters);
	spin_lock_init(&head->lock);
}

STATIC void
xlog_grant_head_wake_all(
	struct xlog_grant_head	*head)
{
	struct xlog_ticket	*tic;

	spin_lock(&head->lock);
	list_for_each_entry(tic, &head->waiters, t_queue)
		wake_up_process(tic->t_task);
	spin_unlock(&head->lock);
}

static inline int
xlog_ticket_reservation(
	struct xlog		*log,
	struct xlog_grant_head	*head,
	struct xlog_ticket	*tic)
{
	if (head == &log->l_write_head) {
		ASSERT(tic->t_flags & XLOG_TIC_PERM_RESERV);
		return tic->t_unit_res;
	} else {
		if (tic->t_flags & XLOG_TIC_PERM_RESERV)
			return tic->t_unit_res * tic->t_cnt;
		else
			return tic->t_unit_res;
	}
}

STATIC bool
xlog_grant_head_wake(
	struct xlog		*log,
	struct xlog_grant_head	*head,
	int			*free_bytes)
{
	struct xlog_ticket	*tic;
	int			need_bytes;

	list_for_each_entry(tic, &head->waiters, t_queue) {
		need_bytes = xlog_ticket_reservation(log, head, tic);
		if (*free_bytes < need_bytes)
			return false;

		*free_bytes -= need_bytes;
		trace_xfs_log_grant_wake_up(log, tic);
		wake_up_process(tic->t_task);
	}

	return true;
}

STATIC int
xlog_grant_head_wait(
	struct xlog		*log,
	struct xlog_grant_head	*head,
	struct xlog_ticket	*tic,
	int			need_bytes) __releases(&head->lock)
					    __acquires(&head->lock)
{
	list_add_tail(&tic->t_queue, &head->waiters);

	do {
		if (XLOG_FORCED_SHUTDOWN(log))
			goto shutdown;
		xlog_grant_push_ail(log, need_bytes);

		__set_current_state(TASK_UNINTERRUPTIBLE);
		spin_unlock(&head->lock);

		XFS_STATS_INC(log->l_mp, xs_sleep_logspace);

		trace_xfs_log_grant_sleep(log, tic);
		schedule();
		trace_xfs_log_grant_wake(log, tic);

		spin_lock(&head->lock);
		if (XLOG_FORCED_SHUTDOWN(log))
			goto shutdown;
	} while (xlog_space_left(log, &head->grant) < need_bytes);

	list_del_init(&tic->t_queue);
	return 0;
shutdown:
	list_del_init(&tic->t_queue);
	return -EIO;
}

/*
 * Atomically get the log space required for a log ticket.
 *
 * Once a ticket gets put onto head->waiters, it will only return after the
 * needed reservation is satisfied.
 *
 * This function is structured so that it has a lock free fast path. This is
 * necessary because every new transaction reservation will come through this
 * path. Hence any lock will be globally hot if we take it unconditionally on
 * every pass.
 *
 * As tickets are only ever moved on and off head->waiters under head->lock, we
 * only need to take that lock if we are going to add the ticket to the queue
 * and sleep. We can avoid taking the lock if the ticket was never added to
 * head->waiters because the t_queue list head will be empty and we hold the
 * only reference to it so it can safely be checked unlocked.
 */
STATIC int
xlog_grant_head_check(
	struct xlog		*log,
	struct xlog_grant_head	*head,
	struct xlog_ticket	*tic,
	int			*need_bytes)
{
	int			free_bytes;
	int			error = 0;

	ASSERT(!(log->l_flags & XLOG_ACTIVE_RECOVERY));

	/*
	 * If there are other waiters on the queue then give them a chance at
	 * logspace before us.  Wake up the first waiters, if we do not wake
	 * up all the waiters then go to sleep waiting for more free space,
	 * otherwise try to get some space for this transaction.
	 */
	*need_bytes = xlog_ticket_reservation(log, head, tic);
	free_bytes = xlog_space_left(log, &head->grant);
	if (!list_empty_careful(&head->waiters)) {
		spin_lock(&head->lock);
		if (!xlog_grant_head_wake(log, head, &free_bytes) ||
		    free_bytes < *need_bytes) {
			error = xlog_grant_head_wait(log, head, tic,
						     *need_bytes);
		}
		spin_unlock(&head->lock);
	} else if (free_bytes < *need_bytes) {
		spin_lock(&head->lock);
		error = xlog_grant_head_wait(log, head, tic, *need_bytes);
		spin_unlock(&head->lock);
	}

	return error;
}

static void
xlog_tic_reset_res(xlog_ticket_t *tic)
{
	tic->t_res_num = 0;
	tic->t_res_arr_sum = 0;
	tic->t_res_num_ophdrs = 0;
}

static void
xlog_tic_add_region(xlog_ticket_t *tic, uint len, uint type)
{
	if (tic->t_res_num == XLOG_TIC_LEN_MAX) {
		/* add to overflow and start again */
		tic->t_res_o_flow += tic->t_res_arr_sum;
		tic->t_res_num = 0;
		tic->t_res_arr_sum = 0;
	}

	tic->t_res_arr[tic->t_res_num].r_len = len;
	tic->t_res_arr[tic->t_res_num].r_type = type;
	tic->t_res_arr_sum += len;
	tic->t_res_num++;
}

/*
 * Replenish the byte reservation required by moving the grant write head.
 */
int
xfs_log_regrant(
	struct xfs_mount	*mp,
	struct xlog_ticket	*tic)
{
	struct xlog		*log = mp->m_log;
	int			need_bytes;
	int			error = 0;

	if (XLOG_FORCED_SHUTDOWN(log))
		return -EIO;

	XFS_STATS_INC(mp, xs_try_logspace);

	/*
	 * This is a new transaction on the ticket, so we need to change the
	 * transaction ID so that the next transaction has a different TID in
	 * the log. Just add one to the existing tid so that we can see chains
	 * of rolling transactions in the log easily.
	 */
	tic->t_tid++;

	xlog_grant_push_ail(log, tic->t_unit_res);

	tic->t_curr_res = tic->t_unit_res;
	xlog_tic_reset_res(tic);

	if (tic->t_cnt > 0)
		return 0;

	trace_xfs_log_regrant(log, tic);

	error = xlog_grant_head_check(log, &log->l_write_head, tic,
				      &need_bytes);
	if (error)
		goto out_error;

	xlog_grant_add_space(log, &log->l_write_head.grant, need_bytes);
	trace_xfs_log_regrant_exit(log, tic);
	xlog_verify_grant_tail(log);
	return 0;

out_error:
	/*
	 * If we are failing, make sure the ticket doesn't have any current
	 * reservations.  We don't want to add this back when the ticket/
	 * transaction gets cancelled.
	 */
	tic->t_curr_res = 0;
	tic->t_cnt = 0;	/* ungrant will give back unit_res * t_cnt. */
	return error;
}

/*
 * Reserve log space and return a ticket corresponding to the reservation.
 *
 * Each reservation is going to reserve extra space for a log record header.
 * When writes happen to the on-disk log, we don't subtract the length of the
 * log record header from any reservation.  By wasting space in each
 * reservation, we prevent over allocation problems.
 */
int
xfs_log_reserve(
	struct xfs_mount	*mp,
	int		 	unit_bytes,
	int		 	cnt,
	struct xlog_ticket	**ticp,
	uint8_t		 	client,
	bool			permanent)
{
	struct xlog		*log = mp->m_log;
	struct xlog_ticket	*tic;
	int			need_bytes;
	int			error = 0;

	ASSERT(client == XFS_TRANSACTION || client == XFS_LOG);

	if (XLOG_FORCED_SHUTDOWN(log))
		return -EIO;

	XFS_STATS_INC(mp, xs_try_logspace);

	ASSERT(*ticp == NULL);
	tic = xlog_ticket_alloc(log, unit_bytes, cnt, client, permanent,
				KM_SLEEP | KM_MAYFAIL);
	if (!tic)
		return -ENOMEM;

	*ticp = tic;

	xlog_grant_push_ail(log, tic->t_cnt ? tic->t_unit_res * tic->t_cnt
					    : tic->t_unit_res);

	trace_xfs_log_reserve(log, tic);

	error = xlog_grant_head_check(log, &log->l_reserve_head, tic,
				      &need_bytes);
	if (error)
		goto out_error;

	xlog_grant_add_space(log, &log->l_reserve_head.grant, need_bytes);
	xlog_grant_add_space(log, &log->l_write_head.grant, need_bytes);
	trace_xfs_log_reserve_exit(log, tic);
	xlog_verify_grant_tail(log);
	return 0;

out_error:
	/*
	 * If we are failing, make sure the ticket doesn't have any current
	 * reservations.  We don't want to add this back when the ticket/
	 * transaction gets cancelled.
	 */
	tic->t_curr_res = 0;
	tic->t_cnt = 0;	/* ungrant will give back unit_res * t_cnt. */
	return error;
}


/*
 * NOTES:
 *
 *	1. currblock field gets updated at startup and after in-core logs
 *		marked as with WANT_SYNC.
 */

/*
 * This routine is called when a user of a log manager ticket is done with
 * the reservation.  If the ticket was ever used, then a commit record for
 * the associated transaction is written out as a log operation header with
 * no data.  The flag XLOG_TIC_INITED is set when the first write occurs with
 * a given ticket.  If the ticket was one with a permanent reservation, then
 * a few operations are done differently.  Permanent reservation tickets by
 * default don't release the reservation.  They just commit the current
 * transaction with the belief that the reservation is still needed.  A flag
 * must be passed in before permanent reservations are actually released.
 * When these type of tickets are not released, they need to be set into
 * the inited state again.  By doing this, a start record will be written
 * out when the next write occurs.
 */
xfs_lsn_t
xfs_log_done(
	struct xfs_mount	*mp,
	struct xlog_ticket	*ticket,
	struct xlog_in_core	**iclog,
	bool			regrant)
{
	struct xlog		*log = mp->m_log;
	xfs_lsn_t		lsn = 0;

	if (XLOG_FORCED_SHUTDOWN(log) ||
	    /*
	     * If nothing was ever written, don't write out commit record.
	     * If we get an error, just continue and give back the log ticket.
	     */
	    (((ticket->t_flags & XLOG_TIC_INITED) == 0) &&
	     (xlog_commit_record(log, ticket, iclog, &lsn)))) {
		lsn = (xfs_lsn_t) -1;
		regrant = false;
	}


	if (!regrant) {
		trace_xfs_log_done_nonperm(log, ticket);

		/*
		 * Release ticket if not permanent reservation or a specific
		 * request has been made to release a permanent reservation.
		 */
		xlog_ungrant_log_space(log, ticket);
	} else {
		trace_xfs_log_done_perm(log, ticket);

		xlog_regrant_reserve_log_space(log, ticket);
		/* If this ticket was a permanent reservation and we aren't
		 * trying to release it, reset the inited flags; so next time
		 * we write, a start record will be written out.
		 */
		ticket->t_flags |= XLOG_TIC_INITED;
	}

	xfs_log_ticket_put(ticket);
	return lsn;
}

/*
 * Attaches a new iclog I/O completion callback routine during
 * transaction commit.  If the log is in error state, a non-zero
 * return code is handed back and the caller is responsible for
 * executing the callback at an appropriate time.
 */
int
xfs_log_notify(
	struct xlog_in_core	*iclog,
	xfs_log_callback_t	*cb)
{
	int	abortflg;

	spin_lock(&iclog->ic_callback_lock);
	abortflg = (iclog->ic_state & XLOG_STATE_IOERROR);
	if (!abortflg) {
		ASSERT_ALWAYS((iclog->ic_state == XLOG_STATE_ACTIVE) ||
			      (iclog->ic_state == XLOG_STATE_WANT_SYNC));
		cb->cb_next = NULL;
		*(iclog->ic_callback_tail) = cb;
		iclog->ic_callback_tail = &(cb->cb_next);
	}
	spin_unlock(&iclog->ic_callback_lock);
	return abortflg;
}

int
xfs_log_release_iclog(
	struct xfs_mount	*mp,
	struct xlog_in_core	*iclog)
{
	if (xlog_state_release_iclog(mp->m_log, iclog)) {
		xfs_force_shutdown(mp, SHUTDOWN_LOG_IO_ERROR);
		return -EIO;
	}

	return 0;
}

/*
 * Mount a log filesystem
 *
 * mp		- ubiquitous xfs mount point structure
 * log_target	- buftarg of on-disk log device
 * blk_offset	- Start block # where block size is 512 bytes (BBSIZE)
 * num_bblocks	- Number of BBSIZE blocks in on-disk log
 *
 * Return error or zero.
 */
int
xfs_log_mount(
	xfs_mount_t	*mp,
	xfs_buftarg_t	*log_target,
	xfs_daddr_t	blk_offset,
	int		num_bblks)
{
	bool		fatal = xfs_sb_version_hascrc(&mp->m_sb);
	int		error = 0;
	int		min_logfsbs;

	if (!(mp->m_flags & XFS_MOUNT_NORECOVERY)) {
		xfs_notice(mp, "Mounting V%d Filesystem",
			   XFS_SB_VERSION_NUM(&mp->m_sb));
	} else {
		xfs_notice(mp,
"Mounting V%d filesystem in no-recovery mode. Filesystem will be inconsistent.",
			   XFS_SB_VERSION_NUM(&mp->m_sb));
		ASSERT(mp->m_flags & XFS_MOUNT_RDONLY);
	}

	mp->m_log = xlog_alloc_log(mp, log_target, blk_offset, num_bblks);
	if (IS_ERR(mp->m_log)) {
		error = PTR_ERR(mp->m_log);
		goto out;
	}

	/*
	 * Validate the given log space and drop a critical message via syslog
	 * if the log size is too small that would lead to some unexpected
	 * situations in transaction log space reservation stage.
	 *
	 * Note: we can't just reject the mount if the validation fails.  This
	 * would mean that people would have to downgrade their kernel just to
	 * remedy the situation as there is no way to grow the log (short of
	 * black magic surgery with xfs_db).
	 *
	 * We can, however, reject mounts for CRC format filesystems, as the
	 * mkfs binary being used to make the filesystem should never create a
	 * filesystem with a log that is too small.
	 */
	min_logfsbs = xfs_log_calc_minimum_size(mp);

	if (mp->m_sb.sb_logblocks < min_logfsbs) {
		xfs_warn(mp,
		"Log size %d blocks too small, minimum size is %d blocks",
			 mp->m_sb.sb_logblocks, min_logfsbs);
		error = -EINVAL;
	} else if (mp->m_sb.sb_logblocks > XFS_MAX_LOG_BLOCKS) {
		xfs_warn(mp,
		"Log size %d blocks too large, maximum size is %lld blocks",
			 mp->m_sb.sb_logblocks, XFS_MAX_LOG_BLOCKS);
		error = -EINVAL;
	} else if (XFS_FSB_TO_B(mp, mp->m_sb.sb_logblocks) > XFS_MAX_LOG_BYTES) {
		xfs_warn(mp,
		"log size %lld bytes too large, maximum size is %lld bytes",
			 XFS_FSB_TO_B(mp, mp->m_sb.sb_logblocks),
			 XFS_MAX_LOG_BYTES);
		error = -EINVAL;
	} else if (mp->m_sb.sb_logsunit > 1 &&
		   mp->m_sb.sb_logsunit % mp->m_sb.sb_blocksize) {
		xfs_warn(mp,
		"log stripe unit %u bytes must be a multiple of block size",
			 mp->m_sb.sb_logsunit);
		error = -EINVAL;
		fatal = true;
	}
	if (error) {
		/*
		 * Log check errors are always fatal on v5; or whenever bad
		 * metadata leads to a crash.
		 */
		if (fatal) {
			xfs_crit(mp, "AAIEEE! Log failed size checks. Abort!");
			ASSERT(0);
			goto out_free_log;
		}
		xfs_crit(mp, "Log size out of supported range.");
		xfs_crit(mp,
"Continuing onwards, but if log hangs are experienced then please report this message in the bug report.");
	}

	/*
	 * Initialize the AIL now we have a log.
	 */
	error = xfs_trans_ail_init(mp);
	if (error) {
		xfs_warn(mp, "AIL initialisation failed: error %d", error);
		goto out_free_log;
	}
	mp->m_log->l_ailp = mp->m_ail;

	/*
	 * skip log recovery on a norecovery mount.  pretend it all
	 * just worked.
	 */
	if (!(mp->m_flags & XFS_MOUNT_NORECOVERY)) {
		int	readonly = (mp->m_flags & XFS_MOUNT_RDONLY);

		if (readonly)
			mp->m_flags &= ~XFS_MOUNT_RDONLY;

		error = xlog_recover(mp->m_log);

		if (readonly)
			mp->m_flags |= XFS_MOUNT_RDONLY;
		if (error) {
			xfs_warn(mp, "log mount/recovery failed: error %d",
				error);
			xlog_recover_cancel(mp->m_log);
			goto out_destroy_ail;
		}
	}

	error = xfs_sysfs_init(&mp->m_log->l_kobj, &xfs_log_ktype, &mp->m_kobj,
			       "log");
	if (error)
		goto out_destroy_ail;

	/* Normal transactions can now occur */
	mp->m_log->l_flags &= ~XLOG_ACTIVE_RECOVERY;

	/*
	 * Now the log has been fully initialised and we know were our
	 * space grant counters are, we can initialise the permanent ticket
	 * needed for delayed logging to work.
	 */
	xlog_cil_init_post_recovery(mp->m_log);

	return 0;

out_destroy_ail:
	xfs_trans_ail_destroy(mp);
out_free_log:
	xlog_dealloc_log(mp->m_log);
out:
	return error;
}

/*
 * Finish the recovery of the file system.  This is separate from the
 * xfs_log_mount() call, because it depends on the code in xfs_mountfs() to read
 * in the root and real-time bitmap inodes between calling xfs_log_mount() and
 * here.
 *
 * If we finish recovery successfully, start the background log work. If we are
 * not doing recovery, then we have a RO filesystem and we don't need to start
 * it.
 */
int
xfs_log_mount_finish(
	struct xfs_mount	*mp)
{
	int	error = 0;
	bool	readonly = (mp->m_flags & XFS_MOUNT_RDONLY);
	bool	recovered = mp->m_log->l_flags & XLOG_RECOVERY_NEEDED;

	if (mp->m_flags & XFS_MOUNT_NORECOVERY) {
		ASSERT(mp->m_flags & XFS_MOUNT_RDONLY);
		return 0;
	} else if (readonly) {
		/* Allow unlinked processing to proceed */
		mp->m_flags &= ~XFS_MOUNT_RDONLY;
	}

	/*
	 * During the second phase of log recovery, we need iget and
	 * iput to behave like they do for an active filesystem.
	 * xfs_fs_drop_inode needs to be able to prevent the deletion
	 * of inodes before we're done replaying log items on those
	 * inodes.  Turn it off immediately after recovery finishes
	 * so that we don't leak the quota inodes if subsequent mount
	 * activities fail.
	 *
	 * We let all inodes involved in redo item processing end up on
	 * the LRU instead of being evicted immediately so that if we do
	 * something to an unlinked inode, the irele won't cause
	 * premature truncation and freeing of the inode, which results
	 * in log recovery failure.  We have to evict the unreferenced
	 * lru inodes after clearing SB_ACTIVE because we don't
	 * otherwise clean up the lru if there's a subsequent failure in
	 * xfs_mountfs, which leads to us leaking the inodes if nothing
	 * else (e.g. quotacheck) references the inodes before the
	 * mount failure occurs.
	 */
	mp->m_super->s_flags |= SB_ACTIVE;
	error = xlog_recover_finish(mp->m_log);
	if (!error)
		xfs_log_work_queue(mp);
	mp->m_super->s_flags &= ~SB_ACTIVE;
	evict_inodes(mp->m_super);

	/*
	 * Drain the buffer LRU after log recovery. This is required for v4
	 * filesystems to avoid leaving around buffers with NULL verifier ops,
	 * but we do it unconditionally to make sure we're always in a clean
	 * cache state after mount.
	 *
	 * Don't push in the error case because the AIL may have pending intents
	 * that aren't removed until recovery is cancelled.
	 */
	if (!error && recovered) {
		xfs_log_force(mp, XFS_LOG_SYNC);
		xfs_ail_push_all_sync(mp->m_ail);
	}
	xfs_wait_buftarg(mp->m_ddev_targp);

	if (readonly)
		mp->m_flags |= XFS_MOUNT_RDONLY;

	return error;
}

/*
 * The mount has failed. Cancel the recovery if it hasn't completed and destroy
 * the log.
 */
int
xfs_log_mount_cancel(
	struct xfs_mount	*mp)
{
	int			error;

	error = xlog_recover_cancel(mp->m_log);
	xfs_log_unmount(mp);

	return error;
}

/*
 * Final log writes as part of unmount.
 *
 * Mark the filesystem clean as unmount happens.  Note that during relocation
 * this routine needs to be executed as part of source-bag while the
 * deallocation must not be done until source-end.
 */

/* Actually write the unmount record to disk. */
static void
xfs_log_write_unmount_record(
	struct xfs_mount	*mp)
{
	/* the data section must be 32 bit size aligned */
	struct xfs_unmount_log_format magic = {
		.magic = XLOG_UNMOUNT_TYPE,
	};
	struct xfs_log_iovec reg = {
		.i_addr = &magic,
		.i_len = sizeof(magic),
		.i_type = XLOG_REG_TYPE_UNMOUNT,
	};
	struct xfs_log_vec vec = {
		.lv_niovecs = 1,
		.lv_iovecp = &reg,
	};
	struct xlog		*log = mp->m_log;
	struct xlog_in_core	*iclog;
	struct xlog_ticket	*tic = NULL;
	xfs_lsn_t		lsn;
	uint			flags = XLOG_UNMOUNT_TRANS;
	int			error;

	error = xfs_log_reserve(mp, 600, 1, &tic, XFS_LOG, 0);
	if (error)
		goto out_err;

	/*
	 * If we think the summary counters are bad, clear the unmount header
	 * flag in the unmount record so that the summary counters will be
	 * recalculated during log recovery at next mount.  Refer to
	 * xlog_check_unmount_rec for more details.
	 */
	if (XFS_TEST_ERROR(xfs_fs_has_sickness(mp, XFS_SICK_FS_COUNTERS), mp,
			XFS_ERRTAG_FORCE_SUMMARY_RECALC)) {
		xfs_alert(mp, "%s: will fix summary counters at next mount",
				__func__);
		flags &= ~XLOG_UNMOUNT_TRANS;
	}

	/* remove inited flag, and account for space used */
	tic->t_flags = 0;
	tic->t_curr_res -= sizeof(magic);
	error = xlog_write(log, &vec, tic, &lsn, NULL, flags);
	/*
	 * At this point, we're umounting anyway, so there's no point in
	 * transitioning log state to IOERROR. Just continue...
	 */
out_err:
	if (error)
		xfs_alert(mp, "%s: unmount record failed", __func__);

	spin_lock(&log->l_icloglock);
	iclog = log->l_iclog;
	atomic_inc(&iclog->ic_refcnt);
	xlog_state_want_sync(log, iclog);
	spin_unlock(&log->l_icloglock);
	error = xlog_state_release_iclog(log, iclog);

	spin_lock(&log->l_icloglock);
	switch (iclog->ic_state) {
	default:
		if (!XLOG_FORCED_SHUTDOWN(log)) {
			xlog_wait(&iclog->ic_force_wait, &log->l_icloglock);
			break;
		}
		/* fall through */
	case XLOG_STATE_ACTIVE:
	case XLOG_STATE_DIRTY:
		spin_unlock(&log->l_icloglock);
		break;
	}

	if (tic) {
		trace_xfs_log_umount_write(log, tic);
		xlog_ungrant_log_space(log, tic);
		xfs_log_ticket_put(tic);
	}
}

/*
 * Unmount record used to have a string "Unmount filesystem--" in the
 * data section where the "Un" was really a magic number (XLOG_UNMOUNT_TYPE).
 * We just write the magic number now since that particular field isn't
 * currently architecture converted and "Unmount" is a bit foo.
 * As far as I know, there weren't any dependencies on the old behaviour.
 */

static int
xfs_log_unmount_write(xfs_mount_t *mp)
{
	struct xlog	 *log = mp->m_log;
	xlog_in_core_t	 *iclog;
#ifdef DEBUG
	xlog_in_core_t	 *first_iclog;
#endif
	int		 error;

	/*
	 * Don't write out unmount record on norecovery mounts or ro devices.
	 * Or, if we are doing a forced umount (typically because of IO errors).
	 */
	if (mp->m_flags & XFS_MOUNT_NORECOVERY ||
	    xfs_readonly_buftarg(log->l_mp->m_logdev_targp)) {
		ASSERT(mp->m_flags & XFS_MOUNT_RDONLY);
		return 0;
	}

	error = xfs_log_force(mp, XFS_LOG_SYNC);
	ASSERT(error || !(XLOG_FORCED_SHUTDOWN(log)));

#ifdef DEBUG
	first_iclog = iclog = log->l_iclog;
	do {
		if (!(iclog->ic_state & XLOG_STATE_IOERROR)) {
			ASSERT(iclog->ic_state & XLOG_STATE_ACTIVE);
			ASSERT(iclog->ic_offset == 0);
		}
		iclog = iclog->ic_next;
	} while (iclog != first_iclog);
#endif
	if (! (XLOG_FORCED_SHUTDOWN(log))) {
		xfs_log_write_unmount_record(mp);
	} else {
		/*
		 * We're already in forced_shutdown mode, couldn't
		 * even attempt to write out the unmount transaction.
		 *
		 * Go through the motions of sync'ing and releasing
		 * the iclog, even though no I/O will actually happen,
		 * we need to wait for other log I/Os that may already
		 * be in progress.  Do this as a separate section of
		 * code so we'll know if we ever get stuck here that
		 * we're in this odd situation of trying to unmount
		 * a file system that went into forced_shutdown as
		 * the result of an unmount..
		 */
		spin_lock(&log->l_icloglock);
		iclog = log->l_iclog;
		atomic_inc(&iclog->ic_refcnt);

		xlog_state_want_sync(log, iclog);
		spin_unlock(&log->l_icloglock);
		error =  xlog_state_release_iclog(log, iclog);

		spin_lock(&log->l_icloglock);

		if ( ! (   iclog->ic_state == XLOG_STATE_ACTIVE
			|| iclog->ic_state == XLOG_STATE_DIRTY
			|| iclog->ic_state == XLOG_STATE_IOERROR) ) {

				xlog_wait(&iclog->ic_force_wait,
							&log->l_icloglock);
		} else {
			spin_unlock(&log->l_icloglock);
		}
	}

	return error;
}	/* xfs_log_unmount_write */

/*
 * Empty the log for unmount/freeze.
 *
 * To do this, we first need to shut down the background log work so it is not
 * trying to cover the log as we clean up. We then need to unpin all objects in
 * the log so we can then flush them out. Once they have completed their IO and
 * run the callbacks removing themselves from the AIL, we can write the unmount
 * record.
 */
void
xfs_log_quiesce(
	struct xfs_mount	*mp)
{
	cancel_delayed_work_sync(&mp->m_log->l_work);
	xfs_log_force(mp, XFS_LOG_SYNC);

	/*
	 * The superblock buffer is uncached and while xfs_ail_push_all_sync()
	 * will push it, xfs_wait_buftarg() will not wait for it. Further,
	 * xfs_buf_iowait() cannot be used because it was pushed with the
	 * XBF_ASYNC flag set, so we need to use a lock/unlock pair to wait for
	 * the IO to complete.
	 */
	xfs_ail_push_all_sync(mp->m_ail);
	xfs_wait_buftarg(mp->m_ddev_targp);
	xfs_buf_lock(mp->m_sb_bp);
	xfs_buf_unlock(mp->m_sb_bp);

	xfs_log_unmount_write(mp);
}

/*
 * Shut down and release the AIL and Log.
 *
 * During unmount, we need to ensure we flush all the dirty metadata objects
 * from the AIL so that the log is empty before we write the unmount record to
 * the log. Once this is done, we can tear down the AIL and the log.
 */
void
xfs_log_unmount(
	struct xfs_mount	*mp)
{
	xfs_log_quiesce(mp);

	xfs_trans_ail_destroy(mp);

	xfs_sysfs_del(&mp->m_log->l_kobj);

	xlog_dealloc_log(mp->m_log);
}

void
xfs_log_item_init(
	struct xfs_mount	*mp,
	struct xfs_log_item	*item,
	int			type,
	const struct xfs_item_ops *ops)
{
	item->li_mountp = mp;
	item->li_ailp = mp->m_ail;
	item->li_type = type;
	item->li_ops = ops;
	item->li_lv = NULL;

	INIT_LIST_HEAD(&item->li_ail);
	INIT_LIST_HEAD(&item->li_cil);
	INIT_LIST_HEAD(&item->li_bio_list);
	INIT_LIST_HEAD(&item->li_trans);
}

/*
 * Wake up processes waiting for log space after we have moved the log tail.
 */
void
xfs_log_space_wake(
	struct xfs_mount	*mp)
{
	struct xlog		*log = mp->m_log;
	int			free_bytes;

	if (XLOG_FORCED_SHUTDOWN(log))
		return;

	if (!list_empty_careful(&log->l_write_head.waiters)) {
		ASSERT(!(log->l_flags & XLOG_ACTIVE_RECOVERY));

		spin_lock(&log->l_write_head.lock);
		free_bytes = xlog_space_left(log, &log->l_write_head.grant);
		xlog_grant_head_wake(log, &log->l_write_head, &free_bytes);
		spin_unlock(&log->l_write_head.lock);
	}

	if (!list_empty_careful(&log->l_reserve_head.waiters)) {
		ASSERT(!(log->l_flags & XLOG_ACTIVE_RECOVERY));

		spin_lock(&log->l_reserve_head.lock);
		free_bytes = xlog_space_left(log, &log->l_reserve_head.grant);
		xlog_grant_head_wake(log, &log->l_reserve_head, &free_bytes);
		spin_unlock(&log->l_reserve_head.lock);
	}
}

/*
 * Determine if we have a transaction that has gone to disk that needs to be
 * covered. To begin the transition to the idle state firstly the log needs to
 * be idle. That means the CIL, the AIL and the iclogs needs to be empty before
 * we start attempting to cover the log.
 *
 * Only if we are then in a state where covering is needed, the caller is
 * informed that dummy transactions are required to move the log into the idle
 * state.
 *
 * If there are any items in the AIl or CIL, then we do not want to attempt to
 * cover the log as we may be in a situation where there isn't log space
 * available to run a dummy transaction and this can lead to deadlocks when the
 * tail of the log is pinned by an item that is modified in the CIL.  Hence
 * there's no point in running a dummy transaction at this point because we
 * can't start trying to idle the log until both the CIL and AIL are empty.
 */
static int
xfs_log_need_covered(xfs_mount_t *mp)
{
	struct xlog	*log = mp->m_log;
	int		needed = 0;

	if (!xfs_fs_writable(mp, SB_FREEZE_WRITE))
		return 0;

	if (!xlog_cil_empty(log))
		return 0;

	spin_lock(&log->l_icloglock);
	switch (log->l_covered_state) {
	case XLOG_STATE_COVER_DONE:
	case XLOG_STATE_COVER_DONE2:
	case XLOG_STATE_COVER_IDLE:
		break;
	case XLOG_STATE_COVER_NEED:
	case XLOG_STATE_COVER_NEED2:
		if (xfs_ail_min_lsn(log->l_ailp))
			break;
		if (!xlog_iclogs_empty(log))
			break;

		needed = 1;
		if (log->l_covered_state == XLOG_STATE_COVER_NEED)
			log->l_covered_state = XLOG_STATE_COVER_DONE;
		else
			log->l_covered_state = XLOG_STATE_COVER_DONE2;
		break;
	default:
		needed = 1;
		break;
	}
	spin_unlock(&log->l_icloglock);
	return needed;
}

/*
 * We may be holding the log iclog lock upon entering this routine.
 */
xfs_lsn_t
xlog_assign_tail_lsn_locked(
	struct xfs_mount	*mp)
{
	struct xlog		*log = mp->m_log;
	struct xfs_log_item	*lip;
	xfs_lsn_t		tail_lsn;

	assert_spin_locked(&mp->m_ail->ail_lock);

	/*
	 * To make sure we always have a valid LSN for the log tail we keep
	 * track of the last LSN which was committed in log->l_last_sync_lsn,
	 * and use that when the AIL was empty.
	 */
	lip = xfs_ail_min(mp->m_ail);
	if (lip)
		tail_lsn = lip->li_lsn;
	else
		tail_lsn = atomic64_read(&log->l_last_sync_lsn);
	trace_xfs_log_assign_tail_lsn(log, tail_lsn);
	atomic64_set(&log->l_tail_lsn, tail_lsn);
	return tail_lsn;
}

xfs_lsn_t
xlog_assign_tail_lsn(
	struct xfs_mount	*mp)
{
	xfs_lsn_t		tail_lsn;

	spin_lock(&mp->m_ail->ail_lock);
	tail_lsn = xlog_assign_tail_lsn_locked(mp);
	spin_unlock(&mp->m_ail->ail_lock);

	return tail_lsn;
}

/*
 * Return the space in the log between the tail and the head.  The head
 * is passed in the cycle/bytes formal parms.  In the special case where
 * the reserve head has wrapped passed the tail, this calculation is no
 * longer valid.  In this case, just return 0 which means there is no space
 * in the log.  This works for all places where this function is called
 * with the reserve head.  Of course, if the write head were to ever
 * wrap the tail, we should blow up.  Rather than catch this case here,
 * we depend on other ASSERTions in other parts of the code.   XXXmiken
 *
 * This code also handles the case where the reservation head is behind
 * the tail.  The details of this case are described below, but the end
 * result is that we return the size of the log as the amount of space left.
 */
STATIC int
xlog_space_left(
	struct xlog	*log,
	atomic64_t	*head)
{
	int		free_bytes;
	int		tail_bytes;
	int		tail_cycle;
	int		head_cycle;
	int		head_bytes;

	xlog_crack_grant_head(head, &head_cycle, &head_bytes);
	xlog_crack_atomic_lsn(&log->l_tail_lsn, &tail_cycle, &tail_bytes);
	tail_bytes = BBTOB(tail_bytes);
	if (tail_cycle == head_cycle && head_bytes >= tail_bytes)
		free_bytes = log->l_logsize - (head_bytes - tail_bytes);
	else if (tail_cycle + 1 < head_cycle)
		return 0;
	else if (tail_cycle < head_cycle) {
		ASSERT(tail_cycle == (head_cycle - 1));
		free_bytes = tail_bytes - head_bytes;
	} else {
		/*
		 * The reservation head is behind the tail.
		 * In this case we just want to return the size of the
		 * log as the amount of space left.
		 */
		xfs_alert(log->l_mp, "xlog_space_left: head behind tail");
		xfs_alert(log->l_mp,
			  "  tail_cycle = %d, tail_bytes = %d",
			  tail_cycle, tail_bytes);
		xfs_alert(log->l_mp,
			  "  GH   cycle = %d, GH   bytes = %d",
			  head_cycle, head_bytes);
		ASSERT(0);
		free_bytes = log->l_logsize;
	}
	return free_bytes;
}


/*
 * Log function which is called when an io completes.
 *
 * The log manager needs its own routine, in order to control what
 * happens with the buffer after the write completes.
 */
static void
xlog_iodone(xfs_buf_t *bp)
{
	struct xlog_in_core	*iclog = bp->b_log_item;
	struct xlog		*l = iclog->ic_log;
	int			aborted = 0;

#ifdef DEBUG
	/* treat writes with injected CRC errors as failed */
	if (iclog->ic_fail_crc)
		bp->b_error = -EIO;
#endif

	/*
	 * Race to shutdown the filesystem if we see an error.
	 */
	if (XFS_TEST_ERROR(bp->b_error, l->l_mp, XFS_ERRTAG_IODONE_IOERR)) {
		xfs_buf_ioerror_alert(bp, __func__);
		xfs_buf_stale(bp);
		xfs_force_shutdown(l->l_mp, SHUTDOWN_LOG_IO_ERROR);
		/*
		 * This flag will be propagated to the trans-committed
		 * callback routines to let them know that the log-commit
		 * didn't succeed.
		 */
		aborted = XFS_LI_ABORTED;
	} else if (iclog->ic_state & XLOG_STATE_IOERROR) {
		aborted = XFS_LI_ABORTED;
	}

	/* log I/O is always issued ASYNC */
	ASSERT(bp->b_flags & XBF_ASYNC);
	xlog_state_done_syncing(iclog, aborted);

	/*
	 * drop the buffer lock now that we are done. Nothing references
	 * the buffer after this, so an unmount waiting on this lock can now
	 * tear it down safely. As such, it is unsafe to reference the buffer
	 * (bp) after the unlock as we could race with it being freed.
	 */
	xfs_buf_unlock(bp);
}

/*
 * Return size of each in-core log record buffer.
 *
 * All machines get 8 x 32kB buffers by default, unless tuned otherwise.
 *
 * If the filesystem blocksize is too large, we may need to choose a
 * larger size since the directory code currently logs entire blocks.
 */
STATIC void
xlog_get_iclog_buffer_size(
	struct xfs_mount	*mp,
	struct xlog		*log)
{
	if (mp->m_logbufs <= 0)
		mp->m_logbufs = XLOG_MAX_ICLOGS;
	if (mp->m_logbsize <= 0)
		mp->m_logbsize = XLOG_BIG_RECORD_BSIZE;

	log->l_iclog_bufs = mp->m_logbufs;
	log->l_iclog_size = mp->m_logbsize;

	/*
	 * # headers = size / 32k - one header holds cycles from 32k of data.
	 */
	log->l_iclog_heads =
		DIV_ROUND_UP(mp->m_logbsize, XLOG_HEADER_CYCLE_SIZE);
	log->l_iclog_hsize = log->l_iclog_heads << BBSHIFT;
}

void
xfs_log_work_queue(
	struct xfs_mount        *mp)
{
	queue_delayed_work(mp->m_sync_workqueue, &mp->m_log->l_work,
				msecs_to_jiffies(xfs_syncd_centisecs * 10));
}

/*
 * Every sync period we need to unpin all items in the AIL and push them to
 * disk. If there is nothing dirty, then we might need to cover the log to
 * indicate that the filesystem is idle.
 */
static void
xfs_log_worker(
	struct work_struct	*work)
{
	struct xlog		*log = container_of(to_delayed_work(work),
						struct xlog, l_work);
	struct xfs_mount	*mp = log->l_mp;

	/* dgc: errors ignored - not fatal and nowhere to report them */
	if (xfs_log_need_covered(mp)) {
		/*
		 * Dump a transaction into the log that contains no real change.
		 * This is needed to stamp the current tail LSN into the log
		 * during the covering operation.
		 *
		 * We cannot use an inode here for this - that will push dirty
		 * state back up into the VFS and then periodic inode flushing
		 * will prevent log covering from making progress. Hence we
		 * synchronously log the superblock instead to ensure the
		 * superblock is immediately unpinned and can be written back.
		 */
		xfs_sync_sb(mp, true);
	} else
		xfs_log_force(mp, 0);

	/* start pushing all the metadata that is currently dirty */
	xfs_ail_push_all(mp->m_ail);

	/* queue us up again */
	xfs_log_work_queue(mp);
}

/*
 * This routine initializes some of the log structure for a given mount point.
 * Its primary purpose is to fill in enough, so recovery can occur.  However,
 * some other stuff may be filled in too.
 */
STATIC struct xlog *
xlog_alloc_log(
	struct xfs_mount	*mp,
	struct xfs_buftarg	*log_target,
	xfs_daddr_t		blk_offset,
	int			num_bblks)
{
	struct xlog		*log;
	xlog_rec_header_t	*head;
	xlog_in_core_t		**iclogp;
	xlog_in_core_t		*iclog, *prev_iclog=NULL;
	xfs_buf_t		*bp;
	int			i;
	int			error = -ENOMEM;
	uint			log2_size = 0;

	log = kmem_zalloc(sizeof(struct xlog), KM_MAYFAIL);
	if (!log) {
		xfs_warn(mp, "Log allocation failed: No memory!");
		goto out;
	}

	log->l_mp	   = mp;
	log->l_targ	   = log_target;
	log->l_logsize     = BBTOB(num_bblks);
	log->l_logBBstart  = blk_offset;
	log->l_logBBsize   = num_bblks;
	log->l_covered_state = XLOG_STATE_COVER_IDLE;
	log->l_flags	   |= XLOG_ACTIVE_RECOVERY;
	INIT_DELAYED_WORK(&log->l_work, xfs_log_worker);

	log->l_prev_block  = -1;
	/* log->l_tail_lsn = 0x100000000LL; cycle = 1; current block = 0 */
	xlog_assign_atomic_lsn(&log->l_tail_lsn, 1, 0);
	xlog_assign_atomic_lsn(&log->l_last_sync_lsn, 1, 0);
	log->l_curr_cycle  = 1;	    /* 0 is bad since this is initial value */

	xlog_grant_head_init(&log->l_reserve_head);
	xlog_grant_head_init(&log->l_write_head);

	error = -EFSCORRUPTED;
	if (xfs_sb_version_hassector(&mp->m_sb)) {
	        log2_size = mp->m_sb.sb_logsectlog;
		if (log2_size < BBSHIFT) {
			xfs_warn(mp, "Log sector size too small (0x%x < 0x%x)",
				log2_size, BBSHIFT);
			goto out_free_log;
		}

	        log2_size -= BBSHIFT;
		if (log2_size > mp->m_sectbb_log) {
			xfs_warn(mp, "Log sector size too large (0x%x > 0x%x)",
				log2_size, mp->m_sectbb_log);
			goto out_free_log;
		}

		/* for larger sector sizes, must have v2 or external log */
		if (log2_size && log->l_logBBstart > 0 &&
			    !xfs_sb_version_haslogv2(&mp->m_sb)) {
			xfs_warn(mp,
		"log sector size (0x%x) invalid for configuration.",
				log2_size);
			goto out_free_log;
		}
	}
	log->l_sectBBsize = 1 << log2_size;

	xlog_get_iclog_buffer_size(mp, log);

	/*
	 * Use a NULL block for the extra log buffer used during splits so that
	 * it will trigger errors if we ever try to do IO on it without first
	 * having set it up properly.
	 */
	error = -ENOMEM;
	bp = xfs_buf_alloc(mp->m_logdev_targp, XFS_BUF_DADDR_NULL,
			   BTOBB(log->l_iclog_size), XBF_NO_IOACCT);
	if (!bp)
		goto out_free_log;

	/*
	 * The iclogbuf buffer locks are held over IO but we are not going to do
	 * IO yet.  Hence unlock the buffer so that the log IO path can grab it
	 * when appropriately.
	 */
	ASSERT(xfs_buf_islocked(bp));
	xfs_buf_unlock(bp);

	/* use high priority wq for log I/O completion */
	bp->b_ioend_wq = mp->m_log_workqueue;
	bp->b_iodone = xlog_iodone;
	log->l_xbuf = bp;

	spin_lock_init(&log->l_icloglock);
	init_waitqueue_head(&log->l_flush_wait);

	iclogp = &log->l_iclog;
	/*
	 * The amount of memory to allocate for the iclog structure is
	 * rather funky due to the way the structure is defined.  It is
	 * done this way so that we can use different sizes for machines
	 * with different amounts of memory.  See the definition of
	 * xlog_in_core_t in xfs_log_priv.h for details.
	 */
	ASSERT(log->l_iclog_size >= 4096);
	for (i=0; i < log->l_iclog_bufs; i++) {
		*iclogp = kmem_zalloc(sizeof(xlog_in_core_t), KM_MAYFAIL);
		if (!*iclogp)
			goto out_free_iclog;

		iclog = *iclogp;
		iclog->ic_prev = prev_iclog;
		prev_iclog = iclog;

		bp = xfs_buf_get_uncached(mp->m_logdev_targp,
					  BTOBB(log->l_iclog_size),
					  XBF_NO_IOACCT);
		if (!bp)
			goto out_free_iclog;

		ASSERT(xfs_buf_islocked(bp));
		xfs_buf_unlock(bp);

		/* use high priority wq for log I/O completion */
		bp->b_ioend_wq = mp->m_log_workqueue;
		bp->b_iodone = xlog_iodone;
		iclog->ic_bp = bp;
		iclog->ic_data = bp->b_addr;
#ifdef DEBUG
		log->l_iclog_bak[i] = &iclog->ic_header;
#endif
		head = &iclog->ic_header;
		memset(head, 0, sizeof(xlog_rec_header_t));
		head->h_magicno = cpu_to_be32(XLOG_HEADER_MAGIC_NUM);
		head->h_version = cpu_to_be32(
			xfs_sb_version_haslogv2(&log->l_mp->m_sb) ? 2 : 1);
		head->h_size = cpu_to_be32(log->l_iclog_size);
		/* new fields */
		head->h_fmt = cpu_to_be32(XLOG_FMT);
		memcpy(&head->h_fs_uuid, &mp->m_sb.sb_uuid, sizeof(uuid_t));

		iclog->ic_size = BBTOB(bp->b_length) - log->l_iclog_hsize;
		iclog->ic_state = XLOG_STATE_ACTIVE;
		iclog->ic_log = log;
		atomic_set(&iclog->ic_refcnt, 0);
		spin_lock_init(&iclog->ic_callback_lock);
		iclog->ic_callback_tail = &(iclog->ic_callback);
		iclog->ic_datap = (char *)iclog->ic_data + log->l_iclog_hsize;

		init_waitqueue_head(&iclog->ic_force_wait);
		init_waitqueue_head(&iclog->ic_write_wait);

		iclogp = &iclog->ic_next;
	}
	*iclogp = log->l_iclog;			/* complete ring */
	log->l_iclog->ic_prev = prev_iclog;	/* re-write 1st prev ptr */

	error = xlog_cil_init(log);
	if (error)
		goto out_free_iclog;
	return log;

out_free_iclog:
	for (iclog = log->l_iclog; iclog; iclog = prev_iclog) {
		prev_iclog = iclog->ic_next;
		if (iclog->ic_bp)
			xfs_buf_free(iclog->ic_bp);
		kmem_free(iclog);
	}
	xfs_buf_free(log->l_xbuf);
out_free_log:
	kmem_free(log);
out:
	return ERR_PTR(error);
}	/* xlog_alloc_log */


/*
 * Write out the commit record of a transaction associated with the given
 * ticket.  Return the lsn of the commit record.
 */
STATIC int
xlog_commit_record(
	struct xlog		*log,
	struct xlog_ticket	*ticket,
	struct xlog_in_core	**iclog,
	xfs_lsn_t		*commitlsnp)
{
	struct xfs_mount *mp = log->l_mp;
	int	error;
	struct xfs_log_iovec reg = {
		.i_addr = NULL,
		.i_len = 0,
		.i_type = XLOG_REG_TYPE_COMMIT,
	};
	struct xfs_log_vec vec = {
		.lv_niovecs = 1,
		.lv_iovecp = &reg,
	};

	ASSERT_ALWAYS(iclog);
	error = xlog_write(log, &vec, ticket, commitlsnp, iclog,
					XLOG_COMMIT_TRANS);
	if (error)
		xfs_force_shutdown(mp, SHUTDOWN_LOG_IO_ERROR);
	return error;
}

/*
 * Push on the buffer cache code if we ever use more than 75% of the on-disk
 * log space.  This code pushes on the lsn which would supposedly free up
 * the 25% which we want to leave free.  We may need to adopt a policy which
 * pushes on an lsn which is further along in the log once we reach the high
 * water mark.  In this manner, we would be creating a low water mark.
 */
STATIC void
xlog_grant_push_ail(
	struct xlog	*log,
	int		need_bytes)
{
	xfs_lsn_t	threshold_lsn = 0;
	xfs_lsn_t	last_sync_lsn;
	int		free_blocks;
	int		free_bytes;
	int		threshold_block;
	int		threshold_cycle;
	int		free_threshold;

	ASSERT(BTOBB(need_bytes) < log->l_logBBsize);

	free_bytes = xlog_space_left(log, &log->l_reserve_head.grant);
	free_blocks = BTOBBT(free_bytes);

	/*
	 * Set the threshold for the minimum number of free blocks in the
	 * log to the maximum of what the caller needs, one quarter of the
	 * log, and 256 blocks.
	 */
	free_threshold = BTOBB(need_bytes);
	free_threshold = max(free_threshold, (log->l_logBBsize >> 2));
	free_threshold = max(free_threshold, 256);
	if (free_blocks >= free_threshold)
		return;

	xlog_crack_atomic_lsn(&log->l_tail_lsn, &threshold_cycle,
						&threshold_block);
	threshold_block += free_threshold;
	if (threshold_block >= log->l_logBBsize) {
		threshold_block -= log->l_logBBsize;
		threshold_cycle += 1;
	}
	threshold_lsn = xlog_assign_lsn(threshold_cycle,
					threshold_block);
	/*
	 * Don't pass in an lsn greater than the lsn of the last
	 * log record known to be on disk. Use a snapshot of the last sync lsn
	 * so that it doesn't change between the compare and the set.
	 */
	last_sync_lsn = atomic64_read(&log->l_last_sync_lsn);
	if (XFS_LSN_CMP(threshold_lsn, last_sync_lsn) > 0)
		threshold_lsn = last_sync_lsn;

	/*
	 * Get the transaction layer to kick the dirty buffers out to
	 * disk asynchronously. No point in trying to do this if
	 * the filesystem is shutting down.
	 */
	if (!XLOG_FORCED_SHUTDOWN(log))
		xfs_ail_push(log->l_ailp, threshold_lsn);
}

/*
 * Stamp cycle number in every block
 */
STATIC void
xlog_pack_data(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	int			roundoff)
{
	int			i, j, k;
	int			size = iclog->ic_offset + roundoff;
	__be32			cycle_lsn;
	char			*dp;

	cycle_lsn = CYCLE_LSN_DISK(iclog->ic_header.h_lsn);

	dp = iclog->ic_datap;
	for (i = 0; i < BTOBB(size); i++) {
		if (i >= (XLOG_HEADER_CYCLE_SIZE / BBSIZE))
			break;
		iclog->ic_header.h_cycle_data[i] = *(__be32 *)dp;
		*(__be32 *)dp = cycle_lsn;
		dp += BBSIZE;
	}

	if (xfs_sb_version_haslogv2(&log->l_mp->m_sb)) {
		xlog_in_core_2_t *xhdr = iclog->ic_data;

		for ( ; i < BTOBB(size); i++) {
			j = i / (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
			k = i % (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
			xhdr[j].hic_xheader.xh_cycle_data[k] = *(__be32 *)dp;
			*(__be32 *)dp = cycle_lsn;
			dp += BBSIZE;
		}

		for (i = 1; i < log->l_iclog_heads; i++)
			xhdr[i].hic_xheader.xh_cycle = cycle_lsn;
	}
}

/*
 * Calculate the checksum for a log buffer.
 *
 * This is a little more complicated than it should be because the various
 * headers and the actual data are non-contiguous.
 */
__le32
xlog_cksum(
	struct xlog		*log,
	struct xlog_rec_header	*rhead,
	char			*dp,
	int			size)
{
	uint32_t		crc;

	/* first generate the crc for the record header ... */
	crc = xfs_start_cksum_update((char *)rhead,
			      sizeof(struct xlog_rec_header),
			      offsetof(struct xlog_rec_header, h_crc));

	/* ... then for additional cycle data for v2 logs ... */
	if (xfs_sb_version_haslogv2(&log->l_mp->m_sb)) {
		union xlog_in_core2 *xhdr = (union xlog_in_core2 *)rhead;
		int		i;
		int		xheads;

		xheads = size / XLOG_HEADER_CYCLE_SIZE;
		if (size % XLOG_HEADER_CYCLE_SIZE)
			xheads++;

		for (i = 1; i < xheads; i++) {
			crc = crc32c(crc, &xhdr[i].hic_xheader,
				     sizeof(struct xlog_rec_ext_header));
		}
	}

	/* ... and finally for the payload */
	crc = crc32c(crc, dp, size);

	return xfs_end_cksum(crc);
}

STATIC void
xlog_write_iclog(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	struct xfs_buf		*bp,
	uint64_t		bno,
	bool			need_flush)
{
	ASSERT(bno < log->l_logBBsize);
	ASSERT(bno + bp->b_io_length <= log->l_logBBsize);

	bp->b_maps[0].bm_bn = log->l_logBBstart + bno;
	bp->b_log_item = iclog;
	bp->b_flags &= ~XBF_FLUSH;
	bp->b_flags |= (XBF_ASYNC | XBF_SYNCIO | XBF_WRITE | XBF_FUA);
	if (need_flush)
		bp->b_flags |= XBF_FLUSH;

	/*
	 * We lock the iclogbufs here so that we can serialise against I/O
	 * completion during unmount.  We might be processing a shutdown
	 * triggered during unmount, and that can occur asynchronously to the
	 * unmount thread, and hence we need to ensure that completes before
	 * tearing down the iclogbufs.  Hence we need to hold the buffer lock
	 * across the log IO to archieve that.
	 */
	xfs_buf_lock(bp);
	if (unlikely(iclog->ic_state & XLOG_STATE_IOERROR)) {
		xfs_buf_ioerror(bp, -EIO);
		xfs_buf_stale(bp);
		xfs_buf_ioend(bp);
		/*
		 * It would seem logical to return EIO here, but we rely on
		 * the log state machine to propagate I/O errors instead of
		 * doing it here. Similarly, IO completion will unlock the
		 * buffer, so we don't do it here.
		 */
		return;
	}

	xfs_buf_submit(bp);
}

/*
 * We need to bump cycle number for the part of the iclog that is
 * written to the start of the log. Watch out for the header magic
 * number case, though.
 */
static unsigned int
xlog_split_iclog(
	struct xlog		*log,
	void			*data,
	uint64_t		bno,
	unsigned int		count)
{
	unsigned int		split_offset = BBTOB(log->l_logBBsize - bno);
	unsigned int		i;

	for (i = split_offset; i < count; i += BBSIZE) {
		uint32_t cycle = get_unaligned_be32(data + i);

		if (++cycle == XLOG_HEADER_MAGIC_NUM)
			cycle++;
		put_unaligned_be32(cycle, data + i);
	}

	return split_offset;
}

/*
 * Flush out the in-core log (iclog) to the on-disk log in an asynchronous 
 * fashion.  Previously, we should have moved the current iclog
 * ptr in the log to point to the next available iclog.  This allows further
 * write to continue while this code syncs out an iclog ready to go.
 * Before an in-core log can be written out, the data section must be scanned
 * to save away the 1st word of each BBSIZE block into the header.  We replace
 * it with the current cycle count.  Each BBSIZE block is tagged with the
 * cycle count because there in an implicit assumption that drives will
 * guarantee that entire 512 byte blocks get written at once.  In other words,
 * we can't have part of a 512 byte block written and part not written.  By
 * tagging each block, we will know which blocks are valid when recovering
 * after an unclean shutdown.
 *
 * This routine is single threaded on the iclog.  No other thread can be in
 * this routine with the same iclog.  Changing contents of iclog can there-
 * fore be done without grabbing the state machine lock.  Updating the global
 * log will require grabbing the lock though.
 *
 * The entire log manager uses a logical block numbering scheme.  Only
 * xlog_write_iclog knows about the fact that the log may not start with
 * block zero on a given device.
 */
STATIC void
xlog_sync(
	struct xlog		*log,
	struct xlog_in_core	*iclog)
{
	uint		count;		/* byte count of bwrite */
	uint		count_init;	/* initial count before roundup */
	int		roundoff;       /* roundoff to BB or stripe */
	int		v2 = xfs_sb_version_haslogv2(&log->l_mp->m_sb);
	uint64_t	bno;
	unsigned int	split = 0;
	int		size;
	bool		need_flush = true;

	XFS_STATS_INC(log->l_mp, xs_log_writes);
	ASSERT(atomic_read(&iclog->ic_refcnt) == 0);

	/* Add for LR header */
	count_init = log->l_iclog_hsize + iclog->ic_offset;

	/* Round out the log write size */
	if (v2 && log->l_mp->m_sb.sb_logsunit > 1) {
		/* we have a v2 stripe unit to use */
		count = XLOG_LSUNITTOB(log, XLOG_BTOLSUNIT(log, count_init));
	} else {
		count = BBTOB(BTOBB(count_init));
	}
	roundoff = count - count_init;
	ASSERT(roundoff >= 0);
	ASSERT((v2 && log->l_mp->m_sb.sb_logsunit > 1 && 
                roundoff < log->l_mp->m_sb.sb_logsunit)
		|| 
		(log->l_mp->m_sb.sb_logsunit <= 1 && 
		 roundoff < BBTOB(1)));

	/* move grant heads by roundoff in sync */
	xlog_grant_add_space(log, &log->l_reserve_head.grant, roundoff);
	xlog_grant_add_space(log, &log->l_write_head.grant, roundoff);

	/* put cycle number in every block */
	xlog_pack_data(log, iclog, roundoff); 

	/* real byte length */
	size = iclog->ic_offset;
	if (v2)
		size += roundoff;
	iclog->ic_header.h_len = cpu_to_be32(size);

	XFS_STATS_ADD(log->l_mp, xs_log_blocks, BTOBB(count));

	bno = BLOCK_LSN(be64_to_cpu(iclog->ic_header.h_lsn));

	/* Do we need to split this write into 2 parts? */
	if (bno + BTOBB(count) > log->l_logBBsize)
		split = xlog_split_iclog(log, &iclog->ic_header, bno, count);

	/* calculcate the checksum */
	iclog->ic_header.h_crc = xlog_cksum(log, &iclog->ic_header,
					    iclog->ic_datap, size);
	/*
	 * Intentionally corrupt the log record CRC based on the error injection
	 * frequency, if defined. This facilitates testing log recovery in the
	 * event of torn writes. Hence, set the IOABORT state to abort the log
	 * write on I/O completion and shutdown the fs. The subsequent mount
	 * detects the bad CRC and attempts to recover.
	 */
#ifdef DEBUG
	if (XFS_TEST_ERROR(false, log->l_mp, XFS_ERRTAG_LOG_BAD_CRC)) {
		iclog->ic_header.h_crc &= cpu_to_le32(0xAAAAAAAA);
		iclog->ic_fail_crc = true;
		xfs_warn(log->l_mp,
	"Intentionally corrupted log record at LSN 0x%llx. Shutdown imminent.",
			 be64_to_cpu(iclog->ic_header.h_lsn));
	}
#endif

	/*
	 * Flush the data device before flushing the log to make sure all meta
	 * data written back from the AIL actually made it to disk before
	 * stamping the new log tail LSN into the log buffer.  For an external
	 * log we need to issue the flush explicitly, and unfortunately
	 * synchronously here; for an internal log we can simply use the block
	 * layer state machine for preflushes.
	 */
	if (log->l_mp->m_logdev_targp != log->l_mp->m_ddev_targp || split) {
		xfs_blkdev_issue_flush(log->l_mp->m_ddev_targp);
		need_flush = false;
	}

	iclog->ic_bp->b_io_length = BTOBB(split ? split : count);
	iclog->ic_bwritecnt = split ? 2 : 1;

	xlog_verify_iclog(log, iclog, count, true);
	xlog_write_iclog(log, iclog, iclog->ic_bp, bno, need_flush);

	if (split) {
		xfs_buf_associate_memory(iclog->ic_log->l_xbuf,
				(char *)&iclog->ic_header + split,
				count - split);
		xlog_write_iclog(log, iclog, iclog->ic_log->l_xbuf, 0, false);
	}
}

/*
 * Deallocate a log structure
 */
STATIC void
xlog_dealloc_log(
	struct xlog	*log)
{
	xlog_in_core_t	*iclog, *next_iclog;
	int		i;

	xlog_cil_destroy(log);

	/*
	 * Cycle all the iclogbuf locks to make sure all log IO completion
	 * is done before we tear down these buffers.
	 */
	iclog = log->l_iclog;
	for (i = 0; i < log->l_iclog_bufs; i++) {
		xfs_buf_lock(iclog->ic_bp);
		xfs_buf_unlock(iclog->ic_bp);
		iclog = iclog->ic_next;
	}

	/*
	 * Always need to ensure that the extra buffer does not point to memory
	 * owned by another log buffer before we free it. Also, cycle the lock
	 * first to ensure we've completed IO on it.
	 */
	xfs_buf_lock(log->l_xbuf);
	xfs_buf_unlock(log->l_xbuf);
	xfs_buf_set_empty(log->l_xbuf, BTOBB(log->l_iclog_size));
	xfs_buf_free(log->l_xbuf);

	iclog = log->l_iclog;
	for (i = 0; i < log->l_iclog_bufs; i++) {
		xfs_buf_free(iclog->ic_bp);
		next_iclog = iclog->ic_next;
		kmem_free(iclog);
		iclog = next_iclog;
	}

	log->l_mp->m_log = NULL;
	kmem_free(log);
}	/* xlog_dealloc_log */

/*
 * Update counters atomically now that memcpy is done.
 */
/* ARGSUSED */
static inline void
xlog_state_finish_copy(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	int			record_cnt,
	int			copy_bytes)
{
	spin_lock(&log->l_icloglock);

	be32_add_cpu(&iclog->ic_header.h_num_logops, record_cnt);
	iclog->ic_offset += copy_bytes;

	spin_unlock(&log->l_icloglock);
}	/* xlog_state_finish_copy */




/*
 * print out info relating to regions written which consume
 * the reservation
 */
void
xlog_print_tic_res(
	struct xfs_mount	*mp,
	struct xlog_ticket	*ticket)
{
	uint i;
	uint ophdr_spc = ticket->t_res_num_ophdrs * (uint)sizeof(xlog_op_header_t);

	/* match with XLOG_REG_TYPE_* in xfs_log.h */
#define REG_TYPE_STR(type, str)	[XLOG_REG_TYPE_##type] = str
	static char *res_type_str[] = {
	    REG_TYPE_STR(BFORMAT, "bformat"),
	    REG_TYPE_STR(BCHUNK, "bchunk"),
	    REG_TYPE_STR(EFI_FORMAT, "efi_format"),
	    REG_TYPE_STR(EFD_FORMAT, "efd_format"),
	    REG_TYPE_STR(IFORMAT, "iformat"),
	    REG_TYPE_STR(ICORE, "icore"),
	    REG_TYPE_STR(IEXT, "iext"),
	    REG_TYPE_STR(IBROOT, "ibroot"),
	    REG_TYPE_STR(ILOCAL, "ilocal"),
	    REG_TYPE_STR(IATTR_EXT, "iattr_ext"),
	    REG_TYPE_STR(IATTR_BROOT, "iattr_broot"),
	    REG_TYPE_STR(IATTR_LOCAL, "iattr_local"),
	    REG_TYPE_STR(QFORMAT, "qformat"),
	    REG_TYPE_STR(DQUOT, "dquot"),
	    REG_TYPE_STR(QUOTAOFF, "quotaoff"),
	    REG_TYPE_STR(LRHEADER, "LR header"),
	    REG_TYPE_STR(UNMOUNT, "unmount"),
	    REG_TYPE_STR(COMMIT, "commit"),
	    REG_TYPE_STR(TRANSHDR, "trans header"),
	    REG_TYPE_STR(ICREATE, "inode create"),
	    REG_TYPE_STR(RUI_FORMAT, "rui_format"),
	    REG_TYPE_STR(RUD_FORMAT, "rud_format"),
	    REG_TYPE_STR(CUI_FORMAT, "cui_format"),
	    REG_TYPE_STR(CUD_FORMAT, "cud_format"),
	    REG_TYPE_STR(BUI_FORMAT, "bui_format"),
	    REG_TYPE_STR(BUD_FORMAT, "bud_format"),
	};
	BUILD_BUG_ON(ARRAY_SIZE(res_type_str) != XLOG_REG_TYPE_MAX + 1);
#undef REG_TYPE_STR

	xfs_warn(mp, "ticket reservation summary:");
	xfs_warn(mp, "  unit res    = %d bytes",
		 ticket->t_unit_res);
	xfs_warn(mp, "  current res = %d bytes",
		 ticket->t_curr_res);
	xfs_warn(mp, "  total reg   = %u bytes (o/flow = %u bytes)",
		 ticket->t_res_arr_sum, ticket->t_res_o_flow);
	xfs_warn(mp, "  ophdrs      = %u (ophdr space = %u bytes)",
		 ticket->t_res_num_ophdrs, ophdr_spc);
	xfs_warn(mp, "  ophdr + reg = %u bytes",
		 ticket->t_res_arr_sum + ticket->t_res_o_flow + ophdr_spc);
	xfs_warn(mp, "  num regions = %u",
		 ticket->t_res_num);

	for (i = 0; i < ticket->t_res_num; i++) {
		uint r_type = ticket->t_res_arr[i].r_type;
		xfs_warn(mp, "region[%u]: %s - %u bytes", i,
			    ((r_type <= 0 || r_type > XLOG_REG_TYPE_MAX) ?
			    "bad-rtype" : res_type_str[r_type]),
			    ticket->t_res_arr[i].r_len);
	}
}

/*
 * Print a summary of the transaction.
 */
void
xlog_print_trans(
	struct xfs_trans	*tp)
{
	struct xfs_mount	*mp = tp->t_mountp;
	struct xfs_log_item	*lip;

	/* dump core transaction and ticket info */
	xfs_warn(mp, "transaction summary:");
	xfs_warn(mp, "  log res   = %d", tp->t_log_res);
	xfs_warn(mp, "  log count = %d", tp->t_log_count);
	xfs_warn(mp, "  flags     = 0x%x", tp->t_flags);

	xlog_print_tic_res(mp, tp->t_ticket);

	/* dump each log item */
	list_for_each_entry(lip, &tp->t_items, li_trans) {
		struct xfs_log_vec	*lv = lip->li_lv;
		struct xfs_log_iovec	*vec;
		int			i;

		xfs_warn(mp, "log item: ");
		xfs_warn(mp, "  type	= 0x%x", lip->li_type);
		xfs_warn(mp, "  flags	= 0x%lx", lip->li_flags);
		if (!lv)
			continue;
		xfs_warn(mp, "  niovecs	= %d", lv->lv_niovecs);
		xfs_warn(mp, "  size	= %d", lv->lv_size);
		xfs_warn(mp, "  bytes	= %d", lv->lv_bytes);
		xfs_warn(mp, "  buf len	= %d", lv->lv_buf_len);

		/* dump each iovec for the log item */
		vec = lv->lv_iovecp;
		for (i = 0; i < lv->lv_niovecs; i++) {
			int dumplen = min(vec->i_len, 32);

			xfs_warn(mp, "  iovec[%d]", i);
			xfs_warn(mp, "    type	= 0x%x", vec->i_type);
			xfs_warn(mp, "    len	= %d", vec->i_len);
			xfs_warn(mp, "    first %d bytes of iovec[%d]:", dumplen, i);
			xfs_hex_dump(vec->i_addr, dumplen);

			vec++;
		}
	}
}

/*
 * Calculate the potential space needed by the log vector.  Each region gets
 * its own xlog_op_header_t and may need to be double word aligned.
 */
static int
xlog_write_calc_vec_length(
	struct xlog_ticket	*ticket,
	struct xfs_log_vec	*log_vector)
{
	struct xfs_log_vec	*lv;
	int			headers = 0;
	int			len = 0;
	int			i;

	/* acct for start rec of xact */
	if (ticket->t_flags & XLOG_TIC_INITED)
		headers++;

	for (lv = log_vector; lv; lv = lv->lv_next) {
		/* we don't write ordered log vectors */
		if (lv->lv_buf_len == XFS_LOG_VEC_ORDERED)
			continue;

		headers += lv->lv_niovecs;

		for (i = 0; i < lv->lv_niovecs; i++) {
			struct xfs_log_iovec	*vecp = &lv->lv_iovecp[i];

			len += vecp->i_len;
			xlog_tic_add_region(ticket, vecp->i_len, vecp->i_type);
		}
	}

	ticket->t_res_num_ophdrs += headers;
	len += headers * sizeof(struct xlog_op_header);

	return len;
}

/*
 * If first write for transaction, insert start record  We can't be trying to
 * commit if we are inited.  We can't have any "partial_copy" if we are inited.
 */
static int
xlog_write_start_rec(
	struct xlog_op_header	*ophdr,
	struct xlog_ticket	*ticket)
{
	if (!(ticket->t_flags & XLOG_TIC_INITED))
		return 0;

	ophdr->oh_tid	= cpu_to_be32(ticket->t_tid);
	ophdr->oh_clientid = ticket->t_clientid;
	ophdr->oh_len = 0;
	ophdr->oh_flags = XLOG_START_TRANS;
	ophdr->oh_res2 = 0;

	ticket->t_flags &= ~XLOG_TIC_INITED;

	return sizeof(struct xlog_op_header);
}

static xlog_op_header_t *
xlog_write_setup_ophdr(
	struct xlog		*log,
	struct xlog_op_header	*ophdr,
	struct xlog_ticket	*ticket,
	uint			flags)
{
	ophdr->oh_tid = cpu_to_be32(ticket->t_tid);
	ophdr->oh_clientid = ticket->t_clientid;
	ophdr->oh_res2 = 0;

	/* are we copying a commit or unmount record? */
	ophdr->oh_flags = flags;

	/*
	 * We've seen logs corrupted with bad transaction client ids.  This
	 * makes sure that XFS doesn't generate them on.  Turn this into an EIO
	 * and shut down the filesystem.
	 */
	switch (ophdr->oh_clientid)  {
	case XFS_TRANSACTION:
	case XFS_VOLUME:
	case XFS_LOG:
		break;
	default:
		xfs_warn(log->l_mp,
			"Bad XFS transaction clientid 0x%x in ticket "PTR_FMT,
			ophdr->oh_clientid, ticket);
		return NULL;
	}

	return ophdr;
}

/*
 * Set up the parameters of the region copy into the log. This has
 * to handle region write split across multiple log buffers - this
 * state is kept external to this function so that this code can
 * be written in an obvious, self documenting manner.
 */
static int
xlog_write_setup_copy(
	struct xlog_ticket	*ticket,
	struct xlog_op_header	*ophdr,
	int			space_available,
	int			space_required,
	int			*copy_off,
	int			*copy_len,
	int			*last_was_partial_copy,
	int			*bytes_consumed)
{
	int			still_to_copy;

	still_to_copy = space_required - *bytes_consumed;
	*copy_off = *bytes_consumed;

	if (still_to_copy <= space_available) {
		/* write of region completes here */
		*copy_len = still_to_copy;
		ophdr->oh_len = cpu_to_be32(*copy_len);
		if (*last_was_partial_copy)
			ophdr->oh_flags |= (XLOG_END_TRANS|XLOG_WAS_CONT_TRANS);
		*last_was_partial_copy = 0;
		*bytes_consumed = 0;
		return 0;
	}

	/* partial write of region, needs extra log op header reservation */
	*copy_len = space_available;
	ophdr->oh_len = cpu_to_be32(*copy_len);
	ophdr->oh_flags |= XLOG_CONTINUE_TRANS;
	if (*last_was_partial_copy)
		ophdr->oh_flags |= XLOG_WAS_CONT_TRANS;
	*bytes_consumed += *copy_len;
	(*last_was_partial_copy)++;

	/* account for new log op header */
	ticket->t_curr_res -= sizeof(struct xlog_op_header);
	ticket->t_res_num_ophdrs++;

	return sizeof(struct xlog_op_header);
}

static int
xlog_write_copy_finish(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	uint			flags,
	int			*record_cnt,
	int			*data_cnt,
	int			*partial_copy,
	int			*partial_copy_len,
	int			log_offset,
	struct xlog_in_core	**commit_iclog)
{
	if (*partial_copy) {
		/*
		 * This iclog has already been marked WANT_SYNC by
		 * xlog_state_get_iclog_space.
		 */
		xlog_state_finish_copy(log, iclog, *record_cnt, *data_cnt);
		*record_cnt = 0;
		*data_cnt = 0;
		return xlog_state_release_iclog(log, iclog);
	}

	*partial_copy = 0;
	*partial_copy_len = 0;

	if (iclog->ic_size - log_offset <= sizeof(xlog_op_header_t)) {
		/* no more space in this iclog - push it. */
		xlog_state_finish_copy(log, iclog, *record_cnt, *data_cnt);
		*record_cnt = 0;
		*data_cnt = 0;

		spin_lock(&log->l_icloglock);
		xlog_state_want_sync(log, iclog);
		spin_unlock(&log->l_icloglock);

		if (!commit_iclog)
			return xlog_state_release_iclog(log, iclog);
		ASSERT(flags & XLOG_COMMIT_TRANS);
		*commit_iclog = iclog;
	}

	return 0;
}

/*
 * Write some region out to in-core log
 *
 * This will be called when writing externally provided regions or when
 * writing out a commit record for a given transaction.
 *
 * General algorithm:
 *	1. Find total length of this write.  This may include adding to the
 *		lengths passed in.
 *	2. Check whether we violate the tickets reservation.
 *	3. While writing to this iclog
 *	    A. Reserve as much space in this iclog as can get
 *	    B. If this is first write, save away start lsn
 *	    C. While writing this region:
 *		1. If first write of transaction, write start record
 *		2. Write log operation header (header per region)
 *		3. Find out if we can fit entire region into this iclog
 *		4. Potentially, verify destination memcpy ptr
 *		5. Memcpy (partial) region
 *		6. If partial copy, release iclog; otherwise, continue
 *			copying more regions into current iclog
 *	4. Mark want sync bit (in simulation mode)
 *	5. Release iclog for potential flush to on-disk log.
 *
 * ERRORS:
 * 1.	Panic if reservation is overrun.  This should never happen since
 *	reservation amounts are generated internal to the filesystem.
 * NOTES:
 * 1. Tickets are single threaded data structures.
 * 2. The XLOG_END_TRANS & XLOG_CONTINUE_TRANS flags are passed down to the
 *	syncing routine.  When a single log_write region needs to span
 *	multiple in-core logs, the XLOG_CONTINUE_TRANS bit should be set
 *	on all log operation writes which don't contain the end of the
 *	region.  The XLOG_END_TRANS bit is used for the in-core log
 *	operation which contains the end of the continued log_write region.
 * 3. When xlog_state_get_iclog_space() grabs the rest of the current iclog,
 *	we don't really know exactly how much space will be used.  As a result,
 *	we don't update ic_offset until the end when we know exactly how many
 *	bytes have been written out.
 */
int
xlog_write(
	struct xlog		*log,
	struct xfs_log_vec	*log_vector,
	struct xlog_ticket	*ticket,
	xfs_lsn_t		*start_lsn,
	struct xlog_in_core	**commit_iclog,
	uint			flags)
{
	struct xlog_in_core	*iclog = NULL;
	struct xfs_log_iovec	*vecp;
	struct xfs_log_vec	*lv;
	int			len;
	int			index;
	int			partial_copy = 0;
	int			partial_copy_len = 0;
	int			contwr = 0;
	int			record_cnt = 0;
	int			data_cnt = 0;
	int			error;

	*start_lsn = 0;

	len = xlog_write_calc_vec_length(ticket, log_vector);

	/*
	 * Region headers and bytes are already accounted for.
	 * We only need to take into account start records and
	 * split regions in this function.
	 */
	if (ticket->t_flags & XLOG_TIC_INITED)
		ticket->t_curr_res -= sizeof(xlog_op_header_t);

	/*
	 * Commit record headers need to be accounted for. These
	 * come in as separate writes so are easy to detect.
	 */
	if (flags & (XLOG_COMMIT_TRANS | XLOG_UNMOUNT_TRANS))
		ticket->t_curr_res -= sizeof(xlog_op_header_t);

	if (ticket->t_curr_res < 0) {
		xfs_alert_tag(log->l_mp, XFS_PTAG_LOGRES,
		     "ctx ticket reservation ran out. Need to up reservation");
		xlog_print_tic_res(log->l_mp, ticket);
		xfs_force_shutdown(log->l_mp, SHUTDOWN_LOG_IO_ERROR);
	}

	index = 0;
	lv = log_vector;
	vecp = lv->lv_iovecp;
	while (lv && (!lv->lv_niovecs || index < lv->lv_niovecs)) {
		void		*ptr;
		int		log_offset;

		error = xlog_state_get_iclog_space(log, len, &iclog, ticket,
						   &contwr, &log_offset);
		if (error)
			return error;

		ASSERT(log_offset <= iclog->ic_size - 1);
		ptr = iclog->ic_datap + log_offset;

		/* start_lsn is the first lsn written to. That's all we need. */
		if (!*start_lsn)
			*start_lsn = be64_to_cpu(iclog->ic_header.h_lsn);

		/*
		 * This loop writes out as many regions as can fit in the amount
		 * of space which was allocated by xlog_state_get_iclog_space().
		 */
		while (lv && (!lv->lv_niovecs || index < lv->lv_niovecs)) {
			struct xfs_log_iovec	*reg;
			struct xlog_op_header	*ophdr;
			int			start_rec_copy;
			int			copy_len;
			int			copy_off;
			bool			ordered = false;

			/* ordered log vectors have no regions to write */
			if (lv->lv_buf_len == XFS_LOG_VEC_ORDERED) {
				ASSERT(lv->lv_niovecs == 0);
				ordered = true;
				goto next_lv;
			}

			reg = &vecp[index];
			ASSERT(reg->i_len % sizeof(int32_t) == 0);
			ASSERT((unsigned long)ptr % sizeof(int32_t) == 0);

			start_rec_copy = xlog_write_start_rec(ptr, ticket);
			if (start_rec_copy) {
				record_cnt++;
				xlog_write_adv_cnt(&ptr, &len, &log_offset,
						   start_rec_copy);
			}

			ophdr = xlog_write_setup_ophdr(log, ptr, ticket, flags);
			if (!ophdr)
				return -EIO;

			xlog_write_adv_cnt(&ptr, &len, &log_offset,
					   sizeof(struct xlog_op_header));

			len += xlog_write_setup_copy(ticket, ophdr,
						     iclog->ic_size-log_offset,
						     reg->i_len,
						     &copy_off, &copy_len,
						     &partial_copy,
						     &partial_copy_len);
			xlog_verify_dest_ptr(log, ptr);

			/*
			 * Copy region.
			 *
			 * Unmount records just log an opheader, so can have
			 * empty payloads with no data region to copy. Hence we
			 * only copy the payload if the vector says it has data
			 * to copy.
			 */
			ASSERT(copy_len >= 0);
			if (copy_len > 0) {
				memcpy(ptr, reg->i_addr + copy_off, copy_len);
				xlog_write_adv_cnt(&ptr, &len, &log_offset,
						   copy_len);
			}
			copy_len += start_rec_copy + sizeof(xlog_op_header_t);
			record_cnt++;
			data_cnt += contwr ? copy_len : 0;

			error = xlog_write_copy_finish(log, iclog, flags,
						       &record_cnt, &data_cnt,
						       &partial_copy,
						       &partial_copy_len,
						       log_offset,
						       commit_iclog);
			if (error)
				return error;

			/*
			 * if we had a partial copy, we need to get more iclog
			 * space but we don't want to increment the region
			 * index because there is still more is this region to
			 * write.
			 *
			 * If we completed writing this region, and we flushed
			 * the iclog (indicated by resetting of the record
			 * count), then we also need to get more log space. If
			 * this was the last record, though, we are done and
			 * can just return.
			 */
			if (partial_copy)
				break;

			if (++index == lv->lv_niovecs) {
next_lv:
				lv = lv->lv_next;
				index = 0;
				if (lv)
					vecp = lv->lv_iovecp;
			}
			if (record_cnt == 0 && !ordered) {
				if (!lv)
					return 0;
				break;
			}
		}
	}

	ASSERT(len == 0);

	xlog_state_finish_copy(log, iclog, record_cnt, data_cnt);
	if (!commit_iclog)
		return xlog_state_release_iclog(log, iclog);

	ASSERT(flags & XLOG_COMMIT_TRANS);
	*commit_iclog = iclog;
	return 0;
}


/*****************************************************************************
 *
 *		State Machine functions
 *
 *****************************************************************************
 */

/* Clean iclogs starting from the head.  This ordering must be
 * maintained, so an iclog doesn't become ACTIVE beyond one that
 * is SYNCING.  This is also required to maintain the notion that we use
 * a ordered wait queue to hold off would be writers to the log when every
 * iclog is trying to sync to disk.
 *
 * State Change: DIRTY -> ACTIVE
 */
STATIC void
xlog_state_clean_log(
	struct xlog *log)
{
	xlog_in_core_t	*iclog;
	int changed = 0;

	iclog = log->l_iclog;
	do {
		if (iclog->ic_state == XLOG_STATE_DIRTY) {
			iclog->ic_state	= XLOG_STATE_ACTIVE;
			iclog->ic_offset       = 0;
			ASSERT(iclog->ic_callback == NULL);
			/*
			 * If the number of ops in this iclog indicate it just
			 * contains the dummy transaction, we can
			 * change state into IDLE (the second time around).
			 * Otherwise we should change the state into
			 * NEED a dummy.
			 * We don't need to cover the dummy.
			 */
			if (!changed &&
			   (be32_to_cpu(iclog->ic_header.h_num_logops) ==
			   		XLOG_COVER_OPS)) {
				changed = 1;
			} else {
				/*
				 * We have two dirty iclogs so start over
				 * This could also be num of ops indicates
				 * this is not the dummy going out.
				 */
				changed = 2;
			}
			iclog->ic_header.h_num_logops = 0;
			memset(iclog->ic_header.h_cycle_data, 0,
			      sizeof(iclog->ic_header.h_cycle_data));
			iclog->ic_header.h_lsn = 0;
		} else if (iclog->ic_state == XLOG_STATE_ACTIVE)
			/* do nothing */;
		else
			break;	/* stop cleaning */
		iclog = iclog->ic_next;
	} while (iclog != log->l_iclog);

	/* log is locked when we are called */
	/*
	 * Change state for the dummy log recording.
	 * We usually go to NEED. But we go to NEED2 if the changed indicates
	 * we are done writing the dummy record.
	 * If we are done with the second dummy recored (DONE2), then
	 * we go to IDLE.
	 */
	if (changed) {
		switch (log->l_covered_state) {
		case XLOG_STATE_COVER_IDLE:
		case XLOG_STATE_COVER_NEED:
		case XLOG_STATE_COVER_NEED2:
			log->l_covered_state = XLOG_STATE_COVER_NEED;
			break;

		case XLOG_STATE_COVER_DONE:
			if (changed == 1)
				log->l_covered_state = XLOG_STATE_COVER_NEED2;
			else
				log->l_covered_state = XLOG_STATE_COVER_NEED;
			break;

		case XLOG_STATE_COVER_DONE2:
			if (changed == 1)
				log->l_covered_state = XLOG_STATE_COVER_IDLE;
			else
				log->l_covered_state = XLOG_STATE_COVER_NEED;
			break;

		default:
			ASSERT(0);
		}
	}
}	/* xlog_state_clean_log */

STATIC xfs_lsn_t
xlog_get_lowest_lsn(
	struct xlog		*log)
{
	struct xlog_in_core	*iclog = log->l_iclog;
	xfs_lsn_t		lowest_lsn = 0, lsn;

	do {
		if (iclog->ic_state & (XLOG_STATE_ACTIVE | XLOG_STATE_DIRTY))
			continue;

		lsn = be64_to_cpu(iclog->ic_header.h_lsn);
		if ((lsn && !lowest_lsn) || XFS_LSN_CMP(lsn, lowest_lsn) < 0)
			lowest_lsn = lsn;
	} while ((iclog = iclog->ic_next) != log->l_iclog);

	return lowest_lsn;
}

STATIC void
xlog_state_do_callback(
	struct xlog		*log,
	int			aborted,
	struct xlog_in_core	*ciclog)
{
	xlog_in_core_t	   *iclog;
	xlog_in_core_t	   *first_iclog;	/* used to know when we've
						 * processed all iclogs once */
	xfs_log_callback_t *cb, *cb_next;
	int		   flushcnt = 0;
	xfs_lsn_t	   lowest_lsn;
	int		   ioerrors;	/* counter: iclogs with errors */
	int		   loopdidcallbacks; /* flag: inner loop did callbacks*/
	int		   funcdidcallbacks; /* flag: function did callbacks */
	int		   repeats;	/* for issuing console warnings if
					 * looping too many times */
	int		   wake = 0;

	spin_lock(&log->l_icloglock);
	first_iclog = iclog = log->l_iclog;
	ioerrors = 0;
	funcdidcallbacks = 0;
	repeats = 0;

	do {
		/*
		 * Scan all iclogs starting with the one pointed to by the
		 * log.  Reset this starting point each time the log is
		 * unlocked (during callbacks).
		 *
		 * Keep looping through iclogs until one full pass is made
		 * without running any callbacks.
		 */
		first_iclog = log->l_iclog;
		iclog = log->l_iclog;
		loopdidcallbacks = 0;
		repeats++;

		do {

			/* skip all iclogs in the ACTIVE & DIRTY states */
			if (iclog->ic_state &
			    (XLOG_STATE_ACTIVE|XLOG_STATE_DIRTY)) {
				iclog = iclog->ic_next;
				continue;
			}

			/*
			 * Between marking a filesystem SHUTDOWN and stopping
			 * the log, we do flush all iclogs to disk (if there
			 * wasn't a log I/O error). So, we do want things to
			 * go smoothly in case of just a SHUTDOWN  w/o a
			 * LOG_IO_ERROR.
			 */
			if (!(iclog->ic_state & XLOG_STATE_IOERROR)) {
				/*
				 * Can only perform callbacks in order.  Since
				 * this iclog is not in the DONE_SYNC/
				 * DO_CALLBACK state, we skip the rest and
				 * just try to clean up.  If we set our iclog
				 * to DO_CALLBACK, we will not process it when
				 * we retry since a previous iclog is in the
				 * CALLBACK and the state cannot change since
				 * we are holding the l_icloglock.
				 */
				if (!(iclog->ic_state &
					(XLOG_STATE_DONE_SYNC |
						 XLOG_STATE_DO_CALLBACK))) {
					if (ciclog && (ciclog->ic_state ==
							XLOG_STATE_DONE_SYNC)) {
						ciclog->ic_state = XLOG_STATE_DO_CALLBACK;
					}
					break;
				}
				/*
				 * We now have an iclog that is in either the
				 * DO_CALLBACK or DONE_SYNC states. The other
				 * states (WANT_SYNC, SYNCING, or CALLBACK were
				 * caught by the above if and are going to
				 * clean (i.e. we aren't doing their callbacks)
				 * see the above if.
				 */

				/*
				 * We will do one more check here to see if we
				 * have chased our tail around.
				 */

				lowest_lsn = xlog_get_lowest_lsn(log);
				if (lowest_lsn &&
				    XFS_LSN_CMP(lowest_lsn,
						be64_to_cpu(iclog->ic_header.h_lsn)) < 0) {
					iclog = iclog->ic_next;
					continue; /* Leave this iclog for
						   * another thread */
				}

				iclog->ic_state = XLOG_STATE_CALLBACK;


				/*
				 * Completion of a iclog IO does not imply that
				 * a transaction has completed, as transactions
				 * can be large enough to span many iclogs. We
				 * cannot change the tail of the log half way
				 * through a transaction as this may be the only
				 * transaction in the log and moving th etail to
				 * point to the middle of it will prevent
				 * recovery from finding the start of the
				 * transaction. Hence we should only update the
				 * last_sync_lsn if this iclog contains
				 * transaction completion callbacks on it.
				 *
				 * We have to do this before we drop the
				 * icloglock to ensure we are the only one that
				 * can update it.
				 */
				ASSERT(XFS_LSN_CMP(atomic64_read(&log->l_last_sync_lsn),
					be64_to_cpu(iclog->ic_header.h_lsn)) <= 0);
				if (iclog->ic_callback)
					atomic64_set(&log->l_last_sync_lsn,
						be64_to_cpu(iclog->ic_header.h_lsn));

			} else
				ioerrors++;

			spin_unlock(&log->l_icloglock);

			/*
			 * Keep processing entries in the callback list until
			 * we come around and it is empty.  We need to
			 * atomically see that the list is empty and change the
			 * state to DIRTY so that we don't miss any more
			 * callbacks being added.
			 */
			spin_lock(&iclog->ic_callback_lock);
			cb = iclog->ic_callback;
			while (cb) {
				iclog->ic_callback_tail = &(iclog->ic_callback);
				iclog->ic_callback = NULL;
				spin_unlock(&iclog->ic_callback_lock);

				/* perform callbacks in the order given */
				for (; cb; cb = cb_next) {
					cb_next = cb->cb_next;
					cb->cb_func(cb->cb_arg, aborted);
				}
				spin_lock(&iclog->ic_callback_lock);
				cb = iclog->ic_callback;
			}

			loopdidcallbacks++;
			funcdidcallbacks++;

			spin_lock(&log->l_icloglock);
			ASSERT(iclog->ic_callback == NULL);
			spin_unlock(&iclog->ic_callback_lock);
			if (!(iclog->ic_state & XLOG_STATE_IOERROR))
				iclog->ic_state = XLOG_STATE_DIRTY;

			/*
			 * Transition from DIRTY to ACTIVE if applicable.
			 * NOP if STATE_IOERROR.
			 */
			xlog_state_clean_log(log);

			/* wake up threads waiting in xfs_log_force() */
			wake_up_all(&iclog->ic_force_wait);

			iclog = iclog->ic_next;
		} while (first_iclog != iclog);

		if (repeats > 5000) {
			flushcnt += repeats;
			repeats = 0;
			xfs_warn(log->l_mp,
				"%s: possible infinite loop (%d iterations)",
				__func__, flushcnt);
		}
	} while (!ioerrors && loopdidcallbacks);

#ifdef DEBUG
	/*
	 * Make one last gasp attempt to see if iclogs are being left in limbo.
	 * If the above loop finds an iclog earlier than the current iclog and
	 * in one of the syncing states, the current iclog is put into
	 * DO_CALLBACK and the callbacks are deferred to the completion of the
	 * earlier iclog. Walk the iclogs in order and make sure that no iclog
	 * is in DO_CALLBACK unless an earlier iclog is in one of the syncing
	 * states.
	 *
	 * Note that SYNCING|IOABORT is a valid state so we cannot just check
	 * for ic_state == SYNCING.
	 */
	if (funcdidcallbacks) {
		first_iclog = iclog = log->l_iclog;
		do {
			ASSERT(iclog->ic_state != XLOG_STATE_DO_CALLBACK);
			/*
			 * Terminate the loop if iclogs are found in states
			 * which will cause other threads to clean up iclogs.
			 *
			 * SYNCING - i/o completion will go through logs
			 * DONE_SYNC - interrupt thread should be waiting for
			 *              l_icloglock
			 * IOERROR - give up hope all ye who enter here
			 */
			if (iclog->ic_state == XLOG_STATE_WANT_SYNC ||
			    iclog->ic_state & XLOG_STATE_SYNCING ||
			    iclog->ic_state == XLOG_STATE_DONE_SYNC ||
			    iclog->ic_state == XLOG_STATE_IOERROR )
				break;
			iclog = iclog->ic_next;
		} while (first_iclog != iclog);
	}
#endif

	if (log->l_iclog->ic_state & (XLOG_STATE_ACTIVE|XLOG_STATE_IOERROR))
		wake = 1;
	spin_unlock(&log->l_icloglock);

	if (wake)
		wake_up_all(&log->l_flush_wait);
}


/*
 * Finish transitioning this iclog to the dirty state.
 *
 * Make sure that we completely execute this routine only when this is
 * the last call to the iclog.  There is a good chance that iclog flushes,
 * when we reach the end of the physical log, get turned into 2 separate
 * calls to bwrite.  Hence, one iclog flush could generate two calls to this
 * routine.  By using the reference count bwritecnt, we guarantee that only
 * the second completion goes through.
 *
 * Callbacks could take time, so they are done outside the scope of the
 * global state machine log lock.
 */
STATIC void
xlog_state_done_syncing(
	xlog_in_core_t	*iclog,
	int		aborted)
{
	struct xlog	   *log = iclog->ic_log;

	spin_lock(&log->l_icloglock);

	ASSERT(iclog->ic_state == XLOG_STATE_SYNCING ||
	       iclog->ic_state == XLOG_STATE_IOERROR);
	ASSERT(atomic_read(&iclog->ic_refcnt) == 0);
	ASSERT(iclog->ic_bwritecnt == 1 || iclog->ic_bwritecnt == 2);


	/*
	 * If we got an error, either on the first buffer, or in the case of
	 * split log writes, on the second, we mark ALL iclogs STATE_IOERROR,
	 * and none should ever be attempted to be written to disk
	 * again.
	 */
	if (iclog->ic_state != XLOG_STATE_IOERROR) {
		if (--iclog->ic_bwritecnt == 1) {
			spin_unlock(&log->l_icloglock);
			return;
		}
		iclog->ic_state = XLOG_STATE_DONE_SYNC;
	}

	/*
	 * Someone could be sleeping prior to writing out the next
	 * iclog buffer, we wake them all, one will get to do the
	 * I/O, the others get to wait for the result.
	 */
	wake_up_all(&iclog->ic_write_wait);
	spin_unlock(&log->l_icloglock);
	xlog_state_do_callback(log, aborted, iclog);	/* also cleans log */
}	/* xlog_state_done_syncing */


/*
 * If the head of the in-core log ring is not (ACTIVE or DIRTY), then we must
 * sleep.  We wait on the flush queue on the head iclog as that should be
 * the first iclog to complete flushing. Hence if all iclogs are syncing,
 * we will wait here and all new writes will sleep until a sync completes.
 *
 * The in-core logs are used in a circular fashion. They are not used
 * out-of-order even when an iclog past the head is free.
 *
 * return:
 *	* log_offset where xlog_write() can start writing into the in-core
 *		log's data space.
 *	* in-core log pointer to which xlog_write() should write.
 *	* boolean indicating this is a continued write to an in-core log.
 *		If this is the last write, then the in-core log's offset field
 *		needs to be incremented, depending on the amount of data which
 *		is copied.
 */
STATIC int
xlog_state_get_iclog_space(
	struct xlog		*log,
	int			len,
	struct xlog_in_core	**iclogp,
	struct xlog_ticket	*ticket,
	int			*continued_write,
	int			*logoffsetp)
{
	int		  log_offset;
	xlog_rec_header_t *head;
	xlog_in_core_t	  *iclog;
	int		  error;

restart:
	spin_lock(&log->l_icloglock);
	if (XLOG_FORCED_SHUTDOWN(log)) {
		spin_unlock(&log->l_icloglock);
		return -EIO;
	}

	iclog = log->l_iclog;
	if (iclog->ic_state != XLOG_STATE_ACTIVE) {
		XFS_STATS_INC(log->l_mp, xs_log_noiclogs);

		/* Wait for log writes to have flushed */
		xlog_wait(&log->l_flush_wait, &log->l_icloglock);
		goto restart;
	}

	head = &iclog->ic_header;

	atomic_inc(&iclog->ic_refcnt);	/* prevents sync */
	log_offset = iclog->ic_offset;

	/* On the 1st write to an iclog, figure out lsn.  This works
	 * if iclogs marked XLOG_STATE_WANT_SYNC always write out what they are
	 * committing to.  If the offset is set, that's how many blocks
	 * must be written.
	 */
	if (log_offset == 0) {
		ticket->t_curr_res -= log->l_iclog_hsize;
		xlog_tic_add_region(ticket,
				    log->l_iclog_hsize,
				    XLOG_REG_TYPE_LRHEADER);
		head->h_cycle = cpu_to_be32(log->l_curr_cycle);
		head->h_lsn = cpu_to_be64(
			xlog_assign_lsn(log->l_curr_cycle, log->l_curr_block));
		ASSERT(log->l_curr_block >= 0);
	}

	/* If there is enough room to write everything, then do it.  Otherwise,
	 * claim the rest of the region and make sure the XLOG_STATE_WANT_SYNC
	 * bit is on, so this will get flushed out.  Don't update ic_offset
	 * until you know exactly how many bytes get copied.  Therefore, wait
	 * until later to update ic_offset.
	 *
	 * xlog_write() algorithm assumes that at least 2 xlog_op_header_t's
	 * can fit into remaining data section.
	 */
	if (iclog->ic_size - iclog->ic_offset < 2*sizeof(xlog_op_header_t)) {
		xlog_state_switch_iclogs(log, iclog, iclog->ic_size);

		/*
		 * If I'm the only one writing to this iclog, sync it to disk.
		 * We need to do an atomic compare and decrement here to avoid
		 * racing with concurrent atomic_dec_and_lock() calls in
		 * xlog_state_release_iclog() when there is more than one
		 * reference to the iclog.
		 */
		if (!atomic_add_unless(&iclog->ic_refcnt, -1, 1)) {
			/* we are the only one */
			spin_unlock(&log->l_icloglock);
			error = xlog_state_release_iclog(log, iclog);
			if (error)
				return error;
		} else {
			spin_unlock(&log->l_icloglock);
		}
		goto restart;
	}

	/* Do we have enough room to write the full amount in the remainder
	 * of this iclog?  Or must we continue a write on the next iclog and
	 * mark this iclog as completely taken?  In the case where we switch
	 * iclogs (to mark it taken), this particular iclog will release/sync
	 * to disk in xlog_write().
	 */
	if (len <= iclog->ic_size - iclog->ic_offset) {
		*continued_write = 0;
		iclog->ic_offset += len;
	} else {
		*continued_write = 1;
		xlog_state_switch_iclogs(log, iclog, iclog->ic_size);
	}
	*iclogp = iclog;

	ASSERT(iclog->ic_offset <= iclog->ic_size);
	spin_unlock(&log->l_icloglock);

	*logoffsetp = log_offset;
	return 0;
}	/* xlog_state_get_iclog_space */

/* The first cnt-1 times through here we don't need to
 * move the grant write head because the permanent
 * reservation has reserved cnt times the unit amount.
 * Release part of current permanent unit reservation and
 * reset current reservation to be one units worth.  Also
 * move grant reservation head forward.
 */
STATIC void
xlog_regrant_reserve_log_space(
	struct xlog		*log,
	struct xlog_ticket	*ticket)
{
	trace_xfs_log_regrant_reserve_enter(log, ticket);

	if (ticket->t_cnt > 0)
		ticket->t_cnt--;

	xlog_grant_sub_space(log, &log->l_reserve_head.grant,
					ticket->t_curr_res);
	xlog_grant_sub_space(log, &log->l_write_head.grant,
					ticket->t_curr_res);
	ticket->t_curr_res = ticket->t_unit_res;
	xlog_tic_reset_res(ticket);

	trace_xfs_log_regrant_reserve_sub(log, ticket);

	/* just return if we still have some of the pre-reserved space */
	if (ticket->t_cnt > 0)
		return;

	xlog_grant_add_space(log, &log->l_reserve_head.grant,
					ticket->t_unit_res);

	trace_xfs_log_regrant_reserve_exit(log, ticket);

	ticket->t_curr_res = ticket->t_unit_res;
	xlog_tic_reset_res(ticket);
}	/* xlog_regrant_reserve_log_space */


/*
 * Give back the space left from a reservation.
 *
 * All the information we need to make a correct determination of space left
 * is present.  For non-permanent reservations, things are quite easy.  The
 * count should have been decremented to zero.  We only need to deal with the
 * space remaining in the current reservation part of the ticket.  If the
 * ticket contains a permanent reservation, there may be left over space which
 * needs to be released.  A count of N means that N-1 refills of the current
 * reservation can be done before we need to ask for more space.  The first
 * one goes to fill up the first current reservation.  Once we run out of
 * space, the count will stay at zero and the only space remaining will be
 * in the current reservation field.
 */
STATIC void
xlog_ungrant_log_space(
	struct xlog		*log,
	struct xlog_ticket	*ticket)
{
	int	bytes;

	if (ticket->t_cnt > 0)
		ticket->t_cnt--;

	trace_xfs_log_ungrant_enter(log, ticket);
	trace_xfs_log_ungrant_sub(log, ticket);

	/*
	 * If this is a permanent reservation ticket, we may be able to free
	 * up more space based on the remaining count.
	 */
	bytes = ticket->t_curr_res;
	if (ticket->t_cnt > 0) {
		ASSERT(ticket->t_flags & XLOG_TIC_PERM_RESERV);
		bytes += ticket->t_unit_res*ticket->t_cnt;
	}

	xlog_grant_sub_space(log, &log->l_reserve_head.grant, bytes);
	xlog_grant_sub_space(log, &log->l_write_head.grant, bytes);

	trace_xfs_log_ungrant_exit(log, ticket);

	xfs_log_space_wake(log->l_mp);
}

/*
 * Flush iclog to disk if this is the last reference to the given iclog and
 * the WANT_SYNC bit is set.
 *
 * When this function is entered, the iclog is not necessarily in the
 * WANT_SYNC state.  It may be sitting around waiting to get filled.
 *
 *
 */
STATIC int
xlog_state_release_iclog(
	struct xlog		*log,
	struct xlog_in_core	*iclog)
{
	int		sync = 0;	/* do we sync? */

	if (iclog->ic_state & XLOG_STATE_IOERROR)
		return -EIO;

	ASSERT(atomic_read(&iclog->ic_refcnt) > 0);
	if (!atomic_dec_and_lock(&iclog->ic_refcnt, &log->l_icloglock))
		return 0;

	if (iclog->ic_state & XLOG_STATE_IOERROR) {
		spin_unlock(&log->l_icloglock);
		return -EIO;
	}
	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE ||
	       iclog->ic_state == XLOG_STATE_WANT_SYNC);

	if (iclog->ic_state == XLOG_STATE_WANT_SYNC) {
		/* update tail before writing to iclog */
		xfs_lsn_t tail_lsn = xlog_assign_tail_lsn(log->l_mp);
		sync++;
		iclog->ic_state = XLOG_STATE_SYNCING;
		iclog->ic_header.h_tail_lsn = cpu_to_be64(tail_lsn);
		xlog_verify_tail_lsn(log, iclog, tail_lsn);
		/* cycle incremented when incrementing curr_block */
	}
	spin_unlock(&log->l_icloglock);

	/*
	 * We let the log lock go, so it's possible that we hit a log I/O
	 * error or some other SHUTDOWN condition that marks the iclog
	 * as XLOG_STATE_IOERROR before the bwrite. However, we know that
	 * this iclog has consistent data, so we ignore IOERROR
	 * flags after this point.
	 */
	if (sync)
		xlog_sync(log, iclog);
	return 0;
}	/* xlog_state_release_iclog */


/*
 * This routine will mark the current iclog in the ring as WANT_SYNC
 * and move the current iclog pointer to the next iclog in the ring.
 * When this routine is called from xlog_state_get_iclog_space(), the
 * exact size of the iclog has not yet been determined.  All we know is
 * that every data block.  We have run out of space in this log record.
 */
STATIC void
xlog_state_switch_iclogs(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	int			eventual_size)
{
	ASSERT(iclog->ic_state == XLOG_STATE_ACTIVE);
	if (!eventual_size)
		eventual_size = iclog->ic_offset;
	iclog->ic_state = XLOG_STATE_WANT_SYNC;
	iclog->ic_header.h_prev_block = cpu_to_be32(log->l_prev_block);
	log->l_prev_block = log->l_curr_block;
	log->l_prev_cycle = log->l_curr_cycle;

	/* roll log?: ic_offset changed later */
	log->l_curr_block += BTOBB(eventual_size)+BTOBB(log->l_iclog_hsize);

	/* Round up to next log-sunit */
	if (xfs_sb_version_haslogv2(&log->l_mp->m_sb) &&
	    log->l_mp->m_sb.sb_logsunit > 1) {
		uint32_t sunit_bb = BTOBB(log->l_mp->m_sb.sb_logsunit);
		log->l_curr_block = roundup(log->l_curr_block, sunit_bb);
	}

	if (log->l_curr_block >= log->l_logBBsize) {
		/*
		 * Rewind the current block before the cycle is bumped to make
		 * sure that the combined LSN never transiently moves forward
		 * when the log wraps to the next cycle. This is to support the
		 * unlocked sample of these fields from xlog_valid_lsn(). Most
		 * other cases should acquire l_icloglock.
		 */
		log->l_curr_block -= log->l_logBBsize;
		ASSERT(log->l_curr_block >= 0);
		smp_wmb();
		log->l_curr_cycle++;
		if (log->l_curr_cycle == XLOG_HEADER_MAGIC_NUM)
			log->l_curr_cycle++;
	}
	ASSERT(iclog == log->l_iclog);
	log->l_iclog = iclog->ic_next;
}	/* xlog_state_switch_iclogs */

/*
 * Write out all data in the in-core log as of this exact moment in time.
 *
 * Data may be written to the in-core log during this call.  However,
 * we don't guarantee this data will be written out.  A change from past
 * implementation means this routine will *not* write out zero length LRs.
 *
 * Basically, we try and perform an intelligent scan of the in-core logs.
 * If we determine there is no flushable data, we just return.  There is no
 * flushable data if:
 *
 *	1. the current iclog is active and has no data; the previous iclog
 *		is in the active or dirty state.
 *	2. the current iclog is drity, and the previous iclog is in the
 *		active or dirty state.
 *
 * We may sleep if:
 *
 *	1. the current iclog is not in the active nor dirty state.
 *	2. the current iclog dirty, and the previous iclog is not in the
 *		active nor dirty state.
 *	3. the current iclog is active, and there is another thread writing
 *		to this particular iclog.
 *	4. a) the current iclog is active and has no other writers
 *	   b) when we return from flushing out this iclog, it is still
 *		not in the active nor dirty state.
 */
int
xfs_log_force(
	struct xfs_mount	*mp,
	uint			flags)
{
	struct xlog		*log = mp->m_log;
	struct xlog_in_core	*iclog;
	xfs_lsn_t		lsn;

	XFS_STATS_INC(mp, xs_log_force);
	trace_xfs_log_force(mp, 0, _RET_IP_);

	xlog_cil_force(log);

	spin_lock(&log->l_icloglock);
	iclog = log->l_iclog;
	if (iclog->ic_state & XLOG_STATE_IOERROR)
		goto out_error;

	if (iclog->ic_state == XLOG_STATE_DIRTY ||
	    (iclog->ic_state == XLOG_STATE_ACTIVE &&
	     atomic_read(&iclog->ic_refcnt) == 0 && iclog->ic_offset == 0)) {
		/*
		 * If the head is dirty or (active and empty), then we need to
		 * look at the previous iclog.
		 *
		 * If the previous iclog is active or dirty we are done.  There
		 * is nothing to sync out. Otherwise, we attach ourselves to the
		 * previous iclog and go to sleep.
		 */
		iclog = iclog->ic_prev;
		if (iclog->ic_state == XLOG_STATE_ACTIVE ||
		    iclog->ic_state == XLOG_STATE_DIRTY)
			goto out_unlock;
	} else if (iclog->ic_state == XLOG_STATE_ACTIVE) {
		if (atomic_read(&iclog->ic_refcnt) == 0) {
			/*
			 * We are the only one with access to this iclog.
			 *
			 * Flush it out now.  There should be a roundoff of zero
			 * to show that someone has already taken care of the
			 * roundoff from the previous sync.
			 */
			atomic_inc(&iclog->ic_refcnt);
			lsn = be64_to_cpu(iclog->ic_header.h_lsn);
			xlog_state_switch_iclogs(log, iclog, 0);
			spin_unlock(&log->l_icloglock);

			if (xlog_state_release_iclog(log, iclog))
				return -EIO;

			spin_lock(&log->l_icloglock);
			if (be64_to_cpu(iclog->ic_header.h_lsn) != lsn ||
			    iclog->ic_state == XLOG_STATE_DIRTY)
				goto out_unlock;
		} else {
			/*
			 * Someone else is writing to this iclog.
			 *
			 * Use its call to flush out the data.  However, the
			 * other thread may not force out this LR, so we mark
			 * it WANT_SYNC.
			 */
			xlog_state_switch_iclogs(log, iclog, 0);
		}
	} else {
		/*
		 * If the head iclog is not active nor dirty, we just attach
		 * ourselves to the head and go to sleep if necessary.
		 */
		;
	}

	if (!(flags & XFS_LOG_SYNC))
		goto out_unlock;

	if (iclog->ic_state & XLOG_STATE_IOERROR)
		goto out_error;
	XFS_STATS_INC(mp, xs_log_force_sleep);
	xlog_wait(&iclog->ic_force_wait, &log->l_icloglock);
	if (iclog->ic_state & XLOG_STATE_IOERROR)
		return -EIO;
	return 0;

out_unlock:
	spin_unlock(&log->l_icloglock);
	return 0;
out_error:
	spin_unlock(&log->l_icloglock);
	return -EIO;
}

static int
__xfs_log_force_lsn(
	struct xfs_mount	*mp,
	xfs_lsn_t		lsn,
	uint			flags,
	int			*log_flushed,
	bool			already_slept)
{
	struct xlog		*log = mp->m_log;
	struct xlog_in_core	*iclog;

	spin_lock(&log->l_icloglock);
	iclog = log->l_iclog;
	if (iclog->ic_state & XLOG_STATE_IOERROR)
		goto out_error;

	while (be64_to_cpu(iclog->ic_header.h_lsn) != lsn) {
		iclog = iclog->ic_next;
		if (iclog == log->l_iclog)
			goto out_unlock;
	}

	if (iclog->ic_state == XLOG_STATE_DIRTY)
		goto out_unlock;

	if (iclog->ic_state == XLOG_STATE_ACTIVE) {
		/*
		 * We sleep here if we haven't already slept (e.g. this is the
		 * first time we've looked at the correct iclog buf) and the
		 * buffer before us is going to be sync'ed.  The reason for this
		 * is that if we are doing sync transactions here, by waiting
		 * for the previous I/O to complete, we can allow a few more
		 * transactions into this iclog before we close it down.
		 *
		 * Otherwise, we mark the buffer WANT_SYNC, and bump up the
		 * refcnt so we can release the log (which drops the ref count).
		 * The state switch keeps new transaction commits from using
		 * this buffer.  When the current commits finish writing into
		 * the buffer, the refcount will drop to zero and the buffer
		 * will go out then.
		 */
		if (!already_slept &&
		    (iclog->ic_prev->ic_state &
		     (XLOG_STATE_WANT_SYNC | XLOG_STATE_SYNCING))) {
			ASSERT(!(iclog->ic_state & XLOG_STATE_IOERROR));

			XFS_STATS_INC(mp, xs_log_force_sleep);

			xlog_wait(&iclog->ic_prev->ic_write_wait,
					&log->l_icloglock);
			return -EAGAIN;
		}
		atomic_inc(&iclog->ic_refcnt);
		xlog_state_switch_iclogs(log, iclog, 0);
		spin_unlock(&log->l_icloglock);
		if (xlog_state_release_iclog(log, iclog))
			return -EIO;
		if (log_flushed)
			*log_flushed = 1;
		spin_lock(&log->l_icloglock);
	}

	if (!(flags & XFS_LOG_SYNC) ||
	    (iclog->ic_state & (XLOG_STATE_ACTIVE | XLOG_STATE_DIRTY)))
		goto out_unlock;

	if (iclog->ic_state & XLOG_STATE_IOERROR)
		goto out_error;

	XFS_STATS_INC(mp, xs_log_force_sleep);
	xlog_wait(&iclog->ic_force_wait, &log->l_icloglock);
	if (iclog->ic_state & XLOG_STATE_IOERROR)
		return -EIO;
	return 0;

out_unlock:
	spin_unlock(&log->l_icloglock);
	return 0;
out_error:
	spin_unlock(&log->l_icloglock);
	return -EIO;
}

/*
 * Force the in-core log to disk for a specific LSN.
 *
 * Find in-core log with lsn.
 *	If it is in the DIRTY state, just return.
 *	If it is in the ACTIVE state, move the in-core log into the WANT_SYNC
 *		state and go to sleep or return.
 *	If it is in any other state, go to sleep or return.
 *
 * Synchronous forces are implemented with a wait queue.  All callers trying
 * to force a given lsn to disk must wait on the queue attached to the
 * specific in-core log.  When given in-core log finally completes its write
 * to disk, that thread will wake up all threads waiting on the queue.
 */
int
xfs_log_force_lsn(
	struct xfs_mount	*mp,
	xfs_lsn_t		lsn,
	uint			flags,
	int			*log_flushed)
{
	int			ret;
	ASSERT(lsn != 0);

	XFS_STATS_INC(mp, xs_log_force);
	trace_xfs_log_force(mp, lsn, _RET_IP_);

	lsn = xlog_cil_force_lsn(mp->m_log, lsn);
	if (lsn == NULLCOMMITLSN)
		return 0;

	ret = __xfs_log_force_lsn(mp, lsn, flags, log_flushed, false);
	if (ret == -EAGAIN)
		ret = __xfs_log_force_lsn(mp, lsn, flags, log_flushed, true);
	return ret;
}

/*
 * Called when we want to mark the current iclog as being ready to sync to
 * disk.
 */
STATIC void
xlog_state_want_sync(
	struct xlog		*log,
	struct xlog_in_core	*iclog)
{
	assert_spin_locked(&log->l_icloglock);

	if (iclog->ic_state == XLOG_STATE_ACTIVE) {
		xlog_state_switch_iclogs(log, iclog, 0);
	} else {
		ASSERT(iclog->ic_state &
			(XLOG_STATE_WANT_SYNC|XLOG_STATE_IOERROR));
	}
}


/*****************************************************************************
 *
 *		TICKET functions
 *
 *****************************************************************************
 */

/*
 * Free a used ticket when its refcount falls to zero.
 */
void
xfs_log_ticket_put(
	xlog_ticket_t	*ticket)
{
	ASSERT(atomic_read(&ticket->t_ref) > 0);
	if (atomic_dec_and_test(&ticket->t_ref))
		kmem_zone_free(xfs_log_ticket_zone, ticket);
}

xlog_ticket_t *
xfs_log_ticket_get(
	xlog_ticket_t	*ticket)
{
	ASSERT(atomic_read(&ticket->t_ref) > 0);
	atomic_inc(&ticket->t_ref);
	return ticket;
}

/*
 * Figure out the total log space unit (in bytes) that would be
 * required for a log ticket.
 */
int
xfs_log_calc_unit_res(
	struct xfs_mount	*mp,
	int			unit_bytes)
{
	struct xlog		*log = mp->m_log;
	int			iclog_space;
	uint			num_headers;

	/*
	 * Permanent reservations have up to 'cnt'-1 active log operations
	 * in the log.  A unit in this case is the amount of space for one
	 * of these log operations.  Normal reservations have a cnt of 1
	 * and their unit amount is the total amount of space required.
	 *
	 * The following lines of code account for non-transaction data
	 * which occupy space in the on-disk log.
	 *
	 * Normal form of a transaction is:
	 * <oph><trans-hdr><start-oph><reg1-oph><reg1><reg2-oph>...<commit-oph>
	 * and then there are LR hdrs, split-recs and roundoff at end of syncs.
	 *
	 * We need to account for all the leadup data and trailer data
	 * around the transaction data.
	 * And then we need to account for the worst case in terms of using
	 * more space.
	 * The worst case will happen if:
	 * - the placement of the transaction happens to be such that the
	 *   roundoff is at its maximum
	 * - the transaction data is synced before the commit record is synced
	 *   i.e. <transaction-data><roundoff> | <commit-rec><roundoff>
	 *   Therefore the commit record is in its own Log Record.
	 *   This can happen as the commit record is called with its
	 *   own region to xlog_write().
	 *   This then means that in the worst case, roundoff can happen for
	 *   the commit-rec as well.
	 *   The commit-rec is smaller than padding in this scenario and so it is
	 *   not added separately.
	 */

	/* for trans header */
	unit_bytes += sizeof(xlog_op_header_t);
	unit_bytes += sizeof(xfs_trans_header_t);

	/* for start-rec */
	unit_bytes += sizeof(xlog_op_header_t);

	/*
	 * for LR headers - the space for data in an iclog is the size minus
	 * the space used for the headers. If we use the iclog size, then we
	 * undercalculate the number of headers required.
	 *
	 * Furthermore - the addition of op headers for split-recs might
	 * increase the space required enough to require more log and op
	 * headers, so take that into account too.
	 *
	 * IMPORTANT: This reservation makes the assumption that if this
	 * transaction is the first in an iclog and hence has the LR headers
	 * accounted to it, then the remaining space in the iclog is
	 * exclusively for this transaction.  i.e. if the transaction is larger
	 * than the iclog, it will be the only thing in that iclog.
	 * Fundamentally, this means we must pass the entire log vector to
	 * xlog_write to guarantee this.
	 */
	iclog_space = log->l_iclog_size - log->l_iclog_hsize;
	num_headers = howmany(unit_bytes, iclog_space);

	/* for split-recs - ophdrs added when data split over LRs */
	unit_bytes += sizeof(xlog_op_header_t) * num_headers;

	/* add extra header reservations if we overrun */
	while (!num_headers ||
	       howmany(unit_bytes, iclog_space) > num_headers) {
		unit_bytes += sizeof(xlog_op_header_t);
		num_headers++;
	}
	unit_bytes += log->l_iclog_hsize * num_headers;

	/* for commit-rec LR header - note: padding will subsume the ophdr */
	unit_bytes += log->l_iclog_hsize;

	/* for roundoff padding for transaction data and one for commit record */
	if (xfs_sb_version_haslogv2(&mp->m_sb) && mp->m_sb.sb_logsunit > 1) {
		/* log su roundoff */
		unit_bytes += 2 * mp->m_sb.sb_logsunit;
	} else {
		/* BB roundoff */
		unit_bytes += 2 * BBSIZE;
        }

	return unit_bytes;
}

/*
 * Allocate and initialise a new log ticket.
 */
struct xlog_ticket *
xlog_ticket_alloc(
	struct xlog		*log,
	int			unit_bytes,
	int			cnt,
	char			client,
	bool			permanent,
	xfs_km_flags_t		alloc_flags)
{
	struct xlog_ticket	*tic;
	int			unit_res;

	tic = kmem_zone_zalloc(xfs_log_ticket_zone, alloc_flags);
	if (!tic)
		return NULL;

	unit_res = xfs_log_calc_unit_res(log->l_mp, unit_bytes);

	atomic_set(&tic->t_ref, 1);
	tic->t_task		= current;
	INIT_LIST_HEAD(&tic->t_queue);
	tic->t_unit_res		= unit_res;
	tic->t_curr_res		= unit_res;
	tic->t_cnt		= cnt;
	tic->t_ocnt		= cnt;
	tic->t_tid		= prandom_u32();
	tic->t_clientid		= client;
	tic->t_flags		= XLOG_TIC_INITED;
	if (permanent)
		tic->t_flags |= XLOG_TIC_PERM_RESERV;

	xlog_tic_reset_res(tic);

	return tic;
}


/******************************************************************************
 *
 *		Log debug routines
 *
 ******************************************************************************
 */
#if defined(DEBUG)
/*
 * Make sure that the destination ptr is within the valid data region of
 * one of the iclogs.  This uses backup pointers stored in a different
 * part of the log in case we trash the log structure.
 */
STATIC void
xlog_verify_dest_ptr(
	struct xlog	*log,
	void		*ptr)
{
	int i;
	int good_ptr = 0;

	for (i = 0; i < log->l_iclog_bufs; i++) {
		if (ptr >= log->l_iclog_bak[i] &&
		    ptr <= log->l_iclog_bak[i] + log->l_iclog_size)
			good_ptr++;
	}

	if (!good_ptr)
		xfs_emerg(log->l_mp, "%s: invalid ptr", __func__);
}

/*
 * Check to make sure the grant write head didn't just over lap the tail.  If
 * the cycles are the same, we can't be overlapping.  Otherwise, make sure that
 * the cycles differ by exactly one and check the byte count.
 *
 * This check is run unlocked, so can give false positives. Rather than assert
 * on failures, use a warn-once flag and a panic tag to allow the admin to
 * determine if they want to panic the machine when such an error occurs. For
 * debug kernels this will have the same effect as using an assert but, unlinke
 * an assert, it can be turned off at runtime.
 */
STATIC void
xlog_verify_grant_tail(
	struct xlog	*log)
{
	int		tail_cycle, tail_blocks;
	int		cycle, space;

	xlog_crack_grant_head(&log->l_write_head.grant, &cycle, &space);
	xlog_crack_atomic_lsn(&log->l_tail_lsn, &tail_cycle, &tail_blocks);
	if (tail_cycle != cycle) {
		if (cycle - 1 != tail_cycle &&
		    !(log->l_flags & XLOG_TAIL_WARN)) {
			xfs_alert_tag(log->l_mp, XFS_PTAG_LOGRES,
				"%s: cycle - 1 != tail_cycle", __func__);
			log->l_flags |= XLOG_TAIL_WARN;
		}

		if (space > BBTOB(tail_blocks) &&
		    !(log->l_flags & XLOG_TAIL_WARN)) {
			xfs_alert_tag(log->l_mp, XFS_PTAG_LOGRES,
				"%s: space > BBTOB(tail_blocks)", __func__);
			log->l_flags |= XLOG_TAIL_WARN;
		}
	}
}

/* check if it will fit */
STATIC void
xlog_verify_tail_lsn(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	xfs_lsn_t		tail_lsn)
{
    int blocks;

    if (CYCLE_LSN(tail_lsn) == log->l_prev_cycle) {
	blocks =
	    log->l_logBBsize - (log->l_prev_block - BLOCK_LSN(tail_lsn));
	if (blocks < BTOBB(iclog->ic_offset)+BTOBB(log->l_iclog_hsize))
		xfs_emerg(log->l_mp, "%s: ran out of log space", __func__);
    } else {
	ASSERT(CYCLE_LSN(tail_lsn)+1 == log->l_prev_cycle);

	if (BLOCK_LSN(tail_lsn) == log->l_prev_block)
		xfs_emerg(log->l_mp, "%s: tail wrapped", __func__);

	blocks = BLOCK_LSN(tail_lsn) - log->l_prev_block;
	if (blocks < BTOBB(iclog->ic_offset) + 1)
		xfs_emerg(log->l_mp, "%s: ran out of log space", __func__);
    }
}	/* xlog_verify_tail_lsn */

/*
 * Perform a number of checks on the iclog before writing to disk.
 *
 * 1. Make sure the iclogs are still circular
 * 2. Make sure we have a good magic number
 * 3. Make sure we don't have magic numbers in the data
 * 4. Check fields of each log operation header for:
 *	A. Valid client identifier
 *	B. tid ptr value falls in valid ptr space (user space code)
 *	C. Length in log record header is correct according to the
 *		individual operation headers within record.
 * 5. When a bwrite will occur within 5 blocks of the front of the physical
 *	log, check the preceding blocks of the physical log to make sure all
 *	the cycle numbers agree with the current cycle number.
 */
STATIC void
xlog_verify_iclog(
	struct xlog		*log,
	struct xlog_in_core	*iclog,
	int			count,
	bool                    syncing)
{
	xlog_op_header_t	*ophead;
	xlog_in_core_t		*icptr;
	xlog_in_core_2_t	*xhdr;
	void			*base_ptr, *ptr, *p;
	ptrdiff_t		field_offset;
	uint8_t			clientid;
	int			len, i, j, k, op_len;
	int			idx;

	/* check validity of iclog pointers */
	spin_lock(&log->l_icloglock);
	icptr = log->l_iclog;
	for (i = 0; i < log->l_iclog_bufs; i++, icptr = icptr->ic_next)
		ASSERT(icptr);

	if (icptr != log->l_iclog)
		xfs_emerg(log->l_mp, "%s: corrupt iclog ring", __func__);
	spin_unlock(&log->l_icloglock);

	/* check log magic numbers */
	if (iclog->ic_header.h_magicno != cpu_to_be32(XLOG_HEADER_MAGIC_NUM))
		xfs_emerg(log->l_mp, "%s: invalid magic num", __func__);

	base_ptr = ptr = &iclog->ic_header;
	p = &iclog->ic_header;
	for (ptr += BBSIZE; ptr < base_ptr + count; ptr += BBSIZE) {
		if (*(__be32 *)ptr == cpu_to_be32(XLOG_HEADER_MAGIC_NUM))
			xfs_emerg(log->l_mp, "%s: unexpected magic num",
				__func__);
	}

	/* check fields */
	len = be32_to_cpu(iclog->ic_header.h_num_logops);
	base_ptr = ptr = iclog->ic_datap;
	ophead = ptr;
	xhdr = iclog->ic_data;
	for (i = 0; i < len; i++) {
		ophead = ptr;

		/* clientid is only 1 byte */
		p = &ophead->oh_clientid;
		field_offset = p - base_ptr;
		if (!syncing || (field_offset & 0x1ff)) {
			clientid = ophead->oh_clientid;
		} else {
			idx = BTOBBT((char *)&ophead->oh_clientid - iclog->ic_datap);
			if (idx >= (XLOG_HEADER_CYCLE_SIZE / BBSIZE)) {
				j = idx / (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
				k = idx % (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
				clientid = xlog_get_client_id(
					xhdr[j].hic_xheader.xh_cycle_data[k]);
			} else {
				clientid = xlog_get_client_id(
					iclog->ic_header.h_cycle_data[idx]);
			}
		}
		if (clientid != XFS_TRANSACTION && clientid != XFS_LOG)
			xfs_warn(log->l_mp,
				"%s: invalid clientid %d op "PTR_FMT" offset 0x%lx",
				__func__, clientid, ophead,
				(unsigned long)field_offset);

		/* check length */
		p = &ophead->oh_len;
		field_offset = p - base_ptr;
		if (!syncing || (field_offset & 0x1ff)) {
			op_len = be32_to_cpu(ophead->oh_len);
		} else {
			idx = BTOBBT((uintptr_t)&ophead->oh_len -
				    (uintptr_t)iclog->ic_datap);
			if (idx >= (XLOG_HEADER_CYCLE_SIZE / BBSIZE)) {
				j = idx / (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
				k = idx % (XLOG_HEADER_CYCLE_SIZE / BBSIZE);
				op_len = be32_to_cpu(xhdr[j].hic_xheader.xh_cycle_data[k]);
			} else {
				op_len = be32_to_cpu(iclog->ic_header.h_cycle_data[idx]);
			}
		}
		ptr += sizeof(xlog_op_header_t) + op_len;
	}
}	/* xlog_verify_iclog */
#endif

/*
 * Mark all iclogs IOERROR. l_icloglock is held by the caller.
 */
STATIC int
xlog_state_ioerror(
	struct xlog	*log)
{
	xlog_in_core_t	*iclog, *ic;

	iclog = log->l_iclog;
	if (! (iclog->ic_state & XLOG_STATE_IOERROR)) {
		/*
		 * Mark all the incore logs IOERROR.
		 * From now on, no log flushes will result.
		 */
		ic = iclog;
		do {
			ic->ic_state = XLOG_STATE_IOERROR;
			ic = ic->ic_next;
		} while (ic != iclog);
		return 0;
	}
	/*
	 * Return non-zero, if state transition has already happened.
	 */
	return 1;
}

/*
 * This is called from xfs_force_shutdown, when we're forcibly
 * shutting down the filesystem, typically because of an IO error.
 * Our main objectives here are to make sure that:
 *	a. if !logerror, flush the logs to disk. Anything modified
 *	   after this is ignored.
 *	b. the filesystem gets marked 'SHUTDOWN' for all interested
 *	   parties to find out, 'atomically'.
 *	c. those who're sleeping on log reservations, pinned objects and
 *	    other resources get woken up, and be told the bad news.
 *	d. nothing new gets queued up after (b) and (c) are done.
 *
 * Note: for the !logerror case we need to flush the regions held in memory out
 * to disk first. This needs to be done before the log is marked as shutdown,
 * otherwise the iclog writes will fail.
 */
int
xfs_log_force_umount(
	struct xfs_mount	*mp,
	int			logerror)
{
	struct xlog	*log;
	int		retval;

	log = mp->m_log;

	/*
	 * If this happens during log recovery, don't worry about
	 * locking; the log isn't open for business yet.
	 */
	if (!log ||
	    log->l_flags & XLOG_ACTIVE_RECOVERY) {
		mp->m_flags |= XFS_MOUNT_FS_SHUTDOWN;
		if (mp->m_sb_bp)
			mp->m_sb_bp->b_flags |= XBF_DONE;
		return 0;
	}

	/*
	 * Somebody could've already done the hard work for us.
	 * No need to get locks for this.
	 */
	if (logerror && log->l_iclog->ic_state & XLOG_STATE_IOERROR) {
		ASSERT(XLOG_FORCED_SHUTDOWN(log));
		return 1;
	}

	/*
	 * Flush all the completed transactions to disk before marking the log
	 * being shut down. We need to do it in this order to ensure that
	 * completed operations are safely on disk before we shut down, and that
	 * we don't have to issue any buffer IO after the shutdown flags are set
	 * to guarantee this.
	 */
	if (!logerror)
		xfs_log_force(mp, XFS_LOG_SYNC);

	/*
	 * mark the filesystem and the as in a shutdown state and wake
	 * everybody up to tell them the bad news.
	 */
	spin_lock(&log->l_icloglock);
	mp->m_flags |= XFS_MOUNT_FS_SHUTDOWN;
	if (mp->m_sb_bp)
		mp->m_sb_bp->b_flags |= XBF_DONE;

	/*
	 * Mark the log and the iclogs with IO error flags to prevent any
	 * further log IO from being issued or completed.
	 */
	log->l_flags |= XLOG_IO_ERROR;
	retval = xlog_state_ioerror(log);
	spin_unlock(&log->l_icloglock);

	/*
	 * We don't want anybody waiting for log reservations after this. That
	 * means we have to wake up everybody queued up on reserveq as well as
	 * writeq.  In addition, we make sure in xlog_{re}grant_log_space that
	 * we don't enqueue anything once the SHUTDOWN flag is set, and this
	 * action is protected by the grant locks.
	 */
	xlog_grant_head_wake_all(&log->l_reserve_head);
	xlog_grant_head_wake_all(&log->l_write_head);

	/*
	 * Wake up everybody waiting on xfs_log_force. Wake the CIL push first
	 * as if the log writes were completed. The abort handling in the log
	 * item committed callback functions will do this again under lock to
	 * avoid races.
	 */
	wake_up_all(&log->l_cilp->xc_commit_wait);
	xlog_state_do_callback(log, XFS_LI_ABORTED, NULL);

#ifdef XFSERRORDEBUG
	{
		xlog_in_core_t	*iclog;

		spin_lock(&log->l_icloglock);
		iclog = log->l_iclog;
		do {
			ASSERT(iclog->ic_callback == 0);
			iclog = iclog->ic_next;
		} while (iclog != log->l_iclog);
		spin_unlock(&log->l_icloglock);
	}
#endif
	/* return non-zero if log IOERROR transition had already happened */
	return retval;
}

STATIC int
xlog_iclogs_empty(
	struct xlog	*log)
{
	xlog_in_core_t	*iclog;

	iclog = log->l_iclog;
	do {
		/* endianness does not matter here, zero is zero in
		 * any language.
		 */
		if (iclog->ic_header.h_num_logops)
			return 0;
		iclog = iclog->ic_next;
	} while (iclog != log->l_iclog);
	return 1;
}

/*
 * Verify that an LSN stamped into a piece of metadata is valid. This is
 * intended for use in read verifiers on v5 superblocks.
 */
bool
xfs_log_check_lsn(
	struct xfs_mount	*mp,
	xfs_lsn_t		lsn)
{
	struct xlog		*log = mp->m_log;
	bool			valid;

	/*
	 * norecovery mode skips mount-time log processing and unconditionally
	 * resets the in-core LSN. We can't validate in this mode, but
	 * modifications are not allowed anyways so just return true.
	 */
	if (mp->m_flags & XFS_MOUNT_NORECOVERY)
		return true;

	/*
	 * Some metadata LSNs are initialized to NULL (e.g., the agfl). This is
	 * handled by recovery and thus safe to ignore here.
	 */
	if (lsn == NULLCOMMITLSN)
		return true;

	valid = xlog_valid_lsn(mp->m_log, lsn);

	/* warn the user about what's gone wrong before verifier failure */
	if (!valid) {
		spin_lock(&log->l_icloglock);
		xfs_warn(mp,
"Corruption warning: Metadata has LSN (%d:%d) ahead of current LSN (%d:%d). "
"Please unmount and run xfs_repair (>= v4.3) to resolve.",
			 CYCLE_LSN(lsn), BLOCK_LSN(lsn),
			 log->l_curr_cycle, log->l_curr_block);
		spin_unlock(&log->l_icloglock);
	}

	return valid;
}

bool
xfs_log_in_recovery(
	struct xfs_mount	*mp)
{
	struct xlog		*log = mp->m_log;

	return log->l_flags & XLOG_ACTIVE_RECOVERY;
}
