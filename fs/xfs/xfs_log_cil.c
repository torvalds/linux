/*
 * Copyright (c) 2010 Red Hat, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_types.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_log_priv.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_error.h"
#include "xfs_alloc.h"
#include "xfs_discard.h"

/*
 * Perform initial CIL structure initialisation. If the CIL is not
 * enabled in this filesystem, ensure the log->l_cilp is null so
 * we can check this conditional to determine if we are doing delayed
 * logging or not.
 */
int
xlog_cil_init(
	struct log	*log)
{
	struct xfs_cil	*cil;
	struct xfs_cil_ctx *ctx;

	log->l_cilp = NULL;
	if (!(log->l_mp->m_flags & XFS_MOUNT_DELAYLOG))
		return 0;

	cil = kmem_zalloc(sizeof(*cil), KM_SLEEP|KM_MAYFAIL);
	if (!cil)
		return ENOMEM;

	ctx = kmem_zalloc(sizeof(*ctx), KM_SLEEP|KM_MAYFAIL);
	if (!ctx) {
		kmem_free(cil);
		return ENOMEM;
	}

	INIT_LIST_HEAD(&cil->xc_cil);
	INIT_LIST_HEAD(&cil->xc_committing);
	spin_lock_init(&cil->xc_cil_lock);
	init_rwsem(&cil->xc_ctx_lock);
	init_waitqueue_head(&cil->xc_commit_wait);

	INIT_LIST_HEAD(&ctx->committing);
	INIT_LIST_HEAD(&ctx->busy_extents);
	ctx->sequence = 1;
	ctx->cil = cil;
	cil->xc_ctx = ctx;
	cil->xc_current_sequence = ctx->sequence;

	cil->xc_log = log;
	log->l_cilp = cil;
	return 0;
}

void
xlog_cil_destroy(
	struct log	*log)
{
	if (!log->l_cilp)
		return;

	if (log->l_cilp->xc_ctx) {
		if (log->l_cilp->xc_ctx->ticket)
			xfs_log_ticket_put(log->l_cilp->xc_ctx->ticket);
		kmem_free(log->l_cilp->xc_ctx);
	}

	ASSERT(list_empty(&log->l_cilp->xc_cil));
	kmem_free(log->l_cilp);
}

/*
 * Allocate a new ticket. Failing to get a new ticket makes it really hard to
 * recover, so we don't allow failure here. Also, we allocate in a context that
 * we don't want to be issuing transactions from, so we need to tell the
 * allocation code this as well.
 *
 * We don't reserve any space for the ticket - we are going to steal whatever
 * space we require from transactions as they commit. To ensure we reserve all
 * the space required, we need to set the current reservation of the ticket to
 * zero so that we know to steal the initial transaction overhead from the
 * first transaction commit.
 */
static struct xlog_ticket *
xlog_cil_ticket_alloc(
	struct log	*log)
{
	struct xlog_ticket *tic;

	tic = xlog_ticket_alloc(log, 0, 1, XFS_TRANSACTION, 0,
				KM_SLEEP|KM_NOFS);
	tic->t_trans_type = XFS_TRANS_CHECKPOINT;

	/*
	 * set the current reservation to zero so we know to steal the basic
	 * transaction overhead reservation from the first transaction commit.
	 */
	tic->t_curr_res = 0;
	return tic;
}

/*
 * After the first stage of log recovery is done, we know where the head and
 * tail of the log are. We need this log initialisation done before we can
 * initialise the first CIL checkpoint context.
 *
 * Here we allocate a log ticket to track space usage during a CIL push.  This
 * ticket is passed to xlog_write() directly so that we don't slowly leak log
 * space by failing to account for space used by log headers and additional
 * region headers for split regions.
 */
void
xlog_cil_init_post_recovery(
	struct log	*log)
{
	if (!log->l_cilp)
		return;

	log->l_cilp->xc_ctx->ticket = xlog_cil_ticket_alloc(log);
	log->l_cilp->xc_ctx->sequence = 1;
	log->l_cilp->xc_ctx->commit_lsn = xlog_assign_lsn(log->l_curr_cycle,
								log->l_curr_block);
}

/*
 * Format log item into a flat buffers
 *
 * For delayed logging, we need to hold a formatted buffer containing all the
 * changes on the log item. This enables us to relog the item in memory and
 * write it out asynchronously without needing to relock the object that was
 * modified at the time it gets written into the iclog.
 *
 * This function builds a vector for the changes in each log item in the
 * transaction. It then works out the length of the buffer needed for each log
 * item, allocates them and formats the vector for the item into the buffer.
 * The buffer is then attached to the log item are then inserted into the
 * Committed Item List for tracking until the next checkpoint is written out.
 *
 * We don't set up region headers during this process; we simply copy the
 * regions into the flat buffer. We can do this because we still have to do a
 * formatting step to write the regions into the iclog buffer.  Writing the
 * ophdrs during the iclog write means that we can support splitting large
 * regions across iclog boundares without needing a change in the format of the
 * item/region encapsulation.
 *
 * Hence what we need to do now is change the rewrite the vector array to point
 * to the copied region inside the buffer we just allocated. This allows us to
 * format the regions into the iclog as though they are being formatted
 * directly out of the objects themselves.
 */
static void
xlog_cil_format_items(
	struct log		*log,
	struct xfs_log_vec	*log_vector)
{
	struct xfs_log_vec *lv;

	ASSERT(log_vector);
	for (lv = log_vector; lv; lv = lv->lv_next) {
		void	*ptr;
		int	index;
		int	len = 0;

		/* build the vector array and calculate it's length */
		IOP_FORMAT(lv->lv_item, lv->lv_iovecp);
		for (index = 0; index < lv->lv_niovecs; index++)
			len += lv->lv_iovecp[index].i_len;

		lv->lv_buf_len = len;
		lv->lv_buf = kmem_alloc(lv->lv_buf_len, KM_SLEEP|KM_NOFS);
		ptr = lv->lv_buf;

		for (index = 0; index < lv->lv_niovecs; index++) {
			struct xfs_log_iovec *vec = &lv->lv_iovecp[index];

			memcpy(ptr, vec->i_addr, vec->i_len);
			vec->i_addr = ptr;
			ptr += vec->i_len;
		}
		ASSERT(ptr == lv->lv_buf + lv->lv_buf_len);
	}
}

/*
 * Prepare the log item for insertion into the CIL. Calculate the difference in
 * log space and vectors it will consume, and if it is a new item pin it as
 * well.
 */
STATIC void
xfs_cil_prepare_item(
	struct log		*log,
	struct xfs_log_vec	*lv,
	int			*len,
	int			*diff_iovecs)
{
	struct xfs_log_vec	*old = lv->lv_item->li_lv;

	if (old) {
		/* existing lv on log item, space used is a delta */
		ASSERT(!list_empty(&lv->lv_item->li_cil));
		ASSERT(old->lv_buf && old->lv_buf_len && old->lv_niovecs);

		*len += lv->lv_buf_len - old->lv_buf_len;
		*diff_iovecs += lv->lv_niovecs - old->lv_niovecs;
		kmem_free(old->lv_buf);
		kmem_free(old);
	} else {
		/* new lv, must pin the log item */
		ASSERT(!lv->lv_item->li_lv);
		ASSERT(list_empty(&lv->lv_item->li_cil));

		*len += lv->lv_buf_len;
		*diff_iovecs += lv->lv_niovecs;
		IOP_PIN(lv->lv_item);

	}

	/* attach new log vector to log item */
	lv->lv_item->li_lv = lv;

	/*
	 * If this is the first time the item is being committed to the
	 * CIL, store the sequence number on the log item so we can
	 * tell in future commits whether this is the first checkpoint
	 * the item is being committed into.
	 */
	if (!lv->lv_item->li_seq)
		lv->lv_item->li_seq = log->l_cilp->xc_ctx->sequence;
}

/*
 * Insert the log items into the CIL and calculate the difference in space
 * consumed by the item. Add the space to the checkpoint ticket and calculate
 * if the change requires additional log metadata. If it does, take that space
 * as well. Remove the amount of space we addded to the checkpoint ticket from
 * the current transaction ticket so that the accounting works out correctly.
 */
static void
xlog_cil_insert_items(
	struct log		*log,
	struct xfs_log_vec	*log_vector,
	struct xlog_ticket	*ticket)
{
	struct xfs_cil		*cil = log->l_cilp;
	struct xfs_cil_ctx	*ctx = cil->xc_ctx;
	struct xfs_log_vec	*lv;
	int			len = 0;
	int			diff_iovecs = 0;
	int			iclog_space;

	ASSERT(log_vector);

	/*
	 * Do all the accounting aggregation and switching of log vectors
	 * around in a separate loop to the insertion of items into the CIL.
	 * Then we can do a separate loop to update the CIL within a single
	 * lock/unlock pair. This reduces the number of round trips on the CIL
	 * lock from O(nr_logvectors) to O(1) and greatly reduces the overall
	 * hold time for the transaction commit.
	 *
	 * If this is the first time the item is being placed into the CIL in
	 * this context, pin it so it can't be written to disk until the CIL is
	 * flushed to the iclog and the iclog written to disk.
	 *
	 * We can do this safely because the context can't checkpoint until we
	 * are done so it doesn't matter exactly how we update the CIL.
	 */
	for (lv = log_vector; lv; lv = lv->lv_next)
		xfs_cil_prepare_item(log, lv, &len, &diff_iovecs);

	/* account for space used by new iovec headers  */
	len += diff_iovecs * sizeof(xlog_op_header_t);

	spin_lock(&cil->xc_cil_lock);

	/* move the items to the tail of the CIL */
	for (lv = log_vector; lv; lv = lv->lv_next)
		list_move_tail(&lv->lv_item->li_cil, &cil->xc_cil);

	ctx->nvecs += diff_iovecs;

	/*
	 * Now transfer enough transaction reservation to the context ticket
	 * for the checkpoint. The context ticket is special - the unit
	 * reservation has to grow as well as the current reservation as we
	 * steal from tickets so we can correctly determine the space used
	 * during the transaction commit.
	 */
	if (ctx->ticket->t_curr_res == 0) {
		/* first commit in checkpoint, steal the header reservation */
		ASSERT(ticket->t_curr_res >= ctx->ticket->t_unit_res + len);
		ctx->ticket->t_curr_res = ctx->ticket->t_unit_res;
		ticket->t_curr_res -= ctx->ticket->t_unit_res;
	}

	/* do we need space for more log record headers? */
	iclog_space = log->l_iclog_size - log->l_iclog_hsize;
	if (len > 0 && (ctx->space_used / iclog_space !=
				(ctx->space_used + len) / iclog_space)) {
		int hdrs;

		hdrs = (len + iclog_space - 1) / iclog_space;
		/* need to take into account split region headers, too */
		hdrs *= log->l_iclog_hsize + sizeof(struct xlog_op_header);
		ctx->ticket->t_unit_res += hdrs;
		ctx->ticket->t_curr_res += hdrs;
		ticket->t_curr_res -= hdrs;
		ASSERT(ticket->t_curr_res >= len);
	}
	ticket->t_curr_res -= len;
	ctx->space_used += len;

	spin_unlock(&cil->xc_cil_lock);
}

static void
xlog_cil_free_logvec(
	struct xfs_log_vec	*log_vector)
{
	struct xfs_log_vec	*lv;

	for (lv = log_vector; lv; ) {
		struct xfs_log_vec *next = lv->lv_next;
		kmem_free(lv->lv_buf);
		kmem_free(lv);
		lv = next;
	}
}

/*
 * Mark all items committed and clear busy extents. We free the log vector
 * chains in a separate pass so that we unpin the log items as quickly as
 * possible.
 */
static void
xlog_cil_committed(
	void	*args,
	int	abort)
{
	struct xfs_cil_ctx	*ctx = args;
	struct xfs_mount	*mp = ctx->cil->xc_log->l_mp;

	xfs_trans_committed_bulk(ctx->cil->xc_log->l_ailp, ctx->lv_chain,
					ctx->start_lsn, abort);

	xfs_alloc_busy_sort(&ctx->busy_extents);
	xfs_alloc_busy_clear(mp, &ctx->busy_extents,
			     (mp->m_flags & XFS_MOUNT_DISCARD) && !abort);

	spin_lock(&ctx->cil->xc_cil_lock);
	list_del(&ctx->committing);
	spin_unlock(&ctx->cil->xc_cil_lock);

	xlog_cil_free_logvec(ctx->lv_chain);

	if (!list_empty(&ctx->busy_extents)) {
		ASSERT(mp->m_flags & XFS_MOUNT_DISCARD);

		xfs_discard_extents(mp, &ctx->busy_extents);
		xfs_alloc_busy_clear(mp, &ctx->busy_extents, false);
	}

	kmem_free(ctx);
}

/*
 * Push the Committed Item List to the log. If @push_seq flag is zero, then it
 * is a background flush and so we can chose to ignore it. Otherwise, if the
 * current sequence is the same as @push_seq we need to do a flush. If
 * @push_seq is less than the current sequence, then it has already been
 * flushed and we don't need to do anything - the caller will wait for it to
 * complete if necessary.
 *
 * @push_seq is a value rather than a flag because that allows us to do an
 * unlocked check of the sequence number for a match. Hence we can allows log
 * forces to run racily and not issue pushes for the same sequence twice. If we
 * get a race between multiple pushes for the same sequence they will block on
 * the first one and then abort, hence avoiding needless pushes.
 */
STATIC int
xlog_cil_push(
	struct log		*log,
	xfs_lsn_t		push_seq)
{
	struct xfs_cil		*cil = log->l_cilp;
	struct xfs_log_vec	*lv;
	struct xfs_cil_ctx	*ctx;
	struct xfs_cil_ctx	*new_ctx;
	struct xlog_in_core	*commit_iclog;
	struct xlog_ticket	*tic;
	int			num_lv;
	int			num_iovecs;
	int			len;
	int			error = 0;
	struct xfs_trans_header thdr;
	struct xfs_log_iovec	lhdr;
	struct xfs_log_vec	lvhdr = { NULL };
	xfs_lsn_t		commit_lsn;

	if (!cil)
		return 0;

	ASSERT(!push_seq || push_seq <= cil->xc_ctx->sequence);

	new_ctx = kmem_zalloc(sizeof(*new_ctx), KM_SLEEP|KM_NOFS);
	new_ctx->ticket = xlog_cil_ticket_alloc(log);

	/*
	 * Lock out transaction commit, but don't block for background pushes
	 * unless we are well over the CIL space limit. See the definition of
	 * XLOG_CIL_HARD_SPACE_LIMIT() for the full explanation of the logic
	 * used here.
	 */
	if (!down_write_trylock(&cil->xc_ctx_lock)) {
		if (!push_seq &&
		    cil->xc_ctx->space_used < XLOG_CIL_HARD_SPACE_LIMIT(log))
			goto out_free_ticket;
		down_write(&cil->xc_ctx_lock);
	}
	ctx = cil->xc_ctx;

	/* check if we've anything to push */
	if (list_empty(&cil->xc_cil))
		goto out_skip;

	/* check for spurious background flush */
	if (!push_seq && cil->xc_ctx->space_used < XLOG_CIL_SPACE_LIMIT(log))
		goto out_skip;

	/* check for a previously pushed seqeunce */
	if (push_seq && push_seq < cil->xc_ctx->sequence)
		goto out_skip;

	/*
	 * pull all the log vectors off the items in the CIL, and
	 * remove the items from the CIL. We don't need the CIL lock
	 * here because it's only needed on the transaction commit
	 * side which is currently locked out by the flush lock.
	 */
	lv = NULL;
	num_lv = 0;
	num_iovecs = 0;
	len = 0;
	while (!list_empty(&cil->xc_cil)) {
		struct xfs_log_item	*item;
		int			i;

		item = list_first_entry(&cil->xc_cil,
					struct xfs_log_item, li_cil);
		list_del_init(&item->li_cil);
		if (!ctx->lv_chain)
			ctx->lv_chain = item->li_lv;
		else
			lv->lv_next = item->li_lv;
		lv = item->li_lv;
		item->li_lv = NULL;

		num_lv++;
		num_iovecs += lv->lv_niovecs;
		for (i = 0; i < lv->lv_niovecs; i++)
			len += lv->lv_iovecp[i].i_len;
	}

	/*
	 * initialise the new context and attach it to the CIL. Then attach
	 * the current context to the CIL committing lsit so it can be found
	 * during log forces to extract the commit lsn of the sequence that
	 * needs to be forced.
	 */
	INIT_LIST_HEAD(&new_ctx->committing);
	INIT_LIST_HEAD(&new_ctx->busy_extents);
	new_ctx->sequence = ctx->sequence + 1;
	new_ctx->cil = cil;
	cil->xc_ctx = new_ctx;

	/*
	 * mirror the new sequence into the cil structure so that we can do
	 * unlocked checks against the current sequence in log forces without
	 * risking deferencing a freed context pointer.
	 */
	cil->xc_current_sequence = new_ctx->sequence;

	/*
	 * The switch is now done, so we can drop the context lock and move out
	 * of a shared context. We can't just go straight to the commit record,
	 * though - we need to synchronise with previous and future commits so
	 * that the commit records are correctly ordered in the log to ensure
	 * that we process items during log IO completion in the correct order.
	 *
	 * For example, if we get an EFI in one checkpoint and the EFD in the
	 * next (e.g. due to log forces), we do not want the checkpoint with
	 * the EFD to be committed before the checkpoint with the EFI.  Hence
	 * we must strictly order the commit records of the checkpoints so
	 * that: a) the checkpoint callbacks are attached to the iclogs in the
	 * correct order; and b) the checkpoints are replayed in correct order
	 * in log recovery.
	 *
	 * Hence we need to add this context to the committing context list so
	 * that higher sequences will wait for us to write out a commit record
	 * before they do.
	 */
	spin_lock(&cil->xc_cil_lock);
	list_add(&ctx->committing, &cil->xc_committing);
	spin_unlock(&cil->xc_cil_lock);
	up_write(&cil->xc_ctx_lock);

	/*
	 * Build a checkpoint transaction header and write it to the log to
	 * begin the transaction. We need to account for the space used by the
	 * transaction header here as it is not accounted for in xlog_write().
	 *
	 * The LSN we need to pass to the log items on transaction commit is
	 * the LSN reported by the first log vector write. If we use the commit
	 * record lsn then we can move the tail beyond the grant write head.
	 */
	tic = ctx->ticket;
	thdr.th_magic = XFS_TRANS_HEADER_MAGIC;
	thdr.th_type = XFS_TRANS_CHECKPOINT;
	thdr.th_tid = tic->t_tid;
	thdr.th_num_items = num_iovecs;
	lhdr.i_addr = &thdr;
	lhdr.i_len = sizeof(xfs_trans_header_t);
	lhdr.i_type = XLOG_REG_TYPE_TRANSHDR;
	tic->t_curr_res -= lhdr.i_len + sizeof(xlog_op_header_t);

	lvhdr.lv_niovecs = 1;
	lvhdr.lv_iovecp = &lhdr;
	lvhdr.lv_next = ctx->lv_chain;

	error = xlog_write(log, &lvhdr, tic, &ctx->start_lsn, NULL, 0);
	if (error)
		goto out_abort_free_ticket;

	/*
	 * now that we've written the checkpoint into the log, strictly
	 * order the commit records so replay will get them in the right order.
	 */
restart:
	spin_lock(&cil->xc_cil_lock);
	list_for_each_entry(new_ctx, &cil->xc_committing, committing) {
		/*
		 * Higher sequences will wait for this one so skip them.
		 * Don't wait for own own sequence, either.
		 */
		if (new_ctx->sequence >= ctx->sequence)
			continue;
		if (!new_ctx->commit_lsn) {
			/*
			 * It is still being pushed! Wait for the push to
			 * complete, then start again from the beginning.
			 */
			xlog_wait(&cil->xc_commit_wait, &cil->xc_cil_lock);
			goto restart;
		}
	}
	spin_unlock(&cil->xc_cil_lock);

	/* xfs_log_done always frees the ticket on error. */
	commit_lsn = xfs_log_done(log->l_mp, tic, &commit_iclog, 0);
	if (commit_lsn == -1)
		goto out_abort;

	/* attach all the transactions w/ busy extents to iclog */
	ctx->log_cb.cb_func = xlog_cil_committed;
	ctx->log_cb.cb_arg = ctx;
	error = xfs_log_notify(log->l_mp, commit_iclog, &ctx->log_cb);
	if (error)
		goto out_abort;

	/*
	 * now the checkpoint commit is complete and we've attached the
	 * callbacks to the iclog we can assign the commit LSN to the context
	 * and wake up anyone who is waiting for the commit to complete.
	 */
	spin_lock(&cil->xc_cil_lock);
	ctx->commit_lsn = commit_lsn;
	wake_up_all(&cil->xc_commit_wait);
	spin_unlock(&cil->xc_cil_lock);

	/* release the hounds! */
	return xfs_log_release_iclog(log->l_mp, commit_iclog);

out_skip:
	up_write(&cil->xc_ctx_lock);
out_free_ticket:
	xfs_log_ticket_put(new_ctx->ticket);
	kmem_free(new_ctx);
	return 0;

out_abort_free_ticket:
	xfs_log_ticket_put(tic);
out_abort:
	xlog_cil_committed(ctx, XFS_LI_ABORTED);
	return XFS_ERROR(EIO);
}

/*
 * Commit a transaction with the given vector to the Committed Item List.
 *
 * To do this, we need to format the item, pin it in memory if required and
 * account for the space used by the transaction. Once we have done that we
 * need to release the unused reservation for the transaction, attach the
 * transaction to the checkpoint context so we carry the busy extents through
 * to checkpoint completion, and then unlock all the items in the transaction.
 *
 * For more specific information about the order of operations in
 * xfs_log_commit_cil() please refer to the comments in
 * xfs_trans_commit_iclog().
 *
 * Called with the context lock already held in read mode to lock out
 * background commit, returns without it held once background commits are
 * allowed again.
 */
void
xfs_log_commit_cil(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	struct xfs_log_vec	*log_vector,
	xfs_lsn_t		*commit_lsn,
	int			flags)
{
	struct log		*log = mp->m_log;
	int			log_flags = 0;
	int			push = 0;

	if (flags & XFS_TRANS_RELEASE_LOG_RES)
		log_flags = XFS_LOG_REL_PERM_RESERV;

	/*
	 * do all the hard work of formatting items (including memory
	 * allocation) outside the CIL context lock. This prevents stalling CIL
	 * pushes when we are low on memory and a transaction commit spends a
	 * lot of time in memory reclaim.
	 */
	xlog_cil_format_items(log, log_vector);

	/* lock out background commit */
	down_read(&log->l_cilp->xc_ctx_lock);
	if (commit_lsn)
		*commit_lsn = log->l_cilp->xc_ctx->sequence;

	xlog_cil_insert_items(log, log_vector, tp->t_ticket);

	/* check we didn't blow the reservation */
	if (tp->t_ticket->t_curr_res < 0)
		xlog_print_tic_res(log->l_mp, tp->t_ticket);

	/* attach the transaction to the CIL if it has any busy extents */
	if (!list_empty(&tp->t_busy)) {
		spin_lock(&log->l_cilp->xc_cil_lock);
		list_splice_init(&tp->t_busy,
					&log->l_cilp->xc_ctx->busy_extents);
		spin_unlock(&log->l_cilp->xc_cil_lock);
	}

	tp->t_commit_lsn = *commit_lsn;
	xfs_log_done(mp, tp->t_ticket, NULL, log_flags);
	xfs_trans_unreserve_and_mod_sb(tp);

	/*
	 * Once all the items of the transaction have been copied to the CIL,
	 * the items can be unlocked and freed.
	 *
	 * This needs to be done before we drop the CIL context lock because we
	 * have to update state in the log items and unlock them before they go
	 * to disk. If we don't, then the CIL checkpoint can race with us and
	 * we can run checkpoint completion before we've updated and unlocked
	 * the log items. This affects (at least) processing of stale buffers,
	 * inodes and EFIs.
	 */
	xfs_trans_free_items(tp, *commit_lsn, 0);

	/* check for background commit before unlock */
	if (log->l_cilp->xc_ctx->space_used > XLOG_CIL_SPACE_LIMIT(log))
		push = 1;

	up_read(&log->l_cilp->xc_ctx_lock);

	/*
	 * We need to push CIL every so often so we don't cache more than we
	 * can fit in the log. The limit really is that a checkpoint can't be
	 * more than half the log (the current checkpoint is not allowed to
	 * overwrite the previous checkpoint), but commit latency and memory
	 * usage limit this to a smaller size in most cases.
	 */
	if (push)
		xlog_cil_push(log, 0);
}

/*
 * Conditionally push the CIL based on the sequence passed in.
 *
 * We only need to push if we haven't already pushed the sequence
 * number given. Hence the only time we will trigger a push here is
 * if the push sequence is the same as the current context.
 *
 * We return the current commit lsn to allow the callers to determine if a
 * iclog flush is necessary following this call.
 *
 * XXX: Initially, just push the CIL unconditionally and return whatever
 * commit lsn is there. It'll be empty, so this is broken for now.
 */
xfs_lsn_t
xlog_cil_force_lsn(
	struct log	*log,
	xfs_lsn_t	sequence)
{
	struct xfs_cil		*cil = log->l_cilp;
	struct xfs_cil_ctx	*ctx;
	xfs_lsn_t		commit_lsn = NULLCOMMITLSN;

	ASSERT(sequence <= cil->xc_current_sequence);

	/*
	 * check to see if we need to force out the current context.
	 * xlog_cil_push() handles racing pushes for the same sequence,
	 * so no need to deal with it here.
	 */
	if (sequence == cil->xc_current_sequence)
		xlog_cil_push(log, sequence);

	/*
	 * See if we can find a previous sequence still committing.
	 * We need to wait for all previous sequence commits to complete
	 * before allowing the force of push_seq to go ahead. Hence block
	 * on commits for those as well.
	 */
restart:
	spin_lock(&cil->xc_cil_lock);
	list_for_each_entry(ctx, &cil->xc_committing, committing) {
		if (ctx->sequence > sequence)
			continue;
		if (!ctx->commit_lsn) {
			/*
			 * It is still being pushed! Wait for the push to
			 * complete, then start again from the beginning.
			 */
			xlog_wait(&cil->xc_commit_wait, &cil->xc_cil_lock);
			goto restart;
		}
		if (ctx->sequence != sequence)
			continue;
		/* found it! */
		commit_lsn = ctx->commit_lsn;
	}
	spin_unlock(&cil->xc_cil_lock);
	return commit_lsn;
}

/*
 * Check if the current log item was first committed in this sequence.
 * We can't rely on just the log item being in the CIL, we have to check
 * the recorded commit sequence number.
 *
 * Note: for this to be used in a non-racy manner, it has to be called with
 * CIL flushing locked out. As a result, it should only be used during the
 * transaction commit process when deciding what to format into the item.
 */
bool
xfs_log_item_in_current_chkpt(
	struct xfs_log_item *lip)
{
	struct xfs_cil_ctx *ctx;

	if (!(lip->li_mountp->m_flags & XFS_MOUNT_DELAYLOG))
		return false;
	if (list_empty(&lip->li_cil))
		return false;

	ctx = lip->li_mountp->m_log->l_cilp->xc_ctx;

	/*
	 * li_seq is written on the first commit of a log item to record the
	 * first checkpoint it is written to. Hence if it is different to the
	 * current sequence, we're in a new checkpoint.
	 */
	if (XFS_LSN_CMP(lip->li_seq, ctx->sequence) != 0)
		return false;
	return true;
}
