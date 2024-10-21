// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2010 Red Hat, Inc. All Rights Reserved.
 */

#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_shared.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_extent_busy.h"
#include "xfs_trans.h"
#include "xfs_trans_priv.h"
#include "xfs_log.h"
#include "xfs_log_priv.h"
#include "xfs_trace.h"
#include "xfs_discard.h"

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
	struct xlog	*log)
{
	struct xlog_ticket *tic;

	tic = xlog_ticket_alloc(log, 0, 1, 0);

	/*
	 * set the current reservation to zero so we know to steal the basic
	 * transaction overhead reservation from the first transaction commit.
	 */
	tic->t_curr_res = 0;
	tic->t_iclog_hdrs = 0;
	return tic;
}

static inline void
xlog_cil_set_iclog_hdr_count(struct xfs_cil *cil)
{
	struct xlog	*log = cil->xc_log;

	atomic_set(&cil->xc_iclog_hdrs,
		   (XLOG_CIL_BLOCKING_SPACE_LIMIT(log) /
			(log->l_iclog_size - log->l_iclog_hsize)));
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
static bool
xlog_item_in_current_chkpt(
	struct xfs_cil		*cil,
	struct xfs_log_item	*lip)
{
	if (test_bit(XLOG_CIL_EMPTY, &cil->xc_flags))
		return false;

	/*
	 * li_seq is written on the first commit of a log item to record the
	 * first checkpoint it is written to. Hence if it is different to the
	 * current sequence, we're in a new checkpoint.
	 */
	return lip->li_seq == READ_ONCE(cil->xc_current_sequence);
}

bool
xfs_log_item_in_current_chkpt(
	struct xfs_log_item *lip)
{
	return xlog_item_in_current_chkpt(lip->li_log->l_cilp, lip);
}

/*
 * Unavoidable forward declaration - xlog_cil_push_work() calls
 * xlog_cil_ctx_alloc() itself.
 */
static void xlog_cil_push_work(struct work_struct *work);

static struct xfs_cil_ctx *
xlog_cil_ctx_alloc(void)
{
	struct xfs_cil_ctx	*ctx;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL | __GFP_NOFAIL);
	INIT_LIST_HEAD(&ctx->committing);
	INIT_LIST_HEAD(&ctx->busy_extents.extent_list);
	INIT_LIST_HEAD(&ctx->log_items);
	INIT_LIST_HEAD(&ctx->lv_chain);
	INIT_WORK(&ctx->push_work, xlog_cil_push_work);
	return ctx;
}

/*
 * Aggregate the CIL per cpu structures into global counts, lists, etc and
 * clear the percpu state ready for the next context to use. This is called
 * from the push code with the context lock held exclusively, hence nothing else
 * will be accessing or modifying the per-cpu counters.
 */
static void
xlog_cil_push_pcp_aggregate(
	struct xfs_cil		*cil,
	struct xfs_cil_ctx	*ctx)
{
	struct xlog_cil_pcp	*cilpcp;
	int			cpu;

	for_each_cpu(cpu, &ctx->cil_pcpmask) {
		cilpcp = per_cpu_ptr(cil->xc_pcp, cpu);

		ctx->ticket->t_curr_res += cilpcp->space_reserved;
		cilpcp->space_reserved = 0;

		if (!list_empty(&cilpcp->busy_extents)) {
			list_splice_init(&cilpcp->busy_extents,
					&ctx->busy_extents.extent_list);
		}
		if (!list_empty(&cilpcp->log_items))
			list_splice_init(&cilpcp->log_items, &ctx->log_items);

		/*
		 * We're in the middle of switching cil contexts.  Reset the
		 * counter we use to detect when the current context is nearing
		 * full.
		 */
		cilpcp->space_used = 0;
	}
}

/*
 * Aggregate the CIL per-cpu space used counters into the global atomic value.
 * This is called when the per-cpu counter aggregation will first pass the soft
 * limit threshold so we can switch to atomic counter aggregation for accurate
 * detection of hard limit traversal.
 */
static void
xlog_cil_insert_pcp_aggregate(
	struct xfs_cil		*cil,
	struct xfs_cil_ctx	*ctx)
{
	int			cpu;
	int			count = 0;

	/* Trigger atomic updates then aggregate only for the first caller */
	if (!test_and_clear_bit(XLOG_CIL_PCP_SPACE, &cil->xc_flags))
		return;

	/*
	 * We can race with other cpus setting cil_pcpmask.  However, we've
	 * atomically cleared PCP_SPACE which forces other threads to add to
	 * the global space used count.  cil_pcpmask is a superset of cilpcp
	 * structures that could have a nonzero space_used.
	 */
	for_each_cpu(cpu, &ctx->cil_pcpmask) {
		struct xlog_cil_pcp	*cilpcp = per_cpu_ptr(cil->xc_pcp, cpu);
		int			old = READ_ONCE(cilpcp->space_used);

		while (!try_cmpxchg(&cilpcp->space_used, &old, 0))
			;
		count += old;
	}
	atomic_add(count, &ctx->space_used);
}

static void
xlog_cil_ctx_switch(
	struct xfs_cil		*cil,
	struct xfs_cil_ctx	*ctx)
{
	xlog_cil_set_iclog_hdr_count(cil);
	set_bit(XLOG_CIL_EMPTY, &cil->xc_flags);
	set_bit(XLOG_CIL_PCP_SPACE, &cil->xc_flags);
	ctx->sequence = ++cil->xc_current_sequence;
	ctx->cil = cil;
	cil->xc_ctx = ctx;
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
	struct xlog	*log)
{
	log->l_cilp->xc_ctx->ticket = xlog_cil_ticket_alloc(log);
	log->l_cilp->xc_ctx->sequence = 1;
	xlog_cil_set_iclog_hdr_count(log->l_cilp);
}

static inline int
xlog_cil_iovec_space(
	uint	niovecs)
{
	return round_up((sizeof(struct xfs_log_vec) +
					niovecs * sizeof(struct xfs_log_iovec)),
			sizeof(uint64_t));
}

/*
 * Allocate or pin log vector buffers for CIL insertion.
 *
 * The CIL currently uses disposable buffers for copying a snapshot of the
 * modified items into the log during a push. The biggest problem with this is
 * the requirement to allocate the disposable buffer during the commit if:
 *	a) does not exist; or
 *	b) it is too small
 *
 * If we do this allocation within xlog_cil_insert_format_items(), it is done
 * under the xc_ctx_lock, which means that a CIL push cannot occur during
 * the memory allocation. This means that we have a potential deadlock situation
 * under low memory conditions when we have lots of dirty metadata pinned in
 * the CIL and we need a CIL commit to occur to free memory.
 *
 * To avoid this, we need to move the memory allocation outside the
 * xc_ctx_lock, but because the log vector buffers are disposable, that opens
 * up a TOCTOU race condition w.r.t. the CIL committing and removing the log
 * vector buffers between the check and the formatting of the item into the
 * log vector buffer within the xc_ctx_lock.
 *
 * Because the log vector buffer needs to be unchanged during the CIL push
 * process, we cannot share the buffer between the transaction commit (which
 * modifies the buffer) and the CIL push context that is writing the changes
 * into the log. This means skipping preallocation of buffer space is
 * unreliable, but we most definitely do not want to be allocating and freeing
 * buffers unnecessarily during commits when overwrites can be done safely.
 *
 * The simplest solution to this problem is to allocate a shadow buffer when a
 * log item is committed for the second time, and then to only use this buffer
 * if necessary. The buffer can remain attached to the log item until such time
 * it is needed, and this is the buffer that is reallocated to match the size of
 * the incoming modification. Then during the formatting of the item we can swap
 * the active buffer with the new one if we can't reuse the existing buffer. We
 * don't free the old buffer as it may be reused on the next modification if
 * it's size is right, otherwise we'll free and reallocate it at that point.
 *
 * This function builds a vector for the changes in each log item in the
 * transaction. It then works out the length of the buffer needed for each log
 * item, allocates them and attaches the vector to the log item in preparation
 * for the formatting step which occurs under the xc_ctx_lock.
 *
 * While this means the memory footprint goes up, it avoids the repeated
 * alloc/free pattern that repeated modifications of an item would otherwise
 * cause, and hence minimises the CPU overhead of such behaviour.
 */
static void
xlog_cil_alloc_shadow_bufs(
	struct xlog		*log,
	struct xfs_trans	*tp)
{
	struct xfs_log_item	*lip;

	list_for_each_entry(lip, &tp->t_items, li_trans) {
		struct xfs_log_vec *lv;
		int	niovecs = 0;
		int	nbytes = 0;
		int	buf_size;
		bool	ordered = false;

		/* Skip items which aren't dirty in this transaction. */
		if (!test_bit(XFS_LI_DIRTY, &lip->li_flags))
			continue;

		/* get number of vecs and size of data to be stored */
		lip->li_ops->iop_size(lip, &niovecs, &nbytes);

		/*
		 * Ordered items need to be tracked but we do not wish to write
		 * them. We need a logvec to track the object, but we do not
		 * need an iovec or buffer to be allocated for copying data.
		 */
		if (niovecs == XFS_LOG_VEC_ORDERED) {
			ordered = true;
			niovecs = 0;
			nbytes = 0;
		}

		/*
		 * We 64-bit align the length of each iovec so that the start of
		 * the next one is naturally aligned.  We'll need to account for
		 * that slack space here.
		 *
		 * We also add the xlog_op_header to each region when
		 * formatting, but that's not accounted to the size of the item
		 * at this point. Hence we'll need an addition number of bytes
		 * for each vector to hold an opheader.
		 *
		 * Then round nbytes up to 64-bit alignment so that the initial
		 * buffer alignment is easy to calculate and verify.
		 */
		nbytes += niovecs *
			(sizeof(uint64_t) + sizeof(struct xlog_op_header));
		nbytes = round_up(nbytes, sizeof(uint64_t));

		/*
		 * The data buffer needs to start 64-bit aligned, so round up
		 * that space to ensure we can align it appropriately and not
		 * overrun the buffer.
		 */
		buf_size = nbytes + xlog_cil_iovec_space(niovecs);

		/*
		 * if we have no shadow buffer, or it is too small, we need to
		 * reallocate it.
		 */
		if (!lip->li_lv_shadow ||
		    buf_size > lip->li_lv_shadow->lv_size) {
			/*
			 * We free and allocate here as a realloc would copy
			 * unnecessary data. We don't use kvzalloc() for the
			 * same reason - we don't need to zero the data area in
			 * the buffer, only the log vector header and the iovec
			 * storage.
			 */
			kvfree(lip->li_lv_shadow);
			lv = xlog_kvmalloc(buf_size);

			memset(lv, 0, xlog_cil_iovec_space(niovecs));

			INIT_LIST_HEAD(&lv->lv_list);
			lv->lv_item = lip;
			lv->lv_size = buf_size;
			if (ordered)
				lv->lv_buf_len = XFS_LOG_VEC_ORDERED;
			else
				lv->lv_iovecp = (struct xfs_log_iovec *)&lv[1];
			lip->li_lv_shadow = lv;
		} else {
			/* same or smaller, optimise common overwrite case */
			lv = lip->li_lv_shadow;
			if (ordered)
				lv->lv_buf_len = XFS_LOG_VEC_ORDERED;
			else
				lv->lv_buf_len = 0;
			lv->lv_bytes = 0;
		}

		/* Ensure the lv is set up according to ->iop_size */
		lv->lv_niovecs = niovecs;

		/* The allocated data region lies beyond the iovec region */
		lv->lv_buf = (char *)lv + xlog_cil_iovec_space(niovecs);
	}

}

/*
 * Prepare the log item for insertion into the CIL. Calculate the difference in
 * log space it will consume, and if it is a new item pin it as well.
 */
STATIC void
xfs_cil_prepare_item(
	struct xlog		*log,
	struct xfs_log_vec	*lv,
	struct xfs_log_vec	*old_lv,
	int			*diff_len)
{
	/* Account for the new LV being passed in */
	if (lv->lv_buf_len != XFS_LOG_VEC_ORDERED)
		*diff_len += lv->lv_bytes;

	/*
	 * If there is no old LV, this is the first time we've seen the item in
	 * this CIL context and so we need to pin it. If we are replacing the
	 * old_lv, then remove the space it accounts for and make it the shadow
	 * buffer for later freeing. In both cases we are now switching to the
	 * shadow buffer, so update the pointer to it appropriately.
	 */
	if (!old_lv) {
		if (lv->lv_item->li_ops->iop_pin)
			lv->lv_item->li_ops->iop_pin(lv->lv_item);
		lv->lv_item->li_lv_shadow = NULL;
	} else if (old_lv != lv) {
		ASSERT(lv->lv_buf_len != XFS_LOG_VEC_ORDERED);

		*diff_len -= old_lv->lv_bytes;
		lv->lv_item->li_lv_shadow = old_lv;
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
 * Format log item into a flat buffers
 *
 * For delayed logging, we need to hold a formatted buffer containing all the
 * changes on the log item. This enables us to relog the item in memory and
 * write it out asynchronously without needing to relock the object that was
 * modified at the time it gets written into the iclog.
 *
 * This function takes the prepared log vectors attached to each log item, and
 * formats the changes into the log vector buffer. The buffer it uses is
 * dependent on the current state of the vector in the CIL - the shadow lv is
 * guaranteed to be large enough for the current modification, but we will only
 * use that if we can't reuse the existing lv. If we can't reuse the existing
 * lv, then simple swap it out for the shadow lv. We don't free it - that is
 * done lazily either by th enext modification or the freeing of the log item.
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
xlog_cil_insert_format_items(
	struct xlog		*log,
	struct xfs_trans	*tp,
	int			*diff_len)
{
	struct xfs_log_item	*lip;

	/* Bail out if we didn't find a log item.  */
	if (list_empty(&tp->t_items)) {
		ASSERT(0);
		return;
	}

	list_for_each_entry(lip, &tp->t_items, li_trans) {
		struct xfs_log_vec *lv;
		struct xfs_log_vec *old_lv = NULL;
		struct xfs_log_vec *shadow;
		bool	ordered = false;

		/* Skip items which aren't dirty in this transaction. */
		if (!test_bit(XFS_LI_DIRTY, &lip->li_flags))
			continue;

		/*
		 * The formatting size information is already attached to
		 * the shadow lv on the log item.
		 */
		shadow = lip->li_lv_shadow;
		if (shadow->lv_buf_len == XFS_LOG_VEC_ORDERED)
			ordered = true;

		/* Skip items that do not have any vectors for writing */
		if (!shadow->lv_niovecs && !ordered)
			continue;

		/* compare to existing item size */
		old_lv = lip->li_lv;
		if (lip->li_lv && shadow->lv_size <= lip->li_lv->lv_size) {
			/* same or smaller, optimise common overwrite case */
			lv = lip->li_lv;

			if (ordered)
				goto insert;

			/*
			 * set the item up as though it is a new insertion so
			 * that the space reservation accounting is correct.
			 */
			*diff_len -= lv->lv_bytes;

			/* Ensure the lv is set up according to ->iop_size */
			lv->lv_niovecs = shadow->lv_niovecs;

			/* reset the lv buffer information for new formatting */
			lv->lv_buf_len = 0;
			lv->lv_bytes = 0;
			lv->lv_buf = (char *)lv +
					xlog_cil_iovec_space(lv->lv_niovecs);
		} else {
			/* switch to shadow buffer! */
			lv = shadow;
			lv->lv_item = lip;
			if (ordered) {
				/* track as an ordered logvec */
				ASSERT(lip->li_lv == NULL);
				goto insert;
			}
		}

		ASSERT(IS_ALIGNED((unsigned long)lv->lv_buf, sizeof(uint64_t)));
		lip->li_ops->iop_format(lip, lv);
insert:
		xfs_cil_prepare_item(log, lv, old_lv, diff_len);
	}
}

/*
 * The use of lockless waitqueue_active() requires that the caller has
 * serialised itself against the wakeup call in xlog_cil_push_work(). That
 * can be done by either holding the push lock or the context lock.
 */
static inline bool
xlog_cil_over_hard_limit(
	struct xlog	*log,
	int32_t		space_used)
{
	if (waitqueue_active(&log->l_cilp->xc_push_wait))
		return true;
	if (space_used >= XLOG_CIL_BLOCKING_SPACE_LIMIT(log))
		return true;
	return false;
}

/*
 * Insert the log items into the CIL and calculate the difference in space
 * consumed by the item. Add the space to the checkpoint ticket and calculate
 * if the change requires additional log metadata. If it does, take that space
 * as well. Remove the amount of space we added to the checkpoint ticket from
 * the current transaction ticket so that the accounting works out correctly.
 */
static void
xlog_cil_insert_items(
	struct xlog		*log,
	struct xfs_trans	*tp,
	uint32_t		released_space)
{
	struct xfs_cil		*cil = log->l_cilp;
	struct xfs_cil_ctx	*ctx = cil->xc_ctx;
	struct xfs_log_item	*lip;
	int			len = 0;
	int			iovhdr_res = 0, split_res = 0, ctx_res = 0;
	int			space_used;
	int			order;
	unsigned int		cpu_nr;
	struct xlog_cil_pcp	*cilpcp;

	ASSERT(tp);

	/*
	 * We can do this safely because the context can't checkpoint until we
	 * are done so it doesn't matter exactly how we update the CIL.
	 */
	xlog_cil_insert_format_items(log, tp, &len);

	/*
	 * Subtract the space released by intent cancelation from the space we
	 * consumed so that we remove it from the CIL space and add it back to
	 * the current transaction reservation context.
	 */
	len -= released_space;

	/*
	 * Grab the per-cpu pointer for the CIL before we start any accounting.
	 * That ensures that we are running with pre-emption disabled and so we
	 * can't be scheduled away between split sample/update operations that
	 * are done without outside locking to serialise them.
	 */
	cpu_nr = get_cpu();
	cilpcp = this_cpu_ptr(cil->xc_pcp);

	/* Tell the future push that there was work added by this CPU. */
	if (!cpumask_test_cpu(cpu_nr, &ctx->cil_pcpmask))
		cpumask_test_and_set_cpu(cpu_nr, &ctx->cil_pcpmask);

	/*
	 * We need to take the CIL checkpoint unit reservation on the first
	 * commit into the CIL. Test the XLOG_CIL_EMPTY bit first so we don't
	 * unnecessarily do an atomic op in the fast path here. We can clear the
	 * XLOG_CIL_EMPTY bit as we are under the xc_ctx_lock here and that
	 * needs to be held exclusively to reset the XLOG_CIL_EMPTY bit.
	 */
	if (test_bit(XLOG_CIL_EMPTY, &cil->xc_flags) &&
	    test_and_clear_bit(XLOG_CIL_EMPTY, &cil->xc_flags))
		ctx_res = ctx->ticket->t_unit_res;

	/*
	 * Check if we need to steal iclog headers. atomic_read() is not a
	 * locked atomic operation, so we can check the value before we do any
	 * real atomic ops in the fast path. If we've already taken the CIL unit
	 * reservation from this commit, we've already got one iclog header
	 * space reserved so we have to account for that otherwise we risk
	 * overrunning the reservation on this ticket.
	 *
	 * If the CIL is already at the hard limit, we might need more header
	 * space that originally reserved. So steal more header space from every
	 * commit that occurs once we are over the hard limit to ensure the CIL
	 * push won't run out of reservation space.
	 *
	 * This can steal more than we need, but that's OK.
	 *
	 * The cil->xc_ctx_lock provides the serialisation necessary for safely
	 * calling xlog_cil_over_hard_limit() in this context.
	 */
	space_used = atomic_read(&ctx->space_used) + cilpcp->space_used + len;
	if (atomic_read(&cil->xc_iclog_hdrs) > 0 ||
	    xlog_cil_over_hard_limit(log, space_used)) {
		split_res = log->l_iclog_hsize +
					sizeof(struct xlog_op_header);
		if (ctx_res)
			ctx_res += split_res * (tp->t_ticket->t_iclog_hdrs - 1);
		else
			ctx_res = split_res * tp->t_ticket->t_iclog_hdrs;
		atomic_sub(tp->t_ticket->t_iclog_hdrs, &cil->xc_iclog_hdrs);
	}
	cilpcp->space_reserved += ctx_res;

	/*
	 * Accurately account when over the soft limit, otherwise fold the
	 * percpu count into the global count if over the per-cpu threshold.
	 */
	if (!test_bit(XLOG_CIL_PCP_SPACE, &cil->xc_flags)) {
		atomic_add(len, &ctx->space_used);
	} else if (cilpcp->space_used + len >
			(XLOG_CIL_SPACE_LIMIT(log) / num_online_cpus())) {
		space_used = atomic_add_return(cilpcp->space_used + len,
						&ctx->space_used);
		cilpcp->space_used = 0;

		/*
		 * If we just transitioned over the soft limit, we need to
		 * transition to the global atomic counter.
		 */
		if (space_used >= XLOG_CIL_SPACE_LIMIT(log))
			xlog_cil_insert_pcp_aggregate(cil, ctx);
	} else {
		cilpcp->space_used += len;
	}
	/* attach the transaction to the CIL if it has any busy extents */
	if (!list_empty(&tp->t_busy))
		list_splice_init(&tp->t_busy, &cilpcp->busy_extents);

	/*
	 * Now update the order of everything modified in the transaction
	 * and insert items into the CIL if they aren't already there.
	 * We do this here so we only need to take the CIL lock once during
	 * the transaction commit.
	 */
	order = atomic_inc_return(&ctx->order_id);
	list_for_each_entry(lip, &tp->t_items, li_trans) {
		/* Skip items which aren't dirty in this transaction. */
		if (!test_bit(XFS_LI_DIRTY, &lip->li_flags))
			continue;

		lip->li_order_id = order;
		if (!list_empty(&lip->li_cil))
			continue;
		list_add_tail(&lip->li_cil, &cilpcp->log_items);
	}
	put_cpu();

	/*
	 * If we've overrun the reservation, dump the tx details before we move
	 * the log items. Shutdown is imminent...
	 */
	tp->t_ticket->t_curr_res -= ctx_res + len;
	if (WARN_ON(tp->t_ticket->t_curr_res < 0)) {
		xfs_warn(log->l_mp, "Transaction log reservation overrun:");
		xfs_warn(log->l_mp,
			 "  log items: %d bytes (iov hdrs: %d bytes)",
			 len, iovhdr_res);
		xfs_warn(log->l_mp, "  split region headers: %d bytes",
			 split_res);
		xfs_warn(log->l_mp, "  ctx ticket: %d bytes", ctx_res);
		xlog_print_trans(tp);
		xlog_force_shutdown(log, SHUTDOWN_LOG_IO_ERROR);
	}
}

static inline void
xlog_cil_ail_insert_batch(
	struct xfs_ail		*ailp,
	struct xfs_ail_cursor	*cur,
	struct xfs_log_item	**log_items,
	int			nr_items,
	xfs_lsn_t		commit_lsn)
{
	int	i;

	spin_lock(&ailp->ail_lock);
	/* xfs_trans_ail_update_bulk drops ailp->ail_lock */
	xfs_trans_ail_update_bulk(ailp, cur, log_items, nr_items, commit_lsn);

	for (i = 0; i < nr_items; i++) {
		struct xfs_log_item *lip = log_items[i];

		if (lip->li_ops->iop_unpin)
			lip->li_ops->iop_unpin(lip, 0);
	}
}

/*
 * Take the checkpoint's log vector chain of items and insert the attached log
 * items into the AIL. This uses bulk insertion techniques to minimise AIL lock
 * traffic.
 *
 * The AIL tracks log items via the start record LSN of the checkpoint,
 * not the commit record LSN. This is because we can pipeline multiple
 * checkpoints, and so the start record of checkpoint N+1 can be
 * written before the commit record of checkpoint N. i.e:
 *
 *   start N			commit N
 *	+-------------+------------+----------------+
 *		  start N+1			commit N+1
 *
 * The tail of the log cannot be moved to the LSN of commit N when all
 * the items of that checkpoint are written back, because then the
 * start record for N+1 is no longer in the active portion of the log
 * and recovery will fail/corrupt the filesystem.
 *
 * Hence when all the log items in checkpoint N are written back, the
 * tail of the log most now only move as far forwards as the start LSN
 * of checkpoint N+1.
 *
 * If we are called with the aborted flag set, it is because a log write during
 * a CIL checkpoint commit has failed. In this case, all the items in the
 * checkpoint have already gone through iop_committed and iop_committing, which
 * means that checkpoint commit abort handling is treated exactly the same as an
 * iclog write error even though we haven't started any IO yet. Hence in this
 * case all we need to do is iop_committed processing, followed by an
 * iop_unpin(aborted) call.
 *
 * The AIL cursor is used to optimise the insert process. If commit_lsn is not
 * at the end of the AIL, the insert cursor avoids the need to walk the AIL to
 * find the insertion point on every xfs_log_item_batch_insert() call. This
 * saves a lot of needless list walking and is a net win, even though it
 * slightly increases that amount of AIL lock traffic to set it up and tear it
 * down.
 */
static void
xlog_cil_ail_insert(
	struct xfs_cil_ctx	*ctx,
	bool			aborted)
{
#define LOG_ITEM_BATCH_SIZE	32
	struct xfs_ail		*ailp = ctx->cil->xc_log->l_ailp;
	struct xfs_log_item	*log_items[LOG_ITEM_BATCH_SIZE];
	struct xfs_log_vec	*lv;
	struct xfs_ail_cursor	cur;
	xfs_lsn_t		old_head;
	int			i = 0;

	/*
	 * Update the AIL head LSN with the commit record LSN of this
	 * checkpoint. As iclogs are always completed in order, this should
	 * always be the same (as iclogs can contain multiple commit records) or
	 * higher LSN than the current head. We do this before insertion of the
	 * items so that log space checks during insertion will reflect the
	 * space that this checkpoint has already consumed.  We call
	 * xfs_ail_update_finish() so that tail space and space-based wakeups
	 * will be recalculated appropriately.
	 */
	ASSERT(XFS_LSN_CMP(ctx->commit_lsn, ailp->ail_head_lsn) >= 0 ||
			aborted);
	spin_lock(&ailp->ail_lock);
	xfs_trans_ail_cursor_last(ailp, &cur, ctx->start_lsn);
	old_head = ailp->ail_head_lsn;
	ailp->ail_head_lsn = ctx->commit_lsn;
	/* xfs_ail_update_finish() drops the ail_lock */
	xfs_ail_update_finish(ailp, NULLCOMMITLSN);

	/*
	 * We move the AIL head forwards to account for the space used in the
	 * log before we remove that space from the grant heads. This prevents a
	 * transient condition where reservation space appears to become
	 * available on return, only for it to disappear again immediately as
	 * the AIL head update accounts in the log tail space.
	 */
	smp_wmb();	/* paired with smp_rmb in xlog_grant_space_left */
	xlog_grant_return_space(ailp->ail_log, old_head, ailp->ail_head_lsn);

	/* unpin all the log items */
	list_for_each_entry(lv, &ctx->lv_chain, lv_list) {
		struct xfs_log_item	*lip = lv->lv_item;
		xfs_lsn_t		item_lsn;

		if (aborted)
			set_bit(XFS_LI_ABORTED, &lip->li_flags);

		if (lip->li_ops->flags & XFS_ITEM_RELEASE_WHEN_COMMITTED) {
			lip->li_ops->iop_release(lip);
			continue;
		}

		if (lip->li_ops->iop_committed)
			item_lsn = lip->li_ops->iop_committed(lip,
					ctx->start_lsn);
		else
			item_lsn = ctx->start_lsn;

		/* item_lsn of -1 means the item needs no further processing */
		if (XFS_LSN_CMP(item_lsn, (xfs_lsn_t)-1) == 0)
			continue;

		/*
		 * if we are aborting the operation, no point in inserting the
		 * object into the AIL as we are in a shutdown situation.
		 */
		if (aborted) {
			ASSERT(xlog_is_shutdown(ailp->ail_log));
			if (lip->li_ops->iop_unpin)
				lip->li_ops->iop_unpin(lip, 1);
			continue;
		}

		if (item_lsn != ctx->start_lsn) {

			/*
			 * Not a bulk update option due to unusual item_lsn.
			 * Push into AIL immediately, rechecking the lsn once
			 * we have the ail lock. Then unpin the item. This does
			 * not affect the AIL cursor the bulk insert path is
			 * using.
			 */
			spin_lock(&ailp->ail_lock);
			if (XFS_LSN_CMP(item_lsn, lip->li_lsn) > 0)
				xfs_trans_ail_update(ailp, lip, item_lsn);
			else
				spin_unlock(&ailp->ail_lock);
			if (lip->li_ops->iop_unpin)
				lip->li_ops->iop_unpin(lip, 0);
			continue;
		}

		/* Item is a candidate for bulk AIL insert.  */
		log_items[i++] = lv->lv_item;
		if (i >= LOG_ITEM_BATCH_SIZE) {
			xlog_cil_ail_insert_batch(ailp, &cur, log_items,
					LOG_ITEM_BATCH_SIZE, ctx->start_lsn);
			i = 0;
		}
	}

	/* make sure we insert the remainder! */
	if (i)
		xlog_cil_ail_insert_batch(ailp, &cur, log_items, i,
				ctx->start_lsn);

	spin_lock(&ailp->ail_lock);
	xfs_trans_ail_cursor_done(&cur);
	spin_unlock(&ailp->ail_lock);
}

static void
xlog_cil_free_logvec(
	struct list_head	*lv_chain)
{
	struct xfs_log_vec	*lv;

	while (!list_empty(lv_chain)) {
		lv = list_first_entry(lv_chain, struct xfs_log_vec, lv_list);
		list_del_init(&lv->lv_list);
		kvfree(lv);
	}
}

/*
 * Mark all items committed and clear busy extents. We free the log vector
 * chains in a separate pass so that we unpin the log items as quickly as
 * possible.
 */
static void
xlog_cil_committed(
	struct xfs_cil_ctx	*ctx)
{
	struct xfs_mount	*mp = ctx->cil->xc_log->l_mp;
	bool			abort = xlog_is_shutdown(ctx->cil->xc_log);

	/*
	 * If the I/O failed, we're aborting the commit and already shutdown.
	 * Wake any commit waiters before aborting the log items so we don't
	 * block async log pushers on callbacks. Async log pushers explicitly do
	 * not wait on log force completion because they may be holding locks
	 * required to unpin items.
	 */
	if (abort) {
		spin_lock(&ctx->cil->xc_push_lock);
		wake_up_all(&ctx->cil->xc_start_wait);
		wake_up_all(&ctx->cil->xc_commit_wait);
		spin_unlock(&ctx->cil->xc_push_lock);
	}

	xlog_cil_ail_insert(ctx, abort);

	xfs_extent_busy_sort(&ctx->busy_extents.extent_list);
	xfs_extent_busy_clear(mp, &ctx->busy_extents.extent_list,
			      xfs_has_discard(mp) && !abort);

	spin_lock(&ctx->cil->xc_push_lock);
	list_del(&ctx->committing);
	spin_unlock(&ctx->cil->xc_push_lock);

	xlog_cil_free_logvec(&ctx->lv_chain);

	if (!list_empty(&ctx->busy_extents.extent_list)) {
		ctx->busy_extents.mount = mp;
		ctx->busy_extents.owner = ctx;
		xfs_discard_extents(mp, &ctx->busy_extents);
		return;
	}

	kfree(ctx);
}

void
xlog_cil_process_committed(
	struct list_head	*list)
{
	struct xfs_cil_ctx	*ctx;

	while ((ctx = list_first_entry_or_null(list,
			struct xfs_cil_ctx, iclog_entry))) {
		list_del(&ctx->iclog_entry);
		xlog_cil_committed(ctx);
	}
}

/*
* Record the LSN of the iclog we were just granted space to start writing into.
* If the context doesn't have a start_lsn recorded, then this iclog will
* contain the start record for the checkpoint. Otherwise this write contains
* the commit record for the checkpoint.
*/
void
xlog_cil_set_ctx_write_state(
	struct xfs_cil_ctx	*ctx,
	struct xlog_in_core	*iclog)
{
	struct xfs_cil		*cil = ctx->cil;
	xfs_lsn_t		lsn = be64_to_cpu(iclog->ic_header.h_lsn);

	ASSERT(!ctx->commit_lsn);
	if (!ctx->start_lsn) {
		spin_lock(&cil->xc_push_lock);
		/*
		 * The LSN we need to pass to the log items on transaction
		 * commit is the LSN reported by the first log vector write, not
		 * the commit lsn. If we use the commit record lsn then we can
		 * move the grant write head beyond the tail LSN and overwrite
		 * it.
		 */
		ctx->start_lsn = lsn;
		wake_up_all(&cil->xc_start_wait);
		spin_unlock(&cil->xc_push_lock);

		/*
		 * Make sure the metadata we are about to overwrite in the log
		 * has been flushed to stable storage before this iclog is
		 * issued.
		 */
		spin_lock(&cil->xc_log->l_icloglock);
		iclog->ic_flags |= XLOG_ICL_NEED_FLUSH;
		spin_unlock(&cil->xc_log->l_icloglock);
		return;
	}

	/*
	 * Take a reference to the iclog for the context so that we still hold
	 * it when xlog_write is done and has released it. This means the
	 * context controls when the iclog is released for IO.
	 */
	atomic_inc(&iclog->ic_refcnt);

	/*
	 * xlog_state_get_iclog_space() guarantees there is enough space in the
	 * iclog for an entire commit record, so we can attach the context
	 * callbacks now.  This needs to be done before we make the commit_lsn
	 * visible to waiters so that checkpoints with commit records in the
	 * same iclog order their IO completion callbacks in the same order that
	 * the commit records appear in the iclog.
	 */
	spin_lock(&cil->xc_log->l_icloglock);
	list_add_tail(&ctx->iclog_entry, &iclog->ic_callbacks);
	spin_unlock(&cil->xc_log->l_icloglock);

	/*
	 * Now we can record the commit LSN and wake anyone waiting for this
	 * sequence to have the ordered commit record assigned to a physical
	 * location in the log.
	 */
	spin_lock(&cil->xc_push_lock);
	ctx->commit_iclog = iclog;
	ctx->commit_lsn = lsn;
	wake_up_all(&cil->xc_commit_wait);
	spin_unlock(&cil->xc_push_lock);
}


/*
 * Ensure that the order of log writes follows checkpoint sequence order. This
 * relies on the context LSN being zero until the log write has guaranteed the
 * LSN that the log write will start at via xlog_state_get_iclog_space().
 */
enum _record_type {
	_START_RECORD,
	_COMMIT_RECORD,
};

static int
xlog_cil_order_write(
	struct xfs_cil		*cil,
	xfs_csn_t		sequence,
	enum _record_type	record)
{
	struct xfs_cil_ctx	*ctx;

restart:
	spin_lock(&cil->xc_push_lock);
	list_for_each_entry(ctx, &cil->xc_committing, committing) {
		/*
		 * Avoid getting stuck in this loop because we were woken by the
		 * shutdown, but then went back to sleep once already in the
		 * shutdown state.
		 */
		if (xlog_is_shutdown(cil->xc_log)) {
			spin_unlock(&cil->xc_push_lock);
			return -EIO;
		}

		/*
		 * Higher sequences will wait for this one so skip them.
		 * Don't wait for our own sequence, either.
		 */
		if (ctx->sequence >= sequence)
			continue;

		/* Wait until the LSN for the record has been recorded. */
		switch (record) {
		case _START_RECORD:
			if (!ctx->start_lsn) {
				xlog_wait(&cil->xc_start_wait, &cil->xc_push_lock);
				goto restart;
			}
			break;
		case _COMMIT_RECORD:
			if (!ctx->commit_lsn) {
				xlog_wait(&cil->xc_commit_wait, &cil->xc_push_lock);
				goto restart;
			}
			break;
		}
	}
	spin_unlock(&cil->xc_push_lock);
	return 0;
}

/*
 * Write out the log vector change now attached to the CIL context. This will
 * write a start record that needs to be strictly ordered in ascending CIL
 * sequence order so that log recovery will always use in-order start LSNs when
 * replaying checkpoints.
 */
static int
xlog_cil_write_chain(
	struct xfs_cil_ctx	*ctx,
	uint32_t		chain_len)
{
	struct xlog		*log = ctx->cil->xc_log;
	int			error;

	error = xlog_cil_order_write(ctx->cil, ctx->sequence, _START_RECORD);
	if (error)
		return error;
	return xlog_write(log, ctx, &ctx->lv_chain, ctx->ticket, chain_len);
}

/*
 * Write out the commit record of a checkpoint transaction to close off a
 * running log write. These commit records are strictly ordered in ascending CIL
 * sequence order so that log recovery will always replay the checkpoints in the
 * correct order.
 */
static int
xlog_cil_write_commit_record(
	struct xfs_cil_ctx	*ctx)
{
	struct xlog		*log = ctx->cil->xc_log;
	struct xlog_op_header	ophdr = {
		.oh_clientid = XFS_TRANSACTION,
		.oh_tid = cpu_to_be32(ctx->ticket->t_tid),
		.oh_flags = XLOG_COMMIT_TRANS,
	};
	struct xfs_log_iovec	reg = {
		.i_addr = &ophdr,
		.i_len = sizeof(struct xlog_op_header),
		.i_type = XLOG_REG_TYPE_COMMIT,
	};
	struct xfs_log_vec	vec = {
		.lv_niovecs = 1,
		.lv_iovecp = &reg,
	};
	int			error;
	LIST_HEAD(lv_chain);
	list_add(&vec.lv_list, &lv_chain);

	if (xlog_is_shutdown(log))
		return -EIO;

	error = xlog_cil_order_write(ctx->cil, ctx->sequence, _COMMIT_RECORD);
	if (error)
		return error;

	/* account for space used by record data */
	ctx->ticket->t_curr_res -= reg.i_len;
	error = xlog_write(log, ctx, &lv_chain, ctx->ticket, reg.i_len);
	if (error)
		xlog_force_shutdown(log, SHUTDOWN_LOG_IO_ERROR);
	return error;
}

struct xlog_cil_trans_hdr {
	struct xlog_op_header	oph[2];
	struct xfs_trans_header	thdr;
	struct xfs_log_iovec	lhdr[2];
};

/*
 * Build a checkpoint transaction header to begin the journal transaction.  We
 * need to account for the space used by the transaction header here as it is
 * not accounted for in xlog_write().
 *
 * This is the only place we write a transaction header, so we also build the
 * log opheaders that indicate the start of a log transaction and wrap the
 * transaction header. We keep the start record in it's own log vector rather
 * than compacting them into a single region as this ends up making the logic
 * in xlog_write() for handling empty opheaders for start, commit and unmount
 * records much simpler.
 */
static void
xlog_cil_build_trans_hdr(
	struct xfs_cil_ctx	*ctx,
	struct xlog_cil_trans_hdr *hdr,
	struct xfs_log_vec	*lvhdr,
	int			num_iovecs)
{
	struct xlog_ticket	*tic = ctx->ticket;
	__be32			tid = cpu_to_be32(tic->t_tid);

	memset(hdr, 0, sizeof(*hdr));

	/* Log start record */
	hdr->oph[0].oh_tid = tid;
	hdr->oph[0].oh_clientid = XFS_TRANSACTION;
	hdr->oph[0].oh_flags = XLOG_START_TRANS;

	/* log iovec region pointer */
	hdr->lhdr[0].i_addr = &hdr->oph[0];
	hdr->lhdr[0].i_len = sizeof(struct xlog_op_header);
	hdr->lhdr[0].i_type = XLOG_REG_TYPE_LRHEADER;

	/* log opheader */
	hdr->oph[1].oh_tid = tid;
	hdr->oph[1].oh_clientid = XFS_TRANSACTION;
	hdr->oph[1].oh_len = cpu_to_be32(sizeof(struct xfs_trans_header));

	/* transaction header in host byte order format */
	hdr->thdr.th_magic = XFS_TRANS_HEADER_MAGIC;
	hdr->thdr.th_type = XFS_TRANS_CHECKPOINT;
	hdr->thdr.th_tid = tic->t_tid;
	hdr->thdr.th_num_items = num_iovecs;

	/* log iovec region pointer */
	hdr->lhdr[1].i_addr = &hdr->oph[1];
	hdr->lhdr[1].i_len = sizeof(struct xlog_op_header) +
				sizeof(struct xfs_trans_header);
	hdr->lhdr[1].i_type = XLOG_REG_TYPE_TRANSHDR;

	lvhdr->lv_niovecs = 2;
	lvhdr->lv_iovecp = &hdr->lhdr[0];
	lvhdr->lv_bytes = hdr->lhdr[0].i_len + hdr->lhdr[1].i_len;

	tic->t_curr_res -= lvhdr->lv_bytes;
}

/*
 * CIL item reordering compare function. We want to order in ascending ID order,
 * but we want to leave items with the same ID in the order they were added to
 * the list. This is important for operations like reflink where we log 4 order
 * dependent intents in a single transaction when we overwrite an existing
 * shared extent with a new shared extent. i.e. BUI(unmap), CUI(drop),
 * CUI (inc), BUI(remap)...
 */
static int
xlog_cil_order_cmp(
	void			*priv,
	const struct list_head	*a,
	const struct list_head	*b)
{
	struct xfs_log_vec	*l1 = container_of(a, struct xfs_log_vec, lv_list);
	struct xfs_log_vec	*l2 = container_of(b, struct xfs_log_vec, lv_list);

	return l1->lv_order_id > l2->lv_order_id;
}

/*
 * Pull all the log vectors off the items in the CIL, and remove the items from
 * the CIL. We don't need the CIL lock here because it's only needed on the
 * transaction commit side which is currently locked out by the flush lock.
 *
 * If a log item is marked with a whiteout, we do not need to write it to the
 * journal and so we just move them to the whiteout list for the caller to
 * dispose of appropriately.
 */
static void
xlog_cil_build_lv_chain(
	struct xfs_cil_ctx	*ctx,
	struct list_head	*whiteouts,
	uint32_t		*num_iovecs,
	uint32_t		*num_bytes)
{
	while (!list_empty(&ctx->log_items)) {
		struct xfs_log_item	*item;
		struct xfs_log_vec	*lv;

		item = list_first_entry(&ctx->log_items,
					struct xfs_log_item, li_cil);

		if (test_bit(XFS_LI_WHITEOUT, &item->li_flags)) {
			list_move(&item->li_cil, whiteouts);
			trace_xfs_cil_whiteout_skip(item);
			continue;
		}

		lv = item->li_lv;
		lv->lv_order_id = item->li_order_id;

		/* we don't write ordered log vectors */
		if (lv->lv_buf_len != XFS_LOG_VEC_ORDERED)
			*num_bytes += lv->lv_bytes;
		*num_iovecs += lv->lv_niovecs;
		list_add_tail(&lv->lv_list, &ctx->lv_chain);

		list_del_init(&item->li_cil);
		item->li_order_id = 0;
		item->li_lv = NULL;
	}
}

static void
xlog_cil_cleanup_whiteouts(
	struct list_head	*whiteouts)
{
	while (!list_empty(whiteouts)) {
		struct xfs_log_item *item = list_first_entry(whiteouts,
						struct xfs_log_item, li_cil);
		list_del_init(&item->li_cil);
		trace_xfs_cil_whiteout_unpin(item);
		item->li_ops->iop_unpin(item, 1);
	}
}

/*
 * Push the Committed Item List to the log.
 *
 * If the current sequence is the same as xc_push_seq we need to do a flush. If
 * xc_push_seq is less than the current sequence, then it has already been
 * flushed and we don't need to do anything - the caller will wait for it to
 * complete if necessary.
 *
 * xc_push_seq is checked unlocked against the sequence number for a match.
 * Hence we can allow log forces to run racily and not issue pushes for the
 * same sequence twice.  If we get a race between multiple pushes for the same
 * sequence they will block on the first one and then abort, hence avoiding
 * needless pushes.
 *
 * This runs from a workqueue so it does not inherent any specific memory
 * allocation context. However, we do not want to block on memory reclaim
 * recursing back into the filesystem because this push may have been triggered
 * by memory reclaim itself. Hence we really need to run under full GFP_NOFS
 * contraints here.
 */
static void
xlog_cil_push_work(
	struct work_struct	*work)
{
	unsigned int		nofs_flags = memalloc_nofs_save();
	struct xfs_cil_ctx	*ctx =
		container_of(work, struct xfs_cil_ctx, push_work);
	struct xfs_cil		*cil = ctx->cil;
	struct xlog		*log = cil->xc_log;
	struct xfs_cil_ctx	*new_ctx;
	int			num_iovecs = 0;
	int			num_bytes = 0;
	int			error = 0;
	struct xlog_cil_trans_hdr thdr;
	struct xfs_log_vec	lvhdr = {};
	xfs_csn_t		push_seq;
	bool			push_commit_stable;
	LIST_HEAD		(whiteouts);
	struct xlog_ticket	*ticket;

	new_ctx = xlog_cil_ctx_alloc();
	new_ctx->ticket = xlog_cil_ticket_alloc(log);

	down_write(&cil->xc_ctx_lock);

	spin_lock(&cil->xc_push_lock);
	push_seq = cil->xc_push_seq;
	ASSERT(push_seq <= ctx->sequence);
	push_commit_stable = cil->xc_push_commit_stable;
	cil->xc_push_commit_stable = false;

	/*
	 * As we are about to switch to a new, empty CIL context, we no longer
	 * need to throttle tasks on CIL space overruns. Wake any waiters that
	 * the hard push throttle may have caught so they can start committing
	 * to the new context. The ctx->xc_push_lock provides the serialisation
	 * necessary for safely using the lockless waitqueue_active() check in
	 * this context.
	 */
	if (waitqueue_active(&cil->xc_push_wait))
		wake_up_all(&cil->xc_push_wait);

	xlog_cil_push_pcp_aggregate(cil, ctx);

	/*
	 * Check if we've anything to push. If there is nothing, then we don't
	 * move on to a new sequence number and so we have to be able to push
	 * this sequence again later.
	 */
	if (test_bit(XLOG_CIL_EMPTY, &cil->xc_flags)) {
		cil->xc_push_seq = 0;
		spin_unlock(&cil->xc_push_lock);
		goto out_skip;
	}


	/* check for a previously pushed sequence */
	if (push_seq < ctx->sequence) {
		spin_unlock(&cil->xc_push_lock);
		goto out_skip;
	}

	/*
	 * We are now going to push this context, so add it to the committing
	 * list before we do anything else. This ensures that anyone waiting on
	 * this push can easily detect the difference between a "push in
	 * progress" and "CIL is empty, nothing to do".
	 *
	 * IOWs, a wait loop can now check for:
	 *	the current sequence not being found on the committing list;
	 *	an empty CIL; and
	 *	an unchanged sequence number
	 * to detect a push that had nothing to do and therefore does not need
	 * waiting on. If the CIL is not empty, we get put on the committing
	 * list before emptying the CIL and bumping the sequence number. Hence
	 * an empty CIL and an unchanged sequence number means we jumped out
	 * above after doing nothing.
	 *
	 * Hence the waiter will either find the commit sequence on the
	 * committing list or the sequence number will be unchanged and the CIL
	 * still dirty. In that latter case, the push has not yet started, and
	 * so the waiter will have to continue trying to check the CIL
	 * committing list until it is found. In extreme cases of delay, the
	 * sequence may fully commit between the attempts the wait makes to wait
	 * on the commit sequence.
	 */
	list_add(&ctx->committing, &cil->xc_committing);
	spin_unlock(&cil->xc_push_lock);

	xlog_cil_build_lv_chain(ctx, &whiteouts, &num_iovecs, &num_bytes);

	/*
	 * Switch the contexts so we can drop the context lock and move out
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
	 *
	 * xfs_log_force_seq requires us to mirror the new sequence into the cil
	 * structure atomically with the addition of this sequence to the
	 * committing list. This also ensures that we can do unlocked checks
	 * against the current sequence in log forces without risking
	 * deferencing a freed context pointer.
	 */
	spin_lock(&cil->xc_push_lock);
	xlog_cil_ctx_switch(cil, new_ctx);
	spin_unlock(&cil->xc_push_lock);
	up_write(&cil->xc_ctx_lock);

	/*
	 * Sort the log vector chain before we add the transaction headers.
	 * This ensures we always have the transaction headers at the start
	 * of the chain.
	 */
	list_sort(NULL, &ctx->lv_chain, xlog_cil_order_cmp);

	/*
	 * Build a checkpoint transaction header and write it to the log to
	 * begin the transaction. We need to account for the space used by the
	 * transaction header here as it is not accounted for in xlog_write().
	 * Add the lvhdr to the head of the lv chain we pass to xlog_write() so
	 * it gets written into the iclog first.
	 */
	xlog_cil_build_trans_hdr(ctx, &thdr, &lvhdr, num_iovecs);
	num_bytes += lvhdr.lv_bytes;
	list_add(&lvhdr.lv_list, &ctx->lv_chain);

	/*
	 * Take the lvhdr back off the lv_chain immediately after calling
	 * xlog_cil_write_chain() as it should not be passed to log IO
	 * completion.
	 */
	error = xlog_cil_write_chain(ctx, num_bytes);
	list_del(&lvhdr.lv_list);
	if (error)
		goto out_abort_free_ticket;

	error = xlog_cil_write_commit_record(ctx);
	if (error)
		goto out_abort_free_ticket;

	/*
	 * Grab the ticket from the ctx so we can ungrant it after releasing the
	 * commit_iclog. The ctx may be freed by the time we return from
	 * releasing the commit_iclog (i.e. checkpoint has been completed and
	 * callback run) so we can't reference the ctx after the call to
	 * xlog_state_release_iclog().
	 */
	ticket = ctx->ticket;

	/*
	 * If the checkpoint spans multiple iclogs, wait for all previous iclogs
	 * to complete before we submit the commit_iclog. We can't use state
	 * checks for this - ACTIVE can be either a past completed iclog or a
	 * future iclog being filled, while WANT_SYNC through SYNC_DONE can be a
	 * past or future iclog awaiting IO or ordered IO completion to be run.
	 * In the latter case, if it's a future iclog and we wait on it, the we
	 * will hang because it won't get processed through to ic_force_wait
	 * wakeup until this commit_iclog is written to disk.  Hence we use the
	 * iclog header lsn and compare it to the commit lsn to determine if we
	 * need to wait on iclogs or not.
	 */
	spin_lock(&log->l_icloglock);
	if (ctx->start_lsn != ctx->commit_lsn) {
		xfs_lsn_t	plsn;

		plsn = be64_to_cpu(ctx->commit_iclog->ic_prev->ic_header.h_lsn);
		if (plsn && XFS_LSN_CMP(plsn, ctx->commit_lsn) < 0) {
			/*
			 * Waiting on ic_force_wait orders the completion of
			 * iclogs older than ic_prev. Hence we only need to wait
			 * on the most recent older iclog here.
			 */
			xlog_wait_on_iclog(ctx->commit_iclog->ic_prev);
			spin_lock(&log->l_icloglock);
		}

		/*
		 * We need to issue a pre-flush so that the ordering for this
		 * checkpoint is correctly preserved down to stable storage.
		 */
		ctx->commit_iclog->ic_flags |= XLOG_ICL_NEED_FLUSH;
	}

	/*
	 * The commit iclog must be written to stable storage to guarantee
	 * journal IO vs metadata writeback IO is correctly ordered on stable
	 * storage.
	 *
	 * If the push caller needs the commit to be immediately stable and the
	 * commit_iclog is not yet marked as XLOG_STATE_WANT_SYNC to indicate it
	 * will be written when released, switch it's state to WANT_SYNC right
	 * now.
	 */
	ctx->commit_iclog->ic_flags |= XLOG_ICL_NEED_FUA;
	if (push_commit_stable &&
	    ctx->commit_iclog->ic_state == XLOG_STATE_ACTIVE)
		xlog_state_switch_iclogs(log, ctx->commit_iclog, 0);
	ticket = ctx->ticket;
	xlog_state_release_iclog(log, ctx->commit_iclog, ticket);

	/* Not safe to reference ctx now! */

	spin_unlock(&log->l_icloglock);
	xlog_cil_cleanup_whiteouts(&whiteouts);
	xfs_log_ticket_ungrant(log, ticket);
	memalloc_nofs_restore(nofs_flags);
	return;

out_skip:
	up_write(&cil->xc_ctx_lock);
	xfs_log_ticket_put(new_ctx->ticket);
	kfree(new_ctx);
	memalloc_nofs_restore(nofs_flags);
	return;

out_abort_free_ticket:
	ASSERT(xlog_is_shutdown(log));
	xlog_cil_cleanup_whiteouts(&whiteouts);
	if (!ctx->commit_iclog) {
		xfs_log_ticket_ungrant(log, ctx->ticket);
		xlog_cil_committed(ctx);
		memalloc_nofs_restore(nofs_flags);
		return;
	}
	spin_lock(&log->l_icloglock);
	ticket = ctx->ticket;
	xlog_state_release_iclog(log, ctx->commit_iclog, ticket);
	/* Not safe to reference ctx now! */
	spin_unlock(&log->l_icloglock);
	xfs_log_ticket_ungrant(log, ticket);
	memalloc_nofs_restore(nofs_flags);
}

/*
 * We need to push CIL every so often so we don't cache more than we can fit in
 * the log. The limit really is that a checkpoint can't be more than half the
 * log (the current checkpoint is not allowed to overwrite the previous
 * checkpoint), but commit latency and memory usage limit this to a smaller
 * size.
 */
static void
xlog_cil_push_background(
	struct xlog	*log)
{
	struct xfs_cil	*cil = log->l_cilp;
	int		space_used = atomic_read(&cil->xc_ctx->space_used);

	/*
	 * The cil won't be empty because we are called while holding the
	 * context lock so whatever we added to the CIL will still be there.
	 */
	ASSERT(!test_bit(XLOG_CIL_EMPTY, &cil->xc_flags));

	/*
	 * We are done if:
	 * - we haven't used up all the space available yet; or
	 * - we've already queued up a push; and
	 * - we're not over the hard limit; and
	 * - nothing has been over the hard limit.
	 *
	 * If so, we don't need to take the push lock as there's nothing to do.
	 */
	if (space_used < XLOG_CIL_SPACE_LIMIT(log) ||
	    (cil->xc_push_seq == cil->xc_current_sequence &&
	     space_used < XLOG_CIL_BLOCKING_SPACE_LIMIT(log) &&
	     !waitqueue_active(&cil->xc_push_wait))) {
		up_read(&cil->xc_ctx_lock);
		return;
	}

	spin_lock(&cil->xc_push_lock);
	if (cil->xc_push_seq < cil->xc_current_sequence) {
		cil->xc_push_seq = cil->xc_current_sequence;
		queue_work(cil->xc_push_wq, &cil->xc_ctx->push_work);
	}

	/*
	 * Drop the context lock now, we can't hold that if we need to sleep
	 * because we are over the blocking threshold. The push_lock is still
	 * held, so blocking threshold sleep/wakeup is still correctly
	 * serialised here.
	 */
	up_read(&cil->xc_ctx_lock);

	/*
	 * If we are well over the space limit, throttle the work that is being
	 * done until the push work on this context has begun. Enforce the hard
	 * throttle on all transaction commits once it has been activated, even
	 * if the committing transactions have resulted in the space usage
	 * dipping back down under the hard limit.
	 *
	 * The ctx->xc_push_lock provides the serialisation necessary for safely
	 * calling xlog_cil_over_hard_limit() in this context.
	 */
	if (xlog_cil_over_hard_limit(log, space_used)) {
		trace_xfs_log_cil_wait(log, cil->xc_ctx->ticket);
		ASSERT(space_used < log->l_logsize);
		xlog_wait(&cil->xc_push_wait, &cil->xc_push_lock);
		return;
	}

	spin_unlock(&cil->xc_push_lock);

}

/*
 * xlog_cil_push_now() is used to trigger an immediate CIL push to the sequence
 * number that is passed. When it returns, the work will be queued for
 * @push_seq, but it won't be completed.
 *
 * If the caller is performing a synchronous force, we will flush the workqueue
 * to get previously queued work moving to minimise the wait time they will
 * undergo waiting for all outstanding pushes to complete. The caller is
 * expected to do the required waiting for push_seq to complete.
 *
 * If the caller is performing an async push, we need to ensure that the
 * checkpoint is fully flushed out of the iclogs when we finish the push. If we
 * don't do this, then the commit record may remain sitting in memory in an
 * ACTIVE iclog. This then requires another full log force to push to disk,
 * which defeats the purpose of having an async, non-blocking CIL force
 * mechanism. Hence in this case we need to pass a flag to the push work to
 * indicate it needs to flush the commit record itself.
 */
static void
xlog_cil_push_now(
	struct xlog	*log,
	xfs_lsn_t	push_seq,
	bool		async)
{
	struct xfs_cil	*cil = log->l_cilp;

	if (!cil)
		return;

	ASSERT(push_seq && push_seq <= cil->xc_current_sequence);

	/* start on any pending background push to minimise wait time on it */
	if (!async)
		flush_workqueue(cil->xc_push_wq);

	spin_lock(&cil->xc_push_lock);

	/*
	 * If this is an async flush request, we always need to set the
	 * xc_push_commit_stable flag even if something else has already queued
	 * a push. The flush caller is asking for the CIL to be on stable
	 * storage when the next push completes, so regardless of who has queued
	 * the push, the flush requires stable semantics from it.
	 */
	cil->xc_push_commit_stable = async;

	/*
	 * If the CIL is empty or we've already pushed the sequence then
	 * there's no more work that we need to do.
	 */
	if (test_bit(XLOG_CIL_EMPTY, &cil->xc_flags) ||
	    push_seq <= cil->xc_push_seq) {
		spin_unlock(&cil->xc_push_lock);
		return;
	}

	cil->xc_push_seq = push_seq;
	queue_work(cil->xc_push_wq, &cil->xc_ctx->push_work);
	spin_unlock(&cil->xc_push_lock);
}

bool
xlog_cil_empty(
	struct xlog	*log)
{
	struct xfs_cil	*cil = log->l_cilp;
	bool		empty = false;

	spin_lock(&cil->xc_push_lock);
	if (test_bit(XLOG_CIL_EMPTY, &cil->xc_flags))
		empty = true;
	spin_unlock(&cil->xc_push_lock);
	return empty;
}

/*
 * If there are intent done items in this transaction and the related intent was
 * committed in the current (same) CIL checkpoint, we don't need to write either
 * the intent or intent done item to the journal as the change will be
 * journalled atomically within this checkpoint. As we cannot remove items from
 * the CIL here, mark the related intent with a whiteout so that the CIL push
 * can remove it rather than writing it to the journal. Then remove the intent
 * done item from the current transaction and release it so it doesn't get put
 * into the CIL at all.
 */
static uint32_t
xlog_cil_process_intents(
	struct xfs_cil		*cil,
	struct xfs_trans	*tp)
{
	struct xfs_log_item	*lip, *ilip, *next;
	uint32_t		len = 0;

	list_for_each_entry_safe(lip, next, &tp->t_items, li_trans) {
		if (!(lip->li_ops->flags & XFS_ITEM_INTENT_DONE))
			continue;

		ilip = lip->li_ops->iop_intent(lip);
		if (!ilip || !xlog_item_in_current_chkpt(cil, ilip))
			continue;
		set_bit(XFS_LI_WHITEOUT, &ilip->li_flags);
		trace_xfs_cil_whiteout_mark(ilip);
		len += ilip->li_lv->lv_bytes;
		kvfree(ilip->li_lv);
		ilip->li_lv = NULL;

		xfs_trans_del_item(lip);
		lip->li_ops->iop_release(lip);
	}
	return len;
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
 * Called with the context lock already held in read mode to lock out
 * background commit, returns without it held once background commits are
 * allowed again.
 */
void
xlog_cil_commit(
	struct xlog		*log,
	struct xfs_trans	*tp,
	xfs_csn_t		*commit_seq,
	bool			regrant)
{
	struct xfs_cil		*cil = log->l_cilp;
	struct xfs_log_item	*lip, *next;
	uint32_t		released_space = 0;

	/*
	 * Do all necessary memory allocation before we lock the CIL.
	 * This ensures the allocation does not deadlock with a CIL
	 * push in memory reclaim (e.g. from kswapd).
	 */
	xlog_cil_alloc_shadow_bufs(log, tp);

	/* lock out background commit */
	down_read(&cil->xc_ctx_lock);

	if (tp->t_flags & XFS_TRANS_HAS_INTENT_DONE)
		released_space = xlog_cil_process_intents(cil, tp);

	xlog_cil_insert_items(log, tp, released_space);

	if (regrant && !xlog_is_shutdown(log))
		xfs_log_ticket_regrant(log, tp->t_ticket);
	else
		xfs_log_ticket_ungrant(log, tp->t_ticket);
	tp->t_ticket = NULL;
	xfs_trans_unreserve_and_mod_sb(tp);

	/*
	 * Once all the items of the transaction have been copied to the CIL,
	 * the items can be unlocked and possibly freed.
	 *
	 * This needs to be done before we drop the CIL context lock because we
	 * have to update state in the log items and unlock them before they go
	 * to disk. If we don't, then the CIL checkpoint can race with us and
	 * we can run checkpoint completion before we've updated and unlocked
	 * the log items. This affects (at least) processing of stale buffers,
	 * inodes and EFIs.
	 */
	trace_xfs_trans_commit_items(tp, _RET_IP_);
	list_for_each_entry_safe(lip, next, &tp->t_items, li_trans) {
		xfs_trans_del_item(lip);
		if (lip->li_ops->iop_committing)
			lip->li_ops->iop_committing(lip, cil->xc_ctx->sequence);
	}
	if (commit_seq)
		*commit_seq = cil->xc_ctx->sequence;

	/* xlog_cil_push_background() releases cil->xc_ctx_lock */
	xlog_cil_push_background(log);
}

/*
 * Flush the CIL to stable storage but don't wait for it to complete. This
 * requires the CIL push to ensure the commit record for the push hits the disk,
 * but otherwise is no different to a push done from a log force.
 */
void
xlog_cil_flush(
	struct xlog	*log)
{
	xfs_csn_t	seq = log->l_cilp->xc_current_sequence;

	trace_xfs_log_force(log->l_mp, seq, _RET_IP_);
	xlog_cil_push_now(log, seq, true);

	/*
	 * If the CIL is empty, make sure that any previous checkpoint that may
	 * still be in an active iclog is pushed to stable storage.
	 */
	if (test_bit(XLOG_CIL_EMPTY, &log->l_cilp->xc_flags))
		xfs_log_force(log->l_mp, 0);
}

/*
 * Conditionally push the CIL based on the sequence passed in.
 *
 * We only need to push if we haven't already pushed the sequence number given.
 * Hence the only time we will trigger a push here is if the push sequence is
 * the same as the current context.
 *
 * We return the current commit lsn to allow the callers to determine if a
 * iclog flush is necessary following this call.
 */
xfs_lsn_t
xlog_cil_force_seq(
	struct xlog	*log,
	xfs_csn_t	sequence)
{
	struct xfs_cil		*cil = log->l_cilp;
	struct xfs_cil_ctx	*ctx;
	xfs_lsn_t		commit_lsn = NULLCOMMITLSN;

	ASSERT(sequence <= cil->xc_current_sequence);

	if (!sequence)
		sequence = cil->xc_current_sequence;
	trace_xfs_log_force(log->l_mp, sequence, _RET_IP_);

	/*
	 * check to see if we need to force out the current context.
	 * xlog_cil_push() handles racing pushes for the same sequence,
	 * so no need to deal with it here.
	 */
restart:
	xlog_cil_push_now(log, sequence, false);

	/*
	 * See if we can find a previous sequence still committing.
	 * We need to wait for all previous sequence commits to complete
	 * before allowing the force of push_seq to go ahead. Hence block
	 * on commits for those as well.
	 */
	spin_lock(&cil->xc_push_lock);
	list_for_each_entry(ctx, &cil->xc_committing, committing) {
		/*
		 * Avoid getting stuck in this loop because we were woken by the
		 * shutdown, but then went back to sleep once already in the
		 * shutdown state.
		 */
		if (xlog_is_shutdown(log))
			goto out_shutdown;
		if (ctx->sequence > sequence)
			continue;
		if (!ctx->commit_lsn) {
			/*
			 * It is still being pushed! Wait for the push to
			 * complete, then start again from the beginning.
			 */
			XFS_STATS_INC(log->l_mp, xs_log_force_sleep);
			xlog_wait(&cil->xc_commit_wait, &cil->xc_push_lock);
			goto restart;
		}
		if (ctx->sequence != sequence)
			continue;
		/* found it! */
		commit_lsn = ctx->commit_lsn;
	}

	/*
	 * The call to xlog_cil_push_now() executes the push in the background.
	 * Hence by the time we have got here it our sequence may not have been
	 * pushed yet. This is true if the current sequence still matches the
	 * push sequence after the above wait loop and the CIL still contains
	 * dirty objects. This is guaranteed by the push code first adding the
	 * context to the committing list before emptying the CIL.
	 *
	 * Hence if we don't find the context in the committing list and the
	 * current sequence number is unchanged then the CIL contents are
	 * significant.  If the CIL is empty, if means there was nothing to push
	 * and that means there is nothing to wait for. If the CIL is not empty,
	 * it means we haven't yet started the push, because if it had started
	 * we would have found the context on the committing list.
	 */
	if (sequence == cil->xc_current_sequence &&
	    !test_bit(XLOG_CIL_EMPTY, &cil->xc_flags)) {
		spin_unlock(&cil->xc_push_lock);
		goto restart;
	}

	spin_unlock(&cil->xc_push_lock);
	return commit_lsn;

	/*
	 * We detected a shutdown in progress. We need to trigger the log force
	 * to pass through it's iclog state machine error handling, even though
	 * we are already in a shutdown state. Hence we can't return
	 * NULLCOMMITLSN here as that has special meaning to log forces (i.e.
	 * LSN is already stable), so we return a zero LSN instead.
	 */
out_shutdown:
	spin_unlock(&cil->xc_push_lock);
	return 0;
}

/*
 * Perform initial CIL structure initialisation.
 */
int
xlog_cil_init(
	struct xlog		*log)
{
	struct xfs_cil		*cil;
	struct xfs_cil_ctx	*ctx;
	struct xlog_cil_pcp	*cilpcp;
	int			cpu;

	cil = kzalloc(sizeof(*cil), GFP_KERNEL | __GFP_RETRY_MAYFAIL);
	if (!cil)
		return -ENOMEM;
	/*
	 * Limit the CIL pipeline depth to 4 concurrent works to bound the
	 * concurrency the log spinlocks will be exposed to.
	 */
	cil->xc_push_wq = alloc_workqueue("xfs-cil/%s",
			XFS_WQFLAGS(WQ_FREEZABLE | WQ_MEM_RECLAIM | WQ_UNBOUND),
			4, log->l_mp->m_super->s_id);
	if (!cil->xc_push_wq)
		goto out_destroy_cil;

	cil->xc_log = log;
	cil->xc_pcp = alloc_percpu(struct xlog_cil_pcp);
	if (!cil->xc_pcp)
		goto out_destroy_wq;

	for_each_possible_cpu(cpu) {
		cilpcp = per_cpu_ptr(cil->xc_pcp, cpu);
		INIT_LIST_HEAD(&cilpcp->busy_extents);
		INIT_LIST_HEAD(&cilpcp->log_items);
	}

	INIT_LIST_HEAD(&cil->xc_committing);
	spin_lock_init(&cil->xc_push_lock);
	init_waitqueue_head(&cil->xc_push_wait);
	init_rwsem(&cil->xc_ctx_lock);
	init_waitqueue_head(&cil->xc_start_wait);
	init_waitqueue_head(&cil->xc_commit_wait);
	log->l_cilp = cil;

	ctx = xlog_cil_ctx_alloc();
	xlog_cil_ctx_switch(cil, ctx);
	return 0;

out_destroy_wq:
	destroy_workqueue(cil->xc_push_wq);
out_destroy_cil:
	kfree(cil);
	return -ENOMEM;
}

void
xlog_cil_destroy(
	struct xlog	*log)
{
	struct xfs_cil	*cil = log->l_cilp;

	if (cil->xc_ctx) {
		if (cil->xc_ctx->ticket)
			xfs_log_ticket_put(cil->xc_ctx->ticket);
		kfree(cil->xc_ctx);
	}

	ASSERT(test_bit(XLOG_CIL_EMPTY, &cil->xc_flags));
	free_percpu(cil->xc_pcp);
	destroy_workqueue(cil->xc_push_wq);
	kfree(cil);
}

