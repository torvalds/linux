// SPDX-License-Identifier: GPL-2.0-only
/* Network filesystem read subrequest result collection, assessment and
 * retrying.
 *
 * Copyright (C) 2024 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/export.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/task_io_accounting_ops.h>
#include "internal.h"

/*
 * Clear the unread part of an I/O request.
 */
static void netfs_clear_unread(struct netfs_io_subrequest *subreq)
{
	netfs_reset_iter(subreq);
	WARN_ON_ONCE(subreq->len - subreq->transferred != iov_iter_count(&subreq->io_iter));
	iov_iter_zero(iov_iter_count(&subreq->io_iter), &subreq->io_iter);
	if (subreq->start + subreq->transferred >= subreq->rreq->i_size)
		__set_bit(NETFS_SREQ_HIT_EOF, &subreq->flags);
}

/*
 * Flush, mark and unlock a folio that's now completely read.  If we want to
 * cache the folio, we set the group to NETFS_FOLIO_COPY_TO_CACHE, mark it
 * dirty and let writeback handle it.
 */
static void netfs_unlock_read_folio(struct netfs_io_subrequest *subreq,
				    struct netfs_io_request *rreq,
				    struct folio_queue *folioq,
				    int slot)
{
	struct netfs_folio *finfo;
	struct folio *folio = folioq_folio(folioq, slot);

	flush_dcache_folio(folio);
	folio_mark_uptodate(folio);

	if (!test_bit(NETFS_RREQ_USE_PGPRIV2, &rreq->flags)) {
		finfo = netfs_folio_info(folio);
		if (finfo) {
			trace_netfs_folio(folio, netfs_folio_trace_filled_gaps);
			if (finfo->netfs_group)
				folio_change_private(folio, finfo->netfs_group);
			else
				folio_detach_private(folio);
			kfree(finfo);
		}

		if (test_bit(NETFS_SREQ_COPY_TO_CACHE, &subreq->flags)) {
			if (!WARN_ON_ONCE(folio_get_private(folio) != NULL)) {
				trace_netfs_folio(folio, netfs_folio_trace_copy_to_cache);
				folio_attach_private(folio, NETFS_FOLIO_COPY_TO_CACHE);
				folio_mark_dirty(folio);
			}
		} else {
			trace_netfs_folio(folio, netfs_folio_trace_read_done);
		}
	} else {
		// TODO: Use of PG_private_2 is deprecated.
		if (test_bit(NETFS_SREQ_COPY_TO_CACHE, &subreq->flags))
			netfs_pgpriv2_mark_copy_to_cache(subreq, rreq, folioq, slot);
	}

	if (!test_bit(NETFS_RREQ_DONT_UNLOCK_FOLIOS, &rreq->flags)) {
		if (folio->index == rreq->no_unlock_folio &&
		    test_bit(NETFS_RREQ_NO_UNLOCK_FOLIO, &rreq->flags)) {
			_debug("no unlock");
		} else {
			trace_netfs_folio(folio, netfs_folio_trace_read_unlock);
			folio_unlock(folio);
		}
	}

	folioq_clear(folioq, slot);
}

/*
 * Unlock any folios that are now completely read.  Returns true if the
 * subrequest is removed from the list.
 */
static bool netfs_consume_read_data(struct netfs_io_subrequest *subreq, bool was_async)
{
	struct netfs_io_subrequest *prev, *next;
	struct netfs_io_request *rreq = subreq->rreq;
	struct folio_queue *folioq = subreq->curr_folioq;
	size_t avail, prev_donated, next_donated, fsize, part, excess;
	loff_t fpos, start;
	loff_t fend;
	int slot = subreq->curr_folioq_slot;

	if (WARN(subreq->transferred > subreq->len,
		 "Subreq overread: R%x[%x] %zu > %zu",
		 rreq->debug_id, subreq->debug_index,
		 subreq->transferred, subreq->len))
		subreq->transferred = subreq->len;

next_folio:
	fsize = PAGE_SIZE << subreq->curr_folio_order;
	fpos = round_down(subreq->start + subreq->consumed, fsize);
	fend = fpos + fsize;

	if (WARN_ON_ONCE(!folioq) ||
	    WARN_ON_ONCE(!folioq_folio(folioq, slot)) ||
	    WARN_ON_ONCE(folioq_folio(folioq, slot)->index != fpos / PAGE_SIZE)) {
		pr_err("R=%08x[%x] s=%llx-%llx ctl=%zx/%zx/%zx sl=%u\n",
		       rreq->debug_id, subreq->debug_index,
		       subreq->start, subreq->start + subreq->transferred - 1,
		       subreq->consumed, subreq->transferred, subreq->len,
		       slot);
		if (folioq) {
			struct folio *folio = folioq_folio(folioq, slot);

			pr_err("folioq: orders=%02x%02x%02x%02x\n",
			       folioq->orders[0], folioq->orders[1],
			       folioq->orders[2], folioq->orders[3]);
			if (folio)
				pr_err("folio: %llx-%llx ix=%llx o=%u qo=%u\n",
				       fpos, fend - 1, folio_pos(folio), folio_order(folio),
				       folioq_folio_order(folioq, slot));
		}
	}

donation_changed:
	/* Try to consume the current folio if we've hit or passed the end of
	 * it.  There's a possibility that this subreq doesn't start at the
	 * beginning of the folio, in which case we need to donate to/from the
	 * preceding subreq.
	 *
	 * We also need to include any potential donation back from the
	 * following subreq.
	 */
	prev_donated = READ_ONCE(subreq->prev_donated);
	next_donated =  READ_ONCE(subreq->next_donated);
	if (prev_donated || next_donated) {
		spin_lock_bh(&rreq->lock);
		prev_donated = subreq->prev_donated;
		next_donated =  subreq->next_donated;
		subreq->start -= prev_donated;
		subreq->len += prev_donated;
		subreq->transferred += prev_donated;
		prev_donated = subreq->prev_donated = 0;
		if (subreq->transferred == subreq->len) {
			subreq->len += next_donated;
			subreq->transferred += next_donated;
			next_donated = subreq->next_donated = 0;
		}
		trace_netfs_sreq(subreq, netfs_sreq_trace_add_donations);
		spin_unlock_bh(&rreq->lock);
	}

	avail = subreq->transferred;
	if (avail == subreq->len)
		avail += next_donated;
	start = subreq->start;
	if (subreq->consumed == 0) {
		start -= prev_donated;
		avail += prev_donated;
	} else {
		start += subreq->consumed;
		avail -= subreq->consumed;
	}
	part = umin(avail, fsize);

	trace_netfs_progress(subreq, start, avail, part);

	if (start + avail >= fend) {
		if (fpos == start) {
			/* Flush, unlock and mark for caching any folio we've just read. */
			subreq->consumed = fend - subreq->start;
			netfs_unlock_read_folio(subreq, rreq, folioq, slot);
			folioq_mark2(folioq, slot);
			if (subreq->consumed >= subreq->len)
				goto remove_subreq;
		} else if (fpos < start) {
			excess = fend - subreq->start;

			spin_lock_bh(&rreq->lock);
			/* If we complete first on a folio split with the
			 * preceding subreq, donate to that subreq - otherwise
			 * we get the responsibility.
			 */
			if (subreq->prev_donated != prev_donated) {
				spin_unlock_bh(&rreq->lock);
				goto donation_changed;
			}

			if (list_is_first(&subreq->rreq_link, &rreq->subrequests)) {
				spin_unlock_bh(&rreq->lock);
				pr_err("Can't donate prior to front\n");
				goto bad;
			}

			prev = list_prev_entry(subreq, rreq_link);
			WRITE_ONCE(prev->next_donated, prev->next_donated + excess);
			subreq->start += excess;
			subreq->len -= excess;
			subreq->transferred -= excess;
			trace_netfs_donate(rreq, subreq, prev, excess,
					   netfs_trace_donate_tail_to_prev);
			trace_netfs_sreq(subreq, netfs_sreq_trace_donate_to_prev);

			if (subreq->consumed >= subreq->len)
				goto remove_subreq_locked;
			spin_unlock_bh(&rreq->lock);
		} else {
			pr_err("fpos > start\n");
			goto bad;
		}

		/* Advance the rolling buffer to the next folio. */
		slot++;
		if (slot >= folioq_nr_slots(folioq)) {
			slot = 0;
			folioq = folioq->next;
			subreq->curr_folioq = folioq;
		}
		subreq->curr_folioq_slot = slot;
		if (folioq && folioq_folio(folioq, slot))
			subreq->curr_folio_order = folioq->orders[slot];
		if (!was_async)
			cond_resched();
		goto next_folio;
	}

	/* Deal with partial progress. */
	if (subreq->transferred < subreq->len)
		return false;

	/* Donate the remaining downloaded data to one of the neighbouring
	 * subrequests.  Note that we may race with them doing the same thing.
	 */
	spin_lock_bh(&rreq->lock);

	if (subreq->prev_donated != prev_donated ||
	    subreq->next_donated != next_donated) {
		spin_unlock_bh(&rreq->lock);
		cond_resched();
		goto donation_changed;
	}

	/* Deal with the trickiest case: that this subreq is in the middle of a
	 * folio, not touching either edge, but finishes first.  In such a
	 * case, we donate to the previous subreq, if there is one, so that the
	 * donation is only handled when that completes - and remove this
	 * subreq from the list.
	 *
	 * If the previous subreq finished first, we will have acquired their
	 * donation and should be able to unlock folios and/or donate nextwards.
	 */
	if (!subreq->consumed &&
	    !prev_donated &&
	    !list_is_first(&subreq->rreq_link, &rreq->subrequests)) {
		prev = list_prev_entry(subreq, rreq_link);
		WRITE_ONCE(prev->next_donated, prev->next_donated + subreq->len);
		subreq->start += subreq->len;
		subreq->len = 0;
		subreq->transferred = 0;
		trace_netfs_donate(rreq, subreq, prev, subreq->len,
				   netfs_trace_donate_to_prev);
		trace_netfs_sreq(subreq, netfs_sreq_trace_donate_to_prev);
		goto remove_subreq_locked;
	}

	/* If we can't donate down the chain, donate up the chain instead. */
	excess = subreq->len - subreq->consumed + next_donated;

	if (!subreq->consumed)
		excess += prev_donated;

	if (list_is_last(&subreq->rreq_link, &rreq->subrequests)) {
		rreq->prev_donated = excess;
		trace_netfs_donate(rreq, subreq, NULL, excess,
				   netfs_trace_donate_to_deferred_next);
	} else {
		next = list_next_entry(subreq, rreq_link);
		WRITE_ONCE(next->prev_donated, excess);
		trace_netfs_donate(rreq, subreq, next, excess,
				   netfs_trace_donate_to_next);
	}
	trace_netfs_sreq(subreq, netfs_sreq_trace_donate_to_next);
	subreq->len = subreq->consumed;
	subreq->transferred = subreq->consumed;
	goto remove_subreq_locked;

remove_subreq:
	spin_lock_bh(&rreq->lock);
remove_subreq_locked:
	subreq->consumed = subreq->len;
	list_del(&subreq->rreq_link);
	spin_unlock_bh(&rreq->lock);
	netfs_put_subrequest(subreq, false, netfs_sreq_trace_put_consumed);
	return true;

bad:
	/* Errr... prev and next both donated to us, but insufficient to finish
	 * the folio.
	 */
	printk("R=%08x[%x] s=%llx-%llx %zx/%zx/%zx\n",
	       rreq->debug_id, subreq->debug_index,
	       subreq->start, subreq->start + subreq->transferred - 1,
	       subreq->consumed, subreq->transferred, subreq->len);
	printk("folio: %llx-%llx\n", fpos, fend - 1);
	printk("donated: prev=%zx next=%zx\n", prev_donated, next_donated);
	printk("s=%llx av=%zx part=%zx\n", start, avail, part);
	BUG();
}

/*
 * Do page flushing and suchlike after DIO.
 */
static void netfs_rreq_assess_dio(struct netfs_io_request *rreq)
{
	struct netfs_io_subrequest *subreq;
	unsigned int i;

	/* Collect unbuffered reads and direct reads, adding up the transfer
	 * sizes until we find the first short or failed subrequest.
	 */
	list_for_each_entry(subreq, &rreq->subrequests, rreq_link) {
		rreq->transferred += subreq->transferred;

		if (subreq->transferred < subreq->len ||
		    test_bit(NETFS_SREQ_FAILED, &subreq->flags)) {
			rreq->error = subreq->error;
			break;
		}
	}

	if (rreq->origin == NETFS_DIO_READ) {
		for (i = 0; i < rreq->direct_bv_count; i++) {
			flush_dcache_page(rreq->direct_bv[i].bv_page);
			// TODO: cifs marks pages in the destination buffer
			// dirty under some circumstances after a read.  Do we
			// need to do that too?
			set_page_dirty(rreq->direct_bv[i].bv_page);
		}
	}

	if (rreq->iocb) {
		rreq->iocb->ki_pos += rreq->transferred;
		if (rreq->iocb->ki_complete)
			rreq->iocb->ki_complete(
				rreq->iocb, rreq->error ? rreq->error : rreq->transferred);
	}
	if (rreq->netfs_ops->done)
		rreq->netfs_ops->done(rreq);
	if (rreq->origin == NETFS_DIO_READ)
		inode_dio_end(rreq->inode);
}

/*
 * Assess the state of a read request and decide what to do next.
 *
 * Note that we're in normal kernel thread context at this point, possibly
 * running on a workqueue.
 */
static void netfs_rreq_assess(struct netfs_io_request *rreq)
{
	trace_netfs_rreq(rreq, netfs_rreq_trace_assess);

	//netfs_rreq_is_still_valid(rreq);

	if (test_and_clear_bit(NETFS_RREQ_NEED_RETRY, &rreq->flags)) {
		netfs_retry_reads(rreq);
		return;
	}

	if (rreq->origin == NETFS_DIO_READ ||
	    rreq->origin == NETFS_READ_GAPS)
		netfs_rreq_assess_dio(rreq);
	task_io_account_read(rreq->transferred);

	trace_netfs_rreq(rreq, netfs_rreq_trace_wake_ip);
	clear_bit_unlock(NETFS_RREQ_IN_PROGRESS, &rreq->flags);
	wake_up_bit(&rreq->flags, NETFS_RREQ_IN_PROGRESS);

	trace_netfs_rreq(rreq, netfs_rreq_trace_done);
	netfs_clear_subrequests(rreq, false);
	netfs_unlock_abandoned_read_pages(rreq);
	if (unlikely(test_bit(NETFS_RREQ_USE_PGPRIV2, &rreq->flags)))
		netfs_pgpriv2_write_to_the_cache(rreq);
}

void netfs_read_termination_worker(struct work_struct *work)
{
	struct netfs_io_request *rreq =
		container_of(work, struct netfs_io_request, work);
	netfs_see_request(rreq, netfs_rreq_trace_see_work);
	netfs_rreq_assess(rreq);
	netfs_put_request(rreq, false, netfs_rreq_trace_put_work_complete);
}

/*
 * Handle the completion of all outstanding I/O operations on a read request.
 * We inherit a ref from the caller.
 */
void netfs_rreq_terminated(struct netfs_io_request *rreq, bool was_async)
{
	if (!was_async)
		return netfs_rreq_assess(rreq);
	if (!work_pending(&rreq->work)) {
		netfs_get_request(rreq, netfs_rreq_trace_get_work);
		if (!queue_work(system_unbound_wq, &rreq->work))
			netfs_put_request(rreq, was_async, netfs_rreq_trace_put_work_nq);
	}
}

/**
 * netfs_read_subreq_progress - Note progress of a read operation.
 * @subreq: The read request that has terminated.
 * @was_async: True if we're in an asynchronous context.
 *
 * This tells the read side of netfs lib that a contributory I/O operation has
 * made some progress and that it may be possible to unlock some folios.
 *
 * Before calling, the filesystem should update subreq->transferred to track
 * the amount of data copied into the output buffer.
 *
 * If @was_async is true, the caller might be running in softirq or interrupt
 * context and we can't sleep.
 */
void netfs_read_subreq_progress(struct netfs_io_subrequest *subreq,
				bool was_async)
{
	struct netfs_io_request *rreq = subreq->rreq;

	trace_netfs_sreq(subreq, netfs_sreq_trace_progress);

	if (subreq->transferred > subreq->consumed &&
	    (rreq->origin == NETFS_READAHEAD ||
	     rreq->origin == NETFS_READPAGE ||
	     rreq->origin == NETFS_READ_FOR_WRITE)) {
		netfs_consume_read_data(subreq, was_async);
		__clear_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags);
	}
}
EXPORT_SYMBOL(netfs_read_subreq_progress);

/**
 * netfs_read_subreq_terminated - Note the termination of an I/O operation.
 * @subreq: The I/O request that has terminated.
 * @error: Error code indicating type of completion.
 * @was_async: The termination was asynchronous
 *
 * This tells the read helper that a contributory I/O operation has terminated,
 * one way or another, and that it should integrate the results.
 *
 * The caller indicates the outcome of the operation through @error, supplying
 * 0 to indicate a successful or retryable transfer (if NETFS_SREQ_NEED_RETRY
 * is set) or a negative error code.  The helper will look after reissuing I/O
 * operations as appropriate and writing downloaded data to the cache.
 *
 * Before calling, the filesystem should update subreq->transferred to track
 * the amount of data copied into the output buffer.
 *
 * If @was_async is true, the caller might be running in softirq or interrupt
 * context and we can't sleep.
 */
void netfs_read_subreq_terminated(struct netfs_io_subrequest *subreq,
				  int error, bool was_async)
{
	struct netfs_io_request *rreq = subreq->rreq;

	switch (subreq->source) {
	case NETFS_READ_FROM_CACHE:
		netfs_stat(&netfs_n_rh_read_done);
		break;
	case NETFS_DOWNLOAD_FROM_SERVER:
		netfs_stat(&netfs_n_rh_download_done);
		break;
	default:
		break;
	}

	if (rreq->origin != NETFS_DIO_READ) {
		/* Collect buffered reads.
		 *
		 * If the read completed validly short, then we can clear the
		 * tail before going on to unlock the folios.
		 */
		if (error == 0 && subreq->transferred < subreq->len &&
		    (test_bit(NETFS_SREQ_HIT_EOF, &subreq->flags) ||
		     test_bit(NETFS_SREQ_CLEAR_TAIL, &subreq->flags))) {
			netfs_clear_unread(subreq);
			subreq->transferred = subreq->len;
			trace_netfs_sreq(subreq, netfs_sreq_trace_clear);
		}
		if (subreq->transferred > subreq->consumed &&
		    (rreq->origin == NETFS_READAHEAD ||
		     rreq->origin == NETFS_READPAGE ||
		     rreq->origin == NETFS_READ_FOR_WRITE)) {
			netfs_consume_read_data(subreq, was_async);
			__clear_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags);
		}
		rreq->transferred += subreq->transferred;
	}

	/* Deal with retry requests, short reads and errors.  If we retry
	 * but don't make progress, we abandon the attempt.
	 */
	if (!error && subreq->transferred < subreq->len) {
		if (test_bit(NETFS_SREQ_HIT_EOF, &subreq->flags)) {
			trace_netfs_sreq(subreq, netfs_sreq_trace_hit_eof);
		} else {
			trace_netfs_sreq(subreq, netfs_sreq_trace_short);
			if (subreq->transferred > subreq->consumed) {
				__set_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
				__clear_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags);
				set_bit(NETFS_RREQ_NEED_RETRY, &rreq->flags);
			} else if (!__test_and_set_bit(NETFS_SREQ_NO_PROGRESS, &subreq->flags)) {
				__set_bit(NETFS_SREQ_NEED_RETRY, &subreq->flags);
				set_bit(NETFS_RREQ_NEED_RETRY, &rreq->flags);
			} else {
				__set_bit(NETFS_SREQ_FAILED, &subreq->flags);
				error = -ENODATA;
			}
		}
	}

	subreq->error = error;
	trace_netfs_sreq(subreq, netfs_sreq_trace_terminated);

	if (unlikely(error < 0)) {
		trace_netfs_failure(rreq, subreq, error, netfs_fail_read);
		if (subreq->source == NETFS_READ_FROM_CACHE) {
			netfs_stat(&netfs_n_rh_read_failed);
		} else {
			netfs_stat(&netfs_n_rh_download_failed);
			set_bit(NETFS_RREQ_FAILED, &rreq->flags);
			rreq->error = subreq->error;
		}
	}

	if (atomic_dec_and_test(&rreq->nr_outstanding))
		netfs_rreq_terminated(rreq, was_async);

	netfs_put_subrequest(subreq, was_async, netfs_sreq_trace_put_terminated);
}
EXPORT_SYMBOL(netfs_read_subreq_terminated);
